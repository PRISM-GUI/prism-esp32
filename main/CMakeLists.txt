# The app. Every firmware but selftest runs PRISM's own example main(), as PRISM writes it
# (${PRISM_SOURCE_DIR}/examples/...), and adds only app_main.c: this board's startup around it
# (boards/korvo1/port.cpp). selftest is the port's own test, in boards/korvo1/selftest.cpp.

# PRISM's tree -- the subprojects/prism submodule that components/prism builds with Meson. Its own
# CMakeLists defines the same variable, but component scopes do not see each other.
set(PRISM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../subprojects/prism")

set(prism_example_source)
if(CONFIG_PRISM_APP_CRDT)
    set(prism_example_source "${PRISM_SOURCE_DIR}/examples/crdt_hub/main.cpp")
elseif(CONFIG_PRISM_APP_WIDGETS)
    set(prism_example_source "${PRISM_SOURCE_DIR}/examples/embedded/widgets_main.cpp")
elseif(CONFIG_PRISM_APP_SCOPE)
    set(prism_example_source "${PRISM_SOURCE_DIR}/examples/embedded/scope_main.cpp")
elseif(CONFIG_PRISM_APP_TONE)
    set(prism_example_source "${PRISM_SOURCE_DIR}/examples/embedded/tone_main.cpp")
endif()

if(prism_example_source)
    if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${prism_example_source}")
        message(FATAL_ERROR "${prism_example_source} is missing: subprojects/prism is older than "
                            "the example this firmware runs. Update the submodule.")
    endif()
    set(srcs app_main.c "${prism_example_source}")
else()
    # selftest: the board's own check, which brings its own app_main and so needs no C wrapper.
    set(srcs "${CMAKE_CURRENT_LIST_DIR}/../boards/korvo1/selftest.cpp")
endif()

idf_component_register(
    SRCS ${srcs}
    # BT and NVS come with prism_bt's own requirements; with CONFIG_BT_ENABLED off (every app
    # but tone) they have no sources.
    # prism_crdt_hub is the crdt firmware's side of PRISM's crdt::Hub (Wi-Fi access point, HTTPS
    # server, WebSocket). Listed for every app: the build's first pass (early expansion) does not
    # see PRISM_APP, so a condition here is silently false; outside the crdt firmware it registers
    # an empty library.
    REQUIRES prism esp_timer esp_psram freertos korvo1 prism_bt prism_crdt_hub
)
