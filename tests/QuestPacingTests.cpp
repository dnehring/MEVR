// SPDX-License-Identifier: MIT
#include "core/vr/QuestPacing.hpp"
#include <cstdio>
#include <limits>
#include <vector>

namespace {
namespace qp = mevr::vr::quest_pacing;
int gFailures = 0;
void Check(bool ok, int line) {
    if (!ok) { std::printf("FAIL QuestPacingTests.cpp:%d\n", line); ++gFailures; }
}
#define CHECK(x) Check((x), __LINE__)
struct Feed {
    qp::Detector detector;
    double time = 1;
    qp::Result Frame(double dt = 1.0 / 36, double work = .003, bool eligible = true, std::uint64_t level = 1, unsigned tries = 3) {
        time += dt;
        return detector.Step({time, dt, work, 1.0 / 72, eligible, level}, tries);
    }
    std::vector<qp::Event> Run(int frames, double dt = 1.0 / 36, double work = .003, bool eligible = true, std::uint64_t level = 1, unsigned tries = 3) {
        std::vector<qp::Event> events;
        for (int i = 0; i < frames; ++i) {
            auto const r = Frame(dt, work, eligible, level, tries);
            if (r.event != qp::Event::None) events.push_back(r.event);
        }
        return events;
    }
};
void Detection() {
    Feed f;
    CHECK(f.Run(108).empty()); // less than 3 s of timestamped evidence
    auto r = f.Frame();
    CHECK(r.event == qp::Event::Entering);
    CHECK(r.rate > .499 && r.rate < .501);
    CHECK(r.workP90 < .5);
    r = f.Frame();
    CHECK(r.event == qp::Event::Recovering && r.tries == 1);
    CHECK(f.Run(350).empty()); // 9.72 s: cooldown cannot be bypassed
    CHECK(f.Run(20).size() == 1);
    auto events = f.Run(800);
    CHECK(events == std::vector<qp::Event>({qp::Event::Recovering, qp::Event::GaveUp}));
    CHECK(f.Run(1000).empty()); // one give-up, no further attempts
    events = f.Run(450, 1.0 / 72);
    CHECK(events == std::vector<qp::Event>({qp::Event::Recovered}));
    // Replenish only on a known new level; same level retains its exhausted budget.
    events = f.Run(200);
    CHECK(events == std::vector<qp::Event>({qp::Event::Entering}));
    events = f.Run(200, 1.0 / 36, .003, true, 2);
    CHECK(events == std::vector<qp::Event>({qp::Event::Entering, qp::Event::Recovering}));
}
void Exclusions() {
    Feed fast, busy, capped, load, focus, sparse;
    CHECK(fast.Run(500, 1.0 / 72).empty());
    CHECK(busy.Run(500, 1.0 / 36, .008).empty());
    CHECK(capped.Run(500, 1.0 / 36, .003, false).empty());
    CHECK(load.Run(90).empty());
    load.Frame(.251);
    CHECK(load.Run(108).empty()); // slow load poisons the entire next 3 s
    CHECK(load.Frame().event == qp::Event::Entering);
    CHECK(focus.Run(90).empty());
    focus.Frame(1.0 / 36, .003, false);
    CHECK(focus.Run(108).empty());
    CHECK(focus.Frame().event == qp::Event::Entering);
    CHECK(sparse.Run(90).empty());
    sparse.detector.Step({sparse.time, .02, std::numeric_limits<double>::quiet_NaN(), .013, true, 1}, 3);
    CHECK(sparse.Run(108).empty());
    Feed p90;
    // 20% heavy frames defeat the p90 gate despite a small median.
    for (int i = 0; i < 500; ++i) CHECK(p90.Frame(1.0 / 36, i % 5 == 0 ? .009 : .002).event == qp::Event::None);
    Feed justAbove;
    CHECK(justAbove.Run(300, 1.0 / (72 * .61)).empty());
}
void ForcedAndCooldown() {
    qp::Detector d;
    qp::Sample s{}; s.now = 1;
    CHECK(d.Step(s, 2, true).event == qp::Event::Entering);
    s.now = 1.01;
    CHECK(d.Step(s, 2, true).event == qp::Event::Recovering);
    s.now = 2; s.level = 2; // changing levels preserves the cooldown
    CHECK(d.Step(s, 2, true).event == qp::Event::Entering);
    s.now = 10;
    CHECK(d.Step(s, 2, true).event == qp::Event::None);
    s.now = 11.02;
    CHECK(d.Step(s, 2, true).event == qp::Event::Recovering);
    d.Suspend();
    s.now = 12; s.level = 0;
    CHECK(d.Step(s, 2, true).event == qp::Event::None); // unknown world cannot grant attempts
    s.now = 22;
    CHECK(d.Step(s, 2, true).event == qp::Event::Recovering);
    s.now = 33;
    CHECK(d.Step(s, 2, true).event == qp::Event::GaveUp);
}
void FrameOwnership() {
    // Fake runtime contract: both render paths, load gap, failed begin, live option change and STOPPING.
    // A prediction is assigned once on wait and remains the one ended, even if several seconds old.
    qp::FrameSlot slot;
    int waits = 0, begins = 0, ends = 0, prediction = 0, ended = 0;
    auto wait = [&] { CHECK(!slot.Pending()); slot.Waited(); prediction = ++waits; };
    auto begin = [&](bool success) { CHECK(slot.NeedsBegin()); if (success) { slot.Begun(); ++begins; } };
    auto present = [&] {
        CHECK(slot.Pending());
        if (slot.NeedsBegin()) begin(true);
        ended = prediction; ++ends; slot.Clear();
        CHECK(ends == begins);
    };
    wait(); begin(true); // full: N begins before either number of scene Draws
    CHECK(!slot.NeedsBegin());
    present(); CHECK(ended == 1);
    wait(); begin(false); CHECK(slot.NeedsBegin()); // no second wait after a failed begin
    present(); CHECK(waits == 2 && begins == 2 && ended == 2);
    wait(); begin(true); // loading gap / switch back to halcyon: finish the existing ticket
    CHECK(slot.state == qp::FrameSlot::State::Begun);
    present(); CHECK(ended == 3);
    wait(); begin(true); slot.Clear(); --begins; // STOPPING cancels this session's outstanding begin
    CHECK(!slot.Pending());
    wait(); CHECK(slot.NeedsBegin()); present(); CHECK(ended == 5);
}
} // namespace
int RunQuestPacingTests() {
    Detection(); Exclusions(); ForcedAndCooldown(); FrameOwnership();
    CHECK(qp::Actions("auto", false).empty()); // PS VR2 default
    CHECK(qp::Actions("auto", true).size() == 3);
    CHECK(qp::Actions("off", true).empty());
    CHECK(qp::Actions("refresh,performance,refresh", false) == std::vector<qp::Action>({qp::Action::Refresh, qp::Action::Performance}));
    CHECK(qp::Actions("bogus", true).empty());
    CHECK(qp::Actions("refresh,", true).empty());
    return gFailures;
}
