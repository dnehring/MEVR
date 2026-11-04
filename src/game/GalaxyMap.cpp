// SPDX-License-Identifier: MIT
// The galaxy map as a hologram we draw: the game's side (game/GalaxyMap.hpp).
#include "game/GalaxyMap.hpp"

#include "core/Log.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
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

void Tick(Link const& link) {
    vr::galaxy_map::Settings const settings = hand_calibration::GetGalaxy();
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    if (link.setScene == nullptr || !settings.hologram || !mode || *mode != kGalaxyMode) {
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
    // A pick from the host: the cross-hair onto it, then the game's own click once it's selected.
    std::uint32_t id = 0;
    if (link.takePick != nullptr && link.takePick(&id)) {
        StartPick(id, behavior, game);
    }
    StepPick(behavior, window);

    auto const model = ReadModel(behavior, controller, game, window);
    if (!model) {
        Forget(link, "the map's objects couldn't be read");
        return;
    }
    gh::Scene scene = gh::BuildScene(*model);
    scene.sequence = static_cast<std::uint32_t>(++gS.ticks);
    scene.tableDistanceM = settings.tableDistanceM;
    scene.tableBelowM = settings.tableBelowM;
    scene.tableRadiusM = settings.tableRadiusM;
    if (model->state != gS.lastState || scene.objectCount != gS.lastCount) {
        log::Info("galaxy hologram: {}", gh::Describe(scene));
        gS.lastState = model->state;
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
