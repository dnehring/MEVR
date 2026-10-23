// SPDX-License-Identifier: MIT
#pragma once

#include "game/GamepadMapping.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace mevr::vr::squad_menu {
struct View;
namespace wheel {
struct Panel;
}
}

// Squad weapons in VR (docs/squad-weapons-design.md): pull the left trigger ([Controls] leftTrigger = lb,
// the weapon wheel's button) and the wheel opens at the left hand at once; pointing the left hand at a
// squadmate as you pull puts it on them. Letting go of the trigger gives the highlighted weapon. (Until
// 2026-10-22 a left-grip squeeze away from the gun opened it, after 0.35 s at nobody; the user found the
// grip kept opening it when grabbing for the foregrip. The left grip is now only the support grip and
// grenades.) The squad takes the trigger per pull (squad_menu::OwnTrigger) on foot and in the wheels, so
// the game never sees it as LB there (no 0.3 s hold, no tap that swaps Shepard's weapon); in the Mako and
// menus it stays LB.
//
// [Squad] style = native (the default since 2026-10-22, host v12): that opens the GAME's own weapon
// wheel (SFXGameModeDefault.EnterWeaponWheel, what holding LB does: ME1's look, its pause, its squad and
// weapon rules), which the host shows on a panel at the left hand (squad_menu::wheel::Panel). The
// right stick (wheelStick) highlights an entry of its one ring (squadmates and weapons together); letting
// go of the trigger sends A for the highlighted entry (the game's own "queue it") and then
// SFXGameModeWeaponWheel.ExitWeaponWheel (what releasing LB does), whose Deactivated applies the queue.
// Pointing at a squadmate puts the wheel on their weapon in hand (the HUD's kept stick angle,
// BioSFHandler_HUD.nLastAngle). The highlighted entry comes from the HUD's nAngle (squad_menu::wheel);
// a tick in the steering hand on each change. The machine is squad_menu::wheel::Machine.
//
// style = custom (the first build, kept as the fallback; also with a host older than v12): a menu we
// draw opens above the hand: the two squadmates' cards left and right (the pointed one chosen, else the
// last one given a weapon), and a radial of the weapon types the chosen one carries. The left stick picks
// the card, a flick of the right stick a weapon; letting go of the trigger (or A) gives it to them, B
// cancels (core/vr/SquadMenu's machine).
//
// Each tick (custom), LE1 on foot with the host's world view and a v11 host:
//   - the squad: the player pawn's BioPawnBehavior.Squad, its members through BioBaseSquad.GetSquadSize /
//     GetMember (the natives the weapon wheel uses), each one's name, class (BioPawnBehavior.GetClass),
//     health (BioEpicPawnBehavior.GetPercentHealth), quick slots (BioEquipment, as the holsters read
//     Shepard's) and position (mapped into tracking space with the view anchor);
//   - the left hand's aim ray against them (squad_menu::PickPointed);
//   - a new pull of the left trigger (latched like the pad's), taken for the squad per pull;
//   - the pad (AdjustPad): kFlagSquadMenu while it's open, so the game sees no sticks, A, B or stick
//     clicks; no LB (nor LT) from the left trigger while the squad has it; and a left grip away from the
//     gun is never LB while the menu is on;
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
    bool nativeSupported = false;  // ...and v12: the wheel panel and the pad's native wheel flags
    std::optional<std::uint32_t> leftGripAway;  // HostServices::leftGripAway (unused since the trigger opens it)
    void (*setMenu)(vr::squad_menu::View const* view) = nullptr;
    void (*setWheelPanel)(vr::squad_menu::wheel::Panel const* panel) = nullptr;  // v12
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;                      // v9
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr; // v10
};

// Finds the script functions. False until the script packages have loaded (or not LE1).
bool TryStartup();
void Tick(double deltaSeconds, Link const& link);
// The pad options while the menu is on: kFlagSquadMenu while ours is open, kFlagNativeWheel (with the
// steering stick and the A it sends) while the left trigger holds the game's wheel, no LB from the left
// trigger while the squad has it, and no LB from a left grip away from the gun. Called by
// game/support_grip before it publishes the flags (so after Tick).
void AdjustPad(gamepad::Options& options);
// Ours is open, or the left trigger holds the game's wheel.
bool IsOpen();
// For the test loop's mod status: a JSON object.
std::string StatusJson();
void LogStats();
// Closes the menu (unpausing the game if it paused it) and forgets everything.
void Shutdown(bool processExiting);

} // namespace mevr::game::squad_weapons
