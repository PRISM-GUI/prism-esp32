cmake_minimum_required(VERSION 3.22)

# Which firmware to build: idf.py -B build/<app> -DPRISM_APP=<app> build (scripts/build.sh does).
# Its ELF is <app>.elf and its configuration build/<app>/sdkconfig, made from sdkconfig.defaults
# plus sdkconfig.<app>, which picks the app in Kconfig (main/Kconfig.projbuild).
set(PRISM_APP selftest CACHE STRING "The firmware to build: selftest, widgets or scope")
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")
set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults"
                       "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${PRISM_APP}")

# The panel demos record a profile (dump on PLAY); the self-test has no use for the RAM it takes.
if(NOT PRISM_APP STREQUAL "selftest")
    set(PRISM_PROFILING ON CACHE BOOL "" FORCE)
else()
    set(PRISM_PROFILING OFF CACHE BOOL "" FORCE)
endif()

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_LIST_DIR}/boards/korvo1)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(${PRISM_APP})
