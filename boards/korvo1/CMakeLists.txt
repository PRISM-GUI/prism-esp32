# The Korvo-1 port: the hooks PRISM's runtime leaves to the board (allocation count and guard,
# system counters, the profile clock), and the panel and touch as PRISM's BandSink and
# TouchSource, and the microphones as the scope's SampleSource. main requires it.
idf_component_register(
    SRCS app_support.cpp port.cpp profile_clock.cpp band_display.cpp touch.cpp mic_source.cpp bsp_timing.c
         uart_stream.cpp buttons.cpp profile_dump.cpp speaker_device.cpp
    INCLUDE_DIRS .
    LDFRAGMENTS linker.lf
    REQUIRES prism esp_timer esp_hw_support esp_system heap freertos esp_psram esp_lcd
             esp_driver_ppa esp_driver_uart esp_mm pthread espressif__esp32_s31_korvo_1_noglib espressif__esp_lcd_touch espressif__esp_codec_dev
)

# The heap calls esp_heap_trace_alloc_hook (port.cpp) only when this component is linked in, and
# the weak reference does not pull it from the archive: ask for it.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,esp_heap_trace_alloc_hook")
