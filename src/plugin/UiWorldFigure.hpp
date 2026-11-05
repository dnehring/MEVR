// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <optional>
#include <string>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

// The squad member behind the equipment, XMods and squad screens: the host's side (research/57 section 7; the mod's side:
// game/UiWorldFigure). The mod's scene capture looks through the game's UI world's camera, but it lives in the level, whose
// scene the capture loop hands it. A capture's probe builds its view family from the eye renderer's first field, the FScene
// (0x4EE4B0, research/57 section 7): for our capture alone, recognised by its render target (the probe's +0x1C), that field holds
// the UI world's scene for the call, so the capture renders the UI world. It must render without Umbra (Umbra knows only the
// level: its visible instances indexed the UI world's views out of range, the render thread's fault at 0x76A5FB, 2026-11-05), so
// it gets no Umbra camera query (Install below); a render while one is still pending for it renders the level, consuming it. Its
// picture is then drawn behind the game's UI on the world UI's panel (plugin/HalcyonOverlays) over the 16:9 stage.
namespace mevr::hook {
class Context;
}

namespace mevr::host::ui_world_figure {

// At attach (LE1): the per-capture Umbra camera thunk (0x7526C0) that BeginRenderingViewFamily (0x7655A0, game thread) calls for
// each of the level's captures while the eye's family uses Umbra. The 2D probe's slot 8 (0x4FD550) then sets the capture's Umbra
// camera, kicks its query and on the render thread counts it (+0x6C), and that capture must then render through Umbra to consume
// it. Ours is skipped (it renders the UI world, which Umbra doesn't know): its count stays 0, so the probe renders it without
// Umbra (0x4EE4B0's second branch) and nothing is left in Umbra's pipeline (the deadlock of 2026-11-05: the eye's resolve waited
// for ever on the unconsumed capture query).
void Install(hook::Context& init);

// The capture loop's hook (plugin/RenderLevers, render thread), around each capture's CaptureScene.
struct CaptureSwap {
    void* original = nullptr;  // the eye renderer's scene, to put back (nullptr: nothing swapped)
    bool skip = false;        // our capture, but the UI world's scene wasn't found: it doesn't render (it would show the level)
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
