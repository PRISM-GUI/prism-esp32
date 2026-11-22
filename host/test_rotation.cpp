// The display rotation: frame pixels to panel pixels and back, and where a band lands.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest.h>

#include "boards/korvo1/rotation.hpp"

using korvo1::band_on_panel;
using korvo1::Pixel;
using korvo1::Rotation;
using korvo1::to_frame;
using korvo1::to_panel;

TEST_CASE("turned clockwise, the frame's top-left is the panel's bottom-left")
{
    CHECK(to_panel({0, 0}, Rotation::clockwise) == Pixel{0, 479});
    CHECK(to_panel({479, 0}, Rotation::clockwise) == Pixel{0, 0});
    CHECK(to_panel({0, 799}, Rotation::clockwise) == Pixel{799, 479});
}

TEST_CASE("turned counter-clockwise, the frame's top-left is the panel's top-right")
{
    CHECK(to_panel({0, 0}, Rotation::counter_clockwise) == Pixel{799, 0});
    CHECK(to_panel({479, 799}, Rotation::counter_clockwise) == Pixel{0, 479});
}

TEST_CASE("to_frame undoes to_panel for every rotation")
{
    for (const Rotation r : {Rotation::none, Rotation::clockwise, Rotation::counter_clockwise})
        for (const Pixel p : {Pixel{0, 0}, Pixel{12, 345}, Pixel{korvo1::frame_width(r) - 1,
                                                                korvo1::frame_height(r) - 1}})
            CHECK(to_frame(to_panel(p, r), r) == p);
}

TEST_CASE("a band of rows lands in a block of the panel")
{
    const auto flat = band_on_panel(48, 48, Rotation::none);
    CHECK((flat.x == 0 && flat.y == 48 && flat.width == 800 && flat.height == 48));
    const auto cw = band_on_panel(16, 16, Rotation::clockwise);
    CHECK((cw.x == 16 && cw.y == 0 && cw.width == 16 && cw.height == 480));
    const auto ccw = band_on_panel(16, 16, Rotation::counter_clockwise);
    CHECK((ccw.x == 800 - 32 && ccw.y == 0 && ccw.width == 16 && ccw.height == 480));
}
