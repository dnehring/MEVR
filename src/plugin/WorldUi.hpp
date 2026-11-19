// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/WorldUi.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <string>

// The game's full-screen screens in the room: the host's side (research/53; core/vr/WorldUi). While the mod's state is fresh
// (HostServices v27 setWorldUi) Halcyon renders the GUI mode as on foot (MevrBridge::WorldUiActive, its live-world-GUI branch)
// and HalcyonOverlays puts the game's UI on its panel where Current says: placed once per opening from the head at that moment
// (toward what the screen belongs to), the stage's shown part only, its dark parts at the backdrop's opacity; the game pointer
// gets the whole window on the panel's plane, so its pixels line up with the shown part. A screen the mod marks for the wrist
// ([WorldUi] wrist, research/53 section 6) is projected from the left wrist instead: it unfolds out of it, rides on it (or stays
// where it was projected) and a faint fan of light joins the two (plugin/WristBeam); without a tracked left controller when it
// opens, it stands in the room.
namespace mevr::host::world_ui {

struct Shown {
    vr::world_ui::Panel panel;   // the shown part, in the app space
    vr::world_ui::Panel settled; // before the screen's unfold: a tab change must not shrink/move the rail
    vr::world_ui::Panel whole;   // the whole window on the panel's plane (the game pointer's quad)
    vr::world_ui::Rect part;     // the shown part of the window (window UV)
    float ageS = 0.0f; // real time since the wrist screen started unfolding
    float backdrop = 0.0f;
    vr::world_ui::Screen screen = vr::world_ui::Screen::None;
    bool onWrist = false;                     // projected from the wrist
    bool puzzle = false;                      // the bypass puzzle is being played (State::puzzle): no game pointer on it, the d-pad
    std::optional<vr::world_ui::Beam> beam;   // its fan of light, when shown
};

// A screen of ours is up (the mod's state is fresh).
bool Active();
void Reserve(std::uint32_t request, std::uint32_t screen, std::uint32_t origin);
void Publish(vr::world_ui::State const* state);
void Forget();
unsigned Ownership(int mode);  // bit 0: active; bit 1: eligible mode-7 reservation; bit 2: reserved before mode change (0-4)
bool HoldFrame(int mode);
void RedirectReady(bool ready);
void NotePresent(bool reserved, bool flatPanel, bool held);
void LogStats();
// The screen to show this frame, given the head (app space) and the game window's size (the UI panel's source); nullopt
// while none is up or it can't be placed yet (no valid head at its opening).
// `wrist`: the left wrist's point (hud_pieces::WristPoint from the left grip) while the left controller is tracked.
std::optional<Shown> Current(XrVector3f const& eyes, XrQuaternionf const& head, bool headValid, std::uint32_t windowWidth,
    std::uint32_t windowHeight, std::optional<XrVector3f> wrist);

// Shepard's view on foot held at render time (research/53; research/le3-native-gui.md "The movie's mouse and the view behind a
// screen"): while a screen of ours stands in the game mode `gameMode` (vr::world_ui::ScreenMode) and the mod sends the view it
// saved on foot with where the player camera's POV lives (State::holdRender), HoldViewBegin puts that view into the POV for the
// one view build that follows and HoldViewEnd puts the camera's own back. Game thread, the renderer's CalcSceneView detour, around the primary
// player's main view only: the game's camera keeps serving everything else. Every game's (LE1's GalaxyViewSwapBegin calls it for
// its world UI, ME3's renderer through MevrSeams::WorldViewHoldBegin). True: held, call HoldViewEnd after the build.
bool HoldViewBegin(int gameMode) noexcept;
void HoldViewEnd() noexcept;
// Any thread: whether HoldViewBegin would hold the view in `gameMode` now, for the renderer's mesh hides: 0 no, 1 for a screen
// on the wrist, 2 for one standing in the room (MevrSeams::WorldViewHeld).
int HoldsView(int gameMode) noexcept;

} // namespace mevr::host::world_ui
