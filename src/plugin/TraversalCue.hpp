// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Traversal.hpp"

#include <openxr/openxr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The traversal cue and blink (research/le2-traversal.md; core/vr/Traversal): the mod sends, each tick, where the game would vault or
// climb from where the player stands (HostServices v40 setTraversal) and how dark the blink is. Here, on the Present thread, two of
// our quads: a small label ("Vault", "Climb up") at the interaction point in the room, upright and turned to the head, a fixed
// angular size where it stands; and, while the blink mask runs, a head-locked black quad over both eyes (the last of our layers).
// Both pictures are made once on the CPU (the label's text by GDI) and copied into their swapchains (plugin/Swapchains); nothing is
// drawn per frame. Cost while nothing is wanted: one locked copy of the latest cue. Logs `traversal cue:`; snapshots show the layers
// "traversalcue" and "traversalblink".
namespace mevr::host::traversal_cue {

// Any thread: the mod's latest cue (HostServices v40). Null forgets it.
void Set(vr::traversal::Cue const* cue);
void Forget();

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;   // the projection layer's (LOCAL, recentered): the cue's place is in it
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;             // the head-tracked world is submitted
    bool menuOpen = false;
    bool headValid = false;
    XrVector3f head{};                   // the eyes' midpoint in appSpace (the projection views')
};

// Present thread, every frame: our layers (the label, then the blink), up to `capacity`. Returns how many.
unsigned Layers(Frame const& frame, void const** layersOut, unsigned capacity);

} // namespace mevr::host::traversal_cue
