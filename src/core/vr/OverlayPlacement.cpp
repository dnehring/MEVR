// SPDX-License-Identifier: MIT
#include "core/vr/OverlayPlacement.hpp"

#include <algorithm>
#include <cmath>

namespace mevr::vr::overlay {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kFollowRate = 4.0f;              // per second: about 1/e of the way in a quarter second
constexpr float kFollowDone = 2.0f * kPi / 180;  // stop following within 2 degrees

XrVector3f Add(XrVector3f a, XrVector3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
XrVector3f Sub(XrVector3f a, XrVector3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
XrVector3f Scale(XrVector3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float Dot(XrVector3f a, XrVector3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
XrVector3f Cross(XrVector3f a, XrVector3f b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float Length(XrVector3f a) { return std::sqrt(Dot(a, a)); }

bool Normalize(XrVector3f& v) {
    float const len = Length(v);
    if (!(len > 1e-6f) || !std::isfinite(len)) {
        return false;
    }
    v = Scale(v, 1.0f / len);
    return true;
}

// Rotation matrix with columns x, y, z (orthonormal, right-handed) to a quaternion.
XrQuaternionf FromBasis(XrVector3f x, XrVector3f y, XrVector3f z) {
    float const m00 = x.x, m10 = x.y, m20 = x.z;
    float const m01 = y.x, m11 = y.y, m21 = y.z;
    float const m02 = z.x, m12 = z.y, m22 = z.z;
    float const trace = m00 + m11 + m22;
    XrQuaternionf q{};
    if (trace > 0.0f) {
        float const s = std::sqrt(trace + 1.0f) * 2.0f;
        q = {(m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s};
    } else if (m00 > m11 && m00 > m22) {
        float const s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
        q = {0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s};
    } else if (m11 > m22) {
        float const s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
        q = {(m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s};
    } else {
        float const s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
        q = {(m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s};
    }
    float const len = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    return {q.x / len, q.y / len, q.z / len, q.w / len};
}

} // namespace

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const t = Scale(Cross(u, v), 2.0f);
    return Add(Add(v, Scale(t, q.w)), Cross(u, t));
}

float HeadingOf(XrQuaternionf const& orientation) {
    XrVector3f f = Rotate(orientation, {0.0f, 0.0f, -1.0f});
    if (f.x * f.x + f.z * f.z < 1e-4f) {
        // Straight up or down: the top of the head points the way the face would.
        XrVector3f const up = Rotate(orientation, {0.0f, 1.0f, 0.0f});
        f = f.y > 0.0f ? Scale(up, -1.0f) : up;
    }
    return std::atan2(-f.x, -f.z);
}

XrQuaternionf YawRotation(float yaw) {
    return {0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
}

float WrapAngle(float radians) {
    if (!std::isfinite(radians)) {
        return 0.0f;
    }
    float r = std::fmod(radians + kPi, 2.0f * kPi);
    if (r <= 0.0f) {
        r += 2.0f * kPi;
    }
    return r - kPi;
}

XrQuaternionf Facing(XrVector3f const& normal, XrVector3f const& up) {
    XrVector3f z = normal;
    if (!Normalize(z)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    XrVector3f x = Cross(up, z);
    if (!Normalize(x)) {
        // `up` is along the normal: any perpendicular will do.
        x = Cross(std::abs(z.x) < 0.9f ? XrVector3f{1.0f, 0.0f, 0.0f} : XrVector3f{0.0f, 0.0f, 1.0f}, z);
        Normalize(x);
    }
    XrVector3f const y = Cross(z, x);
    return FromBasis(x, y, z);
}

void Panel::Reset() {
    yaw_ = 0.0f;
    turnedFor_ = 0.0f;
    following_ = false;
}

void Panel::FaceHeading(XrQuaternionf const& head) {
    yaw_ = HeadingOf(head);
    turnedFor_ = 0.0f;
    following_ = false;
}

void Panel::Update(XrQuaternionf const& head, bool headValid, PanelSettings const& settings, float dt) {
    if (!headValid || !(dt >= 0.0f) || !std::isfinite(dt)) {
        turnedFor_ = 0.0f;
        return;
    }
    dt = std::min(dt, 0.25f);
    float const delta = WrapAngle(HeadingOf(head) - yaw_);
    if (following_) {
        yaw_ = WrapAngle(yaw_ + delta * std::min(1.0f, dt * kFollowRate));
        if (std::abs(WrapAngle(HeadingOf(head) - yaw_)) < kFollowDone) {
            following_ = false;
            turnedFor_ = 0.0f;
        }
        return;
    }
    if (settings.followAfter > 0.0f && std::abs(delta) > settings.followAfter) {
        turnedFor_ += dt;
        if (turnedFor_ >= settings.followDelay) {
            following_ = true;
        }
    } else {
        turnedFor_ = 0.0f;
    }
}

XrPosef Panel::Pose(PanelSettings const& settings) const {
    XrQuaternionf const yaw = YawRotation(yaw_);
    XrVector3f const offset{0.0f, -settings.drop, -settings.distance};
    XrPosef pose{};
    pose.position = Rotate(yaw, offset);
    // Face the eye (the anchor at the origin), so a panel below eye height tilts back toward it.
    pose.orientation = Facing(Scale(pose.position, -1.0f));
    return pose;
}

char const* PlacementName(Placement placement) {
    switch (placement) {
    case Placement::Kept: return "kept";
    case Placement::Appeared: return "appeared";
    case Placement::KindChanged: return "kind changed";
    case Placement::Recentered: return "recentered";
    case Placement::HeadFound: return "head found";
    case Placement::Followed: return "follows the head";
    }
    return "?";
}

void WorldPanel::PlaceAt(XrPosef const& head, bool headValid) {
    placed_ = true;
    blind_ = !headValid;
    if (headValid) {
        anchor_ = head.position;
        heading_.FaceHeading(head.orientation);
    } else {
        // No head: where recentering puts it (the origin, facing -Z), until the head is found.
        anchor_ = {0.0f, 0.0f, 0.0f};
        heading_.Reset();
    }
}

Placement WorldPanel::Update(int kind, std::uint64_t frame, XrPosef const& head, bool headValid,
    WorldPanelSettings const& settings, float dt) {
    bool const appeared = !placed_ || frame < lastFrame_ || frame - lastFrame_ > settings.gapFrames;
    bool const kindChanged = kind != kind_;
    lastFrame_ = frame;
    kind_ = kind;
    if (recenter_) {
        recenter_ = false;
        PlaceAt(head, headValid);
        return Placement::Recentered;
    }
    if (appeared || kindChanged) {
        Placement const why = appeared ? Placement::Appeared : Placement::KindChanged;
        if (placed_ && !blind_ && headValid) {
            float const turned = std::abs(WrapAngle(HeadingOf(head.orientation) - heading_.Yaw()));
            float const moved = Length(Sub(head.position, anchor_));
            if (turned < settings.keepYaw && moved < settings.keepDistance) {
                return Placement::Kept;
            }
        }
        PlaceAt(head, headValid);
        return why;
    }
    if (blind_ && headValid) {
        PlaceAt(head, true);
        return Placement::HeadFound;
    }
    PanelSettings follow;
    follow.followAfter = settings.followAfter;
    follow.followDelay = settings.followDelay;
    bool const wasFollowing = heading_.Following();
    heading_.Update(head.orientation, headValid, follow, dt);
    if ((wasFollowing || heading_.Following()) && headValid && dt > 0.0f && std::isfinite(dt)) {
        // The eyes it faces move with it: it ends up in front of where the player now stands.
        float const k = std::min(1.0f, std::min(dt, 0.25f) * kFollowRate);
        anchor_ = Add(anchor_, Scale(Sub(head.position, anchor_), k));
    }
    return heading_.Following() && !wasFollowing ? Placement::Followed : Placement::Kept;
}

XrPosef WorldPanel::PoseOf(XrVector3f const& local) const {
    XrQuaternionf const yaw = YawRotation(heading_.Yaw());
    XrPosef pose{};
    pose.orientation = yaw;
    pose.position = Add(anchor_, Rotate(yaw, local));
    return pose;
}

bool PlaceOnRay(XrVector3f const& origin, XrVector3f const& direction, float distance, XrVector3f const& eye,
    float angularSize, Billboard& out) {
    XrVector3f dir = direction;
    if (!Normalize(dir) || !(distance > 0.0f) || !(angularSize > 0.0f) || !std::isfinite(origin.x) ||
        !std::isfinite(origin.y) || !std::isfinite(origin.z)) {
        return false;
    }
    XrVector3f const center = Add(origin, Scale(dir, distance));
    XrVector3f toEye = Sub(eye, center);
    float const range = Length(toEye);
    if (!Normalize(toEye)) {
        return false;
    }
    out.pose.position = center;
    out.pose.orientation = Facing(toEye);
    out.size = 2.0f * range * std::tan(0.5f * angularSize);
    return std::isfinite(out.size) && out.size > 0.0f;
}

} // namespace mevr::vr::overlay
