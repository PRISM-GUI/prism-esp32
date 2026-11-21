// The widget demo on the Korvo-1's panel: PRISM's shared WidgetsModel through the frame runtime
// (a logic and a render thread), the timing line on the panel and on the console.

#include "display.hpp"
#include "touch.hpp"

#include <embedded/widgets_app.hpp>
#include <prism/platform/frame_app.hpp>

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

// No screen to run on: say why, then stay here so the line stays readable on the console.
[[noreturn]] void halt(const std::string &error)
{
    std::printf("display: %s\n", error.c_str());
    for (;;)
        vTaskDelay(pdMS_TO_TICKS(1000));
}

void print_stats(std::string_view line)
{
    std::printf("%.*s\n", int(line.size()), line.data());
}

} // namespace

extern "C" void app_main()
{
    auto display = korvo1::Display::open();
    if (!display)
        halt(display.error());
    auto touch = korvo1::Touch::open();
    if (!touch)
        std::printf("touch: %s\n", touch.error().c_str());
    korvo1::Touch input = std::move(touch).value_or(korvo1::Touch{});

    static prism_examples::WidgetsModel model;
    // PRISM's WidgetsModel titles itself "PRISM"; this board's demo says what it runs on.
    model.title.set({.value = "PRISM on ESP32-S31", .heading = true});
    prism::platform::frame_app(*display, input, model, [](prism::platform::FrameContext &ctx) {
        prism_examples::attach_widgets(ctx, model);
        ctx.on_stats(print_stats);
    });
}
