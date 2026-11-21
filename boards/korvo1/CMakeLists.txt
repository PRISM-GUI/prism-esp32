# The Korvo-1 port: the hooks PRISM's runtime leaves to the board (allocation count and guard,
# system counters, the profile clock). main requires it; the --wrap options reach the app's link.
idf_component_register(
    SRCS port.cpp profile_clock.cpp
    REQUIRES prism esp_timer esp_hw_support esp_system heap freertos
)

# The count is taken by wrapping the three allocators; IDF's own malloc, calloc and realloc
# are what the wrap hands on to. INTERFACE: it is the final link that needs them.
target_link_options(${COMPONENT_LIB} INTERFACE
    -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc)
# Keep the wrappers even when only the linker's --wrap refers to them.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,__wrap_malloc")
