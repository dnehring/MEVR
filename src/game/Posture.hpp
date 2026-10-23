// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Floor.hpp"

#include <optional>
#include <string>

// Physical cover and crouch (research/23). [Hands] noCover, physicalCrouch, crouchBelow, standAbove,
// crouchDwellS in <GAME>MEVR.ini (game/hand_calibration). Both act only in VR on foot: the host's
// head-tracked view up, game mode 0 (not the Mako, conversations, menus, cutscenes), the player a BioPawn.
//
// No cover snapping (noCover): ME1 takes cover by itself when the player walks into a wall or low cover
// (no button: A only leaves it), and its cover camera is third person. In VR the player takes cover by
// crouching or leaning behind things, so engine/cover_acquire refuses the native cover test for the
// player's pawn: nothing is written, the game never enters cover. Refused entries (the game's own trace
// found cover the move pushed into) are logged once per change. A cover Shepard is already in (taken
// before, or by a script) is left the A button's way, SFXGameModeDefault.TryExitCover, retried while it
// refuses (an animated transition, the scope); after 2 s the game's own cover-lost path,
// BioPawn.LeaveCover. Without the hook (it failed) every entry is left that way at once.
//
// Physical crouch (physicalCrouch): the eyes' height as a fraction of the standing eye height
// (ik::posture): from the host's floor (STAGE / LOCAL_FLOOR) over a standing height learned from it as
// game/holsters learns it (taken at a recenter, then a slow-rising maximum), or without a floor from the
// head's drop below where it stood at the recenter over [Holsters] eyeHeightM. Below crouchBelow for
// crouchDwellS: BioPawn.ShouldCrouch(true) through ProcessEvent (what L3's ToggleCrouch calls); above
// standAbove: ShouldCrouch(false). Only on changes of the physical posture, so L3 (a toggle) and the
// game's own changes stay until the next physical crouch or stand; a change while sprinting waits (the
// game's ToggleCrouch refuses then too). A crouch we made is released (Shepard stands) when the setting
// goes off, VR goes away for 2 s, or the mod unloads.
//
// Logged: the standing height and its source, every crouch and stand with the head height, the crouch
// changing without us, the view's height above Shepard before and after (the game's crouch camera is 45
// units lower in combat: research/23); a heartbeat line; "posture" in the snapshot's mod status. Game
// thread. LE1 only.
namespace mevr::game::posture {

// What the host offers this tick (Mod.cpp fills it from HostServices).
struct Link {
    // v10: the head's height above the floor (nullopt: an older host, or sizes differ).
    std::optional<vr::floor::State> floor;
};

// Finds the script functions. False until the script packages have loaded (or not LE1).
bool TryStartup();
// noCover is on: Mod.cpp puts engine/cover_acquire's hook in (once).
bool WantsCoverHook();
void Tick(double deltaSeconds, Link const& link);
std::string StatusJson();
void LogStats();
// Releases a crouch we made (unless the process is exiting) and stops refusing cover.
void Shutdown(bool processExiting);

} // namespace mevr::game::posture
