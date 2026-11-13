// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Thespian.hpp"

#include <openxr/openxr.h>

#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

// Thespian mode's panel in the host (research/70 section 8 step 5, core/vr/Thespian; the layout is the owner's concept art):
// while the mod sends a shown panel (HostServices v39 setThespian, every game tick a dialogue wheel is spoken) a quad stands
// under the line of sight to the speaker with the other person's line, one row per reply (the said words lit) and the
// microphone's mark, drawn with the main menu's RmlUi (assets/menu/thespian.rml, a context of its own; its rows are
// vr::thespian::RowsRml's markup). Placed once per wheel; one evictable swapchain (plugin/Swapchains). It takes no input: a
// reply is taken by voice (the mod) or by the game's own wheel. Log `thespian panel:`; snapshot "thespianPanel", layer
// `thespian`.
namespace mevr::host::thespian {

// The mod's panel, each game tick (any thread). A panel not renewed for half a second is gone.
void SetPanel(vr::thespian::Panel const* panel);

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    XrPosef head{};               // the head in app space
    bool othersOwnInput = false;  // our main menu, Halcyon's menu or the calibration is up
};

// Every frame from the overlays (render thread). nullptr while the panel isn't shown.
XrCompositionLayerQuad const* Layer(Frame const& frame);
// The panel went out in the last frame: the game's wheel quad at the hand stands down for it. Any thread.
bool Shown();
// ... and the game's own wheel is hidden under it (the mod hid it; the stick hasn't brought it back). Any thread.
bool HidesGameWheel();
// For snapshots. Any thread.
std::string StatusJson();

}  // namespace mevr::host::thespian
