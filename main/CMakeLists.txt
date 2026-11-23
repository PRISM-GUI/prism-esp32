# The app: main/<CONFIG_PRISM_APP_NAME>.cpp, chosen in Kconfig (main/Kconfig.projbuild).
set(app_source "${CONFIG_PRISM_APP_NAME}.cpp")
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${app_source}")
    message(FATAL_ERROR "main/${app_source} does not exist yet: the '${CONFIG_PRISM_APP_NAME}' "
                        "firmware is not written. Build selftest or widgets.")
endif()

idf_component_register(
    SRCS "${app_source}"
    # bt and nvs_flash are for btspike only. They are listed for every app: the build's first pass
    # (early expansion) does not see PRISM_APP, so a condition here is silently false. With
    # CONFIG_BT_ENABLED off (every other app) bt has no sources.
    # prism_bt is the headset device (components/prism_bt): the tone app uses it, and with
    # Bluetooth off nothing references its archive, so it is built and never linked.
    REQUIRES prism esp_timer esp_psram freertos korvo1 bt nvs_flash prism_bt
)
