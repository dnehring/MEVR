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
inline ue3::ClassRef weaponBuffClass{"SFXGameContent.SFXPowerCustomAction_Marksman", ue3::kEveryGame};
inline ue3::FieldRef name{"SFXGame.SFXPowerCustomActionBase", "PowerName", ue3::kEveryGame};
inline ue3::FieldRef rank{"SFXGame.SFXPowerCustomActionBase", "Rank", ue3::kEveryGame};
inline ue3::FieldRef visible{"SFXGame.SFXPowerCustomActionBase", "DisplayInHUD", ue3::kEveryGame};
inline ue3::FieldRef remaining{"SFXGame.SFXPowerCustomActionBase", "CurrentCooldownTime", ue3::kEveryGame};
inline ue3::FieldRef total{"SFXGame.SFXPowerCustomActionBase", "TotalCooldownTime", ue3::kEveryGame};
inline ue3::FieldRef type{"SFXGame.SFXPowerCustomActionBase", "PowerType", ue3::kEveryGame};
inline ue3::FieldRef minimumRange{"SFXGame.SFXPowerCustomActionBase", "MinimumRange", ue3::kEveryGame};
inline ue3::FieldRef maximumRange{"SFXGame.SFXPowerCustomActionBase", "MaximumRange", ue3::kEveryGame};
inline ue3::FieldRef minimumValue{minimumRange, "CurrentValue"};
inline ue3::FieldRef maximumValue{maximumRange, "CurrentValue"};
inline ue3::FieldRef selection{"SFXGame.BioPlayerController", "m_oPlayerSelection", ue3::kEveryGame};
inline ue3::FieldRef selectionTarget{"SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget", ue3::kEveryGame};

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
    // Installed Marksman overrides this argument as Target; binding the base spelling alone
    // silently disables the real override. Both are object parameters checked by Params::Set.
    char const* field = ue3::runtime::Get().PropertyOf(fn, "oTarget") ? "oTarget" : "Target";
    return args.Set(field, target) && ue3::runtime::Call(object, fn, args) && args.ReturnBool().value_or(false);
}
inline bool ValidTarget(ue3::Object* object, ue3::Object* target) {
    auto* fn = Method(object, "IsValidTarget");
    if (!fn || !ue3::runtime::IsAlive(target)) return false;
    ue3::Params args(ue3::runtime::Get(), fn);
    return args.Set("oTarget", target) && ue3::runtime::Call(object, fn, args) && args.ReturnBool().value_or(false);
}
inline bool LocationInRange(ue3::Object* object, double distance) {
    auto minimum = minimumValue.Read<float>(object), maximum = maximumValue.Read<float>(object);
    return minimum && maximum && vr::power_wheel::NativeLocationInRange(distance, *minimum, *maximum);
}
inline bool LocationSelectionAllows(ue3::Object* controller, ue3::Object* power) {
    // ChooseTargetForPlayer prefers an existing valid selection over an explicit location.
    // Refuse that ambiguity instead of changing the native selection or casting at the wrong actor.
    auto* selected = selection.Read<ue3::Object*>(controller).value_or(nullptr);
    if (!ue3::runtime::IsAlive(selected)) return false;
    auto target = selectionTarget.Read<ue3::Object*>(selected);
    return target && (*target == nullptr || (ue3::runtime::IsAlive(*target) && !ValidTarget(power, *target)));
}
}  // namespace mevr::game::le3_powers
#endif
