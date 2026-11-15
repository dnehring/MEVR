// SPDX-License-Identifier: MIT
#pragma once

// What the host's modules need from the VR renderer, whichever game's it is (docs/le2-continuation-plan.md M2.2): LE1's
// Halcyon renderer (src/halcyon/le1; plugin/RendererLe1.cpp forwards to MELEVR::MevrBridge and its siblings) or Halcyon's
// ME2 renderer (src/halcyon/le2; plugin/RendererLe2.cpp forwards to ME2VR::). One code path for both games: a host module
// that asks the renderer something asks it here, never through a renderer's own headers (vr_config.h, vr_menu.h,
// pchud.h, head_aim.h, render_hook.h, logger.h, me2_menu.h ...), so the same module compiles into either game's host.
//
// The other direction, the renderer telling the host (a frame begun, the session's functions resolved, the HUD's draws),
// stays in each renderer's seams: LE1's are declared in src/halcyon/le1/mevr_bridge.h and implemented by the host's
// modules; ME2's in src/halcyon/le2/mevr_bridge.h, implemented in plugin/HalcyonLe2Bridge.cpp, which calls the same
// game-independent host functions (testloop::WrapSessionFunctions, testloop::OnFrameBegun ...). LE1's seam header also
// holds plain types the host's modules share with the seams (PanelKind, PanelPose, OverlayFrame): a module may include
// it for those, as it declares nothing that only Halcyon's LE1 library defines but `g_perfDraws`, `g_oneDrawStereo` and the
// stereo-pass records, which only LE1's render-side modules (RenderLevers, PerfProbe, DrawReplay) use.
//
// research/le2-renderer-integration.md "The renderer interface" lists what is here, what each game answers and what is
// still LE1-only. Grown as modules move onto it.

#include <Windows.h>

#include <cstdint>
#include <string>

namespace mevr::host {

// Our settings file, <exe dir>\ASI\<GAME>MEVR.ini (engine/SettingsFile: the mod's), as text for the Win32 profile API;
// empty if the exe's path can't be read. Not the renderer's (Halcyon's are MELEVR.ini / MELE2VR.ini), but every module
// asked for it through LE1's MevrBridge::SettingsPath. Any thread.
std::wstring const& SettingsPath() noexcept;

} // namespace mevr::host

namespace mevr::host::renderer {

// ---- Halcyon's own menu and log ---------------------------------------------------------------------------------------

// The renderer's own developer menu is open: Halcyon's ImGui menu (LE1: Ctrl or Shift + \; ME2: its menu key, Insert
// upstream). Not our main menu (plugin/MainMenu). Any thread.
bool OwnMenuOpen() noexcept;
// Opens or closes it (our main menu closes it when it opens: one menu at a time). ME2: me2_menu.cpp's [MEVR] SetOpen, which
// does what its key does (it loads its values, saves them on closing and recenters, as upstream).
void SetOwnMenuOpen(bool open) noexcept;
// The renderer's menu key (a VK code; LE1 menuKey, ME2 [VR] MenuKey: \ in our deploy and ME2's baseline), 0 if none.
int OwnMenuKey() noexcept;

// Recenter the view as the renderer's own recenter does (our main menu's Recenter button). Present thread.
void RequestRecenter() noexcept;

// ---- The renderer's own settings our main menu edits on ME2 (plugin/MainMenuSettings, research/le2-menus.md) ----------
// LE1's rows edit Halcyon's VrConfig themselves (MainMenuSettings' LE1 part: its profiles, its render size, its HUD panel),
// so these answer only ME2's: Halcyon ME2's live values, which its SaveValues writes to MELE2VR.ini. A `has*` false: the
// renderer has no such setting and the row stands down (LE1: all false here). Present thread.
struct MenuRows {
    bool hasLookSmoothing = false;
    float lookSmoothing = 0.0f;       // ME2 [VR] HeadLookSmoothing, 0..0.95 (LE1's headLookSmoothing)
    bool hasFlatScreenWidth = false;
    float flatScreenWidthM = 0.0f;    // ME2 [VR] MenuScreenSize: its flat screen's width (LE1's menuMonoQuadWidth)
    // No depth of field row on ME2: its [VR] DisableDof is read and saved but drives nothing since upstream hid it (its
    // GamerSettings write took the tonemapping off: me2_menu.cpp's [DOFWASH]).
    bool hasMirror = false;
    int mirrorPresentEvery = 0;       // ME2 [VR] MirrorPresentEvery (0: its default, every 8th; LE1's mirrorPresentEvery)
};
MenuRows GetMenuRows() noexcept;
// Applies the rows that `has` (live), as the renderer's own menu would.
void SetMenuRows(MenuRows const& rows) noexcept;
// Saves the renderer's live settings to its file (ME2: SaveValues, MELE2VR.ini), as closing its own menu does.
void SaveOwnSettings(char const* why) noexcept;

// A line in the renderer's own log (LE1 %LOCALAPPDATA%\MELEVR\MELEVR_Log.txt, ME2 ...\MELE2VR\MELE2VR_Log.txt), for
// lines the renderer's log should carry next to its own ([MEVR-COLOUR], [MEVR-HUD] ...). Any thread.
void LogLine(std::string const& line) noexcept;

// ---- The game as the renderer reads it --------------------------------------------------------------------------------

// The game's mode as the renderer reads it (LE1 HeadAim::ReadGameModeSEH: 0 on foot, 1 the Mako, 5 a conversation, 6 a
// cinematic, 7 a GUI, 8 a load or movie, 9 the galaxy map, 10 the bypass minigame; -1 unreadable). ME2: its engine probe's
// raw mode, whose numbers are ME2's [verify] against LE1's before a module relies on one past 0. Any thread.
int GameMode() noexcept;

// The renderer's first person is on and owns the player's view this frame (LE1: fpEnabled and HeadAim::FirstPersonOwnsAny).
// ME2: false until its first person is read through this (M3).
bool FirstPersonOwned() noexcept;

// The render-side head look's arm counter (LE1 RenderHook::GetHeadArmSeq: increments with every armed eye pair); 0 where
// the renderer has none (ME2).
std::uint64_t HeadArmSequence() noexcept;

// The game's window (the swap chain's output window), nullptr until the renderer has seen it. Any thread.
HWND GameWindow() noexcept;

// ---- Settings the renderer keeps for our quads -----------------------------------------------------------------------
// LE1 keeps these in Halcyon's MELEVR.ini (its profiles, edited in our menu and its own); ME2's renderer has no such keys,
// so ME2 gets ME1's code defaults (vr_config.h's [MEVR] fields), the plan's rule. The default member values here are those.

// The body-locked HUD panel and the hand reticle (plugin/HalcyonOverlays).
struct HudSettings {
    bool hudPanel = true;
    float hudPanelDistanceM = 1.5f;   // from the eyes at recenter
    float hudPanelWidthM = 1.3f;      // height follows the game's frame aspect
    float hudPanelDropM = 0.25f;      // centre below eye height at recenter
    float hudPanelFollowDeg = 70.0f;  // 0 = never follows
    bool handReticle = true;
    float handReticleDistanceM = 10.0f;
    float handReticleSizeDeg = 1.5f;
};
HudSettings Hud() noexcept;

// Halcyon's own flat panels (its menu, the flat screen) fixed in the room (plugin/WorldPanels).
struct PanelSettings {
    bool worldLockedPanels = true;
    float panelFollowDeg = 70.0f;  // 0 = never follows
};
PanelSettings Panels() noexcept;

// A recenter the player asks for also removes a tilt past this many degrees (plugin/RecenterTilt).
float RecenterTiltDeg() noexcept;

// ---- Our hand aim -------------------------------------------------------------------------------------------------------

// The mod aims weapons with the right controller (HostServices::setHandAim). LE1: Halcyon's head aim stands down and the
// hand reticle shows (plugin/HalcyonBridge's SetModHandAim). ME2: noted (its head aim is off by the MEVR baseline; the
// reticle comes with M3).
void SetModHandAim(bool on) noexcept;
bool ModHandAim() noexcept;

// ---- The game's own HUD, as the renderer handles it -------------------------------------------------------------------

// The game's screen-centre crosshair hidden (the hand reticle replaces it in gameplay). LE1: Halcyon's PcHud. ME2: not yet
// (its own PcHud has no such switch: M3), the setter a no-op and the getter false.
void SetScreenReticleHidden(bool hidden) noexcept;
bool ScreenReticleHidden() noexcept;
// The HUD's authored layout (the HUD panel takes the game's UI as authored, not Halcyon's VR re-layout). LE1: PcHud's
// SetAuthoredLayout. ME2: a no-op until the HUD panel (M2 step 4).
void SetAuthoredHudLayout(bool on) noexcept;

} // namespace mevr::host::renderer
