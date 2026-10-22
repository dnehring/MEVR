#pragma once

#include "game/GamepadMapping.hpp"

#include <cstdint>
#include <optional>
#include <string>

// Physical grenades and melee (research/20). [Throwables] in <GAME>MEVR.ini (game/hand_calibration).
//
// Grenades: a grenade slot on the body (default the left side of the chest), in the holsters' body
// frame. A NEW squeeze of either hand inside it takes a grenade (the left grip's squeeze only when it
// isn't the two-handed grip: the host's report says it began away from the gun). Holding it, a faint
// buzz; letting go while the hand moves fast enough throws it (ik::throwing: the release velocity is
// the fastest moment of the last ~120 ms; the analog grip lets go early when it opens during a fast
// motion); a slow release puts it back. The throw:
//   - SFXGameModeDefault.UseGrenade() through ProcessEvent: the game's own grenade (count, cooldown,
//     Shepard's throw animation, the squad's notification). ME1 throws only with a weapon out, so
//     taking a grenade with the weapon away draws it first ([Throwables] drawForThrow);
//   - at the animation's ThrowGrenade notify the game's BioActiveGrenade calls the BioGrenade event
//     LaunchGrenadeAtPoint(target from its own camera trace); our post-handler then re-aims the
//     projectile it returned:
//       disc (default): the game's BioGrenadeProjectileFrisbee flies straight at its own speed and only
//       collides near its "intended target"; we start it at the hand (fromHand), point it at where the
//       hand's throw would land (an arc from the hand's release velocity times throwGain, traced with
//       Actor.Trace), and make that point its intended target (no homing);
//       arc (experimental): the game's ballistic grenade class (BioGrenadeProjectileBallistic, shipped
//       but unused: a rigid body with a velocity) launched with the hand's velocity through
//       BioGrenade.LaunchGrenade, replacing the disc.
// Melee: a fast stroke of a hand relative to the head (ik::throwing::SwingDetector: speed, travel in
// one direction, not backward, once per stroke, a cooldown) with an enemy in reach (a living hostile
// BioPawn within meleeReachM and meleeConeDeg of the body's front) calls SFXGameModeDefault.TryMelee():
// the game's pistol whip or rifle butt, its cone and cooldown. A tick on the strike; a thump when the
// target's health drops within 1.2 s (the hit).
// B and X stay the game's melee and grenade unless [Throwables] meleeButton / grenadeButton = 0 (then
// kept from the game on foot; host v14).
//
// Ties to the other hand features: a squeeze that takes a grenade is "claimed" (Claim, before holsters
// and the squad menu tick): the squad menu ignores a claimed left squeeze; holsters keep the gun out
// while the right hand holds a grenade, and after it threw one until the right grip squeezes again
// (RightHandBusy). Logged once per event; a heartbeat line; "throwables" in the snapshot's mod status.
// Game thread. LE2/LE3: off.
namespace mevr::game::throwables {

// What the host offers this tick (Mod.cpp fills it from HostServices).
struct Link {
    // HostServices::rightGripFree (v9) and leftGripAway (v11) reports: nullopt with an older host (our
    // own latch of the grip stands in).
    std::optional<std::uint32_t> rightGrip;
    std::optional<std::uint32_t> leftGrip;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
    void (*hapticContinuous)(int hand, float amplitude, float frequencyHz, float seconds) = nullptr;
    // The host's pad mapping knows gamepad::kFlagNoMeleeButton / kFlagNoGrenadeButton (v14).
    bool buttonFlags = false;
};

// Finds the script functions and registers the launch post-handlers (call before the ProcessEvent hook
// goes live). False until the script packages have loaded (or not LE1).
bool TryStartup();
// Each tick before holsters and the squad menu: which new squeezes take a grenade.
void Claim(Link const& link);
bool ClaimedLeft();
bool ClaimedRight();
// The right hand holds a grenade, or threw (or put back) one and hasn't squeezed the grip since:
// holsters treat the grip as still holding the gun, so it doesn't go back to its holster meanwhile.
bool RightHandBusy();
// Each tick after holsters (it reads their eye height and draw timing).
void Tick(double deltaSeconds, Link const& link);
// B and X kept from the game on foot if turned off; RB kept off a new right squeeze at the grenade slot
// (when the right grip is RB at all). Called by game/support_grip before it publishes the flags.
void AdjustPad(gamepad::Options& options);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::throwables
