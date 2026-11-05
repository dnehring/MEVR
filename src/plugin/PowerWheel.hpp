// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The power wheel's quads (research/55; game/PowerWheel in the mod decides what and where, core/vr/PowerWheel carries it,
// HostServices v28). Here, on the Present thread, the wheel is drawn with plugin/PowerWheelDraw in an ImGui context of its
// own (Halcyon's menu keeps the default one), given plugin/Glow's bloom, copied into its own swapchain and submitted as a
// quad where the mod placed it (at the left palm when it opened, facing the eyes). The first openings also show the hint
// card beside it. The lock's marker (HostServices v29) is a third quad at the locked hostile, facing the eyes, a constant
// angular size. Snapshots carry them as the "powerwheel", "powerhint" and "powerlock" layers.
namespace mevr::host::power_wheel {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;  // the head-tracked world is submitted (not a flat screen)
    bool menuOpen = false;    // Halcyon's menu is open
    XrVector3f head{};        // the head's position, tracking space (the lock's marker faces it)
    bool headValid = false;
};

// Present thread (OverlayLayers), every frame: up to three quads (the wheel, its hint, the lock's marker), as many as are up.
int Layers(Frame const& frame, XrCompositionLayerQuad const** out, int capacity);

}  // namespace mevr::host::power_wheel
