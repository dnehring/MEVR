// SPDX-License-Identifier: MIT
// The game's full-screen screens in the room: the host's side (plugin/WorldUi.hpp, research/53).
#include "plugin/WorldUi.hpp"

#include "core/Log.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "halcyon/le1/mevr_bridge.h"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <cstring>
#include <mutex>

namespace mevr::host::world_ui {

namespace {

namespace wu = vr::world_ui;

constexpr std::uint64_t kFreshMs = 400;  // the mod sends every tick while a screen is up

struct Placed {
    bool valid = false;
    std::uint32_t opening = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t lastMs = 0;  // the last frame it was shown: a screen gone for a second is placed again
    Shown shown;
};
Placed gPlaced;  // render thread
std::mutex gMutex;

} // namespace

bool Active() {
    return wu::Latest(kFreshMs).has_value();
}

std::optional<Shown> Current(XrVector3f const& eyes, XrQuaternionf const& head, bool headValid, std::uint32_t windowWidth,
    std::uint32_t windowHeight) {
    auto const state = wu::Latest(kFreshMs);
    std::lock_guard<std::mutex> const lock(gMutex);
    if (!state || windowWidth == 0 || windowHeight == 0) {
        if (gPlaced.valid && !state) {
            log::Info("world ui: the screen in the room is gone");
        }
        gPlaced.valid = gPlaced.valid && state.has_value();
        return std::nullopt;
    }
    std::uint64_t const now = GetTickCount64();
    if (gPlaced.valid && gPlaced.opening == state->opening && gPlaced.width == windowWidth && gPlaced.height == windowHeight &&
        now - gPlaced.lastMs < 1000) {
        gPlaced.shown.backdrop = std::clamp(state->backdrop, 0.0f, 1.0f);
        gPlaced.lastMs = now;
        return gPlaced.shown;
    }
    if (!headValid) {
        return std::nullopt;
    }
    wu::Rect const stage = wu::Stage(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    wu::Rect const crop{std::clamp(state->stageCrop[0], 0.0f, 1.0f), std::clamp(state->stageCrop[1], 0.0f, 1.0f),
        std::clamp(state->stageCrop[2], 0.0f, 1.0f), std::clamp(state->stageCrop[3], 0.0f, 1.0f)};
    if (!(crop.u1 > crop.u0 + 0.05f) || !(crop.v1 > crop.v0 + 0.05f)) {
        return std::nullopt;
    }
    wu::Rect const part = wu::InWindow(stage, crop);
    float const heightOverWidth = ((part.v1 - part.v0) * static_cast<float>(windowHeight)) / ((part.u1 - part.u0) * static_cast<float>(windowWidth));
    XrVector3f const forward = vr::overlay::Rotate(head, {0.0f, 0.0f, -1.0f});
    std::optional<XrVector3f> anchor;
    if (state->anchored != 0) {
        anchor = XrVector3f{state->anchor[0], state->anchor[1], state->anchor[2]};
    }
    auto const panel = wu::Place(eyes, forward, anchor, state->widthM, state->distanceM, state->belowM, heightOverWidth);
    if (!panel) {
        return std::nullopt;
    }
    Shown shown;
    shown.panel = *panel;
    shown.whole = wu::WholeWindow(*panel, part);
    shown.part = part;
    shown.backdrop = std::clamp(state->backdrop, 0.0f, 1.0f);
    shown.screen = static_cast<wu::Screen>(state->screen);
    gPlaced = {true, state->opening, windowWidth, windowHeight, now, shown};
    char tag[sizeof(state->tag) + 1] = {};
    std::memcpy(tag, state->tag, sizeof(state->tag));
    log::Info("world ui: the game's '{}' screen stands in the room: {:.2f} x {:.2f} m at ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m from the eyes{}; "
              "window UV ({:.3f}, {:.3f})..({:.3f}, {:.3f}) of {}x{}, backdrop {:.2f}",
        tag, panel->widthM, panel->heightM, panel->pose.position.x, panel->pose.position.y, panel->pose.position.z,
        std::hypot(panel->pose.position.x - eyes.x, panel->pose.position.z - eyes.z),
        anchor ? std::format(", toward what it belongs to at ({:.2f}, {:.2f}, {:.2f})", anchor->x, anchor->y, anchor->z) : std::string(", straight ahead"),
        part.u0, part.v0, part.u1, part.v1, windowWidth, windowHeight, shown.backdrop);
    return shown;
}

} // namespace mevr::host::world_ui

namespace MELEVR::MevrBridge {

bool WorldUiActive() noexcept {
    return mevr::host::world_ui::Active();
}

} // namespace MELEVR::MevrBridge
