// SPDX-License-Identifier: MIT
#include "core/ik/Melee.hpp"
#include <array>
#include <cstdio>
#include <limits>
#include <vector>

namespace {
namespace melee = mevr::ik::melee;
int failures = 0;
void Check(bool value, int line) { if (!value) { ++failures; std::printf("MeleeTests:%d failed\n", line); } }
#define CHECK(x) Check((x), __LINE__)

melee::Input Input() {
    melee::Input in;
    in.allowed = true;
    in.generation = 1;
    for (auto& hand : in.hands) {
        hand.available = true;
        hand.neutral = true;
        hand.motion.tracked = true;
        hand.motion.bodyForward = {1, 0, 0};
    }
    return in;
}
std::vector<melee::Request> Stroke(melee::Gestures& g, melee::Input in, double start, double speed = 4) {
    std::vector<melee::Request> out;
    for (int tick = 0; tick < 28; ++tick) {
        for (auto& hand : in.hands) {
            hand.motion.now = start + tick * 0.01;
            hand.motion.position = {tick < 5 ? 0.0 : (tick - 5) * 0.01 * speed, 0, 0};
        }
        if (auto r = g.Step(in)) out.push_back(*r);
    }
    return out;
}
constexpr mevr::ik::Vec3 rest{0.3, -0.38, -0.24};
constexpr mevr::ik::Vec3 chamber{0.3, -0.10, -0.24};
melee::Input ChamberInput() {
    auto in = Input();
    in.chamberHeavy = true;
    in.hands[0].available = false;
    in.hands[1].motion.bodyForward = {0, 0, -1};
    return in;
}
struct ChamberRun {
    melee::Gestures gestures;
    melee::Input in = ChamberInput();
    double time = 0;
    double dt = 1.0 / 31.0;
    int cues = 0;
    std::vector<melee::Request> requests;
    void Sample(mevr::ik::Vec3 position) {
        in.hands[1].motion.position = position;
        in.hands[1].motion.now = time;
        if (auto r = gestures.Step(in)) requests.push_back(*r);
        cues += gestures.HeavyArmedNow() ? 1 : 0;
        time += dt;
    }
    void Hold(mevr::ik::Vec3 position, int frames) { for (int i = 0; i < frames; ++i) Sample(position); }
    void Enter() {
        int const frames = static_cast<int>(0.12 / dt) + 1;
        for (int i = 1; i <= frames; ++i) Sample(rest + (chamber - rest) * (static_cast<double>(i) / frames));
    }
    void Arm() { Hold(rest, 4); Enter(); Hold(chamber, 16); }
    void Strike() {
        for (int i = 1; i <= 8; ++i) Sample({chamber.x, chamber.y, chamber.z - i * dt * 4.0});
    }
};
}
int RunMeleeTests() {
    failures = 0;
    {
        melee::Gestures g;
        auto in = Input();
        auto const r = Stroke(g, in, 0.0, 6);
        CHECK(r.size() == 1); // both hands, sustained fast stroke, one shared request
        if (!r.empty()) CHECK(r[0].intent == melee::Intent::Light && r[0].generation == 1);
        CHECK(Stroke(g, in, 0.3).empty()); // global cooldown also prevents the other hand
    }
    {
        melee::Gestures g;
        auto in = Input();
        in.hands[1].available = false;
        in.hands[0].heavy = true;
        CHECK(Stroke(g, in, 0.0).empty()); // holding modifier across startup cannot arm
        in.hands[0].heavy = false;
        in.hands[0].motion.now = 1;
        g.Step(in); // explicit release
        in.hands[0].heavy = true;
        auto const heavy = Stroke(g, in, 1.1);
        CHECK(heavy.size() == 1);
        if (!heavy.empty()) CHECK(heavy[0].intent == melee::Intent::Heavy);
        CHECK(Stroke(g, in, 2.1).empty()); // no second heavy without release
    }
    for (int interruption = 0; interruption < 4; ++interruption) {
        melee::Gestures g;
        auto in = Input();
        in.hands[1].available = false;
        CHECK(Stroke(g, in, 0.0).size() == 1);
        auto blocked = in;
        blocked.hands[0].motion.now = 0.5;
        if (interruption == 0) blocked.allowed = false; // seated/menu/native action
        if (interruption == 1) blocked.hands[0].available = false; // clip/support ownership
        if (interruption == 2) blocked.hands[0].motion.tracked = false;
        if (interruption == 3) ++blocked.generation; // load/actor/scene
        CHECK(!g.Step(blocked));
        in.generation = blocked.generation;
        in.hands[0].neutral = false;
        CHECK(Stroke(g, in, 1.0).empty()); // no history crossing interruption
        in.hands[0].neutral = true;
        CHECK(Stroke(g, in, 2.0).size() == 1);
    }
    {
        melee::Gestures g;
        auto in = Input();
        in.hands[1].available = false;
        g.Step(in);
        in.hands[0].motion.now = 0.01;
        g.Step(in); // neutral, armed for a later heavy modifier
        in.hands[0].heavy = true;
        in.hands[0].motion.now = 0.02;
        in.hands[0].motion.position = {10, 0, 0}; // recenter/pose jump
        CHECK(!g.Step(in));
        CHECK(Stroke(g, in, 1.0).empty()); // held heavy does not survive the history break
    }
    {
        ChamberRun r;
        r.Hold(chamber, 35);
        CHECK(r.cues == 0 && r.requests.empty()); // held on startup must leave and re-enter
        r.Arm();
        CHECK(r.cues == 1 && r.requests.empty());
        CHECK(r.gestures.HeavyState() == melee::HeavyChamber::Phase::Armed);
        r.Strike();
        CHECK(r.requests.size() == 1);
        if (!r.requests.empty()) CHECK(r.requests[0].intent == melee::Intent::Heavy && r.requests[0].hand == 1);
        r.Strike();
        CHECK(r.requests.size() == 1); // sustained/repeated stroke needs release and new dwell
        r.Hold(rest, 35);
        r.Hold(chamber, 16);
        CHECK(r.cues == 2);
        r.Strike();
        CHECK(r.requests.size() == 2);
    }
    {
        ChamberRun r;
        r.Arm();
        r.Hold(chamber, 50); // expiration while still in chamber cannot automatically re-arm
        CHECK(r.cues == 1 && r.gestures.HeavyState() == melee::HeavyChamber::Phase::Release);
        r.Strike();
        CHECK(r.requests.empty()); // expired heavy intent never downgrades into a late light strike
    }
    // Every admission/ownership failure discards the cue and accumulated pose history.
    for (int interruption = 0; interruption < 9; ++interruption) {
        ChamberRun r;
        r.Arm();
        CHECK(r.cues == 1);
        auto saved = r.in;
        if (interruption == 0) r.in.allowed = false; // focus/menu/authored mode
        if (interruption == 1) r.in.hands[1].available = false; // native cover/storm/action/weapon or hand owner
        if (interruption == 2) r.in.hands[1].neutral = false; // trigger/grip/button; never stolen
        if (interruption == 3) r.in.hands[1].motion.tracked = false;
        if (interruption == 4) ++r.in.generation; // load, actor, weapon or scene replacement
        if (interruption == 5) r.time += 1; // missing tracking interval
        if (interruption == 6) r.time -= 0.1; // clock discontinuity
        if (interruption == 7) r.in.hands[1].motion.suppressed = true;
        if (interruption == 8) r.in.chamberHeavy = false;
        r.Sample(chamber);
        saved.generation = r.in.generation;
        r.in = saved;
        r.Hold(chamber, 20);
        r.Strike();
        CHECK(r.requests.empty() && r.cues == 1);
        r.Arm();
        CHECK(r.cues == 2);
        r.Strike();
        CHECK(r.requests.size() == 1 && r.requests[0].intent == melee::Intent::Heavy);
    }
    {
        ChamberRun r;
        r.Arm();
        r.Sample({10, 0, 0});
        r.Hold(chamber, 20);
        r.Strike();
        CHECK(r.requests.empty());
    }
    {
        ChamberRun r;
        r.Hold(rest, 4);
        r.Hold(chamber, 5);
        auto frozen = r.in;
        for (int i = 0; i < 100; ++i) CHECK(!r.gestures.Step(frozen));
        CHECK(!r.gestures.HeavyArmedNow() && r.gestures.HeavyState() == melee::HeavyChamber::Phase::Holding);
        r.in.hands[1].motion.bodyForward = {std::numeric_limits<double>::quiet_NaN(), 0, -1};
        r.Sample(chamber);
        CHECK(r.gestures.HeavyState() == melee::HeavyChamber::Phase::Release);
    }
    {
        ChamberRun r;
        r.Arm();
        for (int i = 1; i <= 8; ++i) r.Sample({chamber.x + i * r.dt * 4, chamber.y, chamber.z});
        CHECK(r.requests.empty()); // side swipe is not the deliberately forward heavy strike
    }
    {
        auto motion = ChamberInput().hands[1].motion;
        motion.position = chamber;
        CHECK(melee::HeavyChamber::InZone(motion));
        motion.bodyForward = {1, 0, 0};
        motion.position = {0.24, -0.10, 0.3};
        CHECK(melee::HeavyChamber::InZone(motion)); // follows torso yaw, not world heading
        motion.position = {0.24, -0.10, -0.3};
        CHECK(!melee::HeavyChamber::InZone(motion)); // left hand / other side is not the zone
    }
    for (double hz : {31.0, 72.0, 90.0}) {
        ChamberRun r;
        r.dt = 1.0 / hz;
        r.Hold(rest, 4);
        r.Enter();
        r.Hold(chamber, static_cast<int>(0.30 * hz));
        CHECK(r.cues == 0); // time threshold, not a fake-headset frame count
        r.Hold(chamber, static_cast<int>(0.20 * hz));
        CHECK(r.cues == 1);
        r.Strike();
        CHECK(r.requests.size() == 1 && r.requests[0].intent == melee::Intent::Heavy);
    }
    {
        ChamberRun r;
        r.Arm();
        r.dt = 0.005;
        r.Sample(chamber);
        r.Sample({chamber.x, chamber.y, chamber.z - 0.15}); // 30 m/s, below the 0.5 m jump cutoff
        CHECK(r.gestures.HeavyState() == melee::HeavyChamber::Phase::Release);
        CHECK(r.requests.empty());
    }
    {
        ChamberRun r;
        r.Hold(rest, 4);
        for (int i = 1; i <= 8; ++i) r.Sample({rest.x, rest.y, rest.z - i * r.dt * 4});
        CHECK(r.requests.size() == 1 && r.requests[0].intent == melee::Intent::Light);
        CHECK(r.cues == 0); // ordinary forward weapon swing outside the chamber stays light
    }
    {
        ChamberRun r;
        r.in.hands[0].available = true;
        r.in.hands[0].motion.bodyForward = {0, 0, -1};
        r.Arm();
        for (int i = 1; i <= 8; ++i) {
            r.in.hands[0].motion.now = r.time;
            r.in.hands[0].motion.position = {-0.3, -0.3, -i * r.dt * 4};
            r.Sample({chamber.x, chamber.y, chamber.z - i * r.dt * 4});
        }
        CHECK(r.requests.size() == 1 && r.requests[0].intent == melee::Intent::Heavy);
    }
    // The physical rules (ME1's since 2026-11-05, ME2's since 2026-11-17) are exactly ME1's two bare detectors as
    // game/Throwables ran them: the same steps, refusals and strikes on any motion, suppression, tracking loss and jump.
    {
        namespace thr = mevr::ik::throwing;
        melee::Gestures g;
        std::array<thr::SwingDetector, 2> bare;
        thr::SwingParams params;
        params.cooldown = 0.8;
        thr::SwingParams gun = params;
        gun.minSpeed = 3.5;  // the gun hand's own speed, as ME1's right hand with the weapon drawn
        unsigned seed = 12345u;
        auto next = [&seed] { seed = seed * 1103515245u + 12345u; return static_cast<double>((seed >> 8) & 0xFFFF) / 65535.0; };
        int strikes = 0, refusals = 0, both = 0;
        mevr::ik::Vec3 pos[2] = {{-0.2, -0.3, -0.3}, {0.2, -0.3, -0.3}};
        mevr::ik::Vec3 vel[2] = {{0, 0, 0}, {0, 0, 0}};
        double t = 0.0;
        for (int step = 0; step < 6000; ++step) {
            t += 1.0 / 90.0;
            if (step % 97 == 13) t += 0.3;  // a gap: the history starts over
            melee::Input in;
            in.rules = melee::Rules::Physical;
            in.allowed = step % 1500 != 777;  // inactive now and then (ME1's Inactive)
            for (int h = 0; h < 2; ++h) {
                // Bursts of fast strokes in random directions, rests between them.
                if (step % 40 == 0) {
                    double const fast = next() < 0.6 ? 5.0 * next() : 0.0;
                    vel[h] = {fast * (next() - 0.5), fast * (next() - 0.5), -fast * next()};
                }
                pos[h] = pos[h] + vel[h] * (1.0 / 90.0);
                if (step % 40 == 25) pos[h] = {h ? 0.2 : -0.2, -0.3, -0.3};
                if (step % 503 == 101) pos[h] = pos[h] + mevr::ik::Vec3{1.0, 0, 0};  // a tracking jump
                auto& hand = in.hands[static_cast<std::size_t>(h)];
                hand.motion.now = t;
                hand.motion.tracked = step % 211 != 50;
                hand.motion.position = pos[h];
                hand.motion.bodyForward = {0, 0, -1};
                hand.motion.suppressed = (step / 60) % 5 == h;
                hand.params = h == 1 && (step / 300) % 2 == 0 ? gun : params;
                hand.available = false;  // the staged rules' fields mean nothing here
                hand.neutral = false;
            }
            auto const strokes = g.StepAll(in);
            for (int h = 0; h < 2; ++h) {
                auto const& hand = in.hands[static_cast<std::size_t>(h)];
                thr::SwingStep expected;
                if (!in.allowed) {
                    bare[static_cast<std::size_t>(h)].Reset();
                } else {
                    expected = bare[static_cast<std::size_t>(h)].Update(hand.motion, *hand.params);
                }
                auto const& got = strokes.hands[static_cast<std::size_t>(h)];
                CHECK(got.strike == expected.strike && got.discontinuous == expected.discontinuous && got.speed == expected.speed &&
                    got.travel == expected.travel && got.refused == expected.refused);
                CHECK(strokes.requests[static_cast<std::size_t>(h)].has_value() == expected.strike);
                if (expected.strike) {
                    ++strikes;
                    CHECK(strokes.requests[static_cast<std::size_t>(h)]->hand == h &&
                        strokes.requests[static_cast<std::size_t>(h)]->intent == melee::Intent::Light);
                }
                if (expected.refused != nullptr) ++refusals;
            }
            if (strokes.requests[0] && strokes.requests[1]) ++both;
        }
        CHECK(strikes > 10 && refusals > 20);  // the run exercised both
        std::printf("melee physical rules: %d strikes, %d refused strokes, %d steps where both hands struck\n", strikes, refusals, both);
    }
    // Physical: either hand may strike in the same step (the dispatcher's cooldown decides), no neutral pose wanted, and a
    // suppressed stroke is refused and spent: lifting the suppression mid-stroke doesn't strike.
    {
        melee::Gestures g;
        auto in = Input();
        in.rules = melee::Rules::Physical;
        for (auto& hand : in.hands) hand.neutral = false;
        int bothAtOnce = 0;
        for (int tick = 0; tick < 28; ++tick) {
            for (auto& hand : in.hands) {
                hand.motion.now = tick * 0.01;
                hand.motion.position = {tick < 5 ? 0.0 : (tick - 5) * 0.06, 0, 0};
            }
            auto const s = g.StepAll(in);
            if (s.requests[0] && s.requests[1]) ++bothAtOnce;
        }
        CHECK(bothAtOnce == 1);
        melee::Gestures spent;
        auto suppressed = Input();
        suppressed.rules = melee::Rules::Physical;
        int requests = 0, refused = 0;
        for (int tick = 0; tick < 28; ++tick) {
            for (std::size_t h = 0; h < 2; ++h) {
                suppressed.hands[h].motion.now = tick * 0.01;
                suppressed.hands[h].motion.position = {tick < 5 ? 0.0 : (tick - 5) * 0.06, 0, 0};
                suppressed.hands[h].motion.suppressed = tick < 12;
            }
            auto const s = spent.StepAll(suppressed);
            requests += (s.requests[0] ? 1 : 0) + (s.requests[1] ? 1 : 0);
            refused += (s.hands[0].refused != nullptr ? 1 : 0) + (s.hands[1].refused != nullptr ? 1 : 0);
        }
        CHECK(requests == 0 && refused == 2);
    }
    // Switching rules starts over (no history crosses from one rule set to the other).
    {
        melee::Gestures g;
        auto in = Input();
        in.hands[1].available = false;
        auto const staged = Stroke(g, in, 0.0);
        CHECK(staged.size() == 1);
        in.rules = melee::Rules::Physical;
        auto const physical = Stroke(g, in, 1.0);
        CHECK(physical.size() == 1);  // both hands strike in one step; Step gives the first request
    }
    std::printf("melee intent tests: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures;
}
