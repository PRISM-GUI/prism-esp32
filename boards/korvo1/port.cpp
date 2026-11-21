// The port hooks of the ESP32-S31-Korvo-1 (prism/platform/alloc_count.hpp, frame_stats.hpp):
// the allocation count and guard, wrapped around malloc, calloc and realloc, and the figures on
// the timing line, from FreeRTOS's run-time stats and the IDF heap.

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <atomic>
#include <cstdint>

namespace {

std::atomic<std::uint32_t> allocations{0};
std::atomic<bool> guard_armed{false};
std::atomic<std::uint32_t> violations{0};
std::atomic<void *> first_violation{nullptr};

// The first violation keeps the return address of the wrapper's caller: the immediate caller
// of malloc, often operator new.
void count_allocation(void *caller)
{
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (!guard_armed.load(std::memory_order_relaxed)) return;
    if (violations.fetch_add(1, std::memory_order_relaxed) == 0) first_violation = caller;
}

struct Heap {
    std::size_t used, total;
};

Heap heap_of(std::uint32_t caps)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, caps);
    return {info.total_allocated_bytes, info.total_allocated_bytes + info.total_free_bytes};
}

std::uint64_t idle_us_of_both_cores()
{
    std::uint64_t sum = 0;
    for (BaseType_t core = 0; core < configNUMBER_OF_CORES; ++core)
        sum += ulTaskGetIdleRunTimeCounterForCore(core);
    return sum;
}

} // namespace

extern "C" {

void *__real_malloc(std::size_t n);
void *__real_calloc(std::size_t n, std::size_t size);
void *__real_realloc(void *p, std::size_t n);

void *__wrap_malloc(std::size_t n)
{
    count_allocation(__builtin_return_address(0));
    return __real_malloc(n);
}

void *__wrap_calloc(std::size_t n, std::size_t size)
{
    count_allocation(__builtin_return_address(0));
    return __real_calloc(n, size);
}

void *__wrap_realloc(void *p, std::size_t n)
{
    count_allocation(__builtin_return_address(0));
    return __real_realloc(p, n);
}

void prism_allocation_guard_arm(void) { guard_armed = true; }
void prism_allocation_guard_disarm(void) { guard_armed = false; }
std::uint32_t prism_allocation_violations(void) { return violations; }
void *prism_allocation_first_violation(void) { return first_violation; }

} // extern "C"

std::uint32_t prism::platform::allocation_count()
{
    return allocations.load(std::memory_order_relaxed);
}

std::optional<prism::platform::SystemCounters> prism::platform::system_counters()
{
    const Heap heap = heap_of(MALLOC_CAP_DEFAULT);
    const Heap internal = heap_of(MALLOC_CAP_INTERNAL);
    return SystemCounters{
        .idle_us = idle_us_of_both_cores(),
        .heap_used = heap.used,
        .heap_total = heap.total,
        .stacks_used = internal.used,
        .stacks_total = internal.total,
    };
}
