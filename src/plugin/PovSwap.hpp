// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

// A view put into the player camera's POV (CameraCache.POV: location, rotation, FOV) for one CalcSceneView and put back
// after it (research/51 section 8.1): the galaxy map's CIC hold, a conversation in the world's, the galaxy labels' projection.
// The writes are journalled (WorldView.hold* / WorldView.restore*); a fault reading or writing the addresses the mod sent is
// caught. Game thread. Implemented in plugin/GalaxyHologram.cpp.
namespace mevr::host::pov_swap {

struct Swap {
    bool active = false;
    float* location = nullptr;
    std::int32_t* rotation = nullptr;
    float* fov = nullptr;
    float savedLocation[3] = {};
    std::int32_t savedRotation[3] = {};
    float savedFov = 0.0f;
};

// Saves what the POV holds and writes the view; false (nothing written that stays) on a fault.
bool In(Swap& s, float const location[3], std::int32_t const rotation[3], float fov) noexcept;
// Puts the saved POV back.
void Out(Swap& s) noexcept;

} // namespace mevr::host::pov_swap
