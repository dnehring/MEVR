// SPDX-License-Identifier: MIT
#include "core/vr/MakoControls.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>
#include <numbers>

namespace mevr::vr::mako_controls {
namespace {

constexpr float kDegrees = 180.0f / std::numbers::pi_v<float>;

XrVector3f Sub(XrVector3f const& a, XrVector3f const& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
float Dot(XrVector3f const& a, XrVector3f const& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
XrVector3f Along(XrVector3f const& at, XrVector3f const& direction, float amount) {
    return {at.x + direction.x * amount, at.y + direction.y * amount, at.z + direction.z * amount};
}
float Distance(XrVector3f const& a, XrVector3f const& b) {
    XrVector3f const d = Sub(a, b);
    return std::sqrt(Dot(d, d));
}
XrQuaternionf Conjugate(XrQuaternionf const& q) {
    return {-q.x, -q.y, -q.z, q.w};
}
XrQuaternionf Multiply(XrQuaternionf const& a, XrQuaternionf const& b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}
XrVector3f Rotate(XrQuaternionf const& q, XrVector3f const& v) {
    XrVector3f const u{q.x, q.y, q.z};
    float const uv = Dot(u, v);
    float const uu = Dot(u, u);
    XrVector3f const c{u.y * v.z - u.z * v.y, u.z * v.x - u.x * v.z, u.x * v.y - u.y * v.x};
    return {2.0f * uv * u.x + (q.w * q.w - uu) * v.x + 2.0f * q.w * c.x, 2.0f * uv * u.y + (q.w * q.w - uu) * v.y + 2.0f * q.w * c.y,
        2.0f * uv * u.z + (q.w * q.w - uu) * v.z + 2.0f * q.w * c.z};
}
float Approach(float value, float target, float maxStep) {
    return value + std::clamp(target - value, -maxStep, maxStep);
}
void Add(Output& out, int hand, float seconds, float amplitude) {
    if (out.hapticCount < static_cast<int>(std::size(out.haptics))) {
        out.haptics[out.hapticCount++] = {hand, seconds, amplitude};
    }
}
bool Latch(bool& down, float squeeze) {
    down = down ? squeeze > kGripOff : squeeze >= kGripOn;
    return down;
}
// A grip pose whose quaternion is usable (a host that has none sends zeros).
bool Usable(XrQuaternionf const& q) {
    float const n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
    return std::isfinite(n) && n > 0.5f && n < 1.5f;
}

std::mutex gMutex;
View gLatest{};
std::uint64_t gSequence = 0;

// ---- Drawing -------------------------------------------------------------------------------------------------------

std::uint32_t Encode(float linear) {
    float const c = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<std::uint32_t>(std::clamp(c, 0.0f, 1.0f) * 255.0f + 0.5f);
}

// A premultiplied colour being built up, front to back ("over").
struct Paint {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
    void Over(float red, float green, float blue, float coverage) {
        r = red * coverage + r * (1.0f - coverage);
        g = green * coverage + g * (1.0f - coverage);
        b = blue * coverage + b * (1.0f - coverage);
        a = coverage + a * (1.0f - coverage);
    }
    std::uint32_t Pixel() const {
        return Encode(r) | (Encode(g) << 8) | (Encode(b) << 16) | (static_cast<std::uint32_t>(std::clamp(a, 0.0f, 1.0f) * 255.0f + 0.5f) << 24);
    }
};

// Anti-aliased coverage of a signed distance (negative inside), `aa` units across the edge.
float Cover(float distance, float aa) {
    return std::clamp(0.5f - distance / aa, 0.0f, 1.0f);
}
// A rounded box's signed distance, centred at the origin.
float BoxDistance(float x, float y, float halfX, float halfY, float round) {
    float const qx = std::abs(x) - halfX + round;
    float const qy = std::abs(y) - halfY + round;
    float const ox = std::max(qx, 0.0f);
    float const oy = std::max(qy, 0.0f);
    return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - round;
}
// A segment's signed distance, as a capsule of `radius`.
float SegmentDistance(float x, float y, float ax, float ay, float bx, float by, float radius) {
    float const dx = bx - ax;
    float const dy = by - ay;
    float const t = std::clamp(((x - ax) * dx + (y - ay) * dy) / std::max(dx * dx + dy * dy, 1e-6f), 0.0f, 1.0f);
    return std::hypot(x - ax - t * dx, y - ay - t * dy) - radius;
}

struct Tint {
    float r, g, b;
};
constexpr Tint kIdle{0.55f, 0.85f, 1.0f};   // the reticle's cyan
constexpr Tint kNear{0.85f, 0.97f, 1.0f};   // a hand within reach
constexpr Tint kHeld{1.0f, 0.72f, 0.25f};   // in the hand

Tint TintOf(bool inReach, bool held) {
    return held ? kHeld : inReach ? kNear : kIdle;
}

} // namespace

char const* Name(Stop stop) {
    switch (stop) {
    case Stop::None: return "still driving";
    case Stop::LeftMako: return "left the Mako's game mode";
    case Stop::Menu: return "our menu opened";
    case Stop::Calibration: return "the calibration is running";
    case Stop::Tracking: return "a controller stopped tracking";
    case Stop::Level: return "the vehicle went away";
    case Stop::Unload: return "the mod is unloading";
    }
    return "?";
}

bool PlaceSeat(XrPosef const& head, Settings const& settings, Seat& out) {
    XrVector3f const look = Rotate(head.orientation, {0.0f, 0.0f, -1.0f});
    float const length = std::hypot(look.x, look.z);
    if (!(length > 0.05f) || !std::isfinite(length)) {
        return false;
    }
    Seat seat;
    seat.forward = {look.x / length, 0.0f, look.z / length};
    seat.right = {-seat.forward.z, 0.0f, seat.forward.x};  // forward x up
    auto const place = [&](XrVector3f const& offset) {
        XrVector3f at = Along(head.position, seat.right, offset.x);
        at.y += offset.y;
        return Along(at, seat.forward, offset.z);
    };
    seat.leverBase = place(settings.leverOffset);
    seat.stickBase = place(settings.stickOffset);
    out = seat;
    return true;
}

float TwistDegrees(XrQuaternionf const& grab, XrQuaternionf const& now, XrVector3f const& axis) {
    // The turn from the grab, in tracking space: swing-twist about the axis. Its twist part alone is
    // (axis * dot(v, axis), w), whose angle is 2 atan2(dot(v, axis), w).
    XrQuaternionf const q = Multiply(now, Conjugate(grab));
    float const along = q.x * axis.x + q.y * axis.y + q.z * axis.z;
    if (std::abs(along) < 1e-7f && std::abs(q.w) < 1e-7f) {
        return 0.0f;  // a half turn about a perpendicular axis: no twist in it
    }
    float const degrees = 2.0f * std::atan2(along, q.w) * kDegrees;
    return std::remainder(degrees, 360.0f);
}

XrVector3f StickAxis(XrQuaternionf const& grip) {
    return Rotate(grip, {0.0f, 0.0f, -1.0f});
}

float SteerFromTwist(float twistDeg, Settings const& settings) {
    float const limit = std::max(settings.stickTwistDeg, 1.0f);
    float const dead = std::clamp(settings.stickDeadZoneDeg, 0.0f, limit - 0.5f);
    float const size = std::abs(twistDeg);
    float const amount = size <= dead ? 0.0f : std::min((size - dead) / (limit - dead), 1.0f);
    // Counter-clockwise from above (the right-hand rule about an axis pointing up) is a turn to the player's left.
    float const steer = twistDeg > 0.0f ? -amount : amount;
    return settings.stickInvert ? -steer : steer;
}

float FromTilt(float tiltDeg, Settings const& settings) {
    return FromTilt(tiltDeg, settings, settings.stickDeadZoneDeg);
}

float FromTilt(float tiltDeg, Settings const& settings, float deadZoneDeg) {
    float const limit = std::max(settings.stickTiltDeg, 1.0f);
    float const dead = std::clamp(deadZoneDeg, 0.0f, limit - 0.5f);
    float const size = std::abs(tiltDeg);
    float const amount = size <= dead ? 0.0f : std::min((size - dead) / (limit - dead), 1.0f);
    float const value = tiltDeg > 0.0f ? amount : -amount;
    return settings.stickInvert ? -value : value;
}

void TiltFromTravel(XrVector3f const& travel, XrVector3f const& right, XrVector3f const& forward, float heightM, float& rightDeg, float& forwardDeg) {
    float const height = std::max(heightM, 0.02f);
    rightDeg = std::atan2(Dot(travel, right), height) * kDegrees;
    forwardDeg = std::atan2(Dot(travel, forward), height) * kDegrees;
}

float ThrottleFromLever(float lever, float detent) {
    float const notch = std::clamp(detent, 0.0f, 0.9f);
    float const size = std::abs(lever);
    if (size <= notch) {
        return 0.0f;
    }
    float const amount = std::min((size - notch) / (1.0f - notch), 1.0f);
    return lever > 0.0f ? amount : -amount;
}

void StepAim(float& yawDeg, float& pitchDeg, float stickX, float stickY, float degreesPerSecond, double seconds) {
    auto const shaped = [](float value) {
        float const size = std::abs(value);
        if (!(size > kAimStickDeadZone)) {
            return 0.0f;
        }
        float const amount = std::min((size - kAimStickDeadZone) / (1.0f - kAimStickDeadZone), 1.0f);
        return value > 0.0f ? amount : -amount;
    };
    float const step = degreesPerSecond * static_cast<float>(std::clamp(seconds, 0.0, 0.25));
    yawDeg = std::remainder(yawDeg + shaped(stickX) * step, 360.0f);
    pitchDeg = std::clamp(pitchDeg + shaped(stickY) * step, kAimPitchMinDeg, kAimPitchMaxDeg);
}

void Controls::SeedAim(float yawDeg, float pitchDeg) {
    aimYaw_ = std::isfinite(yawDeg) ? std::remainder(yawDeg, 360.0f) : 0.0f;
    aimPitch_ = std::isfinite(pitchDeg) ? std::clamp(pitchDeg, kAimPitchMinDeg, kAimPitchMaxDeg) : 0.0f;
}

void Controls::StopAll(Stop stop, Output& out) {
    if (driving_ || lever_ != 0.0f || steer_ != 0.0f) {
        out.stopped = stop;
    }
    driving_ = false;
    lever_ = 0.0f;
    steer_ = 0.0f;
    twist_ = 0.0f;
    tiltRight_ = tiltForward_ = pitch_ = 0.0f;
    spin_ = 0.0f;
    leverHeld_ = false;
    stickHeld_ = false;
    leverInDetent_ = true;
    stickCentred_ = true;
    stickAtLock_ = false;
    if (stop != Stop::Menu && stop != Stop::Calibration && stop != Stop::Tracking) {
        placed_ = false;  // a new drive, a new seat
        haveRecenters_ = false;
    }
}

Output Controls::Step(Input const& in, Settings const& settings) {
    Output out;
    float const dt = static_cast<float>(std::clamp(in.deltaSeconds, 0.0, 0.25));
    bool const leftTracked = in.left.tracked && Usable(in.left.grip.orientation);
    bool const rightTracked = in.right.tracked && Usable(in.right.grip.orientation);
    // The grip latches run whatever happens: a squeeze held through a stop is no new squeeze afterwards.
    bool const leftWas = leftDown_;
    bool const rightWas = rightDown_;
    bool const leftDown = Latch(leftDown_, in.left.squeeze);
    bool const rightDown = Latch(rightDown_, in.right.squeeze);

    untrackedFor_ = leftTracked && rightTracked ? 0.0f : untrackedFor_ + dt;
    Stop stop = in.stop;
    if (stop == Stop::None && untrackedFor_ > kTrackingGraceS) {
        stop = Stop::Tracking;
    }
    if (stop != Stop::None) {
        StopAll(stop, out);
    } else {
        // Placed when the Mako is entered and on each recenter. A recenter moves tracking space under the hands, so the
        // holds are let go (the lever stays where it is: it isn't a safety stop); the next squeeze grabs again.
        bool const recentered = in.haveRecenters && haveRecenters_ && in.recenters != recenters_;
        if ((!placed_ || recentered) && in.headValid) {
            Seat seat;
            if (PlaceSeat(in.head, settings, seat)) {
                seat_ = seat;
                placed_ = true;
                out.replaced = true;
                if (recentered) {
                    leverHeld_ = false;
                    stickHeld_ = false;
                }
            }
        }
        if (in.haveRecenters && (!haveRecenters_ || !recentered || out.replaced)) {
            recenters_ = in.recenters;
            haveRecenters_ = true;
        }

        if (placed_) {
            float const travel = std::max(settings.throttleTravelM, 0.01f);
            XrVector3f const leverAt = Along(seat_.leverBase, seat_.forward, lever_ * travel);
            out.leverNear = leftTracked && Distance(in.left.grip.position, leverAt) <= settings.grabRadiusM;
            out.stickNear = rightTracked && Distance(in.right.grip.position, seat_.stickBase) <= settings.grabRadiusM;

            // The lever. A squeeze that began out of reach never grabs (only a NEW squeeze in reach does).
            if (leverHeld_ && !leftDown) {
                leverHeld_ = false;
            } else if (!leverHeld_ && leftDown && !leftWas && out.leverNear) {
                leverHeld_ = true;
                leverLast_ = in.left.grip.position;
                driving_ = true;
                out.leverGrabbed = true;
                Add(out, 0, 0.03f, 0.35f);
            }
            if (!leverHeld_ && settings.leverReturns) {
                lever_ = Approach(lever_, 0.0f, kLeverReturnRate * dt);
            }
            if (leverHeld_ && leftTracked) {
                // Travel since the last step along the seat's forward: pushing past an end doesn't wind up.
                lever_ = std::clamp(lever_ + Dot(Sub(in.left.grip.position, leverLast_), seat_.forward) / travel, -1.0f, 1.0f);
                leverLast_ = in.left.grip.position;
            }
            bool const inDetent = ThrottleFromLever(lever_, settings.throttleDetent) == 0.0f;
            if (inDetent != leverInDetent_) {
                leverInDetent_ = inDetent;
                if (inDetent && leverHeld_) {
                    Add(out, 0, 0.03f, 0.5f);  // the notch at neutral
                }
            }

            // The stick.
            if (stickHeld_ && !rightDown) {
                stickHeld_ = false;
            } else if (!stickHeld_ && rightDown && !rightWas && out.stickNear) {
                stickHeld_ = true;
                grabOrientation_ = in.right.grip.orientation;
                axis_ = StickAxis(grabOrientation_);
                stickGrab_ = in.right.grip.position;
                stickSettle_ = kGrabSettleS;
                twist_ = 0.0f;
                twistRaw_ = 0.0f;
                driving_ = true;
                out.stickGrabbed = true;
                Add(out, 1, 0.03f, 0.35f);
            }
            if (stickHeld_ && rightTracked) {
                if (!settings.stickTwist && stickSettle_ > 0.0f) {
                    stickSettle_ -= dt;
                    stickGrab_ = in.right.grip.position;  // the squeeze settling: no tilt or twist yet
                    grabOrientation_ = in.right.grip.orientation;
                    axis_ = StickAxis(grabOrientation_);
                    twist_ = twistRaw_ = 0.0f;
                }
                // The twist (the steering in twist mode; added to the tilt's otherwise, the user, 2026-11-03: turning the stick
                // about its axis did nothing). Unwrapped: past half a turn the raw angle jumps by a whole turn, the steering must not.
                float const raw = TwistDegrees(grabOrientation_, in.right.grip.orientation, axis_);
                twist_ += std::remainder(raw - twistRaw_, 360.0f);
                twistRaw_ = raw;
                if (!settings.stickTwist) {
                    // Held past full tilt, the stick stops there (the hand's further travel tilts nothing).
                    float rightDeg = 0.0f;
                    float forwardDeg = 0.0f;
                    TiltFromTravel(Sub(in.right.grip.position, stickGrab_), seat_.right, seat_.forward, settings.stickHeightM, rightDeg, forwardDeg);
                    float const limit = std::max(settings.stickTiltDeg, 1.0f);
                    tiltRight_ = std::clamp(rightDeg, -limit, limit);
                    tiltForward_ = std::clamp(forwardDeg, -limit, limit);
                }
            }
            if (!stickHeld_ && !settings.stickTwist) {
                // Springing back to centre at the steering's return rate.
                float const back = kSteerRateReturn * std::max(settings.stickTiltDeg, 1.0f) * dt;
                tiltRight_ = Approach(tiltRight_, 0.0f, back);
                tiltForward_ = Approach(tiltForward_, 0.0f, back);
            }
            // The tilt steers up to tiltSteerMax; the twist adds to it (to full lock, or against it), and alone it spins.
            float const twistPart = stickHeld_ ? SteerFromTwist(twist_, settings) : 0.0f;
            float const tiltPart = stickHeld_ && !settings.stickTwist ? FromTilt(tiltRight_, settings) * std::clamp(settings.tiltSteerMax, 0.0f, 1.0f) : 0.0f;
            float const target = std::clamp(tiltPart + twistPart, -1.0f, 1.0f);
            float const rate = (stickHeld_ ? kSteerRateHeld : kSteerRateReturn) * dt;
            steer_ = Approach(steer_, target, rate);
            spin_ = Approach(spin_, twistPart, rate);
            pitch_ = settings.stickTwist ? 0.0f : FromTilt(tiltForward_, settings, settings.stickPitchDeadZoneDeg);
            if (!stickHeld_) {
                twist_ = 0.0f;
            }
            bool const centred = steer_ == 0.0f;
            if (centred != stickCentred_) {
                stickCentred_ = centred;
                if (centred && stickHeld_) {
                    Add(out, 1, 0.03f, 0.5f);  // a tick crossing centre
                }
            }
            bool const atLock = std::abs(steer_) >= 1.0f;
            if (atLock && !stickAtLock_ && stickHeld_) {
                Add(out, 1, 0.08f, 0.9f);  // the stop at full lock
            }
            stickAtLock_ = atLock || (stickAtLock_ && std::abs(steer_) > 0.9f);

            if (stickHeld_) {
                StepAim(aimYaw_, aimPitch_, in.stickX, in.stickY, settings.stickAimRate, dt);
            }
            float const rumble = std::clamp(settings.rumble, 0.0f, 1.0f) * std::clamp(in.engineSpeed, 0.0f, 1.0f);
            out.rumbleLeft = leverHeld_ ? rumble : 0.0f;
            out.rumbleRight = stickHeld_ ? rumble : 0.0f;
        }
    }
    out.lever = lever_;
    out.throttle = ThrottleFromLever(lever_, settings.throttleDetent);
    if (stickHeld_ && spin_ != 0.0f && std::abs(out.throttle) < settings.steerInPlaceBelow) {
        out.throttle = 0.0f;  // the twist turning the Mako on the spot
    }
    out.spin = spin_;
    out.steer = steer_;
    out.twistDeg = twist_;
    out.tiltRightDeg = tiltRight_;
    out.tiltForwardDeg = tiltForward_;
    out.pitch = pitch_;
    out.driving = driving_;
    out.placed = placed_;
    out.leverHeld = leverHeld_;
    out.stickHeld = stickHeld_;
    out.aimYawDeg = aimYaw_;
    out.aimPitchDeg = aimPitch_;
    return out;
}

void Publish(View const& view) {
    std::lock_guard lock(gMutex);
    gLatest = view;
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

View Latest(std::uint64_t maxAgeMs) {
    std::lock_guard lock(gMutex);
    if (gLatest.sequence == 0 || GetTickCount64() - gLatest.timeMs > maxAgeMs) {
        View none;
        none.size = 0;
        return none;
    }
    return gLatest;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = View{};
    gSequence = 0;
}

XrPosef FlatPose(XrVector3f const& at, XrVector3f const& forward) {
    // Tracking space's -Z turned about +Y to `forward`, then tipped back 90 degrees about the seat's right, so the quad's
    // +Y (its top edge) lies along forward and its +Z (its front) points up.
    float const yaw = std::atan2(-forward.x, -forward.z);
    XrQuaternionf const turn{0.0f, std::sin(yaw * 0.5f), 0.0f, std::cos(yaw * 0.5f)};
    float const half = std::numbers::pi_v<float> * -0.25f;
    XrQuaternionf const tip{std::sin(half), 0.0f, 0.0f, std::cos(half)};
    XrPosef pose{};
    pose.orientation = Multiply(turn, tip);
    pose.position = at;
    return pose;
}

std::vector<std::uint32_t> LeverPixels(float lever, float travelM, float detent, bool inReach, bool held) {
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kLeverPixelsWide) * kLeverPixelsLong);
    float const metersPerPixel = kLeverLengthM / static_cast<float>(kLeverPixelsLong);
    Tint const tint = TintOf(inReach, held);
    float const position = std::clamp(lever, -1.0f, 1.0f) * travelM;  // + toward the seat's forward, meters
    float const notch = std::clamp(detent, 0.0f, 0.9f) * travelM;
    for (std::uint32_t row = 0; row < kLeverPixelsLong; ++row) {
        for (std::uint32_t column = 0; column < kLeverPixelsWide; ++column) {
            // Meters from the quad's centre: x right, y toward the seat's forward (row 0).
            float const x = (static_cast<float>(column) + 0.5f - static_cast<float>(kLeverPixelsWide) * 0.5f) * metersPerPixel;
            float const y = (static_cast<float>(kLeverPixelsLong) * 0.5f - static_cast<float>(row) - 0.5f) * metersPerPixel;
            Paint paint;
            // The slot, a dark bed the whole travel long and a light line down its middle: amber behind neutral (reverse).
            paint.Over(0.0f, 0.0f, 0.0f, 0.45f * Cover(BoxDistance(x, y, 0.009f, travelM + 0.008f, 0.008f), metersPerPixel));
            float const line = Cover(BoxDistance(x, y, 0.002f, travelM, 0.002f), metersPerPixel);
            if (y >= 0.0f) {
                paint.Over(kIdle.r, kIdle.g, kIdle.b, 0.8f * line);
            } else {
                paint.Over(1.0f, 0.55f, 0.2f, 0.8f * line);
            }
            // The notch at neutral: a bar across the slot, the detent's width.
            paint.Over(kNear.r, kNear.g, kNear.b, 0.9f * Cover(BoxDistance(x, y, 0.016f, std::max(notch * 0.25f, 0.0015f), 0.001f), metersPerPixel));
            // The handle, where the lever physically is.
            float const handle = BoxDistance(x, y - position, 0.024f, 0.013f, 0.006f);
            paint.Over(0.02f, 0.03f, 0.04f, 0.65f * Cover(handle - 0.003f, metersPerPixel));
            paint.Over(tint.r, tint.g, tint.b, Cover(handle, metersPerPixel));
            pixels[static_cast<std::size_t>(row) * kLeverPixelsWide + column] = paint.Pixel();
        }
    }
    return pixels;
}

std::vector<std::uint32_t> StickPixels(float steer, bool inReach, bool held) {
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(kStickPixels) * kStickPixels);
    float const metersPerPixel = kStickSizeM / static_cast<float>(kStickPixels);
    float const half = kStickSizeM * 0.5f;
    Tint const tint = TintOf(inReach, held);
    // The pointer swings up to 90 degrees each way: clockwise from above is to the player's right.
    float const angle = std::clamp(steer, -1.0f, 1.0f) * std::numbers::pi_v<float> * 0.5f;
    float const tipX = std::sin(angle);
    float const tipY = std::cos(angle);
    Tint const pointer = std::abs(steer) >= 1.0f ? Tint{1.0f, 0.35f, 0.2f} : tint;  // red at full lock
    for (std::uint32_t row = 0; row < kStickPixels; ++row) {
        for (std::uint32_t column = 0; column < kStickPixels; ++column) {
            float const x = (static_cast<float>(column) + 0.5f) * metersPerPixel - half;
            float const y = half - (static_cast<float>(row) + 0.5f) * metersPerPixel;
            float const r = std::hypot(x, y);
            Paint paint;
            paint.Over(0.0f, 0.0f, 0.0f, 0.4f * Cover(r - half * 0.92f, metersPerPixel));  // the dial's dark disc
            paint.Over(tint.r, tint.g, tint.b, 0.9f * Cover(std::abs(r - half * 0.86f) - 0.0016f, metersPerPixel));  // its ring
            // The marks: straight ahead, and full lock each way.
            float const mark = std::min({SegmentDistance(x, y, 0.0f, half * 0.68f, 0.0f, half * 0.8f, 0.0012f),
                SegmentDistance(x, y, half * 0.68f, 0.0f, half * 0.8f, 0.0f, 0.0012f),
                SegmentDistance(x, y, -half * 0.68f, 0.0f, -half * 0.8f, 0.0f, 0.0012f)});
            paint.Over(kNear.r, kNear.g, kNear.b, 0.85f * Cover(mark, metersPerPixel));
            float const needle = SegmentDistance(x, y, 0.0f, 0.0f, tipX * half * 0.62f, tipY * half * 0.62f, 0.0035f);
            paint.Over(0.02f, 0.03f, 0.04f, 0.65f * Cover(needle - 0.002f, metersPerPixel));
            paint.Over(pointer.r, pointer.g, pointer.b, Cover(needle, metersPerPixel));
            paint.Over(tint.r, tint.g, tint.b, Cover(r - half * 0.14f, metersPerPixel));  // the hub
            pixels[static_cast<std::size_t>(row) * kStickPixels + column] = paint.Pixel();
        }
    }
    return pixels;
}

std::uint32_t LeverKey(float lever, float detent, bool inReach, bool held) {
    auto const step = static_cast<int>(std::lround(std::clamp(lever, -1.0f, 1.0f) * 64.0f)) + 64;
    auto const notch = static_cast<int>(std::lround(std::clamp(detent, 0.0f, 1.0f) * 100.0f));
    return static_cast<std::uint32_t>(step) | (static_cast<std::uint32_t>(notch) << 8) | (inReach ? 1u << 16 : 0u) | (held ? 1u << 17 : 0u);
}

std::uint32_t StickKey(float steer, bool inReach, bool held) {
    auto const step = static_cast<int>(std::lround(std::clamp(steer, -1.0f, 1.0f) * 64.0f)) + 64;
    return static_cast<std::uint32_t>(step) | (inReach ? 1u << 8 : 0u) | (held ? 1u << 9 : 0u);
}

std::string Describe(Settings const& s) {
    std::string const stick = s.stickTwist ? std::format("{:.0f} degrees of twist", s.stickTwistDeg)
                                           : std::format("{:.0f} degrees of tilt (the grip {:.2f} m over its pivot; forward and back pitch "
                                                         "the Mako on its thrusters)", s.stickTiltDeg, s.stickHeightM);
    return std::format("physical controls {}: grab within {:.2f} m, full lock at {} (dead zone {:.0f}{}), full throttle at "
                       "{:.2f} m (detent {:.2f}), lever at ({:.2f}, {:.2f}, {:.2f}), stick at ({:.2f}, {:.2f}, {:.2f}) m (right, up, forward "
                       "from the head), the thumbstick turns the turret {:.0f} deg/s, rumble {:.2f}, the camera {} the vehicle",
        s.controls ? "on" : "off", s.grabRadiusM, stick, s.stickDeadZoneDeg, s.stickInvert ? ", inverted" : "", s.throttleTravelM,
        s.throttleDetent, s.leverOffset.x, s.leverOffset.y, s.leverOffset.z, s.stickOffset.x, s.stickOffset.y, s.stickOffset.z, s.stickAimRate,
        s.rumble, s.cameraFollow ? "follows" : "doesn't follow");
}

} // namespace mevr::vr::mako_controls
