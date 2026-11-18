// SPDX-License-Identifier: MIT
// The sequels' galaxy map picked with a controller's ray (game/GalaxyRay.hpp).
#include "game/GalaxyRay.hpp"

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/GalaxyRay.hpp"
#include "core/vr/OverlayPlacement.hpp"
#include "engine/SettingsFile.hpp"
#include "game/GameMenuRequest.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>
#include <vector>

namespace mevr::game::galaxy_projector::ray {
namespace {

namespace gh = vr::galaxy_holo;
namespace gr = vr::galaxy_ray;

ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation", ue3::kEveryGame};
ue3::FieldRef gSelectedObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedObject", ue3::kEveryGame};
ue3::FieldRef gCrossHairObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pCrossHairObject", ue3::kEveryGame};
ue3::FieldRef gCurrentState{"SFXGame.BioCameraBehaviorGalaxy", "m_nCurrentState", ue3::kEveryGame};
ue3::FieldRef gZoomTime{"SFXGame.BioCameraBehaviorGalaxy", "m_fZoomTime", ue3::kEveryGame};
ue3::ClassRef gPcHandlerClass{"SFXGame.BioSFHandler_PCGalaxyMap", ue3::kEveryGame};
ue3::ClassRef gInterpClass{"Engine.InterpActor", ue3::kEveryGame};
// The map's own stick (the handler's, what its GUI axis events set and the native ship movement reads; ME2's and ME3's).
ue3::FieldRef gLeftStickX{"SFXGame.BioSFHandler_GalaxyMap", "m_fLeftStickX", ue3::kEveryGame};
ue3::FieldRef gLeftStickY{"SFXGame.BioSFHandler_GalaxyMap", "m_fLeftStickY", ue3::kEveryGame};
#if defined(MEVR_GAME_LE3)
ue3::FieldRef gHandlerPanel{"SFXGame.SFXGUIMovieLegacyAdapter", "oPanel", ue3::kEveryGame};
ue3::FieldRef gUsingGamepad{"SFXGame.SFXGUIMovie", "bUsingGamepad", ue3::kEveryGame};
#else
ue3::FieldRef gHandlerPanel{"Engine.BioSFHandler", "oPanel", ue3::kEveryGame};
ue3::FieldRef gUsingGamepad{"Engine.BioSFPanel", "bUsingGamepad", ue3::kEveryGame};
#endif
#if defined(MEVR_GAME_LE3)
ue3::FieldRef gSelectableActors{"SFXGame.BioCameraBehaviorGalaxy", "m_aSelectableActors", ue3::kEveryGame};
constexpr std::uint8_t kButtonA = 15, kButtonARelease = 36;  // ME3's BioDefine.BioGuiEvents
#else
ue3::FieldRef gGalaxySelectors{"SFXGame.BioCameraBehaviorGalaxy", "m_GalaxySelectors", ue3::kEveryGame};
ue3::FieldRef gClusterSelectors{"SFXGame.BioCameraBehaviorGalaxy", "m_ClusterSelectors", ue3::kEveryGame};
ue3::FieldRef gSystemSelectors{"SFXGame.BioCameraBehaviorGalaxy", "m_SystemSelectors", ue3::kEveryGame};
ue3::FieldRef gGalaxySelectorActor{gGalaxySelectors, "m_actor"};
ue3::FieldRef gClusterSelectorActor{gClusterSelectors, "m_actor"};
ue3::FieldRef gSystemSelectorActor{gSystemSelectors, "m_actor"};
constexpr std::uint8_t kButtonA = 12, kButtonARelease = 33;  // ME2's Engine.BioGuiEvents
// ME2's orbit (SFXGameModeOrbital): the scanner's reticle.
ue3::ClassRef gOrbitalClass{"SFXGame.SFXGameModeOrbital", ue3::kEveryGame};
ue3::FieldRef gModeManager{"SFXGame.BioPlayerController", "GameModeManager2", ue3::kEveryGame};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes", ue3::kEveryGame};
ue3::FieldRef gReticleRot{"SFXGame.SFXGameModeOrbital", "ReticleRot", ue3::kEveryGame};
ue3::FieldRef gReticleMin{"SFXGame.SFXGameModeOrbital", "ReticleRotMinClamp", ue3::kEveryGame};
ue3::FieldRef gReticleMax{"SFXGame.SFXGameModeOrbital", "ReticleRotMaxClamp", ue3::kEveryGame};
ue3::FieldRef gReticleEffect{"SFXGame.SFXGameModeOrbital", "ReticleEffect", ue3::kEveryGame};
ue3::FieldRef gProbeActive{"SFXGame.SFXGameModeOrbital", "ProbeActive", ue3::kEveryGame};
ue3::FieldRef gComponentOwner{"Engine.ActorComponent", "Owner", ue3::kEveryGame};
ue3::FieldRef gLocalToWorld{"Engine.PrimitiveComponent", "LocalToWorld", ue3::kEveryGame};
ue3::FieldRef gRotation{"Engine.Actor", "Rotation", ue3::kEveryGame};
#endif

constexpr float kToleranceH = 0.06f;  // a pick within this share of the screen's height of an object (ME1's hologram: 2.5 degrees)
constexpr std::uint64_t kSelectWaitMs = 1500;
constexpr std::uint64_t kSteerMaxMs = 30000;  // a flight to a picked object gives up after this
constexpr float kArrivedH = 0.012f;          // the ship on the object's place on the screen (a share of its height)

struct Settings {
    bool pick = true;
    bool scan = true;
};

struct Pending {
    ue3::Object* target = nullptr;
    std::string label;
    std::uint64_t startedMs = 0;
    int stage = 0;  // 0 the cursor put on it, waiting for the game's selection; 1 A sent (its release next tick)
    // The map's cursor is the Normandy (ME2's cluster and system views: a skeletal mesh, not the galaxy view's InterpActor
    // cursor): never put on it (that would skip the flight and its fuel), it is flown there with the map's own stick.
    bool steer = false;
    std::uint64_t arrivedMs = 0;
    int state = -1;  // the map's level when picked (ME3's cluster view enters a system as the ship flies into it)
};

Settings gSettings;
std::uint64_t gSettingsMs = 0;
gh::Pointer gPointer;
std::optional<Pending> gPending;
int gHovered = -1;
ue3::Object* gHoveredActor = nullptr;
std::vector<ue3::Object*> gCandidates;
bool gOnPanel = false;
float gU = 0.0f, gV = 0.0f;
int gPicks = 0, gSelects = 0, gAbandoned = 0, gAims = 0;
std::string gLastPick = "none", gLastScan = "none";
bool gLoggedRay = false, gLoggedFrame = false;
int gFrameCheck = -1;  // the reticle's frame: -1 not checked, 0 the owner's rotation, 1 the world's, 2 neither (stands down)
float gSteerSign[2] = {1.0f, 1.0f};  // the map stick's x and y against the screen's right and up (`mevr galaxy ray steer-sign`)
float gSteerLast[2] = {};
ue3::Object* gHandler = nullptr;  // the live BioSFHandler_GalaxyMap (looked up at most once a second)
int gHandlerIndex = -1;
std::uint64_t gHandlerSearchMs = 0;

ue3::Object* Live(ue3::Object* o) {
    return o != nullptr && ue3::runtime::IsAlive(o) ? o : nullptr;
}
ue3::Object* ObjectField(ue3::FieldRef& field, ue3::Object* o) {
    return o != nullptr && ue3::runtime::IsAlive(o) ? Live(field.Read<ue3::Object*>(o).value_or(nullptr)) : nullptr;
}

void ReadSettings(std::uint64_t now) {
    if (gSettingsMs != 0 && now - gSettingsMs < 2000) return;
    gSettingsMs = now;
    std::wstring const path = engine::SettingsFile().wstring();
    if (path.empty()) return;
    gSettings.pick = GetPrivateProfileIntW(L"Galaxy", L"rayPick", 1, path.c_str()) != 0;
    gSettings.scan = GetPrivateProfileIntW(L"Galaxy", L"rayScan", 1, path.c_str()) != 0;
}

std::string NameOf(ue3::Object* o) {
    return o != nullptr ? ue3::runtime::Get().NameText(o) : std::string("none");
}

// The map's selectable objects at the level it shows (ME2: the galaxy's clusters, a cluster's systems, a system's planets by the
// camera's state 1, 2, 3; ME3: its one list).
std::vector<ue3::Object*> Selectables(ue3::Object* camera) {
    std::vector<ue3::Object*> out;
#if defined(MEVR_GAME_LE3)
    if (auto const list = gSelectableActors.ElementsOf(camera, 256); list && list->stride == sizeof(ue3::Object*)) {
        for (int i = 0; i < list->count; ++i) {
            if (ue3::Object* const a = Live(*static_cast<ue3::Object**>(list->At(i)))) out.push_back(a);
        }
    }
#else
    int const state = gCurrentState.Read<int>(camera).value_or(0);
    ue3::FieldRef* list = state == 1 ? &gGalaxySelectors : state == 2 ? &gClusterSelectors : state == 3 ? &gSystemSelectors : nullptr;
    ue3::FieldRef* actor = state == 1 ? &gGalaxySelectorActor : state == 2 ? &gClusterSelectorActor : &gSystemSelectorActor;
    if (list != nullptr) {
        if (auto const elements = list->ElementsOf(camera, 256)) {
            for (int i = 0; i < elements->count; ++i) {
                if (ue3::Object* const a = Live(actor->Read<ue3::Object*>(elements->At(i)).value_or(nullptr))) out.push_back(a);
            }
        }
    }
#endif
    return out;
}

gh::GameCamera CameraOf(Frame const& f) {
    gh::GameCamera c;
    if (!f.pov.valid || f.clientW <= 0 || f.clientH <= 0) return c;
    constexpr float kRotatorToRad = 3.14159265358979f / 32768.0f;
    c.location[0] = f.pov.location.x, c.location[1] = f.pov.location.y, c.location[2] = f.pov.location.z;
    c.pitch = static_cast<float>(f.pov.rotation.pitch) * kRotatorToRad;
    c.yaw = static_cast<float>(f.pov.rotation.yaw) * kRotatorToRad;
    c.roll = static_cast<float>(f.pov.rotation.roll) * kRotatorToRad;
    c.fovXDeg = f.pov.fovDeg;
    c.aspect = static_cast<float>(f.clientW) / static_cast<float>(f.clientH);
    c.valid = 1;
    return c;
}

// The ray's point on the map's UI panel (the game's screen), the right controller first.
std::optional<gr::Hit> RayHit(Frame const& f, int& hand, float& trigger) {
    vr::controllers::State const s = vr::controllers::Latest();
    hand = s.right.aimValid ? 1 : s.left.aimValid ? 0 : -1;
    if (!s.active || hand < 0 || f.scene == nullptr || f.clientW <= 0 || f.clientH <= 0) return std::nullopt;
    vr::controllers::Hand const& h = hand == 1 ? s.right : s.left;
    trigger = h.trigger;
    XrVector3f const origin = h.aim.position;
    XrVector3f const direction = vr::overlay::Rotate(h.aim.orientation, {0.0f, 0.0f, -1.0f});
    gh::Scene const& scene = *f.scene;
    gr::Panel const panel = gr::PanelOnDisc({scene.discCentre[0], scene.discCentre[1], scene.discCentre[2]},
        {scene.discNormal[0], scene.discNormal[1], scene.discNormal[2]}, origin, scene.windowWidthM, scene.windowCrop,
        static_cast<float>(f.clientW) / static_cast<float>(f.clientH));
    return gr::PanelHit(origin, direction, panel);
}

ue3::Object* Handler();

// The map's A. ME2: its GUI event (the handler's A, BIOGUI_EVENT_BUTTON_A, then its release; `mevr guikey 12` took ME2's map on).
// ME3: the pad's A is the galaxy mode's input binding, not a GUI event (GuiKey(15) did nothing at a selected planet, nor did the
// PC handler's PerformPrimaryAction, whose mouse action was none; run gxr-me3), so the map's own select,
// BioSFHandler_GalaxyMap.SelectGalaxyItem (ValidLevelTransition, ExecuteTravel: "Enter Orbit" took the map to its state 4); the
// release is nothing.
bool SendA(std::uint8_t event, char const* what) {
#if defined(MEVR_GAME_LE3)
    if (event == kButtonARelease) return true;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const handler = Handler();
    ue3::Function* const f = handler != nullptr ? r.FindFunction(r.ClassOf(handler), "SelectGalaxyItem") : nullptr;
    ue3::Params p(r, f);
    bool const called = f != nullptr && p.Valid() && ue3::runtime::Call(handler, f, p);
    if (!called) log::Warn("galaxy ray: the map's {} not sent: {}", what, handler == nullptr ? "no map handler" : "SelectGalaxyItem wasn't called");
    return called;
#else
    std::string why;
    bool const sent = game_menu_request::GuiKey(event, why);
    if (!sent) log::Warn("galaxy ray: the map's {} not sent: {}", what, why);
    return sent;
#endif
}

ue3::Object* Handler() {
    ue3::Reflection& r = ue3::runtime::Get();
    if (gHandler != nullptr && r.IsAlive(gHandler) && r.IndexOf(gHandler) == gHandlerIndex) return gHandler;
    gHandler = nullptr;
    std::uint64_t const now = GetTickCount64();
    if (now - gHandlerSearchMs < 1000 || gPcHandlerClass.Get() == nullptr) return nullptr;
    gHandlerSearchMs = now;
    for (int i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gPcHandlerClass) || r.NameText(o).starts_with("Default__")) continue;
        gHandler = o;  // the newest (a map opened again leaves the old handler alive for a while)
        gHandlerIndex = r.IndexOf(o);
    }
    return gHandler;
}

// The map's stick written (the handler's m_fLeftStickX/Y), or let go.
void Stick(float x, float y) {
    ue3::Object* const handler = Handler();
    float* const sx = handler != nullptr ? gLeftStickX.In<float>(handler) : nullptr;
    float* const sy = handler != nullptr ? gLeftStickY.In<float>(handler) : nullptr;
    if (sx == nullptr || sy == nullptr) return;
    *sx = x;
    *sy = y;
    write_journal::Wrote("GalaxyRay.stick", sx, sizeof(float) * 2);
    gSteerLast[0] = x, gSteerLast[1] = y;
}

std::optional<std::array<float, 2>> ScreenOf(Frame const& f, ue3::Object* actor) {
    auto const at = player::Location(actor);
    if (!at) return std::nullopt;
    float const p[3] = {at->x, at->y, at->z};
    return gh::ToGameScreen(CameraOf(f), p);
}

void EndSteer() {
    if (gSteerLast[0] != 0.0f || gSteerLast[1] != 0.0f) Stick(0.0f, 0.0f);
}

// The Normandy flown toward the picked object with the map's own stick: the direction on the game's screen from the ship to it.
// Arrived (the game selected it): the stick let go and the map's A.
void StepSteer(Frame const& f, Pending& p, std::uint64_t now) {
    ue3::Object* const ship = ObjectField(gCrossHairObject, f.camera);
    vr::controllers::State const pad = vr::controllers::Latest();
    bool const stickUsed = pad.active && std::hypot(pad.left.stickX, pad.left.stickY) > 0.35f;
    // The object brought to the ship's height (the ship flies in the map's plane; a cluster's systems stand above or below it,
    // so their own place on the screen is not where the ship must be to have them selected: ME3's cluster view, run gxr-me3).
    auto const from = ScreenOf(f, ship);
    std::optional<std::array<float, 2>> to;
    if (auto const shipAt = player::Location(ship)) {
        if (auto const targetAt = player::Location(p.target)) {
            float const level[3] = {targetAt->x, targetAt->y, shipAt->z};
            to = gh::ToGameScreen(CameraOf(f), level);
        }
    }
    bool const selected = ObjectField(gSelectedObject, f.camera) == p.target;
    float const aspect = f.clientH > 0 ? static_cast<float>(f.clientW) / static_cast<float>(f.clientH) : 1.0f;
    float dx = 0.0f, dy = 0.0f, distance = 0.0f;
    if (from && to) {
        dx = ((*to)[0] - (*from)[0]) * aspect;
        dy = (*from)[1] - (*to)[1];  // up on the screen
        distance = std::hypot(dx, dy);
    }
    if (int const state = gCurrentState.Read<int>(f.camera).value_or(-1); state != p.state) {
        log::Info("galaxy ray: the flight to '{}' ends: the map went on to its state {} ({} ms)", p.label, state, now - p.startedMs);
        EndSteer();
        gPending.reset();
        return;
    }
    if (stickUsed || !from || !to || now - p.startedMs > kSteerMaxMs) {
        ++gAbandoned;
        log::Info("galaxy ray: the flight to '{}' given up ({})", p.label, stickUsed ? "the player's stick took the ship" : !from || !to
            ? "the ship or the object isn't on the screen" : "it took too long");
        EndSteer();
        gPending.reset();
        return;
    }
    if (selected || distance < kArrivedH) {
        EndSteer();
        if (p.arrivedMs == 0) p.arrivedMs = now;
        if (!selected && now - p.arrivedMs < 1000) return;  // on it: the game's selection follows within a moment
        if (selected && SendA(kButtonA, "A")) {
            ++gSelects;
            log::Info("galaxy ray: the Normandy reached '{}' after {} ms, the game selected it: the map's A sent", p.label, now - p.startedMs);
            p.stage = 1;
            return;
        }
        log::Info("galaxy ray: the Normandy reached '{}' but the game didn't select it", p.label);
        gPending.reset();
        return;
    }
    p.arrivedMs = 0;
    float const speed = std::clamp(distance / 0.05f, 0.35f, 1.0f);  // full stick far away, gentler close in
    Stick(gSteerSign[0] * dx / distance * speed, gSteerSign[1] * dy / distance * speed);
}

// A pick being carried out (ME1's StartPick/StepPick): the cursor on the object, the game's selection, then the map's A.
void StepPending(Frame const& f) {
    if (!gPending) return;
    Pending& p = *gPending;
    std::uint64_t const now = GetTickCount64();
    if (p.stage == 1) {
        SendA(kButtonARelease, "A's release");
        gPending.reset();
        return;
    }
    if (p.steer && ue3::runtime::IsAlive(p.target)) {
        StepSteer(f, p, now);
        return;
    }
    if (!ue3::runtime::IsAlive(p.target) || now - p.startedMs > kSelectWaitMs) {
        ++gAbandoned;
        log::Info("galaxy ray: the pick of '{}' dropped ({})", p.label, ue3::runtime::IsAlive(p.target) ? "the game didn't select it" : "it went");
        gPending.reset();
        return;
    }
    if (ObjectField(gSelectedObject, f.camera) != p.target || gZoomTime.Read<float>(f.camera).value_or(0.0f) > 0.0f) return;
    if (SendA(kButtonA, "A")) {
        ++gSelects;
        log::Info("galaxy ray: '{}' selected by the game after {} ms: the map's A sent", p.label, now - p.startedMs);
    }
    p.stage = 1;
}

void StartPick(Frame const& f, ue3::Object* target) {
    ++gPicks;
    gLastPick = NameOf(target);
    ue3::Object* const crosshair = ObjectField(gCrossHairObject, f.camera);
    auto const at = player::Location(target);
    bool const ship = crosshair != nullptr && gInterpClass.Get() != nullptr && !ue3::IsA(crosshair, gInterpClass);
    if (ship && ObjectField(gSelectedObject, f.camera) != target) {
        log::Info("galaxy ray: picked '{}' (state {}): the Normandy ({}) flies there on the map's stick", gLastPick,
            gCurrentState.Read<int>(f.camera).value_or(-1), NameOf(crosshair));
        Pending pending{target, gLastPick, GetTickCount64(), 0};
        pending.steer = true;
        pending.state = gCurrentState.Read<int>(f.camera).value_or(-1);
        gPending = pending;
        return;
    }
    if (ObjectField(gSelectedObject, f.camera) != target && crosshair != nullptr && at) {
        ue3::Function* const fn = gSetLocation.Get();
        ue3::Params p(ue3::runtime::Get(), fn);
        bool const moved = fn != nullptr && p.Valid() && p.Set("NewLocation", *at) && ue3::runtime::Call(crosshair, fn, p);
        write_journal::Wrote("GalaxyRay.crosshair", crosshair, 0);
        log::Info("galaxy ray: picked '{}' (state {}): the map's cursor {} put on it", gLastPick, gCurrentState.Read<int>(f.camera).value_or(-1),
            moved ? NameOf(crosshair) : "couldn't be");
    } else {
        log::Info("galaxy ray: picked '{}' (state {}), already the game's selection", gLastPick, gCurrentState.Read<int>(f.camera).value_or(-1));
    }
    gPending = Pending{target, gLastPick, GetTickCount64(), 0};
}

void StepPick(Frame const& f, std::optional<gr::Hit> const& hit, int hand, float trigger) {
    gCandidates = Selectables(f.camera);
    gh::GameCamera const camera = CameraOf(f);
    std::vector<gr::Candidate> screen;
    screen.reserve(gCandidates.size());
    for (ue3::Object* a : gCandidates) {
        gr::Candidate c;
        if (auto const at = player::Location(a)) {
            float const p[3] = {at->x, at->y, at->z};
            if (auto const uv = gh::ToGameScreen(camera, p)) c = {(*uv)[0], (*uv)[1], true};
        }
        screen.push_back(c);
    }
    int const candidate = hit ? gr::Nearest(screen, hit->u, hit->v, camera.aspect, kToleranceH) : -1;
    auto const selected = gPointer.Step(candidate, hit ? trigger : 0.0f, hit.has_value());
    ue3::Object* const hovered = candidate >= 0 ? gCandidates[static_cast<std::size_t>(candidate)] : nullptr;
    if (hovered != gHoveredActor && hovered != nullptr && f.haptic != nullptr) f.haptic(hand, 0.012f, 0.18f);
    gHoveredActor = hovered;
    gHovered = candidate;
    if (selected && *selected >= 0 && *selected < static_cast<int>(gCandidates.size()) && (!gPending || gPending->steer)) {
        if (gPending) EndSteer();
        if (f.haptic != nullptr) f.haptic(hand, 0.04f, 0.6f);
        StartPick(f, gCandidates[static_cast<std::size_t>(*selected)]);
    }
}

#if defined(MEVR_GAME_LE2)
ue3::Object* OrbitalMode(ue3::Object* pc) {
    ue3::Object* const manager = ObjectField(gModeManager, pc);
    auto const list = manager != nullptr ? gGameModes.ElementsOf(manager, 64) : std::nullopt;
    if (!list || list->stride != sizeof(ue3::Object*)) return nullptr;
    for (int i = 0; i < list->count; ++i) {
        ue3::Object* const mode = Live(*static_cast<ue3::Object**>(list->At(i)));
        if (mode != nullptr && ue3::IsA(mode, gOrbitalClass)) return mode;
    }
    return nullptr;
}

struct Reticle {
    bool valid = false;
    ue3::Object* mode = nullptr;
    ue3::Rotator rot{}, min{}, max{}, frame{};
    std::array<float, 3> centre{}, at{};
    float radius = 0.0f;
};

// The scanner's reticle: its rotator, clamps, and where its effect is in the world (the owner's place, its frame's rotation).
Reticle ReadReticle(ue3::Object* pc) {
    Reticle r;
    r.mode = OrbitalMode(pc);
    ue3::Object* const effect = ObjectField(gReticleEffect, r.mode);
    ue3::Object* const owner = ObjectField(gComponentOwner, effect);
    auto const rot = r.mode != nullptr ? gReticleRot.Read<ue3::Rotator>(r.mode) : std::nullopt;
    auto const min = r.mode != nullptr ? gReticleMin.Read<ue3::Rotator>(r.mode) : std::nullopt;
    auto const max = r.mode != nullptr ? gReticleMax.Read<ue3::Rotator>(r.mode) : std::nullopt;
    auto const matrix = effect != nullptr ? gLocalToWorld.Read<std::array<float, 16>>(effect) : std::nullopt;
    auto const centre = player::Location(owner);
    auto const frame = owner != nullptr ? gRotation.Read<ue3::Rotator>(owner) : std::nullopt;
    if (!rot || !min || !max || !matrix || !centre || !frame) return r;
    r.rot = *rot, r.min = *min, r.max = *max, r.frame = *frame;
    r.centre = {centre->x, centre->y, centre->z};
    r.at = {(*matrix)[12], (*matrix)[13], (*matrix)[14]};
    float const dx = r.at[0] - r.centre[0], dy = r.at[1] - r.centre[1], dz = r.at[2] - r.centre[2];
    r.radius = std::sqrt(dx * dx + dy * dy + dz * dz);
    r.valid = r.radius > 1.0f;
    return r;
}

// The angle between the reticle's direction from its owner and Vector(ReticleRot) in a frame (the owner's or the world's).
float FrameError(Reticle const& r, ue3::Rotator const& frame) {
    std::array<float, 3> d{(r.at[0] - r.centre[0]) / r.radius, (r.at[1] - r.centre[1]) / r.radius, (r.at[2] - r.centre[2]) / r.radius};
    auto const local = gr::ToLocal(d, frame.pitch, frame.yaw, frame.roll);
    constexpr float k = 3.14159265358979f / 32768.0f;
    float const cp = std::cos(static_cast<float>(r.rot.pitch) * k), sp = std::sin(static_cast<float>(r.rot.pitch) * k);
    float const cy = std::cos(static_cast<float>(r.rot.yaw) * k), sy = std::sin(static_cast<float>(r.rot.yaw) * k);
    float const dot = local[0] * cp * cy + local[1] * cp * sy + local[2] * sp;
    return std::acos(std::clamp(dot, -1.0f, 1.0f)) * 57.29578f;
}

std::string ReticleJson(Reticle const& r) {
    return std::format("{{\"valid\": {}, \"rot\": [{}, {}], \"min\": [{}, {}], \"max\": [{}, {}], \"centre\": [{:.1f}, {:.1f}, {:.1f}], "
                       "\"at\": [{:.1f}, {:.1f}, {:.1f}], \"radius\": {:.1f}, \"frame\": [{}, {}, {}], \"frameCheck\": {}}}",
        r.valid, r.rot.pitch, r.rot.yaw, r.min.pitch, r.min.yaw, r.max.pitch, r.max.yaw, r.centre[0], r.centre[1], r.centre[2], r.at[0],
        r.at[1], r.at[2], r.radius, r.frame.pitch, r.frame.yaw, r.frame.roll, gFrameCheck);
}

// The scanner's reticle put where the ray meets the planet (ME2's orbit): the hit's direction from the reticle's owner in its
// frame as ReticleRot's yaw and pitch, inside the game's clamps. The game's UpdateReticle places the effects from it and its LT
// and RT scan and launch the probe there.
void StepScan(Frame const& f, std::optional<gr::Hit> const& hit) {
    if (!hit || !gSettings.scan) return;
    Reticle r = ReadReticle(f.pc);
    if (!r.valid || gProbeActive.ReadBool(r.mode).value_or(false)) return;
    if (gFrameCheck < 0) {
        float const own = FrameError(r, r.frame), world = FrameError(r, {});
        gFrameCheck = own < 5.0f ? 0 : world < 5.0f ? 1 : 2;
        log::Info("galaxy ray: the scanner's reticle sits {:.1f} units from its owner's place; Vector(ReticleRot) is off its direction "
                  "by {:.1f} degrees in the owner's frame, {:.1f} in the world's: {}", r.radius, own, world,
            gFrameCheck == 0 ? "the owner's frame" : gFrameCheck == 1 ? "the world's frame" : "neither (the ray doesn't aim the scanner)");
    }
    if (gFrameCheck == 2) return;
    auto const ray = gr::ScreenRay(CameraOf(f), hit->u, hit->v);
    auto const on = ray ? gr::SphereHit(*ray, r.centre, r.radius, 1.2f) : std::nullopt;
    if (!on) return;
    std::array<float, 3> d{(*on)[0] - r.centre[0], (*on)[1] - r.centre[1], (*on)[2] - r.centre[2]};
    ue3::Rotator const frame = gFrameCheck == 0 ? r.frame : ue3::Rotator{};
    gr::YawPitch const want = gr::ToRotator(gr::ToLocal(d, frame.pitch, frame.yaw, frame.roll));
    // The yaw nearest the reticle's own winding (a rotator's yaw isn't wrapped), inside the clamps.
    int yaw = want.yaw;
    while (yaw - r.rot.yaw > 32768) yaw -= 65536;
    while (r.rot.yaw - yaw > 32768) yaw += 65536;
    ue3::Rotator* const live = gReticleRot.In<ue3::Rotator>(r.mode);
    if (live == nullptr) return;
    live->yaw = std::clamp(yaw, std::min(r.min.yaw, r.max.yaw), std::max(r.min.yaw, r.max.yaw));
    live->pitch = std::clamp(want.pitch, std::min(r.min.pitch, r.max.pitch), std::max(r.min.pitch, r.max.pitch));
    write_journal::Wrote("GalaxyRay.reticle", live, sizeof(*live));
    if (++gAims == 1) log::Info("galaxy ray: the ray aims the scanner's reticle (yaw {}, pitch {})", live->yaw, live->pitch);
    gLastScan = std::format("yaw {} pitch {}", live->yaw, live->pitch);
}
#endif

// `mevr galaxy ray mouse on|off`: the map's movie told the player uses the mouse (BioSFPanel.bUsingGamepad off, the handler's
// mouse shown) or the pad, as the game's own device switch would (a diagnostic of the PC map's mouse controls).
std::string Mouse(bool on) {
    ue3::Reflection& r = ue3::runtime::Get();
    int done = 0;
    for (int i = 0, n = r.ObjectCount(); i < n && gPcHandlerClass.Get() != nullptr; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gPcHandlerClass) || r.NameText(o).starts_with("Default__")) continue;
        ue3::Object* const panel = ObjectField(gHandlerPanel, o);
        if (panel != nullptr && gUsingGamepad.WriteBool(panel, !on)) write_journal::Wrote("GalaxyRay.usingGamepad", panel, 0);
        if (ue3::Function* const f = r.FindFunction(r.ClassOf(o), "HandleButtonRefresh")) {
            ue3::Params p(r, f);
            if (p.Valid() && p.SetBool("usingGamepad", !on)) (void)ue3::runtime::Call(o, f, p);
        }
        ++done;
    }
    return std::format("galaxy ray: the map's {} handler(s) told the player uses the {}", done, on ? "mouse" : "pad");
}

} // namespace

void Tick(Frame const& f) {
    std::uint64_t const now = GetTickCount64();
    ReadSettings(now);
    if (f.camera == nullptr || f.scene == nullptr) {
        Stop("no map camera");
        return;
    }
    int hand = -1;
    float trigger = 0.0f;
    auto const hit = RayHit(f, hand, trigger);
    gOnPanel = hit.has_value();
    if (hit) gU = hit->u, gV = hit->v;
    if (hit && !gLoggedRay) {
        gLoggedRay = true;
        log::Info("galaxy ray: the {} controller's ray is on the map's screen at ({:.3f}, {:.3f}), {:.2f} m away", hand == 1 ? "right" : "left",
            hit->u, hit->v, hit->distanceM);
    }
#if defined(MEVR_GAME_LE2)
    if (f.orbit) {
        gPointer.Reset();
        gPending.reset();
        StepScan(f, hit);
        return;
    }
#endif
    StepPending(f);
    if (gSettings.pick) StepPick(f, hit, hand, trigger);
}

void Stop(char const* why) {
    if (gPending) log::Info("galaxy ray: the pick of '{}' dropped ({})", gPending->label, why);
    if (gPending && gPending->steer) EndSteer();
    gPending.reset();
    gPointer.Reset();
    gHovered = -1;
    gHoveredActor = nullptr;
    gOnPanel = false;
    gLoggedRay = false;
    gFrameCheck = -1;
}

std::string StatusJson() {
    return std::format("{{\"pick\": {}, \"scan\": {}, \"onPanel\": {}, \"uv\": [{:.3f}, {:.3f}], \"candidates\": {}, \"hovered\": \"{}\", "
                       "\"picks\": {}, \"selects\": {}, \"abandoned\": {}, \"lastPick\": \"{}\", \"pending\": {}, \"steering\": {}, \"stick\": [{:.2f}, {:.2f}], "
                       "\"aims\": {}, \"lastScan\": \"{}\"}}",
        gSettings.pick, gSettings.scan, gOnPanel, gU, gV, gCandidates.size(), NameOf(gHoveredActor), gPicks, gSelects, gAbandoned, gLastPick,
        gPending.has_value(), gPending && gPending->steer, gSteerLast[0], gSteerLast[1], gAims, gLastScan);
}

std::string Command(Frame const& f, std::string const& args) {
    std::istringstream words(args);
    std::string verb, option;
    words >> verb >> option;
    if (verb == "mouse") return Mouse(option != "off");
    if (verb == "a") return std::format("galaxy ray: the map's A {}", SendA(kButtonA, "A") ? "sent" : "not sent");
    if (verb == "steer-sign") {
        float y = 1.0f;
        words >> y;
        gSteerSign[0] = option == "-1" ? -1.0f : 1.0f;
        gSteerSign[1] = y < 0.0f ? -1.0f : 1.0f;
        return std::format("galaxy ray: the map's stick x {} y {} against the screen's right and up", gSteerSign[0], gSteerSign[1]);
    }
    if (verb == "stick") {
        // `mevr galaxy ray stick <x> <y> [ticks]`: the map's own stick written once (a test of the native ship's movement).
        float x = 0.0f, y = 0.0f;
        std::istringstream(option) >> x;
        words >> y;
        Stick(x, y);
        ue3::Object* const handler = Handler();
        return std::format("galaxy ray: the map's stick written ({}, {}) on {}", x, y, NameOf(handler));
    }
    if (verb == "pick" || verb == "scan") {
        (verb == "pick" ? gSettings.pick : gSettings.scan) = option != "off";
        gSettingsMs = GetTickCount64() + 600000;  // the command's choice holds (the file's is read again in 10 minutes)
    }
    if (verb == "screen") {
        // Each selectable object where the game's screen shows it (a test's aim: tools/make-galaxy-pick-recording.py --panel-uv).
        gh::GameCamera const camera = CameraOf(f);
        std::string out = "galaxy ray screen:";
        for (ue3::Object* a : Selectables(f.camera)) {
            auto const at = player::Location(a);
            float const p[3] = {at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f};
            auto const uv = at ? gh::ToGameScreen(camera, p) : std::nullopt;
            out += uv ? std::format(" {} {:.3f},{:.3f};", NameOf(a), (*uv)[0], (*uv)[1]) : std::format(" {} off;", NameOf(a));
        }
        return out;
    }
#if defined(MEVR_GAME_LE2)
    if (verb == "orbit") return "galaxy ray orbit: " + ReticleJson(ReadReticle(f.pc));
#endif
    if (!verb.empty() && verb != "state" && verb != "pick" && verb != "scan") {
        return "usage: mevr galaxy ray [state | pick on|off | scan on|off | mouse on|off | screen | orbit | stick x y | steer-sign x y]";
    }
    return "galaxy ray: " + StatusJson();
}

} // namespace mevr::game::galaxy_projector::ray
#endif
