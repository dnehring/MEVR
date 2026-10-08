cmake_minimum_required(VERSION 3.27)
project(MEVR LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
    message(FATAL_ERROR "MEVR builds with MSVC only (LExSDKv2 requires it).")
endif ()

# One source tree, one ASI per game (docs/trilogy-design.md). LE1 first.
option(MEVR_BUILD_LE1 "Build LE1MEVR.asi" ON)
option(MEVR_BUILD_LE2 "Build LE2MEVR.asi" OFF)
option(MEVR_BUILD_LE3 "Build LE3MEVR.asi" OFF)

# LExSDKv2 honors these as normal variables (CMP0077).
set(LESDK_WITH_LE1 ${MEVR_BUILD_LE1})
set(LESDK_WITH_LE2 ${MEVR_BUILD_LE2})
set(LESDK_WITH_LE3 ${MEVR_BUILD_LE3})
set(LESDK_WITH_LEL OFF)
set(LESDK_WITH_SAMPLES OFF)
set(LESDK_WITH_TESTS OFF)
add_subdirectory(external/LExSDKv2)

set(MEVR_SOURCES
    src/core/Log.cpp
    src/core/Log.hpp
    src/core/Version.cpp
    src/core/Version.hpp
    src/engine/Engine.cpp
    src/engine/Engine.hpp
    src/plugin/Entry.cpp
)

function(mevr_add_plugin GAME)
    set(target MEVR-${GAME})
    add_library(${target} SHARED ${MEVR_SOURCES})
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${target} PRIVATE LESDK-${GAME} version)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    # Same warning policy as the SDK's own consumers; the generated headers need /permissive.
    target_compile_options(${target} PRIVATE /W4 /WX /permissive /utf-8 /Zc:__cplusplus /Zi
        /wd4121 /wd4127 /wd4702)
    target_link_options(${target} PRIVATE /DEBUG)
    set_target_properties(${target} PROPERTIES OUTPUT_NAME "${GAME}MEVR" SUFFIX ".asi")
endfunction()

foreach (game LE1 LE2 LE3)
    if (MEVR_BUILD_${game})
        mevr_add_plugin(${game})
    endif ()
endforeach ()
