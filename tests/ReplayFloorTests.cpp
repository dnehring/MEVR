// SPDX-License-Identifier: MIT
// The replay STAGE-eye gap (research/le2-vrik.md, closed 2026-11-19): a replay substitutes the recorded head where Halcyon
// locates its views, but the host's floor sample (plugin/XrHost MeasureFloor) read the runtime's own head over STAGE, so a
// recorded crouch never reached posture, floor lock, body scale or the holsters (the fake headset's head stands still at
// 1.7526 m). vr::recording::ReplayedFloor is the rule the replay now drives the sample by, and the recording's version 2
// carries the host's floor sample with each frame (Recording::floors, SampleFloor).

#include "core/vr/PoseRecording.hpp"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/ReplayFloorTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

bool Near(double a, double b, double tolerance = 1e-5) {
    return std::abs(a - b) <= tolerance;
}

using namespace mevr::vr::recording;
using mevr::vr::floor::Source;

Frame HeadAt(std::int64_t timeNs, float y, float z = 0.0f) {
    Frame frame;
    frame.timeNs = timeNs;
    frame.headValid = true;
    frame.views[0].pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {-0.032f, y, z}};
    frame.views[1].pose = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.032f, y, z}};
    return frame;
}

FloorSample Floor(float eye, Source source = Source::Stage) {
    FloorSample sample;
    sample.valid = true;
    sample.source = source;
    sample.eyeHeight = eye;
    return sample;
}

FloorSample const kNone{};

// The live sample moved by the replayed head's height over the live head's: the fake headset's 1.7526 m, its head at the
// app space's origin (Halcyon recenters there), a synthetic plan's standing head at 0.
void TestMovedByTheHead() {
    FloorSample const live = Floor(1.7526f);
    // Standing: unchanged.
    FloorSample out = ReplayedFloor(HeadAt(0, 0.0f), kNone, live, 0.0f);
    CHECK(out.valid && out.source == Source::Stage && Near(out.eyeHeight, 1.7526));
    // A 0.45 m crouch (posture-walk.ps1's): 1.30 m over the floor, as the view is drawn.
    out = ReplayedFloor(HeadAt(0, -0.45f), kNone, live, 0.0f);
    CHECK(out.valid && Near(out.eyeHeight, 1.3026));
    // The owner's reach (make-walk-recording.py's `reach`): 0.18 m down, 0.33 m ahead; only the height counts here (the
    // lean guard reads the head's horizontal travel from the view itself).
    out = ReplayedFloor(HeadAt(0, -0.18f, -0.33f), kNone, live, 0.0f);
    CHECK(Near(out.eyeHeight, 1.5726));
    // The live head not at the origin (a recenter elsewhere, the runtime's own drift): only the difference counts.
    out = ReplayedFloor(HeadAt(0, 0.10f), kNone, live, 0.25f);
    CHECK(Near(out.eyeHeight, 1.7526 - 0.15));
    // A recording taller than the live head: higher.
    out = ReplayedFloor(HeadAt(0, 0.12f), kNone, live, 0.0f);
    CHECK(Near(out.eyeHeight, 1.8726));
    // LOCAL_FLOOR stays LOCAL_FLOOR.
    out = ReplayedFloor(HeadAt(0, -0.2f), kNone, Floor(1.6f, Source::LocalFloor), 0.0f);
    CHECK(out.source == Source::LocalFloor && Near(out.eyeHeight, 1.4));
}

// Nothing to move it by: the live sample as it was.
void TestLiveKept() {
    FloorSample const live = Floor(1.7526f);
    Frame noHead = HeadAt(0, -0.5f);
    noHead.headValid = false;
    FloorSample out = ReplayedFloor(noHead, kNone, live, 0.0f);
    CHECK(out.valid && Near(out.eyeHeight, 1.7526));
    out = ReplayedFloor(HeadAt(0, -0.5f), kNone, live, std::nullopt);  // the live head not located
    CHECK(out.valid && Near(out.eyeHeight, 1.7526));
    out = ReplayedFloor(HeadAt(0, -0.5f), kNone, live, std::nanf(""));
    CHECK(Near(out.eyeHeight, 1.7526));
    out = ReplayedFloor(HeadAt(0, -0.5f), kNone, kNone, 0.0f);  // no live floor (the runtime offers none): still none
    CHECK(!out.valid && out.source == Source::None);
}

// A recording with its own floor (version 2): the recorded sample, whatever the live one says.
void TestRecordedFloor() {
    Frame const frame = HeadAt(0, -0.3f);
    FloorSample recorded = Floor(1.569f);  // the owner's 1.869 m less a 0.30 m squat
    FloorSample out = ReplayedFloor(frame, recorded, Floor(1.7526f), 0.0f);
    CHECK(out.valid && out.source == Source::Stage && Near(out.eyeHeight, 1.569));
    out = ReplayedFloor(frame, recorded, kNone, std::nullopt);  // no live floor at all: still the recorded one
    CHECK(out.valid && Near(out.eyeHeight, 1.569));
    recorded.source = Source::None;  // valid without a source: the live one's kind, else STAGE
    CHECK(ReplayedFloor(frame, recorded, Floor(1.7f, Source::LocalFloor), 0.0f).source == Source::LocalFloor);
    CHECK(ReplayedFloor(frame, recorded, kNone, 0.0f).source == Source::Stage);
}

// Version 2's round trip, version 1 still read (no floor), and the floor sampled as the frames are.
void TestFormat() {
    std::vector<std::uint8_t> bytes;
    EncodeHeader(7, bytes);
    Frame const a = HeadAt(0, 0.0f);
    Frame const b = HeadAt(10'000'000, -0.4f);
    Frame const c = HeadAt(20'000'000, -0.4f);
    EncodeFrame(a, Floor(1.869f, Source::LocalFloor), bytes);
    CHECK(bytes.size() == kHeaderBytes + kFrameBytes);
    EncodeFrame(b, Floor(1.469f), bytes);
    EncodeFrame(c, bytes);  // no floor this frame
    Recording rec;
    std::string error;
    CHECK(Decode(bytes.data(), bytes.size(), rec, &error) && rec.frames.size() == 3 && rec.floors.size() == 3);
    if (rec.floors.size() == 3) {
        CHECK(rec.floors[0].valid && rec.floors[0].source == Source::LocalFloor && Near(rec.floors[0].eyeHeight, 1.869));
        CHECK(rec.floors[1].valid && rec.floors[1].source == Source::Stage && Near(rec.floors[1].eyeHeight, 1.469));
        CHECK(!rec.floors[2].valid);
    }
    FloorSample s = SampleFloor(rec, 5'000'000, false);
    CHECK(s.valid && Near(s.eyeHeight, 1.669));
    s = SampleFloor(rec, 15'000'000, false);  // b valid, c not: taken from b
    CHECK(s.valid && Near(s.eyeHeight, 1.469));
    s = SampleFloor(rec, 25'000'000, true);  // looped: halfway between a and b again
    CHECK(s.valid && Near(s.eyeHeight, 1.669));
    Frame f;
    CHECK(Sample(rec, 15'000'000, false, f) && Near(HeadHeight(f), -0.4));

    std::vector<std::uint8_t> v1;
    EncodeHeader(7, v1);
    // A version 1 file (252-byte frames, as the Python generators still write by default): read, no floors.
    std::uint32_t const one = 1;
    std::uint32_t const size1 = static_cast<std::uint32_t>(kFrameBytesV1);
    std::memcpy(v1.data() + 8, &one, 4);
    std::memcpy(v1.data() + 16, &size1, 4);
    for (Frame const& frame : {a, b}) {
        std::vector<std::uint8_t> encoded;
        EncodeFrame(frame, Floor(1.0f), encoded);
        v1.insert(v1.end(), encoded.begin(), encoded.begin() + static_cast<std::ptrdiff_t>(kFrameBytesV1));
    }
    CHECK(Decode(v1.data(), v1.size(), rec, &error) && rec.frames.size() == 2 && rec.floors.empty());
    if (rec.frames.size() == 2) {
        CHECK(Near(HeadHeight(rec.frames[1]), -0.4) && !SampleFloor(rec, 5'000'000, false).valid);
    }
    // Version 2 with version 1's frame size: refused (the floor would read the next frame).
    std::vector<std::uint8_t> bad = v1;
    std::uint32_t const two = 2;
    std::memcpy(bad.data() + 8, &two, 4);
    CHECK(!Decode(bad.data(), bad.size(), rec, &error));
    // An unknown version: refused.
    std::uint32_t const three = 3;
    std::memcpy(bad.data() + 8, &three, 4);
    CHECK(!Decode(bad.data(), bad.size(), rec, &error));
    // Nothing to sample: not valid.
    Recording empty;
    CHECK(!SampleFloor(empty, 0, true).valid);
}

} // namespace

int RunReplayFloorTests() {
    TestMovedByTheHead();
    TestLiveKept();
    TestRecordedFloor();
    TestFormat();
    return gFailures;
}
