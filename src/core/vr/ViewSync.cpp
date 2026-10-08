#include "core/vr/ViewSync.hpp"

#include <deque>
#include <mutex>

namespace mevr::vr::view_sync {
namespace {

// More than this means the game ticked without presenting (e.g. a loading stall); drop the
// oldest so the pairing recovers instead of lagging further behind.
constexpr std::size_t kMaxQueued = 3;

std::mutex gMutex;
std::optional<XrPosef> gLatest;
std::deque<RenderedView> gRendered;

} // namespace

void PublishHead(XrPosef const& pose) {
    std::lock_guard lock(gMutex);
    gLatest = pose;
}

std::optional<XrPosef> LatestHead() {
    std::lock_guard lock(gMutex);
    return gLatest;
}

void PushRendered(RenderedView const& view) {
    std::lock_guard lock(gMutex);
    gRendered.push_back(view);
    while (gRendered.size() > kMaxQueued) {
        gRendered.pop_front();
    }
}

std::optional<RenderedView> PopRendered() {
    std::lock_guard lock(gMutex);
    if (gRendered.empty()) {
        return std::nullopt;
    }
    RenderedView const view = gRendered.front();
    gRendered.pop_front();
    return view;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest.reset();
    gRendered.clear();
}

} // namespace mevr::vr::view_sync
