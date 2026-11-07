// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Sprint as a faster run (LE1, VR on foot; rules in core/vr/Sprint; `[Hands] sprint`, `sprintStaminaS`, `sprintRefillS`).
// The user (2026-11-06): the game's sprint "could be uncomfortable given the animation style"; shooting stays allowed, with
// a limit. With `sprint = speed` the game's SFXGameModeDefault.StormOn (the sprint button's press) is skipped, so the storm
// posture, its camera and the lowered gun never come; while the button is held and Shepard runs, Pawn.GroundSpeed is the
// game's run speed times its own sprint speed over it (BioPawn StormSpeed / CombatStormSpeed), until our stamina runs out.
// StormOff (the release) ends it. `sprint = game` (the default until checked in the game) changes nothing. Log `sprint:`;
// the snapshot's mod status "sprint". Game thread.
namespace mevr::game::sprint {

// Finds the functions and fields and registers the skip handlers (before the CallFunction hook goes in).
bool TryStartup();
// Each mod tick: `headTrackedView` VR on (the host's view).
void Tick(double deltaSeconds, bool headTrackedView);
void LogStats();
std::string StatusJson();
// The run speed put back.
void Shutdown();

} // namespace mevr::game::sprint
