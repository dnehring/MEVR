// SPDX-License-Identifier: MIT
// The renderer interface (plugin/Renderer.hpp) on LE3: Halcyon's ME3 renderer (src/halcyon/le3), forwarding to its own
// functions or to its seams (src/halcyon/le3/mevr_bridge.h). Where ME3's renderer has nothing that answers yet, the
// function says so and answers as LE1's would with that feature off; ME1's defaults stand where ME3 keeps no setting
// (the defaults in Renderer.hpp). research/le3-renderer-integration.md "The renderer interface".

#include "plugin/Renderer.hpp"

#include "core/Log.hpp"
#include "engine/SettingsFile.hpp"
#include "halcyon/le3/calcview_hook.h"
#include "halcyon/le3/d3d_capture.h"
#include "halcyon/le3/engine_probe.h"
#include "halcyon/le3/logger.h"
#include "halcyon/le3/me2_menu.h"
#include "halcyon/le3/me2_xr.h"

#include <dxgi.h>

#include <algorithm>
#include <atomic>

namespace mevr::host {

std::wstring const& SettingsPath() noexcept {
    // A function-local static: the game thread and the Present thread both ask first.
    static std::wstring const path = engine::SettingsFile().wstring();
    return path;
}

} // namespace mevr::host

namespace mevr::host::renderer {
namespace {
std::atomic<HWND> gGameWindow{nullptr};
std::atomic<bool> gModHandAim{false};
}

Caps const& Capabilities() noexcept {
    static Caps const caps = [] {
        Caps c;
        c.name = "halcyon-le3";
        c.eyeImageSeams = true;
        c.hudPanelDraw = true;
        c.hostCineScreen = true;
        c.panelsInRoom = true;
        c.galaxyTable = true;        // [Galaxy] nativeTable, opt-in
        // hudPanelGameplay false: only a measured prompt crop is captured, the native gameplay HUD stays in the eyes.
        // screenReticleHide false: the native LE3 crosshair's ownership has not been verified.
        return c;
    }();
    return caps;
}

bool OwnMenuOpen() noexcept {
    return ME2VR::Menu::IsOpen();
}

void SetOwnMenuOpen(bool open) noexcept {
    ME2VR::Menu::SetOpen(open);  // me2_menu.cpp's [MEVR] SetOpen: what its key does
}

int OwnMenuKey() noexcept {
    return ME2VR::Menu::GetMenuKey();
}

void RequestRecenter() noexcept {
    ME2VR::Me2Xr::Recenter();  // what double-tapping Back, its menu's button and opening its menu do
}

MenuRows GetMenuRows() noexcept {
    MenuRows rows;
    rows.hasLookSmoothing = true;
    rows.lookSmoothing = ME2VR::CalcViewHook::GetHeadLookSmoothing();
    rows.hasFlatScreenWidth = true;
    rows.flatScreenWidthM = ME2VR::Me2Xr::GetMenuScreenSize();
    rows.hasMirror = true;
    rows.mirrorPresentEvery = ME2VR::D3DCapture::GetMirrorPresentEvery();
    return rows;
}

void SetMenuRows(MenuRows const& rows) noexcept {
    // The same clamps as its LoadValues.
    if (rows.hasLookSmoothing) ME2VR::CalcViewHook::SetHeadLookSmoothing(std::clamp(rows.lookSmoothing, 0.0f, 0.95f));
    if (rows.hasFlatScreenWidth) ME2VR::Me2Xr::SetMenuScreenSize(std::clamp(rows.flatScreenWidthM, 0.5f, 5.0f));
    if (rows.hasMirror) ME2VR::D3DCapture::SetMirrorPresentEvery(std::max(rows.mirrorPresentEvery, 0));
}

void SaveOwnSettings(char const* why) noexcept {
    ME2VR::Menu::SaveSettings();  // SaveValues: MELE3VR.ini, as closing its menu does
    log::Info("main menu: the renderer's settings saved to MELE3VR.ini ({})", why != nullptr ? why : "?");
}

void LogLine(std::string const& line) noexcept {
    ME2VR::Log::Line(line);
}

int GameMode() noexcept {
    return ME2VR::EngineProbe::ReadGameMode();
}

int MoveStickHeadLookYawUU() noexcept {
    // me2_menu.cpp's RotateMoveStickByHeadLook, after the pad merge in its XInput hooks: the same sign as LE1's (+ left).
    return ME2VR::Menu::GetMoveFollowsHead() ? ME2VR::CalcViewHook::HeadLookYawUU() : 0;
}

FirstPersonControl FirstPersonControlState() noexcept {
    return {true, ME2VR::EngineProbe::GetFirstPerson(), true, ME2VR::CalcViewHook::Installed()};
}
bool SetFirstPersonEnabled(bool enabled) noexcept {
    ME2VR::EngineProbe::SetFirstPerson(enabled);
    return ME2VR::EngineProbe::GetFirstPerson() == enabled;
}

bool FirstPersonOwned() noexcept {
    // ME3's first person is on ([FirstPerson] Enabled, K): engine_probe's ApplyFirstPerson then owns the on-foot camera
    // modes. Which mode it owns this frame is the mod's half of the floor lock (PawnSample::cameraOwned: the explore and
    // combat modes, as LE1's constant eye), so on/off is LE1's FirstPersonOwnsAny here. [verify] a mode it doesn't own.
    return ME2VR::EngineProbe::GetFirstPerson();
}

std::uint64_t HeadArmSequence() noexcept {
    return 0;  // ME3's head look has no arm counter
}

HWND GameWindow() noexcept {
    // The swap chain the renderer captured (d3d_capture.cpp); its output window doesn't change once known.
    if (HWND const known = gGameWindow.load(std::memory_order_relaxed)) {
        return known;
    }
    IDXGISwapChain* const chain = ME2VR::D3DCapture::GetGameSwapChain();
    DXGI_SWAP_CHAIN_DESC desc{};
    if (chain == nullptr || FAILED(chain->GetDesc(&desc)) || desc.OutputWindow == nullptr) {
        return nullptr;
    }
    gGameWindow.store(desc.OutputWindow, std::memory_order_relaxed);
    return desc.OutputWindow;
}

HudSettings Hud() noexcept {
    HudSettings settings;
    // The shared ring now draws only while the mod owns live hand aim. Native crosshair suppression
    // remains unavailable; the native HUD itself stays in the eyes until its LE3 layout is verified.
    settings.hudPanel = false;
    return settings;
}

PanelSettings Panels() noexcept {
    return {};
}

float RecenterTiltDeg() noexcept {
    return 60.0f;  // ME1's default (vr_config.h recenterTiltDeg)
}

// [MEVR] No LE3 crosshair or HUD-layout native adapter is published yet.
void SetScreenReticleHidden(bool) noexcept {}
bool ScreenReticleHidden() noexcept { return false; }
void SetAuthoredHudLayout(bool) noexcept {}

void SetModHandAim(bool on) noexcept {
    if (gModHandAim.exchange(on, std::memory_order_relaxed) != on) {
        log::Info("halcyon le3: the mod {} weapons with the right controller", on ? "aims" : "no longer aims");
    }
}

bool ModHandAim() noexcept {
    return gModHandAim.load(std::memory_order_relaxed);
}

} // namespace mevr::host::renderer
