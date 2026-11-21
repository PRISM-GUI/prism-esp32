cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Which firmware to build: main/<PRISM_APP>.cpp, linked as <PRISM_APP>.elf
# (idf.py -DPRISM_APP=selftest build; scripts/build.sh passes it).
set(PRISM_APP selftest CACHE STRING "The firmware to build: selftest")
project(${PRISM_APP})
