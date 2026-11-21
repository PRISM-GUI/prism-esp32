// The on-board self-test: PRISM's threads, a snapshot of the widget demo at the panel's size,
// and the PSRAM. One line per check, ending in PASS or FAIL, then the number of failures.

#include <embedded/widgets_app.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/raster/fonts.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <atomic>
#include <cstdio>
#include <thread>

#include "sdkconfig.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

namespace {

int failures = 0;

void check(const char *line, bool ok)
{
    std::printf("%s %s\n", line, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

std::size_t command_count(const prism::render::SceneSnapshot &snap)
{
    std::size_t n = snap.overlay.size();
    for (const auto &dl : snap.draw_lists) n += dl->size();
    return n;
}

void check_thread_join()
{
    std::atomic<bool> ran{false};
    std::thread worker([&] { ran = true; });
    worker.join();
    check("std::thread join", ran.load());
}

void check_snapshot()
{
    prism::raster::Fonts fonts;   // the embedded TTFs measure the text, as on the panel; outlives the tree
    prism_examples::WidgetsModel model;
    prism::app::WidgetTree tree(model);
    tree.set_text_measurer(&fonts);
    auto snap = tree.build_snapshot(800, 480, 1);
    const std::size_t n = snap ? command_count(*snap) : 0;
    char line[64];
    std::snprintf(line, sizeof line, "snapshot built: %zu draw commands", n);
    check(line, n > 0);
}

void check_psram()
{
#if CONFIG_SPIRAM
    const bool found = esp_psram_is_initialized();
    const std::size_t mb = found ? esp_psram_get_size() / (1024 * 1024) : 0;
#else
    const bool found = false;
    const std::size_t mb = 0;
#endif
    char line[32];
    std::snprintf(line, sizeof line, "PSRAM: %zu MB", mb);
    check(line, found && mb > 0);
    if (!found) std::printf("  no PSRAM found: enable CONFIG_SPIRAM, or is this a WROOM-3 module?\n");
}

} // namespace

extern "C" void app_main(void)
{
    std::printf("PRISM self-test on the ESP32-S31-Korvo-1\n");
    check_thread_join();
    check_snapshot();
    check_psram();
    std::printf("failures: %d\n", failures);
}
