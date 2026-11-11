// SPDX-License-Identifier: MIT
// Offline tests for where sounds come from on foot (research/69 A1; core/vr/SoundPlace): the setting's words, who of the
// people near the player is named (the conversation's 8 m rule, kept to 10), the speaking first in a crowd, the loudness for
// the distance, and forgetting those who left.

#include "core/vr/SoundPlace.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/SoundPlaceTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace sp = mevr::vr::sound_place;
namespace cw = mevr::vr::conversation_world;

bool Close(double a, double b, double tolerance = 1e-3) {
    return std::abs(a - b) <= tolerance;
}

sp::Candidate At(std::uint64_t id, double metres) {
    return {id, {metres * 100.0, 0.0, 170.0}};
}

void TestParseOnOff() {
    CHECK(sp::ParseOnOff("1", false));
    CHECK(sp::ParseOnOff(" On ", false));
    CHECK(sp::ParseOnOff("spatial", false));
    CHECK(!sp::ParseOnOff("0", true));
    CHECK(!sp::ParseOnOff("off", true));
    CHECK(!sp::ParseOnOff("game", true));
    CHECK(sp::ParseOnOff("", true));          // missing: the fallback
    CHECK(!sp::ParseOnOff("sideways", false));
}

void TestWithinReach() {
    sp::Overheard overheard;
    sp::Limits const limits;
    cw::Vec3 const ear{0.0, 0.0, 170.0};
    std::vector<sp::Candidate> nearby = {At(1, 1.5), At(2, 7.0), At(3, 9.0)};
    std::vector<sp::Pick> picks = overheard.Step(ear, nearby, 0.0, limits);
    CHECK(picks.size() == 2);                       // 9 m is past the 8 m a voice is placed within
    CHECK(picks.size() == 2 && picks[0].id == 1);   // the nearest first
    CHECK(picks.size() == 2 && picks[1].id == 2);
    CHECK(picks.size() == 2 && Close(picks[0].gain, 1.0));  // full within 2 m
    // 7 m: 3 dB a doubling past 2 m, 3 * log2(3.5) = 5.42 dB quieter.
    CHECK(picks.size() == 2 && Close(20.0 * std::log10(picks[1].gain), -3.0 * std::log2(3.5), 1e-2));
    CHECK(picks.size() == 2 && Close(picks[0].at.x, 150.0) && Close(picks[0].at.z, 170.0));
    CHECK(overheard.Known() == 3);
}

void TestKeptWhileSpeaking() {
    // Once placed, a person stays placed until past the keep radius (10 m): a step back mid-line changes nothing.
    sp::Overheard overheard;
    sp::Limits const limits;
    cw::Vec3 const ear{0.0, 0.0, 170.0};
    std::vector<sp::Candidate> nearby = {At(1, 7.5)};
    CHECK(overheard.Step(ear, nearby, 0.0, limits).size() == 1);
    nearby = {At(1, 9.5)};
    CHECK(overheard.Step(ear, nearby, 0.1, limits).size() == 1);  // kept
    nearby = {At(1, 10.5)};
    CHECK(overheard.Step(ear, nearby, 0.2, limits).empty());       // past the keep radius
    nearby = {At(1, 9.5)};
    CHECK(overheard.Step(ear, nearby, 0.3, limits).empty());       // not again until within 8 m
    // The loudness never goes past the floor (6 dB) however far within the keep radius.
    nearby = {At(1, 7.9)};
    std::vector<sp::Pick> const picks = overheard.Step(ear, nearby, 0.4, limits);
    CHECK(picks.size() == 1 && picks[0].gain >= std::pow(10.0, -6.0 / 20.0) - 1e-9);
}

void TestSpeakersFirstInACrowd() {
    sp::Overheard overheard;
    sp::Limits limits;
    limits.max = 2;
    cw::Vec3 const ear{0.0, 0.0, 170.0};
    std::vector<sp::Candidate> const nearby = {At(1, 1.0), At(2, 2.0), At(3, 3.0), At(4, 6.0)};
    std::vector<sp::Pick> picks = overheard.Step(ear, nearby, 0.0, limits);
    CHECK(picks.size() == 2 && picks[0].id == 1 && picks[1].id == 2);  // nobody has spoken: the nearest
    overheard.Heard(4, 0.05);  // the hook placed one of person 4's sources: they speak
    picks = overheard.Step(ear, nearby, 0.1, limits);
    CHECK(picks.size() == 2 && picks[0].id == 4 && picks[0].speaking);  // the speaker first, though furthest
    CHECK(picks.size() == 2 && picks[1].id == 1 && !picks[1].speaking);
    picks = overheard.Step(ear, nearby, 2.2, limits);                       // 2.15 s since: no longer counted as speaking
    CHECK(picks.size() == 2 && picks[0].id == 1 && picks[1].id == 2);
    overheard.Heard(99, 2.3);  // someone not near: nothing to remember
    CHECK(overheard.Known() == 4);
}

void TestForgetsWhoLeft() {
    sp::Overheard overheard;
    sp::Limits const limits;
    cw::Vec3 const ear{0.0, 0.0, 170.0};
    std::vector<sp::Candidate> nearby = {At(1, 7.5), At(2, 3.0)};
    overheard.Step(ear, nearby, 0.0, limits);
    CHECK(overheard.Known() == 2);
    nearby = {At(2, 3.0)};
    overheard.Step(ear, nearby, 0.1, limits);
    CHECK(overheard.Known() == 1);  // person 1 left the candidates: forgotten
    nearby = {At(1, 9.5), At(2, 3.0)};
    std::vector<sp::Pick> const picks = overheard.Step(ear, nearby, 0.2, limits);
    CHECK(picks.size() == 1 && picks[0].id == 2);  // back at 9.5 m: a new arrival, not kept
    overheard.Clear();
    CHECK(overheard.Known() == 0);
    nearby = {{0, {100.0, 0.0, 170.0}}};
    CHECK(overheard.Step(ear, nearby, 0.3, limits).empty());  // id 0 is nobody
}

void TestReach() {
    sp::Limits const limits;
    CHECK(sp::CandidateReachUnits(limits) >= limits.voice.keepUnits);
    sp::Limits wide;
    wide.voice.withinUnits = 2000.0;
    wide.voice.keepUnits = 500.0;  // a keep radius under the reach is the reach
    CHECK(sp::CandidateReachUnits(wide) >= 2000.0);
}

} // namespace

int RunSoundPlaceTests() {
    TestParseOnOff();
    TestWithinReach();
    TestKeptWhileSpeaking();
    TestSpeakersFirstInACrowd();
    TestForgetsWhoLeft();
    TestReach();
    std::printf("sound place tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
