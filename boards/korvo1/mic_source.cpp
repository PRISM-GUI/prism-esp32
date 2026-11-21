#include "mic_source.hpp"

#include "pcm.hpp"

#include "bsp/esp32_s31_korvo_1.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_pthread.h"
#include "esp_timer.h"

#include <cmath>
#include <cstdio>
#include <utility>

namespace korvo1 {

namespace {

// 24 dB of analog gain ahead of the converter (the codec rounds to 24.5 dB). A MEMS microphone
// is quiet: at 0 dB speech would fill only the lowest bits. 24 dB brings speech at arm's length
// to a good part of the scale and a clap still stays below clipping; the scope's own gain slider
// goes on from there.
constexpr float kInputGainDb = 24.f;

constexpr int kReadError = 20;   // ticks to wait before retrying a failed read, so it cannot spin

constexpr std::int64_t kReportEveryUs = 5'000'000;

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
        named_ = report(esp_pthread_set_cfg(&cfg), "naming the mic thread");
    }
    ~MicThreadName()
    {
        if (named_)
            report(esp_pthread_set_cfg(had_previous_ ? &previous_ : &default_), "restoring the pthread config");
    }
    MicThreadName(const MicThreadName &) = delete;
    MicThreadName &operator=(const MicThreadName &) = delete;

private:
    static bool report(esp_err_t err, const char *what)
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

std::string failure(const char *what, const std::string &hint)
{
    return std::string("microphones: ") + what + ": " + hint;
}

} // namespace

std::expected<std::unique_ptr<MicSource>, std::string> MicSource::open(float rate)
{
    const esp_codec_dev_handle_t codec = bsp_audio_codec_microphone_init();
    if (!codec)
        return std::unexpected(failure("the audio codec did not start",
                                       "check that the board is a Korvo-1 and its I2C bus is not shorted"));
    esp_codec_dev_sample_info_t format{.bits_per_sample = 16,
                                       .channel = 2,
                                       .channel_mask = 0,
                                       .sample_rate = std::uint32_t(std::lround(rate)),
                                       .mclk_multiple = 0};
    if (esp_codec_dev_open(codec, &format) != ESP_CODEC_DEV_OK)
        return std::unexpected(failure("the codec refused the format",
                                       "try 16000 Hz, the rate the board's clocking supports (asked for " +
                                           std::to_string(format.sample_rate) + " Hz)"));
    if (esp_codec_dev_set_in_gain(codec, kInputGainDb) != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(codec);
        return std::unexpected(failure("setting the input gain failed", "power-cycle the board"));
    }
    std::unique_ptr<MicSource> source{new MicSource(codec, rate)};   // owns the codec from here: its destructor closes it
    const MicThreadName name;
    source->thread_ = std::jthread([raw = source.get()](std::stop_token stop) { raw->run(stop); });
    return source;
}

MicSource::~MicSource()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
    esp_codec_dev_close(codec_);
}

void MicSource::report_read_failure(int error)
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

void MicSource::run(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        if (const int error = esp_codec_dev_read(codec_, block_.data(), int(sizeof block_));
            error != ESP_CODEC_DEV_OK) {
            report_read_failure(error);
            vTaskDelay(kReadError);
            continue;
        }
        stereo_i16_to_mono(block_, mono_);
        ring_.push(mono_);
    }
}

} // namespace korvo1
