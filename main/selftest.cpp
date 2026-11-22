// The on-board self-test: PRISM's threads, a snapshot of the widget demo at the panel's size,
// and the PSRAM. One line per check, ending in PASS or FAIL, then the number of failures.

#include <embedded/spectrum.hpp>
#include <embedded/widgets_app.hpp>
#include <prism/app/widget_tree.hpp>
#include <prism/raster/fonts.hpp>
#include <prism/render/scene_snapshot.hpp>

#include <prism/platform/alloc_count.hpp>
#include <prism/platform/frame_stats.hpp>
#include <prism/profile/recorder.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_cpu.h"
#include "esp_pthread.h"

#include "sdkconfig.h"
#if CONFIG_SPIRAM
#include "esp_psram.h"
#endif

namespace {

int failures = 0;
// The checks are over within two seconds of reset, before most terminals are open: app_main
// prints the whole report again every few seconds.
std::string report;

void say(const char *fmt, ...)
{
    char line[192];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(line, sizeof line, fmt, args);
    va_end(args);
    std::fputs(line, stdout);
    report += line;
}

void check(const char *line, bool ok, const char *hint = nullptr)
{
    say("%s %s\n", line, ok ? "PASS" : "FAIL");
    if (ok) return;
    ++failures;
    if (hint) say("  %s\n", hint);
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

int core_running_vector_instruction()
{
    asm volatile("esp.zero.q q0" ::: "memory");
    return esp_cpu_get_core_id();
}

// Straight to the console, not into the report: an illegal instruction reboots the board before
// the report is printed, and this line is then the last one on the console.
void warn_before_simd_check()
{
    std::printf("SIMD check on core 1: if the board resets here with an illegal-instruction panic, "
                "the SIMD core is core 0: swap the pins in pinned_threads()\n");
    std::fflush(stdout);
}

int run_vector_instruction_pinned_to(int core)
{
    esp_pthread_cfg_t previous;
    const bool had_previous = esp_pthread_get_cfg(&previous) == ESP_OK;
    auto cfg = esp_pthread_get_default_config();
    cfg.pin_to_core = core;
    esp_pthread_set_cfg(&cfg);
    std::atomic<int> ran_on{-1};
    std::thread worker([&] { ran_on = core_running_vector_instruction(); });
    worker.join();
    auto restore = had_previous ? previous : esp_pthread_get_default_config();
    esp_pthread_set_cfg(&restore);
    return ran_on.load();
}

void check_simd_core()
{
    constexpr int kSimdCore = 1;
    warn_before_simd_check();
    check("SIMD on core 1", run_vector_instruction_pinned_to(kSimdCore) == kSimdCore,
          "the SIMD core is core 0 on this chip: swap the pins in pinned_threads()");
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
    if (!found) say("  no PSRAM found: enable CONFIG_SPIRAM, or is this a WROOM-3 module?\n");
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

// The scope's FFT on a known signal: a half-scale 1 kHz sine at 16 kHz in a 1024-point window must
// peak in bin 64 (1000 / (16000 / 1024) = 64) at -6 dB, and the DC bin must stay empty.
void check_fft()
{
    constexpr std::size_t n = 1024;
    constexpr float rate = 16000.f, hz = 1000.f;
    std::array<float, n> in{};
    std::array<float, n / 2> db{};
    for (std::size_t i = 0; i < n; ++i)
        in[i] = 0.5f * std::sin(2.f * 3.14159265f * hz * float(i) / rate);
    prism_examples::Spectrum fft{n};
    fft.compute(in, db);
    const auto peak = std::ranges::max_element(db) - db.begin();
    char line[96];
    std::snprintf(line, sizeof line, "fft: 1 kHz sine peaks in bin %d at %.1f dB (want 64, -6)", int(peak), db[std::size_t(peak)]);
    check(line, peak == 64 && std::abs(db[64] + 6.f) < 1.f && db[0] < -40.f,
          "the CMSIS-DSP real FFT or its packed output order is wrong on this target");
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

// app_main runs on a FreeRTOS task that is not a pthread, and a board opens its display there.
// A mark on it must record, not abort (std::this_thread::get_id() asserts on such a task).
void check_profile_mark_on_main_task()
{
    static constexpr auto site = prism::profile::make_site("selftest mark");
    static std::array<prism::profile::Event, 4> storage{};
    prism::profile::Recorder recorder{storage, 1};
    recorder.instant(site);
    std::size_t events = 0;
    recorder.for_each_thread([&](const auto &tv) { events += tv.first.size() + tv.second.size(); });
    check("profile mark on the app_main task", events == 1);
}

} // namespace

extern "C" void app_main(void)
{
    say("PRISM self-test on the ESP32-S31-Korvo-1\n");
    check_thread_join();
    check_simd_core();
    check_snapshot();
    check_psram();
    check_allocation_count();
    check_system_counters();
    check_profile_clock_rate();
    check_fft();
    check_profile_clock_monotonic();
    check_profile_mark_on_main_task();
    say("failures: %d\n", failures);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        std::printf("\n(the self-test result again; press the reset button to run it again)\n%s", report.c_str());
    }
}
