// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Controllers.hpp"

#include <openxr/openxr.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

// Recorded tracking: the head's two eye views and both controllers (poses and inputs), one frame
// per headset frame, so a session can be played back with no headset worn (plugin/TestLoop).
// Engine-agnostic; the format round-trip and the sampling are tested in tests/Main.cpp.
//
// File format (.mevrrec), little-endian, fixed-size records:
//   header (32 bytes): char magic[8] = "MEVRREC\0"; u32 version (1); u32 headerBytes (32);
//                      u32 frameBytes (252); u32 reserved (0); i64 createdUnixMs
//   frames (frameBytes each, until the end of the file; a partial last frame is dropped):
//     i64 timeNs        from the first frame (the runtime's predicted display times)
//     u32 flags         bit 0: head valid; bit 1: controllers active (the session had input focus)
//     2 x view          left, right: pose (orientation xyzw, position xyz), fov (left, right, up,
//                       down, radians): 11 floats
//     2 x hand          left, right: u32 buttons (bit 0 stick click, 1 primary (A/X), 2 secondary
//                       (B/Y), 3 menu, 4 grip pose valid, 5 aim pose valid); trigger, squeeze,
//                       stick x, stick y; grip pose (7 floats); aim pose (7 floats)
// Poses are in Halcyon's recentered LOCAL app space (the space the views and controllers are
// located in), meters. tools/mevrrec.py prints a file as CSV.
namespace mevr::vr::recording {

inline constexpr char kMagic[8] = {'M', 'E', 'V', 'R', 'R', 'E', 'C', '\0'};
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::size_t kHeaderBytes = 32;
inline constexpr std::size_t kFrameBytes = 252;

struct View {
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    XrFovf fov{};
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
    std::int64_t DurationNs() const { return frames.empty() ? 0 : frames.back().timeNs - frames.front().timeNs; }
};

// Appends the header / one frame to `out`.
void EncodeHeader(std::int64_t createdUnixMs, std::vector<std::uint8_t>& out);
void EncodeFrame(Frame const& frame, std::vector<std::uint8_t>& out);

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

// Streams frames to a file, buffered (Present thread: one small fwrite per frame).
class Writer {
public:
    Writer() = default;
    Writer(Writer const&) = delete;
    Writer& operator=(Writer const&) = delete;
    ~Writer() { Close(); }

    bool Open(std::filesystem::path const& path, std::int64_t createdUnixMs);
    bool Append(Frame const& frame);
    void Close();
    bool IsOpen() const { return file_ != nullptr; }
    std::size_t Frames() const { return frames_; }

private:
    std::FILE* file_ = nullptr;
    std::size_t frames_ = 0;
    std::vector<std::uint8_t> scratch_;
};

} // namespace mevr::vr::recording
