// SPDX-License-Identifier: MIT
#include "core/vr/ViewSync.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace mevr::vr::view_sync {
namespace {

// UE3's game thread runs at most one frame ahead of the render thread, so a Present can find
// at most two views queued: its own and the next frame's. Older ones belong to frames that
// never presented (e.g. a loading stall) and are dropped.
constexpr std::size_t kMaxQueued = 2;

std::mutex gMutex;
std::condition_variable gPresented;
std::optional<XrPosef> gLatest;
std::optional<float> gHeadsetFov;
std::optional<float> gHalfIpd;
std::deque<RenderedView> gRendered;
std::optional<RenderedView> gFallback;
std::uint64_t gPresentCount = 0;
std::uint64_t gPushedFrames = 0;     // engine frames that queued a view
std::uint64_t gPresentedFrames = 0;  // of those, how many have been presented

} // namespace

void PublishHead(XrPosef const& pose) {
    std::lock_guard lock(gMutex);
    gLatest = pose;
}

std::optional<XrPosef> LatestHead() {
    std::lock_guard lock(gMutex);
    return gLatest;
}

void PublishHeadsetFov(float horizontalDegrees) {
    std::lock_guard lock(gMutex);
    gHeadsetFov = horizontalDegrees;
}

std::optional<float> HeadsetFov() {
    std::lock_guard lock(gMutex);
    return gHeadsetFov;
}

void PublishHalfIpd(float meters) {
    std::lock_guard lock(gMutex);
    gHalfIpd = meters;
}

std::optional<float> HalfIpd() {
    std::lock_guard lock(gMutex);
    return gHalfIpd;
}

void PushRendered(RenderedView view) {
    std::lock_guard lock(gMutex);
    view.engineFrame = ++gPushedFrames;
    gRendered.push_back(view);
    while (gRendered.size() > kMaxQueued) {
        gRendered.pop_front();
    }
    gFallback.reset();
}

void ReplaceNewest(RenderedView view) {
    std::lock_guard lock(gMutex);
    if (gRendered.empty()) {
        // Already consumed (can't normally happen before the frame's own Present): queue anew.
        view.engineFrame = ++gPushedFrames;
        gRendered.push_back(view);
        return;
    }
    view.engineFrame = gRendered.back().engineFrame;
    gRendered.back() = view;
}

void PublishFallback(RenderedView view) {
    std::lock_guard lock(gMutex);
    gFallback = view;
}

std::optional<RenderedView> Fallback() {
    std::lock_guard lock(gMutex);
    return gFallback;
}

std::uint64_t PresentCount() {
    std::lock_guard lock(gMutex);
    return gPresentCount;
}

std::optional<RenderedView> PeekForPresent(std::size_t& depth) {
    std::lock_guard lock(gMutex);
    depth = gRendered.size();
    if (gRendered.empty()) {
        return std::nullopt;
    }
    return gRendered.front();
}

void NotifyPresented() {
    {
        std::lock_guard lock(gMutex);
        ++gPresentCount;
        if (gRendered.empty()) {
            gPresentedFrames = gPushedFrames;  // nothing tagged (VR off, loading): release any waiter
        } else {
            gPresentedFrames = gRendered.front().engineFrame;
            gRendered.pop_front();
        }
    }
    gPresented.notify_all();
}

void DropOldest() {
    {
        std::lock_guard lock(gMutex);
        if (!gRendered.empty()) {
            gPresentedFrames = gRendered.front().engineFrame;
            gRendered.pop_front();
        }
    }
    gPresented.notify_all();
}

bool HasPendingFrame() {
    std::lock_guard lock(gMutex);
    return gPresentedFrames < gPushedFrames;
}

bool WaitForPresent(std::uint32_t timeoutMs) {
    std::unique_lock lock(gMutex);
    std::uint64_t const target = gPushedFrames;
    return gPresented.wait_for(lock, std::chrono::milliseconds(timeoutMs),
        [target] { return gPresentedFrames >= target; });
}

void ClearFrames() {
    {
        std::lock_guard lock(gMutex);
        gRendered.clear();
        gFallback.reset();
        gPresentedFrames = gPushedFrames;  // release any waiter
    }
    gPresented.notify_all();
}

void Reset() {
    {
        std::lock_guard lock(gMutex);
        gLatest.reset();
        gHeadsetFov.reset();
        gHalfIpd.reset();
    }
    ClearFrames();
}

} // namespace mevr::vr::view_sync
