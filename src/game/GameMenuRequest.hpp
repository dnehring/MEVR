// SPDX-License-Identifier: MIT
#pragma once

#include "game/WorldUi.hpp"

#include <cstdint>
#include <string>

// Our main menu's Game menu and Holster buttons (research/28 "The game's menus", HostServices v19): the host's menu
// leaves a request (core/vr/GameMenuRequest); each tick the mod takes it and, once our menu is closed and its pause
// lifted (game/MenuPause) for a couple of ticks, calls the game's own function on the game thread:
//   - the pause menu: ShowMenuGui on the ACTIVE game mode (the function looked up on that object's class, so the
//     Mako's, the wheels', squad command's or photo mode's override runs; SFXGameModeBase's on foot), what Start's
//     Console_Menu binding calls;
//   - holster: SFXGameModeDefault.TryHolster (Back's Console_Holster), only on foot and only while a weapon is drawn
//     (it's a toggle: with the weapon away it would draw, which [Holsters] onlyHolstersDraw forbids anyway);
//   - leave (ME2 and ME3; the owner's answer to question 16, 2026-11-17; research/28 "Leave"): LeaveWorld on the active
//     mode, what their B tap called on foot. The menu shows it only where it would act: each tick the mod tells the host
//     (HostServices v44 setGameMenuOffers) whether the player is on foot, out of combat and in a level whose script has an
//     armed SFXSeqEvt_LeaveWorld event. ME1 has none on foot (its LeaveWorld is the Mako's).
// Shared request scheduler; per-game native entry points. LE3 contracts: research/le3-native-gui.md. Game thread.
namespace mevr::game::game_menu_request {

struct Link {
    std::uint32_t (*take)() = nullptr;  // HostServices::takeGameMenuRequest (v19); null: an older host (nothing to take)
    bool menuOpen = false;              // HostServices::mainMenu has kMainMenuOpen
    world_ui::Link worldUi;
    void (*offer)(std::uint32_t offers) = nullptr;  // HostServices::setGameMenuOffers (v44); null: nobody to tell
};

void Tick(Link const& link);
// `mevr omni <equipment|squad|map|journal|codex|computer|close|holster>` (a dev verb, research/57): the same request the
// omni-tool rail's entry gives, as if the host had handed it over, so switching screens is testable without a hand on the
// rail. Returns what it did, for console.result.
std::string Inject(std::string const& which);
// `mevr omni offers`: whether the menu's Leave would show now and why (the facts read at once, not the last tick's).
std::string LeaveStatus();
// A GUI event as the player's input gives it to the game's screens: SFXGameModeBase.GuiKey(Event) on the active game mode (the
// native BIOGUI_EVENT numbers, different per game; core/vr/GuiEvents translates only the canonical host queue. False with
// why when there is no mode or no GuiKey. Game thread.
bool GuiKey(std::uint8_t event, std::string& why);
// Runtime mode, focus and parameter contract available before consuming host input.
bool GuiInputReady();
// Forgets a request not yet run.
void Shutdown();
std::string StatusJson();

} // namespace mevr::game::game_menu_request
