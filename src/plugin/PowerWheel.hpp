// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The power wheel's quads (research/55; game/PowerWheel in the mod decides what and where, core/vr/PowerWheel carries it,
// HostServices v28). Here, on the Present thread, the wheel is drawn with plugin/PowerWheelDraw in an ImGui context of its
// own (Halcyon's menu keeps the default one), given plugin/Glow's bloom, copied into its own swapchain and submitted as a
// quad where the mod placed it (at the left palm when it opened, facing the eyes). The first openings also show the hint
// card beside it. Snapshots carry them as the "powerwheel" and "powerhint" layers.
namespace mevr::host::power_wheel {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;  // the head-tracked world is submitted (not a flat screen)
    bool menuOpen = false;    // Halcyon's menu is open
};

// Present thread (OverlayLayers), every frame: up to two quads (the wheel, then the hint), as many as are up.
int Layers(Frame const& frame, XrCompositionLayerQuad const** out, int capacity);

}  // namespace mevr::host::power_wheel
