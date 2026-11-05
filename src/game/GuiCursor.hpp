// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Where the game's Flash cursor is (research/56 section 6), a dev probe for the game pointer's cursor steering (host
// plugin/GamePointer, `[Pointer] gameCursor`). LE1's screens put their mouse where the Windows cursor is in the window's client area
// (GetCursorPos), scaled to the stage and moved up by the 720-tall content's offset, and only read it when real mouse input
// arrives (measured 2026-11-05).
//   mevr guimouse     the focused movie's mouse (_root._xmouse/_ymouse), its stage size, and where the client pixel the Windows
//                     cursor reports would put it
// LE1, game thread.
namespace mevr::game::gui_cursor {

std::string Command(std::string const& args);

} // namespace mevr::game::gui_cursor
