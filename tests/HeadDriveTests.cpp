// SPDX-License-Identifier: MIT
// The player's head drives Shepard in a held scene (the owner's "Your head drives him", 2026-11-20;
// research/le2-conversations-in-world.md section 13, research/69 item 45). A scripted animation leans Shepard's head away
// from the player's eyes while his pawn stays under the view (ME3's Purgatory drink: his head 1.05 m forward onto the bar).
// This replays the first-person body path offline on LE1's Shepard skeleton (tests/EyesInBodyTests.cpp's bones and heights):
// the player standing still (core/ik/BodyFollow), the torso goal as game/BodyFollow builds it with the head drive's bend
// (ik::kHeadDriveBendDegrees), the torso solve (core/ik/TorsoPose), then core/vr/ConversationWorld's StepHeadDrive on his
// drawn head against the player's eyes: a 1 m lean is absorbed (his head back within 0.3 m, never hidden), a 2 m lean falls
// back to the hide after 0.25 s, seated is left to the seat (no drive, no hide).

#include "core/ik/ArmPose.hpp"
#include "core/ik/BodyFollow.hpp"
#include "core/ik/Ik.hpp"
#include "core/ik/Rigid.hpp"
#include "core/ik/TorsoPose.hpp"
#include "core/vr/ConversationWorld.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/HeadDriveTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

using namespace mevr::ik;
namespace cw = mevr::vr::conversation_world;

constexpr double kDeg = std::numbers::pi / 180.0;
constexpr double kUnitsPerMeter = 100.0;
constexpr double kDt = 1.0 / 90.0;

// LE1's Shepard standing idle (tests/EyesInBodyTests.cpp's Le1Rig: `mevr camera trace`'s bones, research/72; the eyes at
// 1.694 m), in his mesh component's space (units; +X forward, +Y right, +Z up, the floor at 0).
struct Rig {
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
    Rig() {
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
    bool Below(int ancestor, int bone) const {
        for (int b = bone; b >= 0; b = parents[static_cast<std::size_t>(b)]) {
            if (b == ancestor) return true;
        }
        return false;
    }
};

Quat AxisAngle(Vec3 axis, double degrees) {
    Vec3 const a = Normalize(axis, {0, 0, 1});
    double const half = degrees * kDeg * 0.5;
    return {a.x * std::sin(half), a.y * std::sin(half), a.z * std::sin(half), std::cos(half)};
}

// The scene's animation: the whole body (Root's subtree) `forwardUnits` ahead of the pawn, the spine bent `bendDegrees`
// forward about LowerBack (Shepard leaning onto a bar). The camera hook bones (God, Camera) stay.
std::vector<Transform> Leaning(Rig const& rig, double forwardUnits, double bendDegrees) {
    std::vector<Transform> pose = rig.pose;
    for (int b = 0; b < Rig::Count; ++b) {
        if (rig.Below(Rig::Root, b)) pose[static_cast<std::size_t>(b)].translation.x += forwardUnits;
    }
    Quat const bend = AxisAngle({0, 1, 0}, bendDegrees);  // about +Y (right): +X forward tips down
    Vec3 const pivot = pose[Rig::LowerBack].translation;
    for (int b = 0; b < Rig::Count; ++b) {
        if (b != Rig::LowerBack && rig.Below(Rig::LowerBack, b)) {
            Transform& t = pose[static_cast<std::size_t>(b)];
            t.translation = pivot + Rotate(bend, t.translation - pivot);
            t.rotation = Normalize(bend * t.rotation);
        }
    }
    return pose;
}

// Tracking space (OpenXR metres, -Z forward, +Y up) to the component's, the floor at 0 (tests/EyesInBodyTests.cpp's Map).
Vec3 Map(Vec3 t) {
    return {-t.z * kUnitsPerMeter, t.x * kUnitsPerMeter, t.y * kUnitsPerMeter};
}

double Flat(Vec3 a, Vec3 b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

struct Run {
    bool ok = true;
    double animatedGapM = 0.0;  // his animated head from the eyes, flat (what the player saw before)
    double drivenGapM = 0.0;    // his drawn head from the eyes after the drive, flat (the last tick)
    bool hidden = false;        // StepHeadDrive's state at the end
    int hides = 0;
    double hiddenAfterS = -1.0;
    bool drove = false;
    bool cameraKept = true;     // God and Camera never written
};

// `seconds` of the player standing still at the origin (eyes 1.694 m: Shepard's own, so no body scale) while the scene's
// animation holds the lean; `bend`: the torso's bend limit (the head drive's, or the on-foot 35).
Run Simulate(double forwardUnits, double bendDegrees, double bendLimit, bool seated, double seconds = 1.5) {
    Rig rig;
    Run run;
    auto const left = FindArmChain(rig.names, rig.parents, 0);
    auto const right = FindArmChain(rig.names, rig.parents, 1);
    auto const chain = left && right ? FindTorsoChain(rig.names, rig.parents, *left, *right) : std::nullopt;
    if (!chain) {
        run.ok = false;
        return run;
    }
    std::vector<Transform> const animated = Leaning(rig, forwardUnits, bendDegrees);
    Vec3 const eyesTracking{0.0, 1.694, 0.0};
    Vec3 const eyes = Map(eyesTracking);
    run.animatedGapM = Flat(animated[Rig::Head].translation, eyes) / kUnitsPerMeter;
    BodyFollow follow;
    BodyFrame const body;
    cw::HeadDriveState state;
    int const ticks = static_cast<int>(seconds / kDt);
    for (int i = 0; i <= ticks; ++i) {
        Vec3 const pivot = HeadPivot(eyesTracking, Quat{});
        FollowInput in;
        in.pivot = pivot;
        in.haveRecenters = true;
        in.deltaSeconds = kDt;
        FollowState const s = follow.Update(in);
        // game/BodyFollow::Fill, first person, the whole body under the view; the head drive's bend.
        TorsoGoal goal;
        goal.reference = {Rig::Head, -1};
        goal.head = Map(pivot);
        goal.body = Rig::Root;
        goal.bodyTurns = true;
        goal.bodyMoves = true;
        goal.bodyAt = Map(s.feet);
        goal.maxBodyShift = 0.75 * kUnitsPerMeter;  // [Hands] bodyMoveMaxM
        goal.maxTwistDegrees = 30.0;
        goal.maxBendDegrees = bendLimit;
        if (bendLimit == kHeadDriveBendDegrees) goal.maxTiltDegrees = kHeadDriveTiltDegrees;
        goal.forward = {1, 0, 0};
        std::vector<Transform> pose = animated;
        // The scene's rule decides on the last drawn head (the writer's result of the tick before; here this tick's).
        cw::HeadDriveFacts facts;
        facts.scene = true;
        facts.withHim = true;
        facts.seated = seated;
        facts.dtS = kDt;
        bool drive = !seated;
        if (drive) {
            TorsoResult const r = PoseTorso(pose, rig.parents, *chain, *left, *right, body, goal);
            if (!r.ok) {
                run.ok = false;
                return run;
            }
        }
        facts.headFromEyesM = Flat(pose[Rig::Head].translation, eyes) / kUnitsPerMeter;
        cw::HeadDriveStep const step = cw::StepHeadDrive(state, facts);
        run.drove = run.drove || step.drive;
        if (step.change == cw::HeadDriveChange::Hid) {
            ++run.hides;
            if (run.hiddenAfterS < 0.0) run.hiddenAfterS = i * kDt;
        }
        run.drivenGapM = facts.headFromEyesM;
        run.hidden = step.hidden;
        run.cameraKept = run.cameraKept && Length(pose[Rig::Camera].translation - animated[Rig::Camera].translation) == 0.0 &&
                         Length(pose[Rig::God].translation - animated[Rig::God].translation) == 0.0;
    }
    return run;
}

// MEVR_HEAD_DRIVE_REPORT=1 prints each run's numbers.
Run Reported(char const* name, Run const& r) {
    char* report = nullptr;
    std::size_t length = 0;
    bool const print = _dupenv_s(&report, &length, "MEVR_HEAD_DRIVE_REPORT") == 0 && report != nullptr && report[0] == '1';
    std::free(report);
    if (print) {
        std::printf("head drive: %-28s animated %.2f m, driven %.2f m, hidden %d (%d hides, after %.2f s), drove %d\n", name, r.animatedGapM,
            r.drivenGapM, r.hidden ? 1 : 0, r.hides, r.hiddenAfterS, r.drove ? 1 : 0);
    }
    return r;
}

} // namespace

int RunHeadDriveTests() {
    gFailures = 0;
    // Standing straight with the player: nothing to do, nothing hidden.
    {
        Run const r = Reported("standing", Simulate(0.0, 0.0, kHeadDriveBendDegrees, false));
        CHECK(r.ok && r.drove && !r.hidden && r.hides == 0 && r.drivenGapM < 0.15);
    }
    // A 1 m lean (the body 0.37 m forward, the spine bent 88 degrees: his head 1.0 m in front of the eyes) is absorbed: the
    // slide brings the body back under the player, the spine straightens, his head within 0.3 m of the eyes; never hidden.
    {
        Run const r = Reported("1 m lean", Simulate(37.0, 88.0, kHeadDriveBendDegrees, false));
        CHECK(r.ok && r.animatedGapM > 0.95 && r.animatedGapM < 1.1);
        CHECK(r.drivenGapM < 0.3);
        CHECK(!r.hidden && r.hides == 0);
        CHECK(r.cameraKept);
        // With the on-foot bend (35 degrees) the same lean leaves his head 0.3 m or more ahead: why the drive widens it.
        Run const onFoot = Reported("1 m lean, on-foot bend", Simulate(37.0, 88.0, 35.0, false));
        CHECK(onFoot.ok && onFoot.drivenGapM >= 0.3);
    }
    // Purgatory's drink as measured (his head 1.05 m forward, dropped 0.15-0.18 m): the body 0.85 m forward, the spine 30.
    {
        Run const r = Reported("the drink", Simulate(85.0, 30.0, kHeadDriveBendDegrees, false));
        CHECK(r.ok && r.animatedGapM > 0.95);
        CHECK(r.drivenGapM < 0.3 && !r.hidden);
    }
    // A 2 m lean (the body 1.9 m forward and bent 30) is past the slide's 0.75 m: the spine leans back at most 30 degrees, his
    // head stays more than 0.6 m ahead, so the body is hidden after 0.25 s, once.
    {
        Run const r = Reported("2 m lean", Simulate(190.0, 30.0, kHeadDriveBendDegrees, false));
        CHECK(r.ok && r.animatedGapM > 1.9);
        CHECK(r.drivenGapM > 0.6);
        CHECK(r.hidden && r.hides == 1);
        CHECK(r.hiddenAfterS >= 0.23 && r.hiddenAfterS <= 0.27);
    }
    // Seated: the seat keeps StepSeat (the view sits with his head); no drive and no hide, whatever his head does.
    {
        Run const r = Reported("2 m lean, seated", Simulate(190.0, 30.0, kHeadDriveBendDegrees, true));
        CHECK(r.ok && !r.drove && !r.hidden && r.hides == 0);
    }
    // The rule alone: the hysteresis, a dip, an unknown distance, leaving the scene.
    {
        cw::HeadDriveState s;
        cw::HeadDriveFacts f;
        f.scene = f.withHim = true;
        f.dtS = 0.1;
        f.headFromEyesM = 0.7;
        CHECK(cw::StepHeadDrive(s, f).change == cw::HeadDriveChange::None);  // 0.1 s
        CHECK(cw::StepHeadDrive(s, f).change == cw::HeadDriveChange::None);  // 0.2 s
        cw::HeadDriveStep step = cw::StepHeadDrive(s, f);                      // 0.3 s
        CHECK(step.change == cw::HeadDriveChange::Hid && step.hidden && step.drive);
        f.headFromEyesM = 0.5;  // between: stays hidden, the timers reset
        for (int i = 0; i < 10; ++i) CHECK(cw::StepHeadDrive(s, f).hidden);
        f.headFromEyesM = std::numeric_limits<double>::quiet_NaN();  // unknown: holds
        CHECK(cw::StepHeadDrive(s, f).hidden);
        f.headFromEyesM = 0.3;
        f.dip = true;  // under a dip: no drive, nothing changes
        for (int i = 0; i < 5; ++i) {
            step = cw::StepHeadDrive(s, f);
            CHECK(!step.drive && step.hidden && step.change == cw::HeadDriveChange::None);
        }
        f.dip = false;
        CHECK(cw::StepHeadDrive(s, f).hidden);
        CHECK(cw::StepHeadDrive(s, f).hidden);
        step = cw::StepHeadDrive(s, f);
        CHECK(step.change == cw::HeadDriveChange::Shown && !step.hidden);
        // Hidden again, then the view leaves him (apart: the scene's own hide rules): let go at once.
        f.headFromEyesM = 0.9;
        for (int i = 0; i < 3; ++i) cw::StepHeadDrive(s, f);
        CHECK(s.hidden);
        f.withHim = false;
        step = cw::StepHeadDrive(s, f);
        CHECK(step.change == cw::HeadDriveChange::Shown && !step.drive && !s.hidden);
        CHECK(std::string(cw::Name(cw::HeadDriveChange::Hid)) == "hid");
    }
    return gFailures;
}
