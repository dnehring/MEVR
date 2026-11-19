// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Controllers.hpp"
#include "core/vr/Floor.hpp"

#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Recorded tracking: the head's two eye views and both controllers (poses and inputs), one frame
// per headset frame, so a session can be played back with no headset worn (plugin/TestLoop).
// Engine-agnostic; the format round-trip and the sampling are tested in tests/Main.cpp, the replayed floor in
// tests/ReplayFloorTests.cpp.
//
// File format (.mevrrec), little-endian, fixed-size records:
//   header (32 bytes): char magic[8] = "MEVRREC\0"; u32 version (2; 1 before 2026-11-19); u32 headerBytes (32);
//                      u32 frameBytes (260; 252 in version 1); u32 reserved (0); i64 createdUnixMs
//   frames (frameBytes each, until the end of the file; a partial last frame is dropped):
//     i64 timeNs        from the first frame (the runtime's predicted display times)
//     u32 flags         bit 0: head valid; bit 1: controllers active (the session had input focus)
//     2 x view          left, right: pose (orientation xyzw, position xyz), fov (left, right, up,
//                       down, radians): 11 floats
//     2 x hand          left, right: u32 buttons (bit 0 stick click, 1 primary (A/X), 2 secondary
//                       (B/Y), 3 menu, 4 grip pose valid, 5 aim pose valid); trigger, squeeze,
//                       stick x, stick y; grip pose (7 floats); aim pose (7 floats)
//     version 2:        u32 floor flags (bit 0 valid; bits 1-2 the source, vr::floor::Source: 1 STAGE, 2 LOCAL_FLOOR);
//                       f32 the eyes' height over the floor (the host's floor sample that frame, plugin/XrHost), meters
// Poses are in Halcyon's recentered LOCAL app space (the space the views and controllers are
// located in), meters. tools/mevrrec.py prints a file as CSV.
namespace mevr::vr::recording {

inline constexpr char kMagic[8] = {'M', 'E', 'V', 'R', 'R', 'E', 'C', '\0'};
inline constexpr std::uint32_t kVersion = 2;
inline constexpr std::size_t kHeaderBytes = 32;
inline constexpr std::size_t kFrameBytesV1 = 252;
inline constexpr std::size_t kFrameBytes = 260;

struct View {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrFovf fov{};
};

// The eyes over the floor as the host measured them with a frame (version 2; a version 1 frame has none). Kept beside the
// frames, not in them, so the replay's per-frame code keeps its layout (Recording::floors, SampleFloor).
struct FloorSample {
    bool valid = false;
    floor::Source source = floor::Source::None;
    float eyeHeight = 0.0f;  // meters
};

struct Frame {
    std::int64_t timeNs = 0;
    bool headValid = false;
    View views[2];
    bool controllersActive = false;
    controllers::Hand hands[2];  // left, right
};

struct Recording {
    std::int64_t createdUnixMs = 0;
    std::vector<Frame> frames;
    std::vector<FloorSample> floors;  // one per frame (version 2), or empty (version 1: no floor samples)
    std::int64_t DurationNs() const { return frames.empty() ? 0 : frames.back().timeNs - frames.front().timeNs; }
};

// Appends the header / one frame (with its floor sample; none: not valid) to `out`.
void EncodeHeader(std::int64_t createdUnixMs, std::vector<std::uint8_t>& out);
void EncodeFrame(Frame const& frame, std::vector<std::uint8_t>& out);
void EncodeFrame(Frame const& frame, FloorSample const& floor, std::vector<std::uint8_t>& out);

// Parses a whole file. False (with `error`) if it isn't a recording or has no frames. A partial last
// frame (the game closed mid-write) is dropped; `truncated` says so.
bool Decode(std::uint8_t const* data, std::size_t size, Recording& out, std::string* error = nullptr,
            bool* truncated = nullptr);
bool Load(std::filesystem::path const& path, Recording& out, std::string* error = nullptr);

// The tracking at `timeNs` after the first frame: positions and FOVs interpolated, orientations
// normalized-lerped, inputs (buttons, triggers, sticks) held from the earlier frame. A pose valid in
// only one of the two frames is taken from that one. With `loop`, times past the end wrap. False when
// `timeNs` is past the end without `loop` (`out` is then the last frame) or there are no frames.
bool Sample(Recording const& recording, std::int64_t timeNs, bool loop, Frame& out);
// The floor sample at `timeNs`, timed as Sample: interpolated between the two frames like the positions (valid in one:
// taken from that one); not valid for a recording without floor samples (version 1).
FloorSample SampleFloor(Recording const& recording, std::int64_t timeNs, bool loop);

// The head's height in the app space: the middle of the two eyes (the VIEW origin), meters.
float HeadHeight(Frame const& frame);

// The floor sample a replay stands in with (plugin/TestLoop for plugin/XrHost's MeasureFloor; research/le2-vrik.md "The
// replay STAGE-eye gap"): the recording's own floor sample where it has one (`recorded`: the player's real height over their
// real floor), else the live sample moved by the replayed head's height over the live head's in the app space (`liveHeadY`:
// both heads in the one space the replay draws in, so a recorded crouch, lean or reach lowers the eyes over the floor by as
// much as it lowers the view), else the live sample unchanged (no replayed head, no live head height, no live floor). Only
// the eye height, validity and source; the caller keeps its counters.
FloorSample ReplayedFloor(Frame const& replayed, FloorSample const& recorded, FloorSample const& live,
                          std::optional<float> liveHeadY);

// Streams frames to a file, buffered (Present thread: one small fwrite per frame).
class Writer {
public:
    Writer() = default;
    Writer(Writer const&) = delete;
    Writer& operator=(Writer const&) = delete;
    ~Writer() { Close(); }

    bool Open(std::filesystem::path const& path, std::int64_t createdUnixMs);
    bool Append(Frame const& frame, FloorSample const& floor = {});
    void Close();
    bool IsOpen() const { return file_ != nullptr; }
    std::size_t Frames() const { return frames_; }

private:
    std::FILE* file_ = nullptr;
    std::size_t frames_ = 0;
    std::vector<std::uint8_t> scratch_;
};

} // namespace mevr::vr::recording
