// SPDX-License-Identifier: MIT
#include "core/vr/Recenter.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::recenter {
namespace {

constexpr float kRadToDeg = 57.2957795f;

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    // v + 2w (u x v) + 2 u x (u x v)
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const t{2.0f * (u.y * v.z - u.z * v.y), 2.0f * (u.z * v.x - u.x * v.z), 2.0f * (u.x * v.y - u.y * v.x)};
    return {v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
        v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
}

float Dot(XrVector3f const& a, XrVector3f const& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

XrVector3f Cross(XrVector3f const& a, XrVector3f const& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

XrQuaternionf Normalized(XrQuaternionf const& q) {
    float const n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-6f) || !std::isfinite(n)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

} // namespace

Tilt Measure(XrQuaternionf const& head) {
    XrQuaternionf const q = Normalized(head);
    XrVector3f const up = Rotate(q, {0.0f, 1.0f, 0.0f});
    XrVector3f const forward = Rotate(q, {0.0f, 0.0f, -1.0f});
    Tilt tilt;
    tilt.upAngleDeg = std::acos(std::clamp(up.y, -1.0f, 1.0f)) * kRadToDeg;
    tilt.pitchDeg = std::asin(std::clamp(forward.y, -1.0f, 1.0f)) * kRadToDeg;
    // The level up: the base's up made perpendicular to the forward. Looking (nearly) straight up or
    // down there is none, and roll means nothing.
    XrVector3f level{-forward.x * forward.y, 1.0f - forward.y * forward.y, -forward.z * forward.y};
    float const length = std::sqrt(Dot(level, level));
    if (length < 0.05f) {  // within about 3 degrees of straight up or down
        tilt.rollDeg = 0.0f;
        tilt.rollKnown = false;
        return tilt;
    }
    level = {level.x / length, level.y / length, level.z / length};
    // Signed angle from the level up to the head's up, about the forward.
    tilt.rollDeg = std::atan2(Dot(Cross(level, up), forward), Dot(level, up)) * kRadToDeg;
    return tilt;
}

bool UpsideDown(Tilt const& tilt) {
    return tilt.rollKnown && std::abs(tilt.rollDeg) > 90.0f;
}

bool Tilted(Tilt const& tilt, Settings const& settings) {
    return settings.tiltDeg > 0.0f && (tilt.upAngleDeg > settings.tiltDeg || UpsideDown(tilt));
}

void TiltWatch::Note(bool tilted, float dt) {
    if (!(dt > 0.0f) || !std::isfinite(dt)) {
        return;
    }
    dt = std::min(dt, 0.25f);  // a hitch doesn't count as a long look
    seconds_ = tilted ? std::min(seconds_ + dt, 60.0f) : std::max(0.0f, seconds_ - 3.0f * dt);
}

Decision Decide(XrQuaternionf const& head, XrQuaternionf const& yawOnly, Settings const& settings, bool automatic,
    float tiltedSeconds) {
    Decision d;
    d.tilt = Measure(head);
    d.orientation = yawOnly;
    if (!(settings.tiltDeg > 0.0f)) {
        d.why = "tilt correction off";
        return d;
    }
    if (!Tilted(d.tilt, settings)) {
        d.why = "the head is level enough in the base space";
        return d;
    }
    if (automatic) {
        d.why = "the first recenter at start-up (the headset may not be on a head yet): yaw only; recenter again to correct";
        return d;
    }
    if (!UpsideDown(d.tilt) && !(tiltedSeconds >= settings.persistSeconds)) {
        d.why = "tilted, but only for a moment (looking down or up?): yaw only";
        return d;
    }
    d.kind = Kind::Corrected;
    d.orientation = Normalized(head);
    d.why = UpsideDown(d.tilt) ? "the head is rolled past 90 degrees in the base space" : "the head has stayed tilted in the base space";
    return d;
}

XrQuaternionf Compose(XrQuaternionf const& a, XrQuaternionf const& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

} // namespace mevr::vr::recenter
