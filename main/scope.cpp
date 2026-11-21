// The audio scope on the Korvo-1's panel: the board's microphones through PRISM's shared scope
// (a waveform and a spectrum), the synthetic sweep when the microphones do not start.

#include "app_support.hpp"
#include "band_display.hpp"
#include "profile_dump.hpp"
#include "mic_source.hpp"
#include "touch.hpp"

#include <embedded/audio_scope.hpp>
#include <embedded/synthetic_source.hpp>
#include <prism/platform/frame_app.hpp>

#include <cstdio>
#include <memory>

namespace {

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
    auto display = korvo1::BandDisplay::open();
    if (!display)
        korvo1::halt(display.error());
    korvo1::Touch input = korvo1::open_touch_or_none();

    static prism_examples::ScopeModel model;
    static prism_examples::ScopeState state{scope_source()};
    prism::platform::frame_app(*display, input, model, [](prism::platform::FrameContext &ctx) {
        prism_examples::attach_scope(ctx, model, state);
        ctx.on_stats(korvo1::print_stats);
        ctx.on_loop(korvo1::dump_profile_on_play());
    }, korvo1::pinned_threads());
}
