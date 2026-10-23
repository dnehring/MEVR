// SPDX-License-Identifier: MIT
#include "core/vr/HostView.hpp"

#include <Windows.h>

#include <mutex>

namespace mevr::vr::host_view {
namespace {

std::mutex gMutex;
State gLatest;
std::uint64_t gSequence = 0;

} // namespace

void Publish(State const& state) {
    std::lock_guard lock(gMutex);
    gLatest = state;
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

void Mirror(State const& state) {
    std::lock_guard lock(gMutex);
    gLatest = state;
}

State Latest(std::uint64_t maxAgeMs) {
    State copy;
    {
        std::lock_guard lock(gMutex);
        copy = gLatest;
    }
    if (copy.timeMs == 0 || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.valid = false;
    }
    return copy;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = {};
}

} // namespace mevr::vr::host_view
