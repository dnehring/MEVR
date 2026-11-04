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
    // Projected from the wrist: when it opened, the wrist smoothed (tracking jitter), the panel as last placed.
    std::uint64_t openedMs = 0;
    LARGE_INTEGER lastCounter{};
    XrVector3f wrist{};
    float heightOverWidth = 1.0f;
    vr::world_ui::Placement placement = vr::world_ui::Placement::Room;
    vr::world_ui::Panel projected;  // where it was projected (WristOnce keeps it)
};
Placed gPlaced;  // render thread
std::mutex gMutex;

constexpr float kWristSmoothS = 0.05f;  // the wrist's smoothing time constant
constexpr float kBeamAfterS = 0.6f;     // a screen left where it was projected keeps its fan this long

double Seconds(LARGE_INTEGER counter) {
    static LARGE_INTEGER const frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

// The screen on the wrist this frame: unfolded by its age, its fan from the wrist (the smoothed one) when wanted.
void Project(Shown& shown, vr::world_ui::Panel const& panel, XrVector3f const& emitter, float ageS, bool beam) {
    namespace wu = vr::world_ui;
    shown.panel = wu::Unfold(panel, ageS);
    shown.whole = wu::WholeWindow(shown.panel, shown.part);
    shown.beam = beam ? wu::BeamTo(emitter, shown.panel) : std::nullopt;
}

} // namespace

bool Active() {
    return wu::Latest(kFreshMs).has_value();
}

std::optional<Shown> Current(XrVector3f const& eyes, XrQuaternionf const& head, bool headValid, std::uint32_t windowWidth,
    std::uint32_t windowHeight, std::optional<XrVector3f> wrist) {
    auto const state = wu::Latest(kFreshMs);
    std::lock_guard<std::mutex> const lock(gMutex);
    if (!state || windowWidth == 0 || windowHeight == 0) {
        if (gPlaced.valid && !state) {
            log::Info("world ui: the screen {} is gone", gPlaced.shown.onWrist ? "on the wrist" : "in the room");
        }
        gPlaced.valid = gPlaced.valid && state.has_value();
        return std::nullopt;
    }
    // The projector: the omni-tool's display where Shepard's arm shows it (the mod's, game/OmniTool), since the arm-length
    // calibration can leave the rendered hand short of the controller; the controller's wrist without it. A screen projected
    // from the wrist still needs the tracked controller (else it stands in the room).
    if (state->emitterValid != 0 && wrist) {
        wrist = XrVector3f{state->emitter[0], state->emitter[1], state->emitter[2]};
    }
    std::uint64_t const now = GetTickCount64();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    if (gPlaced.valid && gPlaced.opening == state->opening && gPlaced.width == windowWidth && gPlaced.height == windowHeight &&
        now - gPlaced.lastMs < 1000) {
        gPlaced.shown.backdrop = std::clamp(state->backdrop, 0.0f, 1.0f);
        gPlaced.lastMs = now;
        if (gPlaced.shown.onWrist) {
            float const dt = gPlaced.lastCounter.QuadPart != 0 ? static_cast<float>(Seconds(counter) - Seconds(gPlaced.lastCounter)) : 0.0f;
            gPlaced.lastCounter = counter;
            float const ageS = static_cast<float>(now - gPlaced.openedMs) / 1000.0f;
            if (gPlaced.placement == wu::Placement::Wrist && wrist && headValid) {
                // Riding on the wrist: the panel follows the smoothed wrist; a lost controller leaves it where it was.
                float const k = 1.0f - std::exp(-std::max(dt, 0.0f) / kWristSmoothS);
                gPlaced.wrist = {gPlaced.wrist.x + (wrist->x - gPlaced.wrist.x) * k, gPlaced.wrist.y + (wrist->y - gPlaced.wrist.y) * k,
                    gPlaced.wrist.z + (wrist->z - gPlaced.wrist.z) * k};
                if (auto const panel = wu::WristPlace(eyes, gPlaced.wrist, state->wristWidthM, state->wristAboveM, state->wristTowardM,
                        gPlaced.heightOverWidth)) {
                    Project(gPlaced.shown, *panel, gPlaced.wrist, ageS, state->beam != 0);
                }
            } else if (gPlaced.placement == wu::Placement::WristOnce) {
                // Left where it was projected: it unfolds there, its fan only while it opens.
                Project(gPlaced.shown, gPlaced.projected, gPlaced.wrist, ageS, state->beam != 0 && ageS < kBeamAfterS);
            } else {
                gPlaced.shown.beam.reset();
            }
        }
        return gPlaced.shown;
    }
    if (!headValid) {
        return std::nullopt;
    }
    wu::Rect const stage = wu::Stage(static_cast<float>(windowWidth), static_cast<float>(windowHeight));
    auto const placement = static_cast<wu::Placement>(state->placement);
    bool const wristWanted = placement == wu::Placement::Wrist || placement == wu::Placement::WristOnce;
    // On the wrist the screen's tighter crop (the minigame's box alone); in the room the mod's.
    wu::Rect const crop = wristWanted && wrist ? wu::WristCrop(static_cast<wu::Screen>(state->screen))
        : wu::Rect{std::clamp(state->stageCrop[0], 0.0f, 1.0f), std::clamp(state->stageCrop[1], 0.0f, 1.0f),
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
    char tag[sizeof(state->tag) + 1] = {};
    std::memcpy(tag, state->tag, sizeof(state->tag));
    if (wristWanted && wrist) {
        // Projected from the left wrist (the omni-tool): it unfolds out of it, its fan of light from it.
        auto const panel = wu::WristPlace(eyes, *wrist, state->wristWidthM, state->wristAboveM, state->wristTowardM, heightOverWidth);
        if (!panel) {
            return std::nullopt;
        }
        Shown shown;
        shown.part = part;
        shown.backdrop = std::clamp(state->backdrop, 0.0f, 1.0f);
        shown.screen = static_cast<wu::Screen>(state->screen);
        shown.onWrist = true;
        Project(shown, *panel, *wrist, 0.0f, state->beam != 0);
        gPlaced = {true, state->opening, windowWidth, windowHeight, now, shown};
        gPlaced.openedMs = now;
        gPlaced.lastCounter = counter;
        gPlaced.wrist = *wrist;
        gPlaced.heightOverWidth = heightOverWidth;
        gPlaced.placement = placement;
        gPlaced.projected = *panel;
        log::Info("world ui: the game's '{}' screen is projected from the left wrist ({}): {:.2f} x {:.2f} m at ({:.2f}, {:.2f}, {:.2f}) m, "
                  "{:.2f} m from the eyes; the wrist at ({:.2f}, {:.2f}, {:.2f}); window UV ({:.3f}, {:.3f})..({:.3f}, {:.3f}) of {}x{}, "
                  "backdrop {:.2f}, {}",
            tag, placement == wu::Placement::Wrist ? "riding on it" : "left where it was projected", panel->widthM, panel->heightM,
            panel->pose.position.x, panel->pose.position.y, panel->pose.position.z,
            std::hypot(panel->pose.position.x - eyes.x, panel->pose.position.y - eyes.y, panel->pose.position.z - eyes.z), wrist->x, wrist->y,
            wrist->z, part.u0, part.v0, part.u1, part.v1, windowWidth, windowHeight, shown.backdrop,
            state->beam == 0 ? "no fan of light"
            : state->emitterValid != 0 ? "a fan of light from the omni-tool's display on Shepard's arm"
                                       : "a fan of light from the controller's wrist");
        return gPlaced.shown;
    }
    auto const panel = wu::Place(eyes, forward, anchor, state->widthM, state->distanceM, state->belowM, heightOverWidth);
    if (!panel) {
        return std::nullopt;
    }
    if (wristWanted) {
        log::Info("world ui: the game's '{}' screen would be projected from the left wrist, but the left controller isn't tracked: "
                  "it stands in the room", tag);
    }
    Shown shown;
    shown.panel = *panel;
    shown.whole = wu::WholeWindow(*panel, part);
    shown.part = part;
    shown.backdrop = std::clamp(state->backdrop, 0.0f, 1.0f);
    shown.screen = static_cast<wu::Screen>(state->screen);
    gPlaced = {true, state->opening, windowWidth, windowHeight, now, shown};
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
