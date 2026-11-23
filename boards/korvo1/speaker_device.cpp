#include "speaker_device.hpp"

#include "thread_config.hpp"

#include <prism/audio/backend.hpp>
#include <prism/audio/render_gate.hpp>

#include "bsp/esp32_s31_korvo_1.h"
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "esp_heap_caps.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <chrono>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace prism::esp32 {

namespace {

constexpr const char* kId = "speaker";
constexpr int kChannels = 2;
constexpr int kFrames = 1024;   // one write: ~23 ms at 44.1 kHz, and what the DMA paces us at
constexpr int kWriterStack = 8192;
constexpr int kMaxVolume = 100;   // esp_codec_dev_set_out_vol takes 0..100
constexpr std::array<int, 2> kRates{44100, 48000};

void log_failure(const char* call, esp_err_t err)
{
    if (err != ESP_OK) std::printf("speaker: %s failed: %s\n", call, esp_err_to_name(err));
}

class Speaker;

// One Output's stream. It owns the pull function and hands it to the writer's gate for as long as
// it lives; the destructor takes it out again, which is what lets Output destroy its render.
class SpeakerStream final : public audio::DeviceStream {
public:
    SpeakerStream(Speaker& speaker, audio::PullFn pull);
    ~SpeakerStream() override;

    [[nodiscard]] bool lost() const override { return false; }   // the codec is on the board
    bool set_volume(float volume) override;                      // hardware volume, see Speaker

private:
    friend class Speaker;

    Speaker& speaker_;
    audio::PullFn pull_;
};

// The device itself: the codec handle and the writer thread, both made on the first open() and kept
// for the firmware's life, so the writer runs from then on and writes silence while no Output is
// attached. simplify: one Output at a time, which is what one Player asks for; two at once would
// share the writer, and the newest attach is the one it plays.
class Speaker final {
public:
    Speaker() = default;
    Speaker(const Speaker&) = delete;
    Speaker& operator=(const Speaker&) = delete;
    ~Speaker();

    [[nodiscard]] std::vector<audio::DeviceInfo> devices() const;
    [[nodiscard]] std::unique_ptr<audio::DeviceStream> open(const audio::Format& format,
                                                            audio::PullFn pull, std::string& error);

    // Both on the thread that creates the Outputs: a stream is made and destroyed there.
    void adopt(SpeakerStream& stream);
    void release(SpeakerStream& stream);

    void set_volume(float volume);

private:
    [[nodiscard]] bool create_codec();
    [[nodiscard]] bool open_codec(int sample_rate);
    void start_writer();
    void run();
    void write(std::vector<std::int16_t>& samples);
    void apply_requests();
    void close_codec();
    [[nodiscard]] bool wait_for_rate(int sample_rate) const;

    std::atomic<bool> quit_{false};
    std::thread writer_;

    // Only the writer touches the codec once it runs: a writer blocked in the DMA would otherwise
    // hold a codec lock nearly all the time, and at its higher priority take it back before the
    // thread asking for a new rate or volume ever got it. Requests travel as atomics instead.
    esp_codec_dev_handle_t device_ = nullptr;   // made by the first open(), before the writer
    std::atomic<int> wanted_rate_{0};   // what the last open() asked for
    std::atomic<int> rate_{0};          // the open rate, 0 while closed
    std::atomic<int> failed_rate_{0};   // a rate the codec refused
    std::atomic<int> volume_{kMaxVolume};
    int applied_volume_ = -1;           // the writer's own

    audio::RenderGate gate_;
    SpeakerStream* current_ = nullptr;
};

SpeakerStream::SpeakerStream(Speaker& speaker, audio::PullFn pull)
    : speaker_{speaker}, pull_{std::move(pull)}
{
    speaker_.adopt(*this);
}

SpeakerStream::~SpeakerStream() { speaker_.release(*this); }

bool SpeakerStream::set_volume(float volume)
{
    speaker_.set_volume(volume);
    return true;
}

Speaker::~Speaker()
{
    quit_ = true;
    if (writer_.joinable()) writer_.join();   // out of the pull and out of the codec before closing
    close_codec();
    device_ = nullptr;
}

std::vector<audio::DeviceInfo> Speaker::devices() const
{
    return {audio::DeviceInfo{.id = kId,
                              .name = "Speaker",
                              .rates = {kRates.begin(), kRates.end()},
                              .max_channels = kChannels,
                              .native = audio::SampleType::s16,
                              .is_default = true}};
}

std::unique_ptr<audio::DeviceStream> Speaker::open(const audio::Format& format, audio::PullFn pull,
                                                   std::string& error)
{
    if (format.channels != kChannels) {
        error = "the speaker plays stereo only";
        return nullptr;
    }
    if (std::ranges::find(kRates, format.sample_rate) == kRates.end()) {
        error = "the speaker cannot play this rate";
        return nullptr;
    }
    if (!create_codec()) {
        error = "the speaker's codec did not start";
        return nullptr;
    }
    wanted_rate_ = format.sample_rate;
    start_writer();
    if (!wait_for_rate(format.sample_rate)) {
        error = "the speaker's codec refused the rate";
        return nullptr;
    }
    return std::make_unique<SpeakerStream>(*this, std::move(pull));
}

void Speaker::adopt(SpeakerStream& stream)
{
    current_ = &stream;
    gate_.attach(&stream.pull_);
}

// Only the stream holding the gate clears it: the previous one's destructor may arrive after the
// next one attached. After detach() returns the writer is out of the pull, so it may die.
void Speaker::release(SpeakerStream& stream)
{
    if (current_ != &stream) return;
    gate_.detach();
    current_ = nullptr;
}

void Speaker::set_volume(float volume)
{
    volume_ = static_cast<int>(std::clamp(volume, 0.f, 1.f) * kMaxVolume);
}

// The codec exists from the first open() on, made on the opening thread before the writer starts:
// a later open() at another rate has the writer reopen the same handle, so the switch between 44.1
// and 48 kHz costs the reopen and nothing else.
bool Speaker::create_codec()
{
    if (device_ != nullptr) return true;
    device_ = bsp_audio_codec_speaker_init();
    if (device_ == nullptr) std::printf("speaker: bsp_audio_codec_speaker_init returned null\n");
    return device_ != nullptr;
}

// On the writer only.
bool Speaker::open_codec(int sample_rate)
{
    if (device_ == nullptr) return false;
    if (rate_.load() != 0) {
        close_codec();
        rate_ = 0;
    }
    esp_codec_dev_sample_info_t info{};
    info.bits_per_sample = 16;
    info.channel = kChannels;
    info.channel_mask = 0;
    info.sample_rate = static_cast<std::uint32_t>(sample_rate);
    info.mclk_multiple = 0;
    if (const auto err = esp_codec_dev_open(device_, &info); err != ESP_OK) {
        log_failure("esp_codec_dev_open", err);
        return false;
    }
    rate_ = sample_rate;
    applied_volume_ = -1;   // a reopened codec takes the volume again
    return true;
}

void Speaker::close_codec()
{
    if (device_ != nullptr) log_failure("esp_codec_dev_close", esp_codec_dev_close(device_));
}

// Internal RAM for the writer's stack: it calls the codec driver, which may touch flash.
void Speaker::start_writer()
{
    if (writer_.joinable()) return;
    writer_ = spawn_with(ThreadConfig{kWriterStack, 0, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
                                      std::nullopt, "speaker"},
                         [this] { return std::thread([this] { run(); }); });
}

// Runs from the first open() to the end: a block per DMA period, silence while no Output is
// attached, so the codec never starves and the next track starts on a running device.
void Speaker::run()
{
    std::vector<std::int16_t> samples(kFrames * kChannels);
    const std::span<std::int16_t> block{samples};
    while (!quit_) {
        apply_requests();
        if (!gate_.call(std::as_writable_bytes(block), kFrames))
            std::ranges::fill(samples, std::int16_t{0});
        if (rate_.load() == 0) {   // the codec refused the last rate: nothing paces us
            std::this_thread::sleep_for(std::chrono::milliseconds{20});
            continue;
        }
        write(samples);
    }
}

// Between two writes: the rate the last open() asked for, and the volume.
void Speaker::apply_requests()
{
    if (const auto wanted = wanted_rate_.load(); wanted != 0 && wanted != rate_.load()
                                                 && wanted != failed_rate_.load()) {
        if (!open_codec(wanted)) failed_rate_ = wanted;
    }
    if (const auto volume = volume_.load(); rate_.load() != 0 && volume != applied_volume_) {
        log_failure("esp_codec_dev_set_out_vol", esp_codec_dev_set_out_vol(device_, volume));
        applied_volume_ = volume;
    }
}

// The opening thread waits for the writer to apply the rate: an Output says playing only once the
// codec runs at its rate. A second at most: the reopen takes a few milliseconds.
bool Speaker::wait_for_rate(int sample_rate) const
{
    for (int waited = 0; waited < 1000; ++waited) {
        if (rate_.load() == sample_rate) return true;
        if (failed_rate_.load() == sample_rate) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return false;
}

// Blocks on the I2S DMA until the codec took the whole buffer: that is the pacing.
void Speaker::write(std::vector<std::int16_t>& samples)
{
    log_failure("esp_codec_dev_write",
                esp_codec_dev_write(device_, samples.data(),
                                    static_cast<int>(samples.size() * sizeof(std::int16_t))));
}

// The backend the registry holds, registered once by register_speaker().
class SpeakerBackend final : public audio::DeviceBackend {
public:
    [[nodiscard]] std::vector<audio::DeviceInfo> devices() override { return speaker_.devices(); }

    [[nodiscard]] std::unique_ptr<audio::DeviceStream> open(std::string_view id,
                                                            const audio::Format& format,
                                                            audio::PullFn pull,
                                                            std::string& error) override
    {
        if (id != kId) {
            error = "no such device";
            return nullptr;
        }
        return speaker_.open(format, std::move(pull), error);
    }

private:
    Speaker speaker_;
};

} // namespace

void register_speaker()
{
    // One backend, registered once, for the firmware's life: the codec opens on the first Output
    // that plays on it.
    static SpeakerBackend backend;
    static const bool registered = (audio::register_backend(backend), true);
    (void)registered;
}

} // namespace prism::esp32
