// SPDX-License-Identifier: MIT
// Offline tests for hand contact with other characters (core/ik/HandContact, research/73): which bones become which body parts,
// the capsules they make, and the hand pushed out of them, sliding, without popping through a capsule it was pushed past.

#include "core/ik/HandContact.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/HandContactTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace ik = mevr::ik;
namespace hc = mevr::ik::hand_contact;

bool Near(double a, double b, double eps = 1e-6) {
    return std::abs(a - b) <= eps;
}

bool Near(ik::Vec3 a, ik::Vec3 b, double eps = 1e-6) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

int CountOf(std::vector<hc::BonePart> const& parts, hc::Part part) {
    int n = 0;
    for (hc::BonePart const& p : parts) {
        n += p.part == part ? 1 : 0;
    }
    return n;
}

void Classify() {
    CHECK(hc::Classify("LowerBack") == hc::Part::Spine);
    CHECK(hc::Classify("Chest2") == hc::Part::Spine);
    CHECK(hc::Classify("Pelvis") == hc::Part::Spine);
    CHECK(hc::Classify("Neck") == hc::Part::Neck);
    CHECK(hc::Classify("Head") == hc::Part::Head);
    CHECK(hc::Classify("LeftCollar") == hc::Part::Collar);
    CHECK(hc::Classify("LeftShoulder") == hc::Part::UpperArm);
    CHECK(hc::Classify("RightElbow") == hc::Part::Forearm);
    CHECK(hc::Classify("RightWrist") == hc::Part::Hand);
    CHECK(hc::Classify("LeftHip") == hc::Part::Thigh);
    CHECK(hc::Classify("LeftKnee") == hc::Part::Shin);
    CHECK(hc::Classify("LeftAnkle") == hc::Part::Foot);
    // Other rigs' names.
    CHECK(hc::Classify("mixamorig:LeftForeArm") == hc::Part::Forearm);
    CHECK(hc::Classify("mixamorig:LeftArm") == hc::Part::UpperArm);
    CHECK(hc::Classify("mixamorig:LeftUpLeg") == hc::Part::Thigh);
    CHECK(hc::Classify("Bip01_R_Hand") == hc::Part::Hand);
    CHECK(hc::Classify("clavicle_l") == hc::Part::Collar);
    // Never colliders.
    CHECK(hc::Classify("RightIndexFinger1") == hc::Part::None);
    CHECK(hc::Classify("LeftToe") == hc::Part::None);
    CHECK(hc::Classify("Eye_Left") == hc::Part::None);
    CHECK(hc::Classify("Prop01") == hc::Part::None);
    CHECK(hc::Classify("LHIK") == hc::Part::None);
    CHECK(hc::Classify("RightElbow-Swivel") == hc::Part::None);
    CHECK(hc::Classify("Camera") == hc::Part::None);
    CHECK(hc::Classify("God") == hc::Part::None);
    CHECK(hc::Classify("Root") == hc::Part::None);
    CHECK(hc::Classify("LeftShoulderTwist") == hc::Part::None);
}

// ME1's human skeleton (research/le2-vrik.md: ME2's is the same), one side of it.
struct Rig {
    std::vector<std::string> names;
    std::vector<int> parents;
    int Add(std::string name, int parent) {
        names.push_back(std::move(name));
        parents.push_back(parent);
        return static_cast<int>(names.size()) - 1;
    }
};

Rig Me1Rig(bool twist) {
    Rig r;
    int const god = r.Add("God", -1);
    int const root = r.Add("Root", god);
    r.Add("Camera", god);
    int const back = r.Add("LowerBack", root);
    int const chest = r.Add("Chest", back);
    int const chest1 = r.Add("Chest1", chest);
    int const chest2 = r.Add("Chest2", chest1);
    int const neck = r.Add("Neck", chest2);
    int const head = r.Add("Head", neck);
    r.Add("Eye_Left", head);
    int const collar = r.Add("LeftCollar", chest2);
    int const shoulder = r.Add("LeftShoulder", collar);
    int const elbowParent = twist ? r.Add("LeftShoulderTwist", shoulder) : shoulder;
    int const elbow = r.Add("LeftElbow", elbowParent);
    int const wrist = r.Add("LeftWrist", elbow);
    r.Add("LeftIndexFinger1", wrist);
    r.Add("Prop02", wrist);
    int const pelvis = r.Add("Pelvis", root);
    int const hip = r.Add("LeftHip", pelvis);
    int const knee = r.Add("LeftKnee", hip);
    int const ankle = r.Add("LeftAnkle", knee);
    r.Add("LeftToe", ankle);
    return r;
}

void Parts() {
    Rig const rig = Me1Rig(false);
    std::vector<hc::BonePart> const parts = hc::PartsFor(rig.names, rig.parents);
    CHECK(CountOf(parts, hc::Part::Spine) == 4);  // LowerBack-Chest, Chest-Chest1, Chest1-Chest2, Chest2-Neck
    CHECK(CountOf(parts, hc::Part::Neck) == 1);
    CHECK(CountOf(parts, hc::Part::Head) == 1);
    CHECK(CountOf(parts, hc::Part::Collar) == 1);
    CHECK(CountOf(parts, hc::Part::UpperArm) == 1);
    CHECK(CountOf(parts, hc::Part::Forearm) == 1);
    CHECK(CountOf(parts, hc::Part::Hand) == 1);
    CHECK(CountOf(parts, hc::Part::Thigh) == 1);
    CHECK(CountOf(parts, hc::Part::Shin) == 1);
    CHECK(parts.size() == 12);

    // A twist bone between the shoulder and the elbow is passed over: the upper arm still runs shoulder to elbow.
    Rig const twisted = Me1Rig(true);
    std::vector<hc::BonePart> const withTwist = hc::PartsFor(twisted.names, twisted.parents);
    CHECK(CountOf(withTwist, hc::Part::UpperArm) == 1);
    for (hc::BonePart const& p : withTwist) {
        if (p.part == hc::Part::UpperArm) {
            CHECK(twisted.names[static_cast<std::size_t>(p.from)] == "LeftShoulder");
            CHECK(twisted.names[static_cast<std::size_t>(p.to)] == "LeftElbow");
        }
    }
}

void Capsules() {
    std::array<ik::Vec3, 2> const at{ik::Vec3{0.0, 0.0, 0.0}, ik::Vec3{0.0, 0.0, 10.0}};
    // The skull above the head bone: from the head bone on along the neck's direction.
    std::optional<hc::Capsule> const head = hc::CapsuleOf({hc::Part::Head, 0, 1, 0.12}, at, 100.0);
    CHECK(head.has_value());
    if (head) {
        CHECK(Near(head->a, {0.0, 0.0, 10.0}));
        CHECK(Near(head->b, {0.0, 0.0, 22.0}));
        CHECK(Near(head->radius, 10.0));
    }
    std::optional<hc::Capsule> const neck = hc::CapsuleOf({hc::Part::Neck, 0, 1}, at, 100.0);
    CHECK(neck.has_value() && Near(neck->a, at[0]) && Near(neck->b, at[1]) && Near(neck->radius, 6.0));
    // Implausible segments are dropped.
    std::array<ik::Vec3, 2> const same{ik::Vec3{1.0, 1.0, 1.0}, ik::Vec3{1.0, 1.0, 1.2}};
    CHECK(!hc::CapsuleOf({hc::Part::Spine, 0, 1}, same, 100.0).has_value());
    std::array<ik::Vec3, 2> const far{ik::Vec3{0.0, 0.0, 0.0}, ik::Vec3{0.0, 0.0, 500.0}};
    CHECK(!hc::CapsuleOf({hc::Part::Spine, 0, 1}, far, 100.0).has_value());
    CHECK(!hc::CapsuleOf({hc::Part::Spine, 0, 5}, at, 100.0).has_value());
}

void Push() {
    std::vector<hc::Capsule> const body{{{0.0, 0.0, 0.0}, {0.0, 0.0, 100.0}, 13.0, hc::Part::Spine}};
    // Outside: untouched.
    hc::Result const free = hc::PushOut({30.0, 0.0, 50.0}, 5.0, body);
    CHECK(!free.touching && Near(free.point, {30.0, 0.0, 50.0}) && Near(free.depth, 0.0));
    // Inside: out to the surface along the line from the axis, the depth measured at the controller's point.
    hc::Result const in = hc::PushOut({10.0, 0.0, 50.0}, 5.0, body);
    CHECK(in.touching && Near(in.point, {18.0, 0.0, 50.0}) && Near(in.depth, 8.0) && in.capsule == 0);
    CHECK(Near(in.normal, {1.0, 0.0, 0.0}));
    // Sliding: a hand moved along the body stays on its surface.
    hc::Result const slid = hc::PushOut({10.0, 0.0, 70.0}, 5.0, body, in.point);
    CHECK(Near(slid.point, {18.0, 0.0, 70.0}));
    // Past the axis: kept on the side it came from (else it popped through to the far side).
    hc::Result const past = hc::PushOut({-3.0, 0.0, 50.0}, 5.0, body, in.point);
    CHECK(Near(past.point, {18.0, 0.0, 50.0}));
    hc::Result const fresh = hc::PushOut({-3.0, 0.0, 50.0}, 5.0, body);
    CHECK(Near(fresh.point, {-18.0, 0.0, 50.0}));
    // On the axis with no history: out somewhere, at the surface.
    hc::Result const onAxis = hc::PushOut({0.0, 0.0, 50.0}, 5.0, body);
    CHECK(onAxis.touching && Near(ik::Length(onAxis.point - ik::Vec3{0.0, 0.0, 50.0}), 18.0));
    // Two capsules that overlap (an arm against the chest): out of both.
    std::vector<hc::Capsule> const two{{{0.0, 0.0, 0.0}, {0.0, 0.0, 100.0}, 13.0, hc::Part::Spine},
                                       {{15.0, 0.0, 0.0}, {15.0, 0.0, 100.0}, 6.0, hc::Part::UpperArm}};
    hc::Result const both = hc::PushOut({12.0, 3.0, 50.0}, 5.0, two);
    CHECK(both.touching);
    for (hc::Capsule const& c : two) {
        double const d = ik::Length(both.point - hc::ClosestOnSegment(both.point, c.a, c.b));
        CHECK(d >= c.radius + 5.0 - 1e-3);
    }
    // Not finite: untouched.
    hc::Result const bad = hc::PushOut({std::nan(""), 0.0, 0.0}, 5.0, body);
    CHECK(!bad.touching);
}

void Haptics() {
    CHECK(Near(hc::PressAmplitude(0.0, 5.0), 0.0));
    CHECK(Near(hc::PressAmplitude(0.001, 5.0), 0.15, 1e-3));
    CHECK(Near(hc::PressAmplitude(10.0, 5.0), 0.6));
    CHECK(Near(hc::PressAmplitude(100.0, 5.0), 0.6));
    CHECK(hc::PressAmplitude(5.0, 5.0) > 0.15 && hc::PressAmplitude(5.0, 5.0) < 0.6);
}

} // namespace

int RunHandContactTests() {
    gFailures = 0;
    Classify();
    Parts();
    Capsules();
    Push();
    Haptics();
    std::printf("hand contact tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
