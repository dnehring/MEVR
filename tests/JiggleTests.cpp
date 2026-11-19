// SPDX-License-Identifier: MIT
// Offline tests for soft-tissue jiggle (core/ik/Jiggle, research/trilogy-jiggle.md section 6): rest stays at rest, steady
// motion moves nothing, a velocity change kicks it and it settles, the closed-form spring is exact (frame-rate independent),
// the soft and hard limits hold, teleports reset, intensity 0 is off.

#include "core/ik/Jiggle.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/JiggleTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace ik = mevr::ik;
namespace jiggle = mevr::ik::jiggle;

// The frame's pivot over time (no rotation); returns the shown vertical offset at each requested sample time.
std::vector<double> Run(jiggle::Settings const& s, std::function<ik::Vec3(double)> path, double dt, double seconds,
                        std::vector<double> const& sampleTimes, double* peak = nullptr, std::vector<double> const* jitter = nullptr) {
    jiggle::State st;
    std::vector<double> samples;
    std::size_t next = 0;
    double t = 0.0;
    double best = 0.0;
    int k = 0;
    while (t <= seconds + 1e-9) {
        jiggle::Input in;
        in.frame.translation = path(t);
        in.dt = k == 0 ? 0.0 : (jitter != nullptr ? (*jitter)[static_cast<std::size_t>(k) % jitter->size()] : dt);
        jiggle::Output const out = jiggle::Step(st, s, in);
        best = std::max(best, std::abs(out.shown.z));
        while (next < sampleTimes.size() && sampleTimes[next] <= t + 1e-9) {
            samples.push_back(out.shown.z);
            ++next;
        }
        ++k;
        double const step = jitter != nullptr ? (*jitter)[static_cast<std::size_t>(k) % jitter->size()] : dt;
        t += step;
    }
    if (peak != nullptr) {
        *peak = best;
    }
    return samples;
}

// A landing: still, then falling at 3 m/s for 0.2 s, then stopped dead (the pivot keeps its height after).
ik::Vec3 Landing(double t) {
    if (t < 0.5) {
        return {0.0, 0.0, 1.0};
    }
    if (t < 0.7) {
        return {0.0, 0.0, 1.0 - 3.0 * (t - 0.5)};
    }
    return {0.0, 0.0, 0.4};
}

void TestRestAndSteady() {
    jiggle::Settings const s = jiggle::Breast();
    double peak = 1.0;
    Run(s, [](double) { return ik::Vec3{0.0, 0.0, 1.0}; }, 1.0 / 72.0, 5.0, {}, &peak);
    CHECK(peak < 1e-9);
    // Walking steadily at 1.4 m/s from the first frame: no kick (the first step only measures the velocity).
    Run(s, [](double t) { return ik::Vec3{1.4 * t, 0.0, 1.0}; }, 1.0 / 72.0, 3.0, {}, &peak);
    CHECK(peak < 1e-6);
}

void TestLanding() {
    jiggle::Settings s = jiggle::Breast();
    s.intensity = 1.0;
    std::vector<double> times;
    for (int i = 0; i <= 300; ++i) {
        times.push_back(i * 0.01);
    }
    double peak = 0.0;
    std::vector<double> const z = Run(s, Landing, 1.0 / 120.0, 3.0, times, &peak);
    CHECK(peak > 0.005);                  // the landing shows
    CHECK(peak <= s.vertical.maxM + 1e-9);  // never past the soft limit
    // Settles: well under 5% of the peak by 1.5 s after the landing.
    CHECK(std::abs(z[220]) < 0.05 * peak);
    // Overshoots (sign changes above 2 mm after the landing): a soft spring wobbles a little, not on and on.
    int crossings = 0;
    for (std::size_t i = 71; i < z.size(); ++i) {
        if (std::abs(z[i]) > 0.002 && std::abs(z[i - 1]) > 0.002 && (z[i] > 0) != (z[i - 1] > 0)) {
            ++crossings;
        }
    }
    CHECK(crossings <= 3);
}

void TestFrameRates() {
    jiggle::Settings s = jiggle::Breast();
    s.intensity = 1.0;
    std::vector<double> times;
    for (int i = 0; i <= 90; ++i) {
        times.push_back(i / 30.0);
    }
    // A walk's bob (2 Hz, 3 cm) under a landing: the same samples at 30, 60, 120 Hz and with jittered steps.
    auto path = [](double t) {
        ik::Vec3 p = Landing(t);
        p.z += 0.03 * std::sin(2.0 * 3.14159265358979323846 * 2.0 * t);
        return p;
    };
    double peak = 0.0;
    std::vector<double> const ref = Run(s, path, 1.0 / 120.0, 3.0, times, &peak);
    std::vector<double> const jitter{1.0 / 60.0 * 1.3, 1.0 / 60.0 * 0.7, 1.0 / 60.0, 1.0 / 60.0 * 1.25, 1.0 / 60.0 * 0.75};
    for (double rate : {30.0, 60.0}) {
        std::vector<double> const other = Run(s, path, 1.0 / rate, 3.0, times);
        double rms = 0.0;
        for (std::size_t i = 0; i < std::min(ref.size(), other.size()); ++i) {
            rms += (ref[i] - other[i]) * (ref[i] - other[i]);
        }
        rms = std::sqrt(rms / static_cast<double>(std::min(ref.size(), other.size())));
        CHECK(rms < 0.10 * peak);
    }
    double jitterPeak = 0.0;
    Run(s, path, 0.0, 3.0, {}, &jitterPeak, &jitter);
    CHECK(std::abs(jitterPeak - peak) < 0.15 * peak);
}

void TestLimitsAndResets() {
    jiggle::Settings s = jiggle::Breast();
    s.intensity = 1.0;
    s.maxAccel = 1000.0;
    // A violent stop (8 m/s to 0): the shown offset stays within the soft limit; the state is clamped.
    double peak = 0.0;
    Run(s, [](double t) { return ik::Vec3{0.0, 0.0, t < 0.3 ? 4.0 - 8.0 * t : 4.0 - 2.4}; }, 1.0 / 72.0, 2.0, {}, &peak);
    CHECK(peak <= s.vertical.maxM + 1e-9);
    // A teleport (2 m in one frame): reset, nothing shown.
    jiggle::State st;
    jiggle::Input in;
    in.frame.translation = {0.0, 0.0, 1.0};
    jiggle::Step(st, s, in);
    in.dt = 1.0 / 72.0;
    jiggle::Step(st, s, in);
    int const resets = st.resets;
    in.frame.translation = {2.0, 0.0, 1.0};
    jiggle::Output const out = jiggle::Step(st, s, in);
    CHECK(st.resets == resets + 1);
    CHECK(ik::Length(out.shown) < 1e-12);
    // Intensity 0: off.
    s.intensity = 0.0;
    Run(s, Landing, 1.0 / 72.0, 3.0, {}, &peak);
    CHECK(peak < 1e-12);
}

} // namespace

int RunJiggleTests() {
    TestRestAndSteady();
    TestLanding();
    TestFrameRates();
    TestLimitsAndResets();
    std::printf("jiggle tests: %d failure(s)\n", gFailures);
    return gFailures;
}
