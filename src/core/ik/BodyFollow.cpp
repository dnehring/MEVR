#include "core/ik/BodyFollow.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mevr::ik {
namespace {

double Wrap(double a) {
    while (a > std::numbers::pi) a -= 2.0 * std::numbers::pi;
    while (a <= -std::numbers::pi) a += 2.0 * std::numbers::pi;
    return a;
}

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

Vec3 HeadPivot(Vec3 eyes, Quat orientation, NeckModel const& neck) {
    // The head's own axes: +Y up, +Z back (OpenXR).
    return eyes + Rotate(Normalize(orientation), Vec3{0.0, -neck.down, neck.back});
}

char const* Name(FollowAnchor anchor) {
    switch (anchor) {
    case FollowAnchor::Start: return "start";
    case FollowAnchor::Recenter: return "recenter";
    case FollowAnchor::Jump: return "tracking jump";
    case FollowAnchor::None: break;
    }
    return "none";
}

FollowState BodyFollow::Update(FollowInput const& in) {
    FollowState out;
    if (!Finite(in.pivot) || !std::isfinite(in.headYaw) || !std::isfinite(in.facingYaw)) {
        return last_;  // nothing new: keep what was there
    }
    double const dt = std::isfinite(in.deltaSeconds) ? std::max(0.0, in.deltaSeconds) : 0.0;
    Vec3 const flat{in.pivot.x, 0.0, in.pivot.z};
    FollowAnchor anchor = FollowAnchor::None;
    if (!active_) {
        anchor = FollowAnchor::Start;
    } else if (in.haveRecenters && haveRecenters_ && in.recenters != recenters_) {
        anchor = FollowAnchor::Recenter;
    } else if (Length(in.pivot - lastPivot_) > settings_.jumpMeters ||
               std::abs(Wrap(in.headYaw - lastHeadYaw_)) > settings_.jumpRadians) {
        anchor = FollowAnchor::Jump;
    }
    if (anchor != FollowAnchor::None) {
        feet_ = flat;
        yaw_.Reset(in.facingYaw);
        active_ = true;
    } else {
        Vec3 const off = flat - feet_;
        double const distance = Length(off);
        double const radius = std::max(0.0, settings_.leanRadius);
        if (distance > radius) {
            feet_ = flat - off * (radius / distance);  // a step: dragged along at the radius
        } else {
            feet_ = feet_ + off * std::clamp(settings_.settlePerSecond * dt, 0.0, 1.0);
        }
        yaw_.Update(in.facingYaw, dt);
    }
    haveRecenters_ = in.haveRecenters;
    recenters_ = in.recenters;
    lastPivot_ = in.pivot;
    lastHeadYaw_ = in.headYaw;
    out.pivot = in.pivot;
    out.feet = {feet_.x, in.pivot.y, feet_.z};
    out.lean = {in.pivot.x - feet_.x, 0.0, in.pivot.z - feet_.z};
    out.yaw = yaw_.Value();
    out.anchored = anchor;
    last_ = out;
    return out;
}

} // namespace mevr::ik
