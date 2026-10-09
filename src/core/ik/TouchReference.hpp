#pragma once

#include "core/ik/Rigid.hpp"

// The Oculus Touch controller's canonical ("legacy") frame, relative to its OpenXR grip pose: a
// fixed, rigid offset that is the same for every Touch model (CV1, Rift S, Quest 1-3, Touch Plus,
// Touch Pro). It's the controller's own body frame, independent of how a runtime defines its grip,
// so a gun held "at a fixed pose relative to the controller" can be expressed in it. We hold guns
// along the aim pose when there is one (game/arm_ik), and fall back to this frame's forward
// without one. Engine-agnostic; tested offline.
//
// Adapted from Overlord by Aeka0 (https://github.com/Aeka0/Overlord, GPL-3.0):
// src/client/component/vr/touch_controller_reference.hpp (touch_legacy_reference) and
// docs/vr-runtime-rendering.md. Its legacy_from_grip is the translation (+/-0.007, -0.00182941,
// 0.1019482) m (left +, right -) and a right-handed rotation of 20.6 degrees about X; the
// reference frame in grip space is its inverse. Changes: our own transform types, and the same
// rotation expressed in UE-style axes. See NOTICE.md.
namespace mevr::ik {

inline constexpr double kTouchReferencePitchDegrees = 20.6;

// The Touch reference frame in the grip's own OpenXR axes (meters; +X right, +Y up, -Z forward):
// Apply(result, p) takes a point given in the reference frame into grip space. `side` 0 left,
// 1 right. Its forward (-Z) is the grip's forward pitched 20.6 degrees down; its origin sits about
// 10 cm ahead of the grip and 3 cm below it (the tracking ring), 7 mm to the side.
Transform TouchReferenceInGrip(int side);

// The same rotation in UE-style grip axes (X = grip -Z, Y = grip +X, Z = grip +Y, as
// game/hand_targets gives grip rotations): a 20.6 degree pitch down, GripTrim(20.6, 0, 0).
// grip * TouchReferenceRotationUe() is the reference frame's rotation in the same space as grip.
Quat TouchReferenceRotationUe();

} // namespace mevr::ik
