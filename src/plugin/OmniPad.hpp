// SPDX-License-Identifier: MIT
#pragma once

#include "plugin/WorldUi.hpp"

#include <openxr/openxr.h>

#include <optional>
#include <string>
#include "core/vr/OmniRail.hpp"

struct ID3D11Device;
struct ID3D11DeviceContext;

// The omni-tool's d-pad in the host (research/53 section 6.3, core/vr/OmniPad): while the bypass puzzle is played (the mod's
// world_ui::State::puzzle) a square quad stands to the right of its screen with four large arms, drawn with the main menu's RmlUi
// (assets/menu/omnipad.rml, a context of its own). The right index fingertip (or a ray and its trigger) pressing an arm sends the
// game's own d-pad event for it (CONTROL_UP and the rest, through the mod's GuiKey, HostServices v30's queue) and its release when
// let go; sliding to another arm changes the direction without lifting. `[OmniTool] pad = 0` turns it off, `padSizeM` sizes it.
namespace mevr::host::omni_pad {

struct Frame {
    std::optional<vr::omni_rail::Body> body; // Shepard's drawn bones in app space
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    std::optional<world_ui::Shown> screen;  // the game's screen this frame (on the wrist or in the room), if any
    bool othersOwnInput = false;            // our main menu, Halcyon's menu or the calibration is up
};

// Every frame from the overlays (render thread). nullptr while the pad isn't shown.
XrCompositionLayerQuad const* Layer(Frame const& frame);
// A controller's ray is on the pad or a press on it is held: its trigger is ours, kept from the game. Any thread.
bool OwnsTriggers();
// For snapshots. Any thread.
std::string StatusJson();

} // namespace mevr::host::omni_pad
