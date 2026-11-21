// The profile clock (prism/profile/recorder.hpp): monotonic per thread, at most 1 us resolution.
//
// Not the CPU cycle counter: there is one per core and the two are not synchronised, so a task
// that migrates would see time jump back or forward. esp_timer is one 64-bit counter (the
// systimer) that both cores read, at 1 MHz.

#include <prism/profile/recorder.hpp>

#include "esp_timer.h"

std::uint64_t prism::platform::profile_ticks()
{
    return static_cast<std::uint64_t>(esp_timer_get_time());
}

std::uint64_t prism::platform::profile_ticks_per_second() { return 1'000'000; }
