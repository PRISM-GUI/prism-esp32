# The app. Two kinds, picked in Kconfig (main/Kconfig.projbuild):
#   - crdt and widgets run PRISM's own example main(), as PRISM writes it (${PRISM_SOURCE_DIR}/
#     examples/...), and add only app_main.c: this board's startup around it (boards/korvo1/port.cpp).
#   - selftest, scope, tone and btspike are this board's own tools, one main/<name>.cpp each.

# PRISM's tree -- the subprojects/prism submodule that components/prism builds with Meson. Its own
# CMakeLists defines the same variable, but component scopes do not see each other.
set(PRISM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/prism")

set(prism_example_source)
if(CONFIG_PRISM_APP_CRDT)
    set(prism_example_source "${PRISM_SOURCE_DIR}/examples/crdt_hub/main.cpp")
elseif(CONFIG_PRISM_APP_WIDGETS)
    set(prism_example_source "${PRISM_SOURCE_DIR}/examples/embedded/widgets_main.cpp")
endif()

if(prism_example_source)
    if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${prism_example_source}")
        message(FATAL_ERROR "${prism_example_source} is missing: subprojects/prism is older than "
                            "the example this firmware runs. Update the submodule.")
    endif()
    set(srcs app_main.c "${prism_example_source}")
else()
    # The build's first pass (early expansion) leaves PRISM_APP empty, so app_source is ".cpp" and
    # skipped there; a pass that has the CONFIG_* and still finds no file fails here.
    set(app_source "${CONFIG_PRISM_APP_NAME}.cpp")
    if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${app_source}")
        message(FATAL_ERROR "main/${app_source} does not exist yet: the '${CONFIG_PRISM_APP_NAME}' "
                            "firmware is not written. Build one of the apps in main/ (scripts/build.sh lists them).")
    endif()
    set(srcs "${app_source}")
endif()

idf_component_register(
    SRCS ${srcs}
    # bt and nvs_flash are for btspike only. They are listed for every app: the build's first pass
    # (early expansion) does not see PRISM_APP, so a condition here is silently false. With
    # CONFIG_BT_ENABLED off (every other app) bt has no sources.
    # prism_bt is the headset device (components/prism_bt): the tone app uses it, and with
    # Bluetooth off nothing references its archive, so it is built and never linked.
    # prism_crdt_hub is the crdt firmware's side of PRISM's crdt::Hub (Wi-Fi access point, HTTPS
    # server, WebSocket). Listed for every app for the same reason; outside the crdt firmware it
    # registers an empty library.
    REQUIRES prism esp_timer esp_psram freertos korvo1 bt nvs_flash prism_bt prism_crdt_hub
)
