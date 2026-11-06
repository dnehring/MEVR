// SPDX-License-Identifier: MIT
// Offline tests for the swapchain budget (core/vr/SwapchainBudget, plugin/Swapchains): which of ours are freed when the runtime
// refuses a swapchain (XR_ERROR_LIMIT_REACHED), and when a refused owner asks again.

#include "core/vr/SwapchainBudget.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/SwapchainBudgetTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace sb = mevr::vr::swapchain_budget;

void TestChoose() {
    std::uint64_t const now = 100'000;
    // The user's 2026-11-06 session at the rail's refusal: the HUD shown this frame, the main menu closed 40 s ago, the
    // calibration's board never shown (refused itself), its panel closed 6 s ago, the power hint 2 s ago.
    std::vector<sb::Live> const live{
        {now - 200'000, now, false},          // 0 HUD panel: shown now
        {now - 90'000, now - 40'000, false},  // 1 main menu: idle 40 s
        {now - 60'000, now - 6'000, true},    // 2 calibration panel: idle 6 s (Essential, but idle)
        {now - 50'000, now - 2'000, false},   // 3 power hint: idle 2 s
        {now - 300, 0, false},                // 4 just made, never shown: not idle yet
        {now - 30'000, now - 500, false},     // 5 reticle: shown half a second ago
    };
    // One refused create: the least recently shown idle one.
    CHECK((sb::Choose(live, now, 1, false) == std::vector<std::size_t>{1}));
    // Three: all the idle ones, oldest first; the shown, the just-made and the half-second ones are kept.
    CHECK((sb::Choose(live, now, 3, false) == std::vector<std::size_t>{1, 2, 3}));
    CHECK((sb::Choose(live, now, 6, false) == std::vector<std::size_t>{1, 2, 3}));
    // Nothing refused, nothing freed.
    CHECK(sb::Choose(live, now, 0, false).empty());
    // An Essential create (the calibration) may also free ours shown 0.1 s ago or more, after the idle ones: the reticle (0.5 s)
    // and the just-made one (0.3 s), oldest first; never what was shown this very frame (the HUD).
    CHECK((sb::Choose(live, now, 6, true) == std::vector<std::size_t>{1, 2, 3, 5, 4}));
    CHECK((sb::Choose(live, now, 4, true) == std::vector<std::size_t>{1, 2, 3, 5}));
    // An Essential one in use is never freed for another Essential create.
    std::vector<sb::Live> const calibrating{{now - 5'000, now - 300, true}, {now - 5'000, now - 300, false}};
    CHECK((sb::Choose(calibrating, now, 2, true) == std::vector<std::size_t>{1}));
    // Nothing idle and nothing Essential asking: nothing.
    std::vector<sb::Live> const busy{{now - 5'000, now, false}, {now - 5'000, now - 900, false}};
    CHECK(sb::Choose(busy, now, 1, false).empty());
    // Exactly a second idle counts.
    std::vector<sb::Live> const edge{{now - 5'000, now - sb::kIdleMs, false}};
    CHECK((sb::Choose(edge, now, 1, false) == std::vector<std::size_t>{0}));
    // A clock that went backwards (shown "after" now) is in use, not idle.
    std::vector<sb::Live> const future{{now - 5'000, now + 50, false}};
    CHECK(sb::Choose(future, now, 1, false).empty());
}

void TestRetry() {
    std::uint64_t const failed = 50'000;
    // Nothing freed: 2 s later.
    CHECK(!sb::RetryDue(failed, 0, failed));
    CHECK(!sb::RetryDue(failed, 0, failed + sb::kRetryMs - 1));
    CHECK(sb::RetryDue(failed, 0, failed + sb::kRetryMs));
    // Freed at that frame's end (same millisecond or later): at once.
    CHECK(sb::RetryDue(failed, failed, failed + 1));
    CHECK(sb::RetryDue(failed, failed + 16, failed + 17));
    // Freed before the refusal: that room was already taken.
    CHECK(!sb::RetryDue(failed, failed - 1, failed + 10));
}

} // namespace

int RunSwapchainBudgetTests() {
    gFailures = 0;
    TestChoose();
    TestRetry();
    std::printf("swapchain budget tests: %s\n", gFailures == 0 ? "ok" : "FAILED");
    return gFailures;
}
