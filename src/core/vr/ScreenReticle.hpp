// SPDX-License-Identifier: MIT
#pragma once

namespace mevr::vr::screen_reticle {

// EGameModes 0..4 are gameplay (foot, vehicle, wheels, squad command).
// The native sniper movie carries the scope view as well as its sight: leave it to the game.
constexpr bool Hide(bool requested, int gameMode, int reticuleMode) {
    return requested && gameMode >= 0 && gameMode <= 4 && reticuleMode >= 0 && reticuleMode < 10 && reticuleMode != 2;
}

// The same rule with the game's own gameplay test already made (engine::adapter::IsHudGameplayMode: ME1's and ME2's
// numbers 0..4 exactly, and ME3's on foot, vehicle, Atlas, wheels, squad command and injured Shepard, whose numbers run to
// 6). ME3's crosshair (game/ScreenReticle's LE3 adapter) asks this one. reticuleMode: -1 unknown (left alone), 2 the scope
// view (left to the game), anything else below 10 a crosshair to hide.
constexpr bool HideInGameplay(bool requested, bool gameplay, int reticuleMode) {
    return requested && gameplay && reticuleMode >= 0 && reticuleMode < 10 && reticuleMode != 2;
}
// Both say the same where ME1's and ME2's numbers are gameplay.
static_assert(HideInGameplay(true, true, 1) == Hide(true, 0, 1) && HideInGameplay(true, true, 2) == Hide(true, 4, 2));
static_assert(HideInGameplay(true, false, 1) == Hide(true, 5, 1) && HideInGameplay(true, true, -1) == Hide(true, 1, -1));
static_assert(HideInGameplay(false, true, 1) == Hide(false, 2, 1));

// A parked automatic cockpit must neither render nor keep pointing its aim marker at the head.
constexpr bool AimCaptureActive(bool parked) {
    return !parked;
}

} // namespace mevr::vr::screen_reticle
