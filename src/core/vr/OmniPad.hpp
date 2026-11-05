// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/WorldUi.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

// The omni-tool's d-pad for the bypass puzzle (research/53 section 6.3; the user, 2026-11-05: "proceed with touch for the puzzle.
// We may need a large d-pad asset to control its movement"). LE1's puzzle (GUI_SF_SkillGame.PCMiniGame) moves a marker from
// outside three turning rings to their centre: up, A or X takes it one ring in (the centre wins), down or B one ring out, left and
// right turn it around while held; the red blocks turn and send it back, the orange ones are walls. Its own d-pad events
// (Engine.EBioGuiEvent's CONTROL_*, through SFXGameModeBase.GuiKey) drive it exactly, and any mouse motion on it turns the marker
// without end, so a touch d-pad beside the screen sends those events and the game pointer stays off the puzzle while it's played.
// This is the game-independent part (tests/OmniPadTests.cpp): the arms, their events, which arm a touch is on, where the pad
// stands. The host draws and works it (plugin/OmniPad, assets/menu/omnipad.rml).
namespace mevr::vr::omni_pad {

enum class Arm : std::uint8_t { None, Up, Down, Left, Right };
// The document's name for an arm ("up", "down", "left", "right"; "" for None).
char const* Name(Arm arm);

// The game's GUI events an arm sends when pressed and when let go (0 for None).
std::uint8_t PressEvent(Arm arm);
std::uint8_t ReleaseEvent(Arm arm);
// What the held arm changing sends, in order: the old arm let go, then the new one pressed.
std::vector<std::uint8_t> Transition(Arm from, Arm to);

// The arm a point of the pad's canvas is on (x right, y down, in canvas pixels): the 45-degree sector around the centre it lies in,
// outside the hub (the centre's `hubFraction` of the size, where no arm is) and inside the canvas. A fingertip that slides from one
// arm to the next changes the direction without lifting, as a thumb on a pad does.
inline constexpr float kHubFraction = 0.13f;
Arm ArmAt(float x, float y, float width, float height, float hubFraction = kHubFraction);

// ---- Where it stands ----
// To the right of the puzzle's screen in its plane (toward the right hand, which works it), level with its centre: the screen on
// the wrist is 0.30 m wide, so a 0.17 m pad gives each arm about 5.7 x 5.7 cm under the fingertip.
inline constexpr float kSizeM = 0.17f;
inline constexpr float kGapM = 0.02f;
struct Pad {
    XrPosef pose{};  // its centre; it faces +Z (toward the eyes), like the screen
    float sizeM = 0.0f;
};
Pad Beside(world_ui::Panel const& screen, float sizeM = kSizeM, float gapM = kGapM);

} // namespace mevr::vr::omni_pad
