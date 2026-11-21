# The Korvo-1 port: the hooks PRISM's runtime leaves to the board (allocation count and guard,
# system counters, the profile clock). main requires it.
idf_component_register(
    SRCS port.cpp profile_clock.cpp
    REQUIRES prism esp_timer esp_hw_support esp_system heap freertos
)

# The heap calls esp_heap_trace_alloc_hook (port.cpp) only when this component is linked in, and
# the weak reference does not pull it from the archive: ask for it.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,esp_heap_trace_alloc_hook")
