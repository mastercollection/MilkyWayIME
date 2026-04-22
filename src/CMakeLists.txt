include(CheckIncludeFiles)

set(MWIME_LIBHANGUL_SOURCE_DIR "${PROJECT_SOURCE_DIR}/external/libhangul")
set(MWIME_LIBHANGUL_BINARY_DIR "${PROJECT_BINARY_DIR}/generated/libhangul")

check_include_files("glob.h" HAVE_GLOB_H)
file(MAKE_DIRECTORY "${MWIME_LIBHANGUL_BINARY_DIR}")
configure_file(
    "${MWIME_LIBHANGUL_SOURCE_DIR}/config.h.cmake.in"
    "${MWIME_LIBHANGUL_BINARY_DIR}/config.h"
)

add_library(mwime_libhangul STATIC
    "${MWIME_LIBHANGUL_SOURCE_DIR}/hangul/hangulctype.c"
    "${MWIME_LIBHANGUL_SOURCE_DIR}/hangul/hangulinputcontext.c"
    "${MWIME_LIBHANGUL_SOURCE_DIR}/hangul/hangulkeyboard.c"
    "${MWIME_LIBHANGUL_SOURCE_DIR}/hangul/hanja.c"
)
target_include_directories(mwime_libhangul PRIVATE
    "${MWIME_LIBHANGUL_BINARY_DIR}"
    "${MWIME_LIBHANGUL_SOURCE_DIR}/hangul"
)
target_compile_definitions(mwime_libhangul PRIVATE
    HAVE_CONFIG_H
    LOCALEDIR=""
    LIBHANGUL_DEFAULT_HANJA_DIC="${MWIME_LIBHANGUL_SOURCE_DIR}/data/hanja/hanja.txt"
    LIBHANGUL_DATA_DIR="${MWIME_LIBHANGUL_SOURCE_DIR}/data"
    TOP_SRCDIR="${MWIME_LIBHANGUL_SOURCE_DIR}"
)
set_target_properties(mwime_libhangul PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

set(MWIME_CORE_SOURCES
    engine/layout/layout_registry.cpp
    engine/shortcut/shortcut_resolver.cpp
    engine/session/input_session.cpp
    adapters/libhangul/libhangul_composer.cpp
)

add_library(mwime_core STATIC ${MWIME_CORE_SOURCES})
target_include_directories(mwime_core
    PUBLIC "${PROJECT_SOURCE_DIR}/src"
    PRIVATE "${MWIME_LIBHANGUL_SOURCE_DIR}/hangul"
)
target_compile_features(mwime_core PUBLIC cxx_std_20)
target_link_libraries(mwime_core PUBLIC mwime_libhangul)

add_library(mwime_tsf_support STATIC
    tsf/edit/composition_edit_session.cpp
    tsf/service/text_service.cpp
)
target_include_directories(mwime_tsf_support PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mwime_tsf_support PRIVATE mwime_core)
target_compile_features(mwime_tsf_support PRIVATE cxx_std_20)

add_library(mwime_tsf SHARED
    tsf/exports.cpp
)
target_include_directories(mwime_tsf PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mwime_tsf PRIVATE mwime_tsf_support)
target_compile_features(mwime_tsf PRIVATE cxx_std_20)

function(mwime_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

mwime_enable_warnings(mwime_core)
mwime_enable_warnings(mwime_tsf_support)
mwime_enable_warnings(mwime_tsf)
