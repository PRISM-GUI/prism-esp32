#pragma once

// The board's startup and teardown around an app that is PRISM's own main() (main/app_main.c): the
// panel, the touch and how the runtime runs on them are the board's, and the app knows nothing
// about them (platform/port.hpp). C linkage, so a C wrapper can call them.

#ifdef __cplusplus
extern "C" {
#endif

// Reserves the render thread's stack, opens the panel (halting on failure) and the touch, then
// registers this board as PRISM's port. Called before the app's main(), as platform::run requires.
void korvo1_start(void);

// main() returned. On this board that only happens when a frame-runtime thread could not start:
// says so, with a hint, and stays here as the panel apps always did.
void korvo1_stopped(void);

#ifdef __cplusplus
}
#endif
