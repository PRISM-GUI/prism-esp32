#!/bin/bash
# Build the firmwares: scripts/build.sh [selftest|widgets|all] (default: all).
# Each one is build/<name>.bin, one image merged at address 0 (bootloader, partition table,
# app): flash it with the web flasher or `esptool.py write_flash 0x0 build/<name>.bin`.
#
# With ESP-IDF in the environment (IDF_PATH set: the container, or your own install) it runs
# idf.py directly; otherwise it runs itself in the build image, with Podman or Docker.
set -euo pipefail
cd "$(dirname "$0")/.."

apps=(selftest widgets)
image=${PRISM_ESP32_IMAGE:-ghcr.io/jeandet/prism-esp32-dev:20261121}

usage() { local IFS='|'; echo "usage: $0 [${apps[*]}|all]" >&2; exit 2; }

in_container() {
    local engine
    engine=$(command -v podman || command -v docker) || {
        echo "Neither ESP-IDF (IDF_PATH) nor Podman/Docker found: install one of them." >&2
        exit 1
    }
    exec "$engine" run --rm -v "$PWD:/project:z" -w /project "$image" scripts/build.sh "$@"
}

build_app() {
    local app=$1
    idf.py -B "build/$app" -DPRISM_APP="$app" build
    (cd "build/$app" && python -m esptool --chip esp32s31 merge-bin -o "../$app.bin" @flash_args)
    echo "build/$app.bin: $(stat -c %s "build/$app.bin") bytes"
}

target=${1:-all}
if [[ $target == scope ]]; then
    echo "The scope firmware is not written yet (main/scope.cpp): build selftest or widgets." >&2
    exit 2
fi
if [[ $target == all ]]; then selected=("${apps[@]}")
elif [[ " ${apps[*]} " == *" $target "* ]]; then selected=("$target")
else usage
fi

[[ -n ${IDF_PATH:-} ]] || in_container "$@"
export IDF_TARGET=esp32s31
for app in "${selected[@]}"; do build_app "$app"; done
