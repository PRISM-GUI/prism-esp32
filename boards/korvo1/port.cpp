// This board as a PRISM port: the port hooks (prism/platform/alloc_count.hpp, frame_stats.hpp),
// the allocation count and guard, fed by the IDF heap hook, and the figures on the timing line,
// from FreeRTOS's run-time stats and the IDF heap; and, at the bottom, the registration itself
// (korvo1_start, boards/korvo1/port.hpp), so that an app is PRISM's own main() and never sees a
// panel, a touch controller, a codec or a thread attribute.

#include "sdkconfig.h"   // CONFIG_BT_ENABLED: first, before the #if that reads it

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_app.hpp>
#include <prism/platform/frame_stats.hpp>
#include <prism/platform/port.hpp>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "app_support.hpp"
#include "band_display.hpp"
#include "jpeg_decoder.hpp"
#include "mic_capture.hpp"
#include "port.hpp"
#include "profile_dump.hpp"
#include "speaker_device.hpp"

#if CONFIG_BT_ENABLED
#include "bluetooth.hpp"
#endif

#include <cstdint>
#include <functional>
#include <optional>
#include <utility>

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

namespace {

// For the whole run: the render thread draws into the panel until main() is back, and the port's
// run closes over both. BandDisplay has a private constructor (it is only ever opened), so the
// panel lives in the optional; the touch needs none.
std::optional<korvo1::BandDisplay> display;
korvo1::Touch touch;

// What platform::run calls: PRISM's frame runtime over this board's panel, touch and thread
// attributes, plus the one hook an app does not have -- the profile dump when the app plays.
void run_board_frames(prism::platform::TreeFactory make_tree,
                      std::function<void(prism::platform::FrameContext &)> setup)
{
    prism::platform::run_frames(
        *display, touch, std::move(make_tree),
        [setup = std::move(setup)](prism::platform::FrameContext &fc) {
            // The AppContext and the app's setup, then our hook: both must be registered before
            // setup returns.
            setup(fc);
            fc.on_loop(korvo1::dump_profile_on_play());
        },
        korvo1::pinned_threads());
}

} // namespace

extern "C" void korvo1_start(void)
{
    // Before Wi-Fi or anything else claims memory: the stack has to be one contiguous block.
    korvo1::reserve_render_stack();

    auto opened = korvo1::BandDisplay::open();
    if (!opened) korvo1::halt(opened.error());
    display = std::move(*opened);
    touch = korvo1::open_touch_or_none();

    // The board's hardware as PRISM's devices, for every app: an app names a device ("speaker",
    // "headset", "mic") and never knows which board it runs on. All of these are idempotent.
    prism::esp32::register_speaker();
    prism::esp32::register_jpeg_decoder();
#if CONFIG_BT_ENABLED
    prism::esp32::bluetooth::start();   // the bonded headset, or a scan for the configured name
#endif
    // A codec that does not start is not registered: PRISM's scope then shows its synthetic
    // sweep, and the line printed here says why.
    (void)korvo1::register_mic_capture();

    prism::platform::set_port({
        .run_frames = run_board_frames,
        .on_stats = korvo1::print_stats,
    });
}

extern "C" void korvo1_stopped(void)
{
    korvo1::halt_runtime_stopped();
}
