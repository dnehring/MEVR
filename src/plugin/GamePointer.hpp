// SPDX-License-Identifier: MIT
#pragma once

#include "halcyon/le1/mevr_bridge.h"

#include <openxr/openxr.h>

namespace mevr::hook {
class Context;
}

#include <string>
#include "core/vr/OmniRail.hpp"

// The game pointer (research/42): while Halcyon shows the game on its flat screen (the game's own menus: inventory,
// journal, map, the pause menu, loading and save screens; the galaxy map), a controller's aim ray on that quad is the
// game's MOUSE: the hit's UV becomes a pixel of the game window's client area, the trigger the left button, the right
// stick the wheel, and a dot on the quad marks the hit. The mouse reaches the game as window messages posted to its
// window and as the OS cursor (SetCursorPos), each a setting, since UE3 reads both. Nothing is sent while the ray is
// off the quad, while our main menu or Halcyon's is open (they own the pointer), or while the guided calibration runs.
// Host side, LE1 only; `[Pointer]` in ASI\LE1MEVR.ini; log `game pointer:`. Present thread unless noted.
namespace mevr::host::game_pointer {

struct Frame {
    std::optional<vr::omni_rail::Body> body; // Shepard's drawn bones in app space
    XrSpace appSpace = XR_NULL_HANDLE;
    bool halcyonMenuOpen = false;   // Halcyon's ImGui menu is open: its pointer, not ours
    bool railOwnsInput = false;     // this frame's drawn rail has the right hand; do not click the screen behind it
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

// At attach (plugin/Entry): user32's GetCursorPos hooked, so that the game's Flash cursor can be put on the pointer's point
// (`[Pointer] gameCursor`, research/57 section 6). Never removed.
void InstallCursorHook(mevr::hook::Context& init);

// HostServices v30: the GUI events the pointer queued for the game's menus (core/vr/GuiEvents), up to `capacity`, oldest first;
// the number taken. Calling it tells the pointer the mod sends them (else it gives the game real input). Any thread.
std::uint32_t TakeGuiEvents(std::uint8_t* events, std::uint32_t capacity);
// A pad direction pressed or let go (vr::gui_events::IsControl: the bypass puzzle's d-pad, plugin/OmniPad, research/53 section
// 6.3), queued for the mod with the pointer's own GUI events and sent through GuiKey on the game thread. Any thread.
void QueueGuiControl(std::uint8_t event);

// The game's UI on a quad of ours (app space; the whole UI frame edge to edge, as the window's client area), pointed at this
// frame: the galaxy map's travel question over the hologram's table (research/51 section 8). Present thread, before Step.
// `touchArea`: the part of the quad a fingertip can touch (quad UV, u0 v0 u1 v1): the visible crop of a world UI screen.
// `projectingHand`: the hand a wrist screen is projected from (0 left), whose fingertip doesn't touch it (it sits 10 cm under the
// screen's edge and pressed and scrolled it in a replay, research/57 section 6); -1 none.
// The world UI's screen stops being a pointer target at once (the bypass puzzle began: research/53 section 6.3): the quad is
// forgotten, GetCursorPos answers the real cursor again and the GUI events not yet taken are dropped. Present thread, before Step.
void ForgetGameUiQuad();
// Every frame the bypass puzzle is played (research/53 section 6.3): its first forgets the quad (ForgetGameUiQuad), and for a
// second after the last no quad of the game's is a pointer target (the world UI's or Halcyon's flat panel, which a hitch can bring
// back for a frame) and GetCursorPos isn't answered: any cursor off its centre turns the puzzle's marker. Present thread.
void HoldOffForPuzzle();
void NoteGameUiQuad(XrPosef const& pose, float width, float height,
    MELEVR::MevrBridge::PanelKind kind = MELEVR::MevrBridge::PanelKind::GalaxyMap, float const* touchArea = nullptr,
    int projectingHand = -1);

} // namespace mevr::host::game_pointer
