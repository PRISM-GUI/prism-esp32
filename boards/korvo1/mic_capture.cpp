#include "mic_capture.hpp"

#include "pcm.hpp"

#include <prism/audio/backend.hpp>
#include <prism/audio/input.hpp>

#include "bsp/esp32_s31_korvo_1.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_pthread.h"
#include "esp_timer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace korvo1 {

namespace {

// The BSP's recommendation: MCLK is not wired, so 16 kHz is the rate the codec's clocking supports.
constexpr int kRate = 16000;
constexpr std::size_t kBlockFrames = 256;

// 24 dB of analog gain ahead of the converter (the codec rounds to 24.5 dB). A MEMS microphone
// is quiet: at 0 dB speech would fill only the lowest bits. 24 dB brings speech at arm's length
// to a good part of the scale and a clap still stays below clipping; the scope's own gain slider
// goes on from there.
constexpr float kInputGainDb = 24.f;

constexpr int kReadError = 20;   // ticks to wait before retrying a failed read, so it cannot spin

constexpr std::int64_t kReportEveryUs = 5'000'000;

void report(const char *what, const char *hint)
{
    std::printf("microphones: %s: %s\n", what, hint);
}

// Names the next thread this one creates "mic" and, when destroyed, puts back the config the caller
// had (or the default one), so the threads created later (the frame runtime's) keep their names.
// The stack stays the default. A failure only leaves the thread unnamed: it is reported, not fatal.
class MicThreadName {
public:
    MicThreadName()
    {
        had_previous_ = esp_pthread_get_cfg(&previous_) == ESP_OK;
        esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
        cfg.thread_name = "mic";
        named_ = report_err(esp_pthread_set_cfg(&cfg), "naming the mic thread");
    }
    ~MicThreadName()
    {
        if (named_)
            report_err(esp_pthread_set_cfg(had_previous_ ? &previous_ : &default_), "restoring the pthread config");
    }
    MicThreadName(const MicThreadName &) = delete;
    MicThreadName &operator=(const MicThreadName &) = delete;

private:
    static bool report_err(esp_err_t err, const char *what)
    {
        if (err != ESP_OK)
            std::printf("microphones: %s failed (%s)\n", what, esp_err_to_name(err));
        return err == ESP_OK;
    }

    esp_pthread_cfg_t previous_{};
    esp_pthread_cfg_t default_ = esp_pthread_get_default_config();
    bool had_previous_ = false;
    bool named_ = false;
};

// The open Input's callback, called from the microphone thread: attach() before the codec's first
// block can arrive, detach() on close, which waits for a callback in flight. The seq_cst pair is
// the handshake RenderGate uses (prism/audio/render_gate.hpp): whichever comes second sees the
// other, so a callback never runs after its Input is gone.
class PushGate {
public:
    void attach(prism::audio::PushFn *push) { push_.store(push, std::memory_order_seq_cst); }
    void detach()
    {
        push_.store(nullptr, std::memory_order_seq_cst);
        while (inside_.load(std::memory_order_seq_cst)) std::this_thread::yield();
    }
    void call(std::span<const std::byte> block, int frames)
    {
        prism::audio::PushFn *const push = push_.load(std::memory_order_seq_cst);
        if (push == nullptr) return;
        inside_.store(true, std::memory_order_seq_cst);
        (*push)(block, frames);
        inside_.store(false, std::memory_order_seq_cst);
    }

private:
    std::atomic<prism::audio::PushFn *> push_{nullptr};
    std::atomic<bool> inside_{false};
};

// One open Input: its callback lives here, attached to the capture's gate for its lifetime.
class MicStream final : public prism::audio::DeviceStream {
public:
    MicStream(PushGate &gate, prism::audio::PushFn push) : gate_(gate), push_(std::move(push))
    {
        gate_.attach(&push_);
    }
    ~MicStream() override { gate_.detach(); }
    MicStream(const MicStream &) = delete;
    MicStream &operator=(const MicStream &) = delete;

    bool lost() const override { return false; }

private:
    PushGate &gate_;
    prism::audio::PushFn push_;
};

// The codec, the reading thread and the gate: one instance, for the whole run.
class MicCapture final : public prism::audio::CaptureBackend {
public:
    MicCapture() = default;
    ~MicCapture() override;
    MicCapture(const MicCapture &) = delete;
    MicCapture &operator=(const MicCapture &) = delete;

    // The codec opened at 16 kHz, 16 bits, two channels, and the thread reading it.
    bool start();

    std::vector<prism::audio::DeviceInfo> devices() override
    {
        return {prism::audio::DeviceInfo{.id = "mic",
                                         .name = "Microphones",
                                         .rates = {kRate},
                                         .max_channels = 1,
                                         .native = prism::audio::SampleType::f32,
                                         .is_default = true}};
    }
    std::unique_ptr<prism::audio::DeviceStream> open(std::string_view device,
                                                     const prism::audio::Format &format,
                                                     prism::audio::PushFn push) override;

private:
    void run(std::stop_token stop);
    void report_read_failure(int error);   // failure path only: the first one, then one line per 5 s

    esp_codec_dev_handle_t codec_ = nullptr;
    PushGate gate_;
    std::array<std::int16_t, 2 * kBlockFrames> block_{};   // allocated once: the thread never allocates
    std::array<float, kBlockFrames> mono_{};
    std::uint32_t failures_ = 0;   // mic thread only
    std::int64_t last_report_us_ = 0;
    std::jthread thread_;
};

bool MicCapture::start()
{
    const esp_codec_dev_handle_t codec = bsp_audio_codec_microphone_init();
    if (!codec) {
        report("the audio codec did not start",
               "check that the board is a Korvo-1 and its I2C bus is not shorted");
        return false;
    }
    esp_codec_dev_sample_info_t format{.bits_per_sample = 16,
                                       .channel = 2,
                                       .channel_mask = 0,
                                       .sample_rate = std::uint32_t(kRate),
                                       .mclk_multiple = 0};
    if (esp_codec_dev_open(codec, &format) != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(codec);
        report("the codec refused 16000 Hz", "the rate the board's clocking supports without MCLK");
        return false;
    }
    if (esp_codec_dev_set_in_gain(codec, kInputGainDb) != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(codec);
        report("setting the input gain failed", "power-cycle the board");
        return false;
    }
    codec_ = codec;
    const MicThreadName name;
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
    return true;
}

MicCapture::~MicCapture()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    if (codec_) esp_codec_dev_close(codec_);
}

std::unique_ptr<prism::audio::DeviceStream> MicCapture::open(std::string_view device,
                                                             const prism::audio::Format &format,
                                                             prism::audio::PushFn push)
{
    // PRISM opens a device it listed, with a format the listing accepts; an empty id is "the
    // default". This codec does its own rate, and one channel: the two capsules are one trace.
    if ((!device.empty() && device != "mic") || format.sample_rate != kRate || format.channels != 1) {
        std::printf("microphones: no stream for %.*s at %d Hz, %d channel(s)\n",
                    static_cast<int>(device.size()), device.data(), format.sample_rate,
                    format.channels);
        return nullptr;
    }
    return std::make_unique<MicStream>(gate_, std::move(push));
}

void MicCapture::report_read_failure(int error)
{
    ++failures_;
    const std::int64_t now = esp_timer_get_time();
    if (failures_ == 1)
        std::printf("microphones: reading the codec failed (esp_codec_dev error %d): check the codec's "
                    "wiring and the I2S pins, then restart the board\n", error);
    else if (now - last_report_us_ >= kReportEveryUs)
        std::printf("microphones: %u reads failed so far (last error %d)\n", unsigned(failures_), error);
    else
        return;
    last_report_us_ = now;
}

void MicCapture::run(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        if (const int error = esp_codec_dev_read(codec_, block_.data(), int(sizeof block_));
            error != ESP_CODEC_DEV_OK) {
            report_read_failure(error);
            vTaskDelay(kReadError);
            continue;
        }
        stereo_i16_to_mono(block_, mono_);
        gate_.call(std::as_bytes(std::span<const float>{mono_}), int(mono_.size()));
    }
}

} // namespace

bool register_mic_capture()
{
    static const bool registered = [] {
        static MicCapture capture;
        if (!capture.start()) return false;
        prism::audio::register_capture_backend(capture);
        return true;
    }();
    return registered;
}

} // namespace korvo1
