#pragma once

// The Korvo-1's two microphones (ES8389 codec, through the BSP's esp_codec_dev) as the scope's
// SampleSource. A thread of its own reads blocks of stereo frames from the codec and pushes their
// mono mean into a lock-free ring, which read() drains without blocking, as the STM32's ADC does.

#include <embedded/sample_source.hpp>

#include <prism/dev/spsc_ring.hpp>

#include "esp_codec_dev.h"

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <thread>

namespace korvo1 {

class MicSource final : public prism_examples::SampleSource {
public:
    static constexpr float kDefaultRate = 16000.f;   // the BSP's recommendation: MCLK is not wired
    static constexpr std::size_t kBlockFrames = 256;

    // The BSP's codec, opened at `rate`, 16 bits, two channels. The error says what failed and
    // what to try.
    static std::expected<std::unique_ptr<MicSource>, std::string> open(float rate = kDefaultRate);

    ~MicSource() override;
    MicSource(const MicSource &) = delete;
    MicSource &operator=(const MicSource &) = delete;

    std::size_t read(std::span<float> out) override { return ring_.pop(out); }
    float sample_rate() const override { return rate_; }
    const char *name() const override { return "mic"; }

private:
    MicSource(esp_codec_dev_handle_t codec, float rate) : codec_(codec), rate_(rate) {}
    void run(std::stop_token stop);

    esp_codec_dev_handle_t codec_;
    float rate_;
    prism::dev::SpscRing<float, 4096> ring_;   // 256 ms at 16 kHz
    std::array<std::int16_t, 2 * kBlockFrames> block_{};   // allocated once: the thread never allocates
    std::array<float, kBlockFrames> mono_{};
    std::jthread thread_;
};

} // namespace korvo1
