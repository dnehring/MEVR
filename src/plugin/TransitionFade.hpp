// SPDX-License-Identifier: MIT
#pragma once

struct ID3D11DeviceContext;

// The dip from black when the view changes between the world and a cinematic (core/vr/TransitionFade, research/52 "A dip from
// black at the switch"), both Halcyon hosts: LE1's MevrBridge::TransitionFadeLayer (xr_session.cpp) forwards here, LE2's
// bridge calls it from me2_xr.cpp's last-layer seam (research/le2-cinema.md). Present thread.
namespace mevr::host::transition_fade {

// This frame's view: 0 the world, 1 the 3D cinema screen, 2 a cinematic on the flat panel, 3 another flat panel, -1 none
// submitted. `cut`: a cut inside one view (LE1's conversation in the world re-placed the player): black now, the same ease.
// Returns a head-locked black quad (an XrCompositionLayerQuad*) to submit LAST, or null.
const void* Layer(void* session, void* viewSpace, ID3D11DeviceContext* context, int view, bool cut = false) noexcept;

} // namespace mevr::host::transition_fade
