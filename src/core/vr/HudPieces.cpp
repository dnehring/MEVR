// SPDX-License-Identifier: MIT
#include "core/vr/HudPieces.hpp"

#include "core/vr/OverlayPlacement.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <mutex>

namespace mevr::vr::hud_pieces {
namespace {

std::mutex gMutex;
Pieces gLatest{};
std::uint64_t gSequence = 0;

constexpr float kStageWidth = 1280.0f;
// The player's status (the health and shield arc, the squad's portraits and bars, the medi-gel count): stage x, and
// stage pixels up from the frame's bottom edge.
constexpr float kWristLeft = 440.0f;
constexpr float kWristRight = 840.0f;
constexpr float kWristAboveBottom = 165.0f;
constexpr float kWristBelowTop = 25.0f;
// The weapon panel (the gun's silhouette, its heat strip, the grenade count, the panel's bracket).
constexpr float kGunLeft = 56.0f;
constexpr float kGunRight = 224.0f;
constexpr float kGunAboveBottom = 145.0f;
constexpr float kGunBelowTop = 10.0f;

Rect BottomBand(std::uint32_t width, std::uint32_t height, float left, float right, float aboveBottom, float belowTop) {
    if (width == 0 || height == 0) {
        return {};
    }
    float const scale = static_cast<float>(width) / kStageWidth;  // frame pixels per stage pixel
    float const h = static_cast<float>(height);
    Rect rect;
    rect.x = left / kStageWidth;
    rect.width = (right - left) / kStageWidth;
    float const top = std::clamp((h - aboveBottom * scale) / h, 0.0f, 1.0f);
    float const bottom = std::clamp((h - belowTop * scale) / h, top, 1.0f);
    rect.y = top;
    rect.height = bottom - top;
    return rect;
}

bool Finite(XrVector3f const& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

float Length(XrVector3f const& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

bool ReadFlag(ini::File const& file, char const* key, bool& out) {
    if (auto const value = ini::GetNumber(file, "Hud", key)) {
        out = *value != 0.0;
        return true;
    }
    return false;
}

bool ReadRange(ini::File const& file, char const* key, float lo, float hi, float& out, std::string* problems) {
    auto const text = ini::Get(file, "Hud", key);
    if (!text || ini::Trim(*text).empty()) {
        return false;
    }
    auto const value = ini::Number(*text);
    if (!value || !std::isfinite(*value) || *value < lo || *value > hi) {
        if (problems != nullptr) {
            *problems += std::format("{}{} = {} isn't a number between {} and {}; its default kept", problems->empty() ? "" : "\n", key,
                *text, lo, hi);
        }
        return false;
    }
    out = static_cast<float>(*value);
    return true;
}

bool ReadCrop(ini::File const& file, char const* key, Rect& out, std::string* problems) {
    auto const text = ini::Get(file, "Hud", key);
    if (!text || ini::Trim(*text).empty()) {
        return false;
    }
    float parts[4] = {};
    if (ParseNumbers(*text, 4, parts) && parts[2] > 0.0f && parts[3] > 0.0f && parts[0] >= 0.0f && parts[1] >= 0.0f &&
        parts[0] + parts[2] <= 1.0f && parts[1] + parts[3] <= 1.0f) {
        out = {parts[0], parts[1], parts[2], parts[3]};
        return true;
    }
    if (problems != nullptr) {
        *problems += std::format("{}{} = {} isn't x y width height (fractions of the HUD frame); the measured band kept",
            problems->empty() ? "" : "\n", key, *text);
    }
    return false;
}

} // namespace

void Publish(Pieces const& pieces) {
    std::lock_guard lock(gMutex);
    gLatest = pieces;
    gLatest.size = sizeof(Pieces);
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
}

Pieces Latest(std::uint64_t maxAgeMs) {
    Pieces copy;
    {
        std::lock_guard lock(gMutex);
        copy = gLatest;
    }
    if (copy.timeMs == 0 || GetTickCount64() - copy.timeMs > maxAgeMs) {
        copy.size = 0;
        copy.gunVisible = false;
    }
    return copy;
}

void Reset() {
    std::lock_guard lock(gMutex);
    gLatest = {};
}

bool ParseNumbers(std::string_view text, int count, float* out) {
    int n = 0;
    for (std::size_t at = 0; at < text.size();) {
        std::size_t const end = text.find_first_of(" ,\t", at);
        std::string_view const token = text.substr(at, end == std::string_view::npos ? std::string_view::npos : end - at);
        if (!token.empty()) {
            auto const value = ini::Number(token);
            if (!value || !std::isfinite(*value) || n >= count) {
                return false;
            }
            out[n++] = static_cast<float>(*value);
        }
        at = end == std::string_view::npos ? text.size() : end + 1;
    }
    return n == count;
}

Settings ReadSettings(ini::File const& file, std::string* problems) {
    Settings s;
    if (problems != nullptr) {
        problems->clear();
    }
    ReadFlag(file, "wristStatus", s.wristStatus);
    ReadFlag(file, "gunStatus", s.gunStatus);
    ReadRange(file, "wristWidthM", 0.02f, 0.5f, s.wristWidthM, problems);
    ReadRange(file, "gunWidthM", 0.02f, 0.5f, s.gunWidthM, problems);
    ReadRange(file, "gunBackM", -0.5f, 1.0f, s.gunBackM, problems);
    ReadRange(file, "gunUpM", -0.2f, 0.3f, s.gunUpM, problems);
    if (auto const text = ini::Get(file, "Hud", "wristOffset"); text && !ini::Trim(*text).empty()) {
        float parts[3] = {};
        if (ParseNumbers(*text, 3, parts) && std::abs(parts[0]) <= 0.5f && std::abs(parts[1]) <= 0.5f && std::abs(parts[2]) <= 0.5f) {
            s.wristOffset = {parts[0], parts[1], parts[2]};
        } else if (problems != nullptr) {
            *problems += std::format("{}wristOffset = {} isn't x y z (meters in the left grip's axes, each within 0.5); the default kept",
                problems->empty() ? "" : "\n", *text);
        }
    }
    ReadCrop(file, "wristCrop", s.wristCrop, problems);
    ReadCrop(file, "gunCrop", s.gunCrop, problems);
    return s;
}

std::string Describe(Settings const& s) {
    std::string line = std::format("the player's status {}", s.wristStatus
            ? std::format("on the left wrist, {:.0f} mm wide, {:+.0f} {:+.0f} {:+.0f} mm from the grip", s.wristWidthM * 1000.0f,
                  s.wristOffset.x * 1000.0f, s.wristOffset.y * 1000.0f, s.wristOffset.z * 1000.0f)
            : std::string("on the HUD only"));
    line += std::format("; the weapon panel {}", s.gunStatus
            ? std::format("on the gun, {:.0f} mm wide, {:.0f} mm behind the muzzle and {:.0f} mm above the bore", s.gunWidthM * 1000.0f,
                  s.gunBackM * 1000.0f, s.gunUpM * 1000.0f)
            : std::string("on the HUD only"));
    if (s.wristCrop.Valid()) {
        line += std::format("; the status band ({:.3f}, {:.3f}) {:.3f} x {:.3f} by hand", s.wristCrop.x, s.wristCrop.y, s.wristCrop.width,
            s.wristCrop.height);
    }
    if (s.gunCrop.Valid()) {
        line += std::format("; the weapon band ({:.3f}, {:.3f}) {:.3f} x {:.3f} by hand", s.gunCrop.x, s.gunCrop.y, s.gunCrop.width,
            s.gunCrop.height);
    }
    return line;
}

Rect DefaultWristCrop(std::uint32_t width, std::uint32_t height) {
    return BottomBand(width, height, kWristLeft, kWristRight, kWristAboveBottom, kWristBelowTop);
}

Rect DefaultGunCrop(std::uint32_t width, std::uint32_t height) {
    return BottomBand(width, height, kGunLeft, kGunRight, kGunAboveBottom, kGunBelowTop);
}

Rect WristCrop(Settings const& settings, std::uint32_t width, std::uint32_t height) {
    return settings.wristCrop.Valid() ? settings.wristCrop : DefaultWristCrop(width, height);
}

Rect GunCrop(Settings const& settings, std::uint32_t width, std::uint32_t height) {
    return settings.gunCrop.Valid() ? settings.gunCrop : DefaultGunCrop(width, height);
}

bool Place(XrVector3f const& position, XrVector3f const& head, float widthM, float aspect, Quad& out, XrVector3f const& up) {
    if (!Finite(position) || !Finite(head) || !(widthM > 0.0f) || !(aspect > 0.0f)) {
        return false;
    }
    XrVector3f const toHead{head.x - position.x, head.y - position.y, head.z - position.z};
    float const distance = Length(toHead);
    if (!(distance > 0.02f)) {
        return false;
    }
    XrVector3f const normal{toHead.x / distance, toHead.y / distance, toHead.z / distance};
    XrVector3f lean = Finite(up) && Length(up) > 1e-3f ? up : XrVector3f{0.0f, 1.0f, 0.0f};
    // An up along the facing leaves no roll to choose: the world's then.
    float const along = std::abs(lean.x * normal.x + lean.y * normal.y + lean.z * normal.z) / Length(lean);
    if (along > 0.98f) {
        lean = {0.0f, 1.0f, 0.0f};
    }
    out.pose.position = position;
    out.pose.orientation = overlay::Facing(normal, lean);
    out.width = std::clamp(widthM, 0.01f, 1.0f);
    out.height = out.width / aspect;
    return true;
}

XrVector3f WristPoint(XrPosef const& grip, XrVector3f const& offset) {
    XrVector3f const moved = overlay::Rotate(grip.orientation, offset);
    return {grip.position.x + moved.x, grip.position.y + moved.y, grip.position.z + moved.z};
}

XrVector3f GunPoint(XrVector3f const& muzzle, XrVector3f const& direction, XrVector3f const& top, float backM, float upM) {
    return {muzzle.x - direction.x * backM + top.x * upM, muzzle.y - direction.y * backM + top.y * upM,
        muzzle.z - direction.z * backM + top.z * upM};
}

} // namespace mevr::vr::hud_pieces
