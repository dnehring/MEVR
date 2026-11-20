// SPDX-License-Identifier: MIT
#pragma once

// A person walked to a point by the game's own AI, with every hold it needs taken for that person only and given back after
// (ME1's walk to a mark, research/69 "People walking to their marks"; the interface research/le2-conversations-in-world.md
// section 4 item 4 names, for the sequels' first-person conversations and lane EXIT's people leaving a conversation). One
// code path; each game's AI move and holds in an adapter at the top of the .cpp:
//   ME1: BioAiController.PushMoveToPoint (its BioSeqData_MoveToPoint terminated at the end), the AI enable counters
//        (BioEpicPawnBehavior.m_nALLEnabled, m_nStackEnabled) raised to 1 and lowered by as much, the conversation mode's ban
//        on AI movement answered true for the walker's controller (DoesGameModeAllowMovement), BioPawn.SetDesiredSpeed(1)
//        each step (the conversation pauses its people's controllers), BioPawn.m_bInGestures cleared (else the animation
//        tree plays gestures over the locomotion) and put back.
//   ME2: its Kismet walk (SFXSeqAct_MoveToNode's: SFXAI_Core.MoveToGoal, the AI's MovingToGoal state, a path at the AI's own
//        pace) to the level's navigation point nearest the point (kept in moveAction; lane EXIT3), else
//        BioAiController.PrecisionMoveTo(vLoc, rRot) (a slow precise step, about 0.3 m/s: lane EXIT2); the same counters
//        (ME2 has BioEpicPawnBehavior's), SetDesiredSpeed; no movement-ban question and no m_bInGestures (ME2 has neither).
//   ME3: BioAiController.MoveToSlot(vSlotLocation) [verify: research/le3-conversations-in-world.md 2.6]; ME3 has none of the
//        counters, the ban or the gesture flag.
// ME1's game/ConversationWorld still walks with its own copy of these steps (its compiled code kept unchanged); it moves onto
// this module in a commit of its own with ME1's regression set (tools/conversation-audit.ps1 -Canaries). Game thread only.

#include "ue3/Runtime.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace mevr::game::ai_walk {

enum class Phase : std::uint8_t { Walking, Arrived, Stalled, Ended };
char const* Name(Phase phase);

// One walker. A value the caller keeps; End gives back everything Start took, once (idempotent).
struct Walk {
    ue3::Object* pawn = nullptr;
    ue3::Object* controller = nullptr;
    ue3::Object* behavior = nullptr;    // the AI enable counters' owner (ME1, ME2), or null
    ue3::Object* moveAction = nullptr;  // ME1's BioSeqData_MoveToPoint, terminated at the end; ME2's goal (a navigation point)
    ue3::Vector to{};
    int allRaised = 0;                  // how much each counter was raised (lowered by as much)
    int stackRaised = 0;
    bool gesturesCleared = false;       // m_bInGestures cleared by Start (put back by End)
    bool answered = false;              // its controller is answered for the movement ban
    std::uint64_t startMs = 0;
    std::uint64_t progressMs = 0;       // the last time it moved kStallUnits
    ue3::Vector progressAt{};
    Phase phase = Phase::Ended;
};

inline constexpr double kArriveUnits = 25.0;  // the move's own tolerance (10) and a little (ME1's kAiArriveUnits)
inline constexpr double kStallUnits = 8.0;    // ... and a stall: less than this in kStallS (ME1's)
inline constexpr double kStallS = 2.0;

// The movement-ban answer (ME1), registered once: before the CallFunction hook goes in (Mod.cpp's startup order).
void TryStartup();
// The walk begun, or nullopt with `why` (the caller then keeps the person where it stands). `pawn` is any pawn with a
// BioAiController; `to` a point on the floor.
std::optional<Walk> Start(ue3::Object* pawn, ue3::Vector const& to, std::string& why);
// Each tick while Walking: the speed kept, arrival or a stall judged (flat distance). Arrived and Stalled still hold: End.
Phase Step(Walk& walk);
// Every hold given back, the AI's move ended. `tearingDown`: the scene's own teardown runs (the gesture mode is left to it).
void End(Walk& walk, char const* why, bool tearingDown = false);
// Walks begun and the movement-ban answers given, for status lines.
std::string StatusJson();
#if defined(MEVR_GAME_LE2)
// ME2's walk to an exact place (the circle's and the squad's, lane FPC5): MoveToGoal takes an actor, and the navigation point
// nearest a place was 1.4-1.8 m off the formation's places in Tali's room (2026-11-20), so the caller's `goal` (an actor it put
// at the place: a BioLookAtTarget) is the goal instead. Else as Start.
std::optional<Walk> StartToGoal(ue3::Object* pawn, ue3::Vector const& to, ue3::Object* goal, std::string& why);
// ME2's move for the next walks (`mevr convo exit walkby goal|precise`, an A/B): "goal" (the default) or "precise"; anything
// else only asks. The answer names the move in use.
char const* SetLe2Move(std::string const& how);
#endif

} // namespace mevr::game::ai_walk
