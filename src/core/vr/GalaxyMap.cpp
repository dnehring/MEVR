// SPDX-License-Identifier: MIT
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/OverlayPlacement.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace mevr::vr::galaxy_map {

std::optional<Screen> NativeTablePanel::Update(bool mapActive, bool enabled, bool ready, XrPosef const& head, float aspect,
    galaxy_holo::TableSettings const& settings) {
    if (!enabled || !mapActive) {
        Reset();
        return std::nullopt;
    }
    float const norm = head.orientation.x * head.orientation.x + head.orientation.y * head.orientation.y +
        head.orientation.z * head.orientation.z + head.orientation.w * head.orientation.w;
    if (!ready || !std::isfinite(aspect) || aspect <= 0 || !std::isfinite(norm) || norm < 0.5f || norm > 1.5f ||
        !std::isfinite(head.position.x) || !std::isfinite(head.position.y) || !std::isfinite(head.position.z) ||
        !std::isfinite(settings.radiusM) || settings.radiusM <= 0 || !std::isfinite(settings.distanceM) ||
        !std::isfinite(settings.belowM) || !std::isfinite(settings.tiltDeg)) return std::nullopt;
    if (!placed_) {
        auto const table = galaxy_holo::PlaceTable(head, settings);
        auto const normal = overlay::Rotate(table.pose.orientation, {0, 1, 0});
        pose_ = galaxy_holo::DiscPose(table.pose.position, normal, head.position);
        placed_ = true;
    }
    return Screen{pose_, settings.radiusM * 2, settings.radiusM * 2 * aspect};
}

namespace {

std::mutex gMutex;
View gView;
std::uint64_t gSequence = 0;

} // namespace

void Publish(View const& view) {
    std::lock_guard lock(gMutex);
    gView = view;
    gView.sequence = ++gSequence;
    gView.timeMs = GetTickCount64();
}

void Reset() {
    std::lock_guard lock(gMutex);
    gView = View{};
}

std::optional<View> Latest(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gMutex);
    if (!gView.active || gView.timeMs == 0 || GetTickCount64() - gView.timeMs > maxAgeMs) {
        return std::nullopt;
    }
    return gView;
}

std::optional<float> UnitsPerMeter(float cameraToCentreUnits, float distanceM) {
    if (!std::isfinite(cameraToCentreUnits) || !std::isfinite(distanceM) || !(cameraToCentreUnits > 0.0f) || !(distanceM > 0.01f)) {
        return std::nullopt;
    }
    return std::clamp(cameraToCentreUnits / distanceM, kMinUnitsPerMeter, kMaxUnitsPerMeter);
}

std::optional<Screen> VirtualScreen(float distanceM, float belowM, float tanHalfX, float tanHalfY) {
    if (!(distanceM > 0.01f) || !(tanHalfX > 0.0f) || !(tanHalfY > 0.0f) || !std::isfinite(distanceM + belowM + tanHalfX + tanHalfY)) {
        return std::nullopt;
    }
    Screen s;
    s.pose.orientation = {0.0f, 0.0f, 0.0f, 1.0f};  // facing +Z: toward a camera looking along -Z
    s.pose.position = {0.0f, -belowM, -distanceM};
    s.width = 2.0f * distanceM * tanHalfX;
    s.height = 2.0f * distanceM * tanHalfY;
    return s;
}

std::optional<XrVector2f> ProjectToWindow(XrVector3f const& point, float belowM, float tanHalfX, float tanHalfY) {
    float const ahead = -point.z;
    if (!(ahead > 1e-4f) || !(tanHalfX > 0.0f) || !(tanHalfY > 0.0f)) {
        return std::nullopt;
    }
    float const x = point.x / (ahead * tanHalfX);             // -1 left .. +1 right
    float const y = (point.y + belowM) / (ahead * tanHalfY);  // -1 bottom .. +1 top, from the camera's height
    return XrVector2f{0.5f + 0.5f * x, 0.5f - 0.5f * y};
}

} // namespace mevr::vr::galaxy_map
