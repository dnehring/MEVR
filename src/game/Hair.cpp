// SPDX-License-Identifier: MIT
#include "game/Hair.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/ik/HairChain.hpp"
#include "core/ik/HandContact.hpp"
#include "core/vr/SettingsFile.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/HairPose.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace mevr::game::hair {
namespace {

namespace hc = ik::hand_contact;
namespace hp = engine::hair_pose;

// Our reflection layer (research/34); engine/ReflectCheck has a row for each.
ue3::FieldRef gHairMesh{"SFXGame.BioPawn", "m_oHairMesh", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden", ue3::kEveryGame};
ue3::FieldRef gHiddenGame{"Engine.PrimitiveComponent", "HiddenGame", ue3::kEveryGame};
ue3::ClassRef gSkeletalMeshClass{"Engine.SkeletalMesh", ue3::kEveryGame};
ue3::FunctionRef gSetSkeletalMesh{"Engine.SkeletalMeshComponent.SetSkeletalMesh", ue3::kEveryGame};
ue3::FunctionRef gDynamicLoadObject{"Core.Object.DynamicLoadObject", ue3::kEveryGame};

constexpr char kBlock[] =
    "\n"
    "[Hair]\n"
    "; Hair that moves (docs/trilogy-hair-plan.md): hairstyles with hair bones sway, swing and settle with the head, stay off\n"
    "; the person's shoulders and are pushed by your hands. ME3's Miranda wears her older rigged hair for it (standIns = 0:\n"
    "; the game's own, which can't move). on = 0 turns it off. rangeM: how far from you people's hair moves; maxPeople: at\n"
    "; most this many, nearest first. stiffness (0..1): how firmly hair returns to its styled shape; damping (0..1): how soon\n"
    "; a swing dies down; inertia (0..1): how much of the head's motion the hair feels; maxBendDeg: how far each joint bends;\n"
    "; gravity (m/s2); radiusM: a strand's thickness for collisions; wind (m/s, along the world's X); hands = 0: your hands\n"
    "; don't touch hair.\n"
    "on = 1\n"
    "rangeM = 6\n"
    "maxPeople = 6\n"
    "stiffness = 0.06\n"
    "damping = 0.10\n"
    "inertia = 1.0\n"
    "maxBendDeg = 50\n"
    "gravity = 9.8\n"
    "radiusM = 0.012\n"
    "wind = 0\n"
    "hands = 1\n"
    "standIns = 1\n";

struct Settings {
    bool on = true;
    double rangeM = 6.0;
    int maxPeople = 6;
    ik::hair::Settings sim;
    double wind = 0.0;
    bool hands = true;
    bool standIns = true;
};
Settings gSettings;
std::optional<bool> gOverride;          // `mevr hair on|off`
std::optional<bool> gStandInOverride;   // `mevr hair standins on|off`
std::uint64_t gSettingsAtMs = 0;
bool gTriedAppend = false;

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsAtMs != 0 && now - gSettingsAtMs < 1000) {
        return;
    }
    gSettingsAtMs = now;
    std::filesystem::path const path = engine::SettingsFile();
    ini::Loaded const loaded = vr::settings_file::Load(path);
    if (loaded.exists && loaded.file.find("Hair") == loaded.file.end() && !gTriedAppend) {
        gTriedAppend = true;
        if (std::string const error = vr::settings_file::Append(path, kBlock); error.empty()) {
            log::Info("hair: added the [Hair] defaults to {}", ini::Utf8(path));
        } else {
            log::Warn("hair: couldn't add the [Hair] defaults to {} ({}); they apply", ini::Utf8(path), error);
        }
    }
    auto num = [&](char const* key) { return ini::GetNumber(loaded.file, "Hair", key); };
    Settings s;
    if (auto v = num("on")) s.on = *v != 0.0;
    if (auto v = num("rangeM")) s.rangeM = std::clamp(*v, 0.5, 30.0);
    if (auto v = num("maxPeople")) s.maxPeople = static_cast<int>(std::clamp(*v, 1.0, static_cast<double>(hp::kMaxTargets)));
    if (auto v = num("stiffness")) s.sim.stiffness = std::clamp(*v, 0.0, 1.0);
    if (auto v = num("damping")) s.sim.damping = std::clamp(*v, 0.0, 1.0);
    if (auto v = num("inertia")) s.sim.inertia = std::clamp(*v, 0.0, 1.0);
    if (auto v = num("maxBendDeg")) s.sim.maxBendDeg = std::clamp(*v, 0.0, 170.0);
    if (auto v = num("gravity")) s.sim.gravity = std::clamp(*v, 0.0, 30.0);
    if (auto v = num("radiusM")) s.sim.radiusM = std::clamp(*v, 0.0, 0.1);
    if (auto v = num("wind")) s.wind = std::clamp(*v, -20.0, 20.0);
    if (auto v = num("hands")) s.hands = *v != 0.0;
    if (auto v = num("standIns")) s.standIns = *v != 0.0;
    s.sim.windMS = {s.wind, 0.0, 0.0};
    if (gOverride) s.on = *gOverride;
    if (gStandInOverride) s.standIns = *gStandInOverride;
    static bool logged = false;
    bool const changed = s.on != gSettings.on || s.standIns != gSettings.standIns || s.hands != gSettings.hands ||
                         s.sim.stiffness != gSettings.sim.stiffness || s.sim.damping != gSettings.sim.damping ||
                         s.sim.inertia != gSettings.sim.inertia || s.sim.maxBendDeg != gSettings.sim.maxBendDeg ||
                         s.rangeM != gSettings.rangeM || s.maxPeople != gSettings.maxPeople || s.wind != gSettings.wind;
    if (!logged || changed) {
        logged = true;
        log::Info("hair: {} (people within {:.1f} m, at most {}; stiffness {:.2f}, damping {:.2f}, inertia {:.2f}, bend {:.0f} deg, "
                  "wind {:.1f} m/s; hands {}; stand-ins {})",
            s.on ? "on" : "off", s.rangeM, s.maxPeople, s.sim.stiffness, s.sim.damping, s.sim.inertia, s.sim.maxBendDeg, s.wind,
            s.hands ? "on" : "off", s.standIns ? "on" : "off");
    }
    gSettings = s;
}

std::string Lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// "SkeletalMesh BIOG_X.Y.Z" -> "biog_x.y.z".
std::string PathOf(ue3::Object const* object) {
    if (object == nullptr) {
        return {};
    }
    std::string const full = ue3::runtime::FullName(object);
    std::size_t const space = full.find(' ');
    return Lower(space == std::string::npos ? full : full.substr(space + 1));
}

// Body parts kept off the hair (everything above the hips) and the player's parts that push it (hands and arms), per mesh
// asset.
struct Parts {
    int boneCount = -1;
    std::vector<hc::BonePart> body;
    std::vector<hc::BonePart> arms;
};
std::unordered_map<ue3::Object const*, Parts> gParts;
ue3::Object const* gWorld = nullptr;

Parts const* PartsOf(ue3::Object* mesh) {
    ue3::Object const* const asset = player::SkeletalMesh(mesh);
    int const count = engine::skeleton::RefBoneCount(mesh);
    if (asset == nullptr || count <= 0) {
        return nullptr;
    }
    auto it = gParts.find(asset);
    if (it != gParts.end() && it->second.boneCount == count) {
        return &it->second;
    }
    Parts parts;
    parts.boneCount = count;
    if (auto const skel = engine::skeleton::ReadRefSkeleton(mesh)) {
        for (hc::BonePart const& p : hc::PartsFor(skel->names, skel->parents)) {
            bool const leg = p.part == hc::Part::Thigh || p.part == hc::Part::Shin || p.part == hc::Part::Foot;
            if (!leg) {
                parts.body.push_back(p);
            }
            if (p.part == hc::Part::Hand || p.part == hc::Part::Forearm || p.part == hc::Part::UpperArm) {
                parts.arms.push_back(p);
            }
        }
    }
    return &gParts.insert_or_assign(asset, std::move(parts)).first->second;
}

// ---- Rigged stand-ins (ME3's Miranda) --------------------------------------------------------------------------------

struct Swapped {
    ue3::Object* component = nullptr;
    ue3::Object* original = nullptr;  // the mesh it had
    ue3::Object* standIn = nullptr;
};
std::vector<Swapped> gSwapped;
std::unordered_map<std::string, ue3::Object*> gStandInObjects;  // rigged path -> object (this level)
std::unordered_map<std::string, bool> gStandInMissing;          // logged once per level

ue3::Object* FindLoaded(std::string const& path) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const cls = gSkeletalMeshClass.Get();
    if (cls == nullptr) {
        return nullptr;
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls)) {
            continue;
        }
        if (PathOf(o) == path) {
            return o;
        }
    }
    return nullptr;
}

ue3::Object* LoadMesh(std::string const& path) {
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || gDynamicLoadObject.Get() == nullptr || gSkeletalMeshClass.Get() == nullptr) {
        return nullptr;
    }
    ue3::memory::OwnedString text(ue3::memory::Widen(path));
    if (!text.Valid()) {
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gDynamicLoadObject.Get());
    if (!p.Set("ObjectName", text.Get()) || !p.Set("ObjectClass", gSkeletalMeshClass.Get()) || !p.SetBool("MayFail", true)) {
        return nullptr;
    }
    ue3::runtime::Call(controller, gDynamicLoadObject.Get(), p);  // the engine's own loader, on the game thread
    return p.Return<ue3::Object*>().value_or(nullptr);
}

ue3::Object* StandInFor(std::string const& riggedPath) {
    auto it = gStandInObjects.find(riggedPath);
    if (it != gStandInObjects.end() && ue3::runtime::IsAlive(it->second)) {
        return it->second;
    }
    if (gStandInMissing.contains(riggedPath)) {
        return nullptr;
    }
    ue3::Object* found = FindLoaded(riggedPath);
    char const* how = "loaded with the level";
    if (found == nullptr) {
        found = LoadMesh(riggedPath);
        how = "loaded by DynamicLoadObject";
    }
    if (found == nullptr) {
        gStandInMissing[riggedPath] = true;
        log::Warn("hair: the rigged stand-in {} isn't loaded and couldn't be loaded: the hairstyle stays the game's", riggedPath);
        return nullptr;
    }
    gStandInObjects[riggedPath] = found;
    log::Info("hair: the rigged stand-in {} found ({})", riggedPath, how);
    return found;
}

bool SetMesh(ue3::Object* component, ue3::Object* mesh) {
    if (component == nullptr || mesh == nullptr || gSetSkeletalMesh.Get() == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), gSetSkeletalMesh.Get());
    if (!p.Valid() || !p.Set("NewMesh", mesh)) {
        return false;
    }
    p.SetBool("bKeepSpaceBases", false);
    return ue3::runtime::Call(component, gSetSkeletalMesh.Get(), p);
}

// The stand-in for this component's mesh, if the table has one and it's wanted.
void ApplyStandIn(ue3::Object* component, ue3::Object* asset) {
    if (!gSettings.standIns || asset == nullptr) {
        return;
    }
    for (Swapped const& s : gSwapped) {
        if (s.component == component && s.standIn == asset) {
            return;  // already ours
        }
    }
    std::string const path = PathOf(asset);
    for (engine::adapter::HairStandIn const& entry : engine::adapter::kHairStandIns) {
        if (entry.used == nullptr || Lower(entry.used) != path) {
            continue;
        }
        ue3::Object* const standIn = StandInFor(Lower(entry.rigged));
        if (standIn == nullptr) {
            return;
        }
        if (SetMesh(component, standIn)) {
            gSwapped.push_back({component, asset, standIn});
            log::Info("hair: {:#x} wears {} instead of {} (rigged: its hair can move)", reinterpret_cast<std::uintptr_t>(component),
                entry.rigged, entry.used);
        } else {
            log::Warn("hair: SetSkeletalMesh refused {} on {:#x}", entry.rigged, reinterpret_cast<std::uintptr_t>(component));
        }
        return;
    }
}

void PutBackStandIns(char const* why) {
    int put = 0;
    for (Swapped const& s : gSwapped) {
        if (ue3::runtime::IsAlive(s.component) && ue3::runtime::IsAlive(s.original) && player::SkeletalMesh(s.component) == s.standIn) {
            put += SetMesh(s.component, s.original) ? 1 : 0;
        }
    }
    if (!gSwapped.empty()) {
        log::Info("hair: {} of {} rigged stand-in(s) put back ({})", put, gSwapped.size(), why);
    }
    gSwapped.clear();
}

// ---- The people near ---------------------------------------------------------------------------------------------------

struct Person {
    ue3::Object* pawn = nullptr;
    ue3::Object* hair = nullptr;
    ue3::Object* body = nullptr;
    double distanceM = 0.0;
};
std::vector<Person> gPeople;
std::string gStatus = "not run yet";
int gPublished = 0;

double Distance(ue3::Vector a, ue3::Vector b) {
    return std::hypot(static_cast<double>(a.x - b.x), static_cast<double>(a.y - b.y), static_cast<double>(a.z - b.z));
}

std::vector<Person> FindPeople(ue3::Object* playerPawn, ue3::Vector at, ue3::Object* world) {
    std::vector<Person> found;
    double const range = gSettings.rangeM * engine::kUnitsPerMeter;
    int guard = 0;
    for (ue3::Object* p = gPawnList.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawn.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (!ue3::runtime::Get().IsAlive(p)) {
            break;  // a level torn down under us leaves freed pawns in the list (CLAUDE.md, LivePawns)
        }
        if (p == playerPawn || !player::IsBioPawn(p) || gHidden.ReadBool(p).value_or(true)) {
            continue;
        }
        ue3::Object* const hair = gHairMesh.Read<ue3::Object*>(p).value_or(nullptr);
        if (hair == nullptr || !ue3::runtime::IsAlive(hair) || gHiddenGame.ReadBool(hair).value_or(false)) {
            continue;
        }
        std::optional<ue3::Vector> const there = player::Location(p);
        if (!there) {
            continue;
        }
        double const d = Distance(*there, at);
        if (d <= range) {
            found.push_back({p, hair, player::Mesh(p), d / engine::kUnitsPerMeter});
        }
    }
    std::sort(found.begin(), found.end(), [](Person const& a, Person const& b) { return a.distanceM < b.distanceM; });
    return found;
}

std::string HairBones(ue3::Object* mesh) {
    std::string out;
    if (auto const skel = engine::skeleton::ReadRefSkeleton(mesh)) {
        for (std::size_t i = 0; i < skel->names.size(); ++i) {
            if (Lower(skel->names[i]).find("hair") != std::string::npos) {
                int const parent = skel->parents[i];
                out += std::format("{}{}({}<-{})", out.empty() ? "" : " ", skel->names[i], i,
                    parent >= 0 && static_cast<std::size_t>(parent) < skel->names.size() ? skel->names[static_cast<std::size_t>(parent)] : "-");
            }
        }
    }
    return out.empty() ? "none" : out;
}

void Stop(char const* why) {
    if (gPublished > 0) {
        hp::Clear();
        gPublished = 0;
    }
    gStatus = why;
}

} // namespace

bool Wanted() {
    RefreshSettings();
    return gSettings.on;
}

void Tick(double /*deltaSeconds*/) {
    RefreshSettings();
    if (!gSettings.on) {
        PutBackStandIns("hair movement off");
        Stop(gOverride ? "off (mevr hair off)" : "off ([Hair] on = 0)");
        return;
    }
    if (!gSettings.standIns && !gSwapped.empty()) {
        PutBackStandIns("stand-ins off");
    }
    if (!ue3::runtime::Up()) {
        Stop("standing by: the reflection layer isn't up");
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const playerPawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    ue3::Object* const world = playerPawn != nullptr ? gWorldInfo.Read<ue3::Object*>(playerPawn).value_or(nullptr) : nullptr;
    std::optional<ue3::Vector> const at = playerPawn != nullptr ? player::Location(playerPawn) : std::nullopt;
    if (world == nullptr || !at) {
        Stop("standing by: no player in a world");
        return;
    }
    if (world != gWorld) {
        gWorld = world;  // a new level: its objects may sit where freed ones were
        gParts.clear();
        gSwapped.clear();
        gStandInObjects.clear();
        gStandInMissing.clear();
        hp::Clear();
        gPublished = 0;
    }
    std::vector<Person> people = FindPeople(playerPawn, *at, world);
    if (people.size() > static_cast<std::size_t>(gSettings.maxPeople)) {
        people.resize(static_cast<std::size_t>(gSettings.maxPeople));
    }
    hp::Request request;
    request.settings = gSettings.sim;
    request.unitsPerMeter = engine::kUnitsPerMeter;
    request.playerPushes = gSettings.hands;
    if (gSettings.hands) {
        if (ue3::Object* const mesh = player::Mesh(playerPawn); mesh != nullptr) {
            if (Parts const* parts = PartsOf(mesh)) {
                request.player = mesh;
                request.playerParts = parts->arms;
            }
        }
    }
    for (Person const& person : people) {
        ue3::Object* const asset = player::SkeletalMesh(person.hair);
        ApplyStandIn(person.hair, asset);
        hp::Target t;
        t.hair = person.hair;
        t.asset = player::SkeletalMesh(person.hair);
        t.body = person.body;
        if (person.body != nullptr) {
            if (Parts const* parts = PartsOf(person.body)) {
                t.bodyParts = parts->body;
            }
        }
        request.targets.push_back(std::move(t));
    }
    gPeople = std::move(people);
    gPublished = static_cast<int>(request.targets.size());
    hp::Publish(std::move(request));
    gStatus = gPublished > 0 ? std::format("on: {} people's hair within {:.1f} m", gPublished, gSettings.rangeM)
                             : std::format("on: nobody within {:.1f} m", gSettings.rangeM);
}

std::string Command(std::string const& args) {
    std::istringstream words(args);
    std::string verb;
    words >> verb;
    if (verb == "on" || verb == "off") {
        gOverride = verb == "on";
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("hair movement {} (until auto or a reload)", verb);
    }
    if (verb == "auto") {
        gOverride.reset();
        gStandInOverride.reset();
        gSettingsAtMs = 0;
        RefreshSettings();
        return std::format("hair movement as [Hair] says: {}", gSettings.on ? "on" : "off");
    }
    if (verb == "standins") {
        std::string v;
        words >> v;
        if (v == "on" || v == "off") {
            gStandInOverride = v == "on";
            gSettingsAtMs = 0;
            RefreshSettings();
            return std::format("rigged stand-ins {}", v);
        }
        return "mevr hair standins on|off";
    }
    if (verb == "trace") {
        double seconds = 10.0;
        words >> seconds;
        wchar_t buffer[MAX_PATH]{};
        GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
        SYSTEMTIME st{};
        GetLocalTime(&st);
        std::filesystem::path const file = std::filesystem::path(buffer) / L"MEVR" /
            std::format(L"hair-trace-{:04}{:02}{:02}-{:02}{:02}{:02}.csv", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        hp::Trace(seconds, ini::Utf8(file));
        return std::format("hair trace for {:.0f} s: {}", seconds, ini::Utf8(file));
    }
    if (verb == "kick") {
        int n = -1;
        double x = 0.0, y = 0.0, z = 0.0;
        words >> n >> x >> y >> z;
        if (n < 0 || static_cast<std::size_t>(n) >= gPeople.size()) {
            return "mevr hair kick <person n> <x> <y> <z> (m/s, world)";
        }
        return hp::Kick(gPeople[static_cast<std::size_t>(n)].hair, {x, y, z}) ? std::format("kicked [{}]'s hair at ({}, {}, {}) m/s", n, x, y, z)
                                                                              : "that hair isn't simulated (no hair bones, or not posed yet)";
    }
    if (verb == "mesh") {
        int n = -1;
        std::string path;
        words >> n >> path;
        if (n < 0 || static_cast<std::size_t>(n) >= gPeople.size() || path.empty()) {
            return "mevr hair mesh <person n from `mevr hair people`> <package.group.mesh>";
        }
        ue3::Object* mesh = FindLoaded(Lower(path));
        if (mesh == nullptr) {
            mesh = LoadMesh(path);
        }
        if (mesh == nullptr) {
            return "not loaded and couldn't be loaded: " + path;
        }
        return SetMesh(gPeople[static_cast<std::size_t>(n)].hair, mesh) ? "set " + PathOf(mesh) : "SetSkeletalMesh failed";
    }
    if (verb.empty() || verb == "people" || verb == "status") {
        std::string out = "hair: " + gStatus;
        hp::Stats const stats = hp::GetStats();
        for (std::size_t i = 0; i < gPeople.size(); ++i) {
            Person const& p = gPeople[i];
            ue3::Object* const asset = player::SkeletalMesh(p.hair);
            ue3::Object* const parent = engine::skeleton::PoseParent(p.hair);
            out += std::format("\n  [{}] pawn {:#x} at {:.2f} m: hair {:#x} {} ({} bones), pose parent {}", i,
                reinterpret_cast<std::uintptr_t>(p.pawn), p.distanceM, reinterpret_cast<std::uintptr_t>(p.hair), PathOf(asset),
                engine::skeleton::RefBoneCount(p.hair),
                parent != nullptr ? std::format("{:#x} {}", reinterpret_cast<std::uintptr_t>(parent), PathOf(player::SkeletalMesh(parent)))
                                  : std::string("none"));
            out += "\n      hair bones: " + HairBones(p.hair);
            for (hp::TargetInfo const& t : stats.targets) {
                if (t.hair != p.hair) {
                    continue;
                }
                out += std::format("\n      stage: {} calls, {} written, {} collisions{}", t.calls, t.written, t.collisions,
                    t.lastReject != nullptr ? std::format(", last refusal: {}", t.lastReject) : std::string());
                for (hp::ChainInfo const& c : t.chains) {
                    std::string bones;
                    for (std::string const& b : c.bones) {
                        bones += (bones.empty() ? "" : " > ") + b;
                    }
                    out += std::format("\n      chain {}: tip {:.1f} cm off the animation, {} reset(s)", bones, c.tipOffsetM * 100.0, c.resets);
                }
            }
        }
        if (stats.broken) {
            out += "\n  the stage STOPPED after an access violation";
        }
        return out;
    }
    return "mevr hair [on|off|auto | people | trace <s> | kick <n> <x> <y> <z> | mesh <n> <path> | standins on|off]";
}

std::string StatusJson() {
    hp::Stats const stats = hp::GetStats();
    std::string out = std::format("{{\"on\": {}, \"status\": \"{}\", \"people\": {}, \"standIns\": {}, \"broken\": {}, \"targets\": [",
        gSettings.on ? "true" : "false", gStatus, gPeople.size(), gSwapped.size(), stats.broken ? "true" : "false");
    for (std::size_t i = 0; i < stats.targets.size(); ++i) {
        hp::TargetInfo const& t = stats.targets[i];
        out += std::format("{}{{\"calls\": {}, \"written\": {}, \"collisions\": {}, \"chains\": {}, \"peakTipM\": {:.3f}}}", i == 0 ? "" : ", ",
            t.calls, t.written, t.collisions, t.chains.size(), t.peakTipM);
    }
    out += "]}";
    return out;
}

void LogStats() {
    hp::Stats const stats = hp::GetStats();
    std::string detail;
    for (hp::TargetInfo const& t : stats.targets) {
        if (!t.chains.empty()) {
            detail += std::format("; {:#x}: {} chains, {} written of {} calls, tips up to {:.1f} cm, {} collisions{}",
                reinterpret_cast<std::uintptr_t>(t.hair), t.chains.size(), t.written, t.calls, t.peakTipM * 100.0, t.collisions,
                t.lastReject != nullptr ? std::format(" (last refusal: {})", t.lastReject) : std::string());
        }
    }
    log::Info("    hair: {}; {} stand-in(s){}{}", gStatus, gSwapped.size(), detail, stats.broken ? "; the stage STOPPED" : "");
    hp::ResetPeriod();
}

void Shutdown() {
    if (ue3::runtime::Up()) {
        PutBackStandIns("shutdown");
    }
    hp::Clear();
    gPeople.clear();
    gParts.clear();
    gStandInObjects.clear();
    gStandInMissing.clear();
    gWorld = nullptr;
    gPublished = 0;
}

} // namespace mevr::game::hair
