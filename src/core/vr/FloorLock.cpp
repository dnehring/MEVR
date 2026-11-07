// SPDX-License-Identifier: MIT
#include "core/vr/FloorLock.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::floor_lock {

bool Applies(Context const& c) {
    return c.enabled && c.world && c.firstPerson && c.pawn.onFoot && c.pawn.pawn && c.pawn.cameraOwned &&
        !c.pawn.cover && !c.cutscene && !c.screen && !c.frontEnd && !c.galaxyCamera && !c.menu;
}

bool Fresh(std::uint64_t sampleMs, std::uint64_t nowMs) {
    return sampleMs != 0 && nowMs >= sampleMs && nowMs - sampleMs <= 250;
}

bool ValidFloor(floor::State const& s, std::uint64_t nowMs) {
    return s.valid && (s.source == floor::Source::Stage || s.source == floor::Source::LocalFloor) &&
        Fresh(s.timeMs, nowMs) && std::isfinite(s.eyeHeight) && s.eyeHeight > 0.2f && s.eyeHeight < 2.7f;
}

Result Correct(Context const& c, floor::State const& s, std::uint64_t nowMs,
    double rawCameraZ, double commonWorldZ, double unitsPerMeter) {
    Result out{false, 0.0, rawCameraZ + commonWorldZ};
    if (!Applies(c) || !c.pawn.floorValid || !Fresh(c.pawn.timeMs, nowMs) || !ValidFloor(s, nowMs) ||
        !std::isfinite(c.pawn.floorZ) || !std::isfinite(rawCameraZ) || !std::isfinite(commonWorldZ) ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0) {
        return out;
    }
    out.active = true;
    out.centreZ = c.pawn.floorZ + unitsPerMeter * static_cast<double>(s.eyeHeight);
    out.correction = out.centreZ - (rawCameraZ + commonWorldZ);
    return out;
}

void StandingReference::Feed(std::uintptr_t skeleton, bool physicalCrouch, bool gameCrouch, bool holstered,
    double stageEyeM, double shepardEyeM, double dt) {
    if (skeleton != skeleton_) {
        *this = {};
        skeleton_ = skeleton;
    }
    if (shepard_ || skeleton == 0) return;
    if (physicalCrouch || gameCrouch || !holstered) {
        quietSeconds_ = seconds_ = playerSum_ = shepardSum_ = 0.0;
        return;
    }
    if (!std::isfinite(stageEyeM) || !std::isfinite(shepardEyeM) ||
        stageEyeM < 0.8 || stageEyeM > 2.5 || shepardEyeM < 0.8 || shepardEyeM > 2.5 || !std::isfinite(dt)) {
        return;
    }
    double const weight = std::clamp(dt, 0.0, 0.05);
    // UnscaledBody may be 500 ms old: let the standing pose replace any cached crouched pose first.
    quietSeconds_ += weight;
    if (quietSeconds_ < 0.5) return;
    seconds_ += weight;
    playerSum_ += stageEyeM * weight;
    shepardSum_ += shepardEyeM * weight;
    if (seconds_ >= 0.5) {
        player_ = playerSum_ / seconds_;
        shepard_ = shepardSum_ / seconds_;
    }
}

} // namespace mevr::vr::floor_lock
