// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

// Our main menu's Game menu and Holster buttons (research/28 "The game's menus", HostServices v19): the host's menu
// leaves a request (core/vr/GameMenuRequest); each tick the mod takes it and, once our menu is closed and its pause
// lifted (game/MenuPause) for a couple of ticks, calls the game's own function on the game thread:
//   - the pause menu: ShowMenuGui on the ACTIVE game mode (the function looked up on that object's class, so the
//     Mako's, the wheels', squad command's or photo mode's override runs; SFXGameModeBase's on foot), what Start's
//     Console_Menu binding calls;
//   - holster: SFXGameModeDefault.TryHolster (Back's Console_Holster), only on foot and only while a weapon is drawn
//     (it's a toggle: with the weapon away it would draw, which [Holsters] onlyHolstersDraw forbids anyway).
// LE1; a no-op elsewhere. Game thread.
namespace mevr::game::game_menu_request {

struct Link {
    std::uint32_t (*take)() = nullptr;  // HostServices::takeGameMenuRequest (v19); null: an older host (nothing to take)
    bool menuOpen = false;              // HostServices::mainMenu has kMainMenuOpen
};

void Tick(Link const& link);
// Forgets a request not yet run.
void Shutdown();
std::string StatusJson();

} // namespace mevr::game::game_menu_request
