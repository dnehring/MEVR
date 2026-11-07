// SPDX-License-Identifier: MIT
#include "core/vr/HostView.hpp"

#include <Windows.h>

#include <mutex>
#include <cmath>

namespace mevr::vr::host_view {
namespace {

std::mutex gMutex;
State gLatest;
std::uint64_t gSequence = 0;

} // namespace

void Publish(State const& state) {
    Publish(state, GetTickCount64());
}

void Publish(State const& state, std::uint64_t sampledAtMs) {
    std::lock_guard lock(gMutex);
    gLatest = state;
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = sampledAtMs;
}

void Mirror(State const& state) {
    std::lock_guard lock(gMutex);
    gLatest = state;
}

double HoldBaseZ(State const& view, double rawCameraZ, double rightZ, double upZ, double forwardZ) {
    double const commonZ = rightZ * view.offsetRight + upZ * view.offsetUp + forwardZ * view.offsetForward;
    return view.valid && view.floorLocked && view.centreValid && std::isfinite(view.centreZ) && std::isfinite(commonZ)
        ? view.centreZ - commonZ : rawCameraZ;
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
