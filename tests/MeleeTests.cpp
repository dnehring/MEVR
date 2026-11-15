// SPDX-License-Identifier: MIT
#include "core/ik/Melee.hpp"
#include <cstdio>
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
    std::printf("melee intent tests: %s\n", failures == 0 ? "ok" : "FAILED");
    return failures;
}
