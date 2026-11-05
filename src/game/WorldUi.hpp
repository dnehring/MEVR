// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/WorldUi.hpp"

#include <string>

// The game's full-screen screens in the room (research/53; the shared part: core/vr/WorldUi). LE1, [WorldUi] on = 1. While the
// game is in its GUI mode (7), each tick: the GUI manager's open panels (BioSFManager.m_lstPanels, each tagged with its library
// tag, nmTag), the first one of ours (core/vr/WorldUi's table) if [WorldUi] screens lists it, and what it belongs to (the
// shop: its handler's m_shop actor, which stands at the vendor), its place in tracking space; sent to the host
// (HostServices v27 setWorldUi), which keeps the world in stereo and stands the game's UI in the room. Through our reflection
// layer; a missing piece stands it down with a `world ui:` line. Game thread.
namespace mevr::game::world_ui {

struct Link {
    // HostServices v27; null: an older host (the screens stay on Halcyon's panel).
    void (*setState)(vr::world_ui::State const* state) = nullptr;
    // The host's omni-tool rail is shown (HostServices::mainMenu's kOmniRailOpen, research/56): with no screen of ours up, on
    // foot, the game's omni-tool goes on Shepard's arm for it too ([WorldUi] omniTool), so the tap opens the omni-tool.
    bool railOpen = false;
};

// Before the ProcessEvent hook goes in: the camera-update handler that records Shepard's view on foot (held while a screen is up).
void TryStartup();
void Tick(Link const& link);
// A screen is up and projected from the wrist ([WorldUi] wrist): arm IK drives Shepard's arms then as on foot (the omni-tool
// on the left forearm meets the controller that projects it), in the held on-foot frame (view_anchor::Hold). Game thread.
bool WristScreenUp();
// Snapshot JSON ("worldUi").
std::string StatusJson();
// Mod shutdown: the host forgets the screen.
void Shutdown(Link const& link);

} // namespace mevr::game::world_ui
