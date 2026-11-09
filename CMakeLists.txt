# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.27)
# The one version number (docs/release-checklist.md section 5). Change it here only: the generated header below carries
# it into the C++ and .rc files, tools/package-release.ps1 reads it from this line and refuses binaries that disagree,
# and the release is tagged v<version>.
project(MEVR VERSION 0.1.1 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MSVC)
    message(FATAL_ERROR "MEVR builds with MSVC only (LExSDKv2 requires it).")
endif ()

# Generated headers (#include "mevr/..."): Version.hpp here, at configure time; BuildInfo.hpp (the commit) at build
# time by the mevr-build-info target below.
set(MEVR_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
if (NOT PROJECT_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR "project(MEVR VERSION ${PROJECT_VERSION}): use three numbers, major.minor.patch")
endif ()
configure_file(tools/cmake/Version.hpp.in ${MEVR_GENERATED_DIR}/mevr/Version.hpp @ONLY)
# The binaries players get name their PDB without the build folder (their debug directory reads "LE1MEVR.pdb", not
# the absolute path); tools/package-release.ps1 keeps the PDBs in a symbols zip of their own, next to the release.
# The PDB's file name by generator expression: the linker's own %_PDB% token doesn't survive CMake's escaping for
# MSBuild (it arrived as %%%%_PDB%%%%, and the binaries named "%%%LE1MEVR.pdb%%%").
function(mevr_pdb_name_only target)
    target_link_options(${target} PRIVATE "/PDBALTPATH:$<TARGET_PDB_FILE_NAME:${target}>")
endfunction()

# One source tree, one ASI per game (docs/trilogy-design.md). LE1 first.
option(MEVR_BUILD_LE1 "Build LE1MEVR.asi" ON)
option(MEVR_BUILD_LE2 "Build LE2MEVR.asi" OFF)
option(MEVR_BUILD_LE3 "Build LE3MEVR.asi" OFF)
# Development: a small stable host ASI plus <GAME>MEVR_mod.dll, which the host reloads whenever
# it changes, so the game can keep running between builds. Release: one self-contained ASI.
option(MEVR_HOT_RELOAD "Build the mod as a separate hot-reloadable DLL" ON)
# LExSDKv2 has no licence, so a players' build of LE1 has none of it (research/34 stage 6 for LE1): the mod runs on our
# own reflection layer, and only development builds keep LExSDKv2 for the cross-check (engine/ReflectCheck). Off by
# default in a release (monolithic) build. LE2/LE3 still read the engine through it (not released).
option(MEVR_LE1_LESDK "LE1: link LExSDKv2 for the reflection cross-check (development builds)" ${MEVR_HOT_RELOAD})

# LExSDKv2 honors these as normal variables (CMP0077).
if (MEVR_BUILD_LE1 AND MEVR_LE1_LESDK)
    set(LESDK_WITH_LE1 ON)
else ()
    set(LESDK_WITH_LE1 OFF)
endif ()
set(LESDK_WITH_LE2 ${MEVR_BUILD_LE2})
set(LESDK_WITH_LE3 ${MEVR_BUILD_LE3})
set(LESDK_WITH_LEL OFF)
set(LESDK_WITH_SAMPLES OFF)
set(LESDK_WITH_TESTS OFF)
# Only when something links it: a release build of LE1 configures and builds without the unlicensed submodule checked
# out (docs/release-checklist.md section 1; research/34 stage 6).
if (LESDK_WITH_LE1 OR LESDK_WITH_LE2 OR LESDK_WITH_LE3)
    add_subdirectory(external/LExSDKv2)
endif ()
# The loader's test ASI is built against LExSDKv2's own SPI header (an outside plugin's view), when the submodule is there.
set(MEVR_HAVE_LESDK_SPI OFF)
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/LExSDKv2/External")
    set(MEVR_HAVE_LESDK_SPI ON)
endif ()

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
        src/core/Ini.cpp
        src/core/vr/Colour.cpp
        src/core/vr/ColourStrip.cpp
        src/core/vr/MenuSettings.cpp
        src/core/vr/PerformanceModes.cpp
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

# The build's commit (tools/cmake/BuildInfo.cmake): ${CMAKE_BINARY_DIR}/generated/mevr/BuildInfo.hpp, checked on every
# build and rewritten only when the commit changes. The host's attach line, the menu's About, the problem report
# (install/Report) and the binaries' ProductVersion (<version>+<commit>) show it.
add_custom_target(mevr-build-info
    COMMAND ${CMAKE_COMMAND} -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR} -DOUTPUT=${MEVR_GENERATED_DIR}/mevr/BuildInfo.hpp
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/BuildInfo.cmake
    BYPRODUCTS ${MEVR_GENERATED_DIR}/mevr/BuildInfo.hpp
    COMMENT "Checking the build's commit"
    VERBATIM)

# MEVR-Setup.exe (tools/setup, research/33): the end user's installer, without ME3Tweaks Mod Manager. A Win32 window in
# the in-game menu's glass style (the same RmlUi setup, renderer, decorators and stylesheet: mevr-menu, assets/menu),
# over src/install. Also renders its pages to PNGs offline (--preview) and has a command line for scripted tests.
if (MEVR_MAIN_MENU)
    add_executable(MEVR-Setup WIN32
        tools/setup/Main.cpp
        tools/setup/SetupApp.cpp
        tools/setup/SetupApp.hpp
        tools/setup/XrProbe.cpp
        tools/setup/XrProbe.hpp
        tools/setup/Setup.rc
        src/core/PngFile.cpp
    )
    # The .rc's icon (assets/icon/mevr.ico, tools/make-icon.py) is found on the include path.
    target_include_directories(MEVR-Setup PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MEVR_GENERATED_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/icon)
    add_dependencies(MEVR-Setup mevr-build-info)
    target_link_libraries(MEVR-Setup PRIVATE mevr-menu mevr-install OpenXR::openxr_loader windowscodecs ole32 d3d11 dxgi dwmapi shell32)
    target_compile_definitions(MEVR-Setup PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(MEVR-Setup PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zi)
    # /DEPENDENTLOADFLAG:0x800 (LOAD_LIBRARY_SEARCH_SYSTEM32): its imports come from System32 only, never from the
    # folder it was downloaded to (DLL planting); Main.cpp's SetDefaultDllDirectories covers what it loads later.
    target_link_options(MEVR-Setup PRIVATE /ENTRY:wWinMainCRTStartup /DEBUG /DEPENDENTLOADFLAG:0x800)
    mevr_pdb_name_only(MEVR-Setup)
endif ()

# MinHook (BSD-2, third_party/minhook): one static library for every target that hooks with it: our loader, core/Hook's
# fallback when no SPI loader attached us (host and mod), and Halcyon's renderer (so the LE1 host has one MinHook).
add_library(mevr-minhook STATIC
    third_party/minhook/src/buffer.c
    third_party/minhook/src/hde/hde64.c
    third_party/minhook/src/hook.c
    third_party/minhook/src/trampoline.c
)
target_include_directories(mevr-minhook PUBLIC third_party/minhook/include)
target_compile_options(mevr-minhook PRIVATE /W3)

# Halcyon's LE1 VR renderer (GPL-3.0, src/halcyon/le1) with its vendored MinHook (mevr-minhook) and Dear ImGui.
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
)
target_include_directories(halcyon-le1 PUBLIC
    ${HALCYON_LE1_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src # [MEVR] game-independent cached frame ownership
    third_party/imgui
    third_party/imgui/backends
)
# Upstream's settings (MELEVRClean.vcxproj): C++17, Unicode. MEVR_ASI selects our changes.
set_target_properties(halcyon-le1 PROPERTIES CXX_STANDARD 17)
target_compile_definitions(halcyon-le1 PRIVATE WIN32 _WINDOWS UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS
    MELEVR_EXPORTS MEVR_ASI)
target_compile_options(halcyon-le1 PRIVATE /W3 /utf-8 /Zi)
target_link_libraries(halcyon-le1 PUBLIC mevr-minhook d3d11 dxgi Shell32 Ole32)

set(MEVR_CORE_SOURCES
    src/core/Exe.cpp
    src/core/Ini.cpp
    src/core/Ini.hpp
    src/core/Exe.hpp
    src/core/ExeCheck.cpp
    src/core/ExeCheck.hpp
    src/core/Hook.cpp
    src/core/Hook.hpp
    src/engine/Addresses.hpp
    src/loader/SpiAbi.hpp
    src/ue3/Pattern.cpp
    src/ue3/Pattern.hpp
    src/core/Log.cpp
    src/core/Log.hpp
    src/core/Window.cpp
    src/core/Window.hpp
    src/core/Version.cpp
    src/core/Version.hpp
)

set(MEVR_HOST_SOURCES
    src/core/Breadcrumbs.hpp
    src/core/FixedText.hpp
    src/engine/CodeDump.cpp
    src/engine/CodeDump.hpp
    src/mod/ModApi.hpp
    src/plugin/CrashBreadcrumbs.cpp
    src/plugin/CrashBreadcrumbs.hpp
    src/plugin/Entry.cpp
    src/plugin/ModLoader.cpp
    src/plugin/ModLoader.hpp
    src/plugin/PresentHook.cpp
    src/plugin/PresentHook.hpp
    src/plugin/ProcessEventHook.cpp
    src/plugin/ProcessEventHook.hpp
    src/ue3/SafeNames.cpp
    src/ue3/SafeNames.hpp
)

set(MEVR_MOD_SOURCES
    src/core/Breadcrumbs.hpp
    src/core/WriteJournal.hpp
    src/core/Hotkey.cpp
    src/core/Hotkey.hpp
    src/core/d3d11/Blit.cpp
    src/core/d3d11/Blit.hpp
    src/core/ik/ArmLength.cpp
    src/core/ik/BodyScale.cpp
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
    src/core/ik/Fingers.cpp
    src/core/ik/Fingers.hpp
    src/core/ik/GunKick.cpp
    src/core/ik/GunKick.hpp
    src/core/ik/Ik.cpp
    src/core/ik/Ik.hpp
    src/core/ik/Posture.cpp
    src/core/ik/Posture.hpp
    src/core/ik/PoseGraft.cpp
    src/core/ik/PoseGraft.hpp
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
    src/core/vr/Ads.cpp
    src/core/vr/Ads.hpp
    src/core/ik/Vent.cpp
    src/core/ik/Vent.hpp
    src/core/vr/AimRay.cpp
    src/core/vr/AimRay.hpp
    src/core/vr/Calibration.cpp
    src/core/vr/Calibration.hpp
    src/core/vr/Comfort.cpp
    src/core/vr/Comfort.hpp
    src/core/vr/CombatStance.cpp
    src/core/vr/FloorLock.cpp
    src/core/vr/Sprint.cpp
    src/core/vr/CombatStance.hpp
    src/core/vr/GameMenuRequest.cpp
    src/core/vr/GameMenuRequest.hpp
    src/core/vr/Controllers.cpp
    src/core/vr/Controllers.hpp
    src/core/vr/Floor.hpp
    src/core/vr/HostView.cpp
    src/core/vr/HudPieces.cpp
    src/core/vr/HudPieces.hpp
    src/core/vr/PowerWheel.cpp
    src/core/vr/PowerWheel.hpp
    src/core/vr/MakoControls.cpp
    src/core/vr/MakoControls.hpp
    src/core/vr/CockpitHazard.cpp
    src/core/vr/CockpitHazard.hpp
    src/core/vr/CockpitPanel.cpp
    src/core/vr/CockpitPanel.hpp
    src/core/vr/MakoGunner.cpp
    src/core/vr/MakoGunner.hpp
    src/core/vr/InteractLabel.cpp
    src/core/vr/InteractLabel.hpp
    src/core/vr/HostView.hpp
    src/core/vr/Output.cpp
    src/core/vr/Output.hpp
    src/core/vr/OverlayPlacement.cpp
    src/core/vr/OverlayPlacement.hpp
    src/core/vr/SceneCaptureScope.hpp
    src/core/vr/ScopeCapture.cpp
    src/core/vr/ScopeArt.cpp
    src/core/vr/CineScreen.cpp
    src/core/vr/GalaxyHologram.cpp
    src/core/vr/GalaxyHologram.hpp
    src/core/vr/GalaxyMap.cpp
    src/core/vr/GalaxyMap.hpp
    src/core/vr/WorldUi.cpp
    src/core/vr/WorldUi.hpp
    src/core/vr/ConversationWorld.cpp
    src/core/vr/ConversationWorld.hpp
    src/core/vr/OmniRail.cpp
    src/core/vr/OmniRail.hpp
    src/core/vr/Spectator.cpp
    src/core/vr/Spectator.hpp
    src/core/vr/ScopeCapture.hpp
    src/core/vr/Sights.cpp
    src/core/vr/Sights.hpp
    src/core/vr/SquadContext.cpp
    src/core/vr/SquadContext.hpp
    src/core/vr/SquadGesture.cpp
    src/core/vr/SquadGesture.hpp
    src/core/vr/SquadPlace.cpp
    src/core/vr/SquadPlace.hpp
    src/core/vr/SquadParser.cpp
    src/core/vr/SquadParser.hpp
    src/core/vr/SquadRetreat.cpp
    src/core/vr/SquadRetreat.hpp
    src/core/vr/VrCover.cpp
    src/core/vr/VrCover.hpp
    src/core/vr/SquadStanding.cpp
    src/core/vr/SquadStanding.hpp
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
    src/engine/SelectionGaze.cpp
    src/engine/SelectionGaze.hpp
    src/engine/Engine.cpp
    src/engine/Engine.hpp
    src/engine/GameAdapter.hpp
    src/engine/GameAdapterSdk.hpp
    src/engine/Legacy.cpp
    src/engine/Legacy.hpp
    src/engine/NativesDump.cpp
    src/engine/NativesDump.hpp
    src/engine/ProcessEvent.cpp
    src/engine/ProcessEvent.hpp
    src/engine/ReflectCheck.cpp
    src/engine/ReflectCheck.hpp
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
    src/game/BodyProbe.cpp
    src/game/BodyProbe.hpp
    src/game/BodyScale.cpp
    src/game/BodyScale.hpp
    src/game/Comfort.cpp
    src/game/Comfort.hpp
    src/game/CameraProbe.cpp
    src/game/CameraProbe.hpp
    src/game/CombatStance.cpp
    src/game/Sprint.cpp
    src/game/CombatStance.hpp
    src/game/ConsoleRequest.cpp
    src/game/ConsoleRequest.hpp
    src/game/SystemSettings.cpp
    src/game/SystemSettings.hpp
    src/game/GamepadBridge.cpp
    src/game/GamepadBridge.hpp
    src/game/GamepadMapping.cpp
    src/game/GamepadMapping.hpp
    src/game/GameMenuRequest.cpp
    src/game/GameMenuRequest.hpp
    src/game/GameArt.cpp
    src/game/GameArt.hpp
    src/game/HoloProbe.cpp
    src/game/HoloProbe.hpp
    src/game/InventoryProbe.cpp
    src/game/InventoryProbe.hpp
    src/game/GuiCursor.cpp
    src/game/GuiCursor.hpp
    src/core/vr/GuiEvents.cpp
    src/core/vr/GuiEvents.hpp
    src/game/Voice.cpp
    src/game/Voice.hpp
    src/core/voice/Grammar.cpp
    src/core/voice/Grammar.hpp
    src/core/vr/VoiceGesture.cpp
    src/core/vr/VoiceGesture.hpp
    src/core/vr/Voice.hpp
    src/game/Fingers.cpp
    src/game/Fingers.hpp
    src/game/GunKick.cpp
    src/game/GunKick.hpp
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
    src/game/HudPieces.cpp
    src/game/ScreenReticle.cpp
    src/game/ScreenReticle.hpp
    src/game/HudPieces.hpp
    src/game/PowerWheel.cpp
    src/game/PowerWheel.hpp
    src/game/Interact.cpp
    src/game/Interact.hpp
    src/game/SquadContext.cpp
    src/game/SquadContext.hpp
    src/game/SquadOrders.cpp
    src/game/SquadOrders.hpp
    src/game/SquadPlace.cpp
    src/game/SquadPlace.hpp
    src/game/SquadAck.cpp
    src/game/SquadAck.hpp
    src/game/CockpitProbe.cpp
    src/game/CockpitProbe.hpp
    src/game/CockpitPanel.cpp
    src/game/CockpitPanel.hpp
    src/game/Mako.cpp
    src/game/MakoControls.cpp
    src/game/MakoControls.hpp
    src/game/Mako.hpp
    src/game/Holsters.cpp
    src/game/Holsters.hpp
    src/game/FocusPause.cpp
    src/game/FocusPause.hpp
    src/game/MenuPause.cpp
    src/game/MenuPause.hpp
    src/game/Player.cpp
    src/game/Player.hpp
    src/game/Posture.cpp
    src/game/Posture.hpp
    src/game/VrCover.cpp
    src/game/VrCover.hpp
    src/game/Reflections.cpp
    src/game/Reflections.hpp
    src/game/ScopeParts.cpp
    src/game/ScopeParts.hpp
    src/game/ScopeCapture.cpp
    src/game/ScopeCapture.hpp
    src/game/GalaxyDump.cpp
    src/game/GalaxyDump.hpp
    src/game/GalaxyMap.cpp
    src/game/GalaxyMap.hpp
    src/game/WorldUi.cpp
    src/game/WorldUi.hpp
    src/game/ConversationWorld.cpp
    src/game/ConversationWorld.hpp
    src/game/UiWorldFigure.cpp
    src/game/UiWorldFigure.hpp
    src/game/OmniTool.cpp
    src/game/OmniTool.hpp
    src/game/CallTrace.cpp
    src/game/CallTrace.hpp
    src/game/MakoGunner.cpp
    src/game/MakoGunner.hpp
    src/game/Spectator.cpp
    src/game/SceneCaptureRig.cpp
    src/game/Spectator.hpp
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
    src/game/Vent.cpp
    src/game/Vent.hpp
    src/game/ViewAnchor.cpp
    src/game/ViewAnchor.hpp
    src/game/ViewLevel.cpp
    src/game/ViewLevel.hpp
    src/game/Weapon.cpp
    src/game/Weapon.hpp
    src/mod/Mod.cpp
    src/mod/ModApi.hpp
    # Our UE3 reflection layer (research/34)
    src/ue3/Handles.cpp
    src/ue3/Handles.hpp
    src/ue3/Layout.hpp
    src/ue3/Memory.cpp
    src/ue3/Memory.hpp
    src/ue3/Names.cpp
    src/ue3/Names.hpp
    src/ue3/Params.cpp
    src/ue3/Params.hpp
    src/ue3/Reflect.cpp
    src/ue3/Reflect.hpp
    src/ue3/Runtime.cpp
    src/ue3/Runtime.hpp
    src/ue3/Types.hpp
)

# NO_SDK: a target that doesn't link LExSDKv2 (the hot-reload host since research/34 stage 4; everything at stage 6).
function(mevr_configure target game)
    cmake_parse_arguments(PARSE_ARGV 2 arg "NO_SDK" "" "")
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    if (NOT arg_NO_SDK)
        target_link_libraries(${target} PRIVATE LESDK-${game})
    else ()
        target_compile_definitions(${target} PRIVATE MEVR_NO_LESDK)
    endif ()
    target_link_libraries(${target} PRIVATE mevr-minhook version d3d11 dxgi)
    target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX MEVR_GAME_${game})
    # Same warning policy as the SDK's own consumers; the generated headers need /permissive.
    target_compile_options(${target} PRIVATE /W4 /WX /permissive /utf-8 /Zc:__cplusplus /Zi
        /wd4121 /wd4127 /wd4702)
    target_link_options(${target} PRIVATE /DEBUG)
    mevr_pdb_name_only(${target})
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
    src/core/vr/CombatStance.cpp
    src/core/vr/FloorLock.cpp
    src/core/vr/Sprint.cpp
    src/core/vr/CombatStance.hpp
    src/core/vr/GameMenuRequest.cpp
    src/core/vr/GameMenuRequest.hpp
    src/core/vr/Controllers.cpp
    src/core/vr/Controllers.hpp
    src/core/vr/Floor.hpp
    src/core/vr/FrameProbe.cpp
    src/core/vr/FrameProbe.hpp
    src/core/vr/QuestPacing.cpp
    src/core/vr/QuestPacing.hpp
    src/core/vr/GamePointer.cpp
    src/core/vr/GamePointer.hpp
    src/core/vr/GuiEvents.cpp
    src/core/vr/GuiEvents.hpp
    src/plugin/Voice.cpp
    src/plugin/Voice.hpp
    src/plugin/VoiceModel.cpp
    src/plugin/VoiceModel.hpp
    src/core/vr/Voice.hpp
    src/voice/VoiceApi.h
    src/core/vr/Upscale.cpp
    src/core/vr/Upscale.hpp
    src/core/Embedded.hpp
    src/core/vr/Haptics.cpp
    src/core/vr/Haptics.hpp
    src/core/vr/HostView.cpp
    src/core/vr/HudPieces.cpp
    src/core/vr/HudPieces.hpp
    src/core/vr/PowerWheel.cpp
    src/core/vr/PowerWheel.hpp
    src/core/vr/MakoControls.cpp
    src/core/vr/MakoControls.hpp
    src/core/vr/CockpitHazard.cpp
    src/core/vr/CockpitHazard.hpp
    src/core/vr/MakoGunner.cpp
    src/core/vr/MakoGunner.hpp
    src/core/vr/InteractLabel.cpp
    src/core/vr/InteractLabel.hpp
    src/core/vr/HostView.hpp
    src/core/vr/ImageConvert.cpp
    src/core/vr/ImageConvert.hpp
    src/core/vr/LayerComposite.cpp
    src/core/vr/LayerComposite.hpp
    src/core/vr/MenuPointer.cpp
    src/core/vr/MenuPointer.hpp
    src/core/vr/CineScreen.cpp
    src/core/vr/CineScreen.hpp
    src/core/vr/PresentationHold.cpp
    src/core/vr/PresentationHold.hpp
    src/core/vr/TransitionFade.cpp
    src/core/vr/TransitionFade.hpp
    src/core/vr/MetricStereo.cpp
    src/core/vr/MetricStereo.hpp
    src/core/vr/OverlayPlacement.cpp
    src/core/vr/OverlayPlacement.hpp
    src/core/vr/PoseRecording.cpp
    src/core/vr/PoseRecording.hpp
    src/core/vr/Recenter.cpp
    src/core/vr/Recenter.hpp
    src/core/vr/SceneCaptureScope.hpp
    src/core/vr/ScopeCapture.cpp
    src/core/vr/ScopeArt.cpp
    src/core/vr/GalaxyHologram.cpp
    src/core/vr/GalaxyHologram.hpp
    src/core/vr/GalaxyMap.cpp
    src/core/vr/GalaxyMap.hpp
    src/core/vr/WorldUi.cpp
    src/core/vr/WorldUi.hpp
    src/core/vr/ConversationWorld.cpp
    src/core/vr/ConversationWorld.hpp
    src/core/vr/OmniRail.cpp
    src/core/vr/OmniRail.hpp
    src/core/vr/OmniPad.cpp
    src/core/vr/OmniPad.hpp
    src/core/gfx/Raster.cpp
    src/core/gfx/Raster.hpp
    src/core/gfx/Swf.cpp
    src/core/gfx/Swf.hpp
    src/core/gfx/SwfFont.cpp
    src/core/gfx/SwfFont.hpp
    src/core/vr/Spectator.cpp
    src/core/vr/Spectator.hpp
    src/core/vr/ScopeCapture.hpp
    src/core/vr/Sights.cpp
    src/core/vr/Sights.hpp
    src/core/vr/SquadContext.cpp
    src/core/vr/SquadContext.hpp
    src/core/vr/SquadGesture.cpp
    src/core/vr/SquadGesture.hpp
    src/core/vr/SquadPlace.cpp
    src/core/vr/SquadPlace.hpp
    src/core/vr/SquadParser.cpp
    src/core/vr/SquadParser.hpp
    src/core/vr/SquadRetreat.cpp
    src/core/vr/SquadRetreat.hpp
    src/core/vr/SquadStanding.cpp
    src/core/vr/SquadStanding.hpp
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
    src/core/CallQueue.hpp
    src/plugin/GamePointer.cpp
    src/plugin/GamePointer.hpp
    src/plugin/GameThreadCalls.cpp
    src/plugin/GameThreadCalls.hpp
    src/plugin/Halcyon.cpp
    src/plugin/Halcyon.hpp
    src/plugin/HalcyonBridge.cpp
    src/plugin/FloorLock.cpp
    src/plugin/HalcyonOverlays.cpp
    src/plugin/MevrMenu.hpp
    src/plugin/MevrMenuPointer.cpp
    src/plugin/MevrMenuTab.cpp
    src/plugin/CineScreen.cpp
    src/plugin/CineScreen.hpp
    src/plugin/PresentationHold.cpp
    src/plugin/MetricStereo.cpp
    src/plugin/MetricStereo.hpp
    src/plugin/PerfProbe.cpp
    src/plugin/PerfProbe.hpp
    src/plugin/QuestPacing.cpp
    src/plugin/QuestPacing.hpp
    src/plugin/RenderLevers.cpp
    src/plugin/RenderLevers.hpp
    src/plugin/RecenterTilt.cpp
    src/plugin/RecenterTilt.hpp
    src/plugin/ScopeCaptureSource.cpp
    src/plugin/ScopeCaptureSource.hpp
    src/plugin/SightLayers.cpp
    src/plugin/SightLayers.hpp
    src/plugin/GalaxyHologram.cpp
    src/plugin/GalaxyHologram.hpp
    src/plugin/WorldUi.cpp
    src/plugin/WorldUi.hpp
    src/plugin/ConversationWorld.cpp
    src/plugin/ConversationWorld.hpp
    src/plugin/UiWorldFigure.cpp
    src/plugin/UiWorldFigure.hpp
    src/plugin/GalaxyHologramDraw.cpp
    src/plugin/GalaxyHologramDraw.hpp
    src/plugin/SightPictures.cpp
    src/plugin/SightPictures.hpp
    src/plugin/Spectator.cpp
    src/plugin/Spectator.hpp
    src/plugin/SquadPanel.cpp
    src/plugin/SquadPanel.hpp
    src/plugin/Upscale.cpp
    src/plugin/Upscale.hpp
    src/plugin/SquadPanelDraw.cpp
    src/plugin/SquadPanelDraw.hpp
    src/plugin/Glow.cpp
    src/plugin/Glow.hpp
    src/plugin/PowerIcons.cpp
    src/plugin/PowerIcons.hpp
    src/plugin/PowerWheel.cpp
    src/plugin/PowerWheel.hpp
    src/plugin/PowerWheelDraw.cpp
    src/plugin/PowerWheelDraw.hpp
    src/plugin/Swapchains.cpp
    src/plugin/Swapchains.hpp
    src/plugin/TestLoop.cpp
    src/plugin/TestLoop.hpp
    src/plugin/Vignette.cpp
    src/plugin/Vignette.hpp
    src/plugin/WorldPanels.cpp
    src/plugin/WorldPanels.hpp
    src/plugin/SquadPlaceDraw.cpp
    src/plugin/SquadPlaceDraw.hpp
    src/plugin/CinemaBackdropDraw.cpp
    src/plugin/CinemaBackdropDraw.hpp
    src/plugin/CinemaBackdrop.cpp
    src/plugin/CinemaBackdrop.hpp
    src/plugin/TransitionFade.cpp
    src/plugin/WristBeam.cpp
    src/plugin/WristBeam.hpp
)

# Text embedded at build time as a char array (tools/cmake/EmbedText.cmake; the declarations in src/core/Embedded.hpp):
# the generated .cpp goes in `out_var` for the targets that use it.
function(mevr_embed_text input symbol out_var)
    set(out "${CMAKE_CURRENT_BINARY_DIR}/embedded/${symbol}.cpp")
    add_custom_command(
        OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/${input} -DOUTPUT=${out} -DSYMBOL=${symbol}
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/EmbedText.cmake
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${input}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/cmake/EmbedText.cmake"
        COMMENT "Embedding ${input} as ${symbol}"
        VERBATIM)
    set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

# AMD FidelityFX Super Resolution 1 (external/fsr1, MIT): the shader headers the eye upscale compiles at run time
# (core/vr/Upscale, plugin/Upscale; research/32).
mevr_embed_text(external/fsr1/ffx_a.h kFfxA MEVR_EMBEDDED_FFX_A)
mevr_embed_text(external/fsr1/ffx_fsr1.h kFfxFsr1 MEVR_EMBEDDED_FFX_FSR1)
set(MEVR_FSR_EMBEDDED ${MEVR_EMBEDDED_FFX_A} ${MEVR_EMBEDDED_FFX_FSR1})

function(mevr_add_plugin game)
    set(host MEVR-${game})
    set(host_sources ${MEVR_HOST_SOURCES} ${MEVR_CORE_SOURCES})
    set(with_halcyon OFF)
    set(with_main_menu OFF)
    if (MEVR_HALCYON AND game STREQUAL "LE1")
        set(with_halcyon ON)
        list(APPEND host_sources ${MEVR_HALCYON_HOST_SOURCES} ${MEVR_FSR_EMBEDDED})
        if (MEVR_MAIN_MENU)
            # Our VR main menu (research/28): LE1 only for now, in the host (not hot-reloaded).
            set(with_main_menu ON)
            list(APPEND host_sources src/plugin/MainMenu.cpp src/plugin/MainMenu.hpp src/plugin/OmniToolRail.cpp src/plugin/OmniToolRail.hpp src/plugin/OmniPad.cpp src/plugin/OmniPad.hpp src/plugin/RailArt.cpp src/plugin/RailArt.hpp
                src/plugin/MainMenuSettings.cpp
                src/plugin/MainMenuSettings.hpp src/plugin/SetupRequest.cpp src/plugin/SetupRequest.hpp src/plugin/ReportRequest.cpp
                src/plugin/ReportRequest.hpp src/plugin/SteamVrSmoothingRequest.cpp src/plugin/SteamVrSmoothingRequest.hpp
                src/install/ReportMarks.hpp src/core/Json.cpp src/core/Json.hpp)
        endif ()
    endif ()
    # LE1 without LExSDKv2 (MEVR_LE1_LESDK off: every release build): neither the mod nor the host links it.
    set(game_sdk "")
    if (game STREQUAL "LE1" AND NOT MEVR_LE1_LESDK)
        set(game_sdk NO_SDK)
    endif ()
    set(host_sdk ${game_sdk})
    if (MEVR_HOT_RELOAD)
        add_library(${host} SHARED ${host_sources})
        set(host_sdk NO_SDK)  # nothing in the host includes LExSDKv2 since research/34 stage 4

        set(mod MEVR-${game}-mod)
        add_library(${mod} SHARED ${MEVR_MOD_SOURCES} ${MEVR_CORE_SOURCES})
        mevr_configure(${mod} ${game} ${game_sdk})
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
    mevr_configure(${host} ${game} ${host_sdk})
    # The version and commit (mevr/Version.hpp, tools/cmake/BuildInfo.cmake): the attach line, the menu's About and the
    # ASI's version resource (plugin/Plugin.rc).
    target_sources(${host} PRIVATE src/plugin/Plugin.rc)
    target_include_directories(${host} PRIVATE ${MEVR_GENERATED_DIR})
    add_dependencies(${host} mevr-build-info)
    set_target_properties(${host} PROPERTIES OUTPUT_NAME "${game}MEVR" SUFFIX ".asi")
endfunction()

foreach (game LE1 LE2 LE3)
    if (MEVR_BUILD_${game})
        mevr_add_plugin(${game})
    endif ()
endforeach ()
# The model tier's runtime (research/54; core/voice/Llm) needs llama.cpp's header (external/llama-cpp, tools/fetch-voice.ps1
# -Llm; gitignored): with it the LE1 host carries the runtime, without it plugin/VoiceModel says the model tier is off.
if (MEVR_BUILD_LE1 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/llama-cpp/include/llama.h")
    target_sources(MEVR-LE1 PRIVATE src/core/voice/Llm.cpp src/core/voice/Llm.hpp)
    target_include_directories(MEVR-LE1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/llama-cpp/include)
    target_compile_definitions(MEVR-LE1 PRIVATE MEVR_HAVE_LLAMA)
endif ()

# The installer's logic (research/33): game and VR detection, the manifest with backups, INI edits that never reset a
# profile, the VR game settings, the launch arguments, optional content (texture packs through Mass Effect Modder),
# the update check. Linked by the setup app (tools/setup) and the tests. No game code, no LExSDKv2.
add_library(mevr-install STATIC
    src/core/Json.cpp
    src/core/Json.hpp
    src/core/Version.cpp
    src/core/Version.hpp
    src/install/Detect.cpp
    src/install/Detect.hpp
    src/install/SteamVrSettings.cpp
    src/install/SteamVrSettings.hpp
    src/install/GameSettings.cpp
    src/install/GameSettings.hpp
    src/install/Http.cpp
    src/install/Http.hpp
    src/install/Ini.cpp
    src/install/Ini.hpp
    src/install/Installer.cpp
    src/install/Installer.hpp
    src/install/Launch.cpp
    src/install/Launch.hpp
    src/install/LaunchArgs.cpp
    src/install/LaunchArgs.hpp
    src/install/Manifest.cpp
    src/install/Manifest.hpp
    src/install/Packs.cpp
    src/install/Packs.hpp
    src/install/Report.cpp
    src/install/Report.hpp
    src/install/ReportMarks.hpp
    src/install/Toc.cpp
    src/install/Toc.hpp
    src/install/Updates.cpp
    src/install/Updates.hpp
    src/install/UpdateInstall.cpp
    src/install/Voice.cpp
    src/install/Voice.hpp
    src/install/Zip.cpp
    src/install/Zip.hpp
)
# Public: install/Updates.hpp's kVersion is mevr/Version.hpp's (configure time; BuildInfo.hpp stays this library's own).
target_include_directories(mevr-install PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src ${MEVR_GENERATED_DIR})
add_dependencies(mevr-install mevr-build-info)
# dxgi: the problem report's GPU and driver (install/Report).
target_link_libraries(mevr-install PUBLIC version bcrypt winhttp shell32 ole32 advapi32 dxgi)
target_compile_definitions(mevr-install PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(mevr-install PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

# Our own ASI loader (src/loader, research/33): bink2w64.dll, a proxy for RAD's Bink DLL that forwards every export to
# bink2w64_original.dll and loads Binaries\Win64\ASI\*.asi, offering LExSDKv2-based plugins the SPI interface they
# expect. Our own new code (MIT; no LEBinkProxy code), MinHook (BSD-2, mevr-minhook above) for the hooks, no LExSDKv2.
# The setup app installs it when no ASI loader is there (ME3Tweaks' LEBinkProxy, if installed, is used instead).
add_library(MEVR-Loader SHARED
    src/loader/Loader.cpp
    src/loader/Loader.rc
    src/install/LaunchArgs.cpp
    src/install/LaunchArgs.hpp
    src/loader/LoaderCore.cpp
    src/loader/LoaderCore.hpp
    src/loader/Forwarders.hpp
    src/loader/SpiAbi.hpp
)
# The generated headers: Loader.rc's version and commit.
target_include_directories(MEVR-Loader PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MEVR_GENERATED_DIR})
add_dependencies(MEVR-Loader mevr-build-info)
target_link_libraries(MEVR-Loader PRIVATE mevr-minhook)
target_compile_definitions(MEVR-Loader PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_compile_options(MEVR-Loader PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zi)
target_link_options(MEVR-Loader PRIVATE /DEBUG)
mevr_pdb_name_only(MEVR-Loader)
set_target_properties(MEVR-Loader PROPERTIES OUTPUT_NAME bink2w64)

# Offline tests for game-independent math (no game, no runtime needed).
option(MEVR_BUILD_TESTS "Build the offline tests" ON)
if (MEVR_BUILD_TESTS)
    enable_testing()
    add_executable(mevr-tests
        tests/Main.cpp
        tests/InstallTests.cpp
        tests/ReportTests.cpp
        src/loader/LoaderCore.cpp
        tests/ReflectTests.cpp
        tests/BreadcrumbTests.cpp
        tests/ExeTests.cpp
        tests/PowerWheelTests.cpp
        tests/VoiceMenuTests.cpp
        tests/SquadPlaceTests.cpp
        tests/BodyScaleTests.cpp
        tests/FloorLockTests.cpp
        tests/CameraDiagnosticTests.cpp
        tests/LandmarkTests.cpp
        tests/ConversationWorldTests.cpp
        tests/GuiEventsTests.cpp
        tests/OmniPadTests.cpp
        tests/TableReturnTests.cpp
        tests/OmniRailMotionTests.cpp
        tests/SwapchainBudgetTests.cpp
        tests/VrCoverTests.cpp
        tests/PerformanceModesTests.cpp
        tests/QuestPacingTests.cpp
        src/core/vr/GuiEvents.cpp
        tests/SquadPhrasings.cpp
        tests/VoiceTests.cpp
        src/core/voice/Wav.cpp
        src/core/voice/Grammar.cpp
        src/core/vr/VoiceGesture.cpp
        tests/CockpitPanelTests.cpp
        src/core/Exe.cpp
        src/core/ExeCheck.cpp
        src/core/Hook.cpp
        src/core/Ini.cpp
        src/core/Log.cpp
        src/ue3/Memory.cpp
        src/ue3/Names.cpp
        src/ue3/SafeNames.cpp
        src/ue3/Params.cpp
        src/ue3/Pattern.cpp
        src/ue3/Reflect.cpp
        src/core/PngFile.cpp
        src/core/ik/ArmLength.cpp
        src/core/ik/BodyScale.cpp
        src/core/ik/ArmPose.cpp
        src/core/ik/BodySlots.cpp
        src/core/ik/HandFrame.cpp
        src/core/ik/BoneNames.cpp
        src/core/ik/CameraHook.cpp
        src/core/ik/Fingers.cpp
        src/core/ik/GunKick.cpp
        src/core/ik/Ik.cpp
        src/core/ik/Posture.cpp
        src/core/ik/PoseGraft.cpp
        src/core/ik/Rigid.cpp
        src/core/ik/BodyFollow.cpp
        src/core/ik/TorsoPose.cpp
        src/core/ik/Throwing.cpp
        src/core/ik/TouchReference.cpp
        src/core/ik/TwoHand.cpp
        src/core/vr/Ads.cpp
        src/core/ik/Vent.cpp
        src/core/vr/Calibration.cpp
        src/core/vr/Comfort.cpp
        src/core/vr/CombatStance.cpp
        src/core/vr/FloorLock.cpp
        src/core/vr/Sprint.cpp
        src/core/vr/HostView.cpp
        src/core/vr/GameMenuRequest.cpp
        src/core/vr/HudPieces.cpp
        src/core/vr/MakoControls.cpp
        src/core/vr/CockpitHazard.cpp
        src/core/vr/CockpitPanel.cpp
        src/core/vr/MakoGunner.cpp
        src/core/vr/InteractLabel.cpp
        src/core/vr/Colour.cpp
        src/core/vr/ColourStrip.cpp
        src/core/vr/FrameProbe.cpp
        src/core/vr/QuestPacing.cpp
        src/core/vr/GamePointer.cpp
        src/core/vr/Haptics.cpp
        src/core/vr/ImageConvert.cpp
        src/core/vr/LayerComposite.cpp
        src/core/vr/MenuPointer.cpp
        src/core/vr/MenuSettings.cpp
        src/core/vr/PerformanceModes.cpp
        src/core/vr/CineScreen.cpp
        src/core/vr/PresentationHold.cpp
        src/core/vr/TransitionFade.cpp
        src/core/vr/MetricStereo.cpp
        src/core/vr/OverlayPlacement.cpp
        src/core/vr/PoseRecording.cpp
        src/core/vr/PowerWheel.cpp
        src/core/vr/Recenter.cpp
        src/core/vr/SceneCaptureScope.hpp
        src/core/vr/ScopeCapture.cpp
        src/core/vr/ScopeArt.cpp
        src/core/vr/GalaxyHologram.cpp
        src/core/vr/GalaxyMap.cpp
        src/core/vr/WorldUi.cpp
        src/core/vr/ConversationWorld.cpp
        src/core/vr/OmniRail.cpp
        src/core/vr/OmniPad.cpp
        src/core/gfx/Raster.cpp
        src/core/gfx/Swf.cpp
        src/core/gfx/SwfFont.cpp
        tests/GfxTests.cpp
        src/core/vr/Spectator.cpp
        src/core/vr/Sights.cpp
        src/core/vr/SquadContext.cpp
        src/core/vr/SquadGesture.cpp
        src/core/vr/SquadPlace.cpp
        src/core/vr/SquadParser.cpp
        src/core/vr/SquadRetreat.cpp
        src/core/vr/VrCover.cpp
        src/core/vr/SquadStanding.cpp
        src/core/vr/SquadMenu.cpp
        src/core/vr/TestRequest.cpp
        src/core/vr/Upscale.cpp
        src/core/vr/ViewLevel.cpp
        src/core/vr/ViewSync.cpp
        src/core/vr/WheelCrop.cpp
        src/engine/XrToUnreal.cpp
        src/game/GamepadMapping.cpp
        src/game/HeadSim.cpp
    )
    target_include_directories(mevr-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-tests PRIVATE mevr-install mevr-minhook OpenXR::headers windowscodecs ole32)
    target_compile_definitions(mevr-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-tests PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    # core/gfx's TrueType fonts are checked by loading them with FreeType (the menu's font engine), when it's built.
    if (TARGET freetype)
        target_link_libraries(mevr-tests PRIVATE freetype)
        target_compile_definitions(mevr-tests PRIVATE MEVR_TEST_FREETYPE)
    endif ()
    add_test(NAME mevr-tests COMMAND mevr-tests)
    add_executable(mevr-draw-replay-tests tests/DrawReplayGpuTests.cpp src/plugin/DrawReplay.cpp
        src/core/Hook.cpp src/core/Ini.cpp src/core/Log.cpp)
    target_include_directories(mevr-draw-replay-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-draw-replay-tests PRIVATE mevr-minhook d3d11 d3dcompiler)
    target_compile_definitions(mevr-draw-replay-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-draw-replay-tests PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    add_dependencies(mevr-tests mevr-draw-replay-tests)
    add_test(NAME mevr-draw-replay COMMAND mevr-draw-replay-tests)
    add_executable(mevr-quest-pacing-host-tests tests/QuestPacingHostTests.cpp
        src/plugin/QuestPacing.cpp src/core/vr/QuestPacing.cpp src/core/Ini.cpp src/core/vr/TestRequest.cpp)
    target_include_directories(mevr-quest-pacing-host-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(mevr-quest-pacing-host-tests PRIVATE OpenXR::headers)
    target_compile_definitions(mevr-quest-pacing-host-tests PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-quest-pacing-host-tests PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    add_dependencies(mevr-tests mevr-quest-pacing-host-tests)
    foreach(pacing_case no-headset unsupported refresh cycle single-rate rejected stop-restore performance perf-unsupported psvr2-auto)
        add_test(NAME mevr-quest-pacing-${pacing_case} COMMAND mevr-quest-pacing-host-tests ${pacing_case})
    endforeach()
    # Every `k...Rva = 0x...` constant in src/ is in our RVA table (engine/Addresses.hpp, research/34 stage 4).
    find_package(Python3 COMPONENTS Interpreter)
    if (Python3_Interpreter_FOUND)
        add_test(NAME mevr-perf-probe COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tests/PerfProbeTests.py)
        add_test(NAME mevr-rva-table COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-rva-table.py
            ${CMAKE_CURRENT_SOURCE_DIR})
        # The knowledge bundle (docs/knowledge, Open Knowledge Format): a typed frontmatter on every concept, complete
        # indexes, resolving links, dated log headings (docs/knowledge/how-to-write.md).
        add_test(NAME mevr-okf COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/check-okf.py
            ${CMAKE_CURRENT_SOURCE_DIR}/docs/knowledge)
    endif ()

    # Our ASI loader in a tiny host process (tools/loader-test, research/33): a folder like the game's Win64 with our
    # bink2w64.dll, a stand-in bink2w64_original.dll (the same 79 export names) and a test ASI built against
    # LExSDKv2's own SPI header; the host checks the forwarders, the attach, FindPattern and the hooks. Skipped without
    # the submodule (a source build of the release).
    if (MEVR_HAVE_LESDK_SPI)
    set(loader_test_dir ${CMAKE_BINARY_DIR}/loader-test/$<CONFIG>)
    add_library(mevr-loader-test-original SHARED tools/loader-test/TestOriginal.cpp tools/loader-test/TestOriginal.def)
    set_target_properties(mevr-loader-test-original PROPERTIES OUTPUT_NAME bink2w64_original RUNTIME_OUTPUT_DIRECTORY ${loader_test_dir})
    target_compile_options(mevr-loader-test-original PRIVATE /W4 /WX /utf-8)
    add_library(mevr-loader-test-asi SHARED tools/loader-test/TestAsi.cpp)
    set_target_properties(mevr-loader-test-asi PROPERTIES OUTPUT_NAME MEVRLoaderTest SUFFIX ".asi" RUNTIME_OUTPUT_DIRECTORY ${loader_test_dir}/ASI)
    target_include_directories(mevr-loader-test-asi PRIVATE external/LExSDKv2/External)
    target_compile_options(mevr-loader-test-asi PRIVATE /W4 /WX /permissive /utf-8)  # the SDK's macros need /permissive, as for our ASI
    add_executable(mevr-loader-test tools/loader-test/Main.cpp)
    set_target_properties(mevr-loader-test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${loader_test_dir})
    target_include_directories(mevr-loader-test PRIVATE external/LExSDKv2/External)
    target_compile_definitions(mevr-loader-test PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-loader-test PRIVATE /W4 /WX /utf-8)
    target_link_options(mevr-loader-test PRIVATE /INCREMENTAL:NO)
    add_dependencies(mevr-loader-test MEVR-Loader mevr-loader-test-original mevr-loader-test-asi)
    add_custom_command(TARGET mevr-loader-test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:MEVR-Loader> ${loader_test_dir}/bink2w64.dll
        VERBATIM)
    add_test(NAME mevr-loader COMMAND mevr-loader-test WORKING_DIRECTORY ${loader_test_dir})
    endif ()

    # A stand-in MassEffect1.exe with the real one's version number, for testing the setup on a fake game folder.
    add_executable(mevr-fake-game tools/setup/FakeGame.cpp tools/setup/FakeGame.rc)
    set_target_properties(mevr-fake-game PROPERTIES OUTPUT_NAME MassEffect1 RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fake-game/$<CONFIG>)
    target_compile_options(mevr-fake-game PRIVATE /W4 /WX)
    # A stand-in for Mass Effect Modder's command line (MassEffectModderNoGui.exe): logs its calls, "unpacks" and
    # "installs" on a fake game, so the setup's texture-pack flow is tested without MEM (research/33 section 10).
    add_executable(mevr-fake-mem tools/setup/FakeMem.cpp)
    set_target_properties(mevr-fake-mem PROPERTIES OUTPUT_NAME MassEffectModderNoGui RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fake-mem/$<CONFIG>)
    target_compile_definitions(mevr-fake-mem PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_compile_options(mevr-fake-mem PRIVATE /W4 /WX /permissive- /utf-8)
    # MEVR Setup's web and MEM paths against a local server (tools/setup/test-network.py, research/33 section 10): the
    # update check's answers, MEM's download and unpacking, a texture pack's install and uninstall on a fake game, and
    # "Install after I quit". Nothing reaches the internet (--test-server); skipped without Python or the setup.
    if (MEVR_MAIN_MENU AND Python3_Interpreter_FOUND)
        add_test(NAME mevr-setup-network COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/setup/test-network.py
            --setup $<TARGET_FILE:MEVR-Setup> --fake-game $<TARGET_FILE:mevr-fake-game> --fake-mem $<TARGET_FILE:mevr-fake-mem>
            --repo ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(mevr-setup-network PROPERTIES TIMEOUT 900)  # section 5 (the voice files) moves ~250 MB locally
        add_test(NAME mevr-setup-updates COMMAND Python3::Interpreter -I ${CMAKE_CURRENT_SOURCE_DIR}/tools/setup/test-updates.py
            --setup $<TARGET_FILE:MEVR-Setup> --fake-game $<TARGET_FILE:mevr-fake-game> --repo ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(mevr-setup-updates PROPERTIES TIMEOUT 300)
    endif ()

    # The gun sights' look, offline (tools/sight-preview): the scope's picture with the host's own shader, the
    # reticles and the window ring, as PNGs from a ray-traced test scene.
    add_executable(mevr-sight-preview
        tools/sight-preview/Main.cpp
        src/core/PngFile.cpp
        src/core/vr/ImageConvert.cpp
        src/core/vr/LayerComposite.cpp
        src/core/vr/OverlayPlacement.cpp
        src/core/vr/SceneCaptureScope.hpp
        src/core/vr/ScopeCapture.cpp
        src/core/vr/ScopeArt.cpp
        src/core/vr/Sights.cpp
        src/plugin/SightPictures.cpp
    )
    target_include_directories(mevr-sight-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-sight-preview PRIVATE OpenXR::headers windowscodecs ole32 d3d11 d3dcompiler)
    target_compile_definitions(mevr-sight-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-sight-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

    # The galaxy hologram, offline (tools/galaxy-preview, research/51 section 8): each map level for both eyes with the host's
    # own drawing code, as PNGs, and a stereo check (every star found in both images and triangulated back into the room).
    add_executable(mevr-galaxy-preview
        tools/galaxy-preview/Main.cpp
        src/core/Json.cpp
        src/core/PngFile.cpp
        src/core/vr/GalaxyHologram.cpp
        src/core/vr/OverlayPlacement.cpp
        src/plugin/GalaxyHologramDraw.cpp
    )
    target_include_directories(mevr-galaxy-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
    target_link_libraries(mevr-galaxy-preview PRIVATE OpenXR::headers windowscodecs ole32 d3d11 d3dcompiler)
    target_compile_definitions(mevr-galaxy-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-galaxy-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

    # The cinema screen's floor and horizon, offline (tools/cinema-preview, research/52): both eyes as PNGs with the host's shader.
    add_executable(mevr-cinema-preview
        tools/cinema-preview/Main.cpp
        src/core/PngFile.cpp
        src/plugin/CinemaBackdropDraw.cpp
    )
    target_include_directories(mevr-cinema-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-cinema-preview PRIVATE OpenXR::headers windowscodecs ole32 d3d11 d3dcompiler)
    target_compile_definitions(mevr-cinema-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-cinema-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

    # The Mako's lever and stick, offline (tools/mako-preview, research/46): PNGs with the host's own pixels.
    add_executable(mevr-mako-preview
        tools/mako-preview/Main.cpp
        src/core/PngFile.cpp
        src/core/vr/MakoControls.cpp
    )
    target_include_directories(mevr-mako-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-mako-preview PRIVATE OpenXR::headers windowscodecs ole32)
    target_compile_definitions(mevr-mako-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-mako-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)

    # Where a game thread's time goes, sampled from outside (tools/stack-sampler, research/22).
    add_executable(mevr-stack-sampler tools/stack-sampler/sampler.cpp)
    target_link_libraries(mevr-stack-sampler PRIVATE dbghelp winmm)
    target_compile_definitions(mevr-stack-sampler PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-stack-sampler PRIVATE /W4 /WX /permissive- /utf-8)

    # How many swapchains the active OpenXR runtime lets a session hold (tools/swapchain-probe, research/21 section 18).
    add_executable(mevr-swapchain-probe tools/swapchain-probe/Main.cpp)
    target_link_libraries(mevr-swapchain-probe PRIVATE OpenXR::openxr_loader d3d11 dxgi)
    target_compile_definitions(mevr-swapchain-probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-swapchain-probe PRIVATE /W4 /WX /permissive- /utf-8)

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

    # The eye upscale, offline (tools/upscale-preview, research/32): FSR 1's EASU and RCAS from core/vr/Upscale's
    # shader text (the embedded AMD headers) on a D3D11 device of its own, over a synthetic test image, as PNGs
    # next to a bilinear scale of the same; --check (a ctest test) compiles them and checks what the output must
    # keep (flat stays flat, nothing beyond the source's range, sharper than bilinear); --bench times an eye.
    add_executable(mevr-upscale-preview
        tools/upscale-preview/Main.cpp
        src/core/PngFile.cpp
        src/core/vr/Colour.cpp
        src/core/vr/Upscale.cpp
        ${MEVR_FSR_EMBEDDED}
    )
    target_include_directories(mevr-upscale-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-upscale-preview PRIVATE windowscodecs ole32 d3d11 d3dcompiler)
    target_compile_definitions(mevr-upscale-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-upscale-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    add_test(NAME mevr-upscale-shader COMMAND mevr-upscale-preview --check)

    # The voice orders' recognizer (research/54): MEVR-Voice.dll, our C interface (src/voice/VoiceApi.h) over Moonshine
    # Voice's prebuilt static libraries (MIT; external/moonshine-voice, fetched by tools/fetch-voice.ps1; /MD release, MSVC
    # 14.40) and ONNX Runtime (MIT, its DLL copied next to ours). The host loads the DLL from ASIoice at run time, so a
    # build without the library is still complete: the game then has no voice. mevr-voice-bench times the models over WAV
    # files (step 2 of the research's plan).
    set(MEVR_VOICE_SDK "${CMAKE_CURRENT_SOURCE_DIR}/external/moonshine-voice")
    option(MEVR_VOICE "Build MEVR-Voice.dll and mevr-voice-bench (needs external/moonshine-voice)" ON)
    if (MEVR_VOICE AND EXISTS "${MEVR_VOICE_SDK}/lib/moonshine.lib")
        set(MEVR_VOICE_LIBS ${MEVR_VOICE_SDK}/lib/moonshine.lib ${MEVR_VOICE_SDK}/lib/bin-tokenizer.lib ${MEVR_VOICE_SDK}/lib/moonshine-utils.lib
            ${MEVR_VOICE_SDK}/lib/ort-utils.lib ${MEVR_VOICE_SDK}/lib/onnxruntime.lib)
        add_library(MEVR-Voice SHARED
            src/voice/Voice.cpp
            src/voice/Voice.rc
            src/voice/VoiceApi.h
            src/core/voice/Wav.cpp
            src/core/voice/Wav.hpp
            src/core/vr/Voice.hpp
        )
        # The generated headers: Voice.rc's version and commit (the release ships the DLL: tools/package-release.ps1).
        target_include_directories(MEVR-Voice PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MEVR_VOICE_SDK}/include ${MEVR_GENERATED_DIR})
        add_dependencies(MEVR-Voice mevr-build-info)
        target_link_libraries(MEVR-Voice PRIVATE ${MEVR_VOICE_LIBS} ole32 avrt)
        target_compile_definitions(MEVR-Voice PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(MEVR-Voice PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus /Zi)
        target_link_options(MEVR-Voice PRIVATE /DEBUG)  # its PDB goes in the release's symbols (tools/package-release.ps1)
        mevr_pdb_name_only(MEVR-Voice)
        add_custom_command(TARGET MEVR-Voice POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${MEVR_VOICE_SDK}/lib/onnxruntime.dll $<TARGET_FILE_DIR:MEVR-Voice>)
        add_executable(mevr-voice-bench
            tools/voice-bench/Main.cpp
            src/core/voice/Wav.cpp
        )
        target_include_directories(mevr-voice-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MEVR_VOICE_SDK}/include)
        target_link_libraries(mevr-voice-bench PRIVATE ${MEVR_VOICE_LIBS})
        target_compile_definitions(mevr-voice-bench PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(mevr-voice-bench PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
        add_dependencies(mevr-voice-bench MEVR-Voice)  # for onnxruntime.dll next to it
    endif ()

    # The model tier offline (research/54 step 6): core/voice/Llm loads llama.cpp's shared build (MIT; external/llama-cpp,
    # tools/fetch-voice.ps1 -Llm) by name at run time, so only its header is needed to build; the bench answers a dataset's
    # prompts under their grammars.
    set(MEVR_LLAMA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/llama-cpp")
    if (EXISTS "${MEVR_LLAMA_DIR}/include/llama.h")
        add_executable(mevr-llm-bench
            tools/llm-bench/Main.cpp
            src/core/voice/Grammar.cpp
            src/core/voice/Llm.cpp
            src/core/vr/SquadContext.cpp
        )
        target_include_directories(mevr-llm-bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MEVR_LLAMA_DIR}/include)
        target_compile_definitions(mevr-llm-bench PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(mevr-llm-bench PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    endif ()

    # Every program ctest runs, as one target: CI (.github/workflows) and tools/package-release.ps1 build it before ctest.
    # The loader too: mevr-tests installs the bink2w64.dll built next to it into its fake game.
    add_custom_target(mevr-test-programs)
    add_dependencies(mevr-test-programs mevr-tests mevr-colour-preview mevr-upscale-preview MEVR-Loader mevr-fake-game mevr-fake-mem)
    if (MEVR_MAIN_MENU)
        add_dependencies(mevr-test-programs MEVR-Setup)
    endif ()
    if (MEVR_HAVE_LESDK_SPI)
        add_dependencies(mevr-test-programs mevr-loader-test)
    endif ()

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

        # The power wheel's look, offline (tools/power-wheel-preview, research/55): the wheel in its states, the hint card
        # and every icon as PNGs, drawn with the host's own code and glow, to set against the concept.
        add_executable(mevr-power-wheel-preview
            tools/power-wheel-preview/Main.cpp
            src/core/PngFile.cpp
            src/core/gfx/Swf.cpp
            src/core/gfx/SwfFont.cpp
            src/core/vr/ImageConvert.cpp
            src/core/vr/PowerWheel.cpp
            src/plugin/Glow.cpp
            src/plugin/PowerIcons.cpp
            src/plugin/PowerWheelDraw.cpp
        )
        target_include_directories(mevr-power-wheel-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(mevr-power-wheel-preview PRIVATE halcyon-le1 OpenXR::headers windowscodecs ole32 d3d11 d3dcompiler)
        target_compile_definitions(mevr-power-wheel-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
        target_compile_options(mevr-power-wheel-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
    endif ()
endif ()

# The game's own UI art, locally (tools/gfx-dump, research/57 section 5): a GFx movie's exports, sprites, shapes and fonts; a
# character drawn to PNG (core/gfx/Raster), a nine-slice fit, a font rebuilt as TrueType (core/gfx/SwfFont) and a text
# sample FreeType renders from it. Reads the player's own files; never commit what it writes.
if (MEVR_BUILD_TESTS AND MEVR_MAIN_MENU)
    add_executable(mevr-gfx-dump
        tools/gfx-dump/Main.cpp
        src/core/PngFile.cpp
        src/core/gfx/Raster.cpp
        src/core/gfx/Swf.cpp
        src/core/gfx/SwfFont.cpp
    )
    target_include_directories(mevr-gfx-dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(mevr-gfx-dump PRIVATE freetype windowscodecs ole32)
    target_compile_definitions(mevr-gfx-dump PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    target_compile_options(mevr-gfx-dump PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
endif ()

# The VR main menu's look, offline (tools/menu-preview, research/28): every section and state as PNGs, drawn with the
# host's own RmlUi setup, renderer, document and backdrop passes, plus the frame's CPU and GPU time.
if (MEVR_BUILD_TESTS AND MEVR_MAIN_MENU)
    add_executable(mevr-menu-preview
        tools/menu-preview/Main.cpp
        src/core/vr/OmniRail.cpp
        src/core/vr/WorldUi.cpp
        src/core/vr/OverlayPlacement.cpp
        src/core/vr/GalaxyHologram.cpp
        src/core/PngFile.cpp
        src/plugin/RailArt.cpp
        src/core/gfx/Raster.cpp
        src/core/gfx/Swf.cpp
        src/core/gfx/SwfFont.cpp
    )
    # The generated headers: About shows this build's version and commit, as in the game.
    target_include_directories(mevr-menu-preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${MEVR_GENERATED_DIR})
    add_dependencies(mevr-menu-preview mevr-build-info)
    target_link_libraries(mevr-menu-preview PRIVATE mevr-menu OpenXR::headers windowscodecs ole32 d3d11)
    target_compile_definitions(mevr-menu-preview PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(mevr-menu-preview PRIVATE /W4 /WX /permissive- /utf-8 /Zc:__cplusplus)
endif ()
