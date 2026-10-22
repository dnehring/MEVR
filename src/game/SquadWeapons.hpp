#pragma once

#include "game/GamepadMapping.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace mevr::vr::squad_menu {
struct View;
}

// Squad weapons in VR (docs/squad-weapons-design.md, option A; built 2026-10-22): point the left hand
// at a squadmate and squeeze the left grip away from the gun's foregrip, and a menu opens above the
// hand: the two squadmates' cards left and right (the pointed one chosen), and a radial of the weapon
// types the chosen one carries. The left stick picks the card, a flick of the right stick a weapon;
// letting go of the grip (or A) gives it to them, B cancels (core/vr/SquadMenu's machine).
//
// Each tick, LE1 on foot with the host's world view and a v11 host:
//   - the squad: the player pawn's BioPawnBehavior.Squad, its members through BioBaseSquad.GetSquadSize /
//     GetMember (the natives the weapon wheel uses), each one's name, class (BioPawnBehavior.GetClass),
//     health (BioEpicPawnBehavior.GetPercentHealth), quick slots (BioEquipment, as the holsters read
//     Shepard's) and position (mapped into tracking space with the view anchor);
//   - the left hand's aim ray against them (squad_menu::PickPointed);
//   - a new left squeeze away from the gun: the host's count (HostServices::leftGripAway);
//   - the pad (AdjustPad): kFlagSquadMenu while it's open, so the game sees no sticks, A, B or stick
//     clicks; and a left grip away from the gun is never LB while the menu is on;
//   - the menu to draw (HostServices::setSquadMenu), haptic ticks and clicks (hapticPulseHz);
//   - on confirm, what the weapon wheel does when it closes (SFXGameModeWeaponWheel.Deactivated, read
//     from its bytecode): BioPlayerController.SquadOrderEquipWeapon(type, pawn) records the choice in
//     the member's squad entry; then, with Shepard's weapon out (IsInCombatActionState),
//     BioPlayerController.ApplyTacticalOrders() (the AI switches), else the member's
//     BioEquipment.ChangeToQuickSlot(type) and BioBaseSquad.SetMemberSwitchWeapon(index, 4). The
//     wheel's own steps for Shepard (UpdatePrimaryAndSecondaryWeapons, DrawPrimaryWeapon) are left out:
//     his weapon isn't being changed. All through ProcessEvent, never the SDK's wrappers.
// Logged once per change (`squad:` lines); a heartbeat line; the snapshot's mod status has a "squad"
// object. Game thread. LE2/LE3: off.
namespace mevr::game::squad_weapons {

// What the host offers this tick (Mod.cpp fills it from HostServices v11).
struct Link {
    bool supported = false;  // a v11 host with a matching squad_menu::View (or the monolithic build)
    std::optional<std::uint32_t> leftGripAway;  // HostServices::leftGripAway
    void (*setMenu)(vr::squad_menu::View const* view) = nullptr;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;                      // v9
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr; // v10
};

// Finds the script functions. False until the script packages have loaded (or not LE1).
bool TryStartup();
void Tick(double deltaSeconds, Link const& link);
// The pad options while the menu is on: kFlagSquadMenu while it's open, and no LB from a left grip away
// from the gun. Called by game/support_grip before it publishes the flags (so after Tick).
void AdjustPad(gamepad::Options& options);
bool IsOpen();
// For the test loop's mod status: a JSON object.
std::string StatusJson();
void LogStats();
// Closes the menu (unpausing the game if it paused it) and forgets everything.
void Shutdown(bool processExiting);

} // namespace mevr::game::squad_weapons
