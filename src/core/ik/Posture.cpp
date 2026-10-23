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
    if (seen_ && input.gameWants != last_) {
        step.external = true;  // the button, a script or sprinting changed it: not ours any more
        owns_ = false;
    }
    seen_ = true;
    last_ = input.gameWants;
    if (input.physical != CrouchEvent::None) {
        pending_ = input.physical == CrouchEvent::Crouch;
        tries_ = 0;
    }
    if (!pending_) {
        return step;
    }
    if (*pending_ == input.gameWants) {
        // Already there (the button got there first, or the game did): a crouch found there is ours from
        // now on, so standing up releases it.
        step.alreadyThere = true;
        owns_ = *pending_;
        pending_.reset();
        return step;
    }
    if (!input.canApply) {
        step.deferred = true;
        return step;
    }
    if (tries_ >= kMaxTries) {
        step.gaveUp = true;
        pending_.reset();
        tries_ = 0;
        return step;
    }
    ++tries_;
    step.action = *pending_ ? CrouchAction::Crouch : CrouchAction::Stand;
    return step;
}

void CrouchArbiter::Applied(bool gameWantsNow) {
    seen_ = true;
    last_ = gameWantsNow;
    bool const took = pending_ && *pending_ == gameWantsNow;
    // A crouch we asked for is ours; a stand (or a release outside a physical change) leaves nothing ours.
    owns_ = gameWantsNow && (took || owns_);
    if (took) {
        pending_.reset();
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
    tries_ = 0;
}

void CrouchArbiter::Forget() {
    seen_ = false;
    last_ = false;
    owns_ = false;
    pending_.reset();
    tries_ = 0;
}

} // namespace mevr::ik::posture
