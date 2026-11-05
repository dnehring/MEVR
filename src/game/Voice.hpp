// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Voice.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// The mod's side of the voice orders (research/54): the radio gesture (rules in core/vr/VoiceGesture: a new LEFT grip
// squeeze with the hand at the left ear, nobody else's, starts a listen; letting go ends it), the host told to listen
// (HostServices v31), the key terms from the roster (the squadmates present, their powers' spoken names), the lines the
// recognizer returns parsed and dispatched through game/SquadOrders' Say: a complete line as one order set, a partial that
// settled for settleMs and parses to a complete, confident order without a power dispatched early (the research's semantic
// endpoint), the rest at the final. A refusal is a double buzz in the left hand, a dispatch one tick. `[Voice]` in
// ASI\LE1MEVR.ini (the mod's keys: gesture, radioRadiusM, earSideM, earDownM, minHoldS, maxHoldS, earlyDispatch, settleMs,
// deicticLagS); `mevr voice wav <path> | listen <s> | keyterms <a,b> | status`. Log `voice:`; snapshot "mod"."voice".
namespace mevr::game::voice {

struct Link {
    bool has = false;  // the host has the voice services (v31)
    void (*setListen)(int on) = nullptr;
    bool (*takeLine)(vr::voice::Line* out) = nullptr;
    void (*state)(vr::voice::State* out) = nullptr;
    void (*setKeyterms)(char const* terms) = nullptr;
    void (*command)(char const* line) = nullptr;
    std::uint32_t (*askModel)(char const* prefix, char const* rest, char const* gbnf) = nullptr;  // null: no model tier
    bool (*takeModelAnswer)(std::uint32_t* id, char* text, std::uint32_t capacity, std::uint32_t* ms) = nullptr;
    std::optional<std::uint32_t> leftGrip;  // HostServices::leftGripAway (v11)
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
};

// Game thread, each tick, before the flat orders' gesture (which must not see the squeeze this claimed).
void Tick(double deltaSeconds, Link const& link);
// True on the tick a listen started: the squeeze is the radio's.
bool ClaimedLeft();
// `mevr voice ...`.
std::string Console(std::string_view rest);

std::string StatusJson();
void LogStats();
void Shutdown();

}  // namespace mevr::game::voice
