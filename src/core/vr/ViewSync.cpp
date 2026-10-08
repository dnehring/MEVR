#include "core/vr/ViewSync.hpp"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace mevr::vr::view_sync {
namespace {

// More than this means frames stopped being presented (e.g. a loading stall); drop the oldest.
constexpr std::size_t kMaxQueued = 4;

std::mutex gMutex;
std::condition_variable gPresented;
std::optional<XrPosef> gLatest;
std::optional<float> gHeadsetFov;
std::deque<RenderedView> gRendered;
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

void PushRendered(RenderedView view) {
    std::lock_guard lock(gMutex);
    view.engineFrame = ++gPushedFrames;
    gRendered.push_back(view);
    while (gRendered.size() > kMaxQueued) {
        gRendered.pop_front();
    }
}

std::optional<RenderedView> TakeForPresent(std::size_t& depth) {
    std::lock_guard lock(gMutex);
    depth = gRendered.size();
    if (gRendered.empty()) {
        return std::nullopt;
    }
    RenderedView const view = gRendered.back();
    gRendered.clear();
    return view;
}

void NotifyPresented() {
    {
        std::lock_guard lock(gMutex);
        gPresentedFrames = gPushedFrames;
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

void Reset() {
    {
        std::lock_guard lock(gMutex);
        gLatest.reset();
        gHeadsetFov.reset();
        gRendered.clear();
        gPresentedFrames = gPushedFrames;  // release any waiter
    }
    gPresented.notify_all();
}

} // namespace mevr::vr::view_sync
