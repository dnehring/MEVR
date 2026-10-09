#include "core/ik/Ik.hpp"

#include <algorithm>
#include <numbers>

namespace mevr::ik {
namespace {

constexpr double kPi = std::numbers::pi;

// Wraps to (-pi, pi].
double WrapAngle(double a) {
    while (a > kPi) a -= 2.0 * kPi;
    while (a <= -kPi) a += 2.0 * kPi;
    return a;
}

// Horizontal unit forward for a yaw (0 = -Z, positive = left, as in OpenXR).
Vec3 YawForward(double yaw) {
    return {-std::sin(yaw), 0.0, -std::cos(yaw)};
}

} // namespace

Vec3 Normalize(Vec3 a, Vec3 fallback) {
    double const length = Length(a);
    return length > 1e-9 ? a * (1.0 / length) : fallback;
}

double BodyYaw::Update(double headYaw, double deltaSeconds) {
    if (!initialized_) {
        Reset(headYaw);
        return yaw_;
    }
    double const offset = WrapAngle(headYaw - yaw_);
    if (offset > deadzone_) {
        yaw_ = WrapAngle(headYaw - deadzone_);  // dragged along at the deadzone's edge
    } else if (offset < -deadzone_) {
        yaw_ = WrapAngle(headYaw + deadzone_);
    } else {
        // Ease back under the head, at a fraction of the remaining offset per second.
        double const step = std::clamp(recenterRate_ * deltaSeconds, 0.0, 1.0);
        yaw_ = WrapAngle(yaw_ + offset * step);
    }
    return yaw_;
}

double BlendYaw(double a, double b, double weight) {
    return WrapAngle(a + WrapAngle(b - a) * weight);
}

double YawOf(double qx, double qy, double qz, double qw) {
    // Forward (-Z) rotated by q; yaw from its horizontal part.
    double fx = -2.0 * (qx * qz + qw * qy);
    double const fy = -2.0 * (qy * qz - qw * qx);
    double fz = -(1.0 - 2.0 * (qx * qx + qy * qy));
    if (fx * fx + fz * fz < 0.01) {
        // Looking (nearly) straight up or down: forward's horizontal part is noise. The head's up
        // vector then points where the face is turned (down) or away from it (up).
        double const sign = fy < 0.0 ? 1.0 : -1.0;
        fx = sign * 2.0 * (qx * qy - qw * qz);
        fz = sign * 2.0 * (qy * qz + qw * qx);
    }
    return std::atan2(-fx, -fz);
}

UpperBody EstimateUpperBody(Vec3 headPosition, double bodyYaw, BodyProportions const& body) {
    Vec3 const up{0.0, 1.0, 0.0};
    UpperBody out;
    out.forward = YawForward(bodyYaw);
    out.right = Cross(out.forward, up);  // -Z x +Y = +X: right when facing forward
    out.neck = headPosition - up * body.neckDown - out.forward * body.neckBack;
    Vec3 const shoulderLine = out.neck - up * body.shoulderDown;
    out.leftShoulder = shoulderLine - out.right * body.shoulderHalfWidth;
    out.rightShoulder = shoulderLine + out.right * body.shoulderHalfWidth;
    return out;
}

TwoBoneResult SolveTwoBone(Vec3 root, Vec3 target, double upper, double lower, Vec3 pole) {
    TwoBoneResult out;
    constexpr double kMinBone = 1e-4;
    if (!(upper >= kMinBone) || !(lower >= kMinBone)) {
        // Not an arm: a collapsed chain at the root, aimed at the target.
        out.mid = root;
        out.end = root + Normalize(target - root) * std::max(upper + lower, 0.0);
        return out;
    }
    Vec3 const toTarget = target - root;
    Vec3 const dir = Normalize(toTarget);
    double const maxReach = upper + lower;
    double const minReach = std::abs(upper - lower);
    double const distance = Length(toTarget);
    out.reached = distance <= maxReach && distance >= minReach;
    double const d = std::clamp(distance, minReach + 1e-6, maxReach - 1e-6);

    // The bend direction: the pole's offset from the root-target line.
    Vec3 const toPole = pole - root;
    Vec3 bend = toPole - dir * Dot(toPole, dir);
    if (Length(bend) < 1e-9) {
        // Pole on the line: pick any perpendicular (prefer bending down).
        Vec3 const down{0.0, -1.0, 0.0};
        bend = down - dir * Dot(down, dir);
        if (Length(bend) < 1e-9) {
            bend = Cross(dir, Vec3{1.0, 0.0, 0.0});
        }
    }
    bend = Normalize(bend);

    // Law of cosines for the angle at the root.
    double const cosA = std::clamp((upper * upper + d * d - lower * lower) / (2.0 * upper * d), -1.0, 1.0);
    double const sinA = std::sqrt(1.0 - cosA * cosA);
    out.mid = root + dir * (cosA * upper) + bend * (sinA * upper);
    out.end = root + dir * d;
    return out;
}

Vec3 ElbowPole(UpperBody const& body, int side) {
    Vec3 const shoulder = side < 0 ? body.leftShoulder : body.rightShoulder;
    Vec3 const down{0.0, -1.0, 0.0};
    return shoulder + down * 0.5 + body.right * (0.3 * side) - body.forward * 0.2;
}

} // namespace mevr::ik
