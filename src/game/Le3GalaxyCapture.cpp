// SPDX-License-Identifier: MIT
// ME3's galaxy map over the CIC's projector: ME3's adapter of the shared flow (game/Le3GalaxyCapture.hpp).
#include "game/Le3GalaxyCapture.hpp"
#if defined(MEVR_GAME_LE3)
#include "game/GalaxyProjector.hpp"
#include "game/HandCalibration.hpp"
#include "game/Le3GalaxyProjector.hpp"
#include "game/Le3GalaxyRetention.hpp"
#include "game/ViewAnchor.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/GalaxyMap.hpp"
#include "core/vr/WorldViewHold.hpp"
#include "engine/ProcessEvent.hpp"
#include "ue3/Params.hpp"
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

namespace mevr::game::le3_galaxy_capture {
namespace {
using namespace le3_galaxy;
namespace gp = galaxy_projector;
namespace gh = vr::galaxy_holo;

ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera", ue3::kEveryGame};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden", ue3::kEveryGame};
ue3::ClassRef gEmitterClass{"Engine.Emitter", ue3::kEveryGame};
ue3::ClassRef gInterpClass{"Engine.InterpActor", ue3::kEveryGame};
ue3::FieldRef gTag{"Engine.Actor", "Tag", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::ClassRef gLensFlareClass{"Engine.LensFlareSource", ue3::kEveryGame};
ue3::FieldRef gLastRenderTime{"Engine.Actor", "LastRenderTime", ue3::kEveryGame};
ue3::FieldRef gTimeSeconds{"Engine.WorldInfo", "TimeSeconds", ue3::kEveryGame};
ue3::ClassRef gInterpActionClass{"Engine.SeqAct_Interp", ue3::kEveryGame};
ue3::FieldRef gInterpPosition{"Engine.SeqAct_Interp", "Position", ue3::kEveryGame};
ue3::ClassRef gDelayClass{"Engine.SeqAct_Delay", ue3::kEveryGame};
ue3::FieldRef gDelayDuration{"Engine.SeqAct_Delay", "Duration", ue3::kEveryGame};
ue3::FieldRef gDelayRemaining{"Engine.SeqAct_Delay", "RemainingTime", ue3::kEveryGame};

constexpr float kNearProjectorUU = 900.0f;  // ME1's "near the console": the room's retention is armed within this, on foot

struct Token {
    ue3::Object* object = nullptr;
    int index = -1;
    static Token Of(ue3::Object* o) { return {o, ue3::runtime::Get().IndexOf(o)}; }
    bool Alive() const { return ue3::runtime::IsAlive(object) && ue3::runtime::Get().IndexOf(object) == index; }
};

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

Publish publishScene = nullptr;
std::optional<vr::floor::State> currentFloor;
vr::world_view::Reference room;
gp::HeldView held;
gp::RenderHold render;
gp::MapPov mapPov;
gp::StereoWindow window;
Disc disc;
bool handlerOn = false;
bool off = false;          // `mevr galaxy capture off`
// ME3's captures without the post-process chain: with any chain (the player's, the engine's default, its UI scene's) their
// pictures came out black although the final post effect bound the capture's own target (plugin/Le3SceneCapture), while
// without one the scene and the finish's gamma pass gave the map as the game shows it (run 20261117-*-LE3-gx2,
// research/le3-galaxy.md "The projector flow"). `mevr galaxy capture on-post` (the player's chain), on-default, on-ui: the
// diagnostics.
bool postProcess = false;
int chainChoice = 0;       // 0 the player's chain, 1 Engine.DefaultPostProcess, 2 DefaultUIScenePostProcess (diagnostics)
bool holding = false;
// The map's interlude (galaxy_projector::MapInterlude: the travel's relay movie, a call, a question, a load): the table stays
// reserved (the room kept, the captures paused, no hold, no window) until the map's own modes come back, which re-open it from
// the saved on-foot measurement at once (ME1's rule, research/51 section 8.4).
bool suspended = false;
std::uint64_t suspendedMs = 0;
bool published = false;
std::vector<Token> hidden;  // the room's own projector hologram, hidden while the window lies on it
std::uint64_t lastProbeMs = 0;
std::string armRefused;
std::string reason = "idle";
unsigned frames = 0, openings = 0, interludes = 0, recentersKept = 0;
int lastMode = -2;         // the mode log's last game mode (-1: unreadable)
// The map's opening and closing cinematics (0 -> 8 -> 11 and 11 -> 8 -> 0, run gx3a) are the game's camera flying between the
// CIC and the map's scene: Shepard's view stays held through them, as ME1's opening and closing (vr::galaxy_map::ClosingHold);
// no window, the renderer's cinematic presentation stands down (MevrSeams::GalaxyOwnsPresentation in its decision).
enum class Bridge { None, Opening, Closing };
Bridge bridge = Bridge::None;
std::uint64_t bridgeFromMs = 0, bridgeUntilMs = 0;
bool reservedOnFoot = false;  // the last tick published the on-foot reservation (at the armed projector)
std::uint64_t heldEndMs = 0;  // when the view was last released: X closes 11 -> 0 -> 8 (0.66 s) -> 0 (run gx3b)
constexpr std::uint64_t kOpeningMs = 6000, kClosingMs = 3000;
std::uint32_t lastRecenters = 0;

bool Vector(json::Value const& v, float out[3]) {
    if (!v.IsArray() || v.Items().size() != 3) return false;
    for (int i = 0; i < 3; ++i) {
        out[i] = static_cast<float>(v.Items()[i].AsNumber(NAN));
        if (!std::isfinite(out[i])) return false;
    }
    return true;
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
    if (!hidden.empty()) log::Info("LE3 galaxy projector: the room's own projector hologram shown again ({} of {} actors)", shown, hidden.size());
    hidden.clear();
}

// `mevr galaxy capture flares`: the lens flares in the player's world nearest the projector, where they are and when they were
// last drawn. The blue band over the pit with the map open was the projector's own flare (BioA_Nor_210CIC's LensFlareSource_31,
// 5 cm from the disc's centre), which the hologram's mesh hides on foot; it is hidden with the hologram now (HideProjector).
std::string FlareList(ue3::Object* pc) {
    struct Row { double distance; json::Value value; };
    std::vector<Row> rows;
    auto& r = ue3::runtime::Get();
    ue3::Object* const world = Object(pc, "Engine.Actor", "WorldInfo");
    float const now = world != nullptr ? gTimeSeconds.Read<float>(world).value_or(0.0f) : 0.0f;
    for (int i = 0, n = r.ObjectCount(); world != nullptr && gLensFlareClass.Get() && i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gLensFlareClass) || r.NameText(o).starts_with("Default__") ||
            Object(o, "Engine.Actor", "WorldInfo") != world) continue;
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

// The GalaxyMapProxy mesh and the emitters over it (the decorative systems), in the player's world.
void HideProjector(ue3::Object* pc) {
    ue3::Function* const f = gSetHidden.Get();
    // A return from the map's interlude: still hidden, unless the room's art was rebuilt meanwhile (new actors, ours gone).
    std::erase_if(hidden, [](Token const& t) { return !t.Alive(); });
    if (f == nullptr || !hidden.empty() || !disc.valid) return;
    auto& r = ue3::runtime::Get();
    ue3::Object* const world = Object(pc, "Engine.Actor", "WorldInfo");
    for (int i = 0, n = r.ObjectCount(); world != nullptr && i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || (!ue3::IsA(o, gInterpClass) && !ue3::IsA(o, gEmitterClass) && !ue3::IsA(o, gLensFlareClass)) ||
            r.NameText(o).starts_with("Default__") || Object(o, "Engine.Actor", "WorldInfo") != world) continue;
        bool wanted = false;
        if (ue3::IsA(o, gInterpClass)) {
            auto const tag = gTag.Read<ue3::Name>(o);
            wanted = tag && r.ToString(*tag) == "GalaxyMapProxy";
        } else if (auto const at = player::Location(o)) {  // its emitters, and its lens flare (BioA_Nor_210CIC's LensFlareSource_31)
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
    log::Info("LE3 galaxy projector: the room's own projector hologram hidden while the map's window lies on it ({} of {} actors)",
        done, hidden.size());
}

void Stop(char const* why) {
    Unpublish();
    window.Pause();
    ShowProjector();
    if (holding || suspended) log::Info("LE3 galaxy projector: the view is the game's again ({})", why);
    if (holding) heldEndMs = GetTickCount64();
    holding = suspended = false;
    reason = why;
}

// A new player or world: every reference of the old one goes (its actors went with it).
void ObserveRoom(ue3::Object* pc) {
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(pc).value_or(nullptr);
    if (!room.Observe({reinterpret_cast<std::uintptr_t>(world), reinterpret_cast<std::uintptr_t>(pc)})) return;
    Unpublish();
    window.Reset("the player/world changed");
    if (holding || suspended) log::Info("LE3 galaxy projector: the player or the world changed with the map {}: everything of the old room forgotten",
        holding ? "open" : "in an interlude");
    hidden.clear();
    suspended = false;
    bridge = Bridge::None;
    held = {};
    render = {};
    mapPov = {};
    disc = {};
    holding = false;
    lastProbeMs = 0;
}

void MeasureDisc(ue3::Object* pc) {
    auto const probe = ObserveProjector(pc);
    if (!probe.Bool("trackingAvailable") || !currentFloor || !currentFloor->valid) return;
    Disc d;
    float centreUU[3]{};
    if (!Vector(probe.Get("trackingCentreM"), d.centre) || !Vector(probe.Get("trackingNormal"), d.normal) ||
        !Vector(probe.Get("centreUU"), centreUU)) return;
    d.radiusM = static_cast<float>(probe.Number("trackingRadiusM"));
    d.radiusUU = static_cast<float>(probe.Number("radiusUU"));
    if (!(d.radiusM > 0.1f && d.radiusM < 10.0f) || !(d.radiusUU > 10.0f)) return;
    d.centreUU = {centreUU[0], centreUU[1], centreUU[2]};
    d.sessions = currentFloor->sessions;
    d.recenters = currentFloor->recenters;
    d.valid = true;
    float const moved = disc.valid ? std::hypot(d.centre[0] - disc.centre[0], d.centre[1] - disc.centre[1], d.centre[2] - disc.centre[2]) : 1e9f;
    if (moved > 0.05f) {
        log::Info("LE3 galaxy projector: the CIC's projector measured on foot: a disc at ({:.2f}, {:.2f}, {:.2f}) m, its normal "
                  "({:.2f}, {:.2f}, {:.2f}) ({:.0f} degrees off vertical), {:.2f} m in radius",
            d.centre[0], d.centre[1], d.centre[2], d.normal[0], d.normal[1], d.normal[2],
            std::acos(std::clamp(std::fabs(d.normal[1]), 0.0f, 1.0f)) * 57.29578f, d.radiusM);
    }
    disc = d;
}

// ---- What the map's opening waits for (the owner's question, 2026-11-17; read only) ----
// From the 0 -> 8 at the console to the map's mode: when the map's level (BioA_Nor_203aGalaxyMap) is loaded and visible, and
// which of the Normandy's matinees (SeqAct_Interp: Position moves) and delays (SeqAct_Delay: RemainingTime moves) ran meanwhile.
struct OpeningProbe {
    bool on = false;
    std::uint64_t fromMs = 0;
    double loadedS = -1.0, visibleS = -1.0;
    struct Op { Token token; bool delay = false; float start = 0.0f, last = 0.0f, duration = 0.0f; };
    std::vector<Op> ops;
} probe;

void ProbeBegin(ue3::Object* pc, std::uint64_t now) {
    probe = {};
    probe.on = true;
    probe.fromMs = now;
    auto& r = ue3::runtime::Get();
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
    (void)pc;
}

void ProbeTick(ue3::Object* pc, std::uint64_t now) {
    if (!probe.on) return;
    double const t = static_cast<double>(now - probe.fromMs) / 1000.0;
    if (probe.loadedS < 0.0 && le3_galaxy_retention::MapLevelLoaded(pc)) probe.loadedS = t;
    if (probe.visibleS < 0.0 && le3_galaxy_retention::MapLevelVisible(pc)) probe.visibleS = t;
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
    log::Info("LE3 galaxy projector: the map's opening: game mode 8 for {:.2f} s (then {}); the map's level loaded at {}, visible at {}; "
              "ran meanwhile ({} watched): {}",
        static_cast<double>(now - probe.fromMs) / 1000.0, modeNow,
        probe.loadedS < 0.0 ? std::string("never") : std::format("{:.2f} s", probe.loadedS),
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
    render = gp::RenderHold{true, pov->location, pov->rotation, pov->fov, now};
    auto const mode = player::GameMode(pc);
    if (!holding && room.Remember(mode.value_or(255), player::FloorLockSample().cameraOwned, false)) {
        gp::Remember(held, *pov->location, *pov->rotation, *pov->fov);
    }
    if (mode && engine::adapter::IsMapMode(*mode)) {
        mapPov = gp::MapPov{true, *pov->location, *pov->rotation, *pov->fov, now};
    }
}
}

void TryStartup() {
    ue3::Function* const update = ue3::runtime::Up() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("LE3 galaxy projector: Engine.Camera.UpdateCamera not found: the map stays on the native panel");
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
    handlerOn = true;
}

void Tick(Publish publish, std::optional<vr::floor::State> const& floor) {
    publishScene = publish;
    currentFloor = floor;
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr) { Stop("no player"); return; }
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
    if (enabled && onFoot && !holding && now - lastProbeMs >= 2000 && le3_galaxy_retention::CicArtVisible(pc)) {
        lastProbeMs = now;
        MeasureDisc(pc);
    }
    // The disc is measured in the room's tracking space: a new XR session drops it (measured again on foot). A recenter on foot
    // waits for the next measurement (2 s); inside the map it keeps the disc, since the held view and the app space move together
    // (the window and the room shift as one: plugin/HalcyonLe3Bridge's Recentered, docs/galaxy-projector-handoff.md "Recenter").
    bool const inside = holding || suspended;
    bool const epoch = floor && floor->valid && disc.valid && floor->sessions == disc.sessions &&
        (inside || floor->recenters == disc.recenters);
    if (floor && floor->valid && floor->recenters != lastRecenters) {
        if (inside && disc.valid && floor->sessions == disc.sessions) {
            ++recentersKept;
            log::Info("LE3 galaxy projector: a recenter with the map {}: the disc kept in the room's space (the held view moves with it)",
                holding ? "open" : "in an interlude");
        }
        lastRecenters = floor->recenters;
    }
    int const modeNumber = mode ? static_cast<int>(*mode) : -1;
    ProbeTick(pc, now);
    if (modeNumber != lastMode && lastMode == 0 && modeNumber == 8 && le3_galaxy_retention::Armed() && NearProjector(pc)) ProbeBegin(pc, now);
    if (modeNumber != lastMode && probe.on && modeNumber != 8) ProbeEnd(pc, now, modeNumber);
    if (probe.on && now - probe.fromMs > 10000) ProbeEnd(pc, now, modeNumber);
    if (modeNumber != lastMode) {
        if (inside || le3_galaxy_retention::Armed()) {
            log::Info("LE3 galaxy projector: game mode {} -> {} ({}; the map's level {})", lastMode, modeNumber,
                holding ? "the map held" : suspended ? "the table reserved" : "on foot, armed",
                le3_galaxy_retention::MapLevelRequested(pc) ? "requested" : "not requested");
        }
        lastMode = modeNumber;
    }
    // Near the projector the room's art is kept through the map's opening (the native streaming unloads it).
    if (enabled && onFoot && epoch && NearProjector(pc) && !le3_galaxy_retention::Armed()) {
        std::string const refused = le3_galaxy_retention::Arm(pc, true);
        if (refused != armRefused) {
            log::Info("LE3 galaxy projector: the room's retention {}", refused.empty() ? "armed near the projector" : "not armed: " + refused);
            armRefused = refused;
        }
    }
    bool const haveRoom = epoch && held.haveView && held.footFrame;
    // The opening and closing cinematics, held at Shepard's (ME1's rule).
    bool const cinematic = mode && engine::adapter::GameMode(*mode) == engine::adapter::Mode::Cinematic;
    if (bridge != Bridge::None && (!cinematic || now >= bridgeUntilMs || !enabled || !haveRoom)) {
        log::Info("LE3 galaxy projector: the map's {} cinematic held at Shepard's for {:.1f} s ({})", bridge == Bridge::Opening ? "opening" : "closing",
            static_cast<double>(now - bridgeFromMs) / 1000.0, !cinematic ? "it ended" : now >= bridgeUntilMs ? "past its bound: the view is the game's" : "the room went");
        bridge = Bridge::None;
    }
    if (enabled && haveRoom && cinematic && bridge == Bridge::None) {
        if (holding) {
            bridge = Bridge::Closing;
            holding = false;
            window.Pause();
            ShowProjector();
        } else if (!suspended && reservedOnFoot && le3_galaxy_retention::Armed() && le3_galaxy_retention::MapLevelRequested(pc)) {
            bridge = now - heldEndMs < 2000 ? Bridge::Closing : Bridge::Opening;  // the closing's 8 comes after a brief 0
        }
        if (bridge != Bridge::None) {
            bridgeFromMs = now;
            bridgeUntilMs = now + (bridge == Bridge::Opening ? kOpeningMs : kClosingMs);
            log::Info("LE3 galaxy projector: the map's {} cinematic: the view held at Shepard's through it", bridge == Bridge::Opening ? "opening" : "closing");
        }
    }
    reservedOnFoot = false;
    if (bridge != Bridge::None) {
        gh::Scene scene;
        scene.sequence = ++frames;
        FillTable(scene, settings);
        gp::PutRenderHold(held, render, true, scene);
        publish(&scene);
        published = true;
        reason = bridge == Bridge::Opening ? "the map's opening cinematic held" : "the map's closing cinematic held";
        return;
    }
    bool const want = enabled && map && haveRoom && le3_galaxy_retention::Active();
    // The map's interlude (the relay's movie, a call, a question, a load): the table reserved, the captures paused, no hold.
    if (enabled && !map && inside && haveRoom && gp::MapInterlude(mode) && le3_galaxy_retention::Kept()) {
        if (!suspended) {
            suspended = true;
            suspendedMs = now;
            ++interludes;
            log::Info("LE3 galaxy projector: the map's interlude (game mode {}): the table reserved, the captures paused, the view the "
                      "game's until the map comes back", modeNumber);
        }
        holding = false;
        window.Pause();
        gh::Scene scene;
        scene.sequence = ++frames;
        FillTable(scene, settings);
        scene.suspended = 1;
        gp::PutRenderHold(held, render, true, scene);  // the host holds it only in the map's modes
        publish(&scene);
        published = true;
        reason = "the map's interlude: the table reserved";
        return;
    }
    if (!want) {
        if (inside) {
            Stop(!enabled ? "off" : !map ? "the map closed" : !epoch ? "the room's tracking changed (a new session)"
                : !le3_galaxy_retention::Active() ? "the room's retention ended" : "no view on foot");
        }
        window.Pause();
        // On foot at the armed projector: the table reserved before the map opens, so its very first frame is held in the CIC
        // and never falls to the renderer's flat galaxy panel (ME1's reservation, research/51 section 8.4).
        if (enabled && onFoot && haveRoom && le3_galaxy_retention::Armed() && NearProjector(pc)) {
            gh::Scene scene;
            scene.sequence = ++frames;
            FillTable(scene, settings);
            scene.suspended = 1;
            gp::PutRenderHold(held, render, true, scene);
            publish(&scene);
            published = true;
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
            log::Info("LE3 galaxy projector: the map is back after its interlude ({:.1f} s): the view held at Shepard's again, the "
                      "window on the disc measured on foot", static_cast<double>(now - suspendedMs) / 1000.0);
        } else {
            ++openings;
            log::Info("LE3 galaxy projector: the map is open in the CIC: the view held at Shepard's ({:.0f}, {:.0f}, {:.0f}; floor "
                      "lock {}, raw Z {:.1f}), the map's window on the projector", held.location.x, held.location.y, held.location.z,
                v.floorLocked, held.rawZ);
        }
        suspended = false;
        HideProjector(pc);
    }
    gh::Scene scene;
    scene.sequence = ++frames;
    FillTable(scene, settings);
    gp::WindowLook look;
    look.widthM = scene.windowWidthM;
    look.depth = settings.windowDepth;
    look.round = settings.windowRound;
    look.backdrop = settings.windowBackdrop;
    look.postProcess = postProcess;
    look.postProcessChain = chainChoice != 0 ? capture_rig::EngineChain(chainChoice == 2) : nullptr;
    // Presentation does not depend on the captures' readiness: the window's layout and UI panel stand while they warm up.
    bool const fresh = mapPov.valid && now - mapPov.ms < 500 && mapPov.fovDeg > 1.0f && mapPov.fovDeg < 179.0f;
    HWND const gameWindow = gp::GameWindow();
    RECT client{};
    if (fresh && gameWindow != nullptr && GetClientRect(gameWindow, &client) && client.right >= 64 && client.bottom >= 64) {
        auto const context = Capture(pc);
        std::optional<ue3::Vector> const centre = context ? player::Location(Object(context->camera, kCamera, "m_pCenterObject")) : std::nullopt;
        window.Step(mapPov, centre, client.right, client.bottom, look, scene);
    } else {
        window.Pause();
    }
    gp::PutRenderHold(held, render, true, scene);
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
    holding = suspended = published = false;
    bridge = Bridge::None;
    hidden.clear();
}

std::string StatusJson() {
    auto out = json::Value::Object();
    out["holding"] = holding;
    out["published"] = published;
    out["reason"] = reason;
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
    out["retentionArmed"] = le3_galaxy_retention::Armed();
    out["retentionActive"] = le3_galaxy_retention::Active();
    out["retentionKept"] = le3_galaxy_retention::Kept();
    out["suspended"] = suspended;
    out["bridge"] = bridge == Bridge::Opening ? "opening" : bridge == Bridge::Closing ? "closing" : "none";
    out["interludes"] = static_cast<int>(interludes);
    out["recentersKept"] = static_cast<int>(recentersKept);
    out["off"] = off;
    return json::Write(out, 0);
}

// `mevr galaxy capture ship`: where the map's ship, its camera and its relay stand (the map's own world units) and the map
// camera's yaw, so a test script can steer with the stick (tools/le2-halcyon-run/galaxy-steps.ps1). Read only.
std::string ShipJson(ue3::Object* pc) {
    auto out = json::Value::Object();
    auto const context = Capture(pc);
    out["available"] = context.has_value();
    if (!context) return json::Write(out, 0);
    auto& r = ue3::runtime::Get();
    auto const put = [&](char const* key, ue3::Object* o) {
        auto row = json::Value::Object();
        row["name"] = o != nullptr ? r.PathName(o) : std::string("none");
        if (auto const at = o != nullptr ? player::Location(o) : std::nullopt) {
            row["x"] = at->x;
            row["y"] = at->y;
            row["z"] = at->z;
        }
        out[key] = std::move(row);
    };
    out["state"] = context->cameraState;
    out["mode"] = context->modeNumber;
    out["activeWorld"] = context->activeWorld;
    put("centre", Object(context->camera, kCamera, "m_pCenterObject"));
    put("cameraObject", Object(context->camera, kCamera, "m_pCameraObject"));
    put("selected", Object(context->camera, kCamera, "m_pSelectedObject"));
    put("crosshair", Object(context->camera, kCamera, "m_pCrossHairObject"));
    put("mapCurrent", Object(context->camera, kCamera, "m_pMapCurrentObject"));
    put("relay", Object(context->camera, kCamera, "m_pMassRelayObject"));
    out["system"] = context->system != nullptr ? r.PathName(context->system) : std::string("none");
    out["planet"] = context->planet != nullptr ? r.PathName(context->planet) : std::string("none");
    if (mapPov.valid) {
        out["cameraYaw"] = mapPov.rotation.yaw;
        out["cameraPitch"] = mapPov.rotation.pitch;
        out["cameraAt"] = std::format("{:.0f} {:.0f} {:.0f}", mapPov.location.x, mapPov.location.y, mapPov.location.z);
    }
    auto selectable = json::Value::Array();
    ue3::FieldRef actors{kCamera, "m_aSelectableActors", ue3::kEveryGame};
    if (auto const list = actors.ElementsOf(context->camera, 256); list && list->stride == sizeof(ue3::Object*)) {
        for (int i = 0; i < list->count && i < 48; ++i) {
            ue3::Object* const a = *static_cast<ue3::Object**>(list->At(i));
            if (!r.IsAlive(a)) continue;
            auto row = json::Value::Object();
            row["name"] = r.NameText(a);
            if (auto const tag = gTag.Read<ue3::Name>(a)) row["tag"] = r.ToString(*tag);
            if (auto const at = player::Location(a)) {
                row["x"] = at->x;
                row["y"] = at->y;
                row["z"] = at->z;
            }
            selectable.Push(std::move(row));
        }
    }
    out["selectable"] = std::move(selectable);
    return json::Write(out, 0);
}

std::string Command(ue3::Object* pc, std::string const& action) {
    if (action == "ship") return "galaxy ship: " + ShipJson(pc);
    if (action == "flares") return "galaxy flares: " + FlareList(pc);
    if (action == "off") {
        off = true;
        Stop("command-off");
    } else if (action == "on" || action == "on-raw" || action == "on-post" || action == "on-default" || action == "on-ui") {
        off = false;
        postProcess = action == "on-post" || action == "on-default" || action == "on-ui";  // diagnostics of the post path
        chainChoice = action == "on-default" ? 1 : action == "on-ui" ? 2 : 0;
        window.Reset("the capture's options changed");
    } else if (action != "state" && !action.empty()) {
        return "usage: mevr galaxy capture on|on-post|on-default|on-ui|off|state|ship|flares";
    }
    return "galaxy capture: " + StatusJson();
}
}
#else
namespace mevr::game::le3_galaxy_capture {
void TryStartup() {}
void Tick(Publish, std::optional<vr::floor::State> const&) {}
void Shutdown(bool) {}
std::string Command(ue3::Object*, std::string const&) { return "galaxy capture: LE3 only"; }
std::string StatusJson() { return "{}"; }
}
#endif
