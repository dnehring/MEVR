// SPDX-License-Identifier: MIT
// The squad holograms' experiment (game/HoloProbe.hpp, research/55 section 4).
#include "game/HoloProbe.hpp"

#include "core/Log.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <sstream>
#include <vector>

namespace mevr::game::holo_probe {
namespace {

#if defined(MEVR_GAME_LE1)
ue3::Object* gHolo = nullptr;     // the copy
ue3::Object* gSource = nullptr;   // what it copies

// A script or native function by name on the object's class, through ProcessEvent (opcode natives handled by runtime::Call).
bool CallOn(ue3::Object* target, std::string_view name, std::function<void(ue3::Params&)> const& set,
    std::function<void(ue3::Params&)> const& get = nullptr) {
    if (target == nullptr) {
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Function* const f = r.FindFunction(r.ClassOf(target), name);
    if (f == nullptr) {
        return false;
    }
    ue3::Params p(r, f);
    if (set) {
        set(p);
    }
    ue3::runtime::Call(target, f, p);
    if (get) {
        get(p);
    }
    return true;
}

std::optional<ue3::Vector> LocationOf(ue3::Object* actor) {
    ue3::Reflection& r = ue3::runtime::Get();
    auto const info = actor != nullptr ? r.PropertyOf(r.ClassOf(actor), "Location") : std::nullopt;
    if (!info) {
        return std::nullopt;
    }
    return *reinterpret_cast<ue3::Vector const*>(reinterpret_cast<std::uint8_t const*>(actor) + info->offset);
}

// The live BioPawns other than the player's, nearest first (within 30 m).
std::vector<ue3::Object*> PawnsNear(ue3::Object* player) {
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const bioPawn = r.FindClass("SFXGame.BioPawn");
    auto const at = LocationOf(player);
    std::vector<std::pair<float, ue3::Object*>> found;
    if (bioPawn == nullptr || !at) {
        return {};
    }
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || o == player || o == gHolo || !r.IsAlive(o) || !r.IsA(o, bioPawn) || r.NameText(o).starts_with("Default__")) {
            continue;
        }
        auto const p = LocationOf(o);
        if (!p) {
            continue;
        }
        float const d = std::hypot(p->x - at->x, p->y - at->y, p->z - at->z);
        if (d < 3000.0f) {
            found.emplace_back(d, o);
        }
    }
    std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
    std::vector<ue3::Object*> out;
    for (auto const& f : found) {
        out.push_back(f.second);
    }
    return out;
}

ue3::Vector Ahead(ue3::Object* player, float units) {
    ue3::Vector at = LocationOf(player).value_or(ue3::Vector{});
    ue3::Reflection& r = ue3::runtime::Get();
    auto const info = r.PropertyOf(r.ClassOf(player), "Rotation");
    if (info) {
        auto const* rot = reinterpret_cast<std::int32_t const*>(reinterpret_cast<std::uint8_t const*>(player) + info->offset);  // pitch yaw roll
        float const yaw = static_cast<float>(rot[1]) * 3.14159265f / 32768.0f;
        at.x += std::cos(yaw) * units;
        at.y += std::sin(yaw) * units;
    }
    return at;
}

std::string Spawn(std::istringstream& words) {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    if (pawn == nullptr) {
        return "holo: no player pawn";
    }
    std::string who = "self";
    float scale = 1.0f;
    words >> who >> scale;
    ue3::Object* source = pawn;
    if (who != "self") {
        int const n = std::max(1, std::atoi(who.c_str()));
        std::vector<ue3::Object*> const nearby = PawnsNear(pawn);
        if (static_cast<int>(nearby.size()) < n) {
            return std::format("holo: only {} other pawns within 30 m", nearby.size());
        }
        source = nearby[n - 1];
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Vector const at = Ahead(pawn, 100.0f);
    ue3::Object* spawned = nullptr;
    bool const called = CallOn(controller, "Spawn", [&](ue3::Params& p) {
        p.Set("SpawnClass", r.ClassOf(source));
        p.Set("SpawnLocation", at);
        p.Set("ActorTemplate", source);
        p.SetBool("bNoCollisionFail", true);
    }, [&](ue3::Params& p) { spawned = p.Return<ue3::Object*>().value_or(nullptr); });
    if (spawned == nullptr) {
        return std::format("holo: Spawn({}, template {}) called {}: no actor", r.FullName(r.ClassOf(source)), r.FullName(source), called);
    }
    gHolo = spawned;
    gSource = source;
    std::string report = std::format("holo: spawned {} from {} at ({:.0f}, {:.0f}, {:.0f})", r.FullName(spawned), r.FullName(source), at.x, at.y, at.z);
    bool const col = CallOn(spawned, "SetCollision", [](ue3::Params& p) {
        p.SetBool("NewColActors", false);
        p.SetBool("NewBlockActors", false);
    });
    bool const phys = CallOn(spawned, "SetPhysics", [](ue3::Params& p) { p.Set("NewPhysics", static_cast<std::uint8_t>(0)); });
    bool const sc = scale != 1.0f && CallOn(spawned, "SetDrawScale", [&](ue3::Params& p) { p.Set("NewScale", scale); });
    report += std::format("; SetCollision {}, SetPhysics(None) {}, SetDrawScale({:.2f}) {}", col, phys, scale, sc);
    log::Info("{}", report);
    return report;
}

std::string Effect(std::istringstream& words) {
    if (gHolo == nullptr || !ue3::runtime::IsAlive(gHolo)) {
        return "holo: none (mevr holo spawn first)";
    }
    int type = 30;
    float fraction = 1.0f;
    words >> type >> fraction;
    bool const set = CallOn(gHolo, "SetEffectsMaterialType", [&](ue3::Params& p) { p.Set("EffectsMaterialType", static_cast<std::uint8_t>(type)); });
    bool const frac = CallOn(gHolo, "SetFractionOfEffectsMaterialEnabled", [&](ue3::Params& p) { p.Set("FractionEnabled", fraction); });
    int now = -1;
    CallOn(gHolo, "GetEffectsMaterialType", [](ue3::Params&) {}, [&](ue3::Params& p) { now = p.Return<std::uint8_t>().value_or(255); });
    return std::format("holo: SetEffectsMaterialType({}) {}, SetFractionOfEffectsMaterialEnabled({:.2f}) {}; GetEffectsMaterialType {}", type, set,
        fraction, frac, now);
}

std::string Info() {
    if (gHolo == nullptr || !ue3::runtime::IsAlive(gHolo)) {
        return "holo: none";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    std::string out = std::format("holo: {} (from {})", r.FullName(gHolo), gSource != nullptr ? r.FullName(gSource) : "?");
    for (char const* field : {"Mesh", "m_oHeadMesh", "m_oHairMesh", "m_oHeadGearMesh", "m_oVisorMesh", "m_oFacePlateMesh"}) {
        auto const info = r.PropertyOf(r.ClassOf(gHolo), field);
        if (!info) {
            continue;
        }
        ue3::Object* const c = *reinterpret_cast<ue3::Object* const*>(reinterpret_cast<std::uint8_t const*>(gHolo) + info->offset);
        ue3::Object* mesh = nullptr;
        if (c != nullptr) {
            auto const m = r.PropertyOf(r.ClassOf(c), "SkeletalMesh");
            mesh = m ? *reinterpret_cast<ue3::Object* const*>(reinterpret_cast<std::uint8_t const*>(c) + m->offset) : nullptr;
        }
        out += std::format("; {} {} ({})", field, c != nullptr ? r.NameText(c) : "none", mesh != nullptr ? r.FullName(mesh) : "no mesh");
    }
    return out;
}
#endif

} // namespace

std::string Command(std::string const& rest) {
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        return "holo: the reflection layer isn't ready";
    }
    std::istringstream words(rest);
    std::string verb;
    words >> verb;
    if (verb == "spawn") {
        return Spawn(words);
    }
    if (verb == "effect") {
        return Effect(words);
    }
    if (verb == "info") {
        return Info();
    }
    if (gHolo == nullptr || !ue3::runtime::IsAlive(gHolo)) {
        return "holo: none (mevr holo spawn [self|n] [scale] first)";
    }
    if (verb == "scale") {
        float s = 1.0f;
        words >> s;
        bool const ok = CallOn(gHolo, "SetDrawScale", [&](ue3::Params& p) { p.Set("NewScale", s); });
        return std::format("holo: SetDrawScale({:.3f}) {}", s, ok);
    }
    if (verb == "move") {
        float x = 0, y = 0, z = 0;
        words >> x >> y >> z;
        ue3::Object* const pawn = player::BioPawn(player::Controller());
        ue3::Vector at = LocationOf(pawn).value_or(ue3::Vector{});
        at.x += x;
        at.y += y;
        at.z += z;
        bool const ok = CallOn(gHolo, "SetLocation", [&](ue3::Params& p) { p.Set("NewLocation", at); });
        return std::format("holo: SetLocation({:.0f}, {:.0f}, {:.0f}) {}", at.x, at.y, at.z, ok);
    }
    if (verb == "clear") {
        bool const ok = CallOn(gHolo, "Destroy", nullptr);
        gHolo = nullptr;
        gSource = nullptr;
        return std::format("holo: Destroy {}", ok);
    }
    return "usage: mevr holo spawn [self|n] [scale] | effect <type> [fraction] | scale <s> | move <x> <y> <z> | info | clear";
#else
    (void)rest;
    return "holo: LE1 only";
#endif
}

void Shutdown() {
#if defined(MEVR_GAME_LE1)
    if (gHolo != nullptr && ue3::runtime::Ready() && ue3::runtime::IsAlive(gHolo)) {
        CallOn(gHolo, "Destroy", nullptr);
    }
    gHolo = nullptr;
    gSource = nullptr;
#endif
}

} // namespace mevr::game::holo_probe
