// SPDX-License-Identifier: MIT
#pragma once

// The hand reticle (research/15): a small ring on the weapon's aim ray (the mod's, HostServices setAimRay) or, until the
// mod publishes one, on the right controller's aim pose, in place of the game's screen-centre crosshair. Game-independent:
// LE1's HalcyonOverlays and LE2's bridge each call Layer once a frame. Moved unchanged out of plugin/HalcyonOverlays.cpp.
// Present thread only.

#include "plugin/QuadSwapchain.hpp"

namespace mevr::host::hand_reticle {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;      // a head-tracked world frame
    bool menuOpen = false;        // the renderer's own menu is open
    bool headValid = false;
    XrVector3f headPosition{};    // in appSpace
    int gameMode = -1;            // the last readable game mode: 0 on foot, 1 the Mako (only on a ray the mod publishes)
    bool handAim = false;         // the mod aims weapons with the right controller
    bool sightHidesRing = false;  // an eye looks through the gun's sight (plugin/SightLayers): the ring hides
    // renderer::Hud()'s handReticle, handReticleDistanceM, handReticleSizeDeg.
    bool enabled = true;
    float distanceM = 10.0f;
    float sizeDeg = 1.5f;
};

// The ring's quad for this frame, or nullptr. Logs where it comes from (the mod's ray, the controller, hidden).
XrCompositionLayerQuad const* Layer(Frame const& frame) noexcept;

} // namespace mevr::host::hand_reticle
