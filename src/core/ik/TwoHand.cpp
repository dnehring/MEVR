// SPDX-License-Identifier: GPL-3.0-only
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

bool Shouldered(Vec3 head, Vec3 headForward, Vec3 up, Vec3 grip, bool wasShouldered, ShoulderZone const& zone) {
    Vec3 const u = Normalize(up, {0.0, 1.0, 0.0});
    Vec3 const level = headForward - u * Dot(headForward, u);
    if (!(Length(level) > 1e-6)) {
        return false;  // looking straight up or down: no facing to measure against
    }
    Vec3 const forward = Normalize(level, level);
    Vec3 const right = Cross(forward, u);
    Vec3 const d = grip - head;
    double const m = wasShouldered ? zone.margin : 0.0;
    double const drop = -Dot(d, u);
    double const side = std::abs(Dot(d, right));
    double const ahead = Dot(d, forward);
    return drop >= zone.minDrop - m && drop <= zone.maxDrop + m && side <= zone.maxSide + m && ahead >= zone.minForward - m &&
           ahead <= zone.maxForward + m;
}

ShoulderZone ShoulderZoneAround(Vec3 stock, double tolerance) {
    double const t = std::isfinite(tolerance) && tolerance > 0.0 ? tolerance : 0.12;
    double const drop = -stock.y;
    double const ahead = -stock.z;
    ShoulderZone zone;
    zone.minDrop = std::max(0.0, drop - t);
    zone.maxDrop = drop + t;
    zone.maxSide = std::max(std::abs(stock.x) + t, 0.20);
    zone.minForward = ahead - t;
    zone.maxForward = ahead + t + 0.05;
    return zone;
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

std::optional<SupportZone> MakeSupportZone(Vec3 rearPosition, Quat rearRotation, Vec3 barrelInHand, Vec3 muzzleInHand,
    Vec3 foregripInHand, double handScale, double frontUnits, double pastUnits) {
    if (!Finite(rearPosition) || !Finite(rearRotation) || !Finite(barrelInHand) || !Finite(muzzleInHand) ||
        !Finite(foregripInHand) || !std::isfinite(handScale) || !std::isfinite(frontUnits) || !std::isfinite(pastUnits) ||
        !(Length(barrelInHand) > 1e-9)) {
        return std::nullopt;
    }
    double const scale = handScale > 1e-6 ? handScale : 1.0;
    Quat const r = Normalize(rearRotation);
    Vec3 const barrel = Normalize(Rotate(r, barrelInHand), {1.0, 0.0, 0.0});
    Vec3 const muzzle = rearPosition + Rotate(r, muzzleInHand * scale);
    double const muzzleAlong = Dot(muzzle - rearPosition, barrel);
    // The bore line at `frontUnits` along the barrel from the hand (never past the muzzle).
    double const front = std::min(std::max(frontUnits, 0.0), std::max(muzzleAlong, 0.0));
    SupportZone zone;
    zone.start = muzzle + barrel * (front - muzzleAlong);
    zone.end = muzzle + barrel * std::max(pastUnits, 0.0);
    zone.foregrip = rearPosition + Rotate(r, foregripInHand * scale);
    return zone;
}

double DistanceToSegment(Vec3 point, Vec3 a, Vec3 b) {
    Vec3 const ab = b - a;
    double const lengthSquared = Dot(ab, ab);
    double const t = lengthSquared > 1e-12 ? std::clamp(Dot(point - a, ab) / lengthSquared, 0.0, 1.0) : 0.0;
    return Length(point - (a + ab * t));
}

double DistanceToZone(Vec3 point, SupportZone const& zone) {
    return std::min(DistanceToSegment(point, zone.start, zone.end), Length(point - zone.foregrip));
}

std::optional<Vec3> CapturedSpan(Quat rear, Vec3 rearPosition, Vec3 supportPosition, double length, double minDistance) {
    if (!Finite(rear) || !Finite(rearPosition) || !Finite(supportPosition) || !std::isfinite(length) || !std::isfinite(minDistance)) {
        return std::nullopt;
    }
    Vec3 const delta = supportPosition - rearPosition;
    double const distance = Length(delta);
    if (distance < std::max(minDistance, 0.5) || !(length > 0.5)) {
        return std::nullopt;
    }
    return Rotate(Conjugate(Normalize(rear)), delta) * (length / distance);
}

NativeIk ClassifyNativeIk(double strength) {
    if (!std::isfinite(strength)) {
        return NativeIk::Unknown;
    }
    if (strength <= 0.01) {
        return NativeIk::Off;
    }
    return strength >= 0.99 ? NativeIk::On : NativeIk::Blending;
}

char const* Name(NativeIk state) {
    switch (state) {
    case NativeIk::Off: return "off";
    case NativeIk::Blending: return "blending";
    case NativeIk::On: return "on";
    case NativeIk::Unknown: break;
    }
    return "unknown";
}

} // namespace mevr::ik
