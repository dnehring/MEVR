# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.20)
project(mevr_le2_reload_core_tests LANGUAGES CXX)

# Deliberately standalone: no main project/runtime/SDK/hot-reload targets.
enable_testing()
add_executable(le2-reload-core-tests ReloadRequestTests.cpp)
target_compile_features(le2-reload-core-tests PRIVATE cxx_std_20)
target_include_directories(le2-reload-core-tests PRIVATE "${CMAKE_CURRENT_LIST_DIR}/../../src")
if(MSVC)
    target_compile_options(le2-reload-core-tests PRIVATE /W4 /WX)
else()
    target_compile_options(le2-reload-core-tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME le2-reload-core COMMAND le2-reload-core-tests)
