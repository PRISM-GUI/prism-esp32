#pragma once

// What this board's startup and its own test share (boards/korvo1/selftest.cpp): stop with a
// message when there is no screen, print the timing line, and open the touch (or run without input).

#include "touch.hpp"

#include <prism/platform/frame_app.hpp>

#include <string>
#include <string_view>

namespace korvo1 {

// No screen to run on: say why, then stay here so the line stays readable on the console.
[[noreturn]] void halt(const std::string &error);

// frame_app returned: on this board only when a runtime thread could not start. Says so, with a
// hint, instead of leaving a black screen, then stays here as halt does.
[[noreturn]] void halt_runtime_stopped();

void print_stats(std::string_view line);

// "heap <where>: internal free N, largest block M": a thread stack needs one block that big.
void print_internal_heap(const char *where);

// The touch controller, or a Touch that never sees a finger (the reason is printed).
Touch open_touch_or_none();

// The frame runtime's options for this board: the UI scale (CONFIG_PRISM_UI_SCALE_PERCENT), and
// logic on core 0, render on core 1. Core 1 is the SIMD core, per Espressif's forum: unconfirmed,
// boards/korvo1/selftest.cpp checks it.
prism::platform::FrameOptions pinned_threads();

// Holds a block of internal RAM for the render thread's stack, from now until pinned_threads()
// starts that thread. For an app whose drivers fragment internal RAM before the frame runtime
// starts (Wi-Fi): called first thing in app_main, the block is there whatever comes after.
void reserve_render_stack();

} // namespace korvo1
