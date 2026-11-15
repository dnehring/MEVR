// SPDX-License-Identifier: MIT
#pragma once

// The motion controllers as the game's Xbox pad 0, with the mod's flags, for both Halcyon hosts (docs/le2-continuation-
// plan.md M2.3; research/le2-renderer-integration.md "Step 6: the pad merge"). Moved unchanged out of LE1's
// plugin/HalcyonBridge.cpp MergeMotionControllers: the mod's input (HostServices v7 setSupportGrip and the flags it has
// carried since: the support grip, v9 the holsters' rightGripFree, v10 gripHolds, v11 the squad's grips, v17 the comfort
// turn and the walk toward the left hand ...), the pad mapping's latches (game/GamepadMapping), its reports back to the
// mod (supportGripHeld, rightGripFree, leftGripAway), the stick turned toward the left hand, the merge over a real pad,
// the L3 + R3 chord kept from the game and the renderer's trigger owners. Each bridge's XInput seam calls Merge between
// its own menus' handling (LE1: our main menu and the calibration's neutral pad and close latch; LE2: plugin/MenusLe2's
// FilterPad) and its packet numbering.
//
// The both-stick-clicks chord (the owner, 2026-11-14: "always prioritize the L3 + R3 stick click"): holding both clicks
// opens and closes our menu ahead of any game or mod use of L3 or R3. Its hold and tap are told on the player's live
// sample in each bridge's OnFrameBegun (game::gamepad::UpdateStickChord); here the pad keeps both clicks from the game
// while both are down, and L3 (also R3 where the renderer asks, Renderer::gateRightClick) waits kStickJoinMs to see
// whether the other joins (game::gamepad::GateClick).

#include "core/vr/Controllers.hpp"
#include "game/GamepadMapping.hpp"

#include <cstdint>

namespace mevr::host::pad_merge {

// ---- The mod's input and the mapping's answers (any thread) ------------------------------------------------------------

// The mod's two-handed grip input and its flags (HostServices::setSupportGrip); stale after kInputMaxAgeMs. Forget drops it
// now (the mod unloads).
inline constexpr std::uint64_t kInputMaxAgeMs = 250;
void SetSupportGrip(float distanceMeters, float acquireMeters, float releaseMeters, std::uint32_t flags);
void ForgetSupportGrip();
// The pad options from the mod's latest input at `nowMs` (GetTickCount64's clock); the defaults (no grip, LT from the
// trigger) when it is stale or none came.
game::gamepad::Options Options(std::uint64_t nowMs);
// The mod's latest flags if fresh at `nowMs`, else 0 (the renderer's seams that read one: the cockpit's head look, the
// physical crouch).
std::uint32_t FreshFlags(std::uint64_t nowMs);
// The mapping holds the support grip (HostServices::supportGripHeld).
bool SupportGripHeld();
// The mapping's right grip report (gamepad::RightGripReport, HostServices v9 rightGripFree).
std::uint32_t RightGripFree();
// The mapping's left grip report (gamepad::LeftGripReport, HostServices v11 leftGripAway).
std::uint32_t LeftGripAway();

// ---- The merge (the XInput hook's thread) ------------------------------------------------------------------------------

// What the renderer's bridge tells the merge.
struct Renderer {
    // The renderer's own menu is open (it zeroes the game's pad): a squeeze there never draws a weapon nor reaches the game as
    // RB once it closes, and isn't counted for the mod (Options::rightGripFree, countKept).
    bool ownMenuOpen = false;
    // The move stick's head-look turn the renderer adds after the merge (Halcyon's moveFollowsHead, + left, Unreal units),
    // taken off first when the left stick walks toward the left hand. Null: none.
    int (*headLookYawUU)() = nullptr;
    // Something of the renderer's host takes the left trigger, or both triggers (a quad of ours under a pointer: the
    // trigger is its click there), this poll. Null: nothing does.
    bool (*ownsLeftTrigger)() = nullptr;
    bool (*ownsTriggers)() = nullptr;
    // R3 waits for L3 too, as L3 waits for R3 (game::gamepad::Options::gateRightClick, the stick chord's gate on both
    // clicks): ME2's (the owner's L3 + R3 priority); LE1 leaves it false (unchanged).
    bool gateRightClick = false;
};

// The motion controllers (`controllers.active`) mapped with the mod's options at `nowMs` and merged over `real` (a real
// pad's state, or a neutral one): what the game's pad 0 reads, before the bridge's own close latch and packet number.
// Updates the reports above and logs each change of mode once. Serialised inside.
XINPUT_GAMEPAD Merge(XINPUT_GAMEPAD const& real, vr::controllers::State const& controllers, Renderer const& renderer,
    std::uint64_t nowMs);
// No motion controllers this poll: nothing holds the support grip.
void NoControllers();

// Everything back to its start (the latches, the reports, the logs' memory, the mod's input). For the unit tests.
void Reset();

} // namespace mevr::host::pad_merge
