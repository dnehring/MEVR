// SPDX-License-Identifier: MIT
#include "core/ik/Posture.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik::posture {

std::optional<double> FractionFromFloor(double eyeAboveFloor, double standingEyeHeight) {
    if (!std::isfinite(eyeAboveFloor) || !std::isfinite(standingEyeHeight) || !(standingEyeHeight > 0.0)) {
        return std::nullopt;
    }
    return eyeAboveFloor / standingEyeHeight;
}

std::optional<double> FractionFromRecenter(double headY, double standingHeadY, double assumedStandingEyeHeight) {
    if (!std::isfinite(headY) || !std::isfinite(standingHeadY)) {
        return std::nullopt;
    }
    // The eyes are as far below the assumed standing height as the head dropped below its standing place.
    return FractionFromFloor(assumedStandingEyeHeight + (headY - standingHeadY), assumedStandingEyeHeight);
}

CrouchParams Sanitize(CrouchParams params) {
    CrouchParams out;
    out.crouchBelow = std::isfinite(params.crouchBelow) ? std::clamp(params.crouchBelow, 0.3, 0.95) : out.crouchBelow;
    out.standAbove = std::isfinite(params.standAbove) ? params.standAbove : out.standAbove;
    out.standAbove = std::clamp(out.standAbove, out.crouchBelow + 0.02, 0.99);
    out.dwellSeconds = std::isfinite(params.dwellSeconds) ? std::clamp(params.dwellSeconds, 0.0, 2.0) : out.dwellSeconds;
    return out;
}

CrouchParams WithMinimumDrop(CrouchParams params, double standingEye, double minDropMeters, std::optional<double> crouchedFraction) {
    if (!std::isfinite(minDropMeters) || !(minDropMeters > 0.0) || !std::isfinite(standingEye) || !(standingEye > 0.0)) {
        return params;
    }
    double limit = 1.0 - minDropMeters / standingEye;
    if (crouchedFraction && std::isfinite(*crouchedFraction) && *crouchedFraction > 0.0) {
        limit = std::max(limit, *crouchedFraction + kCrouchedReachMeters / standingEye);  // the calibrated crouch still crouches
    }
    if (!(params.crouchBelow > limit)) {
        return params;  // already deep enough
    }
    double const gap = std::max(params.standAbove - params.crouchBelow, 0.02);
    params.crouchBelow = limit;
    params.standAbove = limit + gap;
    return Sanitize(params);
}

bool LeanGuard::Update(LeanSample const& s, CrouchParams const& params, double ratio) {
    bool const usable = s.fraction && std::isfinite(*s.fraction) && std::isfinite(s.headX) && std::isfinite(s.headZ) &&
        std::isfinite(s.standingEye) && s.standingEye > 0.0;
    if (!usable) {
        return lean_;  // nothing new: hold (the detector holds too)
    }
    double const eye = *s.fraction * s.standingEye;
    if (!anchored_ || *s.fraction >= params.standAbove) {
        // Standing (or the first look): the head's place and height are where a drop starts from.
        anchored_ = true;
        x_ = s.headX;
        z_ = s.headZ;
        eye_ = eye;
        horizontal_ = drop_ = 0.0;
        lean_ = false;
        return lean_;
    }
    horizontal_ = std::hypot(s.headX - x_, s.headZ - z_);
    drop_ = std::max(0.0, eye_ - eye);
    lean_ = std::isfinite(ratio) && ratio > 0.0 && drop_ > 0.0 && horizontal_ >= ratio * drop_;
    return lean_;
}

CrouchEvent CrouchDetector::Update(std::optional<double> fraction, double deltaSeconds, CrouchParams const& params) {
    if (!fraction || !std::isfinite(*fraction)) {
        dwell_ = 0.0;
        return CrouchEvent::None;
    }
    bool const past = crouched_ ? *fraction > params.standAbove : *fraction < params.crouchBelow;
    if (!past) {
        dwell_ = 0.0;
        return CrouchEvent::None;
    }
    dwell_ += std::max(deltaSeconds, 0.0);
    if (dwell_ < params.dwellSeconds) {
        return CrouchEvent::None;
    }
    dwell_ = 0.0;
    crouched_ = !crouched_;
    return crouched_ ? CrouchEvent::Crouch : CrouchEvent::Stand;
}

ArbiterStep CrouchArbiter::Update(ArbiterInput const& input) {
    ArbiterStep step;
    if (input.physical != CrouchEvent::None) {
        givenUp_ = false;  // a real change: the player's posture is followed afresh
        undone_ = 0;
    }
    if (seen_ && input.gameWants != last_) {
        step.external = true;  // the button, a script or sprinting changed it: not ours any more
        owns_ = false;
        bool const against = input.follow && !input.button && input.physical == CrouchEvent::None &&
            input.gameWants != input.physicalCrouched;
        if (against && !givenUp_) {
            // An undo the caller can explain (the pawn moving, a sprint, a vault: the game's passing reasons) never counts; one
            // soon after our put-back with no such reason does.
            bool const quick = restoredAt_ >= 0.0 && input.nowSeconds - restoredAt_ < kInsistSeconds;
            if (!input.explained) {
                undone_ = quick ? undone_ + 1 : 0;
            }
            if (undone_ >= kInsistTimes) {
                step.insisted = true;  // the game keeps putting it back: its rule (a script) wins
                givenUp_ = true;
                pending_.reset();
                restore_ = false;
                tries_ = 0;
            } else {
                step.restoring = true;
                pending_ = input.physicalCrouched;
                restore_ = true;
                tries_ = 0;
            }
        } else if (restore_) {
            pending_.reset();  // the game went back by itself (or the button did): nothing to put back
            restore_ = false;
            tries_ = 0;
        }
    }
    seen_ = true;
    last_ = input.gameWants;
    if (input.physical != CrouchEvent::None) {
        pending_ = input.physical == CrouchEvent::Crouch;
        restore_ = false;
        tries_ = 0;
    }
    if (!pending_) {
        return step;
    }
    if (*pending_ == input.gameWants) {
        // Already there (the button got there first, or the game did): a crouch found there is ours from
        // now on, so standing up releases it.
        step.alreadyThere = !restore_;
        owns_ = *pending_;
        pending_.reset();
        restore_ = false;
        return step;
    }
    if (!input.canApply || (restore_ && !input.canRestore)) {
        step.deferred = true;
        return step;
    }
    if (tries_ >= kMaxTries) {
        step.gaveUp = true;
        pending_.reset();
        restore_ = false;
        tries_ = 0;
        return step;
    }
    ++tries_;
    if (restore_) {
        restoredAt_ = input.nowSeconds;
    }
    step.action = *pending_ ? CrouchAction::Crouch : CrouchAction::Stand;
    return step;
}

void CrouchArbiter::Pressed(bool gameWantsNow) {
    seen_ = true;
    last_ = gameWantsNow;
    owns_ = false;
    pending_.reset();
    restore_ = false;
    tries_ = 0;
}

void CrouchArbiter::Applied(bool gameWantsNow) {
    seen_ = true;
    last_ = gameWantsNow;
    bool const took = pending_ && *pending_ == gameWantsNow;
    // A crouch we asked for is ours; a stand (or a release outside a physical change) leaves nothing ours.
    owns_ = gameWantsNow && (took || owns_);
    if (took) {
        pending_.reset();
        restore_ = false;
        tries_ = 0;
    }
}

void CrouchArbiter::Observe(bool gameWants) {
    if (seen_ && gameWants != last_) {
        owns_ = false;
    }
    seen_ = true;
    last_ = gameWants;
    pending_.reset();
    restore_ = false;
    tries_ = 0;
}

void CrouchArbiter::Forget() {
    seen_ = false;
    last_ = false;
    owns_ = false;
    pending_.reset();
    tries_ = 0;
    restore_ = false;
    restoredAt_ = -1.0;
    undone_ = 0;
    givenUp_ = false;
}

void CrouchArbiter::ResetTracking(CrouchDetector& detector, std::optional<bool> gameWants) {
    pending_.reset();
    restore_ = false;
    tries_ = 0;
    if (gameWants) Observe(*gameWants);
    detector.Reset(Owns() && gameWants.value_or(last_));
}

bool CrouchArbiter::FinishRelease(std::optional<bool> gameWants) {
    if (!owns_) return true;
    if (gameWants != false) return false;
    Applied(false);
    return true;
}

bool RefuseCoverSnap(CoverSnapInput const& in) {
    return in.noCover && in.onFoot && in.headTracked && !in.calibrating;
}

} // namespace mevr::ik::posture
