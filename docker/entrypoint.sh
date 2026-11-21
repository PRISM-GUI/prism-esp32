#!/bin/bash
# ESP-IDF's environment (idf.py, the toolchain, esptool) for whatever the container runs.
. "$IDF_PATH/export.sh" > /dev/null
exec "$@"
