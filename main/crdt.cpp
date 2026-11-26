// The crdt_pair mixer on the panel, and the same model in a browser on the board's own Wi-Fi:
// crdt_net.cpp serves the page and relays its WebSocket frames into a crdt::Room bound to this
// model's Doc, so an edit on either side lands on the other (examples/crdt_pair/model.hpp is
// PRISM's, shared by the desktop pair, the hub and this firmware).
//
// The panel is also how a visitor learns where to go: the line under the widgets is the address,
// and how many browsers have joined.

#include "app_support.hpp"
#include "band_display.hpp"
#include "crdt_net.hpp"
#include "profile_dump.hpp"
#include "touch.hpp"

#include <crdt_pair/model.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/core/field.hpp>
#include <prism/platform/frame_app.hpp>

#include <cstddef>
#include <string>

namespace {

struct BoardModel {
    crdt_pair::Mixer mixer;
    prism::core::Field<prism::ui::Label<>> status{
        {.value = "starting the access point", .wrap = true}};

    void view(prism::app::WidgetTree::ViewBuilder &vb)
    {
        mixer.view(vb);
        vb.gap(12.f);
        vb.widget_readonly(status);
    }
};

std::string peer_line(std::size_t peers)
{
    if (peers == 0)
        return "https://192.168.4.1 - no browser joined yet";
    const std::string who =
        peers == 1 ? "1 browser joined" : std::to_string(peers) + " browsers joined";
    return "https://192.168.4.1 - " + who;
}

}  // namespace

extern "C" void app_main()
{
    korvo1::reserve_render_stack();   // before Wi-Fi, which fragments internal RAM
    auto display = korvo1::BandDisplay::open();
    if (!display)
        korvo1::halt(display.error());
    korvo1::Touch input = korvo1::open_touch_or_none();

    static BoardModel model;
    // frame_app has no attach(): the fields are bound here, before any browser can join.
    model.mixer.bind();
    static board_crdt::Server server(model.mixer.doc);
    if (std::string error = server.start(); !error.empty())
        korvo1::halt(error);

    prism::platform::frame_app(
        *display, input, model,
        [](prism::platform::FrameContext &ctx) {
            // The one touch point between the sockets and the model: whatever the server queued
            // since the last loop becomes a room join, an applied message or a leave, here.
            ctx.on_loop([] {
                static std::size_t shown = static_cast<std::size_t>(-1);
                const std::size_t peers = server.drain();
                if (peers == shown)
                    return;
                shown = peers;
                model.status.set({.value = peer_line(peers), .wrap = true});
            });
            ctx.on_stats(korvo1::print_stats);
            ctx.on_loop(korvo1::dump_profile_on_play());
        },
        korvo1::pinned_threads());
    korvo1::halt_runtime_stopped();
}
