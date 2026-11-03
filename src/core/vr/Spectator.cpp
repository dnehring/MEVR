// SPDX-License-Identifier: MIT
#include "core/vr/Spectator.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace mevr::vr::spectator {
namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kDegToRad = kPi / 180.0f;

std::mutex gMutex;
Frame gHistory[kHistory];
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

// One axis of the critically damped spring (Game Programming Gems 4, "Critically Damped Ease-In/Ease-Out Smoothing"):
// `change` is current minus target; returns the new change and updates the velocity. Never passes the target.
float Damp(float change, float& velocity, float omega, float dt) {
    float const x = omega * dt;
    float const decay = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float const temp = (velocity + omega * change) * dt;
    velocity = (velocity - omega * temp) * decay;
    float next = (change + temp) * decay;
    if ((change > 0.0f && next < 0.0f) || (change < 0.0f && next > 0.0f)) {  // an overshoot: stop at the target
        next = 0.0f;
        velocity = 0.0f;
    }
    return next;
}

} // namespace

float TanHalfX(float fovDeg) {
    return std::tan(std::clamp(fovDeg, 1.0f, 170.0f) * 0.5f * kDegToRad);
}

float TanHalfY(float fovDeg, int width, int height) {
    float const aspect = width > 0 && height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 16.0f / 9.0f;
    return TanHalfX(fovDeg) / aspect;
}

XrFovf Fov(float fovDeg, int width, int height) {
    float const x = std::atan(TanHalfX(fovDeg));
    float const y = std::atan(TanHalfY(fovDeg, width, height));
    return {-x, x, y, -y};
}

XrQuaternionf Multiply(XrQuaternionf const& a, XrQuaternionf const& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

XrQuaternionf Conjugate(XrQuaternionf const& q) {
    return {-q.x, -q.y, -q.z, q.w};
}

XrQuaternionf Normalize(XrQuaternionf const& q) {
    float const n = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (!(n > 1e-12f) || !std::isfinite(n)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    return {q.x / n, q.y / n, q.z / n, q.w / n};
}

XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    XrVector3f const u{q.x, q.y, q.z};
    XrVector3f const t{2.0f * (u.y * v.z - u.z * v.y), 2.0f * (u.z * v.x - u.x * v.z), 2.0f * (u.x * v.y - u.y * v.x)};
    return {v.x + q.w * t.x + (u.y * t.z - u.z * t.y), v.y + q.w * t.y + (u.z * t.x - u.x * t.z),
        v.z + q.w * t.z + (u.x * t.y - u.y * t.x)};
}

XrVector3f Log(XrQuaternionf const& q0) {
    XrQuaternionf q = Normalize(q0);
    if (q.w < 0.0f) {  // the short way round
        q = {-q.x, -q.y, -q.z, -q.w};
    }
    float const s = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z);
    if (!(s > 1e-7f)) {
        return {0.0f, 0.0f, 0.0f};
    }
    float const angle = 2.0f * std::atan2(s, q.w);
    return {q.x / s * angle, q.y / s * angle, q.z / s * angle};
}

XrQuaternionf Exp(XrVector3f const& v) {
    float const angle = Length(v);
    if (!(angle > 1e-7f) || !std::isfinite(angle)) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    float const s = std::sin(0.5f * angle) / angle;
    return {v.x * s, v.y * s, v.z * s, std::cos(0.5f * angle)};
}

float AngleBetween(XrQuaternionf const& a, XrQuaternionf const& b) {
    return Length(Log(Multiply(b, Conjugate(a))));
}

void Snap(Follow& follow, XrPosef const& target) {
    follow.valid = true;
    follow.pose = target;
    follow.pose.orientation = Normalize(target.orientation);
    follow.velocity = {0.0f, 0.0f, 0.0f};
    follow.angularVelocity = {0.0f, 0.0f, 0.0f};
}

void Step(Follow& follow, XrPosef const& target, float dtS, float smoothS) {
    if (!follow.valid || !(smoothS > 0.0f) || !std::isfinite(smoothS)) {
        Snap(follow, target);
        return;
    }
    float const dt = std::isfinite(dtS) ? std::clamp(dtS, 0.0f, 0.25f) : 0.0f;
    if (!(dt > 0.0f)) {
        return;
    }
    float const omega = 2.0f / smoothS;
    // Position: each axis on its own.
    float change[3] = {follow.pose.position.x - target.position.x, follow.pose.position.y - target.position.y,
        follow.pose.position.z - target.position.z};
    float* velocity[3] = {&follow.velocity.x, &follow.velocity.y, &follow.velocity.z};
    for (int i = 0; i < 3; ++i) {
        change[i] = Damp(change[i], *velocity[i], omega, dt);
    }
    follow.pose.position = {target.position.x + change[0], target.position.y + change[1], target.position.z + change[2]};
    // Orientation: the rotation from the target to the current pose as a vector (current = Exp(e) * target), damped
    // the same way, then back.
    XrQuaternionf const targetQ = Normalize(target.orientation);
    XrVector3f e = Log(Multiply(follow.pose.orientation, Conjugate(targetQ)));
    float* angular[3] = {&follow.angularVelocity.x, &follow.angularVelocity.y, &follow.angularVelocity.z};
    float* components[3] = {&e.x, &e.y, &e.z};
    for (int i = 0; i < 3; ++i) {
        *components[i] = Damp(*components[i], *angular[i], omega, dt);
    }
    follow.pose.orientation = Normalize(Multiply(Exp(e), targetQ));
}

void Publish(Frame const& frame) {
    std::lock_guard lock(gMutex);
    Frame f = frame;
    f.size = sizeof(Frame);
    f.sequence = ++gSequence;
    f.timeMs = GetTickCount64();
    gHistory[f.sequence % kHistory] = f;
}

Frame Latest(std::uint64_t maxAgeMs) {
    Frame copy;
    {
        std::lock_guard lock(gMutex);
        if (gSequence == 0) {
            return Frame{};
        }
        copy = gHistory[gSequence % kHistory];
    }
    if (copy.size != sizeof(Frame) || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.state = kOff;
    }
    return copy;
}

std::optional<Frame> BySequence(std::uint64_t sequence) {
    std::lock_guard lock(gMutex);
    if (sequence == 0 || sequence > gSequence || gSequence - sequence >= kHistory) {
        return std::nullopt;
    }
    Frame const& f = gHistory[sequence % kHistory];
    if (f.sequence != sequence) {
        return std::nullopt;
    }
    return f;
}

std::uint64_t LatestSequence() {
    std::lock_guard lock(gMutex);
    return gSequence;
}

void Reset() {
    std::lock_guard lock(gMutex);
    for (Frame& f : gHistory) {
        f = Frame{};
    }
    for (ArmEntry& a : gArms) {  // the sequence keeps counting: an old arm can't match a later publish by accident
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
    int const last = (gArmNext + kArms - 1) % kArms;
    if (gArms[last].arm == arm) {  // several views of one Draw latch the same arm: the newest note replaces the last
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

} // namespace mevr::vr::spectator
