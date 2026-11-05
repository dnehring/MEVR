// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Ik.hpp"
#include "core/ik/Throwing.hpp"

#include <string>
#include <string_view>
#include <vector>

// The physical vent (research/45): with a weapon drawn, the LEFT hand squeezes its grip on the gun's
// receiver (the top and rear of the gun: behind the foregrip, over and just ahead of the right hand) and
// pulls back and away in one fast stroke, letting go, like racking a slide. That ejects the gun's heat.
// Pure classification, testable offline; game/vent ties it to ME1's weapon heat.
//
// Everything here is in the GUN'S frame, meters of left-controller motion: `along` the barrel from the
// right hand (+ toward the muzzle), `up` above the bore (the gun's top), `side` beside it. The frame moves
// with the right hand, so only the left hand's motion RELATIVE to the gun counts: both hands swinging
// together is no rack. The writer measures it each pose (engine/bone_pose SupportSample).
//
// Conventions shared with the grenades (core/ik/Throwing, research/20): the release velocity is the
// fastest moment of the last ~120 ms; the analog grip lets go early during a fast motion; a slow release
// does nothing. New here: a squeeze held longer than `maxHoldSeconds` lapses (it's a grip, not a rack),
// and the vent has a cooldown and needs heat worth venting.
namespace mevr::ik::vent {

struct OnGun {
    double along = 0.0;
    double up = 0.0;
    double side = 0.0;
};
inline Vec3 AsVec(OnGun const& p) {
    return {p.along, p.up, p.side};
}

// The receiver zone ([Vent] zone*): a box in the gun's frame around the right hand. Its front overlaps the
// support grip's capsule (which starts supportFrontM = 0.10 ahead of the right hand with an 0.18 radius):
// while the left hand is in this box and nothing holds the support grip, the receiver wins (game/support_grip
// hides its zone from the pad mapping). The bottom is shallow so a hand cupped under a pistol's grip (the
// support grip's own place there) stays the support grip.
struct Zone {
    double backMeters = 0.06;   // behind the right hand
    double frontMeters = 0.14;  // ahead of it
    double aboveMeters = 0.14;  // above the bore
    double belowMeters = 0.03;  // below it
    double sideMeters = 0.09;   // either side
};
// `margin` widens every face (hysteresis for a hand that's already in).
bool InZone(OnGun const& p, Zone const& zone, double margin = 0.0);

struct Params {
    double minSpeed = 1.0;        // m/s at the release (the gun's frame)
    double minTravel = 0.05;      // m pulled back along the gun since the squeeze
    double backFraction = 0.3;    // this much of the release speed must point back along the gun
    double maxHoldSeconds = 1.5;  // a squeeze held longer is a grip, not a rack: nothing on its release
    double cooldownSeconds = 4.0; // between vents
    double minHeat = 0.3;         // the gun's heat (fraction of the overheat) under which there's nothing to vent
    throwing::GripParams grip;
};

struct Input {
    double now = 0.0;
    bool onGunValid = false;  // the gun's frame is measured this step (a gun in the right hand, arm IK on)
    OnGun onGun;
    bool newSqueeze = false;  // a new left squeeze this step that nothing else claimed (grenade slot, support grip)
    float squeeze = 0.0f;     // analog grip
    double heat = -1.0;       // the gun's heat as a fraction of the overheat (negative: unknown)
    bool canVent = true;      // the game allows it now (on foot, the weapon out, not switching); else a squeeze is refused
};

enum class Event {
    None,
    Grabbed,  // the left hand took the receiver
    Vented,   // let go in a fast stroke back and away: the heat goes
    Refused,  // a squeeze at the receiver, but no vent now (`why`)
    Slow,     // let go without a rack (`why`): nothing
    Lapsed,   // held too long: nothing on this squeeze
    Lost,     // the gun's frame went away while held
};
char const* Name(Event event);

struct Step {
    Event event = Event::None;
    throwing::Release release;  // at a release: the hand's velocity in the gun's frame
    double back = 0.0;          // its component back along the gun (m/s)
    double travel = 0.0;        // how far back the hand came since the squeeze (m)
    char const* why = nullptr;  // Refused, Slow, Lapsed, Lost
    bool entered = false;       // the zone, this step
    bool left = false;
};

class Machine {
public:
    Step Update(Input const& input, Zone const& zone, Params const& params);
    bool Holding() const { return holding_; }
    bool InZone() const { return inZone_; }
    double HoldSeconds(double now) const { return holding_ ? now - heldSince_ : 0.0; }
    double CooldownLeft(double now) const { return readyAt_ > now ? readyAt_ - now : 0.0; }
    double HandSpeed(double now) const;  // m/s in the gun's frame over the last 40 ms (0: unknown)
    // While holding: how far the hand has come back along the gun since it took the receiver (m; negative: forward).
    // 0 when not holding. What the visible rack follows (Rack below).
    double Travel() const { return holding_ ? start_.along - last_.along : 0.0; }
    unsigned Vents() const { return vents_; }
    // Forgets the hand (the frame went away); keeps the cooldown.
    void Reset();
    void ResetAll();

private:
    throwing::MotionHistory history_;
    bool inZone_ = false;
    bool holding_ = false;
    bool lapsed_ = false;  // this squeeze lapsed: nothing until the grip opens
    double heldSince_ = 0.0;
    OnGun start_;
    OnGun last_;  // the latest valid place
    throwing::GripLatch grip_;
    double readyAt_ = 0.0;
    unsigned vents_ = 0;
};

// Why a vent can't start now (nullptr: it can): no heat worth venting, the cooldown.
char const* Refusal(double heat, double cooldownLeft, Params const& params);

// Haptics (`strength` 0.5 as designed; both hands get the vent).
throwing::Buzz EdgeTick(double strength);   // the hand enters the receiver zone of a hot gun
throwing::Buzz VentPulse(double strength);  // the heat leaves

// ---- The visible rack (2026-11-04, research/45 section 6) ----------------------------------------------------------
// While the left hand holds the vent grip on a hot gun (Machine::Holding), one part of the gun's own model (its slide,
// heat sink or receiver cover: a bone and every bone below it) follows the hand's pull back along the gun, clamped to
// that part's travel. When the grip lets go, vented or not, it snaps forward on a fast spring and stops at its rest
// place, like a slide slamming home (never past it). game/vent moves the bone through engine/bone_pose's weapon-part
// stage; this is the math.

// The hand's pull back along the gun (Machine::Travel, meters of left-controller motion) as the part's offset back
// along the gun in the model's units: `unitsPerMeter` model units per meter (the view's world units per meter over the
// gun mesh's scale), clamped to 0..maxUnits. Nonsense in, 0 out.
double RackTarget(double travelMeters, double unitsPerMeter, double maxUnits);

struct RackParams {
    double maxUnits = 3.0;  // the part's travel (model units: about cm at the life-size 100 units per meter)
    double returnHz = 9.0;  // the forward spring's natural frequency
    double damping = 0.5;   // its damping ratio (it stops at the rest place either way)
};

enum class RackEvent {
    None,
    Released,  // the hold ended this step: the spring takes the part forward
    Home,      // the part is back at its rest place after a release
};

class Rack {
public:
    // One step of `dt` seconds (clamped to 0..0.1). While `holding` the part follows `target` (model units back along
    // the gun, clamped to 0..maxUnits); otherwise a spring takes it forward to 0, where it stops.
    RackEvent Update(double dt, bool holding, double target, RackParams const& params);
    double Offset() const { return x_; }  // model units back from the rest place, 0..maxUnits
    double Velocity() const { return v_; }
    bool AtRest() const { return !holding_ && x_ <= 0.0; }
    bool Holding() const { return holding_; }
    double Peak() const { return peak_; }                    // the deepest offset of the current or last hold
    double ReturnSeconds() const { return returnSeconds_; }  // how long the last return took (at Home)
    void Reset();

private:
    double x_ = 0.0;
    double v_ = 0.0;
    bool holding_ = false;
    bool returning_ = false;
    double peak_ = 0.0;
    double returnSeconds_ = 0.0;
};

// Which bone of a gun's model is the part the rack moves. `setting` is [Vent] <type>Part: "auto" (AutoPartList for the
// weapon type, then the first bone named for a moving part: slide, bolt, cover, heat sink, vent, receiver ...; never one
// named for a sight or a grip), "none", or bone names separated by commas or spaces (the first the model has wins).
// Never the root, a protected bone (game/vent: those a socket hangs on: the muzzle, the tracer, the cool-down effect's
// place) or a bone with a protected bone below it, nor a part of more than kMaxPartBones bones. `weaponType` 0 pistol,
// 1 shotgun, 2 assault rifle, 3 sniper rifle (others: auto has no list).
inline constexpr int kMaxPartBones = 32;
struct PartChoice {
    int bone = -1;           // -1: none (the rack stands down for this model)
    std::vector<int> bones;  // the part: `bone` and every bone below it, parents first
    double maxUnits = 0.0;   // its travel (PartTravel)
    char const* source = ""; // how it was found: "the setting", "the type's list", "its name"
    std::string why;         // none: why
};
PartChoice ChoosePart(std::vector<std::string> const& names, std::vector<int> const& parents, std::vector<int> const& protectedBones,
    std::string_view setting, int weaponType);
// The type's own candidates, in order (research/21's bone lists; research/45 section 6 says why each).
char const* AutoPartList(int weaponType);
// How far the part may go back (model units): the known parts' own, else the type's.
double PartTravel(std::string_view bone, int weaponType);
// `bone` and every bone below it, parents first (bones are stored parents first, as a RefSkeleton is).
std::vector<int> Subtree(std::vector<int> const& parents, int bone);

} // namespace mevr::ik::vent
