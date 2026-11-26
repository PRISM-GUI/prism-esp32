# The apps

Every firmware but `selftest` runs one of PRISM's examples, unchanged: `crdt`, `widgets`, `scope`
and `tone` are PRISM's `main()`s, compiled from the submodule. `selftest` is the board's own test,
in `boards/korvo1/selftest.cpp`. Kconfig picks exactly one (`main/Kconfig.projbuild`), and
`sdkconfig.<name>` says which, so `scripts/build.sh <name>` produces one merged image,
`build/<name>.bin`.

The board is the port. `boards/korvo1/port.cpp` gives PRISM everything that is hardware: the panel
and the touch (`korvo1_start`), the speaker and the Bluetooth headset as audio devices, the
microphones as a capture device, the JPEG engine as image decoder, and the timing line over the
console. `main/app_main.c` is the whole `app_main`: start the board, call PRISM's `main()`, and if
it ever returns, say why. So `main/` holds no code of any app.

| App | What it does | Needs |
|---|---|---|
| `selftest` | Runs the board's checks and prints them on the console. No screen. | nothing |
| `widgets` | PRISM's widget demo on the panel, with touch. | PSRAM, panel |
| `scope` | PRISM's audio scope, on the board's microphones (16 kHz, mono). | PSRAM, panel, the codec |
| `tone` | PRISM's tone player: lists the audio devices, plays a 440 Hz tone on the speaker at 44.1 then 48 kHz, then keeps one on the Bluetooth headset. | PSRAM, speaker, a headset for the last part |
| `crdt` | PRISM's crdt_pair mixer on the panel, and the same model in a browser on the board's own Wi-Fi. | PSRAM, panel, PRISM's browser build |

`widgets`, `scope` and `crdt` show the timing line on the panel and on the console. `scope` falls
back to a synthetic sweep when the microphone codec does not start. `tone` prints to the console
only. `crdt` shows the mixer and, under it, the address a browser opens (`https://192.168.4.1`) and
how many browsers have joined.

## Build and flash

    scripts/build.sh tone
    scripts/build.sh              # everything, except crdt without PRISM's browser build
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

PRISM's `examples/embedded/tone_main.cpp`, so its lines are PRISM's to change. It lists what the
port registered:

    audio devices: 2
      speaker: Speaker (s16, 2 channels, rates 44100 48000, default)
      headset: WH-1000XM3 (s16, 2 channels, rates 44100)
    ...

Then it plays at 44.1 and 48 kHz and prints each Output's state and its worst block against the
budget. The headset part runs because `sdkconfig.tone` turns Bluetooth on: the link's state is
printed as it changes (`headset: Off`, then `headset: Connected to WH-1000XM3`), and so are the
headset's buttons. With `CONFIG_BT_ENABLED` off, the app stops after the second rate.

### crdt

    crdt: access point "PRISM" up at 192.168.4.1
    crdt: serving https://192.168.4.1
    crdt: socket 53 joined
    crdt: socket 53 left

One `joined` line per browser page, one `left` per close or reload (the panel's count is what
matters: it goes up and down with the pages in the room). A refused handshake logs
`crdt: refused a websocket from origin ...`.

`crdt` is the only app that needs more than the submodule: it embeds PRISM's `crdt_pair` page,
built for the browser with emscripten, gzipped. `scripts/build.sh crdt` needs `PRISM_CRDT_WEB_DIR`
to point at that build's output, and says how to make it if it is missing (README, "Edit together:
the crdt firmware"). The same flag turns PRISM's crdt layer on; its engine, ycpp, is plain C++ built
by PRISM's own Meson build, so the crdt firmware needs nothing beyond ESP-IDF.

The hub's OS side is `components/prism_crdt_hub`: the access point, the HTTPS server, the
WebSocket at `/room`, the embedded page and the certificate. The app opens a `crdt::Hub` and
drains it; it never names a socket. PRISM builds no hub on bare metal (no sockets there), so this
component is where `prism/crdt/hub.hpp` gets its body for this chip. Outside the `crdt` firmware it
registers an empty library.

## Where the shared code lives

The port is three files: `boards/korvo1/port.cpp` (the registration, the audio and capture
devices, plus the allocation count and the system counters PRISM's timing line reads),
`boards/korvo1/port.hpp` (the two C functions `app_main.c` calls) and `main/app_main.c`. What the
board's own test shares with the rest is `boards/korvo1/app_support.hpp`: `halt` for a missing
screen, `print_stats` for the timing line, `open_touch_or_none` for input, and `pinned_threads()`
for the runtime options (logic on core 0, render on core 1, the UI scale).

`boards/korvo1/selftest.cpp` is the one firmware that is not an app: it is the board's test, so it
brings its own `app_main()` and `main/CMakeLists.txt` compiles it instead of `app_main.c` +
PRISM's example.

Adding an app in this repository means adding one to PRISM: write `examples/embedded/<name>_main.cpp`
-- a `main()` calling `platform::run` -- add an `elseif(CONFIG_PRISM_APP_<NAME>)` to
`main/CMakeLists.txt`, a `config PRISM_APP_<NAME>` to `main/Kconfig.projbuild`, `sdkconfig.<name>`,
and the name to `apps=(...)` in `scripts/build.sh`. A board-only tool (like `selftest`) writes a
`boards/korvo1/<name>.cpp` with an `app_main()` instead.
