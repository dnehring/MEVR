// SPDX-License-Identifier: MIT
#include "core/ik/GunKick.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mevr::ik::gun_kick {
namespace {

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double Clamp01(double v) {
    return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : 0.0;
}

Quat AxisAngle(Vec3 axis, double radians) {
    double const half = 0.5 * radians;
    double const s = std::sin(half);
    return Normalize(Quat{axis.x * s, axis.y * s, axis.z * s, std::cos(half)});
}

// The internal state never runs away (synthetic shots in a loop, a long hitch): a few caps' worth at most.
constexpr double kStateCaps = 3.0;

} // namespace

Offset ShotKick(double recoilDeg, double scale, Tuning const& tuning) {
    double const recoil = std::isfinite(recoilDeg) ? std::max(recoilDeg, 0.0) : 0.0;
    double const strength = tuning.fullRecoilDeg > 0.0 ? std::clamp(recoil / tuning.fullRecoilDeg, 0.0, 1.0) : 1.0;
    double const s = std::isfinite(scale) ? std::clamp(scale, 0.0, kMaxScale) : 0.0;
    return {s * (tuning.minAngleDeg + (tuning.maxAngleDeg - tuning.minAngleDeg) * strength),
        s * (tuning.minBackM + (tuning.maxBackM - tuning.minBackM) * strength)};
}

double SoftCap(double x, double cap, double knee) {
    if (!std::isfinite(x) || !(cap > 0.0)) {
        return 0.0;
    }
    double const k = std::clamp(std::isfinite(knee) ? knee : 0.0, 0.0, 1.0) * cap;
    double const a = std::abs(x);
    if (a <= k) {
        return x;
    }
    double const room = cap - k;
    double const capped = room > 0.0 ? k + room * std::tanh((a - k) / room) : cap;
    return x < 0.0 ? -capped : capped;
}

void Spring::Kick(Offset peak, Tuning const& tuning) {
    // From rest a push of velocity v0 gives x(t) = v0 t exp(-omega t), whose peak (at t = 1/omega) is v0 / (omega e).
    double const gain = tuning.omega * std::numbers::e;
    double const peaks[2] = {peak.angleDeg, peak.backM};
    for (int c = 0; c < 2; ++c) {
        if (std::isfinite(peaks[c]) && peaks[c] > 0.0) {
            v_[c] += peaks[c] * gain;
        }
    }
    double const caps[2] = {tuning.capAngleDeg, tuning.capBackM};
    for (int c = 0; c < 2; ++c) {
        double const limit = kStateCaps * caps[c] * gain;
        v_[c] = std::clamp(v_[c], -limit, limit);
    }
}

void Spring::Step(double dt, Tuning const& tuning) {
    if (!std::isfinite(dt) || !(dt > 0.0)) {
        return;
    }
    double const w = tuning.omega > 0.0 ? tuning.omega : 45.0;
    double const decay = std::exp(-w * dt);
    double const caps[2] = {tuning.capAngleDeg, tuning.capBackM};
    for (int c = 0; c < 2; ++c) {
        // x'' + 2 w x' + w^2 x = 0: x(t) = (x0 + (v0 + w x0) t) e^-wt, v(t) = (v0 - w (v0 + w x0) t) e^-wt.
        double const x0 = x_[c];
        double const v0 = v_[c];
        double const b = v0 + w * x0;
        x_[c] = (x0 + b * dt) * decay;
        v_[c] = (v0 - w * b * dt) * decay;
        double const limit = kStateCaps * caps[c];
        if (std::abs(x_[c]) > limit) {
            x_[c] = std::clamp(x_[c], -limit, limit);
            v_[c] = 0.0;
        }
        if (!std::isfinite(x_[c]) || !std::isfinite(v_[c])) {
            x_[c] = v_[c] = 0.0;
        }
    }
    if (!Moving()) {
        Reset();
    }
}

Offset Spring::Value(Tuning const& tuning) const {
    return {SoftCap(x_[0], tuning.capAngleDeg, tuning.knee), SoftCap(x_[1], tuning.capBackM, tuning.knee)};
}

Offset Spring::Raw() const {
    return {x_[0], x_[1]};
}

bool Spring::Moving() const {
    return std::abs(x_[0]) > 1e-3 || std::abs(x_[1]) > 1e-5 || std::abs(v_[0]) > 1e-2 || std::abs(v_[1]) > 1e-4;
}

void Spring::Reset() {
    x_[0] = x_[1] = v_[0] = v_[1] = 0.0;
}

Offset Shaped(Offset kick, double twoHandBlend, double raisedWeight, Tuning const& tuning) {
    double const hands = 1.0 - (1.0 - std::clamp(tuning.twoHanded, 0.0, 1.0)) * Clamp01(twoHandBlend);
    double const back = 1.0 - Clamp01(raisedWeight);
    return {(std::isfinite(kick.angleDeg) ? kick.angleDeg : 0.0) * hands, (std::isfinite(kick.backM) ? kick.backM : 0.0) * hands * back};
}

Quat Motion::Rotation() const {
    return AxisAngle(axis, angle);
}

std::optional<Motion> HandMotion(Quat handRotation, Vec3 barrelInHand, Vec3 upInHand, double angleRadians, double backUnits) {
    if (!Finite(barrelInHand) || !Finite(upInHand) || !std::isfinite(angleRadians) || !std::isfinite(backUnits) ||
        !std::isfinite(handRotation.x) || !std::isfinite(handRotation.y) || !std::isfinite(handRotation.z) ||
        !std::isfinite(handRotation.w) || !(Length(barrelInHand) > 1e-9)) {
        return std::nullopt;
    }
    Quat const hand = Normalize(handRotation);
    Vec3 const barrel = ik::Normalize(Rotate(hand, barrelInHand));
    Vec3 up = Rotate(hand, upInHand);
    up = up - barrel * Dot(up, barrel);
    if (!(Length(up) > 1e-6)) {
        return std::nullopt;
    }
    up = ik::Normalize(up);
    // Turning the barrel toward the top: the axis barrel x up (the algebra doesn't depend on handedness, core/ik/Rigid).
    Motion m;
    m.axis = ik::Normalize(Cross(barrel, up));
    m.angle = angleRadians;
    m.translation = barrel * -backUnits;
    return m;
}

void ApplyToHand(Motion const& motion, Vec3& wrist, Quat& rotation) {
    wrist = wrist + motion.translation;
    rotation = Normalize(motion.Rotation() * rotation);
}

void ApplyToHeld(Motion const& motion, Vec3 pivot, double blend, Vec3& wrist, Quat* rotation) {
    double const b = Clamp01(blend);
    if (!(b > 0.0)) {
        return;
    }
    Motion part = motion;
    part.angle *= b;
    part.translation = part.translation * b;
    Quat const turn = part.Rotation();
    wrist = pivot + Rotate(turn, wrist - pivot) + part.translation;
    if (rotation != nullptr) {
        *rotation = Normalize(turn * *rotation);
    }
}

Written ToWritten(Motion const& motion, Vec3 writtenHand) {
    return {motion.Rotation(), writtenHand, motion.translation};
}

// The hand's goal (p, R) became (p + t, q R): a point L on the hand or gun, at x = p + R L, is drawn at x' = W + q R L with W the
// written hand (p + t when it reached its goal). So x = W - t + q^-1 (x' - W), and x' = W + q (x - W + t).
Vec3 UnkickPoint(Written const& w, Vec3 p) {
    return w.pivot - w.translation + Rotate(Conjugate(w.rotation), p - w.pivot);
}

Vec3 UnkickDirection(Written const& w, Vec3 d) {
    return Rotate(Conjugate(w.rotation), d);
}

Vec3 RekickPoint(Written const& w, Vec3 p) {
    return w.pivot + Rotate(w.rotation, p - w.pivot + w.translation);
}

Vec3 RekickDirection(Written const& w, Vec3 d) {
    return Rotate(w.rotation, d);
}

double AngleDeg(Written const& w) {
    return AngleBetween(Quat{}, w.rotation) * 180.0 / std::numbers::pi;
}

double MoveLength(Written const& w) {
    return Length(w.translation);
}

} // namespace mevr::ik::gun_kick
