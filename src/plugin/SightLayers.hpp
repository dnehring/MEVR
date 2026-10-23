#pragma once

#include <openxr/openxr.h>

#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The gun's sight on Halcyon's world layer (research/21; geometry in core/vr/Sights): the mod publishes the
// sight on the held gun each tick (HostServices::setSight), and here, on the Present thread, each eye is
// checked against it and the quads are drawn:
//   - red dot / holo: the reticle as a quad 50 m out along the sight line (collimated), only for an eye whose
//     line along the sight passes through the window (eyeVisibility), plus a faint ring at the window itself;
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
    // How Stereo 2 drew the eye images (research/21 section 9; vr::sights::EyeImage): rendered stereoHalfEyeM either side
    // of the eye midpoint, each image's content moved imageShiftTan right at submit. Not valid: the images are taken
    // as true to the eyes.
    bool stereoValid = false;
    float stereoHalfEyeM = 0.0f;
    float imageShiftTan[2]{};
};

struct Output {
    XrCompositionLayerQuad const* layers[2] = {};
    unsigned count = 0;
    bool looking = false;      // an eye looks through the sight (or sees the scope's picture) this frame
    bool replaceRing = false;  // the sight asks the floating ring reticle to hide meanwhile
};

// Present thread (OverlayLayers), every frame: the sight's quads (at most two) and what they show.
Output Layers(Frame const& frame);
// For snapshots: the eyes against the sight, which eye aims, the scope's source image.
std::string StatusJson();

} // namespace mevr::host::sight_layers
