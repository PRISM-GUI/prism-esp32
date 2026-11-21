// The on-board self-test: PRISM's threads, a snapshot of the widget demo at the panel's size,
// and the PSRAM. One line per check, ending in PASS or FAIL, then the number of failures.

#include <embedded/widgets_app.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/raster/fonts.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>
#include <prism/profile/recorder.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "sdkconfig.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

namespace {

int failures = 0;

void check(const char *line, bool ok, const char *hint = nullptr)
{
    std::printf("%s %s\n", line, ok ? "PASS" : "FAIL");
    if (ok) return;
    ++failures;
    if (hint) std::printf("  %s\n", hint);
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
    check("std::thread join", ran.load(), "CONFIG_PTHREAD_TASK_STACK_SIZE_DEFAULT too small, or no pthread component linked?");
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
    check(line, n > 0, "the widget demo built nothing: check the main task stack (CONFIG_ESP_MAIN_TASK_STACK_SIZE) and the heap");
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

void check_allocation_count()
{
    const auto before = prism::platform::allocation_count();
    int *p = new int(1);
    asm volatile("" : : "r"(p) : "memory");   // the optimiser may not drop the new
    const auto after = prism::platform::allocation_count();
    delete p;
    check("allocation count: new +1", after - before == 1,
          "is CONFIG_HEAP_USE_HOOKS on, and esp_heap_trace_alloc_hook linked from boards/korvo1?");
}

void check_system_counters()
{
    const auto a = prism::platform::system_counters();
    vTaskDelay(pdMS_TO_TICKS(100));
    const auto b = prism::platform::system_counters();
    const bool ok = a && b && a->heap_used > 0 && a->heap_total >= a->heap_used
                    && a->stacks_total >= a->stacks_used && a->stacks_used > 0
                    && b->idle_us > a->idle_us;
    check("system counters: heap reported, idle grows over 100 ms", ok,
          "enable CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS; no heap figures means system_counters() is a stub");
    check("system counters: idle grows by at most 110 ms over 100 ms",
          a && b && b->idle_us - a->idle_us <= 110'000,
          "idle_us must be the mean over cores, in microseconds (CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER)");
}

// The tick rate times the time a 100 ms delay took must come out near 100 ms.
void check_profile_clock_rate()
{
    const auto tps = prism::platform::profile_ticks_per_second();
    const auto t0 = prism::platform::profile_ticks();
    vTaskDelay(pdMS_TO_TICKS(100));
    const auto ms = (prism::platform::profile_ticks() - t0) * 1000 / tps;
    char line[64];
    std::snprintf(line, sizeof line, "profile clock: %llu ticks per second",
                  static_cast<unsigned long long>(tps));
    check(line, tps >= 1'000'000 && ms >= 99 && ms <= 120,
          "esp_timer not running, or profile_ticks_per_second() does not match profile_ticks()?");
}

bool never_goes_back(int reads)
{
    std::uint64_t last = 0;
    for (int i = 0; i < reads; ++i) {
        const auto now = prism::platform::profile_ticks();
        if (now < last) return false;
        last = now;
    }
    return true;
}

// Tasks without a core affinity, more of them than cores, so they migrate: a per-core counter
// would jump back when one lands on the other core.
void check_profile_clock_monotonic()
{
    std::array<std::atomic<bool>, 4> ok{};
    std::array<std::thread, 4> readers;
    for (std::size_t i = 0; i < readers.size(); ++i)
        readers[i] = std::thread([&ok, i] { ok[i] = never_goes_back(1'000'000); });
    for (auto &t : readers) t.join();
    bool all = true;
    for (const auto &o : ok) all = all && o.load();
    check("profile clock never goes back over 1e6 reads", all,
          "a per-core counter: use one clock both cores read (esp_timer_get_time)");
}

} // namespace

extern "C" void app_main(void)
{
    std::printf("PRISM self-test on the ESP32-S31-Korvo-1\n");
    check_thread_join();
    check_snapshot();
    check_psram();
    check_allocation_count();
    check_system_counters();
    check_profile_clock_rate();
    check_profile_clock_monotonic();
    std::printf("failures: %d\n", failures);
}
