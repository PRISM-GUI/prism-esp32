# PRISM on the ESP32-S31-Korvo-1

This repository runs [PRISM](https://github.com/jeandet/PRISM) on Espressif's
ESP32-S31-Korvo-1 board. You can flash a demo and watch it run without installing ESP-IDF.

## The board

The ESP32-S31-Korvo-1 V1.1 is a development board with:

- two RISC-V cores at 320 MHz,
- 16 MB of flash and 16 MB of PSRAM,
- an 800x480 touch screen (RGB panel, GT1151 touch controller),
- two microphones on an ES8389 audio codec,
- four buttons: SET, MODE, VOLP and VOLM.

It has two USB-C ports. Use the one marked UART. The other one only powers the board.

## The three images

Each image is one file, written at address 0. You can pick one of three.

| Image | What it does |
|---|---|
| `selftest.bin` | Checks PRISM's threads, the PSRAM and the clocks. Prints the result on the console. No screen needed. |
| `widgets.bin` | PRISM's widget demo: a counter, a checkbox and a slider. Touch it. |
| `scope.bin` | A scope on the board's microphones: the signal on the left, its spectrum on the right. |

Where to get them:

- A GitHub release has all three (tags `v*`).
- Every CI run keeps them as the `firmware` artifact.
- Or build them yourself (see "Build it yourself").

## Flash from the browser

Nothing to install. It works on macOS, Linux and Windows, in Chrome or Edge.

1. Plug the board in by its UART USB-C port.
2. Open [Espressif's web flasher](https://espressif.github.io/esp-launchpad/).
3. Connect to the board.
4. Choose the image and set the address to `0x0`.
5. Flash it, then press the reset button.

## Flash with `flash.sh`

You need `esptool` (`pip install esptool`). If it is missing, the script tells you.

    scripts/flash.sh widgets.bin

The script finds the board's serial port by itself. It runs:

    esptool.py --chip esp32s31 -p <port> -b 921600 write_flash 0x0 widgets.bin

If it finds two boards, it lists them. Run it again with the port you want:

    scripts/flash.sh widgets.bin /dev/ttyUSB1

If it finds none, it asks whether the UART USB-C port is plugged in.

How it finds the port:

- On Linux it looks at `/dev/ttyUSB*` and `/dev/ttyACM*`. It keeps those whose USB bridge is
  a common one (CP210x 10c4:ea60, CH343 1a86:55d3, CH340 1a86:7523, FTDI 0403:6001, 6010 and
  6015). Espressif does not say which chip the Korvo-1 uses, so this is a list, not a
  single match. If your board is not found, pass the port yourself.
- On macOS it lists `/dev/cu.usbserial*`, `cu.wchusbserial*`, `cu.SLAB_USBtoUART*` and
  `cu.usbmodem*`.
- You may need to be in the `dialout` group on Linux.

`scripts/test_flash.sh` checks the script against a fake `esptool`.

## The console at 921600

The board prints on its UART at **921600** baud, for everything. Open any serial terminal:

    picocom -b 921600 /dev/ttyUSB0

Press reset. The first line or two may be garbage. That is the chip's ROM bootloader, which
speaks at 115200.

What you should see:

- `selftest`: one line per check, each ending in `PASS`:

      PRISM self-test on the ESP32-S31-Korvo-1
      std::thread join PASS
      snapshot built: N draw commands PASS
      PSRAM: 16 MB PASS
      allocation count: new +1 PASS
      system counters: heap reported, idle grows over 100 ms PASS
      system counters: idle grows by at most 110 ms over 100 ms PASS
      profile clock: 1000000 ticks per second PASS

  It ends with `failures: 0`. A `FAIL` line comes with a hint.
- `widgets` and `scope`: a timing line once a second (update, snapshot, draw and present
  times, and the CPU load).

If something is wrong, the line says what to do next. For example
`no PSRAM found: is this a WROOM-3 module?`.

## Take a trace

`widgets` and `scope` record where the time goes. The recorder lives in PSRAM.

1. Flash `widgets.bin` or `scope.bin`. Open no terminal on the port.
2. Start `prism-trace` on the port. Build it from PRISM (`tools/prism-trace/`).

       prism-trace /dev/ttyUSB0 -o trace.json --baud 921600

3. Let the demo run for 15 seconds.
4. Press the **MODE** button. The screen freezes for a few seconds while the dump drains.
5. The console says `profile: dump done (N bytes)`.

`prism-trace` prints a summary. It should say `1000000 ticks/s` and `0 warning(s)`.
Open `trace.json` in [ui.perfetto.dev](https://ui.perfetto.dev).

## Build it yourself

Docker or Podman is all you need.

    git clone --recursive https://github.com/jeandet/prism-esp32
    cd prism-esp32
    scripts/build.sh            # selftest, widgets and scope
    scripts/build.sh widgets    # just one

The images are `build/selftest.bin`, `build/widgets.bin` and `build/scope.bin`.

The script runs in the build image, `ghcr.io/jeandet/prism-esp32-dev`, tagged `idf-<sha7>`, the first 7 characters
of the pinned ESP-IDF commit in `docker/Dockerfile` (and `latest`). It has ESP-IDF at the pinned commit. To build the image yourself:

    podman build -f docker/Dockerfile -t ghcr.io/jeandet/prism-esp32-dev:idf-<sha7> .

If you already have ESP-IDF in your shell (`IDF_PATH` set), the script runs `idf.py` directly.
In that case you also need Meson 1.5 or newer.

Flash from the host, not from the container. USB does not reach a container on macOS.

PRISM is the git submodule `subprojects/prism`. The container sees only this directory, so
it must be the submodule, not a symlink to a checkout elsewhere.

## Add an app

An app is a model and a view, as on the desktop. This repository only holds the board.

1. Write `main/<name>.cpp` with an `app_main()`. Copy `main/widgets.cpp`: it opens the
   display and the touch, then runs PRISM's `frame_app` on your model.
2. Add a line for it in `main/Kconfig.projbuild` (a `config PRISM_APP_<NAME>` and a
   `default "<name>"` for `PRISM_APP_NAME`).
3. Add `sdkconfig.<name>` with one line: `CONFIG_PRISM_APP_<NAME>=y`.
4. Add `<name>` to `apps=(...)` in `scripts/build.sh`.

Then `scripts/build.sh <name>` makes `build/<name>.bin`.

## Screenshots

Put them in `docs/` and link them here.

- `docs/widgets.jpg`: the widgets demo on the panel.
- `docs/scope.jpg`: the scope, with the spectrum of a whistle.
