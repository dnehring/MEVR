// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Overlord by Aeka0 (https://github.com/Aeka0/Overlord), GPL-3.0:
// src/client/component/vr/touch_controller_reference.hpp (touch_legacy_reference). Changes: our
// own transform and quaternion types; the UE-axes form of the rotation.

#include "core/ik/TouchReference.hpp"

#include "core/ik/HandFrame.hpp"

#include <cmath>
#include <numbers>

namespace mevr::ik {

Transform TouchReferenceInGrip(int side) {
    // Overlord's legacy_from_grip: a grip-space point p maps to R p + t in the legacy frame, with
    // R the right-handed rotation about X by 20.6 degrees (rows {1,0,0}, {0,c,-s}, {0,s,c}).
    double const half = kTouchReferencePitchDegrees * std::numbers::pi / 360.0;
    Transform legacyFromGrip;
    legacyFromGrip.rotation = Quat{std::sin(half), 0.0, 0.0, std::cos(half)};
    legacyFromGrip.translation = {side == 0 ? 0.007 : -0.007, -0.00182941, 0.1019482};
    // grip_from_calibration = inverse(legacy_from_grip): the legacy frame in grip space.
    return Inverse(legacyFromGrip);
}

Quat TouchReferenceRotationUe() {
    return GripTrim(kTouchReferencePitchDegrees, 0.0, 0.0);
}

} // namespace mevr::ik
