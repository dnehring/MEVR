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

# The main menu's UI library (research/28): RmlUi (MIT) with its FreeType font engine (FreeType: FTL), both
# static, built only for what links them (the LE1 host and the menu preview). Their own warning levels.
option(MEVR_MAIN_MENU "Build our VR main menu (RmlUi) into the LE1 host" ON)
if (MEVR_MAIN_MENU)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
    add_subdirectory(external/freetype EXCLUDE_FROM_ALL)
    add_library(Freetype::Freetype ALIAS freetype)
    # RmlUi looks for FreeType with find_package; the target above is the one it links.
    set(CMAKE_DISABLE_FIND_PACKAGE_Freetype TRUE)
    set(RMLUI_FONT_ENGINE "freetype" CACHE STRING "" FORCE)
    set(RMLUI_SAMPLES OFF CACHE BOOL "" FORCE)
    set(RMLUI_LUA_BINDINGS OFF CACHE BOOL "" FORCE)
    set(RMLUI_SVG_PLUGIN OFF CACHE BOOL "" FORCE)
    set(RMLUI_LOTTIE_PLUGIN OFF CACHE BOOL "" FORCE)
    set(RMLUI_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
    add_subdirectory(external/RmlUi EXCLUDE_FROM_ALL)
    # RmlUi's own Direct3D 11 renderer (Backends, MIT), wrapped by src/plugin/menu/MenuRenderer for the game's device.
    add_library(rmlui-d3d11 STATIC external/RmlUi/Backends/RmlUi_Renderer_DX11.cpp)
    target_include_directories(rmlui-d3d11 PUBLIC external/RmlUi/Backends)
    target_compile_definitions(rmlui-d3d11 PUBLIC NUM_MSAA_SAMPLES=4 PRIVATE NOMINMAX)
    target_link_libraries(rmlui-d3d11 PUBLIC rmlui_core d3d11 d3dcompiler)
    target_compile_options(rmlui-d3d11 PRIVATE /W3 /utf-8)

    # Our side of the menu, game-independent (no Halcyon, no OpenXR): the renderer wrapper, RmlUi setup and
    # decorators, the data model, the frosted backdrop's passes, the colour preview strip. Linked by the LE1 host
    # and tools/menu-preview.
    add_library(mevr-menu STATIC
        src/core/vr/Colour.cpp
        src/core/vr/ColourStrip.cpp
        src/core/vr/MenuSettings.cpp
        src/plugin/menu/MenuBackdrop.cpp
        src/plugin/menu/MenuBackdrop.hpp
        src/plugin/menu/MenuDecorators.cpp
        src/plugin/menu/MenuDecorators.hpp
        src/plugin/menu/MenuModel.cpp
        src/plugin/menu/MenuModel.hpp
        src/plugin/menu/MenuRenderer.cpp
        src/plugin/menu/MenuRenderer.hpp
        src/plugin/menu/MenuStrip.cpp
        src/plugin/menu/MenuStrip.hpp
        src/plugin/menu/MenuUi.cpp
        src/plugin/menu/MenuUi.hpp
        src/plugin/menu/StateGuard.hpp
    )
    target_include_directories(mevr-menu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-menu PUBLIC rmlui-d3d11 rmlui_core d3d11 d3dcompiler)
    target_compile_definitions(mevr-menu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-menu PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zi)
endif ()

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
    src/core/Hook.cpp
    src/core/Hook.hpp
    src/core/Log.cpp
    src/core/Log.hpp
    src/core/Window.cpp
    src/core/Window.hpp
    src/core/Version.cpp
    src/core/Version.hpp
)

set(MEVR_HOST_SOURCES
    src/engine/CodeDump.cpp
    src/engine/CodeDump.hpp
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
    src/core/ik/ArmLength.cpp
    src/core/ik/ArmLength.hpp
    src/core/ik/ArmPose.cpp
    src/core/ik/ArmPose.hpp
    src/core/ik/HandFrame.cpp
    src/core/ik/HandFrame.hpp
    src/core/ik/BodySlots.cpp
    src/core/ik/BodySlots.hpp
    src/core/ik/BoneNames.cpp
    src/core/ik/BoneNames.hpp
    src/core/ik/CameraHook.cpp
    src/core/ik/CameraHook.hpp
    src/core/ik/Ik.cpp
    src/core/ik/Ik.hpp
    src/core/ik/Posture.cpp
    src/core/ik/Posture.hpp
    src/core/ik/Rigid.cpp
    src/core/ik/Rigid.hpp
    src/core/ik/BodyFollow.cpp
    src/core/ik/BodyFollow.hpp
    src/core/ik/TorsoPose.cpp
    src/core/ik/TorsoPose.hpp
    src/core/ik/TouchReference.cpp
    src/core/ik/TouchReference.hpp
    src/core/ik/Throwing.cpp
    src/core/ik/Throwing.hpp
    src/core/ik/TwoHand.cpp
    src/core/ik/TwoHand.hpp
    src/core/vr/AimRay.cpp
    src/core/vr/AimRay.hpp
    src/core/vr/Calibration.cpp
    src/core/vr/Calibration.hpp
    src/core/vr/Comfort.cpp
    src/core/vr/Comfort.hpp
    src/core/vr/Controllers.cpp
    src/core/vr/Controllers.hpp
    src/core/vr/Floor.hpp
    src/core/vr/HostView.cpp
    src/core/vr/HostView.hpp
    src/core/vr/Output.cpp
    src/core/vr/Output.hpp
    src/core/vr/OverlayPlacement.cpp
    src/core/vr/OverlayPlacement.hpp
    src/core/vr/Sights.cpp
    src/core/vr/Sights.hpp
    src/core/vr/SquadMenu.cpp
    src/core/vr/SquadMenu.hpp
    src/core/vr/TestRequest.cpp
    src/core/vr/TestRequest.hpp
    src/core/vr/ViewLevel.cpp
    src/core/vr/ViewLevel.hpp
    src/core/vr/ViewSync.cpp
    src/core/vr/ViewSync.hpp
    src/core/xr/Input.cpp
    src/core/xr/Input.hpp
    src/core/xr/Session.cpp
    src/core/xr/Session.hpp
    src/engine/BonePose.cpp
    src/engine/BonePose.hpp
    src/engine/CallFunction.cpp
    src/engine/CallFunction.hpp
    src/engine/CodeDump.cpp
    src/engine/CodeDump.hpp
    src/engine/CoverAcquire.cpp
    src/engine/CoverAcquire.hpp
    src/engine/Engine.cpp
    src/engine/Engine.hpp
    src/engine/GameAdapter.hpp
    src/engine/NativesDump.cpp
    src/engine/NativesDump.hpp
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
    src/game/BodyFollow.cpp
    src/game/BodyFollow.hpp
    src/game/BodyCalibration.cpp
    src/game/BodyCalibration.hpp
    src/game/Comfort.cpp
    src/game/Comfort.hpp
    src/game/CameraProbe.cpp
    src/game/CameraProbe.hpp
    src/game/ConsoleRequest.cpp
    src/game/ConsoleRequest.hpp
    src/game/GamepadBridge.cpp
    src/game/GamepadBridge.hpp
    src/game/GamepadMapping.cpp
    src/game/GamepadMapping.hpp
    src/game/HandAim.cpp
    src/game/HandCalibration.cpp
    src/game/HandCalibration.hpp
    src/game/HandAim.hpp
    src/game/HandTargets.cpp
    src/game/HandTargets.hpp
    src/game/HeadSim.cpp
    src/game/HeadSim.hpp
    src/game/HeadTracking.cpp
    src/game/HeadTracking.hpp
    src/game/Holsters.cpp
    src/game/Holsters.hpp
    src/game/MenuPause.cpp
    src/game/MenuPause.hpp
    src/game/Posture.cpp
    src/game/Posture.hpp
    src/game/Reflections.cpp
    src/game/Reflections.hpp
    src/game/Sights.cpp
    src/game/Sights.hpp
    src/game/SkeletonProbe.cpp
    src/game/SkeletonProbe.hpp
    src/game/SquadWeapons.cpp
    src/game/SquadWeapons.hpp
    src/game/SupportGrip.cpp
    src/game/SupportGrip.hpp
    src/game/Throwables.cpp
    src/game/Throwables.hpp
    src/game/ViewAnchor.cpp
    src/game/ViewAnchor.hpp
    src/game/ViewLevel.cpp
    src/game/ViewLevel.hpp
    src/game/Weapon.cpp
    src/game/Weapon.hpp
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
# controller actions, motion controllers as pad 0, its view for our controller mapping, controller
# aim in place of its head aim).
set(MEVR_HALCYON_HOST_SOURCES
    src/core/vr/AimRay.cpp
    src/core/vr/AimRay.hpp
    src/core/vr/Colour.cpp
    src/core/vr/Colour.hpp
    src/core/vr/ColourStrip.cpp
    src/core/vr/ColourStrip.hpp
    src/core/vr/Calibration.cpp
    src/core/vr/Calibration.hpp
    src/core/vr/Comfort.cpp
    src/core/vr/Comfort.hpp
    src/core/vr/Controllers.cpp
    src/core/vr/Controllers.hpp
    src/core/vr/Floor.hpp
    src/core/vr/FrameProbe.cpp
    src/core/vr/FrameProbe.hpp
    src/core/vr/Haptics.cpp
    src/core/vr/Haptics.hpp
    src/core/vr/HostView.cpp
    src/core/vr/HostView.hpp
    src/core/vr/ImageConvert.cpp
    src/core/vr/ImageConvert.hpp
    src/core/vr/LayerComposite.cpp
    src/core/vr/LayerComposite.hpp
    src/core/vr/MenuPointer.cpp
    src/core/vr/MenuPointer.hpp
    src/core/vr/MetricStereo.cpp
    src/core/vr/MetricStereo.hpp
    src/core/vr/OverlayPlacement.cpp
    src/core/vr/OverlayPlacement.hpp
    src/core/vr/PoseRecording.cpp
    src/core/vr/PoseRecording.hpp
    src/core/vr/Recenter.cpp
    src/core/vr/Recenter.hpp
    src/core/vr/Sights.cpp
    src/core/vr/Sights.hpp
    src/core/vr/SquadMenu.cpp
    src/core/vr/SquadMenu.hpp
    src/core/vr/TestRequest.cpp
    src/core/vr/TestRequest.hpp
    src/core/vr/WheelCrop.cpp
    src/core/vr/WheelCrop.hpp
    src/core/xr/Floor.cpp
    src/core/xr/Floor.hpp
    src/core/xr/Input.cpp
    src/core/xr/Input.hpp
    src/core/Hotkey.cpp
    src/core/Hotkey.hpp
    src/core/PngFile.cpp
    src/core/PngFile.hpp
    src/game/GamepadMapping.cpp
    src/game/GamepadMapping.hpp
    src/halcyon/le1/mevr_bridge.h
    src/plugin/Calibration.cpp
    src/plugin/Calibration.hpp
    src/plugin/Colour.cpp
    src/plugin/Colour.hpp
    src/plugin/CalibrationDraw.cpp
    src/plugin/CalibrationDraw.hpp
    src/plugin/DrawReplay.cpp
    src/plugin/DrawReplay.hpp
    src/plugin/Halcyon.cpp
    src/plugin/Halcyon.hpp
    src/plugin/HalcyonBridge.cpp
    src/plugin/HalcyonOverlays.cpp
    src/plugin/MevrMenu.hpp
    src/plugin/MevrMenuPointer.cpp
    src/plugin/MevrMenuTab.cpp
    src/plugin/MetricStereo.cpp
    src/plugin/MetricStereo.hpp
    src/plugin/PerfProbe.cpp
    src/plugin/PerfProbe.hpp
    src/plugin/RenderLevers.cpp
    src/plugin/RenderLevers.hpp
    src/plugin/RecenterTilt.cpp
    src/plugin/RecenterTilt.hpp
    src/plugin/SightLayers.cpp
    src/plugin/SightLayers.hpp
    src/plugin/SightPictures.cpp
    src/plugin/SightPictures.hpp
    src/plugin/SquadPanel.cpp
    src/plugin/SquadPanel.hpp
    src/plugin/SquadPanelDraw.cpp
    src/plugin/SquadPanelDraw.hpp
    src/plugin/TestLoop.cpp
    src/plugin/TestLoop.hpp
    src/plugin/Vignette.cpp
    src/plugin/Vignette.hpp
    src/plugin/WorldPanels.cpp
    src/plugin/WorldPanels.hpp
)

function(mevr_add_plugin game)
    set(host MEVR-${game})
    set(host_sources ${MEVR_HOST_SOURCES} ${MEVR_CORE_SOURCES})
    set(with_halcyon OFF)
    set(with_main_menu OFF)
    if (MEVR_HALCYON AND game STREQUAL "LE1")
        set(with_halcyon ON)
        list(APPEND host_sources ${MEVR_HALCYON_HOST_SOURCES})
        if (MEVR_MAIN_MENU)
            # Our VR main menu (research/28): LE1 only for now, in the host (not hot-reloaded).
            set(with_main_menu ON)
            list(APPEND host_sources src/plugin/MainMenu.cpp src/plugin/MainMenu.hpp src/plugin/MainMenuSettings.cpp
                src/plugin/MainMenuSettings.hpp)
        endif ()
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
        # windowscodecs: WIC, the test loop's PNG snapshots (plugin/TestLoop).
        target_link_libraries(${host} PRIVATE halcyon-le1 OpenXR::openxr_loader windowscodecs)
        target_compile_definitions(${host} PRIVATE MEVR_WITH_HALCYON)
    endif ()
    if (with_main_menu)
        target_link_libraries(${host} PRIVATE mevr-menu)
        target_compile_definitions(${host} PRIVATE MEVR_WITH_MAIN_MENU)
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
        src/core/PngFile.cpp
        src/core/ik/ArmLength.cpp
        src/core/ik/ArmPose.cpp
        src/core/ik/BodySlots.cpp
        src/core/ik/HandFrame.cpp
        src/core/ik/BoneNames.cpp
        src/core/ik/CameraHook.cpp
        src/core/ik/Ik.cpp
        src/core/ik/Posture.cpp
        src/core/ik/Rigid.cpp
        src/core/ik/BodyFollow.cpp
        src/core/ik/TorsoPose.cpp
        src/core/ik/Throwing.cpp
        src/core/ik/TouchReference.cpp
        src/core/ik/TwoHand.cpp
        src/core/vr/Calibration.cpp
        src/core/vr/Comfort.cpp
        src/core/vr/Colour.cpp
        src/core/vr/ColourStrip.cpp
        src/core/vr/FrameProbe.cpp
        src/core/vr/Haptics.cpp
        src/core/vr/ImageConvert.cpp
        src/core/vr/LayerComposite.cpp
        src/core/vr/MenuPointer.cpp
        src/core/vr/MenuSettings.cpp
        src/core/vr/MetricStereo.cpp
        src/core/vr/OverlayPlacement.cpp
        src/core/vr/PoseRecording.cpp
        src/core/vr/Recenter.cpp
        src/core/vr/Sights.cpp
        src/core/vr/SquadMenu.cpp
        src/core/vr/TestRequest.cpp
        src/core/vr/ViewLevel.cpp
        src/core/vr/ViewSync.cpp
        src/core/vr/WheelCrop.cpp
        src/engine/XrToUnreal.cpp
        src/game/GamepadMapping.cpp
        src/game/HeadSim.cpp
    )
    target_include_directories(mevr-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-tests PRIVATE OpenXR::headers windowscodecs ole32)
    target_compile_definitions(mevr-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-tests PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    add_test(NAME mevr-tests COMMAND mevr-tests)

    # The gun sights' look, offline (tools/sight-preview): the scope's picture with the host's own shader, the
    # reticles and the window ring, as PNGs from a ray-traced test scene.
    add_executable(mevr-sight-preview
        tools/sight-preview/Main.cpp
        src/core/PngFile.cpp
        src/core/vr/ImageConvert.cpp
        src/core/vr/LayerComposite.cpp
        src/core/vr/OverlayPlacement.cpp
        src/core/vr/Sights.cpp
        src/plugin/SightPictures.cpp
    )
    target_include_directories(mevr-sight-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-sight-preview PRIVATE OpenXR::headers windowscodecs ole32 d3d11 d3dcompiler)
    target_compile_definitions(mevr-sight-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-sight-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

    # Where a game thread's time goes, sampled from outside (tools/stack-sampler, research/22).
    add_executable(mevr-stack-sampler tools/stack-sampler/sampler.cpp)
    target_link_libraries(mevr-stack-sampler PRIVATE dbghelp winmm)
    target_compile_definitions(mevr-stack-sampler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-stack-sampler PRIVATE /W4 /WX /permissive- /utf-8)

    # The colour correction, offline (tools/colour-preview, research/27): the shader (core/vr/Colour's prelude) on a
    # D3D11 device of its own, checked against the CPU reference, and the test strip through each preset as PNGs.
    # Also a test: the shader must match the CPU reference within one 8-bit step.
    add_executable(mevr-colour-preview
        tools/colour-preview/Main.cpp
        src/core/PngFile.cpp
        src/core/vr/Colour.cpp
        src/core/vr/ColourStrip.cpp
    )
    target_include_directories(mevr-colour-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-colour-preview PRIVATE windowscodecs ole32 d3d11 d3dcompiler)
    target_compile_definitions(mevr-colour-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-colour-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    add_test(NAME mevr-colour-shader COMMAND mevr-colour-preview --check)

    # The squad weapon menu's look, offline (tools/squad-preview): PNGs of sample squads drawn with the
    # host's own drawing code and ImGui's D3D11 backend (from the halcyon-le1 library).
    if (MEVR_HALCYON)
        add_executable(mevr-squad-preview
            tools/squad-preview/Main.cpp
            src/core/PngFile.cpp
            src/core/vr/SquadMenu.cpp
            src/plugin/SquadPanelDraw.cpp
        )
        target_include_directories(mevr-squad-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(mevr-squad-preview PRIVATE halcyon-le1 OpenXR::headers windowscodecs ole32 d3d11)
        target_compile_definitions(mevr-squad-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(mevr-squad-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

        # The guided calibration's look, offline (tools/calibration-preview, research/25): the panel's screens, the
        # target board, the orbs and the check dot as PNGs, and eye views composited as the runtime shows them.
        add_executable(mevr-calibration-preview
            tools/calibration-preview/Main.cpp
            src/core/PngFile.cpp
            src/core/vr/Calibration.cpp
            src/core/vr/ImageConvert.cpp
            src/core/vr/LayerComposite.cpp
            src/plugin/CalibrationDraw.cpp
        )
        target_include_directories(mevr-calibration-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(mevr-calibration-preview PRIVATE halcyon-le1 OpenXR::headers windowscodecs ole32 d3d11)
        target_compile_definitions(mevr-calibration-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(mevr-calibration-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    endif ()
endif ()

# The VR main menu's look, offline (tools/menu-preview, research/28): every section and state as PNGs, drawn with the
# host's own RmlUi setup, renderer, document and backdrop passes, plus the frame's CPU and GPU time.
if (MEVR_BUILD_TESTS AND MEVR_MAIN_MENU)
    add_executable(mevr-menu-preview
        tools/menu-preview/Main.cpp
        src/core/PngFile.cpp
    )
    target_include_directories(mevr-menu-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-menu-preview PRIVATE mevr-menu windowscodecs ole32 d3d11)
    target_compile_definitions(mevr-menu-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-menu-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
endif ()
