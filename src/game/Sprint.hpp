// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Sprint as a faster run, one rule for the trilogy (LE1, LE2, LE3; VR on foot; rules in core/vr/Sprint; `[Hands] sprint`,
// `sprintStaminaS`, `sprintRefillS`). The user (2026-11-06): the game's sprint "could be uncomfortable given the animation style";
// shooting stays allowed, with a limit. The owner (2026-11-16): "Unified sprint". With `sprint = speed` the sprint button's
// `SFXGameModeDefault.StormOn` (A held, all three games) runs as the game wrote it and its sprint intent
// (`BioPlayerController.bWantsToStorm`) is withdrawn at once, so the storm posture, its camera and the lowered gun never come;
// while the button is held and Shepard runs, the run speeds the game reads (Pawn.GroundSpeed, BioPawn.CombatGroundSpeed) are
// raised by the game's own sprint speed over the run in effect (BioPawn StormSpeed, ME1's CombatStormSpeed with a weapon
// drawn; Pawn.GetMaxSpeed), until our stamina runs out. StormOff (the release) ends it. Per game: ME3's native admission
// (game/Player), ME2 not in the game's own cover. `sprint = speed` is the default (the user, 2026-11-06); `sprint = game`
// changes nothing. Logs `sprint:`; the snapshot's mod status "sprint". Game thread.
namespace mevr::game::sprint {

// Finds the functions and fields and registers the press's handlers (before the ProcessEvent and CallFunction hooks go in).
bool TryStartup();
// Each mod tick: `headTrackedView` VR on (the host's view).
// LE3 also requires a fresh ownership callback, including menus/calibration/focus. Defaults refuse it.
void Tick(double deltaSeconds, bool headTrackedView, bool controlsAllowed = false, bool (*freshControlsAllowed)() = nullptr);
void LogStats();
std::string StatusJson();
// The run speeds put back.
void Shutdown();

} // namespace mevr::game::sprint
