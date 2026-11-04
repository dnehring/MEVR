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
// gets the whole window on the panel's plane, so its pixels line up with the shown part.
namespace mevr::host::world_ui {

struct Shown {
    vr::world_ui::Panel panel;   // the shown part, in the app space
    vr::world_ui::Panel whole;   // the whole window on the panel's plane (the game pointer's quad)
    vr::world_ui::Rect part;     // the shown part of the window (window UV)
    float backdrop = 0.0f;
    vr::world_ui::Screen screen = vr::world_ui::Screen::None;
};

// A screen of ours is up (the mod's state is fresh).
bool Active();
// The screen to show this frame, given the head (app space) and the game window's size (the UI panel's source); nullopt
// while none is up or it can't be placed yet (no valid head at its opening).
std::optional<Shown> Current(XrVector3f const& eyes, XrQuaternionf const& head, bool headValid, std::uint32_t windowWidth,
    std::uint32_t windowHeight);

} // namespace mevr::host::world_ui
