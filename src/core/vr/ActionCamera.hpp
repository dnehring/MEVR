// SPDX-License-Identifier: MIT
#pragma once

// ME3's transient action cameras (engine::adapter::kActionCameras): the camera mode the game swaps to for about half a
// second on a roll, a melee, a hit reaction or a cover entry (measured on the combat fixture 2026-11-18: a forward roll is
// SFXCameraMode_Interpolate to SFXCameraMode_Roll for 2 s; Halcyon's ME3 renderer saw _Melee, _Roll, _HitReaction and
// _EnterCover flip its weapon verdict for ~0.5 s each, engine_probe.cpp). The body owns them as it owns Explore and Combat:
// first person, the view level, the floor lock, the player's snap turn and no camera shakes (the owner, 2026-11-18).
// Exact class names, as vr::view_level::CameraModeAllows: a substring would let an unrelated mode in.

#include <algorithm>
#include <iterator>
#include <string_view>

namespace mevr::vr {

inline bool IsActionCamera(std::string_view className) {
    constexpr std::string_view kActionCameras[] = {
        "SFXCameraMode_Roll",
        "SFXCameraMode_Melee",
        "SFXCameraMode_HitReaction",
        "SFXCameraMode_EnterCover",
    };
    return std::find(std::begin(kActionCameras), std::end(kActionCameras), className) != std::end(kActionCameras);
}

// Shepard's body hidden from the eyes during a roll (the owner, 2026-11-18: "Yes for now", docs/trilogy-plan.md "Answered
// 2026-11-18, later"): the roll's camera (SFXCameraMode_Roll, about 2 s), or the game's blend into or out of it (Interpolate,
// whose From or To is the roll's). The renderer's first-person hides take it (MevrSeams::HideBodyForAction, ME3's
// engine_probe.cpp). `fromClass`/`toClass`: the blend's endpoints when `modeClass` is the Interpolate mode (else empty).
// Not in ME1's build (no roll camera; its objects stay as they were: the literals of an unused inline function still land).
#if !defined(MEVR_GAME_LE1)
inline bool HidesBodyForAction(std::string_view modeClass, std::string_view fromClass, std::string_view toClass) {
    constexpr std::string_view kRoll = "SFXCameraMode_Roll";
    if (modeClass == kRoll) {
        return true;
    }
    return modeClass == "SFXCameraMode_Interpolate" && (fromClass == kRoll || toClass == kRoll);
}
#endif

}  // namespace mevr::vr
