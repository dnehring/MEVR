# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.27)
project(mevr_le2_observation_tests LANGUAGES CXX)
set(repo "${CMAKE_CURRENT_LIST_DIR}/../..")
set(MEVR_OBSERVATION_SDK_DIR "${repo}/external/LExSDKv2" CACHE PATH "Initialized project LExSDKv2 submodule (declarations only)")
if(NOT MSVC)
    message(FATAL_ERROR "LE2 SDK declarations and the SEH read boundary require MSVC/Windows")
endif()
if(NOT EXISTS "${MEVR_OBSERVATION_SDK_DIR}/Src/LESDK/Headers.hpp")
    message(FATAL_ERROR "Initialize the pinned external/LExSDKv2 submodule or supply MEVR_OBSERVATION_SDK_DIR")
endif()
# This SDK emits EInterruptionType as commented declarations, not C++ enums.
# Fail this actual-SDK check if its named polarity values change.
file(READ "${MEVR_OBSERVATION_SDK_DIR}/Src/LESDK/LE2/SFXGame_classes.hpp" dialogue_sdk)
if(NOT dialogue_sdk MATCHES "INTERRUPTION_RENEGADE[ \t]*=[ \t]*0," OR
   NOT dialogue_sdk MATCHES "INTERRUPTION_PARAGON[ \t]*=[ \t]*1,")
    message(FATAL_ERROR "LE2 SDK interrupt enum declaration differs from observer mapping")
endif()
add_executable(le2-observation-tests ObservationTests.cpp
    "${repo}/src/engine/Le2DiagnosticObservation.cpp" "${repo}/src/core/Json.cpp" "${repo}/src/core/qa/BoundedUtf16.cpp")
target_compile_features(le2-observation-tests PRIVATE cxx_std_20)
target_compile_definitions(le2-observation-tests PRIVATE MEVR_GAME_LE2 SDK_TARGET_LE2 NOMINMAX _CRT_SECURE_NO_WARNINGS MEVR_LE2_OBSERVATION_TEST)
target_include_directories(le2-observation-tests PRIVATE "${repo}/src")
target_include_directories(le2-observation-tests SYSTEM PRIVATE
    "${MEVR_OBSERVATION_SDK_DIR}/Src" "${MEVR_OBSERVATION_SDK_DIR}/External")
target_precompile_headers(le2-observation-tests PRIVATE "${MEVR_OBSERVATION_SDK_DIR}/Src/LESDK/_Global.pch.hpp")
target_compile_options(le2-observation-tests PRIVATE /W4 /WX /bigobj /permissive /wd4121 /wd26495)

# Compile the fixed production virtual route too; never link or execute it here.
add_library(le2-observation-production OBJECT "${repo}/src/engine/Le2DiagnosticObservation.cpp")
target_compile_features(le2-observation-production PRIVATE cxx_std_20)
target_compile_definitions(le2-observation-production PRIVATE MEVR_GAME_LE2 SDK_TARGET_LE2 NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_include_directories(le2-observation-production PRIVATE "${repo}/src")
target_include_directories(le2-observation-production SYSTEM PRIVATE
    "${MEVR_OBSERVATION_SDK_DIR}/Src" "${MEVR_OBSERVATION_SDK_DIR}/External")
target_precompile_headers(le2-observation-production PRIVATE "${MEVR_OBSERVATION_SDK_DIR}/Src/LESDK/_Global.pch.hpp")
target_compile_options(le2-observation-production PRIVATE /W4 /WX /bigobj /permissive /wd4121 /wd26495)
enable_testing()
add_test(NAME le2-observation COMMAND le2-observation-tests)
