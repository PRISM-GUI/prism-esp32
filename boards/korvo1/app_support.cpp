#include "app_support.hpp"

#include <cstdio>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace korvo1 {

void halt(const std::string &error)
{
    std::printf("display: %s\n", error.c_str());
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void print_stats(std::string_view line)
{
    std::printf("%.*s\n", int(line.size()), line.data());
}

Touch open_touch_or_none()
{
    auto touch = Touch::open();
    if (!touch)
        std::printf("touch: %s\n", touch.error().c_str());
    return std::move(touch).value_or(Touch{});
}

} // namespace korvo1
