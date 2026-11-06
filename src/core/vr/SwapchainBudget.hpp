// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

// The rules plugin/Swapchains follows when the OpenXR runtime refuses a swapchain (XR_ERROR_LIMIT_REACHED; SteamVR's limit is
// 16 a session whatever their size, Halcyon holds 4): which of ours to free for room, and when a refused owner may ask again. Unit-tested
// (tests/SwapchainBudgetTests.cpp).
namespace mevr::vr::swapchain_budget {

constexpr std::uint64_t kIdleMs = 1000;   // not shown for this long: may be freed for any refused create
constexpr std::uint64_t kRecentMs = 100;  // an Essential create may also free ours shown this long ago (no GPU work left on them)
constexpr std::uint64_t kRetryMs = 2000;  // a refused owner asks again this often when nothing was freed

struct Live {
    std::uint64_t createdMs = 0;
    std::uint64_t shownMs = 0;  // last in a submitted layer; 0 never
    bool essential = false;     // the calibration's: never freed for another Essential create while in use

    std::uint64_t LastUse() const { return std::max(createdMs, shownMs); }
};

// Which of `live` to free (indices, in the order to free them) for `needed` refused creates at `nowMs`: ours idle kIdleMs,
// the least recently shown first; when a refused create is Essential, then ours not Essential shown kRecentMs ago or more.
inline std::vector<std::size_t> Choose(std::span<Live const> live, std::uint64_t nowMs, std::size_t needed, bool essentialRequest) {
    std::vector<std::pair<std::uint64_t, std::size_t>> idle;
    std::vector<std::pair<std::uint64_t, std::size_t>> recent;
    for (std::size_t i = 0; i < live.size(); ++i) {
        std::uint64_t const last = live[i].LastUse();
        std::uint64_t const age = nowMs >= last ? nowMs - last : 0;
        if (age >= kIdleMs) {
            idle.emplace_back(last, i);
        } else if (essentialRequest && !live[i].essential && age >= kRecentMs) {
            recent.emplace_back(last, i);
        }
    }
    std::sort(idle.begin(), idle.end());
    std::sort(recent.begin(), recent.end());
    idle.insert(idle.end(), recent.begin(), recent.end());
    std::vector<std::size_t> out;
    for (std::size_t i = 0; i < idle.size() && out.size() < needed; ++i) {
        out.push_back(idle[i].second);
    }
    return out;
}

// A create refused at `failedMs` may ask the runtime again: kRetryMs later, or once a frame's end freed room after it.
inline bool RetryDue(std::uint64_t failedMs, std::uint64_t freedMs, std::uint64_t nowMs) {
    return (nowMs >= failedMs && nowMs - failedMs >= kRetryMs) || (freedMs != 0 && freedMs >= failedMs);
}

} // namespace mevr::vr::swapchain_budget
