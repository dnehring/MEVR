cmake_minimum_required(VERSION 3.27)
project(MEVR LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
    message(FATAL_ERROR "MEVR builds with MSVC only (LExSDKv2 requires it).")
endif ()

# One source tree, one ASI per game (docs/trilogy-design.md). LE1 first.
option(MEVR_BUILD_LE1 "Build LE1MEVR.asi" ON)
option(MEVR_BUILD_LE2 "Build LE2MEVR.asi" OFF)
option(MEVR_BUILD_LE3 "Build LE3MEVR.asi" OFF)
# Development: a small stable host ASI plus <GAME>MEVR_mod.dll, which the host reloads whenever
# it changes, so the game can keep running between builds. Release: one self-contained ASI.
option(MEVR_HOT_RELOAD "Build the mod as a separate hot-reloadable DLL" ON)

# LExSDKv2 honors these as normal variables (CMP0077).
set(LESDK_WITH_LE1 ${MEVR_BUILD_LE1})
set(LESDK_WITH_LE2 ${MEVR_BUILD_LE2})
set(LESDK_WITH_LE3 ${MEVR_BUILD_LE3})
set(LESDK_WITH_LEL OFF)
set(LESDK_WITH_SAMPLES OFF)
set(LESDK_WITH_TESTS OFF)
add_subdirectory(external/LExSDKv2)

# Khronos OpenXR loader (Apache-2.0), linked statically into the mod.
set(DYNAMIC_LOADER OFF)
set(BUILD_API_LAYERS OFF)
set(BUILD_TESTS OFF)
set(BUILD_CONFORMANCE_TESTS OFF)
set(BUILD_SDK_TESTS OFF)
set(BUILD_ALL_EXTENSIONS OFF)
add_subdirectory(external/OpenXR-SDK EXCLUDE_FROM_ALL)

# Halcyon's LE1 VR renderer (GPL-3.0, src/halcyon/le1) with its vendored MinHook and Dear ImGui.
# It runs inside the LE1 host: OpenXR session, Present path, stereo, first person, HUD, menu.
option(MEVR_HALCYON "Use Halcyon's renderer for LE1" ON)
set(HALCYON_LE1_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/halcyon/le1)
add_library(halcyon-le1 STATIC
    ${HALCYON_LE1_DIR}/d3d_capture.cpp
    ${HALCYON_LE1_DIR}/dxgi_proxy.cpp
    ${HALCYON_LE1_DIR}/head_aim.cpp
    ${HALCYON_LE1_DIR}/logger.cpp
    ${HALCYON_LE1_DIR}/pchud.cpp
    ${HALCYON_LE1_DIR}/render_hook.cpp
    ${HALCYON_LE1_DIR}/vr_config.cpp
    ${HALCYON_LE1_DIR}/vr_menu.cpp
    ${HALCYON_LE1_DIR}/xr_session.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_dx11.cpp
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hde/hde64.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
)
target_include_directories(halcyon-le1 PUBLIC
    ${HALCYON_LE1_DIR}
    third_party/minhook/include
    third_party/imgui
    third_party/imgui/backends
)
# Upstream's settings (MELEVRClean.vcxproj): C++17, Unicode. MEVR_ASI selects our changes.
set_target_properties(halcyon-le1 PROPERTIES CXX_STANDARD 17)
target_compile_definitions(halcyon-le1 PRIVATE WIN32 _WINDOWS UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    MELEVR_EXPORTS MEVR_ASI)
target_compile_options(halcyon-le1 PRIVATE /W3 /utf-8 /Zi)
target_link_libraries(halcyon-le1 PUBLIC d3d11 dxgi Shell32 Ole32)

set(MEVR_CORE_SOURCES
    src/core/Log.cpp
    src/core/Log.hpp
    src/core/Window.cpp
    src/core/Window.hpp
    src/core/Version.cpp
    src/core/Version.hpp
)

set(MEVR_HOST_SOURCES
    src/mod/ModApi.hpp
    src/plugin/Entry.cpp
    src/plugin/ModLoader.cpp
    src/plugin/ModLoader.hpp
    src/plugin/PresentHook.cpp
    src/plugin/PresentHook.hpp
    src/plugin/ProcessEventHook.cpp
    src/plugin/ProcessEventHook.hpp
)

set(MEVR_MOD_SOURCES
    src/core/Hotkey.cpp
    src/core/Hotkey.hpp
    src/core/d3d11/Blit.cpp
    src/core/d3d11/Blit.hpp
    src/core/ik/BoneNames.cpp
    src/core/ik/BoneNames.hpp
    src/core/ik/Ik.cpp
    src/core/ik/Ik.hpp
    src/core/vr/Controllers.cpp
    src/core/vr/Controllers.hpp
    src/core/vr/Output.cpp
    src/core/vr/Output.hpp
    src/core/vr/ViewSync.cpp
    src/core/vr/ViewSync.hpp
    src/core/xr/Input.cpp
    src/core/xr/Input.hpp
    src/core/xr/Session.cpp
    src/core/xr/Session.hpp
    src/engine/CallFunction.cpp
    src/engine/CallFunction.hpp
    src/engine/CodeDump.cpp
    src/engine/CodeDump.hpp
    src/engine/Engine.cpp
    src/engine/Engine.hpp
    src/engine/GameAdapter.hpp
    src/engine/Objects.cpp
    src/engine/Objects.hpp
    src/engine/ProcessEvent.cpp
    src/engine/ProcessEvent.hpp
    src/engine/SceneView.cpp
    src/engine/SceneView.hpp
    src/engine/Skeleton.cpp
    src/engine/Skeleton.hpp
    src/engine/XrToUnreal.cpp
    src/engine/XrToUnreal.hpp
    src/game/ArmIk.cpp
    src/game/ArmIk.hpp
    src/game/GamepadBridge.cpp
    src/game/GamepadBridge.hpp
    src/game/GamepadMapping.cpp
    src/game/GamepadMapping.hpp
    src/game/HandAim.cpp
    src/game/HandAim.hpp
    src/game/HeadSim.cpp
    src/game/HeadSim.hpp
    src/game/HeadTracking.cpp
    src/game/HeadTracking.hpp
    src/game/Reflections.cpp
    src/game/Reflections.hpp
    src/game/SkeletonProbe.cpp
    src/game/SkeletonProbe.hpp
    src/mod/Mod.cpp
    src/mod/ModApi.hpp
)

function(mevr_configure target game)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${target} PRIVATE LESDK-${game} version d3d11 dxgi)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
    # Same warning policy as the SDK's own consumers; the generated headers need /permissive.
    target_compile_options(${target} PRIVATE /W4 /WX /permissive /utf-8 /Zc:__cplusplus /Zi
        /wd4121 /wd4127 /wd4702)
    target_link_options(${target} PRIVATE /DEBUG)
endfunction()

# The LE1 host's side of Halcyon's renderer: its startup, and our seams into it (OpenXR loader,
# controller actions, motion controllers as pad 0).
set(MEVR_HALCYON_HOST_SOURCES
    src/core/vr/Controllers.cpp
    src/core/vr/Controllers.hpp
    src/core/xr/Input.cpp
    src/core/xr/Input.hpp
    src/game/GamepadMapping.cpp
    src/game/GamepadMapping.hpp
    src/halcyon/le1/mevr_bridge.h
    src/plugin/Halcyon.cpp
    src/plugin/Halcyon.hpp
    src/plugin/HalcyonBridge.cpp
)

function(mevr_add_plugin game)
    set(host MEVR-${game})
    set(host_sources ${MEVR_HOST_SOURCES} ${MEVR_CORE_SOURCES})
    set(with_halcyon OFF)
    if (MEVR_HALCYON AND game STREQUAL "LE1")
        set(with_halcyon ON)
        list(APPEND host_sources ${MEVR_HALCYON_HOST_SOURCES})
    endif ()
    if (MEVR_HOT_RELOAD)
        add_library(${host} SHARED ${host_sources})

        set(mod MEVR-${game}-mod)
        add_library(${mod} SHARED ${MEVR_MOD_SOURCES} ${MEVR_CORE_SOURCES})
        mevr_configure(${mod} ${game})
        target_link_libraries(${mod} PRIVATE OpenXR::openxr_loader d3dcompiler)
        target_compile_definitions(${mod} PRIVATE MEVR_MOD_DLL)
        set_target_properties(${mod} PROPERTIES OUTPUT_NAME "${game}MEVR_mod")
        # Building the host target builds the mod too.
        add_dependencies(${host} ${mod})
    else ()
        set(all_sources ${host_sources} ${MEVR_MOD_SOURCES})
        list(REMOVE_DUPLICATES all_sources)
        add_library(${host} SHARED ${all_sources})
        target_link_libraries(${host} PRIVATE OpenXR::openxr_loader d3dcompiler)
        target_compile_definitions(${host} PRIVATE MEVR_MONOLITHIC)
    endif ()
    if (with_halcyon)
        target_link_libraries(${host} PRIVATE halcyon-le1 OpenXR::openxr_loader)
        target_compile_definitions(${host} PRIVATE MEVR_WITH_HALCYON)
    endif ()
    mevr_configure(${host} ${game})
    set_target_properties(${host} PROPERTIES OUTPUT_NAME "${game}MEVR" SUFFIX ".asi")
endfunction()

foreach (game LE1 LE2 LE3)
    if (MEVR_BUILD_${game})
        mevr_add_plugin(${game})
    endif ()
endforeach ()

# Offline tests for game-independent math (no game, no runtime needed).
option(MEVR_BUILD_TESTS "Build the offline tests" ON)
if (MEVR_BUILD_TESTS)
    enable_testing()
    add_executable(mevr-tests
        tests/Main.cpp
        src/core/ik/BoneNames.cpp
        src/core/ik/Ik.cpp
        src/core/vr/ViewSync.cpp
        src/engine/XrToUnreal.cpp
        src/game/GamepadMapping.cpp
        src/game/HeadSim.cpp
    )
    target_include_directories(mevr-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-tests PRIVATE OpenXR::headers)
    target_compile_definitions(mevr-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-tests PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    add_test(NAME mevr-tests COMMAND mevr-tests)
endif ()
