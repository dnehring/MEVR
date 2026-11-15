// SPDX-License-Identifier: GPL-3.0-only
// Adapted from the per-eye history/freeze behavior in Halcyon LE1 xr_session.cpp.
#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mevr::vr::stereo_tag {
struct Delay { int newer, older; float fraction; };
inline Delay FrameDelay(float frames, int last) {
    if (!std::isfinite(frames) || frames < 0) frames = 0;
    if (frames > static_cast<float>(last)) frames = static_cast<float>(last);
    int const a = static_cast<int>(frames);
    return {a, a < last ? a + 1 : a, frames - static_cast<float>(a)};
}
// These slots are written and selected alongside the renderer's EXISTING head
// ring. There is no second clock, delay setting or independent pose selection.
template<class Vector, std::size_t N>
class EyeHistory {
public:
    using Pair = std::array<Vector, 2>;
    void Seed(Pair const& eyes) { slots_.fill(eyes); }
    void Write(std::size_t slot, Pair const& eyes) { slots_[slot] = eyes; }
    void Shift(Pair const& eyes) {
        for (std::size_t i = N - 1; i != 0; --i) slots_[i] = slots_[i - 1];
        slots_[0] = eyes;
    }
    Pair At(std::size_t a, std::size_t b, float fraction) const {
        Pair out{};
        for (std::size_t eye = 0; eye != 2; ++eye) {
            auto const& p = slots_[a][eye]; auto const& q = slots_[b][eye];
            out[eye] = {p.x + (q.x - p.x) * fraction, p.y + (q.y - p.y) * fraction,
                        p.z + (q.z - p.z) * fraction};
        }
        return out;
    }
private:
    std::array<Pair, N> slots_{};
};
// A located pose in a new reference/context cannot relabel pixels captured in
// the previous one. Caller may show an explicit live mono fallback until Advance.
class CaptureEpoch {
public:
    void Reset(std::uint64_t sequence) { baseline_ = sequence; fresh_ = false; }
    bool Advance(std::uint64_t sequence) {
        if (sequence != baseline_) fresh_ = true;
        return fresh_;
    }
private:
    std::uint64_t baseline_ = 0;
    bool fresh_ = false;
};
// Cache the WHOLE declaration with the held source: orientation, both positions
// and both FOVs. Copy/release publication is a separate policy (EyePublication).
template<class Tag>
class SourceTag {
public:
    void BeginFrame() { wasActive_ = active_; active_ = false; }
    bool Enter(unsigned context) {
        bool const changed = !wasActive_ || context != context_;
        active_ = true; context_ = context;
        if (changed) valid_ = false;
        return changed;
    }
    void Reset() { valid_ = active_ = wasActive_ = eligible_ = held_ = false; }
    bool Held() const { return held_; }
    Tag const& Select(std::uint64_t sequence, Tag const& candidate, bool freezeEligible) {
        held_ = valid_ && eligible_ && freezeEligible && sequence == sequence_;
        if (!held_) {
            value_ = candidate; valid_ = true;
        }
        sequence_ = sequence; eligible_ = freezeEligible;
        return value_;
    }
private:
    Tag value_{};
    std::uint64_t sequence_ = 0;
    unsigned context_ = 0;
    bool valid_ = false, active_ = false, wasActive_ = false, eligible_ = false, held_ = false;
};
} // namespace mevr::vr::stereo_tag
