#!/bin/bash
# Build the firmwares: scripts/build.sh [selftest|widgets|scope|all] (default: all).
# Each one is build/<name>.bin, one image merged at address 0 (bootloader, partition table,
# app): flash it with the web flasher or `esptool.py write_flash 0x0 build/<name>.bin`.
#
# With ESP-IDF in the environment (IDF_PATH set: the container, or your own install) it runs
# idf.py directly; otherwise it runs itself in the build image, with Podman or Docker.
set -euo pipefail
cd "$(dirname "$0")/.."

apps=(selftest widgets scope)
# The image tag is the first 7 characters of the ESP-IDF commit in docker/Dockerfile, so the tag
# CI publishes and the one used here cannot drift apart.
idf_tag=idf-$(sed -n 's/^ARG IDF_COMMIT=\(.\{7\}\).*/\1/p' docker/Dockerfile)
image=${PRISM_ESP32_IMAGE:-ghcr.io/jeandet/prism-esp32-dev:$idf_tag}

usage() { local IFS='|'; echo "usage: $0 [${apps[*]}|all]" >&2; exit 2; }

in_container() {
    # PRISM_ESP32_ENGINE picks one when both exist: CI builds the image with Docker, and
    # GitHub's runners also have Podman, which cannot see Docker's images.
    local engine=${PRISM_ESP32_ENGINE:-}
    [[ -n $engine ]] || engine=$(command -v podman || command -v docker) || {
        echo "Neither ESP-IDF (IDF_PATH) nor Podman/Docker found: install one of them." >&2
        exit 1
    }
    local user_flags=(--userns=keep-id)
    [[ $engine == *docker ]] && user_flags=(-u "$(id -u):$(id -g)")
    exec "$engine" run --rm "${user_flags[@]}" -e HOME=/tmp -v "$PWD:/project:z" -w /project \
        "$image" scripts/build.sh "$@"
}

build_app() {
    local app=$1
    idf.py -B "build/$app" -DPRISM_APP="$app" build
    (cd "build/$app" && python -m esptool --chip esp32s31 merge-bin -o "../$app.bin" @flash_args)
    echo "build/$app.bin: $(wc -c < "build/$app.bin" | tr -d " ") bytes"
}

target=${1:-all}
if [[ $target == all ]]; then selected=("${apps[@]}")
elif [[ " ${apps[*]} " == *" $target "* ]]; then selected=("$target")
else usage
fi

[[ -n ${IDF_PATH:-} ]] || in_container "$@"
export IDF_TARGET=esp32s31
# ccache for ESP-IDF's own sources too. The three firmwares build in three directories: paths are
# made relative to the project, and the build directory is left out of the hash, so they share
# the cache (debug info then names the first directory, which nothing here reads).
if command -v ccache >/dev/null; then
    export IDF_CCACHE_ENABLE=1 CCACHE_BASEDIR=$PWD CCACHE_NOHASHDIR=true
fi
for app in "${selected[@]}"; do build_app "$app"; done
