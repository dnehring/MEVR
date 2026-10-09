// TwoHandRotation adapts Overlord's `aimed_rotation` (src/client/component/vr/gameplay/
// weapon_profile.hpp) and the coincident-hands rule of its grip_presenter.hpp.
// Overlord: Copyright (C) Aeka0, https://github.com/Aeka0/Overlord, GPL-3.0. Changes: our Vec3/Quat
// types, an explicit minimum hand distance, and finite checks.

#include "core/ik/TwoHand.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik {
namespace {

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool Finite(Quat q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

} // namespace

Quat Slerp(Quat a, Quat b, double t) {
    a = Normalize(a);
    b = Normalize(b);
    t = std::clamp(t, 0.0, 1.0);
    double dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (dot < 0.0) {  // the short way round
        b = {-b.x, -b.y, -b.z, -b.w};
        dot = -dot;
    }
    if (dot > 0.9995) {  // nearly the same: a normalized lerp
        return Normalize(Quat{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t});
    }
    double const angle = std::acos(std::clamp(dot, -1.0, 1.0));
    double const s = std::sin(angle);
    double const wa = std::sin((1.0 - t) * angle) / s;
    double const wb = std::sin(t * angle) / s;
    return Normalize(Quat{a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

std::optional<Quat> TwoHandRotation(Quat rear, Vec3 spanInHand, Vec3 rearPosition, Vec3 supportPosition,
    double minDistance) {
    if (!Finite(rear) || !Finite(spanInHand) || !Finite(rearPosition) || !Finite(supportPosition) ||
        !std::isfinite(minDistance)) {
        return std::nullopt;
    }
    Vec3 const delta = supportPosition - rearPosition;
    double const distance = Length(delta);
    if (distance < std::max(minDistance, 0.5) || Length(spanInHand) < 0.5) {
        return std::nullopt;  // coincident or crossing hands can't define the gun's axis
    }
    rear = Normalize(rear);
    Vec3 const span = Rotate(rear, spanInHand);
    return Normalize(FromTo(span, delta) * rear);
}

GunOffsets OffsetsOnGun(Vec3 point, Vec3 barrelInHand, Vec3 muzzleInHand) {
    Vec3 const barrel = Normalize(barrelInHand, {1.0, 0.0, 0.0});
    double const along = Dot(point, barrel);
    Vec3 const fromMuzzle = point - muzzleInHand;
    Vec3 const off = fromMuzzle - barrel * Dot(fromMuzzle, barrel);
    return {along, Length(off)};
}

bool OnGun(Vec3 point, Vec3 barrelInHand, Vec3 muzzleInHand) {
    if (!Finite(point) || !Finite(barrelInHand) || !Finite(muzzleInHand) || Length(barrelInHand) < 1e-9 ||
        Length(point) < kForegripMinSpan) {
        return false;
    }
    GunOffsets const o = OffsetsOnGun(point, barrelInHand, muzzleInHand);
    double const muzzleAlong = Dot(muzzleInHand, Normalize(barrelInHand, {1.0, 0.0, 0.0}));
    return o.along >= -kForegripBehindGrip && o.along <= muzzleAlong + kForegripPastMuzzle && o.off <= kForegripOffBarrel;
}

Vec3 UnderGrip(Vec3 barrelInHand, Vec3 upInHand, double down, double forward) {
    Vec3 const barrel = Normalize(barrelInHand, {1.0, 0.0, 0.0});
    Vec3 up = upInHand - barrel * Dot(upInHand, barrel);
    up = Normalize(up, {0.0, 0.0, 1.0});
    return barrel * forward - up * down;
}

} // namespace mevr::ik
