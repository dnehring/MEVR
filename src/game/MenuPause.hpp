// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

// "Pause while open" for the host's VR main menu (plugin/MainMenu, research/28 section 6, HostServices v16): while
// the menu is open and the player wants it, the game is paused the way its own weapon wheel pauses it
// (SFXGameModeWeaponWheel.Activated calls WorldInfo.PauseGame(true), research/19), and unpaused when the menu closes,
// the mod unloads or the world goes away. Acquire only in controllable modes (LE1/LE2's existing 0/1,
// LE3's semantic on-foot/vehicle/Atlas): the game's own menus and wheels
// pause it themselves, and a conversation or cutscene isn't ours to stop. All three games; game thread.
// ME2/ME3 restore the original world's saved bPlayersOnly value; a failed restore stays pending for retry.
// ME3 calls BioWorldInfo.PauseGame and observes admission, including its SFXGRI policy; it never forces the bit.
namespace mevr::game::menu_pause {

struct Link {
    bool squadPause = false; // sequel custom squad menus use this same pause owner
    std::uint32_t (*state)() = nullptr;  // HostServices::mainMenu (kMainMenuOpen, kMainMenuPause); null: no menu
};

void Tick(Link const& link);
// Recompute the same owner immediately when a sequel squad panel opens/closes; preserve any main-menu pause.
void SetSquadPause(bool pause);
// The game is paused by the main menu now (we paused it and haven't unpaused yet): game/GameMenuRequest waits for it.
bool Paused();
// Unpauses if the menu paused the game (not while the process exits).
void Shutdown(bool exiting);
std::string StatusJson();

} // namespace mevr::game::menu_pause
