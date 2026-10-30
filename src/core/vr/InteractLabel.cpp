// SPDX-License-Identifier: MIT
#include "core/vr/InteractLabel.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace mevr::vr::interact {
namespace {

std::mutex gMutex;
Label gLatest{};
std::uint64_t gSequence = 0;

constexpr float kStageWidth = 1280.0f;
constexpr float kBandLeft = 384.0f;
constexpr float kBandRight = 896.0f;
constexpr float kBandTop = 30.0f;
constexpr float kBandBottom = 100.0f;

} // namespace

void Publish(Label const& label) {
    std::lock_guard lock(gMutex);
    gLatest = label;
    gLatest.size = sizeof(Label);
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

Label Latest(std::uint64_t maxAgeMs) {
    Label copy;
    {
        std::lock_guard lock(gMutex);
        copy = gLatest;
    }
    if (copy.timeMs == 0 || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.size = 0;
        copy.visible = false;
    }
    return copy;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = {};
}

Rect DefaultCrop(std::uint32_t width, std::uint32_t height) {
    if (width == 0 || height == 0) {
        return {};
    }
    float const scale = static_cast<float>(width) / kStageWidth;  // frame pixels per stage pixel
    float const h = static_cast<float>(height);
    Rect rect;
    rect.x = kBandLeft / kStageWidth;
    rect.width = (kBandRight - kBandLeft) / kStageWidth;
    rect.y = std::min(kBandTop * scale / h, 1.0f);
    rect.height = std::min((kBandBottom - kBandTop) * scale / h, 1.0f - rect.y);
    return rect;
}

Pixels ToPixels(Rect const& rect, std::uint32_t width, std::uint32_t height) {
    if (width < 8 || height < 8) {
        return {};
    }
    auto const span = [](float from, float size, std::uint32_t total, std::uint32_t& start, std::uint32_t& length) {
        float const t = static_cast<float>(total);
        float const a = std::clamp(from, 0.0f, 1.0f) * t;
        float const b = std::clamp(from + size, 0.0f, 1.0f) * t;
        std::uint32_t const s = std::min(static_cast<std::uint32_t>(std::floor(a)), total - 8);
        std::uint32_t const e = std::clamp(static_cast<std::uint32_t>(std::ceil(b)), s + 8, total);
        start = s;
        length = e - s;
    };
    Pixels px;
    span(rect.x, rect.width, width, px.x, px.width);
    span(rect.y, rect.height, height, px.y, px.height);
    return px;
}

bool Place(XrVector3f const& position, XrVector3f const& head, float heightDeg, float aspect, Quad& out) {
    XrVector3f const toHead{head.x - position.x, head.y - position.y, head.z - position.z};
    float const distance = std::sqrt(toHead.x * toHead.x + toHead.y * toHead.y + toHead.z * toHead.z);
    if (!(distance > 0.05f) || !(heightDeg > 0.0f) || !(aspect > 0.0f)) {
        return false;
    }
    float const half = std::clamp(heightDeg, 0.1f, 60.0f) * 0.5f * 3.14159265358979f / 180.0f;
    out.pose.position = position;
    out.pose.orientation = overlay::Facing({toHead.x / distance, toHead.y / distance, toHead.z / distance});
    out.height = 2.0f * distance * std::tan(half);
    out.width = out.height * aspect;
    return true;
}

Point LabelPoint(Point const& boundsOrigin, double boundsHalfHeight, double eyeZ, double raise, double maxRise, double aboveEye) {
    Point p = boundsOrigin;
    double const rise = std::clamp(boundsHalfHeight, 0.0, maxRise) + raise;
    p.z = std::min(boundsOrigin.z + rise, std::max(eyeZ + aboveEye, boundsOrigin.z));
    return p;
}

} // namespace mevr::vr::interact
