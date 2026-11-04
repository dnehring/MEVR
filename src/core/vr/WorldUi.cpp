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
    "MenuBrowser", "Options", "Save", "Load", "MessageBox", "Achievement", "AdditionalContent",
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
    default:
        return {};  // the whole stage until measured (research/53)
    }
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
