// SPDX-License-Identifier: MIT
#include "core/vr/GalaxyRay.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::galaxy_ray {
namespace {

namespace gh = galaxy_holo;

constexpr float kDegToRad = 0.01745329252f;
constexpr float kRotatorToRad = 3.14159265358979f / 32768.0f;

struct Basis {
    std::array<float, 3> forward, right, up;
};

// UE3's FRotationMatrix's axes (X forward, Y right, Z up) for a rotator in radians (ToGameScreen's).
Basis BasisOf(float pitch, float yaw, float roll) {
    float const sp = std::sin(pitch), cp = std::cos(pitch);
    float const sy = std::sin(yaw), cy = std::cos(yaw);
    float const sr = std::sin(roll), cr = std::cos(roll);
    return {{cp * cy, cp * sy, sp}, {sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp},
        {-(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp}};
}

float Dot3(std::array<float, 3> const& a, std::array<float, 3> const& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

} // namespace

Panel PanelOnDisc(XrVector3f const& discCentre, XrVector3f const& discNormal, XrVector3f const& head, float windowWidthM,
    float windowCrop, float aspect) {
    Panel panel;
    panel.pose = gh::DiscPose(discCentre, discNormal, head);
    panel.widthM = windowWidthM / (1.0f - std::clamp(windowCrop, 0.0f, 0.2f));
    panel.heightM = aspect > 0.1f ? panel.widthM / aspect : 0.0f;
    return panel;
}

std::optional<Hit> PanelHit(XrVector3f const& origin, XrVector3f const& direction, Panel const& panel) {
    if (!(panel.widthM > 0.0f) || !(panel.heightM > 0.0f)) return std::nullopt;
    XrVector3f const d = gh::Normalize(direction);
    XrVector3f const normal = overlay::Rotate(panel.pose.orientation, {0.0f, 0.0f, 1.0f});
    float const denom = gh::Dot(d, normal);
    if (std::fabs(denom) < 1e-4f) return std::nullopt;
    float const t = gh::Dot(gh::Sub(panel.pose.position, origin), normal) / denom;
    if (!(t > 0.0f) || !std::isfinite(t)) return std::nullopt;
    XrVector3f const point = gh::Add(origin, gh::Mul(d, t));
    XrVector3f const rel = gh::Sub(point, panel.pose.position);
    float const x = gh::Dot(rel, overlay::Rotate(panel.pose.orientation, {1.0f, 0.0f, 0.0f}));
    float const y = gh::Dot(rel, overlay::Rotate(panel.pose.orientation, {0.0f, 1.0f, 0.0f}));
    Hit hit;
    hit.u = 0.5f + x / panel.widthM;
    hit.v = 0.5f - y / panel.heightM;
    hit.distanceM = t;
    hit.point = point;
    if (hit.u < 0.0f || hit.u > 1.0f || hit.v < 0.0f || hit.v > 1.0f) return std::nullopt;
    return hit;
}

std::optional<WorldRay> ScreenRay(gh::GameCamera const& camera, float u, float v) {
    if (!camera.valid || !(camera.fovXDeg > 1.0f) || !(camera.fovXDeg < 179.0f) || !(camera.aspect > 0.1f)) return std::nullopt;
    Basis const b = BasisOf(camera.pitch, camera.yaw, camera.roll);
    float const tanX = std::tan(camera.fovXDeg * 0.5f * kDegToRad);
    float const tanY = tanX / camera.aspect;
    float const sx = (2.0f * u - 1.0f) * tanX, sy = (1.0f - 2.0f * v) * tanY;
    std::array<float, 3> d{};
    for (int i = 0; i < 3; ++i) d[i] = b.forward[i] + sx * b.right[i] + sy * b.up[i];
    float const n = std::sqrt(Dot3(d, d));
    if (!(n > 1e-6f)) return std::nullopt;
    WorldRay ray;
    for (int i = 0; i < 3; ++i) {
        ray.origin[i] = camera.location[i];
        ray.direction[i] = d[i] / n;
    }
    return ray;
}

std::optional<std::array<float, 3>> SphereHit(WorldRay const& ray, std::array<float, 3> const& centre, float radius, float grazeScale) {
    if (!(radius > 0.0f)) return std::nullopt;
    std::array<float, 3> const oc{ray.origin[0] - centre[0], ray.origin[1] - centre[1], ray.origin[2] - centre[2]};
    float const b = Dot3(oc, ray.direction);
    float const c = Dot3(oc, oc) - radius * radius;
    float const disc = b * b - c;
    if (disc >= 0.0f) {
        float const t = -b - std::sqrt(disc);
        if (t <= 0.0f) return std::nullopt;  // the camera inside the sphere, or the sphere behind it
        return std::array<float, 3>{ray.origin[0] + t * ray.direction[0], ray.origin[1] + t * ray.direction[1],
            ray.origin[2] + t * ray.direction[2]};
    }
    // Missed: within the widened sphere, the rim point nearest the ray (the closest approach pulled onto the sphere).
    float const graze = radius * std::max(1.0f, grazeScale);
    if (-b <= 0.0f || b * b - (Dot3(oc, oc) - graze * graze) < 0.0f) return std::nullopt;
    std::array<float, 3> closest{ray.origin[0] - b * ray.direction[0] - centre[0], ray.origin[1] - b * ray.direction[1] - centre[1],
        ray.origin[2] - b * ray.direction[2] - centre[2]};
    float const n = std::sqrt(Dot3(closest, closest));
    if (!(n > 1e-6f)) return std::nullopt;
    return std::array<float, 3>{centre[0] + closest[0] / n * radius, centre[1] + closest[1] / n * radius,
        centre[2] + closest[2] / n * radius};
}

int Nearest(std::span<Candidate const> candidates, float u, float v, float aspect, float toleranceH) {
    int best = -1;
    float bestDistance = toleranceH;
    float const a = aspect > 0.1f ? aspect : 1.0f;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        Candidate const& c = candidates[i];
        if (!c.valid) continue;
        float const dx = (c.u - u) * a, dy = c.v - v;  // in screen heights
        float const distance = std::sqrt(dx * dx + dy * dy);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

YawPitch ToRotator(std::array<float, 3> const& d) {
    float const yaw = std::atan2(d[1], d[0]);
    float const pitch = std::atan2(d[2], std::sqrt(d[0] * d[0] + d[1] * d[1]));
    return {static_cast<int>(std::lround(yaw / kRotatorToRad)), static_cast<int>(std::lround(pitch / kRotatorToRad))};
}

std::array<float, 3> ToLocal(std::array<float, 3> const& d, int pitch, int yaw, int roll) {
    Basis const b = BasisOf(static_cast<float>(pitch) * kRotatorToRad, static_cast<float>(yaw) * kRotatorToRad,
        static_cast<float>(roll) * kRotatorToRad);
    return {Dot3(d, b.forward), Dot3(d, b.right), Dot3(d, b.up)};
}

} // namespace mevr::vr::galaxy_ray
