// The audio scope on the Korvo-1's panel: the board's microphones through PRISM's shared scope
// (a waveform and a spectrum), the synthetic sweep when the microphones do not start.

#include "display.hpp"
#include "profile_dump.hpp"
#include "mic_source.hpp"
#include "touch.hpp"

#include <embedded/audio_scope.hpp>
#include <embedded/synthetic_source.hpp>
#include <prism/platform/frame_app.hpp>

#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

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

prism_examples::SampleSource &scope_source()
{
    static auto mic = korvo1::MicSource::open();
    static prism_examples::SyntheticSource synthetic{korvo1::MicSource::kDefaultRate};
    if (mic)
        return **mic;
    std::printf("%s\nfalling back to the synthetic source\n", mic.error().c_str());
    return synthetic;
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

    static prism_examples::ScopeModel model;
    static prism_examples::ScopeState state{scope_source()};
    prism::platform::frame_app(*display, input, model, [](prism::platform::FrameContext &ctx) {
        prism_examples::attach_scope(ctx, model, state);
        ctx.on_stats(print_stats);
        ctx.on_loop(korvo1::dump_profile_on_play());
    });
}
