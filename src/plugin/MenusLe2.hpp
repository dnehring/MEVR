// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Controllers.hpp"

#include <Windows.h>
#include <Xinput.h>

#include <openxr/openxr.h>

#include <string>

struct ID3D11Device;

namespace ME2VR::MevrBridge {
struct OverlayFrame;
}

// Our menus on Halcyon's ME2 renderer (docs/le2-continuation-plan.md M4, research/le2-menus.md): what LE1's bridge and
// overlays (plugin/HalcyonBridge.cpp, plugin/HalcyonOverlays.cpp) do for our main menu (plugin/MainMenu) and the guided
// calibration (plugin/Calibration), for plugin/HalcyonLe2Bridge.cpp to call at its seams. The modules themselves are the
// same as LE1's; only this glue is ME2's. Also implements ME2's OnMenuKey and MainMenuOpen seams
// (src/halcyon/le2/mevr_bridge.h). Without MEVR_WITH_MAIN_MENU everything here does nothing.
namespace mevr::host::menus_le2 {

// ---- Present thread ------------------------------------------------------------------------------------------------------
// The session exists: the headset's name for the calibration's per-controller sections, the menu's top bar.
void OnSessionCreated(XrInstance instance, XrSession session);
// After the controller sync and the test loop (`live`: the player's own sample, `state`: a replay's in its place): the
// controllers noted for our menu and the calibration, the interaction profile, the both-sticks hold (our menu, or cancels
// the calibration; a tap takes a snapshot). Returns what everyone else (the mod, the pad mapping) sees: no controllers while
// the calibration runs or our menu is open, else the sample with the calibration's manual offsets, as on LE1.
vr::controllers::State OnFrameBegun(XrSession session, XrSpace space, XrTime time, vr::controllers::State const& live,
    vr::controllers::State const& state, bool focused);
// OverlayLayers, after the vignette: the calibration's quads and our menu's, appended to layersOut[count..capacity). While
// the calibration runs or our menu is open they replace ours before them, as on LE1. Returns the new count.
unsigned Layers(ME2VR::MevrBridge::OverlayFrame const& frame, ID3D11Device* device, void const** layersOut, unsigned count,
    unsigned capacity);
// ---- ME2's game screens on the wrist (M4.3, research/le2-menus.md "ME2's screens on the wrist") ----
// While one of the game's screens of the mod's game/WorldUi owns the GUI mode (or is being opened), the game's UI goes to the
// HUD panel (the bridge sets hud_panel::Frame::gameplay and backdrop from this), and the panel's quad shows the screen's part
// where plugin/WorldUi places it (on the left wrist, its fan of light under it) instead of in front of the body: LE1's
// HalcyonOverlays' world-UI path.
struct WorldUiPanel {
    bool owns = false;      // the panel takes the game's UI this frame for a screen of ours (shown, placed or not yet)
    float backdrop = 0.0f;  // its dark parts' opacity (the screen's; LE1's gPanelBackdrop)
};
// OverlayLayers, before the HUD panel is drawn: this frame's screen (plugin/WorldUi's Current, from the head and the left
// wrist), panelWidth x panelHeight the panel texture (the game's UI frame).
WorldUiPanel BeforePanel(ME2VR::MevrBridge::OverlayFrame const& frame, unsigned panelWidth, unsigned panelHeight);
// After it: `panelLayer` (hud_panel::Layer's quad, or nullptr) appended to layersOut[count..capacity) as this frame shows it:
// unchanged in gameplay; for a screen of ours its fan of light and the panel image's part at the screen's place (the game
// pointer's quad noted); nothing while the screen isn't placed yet. Returns the new count.
unsigned PanelLayers(ME2VR::MevrBridge::OverlayFrame const& frame, ID3D11Device* device, void const* panelLayer,
    void const** layersOut, unsigned count, unsigned capacity);

// The app space was re-origined: our menu and the calibration's layout placed again.
void OnRecentered();
// Every Present: our menu closes itself when it hasn't been drawn for a second (and a running calibration is cancelled).
void OnPresent();

// ---- Any thread (the XInput hook) ----------------------------------------------------------------------------------------
// Our menu or the calibration has the pad, or something held when it closed is still kept from the game: the hook must run
// FilterPad even without motion controllers.
bool PadWanted();
// After the pad mapping: while our menu or the calibration has the pad the game sees it neutral (`real`, a real pad's own
// state or null, navigates our menu); after it closes, what was held then is kept from the game until let go
// (gamepad::CloseLatch).
void FilterPad(XINPUT_GAMEPAD const* real, XINPUT_GAMEPAD& pad);

// ---- Any thread ----------------------------------------------------------------------------------------------------------
// The OpenXR runtime's and headset's names (xrGetInstanceProperties, xrGetSystemProperties), empty before a session.
std::string RuntimeName();
std::string SystemName();

} // namespace mevr::host::menus_le2
