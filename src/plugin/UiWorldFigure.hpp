// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

// The squad member behind the equipment, XMods and squad screens: the host's side (research/56 section 7; the mod's side:
// game/UiWorldFigure). The mod's scene capture looks through the game's UI world's camera, but it lives in the level, whose
// scene the capture loop hands it. A capture's probe builds its view family from the eye renderer's first field, the FScene
// (0x4EE4B0, research/56 section 7): for our capture alone, recognised by its render target (the probe's +0x1C), that field holds
// the UI world's scene for the call, so the capture renders the UI world. Its picture is then drawn behind the game's UI on the
// world UI's panel (plugin/HalcyonOverlays) over the 16:9 stage.
namespace mevr::host::ui_world_figure {

// The capture loop's hook (plugin/RenderLevers, render thread), around each capture's CaptureScene.
struct CaptureSwap {
    void* original = nullptr;  // the eye renderer's scene, to put back (nullptr: nothing swapped)
    bool skip = false;         // our capture, but the UI world's scene wasn't found: it doesn't render (it would show the level)
};
CaptureSwap BeginCapture(void* captureInfo, void* mainRenderer);
void EndCapture(void* mainRenderer, CaptureSwap const& swap);

// The figure's picture for this frame's world UI panel (Present thread), or nullopt: no figure wanted, the capture hasn't rendered
// the UI world lately, or its texture isn't found.
struct Picture {
    ID3D11ShaderResourceView* srv = nullptr;  // owned here; valid until the next ForFrame
    float decodeGamma = 1.0f;                 // the shader raises a sample to this (8-bit display-ready without an sRGB view)
    float opacity = 1.0f;
};
std::optional<Picture> ForFrame(ID3D11Device* device, float backdrop);

// A new session or device: drop the texture and the view.
void Forget();
// Snapshot JSON (the host's "figure").
std::string StatusJson();

} // namespace mevr::host::ui_world_figure
