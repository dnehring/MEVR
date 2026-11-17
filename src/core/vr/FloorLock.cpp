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
    double rawCameraZ, double commonWorldZ, double unitsPerMeter, std::optional<double> seatedEyeM) {
    Result out{false, 0.0, rawCameraZ + commonWorldZ};
    bool const heightValid = c.pawn.seated
        ? seatedEyeM && std::isfinite(*seatedEyeM) && *seatedEyeM > 0.2 && *seatedEyeM < 2.7
        : ValidFloor(s, nowMs);
    if (!Applies(c) || !c.pawn.floorValid || !Fresh(c.pawn.timeMs, nowMs) || !heightValid ||
        !std::isfinite(c.pawn.floorZ) || !std::isfinite(rawCameraZ) || !std::isfinite(commonWorldZ) ||
        !std::isfinite(unitsPerMeter) || unitsPerMeter <= 0.0) {
        return out;
    }
    out.active = true;
    double const trim = std::isfinite(c.heightOffsetM) ? std::clamp(c.heightOffsetM, -0.5, 0.5) : 0.0;
    out.centreZ = c.pawn.floorZ + unitsPerMeter * ((c.pawn.seated ? *seatedEyeM : static_cast<double>(s.eyeHeight)) + trim);
    out.correction = out.centreZ - (rawCameraZ + commonWorldZ);
    return out;
}

Result MoveHold::Step(MoveInput const& in, MoveHoldSettings const& settings) {
    Result const& fresh = in.fresh;
    bool const freshUsable = fresh.active && std::isfinite(fresh.centreZ) && std::isfinite(in.floorZ);
    double const dtS = lastMs_ != 0 && in.nowMs > lastMs_ ? std::min(0.1, (in.nowMs - lastMs_) / 1000.0) : 0.0;
    lastMs_ = in.nowMs;
    bool const rising = in.moving && !wasMoving_;
    wasMoving_ = in.moving;
    auto release = [&]() {
        phase_ = Phase::None;
        haveLast_ = freshUsable;
        if (haveLast_) {
            gap_ = in.floorZ - in.rawCameraZ;
            eye_ = fresh.centreZ - in.floorZ;
        }
        return fresh;
    };
    if (!in.eligible || !std::isfinite(in.rawCameraZ) || !std::isfinite(in.commonWorldZ)) {
        return release();
    }
    if (in.moving) {
        if (phase_ == Phase::None) {
            // Only from the tick before the move began: a lock that comes on mid-move has no floor of its own to hold.
            if (!rising || !haveLast_) return release();
            eased_ = false;
        }
        phase_ = Phase::Moving;  // a new move while settling keeps the floor where it is held now
    } else if (phase_ == Phase::None) {
        return release();
    } else {
        if (phase_ == Phase::Moving) {
            phase_ = Phase::Settling;
            endedMs_ = in.nowMs;
        }
        if (!freshUsable) return release();
        double const upm = std::isfinite(in.unitsPerMeter) && in.unitsPerMeter > 0.0 ? in.unitsPerMeter : 100.0;
        settleGap_ = (in.floorZ - in.rawCameraZ) - gap_;
        if (std::abs(settleGap_) <= settings.releaseM * upm) return release();
        if (in.nowMs - endedMs_ >= static_cast<std::uint64_t>(std::max(0.0, settings.waitS) * 1000.0)) {
            double const step = std::max(0.0, settings.easeMS) * upm * dtS;
            gap_ += std::clamp(settleGap_, -step, step);
            eased_ = true;
        }
    }
    if (freshUsable) eye_ = fresh.centreZ - in.floorZ;
    double const centreZ = in.rawCameraZ + gap_ + eye_;
    correction_ = centreZ - (in.rawCameraZ + in.commonWorldZ);
    return Result{true, correction_, centreZ};
}

void StandingReference::Feed(std::uintptr_t skeleton, bool physicalCrouch, bool gameCrouch, bool holstered,
    double stageEyeM, double shepardEyeM, double dt, bool seated) {
    if (skeleton != skeleton_) {
        *this = {};
        skeleton_ = skeleton;
    }
    // Settled once per skeleton: Shepard measured, and the player too unless seated (or calibrated: the caller then
    // doesn't use it). A seeded Shepard keeps both measuring.
    if ((measured_ && (player_ || seated)) || skeleton == 0) return;
    if (physicalCrouch || gameCrouch || !holstered) {
        quietSeconds_ = seconds_ = playerSum_ = shepardSum_ = 0.0;
        return;
    }
    if ((!seated && (!std::isfinite(stageEyeM) || stageEyeM < 0.8 || stageEyeM > 2.5)) ||
        !std::isfinite(shepardEyeM) || shepardEyeM < 0.8 || shepardEyeM > 2.5 || !std::isfinite(dt)) {
        return;
    }
    double const weight = std::clamp(dt, 0.0, 0.05);
    // UnscaledBody may be 500 ms old: let the standing pose replace any cached crouched pose first.
    quietSeconds_ += weight;
    if (quietSeconds_ < 0.5) return;
    seconds_ += weight;
    if (!seated) playerSum_ += stageEyeM * weight;
    shepardSum_ += shepardEyeM * weight;
    if (seconds_ >= 0.5) {
        if (!seated && !player_) player_ = playerSum_ / seconds_;
        if (!measured_) {
            measured_ = shepardSum_ / seconds_;
            shepard_ = measured_;
        }
    }
}

void StandingReference::Seed(std::uintptr_t skeleton, double shepardEyeM) {
    if (skeleton == 0 || !std::isfinite(shepardEyeM) || shepardEyeM < 0.8 || shepardEyeM > 2.5) return;
    if (skeleton != skeleton_) {
        *this = {};
        skeleton_ = skeleton;
    }
    if (measured_) return;
    shepard_ = shepardEyeM;
    seeded_ = true;
}

} // namespace mevr::vr::floor_lock
