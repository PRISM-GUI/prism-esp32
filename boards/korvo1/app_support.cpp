#include "app_support.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_heap_caps.h"
#include "esp_pthread.h"
#include "sdkconfig.h"

namespace korvo1 {

namespace {

[[noreturn]] void idle_forever()
{
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

} // namespace

void halt(const std::string &error)
{
    std::printf("display: %s\n", error.c_str());
    idle_forever();
}

void halt_runtime_stopped()
{
    std::printf("frame runtime: a thread could not start. Not enough internal RAM for the "
                "runtime's threads? The two 75 KB bands use internal RAM too\n");
    idle_forever();
}

void print_stats(std::string_view line)
{
    std::printf("%.*s\n", int(line.size()), line.data());
}

void print_internal_heap(const char *where)
{
    std::printf("heap %s: internal free %u, largest block %u\n", where,
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
}

namespace {

constexpr int kLogicCore = 0;
constexpr int kRenderCore = 1;

// The render thread's stack (PRISM's kRenderStackBytes, 48 KB), plus room for its task's control
// block, which may be allocated from internal RAM before the stack.
constexpr std::size_t kRenderStackReserve = 52 * 1024;
void *render_stack_reserve = nullptr;

// The logic thread's 64 KB stack goes to PSRAM: the render thread's 48 KB stack needs one
// contiguous internal block, which the bands and the tree's small allocations leave too few of.
void pin_next_thread_to(int core, std::uint32_t stack_caps)
{
    auto cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = core;
    cfg.stack_alloc_caps = stack_caps;
    esp_pthread_set_cfg(&cfg);
}

} // namespace

prism::platform::FrameOptions pinned_threads()
{
    prism::platform::FrameOptions options;
    options.ui_scale = float(CONFIG_PRISM_UI_SCALE_PERCENT) / 100.f;
    options.before_thread_start = [](prism::platform::ThreadRole role) {
        print_internal_heap(role == prism::platform::ThreadRole::logic ? "before logic thread"
                                                                       : "before render thread");
        if (role == prism::platform::ThreadRole::logic)
            pin_next_thread_to(kLogicCore, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        else {
            // Released right before the stack is allocated, so the stack lands in its hole.
            heap_caps_free(std::exchange(render_stack_reserve, nullptr));
            pin_next_thread_to(kRenderCore, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        }
    };
    return options;
}

void reserve_render_stack()
{
    render_stack_reserve = heap_caps_malloc(kRenderStackReserve, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

Touch open_touch_or_none()
{
    auto touch = Touch::open();
    if (!touch)
        std::printf("touch: %s\n", touch.error().c_str());
    return std::move(touch).value_or(Touch{});
}

} // namespace korvo1
