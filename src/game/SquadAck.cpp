// SPDX-License-Identifier: MIT
#include "game/SquadAck.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <format>
#include <vector>

namespace mevr::game::squad_ack {

namespace sc = vr::squad_context;
namespace json = vr::test_request;

namespace {

// The squad's chatter event (an event the natives raise: ProcessEvent) and the pawn's own line (script calls it from
// script, so the CallFunction hook hears it; a native raising it would come through ProcessEvent, hooked too).
ue3::FunctionRef gChatterFn{"SFXGame.BioBaseSquad.SquadHandleChatterEvent"};
ue3::FieldRef gChatterSet{gChatterFn, "eSoundSet"};
ue3::FunctionRef gPlaySoundSetFn{"SFXGame.BioPawn.PlaySoundSet"};
ue3::FieldRef gPlaySoundType{gPlaySoundSetFn, "eType"};

struct Expectation {
    ue3::Object* pawn = nullptr;
    char letter = 0;
    std::string name;
    sc::Order order = sc::Order::None;
    double given = 0.0;
};
std::vector<Expectation> gExpected;
bool gInstalled = false;
bool gToldMissing = false;
bool gCalling = false;  // our own fallback PlaySoundSet, not an answer
double gClock = 0.0;
unsigned gExpectations = 0, gAnswered = 0, gFallbacks = 0, gUnanswered = 0;
std::string gLast;
int gLastSet = -1;
double gLastDelayS = 0.0;

char const* SetName(int set) {
    switch (set) {
    case 2: return "TargetDown";
    case 48: return "GiveCommandFollow";
    case 56: return "Alert";
    case 59: return "HoldingPosition";
    case 61: return "LOSestablished";
    case 62: return "LOSunavailable";
    case 63: return "MoveToFailure";
    case 64: return "MoveToSuccess";
    case 70: return "StartMoveToConfirmation";
    case 77: return "Allclear";
    default: return "?";
    }
}

char const* OrderName(sc::Order order) {
    switch (order) {
    case sc::Order::Move: return "move";
    case sc::Order::Hold: return "hold";
    case sc::Order::Attack: return "attack";
    case sc::Order::Follow: return "follow";
    case sc::Order::Regroup: return "regroup";
    case sc::Order::Use: return "use";
    default: return "order";
    }
}

// Something answered: the oldest expectation (the squad's event names no member) or the pawn's own.
void Answered(ue3::Object* pawn, int set, char const* how) {
    if (gCalling) return;
    auto it = gExpected.end();
    if (pawn != nullptr) {
        it = std::find_if(gExpected.begin(), gExpected.end(), [&](Expectation const& e) { return e.pawn == pawn; });
    }
    if (it == gExpected.end() && pawn == nullptr && !gExpected.empty()) it = gExpected.begin();
    if (it == gExpected.end()) return;
    ++gAnswered;
    gLastSet = set;
    gLastDelayS = gClock - it->given;
    gLast = std::format("{} answered the {} with sound set {} ({}) after {:.2f} s ({})", it->name, OrderName(it->order), set, SetName(set), gLastDelayS, how);
    log::Info("squad orders: {}", gLast);
    gExpected.erase(it);
}

void OnChatter(ue3::Object* /*squad*/, ue3::Function* /*function*/, void* parms, void* /*result*/) {
    std::uint8_t const* const set = gChatterSet.In<std::uint8_t>(parms);
    Answered(nullptr, set != nullptr ? *set : -1, "the squad's chatter event");
}

void OnPlaySoundSetEvent(ue3::Object* pawn, ue3::Function* /*function*/, void* parms, void* /*result*/) {
    std::uint8_t const* const set = gPlaySoundType.In<std::uint8_t>(parms);
    Answered(pawn, set != nullptr ? *set : -1, "the pawn's PlaySoundSet");
}

void OnPlaySoundSetCall(ue3::Object* pawn, ue3::Function* /*function*/, void* /*result*/) {
    Answered(pawn, -1, "the pawn's PlaySoundSet (from script)");
}

void Install() {
    if (gInstalled || !ue3::runtime::Ready()) return;
    ue3::Function* const chatter = gChatterFn.Get();
    ue3::Function* const play = gPlaySoundSetFn.Get();
    if (chatter == nullptr && play == nullptr) {
        if (!gToldMissing) {
            gToldMissing = true;
            log::Warn("squad orders: neither BioBaseSquad.SquadHandleChatterEvent nor BioPawn.PlaySoundSet was found: acknowledgements aren't heard");
        }
        gInstalled = true;
        return;
    }
    if (chatter != nullptr) engine::process_event::AddPostHandler(chatter, &OnChatter);
    if (play != nullptr) {
        engine::process_event::AddPostHandler(play, &OnPlaySoundSetEvent);
        engine::call_function::AddPostHandler(play, &OnPlaySoundSetCall);
    }
    gInstalled = true;
    log::Info("squad orders: acknowledgements heard from {}{}{}", chatter != nullptr ? "BioBaseSquad.SquadHandleChatterEvent" : "",
        chatter != nullptr && play != nullptr ? " and " : "", play != nullptr ? "BioPawn.PlaySoundSet" : "");
}

int FallbackSet(sc::Order order, Settings const& s) {
    switch (order) {
    case sc::Order::Move: return s.moveSet;
    case sc::Order::Hold: return s.holdSet;
    case sc::Order::Attack: return s.attackSet;
    default: return 0;  // the follow's line is the game's; a power order answers with the cast
    }
}

}  // namespace

void Expect(ue3::Object* pawn, char letter, std::string_view name, sc::Order order, double clock) {
    if (pawn == nullptr) return;
    gClock = clock;
    // One expectation per squadmate: a new order replaces the last unanswered one.
    gExpected.erase(std::remove_if(gExpected.begin(), gExpected.end(), [&](Expectation const& e) { return e.pawn == pawn; }), gExpected.end());
    gExpected.push_back({pawn, letter, std::string(name), order, clock});
    ++gExpectations;
}

void Tick(double clock, Settings const& s) {
    gClock = clock;
    Install();
    for (auto it = gExpected.begin(); it != gExpected.end();) {
        if (clock - it->given < s.fallbackS) {
            ++it;
            continue;
        }
        int const set = FallbackSet(it->order, s);
        ue3::Function* const play = gPlaySoundSetFn.Get();
        if (s.fallback && set > 0 && play != nullptr && ue3::runtime::IsAlive(it->pawn)) {
            ue3::Params parms(ue3::runtime::Get(), play);
            parms.Set("eType", static_cast<std::uint8_t>(set));
            gCalling = true;
            bool const called = ue3::runtime::Call(it->pawn, play, parms);
            gCalling = false;
            ++gFallbacks;
            gLast = std::format("{} didn't answer the {} within {:.1f} s: PlaySoundSet({} {}) {} -> {}", it->name, OrderName(it->order), s.fallbackS, set,
                SetName(set), called ? "called" : "not called", called ? (parms.ReturnBool().value_or(false) ? "true" : "false") : "-");
        } else {
            ++gUnanswered;
            gLast = std::format("{} didn't answer the {} within {:.1f} s (no fallback line for it)", it->name, OrderName(it->order), s.fallbackS);
        }
        log::Info("squad orders: {}", gLast);
        it = gExpected.erase(it);
    }
}

std::string StatusJson() {
    return std::format("{{\"pending\": {}, \"expected\": {}, \"answered\": {}, \"fallbacks\": {}, \"unanswered\": {}, \"lastSet\": {}, \"lastDelayS\": {}, \"last\": {}}}",
        gExpected.size(), gExpectations, gAnswered, gFallbacks, gUnanswered, gLastSet, json::JsonNumber(gLastDelayS), json::JsonString(gLast));
}

void LogStats() {
    if (gExpectations == 0) return;
    log::Info("squad orders: acknowledgements: {} expected, {} answered, {} by our fallback line, {} unanswered{}", gExpectations, gAnswered, gFallbacks,
        gUnanswered, gLast.empty() ? std::string() : std::format("; last: {}", gLast));
}

void Shutdown() {
    gExpected.clear();
}

}  // namespace mevr::game::squad_ack
