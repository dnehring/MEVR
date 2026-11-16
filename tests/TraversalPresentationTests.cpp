// SPDX-License-Identifier: MIT
#include "plugin/TraversalPresentation.hpp"
#include <cstdio>
#include <cmath>

int RunTraversalPresentationTests() {
    using namespace mevr;
    host::traversal_presentation::State state;
    int failures = 0;
    auto check = [&](bool ok, char const* message) {
        if (!ok) { std::printf("FAIL traversal presentation: %s\n", message); ++failures; }
    };
    vr::traversal::Presentation p;
    p.flags = vr::traversal::kPresentationActive;
    p.yawOffsetUU = 16384;
    p.recenters = 2; p.sessions = 3; p.sceneGeneration = 4; p.timeMs = 1000;
    check(state.Set(&p, 1000, 2, 3), "fresh view accepted");
    SHORT x = 0, y = 20000;
    state.Apply(x, y, 1010, 2, 3, true);
    check(x == -20000 && std::abs(y) <= 1, "view yaw compensated with inverse movement");
    x = 123; y = 456;
    state.Apply(x, y, 1101, 2, 3, true);
    check(x == 0 && y == 0, "expired view blocks walking");
    auto bad = p; bad.yawOffsetUU = 40000;
    check(!state.Set(&bad, 1001, 2, 3) && state.Active(), "invalid publication retains active ownership");
    x = 123; y = 456;
    state.Apply(x, y, 1010, 3, 3, true);
    check(x == 0 && y == 0, "new recenter refuses old movement basis");
    x = 123; y = 456;
    state.Apply(x, y, 1010, 2, 4, true);
    check(x == 0 && y == 0, "new session refuses old movement basis");
    state.Invalidate();
    x = 123; y = 456;
    state.Apply(x, y, 1010, 2, 3, true);
    check(x == 0 && y == 0, "lifecycle invalidation waits for new publication");
    check(state.Set(&p, 1010, 2, 3), "fresh publication resumes basis");
    x = 123; y = 456;
    state.Apply(x, y, 1010, 2, 3, false);
    check(x == 0 && y == 0, "focus loss blocks walking without losing offset");
    p.flags = 0; p.yawOffsetUU = 0;
    check(state.Set(&p, 1010, 2, 3) && !state.Active(), "fresh inactive publication releases basis");
    x = 123; y = 456;
    state.Apply(x, y, 1010, 2, 3, true);
    check(x == 123 && y == 456, "inactive presentation preserves normal movement");
    p.flags = vr::traversal::kPresentationActive;
    check(state.Set(&p, 1010, 2, 3), "active zero angle still owns freshness");
    check(state.Set(nullptr, 9000, 99, 99) && !state.Active(), "explicit clear always releases ownership");
    return failures;
}
