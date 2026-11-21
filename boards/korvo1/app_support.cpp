#include "app_support.hpp"

#include <cstdio>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_pthread.h"

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

namespace {

constexpr int kLogicCore = 0;
constexpr int kRenderCore = 1;

void pin_next_thread_to(int core)
{
    auto cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = core;
    esp_pthread_set_cfg(&cfg);
}

} // namespace

prism::platform::FrameOptions pinned_threads()
{
    return {.before_thread_start = [](prism::platform::ThreadRole role) {
        pin_next_thread_to(role == prism::platform::ThreadRole::logic ? kLogicCore : kRenderCore);
    }};
}

Touch open_touch_or_none()
{
    auto touch = Touch::open();
    if (!touch)
        std::printf("touch: %s\n", touch.error().c_str());
    return std::move(touch).value_or(Touch{});
}

} // namespace korvo1
