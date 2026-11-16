// SPDX-License-Identifier: MIT
#include "game/MenuBodyHold.hpp"
#include <cstdio>
#include <limits>

namespace {
namespace mb = mevr::game::menu_body;
namespace bp = mevr::engine::bone_pose;
int failures = 0;
void Check(bool pass, int line) { if (!pass) { ++failures; std::printf("MenuBodyHoldTests:%d failed\n", line); } }
#define CHECK(value) Check((value), __LINE__)
mb::Identity Identity() {
    mb::Identity id;
    for (std::size_t i = 0; i < id.objects.size(); ++i) id.objects[i] = i + 1;
    id.names = {11, 12, 13, 14};
    id.componentToWorld = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 10, 20, 30, 1};
    id.epochKnown = true;
    id.sessions = id.recenters = 1;
    return id;
}
bp::Request Request() {
    bp::Request r;
    r.mesh = reinterpret_cast<mevr::ue3::Object const*>(4); // opaque identity only; never dereferenced
    r.boneCount = 9;
    r.parents = {-1, 0, 1};
    r.torso.enabled = r.torso.goal.bodyMoves = true;
    r.torso.goal.bodyAt = {30, 40, 50};
    r.torso.goal.head = {10, 20, 150};
    r.torso.chain.spine = {1, 2};
    r.torso.keys = {3, 4, 5, 6};
    r.keep = {0};
    r.keepKeys = {17};
    r.bodyScale.s = 0.93;
    for (auto& arm : r.arms) {
        arm.target = bp::Target::Component;
        arm.hasRotation = true;
        arm.chain = {1, 2, 3};
        arm.keys[0] = 19;
    }
    r.support.enabled = r.gunKick.enabled = true;
    r.gunShift = {1, 2, 3};
    for (auto& hand : r.fingers.hands) hand.enabled = true;
    for (auto& leg : r.legs) leg.enabled = true;
    return r;
}
}

int RunMenuBodyHoldTests() {
    failures = 0;
    auto const identity = Identity();
    auto const request = Request();
    // A fresh live solve follows camera/component movement; ownership and XR
    // reference changes still reset it. The cached menu hold remains stricter.
    auto moving = identity;
    moving.camera[0] += 20;
    moving.componentToWorld[12] += 20;
    CHECK(identity.SameTrackingSpace(moving));
    for (int change = 0; change < 16; ++change) {
        auto changed = identity;
        if (change < 8) ++changed.objects[static_cast<std::size_t>(change)];
        else if (change < 12) ++changed.names[static_cast<std::size_t>(change - 8)];
        else if (change == 12) ++changed.recenters;
        else if (change == 13) ++changed.sessions;
        else if (change == 14) changed.epochKnown = false;
        else changed.floorLocked = !changed.floorLocked;
        CHECK(!identity.SameTrackingSpace(changed));
    }
    mb::Hold hold;
    CHECK(!hold.Get(identity, true, 1000)); // no fabricated pose when menu opens before gameplay
    hold.Capture(request, identity, true, 1000);
    auto const* retained = hold.Get(identity, true, 1010);
    CHECK(retained);
    if (retained) {
        CHECK(retained->torso.enabled && retained->torso.goal.bodyMoves);
        CHECK(retained->torso.goal.bodyAt.x == 30 && retained->torso.goal.head.z == 150);
        CHECK(retained->bodyScale.s == 0.93 && retained->keep == request.keep && retained->keepKeys == request.keepKeys);
        CHECK(retained->parents == request.parents && retained->torso.keys == request.torso.keys);
        for (auto const& arm : retained->arms) {
            CHECK(arm.target == bp::Target::Off && !arm.hasRotation);
            CHECK(arm.chain.upper == 1 && arm.keys[0] == 19); // torso solve still has shoulder/name metadata
        }
        CHECK(!retained->support.enabled && !retained->gunKick.enabled && retained->gunShift.x == 0);
        for (auto const& hand : retained->fingers.hands) CHECK(!hand.enabled);
        for (auto const& leg : retained->legs) CHECK(!leg.enabled);
    }
    // A menu can stay open with fresh validation; source timestamps cannot be silently renewed after a gap.
    for (std::uint64_t now = 1020; now < 30000; now += 30) CHECK(hold.Get(identity, true, now));
    CHECK(!hold.Get(identity, true, 31000));
    CHECK(!hold.Get(identity, true, 31001));
    hold.Capture(request, identity, true, 32000);
    CHECK(hold.Get(identity, true, 32010));
    hold.Close();
    CHECK(!hold.Get(identity, true, 32020));
    hold.Capture(request, identity, true, 33000);
    CHECK(!hold.Get(identity, true, 33251)); // old pre-menu gameplay sample
    hold.Capture(request, identity, true, 33000);
    CHECK(!hold.Get(identity, true, 32999)); // clock reset
    for (int change = 0; change < 20; ++change) {
        hold.Capture(request, identity, true, 1000);
        auto changed = identity;
        if (change < 8) ++changed.objects[static_cast<std::size_t>(change)];
        else if (change < 12) ++changed.names[static_cast<std::size_t>(change - 8)];
        else if (change == 12) changed.componentToWorld[12] += 0.01; // pawn/component moved, even within same world
        else if (change == 13) changed.componentToWorld[0] = std::numeric_limits<double>::quiet_NaN();
        else if (change == 14) ++changed.recenters;
        else if (change == 15) ++changed.sessions;
        else if (change == 16) changed.epochKnown = false;
        else if (change == 17) changed.camera[2] += 10; // native camera changes (including FP toggle)
        else if (change == 18) changed.floorLocked = true;
        CHECK(!hold.Get(changed, change != 19, 1010)); // final case: cinema/calibration/action/menu owner gate
        CHECK(!hold.Get(identity, true, 1020)); // cannot recover from a mismatch by returning to the old pointers
    }
    for (int blocked = 0; blocked < 6; ++blocked) {
        auto value = request;
        if (blocked == 1) value.torso.enabled = false;
        if (blocked == 2) value.torso.goal.bodyMoves = false;
        if (blocked == 3) value.mesh = nullptr;
        if (blocked == 5) value.mesh = reinterpret_cast<mevr::ue3::Object const*>(99);
        hold.Capture(value, identity, blocked != 0, blocked == 4 ? 0 : 1000);
        CHECK(!hold.Get(identity, true, 1010));
    }
    CHECK(request.support.enabled && request.arms[0].target == bp::Target::Component); // input request unchanged
    std::printf("menu body hold tests: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures;
}
#if defined(MEVR_MENU_BODY_HOLD_STANDALONE)
int main() { return RunMenuBodyHoldTests(); }
#endif
