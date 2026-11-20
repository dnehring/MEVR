// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"

// Soft-tissue jiggle on a bone (research/trilogy-jiggle.md section 6): a per-axis damped spring on the bone's offset in a
// frame that follows its parent (the chest for a breast, the pelvis for a glute), kicked by the change of velocity of the
// tissue's own centre (so steady motion moves nothing, steps and landings do), solved in closed form (exact for any frame
// time: no substeps), soft-limited, reset on teleports. Zero offset is exactly the animated pose. Game-independent; the bone
// output is a small translation and rotation in the frame, applied to the bone by whoever poses it.
namespace mevr::ik::jiggle {

// The most an intensity may be: 1 is the shipped full; the club dancers' boost (core/ik/ClubBoost, [Hair] clubJiggle 2) goes
// above it. The springs' frequencies and damping never change, so a larger intensity is only a wider limit and a stronger kick.
inline constexpr double kMaxIntensity = 3.0;

struct Axis {
    double hz = 2.4;        // natural frequency
    double zeta = 0.25;     // damping ratio (0..1: under-damped)
    double maxM = 0.03;     // the soft limit at intensity 1, metres
    double inertia = 0.35;  // the share of the centre's velocity change the tissue feels, at intensity 1
};

// The frame's axes: x depth (out of the body), y lateral (right), z vertical (up), aligned with the body at rest.
struct Settings {
    Axis depth{3.5, 0.35, 0.01, 0.20};
    Axis lateral{2.2, 0.30, 0.02, 0.30};
    Axis vertical{2.4, 0.25, 0.03, 0.35};
    Vec3 centre{0.05, 0.0, 0.0};   // the tissue's centre from the pivot, in the frame (metres)
    double rotationShare = 0.6;    // of the lateral and vertical offset shown as a turn about the pivot (lever = |centre|)
    double smoothHalfLifeS = 0.02; // the centre's velocity smoothed
    double maxAccel = 35.0;        // m/s2 (about 3.5 g): a velocity change per step clamped to this
    double gravityShare = 0.3;     // the sag when the body leans away from its pose at the reset
    double intensity = 0.5;        // 0..kMaxIntensity: scales the limits and the inertia shares, never the frequencies or damping
};
Settings Breast();
Settings Glute();

struct State {
    Vec3 x;          // offset in the frame, metres
    Vec3 w;          // its velocity
    Vec3 previousCentre;
    Vec3 velocity;   // the centre's smoothed velocity
    Vec3 restGravity;  // gravity in the frame at the reset (the authored pose already sags as it should)
    bool started = false;
    bool primed = false;   // the centre's velocity known (the first step after a reset only measures it)
    int resets = 0;
    int clampHits = 0;
};

struct Input {
    Transform frame;            // world, metres: the frame's rotation (follows the parent bone) and the pivot's place
    double dt = 0.0;            // seconds since the last step; 0 holds
    bool reset = false;         // a cut, a teleport, a new pose source
    Vec3 gravity{0.0, 0.0, -9.81};
};

struct Output {
    Vec3 translation;  // in the frame, metres
    Quat rotation;     // in the frame, about the pivot
    Vec3 shown;        // the soft-limited offset
};

Output Step(State& state, Settings const& settings, Input const& input);

} // namespace mevr::ik::jiggle
