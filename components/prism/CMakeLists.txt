# PRISM, built by its own Meson build (subprojects/prism) with ESP-IDF's compiler and flags, and
# imported as static libraries: prism::core, prism::raster, prism::platform, prism::embedded_apps.
#
# Meson is set up at CMake configure time, so its introspection names the libraries and the
# include directories here (no hand-kept list of PRISM's files or of its subprojects' paths);
# ExternalProject runs ninja at build time.

if(CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_register()
    return()
endif()

idf_component_register()

include(ExternalProject)

set(PRISM_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../subprojects/prism")
set(PRISM_MESON_DIR "${CMAKE_BINARY_DIR}/prism-meson")
set(PRISM_CROSS_FILE "${CMAKE_BINARY_DIR}/prism-meson-cross.ini")
option(PRISM_PROFILING "PRISM's profile marks and global recorder (-Dprofiling=enabled)" OFF)
option(PRISM_CRDT "PRISM's crdt layer (-Dcrdt=enabled)" OFF)

# At this level, not inside a function: Meson's options come from here.
if(PRISM_CRDT)
    set(PRISM_MESON_CRDT "-Dcrdt=enabled")
else()
    set(PRISM_MESON_CRDT "-Dcrdt=disabled")   # crdt is auto (on) in PRISM: skip building it
endif()

find_program(MESON_EXECUTABLE meson REQUIRED)
find_program(NINJA_EXECUTABLE ninja REQUIRED)

# Flags only Meson's own options should set (warnings, language standard), or that point into
# this source tree; anything in a generator expression cannot be evaluated here.
set(_prism_dropped_flags "^-W" "^-std=" "^-fmacro-prefix-map=" "\\$<")

function(prism_kept_flags out)
    set(kept "")
    foreach(flag IN LISTS ARGN)
        set(drop FALSE)
        foreach(pattern IN LISTS _prism_dropped_flags)
            if(flag MATCHES "${pattern}")
                set(drop TRUE)
            endif()
        endforeach()
        if(NOT drop)
            list(APPEND kept "${flag}")
        endif()
    endforeach()
    set(${out} "${kept}" PARENT_SCOPE)
endfunction()

# IDF_VER="..." and the like are left out: a string define says nothing about the ABI.
function(prism_definitions_as_flags out)
    set(flags "")
    foreach(def IN LISTS ARGN)
        if(NOT def MATCHES "\"")
            list(APPEND flags "-D${def}")
        endif()
    endforeach()
    set(${out} "${flags}" PARENT_SCOPE)
endfunction()

function(prism_meson_array out)
    list(TRANSFORM ARGN PREPEND "'")
    list(TRANSFORM ARGN APPEND "'")
    list(JOIN ARGN ", " joined)
    set(${out} "${joined}" PARENT_SCOPE)
endfunction()

# CMAKE_C_FLAGS is @"<build>/toolchain/cflags", ESP-IDF's response file: -march, -mabi, -mtune,
# the C library's specs. Passed on as is, so PRISM compiles with the file IDF compiles with.
function(prism_response_file out flags)
    string(REPLACE "\"" "" flag "${flags}")
    set(${out} "${flag}" PARENT_SCOPE)
endfunction()

function(prism_write_cross_file)
    idf_build_get_property(options COMPILE_OPTIONS)
    idf_build_get_property(c_options C_COMPILE_OPTIONS)
    idf_build_get_property(cxx_options CXX_COMPILE_OPTIONS)
    idf_build_get_property(definitions COMPILE_DEFINITIONS)
    idf_build_get_property(target IDF_TARGET)
    prism_response_file(c_response "${CMAKE_C_FLAGS}")
    prism_response_file(cxx_response "${CMAKE_CXX_FLAGS}")
    prism_definitions_as_flags(defines ${definitions})
    prism_kept_flags(common ${options} ${defines})
    prism_kept_flags(c_only ${c_options})
    prism_kept_flags(cxx_only ${cxx_options})

    set(PRISM_WRAPPER "${CMAKE_CURRENT_LIST_DIR}/no-pthread")
    set(PRISM_CC "${CMAKE_C_COMPILER}")
    set(PRISM_CXX "${CMAKE_CXX_COMPILER}")
    set(PRISM_AR "${CMAKE_AR}")
    string(REGEX REPLACE "gcc$" "strip" PRISM_STRIP "${CMAKE_C_COMPILER}")
    set(IDF_TARGET "${target}")
    prism_meson_array(PRISM_C_ARGS ${c_response} ${common} ${c_only})
    prism_meson_array(PRISM_CPP_ARGS ${cxx_response} ${common} ${cxx_only})
    prism_meson_array(PRISM_LINK_ARGS ${cxx_response})
    configure_file("${CMAKE_CURRENT_LIST_DIR}/meson-cross.ini.in" "${PRISM_CROSS_FILE}" @ONLY)
endfunction()

function(prism_profiling_option out)
    if(PRISM_PROFILING)
        set(${out} -Dprofiling=enabled -Dprofiling_events=4096 -Dprofiling_threads=8 PARENT_SCOPE)
    else()
        set(${out} -Dprofiling=disabled PARENT_SCOPE)
    endif()
endfunction()

# The cross file and the contents of the response files it names: a new ESP-IDF can change
# -march in those files while the cross file stays the same.
function(prism_cross_hash out)
    file(READ "${PRISM_CROSS_FILE}" content)
    foreach(flags IN ITEMS "${CMAKE_C_FLAGS}" "${CMAKE_CXX_FLAGS}")
        prism_response_file(response "${flags}")
        string(REGEX REPLACE "^@" "" path "${response}")
        file(READ "${path}" response_content)
        string(APPEND content "${response_content}")
    endforeach()
    string(SHA256 hash "${content}")
    set(${out} "${hash}" PARENT_SCOPE)
endfunction()

# Reconfiguring keeps a Meson build directory's first cross file: a new one needs a wipe.
function(prism_meson_setup)
    prism_profiling_option(profiling)
    prism_cross_hash(cross_hash)
    set(setup_args
        --cross-file "${PRISM_CROSS_FILE}"
        --buildtype=plain -Dwerror=false
        -Dsdl=disabled -Dio=disabled -Dtests=disabled -Dfreertos_port=disabled
        -Dprism_debug_tools=disabled ${PRISM_MESON_CRDT} ${profiling})
    if(EXISTS "${PRISM_MESON_DIR}/meson-private/coredata.dat")
        if(cross_hash STREQUAL PRISM_MESON_CROSS_HASH)
            list(PREPEND setup_args --reconfigure)
        else()
            list(PREPEND setup_args --wipe)
        endif()
    endif()
    execute_process(
        COMMAND "${MESON_EXECUTABLE}" setup ${setup_args} "${PRISM_MESON_DIR}" "${PRISM_SOURCE_DIR}"
        RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "meson setup of PRISM failed. Read ${PRISM_MESON_DIR}/meson-logs/meson-log.txt; "
                            "if it is a stale build directory, delete ${PRISM_MESON_DIR} and build again")
    endif()
    set(PRISM_MESON_CROSS_HASH "${cross_hash}" CACHE INTERNAL "")
endfunction()

function(prism_introspect_targets out)
    execute_process(
        COMMAND "${MESON_EXECUTABLE}" introspect --targets "${PRISM_MESON_DIR}"
        OUTPUT_VARIABLE json RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "meson introspect --targets ${PRISM_MESON_DIR} failed: delete "
                            "${PRISM_MESON_DIR} and build again, so Meson sets it up from scratch")
    endif()
    set(${out} "${json}" PARENT_SCOPE)
endfunction()

# Every static library the Meson build makes: their names in `out_names`, each one's file in
# PRISM_LIB_<name>.
function(prism_static_libraries json out_names)
    string(JSON count LENGTH "${json}")
    math(EXPR last "${count} - 1")
    set(names "")
    foreach(i RANGE ${last})
        string(JSON type GET "${json}" ${i} type)
        if(type STREQUAL "static library")
            string(JSON name GET "${json}" ${i} name)
            string(JSON file GET "${json}" ${i} filename 0)
            set(PRISM_LIB_${name} "${file}" PARENT_SCOPE)
            list(APPEND names "${name}")
        endif()
    endforeach()
    set(${out_names} "${names}" PARENT_SCOPE)
endfunction()

function(prism_target_parameters json target_name out)
    string(JSON count LENGTH "${json}")
    math(EXPR last "${count} - 1")
    foreach(i RANGE ${last})
        string(JSON name GET "${json}" ${i} name)
        if(name STREQUAL target_name)
            string(JSON params GET "${json}" ${i} target_sources 0 parameters)
            set(${out} "${params}" PARENT_SCOPE)
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "PRISM's Meson build has no target ${target_name}: subprojects/prism is "
                        "too old (it needs examples/embedded); update it with git submodule update")
endfunction()

# What a file including PRISM's headers needs: the include directories, -freflection and the
# defines (-DPRISM_PROFILING, -DFMT_HEADER_ONLY, -DNDEBUG...), so it reads the headers as the
# libraries were compiled with them. Taken from the compile line of the embedded apps'
# library, which sees PRISM's core and every header-only subproject.
function(prism_usage_flags json out_includes out_options)
    prism_target_parameters("${json}" prism_embedded_apps params)
    string(JSON n LENGTH "${params}")
    math(EXPR last "${n} - 1")
    set(includes "")
    set(options "")
    foreach(j RANGE ${last})
        string(JSON p GET "${params}" ${j})
        if(p MATCHES "^-(I|isystem)(.+)$")
            get_filename_component(dir "${CMAKE_MATCH_2}" ABSOLUTE BASE_DIR "${PRISM_MESON_DIR}")
            if(IS_DIRECTORY "${dir}")
                list(APPEND includes "${dir}")
            endif()
        elseif(p MATCHES "^-D")
            list(APPEND options "${p}")
        elseif(p STREQUAL "-freflection")
            list(APPEND options "$<$<COMPILE_LANGUAGE:CXX>:-freflection>")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES includes)
    list(REMOVE_DUPLICATES options)
    set(${out_includes} "${includes}" PARENT_SCOPE)
    set(${out_options} "${options}" PARENT_SCOPE)
endfunction()

function(prism_import name file)
    add_library(${name} STATIC IMPORTED GLOBAL)
    set_target_properties(${name} PROPERTIES IMPORTED_LOCATION "${file}")
    add_dependencies(${name} prism_meson)
endfunction()

prism_write_cross_file()
prism_meson_setup()
prism_introspect_targets(_targets)
prism_static_libraries("${_targets}" _libs)
prism_usage_flags("${_targets}" _includes _options)
message(STATUS "PRISM libraries: ${_libs}")
message(STATUS "PRISM include directories: ${_includes}")
message(STATUS "PRISM usage flags: ${_options}")

set(_byproducts "")
foreach(lib IN LISTS _libs)
    list(APPEND _byproducts "${PRISM_LIB_${lib}}")
endforeach()

# The crdt layer, when it is on: its libraries are static libraries like the rest, so they are
# imported above, but Meson's introspection order puts a subproject's library before the one that
# needs it (libycpp.a before libprism-crdt.a). Repeating the two in a group at the end of the link
# line lets the resolver see either order.
set(_prism_crdt_libs "")
if(PRISM_CRDT)
    if(NOT "prism-crdt" IN_LIST _libs OR NOT "ycpp" IN_LIST _libs)
        message(FATAL_ERROR "PRISM was configured with PRISM_CRDT but its Meson build has no "
                            "prism-crdt and ycpp libraries: subprojects/prism is too old for the crdt "
                            "layer, or -Dcrdt=enabled was refused "
                            "(read ${PRISM_MESON_DIR}/meson-logs/meson-log.txt)")
    endif()
    set(_prism_crdt_libs "${PRISM_LIB_prism-crdt}" "${PRISM_LIB_ycpp}")
endif()

ExternalProject_Add(prism_meson
    SOURCE_DIR "${PRISM_SOURCE_DIR}"
    BINARY_DIR "${PRISM_MESON_DIR}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${NINJA_EXECUTABLE}" -C "${PRISM_MESON_DIR}"
    INSTALL_COMMAND ""
    BUILD_ALWAYS TRUE
    USES_TERMINAL_BUILD TRUE
    BUILD_BYPRODUCTS ${_byproducts})

# PRISM's own four libraries by name; every other one (CMSIS-DSP, libunibreak, SheenBidi..., and
# the crdt layer's prism-crdt and ycpp) is linked after them.
set(_own prism prism-raster prism-platform prism_embedded_apps)
add_library(prism_third_party INTERFACE)
foreach(lib IN LISTS _libs)
    if(NOT lib IN_LIST _own)
        prism_import(prism::lib::${lib} "${PRISM_LIB_${lib}}")
        target_link_libraries(prism_third_party INTERFACE prism::lib::${lib})
    endif()
endforeach()

prism_import(prism::core "${PRISM_LIB_prism}")
target_include_directories(prism::core INTERFACE ${_includes})
target_compile_options(prism::core INTERFACE ${_options})
target_link_libraries(prism::core INTERFACE prism_third_party)

prism_import(prism::raster "${PRISM_LIB_prism-raster}")
target_link_libraries(prism::raster INTERFACE prism::core)

prism_import(prism::platform "${PRISM_LIB_prism-platform}")
target_link_libraries(prism::platform INTERFACE prism::raster)

prism_import(prism::embedded_apps "${PRISM_LIB_prism_embedded_apps}")
target_link_libraries(prism::embedded_apps INTERFACE prism::core)

target_link_libraries(${COMPONENT_LIB} INTERFACE prism::platform prism::embedded_apps)

# The crdt libraries are in the imported libraries above, but the group at the end of the link line
# repeats them so their own order does not matter.
if(_prism_crdt_libs)
    target_link_libraries(${COMPONENT_LIB} INTERFACE
        "-Wl,--start-group" ${_prism_crdt_libs} "-Wl,--end-group")
endif()
