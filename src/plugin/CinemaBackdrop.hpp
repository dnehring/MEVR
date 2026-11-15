// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The cinema screen's floor and horizon in the headset (research/52 "A floor and a horizon"; the owner, 2026-11-06: "we may need a
// background of some type to add a better sense of location so I don't get tunnel visioned and partially motion sick"; the
// floor and horizon, ideally from the game's own art): while the 3D cinema screen is up, a projection layer of its own under the
// screen's quads (MevrBridge::CinemaBackdropLayer, the seam in xr_session.cpp's screen branch), drawn by
// plugin/CinemaBackdropDraw: the floor at the player's real floor (the host's STAGE floor, else 1.6 m under the eyes where the
// sequence began), fixed in the room, the horizon all around, fading in over 0.6 s. Both eyes side by side in ONE swapchain
// (the runtime's swapchain budget, plugin/Swapchains). [Cinematics] backdrop (1; 0 off), backdropBrightness (1). Log `cinema
// backdrop:`; snapshots its "backdrop" layer and "cinemaBackdrop".
struct ID3D11DeviceContext;

namespace mevr::host::cinema_backdrop {

// Snapshot JSON ("cinemaBackdrop").
std::string StatusJson();

// This frame's eyes (app space) and the screen as placed (both Halcyon hosts: LE1's MevrBridge::CinemaBackdropLayer forwards
// here, LE2's bridge calls it from its screen's submit).
struct Frame {
    void* session = nullptr;                 // XrSession
    void* appSpace = nullptr;                // XrSpace
    ID3D11DeviceContext* context = nullptr;
    bool headValid = false;
    float eyeOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float eyePosition[2][3] = {};
    float eyeFov[2][4] = {};                 // angleLeft, angleRight, angleUp, angleDown (radians)
    float screenOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // the screen's quad pose, app space
    float screenPosition[3] = {};
    float screenWidthM = 0.0f;
    int eyeWidth = 0;                        // the eye images' size, for the layer's resolution
    int eyeHeight = 0;
};
// The layer to submit UNDER the screen's quads (an XrCompositionLayerProjection*), or null. Present thread.
const void* Layer(Frame const& frame) noexcept;

} // namespace mevr::host::cinema_backdrop
