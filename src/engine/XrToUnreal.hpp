#pragma once

#include "core/ik/Rigid.hpp"

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

// An OpenXR LOCAL-space orientation as a UE3 world rotation (a quaternion over UE3's axes) for a
// body facing `yawUnits`: ToUnrealOffset's mapping, applied to rotations. The axis flip mirrors
// handedness; the result is still a proper rotation.
ik::Quat ToUnrealOrientation(XrQuaternionf const& orientation, int yawUnits);

// The right-hand axis (unit length) of a view with this rotation (UE3 FRotationMatrix's Y axis).
UnrealVector RightAxis(UnrealRotation const& rotation);

// A point `rightMeters` to the right of `head` (negative = left), along the head's own right
// axis: where an eye sits. OpenXR axes, LOCAL space.
XrPosef OffsetRight(XrPosef const& head, float rightMeters);

// An orthonormal frame: where its forward, right and up axes point, in its parent's axes (for the
// world: UE axes). Also a rotation, mapping a vector given along the frame's own axes (x forward,
// y right, z up) into the parent.
struct Basis {
    UnrealVector forward{1.0, 0.0, 0.0};
    UnrealVector right{0.0, 1.0, 0.0};
    UnrealVector up{0.0, 0.0, 1.0};
};

// A UE3 rotator's axes (FRotationMatrix): yaw, then pitch about the turned right axis, then roll.
Basis ToBasis(UnrealRotation const& rotation);
// An OpenXR orientation's axes (its -Z, +X and +Y) in UE axes.
Basis ToBasis(XrQuaternionf const& orientation);
// A vector given along `basis`'s own axes, in its parent.
UnrealVector Apply(Basis const& basis, UnrealVector const& local);
// `local` (a frame given relative to `parent`) in parent's parent.
Basis Compose(Basis const& parent, Basis const& local);
// The rotation that undoes `basis` (its transpose).
Basis Inverse(Basis const& basis);
// The rotator (roll 0) that looks along `direction`, which needn't be unit length.
UnrealRotation ToRotation(UnrealVector const& direction);
// `angle` moved by whole turns to within half a turn of `reference` (Unreal units), so a rotator
// written back next to the game's own keeps its winding.
int Unwind(int angle, int reference);

// Where tracking space (OpenXR LOCAL / app space: meters, +X right, +Y up, -Z forward; the space
// of the head and controller poses) sits in the world for the view the player is seeing. Maps
// controller poses into the world (hand aim, arm IK).
struct TrackingFrame {
    XrPosef head{};            // the head pose the view was built for (tracking space; AtOrigin: the origin)
    UnrealVector headWorld{};  // where that head is in the world (the view's eye midpoint)
    Basis rotation{};          // the world directions of tracking space's -Z, +X and +Y
    double unitsPerMeter = kUnitsPerMeter;
};

// A tracking-space point in the world.
UnrealVector ToWorld(TrackingFrame const& frame, XrVector3f const& point);
// A tracking-space orientation's axes in the world (an aim pose points along `forward`).
Basis OrientationToWorld(TrackingFrame const& frame, XrQuaternionf const& orientation);

// Where `frame` puts tracking space's origin in the world.
UnrealVector OriginWorld(TrackingFrame const& frame);
// `frame`, measured from tracking space's origin instead of from the head: the origin sits at
// `originWorld`, the axes turn as in `frame`, at `unitsPerMeter`. The origin is where the head
// was at the start or the last recenter (OpenXR LOCAL; Halcyon's recenter re-origins its app
// space at the head), so it stands for the body: points mapped through this frame stay put when
// only the head moves (leaning, 6DOF), whatever the renderer does with the view.
TrackingFrame AtOrigin(TrackingFrame const& frame, UnrealVector const& originWorld, double unitsPerMeter);

// Our renderer: tracking space is level and its forward is the body's yaw (the head yaw is added
// on top in the view), and the view sits at `headWorld`.
TrackingFrame FromBodyYaw(UnrealVector const& headWorld, int yawUnits, XrPosef const& head,
    double unitsPerMeter = kUnitsPerMeter);

// A renderer that builds the view on the game camera (Halcyon's): it turns the camera's view by
// `look` (in the camera's own frame, yaw then pitch, as head look does) and moves it by `offset`
// (along the camera's own axes: x forward, y right, z up; Unreal units). The headset shows that
// view at the head's orientation, so tracking space turns into the world by
// (camera * look) * inverse(head): with look equal to the head's yaw and pitch, that's the camera
// itself; with no look (head-locked view), the camera turned back by the head.
TrackingFrame FromCameraView(UnrealVector const& cameraLocation, UnrealRotation const& cameraRotation,
    UnrealRotation const& look, UnrealVector const& offset, XrPosef const& head, double unitsPerMeter);

} // namespace mevr::engine
