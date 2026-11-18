// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>

// The sequels' shouldered gun without the game's zoom (docs/trilogy-plan.md "Answered 2026-11-18, later", option B; the ME2
// and ME3 audits' WPN-09; research/le2-vrik.md "The shouldered gun on the sequels"). ME2's and ME3's LT is the game's zoom
// (SFXGameModeDefault.TightAim -> SFXWeapon.SetZoomed(true)), which also zooms the camera, sways a sniper's scope, slows time
// for ME2's sniper and refuses melee and the roll. So the support grip holds no LT there (engine::adapter::kSupportGripHoldsLt)
// and, while the gun is shouldered (both hands on it at the shoulder: game/SupportGrip's aimed fire) or a scope's picture is up
// (game/Sights), with [Sights] aimedFire, each of the shot's own calculations (engine::adapter::kZoomedCalls: the per-tick
// accuracy, the spread's range, ME3's sniper damage) runs with the held weapon's bIsZoomed set for that call only: a
// CallFunction scoped handler sets it on the way in, journalled, and puts it back in the hook's __finally. The camera, the
// sway, the time dilation and the refusals never see it. ME1: nothing here (its grip holds LT, the game's own aimed fire).
// Game thread.
namespace mevr::game::zoomed_shots {

// Registers the scoped handlers (before the CallFunction hook goes in). False on ME1, before the layer is up, or when no
// function is found (each found one still works).
bool TryStartup();
// Each tick (game/SupportGrip): the gun shouldered (both hands, at the shoulder), a scope's picture up, [Sights] aimedFire.
void Update(bool shouldered, bool scoped, bool aimedFire);
// Whether the shot's calculations are zoomed now (game/Sights' scoped rewrite stands down then: it would compute the same).
bool Active();
// game/Sights' aim-error post-handler on the sequels: each shot's answer, kept against whether the call was zoomed (the
// heartbeat's and snapshot's shouldered against hip spread).
void NoteAimError(float error);
// `mevr zoomshots [on|off|auto]`: on opens the gate whatever the hands do (a test without a support grip at the shoulder),
// off keeps it shut, auto the hands' and the scope's rule.
std::string Console(std::string_view args);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::zoomed_shots
