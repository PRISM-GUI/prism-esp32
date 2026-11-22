#pragma once

// What the panel apps (main/widgets.cpp, main/scope.cpp) share: stop with a message when there
// is no screen, print the timing line, and open the touch (or run without input).

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

// Logic on core 0, render on core 1. Core 1 is the SIMD core, per Espressif's forum: unconfirmed,
// main/selftest.cpp checks it.
prism::platform::FrameOptions pinned_threads();

} // namespace korvo1
