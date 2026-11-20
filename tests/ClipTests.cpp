// SPDX-License-Identifier: MIT
// Offline tests for our generated clips (core/ik/Clip, tools/motion/README.md's mevr-clip format): loading by the retarget's
// map, frames slerped, the loop wrapping, the root's travel taken out (sway and height kept), malformed files refused.
// A made-up three-bone clip (no game data).

#include "core/Json.hpp"
#include "core/ik/Clip.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ClipTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace ik = mevr::ik;
namespace clip = mevr::ik::clip;
namespace json = mevr::json;

constexpr double kPi = 3.14159265358979323846;

// Bones Root, Pelvis, Head; the map drives Pelvis and Head. Frame k: Pelvis turned k*10 degrees about Z, Head fixed, the root
// walking 10 units per frame along X with a sideways sway of 2 sin(k) and a height of 100 + k.
std::string MakeClip(int frames, int loopStart, int loopEnd) {
    std::string out = R"({"format": "mevr-clip", "version": 1, "name": "test", "fps": 20, "frames": )" + std::to_string(frames) +
                      R"(, "skeleton": {"bones": ["Root", "Pelvis", "Head"], "parents": [-1, 0, 1]}, "loop": {"start": )" +
                      std::to_string(loopStart) + R"(, "end": )" + std::to_string(loopEnd) +
                      R"(}, "retarget": {"map": [["Pelvis", "Hips", null, null], ["Head", "Head", null, null]]}, "clip": [)";
    for (int k = 0; k < frames; ++k) {
        double const half = k * 10.0 * kPi / 180.0 / 2.0;
        char frame[512];
        std::snprintf(frame, sizeof(frame), R"(%s{"root": [%.6f, %.6f, %.6f], "local": [[0,0,0,1], [0,0,%.9f,%.9f], [0,0,0,1]]})",
                      k == 0 ? "" : ", ", 10.0 * k, 2.0 * std::sin(static_cast<double>(k)), 100.0 + k, std::sin(half), std::cos(half));
        out += frame;
    }
    return out + "]}";
}

double YawDeg(ik::Quat q) {
    return 2.0 * std::atan2(q.z, q.w) * 180.0 / kPi;
}

void TestLoadAndSample() {
    std::string error;
    auto const parsed = json::Parse(MakeClip(10, 2, 8), &error);
    CHECK(parsed.has_value());
    auto const c = clip::Load(*parsed, &error);
    CHECK(c.has_value());
    if (!c) {
        std::printf("  %s\n", error.c_str());
        return;
    }
    CHECK(c->bones.size() == 2 && c->bones[0] == "Pelvis" && c->bones[1] == "Head");
    CHECK(c->frames == 10 && c->loopStart == 2 && c->loopEnd == 8);
    CHECK(std::abs(c->LoopSeconds() - 0.3) < 1e-9);
    CHECK(std::abs(c->TravelPerLoop() - std::hypot(60.0, 2.0 * std::sin(8.0) - 2.0 * std::sin(2.0))) < 1e-6);
    // Half way between frames 3 and 4 (1.5 frames into the loop, a quarter of it): the frames' 35 degrees, closed by a quarter
    // of the loop's mismatch (its last frame at 80 degrees, its first at 20: -60), so 20; the height 103.5 likewise by a quarter
    // of 102 - 108.
    clip::Sample const s = clip::At(*c, 1.5 / 20.0);
    CHECK(std::abs(YawDeg(s.locals[0]) - 20.0) < 1e-6);
    CHECK(std::abs(YawDeg(s.locals[1])) < 1e-9);
    CHECK(std::abs(s.root.z - 102.0) < 1e-6);
    // The travel is gone: x stays near 0 through the loop (the line is exactly the frames' straight travel here).
    for (int i = 0; i <= 12; ++i) {
        clip::Sample const t = clip::At(*c, i * 0.025);
        CHECK(std::abs(t.root.x) < 1e-6);
    }
    // Looping: one loop later the same pose.
    clip::Sample const again = clip::At(*c, 1.5 / 20.0 + c->LoopSeconds());
    CHECK(std::abs(YawDeg(again.locals[0]) - 20.0) < 1e-6);
    CHECK(std::abs(again.root.z - 102.0) < 1e-6);
    // The loop closed: just before the wrap the pose is the loop's first (no jump for the hair to feel).
    clip::Sample const end = clip::At(*c, c->LoopSeconds() - 1e-7);
    clip::Sample const begin = clip::At(*c, 0.0);
    CHECK(std::abs(YawDeg(end.locals[0]) - YawDeg(begin.locals[0])) < 1e-3);
    CHECK(std::abs(end.root.z - begin.root.z) < 1e-3 && std::abs(end.root.y - begin.root.y) < 1e-3);
    // The start of the loop: frame 2's sway relative to its own place on the line (0), its height.
    clip::Sample const start = clip::At(*c, 0.0);
    CHECK(std::abs(start.root.y) < 1e-9 && std::abs(start.root.z - 102.0) < 1e-9);
}

void TestRefusals() {
    std::string error;
    CHECK(!clip::Load(*json::Parse(R"({"format": "other"})"), &error).has_value());
    std::string bad = MakeClip(4, 0, 3);
    bad.replace(bad.find("\"Head\", \"Head\""), 14, "\"Neck\", \"Head\"");  // a mapped bone the skeleton lacks
    CHECK(!clip::Load(*json::Parse(bad), &error).has_value());
    CHECK(error.find("Neck") != std::string::npos);
    // A loop outside the clip falls back to the whole clip.
    auto const whole = clip::Load(*json::Parse(MakeClip(5, 3, 9)), &error);
    CHECK(whole.has_value() && whole->loopStart == 0 && whole->loopEnd == 4);
}

} // namespace

int RunClipTests() {
    TestLoadAndSample();
    TestRefusals();
    std::printf("clip tests: %d failure(s)\n", gFailures);
    return gFailures;
}
