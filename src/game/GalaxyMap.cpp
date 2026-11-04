// SPDX-License-Identifier: MIT
// The galaxy map as a hologram we draw: the game's side (game/GalaxyMap.hpp).
#include "game/GalaxyMap.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/CineScreen.hpp"
#include "game/SceneCaptureRig.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ViewAnchor.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace mevr::game::galaxy_map {

namespace {

namespace gh = vr::galaxy_holo;
namespace mem = ue3::memory;

constexpr std::uint8_t kGalaxyMode = 9;  // SFXGameModeManager's GameMode_Galaxy

ue3::ClassRef gCameraGalaxy{"SFXGame.BioCameraBehaviorGalaxy"};
ue3::FieldRef gDynamicObjects{"SFXGame.BioCameraBehaviorGalaxy", "m_pDynamicObjects"};
ue3::FieldRef gSelectableKind{"SFXGame.BioCameraBehaviorGalaxy", "m_sSelectableObject"};
ue3::FieldRef gState{"SFXGame.BioCameraBehaviorGalaxy", "m_nCurrentState"};
ue3::FieldRef gCenterObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pCenterObject"};
ue3::FieldRef gCrossHair{"SFXGame.BioCameraBehaviorGalaxy", "m_pCrossHairObject"};
ue3::FieldRef gSelectedObject{"SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedObject"};
ue3::FieldRef gSelectedCluster{"SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedCluster"};
ue3::FieldRef gSelectedSystem{"SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedSystem"};
ue3::FieldRef gClusterTable{"SFXGame.BioCameraBehaviorGalaxy", "m_pClusterTable"};
ue3::FieldRef gRelayTable{"SFXGame.BioCameraBehaviorGalaxy", "m_pRelayTable"};
ue3::FieldRef gTag{"Engine.Actor", "Tag"};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden"};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gGame{"Engine.WorldInfo", "Game"};
ue3::FunctionRef gGetSimpleString{"SFXGame.BioSPGame.GetSimpleString"};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation"};
ue3::FunctionRef gNumRows{"Engine.Bio2DA.GetNumRows"};
ue3::FunctionRef gNumColumns{"Engine.Bio2DA.GetNumColumns"};
ue3::FunctionRef gColumnName{"Engine.Bio2DA.GetColumnName"};
ue3::FunctionRef gRowNumber{"Engine.Bio2DA.GetRowNumber"};
ue3::FunctionRef gIntEntry{"Engine.Bio2DA.GetIntEntryII"};

ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera"};
ue3::FunctionRef gSetMovieVisibility{"Engine.BioSFPanel.SetMovieVisibility"};
ue3::ClassRef gGalaxyHandler{"SFXGame.BioSFHandler_GalaxyMap"};
ue3::FieldRef gHandlerPanel{"Engine.BioSFHandler", "oPanel"};
// In the CIC: the console (the galaxy map's Kismet event's originator), its use, the tables on foot, the travel question.
ue3::ClassRef gArtUsedEvent{"SFXGame.BioSeqEvt_ArtPlaceableUsed"};
ue3::ClassRef gArtBehavior{"SFXGame.BioArtPlaceableBehavior"};
ue3::FieldRef gOriginator{"Engine.SequenceEvent", "Originator"};
ue3::FunctionRef gArtUsed{"SFXGame.BioArtPlaceableBehavior.OnArtPlaceableUsed"};
ue3::FunctionRef gFloatEntry{"Engine.Bio2DA.GetFloatEntryII"};
ue3::FunctionRef gCheckConditional{"SFXGame.BioWorldInfo.CheckConditional"};
ue3::FunctionRef gInvokeMethod{"Engine.BioSFPanel.InvokeMethod"};
ue3::FieldRef gWaitingForMsgBox{"SFXGame.BioSFHandler_GalaxyMap", "m_bWaitingForMsgBox"};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden"};
ue3::ClassRef gEmitterClass{"Engine.Emitter"};
ue3::ClassRef gInterpActorClass{"Engine.InterpActor"};
constexpr float kProjectorUnits = 100.0f;
// The projector's hologram's disc (research/51 section 8.1): the mesh (bioa_galaxymap_s.GalaxyMapProxy) is a square in its local
// XY plane with the galaxy painted as a disc, 301 units from its centre to its sides (measured from its bounds, 2026-11-04); the
// matinee spins it in that plane, so its up axis stays put.
ue3::FieldRef gMeshComponent{"Engine.DynamicSMActor", "StaticMeshComponent"};
ue3::FieldRef gBounds{"Engine.PrimitiveComponent", "Bounds"};
ue3::FieldRef gBoundsExtent{gBounds, "BoxExtent"};
constexpr float kHologramHalfUnits = 301.0f;
// The map's opening wait: the GalaxyMap sequence's longest BioSeqAct_Delay (2.5 s while the camera flies into the map, which the
// CIC's held view never shows), shortened while the map is used in the CIC; the map is up 0.2 s after the console's use.
ue3::ClassRef gBioDelayClass{"SFXGame.BioSeqAct_Delay"};
ue3::FieldRef gDelayDuration{"Engine.SeqAct_Delay", "Duration"};
constexpr float kOpeningDelayS = 0.05f;
constexpr float kOpeningAsShippedS = 2.5f;  // measured (2026-11-03); what a node already shortened (a reload) is put back to  // the game's own galaxy (a mesh and an emitter) sits within this of the console

// The map's clusters on foot come from GalaxyMap_Cluster: X and Y (0..1) are where the map's level puts each cluster's actor,
// 630 units a step and centred on (0.5, 0.5) (research/51 section 8), so on foot and open the hologram is the same picture.
constexpr float kClusterUnits = 630.0f;
constexpr float kNearConsoleUnits = 900.0f;  // the hologram shows on foot within this of the console (the CIC)

struct Console {
    ue3::Object* usable = nullptr;
    ue3::Object* behavior = nullptr;
    std::uint64_t lastSearchMs = 0;
    std::vector<ue3::Object*> projector;  // the game's own hologram's actors (BIOA_NOR10_03_PLC's InterpActor_0 and Emitter_71)
    ue3::Object* hologram = nullptr;       // of those, the mesh (the InterpActor): the disc the window lies on
    bool projectorHidden = false;
    ue3::Object* openingDelay = nullptr;   // the sequence's opening wait (BioSeqAct_Delay_0)
    float openingOriginalS = 0.0f;
    bool openingShortened = false;
    std::uint64_t usedMs = 0;              // the console was used (OnArtPlaceableUsed: A pressed at it, or our pick)
};
Console gConsole;

struct ClusterRow {
    int row = 0;
    float x = 0.5f;
    float y = 0.5f;
    std::string ref;
    int visibleConditional = 0;
    int visibleFunction = 0;
    int visibleParameter = 0;
};
std::vector<ClusterRow> gRows;
bool gRowsRead = false;
// Which clusters the open map last showed (by text reference), kept in %LOCALAPPDATA%\MEVR\galaxy\visible.txt.
std::map<std::string, bool> gHiddenCache;
bool gHiddenLoaded = false;

// A pick made on foot: the map opened through the console; the pick goes on once its level is up.
struct PendingOpen {
    bool pending = false;
    std::uint32_t id = 0;
    std::uint64_t startedMs = 0;
};
PendingOpen gOpen;
std::uint64_t gAwaySinceMs = 0;
std::uint64_t gLastExitMs = 0;

// In-world ([Galaxy] inWorld): the last on-foot view, held while the game is in its map mode so the player stays in the CIC.
struct Hold {
    bool active = false;     // the map is open in-world: the camera handler holds the view
    bool haveView = false;   // an on-foot view was seen
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    std::uint64_t cameraTicks = 0;
    std::uint64_t lastHideMs = 0;
    int hides = 0;
    std::uint64_t closingUntilMs = 0;  // the map's closing cutscene, held too (the view stays the CIC's)
    std::uint8_t lastMode = 255;
};
Hold gHold;

// The game's own map as a stereo window over the table (research/51 section 8): the game's map camera as it was this tick (before
// the held view replaced it) and two captures from it, side by side.
struct MapPov {
    bool valid = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fovDeg = 0.0f;
    std::uint64_t ms = 0;
};
MapPov gMapPov;

struct Window {
    capture_rig::Rig eyes[2];
    bool on = false;          // enabled and pointed this tick
    bool flashShown = false;  // the map's Flash movie shown for it (the window carries the game's labels)
    std::uint64_t failedMs = 0;
    float baselineUU = 0.0f;
    float crop = 0.0f;
    float convergeUU = 0.0f;
    unsigned pointed = 0;
};
Window gWindow;

// `mevr galaxy view`: the map camera orbiting the centre object (research).
struct ViewOverrideState {
    bool on = false;
    float yawDeg = 0.0f;
    float pitchDeg = -45.0f;
    float distance = 8000.0f;
    std::uint64_t applied = 0;
};
ViewOverrideState gView;
bool gHoldWrites = true;  // `mevr galaxy hold 0`: no held view (research)

// The held view at render time only: the player camera's POV fields (the host writes the held view there for the main view's
// CalcSceneView and puts the map camera back; research/51 section 8).
struct RenderHold {
    bool on = false;
    ue3::Vector* location = nullptr;
    ue3::Rotator* rotation = nullptr;
    float* fov = nullptr;
};
RenderHold gRenderHold;

// The galaxy map's live Flash handler, found again only when it goes (a scan of every object; not every tick).
struct HandlerCache {
    ue3::Object* handler = nullptr;
    std::uint64_t scannedMs = 0;
};
HandlerCache gHandler;

// A pick being carried out: the cross-hair is on the object; once the game selects it, the click.
struct PendingPick {
    ue3::Object* target = nullptr;
    std::string label;
    std::uint64_t startedMs = 0;
    int stage = 0;  // 0 waiting for the selection, 1 cursor moved (click next tick), 2 done
    int ticks = 0;
};

struct State {
    bool published = false;
    ue3::Object* behavior = nullptr;
    bool loggedMissing = false;
    int lastState = -1;
    int lastCount = -1;
    // Per opening: the relay routes (by actor name) read from the tables.
    bool routesRead = false;
    ue3::Object* routesBehavior = nullptr;  // the map camera the routes (and the cluster rows) were read for
    std::vector<std::pair<std::string, std::string>> routes;
    std::optional<PendingPick> pick;
    std::uint64_t ticks = 0;
    int picks = 0;
    int clicks = 0;
    std::string lastPick;
    gh::Scene scene;  // the last sent (status)
};
State gS;
std::map<std::string, std::string> gLabels;  // text reference -> text, for the session

bool Resolved() {
    return gCameraGalaxy.Get() != nullptr && gDynamicObjects.Get() != nullptr && gSelectableKind.Get() != nullptr && gState.Get() != nullptr &&
           gCrossHair.Get() != nullptr && gSelectedObject.Get() != nullptr && gTag.Get() != nullptr && gSetLocation.Get() != nullptr;
}

// The newest live instance (an idle one from an earlier opening may linger, research/51 section 8).
ue3::Object* FindBehavior() {
    if (gS.behavior != nullptr && ue3::runtime::IsAlive(gS.behavior)) {
        return gS.behavior;
    }
    gS.behavior = nullptr;
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gCameraGalaxy) && r.IsAlive(o) && !r.NameText(o).starts_with("Default__")) {
            gS.behavior = o;
        }
    }
    return gS.behavior;
}

ue3::Object* ObjectField(ue3::FieldRef& field, ue3::Object* o) {
    ue3::Object* const v = o != nullptr ? field.Read<ue3::Object*>(o).value_or(nullptr) : nullptr;
    return v != nullptr && ue3::runtime::IsAlive(v) ? v : nullptr;
}

std::string TakeString(ue3::FString string) {
    std::string const text = string.count > 0 && string.count < 65536 ? mem::Narrow(string) : std::string();
    mem::FreeString(string);
    return text;
}

// The text of a text reference, through the game info's GetSimpleString (cached for the session).
std::string Label(std::string const& ref, ue3::Object* game) {
    if (auto const it = gLabels.find(ref); it != gLabels.end()) {
        return it->second;
    }
    std::string text;
    long const id = std::strtol(ref.c_str(), nullptr, 10);
    ue3::Function* const f = gGetSimpleString.Get();
    if (id > 0 && game != nullptr && f != nullptr) {
        ue3::Reflection& r = ue3::runtime::Get();
        ue3::Params p(r, f);
        if (p.Valid() && p.Set("StrRef", static_cast<std::int32_t>(id)) && p.SetBool("bParse", false) && ue3::runtime::Call(game, f, p)) {
            text = TakeString(p.Return<ue3::FString>().value_or(ue3::FString{}));
        }
    }
    gLabels[ref] = text;
    return text;
}

std::optional<std::int32_t> CallInt(ue3::Object* table, ue3::FunctionRef& fn, std::int32_t a = -1, std::int32_t b = -1, char const* aName = nullptr,
                                    char const* bName = nullptr, char const* outName = nullptr) {
    ue3::Function* const f = fn.Get();
    if (f == nullptr || table == nullptr) {
        return std::nullopt;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params p(r, f);
    if (!p.Valid() || (aName != nullptr && !p.Set(aName, a)) || (bName != nullptr && !p.Set(bName, b)) || !ue3::runtime::Call(table, f, p)) {
        return std::nullopt;
    }
    if (outName != nullptr) {
        if (!p.ReturnBool().value_or(false)) {
            return std::nullopt;
        }
        return p.Get<std::int32_t>(outName);
    }
    return p.Return<std::int32_t>();
}

std::optional<std::int32_t> ColumnIndex(ue3::Object* table, char const* column) {
    ue3::Function* const f = gColumnName.Get();
    std::int32_t const columns = CallInt(table, gNumColumns).value_or(0);
    if (f == nullptr) {
        return std::nullopt;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t c = 0; c < std::min(columns, 256); ++c) {
        ue3::Params p(r, f);
        if (p.Valid() && p.Set("nColumn", c) && ue3::runtime::Call(table, f, p) && r.ToString(p.Return<ue3::Name>().value_or(ue3::Name{})) == column) {
            return c;
        }
    }
    return std::nullopt;
}

// The relay routes: GalaxyMap_Relay's StartCluster/EndCluster codes / 10000 are GalaxyMap_Cluster row numbers, whose Name is the
// text reference the cluster's actor is named by.
void ReadRoutes(ue3::Object* behavior) {
    gS.routesRead = true;
    gS.routes.clear();
    ue3::Object* const clusters = ObjectField(gClusterTable, behavior);
    ue3::Object* const relays = ObjectField(gRelayTable, behavior);
    auto const nameColumn = ColumnIndex(clusters, "Name");
    auto const startColumn = ColumnIndex(relays, "StartCluster");
    auto const endColumn = ColumnIndex(relays, "EndCluster");
    if (!nameColumn || !startColumn || !endColumn) {
        log::Info("galaxy hologram: no relay routes (the GalaxyMap tables' columns weren't found)");
        return;
    }
    std::map<std::int32_t, std::string> byRow;
    for (std::int32_t i = 0, n = std::min(CallInt(clusters, gNumRows).value_or(0), 512); i < n; ++i) {
        auto const number = CallInt(clusters, gRowNumber, i, -1, "nRowIndex");
        auto const name = CallInt(clusters, gIntEntry, i, *nameColumn, "nRow", "nColumn", "nEntry");
        if (number && name) {
            byRow[*number] = std::to_string(*name);
        }
    }
    for (std::int32_t i = 0, n = std::min(CallInt(relays, gNumRows).value_or(0), 512); i < n; ++i) {
        auto const a = CallInt(relays, gIntEntry, i, *startColumn, "nRow", "nColumn", "nEntry");
        auto const b = CallInt(relays, gIntEntry, i, *endColumn, "nRow", "nColumn", "nEntry");
        if (a && b && byRow.count(*a / 10000) != 0 && byRow.count(*b / 10000) != 0) {
            gS.routes.emplace_back(byRow[*a / 10000], byRow[*b / 10000]);
        }
    }
    log::Info("galaxy hologram: {} relay routes between {} clusters from the tables", gS.routes.size(), byRow.size());
}

// The game's window: this process's largest visible top-level window.
HWND GameWindow() {
    struct Search {
        DWORD process = 0;
        HWND best = nullptr;
        long area = 0;
    } search{GetCurrentProcessId(), nullptr, 0};
    EnumWindows(
        [](HWND window, LPARAM context) -> BOOL {
            auto* const s = reinterpret_cast<Search*>(context);
            DWORD process = 0;
            GetWindowThreadProcessId(window, &process);
            RECT rc{};
            if (process == s->process && IsWindowVisible(window) && GetClientRect(window, &rc)) {
                long const area = (rc.right - rc.left) * (rc.bottom - rc.top);
                if (area > s->area) {
                    s->area = area;
                    s->best = window;
                }
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&search));
    return search.best;
}

// The map's model, live.
std::optional<gh::MapModel> ReadModel(ue3::Object* behavior, ue3::Object* controller, ue3::Object* game, HWND window) {
    ue3::Reflection& r = ue3::runtime::Get();
    gh::MapModel m;
    m.state = gState.Read<std::int32_t>(behavior).value_or(0);
    ue3::FString const kind = gSelectableKind.Read<ue3::FString>(behavior).value_or(ue3::FString{});
    m.selectable = kind.count > 0 && kind.count < 64 ? mem::Narrow(kind) : std::string();
    auto const elements = gDynamicObjects.ElementsOf(behavior, 512);
    if (!elements) {
        return std::nullopt;
    }
    for (std::int32_t i = 0; i < elements->count; ++i) {
        ue3::Object* const o = *static_cast<ue3::Object**>(elements->At(i));
        if (o == nullptr || !ue3::runtime::IsAlive(o)) {
            continue;
        }
        gh::MapActor a;
        a.tag = r.ToString(gTag.Read<ue3::Name>(o).value_or(ue3::Name{}));
        if (!gh::IsSelectable(a, m.selectable)) {
            continue;
        }
        a.name = r.NameText(o);
        a.hidden = gHidden.ReadBool(o).value_or(false);
        auto const at = player::Location(o);
        if (!at) {
            continue;
        }
        a.location[0] = at->x;
        a.location[1] = at->y;
        a.location[2] = at->z;
        m.actors.push_back(a);
        if (!a.hidden && m.labels.count(a.name) == 0) {
            m.labels[a.name] = Label(a.name, game);
        }
    }
    if (ue3::Object* const centre = ObjectField(gCenterObject, behavior)) {
        if (auto const at = player::Location(centre)) {
            m.hasCentre = true;
            m.centre[0] = at->x;
            m.centre[1] = at->y;
            m.centre[2] = at->z;
        }
    }
    if (ue3::Object* const selected = ObjectField(gSelectedObject, behavior)) {
        m.selected = r.NameText(selected);
    }
    if (auto const pov = player::CameraPov(player::Camera(controller)); pov && pov->location && pov->rotation && pov->fov) {
        constexpr float kUnitToRad = 6.2831853f / 65536.0f;
        m.camera.location[0] = pov->location->x;
        m.camera.location[1] = pov->location->y;
        m.camera.location[2] = pov->location->z;
        m.camera.pitch = static_cast<float>(pov->rotation->pitch) * kUnitToRad;
        m.camera.yaw = static_cast<float>(pov->rotation->yaw) * kUnitToRad;
        m.camera.roll = static_cast<float>(pov->rotation->roll) * kUnitToRad;
        m.camera.fovXDeg = *pov->fov;
        RECT rc{};
        m.camera.aspect = window != nullptr && GetClientRect(window, &rc) && rc.bottom > 0 ? static_cast<float>(rc.right) / static_cast<float>(rc.bottom)
                                                                                          : 1.0f;
        m.camera.valid = 1;
    }
    if (m.state == 1) {
        m.links = gS.routes;
        m.title = "Galaxy";
    } else {
        ue3::FieldRef& owner = m.state == 2 ? gSelectedCluster : gSelectedSystem;
        if (ue3::Object* const o = ObjectField(owner, behavior)) {
            m.title = Label(r.NameText(o), game);
        }
    }
    return m;
}

// The game's own click on its map: a cursor move, then (next tick) the button, posted to the window (the game pointer's way).
void PostCursor(HWND window, bool click) {
    RECT rc{};
    if (window == nullptr || !GetClientRect(window, &rc)) {
        return;
    }
    int const x = (rc.right - rc.left) / 2;
    int const y = (rc.bottom - rc.top) / 2;
    LPARAM const at = MAKELPARAM(x, y);
    if (!click) {
        POINT screen{x, y};
        ClientToScreen(window, &screen);
        SetCursorPos(screen.x, screen.y);
        PostMessageW(window, WM_MOUSEMOVE, 0, at);
    } else {
        PostMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, at);
        PostMessageW(window, WM_LBUTTONUP, 0, at);
    }
}

void StartPick(std::uint32_t id, ue3::Object* behavior, ue3::Object* game) {
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const name = std::to_string(id);
    auto const elements = gDynamicObjects.ElementsOf(behavior, 512);
    ue3::FString const kindText = gSelectableKind.Read<ue3::FString>(behavior).value_or(ue3::FString{});
    std::string const kind = kindText.count > 0 && kindText.count < 64 ? mem::Narrow(kindText) : std::string();
    ue3::Object* target = nullptr;
    for (std::int32_t i = 0; elements && i < elements->count && target == nullptr; ++i) {
        ue3::Object* const o = *static_cast<ue3::Object**>(elements->At(i));
        if (o != nullptr && ue3::runtime::IsAlive(o) && r.NameText(o) == name &&
            gh::IsSelectable({r.ToString(gTag.Read<ue3::Name>(o).value_or(ue3::Name{})), name, {}, false}, kind)) {
            target = o;
        }
    }
    std::string const label = Label(name, game);
    ++gS.picks;
    gS.lastPick = label;
    if (target == nullptr) {
        log::Info("galaxy hologram: pick {} ('{}'): not one of this level's objects any more; ignored", id, label);
        return;
    }
    ue3::Object* const crosshair = ObjectField(gCrossHair, behavior);
    auto const at = player::Location(target);
    if (crosshair == nullptr || !at) {
        log::Info("galaxy hologram: pick '{}': the map has no cross-hair", label);
        return;
    }
    if (ObjectField(gSelectedObject, behavior) != target) {
        ue3::Function* const f = gSetLocation.Get();
        ue3::Params p(r, f);
        bool const moved = p.Valid() && p.Set("NewLocation", *at) && ue3::runtime::Call(crosshair, f, p) && p.ReturnBool().value_or(false);
        log::Info("galaxy hologram: pick '{}': the map's cross-hair moved onto it ({}); waiting for the game to select it", label,
                  moved ? "SetLocation" : "SetLocation refused");
    } else {
        log::Info("galaxy hologram: pick '{}': already selected; activating it", label);
    }
    gS.pick = PendingPick{target, label, GetTickCount64(), 0, 0};
}

void StepPick(ue3::Object* behavior, HWND window) {
    if (!gS.pick) {
        return;
    }
    PendingPick& p = *gS.pick;
    ++p.ticks;
    if (!ue3::runtime::IsAlive(p.target) || GetTickCount64() - p.startedMs > 2000) {
        log::Info("galaxy hologram: pick '{}' abandoned ({} after {} ticks)", p.label,
                  ue3::runtime::IsAlive(p.target) ? "the game didn't select it" : "the object went", p.ticks);
        gS.pick.reset();
        return;
    }
    if (p.stage == 0) {
        if (ObjectField(gSelectedObject, behavior) == p.target) {
            PostCursor(window, false);
            p.stage = 1;
        }
        return;
    }
    if (p.stage == 1) {
        PostCursor(window, true);
        ++gS.clicks;
        log::Info("galaxy hologram: pick '{}': selected by the game after {} ticks, clicked to open it", p.label, p.ticks);
        gS.pick.reset();
    }
}

// Camera.UpdateCamera, after the game's: on foot, remember the view; while the map is open in-world, put that view back (the
// game's camera would show the map's own scene).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) {
        return;
    }
    auto const pov = player::CameraPov(camera);
    if (!pov || pov->location == nullptr || pov->rotation == nullptr || pov->fov == nullptr) {
        return;
    }
    auto const mode = player::GameMode(controller);
    if (gView.on && mode && *mode == kGalaxyMode) {
        // Research: the camera orbits the map's centre object, looking at it.
        ue3::Object* const centre = ObjectField(gCenterObject, FindBehavior());
        if (auto const c = centre != nullptr ? player::Location(centre) : std::nullopt) {
            float const yaw = gView.yawDeg * 0.01745329f;
            float const pitch = gView.pitchDeg * 0.01745329f;
            // UE3: X forward at yaw 0, Y right (yaw +), Z up; the camera looks along (cos p cos y, cos p sin y, sin p).
            ue3::Vector const forward{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
            *pov->location = ue3::Vector{c->x - forward.x * gView.distance, c->y - forward.y * gView.distance, c->z - forward.z * gView.distance};
            write_journal::Wrote("GalaxyMap.viewLocation", pov->location, sizeof(*pov->location));
            *pov->rotation = ue3::Rotator{static_cast<std::int32_t>(gView.pitchDeg * 65536.0f / 360.0f),
                                          static_cast<std::int32_t>(gView.yawDeg * 65536.0f / 360.0f), 0};
            write_journal::Wrote("GalaxyMap.viewRotation", pov->rotation, sizeof(*pov->rotation));
            ++gView.applied;
            return;
        }
    }
    if (!gHold.active) {
        if (mode && *mode == 0) {
            gHold.location = *pov->location;
            gHold.rotation = *pov->rotation;
            gHold.fov = *pov->fov;
            gHold.haveView = true;
        }
        return;
    }
    if (!gHold.haveView) {
        return;
    }
    if (mode && *mode == kGalaxyMode) {
        // The game's map camera (the window's captures look from it), before the held view replaces it.
        gMapPov = MapPov{true, *pov->location, *pov->rotation, *pov->fov, GetTickCount64()};
    }
    // The held view goes in at render time only (the host's CalcSceneView seam): the game keeps its map camera here, so the
    // map's Flash labels and its cursor's picks project through it.
    gRenderHold = RenderHold{true, pov->location, pov->rotation, pov->fov};
    ++gHold.cameraTicks;
}

std::filesystem::path VisibilityFile() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR" / L"galaxy" / L"visible.txt";
}

void LoadVisibility() {
    gHiddenLoaded = true;
    std::ifstream in(VisibilityFile());
    std::string ref;
    int hidden = 0;
    while (in >> ref >> hidden) {
        gHiddenCache[ref] = hidden != 0;
    }
}

void SaveVisibility() {
    std::filesystem::path const path = VisibilityFile();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    for (auto const& [ref, hidden] : gHiddenCache) {
        out << ref << ' ' << (hidden ? 1 : 0) << '\n';
    }
}

// The galaxy map's console: the GalaxyMap sequence's art-placeable event's originator (BIOA_NOR10_03_DSG's BioUseable_0) and its
// behaviour, whose OnArtPlaceableUsed is what pressing A at it does. Looked for every 3 s until found (only on the Normandy).
bool FindConsole() {
    if (gConsole.usable != nullptr && ue3::runtime::IsAlive(gConsole.usable) && gConsole.behavior != nullptr &&
        ue3::runtime::IsAlive(gConsole.behavior)) {
        return true;
    }
    gConsole.usable = nullptr;
    gConsole.behavior = nullptr;
    std::uint64_t const now = GetTickCount64();
    if (now - gConsole.lastSearchMs < 3000 || gArtUsedEvent.Get() == nullptr || gArtBehavior.Get() == nullptr) {
        return false;
    }
    gConsole.lastSearchMs = now;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* sequence = nullptr;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n && gConsole.usable == nullptr; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gArtUsedEvent) && r.IsAlive(o) && r.NameText(r.OuterOf(o)) == "GalaxyMap") {
            gConsole.usable = ObjectField(gOriginator, o);
            sequence = r.OuterOf(o);
        }
    }
    // The opening wait: the sequence's own (not a sub-sequence's) longest delay.
    gConsole.openingDelay = nullptr;
    gConsole.openingShortened = false;
    float longest = 0.0f;
    for (std::int32_t i = 0, n = r.ObjectCount(); sequence != nullptr && gBioDelayClass.Get() != nullptr && i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gBioDelayClass) && r.IsAlive(o) && r.OuterOf(o) == sequence) {
            if (auto const d = gDelayDuration.Read<float>(o); d && *d > longest) {
                longest = *d;
                gConsole.openingDelay = o;
            }
        }
    }
    if (gConsole.openingDelay != nullptr) {
        // 0.1 s or less: shortened by us before a reload; put back as shipped.
        gConsole.openingOriginalS = longest > 0.1f ? longest : kOpeningAsShippedS;
        gConsole.openingShortened = longest <= 0.1f;
        log::Info("galaxy hologram: the map's opening wait is {} ({:.2f} s{})", r.FullName(gConsole.openingDelay), longest,
                  gConsole.openingShortened ? ", already shortened" : "");
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); gConsole.usable != nullptr && i < n && gConsole.behavior == nullptr; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gArtBehavior) && r.IsAlive(o) && r.OuterOf(o) == gConsole.usable) {
            gConsole.behavior = o;
        }
    }
    if (gConsole.usable != nullptr && gConsole.behavior != nullptr) {
        auto const at = player::Location(gConsole.usable);
        gConsole.projector.clear();
        gConsole.projectorHidden = false;
        gConsole.hologram = nullptr;
        for (std::int32_t i = 0, n = r.ObjectCount(); at && i < n; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !r.IsAlive(o) || !(ue3::IsA(o, gEmitterClass) || ue3::IsA(o, gInterpActorClass)) ||
                r.NameText(o).starts_with("Default__")) {
                continue;
            }
            if (auto const p = player::Location(o);
                p && std::sqrt((p->x - at->x) * (p->x - at->x) + (p->y - at->y) * (p->y - at->y) + (p->z - at->z) * (p->z - at->z)) <
                         kProjectorUnits) {
                gConsole.projector.push_back(o);
                if (gConsole.hologram == nullptr && ue3::IsA(o, gInterpActorClass)) {
                    gConsole.hologram = o;
                }
                log::Info("galaxy hologram: the game's own hologram: {} (hidden while ours shows)", r.FullName(o));
            }
        }
        log::Info("galaxy hologram: the galaxy map's console is {} at ({:.0f}, {:.0f}, {:.0f})", r.FullName(gConsole.usable), at ? at->x : 0.0f,
                  at ? at->y : 0.0f, at ? at->z : 0.0f);
        return true;
    }
    gConsole.usable = nullptr;
    return false;
}

std::optional<float> CallFloat(ue3::Object* table, std::int32_t row, std::int32_t column) {
    ue3::Function* const f = gFloatEntry.Get();
    if (f == nullptr || table == nullptr) {
        return std::nullopt;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params p(r, f);
    if (!p.Valid() || !p.Set("nRow", row) || !p.Set("nColumn", column) || !ue3::runtime::Call(table, f, p) || !p.ReturnBool().value_or(false)) {
        return std::nullopt;
    }
    return p.Get<float>("fEntry");
}

// GalaxyMap_Cluster's rows (once per level): place, name, visibility condition.
void ReadRows(ue3::Object* behavior) {
    gRowsRead = true;
    gRows.clear();
    ue3::Object* const table = ObjectField(gClusterTable, behavior);
    auto const x = ColumnIndex(table, "X");
    auto const y = ColumnIndex(table, "Y");
    auto const name = ColumnIndex(table, "Name");
    auto const cond = ColumnIndex(table, "VisibleConditional");
    auto const fn = ColumnIndex(table, "VisibleFunction");
    auto const param = ColumnIndex(table, "VisibleParameter");
    if (!x || !y || !name || !cond || !fn || !param) {
        log::Info("galaxy hologram: GalaxyMap_Cluster's columns weren't found: no galaxy on foot");
        return;
    }
    for (std::int32_t i = 0, n = std::min(CallInt(table, gNumRows).value_or(0), 256); i < n; ++i) {
        ClusterRow row;
        row.row = CallInt(table, gRowNumber, i, -1, "nRowIndex").value_or(i);
        row.x = CallFloat(table, i, *x).value_or(0.5f);
        row.y = CallFloat(table, i, *y).value_or(0.5f);
        auto const ref = CallInt(table, gIntEntry, i, *name, "nRow", "nColumn", "nEntry");
        row.visibleConditional = CallInt(table, gIntEntry, i, *cond, "nRow", "nColumn", "nEntry").value_or(0);
        row.visibleFunction = CallInt(table, gIntEntry, i, *fn, "nRow", "nColumn", "nEntry").value_or(0);
        row.visibleParameter = CallInt(table, gIntEntry, i, *param, "nRow", "nColumn", "nEntry").value_or(0);
        if (ref && *ref > 0) {
            row.ref = std::to_string(*ref);
            gRows.push_back(row);
        }
    }
    log::Info("galaxy hologram: {} clusters from GalaxyMap_Cluster for the galaxy on foot", gRows.size());
}

// A cluster hidden on foot: as the open map last showed it, else its condition (a plot conditional when VisibleConditional is
// set; otherwise hidden until the map has shown it).
bool HiddenOnFoot(ClusterRow const& row, ue3::Object* worldInfo) {
    if (auto const it = gHiddenCache.find(row.ref); it != gHiddenCache.end()) {
        return it->second;
    }
    ue3::Function* const f = gCheckConditional.Get();
    if (row.visibleConditional == 0 || f == nullptr || worldInfo == nullptr) {
        return true;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params p(r, f);
    bool const ok = p.Valid() && p.Set("nConditional", static_cast<std::int32_t>(row.visibleFunction)) &&
                    p.Set("nParam", static_cast<std::int32_t>(row.visibleParameter)) && ue3::runtime::Call(worldInfo, f, p);
    return !(ok && p.ReturnBool().value_or(false));
}

// Where the table stands this tick: over the console, turned so 'away' is the world's -Y (the way the CIC's start faces the
// projector, and the way the game's map camera looks), in tracking space.
struct LastAnchor {
    bool valid = false;
    XrVector3f anchor{};
    float yaw = 0.0f;
    bool disc = false;  // the projector's hologram, where the window lies (tracking space)
    XrVector3f discCentre{};
    XrVector3f discNormal{};
    float discRadiusM = 0.0f;
};
LastAnchor gLastAnchor;
std::uint64_t gDiscLogs = 0;

// The projector's own hologram as a disc in the world: its centre (the actor's place, the pivot it spins about), its normal (the
// mesh's up axis) and its radius (the square's half side, from the component's bounds: each world axis's half extent over what
// the square's two in-plane axes project on it, the smallest of the three, since the mesh's thickness only adds).
struct WorldDisc {
    engine::UnrealVector centre{};
    engine::UnrealVector normal{};
    double radiusUnits = 0.0;
    bool measured = false;
};
std::optional<WorldDisc> HologramDisc() {
    ue3::Object* const actor = gConsole.hologram;
    if (actor == nullptr || !ue3::runtime::IsAlive(actor)) {
        return std::nullopt;
    }
    auto const at = player::Location(actor);
    auto const rotation = player::Rotation(actor);
    if (!at || !rotation) {
        return std::nullopt;
    }
    engine::Basis const axes = engine::ToBasis(engine::UnrealRotation{rotation->pitch, rotation->yaw, rotation->roll});
    WorldDisc disc;
    disc.centre = {at->x, at->y, at->z};
    disc.normal = axes.up;
    disc.radiusUnits = kHologramHalfUnits;
    if (auto const component = gMeshComponent.Read<ue3::Object*>(actor); component && *component != nullptr) {
        if (auto const extent = gBoundsExtent.Read<ue3::Vector>(gBounds.Address(*component))) {
            double const e[3] = {extent->x, extent->y, extent->z};
            double const fx[3] = {axes.forward.x, axes.forward.y, axes.forward.z};
            double const ry[3] = {axes.right.x, axes.right.y, axes.right.z};
            double half = 1e9;
            for (int i = 0; i < 3; ++i) {
                double const across = std::fabs(fx[i]) + std::fabs(ry[i]);
                if (across > 0.3) {
                    half = std::min(half, e[i] / across);
                }
            }
            if (half > 50.0 && half < 2000.0) {
                disc.radiusUnits = half;
                disc.measured = true;
            }
        }
    }
    return disc;
}

bool PlaceInWorld(gh::Scene& scene, vr::galaxy_map::Settings const& settings) {
    auto const frame = view_anchor::Current();
    auto const at = gConsole.usable != nullptr ? player::Location(gConsole.usable) : std::nullopt;
    // While the view is held the table stays where it was when the map opened: the player camera is the map's then (the held
    // view goes in at render time only), so it doesn't say where the CIC is; nor can the player move.
    if (frame && at && !(gHold.active && gLastAnchor.valid)) {
        engine::UnrealVector const centre{at->x, at->y, at->z + settings.worldLiftM * frame->unitsPerMeter};
        XrVector3f const away = engine::DirectionToTracking(*frame, {0.0, -1.0, 0.0});
        gLastAnchor = {true, engine::ToTracking(*frame, centre), std::atan2(-away.x, -away.z)};
        if (auto const disc = HologramDisc()) {
            gLastAnchor.disc = true;
            gLastAnchor.discCentre = engine::ToTracking(*frame, disc->centre);
            gLastAnchor.discNormal = engine::DirectionToTracking(*frame, disc->normal);
            gLastAnchor.discRadiusM = static_cast<float>(disc->radiusUnits / frame->unitsPerMeter);
            if (gDiscLogs++ == 0) {
                log::Info("galaxy hologram: the projector's hologram is a disc at ({:.0f}, {:.0f}, {:.0f}), its normal ({:.2f}, {:.2f}, {:.2f}) "
                          "({:.0f} degrees off vertical), {:.0f} units in radius ({}); the map's window lies on it",
                    disc->centre.x, disc->centre.y, disc->centre.z, disc->normal.x, disc->normal.y, disc->normal.z,
                    std::acos(std::clamp(std::fabs(disc->normal.z), 0.0, 1.0)) * 57.29578, disc->radiusUnits,
                    disc->measured ? "from its bounds" : "as measured on 2026-11-04");
            }
        }
    } else if (!gLastAnchor.valid) {
        return false;
    }
    // Without a view of its own (the map opening, before Halcyon renders the held view as the world again) the last place
    // stands: the view is held where it was, so the table is too.
    scene.inWorld = 1;
    scene.anchorValid = 1;
    scene.anchor[0] = gLastAnchor.anchor.x;
    scene.anchor[1] = gLastAnchor.anchor.y;
    scene.anchor[2] = gLastAnchor.anchor.z;
    scene.anchorYaw = gLastAnchor.yaw;
    scene.tableRadiusM = settings.worldRadiusM;
    scene.tiltDeg = settings.worldTiltDeg;
    scene.forward[0] = 0.0f;
    scene.forward[1] = -1.0f;
    scene.forward[2] = 0.0f;
    if (settings.windowOnHologram && gLastAnchor.disc) {
        scene.windowDisc = 1;
        scene.discCentre[0] = gLastAnchor.discCentre.x;
        scene.discCentre[1] = gLastAnchor.discCentre.y;
        scene.discCentre[2] = gLastAnchor.discCentre.z;
        scene.discNormal[0] = gLastAnchor.discNormal.x;
        scene.discNormal[1] = gLastAnchor.discNormal.y;
        scene.discNormal[2] = gLastAnchor.discNormal.z;
        scene.discRadiusM = gLastAnchor.discRadiusM;
    }
    return true;
}

// The galaxy on foot, from the tables.
std::optional<gh::MapModel> OnFootModel(ue3::Object* behavior, ue3::Object* worldInfo, ue3::Object* game) {
    if (!gRowsRead) {
        ReadRows(behavior);
    }
    if (!gS.routesRead) {
        ReadRoutes(behavior);
    }
    if (!gHiddenLoaded) {
        LoadVisibility();
    }
    if (gRows.empty()) {
        return std::nullopt;
    }
    gh::MapModel m;
    m.state = 1;
    m.selectable = "Cluster";
    m.hasCentre = true;
    for (ClusterRow const& row : gRows) {
        gh::MapActor a;
        a.tag = std::format("Cluster{:02}", row.row % 100);
        a.name = row.ref;
        a.location[0] = (row.x - 0.5f) * kClusterUnits;
        a.location[1] = (row.y - 0.5f) * kClusterUnits;
        a.hidden = HiddenOnFoot(row, worldInfo);
        m.actors.push_back(a);
        if (!a.hidden) {
            m.labels[a.name] = Label(a.name, game);
        }
    }
    m.links = gS.routes;
    m.title = "Galaxy";
    return m;
}

// Pressing A at the console, for the player: the map opens; the view stays held (the camera handler) through its opening.
bool OpenMap(ue3::Object* controller) {
    ue3::Object* const pawn = player::Pawn(controller);
    ue3::Function* const f = gArtUsed.Get();
    if (pawn == nullptr || f == nullptr || gConsole.behavior == nullptr) {
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params p(r, f);
    return p.Valid() && p.Set("User", pawn) && ue3::runtime::Call(gConsole.behavior, f, p);
}

ue3::Object* LiveHandler() {
    if (gHandler.handler != nullptr && ue3::runtime::IsAlive(gHandler.handler) && ue3::IsA(gHandler.handler, gGalaxyHandler)) {
        return gHandler.handler;
    }
    std::uint64_t const now = GetTickCount64();
    if (gHandler.scannedMs != 0 && now - gHandler.scannedMs < 500) {
        return nullptr;
    }
    gHandler.scannedMs = now;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* handler = nullptr;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gGalaxyHandler) && r.IsAlive(o) && !r.NameText(o).starts_with("Default__")) {
            handler = o;  // the newest (an older one may linger)
        }
    }
    gHandler.handler = handler;
    return handler;
}

// An ActionScript function on the map's movie (exitGalaxyMap: the map's own way out).
bool InvokeFlash(ue3::Object* handler, char const* method) {
    ue3::Object* const panel = ObjectField(gHandlerPanel, handler);
    ue3::Function* const f = gInvokeMethod.Get();
    if (panel == nullptr || f == nullptr) {
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params p(r, f);
    std::string const text(method);
    mem::OwnedString name(std::wstring(text.begin(), text.end()));
    return p.Valid() && p.Set("sMethodName", name.Get()) && ue3::runtime::Call(panel, f, p);
}

// The map's opening wait shortened (in the CIC) or put back.
void ShortenOpening(bool shorten) {
    ue3::Object* const node = gConsole.openingDelay;
    if (node == nullptr || gConsole.openingShortened == shorten || !ue3::runtime::IsAlive(node)) {
        return;
    }
    float* const duration = gDelayDuration.In<float>(node);
    if (duration == nullptr) {
        return;
    }
    *duration = shorten ? kOpeningDelayS : gConsole.openingOriginalS;
    write_journal::Wrote("GalaxyMap.openingDelay", duration, sizeof(*duration));
    gConsole.openingShortened = shorten;
    log::Info("galaxy hologram: the map's opening wait {} ({:.2f} s)", shorten ? "shortened" : "as the game has it", *duration);
}

// BioArtPlaceableBehavior.OnArtPlaceableUsed, after the game's: the CIC's console was used (A at it, or our pick), so the map's
// opening (game mode 6) is held like a pick's.
void OnConsoleUsed(ue3::Object* behavior, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (behavior != nullptr && behavior == gConsole.behavior) {
        gConsole.usedMs = GetTickCount64();
    }
}

// The map's Flash movie shown or hidden (the window shows it; our hologram hides it).
bool SetFlashVisible(bool visible) {
    ue3::Object* const panel = ObjectField(gHandlerPanel, LiveHandler());
    ue3::Function* const f = gSetMovieVisibility.Get();
    if (panel == nullptr || f == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), f);
    return p.Valid() && p.SetBool("bVisible", visible) && ue3::runtime::Call(panel, f, p);
}

void PauseWindow() {
    for (capture_rig::Rig& eye : gWindow.eyes) {
        if (eye.Alive() && eye.Enabled()) {
            eye.SetEnabled(false);
        }
    }
    if (gWindow.on) {
        log::Info("galaxy window: paused ({} ticks pointed)", gWindow.pointed);
    }
    gWindow.on = false;
    gWindow.flashShown = false;
    gWindow.pointed = 0;
}

// The game's own map as a stereo window over the table (research/51 section 8): two captures from the game's map camera, its
// cameras side by side so the map's centre lands on the window's plane (core/vr/CineScreen), published in the scene; the map's
// Flash movie shown (its labels and buttons go to the host's UI panel on the same window). False: our hologram instead.
bool StepWindow(vr::galaxy_map::Settings const& settings, bool held, ue3::Object* behavior, gh::Scene& scene) {
    std::uint64_t const now = GetTickCount64();
    bool const wanted = settings.window && held && gMapPov.valid && now - gMapPov.ms < 500 && gMapPov.fovDeg > 1.0f;
    if (!wanted) {
        PauseWindow();
        return false;
    }
    HWND const window = GameWindow();
    RECT client{};
    if (window == nullptr || !GetClientRect(window, &client) || client.right < 64 || client.bottom < 64) {
        PauseWindow();
        return false;
    }
    for (int e = 0; e < 2; ++e) {
        capture_rig::Rig& eye = gWindow.eyes[e];
        if (eye.Alive()) {
            continue;
        }
        eye.Forget();
        if (gWindow.failedMs != 0 && now - gWindow.failedMs < 5000) {
            PauseWindow();
            return false;
        }
        // About the UI panel's resolution (half the window) at the window's aspect, both multiples of 8 (a 1172x1196 target's
        // texture wasn't found behind its resource; the spectator's 1920x1080 is).
        capture_rig::Options options;
        double const aspect = static_cast<double>(client.right) / static_cast<double>(client.bottom);
        int bestW = 1024, bestH = 1024;
        double bestError = 1e9;
        for (int w = 512; w <= 2048; w += 8) {
            int const h = static_cast<int>(std::lround(w / aspect / 8.0)) * 8;
            double const error = std::fabs(static_cast<double>(w) / h - aspect) + 1e-5 * std::abs(w - static_cast<int>(client.right) / 2);
            if (h >= 256 && h <= 2048 && error < bestError) {
                bestError = error;
                bestW = w;
                bestH = h;
            }
        }
        options.width = bestW;
        options.height = bestH;
        options.fovDeg = gMapPov.fovDeg;
        options.nearUnits = 10.0f;
        options.postProcess = true;
        if (!eye.Create(e == 0 ? "galaxy left" : "galaxy right", options, gMapPov.location)) {
            log::Warn("galaxy window: the {} capture failed ({}); our hologram instead, again in 5 s", e == 0 ? "left" : "right", eye.Failure());
            gWindow.failedMs = now;
            PauseWindow();
            return false;
        }
    }
    // The stereo: parallel cameras on the game's, the map's centre on the window's plane, infinity `depth` of the width behind.
    float const yaw = static_cast<float>(gMapPov.rotation.yaw) * 6.2831853f / 65536.0f;
    float const pitch = static_cast<float>(gMapPov.rotation.pitch) * 6.2831853f / 65536.0f;
    ue3::Vector const forward{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
    ue3::Vector const right{-std::sin(yaw), std::cos(yaw), 0.0f};
    float converge = 5000.0f;
    if (auto const c = player::Location(ObjectField(gCenterObject, behavior))) {
        float const along = (c->x - gMapPov.location.x) * forward.x + (c->y - gMapPov.location.y) * forward.y +
            (c->z - gMapPov.location.z) * forward.z;
        if (along > 100.0f) {
            converge = along;
        }
    }
    // On the hologram the window is its diameter wide (the disc found on foot, frozen while the view is held).
    float const widthM = settings.windowOnHologram && gLastAnchor.disc && gLastAnchor.discRadiusM > 0.1f ? 2.0f * gLastAnchor.discRadiusM
                                                                                                          : settings.windowWidthM;
    float const depth = std::min(settings.windowDepth, vr::cine_screen::MaxDepth(widthM, vr::cine_screen::kDefaultIpdM));
    float const crop = vr::cine_screen::CropFraction(depth);
    float const halfH = gMapPov.fovDeg * 0.5f * 0.01745329f;
    float const baseline = vr::cine_screen::BaselineUU(crop, converge, halfH);
    for (int e = 0; e < 2; ++e) {
        capture_rig::Rig& eye = gWindow.eyes[e];
        float const side = (e == 0 ? -0.5f : 0.5f) * baseline;
        ue3::Vector const at{gMapPov.location.x + right.x * side, gMapPov.location.y + right.y * side, gMapPov.location.z + right.z * side};
        if (!eye.SetFov(gMapPov.fovDeg) || (!eye.Enabled() && !eye.SetEnabled(true)) || !eye.Point(at, gMapPov.rotation)) {
            log::Warn("galaxy window: pointing the {} capture failed; our hologram instead", e == 0 ? "left" : "right");
            gWindow.failedMs = now;
            PauseWindow();
            return false;
        }
        eye.Tick();
    }
    if (!gWindow.on) {
        log::Info("galaxy window: the game's own map over the table: two {}x{} captures from the map camera ({:.1f} degrees), {:.0f} units "
                  "apart, the map's centre {:.0f} units away on the window, infinity {:.1f}% of its width behind",
            gWindow.eyes[0].Width(), gWindow.eyes[0].Height(), gMapPov.fovDeg, baseline, converge, depth * 100.0f);
    }
    if (!gWindow.flashShown) {
        gWindow.flashShown = SetFlashVisible(true);  // the game's labels, title and buttons, on the host's UI panel
    }
    gWindow.on = true;
    gWindow.baselineUU = baseline;
    gWindow.crop = crop;
    gWindow.convergeUU = converge;
    ++gWindow.pointed;
    scene.windowOn = 1;
    scene.windowFloat = 0;
    scene.windowRound = settings.windowRound ? 1 : 0;
    scene.windowBackdrop = static_cast<std::uint8_t>(std::lround(std::clamp(settings.windowBackdrop, 0.0f, 1.0f) * 100.0f));
    scene.windowWidth = static_cast<std::uint32_t>(gWindow.eyes[0].Width());
    scene.windowHeight = static_cast<std::uint32_t>(gWindow.eyes[0].Height());
    scene.windowTicks = std::min(gWindow.eyes[0].EnabledTicks(), gWindow.eyes[1].EnabledTicks());
    scene.windowResource[0] = gWindow.eyes[0].Resource();
    scene.windowResource[1] = gWindow.eyes[1].Resource();
    scene.windowGeneration = gWindow.eyes[0].Generation() * 1000003ull + gWindow.eyes[1].Generation();
    scene.windowCrop = crop;
    scene.windowWidthM = widthM;
    return true;
}

// The game's own hologram over the projector, hidden while ours shows there and shown again when ours goes.
void HideProjector(bool hide) {
    if (gConsole.projectorHidden == hide) {
        return;
    }
    ue3::Function* const f = gSetHidden.Get();
    if (f == nullptr) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    int done = 0;
    for (ue3::Object* const o : gConsole.projector) {
        if (o == nullptr || !ue3::runtime::IsAlive(o)) {
            continue;
        }
        ue3::Params p(r, f);
        if (p.Valid() && p.SetBool("bNewHidden", hide) && ue3::runtime::Call(o, f, p)) {
            ++done;
        }
    }
    gConsole.projectorHidden = hide;
    log::Info("galaxy hologram: the game's own hologram {} ({} actors)", hide ? "hidden" : "shown again", done);
}

// The game's own map (the Flash movie) hidden while the map is open in-world, re-asserted twice a second.
void HideFlashMap() {
    std::uint64_t const now = GetTickCount64();
    if (now - gHold.lastHideMs < 500) {
        return;
    }
    gHold.lastHideMs = now;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const panel = ObjectField(gHandlerPanel, LiveHandler());
    ue3::Function* const f = gSetMovieVisibility.Get();
    if (panel == nullptr || f == nullptr) {
        return;
    }
    ue3::Params p(r, f);
    if (p.Valid() && p.SetBool("bVisible", false) && ue3::runtime::Call(panel, f, p)) {
        if (++gHold.hides == 1) {
            log::Info("galaxy hologram: in the CIC: the game's own map hidden ({}), the view held at Shepard's ({:.0f}, {:.0f}, {:.0f})",
                      r.FullName(panel), gHold.location.x, gHold.location.y, gHold.location.z);
        }
    }
}

void Forget(Link const& link, char const* why) {
    PauseWindow();
    HideProjector(false);
    if (!gHold.active) {
        gLastAnchor.valid = false;  // a place found again next time (not while the view is held)
    }
    if (gS.published && link.setScene != nullptr) {
        link.setScene(nullptr);
        log::Info("galaxy hologram: down ({})", why);
    }
    gS.published = false;
    gS.lastState = -1;
    gS.lastCount = -1;
    gS.pick.reset();
}

} // namespace

void TryStartup() {
    ue3::Function* const update = ue3::runtime::Ready() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("galaxy hologram: Engine.Camera.UpdateCamera not found: the map can't be used in the CIC (it opens as the game's own)");
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
    if (ue3::Function* const used = gArtUsed.Get(); used != nullptr) {
        engine::process_event::AddPostHandler(used, &OnConsoleUsed);
    }
}

void Tick(Link const& link) {
    vr::galaxy_map::Settings const settings = hand_calibration::GetGalaxy();
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    std::uint64_t const now = GetTickCount64();
    bool const inWorld = settings.hologram && settings.inWorld && link.setScene != nullptr;
    ShortenOpening(inWorld);  // (once the console is known: near it)
    if (gOpen.pending && now - gOpen.startedMs > 8000) {
        log::Info("galaxy hologram: the map didn't open for the pick within 8 s; dropped");
        gOpen.pending = false;
    }
    // In the CIC the view stays Shepard's while the map is open, through the opening a pick on foot started, and through the
    // closing cutscene after it (both are the game's camera flying between the CIC and the map's scene).
    std::uint8_t const modeNow = mode ? *mode : 255;
    if (gHold.active && gHold.lastMode == kGalaxyMode && modeNow != kGalaxyMode) {
        gHold.closingUntilMs = now + 3000;
    }
    gHold.lastMode = modeNow;
    bool const used = gConsole.usedMs != 0 && now - gConsole.usedMs < 4000;  // the console's use (A, or our pick) opens the map
    bool const opening = ((gOpen.pending || used) && modeNow == 6) || (modeNow == 6 && now < gHold.closingUntilMs);
    bool const holding = inWorld && gHold.haveView && mode && (*mode == kGalaxyMode || opening);
    if (holding != gHold.active) {
        gRenderHold.on = false;  // the camera handler gives the fields again while holding
        gHold.active = holding;
        gHold.hides = 0;
        gHold.lastHideMs = 0;
        gHandler = HandlerCache{};  // a new map: its handler found again
        if (holding) {
            log::Info("galaxy hologram: the map is open in the CIC: the view held, the game's own map hidden");
        } else {
            log::Info("galaxy hologram: the view is the game's again ({} camera updates held)", gHold.cameraTicks);
        }
    }
    if (holding && mode && *mode == kGalaxyMode && !gWindow.on) {
        HideFlashMap();
    }
    if (link.setScene == nullptr || !settings.hologram || !mode || (*mode != kGalaxyMode && !(inWorld && (*mode == 0 || opening)))) {
        Forget(link, !settings.hologram ? "[Galaxy] hologram = 0" : "the map closed");
        return;
    }
    if (!Resolved()) {
        if (!gS.loggedMissing) {
            gS.loggedMissing = true;
            log::Warn("galaxy hologram: the galaxy camera's fields or Actor.SetLocation weren't found through our reflection layer: "
                      "the map stays on Halcyon's panel");
        }
        return;
    }
    ue3::Object* const behavior = FindBehavior();
    if (behavior == nullptr) {
        Forget(link, "no map camera");
        return;
    }
    if (behavior != gS.routesBehavior) {
        // A new map camera (a level load): its tables are read again when needed.
        gS.routesBehavior = behavior;
        gS.routesRead = false;
        gRowsRead = false;
    }
    if (*mode != kGalaxyMode) {
        // On foot only near the console (the CIC); nothing is read further away.
        ue3::Object* const pawn = player::Pawn(controller);
        auto const pawnAt = pawn != nullptr ? player::Location(pawn) : std::nullopt;
        auto const consoleAt = FindConsole() ? player::Location(gConsole.usable) : std::nullopt;
        if (!pawnAt || !consoleAt || std::hypot(pawnAt->x - consoleAt->x, pawnAt->y - consoleAt->y) > kNearConsoleUnits) {
            Forget(link, "away from the galaxy map's console");
            return;
        }
    }
    ue3::Object* const worldInfo = ObjectField(gWorldInfo, controller);
    ue3::Object* const game = ObjectField(gGame, worldInfo);
    if (!gS.routesRead) {
        ReadRoutes(behavior);
    }
    HWND const window = GameWindow();
    std::uint32_t id = 0;
    bool const picked = link.takePick != nullptr && link.takePick(&id);

    gh::Scene scene;
    if (*mode != kGalaxyMode) {
        // ---- On foot (or the map opening) in the CIC: the galaxy from the tables over the console ----
        if (picked && *mode == 0) {
            bool const opened = OpenMap(controller);
            gOpen = PendingOpen{opened, id, now};
            log::Info("galaxy hologram: picked {} on foot: the map {}", Label(std::to_string(id), game),
                      opened ? "opens (the console's use), the pick goes on when it's up" : "couldn't be opened");
        }
        auto const model = OnFootModel(behavior, worldInfo, game);
        if (!model) {
            Forget(link, "no galaxy on foot");
            return;
        }
        scene = gh::BuildScene(*model);
        // On foot, unless the map is opening for a pick: Halcyon decides its galaxy presentation as the map's mode begins.
        scene.onFoot = gOpen.pending || gHold.active ? 0 : 1;
    } else {
        // ---- The map is open ----
        if (picked) {
            StartPick(id, behavior, game);
        }
        StepPick(behavior, window);
        auto const model = ReadModel(behavior, controller, game, window);
        if (!model) {
            Forget(link, "the map's objects couldn't be read");
            return;
        }
        // The pick made on foot, once the map's galaxy is up.
        if (gOpen.pending && model->state == 1) {
            std::string const name = std::to_string(gOpen.id);
            if (std::any_of(model->actors.begin(), model->actors.end(), [&](gh::MapActor const& a) { return a.name == name && !a.hidden; })) {
                gOpen.pending = false;
                StartPick(gOpen.id, behavior, game);
            }
        }
        // What the open map shows is what the galaxy on foot shows next time.
        if (model->state == 1) {
            bool changed = false;
            for (gh::MapActor const& a : model->actors) {
                if (auto const it = gHiddenCache.find(a.name); it == gHiddenCache.end() || it->second != a.hidden) {
                    gHiddenCache[a.name] = a.hidden;
                    changed = true;
                }
            }
            if (changed) {
                SaveVisibility();
            }
        }
        scene = gh::BuildScene(*model);
        StepWindow(settings, inWorld && gHold.active, behavior, scene);
        if (inWorld) {
            // The game asks whether to travel (its own message box, which the host shows over the table): no picks meanwhile.
            if (ue3::Object* const handler = LiveHandler(); handler != nullptr && gWaitingForMsgBox.ReadBool(handler).value_or(false)) {
                scene.askTravel = 1;
            }
        }
    }
    scene.sequence = static_cast<std::uint32_t>(++gS.ticks);
    scene.tableDistanceM = settings.tableDistanceM;
    scene.tableBelowM = settings.tableBelowM;
    scene.tableRadiusM = settings.tableRadiusM;
    if (inWorld) {
        FindConsole();
        if (!PlaceInWorld(scene, settings)) {
            Forget(link, "the console's place isn't known");
            return;
        }
        HideProjector(true);
        // Walking away from the table closes the open map (its own way out), as stepping back from the console would.
        if (*mode == kGalaxyMode) {
            auto const frame = view_anchor::Current();
            float const dx = frame ? frame->head.position.x - scene.anchor[0] : 0.0f;
            float const dz = frame ? frame->head.position.z - scene.anchor[2] : 0.0f;
            bool const away = std::hypot(dx, dz) > settings.worldRadiusM + 1.6f;
            gAwaySinceMs = away ? (gAwaySinceMs == 0 ? now : gAwaySinceMs) : 0;
            if (gAwaySinceMs != 0 && now - gAwaySinceMs > 600 && now - gLastExitMs > 3000) {
                gLastExitMs = now;
                bool const closed = InvokeFlash(LiveHandler(), "exitGalaxyMap");
                log::Info("galaxy hologram: walked away from the table ({:.1f} m from its centre): the map {}", std::hypot(dx, dz),
                          closed ? "closes" : "didn't close");
            }
        }
    }
    if (scene.level != gS.lastState || scene.objectCount != gS.lastCount) {
        log::Info("galaxy hologram: {}{}", gh::Describe(scene), scene.onFoot ? " (on foot, from the tables)" : "");
        gS.lastState = scene.level;
        gS.lastCount = scene.objectCount;
    }
    if (gHold.active && gHold.haveView && gRenderHold.on && gHoldWrites) {
        scene.holdRender = 1;
        scene.holdLocation[0] = gHold.location.x;
        scene.holdLocation[1] = gHold.location.y;
        scene.holdLocation[2] = gHold.location.z;
        scene.holdRotation[0] = gHold.rotation.pitch;
        scene.holdRotation[1] = gHold.rotation.yaw;
        scene.holdRotation[2] = gHold.rotation.roll;
        scene.holdFov = gHold.fov;
        scene.povLocationAddress = reinterpret_cast<std::uint64_t>(gRenderHold.location);
        scene.povRotationAddress = reinterpret_cast<std::uint64_t>(gRenderHold.rotation);
        scene.povFovAddress = reinterpret_cast<std::uint64_t>(gRenderHold.fov);
    }
    link.setScene(&scene);
    gS.published = true;
    gS.scene = scene;
}

std::string ViewOverride(bool on, float yawDeg, float pitchDeg, float distanceUnits) {
    gView.on = on;
    if (!on) {
        return std::format("galaxy view: the game's own camera again ({} camera updates were ours)", gView.applied);
    }
    gView.yawDeg = yawDeg;
    gView.pitchDeg = std::clamp(pitchDeg, -89.0f, 89.0f);
    gView.distance = std::clamp(distanceUnits, 100.0f, 200000.0f);
    gView.applied = 0;
    return std::format("galaxy view: orbiting the map's centre at yaw {:.0f}, pitch {:.0f}, {:.0f} units (while the map is open)", gView.yawDeg,
                       gView.pitchDeg, gView.distance);
}

std::string HoldWrites(bool on) {
    gHoldWrites = on;
    return std::format("galaxy hold: {}", on ? "the view held at Shepard's again (at render time)" : "no held view (the map's own camera is rendered)");
}

std::string ShowFlash(bool visible) {
    ue3::Object* const panel = ObjectField(gHandlerPanel, LiveHandler());
    ue3::Function* const f = gSetMovieVisibility.Get();
    if (panel == nullptr || f == nullptr) {
        return "galaxy flash: the map's movie isn't open";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params p(r, f);
    bool const called = p.Valid() && p.SetBool("bVisible", visible) && ue3::runtime::Call(panel, f, p);
    return std::format("galaxy flash: {}.SetMovieVisibility({}) {}", r.FullName(panel), visible, called ? "called" : "failed");
}

std::string StatusJson() {
    return std::format("{{\"published\": {}, \"level\": {}, \"objects\": {}, \"links\": {}, \"picks\": {}, \"clicks\": {}, \"lastPick\": \"{}\", "
                       "\"pickPending\": {}}}",
        gS.published ? "true" : "false", gS.scene.level, gS.scene.objectCount, gS.scene.linkCount, gS.picks, gS.clicks, gS.lastPick,
        gS.pick ? "true" : "false");
}

void Shutdown(Link const& link, bool exiting) {
    ShortenOpening(false);
    Forget(link, "the mod is unloading");
    for (capture_rig::Rig& eye : gWindow.eyes) {
        if (!exiting) {
            eye.Destroy("the mod is unloading");
        }
        eye.Forget();
    }
    gS = State{};
}

} // namespace mevr::game::galaxy_map
