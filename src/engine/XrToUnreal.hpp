#pragma once

#include <openxr/openxr.h>

namespace mevr::engine {

// UE3 rotator angles in Unreal units (65536 = 360 degrees).
struct UnrealRotation {
    int pitch = 0;  // + looks up
    int yaw = 0;    // + turns right
    int roll = 0;   // + tilts the right ear down
};

// Converts an OpenXR orientation (right-handed: +X right, +Y up, -Z forward) to the UE3 rotator
// that looks the same way (left-handed: +X forward, +Y right, +Z up).
UnrealRotation ToUnrealRotation(XrQuaternionf const& orientation);

// A UE3 world-space vector (+X forward, +Y right, +Z up), in Unreal units.
struct UnrealVector {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Unreal units per meter. UE3 content is built at about 1 unit = 1 cm; ME's human scale and
// prior art (a ~3.2 unit half-IPD, research/13) agree. [verify in headset: world size]
inline constexpr double kUnitsPerMeter = 100.0;

// A head-tracking offset in LOCAL space (meters, OpenXR axes) as a world offset for a body
// facing `yawUnits`: LOCAL forward is the body's forward.
UnrealVector ToUnrealOffset(XrVector3f const& meters, int yawUnits);

// A LOCAL-space point (e.g. a controller) in the world, given where the head is in both: the
// head's LOCAL position and its world location, and the body yaw LOCAL forward maps to.
UnrealVector ToWorld(XrVector3f const& point, XrVector3f const& head, UnrealVector const& headWorld, int yawUnits);

// The right-hand axis (unit length) of a view with this rotation (UE3 FRotationMatrix's Y axis).
UnrealVector RightAxis(UnrealRotation const& rotation);

// A point `rightMeters` to the right of `head` (negative = left), along the head's own right
// axis: where an eye sits. OpenXR axes, LOCAL space.
XrPosef OffsetRight(XrPosef const& head, float rightMeters);

} // namespace mevr::engine
