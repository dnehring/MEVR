// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>

// The weapon's aim ray, as the mod (game/hand_aim) computes it from the weapon the player holds,
// in tracking space (the head's and controllers' OpenXR space: Halcyon's recentered app space),
// meters. The renderer draws the reticle along it, so the reticle, the gun model and the shots
// agree. Published by the mod each tick (through HostServices::setAimRay when the renderer is the
// host's); read on the render thread. Engine-agnostic.
namespace mevr::vr::aim_ray {

struct Ray {
    bool valid = false;
    XrVector3f origin{};     // where shots start (the muzzle), tracking space, meters
    XrVector3f direction{};  // unit length, tracking space
    std::uint64_t sequence = 0;  // set by Publish
    std::uint64_t timeMs = 0;    // GetTickCount64 at Publish
};

// Any thread. `valid` false clears it (no weapon, not on foot, no anchor).
void Publish(Ray const& ray);
// The latest ray; `valid` forced false if older than `maxAgeMs`.
Ray Latest(std::uint64_t maxAgeMs = 250);
void Reset();

} // namespace mevr::vr::aim_ray
