# The apps

Each app is one `main/<name>.cpp` with an `app_main()`. Kconfig picks exactly one
(`main/Kconfig.projbuild`), and `sdkconfig.<name>` says which, so `scripts/build.sh <name>`
produces one merged image, `build/<name>.bin`.

| App | What it does | Needs |
|---|---|---|
| `selftest` | Runs the on-board checks and prints them on the console. No screen. | nothing |
| `widgets` | PRISM's widget demo on the panel, with touch. | PSRAM, panel |
| `scope` | The board's microphones as a waveform and a spectrum. | PSRAM, panel |
| `tone` | Lists the audio devices, decodes a test JPEG, plays a 440 Hz tone on the speaker at 44.1 then 48 kHz, then keeps one on the Bluetooth headset. | PSRAM, speaker, a headset for the last part |
| `btspike` | Throwaway spike: A2DP plus AVRCP next to the widget demo, heap printed every 5 s. | PSRAM, panel, a headset |

`widgets` and `scope` show the timing line on the panel and on the console. `tone` prints to the
console only. `btspike` shows the widget demo and prints one heap line every 5 s on the console.

## Build and flash

    scripts/build.sh tone
    scripts/build.sh              # all five
    scripts/flash.sh build/tone.bin

Docker or Podman is enough. With `IDF_PATH` set, the script runs `idf.py` directly, and then you
also need Meson 1.5 or newer.

## What each app prints

### selftest

    PRISM self-test on the ESP32-S31-Korvo-1
    std::thread join PASS
    SIMD on core 1 PASS
    snapshot built: N draw commands PASS
    PSRAM: 16 MB PASS
    allocation count: new +1 PASS
    system counters: heap reported, idle grows over 100 ms PASS
    system counters: idle grows by at most 110 ms over 100 ms PASS
    profile clock: 1000000 ticks per second PASS
    fft: 1 kHz sine peaks in bin 64 at -6.0 dB (want 64, -6) PASS
    profile clock never goes back over 1e6 reads PASS
    profile mark on the app_main task PASS
    failures: 0

The checks are over within two seconds of reset, so the app prints the whole report again every
5 s. One line goes straight to the console before the report: the SIMD check warns that a reset
there means the vector core is core 0, in which case the pins in `pinned_threads()` must be
swapped.

Each check that fails comes with a hint line. Examples: a task stack too small, `CONFIG_SPIRAM`
off, the heap hook not linked, a per-core profile clock that runs backwards on the other core.

### tone

    audio devices: 2
      speaker: Speaker (s16, 2 channels, rates 44100 48000, default)
      headset: WH-1000XM3 (s16, 2 channels, rates 44100)
    jpeg: 64x64 rgb565 px0=0xF800
    speaker at 44100 Hz: playing
      N blocks, worst X ms of a Y ms block
    speaker at 48000 Hz: playing
      N blocks, worst X ms of a Y ms block
    headset: Off
    ...
    headset: Connected to WH-1000XM3
    headset tone: playing
    headset button: play/pause

The `jpeg` line is the decoder's answer: `px0=0xF800` is the red first pixel of the built-in test
picture, so the hardware engine ran. Any other value, or a line starting with `jpeg:`, is a
failure and says why.

The headset part runs because `sdkconfig.tone` turns Bluetooth on. In a build with
`CONFIG_BT_ENABLED` off, the app stops after the second rate and prints `tone: 44.1 then 48 kHz,
done (reset to play again)`.

### btspike

A throwaway experiment, not an app to build on. It sets the device name to `PRISM_BTSPIKE`,
connects to the first headset named `WH-1000XM3` (a constant in `main/btspike.cpp`, not the
Kconfig option), streams a 440 Hz tone, runs the widget demo, and prints one line every 5 s:

    btspike: streaming internal free N, largest block M, psram free P, late K

`late` counts callbacks that came more than twice a buffer's length after the previous one, so
the stream may have starved. The line exists to answer one question: how much internal RAM
Bluetooth Classic leaves next to the panel. The `prism_bt` component is the supported answer; see
[audio.md](audio.md).

## Where the shared code lives

The panel apps share `boards/korvo1/app_support.hpp`: `halt` for a missing screen,
`print_stats` for the timing line, `open_touch_or_none` for input, and `pinned_threads()` for
the runtime options (logic on core 0, render on core 1, the UI scale).

Adding an app in this repository: copy `main/widgets.cpp`, add a `config PRISM_APP_<NAME>` and a
`default "<name>"` for `PRISM_APP_NAME` in `main/Kconfig.projbuild`, add `sdkconfig.<name>`, and
add the name to `apps=(...)` in `scripts/build.sh`.
