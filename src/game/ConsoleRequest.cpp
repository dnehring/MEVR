// SPDX-License-Identifier: MIT
#include "game/ConsoleRequest.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "engine/Legacy.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "game/CallTrace.hpp"
#include "game/CockpitProbe.hpp"
#include "game/MakoGunner.hpp"
#include "game/GalaxyDump.hpp"
#include "game/GalaxyMap.hpp"
#include "game/Holsters.hpp"
#include "game/OmniTool.hpp"
#include "game/Player.hpp"
#include "game/Spectator.hpp"
#include "core/WriteJournal.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <map>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
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

std::vector<ue3::Object*> TriggerStreams() {
    ue3::Reflection& r = ue3::runtime::Get();
    std::vector<ue3::Object*> found;
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o != nullptr && r.IsAlive(o) && ue3::IsA(o, gTriggerStream) && r.NameText(o).rfind("Default__", 0) != 0) {
            found.push_back(o);
        }
    }
    return found;
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
    return std::string("usage: mevr stream list [filter] | mevr stream touch <index|name>");
}

std::vector<ue3::Object*> gFound;  // `mevr find`'s list, for `use` and `activate`
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
    if (verb == "cockpit") {
        std::string rest;
        std::getline(words, rest);
        return cockpit_probe::Command(rest);  // the in-game cockpit's experiment (research/47)
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
    return std::string("unknown: mevr loc | mevr setloc X Y Z | mevr density [cellUU] | mevr crowd [cellUU] | mevr stream list|touch | mevr weapon <pistol|shotgun|assault|sniper> | mevr cockpit ... | mevr vehicle [enter] | mevr spectator ...");
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
