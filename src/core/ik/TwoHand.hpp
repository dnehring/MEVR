#pragma once

#include "core/ik/Rigid.hpp"

#include <optional>

// A gun held with both hands (the support grip, game/support_grip): where the off hand goes on the
// gun and how the gun turns when it does. Positions in the space of the caller's choice (component
// space in the writer); "in hand" means the rear (right) hand bone's own axes. Engine-agnostic;
// tested offline.
//
// TwoHandRotation is adapted from Overlord (https://github.com/Aeka0/Overlord, GPL-3.0, Aeka0),
// src/client/component/vr/gameplay/weapon_profile.hpp `aimed_rotation` (swing the authored
// grip-to-grip vector onto the hands' vector, keeping the rear hand's roll) and grip_presenter.hpp
// (hands too close together define no axis).
namespace mevr::ik {

// Spherical interpolation from `a` (t = 0) to `b` (t = 1), the short way round.
Quat Slerp(Quat a, Quat b, double t);

// The rear hand's rotation while the other hand holds the gun: `rear` turned by the shortest
// rotation that takes the off hand's place on the gun (`spanInHand`, in the rear hand bone's axes,
// so `Rotate(rear, spanInHand)` now) onto the direction from `rearPosition` to `supportPosition`.
// The barrel then points along the line between the hands, and the roll about it stays the rear
// hand's (no world-up singularity). Nullopt (keep `rear`) when the hands are closer together than
// `minDistance`, the span is (nearly) zero, or anything isn't finite.
std::optional<Quat> TwoHandRotation(Quat rear, Vec3 spanInHand, Vec3 rearPosition, Vec3 supportPosition,
    double minDistance);

// A candidate off-hand point on the gun, in the rear hand bone's axes (units): plausible if it
// isn't on the rear hand itself (`kMinSpan`), lies between `kBehindGrip` behind the hand and
// `kPastMuzzle` past the muzzle along the barrel, and within `kOffBarrel` of the barrel's line.
// `barrelInHand` (any length) and `muzzleInHand` come from game/weapon's grip measurement.
inline constexpr double kForegripMinSpan = 4.0;
inline constexpr double kForegripBehindGrip = 20.0;
inline constexpr double kForegripPastMuzzle = 5.0;
inline constexpr double kForegripOffBarrel = 20.0;
bool OnGun(Vec3 point, Vec3 barrelInHand, Vec3 muzzleInHand);

// The gun at the shoulder (aimed fire, with both hands on it): the rear hand's grip close under
// the eyes, near the cheek and shoulder, in front of the face but not held out. Meters, in any
// space where `up` is up: `drop` below the eyes, `side` off the head's centre, `forward` along the
// head's level facing. Once inside, each bound widens by `margin` (hysteresis).
struct ShoulderZone {
    double minDrop = 0.05;
    double maxDrop = 0.35;
    double maxSide = 0.30;
    double minForward = -0.15;
    double maxForward = 0.40;
    double margin = 0.05;
};
bool Shouldered(Vec3 head, Vec3 headForward, Vec3 up, Vec3 grip, bool wasShouldered, ShoulderZone const& zone = {});

// Along the barrel from the rear hand, and how far off the barrel's line (units), for logs and for
// deciding whether two hands define the gun's axis.
struct GunOffsets {
    double along = 0.0;
    double off = 0.0;
};
GunOffsets OffsetsOnGun(Vec3 point, Vec3 barrelInHand, Vec3 muzzleInHand);

// Where the off hand cups a gun with no foregrip (pistols): under the rear hand's grip, `down`
// units below it (against the gun's up, the thumb side) and `forward` units along the barrel.
Vec3 UnderGrip(Vec3 barrelInHand, Vec3 upInHand, double down = 8.0, double forward = 2.0);

// The support grip's grab zone (2026-10-22: a grab anywhere sensible on the gun takes it): a capsule
// along the gun's bore line, from `frontUnits` ahead of the rear hand (along the barrel, from the
// hand bone) to `pastUnits` past the muzzle, plus a ball around the foregrip itself, both of the
// grab radius. Points in the caller's space (the writer: component space).
struct SupportZone {
    Vec3 start;     // the capsule's axis: the bore line from just ahead of the rear hand...
    Vec3 end;       // ...to a little past the muzzle
    Vec3 foregrip;  // where the off hand goes on the gun
};
// From the rear hand bone (`rearPosition`, `rearRotation`) and the gun in its axes (`barrelInHand`,
// `muzzleInHand`, `foregripInHand`: unscaled, so `handScale` multiplies them, as the writer scales the
// span). The start never passes the muzzle. Nullopt if anything isn't finite or there's no barrel.
std::optional<SupportZone> MakeSupportZone(Vec3 rearPosition, Quat rearRotation, Vec3 barrelInHand, Vec3 muzzleInHand,
    Vec3 foregripInHand, double handScale, double frontUnits, double pastUnits);
// The distance from `point` to the segment a-b (a point segment if they coincide).
double DistanceToSegment(Vec3 point, Vec3 a, Vec3 b);
// How far `point` is from the zone: the nearer of the capsule's axis and the foregrip (the grab
// radius is the caller's: within it, the grip takes the gun).
double DistanceToZone(Vec3 point, SupportZone const& zone);

// The span two-hand aim swings onto the hands' line, captured when a hold begins (in the rear hand's
// axes): the direction from the rear hand to where the off hand took hold, `length` long. So taking
// hold anywhere in the zone doesn't swing the gun, and from then on the barrel turns with the line
// between the hands. Nullopt (use the foregrip's own span) when the hands are closer than
// `minDistance` or anything isn't finite.
std::optional<Vec3> CapturedSpan(Quat rear, Vec3 rearPosition, Vec3 supportPosition, double length, double minDistance);

// The AnimTree's own off-hand IK (LE1: the SkelControlLimb `LeftArm`, LeftWrist -> LHIK;
// research/18) by its ControlStrength: off at or below 0.01 (the engine skips it below 1e-5), on
// at or above 0.99, blending in between; Unknown if the strength isn't finite.
enum class NativeIk { Unknown, Off, Blending, On };
NativeIk ClassifyNativeIk(double strength);
char const* Name(NativeIk state);

} // namespace mevr::ik
