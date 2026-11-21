// The port hooks of the ESP32-S31-Korvo-1 (prism/platform/alloc_count.hpp, frame_stats.hpp):
// the allocation count and guard, fed by the IDF heap hook, and the figures on
// the timing line, from FreeRTOS's run-time stats and the IDF heap.

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdint>

namespace {

// All in DRAM: the heap calls the hook from IRAM, with the flash cache possibly off, so the hook
// and what it touches avoid flash code and flash data. A critical section, not std::atomic:
// without the RISC-V A extension the atomics are library calls IDF does not put in IRAM.
portMUX_TYPE counter_lock = portMUX_INITIALIZER_UNLOCKED;
std::uint32_t allocations = 0;
bool guard_armed = false;
std::uint32_t violations = 0;
void *first_violation = nullptr;

struct Heap {
    std::size_t used, total;
};

// O(1) per heap, unlike heap_caps_get_info, which walks every block with interrupts masked.
Heap heap_of(std::uint32_t caps)
{
    const std::size_t total = heap_caps_get_total_size(caps);
    return {total - heap_caps_get_free_size(caps), total};
}

// The mean, not the sum: the CPU line reads idle time as the share of one second one core was idle.
std::uint64_t idle_us_of_average_core()
{
    std::uint64_t sum = 0;
    for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core)
        sum += ulTaskGetIdleRunTimeCounterForCore(core);
    return sum / configNUMBER_OF_CORES;
}

} // namespace

// ESP-IDF calls this after every successful heap_caps allocation (CONFIG_HEAP_USE_HOOKS): malloc,
// calloc, aligned operator new, a driver's heap_caps_malloc. A realloc that resizes in place is
// counted too: the hook cannot tell it from a new block. The recorded address is the hook's
// return, inside the heap code: break on the hook (or trap there) to see the whole call stack.
// (esp_heap_caps.h declares it IRAM, so the definition is placed there too.)
extern "C" void esp_heap_trace_alloc_hook(void *, size_t, uint32_t)
{
    void *const caller = __builtin_return_address(0);
    portENTER_CRITICAL_SAFE(&counter_lock);
    ++allocations;
    if (guard_armed && violations++ == 0) first_violation = caller;
    portEXIT_CRITICAL_SAFE(&counter_lock);
}

extern "C" {

void prism_allocation_guard_arm(void) { guard_armed = true; }
void prism_allocation_guard_disarm(void) { guard_armed = false; }
std::uint32_t prism_allocation_violations(void) { return violations; }
void *prism_allocation_first_violation(void) { return first_violation; }

} // extern "C"

std::uint32_t prism::platform::allocation_count()
{
    return allocations;
}

std::optional<prism::platform::SystemCounters> prism::platform::system_counters()
{
    const Heap heap = heap_of(MALLOC_CAP_DEFAULT);
    // On ESP-IDF the "RTOS heap" is internal RAM, which overlaps the default heap.
    const Heap internal = heap_of(MALLOC_CAP_INTERNAL);
    return SystemCounters{
        .idle_us = idle_us_of_average_core(),
        .heap_used = heap.used,
        .heap_total = heap.total,
        .stacks_used = internal.used,
        .stacks_total = internal.total,
    };
}
