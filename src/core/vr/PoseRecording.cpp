// SPDX-License-Identifier: MIT
#include "core/vr/PoseRecording.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace mevr::vr::recording {
namespace {

constexpr std::uint32_t kHeadValid = 1u << 0;
constexpr std::uint32_t kControllersActive = 1u << 1;

constexpr std::uint32_t kStickClick = 1u << 0;
constexpr std::uint32_t kPrimary = 1u << 1;
constexpr std::uint32_t kSecondary = 1u << 2;
constexpr std::uint32_t kMenu = 1u << 3;
constexpr std::uint32_t kGripValid = 1u << 4;
constexpr std::uint32_t kAimValid = 1u << 5;

// Little-endian on every Windows target; memcpy keeps it alignment-safe.
template <typename T>
void Put(std::vector<std::uint8_t>& out, T value) {
    std::uint8_t bytes[sizeof(T)];
    std::memcpy(bytes, &value, sizeof(T));
    out.insert(out.end(), bytes, bytes + sizeof(T));
}

void PutPose(std::vector<std::uint8_t>& out, XrPosef const& pose) {
    Put(out, pose.orientation.x);
    Put(out, pose.orientation.y);
    Put(out, pose.orientation.z);
    Put(out, pose.orientation.w);
    Put(out, pose.position.x);
    Put(out, pose.position.y);
    Put(out, pose.position.z);
}

class Reader {
public:
    Reader(std::uint8_t const* data, std::size_t size) : data_(data), size_(size) {}
    template <typename T>
    T Get() {
        T value{};
        if (offset_ + sizeof(T) <= size_) {
            std::memcpy(&value, data_ + offset_, sizeof(T));
        }
        offset_ += sizeof(T);
        return value;
    }
    XrPosef GetPose() {
        XrPosef pose{};
        pose.orientation.x = Get<float>();
        pose.orientation.y = Get<float>();
        pose.orientation.z = Get<float>();
        pose.orientation.w = Get<float>();
        pose.position.x = Get<float>();
        pose.position.y = Get<float>();
        pose.position.z = Get<float>();
        return pose;
    }
    void Skip(std::size_t bytes) { offset_ += bytes; }
    std::size_t Offset() const { return offset_; }

private:
    std::uint8_t const* data_;
    std::size_t size_;
    std::size_t offset_ = 0;
};

float Lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

XrVector3f Lerp(XrVector3f const& a, XrVector3f const& b, float t) {
    return {Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t)};
}

// Normalized lerp along the shorter arc: close enough to slerp between frames ~11 ms apart.
XrQuaternionf Nlerp(XrQuaternionf const& a, XrQuaternionf b, float t) {
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0f) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    XrQuaternionf q{Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)};
    float const n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (n > 1e-6f) {
        q = {q.x / n, q.y / n, q.z / n, q.w / n};
    } else {
        q = a;
    }
    return q;
}

XrPosef Lerp(XrPosef const& a, XrPosef const& b, float t) {
    return {Nlerp(a.orientation, b.orientation, t), Lerp(a.position, b.position, t)};
}

XrFovf Lerp(XrFovf const& a, XrFovf const& b, float t) {
    return {Lerp(a.angleLeft, b.angleLeft, t), Lerp(a.angleRight, b.angleRight, t), Lerp(a.angleUp, b.angleUp, t),
        Lerp(a.angleDown, b.angleDown, t)};
}

// A pose valid in both frames is blended; valid in one, taken from it.
void BlendPose(bool aValid, XrPosef const& a, bool bValid, XrPosef const& b, float t, bool& valid, XrPosef& out) {
    valid = aValid || bValid;
    if (aValid && bValid) {
        out = Lerp(a, b, t);
    } else if (aValid) {
        out = a;
    } else if (bValid) {
        out = b;
    }
}

} // namespace

void EncodeHeader(std::int64_t createdUnixMs, std::vector<std::uint8_t>& out) {
    out.insert(out.end(), std::begin(kMagic), std::end(kMagic));
    Put(out, kVersion);
    Put(out, static_cast<std::uint32_t>(kHeaderBytes));
    Put(out, static_cast<std::uint32_t>(kFrameBytes));
    Put(out, std::uint32_t{0});
    Put(out, createdUnixMs);
}

void EncodeFrame(Frame const& frame, std::vector<std::uint8_t>& out) {
    Put(out, frame.timeNs);
    std::uint32_t const flags = (frame.headValid ? kHeadValid : 0u) | (frame.controllersActive ? kControllersActive : 0u);
    Put(out, flags);
    for (View const& view : frame.views) {
        PutPose(out, view.pose);
        Put(out, view.fov.angleLeft);
        Put(out, view.fov.angleRight);
        Put(out, view.fov.angleUp);
        Put(out, view.fov.angleDown);
    }
    for (controllers::Hand const& hand : frame.hands) {
        std::uint32_t const buttons = (hand.stickClick ? kStickClick : 0u) | (hand.primary ? kPrimary : 0u) |
            (hand.secondary ? kSecondary : 0u) | (hand.menu ? kMenu : 0u) | (hand.gripValid ? kGripValid : 0u) |
            (hand.aimValid ? kAimValid : 0u);
        Put(out, buttons);
        Put(out, hand.trigger);
        Put(out, hand.squeeze);
        Put(out, hand.stickX);
        Put(out, hand.stickY);
        PutPose(out, hand.grip);
        PutPose(out, hand.aim);
    }
}

bool Decode(std::uint8_t const* data, std::size_t size, Recording& out, std::string* error, bool* truncated) {
    auto fail = [error](char const* why) {
        if (error != nullptr) {
            *error = why;
        }
        return false;
    };
    if (truncated != nullptr) {
        *truncated = false;
    }
    if (data == nullptr || size < kHeaderBytes || std::memcmp(data, kMagic, sizeof kMagic) != 0) {
        return fail("not a .mevrrec recording (bad magic)");
    }
    Reader header(data, size);
    header.Skip(sizeof kMagic);
    std::uint32_t const version = header.Get<std::uint32_t>();
    std::uint32_t const headerBytes = header.Get<std::uint32_t>();
    std::uint32_t const frameBytes = header.Get<std::uint32_t>();
    header.Skip(sizeof(std::uint32_t));
    std::int64_t const created = header.Get<std::int64_t>();
    if (version != kVersion) {
        return fail("unsupported recording version");
    }
    // Later versions may only grow the header and the frames; version 1 reads what it knows.
    if (headerBytes < kHeaderBytes || frameBytes < kFrameBytes || headerBytes > size) {
        return fail("bad header or frame size");
    }
    std::size_t const body = size - headerBytes;
    std::size_t const count = body / frameBytes;
    if (truncated != nullptr) {
        *truncated = body % frameBytes != 0;
    }
    if (count == 0) {
        return fail("the recording has no frames");
    }
    out.createdUnixMs = created;
    out.frames.clear();
    out.frames.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        Reader r(data + headerBytes + i * frameBytes, frameBytes);
        Frame frame;
        frame.timeNs = r.Get<std::int64_t>();
        std::uint32_t const flags = r.Get<std::uint32_t>();
        frame.headValid = (flags & kHeadValid) != 0;
        frame.controllersActive = (flags & kControllersActive) != 0;
        for (View& view : frame.views) {
            view.pose = r.GetPose();
            view.fov.angleLeft = r.Get<float>();
            view.fov.angleRight = r.Get<float>();
            view.fov.angleUp = r.Get<float>();
            view.fov.angleDown = r.Get<float>();
        }
        for (controllers::Hand& hand : frame.hands) {
            std::uint32_t const buttons = r.Get<std::uint32_t>();
            hand.stickClick = (buttons & kStickClick) != 0;
            hand.primary = (buttons & kPrimary) != 0;
            hand.secondary = (buttons & kSecondary) != 0;
            hand.menu = (buttons & kMenu) != 0;
            hand.gripValid = (buttons & kGripValid) != 0;
            hand.aimValid = (buttons & kAimValid) != 0;
            hand.trigger = r.Get<float>();
            hand.squeeze = r.Get<float>();
            hand.stickX = r.Get<float>();
            hand.stickY = r.Get<float>();
            hand.grip = r.GetPose();
            hand.aim = r.GetPose();
        }
        out.frames.push_back(frame);
    }
    return true;
}

bool Load(std::filesystem::path const& path, Recording& out, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error != nullptr) {
            *error = "can't open the file";
        }
        return false;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return Decode(bytes.data(), bytes.size(), out, error);
}

bool Sample(Recording const& recording, std::int64_t timeNs, bool loop, Frame& out) {
    auto const& frames = recording.frames;
    if (frames.empty()) {
        return false;
    }
    std::int64_t const start = frames.front().timeNs;
    std::int64_t const duration = recording.DurationNs();
    bool inRange = true;
    if (timeNs < 0) {
        timeNs = 0;
    }
    if (timeNs > duration) {
        if (loop && duration > 0) {
            timeNs %= duration;
        } else {
            inRange = false;
            timeNs = duration;
        }
    }
    std::int64_t const t = start + timeNs;
    // The first frame after t; the one before it is the earlier sample.
    auto const next = std::upper_bound(frames.begin(), frames.end(), t,
        [](std::int64_t value, Frame const& frame) { return value < frame.timeNs; });
    if (next == frames.begin()) {
        out = frames.front();
        return inRange;
    }
    Frame const& a = *(next - 1);
    if (next == frames.end()) {
        out = a;
        return inRange;
    }
    Frame const& b = *next;
    float const span = static_cast<float>(b.timeNs - a.timeNs);
    float const f = span > 0.0f ? std::clamp(static_cast<float>(t - a.timeNs) / span, 0.0f, 1.0f) : 0.0f;

    out = a;  // inputs and flags held from the earlier frame
    out.timeNs = t;
    if (a.headValid && b.headValid) {
        for (int e = 0; e < 2; ++e) {
            out.views[e].pose = Lerp(a.views[e].pose, b.views[e].pose, f);
            out.views[e].fov = Lerp(a.views[e].fov, b.views[e].fov, f);
        }
    } else if (b.headValid) {
        out.headValid = true;
        out.views[0] = b.views[0];
        out.views[1] = b.views[1];
    }
    for (int h = 0; h < 2; ++h) {
        BlendPose(a.hands[h].gripValid, a.hands[h].grip, b.hands[h].gripValid, b.hands[h].grip, f,
            out.hands[h].gripValid, out.hands[h].grip);
        BlendPose(a.hands[h].aimValid, a.hands[h].aim, b.hands[h].aimValid, b.hands[h].aim, f, out.hands[h].aimValid,
            out.hands[h].aim);
    }
    return inRange;
}

bool Writer::Open(std::filesystem::path const& path, std::int64_t createdUnixMs) {
    Close();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (_wfopen_s(&file_, path.c_str(), L"wb") != 0 || file_ == nullptr) {
        file_ = nullptr;
        return false;
    }
    std::setvbuf(file_, nullptr, _IOFBF, 256 * 1024);
    scratch_.clear();
    EncodeHeader(createdUnixMs, scratch_);
    frames_ = 0;
    return std::fwrite(scratch_.data(), 1, scratch_.size(), file_) == scratch_.size();
}

bool Writer::Append(Frame const& frame) {
    if (file_ == nullptr) {
        return false;
    }
    scratch_.clear();
    EncodeFrame(frame, scratch_);
    if (std::fwrite(scratch_.data(), 1, scratch_.size(), file_) != scratch_.size()) {
        return false;
    }
    if (++frames_ % 90 == 0) {
        std::fflush(file_);  // about once a second: a crash or exit loses little
    }
    return true;
}

void Writer::Close() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

} // namespace mevr::vr::recording
