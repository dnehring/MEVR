// SPDX-License-Identifier: MIT
#include "game/Melee.hpp"
#include "core/Log.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"
#include <Windows.h>
#include <format>

namespace mevr::game::melee {
namespace {
bool gStarted = false;
std::string gStatus = "off";
ik::melee::Gestures gGestures;
#if defined(MEVR_GAME_LE3)
// Installed SFXGame scripts and pinned LE3 SDK, research/le3-melee-audit.md.
// Names and types are resolved; no LE2 controller action byte or copied damage implementation.
ue3::ClassRef gModeClass{"SFXGame.SFXGameModeDefault", ue3::kEveryGame};
ue3::ClassRef gInputClass{"SFXGame.BioPlayerInput", ue3::kEveryGame};
ue3::ClassRef gWeaponClass{"SFXGame.SFXWeapon", ue3::kEveryGame};
ue3::ClassRef gActionClass{"SFXGame.BioCustomAction", ue3::kEveryGame};
ue3::ClassRef gLightClass{"SFXGame.SFXCustomAction_PlayerMeleeBase", ue3::kEveryGame};
ue3::ClassRef gHeavyClass{"SFXGame.SFXCustomAction_PlayerHeavyMeleeBase", ue3::kEveryGame};
ue3::FieldRef gWorld{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
ue3::FieldRef gInput{"Engine.PlayerController", "PlayerInput", ue3::kEveryGame};
ue3::FieldRef gCurrentAction{"SFXGame.BioPawn", "CurrentCustomAction", ue3::kEveryGame};
ue3::FieldRef gActionPawn{"SFXGame.BioCustomAction", "m_oPawn", ue3::kEveryGame};
ue3::FunctionRef gGetMode{"SFXGame.BioPlayerController.GetGameModeDefault", ue3::kEveryGame};
ue3::FunctionRef gGetAction{"SFXGame.BioPawn.GetCurrentCustomAction", ue3::kEveryGame};
ue3::FunctionRef gReloading{"SFXGame.BioPlayerController.IsReloading", ue3::kEveryGame};
ue3::FunctionRef gZoomed{"SFXGame.BioPlayerController.IsZoomed", ue3::kEveryGame};
ue3::FunctionRef gCombat{"SFXGame.BioPlayerInput.IsCombatEnabled", ue3::kEveryGame};
ue3::FunctionRef gState{"Core.Object.GetStateName", ue3::kEveryGame};
ue3::FunctionRef gLight{"SFXGame.SFXGameModeDefault.TryMelee", ue3::kEveryGame};
ue3::FunctionRef gHeavy{"SFXGame.SFXGameModeDefault.TryHeavyMelee", ue3::kEveryGame};
struct Context {
    ue3::Object* world = nullptr;
    ue3::Object* pc = nullptr;
    ue3::Object* pawn = nullptr;
    ue3::Object* weapon = nullptr;
    std::uint64_t generation = 0;
    std::uint8_t mode = 255;
    bool operator==(Context const&) const = default;
};
Context gContext;
ue3::Object* gObserved = nullptr;
bool gAwaiting = false;
ik::melee::Intent gIntent = ik::melee::Intent::Light;
double gDeadline = 0;
double Now() { return static_cast<double>(GetTickCount64()) * 0.001; }
bool Shape(ue3::FieldRef& field, ue3::Kind kind, int bytes) {
    auto const* info = field.Get();
    return info != nullptr && info->kind == kind && info->elementSize == bytes && info->arrayDim == 1;
}
bool ReturnShape(ue3::FunctionRef& ref, ue3::Kind kind, int bytes) {
    ue3::Params params(ue3::runtime::Get(), ref.Get());
    auto const* value = params.ReturnSpec();
    return params.Valid() && params.Size() == bytes && params.Specs().size() == 1 && value != nullptr &&
        value->kind == kind && value->size == bytes && value->offset == 0;
}
ue3::Object* Object(ue3::FieldRef& field, ue3::Object* owner) {
    auto* object = field.Read<ue3::Object*>(owner).value_or(nullptr);
    return ue3::runtime::IsAlive(object) ? object : nullptr;
}
std::optional<bool> Bool(ue3::Object* owner, ue3::FunctionRef& ref) {
    auto* fn = ref.Get();
    ue3::Params params(ue3::runtime::Get(), fn);
    if (!params.Valid() || !ue3::runtime::Call(owner, fn, params)) return std::nullopt;
    return params.ReturnBool();
}
Context ReadContext(Link const& link) {
    Context out;
    out.generation = link.input.generation;
    out.pc = player::Controller();
    if (!ue3::runtime::IsAlive(out.pc)) { out.pc = nullptr; return out; }
    out.pawn = player::BioPawn(out.pc);
    if (!ue3::runtime::IsAlive(out.pawn)) out.pawn = nullptr;
    if (out.pawn) out.weapon = Object(gWeapon, out.pawn);
    out.world = Object(gWorld, out.pc);
    out.mode = player::GameMode(out.pc).value_or(255);
    return out;
}
void RefreshContext(Link const& link) {
    auto const context = ReadContext(link);
    if (!(context == gContext)) {
        if (gAwaiting || gObserved != nullptr) gStatus = "observation cancelled: scene/actor/mode changed (native action not modified)";
        gAwaiting = false;
        gObserved = nullptr;
        gGestures.Reset();
        gContext = context;
    }
}
// nullopt = failed query; nullptr = native reports no current action.
std::optional<ue3::Object*> CurrentAction() {
    if (gContext.pawn == nullptr) return std::nullopt;
    auto* fn = gGetAction.Get();
    ue3::Params params(ue3::runtime::Get(), fn);
    if (!params.Valid() || !ue3::runtime::Call(gContext.pawn, fn, params)) return std::nullopt;
    auto const has = params.ReturnBool();
    if (!has) return std::nullopt;
    if (!*has) return static_cast<ue3::Object*>(nullptr);
    auto* action = params.Get<ue3::Object*>("pAction").value_or(nullptr);
    if (!ue3::runtime::IsAlive(action) || !ue3::IsA(action, gActionClass) || Object(gActionPawn, action) != gContext.pawn)
        return std::nullopt;
    return action;
}
char const* Refusal(Link const& link, int hand) {
    if (!gStarted || !ue3::runtime::Up()) return "adapter not ready";
    if (!link.enabled || !link.focused || link.menuOpen || link.authoredSeated || !link.input.allowed)
        return "host context blocks melee";
    if (hand < 0 || hand > 1 || !link.input.hands[static_cast<std::size_t>(hand)].available ||
        link.allowRequest == nullptr || !link.allowRequest(hand)) return "hand ownership blocks melee";
    if (gContext.world == nullptr || gContext.pc == nullptr || gContext.pawn == nullptr || gContext.mode != 0)
        return "not ordinary Shepard gameplay";
    // The movement facts through the one admission read (game/Player), asked in this function's own order of reasons.
    namespace admit = player::admission;
    if (!player::MovementAdmitted(gContext.pc, gContext.pawn, admit::kMoveInput | admit::kAlive))
        return "movement ignored, dead, or unavailable native guard";
    auto* input = Object(gInput, gContext.pc);
    if (!ue3::IsA(input, gInputClass) || Bool(input, gCombat) != true) return "combat input unavailable";
    if (!player::MovementAdmitted(gContext.pc, gContext.pawn, admit::kOutOfCover | admit::kNoCoverAction | admit::kNotStorming))
        return "contextual cover/storm melee requires separate intent";
    auto const current = CurrentAction();
    if (!current || *current != nullptr || gAwaiting || gObserved != nullptr) return "native action or unresolved admission pending";
    if (Bool(gContext.pc, gReloading) != false || Bool(gContext.pc, gZoomed) != false) return "reload or zoom blocks melee";
    auto* weapon = Object(gWeapon, gContext.pawn);
    if (!ue3::IsA(weapon, gWeaponClass)) return "no supported drawn weapon";
    auto* fn = gState.Get();
    ue3::Params params(ue3::runtime::Get(), fn);
    if (!params.Valid() || !ue3::runtime::Call(weapon, fn, params)) return "weapon state unavailable";
    auto const state = params.Return<ue3::Name>();
    if (!state) return "weapon state unavailable";
    auto const name = ue3::runtime::NameText(*state);
    if (ue3::names::EqualsNoCase(name, "WeaponEquipping") || ue3::names::EqualsNoCase(name, "WeaponPuttingDown") ||
        ue3::names::EqualsNoCase(name, "Inactive") || ue3::names::EqualsNoCase(name, "None"))
        return "weapon not ready";
    return nullptr;
}
void Observe() {
    if (!gAwaiting && gObserved == nullptr) return;
    auto const current = CurrentAction();
    if (!current) {
        gStatus = "native action observation unavailable";
        gAwaiting = false;
        gObserved = nullptr;
        return;
    }
    if (gObserved != nullptr && *current != gObserved) {
        gStatus = "observed action left current slot; impact/damage/completion not established";
        gObserved = nullptr;
    }
    if (gAwaiting && *current != nullptr) {
        bool const matches = ue3::IsA(*current, gIntent == ik::melee::Intent::Heavy ? gHeavyClass : gLightClass);
        gStatus = std::format("observed {} action {} id {} (impact not established)", matches ? "requested family" : "different family",
            ue3::runtime::FullName(*current), gCurrentAction.Read<std::int32_t>(gContext.pawn).value_or(-1));
        log::Info("melee: {}", gStatus);
        gObserved = *current;
        gAwaiting = false;
    } else if (gAwaiting && Now() >= gDeadline) {
        gStatus = "dispatched; no action start observed in admission window";
        gAwaiting = false;
    }
}
#endif
} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE3)
    if (!ue3::runtime::Up()) return false;
    for (auto* cls : {&gModeClass, &gInputClass, &gWeaponClass, &gActionClass, &gLightClass, &gHeavyClass})
        if (cls->Get() == nullptr) return false;
    for (auto* field : {&gWorld, &gWeapon, &gInput, &gCurrentAction, &gActionPawn})
        if (field->Get() == nullptr) return false;
    for (auto* fn : {&gGetMode, &gGetAction, &gReloading, &gZoomed, &gCombat, &gState, &gLight, &gHeavy})
        if (fn->Get() == nullptr) return false;
    // Parameter/type guards in addition to named block construction. No guessed offsets. The movement facts' names and
    // shapes (cover, storming, ignored input, death) are game/Player's to check.
    if (!Shape(gCurrentAction, ue3::Kind::Int, 4) || !player::MovementAdmissionReady()) return false;
    for (auto* field : {&gWorld, &gWeapon, &gInput, &gActionPawn})
        if (!Shape(*field, ue3::Kind::Object, 8)) return false;
    for (auto* fn : {&gReloading, &gZoomed, &gCombat})
        if (!ReturnShape(*fn, ue3::Kind::Bool, 4)) return false;
    if (!ReturnShape(gGetMode, ue3::Kind::Object, 8) || !ReturnShape(gState, ue3::Kind::Name, 8)) return false;
    ue3::Params actionParams(ue3::runtime::Get(), gGetAction.Get());
    auto const* actionOut = actionParams.Find("pAction");
    auto const* actionReturn = actionParams.ReturnSpec();
    if (!actionParams.Valid() || actionParams.Size() != 12 || actionParams.Specs().size() != 2 ||
        actionOut == nullptr || actionOut->kind != ue3::Kind::Object || actionOut->size != 8 || actionOut->offset != 0 ||
        actionReturn == nullptr || actionReturn->kind != ue3::Kind::Bool || actionReturn->size != 4 || actionReturn->offset != 8)
        return false;
    for (auto* fn : {&gLight, &gHeavy}) {
        ue3::Params params(ue3::runtime::Get(), fn->Get());
        if (!params.Valid() || params.Size() != 0 || !params.Specs().empty()) return false;
    }
    gStarted = true;
    gStatus = "ready for opt-in native diagnostics; gameplay/headset acceptance pending";
#endif
    return gStarted;
}
std::string Request(ik::melee::Intent intent, int hand, Link const& link) {
#if defined(MEVR_GAME_LE3)
    if (!gStarted || !ue3::runtime::Up()) return "melee: adapter not ready";
    RefreshContext(link);
    if (auto const* why = Refusal(link, hand)) return std::string("melee: refused: ") + why;
    auto* get = gGetMode.Get();
    ue3::Params modeParams(ue3::runtime::Get(), get);
    if (!modeParams.Valid() || !ue3::runtime::Call(gContext.pc, get, modeParams)) return "melee: no native game mode";
    auto* mode = modeParams.Return<ue3::Object*>().value_or(nullptr);
    if (!ue3::runtime::IsAlive(mode) || !ue3::IsA(mode, gModeClass) || ue3::runtime::Get().OuterOf(mode) != gContext.pc)
        return "melee: native game mode owner mismatch";
    // Same-thread final ownership/mode check; functions above were reads, Try* below is the first attack mutation.
    if (!(ReadContext(link) == gContext) || !link.allowRequest(hand)) return "melee: context changed before dispatch";
    auto* fn = (intent == ik::melee::Intent::Heavy ? gHeavy : gLight).Get();
    ue3::Params params(ue3::runtime::Get(), fn);
    if (!params.Valid() || params.Size() != 0) return "melee: unexpected request signature";
    gIntent = intent;
    gAwaiting = true;
    gDeadline = Now() + 0.35;
    if (!ue3::runtime::Call(mode, fn, params)) { gAwaiting = false; return "melee: dispatch failed"; }
    gStatus = intent == ik::melee::Intent::Heavy ? "heavy request dispatched; admission unobserved" : "light request dispatched; admission unobserved";
    Observe();
    return "melee: " + gStatus;
#else
    (void)intent; (void)hand; (void)link;
    return "melee: separate native adapter is LE3 only (LE1 uses Throwables)";
#endif
}
void Tick(Link const& link) {
#if defined(MEVR_GAME_LE3)
    if (!gStarted || !ue3::runtime::Up()) { gGestures.Reset(); return; }
    RefreshContext(link);
    Observe();
    auto input = link.input;
    input.allowed = input.allowed && link.gestures && link.enabled && link.focused && !link.menuOpen && !link.authoredSeated;
    if (!input.allowed) { gGestures.Step(input); return; }
    for (int hand = 0; hand < 2; ++hand)
        input.hands[static_cast<std::size_t>(hand)].available &= Refusal(link, hand) == nullptr;
    auto const request = gGestures.Step(input);
    if (gGestures.HeavyArmedNow() && link.haptic && link.allowRequest && link.allowRequest(1))
        link.haptic(1, 0.045f, 0.35f); // readiness only; native admission/impact are separate observations
    if (request) {
        // No asynchronous queue. The native adapter rechecks all eligibility, without calling Try* as a probe.
        log::Info("melee: stroke {} hand {}: {}", request->stroke, request->hand, Request(request->intent, request->hand, link));
    }
#else
    (void)link;
#endif
}
bool Busy() {
#if defined(MEVR_GAME_LE3)
    return gAwaiting || gObserved != nullptr;
#else
    return false;
#endif
}
std::string Status() {
    char const* phase = "release";
    switch (gGestures.HeavyState()) {
    case ik::melee::HeavyChamber::Phase::Release: break;
    case ik::melee::HeavyChamber::Phase::Idle: phase = "idle"; break;
    case ik::melee::HeavyChamber::Phase::Holding: phase = "holding"; break;
    case ik::melee::HeavyChamber::Phase::Armed: phase = "armed"; break;
    }
    return gStatus + "; heavy chamber " + phase;
}
void Shutdown() {
    gStarted = false;
    gGestures.Reset();
    gStatus = "off";
#if defined(MEVR_GAME_LE3)
    gContext = {};
    gAwaiting = false;
    gObserved = nullptr;
#endif
}
} // namespace mevr::game::melee
