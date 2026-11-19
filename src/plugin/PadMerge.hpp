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
    // ME2 orbital scanning uses the native analog LT/RT pair, with no gameplay trigger remapping.
    bool orbitalTriggers = false;
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
    // The right stick click the mod owns (its hold is the power wheel's, on foot and in the wheels) is kept from the game as
    // R3 (game::gamepad::Options::keepPowerClick): the sequels' (the owner, 2026-11-17: ME2's and ME3's R3 open the area
    // map); LE1 leaves it false (Halcyon LE1's double-click recenter reads R3 from the game's pad; unchanged).
    bool keepPowerClick = false;
    // The left stick click the mod owns (its crouch toggle, VR on foot) is kept from the game as L3
    // (game::gamepad::Options::keepCrouchClick): the sequels' (the owner, 2026-11-18: ME2's L3 is the area map's and its
    // objective locator, ME3's a point of interest's); LE1 leaves it false (ME1's L3 is the game's own crouch toggle).
    bool keepCrouchClick = false;
    // The game's conversation mode is up and its triggers are the interrupts' (ME2: LT Paragon, RT Renegade; M5.2,
    // research/le2-cinema.md): ConversationTriggers below, and no trigger owner of the host's this poll. LE1 leaves it false.
    bool conversationTriggers = false;
};

// ME2's interrupts on the triggers (the owner, 2026-11-14: "the triggers, as ME2 has them on a pad"): in the conversation
// mode the left trigger reaches the game as LT and the right as RT whatever the mod's options would make of them on foot:
// the left trigger is LT (not [Controls] leftTrigger = lb's squad-wheel LB, nor freed for the squad), no support grip still
// latched from gameplay pulls LT, and neither the Mako's controls nor the hand power wheel keep the left hand. The game's own
// route then takes the interrupt (SFXGameModeConversation.InterruptParagon / InterruptRenegade).
game::gamepad::Options ConversationTriggers(game::gamepad::Options options);

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
