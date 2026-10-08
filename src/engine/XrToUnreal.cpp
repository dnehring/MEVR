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

namespace {

double ToRadians(int units) {
    return units * std::numbers::pi / 32768.0;
}

} // namespace

UnrealVector ToUnrealOffset(XrVector3f const& meters, int yawUnits) {
    Vec3 const local = ToUnrealAxes({meters.x, meters.y, meters.z});  // forward, right, up
    double const yaw = ToRadians(yawUnits);
    double const cy = std::cos(yaw);
    double const sy = std::sin(yaw);
    return {
        (local.x * cy - local.y * sy) * kUnitsPerMeter,
        (local.x * sy + local.y * cy) * kUnitsPerMeter,
        local.z * kUnitsPerMeter,
    };
}

XrPosef OffsetRight(XrPosef const& head, float rightMeters) {
    Vec3 const offset = Rotate(head.orientation, {rightMeters, 0.0, 0.0});
    XrPosef out = head;
    out.position.x += static_cast<float>(offset.x);
    out.position.y += static_cast<float>(offset.y);
    out.position.z += static_cast<float>(offset.z);
    return out;
}

ik::Quat ToUnrealOrientation(XrQuaternionf const& orientation, int yawUnits) {
    // ToUnrealAxes is v -> C v with det C = -1; C = -P for the proper rotation P: (x, y, z) ->
    // (z, -x, -y). C R C^-1 = P R P^-1, whose quaternion has the vector part turned by P.
    ik::Quat const local{orientation.z, -orientation.x, -orientation.y, orientation.w};
    double const half = ToRadians(yawUnits) * 0.5;
    ik::Quat const yaw{0.0, 0.0, std::sin(half), std::cos(half)};  // about +Z: X turns toward +Y (right)
    return ik::Normalize(yaw * local);
}

UnrealVector ToWorld(XrVector3f const& point, XrVector3f const& head, UnrealVector const& headWorld, int yawUnits) {
    UnrealVector const offset = ToUnrealOffset({point.x - head.x, point.y - head.y, point.z - head.z}, yawUnits);
    return {headWorld.x + offset.x, headWorld.y + offset.y, headWorld.z + offset.z};
}

UnrealVector RightAxis(UnrealRotation const& rotation) {
    return ToBasis(rotation).right;
}

Basis ToBasis(UnrealRotation const& rotation) {
    double const sp = std::sin(ToRadians(rotation.pitch)), cp = std::cos(ToRadians(rotation.pitch));
    double const sy = std::sin(ToRadians(rotation.yaw)), cy = std::cos(ToRadians(rotation.yaw));
    double const sr = std::sin(ToRadians(rotation.roll)), cr = std::cos(ToRadians(rotation.roll));
    return {
        {cp * cy, cp * sy, sp},
        {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp},
    };
}

Basis ToBasis(XrQuaternionf const& orientation) {
    auto const axis = [&](Vec3 v) {
        Vec3 const u = ToUnrealAxes(Rotate(orientation, v));
        return UnrealVector{u.x, u.y, u.z};
    };
    return {axis({0.0, 0.0, -1.0}), axis({1.0, 0.0, 0.0}), axis({0.0, 1.0, 0.0})};
}

UnrealVector Apply(Basis const& basis, UnrealVector const& local) {
    return {
        basis.forward.x * local.x + basis.right.x * local.y + basis.up.x * local.z,
        basis.forward.y * local.x + basis.right.y * local.y + basis.up.y * local.z,
        basis.forward.z * local.x + basis.right.z * local.y + basis.up.z * local.z,
    };
}

Basis Compose(Basis const& parent, Basis const& local) {
    return {Apply(parent, local.forward), Apply(parent, local.right), Apply(parent, local.up)};
}

Basis Inverse(Basis const& basis) {
    Basis const& b = basis;
    return {
        {b.forward.x, b.right.x, b.up.x},
        {b.forward.y, b.right.y, b.up.y},
        {b.forward.z, b.right.z, b.up.z},
    };
}

UnrealRotation ToRotation(UnrealVector const& direction) {
    return {ToUnits(std::atan2(direction.z, std::hypot(direction.x, direction.y))),
        ToUnits(std::atan2(direction.y, direction.x)), 0};
}

int Unwind(int angle, int reference) {
    long long delta = (static_cast<long long>(angle) - reference) % 65536;
    if (delta > 32768) {
        delta -= 65536;
    } else if (delta < -32768) {
        delta += 65536;
    }
    return static_cast<int>(reference + delta);
}

UnrealVector ToWorld(TrackingFrame const& frame, XrVector3f const& point) {
    XrVector3f const& head = frame.head.position;
    Vec3 const local = ToUnrealAxes({point.x - head.x, point.y - head.y, point.z - head.z});
    double const scale = frame.unitsPerMeter;
    UnrealVector const offset = Apply(frame.rotation, {local.x * scale, local.y * scale, local.z * scale});
    return {frame.headWorld.x + offset.x, frame.headWorld.y + offset.y, frame.headWorld.z + offset.z};
}

Basis OrientationToWorld(TrackingFrame const& frame, XrQuaternionf const& orientation) {
    return Compose(frame.rotation, ToBasis(orientation));
}

UnrealVector OriginWorld(TrackingFrame const& frame) {
    return ToWorld(frame, XrVector3f{0.0f, 0.0f, 0.0f});
}

TrackingFrame AtOrigin(TrackingFrame const& frame, UnrealVector const& originWorld, double unitsPerMeter) {
    XrPosef origin{};
    origin.orientation = {0.0f, 0.0f, 0.0f, 1.0f};
    return {origin, originWorld, frame.rotation, unitsPerMeter};
}

TrackingFrame FromBodyYaw(UnrealVector const& headWorld, int yawUnits, XrPosef const& head, double unitsPerMeter) {
    return {head, headWorld, ToBasis(UnrealRotation{0, yawUnits, 0}), unitsPerMeter};
}

TrackingFrame FromCameraView(UnrealVector const& cameraLocation, UnrealRotation const& cameraRotation,
    UnrealRotation const& look, UnrealVector const& offset, XrPosef const& head, double unitsPerMeter) {
    Basis const camera = ToBasis(cameraRotation);
    Basis const view = Compose(camera, ToBasis(look));
    UnrealVector const moved = Apply(camera, offset);
    return {head,
        {cameraLocation.x + moved.x, cameraLocation.y + moved.y, cameraLocation.z + moved.z},
        Compose(view, Inverse(ToBasis(head.orientation))), unitsPerMeter};
}

} // namespace mevr::engine
