// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace mevr::hook {
class Context;
}

// The player's own body is never culled for standing nearer than the culling camera (research/69 "The culling camera is
// ahead of the eye", 2026-11-10). The engine culls with the game's camera (Umbra's camera is set from the scene view's CPU
// matrices), and in first person the eye is drawn behind it: Halcyon's render-side view offset (the "view back" framing,
// 0.43 m with the shipped settings, plus the lean) lives only in the GPU matrices. A frustum 0.43 m ahead of the eye misses
// what is right under the eye: looking down at 50 degrees the body's box lay 4 cm behind the culling frustum's lower plane at
// the Council's podium and 3 cm inside it a moment later, so the body came and went with the pose. USkeletalMeshComponent's
// UpdateBounds (LE1 0x58EEC0, vtable slot +0x368: the box of the physics bodies at the bones, written to the component's
// Bounds before the transform update sends it to the scene) is post-hooked: for the meshes named here the box and the sphere
// grow by the pad on every side, so a box the eye's frustum reaches is one the culling frustum reaches too.
//
// The hook runs wherever the engine updates a skeletal mesh's bounds (the game thread). LE1 only; no address for LE2/LE3.
namespace mevr::engine::body_bounds {

bool Install(hook::Context& init);
// False: still live (or a call still inside it after waiting), so this module must stay loaded.
bool Uninstall(hook::Context& init, bool waitForCalls);
bool Installed();

inline constexpr int kMaxMeshes = 8;
// The meshes whose bounds grow by `padUnits` on every side as the engine computes them (count 0 or a pad of 0: none).
// Game thread.
void Pad(void const* const* meshes, int count, float padUnits);

struct Stats {
    unsigned padded = 0;  // bounds padded since the last Take
    float pad = 0.0f;     // the pad now (units)
};
Stats Take();

} // namespace mevr::engine::body_bounds
