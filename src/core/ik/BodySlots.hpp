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
// right shoulder, shotgun at the belly, sniper rifle over the left shoulder.
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
    OwnSlot = 1,         // squeeze and release at the held gun's own slot
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
    Holster holster = Holster::OwnSlot;
};

enum class Action {
    None,
    Draw,              // equip `weapon` (the slot's)
    Holster,           // put the held gun away
    Unavailable,       // squeezed at a slot whose weapon the player doesn't carry
    AlreadyHeld,       // squeezed at the held gun's slot with holstering off
    HolsterArmed,      // squeezed at the held gun's slot: holsters on release there
    HolsterCancelled,  // ... but the hand left the slot, or the trigger was pulled
    Missed,            // squeezed near a slot, but not in one
};
char const* Name(Action action);

enum class Phase {
    Idle,          // no squeeze in progress that we own
    Grab,          // a squeeze we acted on (or refused) is still held
    HolsterArmed,  // a squeeze at the held gun's slot: holsters if released there
};

struct Step {
    int entered = -1;  // the slot the hand entered this step
    int left = -1;     // the slot it left
    bool nearby = false;  // within radius + margin of a slot that's on, or in one: new squeezes stay off RB
    Action action = Action::None;
    int slot = -1;  // the slot the action is about
    Weapon weapon = Weapon::None;
};

class Machine {
public:
    Step Update(Input const& input);
    int InSlot() const { return inSlot_; }
    Phase CurrentPhase() const { return phase_; }
    // The yaw should be held: the hand is in a slot or a squeeze is in progress.
    bool Busy() const { return inSlot_ >= 0 || phase_ != Phase::Idle; }
    void Reset();

private:
    int inSlot_ = -1;
    Phase phase_ = Phase::Idle;
    int armedSlot_ = -1;
    Weapon armedWeapon_ = Weapon::None;
};

} // namespace mevr::ik::body_slots
