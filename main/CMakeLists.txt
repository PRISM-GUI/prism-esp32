idf_component_register(
    SRCS "${PRISM_APP}.cpp" "port_stubs.cpp"
    REQUIRES prism esp_timer esp_psram
)

