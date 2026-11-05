// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

// Where the game's Flash cursor is (research/57 section 6), a dev probe for the game pointer's cursor steering (host
// plugin/GamePointer, `[Pointer] gameCursor`). LE1's screens put their mouse where the Windows cursor is in the window's client area
// (GetCursorPos), scaled to the stage and moved up by the 720-tall content's offset, and only read it when real mouse input
// arrives (measured 2026-11-05).
//   mevr guimouse     the focused movie's mouse (_root._xmouse/_ymouse), its stage size, and where the client pixel the Windows
//                     cursor reports would put it
//   mevr guikey <event|w> ...   GUI events through GuiKey on the active game mode, one per tick (w: a tick without one): e.g.
//                     `6 w 50 w w w 52` = the mouse's motion, the left button, its release (game_menu_request::GuiKey)
// Each tick it also sends the game pointer's input as those events (HostServices v30, core/vr/GuiEvents' Scheduler): the host
// answers GetCursorPos with the pointer's point, so GuiKey(6) puts the screen's cursor there and 50/52 click it, exactly.
// LE1, game thread.
namespace mevr::game::gui_cursor {

std::string Command(std::string const& args);
std::string KeyCommand(std::string const& args);
// `take`: HostServices::takeGuiEvents, or null (an older host: only the console's events).
void Tick(std::uint32_t (*take)(std::uint8_t* events, std::uint32_t capacity));
std::string StatusJson();

} // namespace mevr::game::gui_cursor
