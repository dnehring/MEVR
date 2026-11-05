// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/SquadContext.hpp"
#include "ue3/Types.hpp"

#include <string>
#include <string_view>

// The squad's acknowledgement of an order (research/54 "The acknowledgement"; the user, 2026-11-05: squadmates answer a
// move with a line, and the game has a sound for it). The game's own natives (QuickCommandFollowPlayer traced it) raise
// `BioBaseSquad.SquadHandleChatterEvent`, which picks a squadmate and plays one of the BioPawn.EBioSoundSetType lines
// (GiveCommandFollow 48, StartMoveToConfirmation 70, HoldingPosition 59, LOSestablished 61, MoveToSuccess 64...). This
// module expects one after every order the dispatcher gives (not the standing re-asserts, which are silent on purpose),
// logs which set answered and how long it took, and when nothing answers within `ackFallbackS` plays the order's set
// itself through `BioPawn.PlaySoundSet` on the squadmate (`[SquadOrders] ackFallback`, `ackMoveSet`, `ackHoldSet`,
// `ackAttackSet`; 0 = no line for that order), so a spoken or pointed order is always answered. Which sets the natives
// play for move and attack is [verify] (the trace heard only the follow's).
namespace mevr::game::squad_ack {

struct Settings {
    bool fallback = true;
    double fallbackS = 1.0;
    int moveSet = 70;    // StartMoveToConfirmation
    int holdSet = 59;    // HoldingPosition
    int attackSet = 61;  // LOSestablished [verify: the closest to "target in sight"]
};

// An order was just given to this squadmate (game thread). `order` decides the fallback's line.
void Expect(ue3::Object* pawn, char letter, std::string_view name, vr::squad_context::Order order, double clock);
// Game thread, each tick: installs the chatter handlers once the functions resolve, runs the fallbacks.
void Tick(double clock, Settings const& settings);

std::string StatusJson();
void LogStats();
void Shutdown();

}  // namespace mevr::game::squad_ack
