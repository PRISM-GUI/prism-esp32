# The app: main/<CONFIG_PRISM_APP_NAME>.cpp, chosen in Kconfig (main/Kconfig.projbuild).
set(app_source "${CONFIG_PRISM_APP_NAME}.cpp")
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${app_source}")
    message(FATAL_ERROR "main/${app_source} does not exist yet: the '${CONFIG_PRISM_APP_NAME}' "
                        "firmware is not written. Build selftest or widgets.")
endif()

idf_component_register(
    SRCS "${app_source}"
    REQUIRES prism esp_timer esp_psram freertos korvo1
)
