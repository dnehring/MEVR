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

} // namespace mevr::engine
