// SPDX-License-Identifier: MIT
// core/vr/ReloadGesture: the reload by hand's gesture (the pouch, the clip held, the receiver entered from outside), alone and
// driving vr::reload::Coordinator to one native request.
#include "core/vr/ReloadGesture.hpp"

#include <cstdio>
#include <optional>

namespace {
namespace rg = mevr::vr::reload_gesture;
namespace rl = mevr::vr::reload;
int gFailures = 0;
void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ReloadGestureTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

constexpr mevr::ik::vent::OnGun kOutside{0.02, 0.30, 0.0};  // 30 cm over the gun
constexpr mevr::ik::vent::OnGun kNear{0.02, 0.20, 0.0};
constexpr mevr::ik::vent::OnGun kInside{0.02, 0.10, 0.0};   // in the receiver's box

struct Rig {
    rg::Machine machine;
    double t = 0.0;
    rg::Input in;
    Rig() {
        in.tracked = true;
        in.gunValid = true;
        in.reloadable = true;
        in.onGun = kOutside;
    }
    rg::Output Step(float grip, double pouch, mevr::ik::vent::OnGun place) {
        t += 1.0 / 90.0;
        in.nowS = t;
        in.grip = grip;
        in.pouchDistanceM = pouch;
        in.onGun = place;
        return machine.Update(in);
    }
};
} // namespace

int RunReloadGestureTests() {
    // The ordinary gesture: squeeze at the pouch, carry the clip over the gun, push it in.
    {
        Rig r;
        CHECK(r.Step(0.0f, 0.05, kOutside).event == rg::Event::None);
        rg::Output const grab = r.Step(1.0f, 0.05, kOutside);
        CHECK(grab.event == rg::Event::Grabbed && grab.down.eject && !grab.down.acquire && !grab.down.insert);
        rg::Output const second = r.Step(1.0f, 0.30, kOutside);
        CHECK(second.event == rg::Event::None && second.down.eject && second.down.acquire && !second.down.insert && second.holding);
        CHECK(r.Step(1.0f, 0.40, kNear).event == rg::Event::None);
        rg::Output const in = r.Step(1.0f, 0.40, kInside);
        CHECK(in.event == rg::Event::Inserted && in.down.insert && in.down.eject && in.down.acquire);
        rg::Output const after = r.Step(1.0f, 0.40, kInside);  // held on in the receiver: once only
        CHECK(after.event == rg::Event::None && !after.down.eject && !after.down.acquire && !after.down.insert);
        CHECK(r.machine.Insertions() == 1);
    }
    // No reload now (a full clip, no spare one) or no gun: refused, nothing for the coordinator.
    {
        Rig r;
        r.in.reloadable = false;
        r.Step(0.0f, 0.05, kOutside);
        rg::Output const o = r.Step(1.0f, 0.05, kOutside);
        CHECK(o.event == rg::Event::Refused && o.why != nullptr && !o.down.eject);
        Rig g;
        g.in.gunValid = false;
        g.Step(0.0f, 0.05, kOutside);
        CHECK(g.Step(1.0f, 0.05, kOutside).event == rg::Event::Refused);
    }
    // A squeeze away from the pouch, or one another feature claimed, takes nothing.
    {
        Rig r;
        r.Step(0.0f, 0.30, kOutside);
        CHECK(r.Step(1.0f, 0.30, kOutside).event == rg::Event::None);
        Rig c;
        c.Step(0.0f, 0.05, kOutside);
        c.in.squeezeClaimed = true;
        CHECK(c.Step(1.0f, 0.05, kOutside).event == rg::Event::None && !c.machine.Holding());
    }
    // Taken with the hand already in the receiver: it must leave and come back.
    {
        Rig r;
        r.Step(0.0f, 0.05, kInside);
        CHECK(r.Step(1.0f, 0.05, kInside).event == rg::Event::Grabbed);
        r.Step(1.0f, 0.05, kInside);
        for (int i = 0; i < 10; ++i) {
            CHECK(r.Step(1.0f, 0.05, kInside).event == rg::Event::None);
        }
        r.Step(1.0f, 0.30, kNear);
        r.Step(1.0f, 0.30, kOutside);
        CHECK(r.Step(1.0f, 0.30, kNear).event == rg::Event::None);
        CHECK(r.Step(1.0f, 0.30, kInside).event == rg::Event::Inserted);
    }
    // A jump into the receiver (tracking regained, a teleport) is no insertion; out and in again is.
    {
        Rig r;
        r.Step(0.0f, 0.05, kOutside);
        r.Step(1.0f, 0.05, kOutside);
        r.Step(1.0f, 0.30, mevr::ik::vent::OnGun{0.02, 0.60, 0.0});
        CHECK(r.Step(1.0f, 0.30, kInside).event == rg::Event::None);  // 0.5 m in one tick
        CHECK(r.Step(1.0f, 0.30, kInside).event == rg::Event::None);
        r.Step(1.0f, 0.30, kNear);
        CHECK(r.Step(1.0f, 0.30, kInside).event == rg::Event::Inserted);
        // The gun's frame lost and found with the hand inside: the same.
        Rig g;
        g.Step(0.0f, 0.05, kOutside);
        g.Step(1.0f, 0.05, kOutside);
        g.Step(1.0f, 0.30, kNear);
        g.in.gunValid = false;
        g.Step(1.0f, 0.30, kInside);
        g.in.gunValid = true;
        CHECK(g.Step(1.0f, 0.30, kInside).event == rg::Event::None);
        g.Step(1.0f, 0.30, kNear);
        CHECK(g.Step(1.0f, 0.30, kInside).event == rg::Event::Inserted);
    }
    // Let go before the receiver: dropped; the hand lost: lost; held too long: dropped. All signals down after each.
    {
        Rig r;
        r.Step(0.0f, 0.05, kOutside);
        r.Step(1.0f, 0.05, kOutside);
        r.Step(1.0f, 0.30, kOutside);
        rg::Output const drop = r.Step(0.3f, 0.30, kNear);
        CHECK(drop.event == rg::Event::Dropped && !drop.down.eject && !drop.holding);
        CHECK(r.Step(0.3f, 0.30, kInside).event == rg::Event::None);
        Rig l;
        l.Step(0.0f, 0.05, kOutside);
        l.Step(1.0f, 0.05, kOutside);
        l.in.tracked = false;
        CHECK(l.Step(1.0f, 0.30, kOutside).event == rg::Event::Lost);
        Rig h;
        h.Step(0.0f, 0.05, kOutside);
        h.Step(1.0f, 0.05, kOutside);
        rg::Output o;
        for (int i = 0; i < 90 * 9 && o.event == rg::Event::None; ++i) {
            o = h.Step(1.0f, 0.30, kOutside);
        }
        CHECK(o.event == rg::Event::Dropped);
    }
    // Driving the coordinator: one native request from one gesture, none from a dropped clip, none while it is pending.
    {
        rl::Coordinator coordinator(7);
        rl::Identity const identity{1, 1, 1};
        rl::HandGrant const grant{identity, 1, rl::Hand::Left, true};
        std::uint64_t sample = 0;
        std::optional<rl::ClipKey> clip;
        auto feed = [&](rg::Output const& o) {
            rl::Frame f;
            f.sample = ++sample;
            f.identity = identity;
            f.supportedFiniteReserve = f.eligible = f.contextAllowed = f.trackingValid = true;
            f.grant = grant;
            f.down = o.down;
            if (clip) f.insertedClip = *clip;
            rl::StepResult const r = coordinator.Step(f);
            if (r.acquired) clip = r.acquired;
            if (o.event == rg::Event::Dropped || o.event == rg::Event::Lost) coordinator.CancelGesture();
            return r;
        };
        Rig r;
        feed(r.Step(0.0f, 0.30, kOutside));  // all released: the coordinator arms
        CHECK(feed(r.Step(1.0f, 0.05, kOutside)).admission == rl::Admission::Ejected);
        CHECK(feed(r.Step(1.0f, 0.30, kOutside)).admission == rl::Admission::Acquired);
        CHECK(feed(r.Step(1.0f, 0.30, kNear)).admission == rl::Admission::NoEdge);
        rl::StepResult const req = feed(r.Step(1.0f, 0.30, kInside));
        CHECK(req.admission == rl::Admission::Requested && req.request.has_value());
        CHECK(feed(r.Step(1.0f, 0.30, kInside)).admission == rl::Admission::WaitingNative);
        // A second gesture while the first is pending: nothing.
        feed(r.Step(0.0f, 0.30, kOutside));
        CHECK(feed(r.Step(1.0f, 0.05, kOutside)).admission == rl::Admission::WaitingNative);
        // The game finished it: settled, and the next gesture asks again.
        CHECK(coordinator.Observe(rl::Feedback{*req.request, rl::FeedbackKind::Finished, true}) == rl::Observation::Settled);
        r.machine.Reset();
        clip.reset();
        feed(r.Step(0.0f, 0.30, kOutside));
        feed(r.Step(0.0f, 0.30, kOutside));
        CHECK(feed(r.Step(1.0f, 0.05, kOutside)).admission == rl::Admission::Ejected);
        CHECK(feed(r.Step(1.0f, 0.30, kOutside)).admission == rl::Admission::Acquired);
        rl::StepResult const dropped = feed(r.Step(0.0f, 0.30, kNear));  // let go: cancelled, no request
        CHECK(!dropped.request.has_value() && coordinator.GetPhase() == rl::Phase::Idle);
    }
    return gFailures;
}
