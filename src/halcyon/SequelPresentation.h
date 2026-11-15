// SPDX-License-Identifier: GPL-3.0-only
#pragma once
// [MEVR] Shared renderer-to-host presentation data; no native game objects cross this seam.
struct ID3D11DeviceContext;
namespace ME2VR::MevrBridge {
struct OverlayFrame
{
    void* session = nullptr;    // XrSession
    void* appSpace = nullptr;   // XrSpace: Halcyon's recentered LOCAL app space (the projection layer's)
    void* viewSpace = nullptr;  // XrSpace: VIEW (head-locked quads)
    ID3D11DeviceContext* context = nullptr;   // the game's immediate context
    bool worldFrame = false;    // this frame submits the head-tracked world (the projection layer), not a flat panel
    bool menuOpen = false;      // Halcyon's Insert menu is open
    int gameMode = -1;          // the engine's game mode (EngineProbe::ReadGameModeSEH), -1 unreadable
    // Tracked eyes in app space. Projection poses can instead be centered or taken from frame history.
    bool eyesValid = false;
    float eyePosition[2][3] = {};
    // The world layer's views as submitted (each eye's swapchain, rect, pose and FOV), when projectionValid.
    bool projectionValid = false;
    void* projectionSwapchain[2] = {nullptr, nullptr};   // XrSwapchain
    int projectionRect[2][4] = {};                       // x, y, width, height (pixels)
    float projectionOrientation[2][4] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};
    float projectionPosition[2][3] = {};
    float projectionFov[2][4] = {};                      // angleLeft, angleRight, angleUp, angleDown (radians)
    bool stereo = false;        // SFR (the stereo mode whose eyes carry no HUD while the HUD panel takes it)
    // The head at the frame's display time in appSpace (the located views: their mean position, the left eye's
    // orientation, as LE1's), when headValid.
    bool headValid = false;
    float headOrientation[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // xyzw
    float headPosition[3] = {};
};
}
