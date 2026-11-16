// SPDX-License-Identifier: MIT
#pragma once

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
#include "game/Player.hpp"
#include "game/Le3Powers.hpp"
#include "game/SequelPowers.hpp"
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
// The power handles live in game/SequelPowers.hpp since 2026-11-16 (one roster reader); the old names forward to them
// while callers move.
inline ue3::FieldRef& powerManager = sequel_powers::manager;
inline ue3::FieldRef& powers = sequel_powers::powers;
#if defined(MEVR_GAME_LE2)
inline ue3::FieldRef& powerName = sequel_powers::name;
inline ue3::FieldRef& powerRank = sequel_powers::rank;
inline ue3::FieldRef& powerVisible = sequel_powers::visible;
inline ue3::FunctionRef& powerEnabled = sequel_powers::isEnabled;
inline ue3::FunctionRef& powerCooldown = sequel_powers::cooldownRemaining;
inline ue3::FunctionRef& powerEvolved = sequel_powers::evolved;
#endif

struct Power { ue3::Name name; float cooldown; bool enabled; ue3::Object* object = nullptr; };
// A squadmate's trained, shown powers for the squad's context and orders, over the one roster walk the power wheel
// uses too (game/SequelPowers.hpp: the manager's list, ME2's evolved instances). Cooling powers are included, but
// dispatch re-reads both enabled state and cooldown rather than trusting an earlier voice snapshot.
inline std::vector<Power> Powers(ue3::Object* pawn) {
    std::vector<Power> out;
    sequel_powers::ForEach(pawn, [&](ue3::Object* power) {
#if defined(MEVR_GAME_LE3)
        if (!le3_powers::visible.ReadBool(power).value_or(false)) return;
        auto const state = le3_powers::State(power);
        auto const name = le3_powers::name.Read<ue3::Name>(power);
        if (!state || !name || name->Key() == 0) return;
        auto const view = vr::power_wheel::EvaluateNativePower(*state);
        if (!view.valid || !view.learned || std::any_of(out.begin(), out.end(), [&](auto const& p) { return p.name == *name; })) return;
        out.push_back({*name, std::max(state->remaining, 0.0f), view.enabled, power});
#else
        if (!sequel_powers::visible.ReadBool(power).value_or(false) || !sequel_powers::Learned(power)) return;
        auto const name = sequel_powers::name.Read<ue3::Name>(power).value_or(ue3::Name{});
        if (name.Key() == 0 || std::any_of(out.begin(), out.end(), [&](Power const& p) { return p.name.Key() == name.Key(); })) return;
        // A power whose state the game won't state isn't offered (the wheel shows it dim instead).
        auto const enabled = sequel_powers::Enabled(power);
        auto const seconds = sequel_powers::Seconds(power, sequel_powers::cooldownRemaining);
        if (!enabled || !seconds) return;
        out.push_back({name, *seconds, *enabled});
#endif
    });
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
