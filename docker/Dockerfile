# The build image for prism-esp32: ESP-IDF at the pinned commit, its RISC-V toolchain for the
# ESP32-S31, Meson (PRISM's build), and the esp-rs Rust toolchain (Yrs, for the crdt firmware).
# scripts/build.sh runs in it; nothing else on the host needs to know ESP-IDF.
#
#   podman build -f docker/Dockerfile -t ghcr.io/prism-gui/prism-esp32-dev:idf-<sha7> .
#
# ESP-IDF supports the ESP32-S31 on its master branch only: IDF_COMMIT pins the commit every
# firmware of this repository is built and tested with.
ARG BASE=registry.fedoraproject.org/fedora:44
FROM ${BASE}
USER root
RUN dnf install -y --setopt=install_weak_deps=False \
        git wget curl tar xz unzip gzip flex bison gperf python3 python3-pip cmake ninja-build ccache \
        gcc gcc-c++ pkgconf-pkg-config libffi-devel openssl-devel libusb1 dfu-util meson which \
    && dnf clean all

ARG IDF_COMMIT=ad20ded62a2d67a26e501fdc1905f43ba7ad6c81
ENV IDF_PATH=/opt/esp/idf IDF_TOOLS_PATH=/opt/esp/tools
RUN git init -q ${IDF_PATH} && cd ${IDF_PATH} \
    && git remote add origin https://github.com/espressif/esp-idf.git \
    && git fetch -q --depth 1 origin ${IDF_COMMIT} && git checkout -q FETCH_HEAD \
    && git submodule update -q --init --recursive --depth 1 \
    && ./install.sh esp32s31 \
    && rm -rf ${IDF_TOOLS_PATH}/dist
# The project directory is mounted from the host, owned by another user.
RUN git config --system --add safe.directory '*'

# The Rust toolchain the crdt firmware's Yrs is built with (PRISM's yffi subproject, through
# components/prism's PRISM_CRDT). Yrs has no Rust target for this board anywhere but in the esp-rs
# toolchain: upstream nightly's std pins a libc whose newlib stat layout does not build for
# esp-idf, hence espup's, which is selected by RUSTUP_TOOLCHAIN=esp from the Meson cross file.
# System-wide, because scripts/build.sh runs the container with HOME=/tmp: a toolchain installed
# under /root would not be found at build time.
# espup does not list the S31 yet; esp32s3 is enough, since it installs the same esp toolchain
# and the RISC-V target comes from -Zbuild-std.
ENV RUSTUP_HOME=/opt/rust/rustup CARGO_HOME=/opt/rust/cargo
ENV PATH=/opt/rust/cargo/bin:$PATH
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --profile minimal --default-toolchain stable \
    && cargo install espup \
    && espup install --targets esp32s3 \
    && rm -rf ${CARGO_HOME}/registry

COPY docker/entrypoint.sh /opt/esp/entrypoint.sh
ENTRYPOINT ["/opt/esp/entrypoint.sh"]
CMD ["bash"]
