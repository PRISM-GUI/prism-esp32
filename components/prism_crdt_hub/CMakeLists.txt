# The crdt firmware's hub: PRISM's crdt::Hub (include/prism/crdt/hub.hpp) with this board's body --
# the Wi-Fi access point, an HTTPS server, and the WebSocket at /room that feeds a crdt::Room. PRISM
# itself builds no hub on bare metal (prism-net is off there: no sockets), so the transport lives
# here, next to the page and the certificate it serves.
#
# Everything below is inside `if(CONFIG_PRISM_APP_CRDT)`: the other firmwares get the empty library
# the first branch registers. That branch also covers the build's first pass (early expansion),
# where no CONFIG_* is set -- the same caveat as main/CMakeLists.txt.
# The requirements are named in both branches: ESP-IDF builds the component graph in the first
# pass, so a REQUIRES that only the second pass sees is never followed.
set(hub_requires prism nvs_flash esp_event esp_netif esp_wifi esp_http_server esp_https_server)
if(CMAKE_BUILD_EARLY_EXPANSION OR NOT CONFIG_PRISM_APP_CRDT)
    idf_component_register(REQUIRES ${hub_requires})
    return()
endif()

# The page and the wasm module, embedded gzipped. scripts/build.sh has already written them into the
# build directory before calling idf.py.
set(crdt_web_dir "${CMAKE_BINARY_DIR}/crdt-web")
set(crdt_web_files
    "${crdt_web_dir}/crdt_pair.html.gz"
    "${crdt_web_dir}/crdt_pair.js.gz"
    "${crdt_web_dir}/crdt_pair.wasm.gz"
    "${crdt_web_dir}/crdt_pair.data.gz")
foreach(file IN LISTS crdt_web_files)
    if(NOT EXISTS "${file}")
        message(FATAL_ERROR "the crdt firmware serves the browser build of PRISM's crdt_pair "
                            "example, and ${file} is missing. scripts/build.sh gzips it from "
                            "PRISM_CRDT_WEB_DIR; build the bundle first (see the crdt "
                            "section of README.md) and run scripts/build.sh crdt.")
    endif()
endforeach()

# The certificate a browser accepts once and remembers. Never committed: it exists only in the
# build directory, good for the board's own address for ten years.
# scripts/make_cert.py uses ESP-IDF's own Python (PYTHON), which has `cryptography`: no openssl.
set(tls_dir "${CMAKE_CURRENT_BINARY_DIR}/tls")
set(cert_pem "${tls_dir}/cert.pem")
set(key_pem "${tls_dir}/key.pem")
add_custom_command(
    OUTPUT "${cert_pem}" "${key_pem}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${tls_dir}"
    COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../../scripts/make_cert.py"
            "${cert_pem}" "${key_pem}" 192.168.4.1
    COMMENT "generating the board's self-signed certificate"
    VERBATIM)
set_source_files_properties("${cert_pem}" "${key_pem}" PROPERTIES GENERATED TRUE)
# Explicit, so the embed steps below cannot run before the files exist.
add_custom_target(prism_crdt_hub_certificate DEPENDS "${cert_pem}" "${key_pem}")

idf_component_register(
    SRCS hub_espidf.cpp realpath_stub.c
    EMBED_FILES ${crdt_web_files}
    EMBED_TXTFILES "${cert_pem}" "${key_pem}"
    REQUIRES ${hub_requires}
)

# The certificate is a custom command's output: the component has to be told, or the embed step and
# the link can be scheduled before the files exist.
add_dependencies(${COMPONENT_LIB} prism_crdt_hub_certificate)
# libprism_crdt_hub comes before libyrs on the link line, so the realpath stub would be skipped
# before Rust's std asks for it: mark it needed from the start.
target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,realpath")
