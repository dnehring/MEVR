// SPDX-License-Identifier: MIT
// A person walked to a point by the game's own AI (game/AiWalk.hpp).
#include "game/AiWalk.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/ConversationWorld.hpp"
#include "engine/CallFunction.hpp"
#include "engine/NameLookup.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string_view>
#include <vector>

namespace mevr::game::ai_walk {
namespace {

namespace cw = vr::conversation_world;

// ---- Each game's names (the adapter) ---------------------------------------------------------------------------------------
ue3::ClassRef gPawnClass{"Engine.Pawn", ue3::kEveryGame};
ue3::FieldRef gController{"Engine.Pawn", "Controller", ue3::kEveryGame};
ue3::FieldRef gHidden{"Engine.Actor", "bHidden", ue3::kEveryGame};
ue3::ClassRef gAiClass{"SFXGame.BioAiController", ue3::kEveryGame};
#if defined(MEVR_GAME_LE1)
constexpr char kMoveName[] = "PushMoveToPoint";
ue3::FunctionRef gMove{"SFXGame.BioAiController.PushMoveToPoint", ue3::kEveryGame};
ue3::ClassRef gMoveDataClass{"SFXGame.BioSeqData_MoveToPoint", ue3::kEveryGame};
ue3::FieldRef gMoveTerminate{"SFXGame.BioSeqData_MoveToPoint", "Terminate", ue3::kEveryGame};
ue3::FunctionRef gAllowMovement{"SFXGame.BioAiController.DoesGameModeAllowMovement", ue3::kEveryGame};
ue3::FunctionRef gAllowMovementHenchman{"SFXGame.BioAI_Henchman.DoesGameModeAllowMovement", ue3::kEveryGame};
ue3::FieldRef gInGestures{"SFXGame.BioPawn", "m_bInGestures", ue3::kEveryGame};
ue3::FunctionRef gSetDesiredSpeed{"SFXGame.BioPawn.SetDesiredSpeed", ue3::kEveryGame};
#elif defined(MEVR_GAME_LE2)
// PrecisionMoveTo moves a person in and after the conversation mode, slowly (Tali and Prazza 2.76 m in 9 s on Freedom's
// Progress, lane EXIT2, 2026-11-20); MoveToSlot (ME3's) moved neither of them (stalled at 0.00 m).
constexpr char kMoveName[] = "PrecisionMoveTo";
ue3::FunctionRef gMove{"SFXGame.BioAiController.PrecisionMoveTo", ue3::kEveryGame};
ue3::FunctionRef gStop{"SFXGame.BioPawn.StopMovement", ue3::kEveryGame};
ue3::FunctionRef gSetDesiredSpeed{"Engine.Pawn.SetDesiredSpeed", ue3::kEveryGame};
#else
constexpr char kMoveName[] = "MoveToSlot";
ue3::FunctionRef gMove{"SFXGame.BioAiController.MoveToSlot", ue3::kEveryGame};
ue3::FunctionRef gStop{"Engine.Controller.StopLatentExecution", ue3::kEveryGame};
#endif
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
ue3::FieldRef gBehavior{"Engine.Actor", "oBioComponent", ue3::kEveryGame};
ue3::ClassRef gEpicBehaviorClass{"SFXGame.BioEpicPawnBehavior", ue3::kEveryGame};
ue3::FieldRef gAllEnabled{"SFXGame.BioEpicPawnBehavior", "m_nALLEnabled", ue3::kEveryGame};
ue3::FieldRef gStackEnabled{"SFXGame.BioEpicPawnBehavior", "m_nStackEnabled", ue3::kEveryGame};
ue3::FunctionRef gAbilityEnable{"SFXGame.BioActorBehavior.AbilityEnable", ue3::kEveryGame};
#endif

std::vector<ue3::Object*> gAnswered;  // controllers whose movement-ban question is answered true (walkers only)
bool gStarted = false;
unsigned gWalks = 0, gArrived = 0, gStalled = 0, gRefused = 0, gAnswers = 0;
std::string gLastWhy;

double Flat(ue3::Vector const& a, ue3::Vector const& b) {
    return std::hypot(static_cast<double>(a.x) - b.x, static_cast<double>(a.y) - b.y);
}

#if defined(MEVR_GAME_LE1)
bool OnAllowMovement(ue3::Object* context, ue3::Function*, ue3::Function*, std::uint8_t*, void* result) {
    if (result == nullptr || std::find(gAnswered.begin(), gAnswered.end(), context) == gAnswered.end()) return false;
    *static_cast<std::uint32_t*>(result) = 1;
    ++gAnswers;
    return true;
}
#endif

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
// The game's own switch of the AI's subtype (BioActorBehavior.AbilityEnable), else the counter written (ME1's RaiseAiCounter).
int Raise(ue3::FieldRef& field, ue3::Object* behavior, std::wstring_view subtype) {
    std::int32_t* const value = field.In<std::int32_t>(behavior);
    int const by = value != nullptr ? cw::AiEnableDeficit(*value) : 0;
    if (by == 0) return 0;
    ue3::Function* const f = gAbilityEnable.Get();
    std::optional<ue3::Name> const name = engine::name_lookup::Find(subtype);
    if (f != nullptr && name) {
        for (int i = 0; i < by; ++i) {
            ue3::Params p(ue3::runtime::Get(), f);
            if (!p.Valid() || !p.SetBool("bEnable", true) || !p.Set("eEnableType", std::uint8_t{0}) || !p.Set("nmSubType", *name) ||
                !ue3::runtime::Call(behavior, f, p)) {
                break;
            }
        }
        if (field.Read<std::int32_t>(behavior).value_or(0) > 0) return by;
    }
    *value = 1;
    write_journal::Wrote("AiWalk.aiEnabled", value, sizeof(*value));
    return by;
}

void Lower(ue3::FieldRef& field, ue3::Object* behavior, int by) {
    std::int32_t* const value = by > 0 ? field.In<std::int32_t>(behavior) : nullptr;
    if (value == nullptr) return;
    *value -= by;
    write_journal::Wrote("AiWalk.aiEnabled", value, sizeof(*value));
}
#endif

bool SetSpeed(ue3::Object* pawn, float speed) {
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
    ue3::Function* const f = gSetDesiredSpeed.Get();
    if (f == nullptr || pawn == nullptr || !ue3::runtime::IsAlive(pawn)) return false;
    ue3::Params p(ue3::runtime::Get(), f);
#if defined(MEVR_GAME_LE2)
    // ME2's Engine.Pawn.SetDesiredSpeed(fSpeedScaling): with ME1's names every call failed and the speed was never set (lane
    // EXIT2; the walk's slow pace stayed with the fix: it is PrecisionMoveTo's own).
    return p.Valid() && p.Set("fSpeedScaling", speed) && ue3::runtime::Call(pawn, f, p);
#else
    return p.Valid() && p.Set("fDesiredSpeed", speed) && p.SetBool("bForce", false) && ue3::runtime::Call(pawn, f, p);
#endif
#else
    (void)pawn;
    (void)speed;
    return false;
#endif
}

// The game's AI move to the point (each game's function); the move's data object where the game returns one (ME1).
char const* AskMove(Walk& w) {
    ue3::Function* const fn = gMove.Get();
    if (fn == nullptr) return "the game's AI move function is missing";
    ue3::Reflection& r = ue3::runtime::Get();
    if (r.FindFunction(r.ClassOf(w.controller), kMoveName) != fn) return "the controller's class overrides the AI move";
    ue3::Params p(r, fn);
#if defined(MEVR_GAME_LE1)
    if (!p.Valid() || !p.Set("vLoc", w.to) || !p.Set("PointTolerance", 10.0f) || !p.SetBool("bWalking", true) ||
        !p.SetBool("bRequiresPrecisionMovement", true)) {
        return "PushMoveToPoint parameter lookup failed";
    }
    if (!ue3::runtime::Call(w.controller, fn, p)) return "PushMoveToPoint call failed";
    w.moveAction = p.Return<ue3::Object*>().value_or(nullptr);
    if (w.moveAction == nullptr || !ue3::runtime::IsAlive(w.moveAction) || !ue3::IsA(w.moveAction, gMoveDataClass)) {
        w.moveAction = nullptr;
        return "PushMoveToPoint returned no live move action";
    }
    return nullptr;
#elif defined(MEVR_GAME_LE2)
    ue3::Rotator const keep = player::Rotation(w.pawn).value_or(ue3::Rotator{});
    if (!p.Valid() || !p.Set("vLoc", w.to) || !p.Set("rRot", keep)) return "PrecisionMoveTo parameter lookup failed";
    return ue3::runtime::Call(w.controller, fn, p) ? nullptr : "PrecisionMoveTo call failed";
#else
    if (!p.Valid() || !p.Set("vSlotLocation", w.to)) return "MoveToSlot parameter lookup failed";
    return ue3::runtime::Call(w.controller, fn, p) ? nullptr : "MoveToSlot call failed";
#endif
}

void Release(Walk& w) {
    if (w.controller != nullptr) SetSpeed(w.pawn, 0.0f);  // as the move state's own Cleanup
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
    if (w.behavior != nullptr && ue3::runtime::IsAlive(w.behavior) && ue3::IsA(w.behavior, gEpicBehaviorClass)) {
        Lower(gAllEnabled, w.behavior, w.allRaised);
        Lower(gStackEnabled, w.behavior, w.stackRaised);
    }
#endif
    if (w.answered) std::erase(gAnswered, w.controller);
    w.answered = false;
    w.allRaised = w.stackRaised = 0;
    w.behavior = nullptr;
}

} // namespace

char const* Name(Phase phase) {
    switch (phase) {
    case Phase::Walking: return "walking";
    case Phase::Arrived: return "arrived";
    case Phase::Stalled: return "stalled";
    case Phase::Ended: return "ended";
    }
    return "?";
}

void TryStartup() {
    if (gStarted || !ue3::runtime::Up()) return;
    gStarted = true;
#if defined(MEVR_GAME_LE1)
    ue3::Reflection& reflection = ue3::runtime::Get();
    for (ue3::Function* const f : {gAllowMovement.Get(), gAllowMovementHenchman.Get()}) {
        if (f != nullptr && reflection.Parameters(f).size() == 1 && reflection.ParmsSize(f) == 4) {
            engine::call_function::AddAnswerHandler(f, &OnAllowMovement);
        }
    }
#endif
}

std::optional<Walk> Start(ue3::Object* pawn, ue3::Vector const& to, std::string& why) {
    auto refuse = [&](char const* text) -> std::optional<Walk> {
        why = text;
        gLastWhy = text;
        ++gRefused;
        return std::nullopt;
    };
    if (!ue3::runtime::Up()) return refuse("the reflection layer is not up");
#if defined(MEVR_GAME_LE1)
    // The movement-ban answer must be registered at startup, before the CallFunction hook dispatches (never from a handler:
    // the answer list would grow while it is walked). Mod.cpp calls TryStartup where game/ConversationWorld's own is called.
    if (!gStarted) return refuse("ai_walk::TryStartup was not called at startup");
#else
    TryStartup();
#endif
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn) || !ue3::IsA(pawn, gPawnClass)) return refuse("not a live Engine.Pawn");
    ue3::Object* const ai = gController.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (ai == nullptr || !ue3::runtime::IsAlive(ai)) return refuse("no live controller");
    if (!ue3::IsA(ai, gAiClass)) return refuse("the controller is not a BioAiController");
    // The AI runs no state code for a hidden pawn, and nobody sees a hidden person walk (ME1's rule).
    if (gHidden.ReadBool(pawn).value_or(false)) return refuse("it is hidden");
    Walk w;
    w.pawn = pawn;
    w.controller = ai;
    w.to = to;
#if defined(MEVR_GAME_LE1)
    if (!engine::call_function::Installed() || gAllowMovement.Get() == nullptr) {
        return refuse("the conversation mode's ban on AI movement can't be answered (no CallFunction hook)");
    }
#endif
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
    // The AI's hold for the walk, before the move is asked (its native checks the counters at once).
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    if (behavior != nullptr && ue3::runtime::IsAlive(behavior) && ue3::IsA(behavior, gEpicBehaviorClass)) {
        w.behavior = behavior;
        w.allRaised = Raise(gAllEnabled, behavior, L"ALL");
        w.stackRaised = Raise(gStackEnabled, behavior, L"Stack");
    }
#endif
    gAnswered.push_back(ai);
    w.answered = true;
    if (char const* const failed = AskMove(w)) {
        Release(w);
        return refuse(failed);
    }
#if defined(MEVR_GAME_LE1)
    // The gesture mode plays gestures over the locomotion: left for the walk, put back after.
    if (gInGestures.ReadBool(pawn).value_or(false) && gInGestures.WriteBool(pawn, false)) w.gesturesCleared = true;
#endif
    SetSpeed(pawn, 1.0f);
    std::uint64_t const now = GetTickCount64();
    w.startMs = w.progressMs = now;
    w.progressAt = player::Location(pawn).value_or(to);
    w.phase = Phase::Walking;
    ++gWalks;
    log::Info("ai walk: {} walks {:.2f} m to ({:.0f}, {:.0f}, {:.0f}) with the game's AI{}", ue3::runtime::FullName(pawn),
        Flat(w.progressAt, to) / 100.0, to.x, to.y, to.z, w.allRaised + w.stackRaised > 0 ? " (its AI switched on for the walk)" : "");
    return w;
}

Phase Step(Walk& w) {
    if (w.phase != Phase::Walking) return w.phase;
    if (!ue3::runtime::IsAlive(w.pawn) || !ue3::runtime::IsAlive(w.controller)) {
        w.phase = Phase::Stalled;
        return w.phase;
    }
    SetSpeed(w.pawn, 1.0f);  // the paused controller's speed, each tick (the game may zero it again)
    auto const at = player::Location(w.pawn);
    if (!at) return w.phase;
    std::uint64_t const now = GetTickCount64();
    if (Flat(*at, w.to) <= kArriveUnits) {
        w.phase = Phase::Arrived;
        ++gArrived;
    } else if (Flat(*at, w.progressAt) >= kStallUnits) {
        w.progressAt = *at;
        w.progressMs = now;
    } else if (static_cast<double>(now - w.progressMs) >= kStallS * 1000.0) {
        w.phase = Phase::Stalled;
        ++gStalled;
    }
    return w.phase;
}

void End(Walk& w, char const* why, bool tearingDown) {
    if (w.phase == Phase::Ended && w.controller == nullptr) return;
    Release(w);
#if defined(MEVR_GAME_LE1)
    if (w.moveAction != nullptr && ue3::runtime::IsAlive(w.moveAction) && ue3::IsA(w.moveAction, gMoveDataClass)) {
        gMoveTerminate.WriteBool(w.moveAction, true);  // journalled by FieldRef
    }
    if (w.gesturesCleared && !tearingDown && ue3::runtime::IsAlive(w.pawn)) gInGestures.WriteBool(w.pawn, true);
#else
    (void)tearingDown;
    if (ue3::Function* const stop = gStop.Get(); stop != nullptr && w.phase == Phase::Walking) {
#if defined(MEVR_GAME_LE2)
        ue3::Object* const target = w.pawn;        // BioPawn.StopMovement
#else
        ue3::Object* const target = w.controller;  // Controller.StopLatentExecution
#endif
        if (target != nullptr && ue3::runtime::IsAlive(target)) {
            ue3::Params p(ue3::runtime::Get(), stop);
            if (p.Valid()) ue3::runtime::Call(target, stop, p);
        }
    }
#endif
    log::Info("ai walk: {} {} ({})", ue3::runtime::IsAlive(w.pawn) ? ue3::runtime::FullName(w.pawn) : std::string("a gone pawn"),
        Name(w.phase), why);
    w.moveAction = nullptr;
    w.gesturesCleared = false;
    w.controller = nullptr;
    w.phase = Phase::Ended;
}

std::string StatusJson() {
    return std::format("{{\"walks\": {}, \"arrived\": {}, \"stalled\": {}, \"refused\": {}, \"answers\": {}, \"walking\": {}, "
                       "\"lastRefusal\": \"{}\"}}",
        gWalks, gArrived, gStalled, gRefused, gAnswers, gAnswered.size(), gLastWhy);
}

} // namespace mevr::game::ai_walk
