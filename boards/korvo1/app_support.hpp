#pragma once

// What the panel apps (main/widgets.cpp, main/scope.cpp) share: stop with a message when there
// is no screen, print the timing line, and open the touch (or run without input).

#include "touch.hpp"

#include <string>
#include <string_view>

namespace korvo1 {

// No screen to run on: say why, then stay here so the line stays readable on the console.
[[noreturn]] void halt(const std::string &error);

void print_stats(std::string_view line);

// The touch controller, or a Touch that never sees a finger (the reason is printed).
Touch open_touch_or_none();

} // namespace korvo1
