// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// ME1 freezes the world (WorldInfo.bPlayersOnly: only the players tick) whenever its window loses focus: BioWorldInfo.Tick
// sets it while HasFocus() is false and the game mode allows a pause, and clears it when focus comes back (research/47
// "Why the Mako wouldn't let Shepard out"). In VR the desktop window's focus says little: the headset's dashboard or a click
// beside the window freezes NPCs, enemies and the Mako's exit without a word. `[Game] pauseUnfocused = 0` undoes that
// pause right after the tick that made it (a post-handler of BioWorldInfo.Tick); 1 (the default) keeps the game's own
// behaviour. Any other pause (a menu, the wheels, our main menu's PauseGame) is left alone: only m_bPausedByFocusLoss's.
// LE1 only. Log prefix `focus pause:`.
namespace mevr::game::focus_pause {

// Registers the handler (before the ProcessEvent hook goes in).
bool TryStartup();
// Snapshot JSON ("focusPause").
std::string StatusJson();
void Shutdown();

} // namespace mevr::game::focus_pause
