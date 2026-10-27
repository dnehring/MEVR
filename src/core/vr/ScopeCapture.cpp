// SPDX-License-Identifier: MIT
#include "core/vr/ScopeCapture.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace mevr::vr::scope_capture {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

std::mutex gMutex;
Capture gHistory[kHistory];
std::uint64_t gSequence = 0;
std::uint64_t gResets = 0;

constexpr int kArms = 32;
struct ArmEntry {
    std::uint64_t arm = 0;
    std::uint64_t sequence = 0;
};
ArmEntry gArms[kArms];
int gArmNext = 0;

float Length(XrVector3f const& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

XrVector3f Cross(XrVector3f const& a, XrVector3f const& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

} // namespace

float FovDeg(float pictureDeg, float zoom, float margin) {
    if (!std::isfinite(pictureDeg) || !std::isfinite(zoom) || !std::isfinite(margin)) {
        return 10.0f;
    }
    float const half = std::clamp(pictureDeg, 0.5f, 170.0f) * 0.5f * kDegToRad;
    float const t = std::tan(half) / std::max(zoom, 1.0f) * (1.0f + std::clamp(margin, 0.0f, 2.0f));
    return std::clamp(2.0f * std::atan(t) / kDegToRad, 0.5f, 90.0f);
}

float TanHalf(float fovDeg) {
    return std::tan(std::clamp(fovDeg, 0.1f, 170.0f) * 0.5f * kDegToRad);
}

float PictureDeg(float scopeFieldDeg, float lensRadiusM) {
    if (scopeFieldDeg > 0.0f && std::isfinite(scopeFieldDeg)) {
        return scopeFieldDeg;
    }
    float const radius = std::isfinite(lensRadiusM) && lensRadiusM > 0.0f ? lensRadiusM : 0.02f;
    return 2.0f * std::atan(radius / 0.05f) / kDegToRad;
}

float NearPlaneUnits(float sightToMuzzleUnits, float extraUnits) {
    float const toMuzzle = std::isfinite(sightToMuzzleUnits) ? std::max(sightToMuzzleUnits, 0.0f) : 0.0f;
    float const extra = std::isfinite(extraUnits) ? std::max(extraUnits, 0.0f) : 0.0f;
    return std::max(toMuzzle + extra, 10.0f);
}

std::optional<XrQuaternionf> CameraOrientation(XrVector3f const& forward, XrVector3f const& up) {
    float const f = Length(forward);
    if (!(f > 1e-6f) || !std::isfinite(f)) {
        return std::nullopt;
    }
    XrVector3f const z{-forward.x / f, -forward.y / f, -forward.z / f};  // the camera's +Z points back
    if (!(Length(Cross(up, z)) > 1e-4f)) {
        return std::nullopt;
    }
    return overlay::Facing(z, up);
}

void Publish(Capture const& capture) {
    std::lock_guard lock(gMutex);
    Capture c = capture;
    c.size = sizeof(Capture);
    c.sequence = ++gSequence;
    c.timeMs = GetTickCount64();
    gHistory[c.sequence % kHistory] = c;
}

Capture Latest(std::uint64_t maxAgeMs) {
    Capture copy;
    {
        std::lock_guard lock(gMutex);
        if (gSequence == 0) {
            return Capture{};
        }
        copy = gHistory[gSequence % kHistory];
    }
    if (copy.size != sizeof(Capture) || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.state = kOff;
    }
    return copy;
}

std::optional<Capture> BySequence(std::uint64_t sequence) {
    std::lock_guard lock(gMutex);
    if (sequence == 0 || sequence > gSequence || gSequence - sequence >= kHistory) {
        return std::nullopt;
    }
    Capture const& c = gHistory[sequence % kHistory];
    if (c.sequence != sequence) {
        return std::nullopt;
    }
    return c;
}

std::uint64_t LatestSequence() {
    std::lock_guard lock(gMutex);
    return gSequence;
}

void Reset() {
    std::lock_guard lock(gMutex);
    for (Capture& c : gHistory) {
        c = Capture{};
    }
    // The sequence keeps counting: an arm noted before the reset can't match a later publish by accident.
    for (ArmEntry& a : gArms) {
        a = ArmEntry{};
    }
    ++gResets;
}

std::uint64_t Resets() {
    std::lock_guard lock(gMutex);
    return gResets;
}

void NoteDrawArm(std::uint64_t arm) {
    if (arm == 0) {
        return;
    }
    std::lock_guard lock(gMutex);
    // Several views of one Draw latch the same arm: the newest note for it replaces the last.
    int const last = (gArmNext + kArms - 1) % kArms;
    if (gArms[last].arm == arm) {
        gArmNext = last;
    }
    gArms[gArmNext] = {arm, gSequence};
    gArmNext = (gArmNext + 1) % kArms;
}

std::optional<std::uint64_t> SequenceForArm(std::uint64_t arm) {
    if (arm == 0) {
        return std::nullopt;
    }
    std::lock_guard lock(gMutex);
    for (ArmEntry const& a : gArms) {
        if (a.arm == arm && a.sequence != 0) {
            return a.sequence;
        }
    }
    return std::nullopt;
}

TexturePath FindTexture(std::uint64_t resource, std::function<std::optional<std::uint64_t>(std::uint64_t)> const& read,
    std::function<bool(std::uint64_t)> const& isTexture, TexturePath const& hint, std::int32_t firstSpan, std::int32_t secondSpan) {
    auto const plausible = [](std::uint64_t p) {
        // A user-mode heap or image address, 8-aligned.
        return p >= 0x10000 && p < 0x00007FFFFFFFFFFFull && (p & 7u) == 0;
    };
    auto const at = [&](std::uint64_t base, std::int32_t first, std::int32_t second) -> std::optional<std::uint64_t> {
        auto const p = read(base + static_cast<std::uint64_t>(first));
        if (!p || !plausible(*p)) {
            return std::nullopt;
        }
        if (second < 0) {
            return p;
        }
        auto const q = read(*p + static_cast<std::uint64_t>(second));
        if (!q || !plausible(*q)) {
            return std::nullopt;
        }
        return q;
    };
    if (resource == 0 || !read || !isTexture) {
        return {};
    }
    if (hint.Valid()) {
        if (auto const t = at(resource, hint.first, hint.second); t && isTexture(*t)) {
            return {hint.first, hint.second, *t};
        }
    }
    for (std::int32_t first = 0; first < firstSpan; first += 8) {
        auto const p = read(resource + static_cast<std::uint64_t>(first));
        if (!p || !plausible(*p)) {
            continue;
        }
        if (isTexture(*p)) {
            return {first, -1, *p};
        }
        for (std::int32_t second = 0; second < secondSpan; second += 8) {
            auto const q = read(*p + static_cast<std::uint64_t>(second));
            if (q && plausible(*q) && isTexture(*q)) {
                return {first, second, *q};
            }
        }
    }
    return {};
}

} // namespace mevr::vr::scope_capture
