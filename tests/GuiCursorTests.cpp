// SPDX-License-Identifier: MIT
// Offline tests for steering the game's GUI cursor onto the pointer's point (core/vr/GuiCursor, research/56 section 6): a
// simulated Flash cursor that moves by injected counts times its own gain, a tick late, clamped to the stage.

#include "core/vr/GuiCursor.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>
#include <optional>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/GuiCursorTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace gc = mevr::vr::gui_cursor;

// The movie's mouse: counts reach it `latency` ticks after they're sent, times `gain`, clamped to 1280 x 1306.
struct Movie {
    float x = 418.3f;
    float y = 265.5f;
    float gain = 0.82f;
    int latency = 1;
    std::deque<gc::Move> queue;
    void Tick(gc::Move const& sent) {
        queue.push_back(sent);
        while (static_cast<int>(queue.size()) > latency) {
            gc::Move const m = queue.front();
            queue.pop_front();
            x = std::clamp(x + static_cast<float>(m.dx) * gain, 0.0f, 1280.0f);
            y = std::clamp(y + static_cast<float>(m.dy) * gain, 0.0f, 1306.0f);
        }
    }
};

// Ticks until the cursor sits within `within` of the target and the steer says so; -1 if not within `limit`.
int Reach(gc::Steer& steer, Movie& movie, float tx, float ty, int limit = 60, float within = 0.7f) {
    for (int tick = 1; tick <= limit; ++tick) {
        gc::Move const move = steer.Step(movie.x, movie.y, tx, ty);
        movie.Tick(move);
        if (std::fabs(movie.x - tx) <= within && std::fabs(movie.y - ty) <= within && steer.OnTarget()) {
            return tick;
        }
    }
    return -1;
}

void TestReach() {
    // The measured gain: from the stale place to list row 3 of the equipment screen (stage (690, 439)) in a few ticks.
    gc::Steer steer;
    Movie movie;
    int const ticks = Reach(steer, movie, 690.0f, 439.0f);
    CHECK(ticks > 0 && ticks <= 12);
    // Held there: nothing more is sent.
    gc::Move const still = steer.Step(movie.x, movie.y, 690.0f, 439.0f);
    CHECK(still.dx == 0 && still.dy == 0);
    // The next target (the shotgun tab, stage (704, 316)).
    CHECK(Reach(steer, movie, 704.0f, 316.0f) > 0);
}

void TestLearnsGain() {
    // A slower mouse than assumed (another sensitivity): it still gets there and learns the gain.
    for (float const real : {0.3f, 2.0f}) {
        gc::Steer steer;
        Movie movie;
        movie.gain = real;
        movie.latency = 2;
        CHECK(Reach(steer, movie, 100.0f, 1200.0f, 120) > 0);
        CHECK(Reach(steer, movie, 1100.0f, 80.0f, 120) > 0);
        CHECK(std::fabs(steer.Gain(0) - real) < real * 0.25f);
        CHECK(std::fabs(steer.Gain(1) - real) < real * 0.25f);
    }
}

void TestNoTarget() {
    gc::Steer steer;
    Movie movie;
    gc::Move const none = steer.Step(movie.x, movie.y, std::nullopt, std::nullopt);
    CHECK(none.dx == 0 && none.dy == 0 && !steer.OnTarget());
    // A move that never shows (the window lost the focus): after the wait, the next is sent.
    gc::Move const first = steer.Step(movie.x, movie.y, 700.0f, 700.0f);
    CHECK(first.dx > 0 && first.dy > 0);
    int quiet = 0;
    gc::Move again{};
    for (int i = 0; i < 10 && again.dx == 0; ++i) {
        again = steer.Step(movie.x, movie.y, 700.0f, 700.0f);  // the movie never moved
        ++quiet;
    }
    CHECK(again.dx > 0 && quiet == gc::Settings{}.waitTicks);
    CHECK(std::fabs(steer.Gain(0) - 0.82f) < 1e-4f);  // nothing learned from a move that never showed
}

} // namespace

int RunGuiCursorTests() {
    gFailures = 0;
    TestReach();
    TestLearnsGain();
    TestNoTarget();
    return gFailures;
}
