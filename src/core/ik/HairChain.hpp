// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/HandContact.hpp"
#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"

#include <span>
#include <string>
#include <vector>

// Hair on simulated bone chains (docs/trilogy-hair-plan.md section 4, research/trilogy-hair.md; the owner 2026-11-19: hair
// movement on by default, Miranda first, hands and our own VRIK arms move it). A hairstyle's hair bones (by name) are chains
// hanging from a bone the animation poses (the head); each chain is a row of points, the bones' origins plus a tip past the
// last bone, simulated in world space in metres with position-based dynamics (Verlet): gravity, the head's motion felt as
// inertia, damping, a pull back toward the animated pose, a bend limit per joint, no stretch, and collision with capsules
// (the person's own head, neck and shoulders; the player's hands and arms). The result is each bone turned so it points at
// its simulated child, the bones' lengths the mesh's. Fixed substeps of 1/120 s, so every frame rate moves the same.
// Engine-agnostic; tested offline (tests/HairChainTests.cpp). game/Hair feeds it and engine/BonePose writes the result.
namespace mevr::ik::hair {

struct Settings {
    double gravity = 0.0;        // m/s2, along `down`, on top of the authored pose (which already hangs as gravity has it)
    Vec3 down{0.0, 0.0, -1.0};   // world down (UE3: -Z)
    double stiffness = 0.06;     // 0..1: the share of the way back to the animated pose per 1/60 s
    double damping = 0.10;       // 0..1: the share of the velocity lost per 1/60 s
    double inertia = 1.0;        // 0..1: how much of the head's own motion the hair feels (0: it rides the head rigidly)
    double maxBendDeg = 50.0;    // per joint, from the animated bend
    double radiusM = 0.012;      // each point's collision sphere
    Vec3 windMS;                 // a steady wind, m/s (its force grows with the hair's speed through the air)
    double windDrag = 0.6;       // 1/s: how fast the hair takes the wind's speed
    // How far a chain's lower joints rest with the upper body instead of the head (tools/rig's chains hang from the head:
    // with the head turned, hair lying on the shoulders should stay on them, as the games' own rigs hang their lower hair
    // bones from the neck and collars). Bone i of n: its animated target blends from the head-carried pose toward the pose
    // the chain's body bone (Chain::body) carries, by followBody * i / (n - 1). 0: head-rigid targets (single-bone chains
    // always are).
    double followBody = 0.0;
};

// One chain of a skeleton: bones[0] hangs from `anchor` (a bone the animation poses), each next bone is the previous one's
// child. Made by Discover; the simulation state is filled by Step.
struct Chain {
    int anchor = -1;
    std::vector<int> bones;
    // The simulation (world space, metres): bones.size() + 1 points (each bone's origin, then the tip).
    std::vector<Vec3> pos;
    std::vector<Vec3> prev;
    std::vector<Vec3> target;      // this tick's animated points
    std::vector<Vec3> lastTarget;  // the last tick's
    Transform lastAnchor;
    bool started = false;
    double carry = 0.0;            // simulated time not yet stepped (s)
    int resets = 0;
    // The body's surface (Input::surface) at rest: each point's distance from each sphere's centre when the chain was last
    // reset (point i, sphere k at i * spheres + k), the hair lying on the body as authored. While the body moves, a point is
    // never let deeper into a sphere than that (its animated place, carried by the head, swings into the shoulders on a walk).
    std::vector<double> surfaceRest;
    int body = -1;                          // the upper-body bone lower joints follow (Settings::followBody); Discover sets it
    std::vector<Transform> followRest;      // each bone relative to `body` (metres): the bind pose's, set by the caller; else
                                            // recorded at the first step
};

// The chains in a skeleton (names and parents as its RefSkeleton has them): every bone whose name contains "hair" (any
// case) is a hair bone; a chain starts at a hair bone whose parent isn't one and goes on through the hair child (the first
// by index if several: the others start chains of their own, hanging from that bone's ANIMATED pose). Chains come out
// parents first.
std::vector<Chain> Discover(std::span<std::string const> names, std::span<int const> parents);

struct Input {
    // Every bone of the skeleton, animated, in world space, metres (the engine's pose: component space times the
    // component's transform, divided by units per metre). Only the anchor and the chain's bones are read.
    std::span<Transform const> animated;
    // The person's own body (world space, metres): the hair is kept out of it, but never further than its animated point
    // sits (hair at rest lies on the scalp, inside the skull's capsule; that rest stays reachable).
    std::span<hand_contact::Capsule const> body;
    // The body's surface fitted from its mesh (ik::body_spheres, spheres as capsules with both ends at the centre, the same
    // list every step): the hair kept out of each sphere to its whole radius, or as far as the point sat from it at the chain's
    // last reset if less (Chain::surfaceRest), on its animated point's side.
    std::span<hand_contact::Capsule const> surface;
    // Anything else (the player's hands and arms): the hair is kept out by the whole radius.
    std::span<hand_contact::Capsule const> colliders;
    double dt = 0.0;    // seconds since the last Step of this chain (0: paused, nothing moves)
    bool reset = false; // put the chain back on the animated pose, at rest
};

struct StepStats {
    bool reset = false;  // this step reset the chain (asked, a jump, a value not finite)
    int substeps = 0;
    int collisions = 0;  // point-capsule pushes over the substeps
};
StepStats Step(Chain& chain, Settings const& settings, Input const& input);

// The chain's bones as world transforms: each bone's animated rotation turned so the bone points at its simulated child,
// its origin the simulated point (the first bone's is the animated one). `out` gets one per chain bone, in chain order.
// Empty before the first Step.
void Pose(Chain const& chain, std::span<Transform const> animated, std::vector<Transform>& out);

// The tip's distance from where the animation has it (m): what the trace and the tests measure.
// Point i as drawn: the simulation (which stands at its last fixed step's time) moved on to this frame's time by the
// animation's own motion since. What Pose writes and TipOffset measures.
Vec3 Shown(Chain const& chain, std::size_t i);
double TipOffset(Chain const& chain);

} // namespace mevr::ik::hair
