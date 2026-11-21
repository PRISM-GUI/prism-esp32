#include "mic_source.hpp"

#include "pcm.hpp"

#include "bsp/esp32_s31_korvo_1.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>
#include <utility>

namespace korvo1 {

namespace {

// 24 dB of analog gain ahead of the converter (the codec rounds to 24.5 dB). A MEMS microphone
// is quiet: at 0 dB speech would fill only the lowest bits. 24 dB brings speech at arm's length
// to a good part of the scale and a clap still stays below clipping; the scope's own gain slider
// goes on from there.
constexpr float kInputGainDb = 24.f;

constexpr int kReadError = 20;   // ticks to wait before retrying a failed read, so it cannot spin

std::string failure(const char *what, const char *hint)
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
                                       "use 16000 Hz, the rate the board's clocking supports"));
    if (esp_codec_dev_set_in_gain(codec, kInputGainDb) != ESP_CODEC_DEV_OK) {
        esp_codec_dev_close(codec);
        return std::unexpected(failure("setting the input gain failed", "power-cycle the board"));
    }
    std::unique_ptr<MicSource> source{new MicSource(codec, rate)};
    source->thread_ = std::jthread([raw = source.get()](std::stop_token stop) { raw->run(stop); });
    return source;
}

MicSource::~MicSource()
{
    thread_.request_stop();
    thread_.join();
    esp_codec_dev_close(codec_);
}

void MicSource::run(std::stop_token stop)
{
    while (!stop.stop_requested()) {
        if (esp_codec_dev_read(codec_, block_.data(), int(sizeof block_)) != ESP_CODEC_DEV_OK) {
            vTaskDelay(kReadError);
            continue;
        }
        stereo_i16_to_mono(block_, mono_);
        ring_.push(mono_);
    }
}

} // namespace korvo1
