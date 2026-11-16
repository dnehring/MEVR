// SPDX-License-Identifier: MIT
#include "game/Le3GalaxyRetention.hpp"

#if defined(MEVR_GAME_LE3)
#include "game/Le3GalaxyProjector.hpp"
#include "core/Hook.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/Addresses.hpp"
#include <Windows.h>
#include <atomic>

namespace mevr::game::le3_galaxy_retention {
namespace {
using namespace le3_galaxy;
constexpr char kHook[] = "UBioTierManager::ReevaluateStreaming";
using Reevaluate = void(ue3::Object*, int);
Reevaluate* original = nullptr;
bool installed = false;
std::atomic<unsigned> inFlight{0};
ue3::ClassRef managerClass{"SFXGame.BioTierManager", ue3::kEveryGame};
ue3::ClassRef streamingClass{"Engine.LevelStreaming", ue3::kEveryGame};
ue3::FieldRef reevaluate{"SFXGame.BioTierManager", "bReevaluateStreaming", ue3::kEveryGame};
ue3::FieldRef levels{"Engine.WorldInfo", "StreamingLevels", ue3::kEveryGame};
ue3::FieldRef shouldLoad{"Engine.LevelStreaming", "bShouldBeLoaded", ue3::kEveryGame};
ue3::FieldRef shouldShow{"Engine.LevelStreaming", "bShouldBeVisible", ue3::kEveryGame};

struct Token {
    ue3::Object* object{};
    int index{-1};
    static Token Of(ue3::Object* o) { return {o, ue3::runtime::Get().IndexOf(o)}; }
    bool Alive() const { return ue3::runtime::IsAlive(object) && ue3::runtime::Get().IndexOf(object) == index; }
};
struct Lease {
    Token pc, pawn, world, level;
    DWORD thread{};
    ULONGLONG deadline{};
    bool mapSeen{}, owned{}, nativeLoad{}, nativeShow{};
} lease;
unsigned holds = 0, rebuilds = 0;
std::string last = "not-armed";

ue3::Object* CicLevel(ue3::Object* world) {
    auto const list = levels.ElementsOf(world, 4096);
    if (!list || list->stride != sizeof(ue3::Object*)) return nullptr;
    ue3::Object* found = nullptr;
    for (int i = 0; i < list->count; ++i) {
        auto* const o = *static_cast<ue3::Object**>(list->At(i));
        if (!ue3::runtime::IsAlive(o) || !ue3::IsA(o, streamingClass)) continue;
        auto const name = Read<ue3::Name>(o, "Engine.LevelStreaming", "PackageName");
        if (name && ini::Lower(ue3::runtime::NameText(*name)) == "bioa_nor_210cic") {
            if (found) return nullptr;
            found = o;
        }
    }
    return found;
}
bool SameWorld() {
    return lease.world.Alive() && lease.pc.Alive() && lease.pawn.Alive() &&
        player::Controller() == lease.pc.object && player::Pawn(lease.pc.object) == lease.pawn.object &&
        Object(lease.pc.object, "Engine.Actor", "WorldInfo") == lease.world.object &&
        Object(lease.pawn.object, "Engine.Actor", "WorldInfo") == lease.world.object;
}
bool SameLevel() {
    return SameWorld() && lease.level.Alive() && CicLevel(lease.world.object) == lease.level.object;
}
bool Write(ue3::FieldRef& field, bool value) {
    if (!field.WriteBool(lease.level.object, value)) return false;
    write_journal::Wrote("Le3GalaxyRetention.request", field.Address(lease.level.object), 4);
    return true;
}
void Release(char const* reason, bool nativeRebuilt = false) {
    // A completed native rebuild owns fresh requests. Never restore the previous
    // map's requests over the new room/mission decision made by that rebuild.
    if (lease.owned && !nativeRebuilt && SameLevel()) {
        if (shouldLoad.ReadBool(lease.level.object) == true) Write(shouldLoad, lease.nativeLoad);
        if (shouldShow.ReadBool(lease.level.object) == true) Write(shouldShow, lease.nativeShow);
    }
    if (lease.deadline) log::Info("galaxy retention: released ({})", reason);
    lease = {};
    last = reason;
}
void AfterReevaluate(bool nativeRebuilt) {
    if (!lease.deadline || GetCurrentThreadId() != lease.thread) return;
    if (!SameLevel()) { Release("world-player-or-level-changed"); return; }
    auto const mode = player::GameMode(lease.pc.object);
    if (!mode || GetTickCount64() >= lease.deadline) {
        Release("expired-or-no-mode", nativeRebuilt); return;
    }
    bool const map = engine::adapter::IsMapMode(*mode);
    if (lease.mapSeen && !map) { Release("left-map", nativeRebuilt); return; }
    // Wait up to 20 seconds for normal console entry; no camera or story action
    // is initiated here. Once entered, the diagnostic lasts at most 120 seconds.
    if (!map) return;
    if (!lease.mapSeen) {
        lease.mapSeen = true;
        lease.deadline = GetTickCount64() + 120000;
    }
    if (nativeRebuilt) { lease.owned = false; ++rebuilds; }
    if (!lease.owned) {
        auto const load = shouldLoad.ReadBool(lease.level.object);
        auto const show = shouldShow.ReadBool(lease.level.object);
        if (!load || !show) { Release("request-fields-unavailable"); return; }
        lease.nativeLoad = *load; lease.nativeShow = *show;
        // No writes if the native engine already wants the room.
        if (*load && *show) { last = "native-room-retained"; return; }
        lease.owned = true;
        if (!Write(shouldLoad, true) || !Write(shouldShow, true)) {
            Release("request-write-failed"); return;
        }
        ++holds;
        last = "cic-art-requested";
        log::Info("galaxy retention: CIC art requested after native streaming evaluation (load {}, show {})",
            lease.nativeLoad, lease.nativeShow);
    }
}
// All reflection/lease work stays on the arming command's game thread. A bad
// diagnostic must never suppress the native evaluator or fault the engine.
int WillRebuild(ue3::Object* tierManager, int force) {
    __try {
        if (!lease.deadline || GetCurrentThreadId() != lease.thread ||
            !ue3::runtime::IsAlive(tierManager) || !ue3::IsA(tierManager, managerClass)) return -1;
        return force != 0 || reevaluate.ReadBool(tierManager).value_or(false) ? 1 : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
void GuardAfter(bool rebuilt) {
    __try { AfterReevaluate(rebuilt); }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Native streaming still runs on every subsequent call. Disarm without
        // touching uncertain objects; a later native rebuild replaces requests.
        lease = {};
    }
}
void Detour(ue3::Object* tierManager, int force) {
    inFlight.fetch_add(1, std::memory_order_acquire);
    __try {
        int const rebuilt = WillRebuild(tierManager, force);
        original(tierManager, force);
        if (rebuilt >= 0) GuardAfter(rebuilt != 0);
    } __finally { inFlight.fetch_sub(1, std::memory_order_release); }
}
}
void Startup(hook::Context& hooks) {
    if (installed || !managerClass.Get() || !reevaluate.Get()) return;
    auto* const at = hooks.ResolveTyped<Reevaluate>(engine::addresses::le3::kReevaluateStreaming);
    DWORD64 imageBase = 0;
    auto const* const entry = at ? RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(at), &imageBase, nullptr) : nullptr;
    if (!entry || imageBase + entry->BeginAddress != reinterpret_cast<DWORD64>(at)) return;
    installed = hook::Install(hooks, kHook, at, &Detour, original);
}
bool Armed() { return lease.deadline != 0 && GetTickCount64() < lease.deadline && SameLevel(); }
bool Active() { return Armed() && lease.mapSeen && engine::adapter::IsMapMode(player::GameMode(lease.pc.object).value_or(255)); }
bool Shutdown(hook::Context& hooks, bool exiting) {
    if (!exiting) Release("shutdown"); else lease = {};
    if (installed && !hook::Uninstall(hooks, kHook)) return false;
    installed = false;
    for (int i = 0; !exiting && inFlight.load() && i < 200; ++i) Sleep(1);
    return inFlight.load() == 0;
}
std::string Command(ue3::Object* pc, std::string const& action) {
    if (action == "off") Release("command-off");
    else if (action == "on") {
        if (lease.deadline) return "galaxy retention: already armed; use off first";
        auto const probe = ObserveProjector(pc);
        if (!installed || !probe.Bool("available") || !probe.Bool("trackingAvailable") || player::GameMode(pc) != 0)
            return "galaxy retention: requires installed hook and a measured on-foot CIC projector";
        auto* const world = Object(pc, "Engine.Actor", "WorldInfo");
        auto* const level = CicLevel(world);
        if (!level || !Object(level, "Engine.LevelStreaming", "LoadedLevel") ||
            ReadBool(level, "Engine.LevelStreaming", "bIsVisible") != true ||
            shouldLoad.ReadBool(level) != true || shouldShow.ReadBool(level) != true)
            return "galaxy retention: CIC art is not natively loaded and visible";
        lease = {Token::Of(pc), Token::Of(player::Pawn(pc)), Token::Of(world), Token::Of(level),
            GetCurrentThreadId(), GetTickCount64() + 20000};
        holds = rebuilds = 0; last = "armed-waiting-for-native-map-entry";
    } else if (!action.empty() && action != "state") return "usage: mevr galaxy retain on|off|state";
    auto out = json::Value::Object();
    out["installed"] = installed; out["armed"] = lease.deadline != 0;
    out["mapSeen"] = lease.mapSeen; out["ownsRequests"] = lease.owned;
    out["holds"] = static_cast<int>(holds); out["rebuilds"] = static_cast<int>(rebuilds);
    out["reason"] = last; out["presentationVerified"] = false;
    return "galaxy retention: " + json::Write(out, 0);
}
}
#else
namespace mevr::game::le3_galaxy_retention {
void Startup(hook::Context&) {}
bool Armed() { return false; }
bool Active() { return false; }
bool Shutdown(hook::Context&, bool) { return true; }
std::string Command(ue3::Object*, std::string const&) { return "galaxy retention: LE3 only"; }
}
#endif
