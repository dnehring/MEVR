// SPDX-License-Identifier: GPL-3.0-only
// Adapted from Overlord (https://github.com/Aeka0/Overlord, GPL-3.0, Aeka0): the ADS corridor, its smooth approach
// weight and the dwell/hysteresis latch of src/client/component/vr/gameplay/ads_alignment.hpp (`corridor`, `entry`,
// `exit`, `far_extension`, `measure`, `inside`, `approach`) and controller_ads.hpp (`ads_policy::consume`), and the
// per-optic eye-distance assistance of ads_comfort.hpp (`distance`, `distance_weight`, `measure_eye`'s relief clamp, the
// thermal transition's faster spring). Changed here: one engine-agnostic sample in tracking meters (OpenXR axes) instead
// of Overlord's muzzle frame in game units; dwell counted in seconds of ticks; an eye-to-sight reach added to the corridor
// (research/21 section 15: the picture only once the gun is up to the eye); our sight kinds mapped onto its optic classes.
#pragma once

#include <openxr/openxr.h>

// Aiming down the sights (research/21 section 15). The mod decides from the head and the gun as drawn WITHOUT the
// assist's own move (so the assist never feeds its own gate) whether the player aims down the sight (ADS): a scope's
// picture is shown and its lens part hidden only then, and the pull to the eye may then reach further. The continuous
// approach weight drives the alignment assist and the pull below and through ADS (vr::sights::AssistSpring). Pure,
// unit-tested in tests/Main.cpp.
namespace mevr::vr::ads {

// Overlord's corridor: the head (the centre eye) against the muzzle along the barrel (`depth`: behind the muzzle),
// beside the bore line (`lateral`, either side), above it (`height`: below negative), and the head's forward against the
// barrel (`facingDeg`). Exit wider than entry: hysteresis.
struct Corridor {
    float nearDepth;
    float farDepth;
    float lateral;
    float below;
    float above;
    float facingDeg;
};
inline constexpr Corridor kEntry{0.15f, 1.5f, 0.09f, -0.025f, 0.16f, 30.0f};
inline constexpr Corridor kExit{0.10f, 1.7f, 0.13f, -0.06f, 0.22f, 40.0f};
// Overlord's eye-to-sight reach: a long barrel must not use up the corridor's far depth.
inline constexpr float kSightReachM = 0.60f;
// Overlord's dwell (non-thermal optics): inside (or outside) the corridor this long before ADS turns on (off).
inline constexpr float kDwellS = 0.10f;

// Ours: the eye's reach to the sight (the centre eye to the sight's centre, meters). ADS needs the gun up to the eye:
// entry within `entryM`, exit past `exitM`. 0: no reach limit (Overlord's corridor alone).
struct Reach {
    float entryM = 0.40f;
    float exitM = 0.50f;
};

// The corridor's far depth is extended by however much the sight sits behind the muzzle plus the reach, past the far
// depth (Overlord's `far_extension`).
float FarExtension(float sightToMuzzleM);

struct Sample {
    float depth = 0.0f;          // the head behind the muzzle along the barrel (meters)
    float lateral = 0.0f;        // the head beside the bore line (meters, either side)
    float height = 0.0f;         // the head above the bore line, toward the gun's top (meters; below: negative)
    float facingDeg = 180.0f;    // the head's forward against the barrel (degrees)
    float sightToMuzzle = 0.0f;  // the sight's centre behind the muzzle along the barrel (meters)
    float eyeToSight = 0.0f;     // the head to the sight's centre (meters)
    float sightDepth = 0.0f;     // the sight ahead of the head along the barrel (meters)
    bool valid = false;
};
// `head` and `headForward` the head's centre (the eyes' midpoint) and its -Z; `muzzle`, `sight` (its centre), `barrel`
// (unit, toward the target) and `up` (the gun's top) the gun WITHOUT the assist's move; all in tracking space.
Sample Measure(XrVector3f const& head, XrVector3f const& headForward, XrVector3f const& muzzle, XrVector3f const& sight,
    XrVector3f const& barrel, XrVector3f const& up);

// Inside the corridor: entry limits while ADS is off, exit limits while it is on (Overlord's `inside`), and our reach.
bool Inside(Sample const& sample, bool active, Reach const& reach = {});

// 0 at t <= 0, 1 at t >= 1, the quintic smoothstep between.
float Smooth(float t);

// At the calibrated shoulder (research/21 section 20, 2026-11-06, the user: "something a bit more naive when it's on
// shoulder"): the right grip inside the shoulder zone around the guided calibration's stock point (research/25) and the
// head looking along the barrel (25 degrees to come on, 35 to stay). The calibration solved the gun frame from the eye's
// line to the target with the hand at that stock point, so there the eye is on the sight line by construction: the sight
// is on at once, whatever the eye's distance. `inZone` ik::Shouldered's answer; `was` the last answer.
inline constexpr float kShoulderFacingOnDeg = 25.0f;
inline constexpr float kShoulderFacingOffDeg = 35.0f;
bool AtShoulder(bool inZone, float facingDeg, bool was);

// Overlord's `approach`: 0..1, independent of the ADS latch. The angle between the head's forward and the barrel, full
// within 5 degrees and none at 45, times smooth edges between the entry and exit corridors (depth, lateral, height), so
// a gun held parallel at the hip or side gets nothing.
float Approach(Sample const& sample);

// Overlord's `ads_comfort::distance_weight`: by the eye's distance to the sight, full within 0.30 m and none at 0.60 m.
// `ads` (our ADS-on pull, Overlord's thermal profile): full within 0.50 m, none at 0.70 m.
float DistanceWeight(float eyeToSightM, bool ads);

// Overlord's optic classes and how far each slides toward the eye (`ads_comfort::distance`): red dots and iron sights
// none, holographic 5 cm, low power (ours: a scope under 3x) 10 cm, scopes 15 cm.
enum class Optic { Unchanged, Holographic, LowPower, Scope };
float PullDistance(Optic optic);
// How far the gun should slide toward the eye: the sight `sightDepth` ahead of the eye down to `relief`, at most `limit`,
// never away (Overlord's `measure_eye` clamp).
float Clearance(float sightDepth, float relief, float limit);

// The spring's profile (vr::sights::AssistSpring): Overlord's ADS transition (10/s, 0.25 m/s) and its thermal one (24/s,
// 0.9 m/s), ours for the larger pull while ADS is on.
struct SpringProfile {
    float frequency;
    float maxSpeed;
};
inline constexpr SpringProfile kNormalSpring{10.0f, 0.25f};
inline constexpr SpringProfile kFastSpring{24.0f, 0.9f};

// The ADS latch (Overlord's `ads_policy::consume`): `aligned` (Inside) for `dwell` seconds turns it on, not aligned for
// `dwell` seconds turns it off; a change of mind within the dwell restarts it.
class Policy {
public:
    bool Update(bool aligned, float dt, float dwell = kDwellS);
    bool Active() const { return active_; }
    bool Entering() const { return entering_; }
    bool Leaving() const { return leaving_; }
    float Held() const { return timer_; }  // seconds inside (entering) or outside (leaving)
    void Reset() { active_ = entering_ = leaving_ = false; timer_ = 0.0f; }

private:
    bool active_ = false;
    bool entering_ = false;
    bool leaving_ = false;
    float timer_ = 0.0f;
};

} // namespace mevr::vr::ads
