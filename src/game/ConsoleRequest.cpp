// SPDX-License-Identifier: MIT
#include "game/ConsoleRequest.hpp"
#include "game/CameraShake.hpp"
#include "game/BodyProbe.hpp"
#include "game/ConversationWorld.hpp"
#include "game/ConversationFlow.hpp"
#include "game/AuthoredMoment.hpp"

#include "game/SquadContext.hpp"
#include "game/SquadOrders.hpp"
#include "game/SquadPlace.hpp"
#include "game/Voice.hpp"
#include "game/VrCover.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "engine/Legacy.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "game/CallTrace.hpp"
#include "game/CockpitProbe.hpp"
#include "game/MakoGunner.hpp"
#include "game/GalaxyDump.hpp"
#include "game/HoloProbe.hpp"
#include "game/GuiCursor.hpp"
#include "game/ScreenReticle.hpp"
#include "game/UiWorldFigure.hpp"
#include "game/InventoryProbe.hpp"
#include "game/GalaxyMap.hpp"
#include "game/GameMenuRequest.hpp"
#include "game/GunKick.hpp"
#include "game/Holsters.hpp"
#include "game/OmniTool.hpp"
#include "game/Player.hpp"
#include "game/PowerWheel.hpp"
#include "game/SoundPlace.hpp"
#include "game/Spectator.hpp"
#include "game/Thespian.hpp"
#include "game/Throwables.hpp"
#include "game/Vent.hpp"
#include "core/WriteJournal.hpp"

#include <Windows.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace mevr::game::console_request {
namespace {

constexpr float kPollSeconds = 0.5f;
constexpr char const* kConsoleCommand = "Engine.PlayerController.ConsoleCommand";

float gSincePoll = 0.0f;
ue3::Function* gConsoleCommand = nullptr;
bool gLookedUp = false;

std::filesystem::path Directory() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR";
}

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34): the function, found once in the running game.
ue3::FunctionRef gConsoleCommandRef{kConsoleCommand};
bool gToldNoMemory = false;
bool gToldParameters = false;

ue3::Object* PlayerController() {
    return ue3::runtime::LocalPlayerController();
}

// Our own lines, for the measuring loop (research/35 attempt 42): "mevr loc" prints the player's pawn's Location and
// Rotation, "mevr setloc X Y Z" moves the pawn there (Engine.Actor.SetLocation through ProcessEvent, on the game thread,
// which this tick is). The console's own `getall`/`displayall` print nowhere we can read, and the game has no way to
// jump to a known place: this is how a heavy spot is reached again and again without a person walking there.
ue3::FunctionRef gSetLocationRef{"Engine.Actor.SetLocation"};
// "mevr density [cellUU]": where the placed meshes are. Every live static mesh actor, dynamic/interp actor and
// SpeedTree actor in the loaded levels, binned into cells of the given side (default 2000 UU = 20 m), the densest
// cells first: the places to measure at.
ue3::ClassRef gStaticMeshActorBase{"Engine.StaticMeshActorBase"};
ue3::ClassRef gDynamicSMActor{"Engine.DynamicSMActor"};
ue3::ClassRef gSpeedTreeActor{"Engine.SpeedTreeActor"};

std::string Density(ue3::Object const* pawn, float cell) {
    ue3::Reflection& r = ue3::runtime::Get();
    struct Cell { long long key; long long cx; long long cy; int count; float z; };
    std::unordered_map<long long, Cell> cells;
    int seen = 0;
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o)) {
            continue;
        }
        if (!ue3::IsA(o, gStaticMeshActorBase) && !ue3::IsA(o, gDynamicSMActor) && !ue3::IsA(o, gSpeedTreeActor)) {
            continue;
        }
        if (r.NameText(o).rfind("Default__", 0) == 0) {
            continue;
        }
        std::optional<ue3::Vector> const at = player::Location(o);
        if (!at || (at->x == 0.0f && at->y == 0.0f && at->z == 0.0f)) {
            continue;
        }
        ++seen;
        long long const cx = static_cast<long long>(std::floor(at->x / cell));
        long long const cy = static_cast<long long>(std::floor(at->y / cell));
        long long const key = cx * 1000003LL + cy;
        Cell& c = cells[key];
        c.key = key;
        c.cx = cx;
        c.cy = cy;
        c.z += at->z;
        ++c.count;
    }
    std::vector<Cell> sorted;
    for (auto const& [key, c] : cells) {
        sorted.push_back(c);
    }
    std::sort(sorted.begin(), sorted.end(), [](Cell const& a, Cell const& b) { return a.count > b.count; });
    std::optional<ue3::Vector> const here = player::Location(pawn);
    std::string out = std::to_string(seen) + " mesh actors in " + std::to_string(cells.size()) + " cells of " +
                      std::to_string(static_cast<int>(cell)) + " UU";
    if (here) {
        long long const key = static_cast<long long>(std::floor(here->x / cell)) * 1000003LL +
                              static_cast<long long>(std::floor(here->y / cell));
        auto const it = cells.find(key);
        out += "; the pawn's cell has " + std::to_string(it != cells.end() ? it->second.count : 0);
    }
    out += "\n";
    int shown = 0;
    for (Cell const& c : sorted) {
        if (shown++ >= 12) {
            break;
        }
        char line[128];
        std::snprintf(line, sizeof(line), "  %5d meshes at %.0f %.0f (mean z %.0f)\n", c.count, (c.cx + 0.5) * cell,
                      (c.cy + 0.5) * cell, c.z / c.count);
        out += line;
    }
    return out;
}

// "mevr crowd [cellUU]": where the characters are (the crowd survey, research/50). Every live Pawn in the loaded levels
// but the player's (not hidden, not being deleted), binned into cells of the given side (default 1500 UU = 15 m), the
// most crowded cells first, each with how many of its pawns rendered in the last second and the pawns' centroid (a
// place among them, usually open floor, for `tools/spot-survey.ps1`); and how many are near the player.
ue3::ClassRef gPawnClass{"Engine.Pawn"};
ue3::FieldRef gActorHidden{"Engine.Actor", "bHidden"};
ue3::FieldRef gActorDeleteMe{"Engine.Actor", "bDeleteMe"};
ue3::FieldRef gActorLastRender{"Engine.Actor", "LastRenderTime"};
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gWorldTimeSeconds{"Engine.WorldInfo", "TimeSeconds"};

std::string Crowd(ue3::Object* pawn, float cell) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = gActorWorldInfo.Read<ue3::Object*>(pawn).value_or(nullptr);
    std::optional<float> const now = world != nullptr ? gWorldTimeSeconds.Read<float>(world) : std::nullopt;
    std::optional<ue3::Vector> const here = player::Location(pawn);
    struct Cell { long long cx; long long cy; int count; int rendered; double x; double y; double z; };
    std::unordered_map<long long, Cell> cells;
    int seen = 0;
    int rendered = 0;
    int near10 = 0;
    int near25 = 0;
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || o == pawn || !r.IsAlive(o) || !ue3::IsA(o, gPawnClass)) {
            continue;
        }
        if (r.NameText(o).rfind("Default__", 0) == 0 || gActorHidden.ReadBool(o).value_or(false) || gActorDeleteMe.ReadBool(o).value_or(false)) {
            continue;
        }
        std::optional<ue3::Vector> const at = player::Location(o);
        if (!at || (at->x == 0.0f && at->y == 0.0f && at->z == 0.0f)) {
            continue;
        }
        ++seen;
        std::optional<float> const last = gActorLastRender.Read<float>(o);
        bool const shown = now && last && *now - *last < 1.0f;
        rendered += shown ? 1 : 0;
        if (here) {
            double const dx = at->x - here->x;
            double const dy = at->y - here->y;
            double const dz = at->z - here->z;
            double const d = std::sqrt(dx * dx + dy * dy + dz * dz);
            near10 += d < 1000.0 ? 1 : 0;
            near25 += d < 2500.0 ? 1 : 0;
        }
        long long const cx = static_cast<long long>(std::floor(at->x / cell));
        long long const cy = static_cast<long long>(std::floor(at->y / cell));
        Cell& c = cells[cx * 1000003LL + cy];
        c.cx = cx;
        c.cy = cy;
        ++c.count;
        c.rendered += shown ? 1 : 0;
        c.x += at->x;
        c.y += at->y;
        c.z += at->z;
    }
    std::vector<Cell> sorted;
    for (auto const& [key, c] : cells) {
        sorted.push_back(c);
    }
    std::sort(sorted.begin(), sorted.end(), [](Cell const& a, Cell const& b) { return a.count > b.count; });
    std::string out = std::format("{} pawns ({} rendered in the last second) in {} cells of {} UU", seen, rendered, cells.size(), static_cast<int>(cell));
    if (here) {
        out += std::format("; near the player: {} within 10 m, {} within 25 m", near10, near25);
    }
    out += "\n";
    int shown = 0;
    for (Cell const& c : sorted) {
        if (shown++ >= 12) {
            break;
        }
        out += std::format("  {:3} pawns ({:3} rendered) in the cell at {:.0f} {:.0f}; their centroid {:.0f} {:.0f} {:.0f}\n", c.count, c.rendered,
            (c.cx + 0.5) * cell, (c.cy + 0.5) * cell, c.x / c.count, c.y / c.count, c.z / c.count);
    }
    return out;
}

// "mevr stream list [filter]" / "mevr stream touch <index|name>": the level's streaming volumes (research/50). LE1 shows
// a hub's areas through BioTriggerStream volumes: touching one applies its streaming state (the chunks to show and to
// load). `list` prints each volume (its index, name, place and states; with a filter, only volumes whose name or chunk
// names contain it); `touch` calls the volume's own Touch event with the player's pawn, so the game streams that area
// in as when the player walks into it (then move the pawn there).
ue3::ClassRef gTriggerStream{"SFXGame.BioTriggerStream"};
ue3::FieldRef gTriggerStates{"SFXGame.BioTriggerStream", "StreamingStates"};
ue3::FieldRef gStateName{gTriggerStates, "StateName"};
ue3::FieldRef gStateInChunk{gTriggerStates, "InChunkName"};
ue3::FieldRef gStateVisible{gTriggerStates, "VisibleChunkNames"};
ue3::FieldRef gStateLoad{gTriggerStates, "LoadChunkNames"};
ue3::FunctionRef gTriggerTouch{"SFXGame.BioTriggerStream.Touch"};
// "mevr stream chunks" / "mevr stream states" / "mevr stream state <name> on|off" (research/59 "The launcher's gaps closed"):
// a plot-loaded chunk (the medical bay's NOR10_07wake) is listed by no volume's plain state (StateName None or Default,
// applied by a touch) but by a NAMED one, which applies only while the world's streaming state of that name is on
// (BioWorldInfo.m_WorldStreamingStates, set by the plot's Kismet through BioSeqAct_SetStreamingState). `chunks` prints every
// streaming level's load state, one per line, so a script waits for `loaded=1 visible=1` before teleporting (Virmire's
// teleport into a chunk whose floor wasn't loaded dropped Shepard to the kill plane); `states` the world's named states;
// `state` switches one the game's own way: a loaded BioSeqAct_SetStreamingState's Activated (its script), with our name and
// value put on the action for the call and its own put back after; with none loaded, the world's entry is written and
// every volume's Retouch (native) called [verify]. Never the engine console's `streamlevelin`: it froze the game.
ue3::FieldRef gActorWorld{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels"};
ue3::FieldRef gLevelPackage{"Engine.LevelStreaming", "PackageName"};
ue3::FieldRef gLevelChunk{"Engine.LevelStreaming", "VirtualChunkName"};
ue3::FieldRef gLevelLoaded{"Engine.LevelStreaming", "LoadedLevel"};
ue3::FieldRef gLevelShouldLoad{"Engine.LevelStreaming", "bShouldBeLoaded"};
ue3::FieldRef gLevelShouldShow{"Engine.LevelStreaming", "bShouldBeVisible"};
ue3::FieldRef gLevelVisible{"Engine.LevelStreaming", "bIsVisible"};
ue3::FieldRef gWorldStates{"SFXGame.BioWorldInfo", "m_WorldStreamingStates"};
ue3::FieldRef gWorldStateName{gWorldStates, "Name"};
ue3::FieldRef gWorldStateEnabled{gWorldStates, "Enabled"};
ue3::ClassRef gSetStreamingState{"SFXGame.BioSeqAct_SetStreamingState"};
ue3::FieldRef gSetStateName{"SFXGame.BioSeqAct_SetStreamingState", "StateName"};
ue3::FieldRef gSetStateValue{"SFXGame.BioSeqAct_SetStreamingState", "NewValue"};
ue3::FunctionRef gSetStateActivated{"SFXGame.BioSeqAct_SetStreamingState.Activated"};
ue3::FunctionRef gTriggerRetouch{"SFXGame.BioTriggerStream.Retouch"};

std::vector<ue3::Object*> Instances(ue3::ClassRef& cls) {
    ue3::Reflection& r = ue3::runtime::Get();
    std::vector<ue3::Object*> found;
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsAlive(o) && ue3::IsA(o, cls) && r.NameText(o).rfind("Default__", 0) != 0) {
            found.push_back(o);
        }
    }
    return found;
}

std::vector<ue3::Object*> TriggerStreams() {
    return Instances(gTriggerStream);
}

// The world (Engine.Actor.WorldInfo) of the pawn, else of the player controller.
ue3::Object* WorldOf(ue3::Object* pawn) {
    ue3::Object* world = pawn != nullptr ? gActorWorld.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    if (world == nullptr) {
        if (ue3::Object* const pc = PlayerController()) {
            world = gActorWorld.Read<ue3::Object*>(pc).value_or(nullptr);
        }
    }
    return world;
}

// The world's streaming state of that name: its element, or nullptr.
void* WorldStateNamed(ue3::Object* world, ue3::Name name) {
    if (auto const states = gWorldStates.ElementsOf(world)) {
        for (std::int32_t i = 0; i < states->count; ++i) {
            void* const state = states->At(i);
            if (gWorldStateName.Read<ue3::Name>(state) == std::optional<ue3::Name>(name)) {
                return state;
            }
        }
    }
    return nullptr;
}

std::string Tri(std::optional<bool> value) {
    return value ? (*value ? "on" : "off") : "unknown";
}

// The engine's Name whose text is `text`, among the names the loaded streaming data holds (the actions' StateName, the
// volumes' states, the world's states): no name-table lookup, and a name nothing loaded knows can't switch anything.
std::optional<ue3::Name> StateNameFor(std::string const& text, std::vector<ue3::Object*> const& actions, std::vector<ue3::Object*> const& triggers, ue3::Object* world) {
    ue3::Reflection& r = ue3::runtime::Get();
    auto matches = [&](std::optional<ue3::Name> const& name) { return name && ue3::names::EqualsNoCase(r.ToString(*name), text); };
    for (ue3::Object* const action : actions) {
        if (auto const name = gSetStateName.Read<ue3::Name>(action); matches(name)) {
            return name;
        }
    }
    for (ue3::Object* const trigger : triggers) {
        if (auto const states = gTriggerStates.ElementsOf(trigger)) {
            for (std::int32_t s = 0; s < states->count; ++s) {
                if (auto const name = gStateName.Read<ue3::Name>(states->At(s)); matches(name)) {
                    return name;
                }
            }
        }
    }
    if (auto const states = gWorldStates.ElementsOf(world)) {
        for (std::int32_t s = 0; s < states->count; ++s) {
            if (auto const name = gWorldStateName.Read<ue3::Name>(states->At(s)); matches(name)) {
                return name;
            }
        }
    }
    return std::nullopt;
}

std::string StreamChunks(ue3::Object* pawn) {
    ue3::Object* const world = WorldOf(pawn);
    auto const levels = gStreamingLevels.ElementsOf(world);
    if (!levels) {
        return std::string("mevr stream chunks: no streaming levels (no world, or WorldInfo.StreamingLevels not read)");
    }
    std::string out = std::format("{} streaming levels (package loaded visible shouldLoad shouldShow chunk)\n", levels->count);
    for (std::int32_t i = 0; i < levels->count; ++i) {
        ue3::Object* const level = *static_cast<ue3::Object**>(levels->At(i));
        if (level == nullptr) {
            continue;
        }
        auto const package = gLevelPackage.Read<ue3::Name>(level);
        auto const chunk = gLevelChunk.Read<ue3::Name>(level);
        auto flag = [&](ue3::FieldRef& field) { return field.ReadBool(level).value_or(false) ? '1' : '0'; };
        out += std::format("{} loaded={} visible={} shouldLoad={} shouldShow={} chunk={}\n", package ? ue3::runtime::NameText(*package) : std::string("?"),
            gLevelLoaded.Read<ue3::Object*>(level).value_or(nullptr) != nullptr ? '1' : '0', flag(gLevelVisible), flag(gLevelShouldLoad), flag(gLevelShouldShow),
            chunk ? ue3::runtime::NameText(*chunk) : std::string("?"));
    }
    return out;
}

// `mevr stream show <package> [on|off]`: a streaming level's bShouldBeVisible written, the engine's level streaming adds it to the
// world (or takes it out) on its next update, as a streaming volume's state would, without the volume's events (research/69 item
// 36: the council's chamber shown during the walk with Anderson). Loaded levels only: a hidden level that isn't loaded is left.
std::string StreamShow(ue3::Object* pawn, std::string const& package, std::string const& value) {
    bool on = true;
    if (value == "off" || value == "0" || value == "false") {
        on = false;
    } else if (!(value.empty() || value == "on" || value == "1" || value == "true")) {
        return std::string("usage: mevr stream show <package> [on|off] (mevr stream chunks lists them)");
    }
    ue3::Object* const world = WorldOf(pawn);
    auto const levels = gStreamingLevels.ElementsOf(world);
    if (!levels || package.empty()) {
        return std::string("usage: mevr stream show <package> [on|off] (no streaming levels, or no package named)");
    }
    auto const lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    for (std::int32_t i = 0; i < levels->count; ++i) {
        ue3::Object* const level = *static_cast<ue3::Object**>(levels->At(i));
        auto const name = level != nullptr ? gLevelPackage.Read<ue3::Name>(level) : std::nullopt;
        if (!name || lower(ue3::runtime::NameText(*name)) != lower(package)) {
            continue;
        }
        bool const loaded = gLevelLoaded.Read<ue3::Object*>(level).value_or(nullptr) != nullptr;
        if (on && !loaded) {
            return std::format("{} is not loaded: left hidden", package);
        }
        bool const was = gLevelShouldShow.ReadBool(level).value_or(false);
        if (!gLevelShouldShow.WriteBool(level, on)) {
            return std::format("{}: bShouldBeVisible not written", package);
        }
        write_journal::Wrote("ConsoleRequest.streamShow", static_cast<std::uint8_t*>(static_cast<void*>(level)) + gLevelShouldShow.Get()->offset, 4);
        return std::format("{}: bShouldBeVisible {} -> {} (visible now {})", package, was ? 1 : 0, on ? 1 : 0,
            gLevelVisible.ReadBool(level).value_or(false) ? 1 : 0);
    }
    return std::format("no streaming level {} (mevr stream chunks lists them)", package);
}

// `mevr stream people <package>`: the level's pawns (SFXGame.BioPawn) with their tag, hidden flag and place: who a streaming level
// would show (research/69 item 36: the hearing's people are hidden until its black screen).
ue3::FieldRef gActorTag{"Engine.Actor", "Tag"};
std::string StreamPeople(ue3::Object* pawn, std::string const& package) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const pawnClass = r.FindClass("SFXGame.BioPawn");
    if (pawnClass == nullptr || package.empty()) {
        return std::string("usage: mevr stream people <package> (SFXGame.BioPawn not found, or no package named)");
    }
    std::string const prefix = package + ".";
    ue3::Object* const world = WorldOf(pawn);
    float const now = world != nullptr ? gWorldTimeSeconds.Read<float>(world).value_or(-1.0f) : -1.0f;
    std::string out;
    int n = 0;
    for (std::int32_t i = 0, count = r.ObjectCount(); i < count; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, pawnClass)) {
            continue;
        }
        std::string const name = r.FullName(o);
        std::size_t const space = name.find(' ');
        std::string const path = space == std::string::npos ? name : name.substr(space + 1);
        if (path.rfind(prefix, 0) != 0 || name.find("Default__") != std::string::npos) {
            continue;
        }
        auto const tag = gActorTag.Read<ue3::Name>(o);
        auto const at = player::Location(o);
        float const drawn = gActorLastRender.Read<float>(o).value_or(-1.0f);
        out += std::format("\n{} tag {} hidden {} drawn {} at {:.0f} {:.0f} {:.0f}", r.NameText(o), tag ? ue3::runtime::NameText(*tag) : std::string("?"),
            gActorHidden.ReadBool(o).value_or(false) ? 1 : 0,
            now >= 0.0f && drawn > 0.0f ? std::format("{:.2f} s ago", now - drawn) : std::string("never"),
            at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f);
        ++n;
    }
    return std::format("{} pawns in {}{}", n, package, out);
}

std::string StreamStates(ue3::Object* pawn) {
    ue3::Object* const world = WorldOf(pawn);
    auto const states = gWorldStates.ElementsOf(world);
    if (!states) {
        return std::string("the world has no named streaming states (BioWorldInfo.m_WorldStreamingStates is empty or wasn't read)");
    }
    std::string out = std::format("{} world streaming states\n", states->count);
    for (std::int32_t i = 0; i < states->count; ++i) {
        void const* const state = states->At(i);
        auto const name = gWorldStateName.Read<ue3::Name>(state);
        out += std::format("{} enabled={}\n", name ? ue3::runtime::NameText(*name) : std::string("?"), gWorldStateEnabled.ReadBool(state).value_or(false) ? 1 : 0);
    }
    return out;
}

std::string StreamState(ue3::Object* pawn, std::string const& name, std::string const& value, std::vector<ue3::Object*> const& triggers) {
    bool on = false;
    if (value == "on" || value == "1" || value == "true") {
        on = true;
    } else if (!(value == "off" || value == "0" || value == "false")) {
        return std::string("usage: mevr stream state <StateName> on|off (mevr stream states lists the world's)");
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Object* const world = WorldOf(pawn);
    if (world == nullptr) {
        return std::string("mevr stream state: no world (Engine.Actor.WorldInfo not read)");
    }
    std::vector<ue3::Object*> const actions = Instances(gSetStreamingState);
    std::optional<ue3::Name> const state = StateNameFor(name, actions, triggers, world);
    if (!state) {
        return std::format("mevr stream state: no loaded action, volume or world state knows a state '{}' (mevr stream states, mevr stream list): nothing done", name);
    }
    void* const worldState = WorldStateNamed(world, *state);
    std::optional<bool> const before = worldState != nullptr ? gWorldStateEnabled.ReadBool(worldState) : std::nullopt;
    std::string route;
    bool called = false;
    if (!actions.empty()) {
        ue3::Function* const activated = gSetStateActivated.Get();
        if (activated == nullptr) {
            return std::string("mevr stream state: SFXGame.BioSeqAct_SetStreamingState.Activated not found: nothing done");
        }
        // An action already set to this name and value needs no write; else the first one carries ours for the call.
        ue3::Object* action = nullptr;
        for (ue3::Object* const candidate : actions) {
            if (gSetStateName.Read<ue3::Name>(candidate) == state && gSetStateValue.ReadBool(candidate) == std::optional<bool>(on)) {
                action = candidate;
                break;
            }
        }
        bool wrote = false;
        ue3::Name savedName{};
        bool savedValue = false;
        if (action == nullptr) {
            action = actions.front();
            ue3::Name* const slot = gSetStateName.In<ue3::Name>(action);
            std::optional<bool> const held = gSetStateValue.ReadBool(action);
            if (slot == nullptr || !held || gSetStateValue.Get() == nullptr) {
                return std::format("mevr stream state: {}'s StateName or NewValue isn't readable: nothing done", r.FullName(action));
            }
            savedName = *slot;
            savedValue = *held;
            *slot = *state;
            write_journal::Wrote("ConsoleRequest.streamState.name", slot);
            gSetStateValue.WriteBool(action, on);
            write_journal::Wrote("ConsoleRequest.streamState.value", static_cast<std::uint8_t*>(static_cast<void*>(action)) + gSetStateValue.Get()->offset, 4);
            wrote = true;
        }
        ue3::Params parms(r, activated);
        called = ue3::runtime::Call(action, activated, parms);
        if (wrote) {
            if (ue3::Name* const slot = gSetStateName.In<ue3::Name>(action)) {
                *slot = savedName;
                write_journal::Wrote("ConsoleRequest.streamState.name", slot);
            }
            gSetStateValue.WriteBool(action, savedValue);
            write_journal::Wrote("ConsoleRequest.streamState.value", static_cast<std::uint8_t*>(static_cast<void*>(action)) + gSetStateValue.Get()->offset, 4);
        }
        route = std::format("{}.Activated(){}", r.FullName(action), wrote ? " (our name and value on it for the call, its own put back)" : " (it already held them)");
    } else {
        if (worldState == nullptr) {
            return std::format("mevr stream state: no BioSeqAct_SetStreamingState is loaded and the world has no state '{}' to switch: nothing done", name);
        }
        ue3::Function* const retouch = gTriggerRetouch.Get();
        if (retouch == nullptr) {
            return std::string("mevr stream state: SFXGame.BioTriggerStream.Retouch not found: nothing done");
        }
        gWorldStateEnabled.WriteBool(worldState, on);
        write_journal::Wrote("ConsoleRequest.streamState.world", static_cast<std::uint8_t*>(worldState) + gWorldStateEnabled.Get()->offset, 4);
        int retouched = 0;
        for (ue3::Object* const trigger : triggers) {
            ue3::Params parms(r, retouch);
            retouched += ue3::runtime::Call(trigger, retouch, parms) ? 1 : 0;
        }
        called = retouched > 0;
        route = std::format("the world's entry written and {} of {} volumes' Retouch called [verify]", retouched, triggers.size());
    }
    // Looked up again: the call may have appended the world's entry (its array moved).
    void* const afterState = WorldStateNamed(world, *state);
    std::optional<bool> const after = afterState != nullptr ? gWorldStateEnabled.ReadBool(afterState) : std::nullopt;
    std::string const text = std::format("stream state: {} {} through {}: called {}; the world's state was {}, is {} (mevr stream chunks shows the levels)", name,
        on ? "on" : "off", route, called, Tri(before), Tri(after));
    log::Info("{}", text);
    return text;
}

std::string NameList(ue3::FieldRef& field, void const* state) {
    std::string out;
    if (auto const names = field.ElementsOf(state)) {
        for (std::int32_t j = 0; j < names->count; ++j) {
            if (auto const* name = static_cast<ue3::Name const*>(names->At(j))) {
                out += (out.empty() ? "" : ",") + ue3::runtime::NameText(*name);
            }
        }
    }
    return out;
}

std::string Streams(ue3::Object* pawn, std::istringstream& words) {
    std::string verb;
    words >> verb;
    std::vector<ue3::Object*> const triggers = TriggerStreams();
    ue3::Reflection& r = ue3::runtime::Get();
    if (verb == "list") {
        std::string filter;
        words >> filter;
        auto lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        std::string const want = lower(filter);
        std::string out = std::format("{} BioTriggerStream volumes{}\n", triggers.size(), filter.empty() ? "" : std::format(" (showing those matching '{}')", filter));
        for (std::size_t i = 0; i < triggers.size(); ++i) {
            ue3::Object* const t = triggers[i];
            std::string text = std::format("[{}] {}", i, r.NameText(t));
            if (auto const at = player::Location(t)) {
                text += std::format(" at {:.0f} {:.0f} {:.0f}", at->x, at->y, at->z);
            }
            if (auto const states = gTriggerStates.ElementsOf(t)) {
                for (std::int32_t s = 0; s < states->count; ++s) {
                    void const* const state = states->At(s);
                    auto const name = gStateName.Read<ue3::Name>(state);
                    auto const in = gStateInChunk.Read<ue3::Name>(state);
                    text += std::format("\n    state {} (in {}): visible {}; load {}", name ? ue3::runtime::NameText(*name) : "?", in ? ue3::runtime::NameText(*in) : "?",
                        NameList(gStateVisible, state), NameList(gStateLoad, state));
                }
            }
            if (want.empty() || lower(text).find(want) != std::string::npos) {
                out += text + "\n";
            }
        }
        return out;
    }
    if (verb == "touch") {
        std::string which;
        words >> which;
        ue3::Object* trigger = nullptr;
        for (std::size_t i = 0; i < triggers.size() && trigger == nullptr; ++i) {
            if (which == std::to_string(i) || which == r.NameText(triggers[i])) {
                trigger = triggers[i];
            }
        }
        if (trigger == nullptr) {
            return std::format("mevr stream touch: no volume '{}' among {} (mevr stream list)", which, triggers.size());
        }
        ue3::Function* const touch = gTriggerTouch.Get();
        if (touch == nullptr) {
            return std::string("(SFXGame.BioTriggerStream.Touch not found)");
        }
        ue3::Params parms(r, touch);
        parms.Set("Other", pawn);
        parms.Set("HitLocation", player::Location(pawn).value_or(ue3::Vector{}));
        bool const called = ue3::runtime::Call(trigger, touch, parms);  // its script applies the streaming state
        std::optional<ue3::Vector> const at = player::Location(trigger);
        return std::format("{}.Touch(the player's pawn): called {}; the volume is at {:.0f} {:.0f} {:.0f} (`mevr vehicle levels` shows the streaming)", r.NameText(trigger),
            called, at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f);
    }
    if (verb == "chunks") {
        return StreamChunks(pawn);
    }
    if (verb == "people") {
        std::string package;
        words >> package;
        return StreamPeople(pawn, package);
    }
    if (verb == "show") {
        std::string package, value;
        words >> package >> value;
        return StreamShow(pawn, package, value);
    }
    if (verb == "states") {
        return StreamStates(pawn);
    }
    if (verb == "state") {
        std::string name, value;
        words >> name >> value;
        return StreamState(pawn, name, value, triggers);
    }
    return std::string("usage: mevr stream list [filter] | mevr stream touch <index|name> | mevr stream chunks | mevr stream show <package> [on|off] | mevr stream people <package> | mevr stream states | mevr stream state <StateName> on|off");
}

std::vector<ue3::Object*> gFound;  // `mevr find`'s list, for `use` and `activate`

// An object's C++ vtable and the function at a byte offset in it, read under SEH (`mevr vslot`: a hand-typed offset).
bool ReadVtableSlot(void const* object, std::size_t offset, std::uintptr_t& vtable, std::uintptr_t& fn) {
    __try {
        void* const* const table = *static_cast<void* const* const*>(object);
        vtable = reinterpret_cast<std::uintptr_t>(table);
        fn = reinterpret_cast<std::uintptr_t>(table[offset / sizeof(void*)]);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
ue3::ClassRef gActorClassRef{"Engine.Actor"};
ue3::FieldRef gEventOriginator{"Engine.SequenceEvent", "Originator"};

std::optional<std::string> RunOurs(ue3::Object* pc, std::string const& line) {
    if (line.rfind("mevr ", 0) != 0) {
        return std::nullopt;
    }
    std::istringstream words(line.substr(5));
    std::string verb;
    words >> verb;
    ue3::Object* const pawn = player::Pawn(pc);
    if (pawn == nullptr) {
        return std::string("(no pawn)");
    }
    char text[160]{};
    if (verb == "loc") {
        std::optional<ue3::Vector> const at = player::Location(pawn);
        std::optional<ue3::Rotator> const rot = player::Rotation(pawn);
        if (!at || !rot) {
            return std::string("(the pawn's Location or Rotation can't be read)");
        }
        std::snprintf(text, sizeof(text), "loc %.1f %.1f %.1f rot %d %d %d", at->x, at->y, at->z, rot->pitch, rot->yaw, rot->roll);
        return std::string(text);
    }
    if (verb == "galaxy") {
        // `mevr galaxy dump [tag]`: the open galaxy map's model to %LOCALAPPDATA%\MEVR\galaxy (research/51 section 8).
        std::string sub;
        words >> sub;
        if (sub == "dump") {
            std::string tag;
            words >> tag;
            return galaxy_dump::Dump(tag);
        }
        if (sub == "click") {
            float u = -1.0f, v = -1.0f;
            words >> u >> v;
            return galaxy_dump::Click(u, v);
        }
        if (sub == "view") {
            // `mevr galaxy view <yawDeg> <pitchDeg> <distanceUnits>` | `mevr galaxy view off`
            std::string first;
            words >> first;
            if (first == "off" || first.empty()) {
                return galaxy_map::ViewOverride(false, 0.0f, 0.0f, 0.0f);
            }
            float pitch = -45.0f, distance = 8000.0f;
            words >> pitch >> distance;
            return galaxy_map::ViewOverride(true, static_cast<float>(ini::Number(first).value_or(0.0)), pitch, distance);
        }
        if (sub == "hold") {
            int on = 1;
            words >> on;
            return galaxy_map::HoldWrites(on != 0);
        }
        if (sub == "flash") {
            int visible = 1;
            words >> visible;
            return galaxy_map::ShowFlash(visible != 0);
        }
        if (sub == "press") {
            float u = -1.0f, v = -1.0f;
            int holdMs = 300, moves = 0, preMs = 0;
            words >> u >> v >> holdMs >> moves >> preMs;
            return galaxy_dump::Press(u, v, holdMs, moves != 0, preMs);
        }
        if (sub == "pick") {
            int index = -1;
            words >> index;
            return galaxy_dump::Pick(index, true);
        }
        if (sub == "where") {
            return galaxy_dump::Pick(-1, false);
        }
        if (sub == "use") {
            return galaxy_map::UseConsole();
        }
        if (sub == "aim") {
            int index = -1;
            words >> index;
            return galaxy_dump::Aim(index);
        }
        if (sub == "invoke" || sub == "var") {
            std::string arg;
            words >> arg;
            return sub == "invoke" ? galaxy_dump::Invoke(arg) : galaxy_dump::Variable(arg);
        }
        if (sub == "movie") {
            return galaxy_dump::Movie();
        }
        if (sub == "trace") {
            std::string state;
            words >> state;
            return galaxy_dump::Trace(state != "off");
        }
        if (sub == "input") {
            int event = -1;
            float value = 0.0f;
            words >> event >> value;
            return galaxy_dump::Input(event, value);
        }
        return std::string("usage: mevr galaxy dump [tag] | click <u> <v> | pick <i> | where | trace on|off | input <event> <value>");
    }
    if (verb == "findplayer") {
        // `mevr findplayer [pawn|behavior|controller]`: the player's own object as `mevr find`'s [0], at once (find names every
        // object in the game and took longer than the console's 10 s; research/38 "Linear walking": `mevr props 0`,
        // `mevr setprop 0 <field> <value>` on the pawn behaviour's walk values).
        std::string which;
        words >> which;
        ue3::Reflection& r = ue3::runtime::Get();
        ue3::Object* const playerController = player::Controller();
        ue3::Object* const playerPawn = playerController != nullptr ? player::BioPawn(playerController) : nullptr;
        ue3::Object* found = which == "controller" ? playerController : playerPawn;
        if (which == "behavior" && playerPawn != nullptr) {
            auto const info = r.PropertyOf(r.ClassOf(playerPawn), "m_oBehavior");
            found = nullptr;
            if (info) {
                std::memcpy(&found, reinterpret_cast<std::uint8_t const*>(playerPawn) + info->offset, sizeof found);
            }
        }
        gFound.clear();
        if (found == nullptr || !r.IsAlive(found)) {
            return std::string("mevr findplayer: no such object (usage: mevr findplayer [pawn|behavior|controller])");
        }
        gFound.push_back(found);
        return std::format("1 found\n[0] {}", r.FullName(found));
    }
    if (verb == "find") {
        // `mevr find <text>`: live objects whose full name holds the text (case-insensitive), numbered for `use` and `activate`
        // (finding the galaxy map's usable object on the Normandy, research/51).
        std::string pattern;
        words >> pattern;
        if (pattern.empty()) {
            return std::string("usage: mevr find <text>");
        }
        auto const lower = [](std::string v) {
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return v;
        };
        std::string const want = lower(pattern);
        ue3::Reflection& r = ue3::runtime::Get();
        gFound.clear();
        std::string out;
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n && gFound.size() < 40; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !r.IsAlive(o)) {
                continue;
            }
            std::string const name = r.FullName(o);
            if (lower(name).find(want) == std::string::npos || name.find("Default__") != std::string::npos) {
                continue;
            }
            out += std::format("[{}] {}", gFound.size(), name);
            if (auto const at = player::Location(o); at && ue3::IsA(o, gActorClassRef)) {
                out += std::format(" at {:.0f} {:.0f} {:.0f}", at->x, at->y, at->z);
            }
            out += "\n";
            gFound.push_back(o);
        }
        return std::format("{} found{}\n{}", gFound.size(), gFound.size() == 40 ? " (the first 40)" : "", out);
    }
    if (verb == "loot") {
        std::size_t index = 0;
        words >> index;
        if (index >= gFound.size() || !ue3::runtime::IsAlive(gFound[index])) {
            return std::format("mevr loot: no object {} (mevr find first)", index);
        }
        return galaxy_dump::ShowLoot(pc, gFound[index]);
    }
    if (verb == "lootcorpse") {
        std::size_t index = 0;
        words >> index;
        if (index >= gFound.size() || !ue3::runtime::IsAlive(gFound[index])) {
            return std::format("mevr lootcorpse: no object {} (mevr find first)", index);
        }
        return galaxy_dump::LootCorpse(pc, gFound[index]);
    }
    if (verb == "skillgame") {
        // `mevr skillgame start [type 0|1] [difficulty 0..2] [n]` (n: the (n-1)-th `find` object as the dependent, the plain
        // InitializeSkillGame), `mevr skillgame state`, `mevr skillgame gel`, `mevr skillgame decrypt|electronics [value]` (Shepard's
        // skill shown, or raised in memory for a test).
        std::string sub;
        words >> sub;
        int a = 0, b = 0, c = 0;
        words >> a >> b >> c;
        ue3::Object* dependent = nullptr;
        if (sub == "start" && c > 0) {
            std::size_t const index = static_cast<std::size_t>(c - 1);
            if (index >= gFound.size() || !ue3::runtime::IsAlive(gFound[index])) {
                return std::format("mevr skillgame: no object {} (mevr find first; the dependent is given as n + 1)", index);
            }
            dependent = gFound[index];
        }
        return galaxy_dump::SkillGame(pc, pawn, sub.empty() ? "state" : sub, a, b, dependent);
    }
    if (verb == "gunner") {
        // `mevr gunner`: the Mako's gunner (research/47 "The gunner"): who aims, its target, the last scan's candidates.
        // `mevr gunner wake <n>` (tests): the n-th of `find` / `instances` (a dormant enemy) shown and given collision.
        std::string sub;
        words >> sub;
        if (sub == "wake") {
            std::size_t index = 0;
            words >> index;
            if (index >= gFound.size()) {
                return "usage: mevr gunner wake <n> (mevr instances BioPawn <name> first)";
            }
            return mako_gunner::Wake(gFound[index]);
        }
        return mako_gunner::StatusJson();
    }
    if (verb == "cover") {
        // `mevr cover [test]`: VR cover (research/23 section 6): its status, or every awake hostile judged now, as a hit from
        // it would be (the head hidden from it by cover within reach, and whether the game's own cone would cut it).
        std::string rest;
        std::getline(words, rest);
        return vr_cover::Console(rest);
    }
    if (verb == "powerwheel") {
        // `mevr powerwheel [open|hand x y|pick n|equip|close|roster]`: the power wheel at the left hand (research/55) without
        // tracked controllers: X held, the hand's offset from its centre (metres), a tile picked, X let go (equips), closed.
        std::string rest;
        std::getline(words, rest);
        return power_wheel::Console(rest);
    }
    if (verb == "melee") {
        // `mevr melee punch|strike`: the right hand's melee now, through the gesture's rules (research/20 "Gesture only"):
        // punch = a fist (with the weapon away, the pistol whip power), strike = an open hand. No tracked controllers needed.
        std::string rest;
        std::getline(words, rest);
        return throwables::Console(rest);
    }
    if (verb == "say") {
        // `mevr say <words>`: the words as a recognizer would give them (research/54), through the rule parser, the squad
        // context and the dispatcher: the typed stand-in for speech. The outcome per clause.
        std::string rest;
        std::getline(words, rest);
        while (!rest.empty() && rest.front() == ' ') {
            rest.erase(rest.begin());
        }
        if (rest.empty()) {
            return std::string("usage: mevr say <words> (e.g. mevr say Garrus, take out those troops on the bridge)");
        }
        return squad_orders::Say(rest);
    }
    if (verb == "voice") {
        // `mevr voice wav <path> | listen <seconds> | keyterms <a,b> | stop | status | say <words>`: the host's recognizer
        // (research/54) fed a WAV as one listen (its lines come back through the same path as the microphone's), told to
        // listen from the microphone, given key terms, or asked how it is; `say` is a typed transcript on the whole path (the
        // rules, then the model when they're not sure; `mevr say` alone stops at the rules).
        std::string rest;
        std::getline(words, rest);
        return voice::Console(rest);
    }
    if (verb == "place") {
        // `mevr place aim <pitchDeg> [yawDeg] | release | status`: squad placement's right B held from the head (no tracked
        // controllers needed), the arc at that pitch and that yaw from the look; release lets go (the order is the log's line).
        std::string rest;
        std::getline(words, rest);
        return squad_place::Console(rest);
    }
    if (verb == "order") {
        // `mevr order WHO|WEAPON|POWER|ORDER|TARGET`: an action line (research/54) validated against the squad context and
        // given through the same dispatcher as the gesture, e.g. `ALL|-|-|attack|E1`, `G|-|-|follow|-`, `W|-|-|move|E2`.
        std::string actionLine;
        words >> actionLine;
        auto const action = vr::squad_context::ParseLine(actionLine);
        if (!action) {
            return std::string("usage: mevr order WHO|WEAPON|POWER|ORDER|TARGET (ALL|-|-|attack|E1; G|-|-|follow|-; W|-|-|move|E2)");
        }
        return squad_orders::Give(*action);
    }
    if (verb == "context") {
        // `mevr context [word [secondsAgo]]`: the squad context's text block now (research/54), with PT1 "<word>" resolved
        // that many seconds ago (default 0.5) from the pose history; `mevr context prefix`: the cached prefix.
        std::string word;
        std::string agoText;
        words >> word >> agoText;
        if (word == "prefix") {
            return squad_context::Prefix();
        }
        return squad_context::Block("", word, ini::Number(agoText).value_or(0.5));
    }
    if (verb == "calltrace") {
        // `mevr calltrace <seconds> <word> [-word...]` | `mevr calltrace off`: every script call and ProcessEvent whose
        // function's path holds a word and no -word (case-insensitive), with its caller, to the log for that long
        // (game/call_trace).
        std::string first;
        words >> first;
        if (first == "off" || first.empty()) {
            return call_trace::Stop();
        }
        std::optional<double> const seconds = ini::Number(first);
        if (!seconds) {
            return "usage: mevr calltrace <seconds> <word> [word...] | off";
        }
        std::vector<std::string> filters;
        for (std::string word; words >> word;) {
            filters.push_back(word);
        }
        return call_trace::Start(*seconds, filters);
    }
    if (verb == "omnitool") {
        // `mevr omnitool on [lifetime s] | off [fade s] | state`: the game's omni-tool on Shepard's arm (research/53 section 6).
        std::string rest;
        std::getline(words, rest);
        return omni_tool::Console(rest);
    }
    if (verb == "gui") {
        std::string which;
        words >> which;
        return galaxy_dump::ShowGui(pc, which);
    }
    if (verb == "omni") {
        // `mevr omni <equipment|squad|map|journal|codex|computer|close|holster>`: what the omni-tool rail's entries ask
        // (research/57), without a hand on the rail.
        std::string which;
        words >> which;
        return game_menu_request::Inject(which);
    }
    if (verb == "moment") {
        // `mevr moment`: what the level's script did to the player (a track's walk, a teleport, a black screen; research/69).
        std::string rest;
        std::getline(words, rest);
        return authored_moment::Command(rest);
    }
    if (verb == "thespian") {
        // `mevr thespian [cards | watch on|off | say <words> | pick <position>]`: the live dialogue wheel as script cards, and
        // a reply taken by what a player would say (research/70).
        std::string rest;
        std::getline(words, rest);
        return thespian::Command(rest);
    }
    if (verb == "convo") {
        // `mevr convo [dump | world on|off|screen|vr | pick | skip | end]`: the conversation now (research/59), or how the next one
        // is shown. A level's conversations start from its script: `mevr instances BioSeqAct_StartConversation`, `mevr activate n 0`.
        std::string rest;
        std::getline(words, rest);
        std::string const trimmed = rest.substr(rest.find_first_not_of(' ') == std::string::npos ? rest.size() : rest.find_first_not_of(' '));
        if (trimmed.rfind("flow", 0) == 0) {
            return conversation_flow::Command(trimmed.size() > 4 ? trimmed.substr(4) : std::string());  // `mevr convo flow [list|rescan|restore]`
        }
        return conversation_world::Command(rest);
    }
    if (verb == "voices") {
        // `mevr voices [status | census [seconds] | overheard on|off | gun on|off]`: where sounds come from on foot (research/69 A1,
        // A2); the census lists every sound source's owner, cue and whether the game placed it.
        std::string rest;
        std::getline(words, rest);
        return sound_place::Command(rest);
    }
    if (verb == "body") {
        // `mevr body`: Shepard's drawn dimensions against the player's (research/58, the body's scale).
        std::string rest;
        std::getline(words, rest);
        return body_probe::Command(rest);
    }
    if (verb == "guimouse") {
        // `mevr guimouse`: where the game's Flash cursor is (research/57 section 6).
        std::string rest;
        std::getline(words, rest);
        return gui_cursor::Command(rest);
    }
    if (verb == "uiworld") {
        // `mevr uiworld`: the game's UI world, its camera and our capture of it (research/57 section 7).
        std::string rest;
        std::getline(words, rest);
        return ui_world_figure::Command(rest);
    }
    if (verb == "guikey") {
        // `mevr guikey <event|w> ...` (research/57 section 6): GUI events through GuiKey, one per tick.
        std::string rest;
        std::getline(words, rest);
        return gui_cursor::KeyCommand(rest);
    }
    if (verb == "inv") {
        // `mevr inv [list] | slot <n> | slots | equip <i>` (research/57 section 6: the touch inventory's data path).
        std::string rest;
        std::getline(words, rest);
        return inventory_probe::Command(rest);
    }
    if (verb == "holo") {
        // `mevr holo spawn|effect|scale|move|info|clear` (research/57 section 4: the squad holograms' experiment).
        std::string rest;
        std::getline(words, rest);
        return holo_probe::Command(rest);
    }
    if (verb == "panel") {
        // `mevr panel list` | `mevr panel <tag> show|hide|var <path>|invoke <method>|movie` (research/53).
        std::string tag, sub, arg;
        words >> tag >> sub >> arg;
        return galaxy_dump::Panel(pc, tag, sub.empty() ? "list" : sub, arg);
    }
    if (verb == "instances") {
        // `mevr instances <Package.Class> [text]`: live objects of that class (or a subclass) whose full name holds the text, each
        // with the actor it belongs to and that actor's place; on `find`'s list (for `use`, `props`, `loot`...). Up to 60.
        std::string path, filter;
        words >> path >> filter;
        ue3::Reflection& r = ue3::runtime::Get();
        ue3::Class* const cls = path.empty() ? nullptr : r.FindClass(path);
        if (cls == nullptr) {
            return std::string("usage: mevr instances <Package.Class> [text] (no such class)");
        }
        ue3::Class* const actorClass = r.FindClass("Engine.Actor");
        gFound.clear();
        std::string out;
        int total = 0;
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
                continue;
            }
            std::string const name = r.FullName(o);
            if (name.find("Default__") != std::string::npos || (!filter.empty() && name.find(filter) == std::string::npos)) {
                continue;
            }
            ++total;
            if (gFound.size() >= 60) {
                continue;
            }
            ue3::Object* owner = o;
            for (int depth = 0; owner != nullptr && depth < 8 && !(actorClass != nullptr && r.IsA(owner, actorClass)); ++depth) {
                owner = r.OuterOf(owner);
            }
            out += std::format("\n[{}] {}", gFound.size(), name);
            if (owner != nullptr && owner != o) {
                out += " (of " + r.NameText(owner) + ")";
            }
            if (auto const at = owner != nullptr ? player::Location(owner) : std::nullopt) {
                out += std::format(" at {:.0f} {:.0f} {:.0f}", at->x, at->y, at->z);
            }
            gFound.push_back(o);
        }
        return std::format("{} instances{}", total, out);
    }
    if (verb == "vslot") {
        // `mevr vslot <n> <hex offset>`: for object n of `find`'s list (after `mevr instances`), the function its C++ vtable
        // holds at that byte offset, as exe+RVA (reading a native's virtual call: research/69 "Walking to marks").
        std::size_t index = 0;
        std::string offsetText;
        words >> index >> offsetText;
        if (index >= gFound.size() || offsetText.empty()) {
            return std::string("usage: mevr vslot <n of mevr instances> <hex byte offset>");
        }
        // A kept object may be gone (a level changed since `instances`), and the offset is typed by hand: alive, parsed without
        // throwing, aligned and within a vtable's reach, read under SEH (the Codex review, 2026-11-11).
        std::size_t offset = 0;
        std::string_view const hex = std::string_view(offsetText).substr(offsetText.starts_with("0x") || offsetText.starts_with("0X") ? 2 : 0);
        if (std::from_chars(hex.data(), hex.data() + hex.size(), offset, 16).ec != std::errc{} || offset % sizeof(void*) != 0 || offset >= 0x2000) {
            return std::string("mevr vslot: the offset is a hex byte offset, a multiple of 8, under 0x2000");
        }
        if (!ue3::runtime::IsAlive(gFound[index])) {
            return std::format("mevr vslot: object {} is gone (mevr instances again)", index);
        }
        auto const base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
        std::uintptr_t vtable = 0, fn = 0;
        if (!ReadVtableSlot(gFound[index], offset, vtable, fn)) {
            return std::format("mevr vslot: object {}'s vtable slot +0x{:X} can't be read", index, offset);
        }
        return std::format("{}: vtable exe+0x{:X}, slot +0x{:X} = exe+0x{:X}", ue3::runtime::Get().FullName(gFound[index]),
            vtable - base, offset, fn - base);
    }
    if (verb == "classes") {
        // `mevr classes <text>`: the loaded script classes whose name holds the text (case-insensitive), each with its parent;
        // each class's default object goes on `find`'s list (for `props` and `funcs`). Auditing the game's UIs (research/53).
        std::string pattern;
        words >> pattern;
        if (pattern.empty()) {
            return std::string("usage: mevr classes <text>");
        }
        auto const lower = [](std::string v) {
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return v;
        };
        std::string const want = lower(pattern);
        ue3::Reflection& r = ue3::runtime::Get();
        ue3::Class* const classClass = r.ClassClass();
        std::vector<std::pair<std::string, ue3::Object*>> classes;
        std::map<std::string, ue3::Object*> defaults;
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !r.IsAlive(o)) {
                continue;
            }
            std::string const name = r.NameText(o);
            if (lower(name).find(want) == std::string::npos) {
                continue;
            }
            if (r.ClassOf(o) == classClass) {
                classes.emplace_back(name, o);
            } else if (name.starts_with("Default__")) {
                defaults[name.substr(9)] = o;
            }
        }
        std::sort(classes.begin(), classes.end());
        gFound.clear();
        std::string out = std::format("{} classes", classes.size());
        for (auto const& [name, cls] : classes) {
            if (gFound.size() >= 300) {
                out += "\n(the first 300)";
                break;
            }
            ue3::Struct* const super = r.SuperOf(reinterpret_cast<ue3::Struct const*>(cls));
            auto const d = defaults.find(name);
            out += std::format("\n[{}] {} : {}", gFound.size(), r.PathName(cls), super != nullptr ? r.NameText(super) : "-");
            gFound.push_back(d != defaults.end() ? d->second : cls);
        }
        return out;
    }
    if (verb == "reticles") {
        return screen_reticle::Command();
    }
    if (verb == "funcs") {
        // `mevr funcs <n> <text>`: the n-th of `find`'s functions (its class chain) whose name holds the text, with parameters.
        std::size_t index = 0;
        std::string pattern;
        words >> index >> pattern;
        ue3::Reflection& r = ue3::runtime::Get();
        if (index >= gFound.size() || !r.IsAlive(gFound[index])) {
            return std::format("mevr funcs: no object {} (mevr find first)", index);
        }
        auto const lower = [](std::string v) {
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return v;
        };
        std::string const want = lower(pattern);
        std::string out = r.FullName(gFound[index]) + ":";
        int shown = 0;
        for (ue3::Struct* c = r.ClassOf(gFound[index]); c != nullptr && shown < 50; c = r.SuperOf(c)) {
            for (ue3::Field* f = r.ChildrenOf(c); f != nullptr && shown < 50; f = r.NextOf(f)) {
                if (r.ClassOf(f) != r.FunctionClass() || lower(r.NameText(f)).find(want) == std::string::npos) {
                    continue;
                }
                std::string params;
                for (ue3::ParamSpec const& ps : r.Parameters(reinterpret_cast<ue3::Function*>(f))) {
                    params += (params.empty() ? "" : ", ") + ps.name;
                }
                out += std::format("\n  {}.{}({})", r.NameText(c), r.NameText(f), params);
                ++shown;
            }
        }
        return out;
    }
    if (verb == "origin") {
        // `mevr origin <n>`: the n-th of `find` (a Kismet event) gives its Originator (the actor it belongs to), appended to the list.
        std::size_t index = 0;
        words >> index;
        ue3::Reflection& r = ue3::runtime::Get();
        if (index >= gFound.size() || !r.IsAlive(gFound[index])) {
            return std::format("mevr origin: no object {} (mevr find first)", index);
        }
        ue3::Object* const origin = gEventOriginator.Read<ue3::Object*>(gFound[index]).value_or(nullptr);
        if (origin == nullptr) {
            return std::format("{} has no Originator", r.FullName(gFound[index]));
        }
        gFound.push_back(origin);
        auto const at = player::Location(origin);
        return std::format("[{}] {}{}", gFound.size() - 1, r.FullName(origin), at ? std::format(" at {:.0f} {:.0f} {:.0f}", at->x, at->y, at->z) : std::string());
    }
    if (verb == "used") {
        // `mevr used <n>`: an art placeable's behaviour (the n-th of `find`) told the player's pawn used it
        // (BioArtPlaceableBehavior.OnArtPlaceableUsed: the event its Kismet BioSeqEvt_ArtPlaceableUsed listens for).
        std::size_t index = 0;
        words >> index;
        ue3::Reflection& r = ue3::runtime::Get();
        if (index >= gFound.size() || !r.IsAlive(gFound[index])) {
            return std::format("mevr used: no object {} (mevr find first)", index);
        }
        ue3::Function* const f = r.FindFunction(r.ClassOf(gFound[index]), "OnArtPlaceableUsed");
        if (f == nullptr) {
            return std::format("{} has no OnArtPlaceableUsed", r.FullName(gFound[index]));
        }
        ue3::Params parms(r, f);
        parms.Set("User", pawn);
        bool const called = ue3::runtime::Call(gFound[index], f, parms);
        return std::format("{}.OnArtPlaceableUsed(the player's pawn) called {}", r.FullName(gFound[index]), called);
    }
    if (verb == "spawnpawn") {
        // `mevr spawnpawn <ActorType path> [count 1] [distanceM 8] [spreadM 2] [hostile|ring|hostile,ring]` (the powers QA arena, 2026-11-07): the game's own
        // BioSPGame.SpawnPawn(TheActorType, SpawnLocation, SpawnRotation, bIsPlayer=false) builds a whole NPC from its BioActorType
        // (e.g. BIOA_UNC_C.Mercenary.MIN_MercenaryHumanMaleRocketSoldier), in a row across the player's facing, `distanceM` ahead,
        // each facing the player. Says what each one is: its class, health, whether it's hostile to the player.
        std::string type;
        int count = 1;
        float distanceM = 8.0f, spreadM = 2.0f;
        std::string mode;
        words >> type >> count >> distanceM >> spreadM >> mode;
        ue3::Reflection& r = ue3::runtime::Get();
        auto const objectField = [&](ue3::Object* o, std::string_view field) -> ue3::Object* {
            if (o == nullptr) {
                return nullptr;
            }
            auto const info = r.PropertyOf(r.ClassOf(o), field);
            if (!info) {
                return nullptr;
            }
            ue3::Object* value = nullptr;
            std::memcpy(&value, reinterpret_cast<std::uint8_t const*>(o) + info->offset, sizeof value);
            return value;
        };
        ue3::Object* const game = objectField(objectField(pc, "WorldInfo"), "Game");
        if (type.empty() || game == nullptr) {
            return std::string(game == nullptr ? "mevr spawnpawn: no WorldInfo.Game" : "usage: mevr spawnpawn <ActorType path> [count] [distanceM] [spreadM]");
        }
        ue3::Function* const spawn = r.FindFunction(r.ClassOf(game), "SpawnPawn");
        if (spawn == nullptr) {
            return std::format("{} has no SpawnPawn", r.FullName(game));
        }
        std::optional<ue3::Vector> const at = player::Location(pawn);
        std::optional<ue3::Rotator> const facing = player::Rotation(pawn);
        if (!at || !facing) {
            return std::string("mevr spawnpawn: no player place");
        }
        constexpr double kUnitsPerRad = 32768.0 / 3.14159265358979;
        double const yaw = facing->yaw / kUnitsPerRad;
        double const fx = std::cos(yaw), fy = std::sin(yaw);
        count = std::clamp(count, 1, 40);
        ue3::memory::OwnedString const name(ue3::memory::Widen(type));
        ue3::Function* const hostile = r.FindFunction(r.ClassOf(pawn), "IsHostile");
        // "hostile": the level's first live combat squad (a BioSquadCombat, as UNC20's mercenaries are in).
        ue3::Object* combatSquad = nullptr;
        if (mode.find("hostile") != std::string::npos) {
            for (std::int32_t i = 0, n = r.ObjectCount(); i < n && combatSquad == nullptr; ++i) {
                ue3::Object* const o = r.ObjectAt(i);
                if (o != nullptr && r.IsAlive(o) && r.NameText(r.ClassOf(o)) == "BioSquadCombat" &&
                    r.FullName(o).find("Default__") == std::string::npos) {
                    combatSquad = o;
                }
            }
            if (combatSquad == nullptr) {
                return std::string("mevr spawnpawn: no BioSquadCombat in this level to join");
            }
        }
        std::string out = std::format("mevr spawnpawn {} x{} at {:.1f} m:", type, count, distanceM);
        int made = 0;
        bool const ring = mode.find("ring") != std::string::npos;  // "ring": all around the player, each facing them
        for (int i = 0; i < count; ++i) {
            double const side = (i - (count - 1) / 2.0) * spreadM * 100.0;
            ue3::Vector where{static_cast<float>(at->x + fx * distanceM * 100.0 - fy * side),
                static_cast<float>(at->y + fy * distanceM * 100.0 + fx * side), at->z};
            ue3::Rotator back{0, static_cast<std::int32_t>(facing->yaw + 32768), 0};
            if (ring) {
                double const a = yaw + 2.0 * 3.14159265358979 * (i + 0.5) / count;
                where = {static_cast<float>(at->x + std::cos(a) * distanceM * 100.0), static_cast<float>(at->y + std::sin(a) * distanceM * 100.0), at->z};
                back.yaw = static_cast<std::int32_t>(a * kUnitsPerRad + 32768.0);
            }
            ue3::Params parms(r, spawn);
            parms.Set("TheActorType", name.Get());
            parms.Set("SpawnLocation", where);
            parms.Set("SpawnRotation", back);
            parms.SetBool("bIsPlayer", false);
            bool const called = ue3::runtime::Call(game, spawn, parms);
            ue3::Object* const made1 = parms.Return<ue3::Object*>().value_or(nullptr);
            if (made1 != nullptr) {
                ++made;
                gFound.push_back(made1);
            }
            std::string squadText;
            if (made1 != nullptr && combatSquad != nullptr) {
                ue3::Object* const behavior = objectField(objectField(made1, "m_oBehavior"), "m_oSquadInterface");  // BioPawnSquadInterface
                ue3::Function* const change = behavior != nullptr ? r.FindFunction(r.ClassOf(behavior), "ChangeSquads") : nullptr;
                if (change != nullptr) {
                    ue3::Params c(r, change);
                    c.Set("NewSquad", combatSquad);
                    squadText = std::format(", ChangeSquads({}) {}", r.NameText(combatSquad), ue3::runtime::Call(behavior, change, c));
                } else {
                    squadText = ", no ChangeSquads";
                }
            }
            std::string hostileText = "?";
            if (made1 != nullptr && hostile != nullptr) {
                ue3::Params h(r, hostile);
                h.Set("Other", made1);
                if (ue3::runtime::Call(pawn, hostile, h)) {
                    hostileText = h.ReturnBool().value_or(false) ? "hostile" : "not hostile";
                }
            }
            out += std::format("\n  #{} called {}: {} ({}{})", i, called, made1 != nullptr ? r.FullName(made1) : std::string("nothing"), hostileText,
                squadText);
        }
        out += std::format("\n  {} made; they're appended to the found list (mevr props <n>, mevr call <n> <Function>)", made);
        return out;
    }
    if (verb == "call1") {
        // `mevr call1 <n> <Function> <m>`: the n-th found object's function called with the m-th found object as its first
        // parameter (the rest left zero), e.g. a behaviour's ChangeSquads(squad).
        std::size_t index = 0, arg = 0;
        std::string name;
        words >> index >> name >> arg;
        ue3::Reflection& r = ue3::runtime::Get();
        if (index >= gFound.size() || arg >= gFound.size() || !r.IsAlive(gFound[index]) || !r.IsAlive(gFound[arg]) || name.empty()) {
            return std::string("usage: mevr call1 <n> <Function> <m> (mevr find or instances first)");
        }
        ue3::Function* const f = r.FindFunction(r.ClassOf(gFound[index]), name);
        if (f == nullptr) {
            return std::format("{} has no {}", r.FullName(gFound[index]), name);
        }
        ue3::Params parms(r, f);
        ue3::ParamSpec const* first = nullptr;
        for (ue3::ParamSpec const& spec : parms.Specs()) {
            if (&spec != parms.ReturnSpec() && first == nullptr) {
                first = &spec;
            }
        }
        if (first == nullptr || !parms.Set(first->name, gFound[arg])) {
            return std::format("{}'s first parameter isn't an object", name);
        }
        bool const called = ue3::runtime::Call(gFound[index], f, parms);
        return std::format("{}.{}({}) called {}{}", r.FullName(gFound[index]), name, r.FullName(gFound[arg]), called,
            parms.ReturnBool() ? std::format(", returned {}", *parms.ReturnBool()) : std::string());
    }
    if (verb == "call") {
        // `mevr call <n> <Function>`: a function without parameters (a return value is fine) called on the n-th of `find` /
        // `instances` through ProcessEvent (research: e.g. SFXGameModeDefault.Used, what A does on the current selection).
        std::size_t index = 0;
        std::string name;
        words >> index >> name;
        ue3::Reflection& r = ue3::runtime::Get();
        if (index >= gFound.size() || !r.IsAlive(gFound[index]) || name.empty()) {
            return std::string("usage: mevr call <n> <Function> (mevr find or instances first)");
        }
        ue3::Function* const f = r.FindFunction(r.ClassOf(gFound[index]), name);
        if (f == nullptr) {
            return std::format("{} has no {}", r.FullName(gFound[index]), name);
        }
        ue3::Params parms(r, f);
        for (ue3::ParamSpec const& spec : parms.Specs()) {
            if (&spec != parms.ReturnSpec()) {
                return std::format("{} takes parameters: mevr call only calls functions without them", name);
            }
        }
        bool const called = ue3::runtime::Call(gFound[index], f, parms);
        return std::format("{}.{} called {}{}", r.FullName(gFound[index]), name, called,
            parms.ReturnBool() ? std::format(", returned {}", *parms.ReturnBool()) : std::string());
    }
    if (verb == "use" || verb == "activate") {
        // `mevr use <n>`: the n-th of `find` used by the player's pawn (Actor.UsedBy: what A on it does). `mevr activate <n> [input]`:
        // a Kismet op's input fired (SequenceOp.ForceActivateInput).
        std::size_t index = 0;
        int input = 0;
        words >> index >> input;
        ue3::Reflection& r = ue3::runtime::Get();
        if (index >= gFound.size() || !r.IsAlive(gFound[index])) {
            return std::format("mevr {}: no object {} (mevr find first)", verb, index);
        }
        ue3::Object* const target = gFound[index];
        ue3::Function* const f = r.FindFunction(r.ClassOf(target), verb == "use" ? "UsedBy" : "ForceActivateInput");
        if (f == nullptr) {
            return std::format("{} has no {}", r.FullName(target), verb == "use" ? "UsedBy" : "ForceActivateInput");
        }
        ue3::Params parms(r, f);
        if (verb == "use") {
            parms.Set("User", pawn);
        } else {
            parms.Set("InputIdx", static_cast<std::int32_t>(input));
        }
        bool const called = ue3::runtime::Call(target, f, parms);
        return std::format("{}.{} called {}{}", r.FullName(target), verb == "use" ? "UsedBy(the player's pawn)" : std::format("ForceActivateInput({})", input),
            called, verb == "use" ? std::format(", returned {}", parms.ReturnBool().value_or(false)) : std::string());
    }
    if (verb == "props" || verb == "setprop") {
        // `mevr props <n>`: the n-th of `find`'s properties; `mevr setprop <n> <field> <value>`: one written (research).
        std::size_t index = 0;
        std::string field, value;
        words >> index >> field >> value;
        if (index >= gFound.size() || !ue3::runtime::IsAlive(gFound[index])) {
            return std::format("mevr {}: no object {} (mevr find first)", verb, index);
        }
        return verb == "props" ? galaxy_dump::Props(gFound[index]) : galaxy_dump::SetProperty(gFound[index], field, value);
    }
    if (verb == "hide") {
        // `mevr hide <n> [0|1]`: Actor.SetHidden on the n-th of `find` (1, the default, hides; 0 shows).
        std::size_t index = 0;
        int hide = 1;
        words >> index >> hide;
        ue3::Reflection& r = ue3::runtime::Get();
        if (index >= gFound.size() || !r.IsAlive(gFound[index])) {
            return std::format("mevr hide: no object {} (mevr find first)", index);
        }
        ue3::Function* const f = r.FindFunction(r.ClassOf(gFound[index]), "SetHidden");
        if (f == nullptr) {
            return r.FullName(gFound[index]) + " has no SetHidden";
        }
        ue3::Params parms(r, f);
        parms.SetBool("bNewHidden", hide != 0);
        bool const called = ue3::runtime::Call(gFound[index], f, parms);
        return std::format("{}.SetHidden({}) called {}", r.FullName(gFound[index]), hide != 0, called);
    }
    if (verb == "near") {
        // `mevr near <x> <y> <z> <radius> [text]`: live actors within the radius (units) of a point, nearest first, with their
        // places (finding where things stand, e.g. the CIC's galaxy map platform). Up to 60.
        double x = 0.0, y = 0.0, z = 0.0, radius = 500.0;
        std::string filter;
        words >> x >> y >> z >> radius >> filter;
        ue3::Reflection& r = ue3::runtime::Get();
        std::vector<std::pair<double, std::string>> found;
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !ue3::IsA(o, gActorClassRef) || !r.IsAlive(o)) {
                continue;
            }
            auto const at = player::Location(o);
            if (!at) {
                continue;
            }
            double const d = std::sqrt((at->x - x) * (at->x - x) + (at->y - y) * (at->y - y) + (at->z - z) * (at->z - z));
            std::string const name = r.FullName(o);
            if (d > radius || name.find("Default__") != std::string::npos || (!filter.empty() && name.find(filter) == std::string::npos)) {
                continue;
            }
            found.emplace_back(d, std::format("{:6.0f}  ({:.0f}, {:.0f}, {:.0f})  {}", d, at->x, at->y, at->z, name));
        }
        std::sort(found.begin(), found.end());
        std::string out = std::format("{} actors within {:.0f} of ({:.0f}, {:.0f}, {:.0f})", found.size(), radius, x, y, z);
        for (std::size_t i = 0; i < std::min<std::size_t>(found.size(), 60); ++i) {
            out += "\n" + found[i].second;
        }
        return out;
    }
    if (verb == "setloc") {
        ue3::Vector to{};
        if (!(words >> to.x >> to.y >> to.z)) {
            return std::string("usage: mevr setloc X Y Z");
        }
        ue3::Function* const setLocation = gSetLocationRef.Get();
        if (setLocation == nullptr) {
            return std::string("(Engine.Actor.SetLocation not found)");
        }
        ue3::Params parms(ue3::runtime::Get(), setLocation);
        parms.Set("NewLocation", to);
        ue3::runtime::Call(pawn, setLocation, parms);  // the native through ProcessEvent (game thread: this tick)
        write_journal::Wrote("ConsoleRequest.SetLocation", pawn, 0);
        std::optional<ue3::Vector> const at = player::Location(pawn);
        std::optional<bool> const ok = parms.Return<bool>();
        std::snprintf(text, sizeof(text), "%s returned %s: now %.1f %.1f %.1f", ue3::runtime::Get().FullName(pawn).c_str(),
                      ok ? (*ok ? "true" : "false") : "?", at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f);
        return std::string(text);
    }
    if (verb == "density") {
        float cell = 2000.0f;
        words >> cell;
        return Density(pawn, cell > 1.0f ? cell : 2000.0f);
    }
    if (verb == "crowd") {
        float cell = 1500.0f;
        words >> cell;
        return Crowd(pawn, cell > 1.0f ? cell : 1500.0f);
    }
    if (verb == "stream") {
        return Streams(pawn, words);
    }
    if (verb == "weapon") {
        std::string which;
        words >> which;
#if defined(MEVR_GAME_LE1)
        return holsters::DrawByName(which);  // the holsters' draw (SwitchToWeapon), for the measuring loop's scoped runs
#else
        (void)which;
        return std::string("mevr weapon: LE1 only (the body holsters)");
#endif
    }
    if (verb == "kick") {
        std::string rest;
        std::getline(words, rest);
        return gun_kick::Command(rest);  // synthetic shots for the gun kick (research/24 section 8)
    }
    if (verb == "cockpit") {
        std::string rest;
        std::getline(words, rest);
        return cockpit_probe::Command(rest);  // the in-game cockpit's experiment (research/47)
    }
    if (verb == "shake") {
        std::string rest;
        std::getline(words, rest);
        return camera_shake::Command(rest);  // the game's camera shakes in VR (research/71)
    }
    if (verb == "vent") {
        std::string rest;
        std::getline(words, rest);
        return vent::Command(rest);  // the vent's tests without the controllers: heat, the visible rack (research/45)
    }
    if (verb == "spectator") {
        std::string rest;
        std::getline(words, rest);
        return spectator::Command(rest);  // the spectator camera (research/49)
    }
    if (verb == "vehicle") {
        std::string rest;
        std::getline(words, rest);
        return cockpit_probe::VehicleCommand(rest);  // what a console `open <map>` produced; `enter` gets in
    }
    return std::string("unknown: mevr loc | mevr setloc X Y Z | mevr density [cellUU] | mevr crowd [cellUU] | mevr stream list|touch|chunks|states|state | mevr weapon <pistol|shotgun|assault|sniper> | mevr vent heat|rack|part | mevr kick [recoilDeg] [shots] [ms] | mevr shake [screen <0..1> | view on|off] | mevr cockpit ... | mevr vehicle [enter] | mevr spectator ... | mevr omni <screen> | mevr holo ... | mevr inv ... | mevr guimouse | mevr guikey ... | mevr uiworld");
}

ue3::Function* FindConsoleCommand() {
    return gConsoleCommandRef.Get();
}

// One command; bWriteToLog false, so what it prints comes back as the return value instead of going to the log.
// Both strings are in the engine's memory: ours goes in, the engine's comes back, and the block (ours) frees both.
// Nullopt: it can't be run (no allocator, or the parameters aren't as expected; logged once).
std::optional<std::string> Run(ue3::Object* pc, ue3::Function* function, std::string const& command) {
    if (!ue3::memory::Ready()) {
        if (!gToldNoMemory) {
            gToldNoMemory = true;
            log::Warn("console: the engine's allocator wasn't found (see the 'reflect:' lines): commands aren't run");
        }
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::memory::OwnedString text(ue3::memory::Widen(command));
    if (!text.Valid() || !parms.Set("Command", text.Get()) || !parms.SetBool("bWriteToLog", false) ||
        !parms.Return<ue3::FString>()) {
        if (!gToldParameters) {
            gToldParameters = true;
            log::Warn("console: ConsoleCommand's parameters aren't (Command, bWriteToLog, a string ReturnValue): commands aren't run");
        }
        return std::nullopt;
    }
    ue3::runtime::Call(pc, function, parms);  // the native directly, never a generated wrapper
    ue3::FString returned = parms.Return<ue3::FString>().value_or(ue3::FString{});
    std::string output = ue3::memory::Narrow(returned);
    ue3::memory::FreeString(returned);
    return output;
}

#else

// LE2/LE3: LExSDKv2 until stage 5 (engine/Legacy, research/34).
ue3::Object* PlayerController() {
    return engine::legacy::LocalPlayerController();
}

ue3::Function* FindConsoleCommand() {
    return engine::legacy::FindFunction(kConsoleCommand);
}

std::optional<std::string> Run(ue3::Object* pc, ue3::Function* function, std::string const& command) {
    return engine::legacy::ConsoleCommand(pc, function, command);
}

#endif

// Once, with a player there (so, on LE1, our reflection layer is up).
bool LookUp() {
    if (!gLookedUp) {
        gLookedUp = true;
        gConsoleCommand = FindConsoleCommand();
        if (gConsoleCommand == nullptr) {
            log::Warn("console: Engine.PlayerController.ConsoleCommand not found: console commands are ignored");
        }
    }
    return gConsoleCommand != nullptr;
}

} // namespace

bool RunCommand(std::string const& command, std::string* output) {
    ue3::Object* const pc = PlayerController();
    if (pc == nullptr || !LookUp()) {
        return false;
    }
    std::optional<std::string> const printed = Run(pc, gConsoleCommand, command);
    if (!printed) {
        return false;
    }
    if (output != nullptr) {
        *output = *printed;
    }
    return true;
}

void Tick(float deltaSeconds) {
    gSincePoll += deltaSeconds;
    if (gSincePoll < kPollSeconds) {
        return;
    }
    gSincePoll = 0.0f;
    std::filesystem::path const directory = Directory();
    std::error_code ec;
    if (directory.empty() || !std::filesystem::exists(directory / L"console.request", ec)) {
        return;
    }
    ue3::Object* const pc = PlayerController();
    if (pc == nullptr) {
        return;  // kept until there is a player
    }
    LookUp();
    std::stringstream text;
    {
        std::ifstream in(directory / L"console.request", std::ios::binary);
        text << in.rdbuf();
    }
    std::filesystem::remove(directory / L"console.request", ec);
    std::string result;
    std::string line;
    while (std::getline(text, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        size_t const start = line.find_first_not_of(" \t\xEF\xBB\xBF");  // a BOM from PowerShell
        if (start == std::string::npos) {
            continue;
        }
        line.erase(0, start);
        std::string output;
#if defined(MEVR_GAME_LE1)
        if (std::optional<std::string> ours = RunOurs(pc, line)) {
            output = *ours;
        } else
#endif
        {
            output = gConsoleCommand != nullptr
                ? Run(pc, gConsoleCommand, line).value_or("(ConsoleCommand can't be run: see the log)")
                : std::string("(ConsoleCommand not found)");
        }
        log::Info("console: {} -> {}", line, output.empty() ? std::string("(no output)") : output);
        result += "> " + line + "\n" + output + (output.empty() || output.back() == '\n' ? "" : "\n");
    }
    // Written whole, then renamed, so a reader never sees half of it.
    {
        std::ofstream out(directory / L"console.result.tmp", std::ios::binary | std::ios::trunc);
        out << result;
    }
    std::filesystem::rename(directory / L"console.result.tmp", directory / L"console.result", ec);
}

} // namespace mevr::game::console_request
