// SPDX-License-Identifier: MIT
// The game's full-screen screens in the room: the host's side (plugin/WorldUi.hpp, research/53).
#include "plugin/WorldUi.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "engine/GameAdapter.hpp"
#include "halcyon/MevrSeams.h"
#include "plugin/GalaxyHologram.hpp"
#include "plugin/Renderer.hpp"
#if defined(MEVR_WITH_HALCYON)
#include "halcyon/le1/mevr_bridge.h"
#endif

#include <Windows.h>

#include <algorithm>
#include <atomic>
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
std::mutex gOpeningMutex;
wu::Opening gOpening;
bool gGuard = false;  // keep the last clean eyes until the admitted UI has actually been redirected and shown
std::uint64_t gGuardSinceMs = 0;
std::uint64_t gReservedPresents = 0;
std::uint64_t gReservedFlat = 0;
std::uint64_t gHeldPresents = 0;
std::atomic<std::uint64_t> gHeldViews{0};  // main view builds with Shepard's view held (HoldViewBegin)
// Main view builds in the GUI mode while an opening of ours is reserved and nothing is published yet: drawn from the GUI mode's
// own camera (the renderer's clean-eye guard keeps them out of the eyes). The mod counts the states it published without the
// hold it wanted (game/WorldUi). Game thread.
std::uint64_t gUnheldViews = 0;
std::uint64_t gUnheldLoggedMs = 0;

void ExpireOpening() {
    wu::Screen const screen = gOpening.Expected();
    if (gOpening.Expire(GetTickCount64())) {
        gGuard = false;
        log::Info("world ui: reservation timeout for {}", wu::Name(screen));
    }
    if (gGuard && GetTickCount64() - gGuardSinceMs >= wu::Opening::kTimeoutMs) {
        gGuard = false;
        log::Info("world ui: clean-eye guard timeout (UI redirection not acknowledged)");
    }
}

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
    shown.ageS = ageS;
    shown.panel = wu::Unfold(panel, ageS);
    shown.settled = panel;
    shown.whole = wu::WholeWindow(shown.panel, shown.part);
    shown.beam = beam ? wu::BeamTo(emitter, shown.panel) : std::nullopt;
}

} // namespace

bool Active() {
    return wu::Latest(kFreshMs).has_value();
}

void Reserve(std::uint32_t request, std::uint32_t screen, std::uint32_t origin) {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    ExpireOpening();
    if (screen == 0) {
        if (gOpening.Cancel(request)) {
            gGuard = false;
            log::Info("world ui: reservation cancelled (request {})", request);
        }
        if (request == 0) gGuard = false;
        return;
    }
    if (gOpening.Cancel(0)) {
        log::Info("world ui: reservation cancelled (replaced)");
    }
    if (gOpening.Begin(request, static_cast<wu::Screen>(screen), static_cast<vr::game_menu::Origin>(origin), GetTickCount64())) {
        gGuard = true;
        gGuardSinceMs = GetTickCount64();
        log::Info("world ui: reserved for {} ({})", wu::Name(gOpening.Expected()),
            gOpening.Source() == vr::game_menu::Origin::OmniRail ? "rail" : "menu");
    }
}

void Publish(wu::State const* state) {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    ExpireOpening();
    if (state != nullptr) {
        wu::Publish(*state);
        if (state->active != 0 && gOpening.Matches(state->openingRequest, state->visibleScreens)) {
            log::Info("world ui: reservation released for {} (published, request {})", wu::Name(gOpening.Expected()), gOpening.Request());
            gOpening.Cancel(0);
            // gGuard lasts until a redirected panel was submitted, not merely until classification.
        }
    } else {
        wu::Forget();  // an empty tick is not a rejected opening
    }
}

void Forget() {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    if (gOpening.Cancel(0)) log::Info("world ui: reservation cancelled (unload)");
    gGuard = false;
    wu::Forget();
}

unsigned Ownership(int mode) {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    ExpireOpening();
    if (gOpening.Pending() && !wu::GuiMode(mode) && !wu::PlayMode(mode)) {
        gOpening.Cancel(0);
        gGuard = false;
        log::Info("world ui: reservation cancelled (game mode {})", mode);
    }
    return (wu::ScreenMode(mode) && Active() ? 1u : 0u) | (gOpening.Eligible(mode) ? 2u : 0u) |
        (gOpening.Pending() && wu::PlayMode(mode) ? 4u : 0u);  // opening call hasn't changed the mode yet
}

bool HoldFrame(int mode) {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    ExpireOpening();
    return (wu::GuiMode(mode) || wu::PlayMode(mode)) && gGuard && (gOpening.Pending() || Active());
}

void RedirectReady(bool ready) {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    if (ready && !gOpening.Pending()) gGuard = false;
}

void NotePresent(bool reserved, bool flatPanel, bool held) {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    if (reserved || gOpening.Pending()) {  // include a request armed after this Present sampled its mode/ownership
        ++gReservedPresents;
        if (flatPanel) ++gReservedFlat;
    }
    if (held) ++gHeldPresents;
}

void LogStats() {
    std::lock_guard<std::mutex> const lock(gOpeningMutex);
    ExpireOpening();
    log::Info("    world ui: reservation Presents {}, flat-panel frames during reservation {} (expected 0), clean-eye holds {}, "
              "views held at Shepard's {}, views behind a screen of ours not held {}", gReservedPresents, gReservedFlat, gHeldPresents,
        gHeldViews.load(std::memory_order_relaxed), gUnheldViews);
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
        gPlaced.shown.puzzle = state->puzzle != 0;
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
        shown.puzzle = state->puzzle != 0;
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
    shown.settled = *panel;
    shown.whole = wu::WholeWindow(*panel, part);
    shown.part = part;
    shown.backdrop = std::clamp(state->backdrop, 0.0f, 1.0f);
    shown.screen = static_cast<wu::Screen>(state->screen);
    shown.puzzle = state->puzzle != 0;
    gPlaced = {true, state->opening, windowWidth, windowHeight, now, shown};
    log::Info("world ui: the game's '{}' screen stands in the room: {:.2f} x {:.2f} m at ({:.2f}, {:.2f}, {:.2f}) m, {:.2f} m from the eyes{}; "
              "window UV ({:.3f}, {:.3f})..({:.3f}, {:.3f}) of {}x{}, backdrop {:.2f}",
        tag, panel->widthM, panel->heightM, panel->pose.position.x, panel->pose.position.y, panel->pose.position.z,
        std::hypot(panel->pose.position.x - eyes.x, panel->pose.position.z - eyes.z),
        anchor ? std::format(", toward what it belongs to at ({:.2f}, {:.2f}, {:.2f})", anchor->x, anchor->y, anchor->z) : std::string(", straight ahead"),
        part.u0, part.v0, part.u1, part.v1, windowWidth, windowHeight, shown.backdrop);
    return shown;
}

namespace {

constexpr std::uint64_t kHoldFreshMs = 500;  // the mod sends every tick while a screen is up

// The player camera's POV fields while the held view is in them (game thread: the CalcSceneView detour), and what they held.
struct HeldPov {
    bool active = false;
    float* location = nullptr;
    std::int32_t* rotation = nullptr;
    float* fov = nullptr;
    float savedLocation[3] = {};
    std::int32_t savedRotation[3] = {};
    float savedFov = 0.0f;
};
HeldPov gHeldPov;

// The held view's state, if it would be held in `gameMode` now: a screen of ours up (fresh), the mod's saved view and the POV's place.
std::optional<wu::State> HoldState(int gameMode) {
    if (!wu::ScreenMode(gameMode)) {
        return std::nullopt;
    }
    auto const ui = wu::Latest(kHoldFreshMs);
    if (!ui || ui->holdRender == 0 || ui->povLocationAddress == 0 || ui->povRotationAddress == 0 || ui->povFovAddress == 0) {
        return std::nullopt;
    }
    return ui;
}

// The POV's fields swapped (journalled as LE1's GalaxyViewSwapBegin always did: the same names), guarded: false if unreadable.
bool SwapPovIn(HeldPov& s, float const location[3], std::int32_t const rotation[3], float fov) noexcept {
    __try {
        for (int i = 0; i < 3; ++i) {
            s.savedLocation[i] = s.location[i];
            s.savedRotation[i] = s.rotation[i];
        }
        s.savedFov = *s.fov;
        for (int i = 0; i < 3; ++i) {
            s.location[i] = location[i];
            s.rotation[i] = rotation[i];
        }
        write_journal::Wrote("WorldView.holdLocation", s.location, sizeof(float) * 3);
        write_journal::Wrote("WorldView.holdRotation", s.rotation, sizeof(std::int32_t) * 3);
        *s.fov = fov;
        write_journal::Wrote("WorldView.holdFov", s.fov, sizeof(float));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void SwapPovOut(HeldPov& s) noexcept {
    __try {
        for (int i = 0; i < 3; ++i) {
            s.location[i] = s.savedLocation[i];
            s.rotation[i] = s.savedRotation[i];
        }
        write_journal::Wrote("WorldView.restoreLocation", s.location, sizeof(float) * 3);
        write_journal::Wrote("WorldView.restoreRotation", s.rotation, sizeof(std::int32_t) * 3);
        *s.fov = s.savedFov;
        write_journal::Wrote("WorldView.restoreFov", s.fov, sizeof(float));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

} // namespace

bool HoldViewBegin(int gameMode) noexcept {
    HoldViewEnd();  // never two in a row: a missed end puts the camera's own view back first
    auto const ui = HoldState(gameMode);
    if (!ui) {
        if (wu::ScreenMode(gameMode) && !Active() && (Ownership(gameMode) & 2u) != 0) {
            ++gUnheldViews;  // the GUI mode's own camera behind an opening of ours: what the clean-eye guard hides
            std::uint64_t const now = GetTickCount64();
            if (now - gUnheldLoggedMs > 2000) {
                gUnheldLoggedMs = now;
                log::Info("world ui: a main view built for a reserved opening before its first state (the GUI mode's own camera, "
                          "{} so far)", gUnheldViews);
            }
        }
        return false;
    }
    gHeldPov.location = reinterpret_cast<float*>(ui->povLocationAddress);
    gHeldPov.rotation = reinterpret_cast<std::int32_t*>(ui->povRotationAddress);
    gHeldPov.fov = reinterpret_cast<float*>(ui->povFovAddress);
    if (!SwapPovIn(gHeldPov, ui->holdLocation, ui->holdRotation, ui->holdFov)) {
        return false;
    }
    gHeldPov.active = true;
    if (gHeldViews++ == 0) {
        float const dx = gHeldPov.savedLocation[0] - ui->holdLocation[0];
        float const dy = gHeldPov.savedLocation[1] - ui->holdLocation[1];
        float const dz = gHeldPov.savedLocation[2] - ui->holdLocation[2];
        log::Info("world ui: the view held at Shepard's for the main view's build only (the game's screen keeps its own camera, "
                  "{:.0f} units from the held eyes: ({:+.0f}, {:+.0f}, {:+.0f}))", std::sqrt(dx * dx + dy * dy + dz * dz), dx, dy, dz);
    }
    return true;
}

void HoldViewEnd() noexcept {
    if (gHeldPov.active) {
        SwapPovOut(gHeldPov);
        gHeldPov.active = false;
    }
}

int HoldsView(int gameMode) noexcept {
    auto const ui = HoldState(gameMode);
    if (!ui) {
        return 0;
    }
    return static_cast<wu::Placement>(ui->placement) == wu::Placement::Room ? 2 : 1;
}

namespace {
// The mod sends every tick; its word stands this long (plugin/ConversationWorld's kFreshMs on LE1: a stalled tick bridged).
constexpr std::uint64_t kConversationFreshMs = 1500;
HeldPov gConversationPov;
std::atomic<std::uint64_t> gConversationViews{0};

std::optional<vr::conversation_world::State> ConversationState() noexcept {
    auto const s = vr::conversation_world::Latest(kConversationFreshMs);
    if (!s || s->size != sizeof(vr::conversation_world::State) || s->active == 0) {
        return std::nullopt;
    }
    return s;
}
} // namespace

bool ConversationShown() noexcept {
    return ConversationState().has_value();
}

bool HoldConversationBegin() noexcept {
    HoldConversationEnd();
    auto const s = ConversationState();
    if (!s || s->holdRender == 0 || s->povLocationAddress == 0 || s->povRotationAddress == 0 || s->povFovAddress == 0) {
        return false;
    }
    gConversationPov.location = reinterpret_cast<float*>(s->povLocationAddress);
    gConversationPov.rotation = reinterpret_cast<std::int32_t*>(s->povRotationAddress);
    gConversationPov.fov = reinterpret_cast<float*>(s->povFovAddress);
    if (!SwapPovIn(gConversationPov, s->holdLocation, s->holdRotation, s->holdFov)) {
        return false;
    }
    gConversationPov.active = true;
    if (gConversationViews.fetch_add(1) == 0) {
        float const dx = gConversationPov.savedLocation[0] - s->holdLocation[0];
        float const dy = gConversationPov.savedLocation[1] - s->holdLocation[1];
        float const dz = gConversationPov.savedLocation[2] - s->holdLocation[2];
        log::Info("conversation world: the view held at the player's place for the main view's build only (the director's camera "
                  "serves the game, {:.0f} units from the held eyes)", std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    return true;
}

void HoldConversationEnd() noexcept {
    if (gConversationPov.active) {
        SwapPovOut(gConversationPov);
        gConversationPov.active = false;
    }
}

} // namespace mevr::host::world_ui

// ---- The held view's seams (src/halcyon/MevrSeams.h): ME3's renderer; LE1's GalaxyViewSwapBegin calls HoldViewBegin itself ----
namespace {
// The map's main views on a sequel, held at Shepard's or not (the game's map camera: the opening's or a return's wrong-camera
// frames), counted per stay in the map's modes and logged when it ends (docs/galaxy-projector-handoff.md "Opening and closing").
struct MapViews {
    bool inMap = false;
    std::uint64_t held = 0, unheld = 0, beforeFirstHeld = 0;
};
MapViews gMapViews;  // game thread (CalcSceneView)

void NoteMapView(int mode, bool held) noexcept {
    bool const map = mevr::engine::adapter::IsMapMode(mode);
    MapViews& v = gMapViews;
    if (map) {
        v.inMap = true;
        if (held) {
            ++v.held;
        } else {
            ++v.unheld;
            if (v.held == 0) ++v.beforeFirstHeld;
        }
        return;
    }
    if (v.inMap && (v.held != 0 || v.unheld != 0)) {
        mevr::log::Info("galaxy hologram: the map's main views (game mode {} now): {} held at Shepard's, {} from the map's camera "
                        "({} of them before the first held one)", mode, v.held, v.unheld, v.beforeFirstHeld);
    }
    v = {};
}
}  // namespace

namespace MevrSeams {

bool WorldViewHoldBegin() noexcept {
    // The galaxy map in the CIC first (as LE1's GalaxyViewSwapBegin): wanted but not written, no other hold takes the view.
    int const mode = mevr::host::renderer::GameMode();
    bool galaxyWanted = false;
    bool const galaxyHeld = mevr::host::galaxy_hologram::HoldViewBegin(mode, galaxyWanted);
    NoteMapView(mode, galaxyHeld);
    if (galaxyWanted) {
        return galaxyHeld;
    }
    // A conversation in the world (LE1's GalaxyViewSwapBegin's second branch): the director's camera keeps serving the game.
    if (mevr::host::world_ui::ConversationShown()) {
        return mevr::host::world_ui::HoldConversationBegin();
    }
    return mevr::host::world_ui::HoldViewBegin(mode);
}

void WorldViewHoldEnd() noexcept {
    mevr::host::galaxy_hologram::HoldViewEnd();
    mevr::host::world_ui::HoldConversationEnd();
    mevr::host::world_ui::HoldViewEnd();
}

int WorldViewHeld(int mode) noexcept {
    // Held for the galaxy map: Shepard stands at the console, his head and body hidden at the held eyes (2, a screen in the room).
    if (mevr::host::galaxy_hologram::Holds(mode)) {
        return 2;
    }
    // A conversation in the world: the eyes at Shepard's head, first person's hides (his head hidden, his body under the player).
    if (mevr::host::world_ui::ConversationShown()) {
        return 1;
    }
    return mevr::host::world_ui::HoldsView(mode);
}

bool ConversationInWorld() noexcept {
    return mevr::host::world_ui::ConversationShown();
}

bool GalaxyOwnsPresentation(int mode) noexcept {
    return mevr::host::galaxy_hologram::Holds(mode);
}

bool WorldUiHoldFrame(int mode) noexcept {
    bool const held = mevr::host::world_ui::HoldFrame(mode);
    mevr::host::world_ui::NotePresent(false, false, held);  // the stats line's clean-eye holds (LE1's xr_session.cpp notes its own)
    return held;
}

} // namespace MevrSeams

#if defined(MEVR_WITH_HALCYON)
// LE1's seams (xr_session.cpp's decision). ME2's are in plugin/MenusLe2.cpp.
namespace MELEVR::MevrBridge {

bool WorldUiActive() noexcept {
    return mevr::host::world_ui::Active();
}

unsigned WorldUiOwnership(int mode) noexcept { return mevr::host::world_ui::Ownership(mode); }
bool WorldUiHoldFrame(int mode) noexcept { return mevr::host::world_ui::HoldFrame(mode); }
void WorldUiPresent(bool reserved, bool flatPanel, bool held) noexcept { mevr::host::world_ui::NotePresent(reserved, flatPanel, held); }

} // namespace MELEVR::MevrBridge
#endif
