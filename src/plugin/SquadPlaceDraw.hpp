// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <d3d11.h>

#include <string>

// Squad placement's arc and landing ring (game/SquadPlace, core/vr/SquadPlace's View, HostServices v32): while the mod's view is
// active and fresh, a projection layer of its own (both eyes, galaxy_draw's renderer: additive light in a premultiplied target):
// the arc from the right hand as a ribbon, brighter toward the landing; on floor a ring lying on the surface (blue for the squad,
// amber for one squadmate, green for "on me") that grows in over the first 0.15 s; on a wall or nothing a red end mark. Nothing at
// all, not even a swapchain, until the first arc. Snapshots carry it as the "squadplace-left"/"-right" layers.
namespace mevr::host::squad_place_draw {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool headValid = false;
    XrPosef head{};
    XrPosef eyes[2]{};
    XrFovf fov[2]{};
    int eyeWidth = 0;  // the world layer's eye image, pixels (0: unknown)
    int eyeHeight = 0;
};

// Present thread, each frame from the overlays: the arc's layer, or null.
XrCompositionLayerBaseHeader const* Layer(Frame const& frame);
// Snapshot JSON ("squadPlace").
std::string StatusJson();

} // namespace mevr::host::squad_place_draw
