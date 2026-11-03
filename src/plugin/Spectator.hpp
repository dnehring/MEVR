// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <string>
#include <vector>

struct ID3D11Texture2D;
struct IDXGISwapChain;

// The spectator camera's host side (research/49; the mod's side: game/Spectator; what's shared: core/vr/Spectator.hpp).
// Present (render) thread.
//
// While the mod publishes a capturing Frame (HostServices v24):
//   - each quad layer's swapchain image is copied into a texture of ours as it is released (OnImageReleased; the
//     runtime owns the image once released), and the reticle's static pixels are uploaded once;
//   - at xrEndFrame (the test loop's wrapped xrEndFrame), the render target's D3D11 texture is found behind its
//     FTextureResource (plugin/ScopeCaptureSource's guarded search), drawn into a 16:9 FP16 picture of ours, and every
//     located quad layer of the frame is drawn over it from the camera's pose and symmetric field, as the compositor
//     would show them to an eye there (perspective-correct, blended in linear light, premultiplied or not as the layer
//     says), the right eye's single-eye layers and both eyes' ones, never the comfort vignette;
//   - before the window's Present ([MEVR] seam MevrBridge::SpectatorMirror in Halcyon's PresentHook), the picture is
//     drawn into the backbuffer letterboxed, in place of Halcyon's one-eye mirror; the pass-0 snapshot's dirty gate is
//     put back around it.
// The snapshot (plugin/TestLoop) stages the picture as `<stamp>-spectator.png`. Nothing runs while the mod isn't capturing.
namespace mevr::host::spectator {

// One quad layer of the frame, located in the eyes' space (Halcyon's app space; plugin/TestLoop locates them).
struct Layer {
    XrSwapchain swapchain = XR_NULL_HANDLE;
    XrRect2Di rect{};
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrExtent2Df size{};
    XrCompositionLayerFlags flags = 0;
    XrEyeVisibility visibility = XR_EYE_VISIBILITY_BOTH;
    std::string name;
};

// A new session: every swapchain copy goes (their handles die with the session).
void OnSessionCreated();
// Each frame, from the overlays (OverlayLayers): the arm of the submitted pair, and the float panel's SDR white scale.
void NoteFrame(std::uint64_t pairArm, float floatWhiteScale);
// The capture publish a Draw renders (MevrBridge::NoteDrawArm, game thread).
void NoteDrawArm(std::uint64_t arm);
// True while the mod is capturing (cheap: an atomic, refreshed each frame by NoteFrame). Any thread.
bool Wanted();
// A swapchain image right before its release (Halcyon's through the wrapped release, ours through OnImageWritten).
void OnImageReleased(XrSwapchain swapchain, ID3D11Texture2D* image);
// At xrEndFrame, before the runtime's: the frame's projection swapchains (never copied) and its located quad layers.
// Returns the picture if one was made this frame (for the snapshot), else null.
ID3D11Texture2D* OnEndFrame(std::vector<XrSwapchain> const& projectionSwapchains, std::vector<Layer> const& layers);
// Before the window's Present: the picture into the backbuffer (if the mod wants it there and one was made).
void MirrorPresent(IDXGISwapChain* swapChain);
// Snapshot JSON (the host's "spectator").
std::string StatusJson();

} // namespace mevr::host::spectator
