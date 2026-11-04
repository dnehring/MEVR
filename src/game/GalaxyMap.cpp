// SPDX-License-Identifier: MIT
// The galaxy map as a hologram we draw: the game's side (game/GalaxyMap.hpp).
#include "game/GalaxyMap.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
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
ue3::FunctionRef gTravelConfirm{"SFXGame.BioSFHandler_GalaxyMap.TravelConfirm"};
ue3::FieldRef gWaitingForMsgBox{"SFXGame.BioSFHandler_GalaxyMap", "m_bWaitingForMsgBox"};

// The map's clusters on foot come from GalaxyMap_Cluster: X and Y (0..1) are where the map's level puts each cluster's actor,
// 630 units a step and centred on (0.5, 0.5) (research/51 section 8), so on foot and open the hologram is the same picture.
constexpr float kClusterUnits = 630.0f;
constexpr float kNearConsoleUnits = 900.0f;  // the hologram shows on foot within this of the console (the CIC)

struct Console {
    ue3::Object* usable = nullptr;
    ue3::Object* behavior = nullptr;
    std::uint64_t lastSearchMs = 0;
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
};
Hold gHold;

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
    *pov->location = gHold.location;
    write_journal::Wrote("GalaxyMap.holdLocation", pov->location, sizeof(*pov->location));
    *pov->rotation = gHold.rotation;
    write_journal::Wrote("GalaxyMap.holdRotation", pov->rotation, sizeof(*pov->rotation));
    *pov->fov = gHold.fov;
    write_journal::Wrote("GalaxyMap.holdFov", pov->fov, sizeof(*pov->fov));
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
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n && gConsole.usable == nullptr; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gArtUsedEvent) && r.IsAlive(o) && r.NameText(r.OuterOf(o)) == "GalaxyMap") {
            gConsole.usable = ObjectField(gOriginator, o);
        }
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); gConsole.usable != nullptr && i < n && gConsole.behavior == nullptr; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gArtBehavior) && r.IsAlive(o) && r.OuterOf(o) == gConsole.usable) {
            gConsole.behavior = o;
        }
    }
    if (gConsole.usable != nullptr && gConsole.behavior != nullptr) {
        auto const at = player::Location(gConsole.usable);
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
bool PlaceInWorld(gh::Scene& scene, vr::galaxy_map::Settings const& settings) {
    auto const frame = view_anchor::Current();
    auto const at = gConsole.usable != nullptr ? player::Location(gConsole.usable) : std::nullopt;
    if (!frame || !at) {
        return false;
    }
    engine::UnrealVector const centre{at->x, at->y, at->z + settings.worldLiftM * frame->unitsPerMeter};
    XrVector3f const anchor = engine::ToTracking(*frame, centre);
    XrVector3f const away = engine::DirectionToTracking(*frame, {0.0, -1.0, 0.0});
    scene.inWorld = 1;
    scene.anchorValid = 1;
    scene.anchor[0] = anchor.x;
    scene.anchor[1] = anchor.y;
    scene.anchor[2] = anchor.z;
    scene.anchorYaw = std::atan2(-away.x, -away.z);
    scene.tableRadiusM = settings.worldRadiusM;
    scene.tiltDeg = settings.worldTiltDeg;
    scene.forward[0] = 0.0f;
    scene.forward[1] = -1.0f;
    scene.forward[2] = 0.0f;
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
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* handler = nullptr;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gGalaxyHandler) && r.IsAlive(o) && !r.NameText(o).starts_with("Default__")) {
            handler = o;
        }
    }
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

// The answer to the game's travel question (its message box hidden with the map): TravelConfirm(A pressed, context 0).
void AnswerTravel(ue3::Object* handler, bool yes) {
    ue3::Function* const f = gTravelConfirm.Get();
    if (handler == nullptr || f == nullptr) {
        return;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Params p(r, f);
    bool const called = p.Valid() && p.SetBool("bAPressed", yes) && p.Set("nContext", static_cast<std::int32_t>(0)) && ue3::runtime::Call(handler, f, p);
    log::Info("galaxy hologram: the travel question answered {} ({})", yes ? "yes" : "no", called ? "TravelConfirm" : "TravelConfirm failed");
}

// The game's own map (the Flash movie) hidden while the map is open in-world, re-asserted twice a second.
void HideFlashMap() {
    std::uint64_t const now = GetTickCount64();
    if (now - gHold.lastHideMs < 500) {
        return;
    }
    gHold.lastHideMs = now;
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* handler = nullptr;
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && ue3::IsA(o, gGalaxyHandler) && r.IsAlive(o) && !r.NameText(o).starts_with("Default__")) {
            handler = o;
        }
    }
    ue3::Object* const panel = ObjectField(gHandlerPanel, handler);
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
    if (gS.published && link.setScene != nullptr) {
        link.setScene(nullptr);
        log::Info("galaxy hologram: down ({})", why);
    }
    gS.published = false;
    gS.lastState = -1;
    gS.lastCount = -1;
    gS.routesRead = false;
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
}

void Tick(Link const& link) {
    vr::galaxy_map::Settings const settings = hand_calibration::GetGalaxy();
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    std::uint64_t const now = GetTickCount64();
    bool const inWorld = settings.hologram && settings.inWorld && link.setScene != nullptr;
    if (gOpen.pending && now - gOpen.startedMs > 8000) {
        log::Info("galaxy hologram: the map didn't open for the pick within 8 s; dropped");
        gOpen.pending = false;
    }
    // In the CIC the view stays Shepard's while the map is open, and through the opening a pick on foot started.
    bool const opening = gOpen.pending && mode && *mode == 6;
    bool const holding = inWorld && gHold.haveView && mode && (*mode == kGalaxyMode || opening);
    if (holding != gHold.active) {
        gHold.active = holding;
        gHold.hides = 0;
        gHold.lastHideMs = 0;
        if (holding) {
            log::Info("galaxy hologram: the map is open in the CIC: the view held, the game's own map hidden");
        } else {
            log::Info("galaxy hologram: the view is the game's again ({} camera updates held)", gHold.cameraTicks);
        }
    }
    if (holding && mode && *mode == kGalaxyMode) {
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
        ue3::Object* const pawn = player::Pawn(controller);
        auto const pawnAt = pawn != nullptr ? player::Location(pawn) : std::nullopt;
        auto const consoleAt = FindConsole() ? player::Location(gConsole.usable) : std::nullopt;
        if (!pawnAt || !consoleAt ||
            std::hypot(pawnAt->x - consoleAt->x, pawnAt->y - consoleAt->y) > kNearConsoleUnits) {
            Forget(link, "away from the galaxy map's console");
            return;
        }
        if (picked && *mode == 0 && id != gh::kPickYes && id != gh::kPickNo) {
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
        scene.onFoot = 1;
    } else {
        // ---- The map is open ----
        if (picked) {
            if (id == gh::kPickYes || id == gh::kPickNo) {
                AnswerTravel(LiveHandler(), id == gh::kPickYes);
            } else {
                StartPick(id, behavior, game);
            }
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
        if (inWorld) {
            if (ue3::Object* const handler = LiveHandler(); handler != nullptr && gWaitingForMsgBox.ReadBool(handler).value_or(false)) {
                scene.askTravel = 1;
                std::string selected;
                for (int i = 0; i < scene.objectCount; ++i) {
                    if ((scene.objects[i].flags & gh::kSelected) != 0) {
                        selected = scene.objects[i].label;
                    }
                }
                std::snprintf(scene.askText, gh::kLabelBytes, "Travel to %s?", selected.empty() ? "this system" : selected.c_str());
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
    link.setScene(&scene);
    gS.published = true;
    gS.scene = scene;
}

std::string StatusJson() {
    return std::format("{{\"published\": {}, \"level\": {}, \"objects\": {}, \"links\": {}, \"picks\": {}, \"clicks\": {}, \"lastPick\": \"{}\", "
                       "\"pickPending\": {}}}",
        gS.published ? "true" : "false", gS.scene.level, gS.scene.objectCount, gS.scene.linkCount, gS.picks, gS.clicks, gS.lastPick,
        gS.pick ? "true" : "false");
}

void Shutdown(Link const& link) {
    Forget(link, "the mod is unloading");
    gS = State{};
}

} // namespace mevr::game::galaxy_map
