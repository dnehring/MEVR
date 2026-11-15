// SPDX-License-Identifier: MIT
// Offline tests for Thespian mode's turn rules (core/vr/Thespian, research/70 section 8 step 4): when a spoken reply is
// taken, when it is not, and what the microphone's mark shows.

#include "core/vr/Thespian.hpp"

#include <cmath>
#include <cstdio>
#include <string>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ThespianTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

namespace th = mevr::vr::thespian;

th::Match Heard(int leader, bool decided, bool end, float coverage, bool byLabel = false) {
    th::Match m;
    m.words = true;
    m.leader = leader;
    m.decided = decided;
    m.reachedEnd = end;
    m.coverage = coverage;
    m.byLabel = byLabel;
    return m;
}

th::Input In(bool wheel, std::uint32_t silentMs, th::Match now = {}, th::Match atEnd = {}, bool other = false) {
    th::Input in;
    in.wheelUp = wheel;
    in.silentMs = silentMs;
    in.now = now;
    in.atEnd = atEnd;
    in.otherSpeaking = other;
    in.dt = 0.011;
    return in;
}

void TestListenOwnership() {
    th::ListenOwnership listens;
    // The game ticks Voice before Thespian. An ordinary or developer-open radio listen observed on foot
    // with no spoken turn must remain routable as a squad command on the next tick.
    listens.Observe(false, false, 100);
    listens.Observe(false, true, 101);
    CHECK(!listens.Owns(0) && !listens.Owns(100) && !listens.Owns(101));
    // During an actual turn, a pending closed stream is not ours; the accepted open stream is.
    listens.Observe(true, false, 200);
    CHECK(!listens.Owns(200));
    listens.Observe(true, true, 200);
    CHECK(listens.Owns(200));
    // Ending the turn or opening a menu preserves its late-final ownership, without swallowing the next radio.
    listens.Observe(false, false, 200);
    listens.Observe(false, true, 300);
    CHECK(listens.Owns(200) && !listens.Owns(300));
    for (int i = 0; i < 12; ++i) listens.Observe(true, true, 400);
    CHECK(listens.Owns(200) && listens.Owns(400)); // repeated state samples must not evict history
}

void TestMenuOwnership() {
    th::State state;
    th::Settings settings;
    th::Step(state, settings, In(true, 0));
    th::Match const full = Heard(1, true, true, 1.0f);
    auto leading = th::Step(state, settings, In(true, 0, full, full));
    CHECK(leading.phase == th::Phase::Leading && leading.commit < 0);
    // A full, confident transcript and elapsed silence would commit here, but a hidden panel cannot select.
    auto hidden = In(true, 2000, full, full);
    hidden.otherUiOwnsInput = true;
    auto blocked = th::Step(state, settings, hidden);
    CHECK(blocked.phase == th::Phase::Off && blocked.commit < 0 && state.phase == th::Phase::Off);
    // Returning requests a new transcript; it must not resume that hidden partial at its commit threshold.
    auto resumed = th::Step(state, settings, In(true, 2000, full, full));
    CHECK(resumed.phase == th::Phase::Armed && resumed.restart && resumed.commit < 0);
    auto empty = th::Step(state, settings, In(true, 2000));
    CHECK(empty.phase == th::Phase::Armed && empty.commit < 0);
}

void TestWheelIdentity() {
    th::WheelIdentity const original{7, 1, 2, 3, 4, 5, 10, 123};
    CHECK(th::SameWheel(original, original));
    auto next = original;
    next.entry = 11; // same [0,1,2] offers and visible slots on the next line
    CHECK(!th::SameWheel(original, next));
    next = original; ++next.sceneGeneration; // pointer reuse after load/travel
    CHECK(!th::SameWheel(original, next));
    next = original; ++next.data;
    CHECK(!th::SameWheel(original, next));
    next = original; ++next.handler;
    CHECK(!th::SameWheel(original, next));
    next = original; ++next.offers; // investigate/back without an entry change
    CHECK(!th::SameWheel(original, next));
    next = original; next.entry = -1;
    CHECK(!th::SameWheel(next, next));
    CHECK(!th::SameWheel({}, {}));
}

void TestTurnRestartBarrier() {
    CHECK(!th::FreshTurnListen(100, 200, 100)); // late final from the prior wheel
    CHECK(!th::FreshTurnListen(100, 200, 150)); // an earlier radio listen is not the new turn
    CHECK(!th::FreshTurnListen(200, 200, 200)); // request coalesced with an already open stream
    CHECK(!th::FreshTurnListen(100, 0, 300)); // stop is not yet observed; start not requested
    CHECK(th::FreshTurnListen(100, 200, 200));
    CHECK(th::FreshTurnListen(100, 200, 300));
    th::State state; state.phase = th::Phase::Committed; state.committed = 1;
    th::Output out; out.phase = th::Phase::Committed; out.commit = 1; out.leader = 1;
    th::RejectSelection(state, out);
    CHECK(state.phase == th::Phase::Off && state.committed == -1);
    CHECK(out.phase == th::Phase::Off && out.commit == -1 && out.leader == -1);
    auto const full = Heard(1, true, true, 1.0f);
    auto const next = th::Step(state, {}, In(true, 2000, full, full));
    CHECK(next.phase == th::Phase::Armed && next.restart && next.commit == -1);
}

void TestStrictness() {
    CHECK(th::ParseStrictness("Method") == th::Strictness::Method);
    CHECK(th::ParseStrictness(" relaxed ") == th::Strictness::Relaxed);
    CHECK(th::ParseStrictness("nonsense") == th::Strictness::Performer);
    th::Match const half = Heard(2, true, false, 0.55f);
    th::Match const little = Heard(2, true, false, 0.3f);
    th::Match const label = Heard(2, true, false, 0.0f, true);
    th::Match const whole = Heard(2, true, true, 0.95f);
    th::Match const undecided = Heard(2, false, true, 1.0f);
    CHECK(th::Allows(th::Strictness::Relaxed, little) && th::Allows(th::Strictness::Relaxed, label));
    CHECK(th::Allows(th::Strictness::Performer, half) && !th::Allows(th::Strictness::Performer, little));
    CHECK(th::Allows(th::Strictness::Performer, label));
    CHECK(th::Allows(th::Strictness::Method, whole) && !th::Allows(th::Strictness::Method, half) && !th::Allows(th::Strictness::Method, label));
    for (th::Strictness const s : {th::Strictness::Relaxed, th::Strictness::Performer, th::Strictness::Method}) {
        CHECK(!th::Allows(s, undecided));  // never on a guess
        CHECK(th::ParseStrictness(th::Name(s)) == s);
    }
}

void TestTurn() {
    th::Settings const settings;
    th::State state;
    // No wheel: off. The wheel comes up: armed, a fresh transcript.
    th::Output out = th::Step(state, settings, In(false, 0));
    CHECK(out.phase == th::Phase::Off && out.mic == th::Mic::Off && out.commit < 0);
    out = th::Step(state, settings, In(true, 5000));
    CHECK(out.phase == th::Phase::Armed && out.restart && out.mic == th::Mic::Armed);
    // Quiet for a long time with nothing heard: nothing happens.
    out = th::Step(state, settings, In(true, 60000));
    CHECK(out.phase == th::Phase::Armed && out.commit < 0 && !out.restart);

    // Speaking, no card decided yet.
    out = th::Step(state, settings, In(true, 0, Heard(1, false, false, 0.1f), Heard(1, false, false, 0.1f)));
    CHECK(out.phase == th::Phase::Hearing && out.mic == th::Mic::Hearing && out.leader < 0);
    // Decided mid-line, still speaking: the row is lit, nothing taken.
    out = th::Step(state, settings, In(true, 50, Heard(1, true, false, 0.6f), Heard(1, true, false, 0.6f)));
    CHECK(out.phase == th::Phase::Leading && out.leader == 1 && out.commit < 0);
    // The line's end reached, 150 ms quiet: not yet (endMs is 200).
    out = th::Step(state, settings, In(true, 150, Heard(1, true, true, 1.0f), Heard(1, true, true, 1.0f)));
    CHECK(out.commit < 0);
    // 200 ms quiet: taken.
    out = th::Step(state, settings, In(true, 200, Heard(1, true, true, 1.0f), Heard(1, true, true, 1.0f)));
    CHECK(out.commit == 1 && out.phase == th::Phase::Committed && out.mic == th::Mic::Understood);
    // Committed: nothing more, whatever is heard, until the wheel has gone down.
    out = th::Step(state, settings, In(true, 5000, Heard(0, true, true, 1.0f), Heard(0, true, true, 1.0f)));
    CHECK(out.commit < 0 && out.phase == th::Phase::Committed && out.leader == 1);
    out = th::Step(state, settings, In(false, 0));
    CHECK(out.phase == th::Phase::Off);
    out = th::Step(state, settings, In(true, 0));
    CHECK(out.phase == th::Phase::Armed && out.restart);
}

void TestPartWay() {
    th::Settings const settings;
    th::State state;
    th::Step(state, settings, In(true, 0));
    // Stopped half-way through a line the matcher is sure of: not at 300 ms, taken at 900.
    th::Match const half = Heard(3, true, false, 0.6f);
    th::Output out = th::Step(state, settings, In(true, 300, half, half));
    CHECK(out.commit < 0 && out.phase == th::Phase::Leading);
    out = th::Step(state, settings, In(true, 900, half, half));
    CHECK(out.commit == 3);

    // Undecided while speaking, decided once stopped (the end's smaller margin): taken at the pause.
    state = {};
    th::Step(state, settings, In(true, 0));
    out = th::Step(state, settings, In(true, 950, Heard(2, false, false, 0.7f), Heard(2, true, false, 0.7f)));
    CHECK(out.commit == 2);

    // Too little of the line for a performer: lit, never taken; the mark says so and listening goes on.
    state = {};
    th::Step(state, settings, In(true, 0));
    th::Match const little = Heard(2, true, false, 0.3f);
    out = th::Step(state, settings, In(true, 1200, little, little));
    CHECK(out.commit < 0 && out.phase == th::Phase::Leading && !out.restart && out.mic == th::Mic::More);
    // ... but relaxed takes it.
    th::Settings relaxed;
    relaxed.strictness = th::Strictness::Relaxed;
    out = th::Step(state, relaxed, In(true, 1200, little, little));
    CHECK(out.commit == 2);

    // Talk that matches nothing: "didn't catch that" at the pause, dropped after the long quiet.
    state = {};
    th::Step(state, settings, In(true, 0));
    th::Match const talk = Heard(-1, false, false, 0.0f);
    out = th::Step(state, settings, In(true, 1000, talk, talk));
    CHECK(out.commit < 0 && out.mic == th::Mic::Unsure && !out.restart);
    out = th::Step(state, settings, In(true, 3600, talk, talk));
    CHECK(out.commit < 0 && out.restart);
}

void TestOtherSpeaking() {
    th::Settings const settings;
    th::State state;
    // The wheel comes up while the other person still talks.
    th::Step(state, settings, In(true, 0, {}, {}, true));
    // A whole line read over them, then quiet: lit, not taken while they speak.
    th::Match const whole = Heard(0, true, true, 1.0f);
    th::Output out = th::Step(state, settings, In(true, 1500, whole, whole, true));
    CHECK(out.commit < 0 && out.phase == th::Phase::Leading && out.leader == 0);
    // Their line ends: the decided line stands and is taken at once (the quiet has lasted).
    out = th::Step(state, settings, In(true, 1600, whole, whole, false));
    CHECK(out.commit == 0);

    // Undecided words heard while they spoke are dropped when their line ends.
    state = {};
    th::Step(state, settings, In(true, 0, {}, {}, true));
    th::Match const noise = Heard(1, false, false, 0.1f);
    out = th::Step(state, settings, In(true, 100, noise, noise, true));
    CHECK(out.commit < 0 && !out.restart);
    out = th::Step(state, settings, In(true, 100, noise, noise, false));
    CHECK(out.restart && out.phase == th::Phase::Armed && out.commit < 0);
}

void TestCommitNotTaken() {
    th::Settings settings;
    th::State state;
    th::Step(state, settings, In(true, 0));
    th::Match const whole = Heard(0, true, true, 1.0f);
    th::Output out = th::Step(state, settings, In(true, 250, whole, whole));
    CHECK(out.commit == 0);
    // The wheel stays up (the game refused): after committedMaxS it listens again with a fresh transcript.
    for (int i = 0; i < 400 && out.phase == th::Phase::Committed; ++i) {
        out = th::Step(state, settings, In(true, 250, whole, whole));
    }
    CHECK(out.phase == th::Phase::Armed && out.restart && out.commit < 0);
}

void TestDoubt() {
    // A line read to its end whose telling word was only heard alike: nothing taken, "Not sure which", and the words are
    // dropped at the pause so the second reading stands alone.
    th::Settings const settings;
    th::State state;
    th::Step(state, settings, In(true, 5000));
    th::Match doubt = Heard(0, false, true, 0.8f);
    doubt.doubtful = true;
    th::Output out = th::Step(state, settings, In(true, 300, doubt, doubt));
    CHECK(out.commit < 0 && out.mic == th::Mic::Hearing && !out.restart);
    out = th::Step(state, settings, In(true, 950, doubt, doubt));
    CHECK(out.commit < 0 && out.restart && out.mic == th::Mic::Doubt);
    // The mark stays while the player reads again; the second reading is taken.
    out = th::Step(state, settings, In(true, 2000));
    CHECK(out.mic == th::Mic::Doubt && out.commit < 0);
    th::Match const clear = Heard(0, true, true, 1.0f);
    out = th::Step(state, settings, In(true, 250, clear, clear));
    CHECK(out.commit == 0);
    // An undecided reading without a doubt is the old "Didn't catch that", kept until the stale time.
    th::State other;
    th::Step(other, settings, In(true, 5000));
    th::Match const vague = Heard(1, false, false, 0.2f);
    out = th::Step(other, settings, In(true, 950, vague, vague));
    CHECK(out.mic == th::Mic::Unsure && !out.restart);
    th::Panel panel;
    panel.mic = static_cast<std::uint8_t>(th::Mic::Doubt);
    CHECK(th::PromptText(panel).starts_with("Not sure which") && std::string(th::MicClass(th::Mic::Doubt)) == "unsure");
}

void TestTones() {
    CHECK(th::ToneOf(0, 0) == th::Tone::Default);
    CHECK(th::ToneOf(1, 0) == th::Tone::Agree && th::ToneOf(3, 0) == th::Tone::Agree);
    CHECK(th::ToneOf(2, 0) == th::Tone::Disagree && th::ToneOf(4, 0) == th::Tone::Disagree);
    CHECK(th::ToneOf(5, 0) == th::Tone::Investigate);
    CHECK(th::ToneOf(1, 1) == th::Tone::Charm && th::ToneOf(2, 2) == th::Tone::Intimidate);
    CHECK(th::ToneOf(1, 4) == th::Tone::Unavailable);
    th::Panel const panel;
    CHECK(panel.size == sizeof(th::Panel));
}

}  // namespace

void TestPanel() {
    th::Panel panel;
    panel.cardCount = 3;
    panel.mainCount = 2;
    std::snprintf(panel.cards[0].label, sizeof panel.cards[0].label, "%s", "Who's <that>?");
    std::snprintf(panel.cards[0].line, sizeof panel.cards[0].line, "%s", "I don't know. Tell me!");
    panel.cards[0].tone = static_cast<std::uint8_t>(th::Tone::Agree);
    panel.cards[0].look = static_cast<std::uint8_t>(th::Look::Leading);
    panel.cards[0].lit = 0b011;  // "I", "don't"
    panel.cards[0].next = 2;
    std::snprintf(panel.cards[1].label, sizeof panel.cards[1].label, "%s", "Goodbye.");
    panel.cards[1].look = static_cast<std::uint8_t>(th::Look::Dim);
    std::snprintf(panel.cards[2].label, sizeof panel.cards[2].label, "%s", "Saren");
    std::snprintf(panel.cards[2].line, sizeof panel.cards[2].line, "%s", "What about Saren?");
    panel.cards[2].tone = static_cast<std::uint8_t>(th::Tone::Investigate);
    std::string const rml = th::RowsRml(panel);
    CHECK(rml.find("class=\"row agree leading\"") != std::string::npos);
    CHECK(rml.find("Who's &lt;that&gt;?") != std::string::npos);
    CHECK(rml.find("<span class=\"said\">I</span><span class=\"said\"> don't</span><span class=\"next\"> know</span><span>. Tell</span><span> me</span>!") != std::string::npos);
    CHECK(rml.find("class=\"row default dim\"") != std::string::npos);
    CHECK(rml.find("<span>Goodbye</span>.") != std::string::npos);  // a menu card: its label is the script
    CHECK(rml.find("<div class=\"divider\"/><div class=\"row investigate idle topic\">") != std::string::npos);
    panel.mic = static_cast<std::uint8_t>(th::Mic::Armed);
    CHECK(th::PromptText(panel) == "Read a line aloud");
    panel.mic = static_cast<std::uint8_t>(th::Mic::Understood);
    CHECK(th::PromptText(panel) == "Who's <that>?");

    // The place: 1 m away toward the speaker, below the line to their head, facing the eyes.
    XrVector3f const eyes{0.0f, 1.7f, 0.0f};
    XrPosef const pose = th::PlacePanel(eyes, {0.0f, 0.0f, -1.0f}, true, {2.0f, 1.7f, 0.0f}, 0.6f);
    float const dx = pose.position.x - eyes.x, dy = pose.position.y - eyes.y, dz = pose.position.z - eyes.z;
    CHECK(std::abs(std::sqrt(dx * dx + dy * dy + dz * dz) - th::kPanelDistanceM) < 1e-3f);
    CHECK(dx > 0.7f && std::abs(dz) < 1e-3f && dy < -0.3f);  // toward the speaker (+x), below the eyes
    // The quad's +Z (its face) points back at the eyes.
    XrQuaternionf const q = pose.orientation;
    XrVector3f const face{2.0f * (q.x * q.z + q.w * q.y), 2.0f * (q.y * q.z - q.w * q.x), 1.0f - 2.0f * (q.x * q.x + q.y * q.y)};
    CHECK(face.x * -dx + face.y * -dy + face.z * -dz > 0.99f * th::kPanelDistanceM);
    // Without a speaker: along the head's flat forward.
    XrPosef const front = th::PlacePanel(eyes, {0.0f, -0.5f, -1.0f}, false, {}, 0.6f);
    CHECK(front.position.z < -0.6f && std::abs(front.position.x) < 1e-3f && front.position.y < eyes.y - 0.3f);
}

int RunThespianTests() {
    // Missing/revoked, old and future render contexts cannot dispatch invisible cards.
    CHECK(!th::InputContextFresh(0, 100));
    CHECK(th::InputContextFresh(100, 100));
    CHECK(th::InputContextFresh(100, 133)); // the null headset's normal frame interval
    CHECK(th::InputContextFresh(100, 200));
    CHECK(!th::InputContextFresh(100, 201));
    CHECK(!th::InputContextFresh(101, 100));
    TestWheelIdentity();
    TestTurnRestartBarrier();
    TestListenOwnership();
    TestMenuOwnership();
    TestPanel();
    TestStrictness();
    TestTurn();
    TestPartWay();
    TestOtherSpeaking();
    TestCommitNotTaken();
    TestDoubt();
    TestTones();
    return gFailures;
}
