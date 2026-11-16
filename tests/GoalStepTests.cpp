// SPDX-License-Identifier: MIT
// Offline tests for arm IK's goals across a root-motion step (core/ik/GoalStep, research/le2-vrik.md "Arm IK across root-motion
// steps"): what counts as a step, when the rule applies, and that a solve in the goals' frame moved back lands the hand with the
// camera after the actor's step.

#include "core/ik/ArmPose.hpp"
#include "core/ik/GoalStep.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/GoalStepTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

bool Near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

bool Near(mevr::ik::Vec3 a, mevr::ik::Vec3 b, double eps = 1e-6) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

namespace ik = mevr::ik;

// A mesh frame: the origin moved, no turn.
ik::Affine At(ik::Vec3 origin) {
    ik::Affine a;
    a.m[3][0] = origin.x;
    a.m[3][1] = origin.y;
    a.m[3][2] = origin.z;
    return a;
}

ik::StepInput Vault(double anchorMoveX) {
    ik::StepInput in;
    in.rule = ik::StepRule::Camera;
    in.viewOnAnchor = true;
    in.anchorKnown = true;
    in.anchorThen = {0, 0, 160};
    in.anchorNow = {anchorMoveX, 0, 160};
    return in;
}

void TestOrdinaryFrames() {
    // Walking or standing: the camera's bone stands still in component space (the idle animation's sway: a unit or so), the
    // component moves a frame's worth (not seen at the write on ME2, but counted).
    for (ik::StepRule rule : {ik::StepRule::Off, ik::StepRule::Camera}) {
        ik::StepInput in = Vault(0.4);
        in.rule = rule;
        in.componentThen = At({100, 200, 50});
        in.componentNow = At({112, 200, 50});
        ik::Step const step = ik::FindStep(in);
        CHECK(!step.seen);
        CHECK(!step.toNow);
        CHECK(Near(step.componentUnits, 12.0));
    }
}

void TestCameraRule() {
    // The vault's forward step (measured: the camera's bone 86-113 units back along x in the pose written before the actor steps).
    ik::StepInput in = Vault(-110);
    ik::Step const step = ik::FindStep(in);
    CHECK(step.seen);
    CHECK(step.toNow.has_value());
    if (step.toNow) {
        CHECK(Near(step.toNow->translation, {-110, 0, 0}));
        // A goal at the camera's bone + (30, 20, -40) then is there now too: after the actor steps 110 on, it is where it was in the
        // world, with the camera.
        ik::Vec3 const goalThen{30, 20, 120};
        ik::Vec3 const goalNow = ik::Apply(*step.toNow, goalThen);
        CHECK(Near(goalNow - in.anchorNow, goalThen - in.anchorThen));
        ik::Affine const before = At({0, 0, 0});
        ik::Affine const stepped = At({110, 0, 0});
        CHECK(Near(ik::TransformPoint(stepped, goalNow), ik::TransformPoint(before, goalThen)));
    }
    // Off: the step is seen (logged), nothing changes.
    in.rule = ik::StepRule::Off;
    ik::Step const off = ik::FindStep(in);
    CHECK(off.seen);
    CHECK(!off.toNow);
}

void TestHeldViewAndUnknownAnchor() {
    // A held view (a conversation shown in the world, a scripted walk): an animation's jump of the bone doesn't move the camera.
    ik::StepInput in = Vault(-60);
    in.viewOnAnchor = false;
    ik::Step const held = ik::FindStep(in);
    CHECK(held.seen);
    CHECK(!held.toNow);
    // Another bone at that index (a new skeleton): nothing to follow.
    in = Vault(-60);
    in.anchorKnown = false;
    ik::Step const unknown = ik::FindStep(in);
    CHECK(!unknown.seen);
    CHECK(!unknown.toNow);
    // A teleport moves the component alone: the camera goes with the pawn, the goals ride the component as before.
    in = Vault(0.0);
    in.componentNow = At({500, 300, 0});
    ik::Step const teleport = ik::FindStep(in);
    CHECK(!teleport.seen);
    CHECK(!teleport.toNow);
    CHECK(Near(teleport.componentUnits, std::sqrt(500.0 * 500.0 + 300.0 * 300.0)));
}

void TestThreshold() {
    ik::StepInput in = Vault(-40);
    in.units = 50.0;
    CHECK(!ik::FindStep(in).toNow);
    in.anchorNow = {-60, 0, 160};
    CHECK(ik::FindStep(in).toNow.has_value());
    in.units = -1.0;  // unusable: the default 1
    in.anchorNow = {-1.5, 0, 160};
    CHECK(ik::FindStep(in).toNow.has_value());
    in.anchorNow = {-0.5, 0, 160};
    CHECK(!ik::FindStep(in).seen);
    in.anchorNow = {std::nan(""), 0, 160};
    CHECK(!ik::FindStep(in).toNow);
}

// The writer's loop: the animated pose moved into the goals' frame, solved there, the result moved back.
void TestSolveInTheGoalsFrame() {
    std::vector<int> const parents{-1, 0, 1, 2};
    std::vector<ik::Transform> pose(4);
    pose[0].translation = {-110, 0, 0};    // the body, composed 110 units back for the actor's coming step
    pose[1].translation = {-110, 20, 140};  // the shoulder
    pose[2].translation = {-110, 48, 140};  // the elbow
    pose[3].translation = {-110, 74, 140};  // the wrist
    ik::ArmChain chain{1, 2, 3};
    ik::BodyFrame body;
    ik::Transform const toNow{{}, {-110, 0, 0}, 1.0};
    ik::ArmGoal goal;
    goal.wrist = {30, 25, 120};  // computed against the pose before the step (its shoulder at (0, 20, 140))
    ik::Transform const back = ik::Inverse(toNow);
    for (ik::Transform& bone : pose) {
        bone = ik::Compose(bone, back);
    }
    ik::ArmResult const result = ik::PoseArm(pose, parents, chain, 1, body, goal);
    CHECK(result.ok);
    for (ik::Transform& bone : pose) {
        bone = ik::Compose(bone, toNow);
    }
    // The wrist where the goal was against the shoulder, with the skeleton: 110 units back in component space.
    CHECK(Near(pose[3].translation, {-80, 25, 120}, 1e-3));
    CHECK(Near(pose[1].translation, {-110, 20, 140}, 1e-9));
}

} // namespace

int RunGoalStepTests() {
    gFailures = 0;
    TestOrdinaryFrames();
    TestCameraRule();
    TestHeldViewAndUnknownAnchor();
    TestThreshold();
    TestSolveInTheGoalsFrame();
    std::printf("goal step tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
