// SPDX-License-Identifier: MIT
// The galaxy map over the CIC's projector, the sequels' one flow (game/GalaxyProjectorFlow.hpp). Its shape is ME3's first
// adapter's (game/Le3GalaxyCapture, lane GX, 2026-11-17), lifted out so ME2 runs the same code with its own facts (lane GX2),
// with ME3's travel, interlude, reservation, opening and closing rules (lane GX3) moved in (lane GX4): one flow, two adapters.
#include "game/GalaxyProjectorFlow.hpp"

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)  // the sequels' flow; ME1 keeps its own adapter, game/GalaxyMap
#include "core/Ini.hpp"
#include "core/Json.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/WorldViewHold.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/DepthOfFieldHold.hpp"
#include "game/GalaxyProjector.hpp"
#include "game/GalaxyRay.hpp"
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
namespace ga = engine::adapter;

ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden", ue3::kEveryGame};
ue3::ClassRef gEmitterClass{"Engine.Emitter", ue3::kEveryGame};
ue3::ClassRef gLensFlareClass{"Engine.LensFlareSource", ue3::kEveryGame};
ue3::ClassRef gInterpClass{"Engine.InterpActor", ue3::kEveryGame};
ue3::ClassRef gPointClass{"SFXGame.SFXPointOfInterest", ue3::kEveryGame};
ue3::ClassRef gMeshComponentClass{"Engine.StaticMeshComponent", ue3::kEveryGame};
ue3::ClassRef gLevelStreamingClass{"Engine.LevelStreaming", ue3::kEveryGame};
ue3::ClassRef gGalaxyModeClass{"SFXGame.SFXGameModeGalaxy", ue3::kEveryGame};
ue3::ClassRef gGalaxyCameraClass{"SFXGame.BioCameraBehaviorGalaxy", ue3::kEveryGame};
ue3::ClassRef gInterpActionClass{"Engine.SeqAct_Interp", ue3::kEveryGame};
ue3::ClassRef gDelayClass{"Engine.SeqAct_Delay", ue3::kEveryGame};
ue3::FieldRef gTag{"Engine.Actor", "Tag", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gLastRenderTime{"Engine.Actor", "LastRenderTime", ue3::kEveryGame};
ue3::FieldRef gTimeSeconds{"Engine.WorldInfo", "TimeSeconds", ue3::kEveryGame};
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
ue3::FieldRef gCameraObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pCameraObject", ue3::kEveryGame};
ue3::FieldRef gSelectedObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedObject", ue3::kEveryGame};
ue3::FieldRef gCrossHairObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pCrossHairObject", ue3::kEveryGame};
ue3::FieldRef gMassRelayObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pMassRelayObject", ue3::kEveryGame};
ue3::FieldRef gCurrentState{"SFXGame.BioCameraBehaviorGalaxy", "m_nCurrentState", ue3::kEveryGame};
ue3::FieldRef gCurrentSystem{"SFXGame.BioCameraBehaviorGalaxy", "m_pCurrentSystem", ue3::kEveryGame};
ue3::FieldRef gCurrentPlanet{"SFXGame.BioCameraBehaviorGalaxy", "m_pCurrentPlanet", ue3::kEveryGame};
// The map's selectable objects: ME3's one list of the current level's actors (lane GX3's steer names them by their tags);
// ME2's per level (the galaxy, a cluster, a system) as selectors (ME3 has those too).
#if defined(MEVR_GAME_LE3)
ue3::FieldRef gSelectableActors{"SFXGame.BioCameraBehaviorGalaxy", "m_aSelectableActors", ue3::kEveryGame};
#else
ue3::FieldRef gGalaxySelectors{"SFXGame.BioCameraBehaviorGalaxy", "m_GalaxySelectors", ue3::kEveryGame};
ue3::FieldRef gClusterSelectors{"SFXGame.BioCameraBehaviorGalaxy", "m_ClusterSelectors", ue3::kEveryGame};
ue3::FieldRef gSystemSelectors{"SFXGame.BioCameraBehaviorGalaxy", "m_SystemSelectors", ue3::kEveryGame};
ue3::FieldRef gGalaxySelectorActor{gGalaxySelectors, "m_actor"};
ue3::FieldRef gClusterSelectorActor{gClusterSelectors, "m_actor"};
ue3::FieldRef gSystemSelectorActor{gSystemSelectors, "m_actor"};
#endif
#if defined(MEVR_GAME_LE2)  // ME2's galaxy mode has the tabs as functions; ME3's binds TabLeft/TabRight elsewhere
ue3::FunctionRef gTabLeft{"SFXGame.SFXGameModeGalaxy.TabLeft", ue3::kEveryGame};
ue3::FunctionRef gTabRight{"SFXGame.SFXGameModeGalaxy.TabRight", ue3::kEveryGame};
#endif
ue3::FieldRef gInterpPosition{"Engine.SeqAct_Interp", "Position", ue3::kEveryGame};
ue3::FieldRef gDelayDuration{"Engine.SeqAct_Delay", "Duration", ue3::kEveryGame};
ue3::FieldRef gDelayRemaining{"Engine.SeqAct_Delay", "RemainingTime", ue3::kEveryGame};

constexpr char kProjectorTag[] = "GalaxyMapProxy";
constexpr char kProjectorMesh[] = "BIOA_GalaxyMap_S.GalaxyMapProxy";
constexpr char kUseTag[] = "placu_galaxyMap";
constexpr float kNearProjectorUU = 900.0f;  // ME1's "near the console": the room's retention is armed within this, on foot
constexpr std::uint64_t kSettledOnFootMs = 3000;  // a `scale set` only this long into gameplay (gotchas/me2-scale-set-during-load)
// The map's opening and closing cinematics (ME3 0 -> 8 -> 11 and 11 -> 0 -> 8 -> 0, run gx3a/gx3b; ME2's 0 -> 6 -> 9) are the
// game's camera flying between the CIC and the map's scene: Shepard's view stays held through them, as ME1's opening and closing
// (vr::galaxy_map::ClosingHold), at most this long each.
constexpr std::uint64_t kOpeningMs = 6000, kClosingMs = 3000;
// The GalaxyMap sequence's opening wait shortened to ME1's 0.05 s (game/GalaxyMap's kOpeningDelayS): ME1's BioSeqAct_Delay_0
// "Min intro scene time", ME2's BioSeqAct_Delay_0 (the same comment), ME3's SeqAct_Delay_1, all 2.5 s; the map's level is up in
// 0.1 s (ME3, run gx3b), and the camera's flight it waits for plays under Shepard's held view.
constexpr float kOpeningWaitS = 0.05f;
constexpr float kOpeningWaitAsShippedS = 2.5f;  // what a node already shortened (by a mod that unloaded without putting it back) gets
// A reload with the map open (ME3 loads a save written in the map straight into its mode): the room before the load is the room
// after it if the new world's projector is where it was and the XR session the same, at most this long after the load.
constexpr std::uint64_t kCarryMs = 120000;

struct Token {
    ue3::Object* object = nullptr;
    int index = -1;
    static Token Of(ue3::Object* o) { return {o, ue3::runtime::Get().IndexOf(o)}; }
    bool Alive() const { return object != nullptr && ue3::runtime::IsAlive(object) && ue3::runtime::Get().IndexOf(object) == index; }
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

// A streaming level of the player's world by its package (lower case), or null.
ue3::Object* StreamingLevel(ue3::Object* pc, char const* package) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = ObjectField(gWorldInfo, pc);
    // The map's level, looked up once per world (its streaming object stays while the world does).
    static ue3::Object* cachedWorld = nullptr;
    static ue3::Object* cachedLevel = nullptr;
    static int cachedIndex = -1;
    static char const* cachedPackage = nullptr;
    if (world != nullptr && world == cachedWorld && package == cachedPackage && cachedLevel != nullptr && r.IsAlive(cachedLevel) &&
        r.IndexOf(cachedLevel) == cachedIndex) {
        return cachedLevel;
    }
    auto const list = world != nullptr && package != nullptr ? gStreamingLevels.ElementsOf(world, 4096) : std::nullopt;
    if (!list || list->stride != sizeof(ue3::Object*)) return nullptr;
    for (int i = 0; i < list->count; ++i) {
        ue3::Object* const level = Live(*static_cast<ue3::Object**>(list->At(i)));
        if (level == nullptr || !ue3::IsA(level, gLevelStreamingClass)) continue;
        auto const name = gPackageName.Read<ue3::Name>(level);
        if (name && ini::Lower(r.ToString(*name)) == package) {
            cachedWorld = world, cachedLevel = level, cachedIndex = r.IndexOf(level), cachedPackage = package;
            return level;
        }
    }
    return nullptr;
}

// The map's own level's request and state (the adapter's mapLevel).
struct LevelRequest {
    bool found = false, load = false, show = false, loaded = false, visible = false;
};
LevelRequest MapLevel(ue3::Object* pc, char const* package) {
    LevelRequest out;
    ue3::Object* const level = StreamingLevel(pc, package);
    if (level == nullptr) return out;
    out.found = true;
    out.load = gShouldLoad.ReadBool(level).value_or(false);
    out.show = gShouldShow.ReadBool(level).value_or(false);
    out.loaded = Live(gLoadedLevel.Read<ue3::Object*>(level).value_or(nullptr)) != nullptr;
    out.visible = gIsVisible.ReadBool(level).value_or(false);
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

enum class Bridge { None, Opening, Closing };

// What the map's opening waits for (read only): from the console's 0 to the map's mode, when the map's level is loaded and
// visible and which of the Normandy's matinees (SeqAct_Interp: Position moves) and delays (SeqAct_Delay: RemainingTime moves)
// ran meanwhile (the owner's question, 2026-11-17; lane GX3's probe, every sequel's now).
struct OpeningProbe {
    bool on = false;
    std::uint64_t fromMs = 0;
    double loadedS = -1.0, visibleS = -1.0;
    std::string modes;
    struct Op { Token token; bool delay = false; float start = 0.0f, last = 0.0f, duration = 0.0f; };
    std::vector<Op> ops;
};

// The GalaxyMap sequence's opening wait (ME1's game/GalaxyMap ShortenOpening): the sequence's own longest delay.
struct OpeningWait {
    Token node;
    float originalS = 0.0f;
    bool shortened = false;
    bool keep = false;  // `mevr galaxy capture opening-keep`: as the game has it
    std::uint64_t searchedMs = 0;
    unsigned cuts = 0;
};

// The room as it was before a reload with the map open (step 4 of lane GX4): its disc, Shepard's view there.
struct Carry {
    bool valid = false;
    HeldView held;
    Disc disc;
    std::uint64_t ms = 0;
    std::string refused;  // the last reason it wasn't taken (logged once each)
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
bool holdOnly = false;      // `mevr galaxy capture hold-only`: the view held and the UI panel on the disc, no captures (a diagnostic)
bool holding = false;
// The map's interlude (galaxy_projector::MapInterlude: the travel's movie, a call, a question, a load): the table stays
// reserved (the room kept, the captures paused, no hold, no window) until the map's own modes come back, which re-open it from
// the saved on-foot measurement at once (ME1's rule, research/51 section 8.4).
bool suspended = false;
std::uint64_t suspendedMs = 0;
bool published = false;
std::vector<Token> hidden;  // the room's own projector hologram, hidden while the window lies on it
std::uint64_t lastProbeMs = 0;
std::string armRefused;
std::string reason = "idle";
std::string lastMeasure = "not measured";
unsigned frames = 0, openings = 0, interludes = 0, recentersKept = 0, reopenedAfterLoad = 0;
std::uint64_t onFootSinceMs = 0;
depth_of_field::Hold dof{"galaxy projector"};
int lastMode = -2;            // the mode log's last game mode (-1: unreadable)
Bridge bridge = Bridge::None;
bool closingFromMap = false;  // the closing began in the map's mode (map -> cinematic): an interlude if it outlasts its bound
std::uint64_t bridgeFromMs = 0, bridgeUntilMs = 0;
bool reservedOnFoot = false;  // the last tick published the on-foot reservation (at the armed projector)
std::uint64_t heldEndMs = 0;  // when the view was last released: ME3's X closes 11 -> 0 -> 8 (0.66 s) -> 0 (run gx3b)
std::uint32_t lastRecenters = 0;
LevelRequest footMapLevel;    // the map's level as it was last on foot (its opening's request is a change from this)
void (*hapticPulse)(int hand, float seconds, float amplitude) = nullptr;
gh::Scene lastScene;  // the scene published last while the map was held (the ray's console line reads its panel)
OpeningProbe probe;
OpeningWait wait;
Carry carry;

bool RetentionNeeded() {
    return adapter.armRetention != nullptr && adapter.retentionArmed != nullptr && adapter.retentionActive != nullptr;
}
bool RetentionArmed() {
    return !RetentionNeeded() || adapter.retentionArmed();
}
// The room is there for the map: kept by the retention while the map is open (or nothing to keep).
bool RoomActive() {
    return !RetentionNeeded() || adapter.retentionActive();
}
// ... and through the map's interludes.
bool RoomKept() {
    if (!RetentionNeeded()) return true;
    return adapter.retentionKept != nullptr ? adapter.retentionKept() : adapter.retentionActive();
}
bool RoomVisible(ue3::Object* pc) {
    return adapter.roomVisible == nullptr || adapter.roomVisible(pc);
}
// The console's use began the map's opening: its level requested since the player was last on foot.
bool MapLevelRequested(ue3::Object* pc) {
    if (adapter.mapLevel == nullptr) return false;
    LevelRequest const now = MapLevel(pc, adapter.mapLevel);
    return now.found && ((now.load && !footMapLevel.load) || (now.show && !footMapLevel.show));
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

// The GalaxyMapProxy mesh, the emitters over it (the decorative systems) and its lens flare, in the player's world. The flare
// (ME3's BioA_Nor_210CIC LensFlareSource_31, 5 cm from the disc's centre) is hidden by the hologram's mesh on foot; with the mesh
// hidden for the map its streak crossed the room as a blue band (lane GX3).
void HideProjector(ue3::Object* pc) {
    ue3::Function* const f = gSetHidden.Get();
    // A return from the map's interlude: still hidden, unless the room's art was rebuilt meanwhile (new actors, ours gone).
    std::erase_if(hidden, [](Token const& t) { return !t.Alive(); });
    if (f == nullptr || !hidden.empty() || !disc.valid) return;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = ObjectField(gWorldInfo, pc);
    for (int i = 0, n = r.ObjectCount(); world != nullptr && i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o)) continue;
        bool const interp = ue3::IsA(o, gInterpClass);
        if ((!interp && !ue3::IsA(o, gEmitterClass) && !(gLensFlareClass.Get() != nullptr && ue3::IsA(o, gLensFlareClass))) ||
            r.NameText(o).starts_with("Default__") || ObjectField(gWorldInfo, o) != world) continue;
        bool wanted = false;
        if (interp) {
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
    ray::Stop(why);
    Unpublish();
    window.Pause();
    ShowProjector();
    if (holding || suspended) log::Info("{}: the view is the game's again ({})", adapter.name, why);
    if (holding) heldEndMs = GetTickCount64();
    holding = suspended = false;
    reason = why;
}

// ---- The GalaxyMap sequence's opening wait (ME1's ShortenOpening) ----
// The sequence named GalaxyMap under a Normandy level's Main_Sequence: its own (not a sub-sequence's) longest delay.
void FindOpeningWait(std::uint64_t now) {
    if (wait.node.Alive() || now - wait.searchedMs < 3000 || gDelayClass.Get() == nullptr) return;
    wait = OpeningWait{{}, 0.0f, false, wait.keep, now, wait.cuts};
    ue3::Reflection& r = ue3::runtime::Get();
    float longest = 0.0f;
    ue3::Object* found = nullptr;
    for (int i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gDelayClass)) continue;
        ue3::Object* const sequence = r.OuterOf(o);
        if (sequence == nullptr || r.NameText(sequence) != "GalaxyMap" || r.NameText(r.OuterOf(sequence)) != "Main_Sequence") continue;
        if (ini::Lower(r.PathName(o)).find("_nor") == std::string::npos) continue;  // the Normandy's levels
        if (auto const d = gDelayDuration.Read<float>(o); d && *d > longest) {
            longest = *d;
            found = o;
        }
    }
    if (found == nullptr || !(longest > 1.0f || longest <= 0.1f)) return;  // none, or no wait worth shortening (a 1 s or less)
    wait.node = Token::Of(found);
    wait.originalS = longest > 0.1f ? longest : kOpeningWaitAsShippedS;  // 0.1 s or less: shortened before; put back as shipped
    wait.shortened = longest <= 0.1f;
    log::Info("{}: the map's opening wait is {} ({:.2f} s{})", adapter.name, r.FullName(found), longest,
        wait.shortened ? ", already shortened" : "");
}

// The opening wait shortened (in the CIC) or put back.
void ShortenOpening(bool shorten) {
    if (!wait.node.Alive() || wait.shortened == shorten) return;
    float* const duration = gDelayDuration.In<float>(wait.node.object);
    if (duration == nullptr) return;
    *duration = shorten ? kOpeningWaitS : wait.originalS;
    write_journal::Wrote("GalaxyProjectorFlow.openingWait", duration, sizeof(*duration));
    wait.shortened = shorten;
    if (shorten) ++wait.cuts;
    log::Info("{}: the map's opening wait {} ({:.2f} s)", adapter.name, shorten ? "shortened" : "as the game has it", *duration);
}

// A new player or world: every reference of the old one goes (its actors went with it). The map open at that moment (a reload
// with the map open) leaves its room carried, for a map the load opens straight away in the same room.
void ObserveRoom(ue3::Object* pc) {
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr);
    if (!room.Observe({reinterpret_cast<std::uintptr_t>(world), reinterpret_cast<std::uintptr_t>(pc)})) return;
    // A controller replacement can leave the old sequence alive in this world: our write goes back before it is forgotten.
    ShortenOpening(false);
    wait = OpeningWait{{}, 0.0f, false, wait.keep, 0, wait.cuts};
    Unpublish();
    window.Reset("the player/world changed");
    if (holding || suspended) {
        log::Info("{}: the player or the world changed with the map {}: everything of the old room forgotten (carried for a map "
                  "the load opens in the same room)", adapter.name, holding ? "open" : "in an interlude");
        if (disc.valid && held.haveView && held.footFrame) carry = Carry{true, held, disc, GetTickCount64(), {}};
    }
    hidden.clear();
    suspended = false;
    bridge = Bridge::None;
    held = {};
    render = {};
    mapPov = {};
    disc = {};
    holding = false;
    lastProbeMs = 0;
    onFootSinceMs = 0;
    footMapLevel = {};
    probe = {};
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

// A reload with the map open went straight into the map's mode: the room carried from before the load is taken if the new
// world's projector stands where it stood (the same CIC), the XR session and recenter are the same, the room's art is there
// and its retention arms for the open map; else the map is the renderer's flat panel (ME1's rule) and the reason is logged.
void TakeCarry(ue3::Object* pc, std::uint64_t now) {
    auto const refuse = [&](std::string why) {
        if (why != carry.refused) {
            LevelRequest const map = adapter.mapLevel != nullptr ? MapLevel(pc, adapter.mapLevel) : LevelRequest{};
            log::Info("{}: the map is open after a load, not on the projector: {} (the map's level loaded {}, visible {}; {})", adapter.name,
                why, map.loaded, map.visible, ProjectorJson(pc));
            carry.refused = why;
        }
    };
    if (now - carry.ms > kCarryMs) {
        refuse("the room before the load is too old");
        carry.valid = false;
        return;
    }
    if (!currentFloor || !currentFloor->valid || currentFloor->sessions != carry.disc.sessions ||
        currentFloor->recenters != carry.disc.recenters) {
        refuse("the headset's session or recenter changed since");
        return;
    }
    if (!RoomVisible(pc)) {
        refuse("the CIC's art isn't loaded and visible in the new world");
        return;
    }
    Projector const p = FindProjector(pc);
    if (!p.valid) {
        refuse("the new world's projector: " + p.reason);
        return;
    }
    double const moved = std::hypot(p.centre[0] - carry.disc.centreUU.x, p.centre[1] - carry.disc.centreUU.y, p.centre[2] - carry.disc.centreUU.z);
    if (moved > 2.0 || std::fabs(p.radiusUU - carry.disc.radiusUU) > 1.0) {
        refuse(std::format("the new world's projector is elsewhere ({:.1f} units away)", moved));
        return;
    }
    if (RetentionNeeded()) {
        std::string const refused = adapter.armRetentionInMap != nullptr ? adapter.armRetentionInMap(pc) : "no retention for an open map";
        if (!refused.empty()) {
            refuse("the room's retention: " + refused);
            return;
        }
    }
    held = carry.held;
    disc = carry.disc;
    carry = {};
    ++reopenedAfterLoad;
    log::Info("{}: the map opened by the load is in the same CIC: re-armed from the disc measured on foot before the load", adapter.name);
}

// ---- What the map's opening waits for (read only) ----
void ProbeBegin(std::uint64_t now, int mode) {
    probe = {};
    probe.on = true;
    probe.fromMs = now;
    probe.modes = std::format("0 -> {}", mode);
    ue3::Reflection& r = ue3::runtime::Get();
    for (int i = 0, n = r.ObjectCount(); i < n && probe.ops.size() < 4096; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o)) continue;
        bool const interp = gInterpActionClass.Get() && ue3::IsA(o, gInterpActionClass);
        bool const delay = !interp && gDelayClass.Get() && ue3::IsA(o, gDelayClass);
        if (!interp && !delay) continue;
        std::string const path = r.PathName(o);
        if (path.starts_with("Default__") || ini::Lower(path).find("_nor") == std::string::npos) continue;  // the Normandy's levels
        OpeningProbe::Op op;
        op.token = Token::Of(o);
        op.delay = delay;
        op.start = op.last = (delay ? gDelayRemaining : gInterpPosition).Read<float>(o).value_or(0.0f);
        if (delay) op.duration = gDelayDuration.Read<float>(o).value_or(0.0f);
        probe.ops.push_back(op);
    }
}

void ProbeTick(ue3::Object* pc, std::uint64_t now) {
    if (!probe.on) return;
    double const t = static_cast<double>(now - probe.fromMs) / 1000.0;
    if (adapter.mapLevel != nullptr) {
        LevelRequest const map = MapLevel(pc, adapter.mapLevel);
        if (probe.loadedS < 0.0 && map.loaded && !footMapLevel.loaded) probe.loadedS = t;
        if (probe.visibleS < 0.0 && map.visible) probe.visibleS = t;
    }
    for (auto& op : probe.ops) {
        if (op.token.Alive()) op.last = (op.delay ? gDelayRemaining : gInterpPosition).Read<float>(op.token.object).value_or(op.last);
    }
}

void ProbeEnd(ue3::Object* pc, std::uint64_t now, int modeNow) {
    if (!probe.on) return;
    ProbeTick(pc, now);
    probe.on = false;
    std::string ran;
    int count = 0;
    for (auto const& op : probe.ops) {
        if (!op.token.Alive() || std::fabs(op.last - op.start) < 0.01f || count >= 8) continue;
        ++count;
        ran += std::format("{}{} {} ({} {:.2f} -> {:.2f}{})", ran.empty() ? "" : "; ", op.delay ? "delay" : "matinee",
            ue3::runtime::Get().PathName(op.token.object), op.delay ? "remaining" : "position", op.start, op.last,
            op.delay ? std::format(", duration {:.2f}", op.duration) : std::string());
    }
    log::Info("{}: the map's opening: game modes {} in {:.2f} s (now {}); the map's level loaded at {}, visible at {}; ran meanwhile "
              "({} watched): {}",
        adapter.name, probe.modes, static_cast<double>(now - probe.fromMs) / 1000.0, modeNow,
        probe.loadedS < 0.0 ? std::string("never (or before)") : std::format("{:.2f} s", probe.loadedS),
        probe.visibleS < 0.0 ? std::string("never") : std::format("{:.2f} s", probe.visibleS), probe.ops.size(),
        ran.empty() ? std::string("no matinee or delay moved") : ran);
}

// The table's place in the room (the disc measured on foot) and the window's layout, for the held map and its reservations.
void FillTable(gh::Scene& scene, vr::galaxy_map::Settings const& settings) {
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
    scene.windowOn = 1;  // the layout reserved (the UI panel's width) even before the captures are up
    scene.windowWidthM = 2.0f * scene.discRadiusM;
}

// A scene without the window: a bridge (the opening or closing cinematic held) or a reservation (suspended). `openingCue`: the
// on-foot reservation at the projector, whose cinematic the host holds before this flow's next tick sees it (scene.holdPad[0],
// a spare byte: the struct's size and layout unchanged; plugin/GalaxyHologram's TableHeld): the renderer showed the opening's
// first 2-7 frames as a cinematic (the 3D cinema screen) until the bridge was published, ME2 and ME3 alike (lane GX4).
void PublishTable(vr::galaxy_map::Settings const& settings, bool reserve, bool openingCue = false) {
    gh::Scene scene;
    scene.sequence = ++frames;
    FillTable(scene, settings);
    scene.suspended = reserve ? 1 : 0;
    scene.holdPad[gh::kOpeningCue] = openingCue ? 1 : 0;
    PutRenderHold(held, render, true, scene);  // a reservation: the host holds it only in the map's modes
    publishScene(&scene);
    published = true;
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
    if (mode && ga::IsMapMode(*mode)) {
        mapPov = MapPov{true, *pov->location, *pov->rotation, *pov->fov, now};
    }
}

// The galaxy game mode (ME2's and ME3's SFXGameModeGalaxy among GameModeManager2.GameModes), or null.
ue3::Object* GalaxyMode(ue3::Object* pc) {
    ue3::Object* const manager = ObjectField(gModeManager, pc);
    if (manager == nullptr) return nullptr;
    auto const list = gGameModes.ElementsOf(manager, 64);
    if (!list || list->stride != sizeof(ue3::Object*)) return nullptr;
    for (int i = 0; i < list->count; ++i) {
        ue3::Object* const mode = Live(*static_cast<ue3::Object**>(list->At(i)));
        if (mode != nullptr && ue3::IsA(mode, gGalaxyModeClass)) return mode;
    }
    return nullptr;
}

// The galaxy mode's camera behaviour (its GalaxyCam), or null.
ue3::Object* GalaxyCamera(ue3::Object* pc) {
    ue3::Object* const camera = ObjectField(gGalaxyCam, GalaxyMode(pc));
    return camera != nullptr && ue3::IsA(camera, gGalaxyCameraClass) ? camera : nullptr;
}

// The map as the ray sees it this tick (game/GalaxyRay): its camera, mode and the scene published for the window.
ray::Frame RayFrame(ue3::Object* pc, gh::Scene const* scene) {
    ray::Frame f;
    f.pc = pc;
    f.camera = GalaxyCamera(pc);
    auto const mode = player::GameMode(pc);
    f.gameMode = mode ? static_cast<int>(*mode) : -1;
    f.orbit = adapter.orbitMode >= 0 && f.gameMode == adapter.orbitMode;
    f.scene = scene;
    f.pov = mapPov;
    RECT client{};
    if (HWND const w = GameWindow(); w != nullptr && GetClientRect(w, &client)) f.clientW = client.right, f.clientH = client.bottom;
    f.haptic = hapticPulse;
    return f;
}

#if defined(MEVR_GAME_LE2)
// `mevr galaxy capture tab-left|tab-right` (ME2): the map's own LB/RB (SFXGameModeGalaxy.TabLeft/TabRight: the next selectable
// object), a test's way to pick a cluster where the stick's cursor won't settle (ME2's galaxy view). In the map only.
std::string Tab(ue3::Object* pc, bool right) {
    auto const mode = player::GameMode(pc);
    ue3::Object* const galaxy = mode && ga::IsMapMode(*mode) ? GalaxyMode(pc) : nullptr;
    ue3::Function* const f = (right ? gTabRight : gTabLeft).Get();
    if (galaxy == nullptr || f == nullptr) return "galaxy tab: not in the map, or SFXGameModeGalaxy.TabLeft/TabRight not found";
    ue3::Params p(ue3::runtime::Get(), f);
    bool const called = p.Valid() && ue3::runtime::Call(galaxy, f, p);
    ue3::Object* const camera = GalaxyCamera(pc);
    ue3::Object* const selected = ObjectField(gSelectedObject, camera);
    return std::format("galaxy tab: {} called {}; selected {}", right ? "TabRight" : "TabLeft", called,
        selected != nullptr ? ue3::runtime::Get().PathName(selected) : std::string("none"));
}
#endif

json::Value ActorRow(ue3::Object* o) {
    auto row = json::Value::Object();
    ue3::Reflection& r = ue3::runtime::Get();
    row["name"] = o != nullptr ? r.PathName(o) : std::string("none");
    if (auto const at = o != nullptr ? player::Location(o) : std::nullopt) {
        row["x"] = at->x;
        row["y"] = at->y;
        row["z"] = at->z;
    }
    return row;
}

// `mevr galaxy capture ship`: where the map's ship, its camera and its relay stand (the map's own world units) and the map
// camera's yaw, so a test script can steer with the stick (tools/le2-halcyon-run/galaxy-steps.ps1). Read only.
std::string ShipJson(ue3::Object* pc) {
    auto out = json::Value::Object();
    auto const mode = player::GameMode(pc);
    ue3::Object* const camera = mode && ga::IsMapMode(*mode) ? GalaxyCamera(pc) : nullptr;
    out["available"] = camera != nullptr;
    if (camera == nullptr) return json::Write(out, 0);
    ue3::Reflection& r = ue3::runtime::Get();
    out["mode"] = static_cast<int>(*mode);
    out["state"] = gCurrentState.Read<int>(camera).value_or(-1);
    out["centre"] = ActorRow(ObjectField(gCenterObject, camera));
    out["cameraObject"] = ActorRow(ObjectField(gCameraObject, camera));
    out["selected"] = ActorRow(ObjectField(gSelectedObject, camera));
    out["crosshair"] = ActorRow(ObjectField(gCrossHairObject, camera));
    out["relay"] = ActorRow(ObjectField(gMassRelayObject, camera));
    ue3::Object* const system = ObjectField(gCurrentSystem, camera);
    ue3::Object* const planet = ObjectField(gCurrentPlanet, camera);
    out["system"] = system != nullptr ? r.PathName(system) : std::string("none");
    out["planet"] = planet != nullptr ? r.PathName(planet) : std::string("none");
    if (mapPov.valid) {
        out["cameraYaw"] = mapPov.rotation.yaw;
        out["cameraPitch"] = mapPov.rotation.pitch;
        out["cameraAt"] = std::format("{:.0f} {:.0f} {:.0f}", mapPov.location.x, mapPov.location.y, mapPov.location.z);
    }
    auto selectable = json::Value::Array();
    auto const add = [&](ue3::Object* a, char const* level) {
        if (a == nullptr || !r.IsAlive(a) || selectable.Items().size() >= 64) return;
        auto row = json::Value::Object();
        row["name"] = r.NameText(a);
        if (auto const tag = gTag.Read<ue3::Name>(a)) row["tag"] = r.ToString(*tag);
        if (level != nullptr) row["level"] = level;
        if (auto const at = player::Location(a)) {
            row["x"] = at->x;
            row["y"] = at->y;
            row["z"] = at->z;
        }
        selectable.Push(std::move(row));
    };
#if defined(MEVR_GAME_LE3)
    if (auto const list = gSelectableActors.ElementsOf(camera, 256); list && list->stride == sizeof(ue3::Object*)) {
        for (int i = 0; i < list->count; ++i) add(Live(*static_cast<ue3::Object**>(list->At(i))), nullptr);
    }
#else
    struct Selectors { ue3::FieldRef* list; ue3::FieldRef* actor; char const* level; };
    for (Selectors s : {Selectors{&gSystemSelectors, &gSystemSelectorActor, "system"}, Selectors{&gClusterSelectors, &gClusterSelectorActor, "cluster"},
             Selectors{&gGalaxySelectors, &gGalaxySelectorActor, "galaxy"}}) {  // ME2
        auto const list = s.list->ElementsOf(camera, 256);
        if (!list) continue;
        for (int i = 0; i < list->count; ++i) add(Live(s.actor->Read<ue3::Object*>(list->At(i)).value_or(nullptr)), s.level);
    }
#endif
    out["selectable"] = std::move(selectable);
    return json::Write(out, 0);
}

// `mevr galaxy capture flares`: the lens flares in the player's world nearest the projector, where they are and when they were
// last drawn (the blue band over ME3's pit was the projector's own flare, lane GX3).
std::string FlareList(ue3::Object* pc) {
    struct Row { double distance; json::Value value; };
    std::vector<Row> rows;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = ObjectField(gWorldInfo, pc);
    float const now = world != nullptr ? gTimeSeconds.Read<float>(world).value_or(0.0f) : 0.0f;
    for (int i = 0, n = r.ObjectCount(); world != nullptr && gLensFlareClass.Get() && i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gLensFlareClass) || r.NameText(o).starts_with("Default__") ||
            ObjectField(gWorldInfo, o) != world) continue;
        auto row = json::Value::Object();
        row["path"] = r.PathName(o);
        double distance = 1e9;
        if (auto const at = player::Location(o)) {
            row["at"] = std::format("{:.0f} {:.0f} {:.0f}", at->x, at->y, at->z);
            if (disc.valid) {
                distance = std::hypot(at->x - disc.centreUU.x, at->y - disc.centreUU.y, at->z - disc.centreUU.z) / 100.0;
                row["fromDiscM"] = distance;
            }
        }
        if (auto const drawn = gLastRenderTime.Read<float>(o)) row["drawnAgoS"] = now - *drawn;
        bool mine = false;
        for (Token const& t : hidden) mine = mine || t.object == o;
        row["hiddenByUs"] = mine;
        rows.push_back({distance, std::move(row)});
    }
    std::sort(rows.begin(), rows.end(), [](Row const& a, Row const& b) { return a.distance < b.distance; });
    auto out = json::Value::Array();
    for (std::size_t k = 0; k < rows.size() && k < 24; ++k) out.Push(std::move(rows[k].value));
    return json::Write(out, 0);
}

} // namespace

std::optional<ue3::Vector> MapCentre(ue3::Object* pc) {
    return player::Location(ObjectField(gCenterObject, GalaxyCamera(pc)));
}

void TryStartup(Adapter const& a) {
    adapter = a;
    dof = depth_of_field::Hold{adapter.name};
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
        // A load with the map open, the player gone for a tick: the room is carried for a map the load opens in the same room.
        if ((holding || suspended) && disc.valid && held.haveView && held.footFrame) carry = Carry{true, held, disc, GetTickCount64(), {}};
        Stop("no player");
        return;
    }
    ObserveRoom(pc);
    vr::galaxy_map::Settings const settings = hand_calibration::GetGalaxy();
    bool const enabled = !off && handlerOn && publish != nullptr && settings.hologram && settings.inWorld && settings.window;
    auto const mode = player::GameMode(pc);
    std::uint64_t const now = GetTickCount64();
    bool const onFoot = mode && *mode == 0;
    bool const map = mode && ga::IsMapMode(*mode);
    if (onFoot && !holding && room.Available() && player::FloorLockSample().cameraOwned) {
        if (auto const frame = view_anchor::Current()) held.footFrame = frame;
    }
    if (onFoot) carry = {};  // on foot in this world: the room is measured here (the carry was for a map a load opened)
    // The projector, measured on foot on the CIC's deck (every 2 s, after a second on foot: ME2's closing passes through a brief
    // 0 under the closing camera, lane GX4): its disc is where the map's window lies.
    onFootSinceMs = !onFoot ? 0 : onFootSinceMs != 0 ? onFootSinceMs : now;
    if (enabled && onFoot && !holding && now - onFootSinceMs >= 1000 && now - lastProbeMs >= 2000 && RoomVisible(pc)) {
        lastProbeMs = now;
        MeasureDisc(pc);
    }
    // The depth of field off near the projector before the map opens, back away from it (the adapter's; on foot and settled only).
    if (adapter.depthOfFieldOff && onFoot && now - onFootSinceMs >= kSettledOnFootMs) {
        if (enabled && disc.valid && NearProjector(pc)) {
            dof.Off("near the CIC's projector: the map's opening with it on faults this game's rendering thread");
        } else {
            dof.Back("away from the projector");
        }
    }
    // The disc is measured in the room's tracking space: a new XR session drops it (measured again on foot). A recenter on foot
    // waits for the next measurement (2 s); inside the map it keeps the disc, since the held view and the app space move together
    // (the window and the room shift as one: plugin/HalcyonSequelBridge's Recentered, docs/galaxy-projector-handoff.md "Recenter").
    bool const inside = holding || suspended;
    bool const epoch = floor && floor->valid && disc.valid && floor->sessions == disc.sessions && (inside || floor->recenters == disc.recenters);
    if (floor && floor->valid && floor->recenters != lastRecenters) {
        if (inside && disc.valid && floor->sessions == disc.sessions) {
            ++recentersKept;
            log::Info("{}: a recenter with the map {}: the disc kept in the room's space (the held view moves with it)", adapter.name,
                holding ? "open" : "in an interlude");
        }
        lastRecenters = floor->recenters;
    }
    // The map's level as it is on foot: its opening's request is a change from this.
    if (onFoot && disc.valid && adapter.mapLevel != nullptr) footMapLevel = MapLevel(pc, adapter.mapLevel);
    int const modeNumber = mode ? static_cast<int>(*mode) : -1;
    ProbeTick(pc, now);
    if (modeNumber != lastMode) {
        // The console's use straight into the map (the opening's cinematic begins the probe below, with its bridge).
        if (lastMode == 0 && map && reservedOnFoot) ProbeBegin(now, modeNumber);
        else if (probe.on) probe.modes += std::format(" -> {}", modeNumber);
        if (probe.on && (map || modeNumber == 0)) ProbeEnd(pc, now, modeNumber);
        if (inside || reservedOnFoot || (RetentionNeeded() && adapter.retentionArmed())) {
            log::Info("{}: game mode {} -> {} ({}; the map's level {})", adapter.name, lastMode, modeNumber,
                holding ? "the map held" : suspended ? "the table reserved" : bridge != Bridge::None ? "a bridge held" : "on foot, armed",
                MapLevelRequested(pc) ? "requested" : "not requested");
        }
        lastMode = modeNumber;
    }
    if (probe.on && now - probe.fromMs > 10000) ProbeEnd(pc, now, modeNumber);
    // Near the projector the room's art is kept through the map's opening where the game would unload it (the adapter's).
    if (RetentionNeeded() && enabled && onFoot && epoch && NearProjector(pc)) {
        if (!adapter.retentionArmed()) {
            std::string const refused = adapter.armRetention(pc);
            if (refused != armRefused) {
                log::Info("{}: the room's retention {}", adapter.name, refused.empty() ? "armed near the projector" : "not armed: " + refused);
                armRefused = refused;
            }
        } else if (adapter.refreshRetention != nullptr) {
            adapter.refreshRetention();  // its wait for the map runs from the player's last moment at the projector
        }
    }
    // The GalaxyMap sequence's opening wait, shortened while the map opens in this CIC (ME1's rule), put back otherwise.
    if (disc.valid) FindOpeningWait(now);  // (in the CIC only: a search of every object)
    ShortenOpening(enabled && !wait.keep && disc.valid);
    // A reload with the map open: the map came up in its mode with no view on foot in this world.
    if (carry.valid && enabled && map && !inside && !disc.valid) TakeCarry(pc, now);
    bool const haveRoom = epoch && held.haveView && held.footFrame;
    // The opening and closing cinematics, held at Shepard's (ME1's rule).
    bool const cinematic = mode && ga::GameMode(*mode) == ga::Mode::Cinematic;
    if (bridge != Bridge::None && (!cinematic || now >= bridgeUntilMs || !enabled || !haveRoom)) {
        bool const outlasted = cinematic && now >= bridgeUntilMs;
        log::Info("{}: the map's {} cinematic held at Shepard's for {:.1f} s ({})", adapter.name,
            bridge == Bridge::Opening ? "opening" : "closing", static_cast<double>(now - bridgeFromMs) / 1000.0,
            !cinematic ? "it ended" : outlasted ? "past its bound: the view is the game's" : "the room went");
        reservedOnFoot = false;  // an opening's cue is spent: a cinematic past its bound isn't bridged again
        // A cinematic straight from the map that outlasts the closing's bound was the map's interlude (a call, a travel scene):
        // the table stays reserved for the map's return (ME1's RememberTable after its ClosingHold).
        if (bridge == Bridge::Closing && closingFromMap && outlasted && enabled && haveRoom && RoomKept()) {
            suspended = true;
            suspendedMs = bridgeFromMs;
            ++interludes;
            log::Info("{}: the map's interlude (a cinematic, game mode {}): the table reserved, the captures paused, the view the "
                      "game's until the map comes back", adapter.name, modeNumber);
        }
        bridge = Bridge::None;
    }
    if (enabled && haveRoom && cinematic && bridge == Bridge::None && !suspended) {
        if (holding) {
            bridge = Bridge::Closing;
            closingFromMap = true;
            holding = false;
            heldEndMs = now;
            window.Pause();
            ShowProjector();
        } else if (heldEndMs != 0 && now - heldEndMs < 2000) {
            // The closing's cinematic comes after a brief 0 (ME3 11 -> 0 -> 8 -> 0, ME2 9 -> 0 -> 6 -> 0): ME1's ClosingHold, held
            // whatever the map's level (ME2's is no longer requested by then).
            bridge = Bridge::Closing;
            closingFromMap = false;
        } else if (reservedOnFoot && RetentionArmed() && MapLevelRequested(pc)) {
            bridge = Bridge::Opening;
            closingFromMap = false;
            ProbeBegin(now, modeNumber);
        }
        if (bridge != Bridge::None) {
            bridgeFromMs = now;
            bridgeUntilMs = now + (bridge == Bridge::Opening ? kOpeningMs : kClosingMs);
            log::Info("{}: the map's {} cinematic: the view held at Shepard's through it", adapter.name,
                bridge == Bridge::Opening ? "opening" : "closing");
        }
    }
    bool const wasReserved = reservedOnFoot;
    reservedOnFoot = false;
    if (bridge == Bridge::Opening && RetentionNeeded() && adapter.refreshRetention != nullptr) adapter.refreshRetention();
    if (bridge != Bridge::None) {
        reservedOnFoot = wasReserved && bridge == Bridge::Opening;  // the opening's cinematic keeps the on-foot reservation's cue
        PublishTable(settings, false);
        reason = bridge == Bridge::Opening ? "the map's opening cinematic held" : "the map's closing cinematic held";
        return;
    }
    bool const want = enabled && map && haveRoom && RoomActive();
    // The map's interlude (the relay's movie, a call, a question, a load): the table reserved, the captures paused, no hold.
    if (enabled && !map && (holding || suspended) && haveRoom && MapInterlude(mode) && RoomKept()) {
        if (!suspended) {
            suspended = true;
            suspendedMs = now;
            ++interludes;
            log::Info("{}: the map's interlude (game mode {}): the table reserved, the captures paused, the view the game's until the "
                      "map comes back", adapter.name, modeNumber);
        }
        holding = false;
        window.Pause();
        PublishTable(settings, true);
        reason = "the map's interlude: the table reserved";
        return;
    }
    if (!want) {
        if (holding || suspended) {
            Stop(!enabled ? "off" : !map ? "the map closed" : !epoch ? "the room's tracking changed (a new session)"
                : !RoomActive() ? "the room's retention ended" : "no view on foot");
        }
        window.Pause();
        // On foot at the armed projector: the table reserved before the map opens, so its very first frame is held in the CIC
        // and never falls to the renderer's flat galaxy panel (ME1's reservation, research/51 section 8.4).
        if (enabled && onFoot && haveRoom && RetentionArmed() && NearProjector(pc)) {
            PublishTable(settings, true, true);
            reservedOnFoot = true;
            reason = "on foot at the projector: the table reserved for the map's first frame";
            return;
        }
        Unpublish();
        reason = !enabled ? "off" : map ? "map without the room (no retention, view or tracking)" : onFoot ? "on foot" : "not the map";
        return;
    }
    if (!holding) {
        holding = true;
        auto const& v = held.floorView;
        if (suspended) {
            log::Info("{}: the map is back after its interlude ({:.1f} s): the view held at Shepard's again, the window on the disc "
                      "measured on foot", adapter.name, static_cast<double>(now - suspendedMs) / 1000.0);
        } else {
            ++openings;
            log::Info("{}: the map is open in the CIC: the view held at Shepard's ({:.0f}, {:.0f}, {:.0f}; floor lock {}, raw Z {:.1f}), "
                      "the map's window on the projector", adapter.name, held.location.x, held.location.y, held.location.z, v.floorLocked,
                held.rawZ);
        }
        suspended = false;
        HideProjector(pc);
    }
    gh::Scene scene;
    scene.sequence = ++frames;
    FillTable(scene, settings);
    WindowLook look;
    look.widthM = scene.windowWidthM;
    look.depth = settings.windowDepth;
    look.round = settings.windowRound;
    look.backdrop = settings.windowBackdrop;
    look.postProcess = postProcess;
    look.postProcessChain = chainChoice != 0 ? capture_rig::EngineChain(chainChoice == 2) : nullptr;
    // Presentation does not depend on the captures' readiness: the window's layout and UI panel stand while they warm up.
    bool const fresh = mapPov.valid && now - mapPov.ms < 500 && mapPov.fovDeg > 1.0f && mapPov.fovDeg < 179.0f;
    HWND const gameWindow = GameWindow();
    RECT client{};
    if (!holdOnly && fresh && gameWindow != nullptr && GetClientRect(gameWindow, &client) && client.right >= 64 &&
        client.bottom >= 64) {
        window.Step(mapPov, MapCentre(pc), client.right, client.bottom, look, scene);
    } else {
        window.Pause();
    }
    PutRenderHold(held, render, true, scene);
    // The right controller's ray on the map (ME1's way of picking it): its objects picked, ME2's scanner aimed (game/GalaxyRay).
    if (fresh) {
        lastScene = scene;
        ray::Tick(RayFrame(pc, &scene));
    } else {
        ray::Stop("the map camera isn't fresh");
    }
    publish(&scene);
    published = true;
    reason = window.On() ? "held, the window on the projector" : "held, the window warming up";
}

void SetHaptic(void (*haptic)(int hand, float seconds, float amplitude)) {
    hapticPulse = haptic;
}

std::string RayCommand(ue3::Object* pc, std::string const& args) {
    return ray::Command(RayFrame(pc, holding ? &lastScene : nullptr), args);
}

void Shutdown(bool exiting) {
    if (!exiting) {
        Unpublish();
        ShowProjector();
        ShortenOpening(false);
        // Back on foot only: a hot reload with ME2's map open put the depth of field back on, and the map's closing faulted the
        // render thread at exe+0x98C25D (lane GX4, gotchas/me2-scale-set-during-load). Left off otherwise (the next generation
        // turns it off again near the projector and back away from it).
        ue3::Object* const pc = player::Controller();
        if (dof.IsOff() && pc != nullptr && player::GameMode(pc).value_or(255) == 0) {
            dof.Back("the mod unloads");
        } else if (dof.IsOff()) {
            log::Info("{}: the depth of field left off as the mod unloads (not on foot: putting it back now faults this game's "
                      "rendering thread)", adapter.name);
        }
    }
    window.Shutdown(exiting);
    publishScene = nullptr;
    currentFloor.reset();
    holding = suspended = published = false;
    bridge = Bridge::None;
    hidden.clear();
    wait = {};
    carry = {};
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
    out["retentionKept"] = RetentionNeeded() ? RoomKept() : false;
    out["suspended"] = suspended;
    out["bridge"] = bridge == Bridge::Opening ? "opening" : bridge == Bridge::Closing ? "closing" : "none";
    out["interludes"] = static_cast<int>(interludes);
    out["recentersKept"] = static_cast<int>(recentersKept);
    out["reopenedAfterLoad"] = static_cast<int>(reopenedAfterLoad);
    out["carried"] = carry.valid;
    out["openingWait"] = !wait.node.Alive() ? std::string("not found")
        : std::format("{} ({})", wait.shortened ? "shortened" : "as the game has it", wait.keep ? "kept by command" : "cut in the CIC");
    out["openingWaitCuts"] = static_cast<int>(wait.cuts);
    out["off"] = off;
    out["holdOnly"] = holdOnly;
    out["depthOfFieldOff"] = dof.IsOff();
    return json::Write(out, 0);
}

std::string Command(ue3::Object* pc, std::string const& action) {
    if (action == "ship") return "galaxy ship: " + ShipJson(pc);
    if (action == "flares") return "galaxy flares: " + FlareList(pc);
#if defined(MEVR_GAME_LE2)
    if (action == "tab-left" || action == "tab-right") return Tab(pc, action == "tab-right");
#endif
    if (action == "off") {
        off = true;
        Stop("command-off");
    } else if (action == "hold-only") {
        off = false;
        holdOnly = true;
        window.Reset("the captures are off (hold-only)");
    } else if (action == "opening-keep" || action == "opening-cut") {
        wait.keep = action == "opening-keep";  // the next tick puts the wait back or shortens it
    } else if (action == "on" || action == "on-raw" || action == "on-post" || action == "on-default" || action == "on-ui") {
        off = false;
        holdOnly = false;
        postProcess = action == "on-post" || action == "on-default" || action == "on-ui";  // diagnostics of the post path
        chainChoice = action == "on-default" ? 1 : action == "on-ui" ? 2 : 0;
        window.Reset("the capture's options changed");
    } else if (action != "state" && !action.empty()) {
        return "usage: mevr galaxy capture on|on-post|on-default|on-ui|hold-only|off|opening-keep|opening-cut|ship|flares|tab-left|"
               "tab-right|state";
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
