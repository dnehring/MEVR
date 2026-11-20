// SPDX-License-Identifier: MIT
#include "core/ik/Jiggle.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::ik::jiggle {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTeleportM = 0.5;
constexpr double kTeleportMS = 10.0;
constexpr double kMaxStepS = 0.25;

Quat AxisAngle(Vec3 axis, double angle) {
    double const s = std::sin(angle * 0.5);
    return {axis.x * s, axis.y * s, axis.z * s, std::cos(angle * 0.5)};
}

bool Finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

double& At(Vec3& v, int i) {
    return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}
double At(Vec3 const& v, int i) {
    return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}

Axis const& AxisOf(Settings const& s, int i) {
    return i == 0 ? s.depth : (i == 1 ? s.lateral : s.vertical);
}

void Reset(State& s, Vec3 centre, Vec3 gravityLocal) {
    s.x = {};
    s.w = {};
    s.previousCentre = centre;
    s.velocity = {};
    s.restGravity = gravityLocal;
    s.started = true;
    s.primed = false;
    ++s.resets;
}

// One axis' exact under-damped spring step about the equilibrium `c` (Juckett; Holden's "Spring-It-On").
void SpringStep(double& x, double& w, double c, double omega, double zeta, double dt) {
    zeta = std::clamp(zeta, 0.0, 0.999);
    double const a = zeta * omega;
    double const wd = omega * std::sqrt(1.0 - zeta * zeta);
    double const e = std::exp(-a * dt);
    double const cs = std::cos(wd * dt);
    double const sn = std::sin(wd * dt);
    double const y = x - c;
    double const y1 = e * (y * cs + (w + a * y) / wd * sn);
    double const w1 = e * (w * cs - (a * w + omega * omega * y) / wd * sn);
    x = y1 + c;
    w = w1;
}

} // namespace

Settings Breast() {
    return {};
}

Settings Glute() {
    Settings s;
    s.depth = {5.0, 0.50, 0.004, 0.15};
    s.lateral = {3.5, 0.45, 0.008, 0.25};
    s.vertical = {4.0, 0.40, 0.012, 0.30};
    s.centre = {0.04, 0.0, 0.0};
    return s;
}

Output Step(State& state, Settings const& s, Input const& in) {
    Output out;
    out.rotation = Quat{};
    Quat const toLocal = Conjugate(Normalize(in.frame.rotation));
    Vec3 const centre = in.frame.translation + Rotate(in.frame.rotation, s.centre);
    Vec3 const gravityLocal = Rotate(toLocal, in.gravity);
    double const intensity = std::clamp(s.intensity, 0.0, kMaxIntensity);
    if (!state.started || in.reset || !Finite(centre) || in.dt > kMaxStepS) {
        Reset(state, centre, gravityLocal);
        return out;
    }
    if (!(in.dt > 0.0)) {
        return out;  // a hold: nothing moves (the offset shows as it was)
    }
    Vec3 const moved = centre - state.previousCentre;
    Vec3 const u = moved * (1.0 / in.dt);
    if (Length(moved) > kTeleportM || Length(u) > kTeleportMS) {
        Reset(state, centre, gravityLocal);
        return out;
    }
    state.previousCentre = centre;
    if (!state.primed) {
        state.velocity = u;  // the first step after a reset measures the velocity: no kick from it
        state.primed = true;
    } else {
        // The centre's velocity, smoothed (frame-rate independent decay), its change clamped: the inertial kick.
        Vec3 const before = state.velocity;
        double const keep = std::pow(2.0, -in.dt / std::max(1e-4, s.smoothHalfLifeS));
        state.velocity = state.velocity + (u - state.velocity) * (1.0 - keep);
        Vec3 change = state.velocity - before;
        double const limit = s.maxAccel * in.dt;
        if (Length(change) > limit) {
            change = change * (limit / Length(change));
        }
        Vec3 const local = Rotate(toLocal, change);
        for (int i = 0; i < 3; ++i) {
            At(state.w, i) -= AxisOf(s, i).inertia * intensity * At(local, i);
        }
    }
    for (int i = 0; i < 3; ++i) {
        Axis const& axis = AxisOf(s, i);
        double const omega = 2.0 * kPi * std::max(0.05, axis.hz);
        double const maxM = axis.maxM * intensity;
        double const sag = s.gravityShare * (At(gravityLocal, i) - At(state.restGravity, i)) / (omega * omega);
        double& x = At(state.x, i);
        double& w = At(state.w, i);
        SpringStep(x, w, sag, omega, axis.zeta, in.dt);
        if (!(maxM > 0.0)) {
            x = 0.0;
            w = 0.0;
            continue;
        }
        if (std::abs(x) > 1.5 * maxM) {
            x = std::copysign(1.5 * maxM, x);
            if (w * x > 0.0) {
                w = 0.0;  // no further outward
            }
            ++state.clampHits;
        }
        At(out.shown, i) = maxM * std::tanh(x / maxM);
    }
    if (!Finite(state.x) || !Finite(state.w)) {
        Reset(state, centre, gravityLocal);
        return Output{};
    }
    // Shown: depth as a translation; lateral and vertical part translation, part a turn about the pivot that carries the
    // centre by the same distance (lever = the centre's distance from the pivot).
    double const lever = std::max(0.01, Length(s.centre));
    double const rho = std::clamp(s.rotationShare, 0.0, 1.0);
    out.translation = {out.shown.x, (1.0 - rho) * out.shown.y, (1.0 - rho) * out.shown.z};
    // A centre ahead on +x: a turn about +z moves it toward +y, a turn about +y toward -z.
    Quat const yaw = AxisAngle({0.0, 0.0, 1.0}, rho * out.shown.y / lever);
    Quat const pitch = AxisAngle({0.0, 1.0, 0.0}, -rho * out.shown.z / lever);
    out.rotation = Normalize(yaw * pitch);
    return out;
}

} // namespace mevr::ik::jiggle
