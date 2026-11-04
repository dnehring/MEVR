// SPDX-License-Identifier: MIT
#pragma once

#include "halcyon/le1/mevr_bridge.h"

#include <openxr/openxr.h>

#include <string>

// The game pointer (research/42): while Halcyon shows the game on its flat screen (the game's own menus: inventory,
// journal, map, the pause menu, loading and save screens; the galaxy map), a controller's aim ray on that quad is the
// game's MOUSE: the hit's UV becomes a pixel of the game window's client area, the trigger the left button, the right
// stick the wheel, and a dot on the quad marks the hit. The mouse reaches the game as window messages posted to its
// window and as the OS cursor (SetCursorPos), each a setting, since UE3 reads both. Nothing is sent while the ray is
// off the quad, while our main menu or Halcyon's is open (they own the pointer), or while the guided calibration runs.
// Host side, LE1 only; `[Pointer]` in ASI\LE1MEVR.ini; log `game pointer:`. Present thread unless noted.
namespace mevr::host::game_pointer {

struct Frame {
    XrSpace appSpace = XR_NULL_HANDLE;
    bool halcyonMenuOpen = false;   // Halcyon's ImGui menu is open: its pointer, not ours
    bool headValid = false;
    XrVector3f head{};              // the eyes' centre, app space (the dot's angular size)
};

struct Dot {
    XrPosef pose{};      // app space, facing like the flat quad
    float size = 0.0f;   // edge length, meters
    bool pressed = false;
};

// Every frame (OverlayLayers), after the controllers were synced and the flat quad (if any) was noted this frame
// through MevrBridge::OnFlatQuad: steps the pointer, sends the mouse to the game, and says where the dot goes.
// False: no dot this frame (and anything held was released).
bool Step(Frame const& frame, Dot& dot);

// For snapshots (plugin/TestLoop). Any thread.
std::string StatusJson();

// A controller's ray is on the flat screen (or a press on it is held): its trigger is the mouse button, so the pad's triggers are
// kept from the game (a trigger that also reached the game as RT switched a Flash box to the pad, which then ignored the click:
// the galaxy map's travel question, research/51 section 8). Any thread.
bool OwnsTriggers();

// The game's UI on a quad of ours (app space; the whole UI frame edge to edge, as the window's client area), pointed at this
// frame: the galaxy map's travel question over the hologram's table (research/51 section 8). Present thread, before Step.
void NoteGameUiQuad(XrPosef const& pose, float width, float height,
    MELEVR::MevrBridge::PanelKind kind = MELEVR::MevrBridge::PanelKind::GalaxyMap);

} // namespace mevr::host::game_pointer
