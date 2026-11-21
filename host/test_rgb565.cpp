// The CPU fallback of the display's conversion: PRISM's ARGB8888 pixels into the panel's RGB565.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "boards/korvo1/rgb565.hpp"

#include <array>
#include <cstdint>

namespace {

std::uint16_t convert_one(std::uint32_t argb)
{
    std::array<std::uint32_t, 1> in{argb};
    std::array<std::uint16_t, 1> out{};
    korvo1::argb8888_to_rgb565(in, out);
    return out[0];
}

} // namespace

TEST_CASE("primaries and white map to their RGB565 values")
{
    CHECK(convert_one(0xFFFF0000) == 0xF800);
    CHECK(convert_one(0xFF00FF00) == 0x07E0);
    CHECK(convert_one(0xFF0000FF) == 0x001F);
    CHECK(convert_one(0xFFFFFFFF) == 0xFFFF);
}

TEST_CASE("the alpha channel is ignored")
{
    CHECK(convert_one(0x00FF0000) == 0xF800);
    CHECK(convert_one(0x12345678) == convert_one(0xFF345678));
}

TEST_CASE("a 3-pixel span converts all 3")
{
    std::array<std::uint32_t, 3> in{0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    std::array<std::uint16_t, 3> out{};
    korvo1::argb8888_to_rgb565(in, out);
    CHECK(out == std::array<std::uint16_t, 3>{0xF800, 0x07E0, 0x001F});
}
