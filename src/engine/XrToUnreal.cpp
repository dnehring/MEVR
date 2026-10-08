#include "engine/XrToUnreal.hpp"

#include <cmath>
#include <numbers>

namespace mevr::engine {
namespace {

struct Vec3 {
    double x, y, z;
};

double Dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// v' = q v q^-1 for a unit quaternion.
Vec3 Rotate(XrQuaternionf const& q, Vec3 v) {
    Vec3 const u{q.x, q.y, q.z};
    double const s = q.w;
    Vec3 const uxv{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    double const uv = Dot(u, v);
    double const uu = Dot(u, u);
    return {
        2.0 * uv * u.x + (s * s - uu) * v.x + 2.0 * s * uxv.x,
        2.0 * uv * u.y + (s * s - uu) * v.y + 2.0 * s * uxv.y,
        2.0 * uv * u.z + (s * s - uu) * v.z + 2.0 * s * uxv.z,
    };
}

// OpenXR (x right, y up, -z forward) to UE3 (x forward, y right, z up). Determinant -1: this is
// also the right- to left-handed flip.
Vec3 ToUnrealAxes(Vec3 v) {
    return {-v.z, v.x, v.y};
}

int ToUnits(double radians) {
    return static_cast<int>(std::lround(radians * 32768.0 / std::numbers::pi));
}

} // namespace

UnrealRotation ToUnrealRotation(XrQuaternionf const& orientation) {
    Vec3 const forward = ToUnrealAxes(Rotate(orientation, {0.0, 0.0, -1.0}));
    Vec3 const up = ToUnrealAxes(Rotate(orientation, {0.0, 1.0, 0.0}));

    double const yaw = std::atan2(forward.y, forward.x);
    double const pitch = std::atan2(forward.z, std::hypot(forward.x, forward.y));

    // Roll: the head's up vector measured against the up/right axes of the same yaw and pitch
    // with zero roll (UE3's FRotationMatrix with Roll = 0).
    Vec3 const right0{-std::sin(yaw), std::cos(yaw), 0.0};
    Vec3 const up0{-std::sin(pitch) * std::cos(yaw), -std::sin(pitch) * std::sin(yaw), std::cos(pitch)};
    double const roll = std::atan2(Dot(up, right0), Dot(up, up0));

    return {ToUnits(pitch), ToUnits(yaw), ToUnits(roll)};
}

} // namespace mevr::engine
