#!/bin/bash
# Build the firmwares: scripts/build.sh [selftest|widgets|scope|tone|crdt|all] (default: all).
# Each one is build/<name>.bin, one image merged at address 0 (bootloader, partition table,
# app): flash it with the web flasher or `esptool.py write_flash 0x0 build/<name>.bin`.
#
# crdt also embeds PRISM's browser build, which is a separate emscripten build of PRISM: it needs
# PRISM_CRDT_WEB_DIR (see crdt_web_bundle below). "all" skips that firmware when the variable is
# not set, so the other four still build.
#
# With ESP-IDF in the environment (IDF_PATH set: the container, or your own install) it runs
# idf.py directly; otherwise it runs itself in the build image, with Podman or Docker.
set -euo pipefail
cd "$(dirname "$0")/.."

apps=(selftest widgets scope tone crdt)
# The image tag is the first 7 characters of the ESP-IDF commit in docker/Dockerfile, so the tag
# CI publishes and the one used here cannot drift apart.
idf_tag=idf-$(sed -n 's/^ARG IDF_COMMIT=\(.\{7\}\).*/\1/p' docker/Dockerfile)
image=${PRISM_ESP32_IMAGE:-ghcr.io/prism-gui/prism-esp32-dev:$idf_tag}

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

# The crdt firmware embeds the three files of PRISM's browser build, gzipped, so a browser loads
# the page and the wasm module from flash. PRISM_CRDT_WEB_DIR is the emscripten build's
# examples/crdt_pair/ directory; this runs on the host, before the container, because that build
# lives outside this repository (the container only sees the repository).
crdt_web_bundle() {
    local dir=$1
    if [[ -z ${PRISM_CRDT_WEB_DIR:-} ]]; then
        cat >&2 <<'EOF'
the crdt firmware serves the browser build of PRISM's crdt_pair example, which is not built yet.
Build it with emscripten (PRISM's doc/integrations/crdt.md):

    meson setup builddir-wasm --cross-file cross/wasm32-emscripten.ini \
        --buildtype=release -Dtests=disabled -Dbenchmarks=disabled -Dcrdt=enabled
    ninja -C builddir-wasm examples/crdt_pair/crdt_pair.html

then point PRISM_CRDT_WEB_DIR at its output directory:

    PRISM_CRDT_WEB_DIR=builddir-wasm/examples/crdt_pair scripts/build.sh crdt
EOF
        exit 1
    fi
    mkdir -p "$dir"
    for file in crdt_pair.html crdt_pair.js crdt_pair.wasm crdt_pair.data; do
        if [[ ! -f $PRISM_CRDT_WEB_DIR/$file ]]; then
            echo "PRISM_CRDT_WEB_DIR=$PRISM_CRDT_WEB_DIR has no $file:" \
                 "point it at PRISM's builddir-wasm/examples/crdt_pair." >&2
            exit 1
        fi
        crdt_web_gzip "$PRISM_CRDT_WEB_DIR/$file" "$dir/$file.gz"
    done
}

crdt_web_gzip() {
    local src=$1 dst=$2
    gzip -9 -n -c "$src" > "$dst.tmp"
    # -n keeps the bytes reproducible, so an unchanged input leaves the file untouched and the
    # firmware is not relinked for nothing.
    if cmp -s "$dst.tmp" "$dst"; then rm -f "$dst.tmp"; else mv "$dst.tmp" "$dst"; fi
}

build_app() {
    local app=$1
    local extra=()
    if [[ $app == crdt ]]; then extra=(-DPRISM_CRDT=ON); fi
    idf.py -B "build/$app" -DPRISM_APP="$app" "${extra[@]}" build
    (cd "build/$app" && python -m esptool --chip esp32s31 merge-bin -o "../$app.bin" @flash_args)
    echo "build/$app.bin: $(wc -c < "build/$app.bin" | tr -d " ") bytes"
}

target=${1:-all}
if [[ $target == all ]]; then
    selected=()
    for app in "${apps[@]}"; do
        if [[ $app == crdt && -z ${PRISM_CRDT_WEB_DIR:-} ]]; then
            echo "skipping crdt: no browser build to embed (set PRISM_CRDT_WEB_DIR," \
                 "see the crdt section of README.md)" >&2
            continue
        fi
        selected+=("$app")
    done
elif [[ " ${apps[*]} " == *" $target "* ]]; then selected=("$target")
else usage
fi

# Before the container: it only sees the repository, and PRISM's browser build is not in it.
if [[ " ${selected[*]} " == *" crdt "* ]]; then crdt_web_bundle build/crdt/crdt-web; fi

[[ -n ${IDF_PATH:-} ]] || in_container "$@"
export IDF_TARGET=esp32s31
# ccache for ESP-IDF's own sources too. The firmwares build in one directory each: paths are
# made relative to the project, and the build directory is left out of the hash, so they share
# the cache (debug info then names the first directory, which nothing here reads).
if command -v ccache >/dev/null; then
    export IDF_CCACHE_ENABLE=1 CCACHE_BASEDIR=$PWD CCACHE_NOHASHDIR=true
fi
for app in "${selected[@]}"; do build_app "$app"; done
