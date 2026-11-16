// SPDX-License-Identifier: MIT
#pragma once

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
#include "game/Player.hpp"
#include "game/Le3Powers.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

// Shared sequel inventory and order prerequisites; LE3's distinct contracts are explicit below.
// Calls stay on the game thread. Object lists are rebuilt before dispatch.
namespace mevr::game::le2_squad {
inline ue3::FieldRef inventoryManager{"Engine.Pawn", "InvManager", ue3::kEveryGame};
inline ue3::FieldRef inventoryChain{"Engine.InventoryManager", "InventoryChain", ue3::kEveryGame};
inline ue3::FieldRef nextInventory{"Engine.Inventory", "Inventory", ue3::kEveryGame};
inline ue3::FieldRef equippedWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
inline ue3::FieldRef inputField{"Engine.PlayerController", "PlayerInput", ue3::kEveryGame};
inline ue3::ClassRef weaponClass{"SFXGame.SFXWeapon", ue3::kEveryGame};
inline ue3::ClassRef weaponTypes[] = {
#if defined(MEVR_GAME_LE3)
    ue3::ClassRef{"SFXGame.SFXWeapon_Pistol_Base", ue3::kEveryGame},
#else
    ue3::ClassRef{"SFXGame.SFXWeapon_HeavyPistol_Base", ue3::kEveryGame},
#endif
    ue3::ClassRef{"SFXGame.SFXWeapon_Shotgun_Base", ue3::kEveryGame},
    ue3::ClassRef{"SFXGame.SFXWeapon_AssaultRifle_Base", ue3::kEveryGame},
    ue3::ClassRef{"SFXGame.SFXWeapon_SniperRifle_Base", ue3::kEveryGame},
#if defined(MEVR_GAME_LE3)
    ue3::ClassRef{"SFXGame.SFXWeapon_SMG_Base", ue3::kEveryGame},
#else
    ue3::ClassRef{"SFXGame.SFXWeapon_AutoPistol_Base", ue3::kEveryGame},
#endif
};
inline ue3::FunctionRef isHostile{"SFXGame.BioPawn.IsHostile", ue3::kEveryGame};
inline ue3::FunctionRef isDead{"Engine.Pawn.IsDead", ue3::kEveryGame};
inline ue3::FunctionRef combatEnabled{"SFXGame.BioPlayerInput.IsCombatEnabled", ue3::kEveryGame};
inline ue3::FunctionRef orderWeapon{"SFXGame.BioPlayerController.OrderWeaponSwitch", ue3::kEveryGame};
inline ue3::FunctionRef orderAttack{"SFXGame.BioPlayerController.HenchmanOrderAttackTarget", ue3::kEveryGame};
inline ue3::FunctionRef orderFollow{"SFXGame.BioPlayerController.HenchmanOrderFollow", ue3::kEveryGame};
inline ue3::FunctionRef orderHold{"SFXGame.BioPlayerController.HenchmanOrderHoldPosition", ue3::kEveryGame};
inline ue3::FunctionRef orderPower{"SFXGame.BioPlayerController.SquadOrderUsePower", ue3::kEveryGame};
inline ue3::FunctionRef applyOrders{"SFXGame.BioPlayerController.ApplyTacticalOrders", ue3::kEveryGame};
inline ue3::FieldRef powerManager{"SFXGame.BioPawn", "PowerManager", ue3::kEveryGame};
inline ue3::FieldRef powers{"SFXGame.SFXPowerManager", "Powers", ue3::kEveryGame};
inline ue3::FieldRef powerName{"SFXGame.SFXPower", "PowerName", ue3::kEveryGame};
inline ue3::FieldRef powerRank{"SFXGame.SFXPower", "Rank", ue3::kEveryGame};
inline ue3::FieldRef powerVisible{"SFXGame.SFXPower", "DisplayInHUD", ue3::kEveryGame};
inline ue3::FunctionRef powerEnabled{"SFXGame.SFXPower.IsEnabled", ue3::kEveryGame};
inline ue3::FunctionRef powerCooldown{"SFXGame.SFXPower.GetCooldownRemaining", ue3::kEveryGame};
inline ue3::FunctionRef powerEvolved{"SFXGame.SFXPowerManager.HasPowerBeenEvolved", ue3::kEveryGame};

struct Power { ue3::Name name; float cooldown; bool enabled; ue3::Object* object = nullptr; };
// Same native source and evolution resolution as game/PowerWheel.cpp; includes cooling powers in context,
// but dispatch re-reads both enabled state and cooldown rather than trusting an earlier voice snapshot.
inline std::vector<Power> Powers(ue3::Object* pawn) {
    std::vector<Power> out;
    auto* manager = powerManager.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto list = ue3::runtime::IsAlive(manager) ? powers.ElementsOf(manager) : std::nullopt;
    if (!list) return out;
#if defined(MEVR_GAME_LE3)
    if (list->count > 128) return out;
    for (int i = 0; i < list->count; ++i) {
        auto** slot = static_cast<ue3::Object**>(list->At(i));
        auto* object = slot != nullptr ? *slot : nullptr;
        if (!ue3::runtime::IsAlive(object) || !le3_powers::visible.ReadBool(object).value_or(false)) continue;
        auto const state = le3_powers::State(object);
        auto const name = le3_powers::name.Read<ue3::Name>(object);
        if (!state || !name || name->Key() == 0) continue;
        auto const view = vr::power_wheel::EvaluateNativePower(*state);
        if (!view.valid || !view.learned || std::any_of(out.begin(), out.end(), [&](auto const& p) { return p.name == *name; })) continue;
        out.push_back({*name, std::max(state->remaining, 0.0f), view.enabled, object});
    }
#else
    for (int i = 0; i < list->count && i < 64; ++i) {
        auto** slot = static_cast<ue3::Object**>(list->At(i));
        auto* power = slot != nullptr ? *slot : nullptr;
        if (!ue3::runtime::IsAlive(power)) continue;
        if (auto* fn = powerEvolved.Get()) {
            ue3::Params args(ue3::runtime::Get(), fn);
            if (args.Set("Power", power) && ue3::runtime::Call(manager, fn, args) && args.ReturnBool().value_or(false))
                power = args.Get<ue3::Object*>("EvolvedPower").value_or(nullptr);
        }
        if (!ue3::runtime::IsAlive(power) || !powerVisible.ReadBool(power).value_or(false) ||
            powerRank.Read<float>(power).value_or(0.0f) <= 0.0f) continue;
        auto name = powerName.Read<ue3::Name>(power).value_or(ue3::Name{});
        if (name.Key() == 0 || std::any_of(out.begin(), out.end(), [&](Power const& p) { return p.name.Key() == name.Key(); })) continue;
        auto* enabled = powerEnabled.Get(); auto* cooldown = powerCooldown.Get();
        if (enabled == nullptr || cooldown == nullptr) continue;
        ue3::Params e(ue3::runtime::Get(), enabled), c(ue3::runtime::Get(), cooldown);
        if (!ue3::runtime::Call(power, enabled, e) || !ue3::runtime::Call(power, cooldown, c)) continue;
        auto seconds = c.Return<float>();
        if (!seconds || !std::isfinite(*seconds)) continue;
        out.push_back({name, std::max(0.0f, *seconds), e.ReturnBool().value_or(false)});
    }
#endif
    return out;
}

inline bool Living(ue3::Object* pawn) {
    auto* fn = isDead.Get();
    if (!ue3::runtime::IsAlive(pawn) || fn == nullptr) return false;
    ue3::Params args(ue3::runtime::Get(), fn);
    return ue3::runtime::Call(pawn, fn, args) && !args.ReturnBool().value_or(true);
}

inline bool Hostile(ue3::Object* target) {
    auto* pawn = player::BioPawn(player::Controller()); auto* fn = isHostile.Get();
    if (!Living(target) || !Living(pawn) || fn == nullptr) return false;
    ue3::Params args(ue3::runtime::Get(), fn);
    return args.Set("Other", target) && ue3::runtime::Call(pawn, fn, args) && args.ReturnBool().value_or(false);
}

inline bool CanOrder() {
    if (!ue3::runtime::Up()) return false;
    auto* controller = player::Controller();
    if (player::GameMode(controller) != 0 || !Living(player::BioPawn(controller))) return false;
    auto* input = inputField.Read<ue3::Object*>(controller).value_or(nullptr);
    auto* fn = combatEnabled.Get();
    if (!ue3::runtime::IsAlive(input) || fn == nullptr) return false;
    ue3::Params args(ue3::runtime::Get(), fn);
    return ue3::runtime::Call(input, fn, args) && args.ReturnBool().value_or(false);
}

// LE3 adds an explicit input-method parameter to all henchman order wrappers.
// Installed scripts default to CIM_Default=0; custom voice uses the same route,
// never CIM_Kinect or a less restrictive native admission path.
inline bool BindInputMethod(ue3::Params& args) {
#if defined(MEVR_GAME_LE3)
    return args.Set("eInputMethod", std::uint8_t{0});
#else
    (void)args;
    return true;
#endif
}

#if defined(MEVR_GAME_LE3)
inline ue3::FieldRef worldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
inline ue3::FieldRef playerSquad{"SFXGame.BioWorldInfo", "m_playerSquad", ue3::kEveryGame};
inline ue3::FieldRef squadMembers{"SFXGame.BioBaseSquad", "Members", ue3::kEveryGame};
inline ue3::FieldRef pawnController{"Engine.Pawn", "Controller", ue3::kEveryGame};
inline ue3::ClassRef henchmanAI{"SFXGame.SFXAI_Henchman", ue3::kEveryGame};
inline ue3::FunctionRef actorName{"SFXGame.BioPawn.GetActorGameName", ue3::kEveryGame};
inline std::string ActorName(ue3::Object* pawn) {
    auto* fn = actorName.Get();
    if (!ue3::runtime::IsAlive(pawn) || !fn || !ue3::memory::Ready()) return {};
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!ue3::runtime::Call(pawn, fn, args)) return {};
    auto text = args.Return<ue3::FString>();
    if (!text) return {};
    auto result = ue3::memory::Narrow(*text);
    ue3::memory::FreeString(*text);  // native/script FString return belongs to caller
    return result;
}
#endif

inline bool RecipientReady(ue3::Object* pawn) {
    if (!Living(pawn)) return false;
#if defined(MEVR_GAME_LE3)
    auto* world = worldInfo.Read<ue3::Object*>(player::BioPawn(player::Controller())).value_or(nullptr);
    auto* squad = ue3::runtime::IsAlive(world) ? playerSquad.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
    auto members = ue3::runtime::IsAlive(squad) ? squadMembers.ElementsOf(squad) : std::nullopt;
    if (!members || members->count > 16) return false;
    bool found = false;
    for (int i = 0; i < members->count; ++i) {
        auto** slot = static_cast<ue3::Object**>(members->At(i));
        found = found || (slot != nullptr && *slot == pawn);
    }
    auto* ai = pawnController.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (!found || !ue3::runtime::IsAlive(ai) || !ue3::IsA(ai, henchmanAI)) return false;
    auto* fn = ue3::runtime::Get().FindFunction(ue3::runtime::Get().ClassOf(ai), "IsEnabled");
    if (!fn) return false;
    ue3::Params args(ue3::runtime::Get(), fn);
    return ue3::runtime::Call(ai, fn, args) && args.ReturnBool().value_or(false);
#else
    return true;
#endif
}

inline int WeaponType(ue3::Object* weapon) {
    if (!ue3::runtime::IsAlive(weapon)) return -1;
    for (int i = 0; i < 5; ++i) if (ue3::IsA(weapon, weaponTypes[i])) return i;
    return -1;
}

inline char const* WeaponLabel(int type) {
    switch (type) {
    case 0: return "PISTOL";
    case 1: return "SHOTGUN";
    case 2: return "ASSAULT RIFLE";
    case 3: return "SNIPER RIFLE";
    case 4: return "SMG";
    default: return "WEAPON";
    }
}

inline std::vector<ue3::Object*> Weapons(ue3::Object* pawn) {
    std::vector<ue3::Object*> out;
    if (!ue3::runtime::IsAlive(pawn)) return out;
    auto* manager = inventoryManager.Read<ue3::Object*>(pawn).value_or(nullptr);
    auto* item = ue3::runtime::IsAlive(manager) ? inventoryChain.Read<ue3::Object*>(manager).value_or(nullptr) : nullptr;
    std::vector<ue3::Object*> visited;
    for (int n = 0; n < 64 && ue3::runtime::IsAlive(item); ++n) {
        if (std::find(visited.begin(), visited.end(), item) != visited.end()) break;
        visited.push_back(item);
        if (ue3::IsA(item, weaponClass)) out.push_back(item);
        item = nextInventory.Read<ue3::Object*>(item).value_or(nullptr);
    }
    auto* held = equippedWeapon.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (ue3::runtime::IsAlive(held) && ue3::IsA(held, weaponClass) && std::find(out.begin(), out.end(), held) == out.end()) out.push_back(held);
    std::stable_sort(out.begin(), out.end(), [](auto* a, auto* b) { return WeaponType(a) < WeaponType(b); });
    return out;
}
} // namespace mevr::game::le2_squad
#endif
