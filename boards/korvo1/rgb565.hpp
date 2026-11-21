#pragma once

// PRISM's ARGB8888 pixels (0xAARRGGBB) as the panel's RGB565: the top bits of each channel, the
// alpha dropped. The display's CPU path (CONFIG_PRISM_DISPLAY_CPU_CONVERT); the PPA does the same
// in hardware otherwise.

#include <algorithm>
#include <cstdint>
#include <span>

namespace korvo1 {

constexpr std::uint16_t to_rgb565(std::uint32_t argb)
{
    return static_cast<std::uint16_t>(((argb >> 8) & 0xF800) | ((argb >> 5) & 0x07E0) |
                                      ((argb >> 3) & 0x001F));
}

// Converts as many pixels as both spans hold.
inline void argb8888_to_rgb565(std::span<const std::uint32_t> in, std::span<std::uint16_t> out)
{
    std::ranges::transform(in.first(std::min(in.size(), out.size())), out.begin(), to_rgb565);
}

} // namespace korvo1
