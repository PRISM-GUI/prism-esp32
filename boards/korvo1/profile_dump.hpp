#pragma once

// The profile recorder dumped over the console UART when a button goes down (the STM32 port's
// dump_on_button). Without PRISM_PROFILING it is a hook that does nothing.

#include <functional>

namespace korvo1 {

// A hook for FrameContext::on_loop: it runs on the logic thread once per loop turn, and dumps on
// the press of the board's PLAY button (the BSP calls it MODE). Setup errors are printed, and
// the hook then does nothing.
std::function<void()> dump_profile_on_play();

} // namespace korvo1
