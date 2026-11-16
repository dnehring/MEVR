// SPDX-License-Identifier: MIT
#pragma once
#if defined(MEVR_GAME_LE3)
#include "core/Json.hpp"
#include "engine/GameAdapter.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Runtime.hpp"
#include "ue3/UnambiguousMethod.hpp"
#include <cmath>
#include <format>

// Native galaxy/orbital adapter, game thread only after reflection validation.
// Presentation and native pad input remain the shared table/pointer/PadMerge path.
namespace mevr::game::le3_galaxy {
inline constexpr char kCamera[] = "SFXGame.BioCameraBehaviorGalaxy";
inline constexpr char kSystem[] = "SFXGame.SFXSystem";
inline constexpr char kOrbital[] = "SFXGame.SFXGameModeOrbital";
inline ue3::ClassRef cameraClass{kCamera, ue3::kEveryGame};
inline ue3::ClassRef galaxyModeClass{"SFXGame.SFXGameModeGalaxy", ue3::kEveryGame};
inline ue3::ClassRef orbitalClass{kOrbital, ue3::kEveryGame};
inline ue3::ClassRef systemClass{kSystem, ue3::kEveryGame};
inline ue3::ClassRef planetClass{"SFXGame.BioPlanet", ue3::kEveryGame};
inline ue3::ClassRef orbitalDataClass{"SFXGame.BioSeqAct_OrbitalGame", ue3::kEveryGame};
inline ue3::FieldRef manager{"SFXGame.BioPlayerController", "GameModeManager2", ue3::kEveryGame};
inline ue3::FieldRef modes{"SFXGame.SFXGameModeManager", "GameModes", ue3::kEveryGame};
inline ue3::FieldRef camera{"SFXGame.SFXGameModeGalaxy", "GalaxyCam", ue3::kEveryGame};
inline ue3::FieldRef latents{"Engine.Actor", "LatentActions", ue3::kEveryGame};

template<class T> inline std::optional<T> Read(ue3::Object* object, char const* owner, char const* field) {
    if (!ue3::runtime::IsAlive(object)) return std::nullopt;
    ue3::FieldRef ref{owner, field, ue3::kEveryGame};
    return ref.Read<T>(object);
}
inline std::optional<bool> ReadBool(ue3::Object* object, char const* owner, char const* field) {
    if (!ue3::runtime::IsAlive(object)) return std::nullopt;
    ue3::FieldRef ref{owner, field, ue3::kEveryGame};
    return ref.ReadBool(object);
}
inline ue3::Object* Object(ue3::Object* object, char const* owner, char const* field) {
    auto* result = Read<ue3::Object*>(object, owner, field).value_or(nullptr);
    return ue3::runtime::IsAlive(result) ? result : nullptr;
}
struct Context {
    ue3::Object *pc{}, *pawn{}, *world{}, *manager{}, *mode{}, *camera{}, *system{}, *planet{}, *orbitalData{};
    int modeNumber{}, cameraState{}, activeWorld{};
    friend bool operator==(Context const&, Context const&) = default;
};
inline std::optional<Context> Capture(ue3::Object* pc) {
    if (!ue3::runtime::Up() || pc != player::Controller()) return std::nullopt;
    auto n = player::GameMode(pc);
    if (!n || !engine::adapter::IsMapMode(*n)) return std::nullopt;
    auto* pawn = player::Pawn(pc);
    auto* world = Object(pc, "Engine.Actor", "WorldInfo");
    if (!ue3::runtime::IsAlive(pawn) || !world || Object(pawn, "Engine.Actor", "WorldInfo") != world) return std::nullopt;
    auto* mgr = manager.Read<ue3::Object*>(pc).value_or(nullptr);
    if (!ue3::runtime::IsAlive(mgr)) return std::nullopt;
    auto list = modes.ElementsOf(mgr, 64);
    if (!list || list->stride != sizeof(ue3::Object*) || list->count <= *n || list->count <= 11) return std::nullopt;
    auto* galaxyMode = *static_cast<ue3::Object**>(list->At(11));
    auto* mode = *static_cast<ue3::Object**>(list->At(*n));
    auto& r = ue3::runtime::Get();
    if (!r.IsAlive(galaxyMode) || !r.IsAlive(mode) || !ue3::IsA(galaxyMode, galaxyModeClass) ||
        r.OuterOf(galaxyMode) != pc || r.OuterOf(mode) != pc ||
        !ReadBool(mode, "SFXGame.SFXGameModeBase", "bIsActive").value_or(false)) return std::nullopt;
    bool const orbital = engine::adapter::IsOrbitalMode(*n);
    if (orbital && !ue3::IsA(mode, orbitalClass)) return std::nullopt;
    auto* cam = camera.Read<ue3::Object*>(galaxyMode).value_or(nullptr);
    if (!r.IsAlive(cam) || !ue3::IsA(cam, cameraClass)) return std::nullopt;
    auto state = Read<int>(cam, kCamera, "m_nCurrentState"), active = Read<int>(cam, kCamera, "m_nActiveWorld");
    if (!state || *state < 0 || *state > 5 || !active) return std::nullopt;
    auto* system = Object(cam, kCamera, "m_pCurrentSystem");
    auto* planet = Object(cam, kCamera, "m_pCurrentPlanet");
    if ((system && !ue3::IsA(system, systemClass)) || (planet && !ue3::IsA(planet, planetClass))) return std::nullopt;
    Context out{pc, pawn, world, mgr, mode, cam, system, planet, nullptr, *n, *state, *active};
    if (orbital) {
        // Reuse LE2's unique live latent-action proof, using LE3 reflection rather than its offsets.
        auto actions = latents.ElementsOf(pawn, 64);
        if (!actions || actions->stride != sizeof(ue3::Object*)) return std::nullopt;
        for (int i = 0; i < actions->count; ++i) {
            auto* action = *static_cast<ue3::Object**>(actions->At(i));
            if (!r.IsAlive(action)) return std::nullopt;
            if (!ue3::IsA(action, orbitalDataClass)) continue;
            if (out.orbitalData) return std::nullopt;
            out.orbitalData = action;
        }
        if (!out.orbitalData || !planet ||
            Object(out.orbitalData, "SFXGame.BioSeqAct_OrbitalGame", "CurrentPlanet") != planet) return std::nullopt;
        auto* actor = Object(out.orbitalData, "SFXGame.BioSeqAct_OrbitalGame", "Planet");
        if (!actor || Object(actor, "Engine.Actor", "WorldInfo") != world) return std::nullopt;
    }
    return out;
}
template<class T> inline json::Value Scalar(std::optional<T> value) {
    auto out = json::Value::Object(); out["available"] = value.has_value();
    if (value) out["value"] = *value;
    return out;
}
inline json::Value Float(ue3::Object* object, char const* owner, char const* field) {
    auto value = Read<float>(object, owner, field);
    if (value && !std::isfinite(*value)) value.reset();
    return Scalar(value);
}
inline json::Value Identity(ue3::Object* object) {
    auto out = json::Value::Object();
    out["address"] = std::format("0x{:X}", reinterpret_cast<std::uintptr_t>(object));
    if (ue3::runtime::IsAlive(object)) {
        auto& r = ue3::runtime::Get();
        out["index"] = r.IndexOf(object); out["name"] = r.PathName(object);
    }
    return out;
}
inline json::Value Bindings(ue3::Object* mode, char const* field) {
    auto out = json::Value::Object(); out["available"] = false;
    ue3::FieldRef bindings{"SFXGame.SFXGameModeBase", field, ue3::kEveryGame};
    ue3::FieldRef name{bindings, "Name"}, command{bindings, "Command"};
    auto elements = bindings.ElementsOf(mode, 256);
    if (!elements) return out;
    auto values = json::Value::Array();
    for (int i = 0; i < elements->count; ++i) {
        auto key = name.Read<ue3::Name>(elements->At(i));
        auto text = command.Read<ue3::FString>(elements->At(i));
        if (!key || !text || text->count < 0 || text->count > 1024 || text->max < text->count ||
            (text->count && (!text->data || text->data[text->count - 1] != L'\0'))) return out;
        auto row = json::Value::Object();
        row["key"] = ue3::runtime::NameText(*key);
        // Borrowed engine property: never FreeString. Empty FString needs no pointer read.
        row["command"] = text->count ? ue3::memory::Narrow(*text) : std::string{};
        values.Push(std::move(row));
    }
    out["available"] = true; out["values"] = std::move(values);
    return out;
}
inline json::Value Observe(ue3::Object* pc) {
    auto out = json::Value::Object();
    out["schemaVersion"] = 1; out["game"] = "LE3"; out["available"] = false;
    out["persistenceVerified"] = false; out["scanOutcomeVerified"] = false;
    out["coherentSimulationSnapshot"] = false; out["durableObjectIdentity"] = false;
    out["reason"] = "no-consistent-native-map-context";
    auto c = Capture(pc); if (!c) return out;
    out["world"] = Identity(c->world); out["pawn"] = Identity(c->pawn);
    out["modeObject"] = Identity(c->mode); out["camera"] = Identity(c->camera);
    out["system"] = Identity(c->system); out["planet"] = Identity(c->planet);
    out["orbitalData"] = Identity(c->orbitalData);
    out["gameMode"] = c->modeNumber; out["cameraState"] = c->cameraState; out["activeWorld"] = c->activeWorld;
    out["bindings"] = Bindings(c->mode, "Bindings");
    out["localizedBindings"] = Bindings(c->mode, "LocalizedBindings");
    out["scanEnabled"] = Scalar(ReadBool(c->camera, kCamera, "m_bCanDoSystemScan"));
    out["paused"] = Scalar(ReadBool(c->camera, kCamera, "m_bPaused"));
    out["scanCooldown"] = Float(c->camera, kCamera, "m_fSystemScanCooldown");
    out["alert"] = Float(c->system, kSystem, "m_fReaperAlertLevel");
    out["graceTime"] = Float(c->system, kSystem, "m_fReaperGraceTime");
    out["chaseFlag"] = Scalar(ReadBool(c->system, kSystem, "m_bReapersChasePlayer"));
    out["reapersActive"] = Scalar(ReadBool(c->system, kSystem, "m_bSystemReapersActive"));
    out["reapersDetected"] = Scalar(ReadBool(c->system, kSystem, "m_bReapersHaveBeenDetected"));
    out["scannedPlanetCount"] = Scalar(Read<int>(c->system, kSystem, "m_nScannedPlanets"));
    if (engine::adapter::IsOrbitalMode(c->modeNumber)) {
        for (char const* name : {"Scanning", "ProbeActive", "ImpactActive", "bRescaling", "bExiting"})
            out[name] = Scalar(ReadBool(c->mode, kOrbital, name));
    }
    auto after = Capture(pc);
    if (!after || *after != *c) { out["reason"] = "map-context-changed"; return out; }
    out["available"] = true; out["reason"] = "native-context-bracketed-read-only-values";
    return out;
}

enum class Action { SystemScan, StartOrbitalScan, StopOrbitalScan, LaunchProbe };
// Explicit diagnostic/native input action only. Do not call this in addition to the native
// pad route for the same trigger. A returned true reports invocation, never scan/loot success.
inline bool Request(ue3::Object* pc, Action action, bool inputAllowed = false) {
    // Supplied by the existing caller's menu/scene/input ownership checks; false by default.
    if (!inputAllowed) return false;
    if (action != Action::SystemScan && action != Action::StartOrbitalScan &&
        action != Action::StopOrbitalScan && action != Action::LaunchProbe) return false;
    auto c = Capture(pc); if (!c) return false;
    char const* method = nullptr;
    if (action == Action::SystemScan) {
        auto cooldown = Read<float>(c->camera, kCamera, "m_fSystemScanCooldown");
        if (!engine::adapter::IsGalaxyMode(c->modeNumber) || c->cameraState != 3 || !c->system ||
            !cooldown || !std::isfinite(*cooldown) || *cooldown > 0 ||
            !ReadBool(c->camera, kCamera, "m_bCanDoSystemScan").value_or(false) ||
            ReadBool(c->camera, kCamera, "m_bPaused").value_or(true)) return false;
        method = "ScanSystem";
    } else {
        if (!engine::adapter::IsOrbitalMode(c->modeNumber) || !c->orbitalData) return false;
        if (action != Action::StopOrbitalScan &&
            (ReadBool(c->mode, kOrbital, "bRescaling").value_or(true) || ReadBool(c->mode, kOrbital, "bExiting").value_or(true)))
            return false;
        if (action == Action::LaunchProbe && (!ReadBool(c->mode, kOrbital, "Scanning").value_or(false) ||
            ReadBool(c->mode, kOrbital, "ProbeActive").value_or(true) || ReadBool(c->mode, kOrbital, "ImpactActive").value_or(true)))
            return false;
        method = action == Action::LaunchProbe ? "LaunchProbe" : action == Action::StartOrbitalScan ? "StartScanning" : "StopScanning";
    }
    auto& r = ue3::runtime::Get();
    auto* fn = ue3::UnambiguousMethod(r, c->mode, method);
    if (!fn || r.ParmsSize(fn) != 0) return false;
    ue3::Params args(r, fn);
    return args.Valid() && args.Specs().empty() && Capture(pc) == c && ue3::runtime::Call(c->mode, fn, args);
}
} // namespace mevr::game::le3_galaxy
#endif
