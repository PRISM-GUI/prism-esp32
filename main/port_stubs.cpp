// The port hooks PRISM's runtime calls and leaves to the board (prism/platform/alloc_count.hpp,
// frame_stats.hpp, prism/profile/recorder.hpp). Placeholders until the board's real ones.

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>
#include <prism/profile/recorder.hpp>

#include "esp_timer.h"

// simplify: no allocation count, the timing line shows 0 allocs/frame; count malloc through
// ESP-IDF's heap hooks to replace it (Task 5).
std::uint32_t prism::platform::allocation_count() { return 0; }

// simplify: no CPU load or heap figures on the timing line; read FreeRTOS's run-time stats and
// heap_caps to replace it (Task 5).
std::optional<prism::platform::SystemCounters> prism::platform::system_counters()
{
    return std::nullopt;
}

// simplify: a 1 µs clock (esp_timer) instead of the CPU cycle counter; Task 5 uses
// esp_cpu_get_cycle_count() extended to 64 bits.
std::uint64_t prism::platform::profile_ticks()
{
    return static_cast<std::uint64_t>(esp_timer_get_time());
}

std::uint64_t prism::platform::profile_ticks_per_second() { return 1'000'000; }

// simplify: the allocation guard is never armed and sees nothing; Task 5 wraps malloc.
extern "C" void prism_allocation_guard_arm(void) {}
extern "C" void prism_allocation_guard_disarm(void) {}
extern "C" std::uint32_t prism_allocation_violations(void) { return 0; }
extern "C" void *prism_allocation_first_violation(void) { return nullptr; }
