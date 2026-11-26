# The build image for prism-esp32: ESP-IDF at the pinned commit, its RISC-V toolchain for the
# ESP32-S31, and Meson (PRISM's build).
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

COPY docker/entrypoint.sh /opt/esp/entrypoint.sh
ENTRYPOINT ["/opt/esp/entrypoint.sh"]
CMD ["bash"]
