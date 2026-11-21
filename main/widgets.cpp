// The widget demo on the Korvo-1's panel: PRISM's shared WidgetsModel through the frame runtime
// (a logic and a render thread), the timing line on the panel and on the console.

#include "app_support.hpp"
#include "display.hpp"
#include "profile_dump.hpp"
#include "touch.hpp"

#include <embedded/widgets_app.hpp>
#include <prism/platform/frame_app.hpp>

extern "C" void app_main()
{
    auto display = korvo1::Display::open();
    if (!display)
        korvo1::halt(display.error());
    korvo1::Touch input = korvo1::open_touch_or_none();

    static prism_examples::WidgetsModel model;
    // PRISM's WidgetsModel titles itself "PRISM"; this board's demo says what it runs on.
    model.title.set({.value = "PRISM on ESP32-S31", .heading = true});
    prism::platform::frame_app(*display, input, model, [](prism::platform::FrameContext &ctx) {
        prism_examples::attach_widgets(ctx, model);
        ctx.on_stats(korvo1::print_stats);
        ctx.on_loop(korvo1::dump_profile_on_play());
    }, korvo1::pinned_threads());
}
