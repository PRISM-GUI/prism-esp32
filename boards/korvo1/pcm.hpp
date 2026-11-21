#pragma once

// The microphones' samples as the scope wants them: interleaved stereo int16 into mono floats.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>

namespace korvo1 {

// (l + r) / 65536 per frame: the mean of the two channels, in [-1, 1). Converts as many whole
// frames as both spans hold.
inline void stereo_i16_to_mono(std::span<const std::int16_t> interleaved, std::span<float> out)
{
    const std::size_t frames = std::min(interleaved.size() / 2, out.size());
    for (std::size_t i = 0; i < frames; ++i)
        out[i] = (float(interleaved[2 * i]) + float(interleaved[2 * i + 1])) / 65536.f;
}

} // namespace korvo1
