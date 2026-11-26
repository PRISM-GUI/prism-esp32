# The app: main/<CONFIG_PRISM_APP_NAME>.cpp, chosen in Kconfig (main/Kconfig.projbuild).
set(app_source "${CONFIG_PRISM_APP_NAME}.cpp")
if(NOT CMAKE_BUILD_EARLY_EXPANSION AND NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/${app_source}")
    message(FATAL_ERROR "main/${app_source} does not exist yet: the '${CONFIG_PRISM_APP_NAME}' "
                        "firmware is not written. Build one of the apps in main/ (scripts/build.sh lists them).")
endif()

set(srcs "${app_source}")
set(embed_files)
set(embed_text_files)

# The crdt firmware: its own network stack side (main/crdt_net.cpp), the page and the wasm module
# embedded gzipped, and a certificate made at build time.
#
# This block is skipped in the build's first pass (early expansion), where no CONFIG_* is set --
# the same caveat as above, and the reason nothing here guards on a CONFIG_ it needs in that pass.
# scripts/build.sh has already written the bundle into the build directory by then, since it does
# so before calling idf.py.
if(CONFIG_PRISM_APP_CRDT)
    list(APPEND srcs crdt_net.cpp realpath_stub.c)

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
    list(APPEND embed_files ${crdt_web_files})

    # The certificate a browser accepts once and remembers. Never committed: it exists only in the
    # build directory, good for the board's own address for ten years.
    # scripts/make_cert.py uses ESP-IDF's own Python (PYTHON), which has `cryptography`: no openssl.
    set(tls_dir "${CMAKE_CURRENT_BINARY_DIR}/tls")
    set(cert_pem "${tls_dir}/cert.pem")
    set(key_pem "${tls_dir}/key.pem")
    add_custom_command(
        OUTPUT "${cert_pem}" "${key_pem}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${tls_dir}"
        COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../scripts/make_cert.py"
                "${cert_pem}" "${key_pem}" 192.168.4.1
        COMMENT "generating the board's self-signed certificate"
        VERBATIM)
    set_source_files_properties("${cert_pem}" "${key_pem}" PROPERTIES GENERATED TRUE)
    # Explicit, so the embed steps below cannot run before the files exist.
    add_custom_target(prism_tls_certificate DEPENDS "${cert_pem}" "${key_pem}")
    list(APPEND embed_text_files "${cert_pem}" "${key_pem}")
endif()

idf_component_register(
    SRCS ${srcs}
    EMBED_FILES ${embed_files}
    EMBED_TXTFILES ${embed_text_files}
    # bt and nvs_flash are for btspike only. They are listed for every app: the build's first pass
    # (early expansion) does not see PRISM_APP, so a condition here is silently false. With
    # CONFIG_BT_ENABLED off (every other app) bt has no sources.
    # prism_bt is the headset device (components/prism_bt): the tone app uses it, and with
    # Bluetooth off nothing references its archive, so it is built and never linked.
    # The network components are the crdt firmware's, listed for every app for the same reason;
    # they compile without its Kconfig options, and another app never links them.
    REQUIRES prism esp_timer esp_psram freertos korvo1 bt nvs_flash prism_bt
             esp_wifi esp_netif esp_event esp_http_server esp_https_server
)

# The certificate is a custom command's output: the component has to be told, or the embed step and
# the link can be scheduled before the files exist.
if(CONFIG_PRISM_APP_CRDT)
    add_dependencies(${COMPONENT_LIB} prism_tls_certificate)
    # libmain comes before libyrs on the link line, so the realpath stub would be skipped before
    # Rust's std asks for it: mark it needed from the start.
    target_link_libraries(${COMPONENT_LIB} INTERFACE "-Wl,-u,realpath")
endif()
