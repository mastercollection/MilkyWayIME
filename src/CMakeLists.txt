set(MWIME_CORE_SOURCES
    engine/layout/layout_registry.cpp
    engine/shortcut/shortcut_resolver.cpp
    engine/session/input_session.cpp
    adapters/libhangul/libhangul_composer_stub.cpp
)

add_library(mwime_core STATIC ${MWIME_CORE_SOURCES})
target_include_directories(mwime_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_features(mwime_core PUBLIC cxx_std_20)

add_library(mwime_tsf SHARED
    tsf/exports.cpp
    tsf/service/text_service.cpp
)
target_include_directories(mwime_tsf PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(mwime_tsf PRIVATE mwime_core)
target_compile_features(mwime_tsf PRIVATE cxx_std_20)

function(mwime_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

mwime_enable_warnings(mwime_core)
mwime_enable_warnings(mwime_tsf)
