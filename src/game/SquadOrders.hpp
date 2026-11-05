// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"

#include <cstdint>
#include <optional>
#include <string>

// The flat squad orders in VR (research/54 "The flat orders"; the user: the game's own orders must work without voice)
// and the one dispatcher every order path ends in. The gesture (rules in core/vr/SquadGesture): a LEFT grip squeeze
// that no other feature claimed (the host counts the squeezes that began away from the support grip's zone;
// game/throwables and game/vent claim theirs first) points an order with the left aim ray; letting go gives it: on
// an enemy group, attack; on the ground farther than followWithinM, move there; near Shepard, on a squadmate, or on
// nothing, follow. A squadmate the ray pointed at when the squeeze began gets the order alone. The game's calls, on
// the game thread: `BioPlayerController.QuickCommandAttackTarget(iSquadMember, pTarget)`, `QuickCommandMoveTo
// (iSquadMember)` with the controller's Rotation turned toward the point for the call (the native drops the point
// along the controller's facing: research/54's trace), `QuickCommandFollowPlayer(iSquadMember)`; the squad index
// is the squad menu's. Powers and weapons aren't dispatched yet (the squad weapon menu gives weapons). `mevr order
// <line>` gives any action line (`ALL|-|-|attack|E1`) through the same path. `[SquadOrders]` in ASI\LE1MEVR.ini.
namespace mevr::game::squad_orders {

struct Link {
    // HostServices::leftGripAway (v11): nullopt with an older host (our own latch of the grip stands in).
    std::optional<std::uint32_t> leftGrip;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
};

// Game thread, each tick after the squad context's.
void Tick(double deltaSeconds, Link const& link);

// Any order: validated against the squad context's scene, resolved, called. The outcome in words (the log's).
std::string Give(vr::squad_context::Action const& action);
// Spoken words (typed, until a recognizer feeds them): the rule parser against the scene, each clause's deictics resolved
// from the pose history `secondsAgo`, each action given as above; below [SquadOrders] minConfidence nothing is given and the
// unresolved words are the answer (where a model would take over). The outcomes, one line per clause.
std::string Say(std::string_view transcript, double secondsAgo = 0.5);

std::string StatusJson();
void LogStats();
void Shutdown();

}  // namespace mevr::game::squad_orders
