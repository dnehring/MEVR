// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"

#include <optional>

// Arm IK's goals across a root-motion step (research/le2-vrik.md "Arm IK across root-motion steps"). The mod computes the
// goals in its tick, in the mesh's component space then; the next UpdateSkelPose writes them. ME2's vault moves the pawn
// 1.1-1.5 m in one frame while the engine moves the skeleton back by as much (the body and the camera, hung on the `Camera`
// bone, stay where they were), so goals left in component space rode the step: the arms and the gun drawn 0.4-1.1 m off for a
// frame, three times a vault. A step is told from the camera's hook bone (the anchor): it stands still in component space
// from frame to frame except when the engine moves the skeleton under a moving actor. Engine-agnostic; tested offline.
namespace mevr::ik {

enum class StepRule {
    Off,     // the goals ride the component (as before 2026-11-15)
    Camera,  // the component and the anchor both moved past `units` (the engine moved the skeleton back under a stepping
             // pawn): the goals follow the anchor's move in component space, so they stay with the camera
    World,   // past `units` of the component's move in the world, the goals stay where they were in the world
};

struct StepInput {
    StepRule rule = StepRule::Camera;
    double units = 20.0;           // a step: more than this between the goals and the write
    Affine componentThen;          // the mesh's LocalToWorld when the goals were computed
    Affine componentNow;           // ...and at the write
    bool anchorKnown = false;      // the anchor bone is the same bone in both poses
    Vec3 anchorThen;               // its component-space translation in the goals' pose
    Vec3 anchorNow;                // ...in the pose being written
};

struct Step {
    bool seen = false;        // the anchor or the component moved past `units`
    double componentUnits = 0.0;
    double anchorUnits = 0.0;
    Vec3 componentMove;       // world
    Vec3 anchorMove;          // component space
    // Component now <- the goals' frame: solve in the goals' frame (each bone composed with the inverse) and compose the written
    // bones with this. Nullopt: write the goals as published (no step, the rule off, or the frames unusable).
    std::optional<Transform> toNow;
};

Step FindStep(StepInput const& in);

// An affine with a uniform positive scale as a bone-style transform; nullopt if it shears, mirrors or degenerates.
std::optional<Transform> TransformOfAffine(Affine const& a);

} // namespace mevr::ik
