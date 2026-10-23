#pragma once

#include <openxr/openxr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The comfort vignette (research/31): while the stick turns (or moves) the player, the view's edges darken with a
// soft radial falloff, fading in and out over 0.15 s. The mod says how dark, each tick (HostServices v17
// setVignette: [Comfort] vignette, vignetteStrength, vignetteWhen, game/comfort); here, on the Present thread
// (HalcyonOverlays), it is a head-locked quad 1 m ahead in the VIEW space, wide enough to cover both eyes' fields of
// view: premultiplied black, its alpha only at the edges (core/vr/Comfort VignetteMask). Never touches
// xr_session's frame loop: it is one more of our quads, below the HUD panel and the reticle.
//
// Cost: nothing at all while it isn't wanted (one atomic load; no layer, no swapchain work). While fading, a
// 128 x 128 texture is refilled from the CPU when its level changes (64 levels) and copied into the swapchain;
// steady, only the layer is submitted. Logged every 10 s while used (`vignette:`). Snapshots carry it as the
// "vignette" layer.
namespace mevr::host::vignette {

// Any thread: how dark the edges should be now (0..1), from the mod. Stale after 250 ms (fades out).
void SetTarget(float amount);
// The mod is gone: fade out.
void Forget();

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;   // the head-tracked world is submitted (not a flat screen)
    bool menuOpen = false;     // Halcyon's menu
    bool fovValid = false;
    float fov[2][4] = {};      // each eye's projection FOV: left, right, up, down (radians)
};

// Present thread, every frame: the vignette's quad, or nullptr when there's nothing to show.
XrCompositionLayerQuad const* Layer(Frame const& frame);

} // namespace mevr::host::vignette
