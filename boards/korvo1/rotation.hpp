#pragma once

// How the board is held: the frame PRISM draws and the touch points it reads are in a logical
// frame; the display and the touch controller work in the panel's own 800x480. Turned a quarter
// clockwise, the panel's left edge is on top and its bottom edge on the left; turned
// counter-clockwise, its right edge is on top and its top edge on the left.
// The board's choice is display_rotation.hpp (Kconfig); this header is pure, for the host tests.

namespace korvo1 {

enum class Rotation { none, clockwise, counter_clockwise };

inline constexpr int kPanelWidth = 800;
inline constexpr int kPanelHeight = 480;

struct Pixel {
    int x;
    int y;
    friend constexpr bool operator==(Pixel, Pixel) = default;
};

constexpr bool portrait(Rotation r) { return r != Rotation::none; }
constexpr int frame_width(Rotation r) { return portrait(r) ? kPanelHeight : kPanelWidth; }
constexpr int frame_height(Rotation r) { return portrait(r) ? kPanelWidth : kPanelHeight; }

// The panel pixel that shows the frame's pixel `p`.
constexpr Pixel to_panel(Pixel p, Rotation r)
{
    switch (r) {
    case Rotation::clockwise: return {p.y, kPanelHeight - 1 - p.x};
    case Rotation::counter_clockwise: return {kPanelWidth - 1 - p.y, p.x};
    case Rotation::none: break;
    }
    return p;
}

// The frame pixel shown at the panel's pixel `p`: to_panel's inverse.
constexpr Pixel to_frame(Pixel p, Rotation r)
{
    switch (r) {
    case Rotation::clockwise: return {kPanelHeight - 1 - p.y, p.x};
    case Rotation::counter_clockwise: return {p.y, kPanelWidth - 1 - p.x};
    case Rotation::none: break;
    }
    return p;
}

// The panel rectangle a band of frame rows [first_row, first_row + rows) lands in.
struct PanelBlock {
    int x;
    int y;
    int width;
    int height;
};

constexpr PanelBlock band_on_panel(int first_row, int rows, Rotation r)
{
    const Pixel a = to_panel({0, first_row}, r);
    const Pixel b = to_panel({frame_width(r) - 1, first_row + rows - 1}, r);
    const int x = a.x < b.x ? a.x : b.x;
    const int y = a.y < b.y ? a.y : b.y;
    return {x, y, (a.x < b.x ? b.x : a.x) - x + 1, (a.y < b.y ? b.y : a.y) - y + 1};
}

} // namespace korvo1
