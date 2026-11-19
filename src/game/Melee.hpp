// SPDX-License-Identifier: MIT
#pragma once
#include "core/ik/Melee.hpp"
#include "game/GamepadMapping.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Melee by gesture, the trilogy's one path (research/20 "Gesture only"; docs/trilogy-parity-ledger.md G43 and Forks 12; the
// owner, 2026-11-17: "B on ME2 and ME3 works as on ME1", melee is ME1's fist gesture). Each hand's fast stroke relative to the
// head (ik::melee::Gestures over ik::throwing::SwingDetector) becomes the game's melee through one dispatcher (Strike in
// Melee.cpp) and a small adapter per game for its native names:
//   - which melee (ik::throwing::RouteMelee): the weapon out, either hand's stroke is the game's melee with it; the weapon
//     away, only the right FIST (grip and trigger held) punches; the left hand never does (its trigger and grip are the
//     squad's);
//   - when (ik::throwing::MeleeWait): both hands share one cooldown ([Throwables] meleeCooldownS) that also waits out the
//     game's own melee cooldown, so the gesture never swings faster than the game; an enemy in reach first
//     ([Throwables] meleeNeedsEnemy, meleeReachM, meleeConeDeg);
//   - the game's own calls: ME1 SFXGameModeDefault.TryMelee (pistol whip or rifle butt by the quick slot) and, the weapon
//     away, BioPlayerInput.ActivatePower(Pistol_Whip); ME2 TryMelee (ActivatePower of the held weapon's MeleePowerName), and
//     nothing with the weapon away (holstered, ME2's pawn holds no weapon and its melee is the held weapon's: refused by name);
//     ME3 TryMelee or TryHeavyMelee behind its native admission (research/le3-melee-audit.md).
// The strokes: ME1 and ME2 follow the physical rules (ik::melee::Rules::Physical), fed each new controller sample by
// game/Throwables on ME1 (its body frame, a grenade's hand suppressed) and by Tick on ME2; ME3 keeps the staged opt-in rules
// (off until `mevr melee requests on` and `mevr melee gestures on`: the owner's question 7, docs/trilogy-plan.md).
// B is never the game's melee on foot in VR while [Throwables] meleeButton = 0 (ME1's default since 2026-11-05; ME2 and ME3
// since 2026-11-17): AdjustPad keeps it from the game; it is squad placement's (game/SquadPlace). Game thread.
namespace mevr::game::melee {

// The controller pulses the host offers (HostServices hapticPulse v9, hapticPulseHz v10), or null.
struct Haptics {
    void (*pulse)(int hand, float seconds, float amplitude) = nullptr;
    void (*pulseHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
};

// One tick of the physical rules (ME1: game/Throwables' tick with its body frame; ME2: Tick).
struct Physical {
    bool active = false;               // on foot in VR with tracked controllers and the player; false: the strokes start over
    std::optional<double> sampleTime;  // a new controller sample's time this tick (the strokes run on new samples only)
    ik::Vec3 head;                     // the head, tracking space
    double bodyYaw = 0.0;              // the body's front (the holsters' frame)
    std::array<bool, 2> suppressed{};  // the caller's reasons a hand doesn't strike (ME1: a grenade in it, or one just thrown)
    Haptics haptics;
    bool buttonFlags = false;          // the host's pad mapping can keep B from the game (HostServices v14)
};
void StepPhysical(Physical const&);
// Each tick, active or not: a strike's target watched for its damage (the hit's thump in the striking hand).
void WatchHit(double deltaSeconds, Haptics const&);

// What the host offers this tick (Mod.cpp's MeleeLink). ME2 takes its haptics and pad flags; ME3 its staged rules' context.
struct Link {
    bool enabled = false;          // ME3: requests on (`mevr melee requests on`); default inert
    bool gestures = false;         // ME3: gestures on (`mevr melee gestures on`), only after native command acceptance
    bool focused = false;
    bool menuOpen = true;
    bool authoredSeated = true;    // authored scene, NOT physically seated play calibration
    // ME3: re-reads the host's CURRENT menu/hand ownership at the instant of dispatch. A missing callback refuses dispatch,
    // including diagnostic requests.
    bool (*allowRequest)(int hand) = nullptr;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    void (*hapticHz)(int hand, float seconds, float amplitude, float frequencyHz) = nullptr;
    bool buttonFlags = false;      // the host's pad mapping knows gamepad::kFlagNoMeleeButton (v14)
    ik::melee::Input input;        // ME3: the staged rules' hands
};

// Finds the game's functions (each game's own). False until the script packages have loaded.
bool TryStartup();
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// The sequels' gate of the physical controls on foot (the strokes, the held chest and receiver of game/Throwables and
// game/Vent): [Throwables] enabled, our reflection layer up, game mode 0 with a pawn, a head-tracked view, active controllers
// (not calibrating) and a view anchor. False with the reason in `why`. Game thread.
bool PhysicalControlsOnFoot(std::string& why);
#endif
// Each tick after game/Throwables: ME2 feeds the physical rules itself; ME3 runs its staged rules; ME1's strokes come through
// game/Throwables (StepPhysical), so here it does nothing.
void Tick(double deltaSeconds, Link const&);
// B kept from the game on foot in VR ([Throwables] enabled, meleeButton = 0): every game. Called by game/support_grip before it
// publishes the pad flags (after Tick and game/Throwables' tick).
void AdjustPad(gamepad::Options& options);
// B reaches the game as its own button on foot now (AdjustPad didn't keep it, or the host can't): squad placement stands down
// while it does. As of the last AdjustPad.
bool BIsGameButton();
// ME3: a native request is awaiting admission or an admitted melee still owns the action slot.
bool Busy();
// ME3's staged diagnostics: a light or heavy request now, from `hand`, through Strike's rules (a fresh Link: no retained
// menu snapshot or queued command). Reports "dispatched" separately from observing an action; never impact or damage.
std::string Request(ik::melee::Intent, int hand, Link const&);
#if defined(MEVR_GAME_LE3)
// ME3's omni-blade with the weapon away (docs/trilogy-plan.md "Answered 2026-11-18, later", option A): the right hand's fist
// (grip and trigger held, nothing else) with no weapon in hand. Mod.cpp's hand availability counts it as released controls,
// so a fist stroke reaches Strike, whose DispatchFist starts the class's heavy melee action itself.
bool FistAway(int hand);
// `mevr melee punch` on ME3: the right fist's strike now through the same rules (the weapon must be away: `mevr weapon away`).
std::string Punch(Link const&);
#endif
// `mevr melee punch|strike` (tests, no tracked controllers needed): the right hand's strike now, through Strike's rules
// (punch: a fist; any direction, the enemy within meleeReachM). Every game. Game thread.
std::string Console(std::string_view args);
// For the snapshot's mod status (game/Throwables' "melee" object on ME1) and the heartbeat.
std::string StatusJson();
std::string Status();
void LogStats();
void Shutdown();

} // namespace mevr::game::melee
