// SPDX-License-Identifier: MIT
#pragma once

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
#include "game/Player.hpp"
#include "game/Le3Powers.hpp"
#include "game/SequelPowers.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Runtime.hpp"
#include "engine/GameAdapter.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
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
inline ue3::FunctionRef isHostile{engine::adapter::kIsHostileFunction, ue3::kEveryGame};  // ME3: Engine.Pawn's
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
#else
// ME2's name of a pawn (docs/trilogy-parity-ledger.md G30): its PrettyName, a string reference (SFXPawn.GetPrettyName, the
// squad HUD's own), as text through the game info's GetSimpleString. A henchman's BioActorBehavior.ActorGameName is the
// class default "BioPawn" (the roster read that until 2026-11-16), so the behaviour's own GetActorGameName comes second
// and only where it says something else. Functions are looked up on the object's class: a subclass may override them.
inline ue3::FunctionRef prettyName{"SFXGame.SFXPawn.GetPrettyName", ue3::kEveryGame};
inline ue3::FunctionRef simpleString{"SFXGame.SFXGame.GetSimpleString", ue3::kEveryGame};
inline ue3::FunctionRef behaviorName{"SFXGame.BioActorBehavior.GetActorGameName", ue3::kEveryGame};
inline ue3::FieldRef actorWorld{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
inline ue3::FieldRef worldGame{"Engine.WorldInfo", "Game", ue3::kEveryGame};
inline ue3::FieldRef pawnBehavior{"SFXGame.BioPawn", "m_oBehavior", ue3::kEveryGame};

struct NameRead {
    std::string name;       // empty: nothing usable (the caller falls back to the pawn's class)
    char const* source = "pawn class";
    std::int32_t prettyRef = 0;
    std::string prettyText;
    std::string behaviorText;
};

inline std::string TakeString(ue3::Params& parms) {
    auto text = parms.Return<ue3::FString>();
    if (!text) return {};
    std::string result = ue3::memory::Narrow(*text);
    ue3::memory::FreeString(*text);  // a script function's FString return belongs to the caller
    return result;
}

inline NameRead ActorName(ue3::Object* pawn) {
    NameRead out;
    if (!ue3::runtime::IsAlive(pawn) || !ue3::memory::Ready()) return out;
    auto& r = ue3::runtime::Get();
    if (prettyName.Get() != nullptr && simpleString.Get() != nullptr) {
        if (auto* fn = r.FindFunction(r.ClassOf(pawn), "GetPrettyName")) {
            ue3::Params args(r, fn);
            if (ue3::runtime::Call(pawn, fn, args)) out.prettyRef = args.Return<std::int32_t>().value_or(0);
        }
        auto* world = actorWorld.Read<ue3::Object*>(pawn).value_or(nullptr);
        auto* game = ue3::runtime::IsAlive(world) ? worldGame.Read<ue3::Object*>(world).value_or(nullptr) : nullptr;
        auto* text = ue3::runtime::IsAlive(game) ? r.FindFunction(r.ClassOf(game), "GetSimpleString") : nullptr;
        if (out.prettyRef > 0 && text != nullptr) {
            ue3::Params args(r, text);
            if (args.Set("StrRef", out.prettyRef) && args.SetBool("bParse", false) && ue3::runtime::Call(game, text, args)) {
                out.prettyText = TakeString(args);
            }
        }
    }
    auto* behavior = pawnBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (behaviorName.Get() != nullptr && ue3::runtime::IsAlive(behavior)) {
        if (auto* fn = r.FindFunction(r.ClassOf(behavior), "GetActorGameName")) {
            ue3::Params args(r, fn);
            if (ue3::runtime::Call(behavior, fn, args)) out.behaviorText = TakeString(args);
        }
    }
    if (!out.prettyText.empty()) {
        out.name = out.prettyText;
        out.source = "SFXPawn.GetPrettyName";
    } else if (!out.behaviorText.empty() && out.behaviorText != "BioPawn") {
        out.name = out.behaviorText;
        out.source = "BioActorBehavior.GetActorGameName";
    }
    return out;
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

// A move order's marker (research/54 "Placement by an arc" on ME2, 2026-11-17): the hold order's AI walks to the squadmate's
// move indicator (SFXAI_Henchman.MovingToHoldLocation pathfinds to BioBaseSquad.GetMemberMoveIndicator), not to the order's
// point, and the game's own move order puts that marker there first (the native BioPlayerController.UpdateMoveToIndicator).
// HenchmanOrderHoldPosition alone left Jacob where he stood. Call this before every hold order with a point.
inline ue3::FunctionRef updateMoveIndicator{"SFXGame.BioPlayerController.UpdateMoveToIndicator", ue3::kEveryGame};
inline bool PlaceMoveIndicator(ue3::Object* controller, ue3::Object* pawn, ue3::Vector const& at) {
    auto* fn = updateMoveIndicator.Get();
    if (fn == nullptr || !ue3::runtime::IsAlive(controller) || !ue3::runtime::IsAlive(pawn)) return false;
    ue3::Params args(ue3::runtime::Get(), fn);
    return args.Set("pSquadPawn", pawn) && args.Set("vMoveToPosition", at) && ue3::runtime::Call(controller, fn, args) &&
           args.ReturnBool().value_or(false);
}

#if defined(MEVR_GAME_LE2)
// What the henchman's AI made of an order (research/54 "Placement by an arc", ME2): HenchmanOrderHoldPosition only queues it
// (SFXAI_Henchman.AddOrder, refused while the AI follows or defends an actor the level asked for); the Idle state's timer
// takes the queue, sets m_vHoldLocation, and Idle's ChooseBehavior walks there while Shepard is within
// TetherDistanceWhileExecutingOrder. Read only, for the log and `mevr place status`.
inline ue3::FieldRef aiOrders{"SFXGame.SFXAI_Henchman", "m_Orders", ue3::kEveryGame};
inline ue3::FieldRef aiHoldLocation{"SFXGame.SFXAI_Henchman", "m_vHoldLocation", ue3::kEveryGame};
inline ue3::FieldRef aiHolding{"SFXGame.SFXAI_Henchman", "m_bHoldingPosition", ue3::kEveryGame};
inline ue3::FieldRef aiFollowPlayer{"SFXGame.SFXAI_Henchman", "m_bFollowPlayer", ue3::kEveryGame};
inline ue3::FieldRef aiDefend{"SFXGame.SFXAI_Henchman", "m_ActorToDefend", ue3::kEveryGame};
inline ue3::FieldRef aiTether{"SFXGame.SFXAI_Henchman", "TetherDistanceWhileExecutingOrder", ue3::kEveryGame};
inline ue3::FieldRef aiFollowActor{"SFXGame.SFXAI_Core", "m_RequestedActorToFollow", ue3::kEveryGame};
inline ue3::FunctionRef aiStateName{"Core.Object.GetStateName", ue3::kEveryGame};
inline ue3::FieldRef aiPawnController{"Engine.Pawn", "Controller", ue3::kEveryGame};
inline ue3::ClassRef aiClass{"SFXGame.SFXAI_Henchman", ue3::kEveryGame};
inline std::string OrderState(ue3::Object* pawn) {
    auto* ai = ue3::runtime::IsAlive(pawn) ? aiPawnController.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    if (!ue3::runtime::IsAlive(ai) || !ue3::IsA(ai, aiClass)) return "no henchman AI";
    std::string state = "?";
    if (auto* fn = aiStateName.Get()) {
        ue3::Params args(ue3::runtime::Get(), fn);
        if (ue3::runtime::Call(ai, fn, args)) state = ue3::runtime::NameText(args.Return<ue3::Name>().value_or(ue3::Name{}));
    }
    auto const orders = aiOrders.ElementsOf(ai, 64);
    auto const hold = aiHoldLocation.Read<ue3::Vector>(ai).value_or(ue3::Vector{});
    auto const name = [](ue3::Object* o) { return o != nullptr ? ue3::runtime::Get().NameText(o) : std::string("none"); };
    return std::format("state {}, {} queued order(s), hold at ({:.0f}, {:.0f}, {:.0f}){}{}, tether {:.0f}, following the level's {}, defending {}",
        state, orders ? orders->count : -1, hold.x, hold.y, hold.z, aiHolding.ReadBool(ai).value_or(false) ? " (holding)" : "",
        aiFollowPlayer.ReadBool(ai).value_or(false) ? ", follows Shepard" : "", aiTether.Read<float>(ai).value_or(-1.0f),
        name(aiFollowActor.Read<ue3::Object*>(ai).value_or(nullptr)), name(aiDefend.Read<ue3::Object*>(ai).value_or(nullptr)));
}
#endif
} // namespace mevr::game::le2_squad
#endif
