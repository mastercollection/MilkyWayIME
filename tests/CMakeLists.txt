add_executable(mwime_tests
    unit/input_session_tests.cpp
)
target_link_libraries(mwime_tests PRIVATE mwime_core)
target_compile_features(mwime_tests PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(mwime_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(mwime_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME mwime_tests COMMAND mwime_tests)
