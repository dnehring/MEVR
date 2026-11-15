// SPDX-License-Identifier: MIT
#pragma once

#if defined(MEVR_GAME_LE3)
#include "core/vr/NativePower.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

// Installed LE3 package contracts, research/le3-powers.md. No SDK offsets or writes.
namespace mevr::game::le3_powers {
inline ue3::FieldRef manager{"SFXGame.BioPawn", "PowerManager", ue3::kEveryGame};
inline ue3::FieldRef powers{"SFXGame.SFXPowerManager", "Powers", ue3::kEveryGame};
inline ue3::ClassRef powerClass{"SFXGame.SFXPowerCustomActionBase", ue3::kEveryGame};
inline ue3::ClassRef ammoClass{"SFXGameContent.SFXPowerCustomAction_AmmoPower", ue3::kEveryGame};
inline ue3::ClassRef grenadeClass{"SFXGameContent.SFXPowerCustomAction_GrenadeBase", ue3::kEveryGame};
inline ue3::ClassRef reviveClass{"SFXGameContent.SFXPowerCustomAction_Unity", ue3::kEveryGame};
inline ue3::FieldRef name{"SFXGame.SFXPowerCustomActionBase", "PowerName", ue3::kEveryGame};
inline ue3::FieldRef rank{"SFXGame.SFXPowerCustomActionBase", "Rank", ue3::kEveryGame};
inline ue3::FieldRef visible{"SFXGame.SFXPowerCustomActionBase", "DisplayInHUD", ue3::kEveryGame};
inline ue3::FieldRef remaining{"SFXGame.SFXPowerCustomActionBase", "CurrentCooldownTime", ue3::kEveryGame};
inline ue3::FieldRef total{"SFXGame.SFXPowerCustomActionBase", "TotalCooldownTime", ue3::kEveryGame};
inline ue3::FieldRef type{"SFXGame.SFXPowerCustomActionBase", "PowerType", ue3::kEveryGame};

inline ue3::Function* Method(ue3::Object* object, char const* method) {
    auto& r = ue3::runtime::Get();
    return r.IsAlive(object) ? r.FindFunction(r.ClassOf(object), method) : nullptr;
}
inline std::optional<vr::power_wheel::NativePowerState> State(ue3::Object* object) {
    if (!ue3::IsA(object, powerClass)) return std::nullopt;
    auto const learned = rank.Read<float>(object), left = remaining.Read<float>(object), duration = total.Read<float>(object);
    auto* fn = Method(object, "IsEnabled");
    if (!learned || !left || !duration || fn == nullptr) return std::nullopt;
    ue3::Params args(ue3::runtime::Get(), fn);
    if (!ue3::runtime::Call(object, fn, args)) return std::nullopt;
    auto enabled = args.ReturnBool();
    if (!enabled) return std::nullopt;
    vr::power_wheel::NativePowerState state{*learned, *enabled, *left, *duration, ue3::IsA(object, grenadeClass)};
    if (state.grenade) {
        fn = Method(object, "GetGrenadeCount");
        if (fn != nullptr) {
            ue3::Params count(ue3::runtime::Get(), fn);
            if (ue3::runtime::Call(object, fn, count)) state.grenades = count.Return<int>();
        }
    }
    return state;
}
inline bool CanUse(ue3::Object* object, ue3::Object* target) {
    auto* fn = Method(object, "CanUsePower");
    if (fn == nullptr || (target != nullptr && !ue3::runtime::IsAlive(target))) return false;
    ue3::Params args(ue3::runtime::Get(), fn);
    return args.Set("oTarget", target) && ue3::runtime::Call(object, fn, args) && args.ReturnBool().value_or(false);
}
}  // namespace mevr::game::le3_powers
#endif
