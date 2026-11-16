// SPDX-License-Identifier: MIT
#pragma once
#if defined(MEVR_GAME_LE3)
#include "core/Json.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"
#include "ue3/UnambiguousMethod.hpp"

#include <cmath>
#include <format>

// Read-only successor to ConsoleRequest::WeaponProbeLe3. Game thread only, after native
// reflection cross-check. This is observation, not a reload dispatcher or production profile.
namespace mevr::game::le3_weapon_ammo {
inline ue3::ClassRef weaponClass{"SFXGame.SFXWeapon", ue3::kEveryGame};
inline ue3::ClassRef pawnClass{"SFXGame.BioPawn", ue3::kEveryGame};
inline ue3::ClassRef controllerClass{"SFXGame.BioPlayerController", ue3::kEveryGame};
inline ue3::FieldRef held{"Engine.Pawn", "Weapon", ue3::kEveryGame};
inline ue3::FieldRef world{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
inline ue3::FieldRef pawnController{"Engine.Pawn", "Controller", ue3::kEveryGame};
inline ue3::FieldRef inventory{"Engine.Pawn", "InvManager", ue3::kEveryGame};
inline ue3::FieldRef weaponInventory{"Engine.Inventory", "InvManager", ue3::kEveryGame};
inline ue3::FieldRef instigator{"Engine.Actor", "Instigator", ue3::kEveryGame};
inline ue3::FieldRef pending{"Engine.InventoryManager", "PendingWeapon", ue3::kEveryGame};
inline ue3::FieldRef magazine{"SFXGame.SFXWeapon", "MagSize", ue3::kEveryGame};
inline ue3::FieldRef magazineValue{magazine, "Value"};
inline ue3::FieldRef spare{"SFXGame.SFXWeapon", "CurrentSpareAmmo", ue3::kEveryGame};
inline ue3::FieldRef used{"SFXGame.SFXWeapon", "AmmoUsedCount", ue3::kEveryGame};
inline ue3::FieldRef infinite{"SFXGame.SFXWeapon", "bInfiniteAmmo", ue3::kEveryGame};
inline ue3::FieldRef reloadFlag{"SFXGame.SFXWeapon", "bWeaponCanBeReloaded", ue3::kEveryGame};

struct ObjectIdentity {
    ue3::Object* object{};
    ue3::Class* cls{};
    int index{-1};
    ue3::Name name{};
    friend bool operator==(ObjectIdentity const&, ObjectIdentity const&) = default;
};
inline ObjectIdentity Identity(ue3::Object* object) {
    auto& r = ue3::runtime::Get();
    return r.IsAlive(object) ? ObjectIdentity{object, r.ClassOf(object), r.IndexOf(object), r.NameOf(object)} : ObjectIdentity{};
}
struct Context {
    ObjectIdentity controller, pawn, worldInfo, manager, weapon, pendingWeapon;
    std::uint8_t mode{};
    friend bool operator==(Context const&, Context const&) = default;
};
inline std::optional<Context> Capture(ue3::Object* pc) {
    if (!ue3::runtime::Up() || !ue3::runtime::IsAlive(pc) || !ue3::IsA(pc, controllerClass)) return std::nullopt;
    auto* pawn = player::Pawn(pc);
    if (!ue3::runtime::IsAlive(pawn) || !ue3::IsA(pawn, pawnClass)) return std::nullopt;
    auto* gun = held.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto* manager = inventory.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto* wi = world.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (!ue3::runtime::IsAlive(gun) || !ue3::IsA(gun, weaponClass) ||
        !ue3::runtime::IsAlive(manager) || !ue3::runtime::IsAlive(wi)) return std::nullopt;
    if (pawnController.Read<ue3::Object*>(pawn) != pc || world.Read<ue3::Object*>(pc) != wi ||
        world.Read<ue3::Object*>(gun) != wi || instigator.Read<ue3::Object*>(gun) != pawn ||
        weaponInventory.Read<ue3::Object*>(gun) != manager) return std::nullopt;
    auto next = pending.Read<ue3::Object*>(manager);
    auto mode = player::GameMode(pc);
    if (!next || !mode || (*next && !ue3::runtime::IsAlive(*next))) return std::nullopt;
    return Context{Identity(pc), Identity(pawn), Identity(wi), Identity(manager), Identity(gun), Identity(*next), *mode};
}
struct Raw {
    std::optional<float> magazine;
    std::optional<int> spare, used;
    std::optional<bool> infinite, reloadFlag;
    friend bool operator==(Raw const&, Raw const&) = default;
};
inline Raw ReadRaw(ue3::Object* gun) {
    return {magazineValue.Read<float>(magazine.Address(gun)), spare.Read<int>(gun), used.Read<int>(gun),
            infinite.ReadBool(gun), reloadFlag.ReadBool(gun)};
}
inline json::Value ObjectJson(ObjectIdentity const& id) {
    auto out = json::Value::Object();
    out["address"] = std::format("0x{:X}", reinterpret_cast<std::uintptr_t>(id.object));
    out["index"] = id.index;
    out["nameBits"] = std::format("0x{:X}", id.name.Key());
    out["class"] = id.cls ? ue3::runtime::Get().PathName(id.cls) : "None";
    return out;
}
template<class T> inline json::Value Scalar(std::optional<T> value) {
    auto out = json::Value::Object();
    out["available"] = value.has_value();
    if (value) out["value"] = *value;
    return out;
}
inline json::Value Query(ue3::Object* object, char const* name, bool boolean) {
    auto out = json::Value::Object();
    out["available"] = false;
    out["reason"] = "missing-method-or-ambiguous-state-declaration";
    auto& r = ue3::runtime::Get();
    auto* fn = ue3::UnambiguousMethod(r, object, name);
    if (!fn) return out;
    out["declaration"] = r.PathName(fn);
    ue3::Params args(r, fn);
    auto const* result = args.ReturnSpec();
    if (!args.Valid() || args.Specs().size() != 1 || !result || result->size != 4 ||
        result->kind != (boolean ? ue3::Kind::Bool : ue3::Kind::Int)) {
        out["reason"] = "not-a-zero-input-typed-query";
        return out;
    }
    if (!ue3::runtime::Call(object, fn, args)) { out["reason"] = "native-query-failed"; return out; }
    if (boolean) {
        auto value = args.ReturnBool();
        if (!value) return out;
        out["value"] = *value;
    } else {
        auto value = args.Return<int>();
        if (!value || *value < 0) { out["reason"] = "negative-or-missing-native-count"; return out; }
        out["value"] = *value;
    }
    out["available"] = true;
    out["reason"] = "actual-class-native-query-no-state-override";
    return out;
}
inline json::Value Observe(ue3::Object* pc) {
    auto out = json::Value::Object();
    out["schemaVersion"] = 1;
    out["game"] = "LE3";
    out["available"] = false;
    out["dispatchCapability"] = false;
    out["reloadCompletionVerified"] = false;
    out["durableObjectIdentity"] = false;
    out["coherentSimulationSnapshot"] = false;
    out["ordinaryCombatVerified"] = false;
    out["reason"] = "no-consistent-selected-weapon-context";
    auto before = Capture(pc);
    if (!before) return out;
    auto* gun = before->weapon.object;
    auto raw = ReadRaw(gun);
    auto effective = json::Value::Object();
    // Never invoke TryReload, StartFire, DoReload or any resource transfer here.
    effective["magazine"] = Query(gun, "GetMagazineSize", false);
    effective["loaded"] = Query(gun, "GetAmmoCountInMagazine", false);
    effective["reserve"] = Query(gun, "GetCurrentSpareAmmo", false);
    effective["canReload"] = Query(gun, "CanReload", true);
    effective["isReloading"] = Query(pc, "IsReloading", true);
    auto after = Capture(pc);
    if (!after || *before != *after) { out["reason"] = "context-changed-during-query"; return out; }
    auto rawAfter = ReadRaw(gun);
    if (raw != rawAfter) { out["reason"] = "raw-values-changed-during-query"; return out; }
    if (raw.magazine && !std::isfinite(*raw.magazine)) raw.magazine.reset();
    auto fields = json::Value::Object();
    fields["magazine"] = Scalar(raw.magazine);
    fields["reserve"] = Scalar(raw.spare);
    fields["used"] = Scalar(raw.used);
    fields["infinite"] = Scalar(raw.infinite);
    fields["reloadFlag"] = Scalar(raw.reloadFlag);
    auto identities = json::Value::Object();
    identities["controller"] = ObjectJson(before->controller);
    identities["pawn"] = ObjectJson(before->pawn);
    identities["worldInfo"] = ObjectJson(before->worldInfo);
    identities["inventory"] = ObjectJson(before->manager);
    identities["weapon"] = ObjectJson(before->weapon);
    identities["pendingWeapon"] = ObjectJson(before->pendingWeapon);
    out["identity"] = std::move(identities);
    out["gameMode"] = static_cast<int>(before->mode);
    out["gameplayMode"] = before->mode == 0; // Necessary, never sufficient, for a combat fixture.
    out["raw"] = std::move(fields);
    // A legitimate override may disagree with stored counts. Only the audited base
    // declarations are eligible for these scalar cross-checks; disagreement is evidence,
    // never permission to normalize fields or manufacture ammo.
    auto checks = json::Value::Object();
    auto const& capacity = effective.Get("magazine");
    if (raw.magazine && *raw.magazine >= 0 && static_cast<double>(*raw.magazine) < 2147483648.0 &&
        capacity.Bool("available") && capacity.String("declaration") == "SFXGame.SFXWeapon.GetMagazineSize")
        checks["baseMagazineAgrees"] = static_cast<int>(*raw.magazine) == capacity.Number("value");
    auto const& reserve = effective.Get("reserve");
    if (raw.spare && reserve.Bool("available") && reserve.String("declaration") == "SFXGame.SFXWeapon.GetCurrentSpareAmmo")
        checks["baseReserveAgrees"] = *raw.spare == reserve.Number("value");
    auto const& loaded = effective.Get("loaded");
    if (raw.used && loaded.Bool("available") && capacity.Bool("available") &&
        loaded.String("declaration") == "SFXGame.SFXWeapon.GetAmmoCountInMagazine")
        checks["baseLoadedAgrees"] = capacity.Number("value") - *raw.used == loaded.Number("value");
    out["crossChecks"] = std::move(checks);
    out["effective"] = std::move(effective);
    out["available"] = true; // Each individual value carries its own availability.
    out["reason"] = "selected-weapon-context-and-raw-values-stable-across-queries";
    return out;
}
} // namespace mevr::game::le3_weapon_ammo
#endif
