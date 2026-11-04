// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <d3d11.h>

#include <string>

// The galaxy map as a hologram we draw (research/51 section 8): the mod sends the map's level (HostServices v26,
// core/vr/GalaxyHologram) and the host draws it as a projection layer of its own (both eyes, plugin/GalaxyHologramDraw). In the
// CIC ([Galaxy] inWorld) it stands over the projector where the mod anchors it, on foot, through the map's opening and closing
// and with the map open (the mod holds the view at Shepard's and hides the game's own map); otherwise, with the map open, on a
// table in front of the player over the game's flat screen, which WorldPanels moves back and up. A controller's aim ray hovers
// the objects; its trigger picks one, which the mod turns into the game's own selection (on foot it opens the map first); the
// game's travel question shows as Yes / No over the table. The game pointer leaves the trigger alone while the ray is on it.
namespace mevr::host::galaxy_hologram {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
    int gameMode = -1;
    bool menuOpen = false;  // Halcyon's menu (ours takes no frame here)
    bool headValid = false;
    XrPosef head{};
    XrPosef eyes[2]{};
    XrFovf fov[2]{};
    int eyeWidth = 0;   // the world layer's eye image, pixels (0: unknown)
    int eyeHeight = 0;
};

// Each frame from the overlays (Present thread): the hologram's layer, or null.
XrCompositionLayerBaseHeader const* Layer(Frame const& frame);
// The hologram is up (the flat screen moves aside).
bool Active();
// A controller's ray is on the table (the game pointer then doesn't click the flat screen).
bool Hovering();
// Snapshot JSON ("galaxyHologram").
std::string StatusJson();

} // namespace mevr::host::galaxy_hologram
