# PRISM on the ESP32-S31-Korvo-1

A port of [PRISM](https://github.com/jeandet/PRISM) to Espressif's ESP32-S31-Korvo-1 V1.1
(2 × RISC-V at 320 MHz, 16 MB PSRAM, 16 MB flash, 800×480 RGB panel). This is a plain ESP-IDF
project. PRISM is the subproject `subprojects/prism`, built by its own Meson build with
ESP-IDF's compiler and flags (`components/prism/`). This repository holds only the board code.

**Status: skeleton.** One firmware so far, `selftest`: it checks PRISM's threads, builds a
snapshot of PRISM's widget demo at 800×480, and checks the PSRAM. The display, touch, the
microphones and the two demo apps come next (PRISM's `doc/design/esp32-port.md`).

## Get it

    git clone https://github.com/jeandet/prism-esp32
    cd prism-esp32
    git submodule update --init

A fresh clone needs PRISM's `main` to carry the `freertos_port` option and
`examples/embedded/` (`prism_embedded_apps_dep`). Until those PRISM commits are pushed, build
against a local PRISM checkout: `ln -sfn <your PRISM checkout> subprojects/prism`.

## Build

    scripts/build.sh selftest      # -> build/selftest.bin (or: scripts/build.sh all)

With ESP-IDF in your shell (`IDF_PATH` set), the script runs `idf.py` directly. Otherwise it
runs in the build image (`docker/Dockerfile`, ESP-IDF at the pinned commit) with Podman or
Docker. Build the image once:

    podman build -f docker/Dockerfile -t ghcr.io/jeandet/prism-esp32-dev:20261121 .

The container sees only this directory: `subprojects/prism` must be the submodule there, not
a symlink to a checkout elsewhere.

You need Meson (>= 1.5) next to ESP-IDF when you build without the image. Developers with
ESP-IDF can also run `idf.py -B build/selftest -DPRISM_APP=selftest build` directly.

On the lab's Coder workspace: `scripts/remote.sh 'scripts/build.sh selftest'`.

## Flash and read the console

Each `build/<name>.bin` is one image written at address 0:

    esptool.py write_flash 0x0 build/selftest.bin

or use Espressif's web flasher. Open a serial terminal at **921600** baud on the board's
USB-UART port and press reset. The self-test prints one line per check, ending in PASS or
FAIL, then `failures: 0`.
