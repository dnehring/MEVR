// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"
#include "engine/XrToUnreal.hpp"

#include <cstdint>
#include <optional>
#include <string>

// The one dispatcher every squad order path ends in (research/54): squad placement's B arc (game/SquadPlace), the voice
// orders (game/Voice) and `mevr order`. The left-grip gesture orders that were the first path (a squeeze pointed with the left
// aim ray) are gone since 2026-11-18 (the owner: "since we have our dedicated system of squad control I don't see why we'd
// want to support any other method"); the left aim ray is still read each tick for the voice orders' "there". The game's calls, on
// the game thread: `BioPlayerController.QuickCommandAttackTarget(iSquadMember, pTarget)`, `QuickCommandMoveTo
// (iSquadMember)` with the controller's Rotation turned toward the point for the call (the native drops the point
// along the controller's facing: research/54's trace), `QuickCommandFollowPlayer(iSquadMember)`; the squad index
// is the squad menu's. Powers and weapons aren't dispatched yet (the squad weapon menu gives weapons). `mevr order
// <line>` gives any action line (`ALL|-|-|attack|E1`) through the same path. `[SquadOrders]` in ASI\LE1MEVR.ini.
namespace mevr::game::squad_orders {

struct Link {
    bool menuOpen = false; // main menu or wrist rail owns inputs
    // Unused since the left-grip gesture orders went (2026-11-18); kept so the callers' links stay as they are.
    std::optional<std::uint32_t> leftGrip;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
};

// Game thread, each tick after the squad context's.
void Tick(double deltaSeconds, Link const& link);
// Refresh before voice callbacks this tick.
void SetMenuOpen(bool open);

// Any order: validated against the squad context's scene, resolved, called. The outcome in words (the log's).
std::string Give(vr::squad_context::Action const& action);
// A move or a hold to a place in the world (game units): what game/SquadPlace's arc gives. untilCalledBack: a hold kept until a
// follow (the standing order's "until I say so" and "stay there"), not ended by the calm.
std::string GiveAt(vr::squad_context::Action const& action, engine::UnrealVector const& point, bool untilCalledBack = false);
// Spoken words (typed, until a recognizer feeds them): the rule parser against the scene, each clause's deictics resolved
// from the pose history `secondsAgo`, each action given as above; below [SquadOrders] minConfidence nothing is given and the
// unresolved words are the answer (where a model would take over). The outcomes, one line per clause.
std::string Say(std::string_view transcript, double secondsAgo = 0.5);

std::string StatusJson();
void LogStats();
void Shutdown();

}  // namespace mevr::game::squad_orders
