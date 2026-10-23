// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The squad weapon menu's quad (docs/squad-weapons-design.md, game/squad_weapons in the mod): the mod
// publishes what to show (core/vr/SquadMenu, HostServices::setSquadMenu); here, on the Present thread,
// it is drawn with Dear ImGui (a context of its own, so Halcyon's menu is untouched) into a texture,
// copied into its own swapchain and submitted as a quad in the app space, placed above the left hand
// when the menu opens and left there while it stays open. Snapshots carry it as the "squad" layer.
namespace mevr::host::squad_panel {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    bool worldFrame = false;  // the head-tracked world is submitted (not a flat screen)
    bool menuOpen = false;    // Halcyon's menu is open
    bool headValid = false;
    XrPosef head{};           // the eyes' centre, app space
};

// Present thread (OverlayLayers), every frame: the menu's quad layer, or nullptr when it's closed.
XrCompositionLayerQuad const* Layer(Frame const& frame);
// Recentering re-origined the app space: an open menu is placed again.
void Recentered();

} // namespace mevr::host::squad_panel
