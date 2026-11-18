// SPDX-License-Identifier: MIT
#include "game/SquadAck.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <cstring>
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

// The definitive one: a chatter line played on a pawn (native raises BioPawn.OnChatterLinePlayed after the sound starts;
// the trace of 2026-11-05 12:30: a follow made SquadHandleChatterEvent a SCRIPT call, so only this and HandleChatterEvent's
// CallFunction see it). On the player's own pawn it's Shepard's bark ("regroup!"), logged, not an answer.
void OnChatterLinePlayed(ue3::Object* pawn, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (gCalling) return;
    if (pawn != nullptr && pawn == player::BioPawn(player::Controller())) {
        if (!gExpected.empty()) log::Info("squad orders: Shepard's own line played ({:.2f} s after the order)", gClock - gExpected.front().given);
        return;
    }
    Answered(pawn, -1, "a chatter line played on the pawn");
}

void OnHandleChatterCall(ue3::Object* pawn, ue3::Function* /*function*/, void* /*result*/) {
    if (gCalling || pawn == nullptr || pawn == player::BioPawn(player::Controller())) return;
    for (Expectation const& e : gExpected) {
        if (e.pawn == pawn) log::Info("squad orders: the chatter event reached {} ({:.2f} s after the {})", e.name, gClock - e.given, OrderName(e.order));
    }
}

// Every function of the name: the player's squad is a BioSquadPlayer and the squadmates SFXPawn_* subclasses, and an
// override (or a state's version) is another UFunction than BioBaseSquad's or BioPawn's (the first build hooked only those
// and heard nothing in the game, 2026-11-05).
std::string HookAll(char const* name, engine::process_event::Handler onEvent, engine::call_function::Handler onCall) {
    std::string names;
    ue3::Reflection& reflection = ue3::runtime::Get();
    for (ue3::Function* function : reflection.FindFunctionsNamed(name)) {
        std::string const full = reflection.FullName(function);
        if (!full.starts_with("Function ")) continue;
        if (onEvent != nullptr) engine::process_event::AddPostHandler(function, onEvent);
        if (onCall != nullptr) engine::call_function::AddPostHandler(function, onCall);
        names += std::format("{}{}", names.empty() ? "" : ", ", full.substr(9));
    }
    return names;
}

#if !defined(MEVR_GAME_LE1)
// ME2 and ME3 (the audits' PWR-37; research/54 "ME2 placement and orders on Save 4"): ME1's chatter functions don't exist;
// the squadmate's AI answers an order itself by a vocalization event (ESFXVocalizationEventID, the same numbers on both:
// ReceivedOrder_Attack 2..5, _Move 7, _Follow 8, _TakeCover 9, _Hold 10), through the game's TriggerVocalizationEvent (ME2's
// SFXGame, ME3's SFXGRI: VocalizationID, InstigatedBy) and the vocalization manager's (SFXVocalizationManager: Id,
// InInstigator). A script call's handler sees no parameters, so the answer is read from a frame that holds them: the event's
// own when native code raises it (ProcessEvent's parameters), else the calling function's Locals when one of them calls the
// other (a frame post-handler on each). Who the instigator is decides whose order it answers; nothing is played in its place.
struct VocalArgs {
    ue3::Function* function = nullptr;
    std::int32_t id = -1;          // the byte's offset in the function's frame, -1: none
    std::int32_t instigator = -1;  // the pawn pointer's offset
};
std::vector<VocalArgs> gVocalArgs;
unsigned gVocalHeard = 0;

VocalArgs const* ArgsOf(ue3::Function* function) {
    for (VocalArgs const& a : gVocalArgs) {
        if (a.function == function) return &a;
    }
    return nullptr;
}

char const* VocalName(int id) {
    switch (id) {
    case 2: return "ReceivedOrder_Attack";
    case 3: return "ReceivedOrder_Attack_TargetValid";
    case 4: return "ReceivedOrder_Attack_MovingFirst";
    case 5: return "ReceivedOrder_Attack_KilledTarget";
    case 6: return "ReceivedOrder_ChangeWeapon";
    case 7: return "ReceivedOrder_Move";
    case 8: return "ReceivedOrder_Follow";
    case 9: return "ReceivedOrder_TakeCover";
    case 10: return "ReceivedOrder_Hold";
    default: return "another vocalization";
    }
}

void Vocalized(ue3::Function* frameFunction, std::uint8_t const* frame, char const* how) {
    VocalArgs const* const a = frame != nullptr ? ArgsOf(frameFunction) : nullptr;
    if (a == nullptr || gCalling) return;
    int const id = frame[a->id];
    ue3::Object* instigator = nullptr;
    std::memcpy(&instigator, frame + a->instigator, sizeof(instigator));
    ++gVocalHeard;
    auto it = std::find_if(gExpected.begin(), gExpected.end(), [&](Expectation const& e) { return e.pawn == instigator; });
    if (!gExpected.empty() && gVocalHeard <= 40 && (it == gExpected.end() || id < 2 || id > 10)) {
        // While an order waits: every other vocalization, so a run's log says what the AI did say (40 at most).
        log::Info("squad orders: vocalization {} ({}) by {} ({}), not an answer to a pending order", id, VocalName(id),
            instigator != nullptr && ue3::runtime::IsAlive(instigator) ? ue3::runtime::Get().FullName(instigator) : std::string("nobody"), how);
    }
    if (id < 2 || id > 10 || instigator == nullptr) return;  // not an order's answer
    if (it == gExpected.end()) return;
    ++gAnswered;
    gLastSet = id;
    gLastDelayS = gClock - it->given;
    gLast = std::format("{} answered the {} with vocalization {} ({}) after {:.2f} s ({})", it->name, OrderName(it->order), id,
        VocalName(id), gLastDelayS, how);
    log::Info("squad orders: {}", gLast);
    gExpected.erase(it);
}

void OnVocalEvent(ue3::Object* /*object*/, ue3::Function* function, void* parms, void* /*result*/) {
    Vocalized(function, static_cast<std::uint8_t const*>(parms), "the vocalization event");
}

void OnVocalFrame(ue3::Object* /*context*/, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* callerLocals,
    void* /*result*/) {
    Vocalized(caller, callerLocals, "the vocalization event's frame");
}

std::string InstallVocalization() {
    std::string names;
    ue3::Reflection& reflection = ue3::runtime::Get();
    for (ue3::Function* function : reflection.FindFunctionsNamed("TriggerVocalizationEvent")) {
        std::string const full = reflection.FullName(function);
        if (!full.starts_with("Function ")) continue;
        VocalArgs a{function};
        for (char const* name : {"VocalizationID", "Id"}) {
            if (auto const p = reflection.PropertyOf(function, name)) a.id = p->offset;
        }
        for (char const* name : {"InstigatedBy", "InInstigator"}) {
            if (auto const p = reflection.PropertyOf(function, name)) a.instigator = p->offset;
        }
        ue3::runtime::FlushNotes();
        if (a.id < 0 || a.instigator < 0) continue;
        gVocalArgs.push_back(a);
        engine::process_event::AddPostHandler(function, &OnVocalEvent);
        engine::call_function::AddFramePostHandler(function, &OnVocalFrame);
        names += std::format("{}{}", names.empty() ? "" : ", ", full.substr(9));
    }
    return names;
}
#endif

void Install() {
#if defined(MEVR_GAME_LE1)
    if (gInstalled || !ue3::runtime::Ready()) return;
#else
    if (gInstalled || !ue3::runtime::Up()) return;
    std::string const vocal = InstallVocalization();
    log::Info("squad orders: acknowledgements heard from the AI's vocalization events: {}", vocal.empty() ? "none found" : vocal);
#endif
    std::string const chatter = HookAll("SquadHandleChatterEvent", &OnChatter, nullptr);
    std::string const play = HookAll("PlaySoundSet", &OnPlaySoundSetEvent, &OnPlaySoundSetCall);
    std::string const played = HookAll("OnChatterLinePlayed", &OnChatterLinePlayed, nullptr);
    HookAll("HandleChatterEvent", nullptr, &OnHandleChatterCall);
    gInstalled = true;
#if !defined(MEVR_GAME_LE1)
    if (chatter.empty() && play.empty() && played.empty()) return;  // the sequels: ME1's functions aren't there
#endif
    if (chatter.empty() && play.empty() && played.empty()) {
        if (!gToldMissing) {
            gToldMissing = true;
            log::Warn("squad orders: no SquadHandleChatterEvent or PlaySoundSet function was found: acknowledgements aren't heard");
        }
        return;
    }
    log::Info("squad orders: acknowledgements heard from {}; {}; {}", chatter.empty() ? "-" : chatter, play.empty() ? "-" : play, played.empty() ? "-" : played);
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
#if defined(MEVR_GAME_LE1)
        if (clock - it->given < s.fallbackS) {
#else
        // No line of ours to play on the sequels (ME1's PlaySoundSet isn't there), so the window only waits for the AI's own
        // answer, which ME2's gives 1.0-1.5 s after a move order (lane AU4, 2026-11-18): at least 3 s.
        if (clock - it->given < std::max(s.fallbackS, 3.0)) {
#endif
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
