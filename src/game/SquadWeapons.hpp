// SPDX-License-Identifier: MIT
#pragma once

#include "game/GamepadMapping.hpp"
#include "ue3/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mevr::vr::squad_menu::wheel {
struct Panel;
}

// Squad weapons in VR (docs/squad-weapons-design.md): on every game the left trigger's pull ([Controls] leftTrigger = lb)
// is the game's own weapon wheel, whatever the left hand does (open, or on the gun's foregrip), shown on a panel at the
// left hand (plugin/WheelPanel). (The owner, 2026-11-18: "just remove the custom menu and just always default to the
// classic menu"; our drawn menu, [Squad] style = custom, is gone, and an old file's custom moves to native once.) Who the
// pull is: vr::squad_menu::RouteTrigger, decided per pull (squad_menu::OwnTrigger):
//
// ME1, on foot and in the wheels, the left hand off the gun: the squad's. A new pull opens the GAME's own weapon wheel at
// once (SFXGameModeDefault.EnterWeaponWheel, what holding LB does: ME1's look, its pause, its squad and weapon rules); the
// game sees no LB from the trigger (no 0.3 s hold, no tap that swaps Shepard's weapon). The right stick (wheelStick)
// highlights an entry of its one ring (squadmates and weapons together); letting go of the trigger sends A for the
// highlighted entry (the game's own "queue it") and then SFXGameModeWeaponWheel.ExitWeaponWheel (what releasing LB does),
// whose Deactivated applies the queue. Pointing the left hand at a squadmate as you pull puts the wheel on their weapon in
// hand (the HUD's kept stick angle, BioSFHandler_HUD.nLastAngle). The highlighted entry comes from the HUD's nAngle
// (squad_menu::wheel); a tick in the steering hand on each change. The machine is squad_menu::wheel::Machine. (Until
// 2026-10-22 a left-grip squeeze away from the gun opened it; the grip is now only the support grip and grenades.)
//
// Everywhere else the pull is the game's own LB, whose hold opens the same wheel by the game's binding: ME1 with both
// hands on the gun, in the Mako (boost) and menus; ME2 and ME3 always (their wheels are other UIs, which the mod can't
// open, steer or preselect; ME3's renderer knows its wheel by the pad's LB held). On the sequels the module only reads the
// squad (BioWorldInfo.m_playerSquad, each henchman's inventory chain and Pawn.Weapon) for the squad's context, orders and
// voice, and switches a squadmate's weapon for them (GiveWeapon: BioPlayerController.OrderWeaponSwitch, game/Le2SquadWeapons.inc).
//
// Each tick on ME1 with a v12 host: the squad (the player pawn's BioPawnBehavior.Squad, its members through
// BioBaseSquad.GetSquadSize / GetMember, each one's name, class, health and quick slots); the left hand's aim ray against
// them (squad_menu::PickPointed); the pad (AdjustPad): kFlagNativeWheel while the left trigger holds the game's wheel,
// with the steering stick and the A it sends; no LB (nor LT) from the left trigger while the squad has it; and a left grip
// away from the gun is never LB. Every game: the wheel panel's settings (HostServices::setWheelPanel). Logged once per change
// (`squad:` lines); a heartbeat line; the snapshot's mod status has a "squad" object. Game thread.
namespace mevr::game::squad_weapons {

// What the host offers this tick (Mod.cpp fills it from HostServices v12).
struct Link {
    bool menuOpen = false; // main menu or wrist rail owns inputs
    bool supported = false;  // a v12 host: the pad's native wheel flags
    // v12: the wheel panel's settings (a matching squad_menu::wheel::Panel); null where the host doesn't take them (the sequels')
    void (*setWheelPanel)(vr::squad_menu::wheel::Panel const* panel) = nullptr;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;                      // v9
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr; // v10
};

// Finds the script functions. False until the script packages have loaded.
bool TryStartup();
void Tick(double deltaSeconds, Link const& link);
// The pad options: kFlagNativeWheel (with the steering stick and the A it sends) while the left trigger holds the game's
// wheel (ME1), no LB from the left trigger while the squad has it, and no LB from a left grip away from the gun. Called by
// game/support_grip before it publishes the flags (so after Tick).
void AdjustPad(gamepad::Options& options);
// The left trigger holds the game's wheel (ME1's OpensWheel route). False on the sequels, whose wheel is the game's LB.
bool IsOpen();
// For the test loop's mod status: a JSON object.
std::string StatusJson();
void LogStats();
// Lets go of the game's wheel if the left trigger holds it, and forgets everything.
void Shutdown(bool processExiting);

// For other modules (the squad context, research/54): this tick's squadmates as the module last read them (names, classes,
// weapons, health; -1 health unknown), at most kMaxMembers. None while it can't read the squad.
struct MemberView {
    ue3::Object* pawn = nullptr;
    int classId = -1;
    std::uint8_t carried = 0;
    int current = -1;
    float health = -1.0f;
    int squadIndex = -1;  // the squad's own index (QuickCommand*'s iSquadMember)
    std::string name;
};
std::vector<MemberView> Members(bool refresh = false);
// A weapon object's quick-slot type (0 pistol .. 3 sniper rifle, the sequels' 4 the SMG; -1 not a ranged weapon).
int WeaponTypeOf(ue3::Object* weapon);
// A squadmate's weapon switched by the game's own order (the squad orders' "switch to your sniper"; ME1 the weapon wheel's
// close, the sequels OrderWeaponSwitch): the outcome in words.
std::string GiveWeapon(ue3::Object* pawn, int weapon);

} // namespace mevr::game::squad_weapons
