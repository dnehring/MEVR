// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

// "Pause while open" for the host's VR main menu (plugin/MainMenu, research/28 section 6, HostServices v16): while
// the menu is open and the player wants it, the game is paused the way its own weapon wheel pauses it
// (SFXGameModeWeaponWheel.Activated calls WorldInfo.PauseGame(true), research/19), and unpaused when the menu closes,
// the mod unloads or the world goes away. Only on foot or in the Mako (modes 0 and 1): the game's own menus and wheels
// pause it themselves, and a conversation or cutscene isn't ours to stop. LE1; a no-op elsewhere. Game thread.
namespace mevr::game::menu_pause {

struct Link {
    std::uint32_t (*state)() = nullptr;  // HostServices::mainMenu (kMainMenuOpen, kMainMenuPause); null: no menu
};

void Tick(Link const& link);
// Unpauses if the menu paused the game (not while the process exits).
void Shutdown(bool exiting);
std::string StatusJson();

} // namespace mevr::game::menu_pause
