// SPDX-License-Identifier: MIT
// The renderer interface (plugin/Renderer.hpp) on LE1: Halcyon's LE1 renderer (src/halcyon/le1). Each function forwards
// to what the host's modules called directly before the interface existed, so LE1 behaves exactly as before.

#include "plugin/Renderer.hpp"

#include "engine/SettingsFile.hpp"
#include "halcyon/le1/head_aim.h"
#include "halcyon/le1/logger.h"
#include "halcyon/le1/mevr_bridge.h"
#include "halcyon/le1/pchud.h"
#include "halcyon/le1/render_hook.h"
#include "halcyon/le1/vr_config.h"
#include "halcyon/le1/vr_menu.h"
#include "plugin/Halcyon.hpp"
#include "plugin/MevrMenu.hpp"

namespace mevr::host {

std::wstring const& SettingsPath() noexcept {
    // A function-local static: the game thread and the Present thread both ask first.
    static std::wstring const path = engine::SettingsFile().wstring();
    return path;
}

} // namespace mevr::host

namespace mevr::host::renderer {

bool OwnMenuOpen() noexcept {
    return MELEVR::Menu::IsOpen();
}

void SetOwnMenuOpen(bool open) noexcept {
    MELEVR::Menu::SetOpen(open);
}

int OwnMenuKey() noexcept {
    return MELEVR::Config::Get().menuKey;
}

void RequestRecenter() noexcept {
    MELEVR::Menu::RequestRecenter();
}

MenuRows GetMenuRows() noexcept {
    return {};  // LE1's menu edits Halcyon's VrConfig itself (MainMenuSettings' LE1 part)
}

void SetMenuRows(MenuRows const&) noexcept {}

void SaveOwnSettings(char const*) noexcept {}

void LogLine(std::string const& line) noexcept {
    MELEVR::Logger::LogLine(line);
}

int GameMode() noexcept {
    return MELEVR::HeadAim::ReadGameModeSEH();
}

int MoveStickHeadLookYawUU() noexcept {
    return MELEVR::Config::Get().moveFollowsHead ? MELEVR::RenderHook::HeadLookYawUU() : 0;
}

bool FirstPersonOwned() noexcept {
    return MELEVR::Config::Get().fpEnabled && MELEVR::HeadAim::FirstPersonOwnsAny();
}

std::uint64_t HeadArmSequence() noexcept {
    return MELEVR::RenderHook::GetHeadArmSeq();
}

HWND GameWindow() noexcept {
    return MELEVR::MevrBridge::GameWindow();
}

HudSettings Hud() noexcept {
    auto const& c = MELEVR::Config::Get();
    HudSettings s;
    s.hudPanel = c.hudPanel;
    s.hudPanelDistanceM = c.hudPanelDistanceM;
    s.hudPanelWidthM = c.hudPanelWidthM;
    s.hudPanelDropM = c.hudPanelDropM;
    s.hudPanelFollowDeg = c.hudPanelFollowDeg;
    s.handReticle = c.handReticle;
    s.handReticleDistanceM = c.handReticleDistanceM;
    s.handReticleSizeDeg = c.handReticleSizeDeg;
    return s;
}

PanelSettings Panels() noexcept {
    auto const& c = MELEVR::Config::Get();
    PanelSettings s;
    s.worldLockedPanels = c.worldLockedPanels;
    s.panelFollowDeg = c.panelFollowDeg;
    return s;
}

float RecenterTiltDeg() noexcept {
    return MELEVR::Config::Get().recenterTiltDeg;
}

void SetScreenReticleHidden(bool hidden) noexcept {
    MELEVR::Pchud::SetCrosshairHidden(hidden);
}

bool ScreenReticleHidden() noexcept {
    return MELEVR::Pchud::CrosshairHidden();
}

void SetAuthoredHudLayout(bool on) noexcept {
    MELEVR::Pchud::SetAuthoredLayout(on);
}

void SetModHandAim(bool on) noexcept {
    halcyon::SetModHandAim(on);
}

bool ModHandAim() noexcept {
    return MELEVR::MevrBridge::ControllerAimActive();
}

} // namespace mevr::host::renderer
