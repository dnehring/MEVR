// SPDX-License-Identifier: MIT
// The game's full-screen screens in the room (core/vr/WorldUi.hpp, research/53).

#include "core/vr/WorldUi.hpp"

#include "core/vr/GalaxyHologram.hpp"
#include "core/vr/OverlayPlacement.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <mutex>

#include <Windows.h>

namespace mevr::vr::world_ui {

namespace {

namespace gh = galaxy_holo;

constexpr std::array<char const*, static_cast<std::size_t>(Screen::Count)> kTags = {
    "None", "shop", "Loot", "Inventory", "XMods", "CharacterRecord", "Specialization", "Journal", "Codex", "Map", "PartySelect",
    "MenuBrowser", "Options", "Save", "Load", "MessageBox", "Achievement", "AdditionalContent", "SkillGame", "DesignerUI", "GameOver",
};

bool SameText(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
        return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
    });
}

std::mutex gMutex;
State gLatest;
bool gHave = false;
std::uint64_t gSequence = 0;

} // namespace

Screen FromTag(std::string_view tag) {
    for (std::size_t i = 1; i < kTags.size(); ++i) {
        if (SameText(tag, kTags[i])) {
            return static_cast<Screen>(i);
        }
    }
    return Screen::None;
}

char const* Name(Screen screen) {
    auto const i = static_cast<std::size_t>(screen);
    return i < kTags.size() ? kTags[i] : "None";
}

void Publish(State const& state) {
    std::lock_guard<std::mutex> const lock(gMutex);
    gLatest = state;
    gLatest.sequence = ++gSequence;
    gLatest.timeMs = GetTickCount64();
    gHave = true;
}

void Forget() {
    std::lock_guard<std::mutex> const lock(gMutex);
    gHave = false;
}

std::optional<State> Latest(std::uint64_t maxAgeMs) {
    std::lock_guard<std::mutex> const lock(gMutex);
    if (!gHave || gLatest.active == 0 || GetTickCount64() - gLatest.timeMs > maxAgeMs) {
        return std::nullopt;
    }
    return gLatest;
}

Rect Stage(float windowWidth, float windowHeight, float aspect) {
    if (!(windowWidth > 0.0f) || !(windowHeight > 0.0f) || !(aspect > 0.0f)) {
        return {};
    }
    float const window = windowWidth / windowHeight;
    if (window < aspect) {
        // Taller than the stage: letterboxed (bands above and below).
        float const shown = window / aspect;  // the stage's height, a share of the window's
        return {0.0f, 0.5f - 0.5f * shown, 1.0f, 0.5f + 0.5f * shown};
    }
    float const shown = aspect / window;  // the stage's width, a share of the window's
    return {0.5f - 0.5f * shown, 0.0f, 0.5f + 0.5f * shown, 1.0f};
}

Rect InWindow(Rect const& stage, Rect const& part) {
    float const w = stage.u1 - stage.u0;
    float const h = stage.v1 - stage.v0;
    return {stage.u0 + part.u0 * w, stage.v0 + part.v0 * h, stage.u0 + part.u1 * w, stage.v0 + part.v1 * h};
}

Rect DefaultCrop(Screen screen) {
    switch (screen) {
    case Screen::Shop:
        // Measured 2026-11-04 (the Normandy's requisitions officer): the item picture, the item card and the lists span stage
        // x 0.17..0.83, the whole height (the title "Store" at the top, the back button at the bottom right).
        return {0.14f, 0.0f, 0.86f, 1.0f};
    case Screen::Loot:
        // Measured 2026-11-04 (a real container on UNC20): "Recovered Items", the list, the value and the two buttons span stage
        // x 0.317..0.683, y 0.064..0.922; the rest was an empty backdrop over the container. With a margin.
        return {0.30f, 0.04f, 0.70f, 0.95f};
    // The minigame keeps the whole stage in the room: its frame (stage x 0.226..0.775, y 0.120..0.848, measured 2026-11-04,
    // research/53 section 6) cropped to the panel's 1.20 m would stand 64 degrees wide at 0.95 m. On the wrist: WristCrop.
    default:
        return {};  // the whole stage until measured (research/53)
    }
}

float RoomWidth(Screen screen, float widthM) {
    if (screen == Screen::Loot) {
        Rect const crop = DefaultCrop(screen);
        return widthM * (crop.u1 - crop.u0);
    }
    return widthM;
}

Rect WristCrop(Screen screen) {
    if (screen == Screen::SkillGame) {
        return {0.21f, 0.10f, 0.79f, 0.87f};  // its frame (measured above) with a little margin: the projector shows only the box
    }
    return DefaultCrop(screen);
}

std::optional<Panel> Place(XrVector3f const& eyes, XrVector3f const& headForward, std::optional<XrVector3f> anchor, float widthM,
    float distanceM, float belowM, float heightOverWidth) {
    if (!(widthM > 0.05f) || !(heightOverWidth > 0.01f) || !std::isfinite(widthM) || !std::isfinite(heightOverWidth)) {
        return std::nullopt;
    }
    float distance = std::max(distanceM, kNearestM);
    XrVector3f toward{headForward.x, 0.0f, headForward.z};
    if (anchor) {
        XrVector3f const flat{anchor->x - eyes.x, 0.0f, anchor->z - eyes.z};
        float const reach = gh::Length(flat);
        if (reach > kNearestM + kNearAnchorM) {
            toward = flat;
            distance = std::clamp(reach - kNearAnchorM, kNearestM, distance);
        } else if (reach > 0.05f) {
            toward = flat;  // what it belongs to is right here: the screen at the nearest distance, toward it
            distance = kNearestM;
        }
    }
    float const flatLength = gh::Length(toward);
    toward = flatLength > 1e-4f ? gh::Mul(toward, 1.0f / flatLength) : XrVector3f{0.0f, 0.0f, -1.0f};
    XrVector3f centre = gh::Add(eyes, gh::Mul(toward, distance));
    centre.y = eyes.y - belowM;
    Panel panel;
    panel.pose = gh::DiscPose(centre, gh::Sub(eyes, centre), eyes);  // facing the eyes, its X level
    panel.widthM = widthM;
    panel.heightM = widthM * heightOverWidth;
    return panel;
}

std::optional<Panel> WristPlace(XrVector3f const& eyes, XrVector3f const& wrist, float widthM, float aboveM, float towardM,
    float heightOverWidth) {
    if (!(widthM > 0.05f) || !(heightOverWidth > 0.01f) || !std::isfinite(widthM) || !std::isfinite(heightOverWidth) ||
        !std::isfinite(aboveM) || !std::isfinite(towardM)) {
        return std::nullopt;
    }
    float const height = widthM * heightOverWidth;
    XrVector3f toward{eyes.x - wrist.x, 0.0f, eyes.z - wrist.z};
    float const flat = gh::Length(toward);
    toward = flat > 1e-4f ? gh::Mul(toward, 1.0f / flat) : XrVector3f{0.0f, 0.0f, 1.0f};
    XrVector3f const base = gh::Add(wrist, gh::Mul(toward, towardM));
    // Up as the eyes see it: the facing pose's own +Y (across the line of sight), taken where the screen ends up (it faces the
    // eyes from its centre; a few rounds settle it).
    XrVector3f centre = base;
    for (int round = 0; round < 4; ++round) {
        XrVector3f const up = overlay::Rotate(gh::DiscPose(centre, gh::Sub(eyes, centre), eyes).orientation, {0.0f, 1.0f, 0.0f});
        centre = gh::Add(base, gh::Mul(up, aboveM + 0.5f * height));
    }
    XrVector3f const fromEyes = gh::Sub(centre, eyes);
    float const reach = gh::Length(fromEyes);
    if (reach < kWristNearestM) {  // never in the face
        centre = reach > 1e-4f ? gh::Add(eyes, gh::Mul(fromEyes, kWristNearestM / reach)) : gh::Add(eyes, {0.0f, 0.0f, -kWristNearestM});
    }
    Panel panel;
    panel.pose = gh::DiscPose(centre, gh::Sub(eyes, centre), eyes);  // facing the eyes, its X level
    panel.widthM = widthM;
    panel.heightM = height;
    return panel;
}

Panel Unfold(Panel const& panel, float seconds) {
    if (!(seconds < kUnfoldS)) {
        return panel;
    }
    float const t = std::clamp(seconds / kUnfoldS, 0.0f, 1.0f);
    float const eased = 1.0f - (1.0f - t) * (1.0f - t);  // ease out
    Panel out = panel;
    out.heightM = std::max(panel.heightM * eased, 0.002f);
    out.widthM = panel.widthM * (kUnfoldWidth + (1.0f - kUnfoldWidth) * eased);
    XrVector3f const up = overlay::Rotate(panel.pose.orientation, {0.0f, 1.0f, 0.0f});
    out.pose.position = gh::Sub(panel.pose.position, gh::Mul(up, 0.5f * (panel.heightM - out.heightM)));  // the bottom edge stays
    return out;
}

std::optional<Beam> BeamTo(XrVector3f const& emitter, Panel const& panel) {
    XrVector3f const up = overlay::Rotate(panel.pose.orientation, {0.0f, 1.0f, 0.0f});
    XrVector3f const across = overlay::Rotate(panel.pose.orientation, {1.0f, 0.0f, 0.0f});
    XrVector3f const edge = gh::Sub(panel.pose.position, gh::Mul(up, 0.5f * panel.heightM));
    XrVector3f y = gh::Sub(edge, emitter);
    float const height = gh::Length(y);
    if (!(height > 0.01f)) {
        return std::nullopt;
    }
    y = gh::Mul(y, 1.0f / height);
    XrVector3f x = gh::Sub(across, gh::Mul(y, gh::Dot(across, y)));
    float const xLength = gh::Length(x);
    if (!(xLength > 0.2f)) {
        return std::nullopt;
    }
    x = gh::Mul(x, 1.0f / xLength);
    XrVector3f const z = gh::Cross(x, y);
    // The rotation whose columns are x, y, z, as a quaternion.
    float const m00 = x.x, m01 = y.x, m02 = z.x;
    float const m10 = x.y, m11 = y.y, m12 = z.y;
    float const m20 = x.z, m21 = y.z, m22 = z.z;
    XrQuaternionf q{};
    float const trace = m00 + m11 + m22;
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
    Beam beam;
    beam.pose.orientation = q;
    beam.pose.position = gh::Mul(gh::Add(emitter, edge), 0.5f);
    beam.widthM = panel.widthM;
    beam.heightM = height;
    return beam;
}

std::vector<std::uint32_t> BeamPixels(int size, float red, float green, float blue) {
    size = std::clamp(size, 8, 1024);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(size) * size, 0u);
    auto const byte = [](float v) { return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f)); };
    auto const toLinear = [](float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); };
    auto const toSrgb = [](float l) { return l <= 0.0031308f ? l * 12.92f : 1.055f * std::pow(l, 1.0f / 2.4f) - 0.055f; };
    red = toLinear(std::clamp(red, 0.0f, 1.0f));
    green = toLinear(std::clamp(green, 0.0f, 1.0f));
    blue = toLinear(std::clamp(blue, 0.0f, 1.0f));
    for (int row = 0; row < size; ++row) {
        float const v = (static_cast<float>(row) + 0.5f) / static_cast<float>(size);  // 0 at the panel, 1 at the emitter
        float const half = 0.5f * (1.0f + (kBeamApex - 1.0f) * v);                       // the fan's half width (of the width)
        float const strength = kBeamFar + (kBeamNear - kBeamFar) * v * v;               // brightest near the emitter
        for (int column = 0; column < size; ++column) {
            float const u = (static_cast<float>(column) + 0.5f) / static_cast<float>(size);
            float const side = std::abs(u - 0.5f) / std::max(half, 1e-4f);  // 0 on the fan's middle line, 1 at its side
            float const soft = std::clamp((1.0f - side) / 0.35f, 0.0f, 1.0f);  // soft sides
            float const a = strength * soft * soft * (3.0f - 2.0f * soft);
            pixels[static_cast<std::size_t>(row) * size + column] =
                byte(toSrgb(red * a)) | (byte(toSrgb(green * a)) << 8) | (byte(toSrgb(blue * a)) << 16) | (byte(a) << 24);
        }
    }
    return pixels;
}

Panel WholeWindow(Panel const& shown, Rect const& part) {
    float const du = std::max(part.u1 - part.u0, 1e-4f);
    float const dv = std::max(part.v1 - part.v0, 1e-4f);
    Panel whole;
    whole.widthM = shown.widthM / du;
    whole.heightM = shown.heightM / dv;
    // The shown part's centre sits at ((u0+u1)/2, (v0+v1)/2) of the window; the window's centre is offset from it by that much
    // the other way (window V runs down, the panel's +Y up).
    float const offsetX = (0.5f - 0.5f * (part.u0 + part.u1)) * whole.widthM;
    float const offsetY = (0.5f * (part.v0 + part.v1) - 0.5f) * whole.heightM;
    XrVector3f const offset = overlay::Rotate(shown.pose.orientation, {offsetX, offsetY, 0.0f});
    whole.pose.orientation = shown.pose.orientation;
    whole.pose.position = gh::Add(shown.pose.position, offset);
    return whole;
}

} // namespace mevr::vr::world_ui
