// SPDX-License-Identifier: MIT
#pragma once

namespace mevr::vr::screen_reticle {

// EGameModes 0..4 are gameplay (foot, vehicle, wheels, squad command).
// The native sniper movie carries the scope view as well as its sight: leave it to the game.
constexpr bool Hide(bool requested, int gameMode, int reticuleMode) {
    return requested && gameMode >= 0 && gameMode <= 4 && reticuleMode >= 0 && reticuleMode < 10 && reticuleMode != 2;
}

// A parked automatic cockpit must neither render nor keep pointing its aim marker at the head.
constexpr bool AimCaptureActive(bool parked) {
    return !parked;
}

} // namespace mevr::vr::screen_reticle
