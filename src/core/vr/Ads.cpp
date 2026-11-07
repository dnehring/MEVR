// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Overlord (https://github.com/Aeka0/Overlord, GPL-3.0, Aeka0): src/client/component/vr/gameplay/
// ads_alignment.hpp, controller_ads.hpp and ads_comfort.hpp (see core/vr/Ads.hpp for what was taken and changed).
#include "core/vr/Ads.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::ads {
namespace {

constexpr float kDeg = 180.0f / 3.14159265358979f;

float Dot(XrVector3f a, XrVector3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XrVector3f Sub(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Scale(XrVector3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
XrVector3f Cross(XrVector3f a, XrVector3f b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float Length(XrVector3f a) { return std::sqrt(Dot(a, a)); }
bool Finite(XrVector3f a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z) && std::abs(a.x) < 1e6f &&
    std::abs(a.y) < 1e6f && std::abs(a.z) < 1e6f; }

bool ValidSightDistance(float meters) {
    return std::isfinite(meters) && meters >= 0.0f && meters <= 10.0f;
}

} // namespace

float FarExtension(float sightToMuzzleM) {
    // Long barrels and suppressors must not consume the user's eye-to-sight reach (Overlord).
    return std::max(0.0f, sightToMuzzleM + kSightReachM - kEntry.farDepth);
}

Sample Measure(XrVector3f const& head, XrVector3f const& headForward, XrVector3f const& muzzle, XrVector3f const& sight,
    XrVector3f const& barrelIn, XrVector3f const& upIn) {
    Sample out;
    if (!Finite(head) || !Finite(headForward) || !Finite(muzzle) || !Finite(sight) || !Finite(barrelIn) || !Finite(upIn)) {
        return out;
    }
    float const barrelLength = Length(barrelIn);
    float const forwardLength = Length(headForward);
    if (!(barrelLength > 1e-6f) || !(forwardLength > 1e-6f)) {
        return out;
    }
    XrVector3f const barrel = Scale(barrelIn, 1.0f / barrelLength);
    XrVector3f up = Sub(upIn, Scale(barrel, Dot(upIn, barrel)));  // the gun's top, square to the barrel
    float const upLength = Length(up);
    if (!(upLength > 1e-3f)) {
        return out;
    }
    up = Scale(up, 1.0f / upLength);
    XrVector3f const right = Cross(barrel, up);
    XrVector3f const offset = Sub(head, muzzle);
    out.depth = -Dot(offset, barrel);
    out.lateral = std::abs(Dot(offset, right));
    out.height = Dot(offset, up);
    out.facingDeg = std::acos(std::clamp(Dot(headForward, barrel) / forwardLength, -1.0f, 1.0f)) * kDeg;
    out.sightToMuzzle = Dot(Sub(muzzle, sight), barrel);
    out.eyeToSight = Length(Sub(sight, head));
    out.sightDepth = Dot(Sub(sight, head), barrel);
    out.valid = std::isfinite(out.depth) && std::isfinite(out.lateral) && std::isfinite(out.height) && std::isfinite(out.facingDeg) &&
        std::isfinite(out.sightToMuzzle) && std::isfinite(out.eyeToSight);
    return out;
}

bool Inside(Sample const& value, bool active, Reach const& reach) {
    float const sightToMuzzle = std::max(value.sightToMuzzle, 0.0f);
    if (!value.valid || !ValidSightDistance(sightToMuzzle)) {
        return false;
    }
    Corridor const& limits = active ? kExit : kEntry;
    bool const corridor = value.depth >= limits.nearDepth && value.depth <= limits.farDepth + FarExtension(sightToMuzzle) &&
        value.lateral <= limits.lateral && value.height >= limits.below && value.height <= limits.above &&
        value.facingDeg <= limits.facingDeg;
    float const reachLimit = active ? reach.exitM : reach.entryM;
    bool const reached = !(reachLimit > 0.0f) || value.eyeToSight <= reachLimit;
    return corridor && reached;
}

float Smooth(float t) {
    if (!(t > 0.0f)) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    // Quintic easing also brings acceleration to zero at the endpoints (Overlord).
    return std::clamp(t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f), 0.0f, 1.0f);
}

float Approach(Sample const& value) {
    float const sightToMuzzle = std::max(value.sightToMuzzle, 0.0f);
    if (!value.valid || !ValidSightDistance(sightToMuzzle) || !std::isfinite(value.facingDeg) || value.facingDeg < 0.0f ||
        value.facingDeg > 180.0f) {
        return 0.0f;
    }
    // Angular approach, independent of the ADS latch: full within 5 degrees, none at 45.
    float const angular = Smooth((45.0f - value.facingDeg) / 40.0f);
    // The same positional corridor as ADS, with smooth edges, so a parallel gun at the hip or side gets nothing.
    float const farLimit = kExit.farDepth + FarExtension(sightToMuzzle);
    return angular * Smooth((value.depth - kExit.nearDepth) / (kEntry.nearDepth - kExit.nearDepth)) *
        Smooth((farLimit - value.depth) / (kExit.farDepth - kEntry.farDepth)) *
        Smooth((kExit.lateral - value.lateral) / (kExit.lateral - kEntry.lateral)) *
        Smooth((value.height - kExit.below) / (kEntry.below - kExit.below)) *
        Smooth((kExit.above - value.height) / (kExit.above - kEntry.above));
}

float DistanceWeight(float meters, bool ads) {
    if (!std::isfinite(meters) || meters < 0.0f) {
        return 0.0f;
    }
    if (ads) {
        // Overlord's thermal profile: full across normal two-hand aiming reach, an extended gun still eases back.
        return Smooth((0.70f - meters) / 0.20f);
    }
    constexpr float kFull = 0.30f;
    return Smooth((kSightReachM - meters) / (kSightReachM - kFull));
}

float PullDistance(Optic optic) {
    switch (optic) {
    case Optic::Holographic: return 0.05f;
    case Optic::LowPower: return 0.10f;
    case Optic::Scope: return 0.15f;
    default: return 0.0f;
    }
}

float Clearance(float sightDepth, float relief, float limit) {
    if (!std::isfinite(sightDepth) || !std::isfinite(relief) || !std::isfinite(limit) || !(limit > 0.0f)) {
        return 0.0f;
    }
    return std::clamp(sightDepth - relief, 0.0f, limit);
}

bool Policy::Update(bool aligned, float dtIn, float dwell) {
    float const dt = std::isfinite(dtIn) ? std::clamp(dtIn, 0.0f, 0.25f) : 0.0f;
    if (!active_) {
        leaving_ = false;
        if (!aligned) {
            entering_ = false;
            timer_ = 0.0f;
        } else if (!entering_) {
            entering_ = true;
            timer_ = 0.0f;
        } else {
            timer_ += dt;
            if (timer_ >= dwell) {
                active_ = true;
                entering_ = false;
                timer_ = 0.0f;
            }
        }
    } else if (aligned) {
        leaving_ = false;
        timer_ = 0.0f;
    } else if (!leaving_) {
        leaving_ = true;
        timer_ = 0.0f;
    } else {
        timer_ += dt;
        if (timer_ >= dwell) {
            active_ = false;
            leaving_ = false;
            timer_ = 0.0f;
        }
    }
    return active_;
}

bool AtShoulder(bool inZone, float facingDeg, bool was) {
    if (!inZone || !std::isfinite(facingDeg) || facingDeg < 0.0f) {
        return false;
    }
    return facingDeg <= (was ? kShoulderFacingOffDeg : kShoulderFacingOnDeg);
}

} // namespace mevr::vr::ads
