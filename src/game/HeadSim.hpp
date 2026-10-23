// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <optional>

// A stand-in for the headset: scripted head motion, so the camera path can be developed and
// checked on the monitor without VR. Used only while VR is off.
namespace mevr::game::head_sim {

enum class Pattern {
    Off,
    Sweep,  // slow figure-eight: yaw +-25 degrees, pitch +-12 degrees
    Nod,    // pitch only, +-20 degrees: isolates vertical judder
};

// Off -> Sweep -> Nod -> Off. Returns the new pattern.
Pattern Cycle();
Pattern Current();
char const* Name(Pattern pattern);

// The pattern's head orientation (OpenXR axes, LOCAL space, no translation) `seconds` after it
// started. Pure, so it can be tested.
XrPosef PoseAt(Pattern pattern, double seconds);

// The current pattern's pose now, or nullopt when off.
std::optional<XrPosef> CurrentPose();

} // namespace mevr::game::head_sim
