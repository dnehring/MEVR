// SPDX-License-Identifier: MIT
#pragma once

#include "core/ik/Rigid.hpp"

#include <optional>

// Arm IK's goals across a root-motion step (research/le2-vrik.md "Arm IK across root-motion steps"). The mod computes the
// goals in its tick (after the frame), in the mesh's component space; the next frame's UpdateSkelPose writes them, and only
// then does the game move the actor. ME2's vault composes a pose whose `Camera` bone (and the body with it) is moved back by
// as much as the actor is about to step (1.1-1.5 m), and the actor then steps: the body and the camera stay where they were
// in the world, but goals left in component space rode the step (the arms and the gun drawn 0.4-1.1 m off for a frame, three
// times a vault). Measured on ME2 (2026-11-15): between the goals and the write the component never moved (0.0 units in 3201
// writes); the camera's bone moved 20-113 units in component space at each step, 1-20 units in the frames around them, under
// 1 unit standing. So the goals follow the camera's hook bone (the anchor) from where it stood in the pose they were computed
// from to where it stands in the pose being written, whenever it moved past `units`: past 1 unit every tick of three vaults kept
// both wrists within 0.08 m of the controllers (0.07 standing), past 5 within 0.12, past 20 within 0.21, never 0.70-1.04.
// Engine-agnostic; tested offline.
namespace mevr::ik {

enum class StepRule {
    Off,     // the goals ride the component (as before 2026-11-15)
    Camera,  // the view hangs on the anchor and the anchor moved past `units`: the goals follow its move, staying with the camera
};

struct StepInput {
    StepRule rule = StepRule::Camera;
    double units = 1.0;            // a step: the anchor moved more than this (component units) between the goals and the write
    bool viewOnAnchor = false;     // the view hangs on the anchor (first person on foot, not held): the camera goes where it goes
    bool anchorKnown = false;      // the anchor bone is the same bone in both poses
    Vec3 anchorThen;               // its component-space translation in the goals' pose
    Vec3 anchorNow;                // ...in the pose being written
    Affine componentThen;          // the mesh's LocalToWorld when the goals were computed (diagnostics: how far it moved)
    Affine componentNow;           // ...and at the write
};

struct Step {
    bool seen = false;        // the anchor moved past `units` (whatever the rule and the view)
    double componentUnits = 0.0;
    double anchorUnits = 0.0;
    Vec3 componentMove;       // world
    Vec3 anchorMove;          // component space
    // Component now <- the goals' frame: solve in the goals' frame (each bone composed with the inverse) and compose the written
    // bones with this. Nullopt: write the goals as published (no step, the rule off, the view not on the anchor).
    std::optional<Transform> toNow;
};

Step FindStep(StepInput const& in);

} // namespace mevr::ik
