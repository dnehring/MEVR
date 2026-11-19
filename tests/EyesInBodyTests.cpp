// SPDX-License-Identifier: MIT
// The eyes inside the body (research/29 "The eyes inside the body", 2026-11-18). The owner, from ME1 in the headset:
// "sometimes I could clip and look into my own body". This replays the first-person body path offline on a skeleton laid
// out as LE1's Shepard (the bones and heights from `mevr camera trace`, research/72; his eyes 1.694 m, research/58): the
// player's head (squats, bends, leans, a look down, a quick turn) through the head's pivot and the feet (core/ik/BodyFollow),
// the torso goal as game/BodyFollow builds it, the body scale as engine/BonePose applies it (research/58), the torso solve
// (core/ik/TorsoPose) and the legs (core/ik/TorsoPose's LegUnderBody), then measures where the player's eyes are against
// Shepard's drawn face and armour. Set MEVR_EYES_IN_BODY_REPORT=1 to print the table.

#include "core/ik/ArmPose.hpp"
#include "core/ik/BodyFollow.hpp"
#include "core/ik/BodyScale.hpp"
#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"
#include "core/ik/TorsoPose.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/EyesInBodyTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

using namespace mevr::ik;
namespace bs = mevr::ik::body_scale;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kUnitsPerMeter = 100.0;  // life-size (research/30)
constexpr double kNearPlane = 10.0;       // LocalPlayer.NearClipPlane, units
constexpr double kDt = 1.0 / 90.0;
constexpr double kCrouchSlackMeters = 0.06;  // game/BodyFollow's: the run's bob left alone while moving

Quat AxisAngle(Vec3 axis, double degrees) {
    Vec3 const a = Normalize(axis, {0, 0, 1});
    double const half = degrees * kDeg * 0.5;
    return {a.x * std::sin(half), a.y * std::sin(half), a.z * std::sin(half), std::cos(half)};
}

// Shepard (LE1, HMM_ARM_MEDb) standing idle, holstered, in his mesh component's space (units; +X forward, +Y right, +Z
// up, the floor at 0): the bones `mevr camera trace` records (research/72, the Presidium idle, the mean of the first
// rows), LE1's hierarchy (the `skeleton:` lines of LE1MEVR.log: God > Root > LowerBack > Chest > Chest1 > Chest2; Root >
// Pelvis > hips; God > Camera), the eyes at 1.694 m (research/58). Bones the trace doesn't record are placed between their
// neighbours (Chest, Chest1, Neck1, the knees, the toes) or from the drawn arm lengths (research/58: upper 28.9, lower
// 26.3, the shoulder joints 37.4 apart).
struct Le1Rig {
    enum Bone : int {
        God, Root, LowerBack, Chest, Chest1, Chest2, LeftCollar, RightCollar, Pelvis, RightHip, RightKnee, RightAnkle, RightToe,
        LeftHip, LeftKnee, LeftAnkle, LeftToe, Neck, Neck1, Head, HeadBase, EyeRight, EyeLeft, LeftShoulder, LeftElbow,
        LeftWrist, RightShoulder, RightElbow, RightWrist, Camera, Count
    };
    std::vector<std::string> names{"God", "Root", "LowerBack", "Chest", "Chest1", "Chest2", "LeftCollar", "RightCollar",
        "Pelvis", "RightHip", "RightKnee", "RightAnkle", "RightToe", "LeftHip", "LeftKnee", "LeftAnkle", "LeftToe", "Neck",
        "Neck1", "Head", "headBase", "Eye_Right", "Eye_Left", "LeftShoulder", "LeftElbow", "LeftWrist", "RightShoulder",
        "RightElbow", "RightWrist", "Camera"};
    std::vector<int> parents{-1, God, Root, LowerBack, Chest, Chest1, Chest2, Chest2, Root, Pelvis, RightHip, RightKnee,
        RightAnkle, Pelvis, LeftHip, LeftKnee, LeftAnkle, Chest2, Neck, Neck1, Head, HeadBase, HeadBase, LeftCollar,
        LeftShoulder, LeftElbow, RightCollar, RightShoulder, RightElbow, God};
    std::vector<Transform> pose;
    Le1Rig() {
        std::vector<Vec3> const at{{0.0, 0.0, 88.0}, {0.6, 0.7, 100.9}, {-0.8, 0.7, 104.0}, {-1.4, 0.6, 116.0},
            {-1.8, 0.5, 128.0}, {-2.1, 0.4, 140.1}, {-2.7, -2.8, 148.3}, {-2.7, 3.3, 148.4}, {0.6, 0.7, 100.9},
            {4.0, 11.6, 99.4}, {0.4, 10.8, 55.2}, {-7.3, 9.9, 11.05}, {6.0, 10.5, 2.0}, {2.7, -10.5, 99.3}, {-0.2, -9.9, 55.2},
            {-7.0, -9.3, 11.1}, {6.0, -10.0, 2.0}, {-0.6, 0.2, 153.0}, {0.5, 0.15, 159.4}, {1.6, 0.1, 165.8},
            {1.6, 0.1, 166.0}, {10.0, 3.1, 169.4}, {10.0, -3.1, 169.4}, {-2.5, -18.7, 145.0}, {-1.0, -21.0, 116.2},
            {3.0, -20.0, 90.3}, {-2.5, 18.7, 145.0}, {-1.0, 21.0, 116.2}, {3.0, 20.0, 90.3}, {0.0, 0.0, 88.0}};
        for (Vec3 const& p : at) {
            pose.push_back({Quat{}, p, 1.0});
        }
    }
};

// The armour around the spine as capsules (drawn units before the body's scale): the chest, the collar and the neck seal
// (the head is a separate mesh, hidden in first person; Halcyon hides the eyes too), and the shoulders.
struct Capsule {
    int a;
    int b;
    double radius;
};
std::vector<Capsule> const kArmour{{Le1Rig::LowerBack, Le1Rig::Chest2, 14.0}, {Le1Rig::Chest2, Le1Rig::Neck, 11.0},
    {Le1Rig::Neck, Le1Rig::Neck1, 7.0}, {Le1Rig::LeftCollar, Le1Rig::LeftShoulder, 8.0},
    {Le1Rig::RightCollar, Le1Rig::RightShoulder, 8.0}};

bool NearlyLifted(double lift, double expected, double tolerance) {
    return std::abs(lift - expected) <= tolerance;
}

double SegmentDistance(Vec3 p, Vec3 a, Vec3 b) {
    Vec3 const ab = b - a;
    double const t = std::clamp(Dot(p - a, ab) / std::max(1e-12, Dot(ab, ab)), 0.0, 1.0);
    return Length(p - (a + ab * t));
}

// Tracking space (OpenXR: metres, +X right, +Y up, -Z forward; the floor at y = 0) to the component's (+X forward, +Y right,
// +Z up), as the view maps it with the floor lock (research/58 section 6: the floor at the pawn cylinder's bottom).
Vec3 Map(Vec3 t) {
    return {-t.z * kUnitsPerMeter, t.x * kUnitsPerMeter, t.y * kUnitsPerMeter};
}

// The body's yaw (BodyYaw's convention: 0 = -Z, + left) as a component direction.
Vec3 YawForward(double yaw) {
    return {std::cos(yaw), -std::sin(yaw), 0.0};
}

// One tick of the player's head: the eyes' midpoint and the head's orientation, tracking space.
struct Head {
    Vec3 eyes;
    Quat orientation;
};

// The player: the owner's calibrated standing eye height (research/58: 1.84 m) and the hip joint's height.
struct Player {
    double eyeHeight = 1.84;
    double hipHeight = 1.0;
    // The head's pivot (the neck model: 10 cm under the eyes) above the hips.
    double SpineLength() const { return eyeHeight - 0.10 - hipHeight; }
};

// A head that looks down `pitch` degrees and is turned `yaw` (+ left), its pivot at `pivot`.
Head HeadAt(Vec3 pivot, double pitch, double yaw = 0.0) {
    Quat const q = Normalize(AxisAngle({0, 1, 0}, yaw) * AxisAngle({1, 0, 0}, -pitch));
    NeckModel const neck;
    return {pivot + Rotate(q, Vec3{0.0, neck.down, -neck.back}), q};
}

// The player's moves, as a head pose at time t (seconds). Each stands still for a second, then moves over `moveS`,
// then holds.
struct Scenario {
    std::string name;
    double seconds = 4.0;
    std::function<Head(Player const&, double)> head;
};

double Ease(double t, double start, double length) {
    double const u = std::clamp((t - start) / std::max(1e-9, length), 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

// The standing pivot: over the feet at the origin.
Vec3 StandingPivot(Player const& p) {
    return {0.0, p.eyeHeight - 0.10, 0.0};
}

Scenario Stand() {
    return {"stand straight", 3.0, [](Player const& p, double) { return HeadAt(StandingPivot(p), 0.0); }};
}

// A squat (knees bent, back upright): the head straight down by `meters`, looking ahead or down at the floor.
Scenario Squat(double meters, double pitch = 0.0) {
    char name[64];
    std::snprintf(name, sizeof name, "squat %2.0f cm%s", meters * 100.0, pitch > 0.0 ? ", look down 45" : "");
    return {name, 4.0, [meters, pitch](Player const& p, double t) {
        double const k = Ease(t, 1.0, 0.6);
        return HeadAt(StandingPivot(p) - Vec3{0.0, meters * k, 0.0}, pitch * k);
    }};
}

// A bend forward at the hips by `degrees` (the hips going back a little, as a person's do), the head looking down past it.
Scenario Bend(double degrees) {
    char name[64];
    std::snprintf(name, sizeof name, "bend %2.0f deg", degrees);
    return {name, 4.0, [degrees](Player const& p, double t) {
        double const k = Ease(t, 1.0, 0.8);
        double const a = degrees * k * kDeg;
        double const back = 0.15 * std::sin(a);  // the hips go back to balance
        Vec3 const hips{0.0, p.hipHeight, back};
        Vec3 const pivot = hips + Vec3{0.0, p.SpineLength() * std::cos(a), -p.SpineLength() * std::sin(a)};
        return HeadAt(pivot, std::min(90.0, degrees * k + 20.0 * k));
    }};
}

// A lean back at the hips: the head's pivot `meters` behind the feet, the head level.
Scenario LeanBack(double meters) {
    char name[64];
    std::snprintf(name, sizeof name, "lean back %2.0f cm", meters * 100.0);
    return {name, 4.0, [meters](Player const& p, double t) {
        double const k = Ease(t, 1.0, 0.6);
        double const a = std::asin(std::clamp(meters * k / p.SpineLength(), -1.0, 1.0));
        Vec3 const pivot{0.0, p.hipHeight + p.SpineLength() * std::cos(a), p.SpineLength() * std::sin(a)};
        return HeadAt(pivot, 0.0);
    }};
}

// Standing straight, looking down at the chest.
Scenario LookDown(double pitch) {
    char name[64];
    std::snprintf(name, sizeof name, "look down %2.0f deg", pitch);
    return {name, 3.0, [pitch](Player const& p, double t) { return HeadAt(StandingPivot(p), pitch * Ease(t, 1.0, 0.4)); }};
}

// A quick head turn: 90 degrees left in 0.15 s, held; measured over the turn.
Scenario QuickTurn() {
    return {"quick turn 90 deg", 2.0, [](Player const& p, double t) {
        return HeadAt(StandingPivot(p), 10.0, 90.0 * Ease(t, 1.0, 0.15));
    }};
}

struct Options {
    bool crouch = false;      // the fix: the body down to the head (TorsoGoal::crouch), the legs under it
    double bodyScale = 1.0;   // 0: auto (the player's eye height over Shepard's)
    double gameCrouch = 0.0;  // the animation's own crouch: Shepard's body this many units lower, his knees bent
    double moving = 0.0;      // the legs keeping the run (LegsState::weight): the gap within the slack is the run's
    bool up = true;           // [Hands] crouchBodyUp: the body also comes up to a head above it
    bool headLimit = true;    // game/ArmIk's TorsoGoal::crouchMaxDrop: no deeper than the player's own head went down
    double raised = 0.0;      // the animation's upper body this many units higher than standing (ME2's run, research/29)
};

// game/ArmIk's margin for TorsoGoal::crouchMaxDrop (research/29 "A run never lowers the body").
constexpr double kCrouchDropMarginMeters = 0.02;

// A pose that holds Shepard's head higher than his standing one, as ME2's run and the steps after it do (the owner's log of
// 2026-11-18: his shoulders 6-7 units over the idle's, the gap 15-19 cm with the eyes at standing height): everything from
// the spine's bottom joint up (LowerBack's subtree: the spine, the collars, the arms, the head and the eyes) `units` higher.
std::vector<Transform> Raised(Le1Rig const& rig, std::vector<Transform> pose, double units) {
    if (units == 0.0) {
        return pose;
    }
    for (int b = 0; b < Le1Rig::Count; ++b) {
        for (int a = b; a >= 0; a = rig.parents[a]) {
            if (a == Le1Rig::LowerBack) {
                pose[b].translation.z += units;
                break;
            }
        }
    }
    return pose;
}

// Shepard's pose as the game's crouch would have it, roughly: the whole body (Root's subtree) `units` lower, the feet where
// they stood and the knees bent forward (the rig's legs solved to their ankles).
std::vector<Transform> GameCrouched(Le1Rig const& rig, double units) {
    std::vector<Transform> pose = rig.pose;
    if (units <= 0.0) {
        return pose;
    }
    for (int b = Le1Rig::Root; b < Le1Rig::Count; ++b) {
        if (b != Le1Rig::Camera) {
            pose[b].translation.z -= units;
        }
    }
    BodyFrame const body;
    for (int side : {0, 1}) {
        auto const leg = FindLegChain(rig.names, rig.parents, side);
        if (leg) {
            if (auto const goal = LegUnderBody(pose, *leg, {0.0, 0.0, units}, body)) {
                PoseArm(pose, rig.parents, *leg, side, body, *goal);
            }
        }
    }
    return pose;
}

// What the player sees at one tick (drawn units = cm at 100 per metre).
struct Sample {
    double faceGap = 0.0;  // the player's eyes from Shepard's drawn eyes
    double below = 0.0;    // ...how far below them (+ below)
    double behind = 0.0;   // ...how far behind them along the body's facing (+ behind)
    double depth = -1e9;   // how deep inside the armour the eyes are (+ inside)
    double ankleLift = 0.0;     // the higher ankle above where it stood (+ off the floor, - into it)
    double kneeForward = 0.0;   // the knees ahead of the hip-ankle line (+ forward), the lesser of the two
    double lift = 0.0;          // the body's vertical move (+ up)
    bool ok = true;
};

struct Run {
    Sample last;
    Sample worst;  // the deepest inside, over the run
    bool ok = true;
};

Run Simulate(Scenario const& scenario, Player const& player, Options const& options) {
    Le1Rig rig;
    auto const left = FindArmChain(rig.names, rig.parents, 0);
    auto const right = FindArmChain(rig.names, rig.parents, 1);
    auto const chain = left && right ? FindTorsoChain(rig.names, rig.parents, *left, *right) : std::nullopt;
    auto const leftLeg = FindLegChain(rig.names, rig.parents, 0);
    auto const rightLeg = FindLegChain(rig.names, rig.parents, 1);
    Run run;
    if (!chain || !leftLeg || !rightLeg) {
        run.ok = false;
        return run;
    }
    double const shepardEye = 0.5 * (rig.pose[Le1Rig::EyeLeft].translation.z + rig.pose[Le1Rig::EyeRight].translation.z);
    double const s = options.bodyScale > 0.0 ? options.bodyScale
                                             : std::clamp(player.eyeHeight * kUnitsPerMeter / shepardEye, bs::kMin, bs::kMax);
    std::vector<Transform> const animated = Raised(rig, GameCrouched(rig, options.gameCrouch), options.raised);
    BodyFollow follow;
    BodyFrame const body;
    int const ticks = static_cast<int>(scenario.seconds / kDt);
    for (int i = 0; i <= ticks; ++i) {
        double const t = i * kDt;
        Head const head = scenario.head(player, t);
        Vec3 const pivot = HeadPivot(head.eyes, head.orientation);
        double const headYaw = YawOf(head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w);
        FollowInput in;
        in.pivot = pivot;
        in.headYaw = headYaw;
        in.facingYaw = headYaw;
        in.haveRecenters = true;
        in.deltaSeconds = kDt;
        FollowState const state = follow.Update(in);

        // game/BodyFollow::Fill, first person, the whole body under the view.
        TorsoGoal goal;
        goal.reference = {Le1Rig::Head, -1};
        goal.head = Map(pivot);
        goal.body = Le1Rig::Root;
        goal.bodyTurns = true;
        goal.bodyMoves = true;
        goal.bodyAt = Map(state.feet);
        goal.maxBodyShift = 0.75 * kUnitsPerMeter;
        goal.maxTwistDegrees = 30.0;
        goal.forward = YawForward(state.yaw);
        if (options.crouch) {
            goal.crouch = true;
            goal.eyes = {Le1Rig::EyeLeft, Le1Rig::EyeRight};
            goal.neckDown = NeckModel{}.down * kUnitsPerMeter;
            goal.crouchSlack = kCrouchSlackMeters * kUnitsPerMeter;
            goal.crouchLoose = options.moving;
            if (options.headLimit) {
                // game/ArmIk: the gap a standing Shepard would show (his standing eyes at the drawn scale over the floor at 0).
                goal.crouchMaxDrop = CrouchDropLimit(s * shepardEye, goal.neckDown, goal.head.z, kCrouchDropMarginMeters * kUnitsPerMeter);
            }
        }

        // engine/BonePose: the goals through the scale's inverse, the solves, the scale.
        Vec3 const eyes = Map(head.eyes);
        bs::Scale const scale{s, {eyes.x, eyes.y, 0.0}};
        goal.head = bs::Unscale(scale, goal.head);
        goal.bodyAt = bs::Unscale(scale, goal.bodyAt);
        goal.maxBodyShift = bs::UnscaleLength(scale, goal.maxBodyShift);
        goal.neckDown = bs::UnscaleLength(scale, goal.neckDown);
        goal.crouchSlack = bs::UnscaleLength(scale, goal.crouchSlack);
        goal.crouchMaxDrop = bs::UnscaleLength(scale, goal.crouchMaxDrop);
        std::vector<Transform> pose = animated;
        std::array<ArmChain, 2> const legs{*leftLeg, *rightLeg};
        if (options.crouch) {
            // engine/BonePose: the room the legs give, up only with crouchUp.
            goal.crouchUp = options.up;
            LegRoom const room = LegsRoom(pose, legs, body.up);
            goal.maxDrop = room.drop;
            goal.maxRise = goal.crouchUp ? room.rise : 0.0;
        }
        TorsoResult const r = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
        if (!r.ok) {
            run.ok = false;
            return run;
        }
        if (options.crouch) {
            // engine/BonePose's planted legs: the ankles back by the lift (and the plant's correction, none here).
            Vec3 const move = r.feetCorrection - body.up * r.bodyLift;
            for (int side : {0, 1}) {
                if (auto const leg = LegUnderBody(pose, legs[side], move, body)) {
                    if (!PoseArm(pose, rig.parents, legs[side], side, body, *leg).ok) {
                        run.ok = false;
                        return run;
                    }
                }
            }
        }
        for (int b = 0; b < Le1Rig::Count; ++b) {
            if (b != Le1Rig::God && b != Le1Rig::Camera) {
                pose[b] = bs::Apply(scale, pose[b]);
            }
        }

        // Where the player's eyes are against the drawn Shepard.
        Sample sample;
        Vec3 const face = (pose[Le1Rig::EyeLeft].translation + pose[Le1Rig::EyeRight].translation) * 0.5;
        Vec3 const facing = Normalize(r.facing, {1, 0, 0});
        sample.faceGap = Length(eyes - face);
        sample.below = face.z - eyes.z;
        sample.behind = Dot(face - eyes, facing);
        for (Capsule const& c : kArmour) {
            double const d = SegmentDistance(eyes, pose[c.a].translation, pose[c.b].translation);
            sample.depth = std::max(sample.depth, c.radius * s - d);
        }
        Vec3 const restLeft = bs::Apply(scale, animated[Le1Rig::LeftAnkle]).translation;
        Vec3 const restRight = bs::Apply(scale, animated[Le1Rig::RightAnkle]).translation;
        sample.ankleLift = std::max(pose[Le1Rig::LeftAnkle].translation.z - restLeft.z, pose[Le1Rig::RightAnkle].translation.z - restRight.z);
        auto const kneeAhead = [&](int hip, int knee, int ankle) {
            Vec3 const h = pose[hip].translation;
            Vec3 const a = pose[ankle].translation;
            Vec3 const k = pose[knee].translation;
            Vec3 const along = Normalize(a - h, {0, 0, -1});
            Vec3 const off = (k - h) - along * Dot(k - h, along);
            return Dot(off, facing);
        };
        sample.kneeForward = std::min(kneeAhead(Le1Rig::LeftHip, Le1Rig::LeftKnee, Le1Rig::LeftAnkle),
            kneeAhead(Le1Rig::RightHip, Le1Rig::RightKnee, Le1Rig::RightAnkle));
        sample.lift = r.bodyLift * s;
        run.last = sample;
        if (i == 0 || sample.depth > run.worst.depth) {
            run.worst = sample;
        }
    }
    return run;
}

std::vector<Scenario> Scenarios() {
    std::vector<Scenario> all{Stand()};
    for (double const d : {0.10, 0.20, 0.30, 0.40, 0.46}) {
        all.push_back(Squat(d));
    }
    all.push_back(Squat(0.20, 45.0));
    for (double const a : {15.0, 30.0, 45.0, 60.0, 70.0}) {
        all.push_back(Bend(a));
    }
    all.push_back(LeanBack(0.10));
    all.push_back(LeanBack(0.20));
    all.push_back(LookDown(60.0));
    all.push_back(LookDown(80.0));
    all.push_back(QuickTurn());
    return all;
}

// Squats from the game's crouch (Shepard crouched by the animation): the player's eyes at these fractions of their
// standing height (the physical crouch's thresholds: crouch below 0.75, stand above 0.85).
std::vector<Scenario> CrouchedScenarios() {
    std::vector<Scenario> all;
    for (double const f : {0.84, 0.74, 0.60, 0.45}) {
        Scenario s = Squat((1.0 - f) * Player{}.eyeHeight);
        char name[64];
        std::snprintf(name, sizeof name, "eyes at %.2f of standing", f);
        s.name = name;
        all.push_back(s);
    }
    return all;
}

void Report(char const* title, Player const& player, Options const& options, std::vector<Scenario> const& scenarios) {
    std::printf("\n%s (player's eyes %.2f m, body scale %s):\n", title, player.eyeHeight,
        options.bodyScale > 0.0 ? std::to_string(options.bodyScale).c_str() : "auto");
    std::printf("  %-26s %7s %6s %6s %7s %6s %6s %6s | worst over the move: %6s %7s\n", "case", "face cm", "below", "behind",
        "inside", "lift", "ankle", "knee", "below", "inside");
    for (Scenario const& scenario : scenarios) {
        Run const run = Simulate(scenario, player, options);
        if (!run.ok) {
            std::printf("  %-26s solve failed\n", scenario.name.c_str());
            continue;
        }
        Sample const& a = run.last;
        Sample const& w = run.worst;
        std::printf("  %-26s %7.1f %6.1f %6.1f %7.1f %6.1f %6.1f %6.1f | %27.1f %7.1f%s\n", scenario.name.c_str(), a.faceGap, a.below,
            a.behind, a.depth, a.lift, a.ankleLift, a.kneeForward, w.below, w.depth,
            w.depth > 0.0 ? "  INSIDE" : (w.depth > -kNearPlane ? "  near plane" : ""));
    }
}

} // namespace

int RunEyesInBodyTests() {
    char* report = nullptr;
    std::size_t length = 0;
    bool const print = _dupenv_s(&report, &length, "MEVR_EYES_IN_BODY_REPORT") == 0 && report != nullptr && report[0] != '\0' &&
                       report[0] != '0';
    std::free(report);
    Player const owner;
    if (print) {
        std::printf("\nThe eyes inside the body (tests/EyesInBodyTests.cpp; research/29). cm at 100 units a metre, drawn: face = the "
                    "player's eyes from Shepard's drawn eyes, below/behind them; inside = how deep in the armour (+ inside; within "
                    "%.0f the near plane cuts it); lift = the body's move (+ up); ankle = the feet off the floor; knee = ahead of "
                    "the hip-ankle line.\n", kNearPlane);
        Report("Before: the body slides under the view, the spine leans (crouchBody 0)", owner, {false, 0.0}, Scenarios());
        Report("After: the body goes down to the head, the legs bend (crouchBody 1)", owner, {true, 0.0}, Scenarios());
        Report("Before, from the game's crouch (Shepard 45 units lower)", owner, {false, 0.0, 45.0}, CrouchedScenarios());
        Report("After, from the game's crouch (crouchBodyUp 1)", owner, {true, 0.0, 45.0}, CrouchedScenarios());
        Report("After, the game's crouch with the player standing (the button; crouchBodyUp 1)", owner, {true, 0.0, 45.0}, {Stand()});
        Report("After, moving (the run's bob of up to 6 cm left alone)", owner, {true, 0.0, 0.0, 1.0},
            {Stand(), Squat(0.10), Squat(0.20), Squat(0.40)});
        Report("ME2's run: the head 13 units over standing, without the head's limit (the bug)", owner,
            {true, 0.0, 0.0, 0.0, false, false, 13.0}, {Stand(), Squat(0.10), Squat(0.20)});
        Report("ME2's run: the head 13 units over standing, the body no deeper than the player's head", owner,
            {true, 0.0, 0.0, 0.0, false, true, 13.0}, {Stand(), Squat(0.10), Squat(0.20)});
        Player const short_{1.50, 0.80};
        Report("Before, a player with 1.50 m eyes", short_, {false, 0.0}, {Stand(), Squat(0.20), Bend(45.0)});
        Report("After, a player with 1.50 m eyes", short_, {true, 0.0}, {Stand(), Squat(0.20), Bend(45.0)});
    }

    // The cause, measured: without the rule a squat from 20 cm down puts the eyes inside the armour, and a bend from 30
    // degrees passes through it; standing straight the eyes are at Shepard's face.
    Options const before{false, 0.0};
    Options const after{true, 0.0};
    Run const standBefore = Simulate(Stand(), owner, before);
    CHECK(standBefore.ok && standBefore.last.faceGap < 2.0 && standBefore.worst.depth < 0.0);
    for (double const d : {0.20, 0.30, 0.40}) {
        Run const r = Simulate(Squat(d), owner, before);
        CHECK(r.ok && r.last.depth > 0.0 && r.last.below > d * 100.0 - 3.0);
    }
    for (double const a : {30.0, 45.0, 60.0, 70.0}) {
        Run const r = Simulate(Bend(a), owner, before);
        CHECK(r.ok && r.worst.depth > 0.0);
    }

    // Standing straight with the rule: nothing moves (no lift, the legs as animated, the same pose to a few millimetres).
    Run const standAfter = Simulate(Stand(), owner, after);
    CHECK(standAfter.ok && standAfter.last.lift == 0.0 && std::abs(standAfter.last.ankleLift) < 1e-9);
    CHECK(std::abs(standAfter.last.faceGap - standBefore.last.faceGap) < 0.5 && std::abs(standAfter.last.depth - standBefore.last.depth) < 0.5);
    CHECK(std::abs(standAfter.last.kneeForward - standBefore.last.kneeForward) < 1e-9);  // the leg untouched

    // Squats up to the physical crouch's threshold (0.75 of the standing eye height: 46 cm for the owner): the eyes within 2 cm
    // of Shepard's face all the way down and back, never inside, the feet on the floor, the knees bending forward.
    for (double const d : {0.05, 0.10, 0.20, 0.30, 0.40, 0.46}) {
        Run const r = Simulate(Squat(d), owner, after);
        CHECK(r.ok && r.last.faceGap < 2.0 && r.worst.depth < 0.0);
        CHECK(NearlyLifted(r.last.lift, -d * 100.0, 2.0));
        CHECK(std::abs(r.last.ankleLift) < 0.5 && r.last.kneeForward > 5.0);
    }
    // A bend, however deep: never inside over the whole move.
    for (double const a : {15.0, 30.0, 45.0, 60.0, 70.0}) {
        Run const r = Simulate(Bend(a), owner, after);
        CHECK(r.ok && r.worst.depth < 0.0 && std::abs(r.last.ankleLift) < 0.5);
    }
    // Leaning back, looking down, a quick turn: as before (nothing to lower).
    for (Scenario const& s : {LeanBack(0.10), LookDown(80.0), QuickTurn()}) {
        Run const r0 = Simulate(s, owner, before);
        Run const r1 = Simulate(s, owner, after);
        CHECK(r0.ok && r1.ok && r1.worst.depth < 0.0 && std::abs(r1.last.depth - r0.last.depth) < 1.0);
    }
    // A shorter player (a smaller body): the same.
    Player const smaller{1.50, 0.80};
    Run const smallSquat = Simulate(Squat(0.20), smaller, after);
    CHECK(smallSquat.ok && smallSquat.last.faceGap < 3.0 && smallSquat.worst.depth < 0.0 && std::abs(smallSquat.last.ankleLift) < 0.5);

    // From the game's crouch (Shepard's body 45 units lower, his knees bent): the body comes up to a head above it (the
    // hysteresis band before the game stands him up) and down to one below it, as far as the legs fold.
    Options const crouched{true, 0.0, 45.0};
    Run const band = Simulate(CrouchedScenarios()[0], owner, crouched);
    CHECK(band.ok && band.last.faceGap < 2.0 && band.last.lift > 10.0 && std::abs(band.last.ankleLift) < 0.5);
    Run const low = Simulate(CrouchedScenarios()[2], owner, crouched);
    CHECK(low.ok && low.last.faceGap < 2.0 && low.last.lift < -10.0 && std::abs(low.last.ankleLift) < 0.5);
    Run const lowBefore = Simulate(CrouchedScenarios()[2], {}, {false, 0.0, 45.0});
    CHECK(lowBefore.ok && lowBefore.last.depth > 0.0);
    // Down only ([Hands] crouchBodyUp 0): no rise out of the game's crouch, the drop as before.
    Options const downOnly{true, 0.0, 45.0, 0.0, false};
    Run const bandDown = Simulate(CrouchedScenarios()[0], owner, downOnly);
    CHECK(bandDown.ok && bandDown.last.lift == 0.0 && bandDown.last.below < -10.0);
    Run const lowDown = Simulate(CrouchedScenarios()[2], owner, downOnly);
    CHECK(lowDown.ok && NearlyLifted(lowDown.last.lift, low.last.lift, 1e-6));

    // Moving (the legs keeping the run): a gap within the slack is the run's own bob and stays; past it the body follows.
    Options const moving{true, 0.0, 0.0, 1.0};
    Run const runStand = Simulate(Stand(), owner, moving);
    CHECK(runStand.ok && runStand.last.lift == 0.0);
    Run const runSquat = Simulate(Squat(0.20), owner, moving);
    CHECK(runSquat.ok && NearlyLifted(runSquat.last.lift, -(20.0 - kCrouchSlackMeters * 100.0), 2.5) && runSquat.worst.depth < 0.0);

    // A run never lowers the body (research/29 "A run never lowers the body", 2026-11-18): ME2's run and the steps after it
    // hold Shepard's head higher than standing. Without the limit a player standing straight had the body go down for it (the
    // owner's 14-17 cm mid-run, the knees bent), in the run (the slack) and after it (no slack); with it, at most the margin.
    for (double const weight : {0.0, 1.0}) {
        Options bug{true, 0.0, 0.0, weight, false, false, 13.0};
        Run const without = Simulate(Stand(), owner, bug);
        CHECK(without.ok && without.last.lift < (weight > 0.0 ? -4.0 : -10.0));
        bug.headLimit = true;
        Run const with = Simulate(Stand(), owner, bug);
        CHECK(with.ok && with.last.lift <= 0.0 && with.last.lift > -(kCrouchDropMarginMeters * 100.0 + 0.5));
        CHECK(std::abs(with.last.ankleLift) < 0.5);
    }
    // ...and the player's own crouch still takes the body down with the head, as far as the head went.
    for (double const d : {0.10, 0.20, 0.30}) {
        Run const r = Simulate(Squat(d), owner, {true, 0.0, 0.0, 0.0, false, true, 13.0});
        CHECK(r.ok && NearlyLifted(r.last.lift, -d * 100.0, 2.5) && std::abs(r.last.ankleLift) < 0.5);
    }
    // The limit is the gap a standing Shepard shows: it binds nowhere above (every squat, bend and the game's crouch above
    // ran with it); an unknown standing height is no limit; never below the margin.
    CHECK(CrouchDropLimit(184.0, 10.0, 174.0, 2.0) == 2.0);
    CHECK(CrouchDropLimit(184.0, 10.0, 154.0, 2.0) == 22.0);
    CHECK(CrouchDropLimit(184.0, 10.0, 190.0, 2.0) == 2.0);
    CHECK(std::isinf(CrouchDropLimit(std::numeric_limits<double>::quiet_NaN(), 10.0, 174.0, 2.0)));
    {
        // In the solve: a gap past the limit is held at it and says so; within it, nothing changes.
        Le1Rig rig;
        auto const left = FindArmChain(rig.names, rig.parents, 0);
        auto const right = FindArmChain(rig.names, rig.parents, 1);
        auto const chain = FindTorsoChain(rig.names, rig.parents, *left, *right);
        CHECK(chain.has_value());
        TorsoGoal goal;
        goal.reference = {Le1Rig::Head, -1};
        goal.head = rig.pose[Le1Rig::Head].translation - Vec3{0, 0, 30};
        goal.forward = {1, 0, 0};
        goal.body = Le1Rig::Root;
        goal.bodyMoves = true;
        goal.bodyAt = rig.pose[Le1Rig::Root].translation;
        goal.maxBodyShift = 75.0;
        goal.crouch = true;
        goal.eyes = {Le1Rig::EyeLeft, Le1Rig::EyeRight};
        goal.neckDown = 10.0;
        goal.maxDrop = 60.0;
        goal.crouchMaxDrop = 8.0;
        std::vector<Transform> pose = rig.pose;
        TorsoResult const held = PoseTorso(pose, rig.parents, *chain, *left, *right, BodyFrame{}, goal);
        CHECK(held.ok && NearlyLifted(held.bodyLift, -8.0, 1e-9) && held.liftLimited && held.liftByHead);
        goal.crouchMaxDrop = 50.0;
        pose = rig.pose;
        TorsoResult const free = PoseTorso(pose, rig.parents, *chain, *left, *right, BodyFrame{}, goal);
        goal.crouchMaxDrop = std::numeric_limits<double>::infinity();
        pose = rig.pose;
        TorsoResult const none = PoseTorso(pose, rig.parents, *chain, *left, *right, BodyFrame{}, goal);
        CHECK(free.ok && none.ok && !free.liftByHead && free.bodyLift < -20.0 && free.bodyLift == none.bodyLift);
    }

    // The parts. The room the legs give: standing, (nearly) no rise and a deep drop; crouched, room both ways.
    {
        Le1Rig rig;
        std::array<ArmChain, 2> const legs{*FindLegChain(rig.names, rig.parents, 0), *FindLegChain(rig.names, rig.parents, 1)};
        LegRoom const standing = LegsRoom(rig.pose, legs, {0, 0, 1});
        CHECK(standing.rise < 2.0 && standing.drop > 60.0);
        LegRoom const bent = LegsRoom(GameCrouched(rig, 45.0), legs, {0, 0, 1});
        CHECK(bent.rise > 35.0 && bent.drop > 20.0 && bent.drop < standing.drop - 40.0);
        // A leg that isn't there: no room at all.
        std::array<ArmChain, 2> const broken{legs[0], ArmChain{}};
        LegRoom const none = LegsRoom(rig.pose, broken, {0, 0, 1});
        CHECK(none.drop == 0.0 && none.rise == 0.0);

        // The leg under the body: nothing for no move; for a drop, the foot back on its spot at its angle, the knee forward.
        BodyFrame const body;
        CHECK(!LegUnderBody(rig.pose, legs[0], {}, body));
        std::vector<Transform> pose = rig.pose;
        Quat const footTurn = AxisAngle({0, 0, 1}, 20.0);
        pose[Le1Rig::LeftAnkle].rotation = footTurn;
        Vec3 const ankle = pose[Le1Rig::LeftAnkle].translation;
        for (int b : {Le1Rig::LeftHip, Le1Rig::LeftKnee, Le1Rig::LeftAnkle, Le1Rig::LeftToe}) {
            pose[b].translation.z -= 20.0;  // the body lowered with the leg on it
        }
        auto const goal = LegUnderBody(pose, legs[0], {0, 0, 20.0}, body);
        CHECK(goal.has_value());
        if (goal) {
            Vec3 const kneeBefore = pose[Le1Rig::LeftKnee].translation;
            CHECK(PoseArm(pose, rig.parents, legs[0], 0, body, *goal).ok);
            CHECK(Length(pose[Le1Rig::LeftAnkle].translation - ankle) < 1e-6);
            CHECK(AngleBetween(pose[Le1Rig::LeftAnkle].rotation, footTurn) < 1e-6);
            CHECK(pose[Le1Rig::LeftKnee].translation.x > kneeBefore.x + 5.0);
        }
    }

    // Without both legs the torso can't lower the body (the writer gives it no room): crouch on, no move.
    {
        Le1Rig rig;
        auto const left = FindArmChain(rig.names, rig.parents, 0);
        auto const right = FindArmChain(rig.names, rig.parents, 1);
        auto const chain = FindTorsoChain(rig.names, rig.parents, *left, *right);
        CHECK(chain.has_value());
        TorsoGoal goal;
        goal.reference = {Le1Rig::Head, -1};
        goal.head = rig.pose[Le1Rig::Head].translation - Vec3{0, 0, 30};
        goal.forward = {1, 0, 0};
        goal.body = Le1Rig::Root;
        goal.bodyMoves = true;
        goal.bodyAt = rig.pose[Le1Rig::Root].translation;
        goal.maxBodyShift = 75.0;
        goal.crouch = true;
        goal.eyes = {Le1Rig::EyeLeft, Le1Rig::EyeRight};
        goal.neckDown = 10.0;
        std::vector<Transform> pose = rig.pose;
        TorsoResult const r = PoseTorso(pose, rig.parents, *chain, *left, *right, BodyFrame{}, goal);
        CHECK(r.ok && r.bodyLift == 0.0 && r.liftLimited && r.crouchGap > 20.0);
        // With room: the gap closed, the camera hook bones never moved.
        goal.maxDrop = 60.0;
        pose = rig.pose;
        TorsoResult const lowered = PoseTorso(pose, rig.parents, *chain, *left, *right, BodyFrame{}, goal);
        CHECK(lowered.ok && !lowered.liftLimited && lowered.bodyLift < -20.0);
        CHECK(Length(pose[Le1Rig::Camera].translation - rig.pose[Le1Rig::Camera].translation) == 0.0 &&
              Length(pose[Le1Rig::God].translation - rig.pose[Le1Rig::God].translation) == 0.0);
        CHECK(std::abs(pose[Le1Rig::Head].translation.z - (goal.head.z + goal.neckDown - (169.4 - 165.8))) < 1.0);
        // The rule off: the old solve exactly.
        goal.crouch = false;
        std::vector<Transform> a = rig.pose;
        TorsoResult const off = PoseTorso(a, rig.parents, *chain, *left, *right, BodyFrame{}, goal);
        TorsoGoal plain = goal;
        plain.eyes = {-1, -1};
        plain.neckDown = plain.maxDrop = 0.0;
        std::vector<Transform> b = rig.pose;
        CHECK(off.ok && off.bodyLift == 0.0 && PoseTorso(b, rig.parents, *chain, *left, *right, BodyFrame{}, plain).ok);
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(Length(a[i].translation - b[i].translation) == 0.0);
        }
    }
    return gFailures;
}
