// SPDX-License-Identifier: MIT
// The galaxy map over the CIC's projector, the sequels' one flow (game/GalaxyProjectorFlow.hpp). Its shape is ME3's first
// adapter's (game/Le3GalaxyCapture, lane GX, 2026-11-17), lifted out so ME2 runs the same code with its own facts.
#include "game/GalaxyProjectorFlow.hpp"

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)  // the sequels' flow; ME1 keeps its own adapter, game/GalaxyMap
#include "core/Json.hpp"
#include "core/Log.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/WorldViewHold.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/GalaxyProjector.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <format>
#include <vector>

namespace mevr::game::galaxy_projector::flow {
namespace {

namespace gh = vr::galaxy_holo;

ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden", ue3::kEveryGame};
ue3::ClassRef gEmitterClass{"Engine.Emitter", ue3::kEveryGame};
ue3::ClassRef gInterpClass{"Engine.InterpActor", ue3::kEveryGame};
ue3::ClassRef gPointClass{"SFXGame.SFXPointOfInterest", ue3::kEveryGame};
ue3::ClassRef gMeshComponentClass{"Engine.StaticMeshComponent", ue3::kEveryGame};
ue3::ClassRef gLevelStreamingClass{"Engine.LevelStreaming", ue3::kEveryGame};
ue3::ClassRef gGalaxyModeClass{"SFXGame.SFXGameModeGalaxy", ue3::kEveryGame};
ue3::ClassRef gGalaxyCameraClass{"SFXGame.BioCameraBehaviorGalaxy", ue3::kEveryGame};
ue3::FieldRef gTag{"Engine.Actor", "Tag", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gMeshComponent{"Engine.DynamicSMActor", "StaticMeshComponent", ue3::kEveryGame};
ue3::FieldRef gStaticMesh{"Engine.StaticMeshComponent", "StaticMesh", ue3::kEveryGame};
ue3::FieldRef gBounds{"Engine.PrimitiveComponent", "Bounds", ue3::kEveryGame};
ue3::FieldRef gBoxExtent{gBounds, "BoxExtent"};
ue3::FieldRef gLocalToWorld{"Engine.PrimitiveComponent", "LocalToWorld", ue3::kEveryGame};
ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels", ue3::kEveryGame};
ue3::FieldRef gPackageName{"Engine.LevelStreaming", "PackageName", ue3::kEveryGame};
ue3::FieldRef gLoadedLevel{"Engine.LevelStreaming", "LoadedLevel", ue3::kEveryGame};
ue3::FieldRef gIsVisible{"Engine.LevelStreaming", "bIsVisible", ue3::kEveryGame};
ue3::FieldRef gShouldLoad{"Engine.LevelStreaming", "bShouldBeLoaded", ue3::kEveryGame};
ue3::FieldRef gShouldShow{"Engine.LevelStreaming", "bShouldBeVisible", ue3::kEveryGame};
ue3::FieldRef gModeManager{"SFXGame.BioPlayerController", "GameModeManager2", ue3::kEveryGame};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes", ue3::kEveryGame};
ue3::FieldRef gGalaxyCam{"SFXGame.SFXGameModeGalaxy", "GalaxyCam", ue3::kEveryGame};
ue3::FieldRef gCenterObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pCenterObject", ue3::kEveryGame};

constexpr char kProjectorTag[] = "GalaxyMapProxy";
constexpr char kProjectorMesh[] = "BIOA_GalaxyMap_S.GalaxyMapProxy";
constexpr char kUseTag[] = "placu_galaxyMap";
constexpr float kNearProjectorUU = 900.0f;  // ME1's "near the console": the room's retention is armed within this, on foot

struct Token {
    ue3::Object* object = nullptr;
    int index = -1;
    static Token Of(ue3::Object* o) { return {o, ue3::runtime::Get().IndexOf(o)}; }
    bool Alive() const { return ue3::runtime::IsAlive(object) && ue3::runtime::Get().IndexOf(object) == index; }
};

ue3::Object* Live(ue3::Object* o) {
    return ue3::runtime::IsAlive(o) ? o : nullptr;
}
ue3::Object* ObjectField(ue3::FieldRef& field, ue3::Object* object) {
    return object != nullptr && ue3::runtime::IsAlive(object) ? Live(field.Read<ue3::Object*>(object).value_or(nullptr)) : nullptr;
}

// The projector in the player's world: the InterpActor tagged GalaxyMapProxy with BIOA_GalaxyMap_S.GalaxyMapProxy (a square in
// its local XY plane with the galaxy painted as a disc), and the console's point of interest placu_galaxyMap. The same names
// in ME2's CIC (BioA_Nor_110's InterpActor_0, BioD_Nor_103CIC's SFXPointOfInterest_6) and ME3's (research/le3-galaxy.md).
struct Projector {
    bool valid = false;
    std::string reason = "not measured";
    ue3::Object* actor = nullptr;
    ue3::Object* use = nullptr;
    std::array<double, 3> centre{};
    std::array<double, 3> normal{};
    double radiusUU = 0.0;
    int candidates = 0, uses = 0;
};

Projector FindProjector(ue3::Object* pc) {
    Projector out;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = ObjectField(gWorldInfo, pc);
    if (world == nullptr) {
        out.reason = "no world";
        return out;
    }
    if (gInterpClass.Get() == nullptr || gPointClass.Get() == nullptr || gMeshComponentClass.Get() == nullptr) {
        out.reason = "the projector's classes weren't found";
        return out;
    }
    for (int i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o)) continue;
        bool const interp = ue3::IsA(o, gInterpClass);
        bool const point = !interp && ue3::IsA(o, gPointClass);
        if ((!interp && !point) || r.NameText(o).starts_with("Default__") || ObjectField(gWorldInfo, o) != world) continue;
        auto const tag = gTag.Read<ue3::Name>(o);
        if (!tag) continue;
        std::string const name = r.ToString(*tag);
        if (interp && name == kProjectorTag) {
            out.actor = o;
            ++out.candidates;
        } else if (point && name == kUseTag) {
            out.use = o;
            ++out.uses;
        }
    }
    if (out.candidates != 1 || out.uses != 1) {
        out.reason = std::format("{} projector and {} console candidates (one of each wanted)", out.candidates, out.uses);
        return out;
    }
    ue3::Object* const component = ObjectField(gMeshComponent, out.actor);
    if (component == nullptr || !ue3::IsA(component, gMeshComponentClass) || r.OuterOf(component) != out.actor) {
        out.reason = "the projector has no mesh component";
        return out;
    }
    ue3::Object* const mesh = ObjectField(gStaticMesh, component);
    if (mesh == nullptr || r.PathName(mesh) != kProjectorMesh) {
        out.reason = std::format("the projector's mesh is {}", mesh != nullptr ? r.PathName(mesh) : std::string("none"));
        return out;
    }
    auto const extent = gBoxExtent.Read<ue3::Vector>(gBounds.Address(component));
    auto const matrix = gLocalToWorld.Read<std::array<float, 16>>(component);
    if (!extent || !matrix) {
        out.reason = "the projector's bounds or transform weren't read";
        return out;
    }
    auto const& m = *matrix;
    for (float x : m) {
        if (!std::isfinite(x)) {
            out.reason = "the projector's transform isn't finite";
            return out;
        }
    }
    if (std::fabs(m[3]) > 1e-5f || std::fabs(m[7]) > 1e-5f || std::fabs(m[11]) > 1e-5f || std::fabs(m[15] - 1.0f) > 1e-5f) {
        out.reason = "the projector's transform isn't affine";
        return out;
    }
    std::array<double, 3> forward{m[0], m[1], m[2]}, right{m[4], m[5], m[6]};
    auto const length = [](auto const& v) { return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); };
    double const f = length(forward), s = length(right);
    if (f < 1e-6 || s < 1e-6 || std::fabs(f - s) > 1e-4 * std::max(f, s)) {
        out.reason = "the projector isn't a square (scaled unevenly in its plane)";
        return out;
    }
    for (double& x : forward) x /= f;
    for (double& x : right) x /= s;
    auto const radius = gh::ProjectorRadius({extent->x, extent->y, extent->z}, forward, right);
    if (!radius || *radius <= 50.0 || *radius >= 2000.0) {
        out.reason = "the projector's radius is out of range";
        return out;
    }
    out.centre = {m[12], m[13], m[14]};
    out.normal = {forward[1] * right[2] - forward[2] * right[1], forward[2] * right[0] - forward[0] * right[2],
        forward[0] * right[1] - forward[1] * right[0]};
    out.radiusUU = *radius;
    out.valid = true;
    out.reason = "measured";
    return out;
}

// The projector's disc as Shepard's view on foot maps it (tracking space), and in the world; measured on foot only (in the
// map the player camera is the map's, which can't say where the room is), in the floor's session and recenter epoch.
struct Disc {
    bool valid = false;
    float centre[3] = {};
    float normal[3] = {};
    float radiusM = 0.0f;
    ue3::Vector centreUU{};
    float radiusUU = 0.0f;
    std::uint32_t sessions = 0, recenters = 0;
};

Adapter adapter;
Publish publishScene = nullptr;
std::optional<vr::floor::State> currentFloor;
vr::world_view::Reference room;
HeldView held;
RenderHold render;
MapPov mapPov;
StereoWindow window;
Disc disc;
bool handlerOn = false;
bool off = false;           // `mevr galaxy capture off`
bool postProcess = false;   // the captures without a post-process chain (ME3: any chain left them black; ME2 the same default)
int chainChoice = 0;        // 0 the player's chain, 1 Engine.DefaultPostProcess, 2 DefaultUIScenePostProcess (diagnostics)
bool holding = false;
bool published = false;
std::vector<Token> hidden;  // the room's own projector hologram, hidden while the window lies on it
std::uint64_t lastProbeMs = 0;
std::string armRefused;
std::string reason = "idle";
std::string lastMeasure = "not measured";
unsigned frames = 0, openings = 0;

bool RetentionNeeded() {
    return adapter.armRetention != nullptr && adapter.retentionArmed != nullptr && adapter.retentionActive != nullptr;
}
bool RoomVisible(ue3::Object* pc) {
    return adapter.roomVisible == nullptr || adapter.roomVisible(pc);
}

void Unpublish() {
    if (published && publishScene) publishScene(nullptr);
    published = false;
}

void ShowProjector() {
    ue3::Function* const f = gSetHidden.Get();
    int shown = 0;
    for (Token const& t : hidden) {
        if (f == nullptr || !t.Alive()) continue;  // the room's art rebuilt on the way out: a new actor, never hidden by us
        ue3::Params p(ue3::runtime::Get(), f);
        if (p.Valid() && p.SetBool("bNewHidden", false) && ue3::runtime::Call(t.object, f, p)) ++shown;
    }
    if (!hidden.empty()) log::Info("{}: the room's own projector hologram shown again ({} of {} actors)", adapter.name, shown, hidden.size());
    hidden.clear();
}

// The GalaxyMapProxy mesh and the emitters over it (the decorative systems), in the player's world.
void HideProjector(ue3::Object* pc) {
    ue3::Function* const f = gSetHidden.Get();
    if (f == nullptr || !hidden.empty() || !disc.valid) return;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = ObjectField(gWorldInfo, pc);
    for (int i = 0, n = r.ObjectCount(); world != nullptr && i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || (!ue3::IsA(o, gInterpClass) && !ue3::IsA(o, gEmitterClass)) ||
            r.NameText(o).starts_with("Default__") || ObjectField(gWorldInfo, o) != world) continue;
        bool wanted = false;
        if (ue3::IsA(o, gInterpClass)) {
            auto const tag = gTag.Read<ue3::Name>(o);
            wanted = tag && r.ToString(*tag) == kProjectorTag;
        } else if (auto const at = player::Location(o)) {
            float const dx = at->x - disc.centreUU.x, dy = at->y - disc.centreUU.y, dz = at->z - disc.centreUU.z;
            wanted = std::sqrt(dx * dx + dy * dy + dz * dz) < 1.5f * disc.radiusUU;
        }
        if (wanted) hidden.push_back(Token::Of(o));
    }
    int done = 0;
    for (Token const& t : hidden) {
        ue3::Params p(r, f);
        if (p.Valid() && p.SetBool("bNewHidden", true) && ue3::runtime::Call(t.object, f, p)) ++done;
    }
    log::Info("{}: the room's own projector hologram hidden while the map's window lies on it ({} of {} actors)", adapter.name, done,
        hidden.size());
}

void Stop(char const* why) {
    Unpublish();
    window.Pause();
    ShowProjector();
    if (holding) log::Info("{}: the view is the game's again ({})", adapter.name, why);
    holding = false;
    reason = why;
}

// A new player or world: every reference of the old one goes (its actors went with it).
void ObserveRoom(ue3::Object* pc) {
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr);
    if (!room.Observe({reinterpret_cast<std::uintptr_t>(world), reinterpret_cast<std::uintptr_t>(pc)})) return;
    Unpublish();
    window.Reset("the player/world changed");
    hidden.clear();
    held = {};
    render = {};
    mapPov = {};
    disc = {};
    holding = false;
    lastProbeMs = 0;
}

void MeasureDisc(ue3::Object* pc) {
    Projector const p = FindProjector(pc);
    lastMeasure = p.reason;
    if (!p.valid || !currentFloor || !currentFloor->valid) return;
    if (!player::FloorLockSample().cameraOwned || view_anchor::Held()) {
        lastMeasure = "on foot, but the view isn't the player's own";
        return;
    }
    auto const frame = view_anchor::Current();
    if (!frame || !std::isfinite(frame->unitsPerMeter) || frame->unitsPerMeter <= 0.0) {
        lastMeasure = "no tracking frame";
        return;
    }
    auto const at = engine::ToTracking(*frame, {p.centre[0], p.centre[1], p.centre[2]});
    auto const up = engine::DirectionToTracking(*frame, {p.normal[0], p.normal[1], p.normal[2]});
    if (!std::isfinite(at.x) || !std::isfinite(at.y) || !std::isfinite(at.z) || !std::isfinite(up.x) || !std::isfinite(up.y) ||
        !std::isfinite(up.z)) {
        lastMeasure = "the tracking frame mapped the projector to no place";
        return;
    }
    Disc d;
    d.centre[0] = at.x, d.centre[1] = at.y, d.centre[2] = at.z;
    d.normal[0] = up.x, d.normal[1] = up.y, d.normal[2] = up.z;
    d.radiusM = static_cast<float>(p.radiusUU / frame->unitsPerMeter);
    d.radiusUU = static_cast<float>(p.radiusUU);
    d.centreUU = {static_cast<float>(p.centre[0]), static_cast<float>(p.centre[1]), static_cast<float>(p.centre[2])};
    if (!(d.radiusM > 0.1f && d.radiusM < 10.0f)) {
        lastMeasure = "the projector's radius in metres is out of range";
        return;
    }
    d.sessions = currentFloor->sessions;
    d.recenters = currentFloor->recenters;
    d.valid = true;
    float const moved = disc.valid ? std::hypot(d.centre[0] - disc.centre[0], d.centre[1] - disc.centre[1], d.centre[2] - disc.centre[2]) : 1e9f;
    if (moved > 0.05f) {
        log::Info("{}: the CIC's projector measured on foot: a disc at ({:.2f}, {:.2f}, {:.2f}) m, its normal ({:.2f}, {:.2f}, {:.2f}) "
                  "({:.0f} degrees off vertical), {:.2f} m in radius (the world's ({:.1f}, {:.1f}, {:.1f}), {:.1f} units)",
            adapter.name, d.centre[0], d.centre[1], d.centre[2], d.normal[0], d.normal[1], d.normal[2],
            std::acos(std::clamp(std::fabs(d.normal[1]), 0.0f, 1.0f)) * 57.29578f, d.radiusM, d.centreUU.x, d.centreUU.y, d.centreUU.z,
            d.radiusUU);
    }
    disc = d;
}

bool NearProjector(ue3::Object* pc) {
    auto const at = player::Location(player::Pawn(pc));
    return at && disc.valid && std::hypot(at->x - disc.centreUU.x, at->y - disc.centreUU.y) < kNearProjectorUU;
}

// Camera.UpdateCamera, after the game's: Shepard's view on foot remembered, the map camera's POV and where the POV lives
// (the host swaps the held view in around the main view's CalcSceneView only; the game keeps its map camera).
void OnUpdateCamera(ue3::Object* playerCamera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const pc = player::Controller();
    if (playerCamera == nullptr || pc == nullptr || player::CameraOwner(playerCamera) != pc) return;
    ObserveRoom(pc);
    auto const pov = player::CameraPov(playerCamera);
    if (!pov || pov->location == nullptr || pov->rotation == nullptr || pov->fov == nullptr) return;
    std::uint64_t const now = GetTickCount64();
    render = RenderHold{true, pov->location, pov->rotation, pov->fov, now};
    auto const mode = player::GameMode(pc);
    if (!holding && room.Remember(mode.value_or(255), player::FloorLockSample().cameraOwned, false)) {
        Remember(held, *pov->location, *pov->rotation, *pov->fov);
    }
    if (mode && engine::adapter::IsMapMode(*mode)) {
        mapPov = MapPov{true, *pov->location, *pov->rotation, *pov->fov, now};
    }
}

} // namespace

std::optional<ue3::Vector> MapCentre(ue3::Object* pc) {
    ue3::Object* const manager = ObjectField(gModeManager, pc);
    if (manager == nullptr) return std::nullopt;
    auto const list = gGameModes.ElementsOf(manager, 64);
    if (!list || list->stride != sizeof(ue3::Object*)) return std::nullopt;
    for (int i = 0; i < list->count; ++i) {
        ue3::Object* const mode = Live(*static_cast<ue3::Object**>(list->At(i)));
        if (mode == nullptr || !ue3::IsA(mode, gGalaxyModeClass)) continue;
        ue3::Object* const camera = ObjectField(gGalaxyCam, mode);
        if (camera == nullptr || !ue3::IsA(camera, gGalaxyCameraClass)) continue;
        if (auto const at = player::Location(ObjectField(gCenterObject, camera))) return at;
    }
    return std::nullopt;
}

void TryStartup(Adapter const& a) {
    adapter = a;
    ue3::Function* const update = ue3::runtime::Up() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("{}: Engine.Camera.UpdateCamera not found: the map stays on the renderer's flat panel", adapter.name);
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
    handlerOn = true;
}

void Tick(Publish publish, std::optional<vr::floor::State> const& floor) {
    publishScene = publish;
    currentFloor = floor;
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr) {
        Stop("no player");
        return;
    }
    ObserveRoom(pc);
    vr::galaxy_map::Settings const settings = hand_calibration::GetGalaxy();
    bool const enabled = !off && handlerOn && publish != nullptr && settings.hologram && settings.inWorld && settings.window;
    auto const mode = player::GameMode(pc);
    std::uint64_t const now = GetTickCount64();
    bool const onFoot = mode && *mode == 0;
    bool const map = mode && engine::adapter::IsMapMode(*mode);
    if (onFoot && !holding && room.Available() && player::FloorLockSample().cameraOwned) {
        if (auto const frame = view_anchor::Current()) held.footFrame = frame;
    }
    // The projector, measured on foot on the CIC's deck (every 2 s): its disc is where the map's window lies.
    if (enabled && onFoot && !holding && now - lastProbeMs >= 2000 && RoomVisible(pc)) {
        lastProbeMs = now;
        MeasureDisc(pc);
    }
    bool const epoch = floor && floor->valid && disc.valid && floor->sessions == disc.sessions && floor->recenters == disc.recenters;
    // Near the projector the room's art is kept through the map's opening where the game would unload it (the adapter's).
    if (RetentionNeeded() && enabled && onFoot && epoch && NearProjector(pc) && !adapter.retentionArmed()) {
        std::string const refused = adapter.armRetention(pc);
        if (refused != armRefused) {
            log::Info("{}: the room's retention {}", adapter.name, refused.empty() ? "armed near the projector" : "not armed: " + refused);
            armRefused = refused;
        }
    }
    bool const roomKept = !RetentionNeeded() || adapter.retentionActive();
    bool const want = enabled && map && epoch && held.haveView && held.footFrame && roomKept;
    if (want != holding) {
        if (!want) {
            Stop(!enabled ? "off" : !map ? "the map closed" : !epoch ? "the room's tracking changed (recenter or session)"
                : !roomKept ? "the room's retention ended" : "no view on foot");
            return;
        }
        holding = true;
        ++openings;
        auto const& v = held.floorView;
        log::Info("{}: the map is open in the CIC: the view held at Shepard's ({:.0f}, {:.0f}, {:.0f}; floor lock {}, raw Z {:.1f}), the "
                  "map's window on the projector", adapter.name, held.location.x, held.location.y, held.location.z, v.floorLocked, held.rawZ);
        HideProjector(pc);
    }
    if (!holding) {
        Unpublish();
        window.Pause();
        reason = !enabled ? "off" : map ? "map without the room (no retention, view or tracking)" : onFoot ? "on foot" : "not the map";
        return;
    }
    gh::Scene scene;
    scene.sequence = ++frames;
    scene.inWorld = 1;
    scene.anchorValid = 1;
    scene.onFoot = 0;
    for (int i = 0; i < 3; ++i) {
        scene.anchor[i] = disc.centre[i];
        scene.discCentre[i] = disc.centre[i];
        scene.discNormal[i] = disc.normal[i];
    }
    scene.windowDisc = 1;
    scene.discRadiusM = disc.radiusM * settings.windowScale;
    WindowLook look;
    look.widthM = 2.0f * scene.discRadiusM;
    look.depth = settings.windowDepth;
    look.round = settings.windowRound;
    look.backdrop = settings.windowBackdrop;
    look.postProcess = postProcess;
    look.postProcessChain = chainChoice != 0 ? capture_rig::EngineChain(chainChoice == 2) : nullptr;
    // Presentation does not depend on the captures' readiness: the window's layout and UI panel stand while they warm up.
    scene.windowOn = 1;
    scene.windowWidthM = look.widthM;
    bool const fresh = mapPov.valid && now - mapPov.ms < 500 && mapPov.fovDeg > 1.0f && mapPov.fovDeg < 179.0f;
    HWND const gameWindow = GameWindow();
    RECT client{};
    if (fresh && gameWindow != nullptr && GetClientRect(gameWindow, &client) && client.right >= 64 && client.bottom >= 64) {
        window.Step(mapPov, MapCentre(pc), client.right, client.bottom, look, scene);
    } else {
        window.Pause();
    }
    PutRenderHold(held, render, true, scene);
    publish(&scene);
    published = true;
    reason = window.On() ? "held, the window on the projector" : "held, the window warming up";
}

void Shutdown(bool exiting) {
    if (!exiting) {
        Unpublish();
        ShowProjector();
    }
    window.Shutdown(exiting);
    publishScene = nullptr;
    currentFloor.reset();
    holding = published = false;
    hidden.clear();
}

std::string StatusJson() {
    auto out = json::Value::Object();
    out["holding"] = holding;
    out["published"] = published;
    out["reason"] = reason;
    out["measure"] = lastMeasure;
    out["openings"] = static_cast<int>(openings);
    out["frames"] = static_cast<int>(frames);
    out["discValid"] = disc.valid;
    out["discRadiusM"] = disc.radiusM;
    out["haveView"] = held.haveView;
    out["footFrame"] = held.footFrame.has_value();
    out["windowOn"] = window.On();
    out["baselineUU"] = window.BaselineUU();
    out["convergeUU"] = window.ConvergeUU();
    out["captureWidth"] = window.Eye(0).Width();
    out["captureHeight"] = window.Eye(0).Height();
    out["postProcess"] = postProcess;
    out["chain"] = chainChoice == 1 ? "engine default" : chainChoice == 2 ? "engine UI scene" : "the player's";
    out["projectorHidden"] = static_cast<int>(hidden.size());
    out["retentionNeeded"] = RetentionNeeded();
    out["retentionArmed"] = RetentionNeeded() ? adapter.retentionArmed() : false;
    out["retentionActive"] = RetentionNeeded() ? adapter.retentionActive() : false;
    out["off"] = off;
    return json::Write(out, 0);
}

std::string Command(ue3::Object* /*pc*/, std::string const& action) {
    if (action == "off") {
        off = true;
        Stop("command-off");
    } else if (action == "on" || action == "on-raw" || action == "on-post" || action == "on-default" || action == "on-ui") {
        off = false;
        postProcess = action == "on-post" || action == "on-default" || action == "on-ui";  // diagnostics of the post path
        chainChoice = action == "on-default" ? 1 : action == "on-ui" ? 2 : 0;
        window.Reset("the capture's options changed");
    } else if (action != "state" && !action.empty()) {
        return "usage: mevr galaxy capture on|on-post|on-default|on-ui|off|state";
    }
    return "galaxy capture: " + StatusJson();
}

std::string ProjectorJson(ue3::Object* pc) {
    auto out = json::Value::Object();
    out["readOnly"] = true;
    if (!ue3::runtime::Up() || pc == nullptr) {
        out["reason"] = "no player";
        return json::Write(out, 0);
    }
    auto const mode = player::GameMode(pc);
    out["gameMode"] = mode ? static_cast<int>(*mode) : -1;
    Projector const p = FindProjector(pc);
    out["available"] = p.valid;
    out["reason"] = p.reason;
    out["projectorCandidates"] = p.candidates;
    out["useCandidates"] = p.uses;
    ue3::Reflection& r = ue3::runtime::Get();
    if (p.actor != nullptr) out["projector"] = r.PathName(p.actor);
    if (p.use != nullptr) {
        out["useTarget"] = r.PathName(p.use);
        if (auto const at = player::Location(p.use)) {
            auto v = json::Value::Array();
            v.Push(at->x), v.Push(at->y), v.Push(at->z);
            out["useAtUU"] = std::move(v);
        }
    }
    if (p.valid) {
        auto c = json::Value::Array(), n = json::Value::Array();
        for (double x : p.centre) c.Push(x);
        for (double x : p.normal) n.Push(x);
        out["centreUU"] = std::move(c);
        out["normal"] = std::move(n);
        out["radiusUU"] = p.radiusUU;
    }
    if (auto const at = player::Location(player::Pawn(pc))) {
        auto v = json::Value::Array();
        v.Push(at->x), v.Push(at->y), v.Push(at->z);
        out["pawnUU"] = std::move(v);
    }
    if (auto const centre = MapCentre(pc)) {
        auto v = json::Value::Array();
        v.Push(centre->x), v.Push(centre->y), v.Push(centre->z);
        out["mapCentreUU"] = std::move(v);
    }
    // Every streaming level of the player's world: which the map's opening loads, shows and unloads (the retention question).
    ue3::Object* const world = ObjectField(gWorldInfo, pc);
    auto rows = json::Value::Array();
    if (auto const list = world != nullptr ? gStreamingLevels.ElementsOf(world, 4096) : std::nullopt;
        list && list->stride == sizeof(ue3::Object*)) {
        for (int i = 0; i < list->count; ++i) {
            ue3::Object* const level = Live(*static_cast<ue3::Object**>(list->At(i)));
            if (level == nullptr || !ue3::IsA(level, gLevelStreamingClass)) continue;
            auto const package = gPackageName.Read<ue3::Name>(level);
            if (!package) continue;
            auto row = json::Value::Object();
            row["package"] = r.ToString(*package);
            row["loaded"] = Live(gLoadedLevel.Read<ue3::Object*>(level).value_or(nullptr)) != nullptr;
            row["visible"] = gIsVisible.ReadBool(level).value_or(false);
            row["shouldLoad"] = gShouldLoad.ReadBool(level).value_or(false);
            row["shouldShow"] = gShouldShow.ReadBool(level).value_or(false);
            rows.Push(std::move(row));
        }
    }
    out["streaming"] = std::move(rows);
    return json::Write(out, 0);
}

} // namespace mevr::game::galaxy_projector::flow
#endif
