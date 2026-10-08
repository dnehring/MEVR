#include "core/vr/AimRay.hpp"

#include <Windows.h>

#include <mutex>

namespace mevr::vr::aim_ray {
namespace {

std::mutex gMutex;
Ray gLatest;
std::uint64_t gSequence = 0;

} // namespace

void Publish(Ray const& ray) {
    std::lock_guard lock(gMutex);
    gLatest = ray;
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

Ray Latest(std::uint64_t maxAgeMs) {
    Ray copy;
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

} // namespace mevr::vr::aim_ray
