// SPDX-License-Identifier: MIT
#pragma once

#include "ue3/Types.hpp"

#include <cstdint>

namespace mevr::hook {
class Context;
}

// What the player can use (A) is chosen by BioPlayerSelection's native UpdateSelection (LE1 0xC79D70, research/40):
// from the actors rendered in the last second, the one in range whose direction is closest to the player camera's
// cached view (Camera.CameraCache.POV: its Location and Rotation, inside half the POV's FOV and the distance-scaled
// selection angle), seen from the POV's location. In VR the head turns the rendered view only, so that cone points
// where the body and the stick face, not where the player looks.
//
// A hook on that function lets the game choose from where the player looks: for the call's duration the camera's
// cached Location and Rotation are the head's (in the world), then put back. The provider (game/Interact) decides per
// call whether to, and where the POV and the gaze are. The hook runs on the game thread (the controller's tick).
// LE1 only; no address for LE2/LE3.
namespace mevr::engine::selection_gaze {

// One call's swap: the POV's fields (in the camera) and the gaze to put there.
struct Swap {
    ue3::Vector* povLocation = nullptr;
    ue3::Rotator* povRotation = nullptr;
    ue3::Vector location;
    ue3::Rotator rotation;
};
// Asked at each call with the BioPlayerSelection it runs on; false: the game's own camera is used.
using Provider = bool (*)(void const* selection, Swap& out);

// The hook goes in once (lazily): false if it couldn't (another game, not a function start, LEBinkProxy refused).
bool Install(hook::Context& init);
// False: still live (or a call still inside it after waiting), so this module must stay loaded.
bool Uninstall(hook::Context& init, bool waitForCalls);
bool Installed();

// Game thread. Null: no swaps.
void SetProvider(Provider provider);

struct Stats {
    unsigned calls = 0;    // UpdateSelection calls since the last Take
    unsigned swapped = 0;  // ...of which ran from the gaze
};
Stats Take();

} // namespace mevr::engine::selection_gaze
