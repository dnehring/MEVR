// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The gun's sight on Halcyon's world layer (research/21; geometry in core/vr/Sights): the mod publishes the
// sight on the held gun each tick (HostServices::setSight), and here, on the Present thread, each eye is
// checked against it and the quads are drawn:
//   - red dot / holo: the reticle as a quad 50 m out along the sight line (collimated), only for an eye whose
//     line along the sight passes through the window (eyeVisibility), plus a faint ring at the window itself; on a
//     model whose sight housing is solid (kWindow, research/21 section 17), that eye also sees the world through the
//     window: the other eye's image at 1x on a disc there (the same copy and shader as the scope's picture);
//   - scope: a disc at the eyepiece, for the aiming eye only, showing a magnified crop of an eye image this
//     frame already rendered (the other eye's by default: the gun's own scope model blocks the aiming eye's
//     view of the target). The image is copied as Halcyon releases it (testloop::SetReleaseTap), so the
//     picture is the frame's own; no extra scene render.
// Snapshots carry the layers as "sight", "sightframe" and "scope".
namespace mevr::host::sight_layers {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;   // the head-tracked world is submitted (not a flat screen)
    bool menuOpen = false;     // Halcyon's menu is open
    bool onFoot = false;       // the game's mode is on foot
    bool eyesValid = false;
    XrPosef eyes[2]{};         // this frame's views (app space)
    bool projectionValid = false;
    XrSwapchain projectionSwapchain[2]{};
    XrRect2Di projectionRect[2]{};
    XrPosef projectionPose[2]{};  // the pose each eye image was rendered for (as submitted)
    XrFovf projectionFov[2]{};
    std::uint64_t pairArm = 0;    // the submitted pair's Draw (Halcyon's arm; 0 unknown): the scope capture's pose
};

inline constexpr unsigned kMaxLayers = 3;  // a dot's window, its ring and the dot; or a scope's picture

struct Output {
    XrCompositionLayerQuad const* layers[kMaxLayers] = {};
    unsigned count = 0;
    bool looking = false;      // an eye looks through the sight (or sees the scope's picture) this frame
    bool replaceRing = false;  // the sight asks the floating ring reticle to hide meanwhile
};

// Present thread (OverlayLayers), every frame: the sight's quads (at most kMaxLayers, back to front) and what they show.
Output Layers(Frame const& frame);
// For snapshots: the eyes against the sight, which eye aims, the scope's source image.
std::string StatusJson();

} // namespace mevr::host::sight_layers
