// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"

#include <optional>

// A shot's visual kick of the gun in the hand (research/24 section 8, game/gun_kick): each of the player's shots gives the
// held gun a short push, muzzle up and back, that springs back within about a tenth of a second; automatic fire stacks the
// pushes under a soft cap. Visual only: the writer (engine/BonePose) turns the right hand's goal by it after the support grip
// tied the hands, and game/weapon takes the kick back out of everything it reads of the drawn gun (`Written`), so the aim ray,
// the shots, ADS and the sight assist see the gun where the controller holds it. Engine-agnostic; tested offline.
namespace mevr::ik::gun_kick {

struct Tuning {
    // A shot's kick from the game's recoil for it (ABioWeaponRanged::GetRecoil, degrees of view pitch a shot: an assault rifle
    // 0.1-0.2, a shotgun or a sniper rifle 2-5, research/24 section 2): strength = recoil / fullRecoilDeg (0..1), the kick from
    // the min (any shot) to the max (a full-strength one).
    double fullRecoilDeg = 3.0;
    double minAngleDeg = 0.8;
    double maxAngleDeg = 4.0;
    double minBackM = 0.004;
    double maxBackM = 0.022;
    // The spring: critically damped, each shot a push of its velocity, so the kick rises from zero, peaks 1/omega after the
    // shot (22 ms) and is back to a tenth of its peak about 5.3/omega after it (118 ms).
    double omega = 45.0;
    // Stacked kicks (automatic fire) are soft-capped: as they are up to `knee` of the cap, then easing into it, never past.
    double capAngleDeg = 6.0;
    double capBackM = 0.03;
    double knee = 0.6;
    // Held with both hands (the support grip's blend 1), the kick is this share of the one-handed one.
    double twoHanded = 0.5;
};

// [Hands] gunKickScale's range.
inline constexpr double kMaxScale = 2.0;

struct Offset {
    double angleDeg = 0.0;  // the muzzle up, about the gun's right axis through the hand
    double backM = 0.0;     // the gun back along its barrel, meters of the player's world
};

// A shot's peak kick for the game's recoil (degrees; non-finite or negative counts as 0: the min kick) times the scale (0..2).
Offset ShotKick(double recoilDeg, double scale, Tuning const& tuning = {});

// `x` up to knee * cap, then easing toward `cap` (continuous, with a continuous slope; never past it). Negative mirrors.
double SoftCap(double x, double cap, double knee);

// The kick's spring, one channel each for the angle and the back motion.
class Spring {
public:
    // A shot: from rest the kick then peaks at `peak` (superposed on whatever is still moving).
    void Kick(Offset peak, Tuning const& tuning = {});
    // `dt` seconds on (the exact solution: stable for any step; a non-finite or negative step does nothing).
    void Step(double dt, Tuning const& tuning = {});
    Offset Value(Tuning const& tuning = {}) const;  // capped
    Offset Raw() const;                             // before the cap
    // Anything left (above a thousandth of a degree or a hundredth of a millimetre, or moving).
    bool Moving() const;
    void Reset();

private:
    double x_[2]{};  // 0: degrees, 1: meters
    double v_[2]{};
};

// The kick as drawn: times the two-handed share (by the support grip's blend, 0..1), and its back motion faded out as the gun
// comes up to the eye (`raisedWeight`, 0..1: the sights' approach weight; a scope pulled to the eye isn't pushed into it).
Offset Shaped(Offset kick, double twoHandBlend, double raisedWeight, Tuning const& tuning = {});

// The kick as a rigid motion of the right hand, in the caller's space (component space in the writer): about the hand, a turn of
// `angle` radians about `axis` (unit), then a move by `translation`.
struct Motion {
    Vec3 axis{0.0, 1.0, 0.0};
    double angle = 0.0;
    Vec3 translation;
    Quat Rotation() const;
};
// For a hand at `handRotation` holding a gun whose barrel and top are `barrelInHand` and `upInHand` in the hand's axes: the muzzle
// turned `angleRadians` toward the gun's top, and the gun moved `backUnits` back along its (unkicked) barrel, so the line the
// shots take keeps its direction under the move. Nullopt if the barrel and the top don't define a plane or anything isn't finite.
std::optional<Motion> HandMotion(Quat handRotation, Vec3 barrelInHand, Vec3 upInHand, double angleRadians, double backUnits);
// The kicked hand goal: its position moves by the translation (the turn is about the hand itself), its rotation turns.
void ApplyToHand(Motion const& motion, Vec3& wrist, Quat& rotation);
// Something held on the gun (the left hand on the foregrip) carried `blend` (0..1) of the motion about the right hand at
// `pivot`; its rotation too unless null.
void ApplyToHeld(Motion const& motion, Vec3 pivot, double blend, Vec3& wrist, Quat* rotation);

// What a write left in the drawn pose: the turn about where the right hand bone was written (`pivot`), then the move. Readers of
// the drawn hand or gun take a point or direction back to where the controller holds it with Unkick, and the other way with
// Rekick (an un-kicked point where the drawn model shows it). Exact for the rotation; a hand short of its goal (out of reach)
// keeps part of the move, which then shifts the un-kicked reading along the barrel's own line.
struct Written {
    Quat rotation;
    Vec3 pivot;
    Vec3 translation;
};
Written ToWritten(Motion const& motion, Vec3 writtenHand);
Vec3 UnkickPoint(Written const& w, Vec3 p);
Vec3 UnkickDirection(Written const& w, Vec3 d);
Vec3 RekickPoint(Written const& w, Vec3 p);
Vec3 RekickDirection(Written const& w, Vec3 d);
// The turn in degrees (0..180) and the move's length, for logs.
double AngleDeg(Written const& w);
double MoveLength(Written const& w);

} // namespace mevr::ik::gun_kick
