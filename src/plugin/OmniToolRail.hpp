// SPDX-License-Identifier: MIT
#pragma once

#include "plugin/WorldUi.hpp"

#include <openxr/openxr.h>

#include <optional>
#include <string>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The omni-tool's rail in the host (research/56): the column of the game's screens to the left of the screen projected from
// the left wrist (or alone on the wrist), drawn with the main menu's RmlUi (assets/menu/omnitool.rml, a second context of
// plugin/menu's Ui) into a quad of its own. Its entries ask the mod to open a screen through the main menu's request slot
// (HostServices v19: vr::game_menu::Action); a controller's ray or the index fingertip is its mouse (vr::game_pointer). Tapping
// the left wrist with the right index fingertip opens and closes it (vr::omni_rail::Tap). The rules: core/vr/OmniRail.
namespace mevr::host::omni_rail {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    bool headValid = false;
    XrVector3f eyes{};                          // app space
    std::optional<world_ui::Shown> screen;      // the game's screen this frame (on the wrist or in the room), if any
    bool wristValid = false;
    XrVector3f wrist{};                         // the left wrist's point (the HUD's wrist piece), app space
    bool gunDrawn = false;                      // the mod's gun panel shows: no tap
    int gameMode = -1;                          // the game mode Halcyon last read (0 on foot, 7 the game's screens)
    bool othersOwnInput = false;                // our main menu, Halcyon's menu or the calibration is up
};

// Every frame from the overlays (render thread). nullptr while the rail isn't shown.
XrCompositionLayerQuad const* Layer(Frame const& frame);
// The rail opened or closed from elsewhere (the main menu's button, a test). Any thread.
void Toggle(char const* why);
// A controller's ray is on the rail or a press on it is held: its trigger is ours, kept from the game. Any thread.
bool OwnsTriggers();
// The rail is shown (alone on the wrist or beside a screen): the mod puts the game's omni-tool on Shepard's arm for it
// (HostServices::mainMenu's kOmniRailOpen). Any thread.
bool Shown();
// For snapshots. Any thread.
std::string StatusJson();

} // namespace mevr::host::omni_rail
