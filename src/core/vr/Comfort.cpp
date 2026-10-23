// SPDX-License-Identifier: MIT
#include "core/vr/Comfort.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <numbers>
#include <string>

namespace mevr::vr::comfort {
namespace {

constexpr double kUnitsPerDegree = 65536.0 / 360.0;
constexpr double kRadians = std::numbers::pi / 180.0;

std::string Lower(std::string_view text) {
    std::string out;
    for (char const c : text) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

float Finite(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float SmoothStep(float edge0, float edge1, float x) {
    float const t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

char const* Name(TurnStyle style) {
    switch (style) {
    case TurnStyle::Game: return "game";
    case TurnStyle::Smooth: return "smooth";
    case TurnStyle::Snap: return "snap";
    }
    return "smooth";
}

std::optional<TurnStyle> ParseTurn(std::string_view text) {
    std::string const t = Lower(text);
    if (t == "smooth") {
        return TurnStyle::Smooth;
    }
    if (t == "snap") {
        return TurnStyle::Snap;
    }
    if (t == "game" || t == "native") {
        return TurnStyle::Game;
    }
    return std::nullopt;
}

char const* Name(VignetteWhen when) {
    return when == VignetteWhen::TurningAndMoving ? "move" : "turn";
}

std::optional<VignetteWhen> ParseVignetteWhen(std::string_view text) {
    std::string const t = Lower(text);
    if (t == "turn" || t == "turning") {
        return VignetteWhen::Turning;
    }
    if (t == "move" || t == "moving" || t == "turnandmove" || t == "both") {
        return VignetteWhen::TurningAndMoving;
    }
    return std::nullopt;
}

char const* Name(WalkToward walk) {
    return walk == WalkToward::LeftHand ? "hand" : "head";
}

std::optional<WalkToward> ParseWalk(std::string_view text) {
    std::string const t = Lower(text);
    if (t == "head" || t == "look") {
        return WalkToward::Head;
    }
    if (t == "hand" || t == "lefthand" || t == "left") {
        return WalkToward::LeftHand;
    }
    return std::nullopt;
}

Settings Sanitize(Settings s) {
    Settings const d;
    s.snapDegrees = std::clamp(Finite(s.snapDegrees, d.snapDegrees), 5.0f, 180.0f);
    s.turnSpeed = std::clamp(Finite(s.turnSpeed, d.turnSpeed), 10.0f, 720.0f);
    s.deadzone = std::clamp(Finite(s.deadzone, d.deadzone), 0.0f, 0.9f);
    s.snapThreshold = std::clamp(Finite(s.snapThreshold, d.snapThreshold), 0.2f, 0.98f);
    s.snapRearm = std::clamp(Finite(s.snapRearm, d.snapRearm), 0.05f, s.snapThreshold - 0.05f);
    s.snapCooldown = std::clamp(Finite(s.snapCooldown, d.snapCooldown), 0.0f, 2.0f);
    s.vignetteStrength = std::clamp(Finite(s.vignetteStrength, d.vignetteStrength), 0.0f, 1.0f);
    s.moveThreshold = std::clamp(Finite(s.moveThreshold, d.moveThreshold), 0.05f, 0.95f);
    return s;
}

float StepSnap(SnapTurn& snap, float stickX, double now, Settings const& settings) {
    float const x = Finite(stickX, 0.0f);
    float const magnitude = std::abs(x);
    if (!snap.armed) {
        if (magnitude < settings.snapRearm) {
            snap.armed = true;
        }
        return 0.0f;
    }
    if (magnitude < settings.snapThreshold || now < snap.readyAt) {
        return 0.0f;
    }
    snap.armed = false;
    snap.readyAt = now + settings.snapCooldown;
    return x > 0.0f ? settings.snapDegrees : -settings.snapDegrees;
}

bool StickTurns(float stickX, Settings const& settings) {
    return std::abs(Finite(stickX, 0.0f)) > settings.deadzone;
}

double SmoothTurnDegrees(float stickX, double seconds, Settings const& settings) {
    float const x = std::clamp(Finite(stickX, 0.0f), -1.0f, 1.0f);
    float const magnitude = std::abs(x);
    if (magnitude <= settings.deadzone || !(seconds > 0.0)) {
        return 0.0;
    }
    double const past = (magnitude - settings.deadzone) / std::max(1e-3f, 1.0f - settings.deadzone);
    double const seconds_ = std::min(seconds, 0.1);  // a hitch doesn't spin the player
    return (x > 0.0f ? 1.0 : -1.0) * std::min(past, 1.0) * settings.turnSpeed * seconds_;
}

int YawUnits::Take(double degrees) {
    if (!std::isfinite(degrees)) {
        return 0;
    }
    double const total = remainder + degrees * kUnitsPerDegree;
    double const whole = std::trunc(total);
    remainder = total - whole;
    return static_cast<int>(whole);
}

CameraGuess TurnCamera(double x, double y, int yaw, int units, bool pawnTurned, double pivotX, double pivotY) {
    CameraGuess out{x, y, yaw + units};
    if (pawnTurned && units != 0) {
        double const a = static_cast<double>(units) * 2.0 * std::numbers::pi / 65536.0;
        double const c = std::cos(a);
        double const s = std::sin(a);
        double const dx = x - pivotX;
        double const dy = y - pivotY;
        out.x = pivotX + dx * c - dy * s;
        out.y = pivotY + dx * s + dy * c;
    }
    return out;
}

float VignetteFade::Step(float target, float seconds, float fadeSeconds) {
    float const t = std::clamp(Finite(target, 0.0f), 0.0f, 1.0f);
    float const dt = std::clamp(Finite(seconds, 0.0f), 0.0f, 0.1f);
    float const step = fadeSeconds > 0.0f ? dt / fadeSeconds : 1.0f;
    if (value < t) {
        value = std::min(t, value + step);
    } else if (value > t) {
        value = std::max(t, value - step);
    }
    return value;
}

float VignetteDrive::Target(bool turning, bool moving, bool snapped, double now, Settings const& settings) {
    if (snapped) {
        pulseUntil = now + kSnapPulseSeconds;
    }
    if (!settings.vignette) {
        return 0.0f;
    }
    bool const wanted = turning || now < pulseUntil || (moving && settings.vignetteWhen == VignetteWhen::TurningAndMoving);
    return wanted ? settings.vignetteStrength : 0.0f;
}

std::vector<std::uint8_t> VignetteMask(int width, int height, float halfWidth, float halfHeight, float innerDegrees,
    float outerDegrees) {
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0)));
    if (width <= 0 || height <= 0) {
        return mask;
    }
    float const inner = innerDegrees * static_cast<float>(kRadians);
    float const outer = std::max(outerDegrees, innerDegrees + 1.0f) * static_cast<float>(kRadians);
    for (int y = 0; y < height; ++y) {
        float const v = ((static_cast<float>(y) + 0.5f) / static_cast<float>(height) * 2.0f - 1.0f) * halfHeight;
        for (int x = 0; x < width; ++x) {
            float const u = ((static_cast<float>(x) + 0.5f) / static_cast<float>(width) * 2.0f - 1.0f) * halfWidth;
            float const angle = std::atan(std::sqrt(u * u + v * v));  // the quad is 1 m ahead
            float const a = SmoothStep(inner, outer, angle);
            mask[static_cast<std::size_t>(y) * width + x] = static_cast<std::uint8_t>(std::lround(a * 255.0f));
        }
    }
    return mask;
}

void VignettePixels(std::vector<std::uint8_t> const& mask, float strength, std::vector<std::uint32_t>& pixels) {
    pixels.resize(mask.size());
    std::uint32_t const scale = static_cast<std::uint32_t>(std::lround(std::clamp(Finite(strength, 0.0f), 0.0f, 1.0f) * 256.0f));
    for (std::size_t i = 0; i < mask.size(); ++i) {
        std::uint32_t const a = (static_cast<std::uint32_t>(mask[i]) * scale) >> 8;
        pixels[i] = a << 24;  // R, G, B 0: premultiplied black
    }
}

HalfExtents CoverFov(float const fov[2][4]) {
    float const limit = 75.0f * static_cast<float>(kRadians);
    float horizontal = 0.0f;
    float vertical = 0.0f;
    for (int e = 0; e < 2; ++e) {
        horizontal = std::max({horizontal, std::abs(Finite(fov[e][0], 0.0f)), std::abs(Finite(fov[e][1], 0.0f))});
        vertical = std::max({vertical, std::abs(Finite(fov[e][2], 0.0f)), std::abs(Finite(fov[e][3], 0.0f))});
    }
    float const minimum = 30.0f * static_cast<float>(kRadians);
    horizontal = std::clamp(horizontal, minimum, limit);
    vertical = std::clamp(vertical, minimum, limit);
    // The eyes sit about 32 mm either side of the view's centre: a little more room, and 5% for good measure.
    return {std::tan(horizontal) * 1.05f + 0.04f, std::tan(vertical) * 1.05f + 0.02f};
}

std::optional<double> FlatYawLeft(float x, float y, float z) {
    double const flat = std::hypot(static_cast<double>(x), static_cast<double>(z));
    double const length = std::sqrt(flat * flat + static_cast<double>(y) * y);
    if (!(length > 1e-6) || flat / length < 0.26) {  // within about 75 degrees of vertical
        return std::nullopt;
    }
    return std::atan2(-static_cast<double>(x), -static_cast<double>(z));
}

void Forward(float qx, float qy, float qz, float qw, float& x, float& y, float& z) {
    // -Z rotated by q.
    x = -2.0f * (qx * qz + qw * qy);
    y = -2.0f * (qy * qz - qw * qx);
    z = -(1.0f - 2.0f * (qx * qx + qy * qy));
}

int HandWalkUnits(double handYawLeft, int headLookUnits) {
    if (!std::isfinite(handYawLeft)) {
        return 0;
    }
    // Halcyon then turns the stick left by headLookUnits; RotateStick turns it right by what we return. The two
    // together turn it left by the hand's yaw.
    double const handUnits = handYawLeft * 32768.0 / std::numbers::pi;
    double wanted = static_cast<double>(headLookUnits) - handUnits;
    wanted = std::remainder(wanted, 65536.0);
    return static_cast<int>(std::lround(wanted));
}

} // namespace mevr::vr::comfort
