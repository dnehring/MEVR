// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <string>
#include <utility>

// The game's GUI cursor steered onto the pointer's point (research/56 section 6, touch on the game's own screens; the rules in
// core/vr/GuiCursor). LE1's Flash screens move their mouse by raw relative mouse motion only: SetCursorPos, posted WM_MOUSEMOVE
// and BioPlayerInput.SmoothGuiMouse's result don't move it (measured 2026-11-05), while posted clicks land wherever it is. So each
// tick, while the host's pointer is on a game screen, the mod reads the focused movie's mouse (_root._xmouse/_ymouse, stage units)
// and injects the relative motion that closes the gap (SendInput, MOUSEEVENTF_MOVE), only while the game's window is in front
// (else the motion would go to another program).
//   mevr guimouse                 the focused movie's mouse, the target, the moves sent and the learned gains
//   mevr guimouse to <x> <y>      steer onto that stage point (a test without the pointer)
//   mevr guimouse off             stop steering for the console's target
// LE1, game thread.
namespace mevr::game::gui_cursor {

// `pointer`: the host's pointer on a game screen this frame, as window UV (0..1 from the top left), or nullopt.
void Tick(std::optional<std::pair<float, float>> pointer);
std::string Command(std::string const& args);
std::string StatusJson();

} // namespace mevr::game::gui_cursor
