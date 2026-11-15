// SPDX-License-Identifier: MIT
// Offline tests for arm IK's goals across a root-motion step (core/ik/GoalStep, research/le2-vrik.md "Arm IK across root-motion
// steps"): what counts as a step, what each rule does, and that a solve in the goals' frame moved back lands the hand where the
// rule says.

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

// A mesh frame: yaw (radians) about Z, then the origin.
ik::Affine Frame(double yaw, ik::Vec3 origin, double scale = 1.0) {
    ik::Affine a;
    double const c = std::cos(yaw) * scale;
    double const s = std::sin(yaw) * scale;
    a.m[0][0] = c;
    a.m[0][1] = s;
    a.m[1][0] = -s;
    a.m[1][1] = c;
    a.m[2][2] = scale;
    a.m[3][0] = origin.x;
    a.m[3][1] = origin.y;
    a.m[3][2] = origin.z;
    return a;
}

void TestOrdinaryFrames() {
    // Walking: the component moves a frame's worth, the camera's bone stands still in component space: no step, any rule.
    for (ik::StepRule rule : {ik::StepRule::Off, ik::StepRule::Camera, ik::StepRule::World}) {
        ik::StepInput in;
        in.rule = rule;
        in.componentThen = Frame(0.3, {100, 200, 50});
        in.componentNow = Frame(0.3, {112, 200, 50});
        in.anchorKnown = true;
        in.anchorThen = {0, 0, 160};
        in.anchorNow = {0.4, -0.2, 160.3};
        ik::Step const step = ik::FindStep(in);
        CHECK(!step.seen);
        CHECK(!step.toNow);
        CHECK(Near(step.componentUnits, 12.0));
    }
}

void TestCameraRule() {
    // The vault's step: the pawn 110 units on, the engine moves the skeleton back by as much (the camera's bone with it).
    ik::StepInput in;
    in.rule = ik::StepRule::Camera;
    in.componentThen = Frame(0.0, {0, 0, 0});
    in.componentNow = Frame(0.0, {110, 0, 0});
    in.anchorKnown = true;
    in.anchorThen = {0, 0, 160};
    in.anchorNow = {-110, 0, 160};
    ik::Step const step = ik::FindStep(in);
    CHECK(step.seen);
    CHECK(step.toNow.has_value());
    if (step.toNow) {
        CHECK(Near(step.toNow->translation, {-110, 0, 0}));
        // A goal at the camera's bone + (30, 20, -40) then is there now too: with the camera in the world.
        ik::Vec3 const goalThen{30, 20, 120};
        ik::Vec3 const goalNow = ik::Apply(*step.toNow, goalThen);
        CHECK(Near(goalNow - in.anchorNow, goalThen - in.anchorThen));
        CHECK(Near(ik::TransformPoint(in.componentNow, goalNow), ik::TransformPoint(in.componentThen, goalThen)));
    }
    // Off: the step is seen (logged), nothing changes.
    in.rule = ik::StepRule::Off;
    ik::Step const off = ik::FindStep(in);
    CHECK(off.seen);
    CHECK(!off.toNow);
    // Without the anchor the camera rule has nothing to follow.
    in.rule = ik::StepRule::Camera;
    in.anchorKnown = false;
    ik::Step const unknown = ik::FindStep(in);
    CHECK(unknown.seen);  // the component's move alone
    CHECK(!unknown.toNow);
}

void TestCameraRuleIgnoresTeleports() {
    // A teleport (SetLocation): the component jumps, the skeleton doesn't move in component space; the camera goes with the pawn,
    // so the goals ride the component as before.
    ik::StepInput in;
    in.rule = ik::StepRule::Camera;
    in.componentThen = Frame(0.0, {0, 0, 0});
    in.componentNow = Frame(0.0, {500, 300, 0});
    in.anchorKnown = true;
    in.anchorThen = {0, 0, 160};
    in.anchorNow = {0, 0, 160};
    ik::Step const step = ik::FindStep(in);
    CHECK(step.seen);
    CHECK(!step.toNow);
    // An animation moves the camera's bone alone (a conversation's gesture while the view is held): nothing to follow either.
    in.componentNow = in.componentThen;
    in.anchorNow = {-60, 10, 150};
    ik::Step const animated = ik::FindStep(in);
    CHECK(animated.seen);
    CHECK(!animated.toNow);
}

void TestWorldRule() {
    ik::StepInput in;
    in.rule = ik::StepRule::World;
    in.componentThen = Frame(0.5, {1000, -300, 20}, 1.2);
    in.componentNow = Frame(0.9, {1050, -200, 80}, 1.2);
    ik::Step const step = ik::FindStep(in);
    CHECK(step.seen);
    CHECK(step.toNow.has_value());
    if (step.toNow) {
        ik::Vec3 const goalThen{30, 20, 120};
        ik::Vec3 const goalNow = ik::Apply(*step.toNow, goalThen);
        CHECK(Near(ik::TransformPoint(in.componentNow, goalNow), ik::TransformPoint(in.componentThen, goalThen), 1e-6));
        CHECK(Near(step.toNow->scale, 1.0));
    }
}

void TestThreshold() {
    ik::StepInput in;
    in.rule = ik::StepRule::Camera;
    in.units = 50.0;
    in.componentNow = Frame(0.0, {100, 0, 0});
    in.anchorKnown = true;
    in.anchorThen = {0, 0, 0};
    in.anchorNow = {-40, 0, 0};
    CHECK(!ik::FindStep(in).toNow);  // the camera's bone 40 of 50
    in.anchorNow = {-60, 0, 0};
    CHECK(ik::FindStep(in).toNow.has_value());
    in.units = -1.0;  // unusable: the default 20
    in.anchorNow = {-25, 0, 0};
    CHECK(ik::FindStep(in).toNow.has_value());
    in.anchorNow = {-15, 0, 0};
    CHECK(ik::FindStep(in).seen);  // the component's 100 units
    CHECK(!ik::FindStep(in).toNow);
}

// The writer's loop: the animated pose moved into the goals' frame, solved there, the result moved back.
void TestSolveInTheGoalsFrame() {
    std::vector<int> const parents{-1, 0, 1, 2};
    std::vector<ik::Transform> pose(4);
    pose[0].translation = {-110, 0, 0};    // the root, moved back by the engine
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
    TestCameraRuleIgnoresTeleports();
    TestWorldRule();
    TestThreshold();
    TestSolveInTheGoalsFrame();
    std::printf("goal step tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
