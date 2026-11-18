# SPDX-License-Identifier: MIT
# The prebuilt cache's own project (tools/cmake/ThirdParty.cmake): one group of the third-party libraries, compiled with
# the main project's settings and flags (it passes them in), in a cache entry's folder. Run by the main configure, never
# by hand:  -DMEVR_TP_ROOT=<repository> -DMEVR_TP_GROUP=base|lesdk -DMEVR_TP_MENU=ON|OFF -DMEVR_TP_GAMES=LE1,LE2,LE3
# It exports the libraries' targets with their usage requirements (mevr-targets-*.cmake), records the folders each
# submodule was compiled from and the toolchain it got (mevr-sources.cmake), and lists the libraries it builds
# (mevr-files.txt).
cmake_minimum_required(VERSION 3.27)  # the main project's, so the same policies
project(MEVR-thirdparty LANGUAGES C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../ThirdParty.cmake)
if (NOT MEVR_TP_ROOT OR NOT MEVR_TP_GROUP)
    message(FATAL_ERROR "tools/cmake/thirdparty is configured by the main project's tools/cmake/ThirdParty.cmake")
endif ()
string(REPLACE "," ";" MEVR_TP_GAMES "${MEVR_TP_GAMES}")
foreach (game LE1 LE2 LE3)
    if (game IN_LIST MEVR_TP_GAMES)
        set(LESDK_WITH_${game} ON)
    else ()
        set(LESDK_WITH_${game} OFF)
    endif ()
endforeach ()
mevr_third_party_group_info(${MEVR_TP_GROUP} ${MEVR_TP_MENU} modules targets)

# Each submodule from the folder it really is (a lane's external\ folders are junctions to the main checkout's), so the
# libraries' debug information doesn't name a worktree that is pruned later.
set(sources "")
foreach (module IN LISTS modules)
    file(REAL_PATH "${MEVR_TP_ROOT}/external/${module}" MEVR_TP_DIR_${module})
    list(APPEND sources "${module}=${MEVR_TP_DIR_${module}}")
endforeach ()

if (MEVR_TP_GROUP STREQUAL "lesdk")
    mevr_third_party_add_lesdk()
    export(TARGETS ${targets} FILE ${CMAKE_BINARY_DIR}/mevr-targets-lesdk.cmake)
else ()
    mevr_third_party_add_base(${MEVR_TP_MENU})
    export(TARGETS openxr_loader headers NAMESPACE OpenXR:: FILE ${CMAKE_BINARY_DIR}/mevr-targets-openxr.cmake)
    if (MEVR_TP_MENU)
        set_target_properties(rmlui_core PROPERTIES EXPORT_NAME rmlui_core)  # RmlUi's own export name is "Core"
        export(TARGETS rmlui_core freetype FILE ${CMAKE_BINARY_DIR}/mevr-targets-ui.cmake)
    endif ()
endif ()
# What the main configure builds (the base group's libraries are EXCLUDE_FROM_ALL, as in the main project, so the
# solution names them only through a target that depends on them), and the libraries that build must leave (the main
# configure checks them; the entry's folder was empty before it).
add_custom_target(mevr-thirdparty ALL)
add_dependencies(mevr-thirdparty ${targets})
set(files "")
foreach (target IN LISTS targets)
    list(APPEND files "$<TARGET_FILE:${target}>")
endforeach ()
file(GENERATE OUTPUT ${CMAKE_BINARY_DIR}/mevr-files.txt CONTENT "$<JOIN:${files},\n>\n")

file(WRITE ${CMAKE_BINARY_DIR}/mevr-sources.cmake
    "set(MEVR_TP_SOURCES \"${sources}\")\n"
    "set(MEVR_TP_TOOLCHAIN \"${CMAKE_C_COMPILER}|${CMAKE_CXX_COMPILER}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}|${CMAKE_VS_PLATFORM_TOOLSET}\")\n")
