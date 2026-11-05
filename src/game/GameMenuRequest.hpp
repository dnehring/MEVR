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
// `mevr omni <equipment|squad|map|journal|codex|computer|close|holster>` (a dev verb, research/56): the same request the
// omni-tool rail's entry gives, as if the host had handed it over, so switching screens is testable without a hand on the
// rail. Returns what it did, for console.result.
std::string Inject(std::string const& which);
// A GUI event as the player's input gives it to the game's screens: SFXGameModeBase.GuiKey(Event) on the active game mode (the
// BIOGUI_EVENT numbers: 13 B, 50 the left mouse button, 52 its release, 28/29 the wheel, 6/7 the mouse's motion). False with
// why when there is no mode or no GuiKey. LE1, game thread.
bool GuiKey(std::uint8_t event, std::string& why);
// Forgets a request not yet run.
void Shutdown();
std::string StatusJson();

} // namespace mevr::game::game_menu_request
