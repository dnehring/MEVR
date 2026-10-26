// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"

#include <array>
#include <optional>
#include <string_view>

// Body holsters (research/19): weapon slots on the player's body, reached with the right hand.
// Pure geometry and the grab/holster state machine, in OpenXR LOCAL space (meters; +X right, +Y
// up, -Z forward), so they're testable offline; game/holsters ties them to the game.
//
// Conventions from the VR shooters surveyed in research/19 (our own code): slots hang off a body
// yaw that lags the head and is held while a hand is in a slot; the anchor is the eye as it would
// be with the head upright over its neck pivot (looking down moves nothing), its height smoothed so
// crouching is followed but jitter isn't; offsets scale with the player's eye height; zones are
// spheres with exit hysteresis, ties broken by distance over radius (as Overlord's
// weapon_holsters.hpp does); only a new squeeze inside a zone acts.
namespace mevr::ik::body_slots {

inline constexpr int kSlots = 4;
// The eye height the default offsets are written for (meters, standing).
inline constexpr double kReferenceEyeHeight = 1.65;

// The game's ranged weapon types (LE1 EBioItemWeaponRangedType = ESwitchToWeapon = quick slot).
enum class Weapon : int { None = -1, Pistol = 0, Shotgun = 1, AssaultRifle = 2, Sniper = 3 };
char const* Name(Weapon weapon);
// "pistol", "shotgun", "rifle" / "assault" / "assaultrifle", "sniper", "none" / "off"
// (case-insensitive); nullopt for anything else.
std::optional<Weapon> ParseWeapon(std::string_view text);

struct Slot {
    char const* name = "";
    Weapon weapon = Weapon::None;  // None: the slot is off
    // From the anchor, in the body frame, at the reference eye height (meters): x right, y up,
    // z forward (+ ahead of the eyes, - behind).
    Vec3 offset;
    double radius = 0.13;
};

struct Layout {
    std::array<Slot, kSlots> slots;
    double eyeHeight = kReferenceEyeHeight;  // the player's standing eye height: scales the offsets
    double exitMargin = 0.05;  // a hand leaves a slot this far past its radius; also the "near" margin
};
// The user's layout (research/19, "Default slots"): pistol at the right hip, assault rifle over the
// right shoulder, shotgun at the left hip (a cross-draw; the belly until 2026-10-25), sniper rifle over the left shoulder.
Layout DefaultLayout();
// Offsets scale by eyeHeight / kReferenceEyeHeight, clamped to a sane range.
double Scale(Layout const& layout);

// The eye as it would be with the head upright: the head turns about a neck pivot `neckBack`
// behind and `neckDown` below the eyes (head axes), and the eye is put back above that pivot at the
// head's yaw. So pitching or rolling the head doesn't move the anchor; turning it doesn't either.
Vec3 NeutralEye(Vec3 headPosition, Quat headOrientation, double neckBack = 0.08, double neckDown = 0.08);

// Unit forward and right of a body yaw (0 = -Z, + = turned left, as ik::BodyYaw).
Vec3 YawForward(double yaw);
Vec3 YawRight(double yaw);

struct Frame {
    Vec3 anchor;      // NeutralEye, smoothed
    double yaw = 0.0;
};
Vec3 SlotCenter(Frame const& frame, Slot const& slot, double scale);

// The anchor's height follows with a time constant (crouching moves the slots, jitter doesn't);
// its horizontal position follows at once.
class Anchor {
public:
    explicit Anchor(double heightSeconds = 0.08) : heightSeconds_(heightSeconds) {}
    Vec3 Update(Vec3 neutralEye, double deltaSeconds);
    void Reset() { initialized_ = false; }

private:
    double heightSeconds_;
    double y_ = 0.0;
    bool initialized_ = false;
};

enum class Holster {
    Off = 0,
    OwnSlot = 1,         // at the held gun's own slot (squeeze and release; with gripHolds, let go there)
    OwnOrEmptySlot = 2,  // ... or at a slot whose weapon the player doesn't carry
};

struct Input {
    // The hand's distance from each slot's center (meters); negative or not finite: the slot is off.
    std::array<double, kSlots> distance{-1.0, -1.0, -1.0, -1.0};
    std::array<double, kSlots> radius{};
    std::array<Weapon, kSlots> weapon{Weapon::None, Weapon::None, Weapon::None, Weapon::None};
    std::array<bool, kSlots> available{};  // the player carries a weapon of the slot's type
    double exitMargin = 0.05;
    bool tracked = true;     // the hand's pose is valid (false: it's in no slot)
    bool gripDown = false;   // the grip, latched
    bool newSqueeze = false;  // a squeeze began since the last step (one the game didn't see as RB)
    bool trigger = false;
    Weapon held = Weapon::None;  // drawn now (None: holstered or unknown)
    // The game is switching weapons (BioPawn.IsSwitchingWeapons: a draw's animation, about 0.24 s at drawRate 6).
    // `held` turns on about 0.01 s into a draw, but the game ignores TryHolster until the switch is over, so a
    // let-go then is kept (Returning, no grace) and acted on once the switch has finished.
    bool switching = false;
    Holster holster = Holster::OwnSlot;
    // The grip holds the gun ([Holsters] gripHolds, research/19 "The right grip holds the gun"):
    // squeeze at a slot to draw, keep squeezing to carry, let go at the gun's own slot to holster it;
    // let go anywhere else and it goes back to its holster after `releaseGrace` seconds (negative:
    // never), unless squeezed again first, and never while the trigger is held.
    bool gripHolds = false;
    double releaseGrace = 0.3;
    double deltaSeconds = 0.0;  // since the last step (the grace's clock)
    // gripHolds: the squeeze held now holds the gun (the host kept it from the game), even if it began
    // while the machine wasn't running (a menu, the power wheel, a conversation): with a gun out, it's
    // taken as held, so letting go still puts the gun away.
    bool gripKept = false;
};

enum class Action {
    None,
    Draw,              // equip `weapon` (the slot's)
    Holster,           // put the held gun away
    Unavailable,       // squeezed at a slot whose weapon the player doesn't carry
    AlreadyHeld,       // squeezed at the held gun's slot (holstering off; with gripHolds: take it)
    HolsterArmed,      // squeezed at the held gun's slot: holsters on release there
    HolsterCancelled,  // ... but the hand left the slot, or the trigger was pulled
    Missed,            // squeezed near a slot, but not in one
    // gripHolds:
    ReturnArmed,       // let go away from the gun's slot: it goes back after the grace
    ReturnCancelled,   // ... squeezed again in time (or the gun changed or went away)
    Return,            // ... the grace is over: put the gun away (as Holster)
};
char const* Name(Action action);

enum class Phase {
    Idle,          // no squeeze in progress that we own
    Grab,          // a squeeze we acted on (or refused) is still held; with gripHolds, the grip held
    HolsterArmed,  // a squeeze at the held gun's slot: holsters if released there
    Returning,     // gripHolds: let go away from the gun's slot, the grace running
};

struct Step {
    int entered = -1;  // the slot the hand entered this step
    int left = -1;     // the slot it left
    bool nearby = false;  // within radius + margin of a slot that's on, or in one: new squeezes stay off RB
    Action action = Action::None;
    int slot = -1;  // the slot the action is about
    Weapon weapon = Weapon::None;
    // gripHolds: the grace is waiting for the trigger to be let go (a burst never ends in a holster).
    bool returnWaitsForTrigger = false;
    // gripHolds: ...or for the game's weapon switch (the draw's animation) to finish.
    bool returnWaitsForSwitch = false;
};

class Machine {
public:
    Step Update(Input const& input);
    int InSlot() const { return inSlot_; }
    Phase CurrentPhase() const { return phase_; }
    // The yaw should be held: the hand is in a slot or a squeeze at one is in progress (not the long
    // hold of a carried gun).
    bool Busy() const { return inSlot_ >= 0 || phase_ == Phase::HolsterArmed || (phase_ == Phase::Grab && !holds_); }
    // gripHolds: the seconds left before a let-go gun goes back (negative: none running).
    double ReturnLeft() const { return phase_ == Phase::Returning ? returnLeft_ : -1.0; }
    void Reset();

private:
    void UpdateHolding(Input const& input, Step& step);

    int inSlot_ = -1;
    Phase phase_ = Phase::Idle;
    int armedSlot_ = -1;
    Weapon armedWeapon_ = Weapon::None;
    bool holds_ = false;     // the last step's gripHolds
    // gripHolds: the weapon this squeeze drew (it may not be out yet when the grip opens), and the
    // gun a let-go is returning, with its grace and how long it has waited for a draw to finish.
    Weapon drawn_ = Weapon::None;
    Weapon returning_ = Weapon::None;
    double returnLeft_ = 0.0;
    double returnWaited_ = 0.0;
    bool returnAwaitsDraw_ = false;  // let go before the drawn gun was out
};
// gripHolds: a let-go draw still on its way is waited for this long before the return gives up.
inline constexpr double kReturnWaitForDrawSeconds = 2.0;

// ---- Contact haptics (research/19, "Haptics as contact") ----------------------------------------
// Like a physical holster the hand bumps into: a continuous buzz while the hand overlaps a slot,
// stronger and higher the deeper it is, a firmer edge when it crosses in, a crisp click on a draw or
// holster. `strength` is [Holsters] hapticAmplitude (0..1, 0.5 = as designed, 0 = none).
struct Buzz {
    float amplitude = 0.0f;   // 0..1
    float frequency = 0.0f;   // Hz; 0 = the runtime's choice (many runtimes ignore it anyway)
    float seconds = 0.0f;     // pulses only
};
// How deep the hand is in a slot: 1 at the center, 0 at the radius and beyond (the hysteresis band).
double Depth(double distance, double radius);
// The continuous contact buzz for a depth (0..1); `holding` a carried gun halves it (a gun hand
// resting near a slot shouldn't drone).
Buzz Contact(double depth, double strength, bool holding);
Buzz EnterEdge(double strength);   // crossing into a slot
Buzz DrawClick(double strength);   // the gun comes out (a grab's transient)
Buzz HolsterClick(double strength);  // it goes in (the release mirror: lower amplitude and frequency)
Buzz Refused(double strength);     // no weapon of that type

// ---- Standing eye height from the floor (research/19, "Eye height from the floor") -------------
// The scale of the slots comes from the player's standing eye height, not the current head height,
// so crouching doesn't shrink the layout (the anchor still follows the head down). Measured as the
// eye height above the floor (host: OpenXR STAGE/LOCAL_FLOOR): taken at a recenter (the player is
// told to stand straight for it), then a slow-rising maximum. Never lowered by crouching; reset for a
// new session.
class StandingHeight {
public:
    static constexpr double kMin = 0.9;   // a sample outside this isn't an eye above a floor
    static constexpr double kMax = 2.3;
    static constexpr double kRiseSeconds = 2.0;  // time constant toward a higher sample
    // One sample of the current eye height above the floor (meters).
    void Update(double eyeHeight, double deltaSeconds);
    // A recenter: the player stands straight, so this sample is the standing height (if plausible).
    void Recenter(double eyeHeight);
    void Reset() { value_ = 0.0; }
    bool Valid() const { return value_ > 0.0; }
    double Value() const { return value_; }

private:
    double value_ = 0.0;
};

} // namespace mevr::ik::body_slots
