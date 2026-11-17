// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <string>

// The reload by hand on the sequels (docs/trilogy-plan.md T26, research/le2-vrik.md "The reload by hand"): ME2 and ME3
// reload thermal clips (ME1 has heat and the vent: n/a there, this module stands down). With a gun in the right hand, the
// LEFT hand squeezes its grip at the clip pouch (a body slot on the left hip, the holsters' body frame and scale, [Reload]
// pouch), carries the clip to the gun and pushes it into the receiver (core/ik/Vent's zone in the gun's frame, [Vent]
// zone*: one receiver for the vent and the reload). core/vr/ReloadGesture qualifies the gesture; vr::reload::Coordinator
// (core/vr/PhysicalReloadRequest) turns its signals into one correlated request; this adapter asks the game's own reload
// for it: SFXGameModeDefault.TryReload on the player controller's default game mode (the button's exec function, which
// calls Weapon.TryReload through the script VM, state-aware: a weapon already reloading ignores it), never DoReload,
// never an R press. The game is the authority for the ammo: the adapter only observes it (feedback for the coordinator):
//   accepted   BioPlayerController.IsReloading turned true (or the weapon entered its reload state);
//   finished   no longer reloading, with the clip's rounds back (the magazine's count up, or the used count down);
//   aborted    no longer reloading with the counts unchanged; rejected: never reloading within 0.6 s, counts unchanged.
// The game's own R and its automatic reload stay. Logs `reload:`; snapshot "mod"."reload"; `mevr reload ...`.
namespace mevr::game::reload {

// What the host offers this tick (Mod.cpp fills it from HostServices).
struct Link {
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
};

// Finds the game's names (the sequels only). False until the script packages have loaded, and always on ME1.
bool TryStartup();
// Each tick after game/vent's Tick (the grenades', the vent's and the support grip's claims of a left squeeze come first).
void Tick(double deltaSeconds, Link const& link);
// This tick the left grip is the reload's (a clip held): not a free hand for other gestures.
bool ClaimedLeft();
std::string StatusJson();
void LogStats();
void Shutdown();

// `mevr reload ...` (game/ConsoleRequest, the game thread):
//   (nothing) | status   the held gun's ammo (magazine, used, spare), whether the game would reload, the gesture's state
//   try                  the same native request a gesture makes (SFXGameModeDefault.TryReload), followed like one:
//                        the adapter's check without the controllers
std::string Command(std::string const& args);

} // namespace mevr::game::reload
