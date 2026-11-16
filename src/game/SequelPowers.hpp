// SPDX-License-Identifier: MIT
#pragma once

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
#include "game/Le3Powers.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

// The sequels' power roster, read once for everyone (docs/trilogy-parity-ledger.md Forks 6): a pawn's
// SFXGame.BioPawn.PowerManager, its SFXPowerManager.Powers, and on ME2 the evolved instance in place of the base one
// (SFXPowerManager.HasPowerBeenEvolved). The power wheel (game/PowerWheel) and the squad's context and orders
// (game/Le2Squad.hpp `le2_squad::Powers`) both walk it through ForEach and read one instance through the functions
// below; until 2026-11-16 each had its own copy of the walk and of ME2's handles. ME3's per-instance state stays
// game/Le3Powers.hpp's (`le3_powers::State`), which both already shared. Game thread only; nothing is written.
namespace mevr::game::sequel_powers {

#if defined(MEVR_GAME_LE3)
inline ue3::FieldRef& manager = le3_powers::manager;
inline ue3::FieldRef& powers = le3_powers::powers;
inline constexpr int kMaxPowers = 128;  // a longer list is not a roster we know: refused whole
#else
inline ue3::FieldRef manager{"SFXGame.BioPawn", "PowerManager", ue3::kEveryGame};
inline ue3::FieldRef powers{"SFXGame.SFXPowerManager", "Powers", ue3::kEveryGame};
inline constexpr int kMaxPowers = 64;  // the first 64, as ME1's roster read

// ME2's SFXPower (research/55 "Mass Effect 2").
inline ue3::FieldRef name{"SFXGame.SFXPower", "PowerName", ue3::kEveryGame};
inline ue3::FieldRef baseName{"SFXGame.SFXPower", "BaseName", ue3::kEveryGame};
inline ue3::FieldRef rank{"SFXGame.SFXPower", "Rank", ue3::kEveryGame};
inline ue3::FieldRef visible{"SFXGame.SFXPower", "DisplayInHUD", ue3::kEveryGame};
inline ue3::FunctionRef isEnabled{"SFXGame.SFXPower.IsEnabled", ue3::kEveryGame};
inline ue3::FunctionRef cooldownRemaining{"SFXGame.SFXPower.GetCooldownRemaining", ue3::kEveryGame};
inline ue3::FunctionRef cooldownTotal{"SFXGame.SFXPower.GetTotalCooldownTime", ue3::kEveryGame};
inline ue3::FunctionRef evolved{"SFXGame.SFXPowerManager.HasPowerBeenEvolved", ue3::kEveryGame};

// The instance the game casts for `power`: its evolution if the manager has one, else itself. Null if the evolved
// instance the game names is gone.
inline ue3::Object* Evolved(ue3::Object* managerObject, ue3::Object* power) {
    auto* const fn = evolved.Get();
    if (fn == nullptr) return power;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (args.Set("Power", power) && ue3::runtime::Call(managerObject, fn, args) && args.ReturnBool().value_or(false)) {
        return args.Get<ue3::Object*>("EvolvedPower").value_or(nullptr);
    }
    return power;
}

inline bool Learned(ue3::Object* power) {
    return rank.Read<float>(power).value_or(0.0f) > 0.0f;
}

// The game's own SFXPower.IsEnabled; nullopt: the function isn't there or the call failed.
inline std::optional<bool> Enabled(ue3::Object* power) {
    auto* const fn = isEnabled.Get();
    if (fn == nullptr) return std::nullopt;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!ue3::runtime::Call(power, fn, args)) return std::nullopt;
    return args.ReturnBool().value_or(false);
}

// Seconds from one of the cooldown functions (cooldownRemaining, cooldownTotal), never negative; nullopt: the
// function isn't there, the call failed or the answer isn't a finite number.
inline std::optional<float> Seconds(ue3::Object* power, ue3::FunctionRef& ref) {
    auto* const fn = ref.Get();
    if (fn == nullptr) return std::nullopt;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!ue3::runtime::Call(power, fn, args)) return std::nullopt;
    auto const seconds = args.Return<float>();
    if (!seconds || !std::isfinite(*seconds)) return std::nullopt;
    return std::max(0.0f, *seconds);
}
#endif

inline ue3::Object* ManagerOf(ue3::Object* pawn) {
    return manager.Read<ue3::Object*>(pawn).value_or(nullptr);
}

// Calls visit(ue3::Object* power) for each live power instance on the pawn's manager, in the game's order: on ME2
// the evolved instance where the manager has one. Nothing for a pawn without a live manager or (ME3) with a list
// longer than kMaxPowers. The caller filters (hidden, untrained, its own duplicates) and reads the state.
template <class Visit>
void ForEach(ue3::Object* pawn, Visit&& visit) {
    auto* const managerObject = ManagerOf(pawn);
    auto const list = ue3::runtime::IsAlive(managerObject) ? powers.ElementsOf(managerObject) : std::nullopt;
    if (!list) return;
#if defined(MEVR_GAME_LE3)
    if (list->count > kMaxPowers) return;
#endif
    for (int i = 0; i < list->count && i < kMaxPowers; ++i) {
        auto** const slot = static_cast<ue3::Object**>(list->At(i));
        auto* power = slot != nullptr ? *slot : nullptr;
        if (!ue3::runtime::IsAlive(power)) continue;
#if defined(MEVR_GAME_LE2)
        power = Evolved(managerObject, power);
        if (!ue3::runtime::IsAlive(power)) continue;
#endif
        visit(power);
    }
}

}  // namespace mevr::game::sequel_powers
#endif
