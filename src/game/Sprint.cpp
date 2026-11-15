// SPDX-License-Identifier: MIT
#include "game/Sprint.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Sprint.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Engine.hpp"
#include "game/HandCalibration.hpp"
#include "core/vr/TestRequest.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>
#include <cmath>
#include <format>
#include <string>

namespace mevr::game::sprint {
namespace {

namespace sp = vr::sprint;

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE3)
ue3::FunctionRef gStormOnFn{"SFXGame.SFXGameModeDefault.StormOn", ue3::kEveryGame};
ue3::FunctionRef gStormOffFn{"SFXGame.SFXGameModeDefault.StormOff", ue3::kEveryGame};
ue3::FieldRef gGroundSpeed{"Engine.Pawn", "GroundSpeed", ue3::kEveryGame};
ue3::FieldRef gStormSpeed{"SFXGame.BioPawn", "StormSpeed", ue3::kEveryGame};
ue3::FieldRef gCombatStormSpeed{"SFXGame.BioPawn", "CombatStormSpeed"};
ue3::FieldRef gVelocity{"Engine.Actor", "Velocity", ue3::kEveryGame};
#if defined(MEVR_GAME_LE3)
ue3::FieldRef gStorming{"SFXGame.BioPawn", "bStorming", ue3::kEveryGame};
#else
ue3::FieldRef gStorming{"SFXGame.BioPlayerController", "bStorming", ue3::kEveryGame};
#endif
#endif

#if defined(MEVR_GAME_LE3)
ue3::FunctionRef gDead{"Engine.Pawn.IsDead", ue3::kEveryGame};
ue3::FunctionRef gMoveIgnored{"Engine.PlayerController.IsMoveInputIgnored", ue3::kEveryGame};
ue3::FunctionRef gStormDisabled{"SFXGame.BioPlayerController.IsStormDisabled", ue3::kEveryGame};
ue3::FieldRef gCurrentAction{"SFXGame.BioPawn", "CurrentCustomAction", ue3::kEveryGame};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics", ue3::kEveryGame};
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType", ue3::kEveryGame};
ue3::FieldRef gCrouched{"Engine.Pawn", "bIsCrouched", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
bool (*gControlsAllowed)() = nullptr;
std::uint64_t gTickTime = 0;
ue3::Object* gHeldController = nullptr;
ue3::Object* gHeldWorld = nullptr;
std::optional<bool> NativeBool(ue3::Object* object, ue3::FunctionRef& ref) {
    ue3::Params p(ue3::runtime::Get(), ref.Get());
    return p.Valid() && ue3::runtime::Call(object, ref.Get(), p) ? p.ReturnBool() : std::nullopt;
}
bool NativeAllowed(ue3::Object* pc, ue3::Object* pawn) {
    return ue3::runtime::IsAlive(pc) && ue3::runtime::IsAlive(pawn) && player::GameMode(pc) == 0 &&
        gPhysics.Read<std::uint8_t>(pawn).value_or(255) == 1 &&
        gCurrentAction.Read<std::int32_t>(pawn).value_or(-1) == 0 &&
        gCoverType.Read<std::uint8_t>(pawn).value_or(255) == 0 &&
        gCrouched.ReadBool(pawn) == false && gStorming.ReadBool(pawn) == false &&
        NativeBool(pc, gMoveIgnored) == false && NativeBool(pc, gStormDisabled) == false && NativeBool(pawn, gDead) == false;
}
bool FreshOwner(ue3::Object* context) {
    auto const now = GetTickCount64();
    auto* const pc = player::Controller();
    return context && ue3::runtime::IsAlive(context) && gTickTime && now >= gTickTime && now - gTickTime <= 100 &&
        gControlsAllowed && gControlsAllowed() && ue3::runtime::Get().OuterOf(context) == pc &&
        NativeAllowed(pc, player::BioPawn(pc));
}
#endif

constexpr float kRunningUnits = 200.0f;  // horizontal speed (units/s) that counts as running: the walk is 130, the run 400

bool gStarted = false;
bool gSkipReady = false;
bool gActive = false;     // the last tick: sprint = speed, VR, on foot (StormOn is skipped)
bool gHeld = false;       // the sprint button, from StormOn / StormOff
sp::State gState;
ue3::Object* gPawn = nullptr;  // the pawn whose GroundSpeed we wrote
void const* gPawnWorld = nullptr;
float gBase = 0.0f;            // its run speed as the game set it
float gWrote = 0.0f;           // what we wrote (0: nothing)
float gFactor = 1.0f;
unsigned gSkipped = 0;
unsigned gOtherRoute = 0;      // the game sprinted by a route other than StormOn while we were on
bool gLoggedFirst = false;
bool gLoggedOther = false;

bool OnStormOn(ue3::Object* context, ue3::Function*, ue3::Function*) {
#if defined(MEVR_GAME_LE3)
    if (!gActive || !FreshOwner(context)) { gHeld = false; return false; }
    gHeldController = player::Controller();
    gHeldWorld = gWorldInfo.Read<ue3::Object*>(gHeldController).value_or(nullptr);
#else
    (void)context;
#endif
    gHeld = true;
    if (!gActive) {
        return false;  // the game's own sprint
    }
    ++gSkipped;
    if (!gLoggedFirst) {
        gLoggedFirst = true;
        log::Info("sprint: the sprint button: the game's sprint skipped, a faster run instead ([Hands] sprint = speed)");
    }
    return true;
}

bool OnStormOff(ue3::Object*, ue3::Function*, ue3::Function*) {
    gHeld = false;
    return false;  // harmless when not sprinting; ends the game's own sprint when it is
}

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE3)
// The run speed back where the game left it, if what's there is still ours.
void Restore() {
    // A transition may destroy the old pawn before this tick. Never form a field address until validated.
    if (gPawn != nullptr && gWrote > 0.0f && gPawnWorld != nullptr && gPawnWorld == engine::World() &&
        ue3::runtime::IsAlive(gPawn) && player::IsBioPawn(gPawn)) {
        if (float* const speed = gGroundSpeed.In<float>(gPawn); speed != nullptr && *speed == gWrote) {
            *speed = gBase;
            write_journal::Wrote("Sprint.groundSpeed(restore)", speed, sizeof(float));
        }
    }
    gWrote = 0.0f;
    gPawn = nullptr;
    gPawnWorld = nullptr;
}
#endif

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE3)
    if (gStarted) {
        return true;
    }
    ue3::Function* const on = gStormOnFn.Get();
    ue3::Function* const off = gStormOffFn.Get();
    if (on == nullptr || off == nullptr) {
        return false;
    }
#if defined(MEVR_GAME_LE3)
    if (!gGroundSpeed.Get() || !gStormSpeed.Get() || !gVelocity.Get() || !gStorming.Get() ||
        !gMoveIgnored.Get() || !gDead.Get() || !gStormDisabled.Get() || !gCurrentAction.Get() || !gPhysics.Get() ||
        !gCoverType.Get() || !gCrouched.Get() || !gWorldInfo.Get()) return false;
#endif
    gStarted = true;
    ue3::Reflection& reflection = ue3::runtime::Get();
    auto const skippable = [&](ue3::Function* f) {
        return (reflection.FunctionFlags(f) & ue3::function_flags::kNative) == 0 && reflection.ParmsSize(f) == 0 &&
            reflection.Parameters(f).empty();
    };
    gSkipReady = skippable(on) && skippable(off);
    if (gSkipReady) {
        engine::call_function::AddSkipHandler(on, &OnStormOn);
        engine::call_function::AddSkipHandler(off, &OnStormOff);
    }
    log::Info("sprint: SFXGameModeDefault.StormOn / StormOff {}", gSkipReady
            ? "are script functions without parameters: the sprint button can be a faster run ([Hands] sprint)"
            : "can't be skipped safely: the game's own sprint stays");
    return true;
#else
    return false;
#endif
}

void Tick(double deltaSeconds, bool headTrackedView, bool controlsAllowed, bool (*freshControlsAllowed)()) {
#if defined(MEVR_GAME_LE3)
    gControlsAllowed = freshControlsAllowed;
    gTickTime = GetTickCount64();
#else
    (void)controlsAllowed; (void)freshControlsAllowed;
#endif
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE3)
    hand_calibration::Sprint const settings = hand_calibration::GetSprint();
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    gActive = gSkipReady && settings.rules.enabled && headTrackedView && pawn != nullptr && mode && *mode == 0;
#if defined(MEVR_GAME_LE3)
    gActive = gActive && controlsAllowed && gControlsAllowed && gControlsAllowed() && NativeAllowed(controller, pawn);
    auto* const currentWorld = ue3::runtime::IsAlive(controller) ? gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    if (controller != gHeldController || currentWorld != gHeldWorld) gHeld = false;
#endif
    if (!gActive) {
        Restore();
        gState.boosting = false;
        if (!headTrackedView || !mode || *mode != 0
#if defined(MEVR_GAME_LE3)
            || !gActive
#endif
        ) {
            gHeld = false;  // a menu or a vehicle: the press is over
        }
        return;
    }
#if defined(MEVR_GAME_LE3)
    bool const nativeStorming = gStorming.ReadBool(pawn).value_or(false);
#else
    bool const nativeStorming = gStorming.ReadBool(controller).value_or(false);
#endif
    if (nativeStorming && !gLoggedOther) {
        gLoggedOther = true;
        ++gOtherRoute;
        log::Warn("sprint: the game sprints by a route other than StormOn: the faster run can't replace it [verify]");
    }
    float horizontal = 0.0f;
    if (auto const v = gVelocity.Read<ue3::Vector>(pawn)) {
        horizontal = std::sqrt(v->x * v->x + v->y * v->y);
    }
    bool const boosting = sp::Step(gState, settings.rules, gHeld, horizontal > kRunningUnits, static_cast<float>(deltaSeconds));
    float* const speed = gGroundSpeed.In<float>(pawn);
    if (speed == nullptr || !std::isfinite(*speed) || *speed <= 0.0f) {
        return;
    }
    void const* const world = engine::World();
    if (pawn != gPawn || world != gPawnWorld) {
        Restore();
    }
    if (!boosting) {
        Restore();
        return;
    }
    if (gWrote == 0.0f || *speed != gWrote) {
        gBase = *speed;  // the game set (or reset) the run speed
    }
#if defined(MEVR_GAME_LE3)
    // LE3 declares StormSpeed, not LE1's CombatStormSpeed. Never invent that missing field.
    float const sprintSpeed = gStormSpeed.Read<float>(pawn).value_or(0.0f);
#else
    bool const drawn = weapon::Type() != weapon::RangedType::Unknown;
    float const sprintSpeed = (drawn ? gCombatStormSpeed : gStormSpeed).Read<float>(pawn).value_or(0.0f);
#endif
    gFactor = sp::SpeedFactor(gBase, sprintSpeed);
    gPawn = pawn;
    gPawnWorld = world;
    gWrote = gBase * gFactor;
    if (*speed != gWrote) {
        *speed = gWrote;
        write_journal::Wrote("Sprint.groundSpeed", speed, sizeof(float));
    }
#else
    (void)deltaSeconds;
    (void)headTrackedView;
#endif
}

void LogStats() {
    hand_calibration::Sprint const settings = hand_calibration::GetSprint();
    log::Info("    sprint: {}; {} presses taken, stamina {:.0f}%{}, emptied {} times; speed x{:.2f}{}",
        !settings.rules.enabled ? std::string("the game's own ([Hands] sprint = game)")
            : gActive         ? std::string("a faster run")
                              : std::string("a faster run when on foot in VR"),
        gSkipped, gState.stamina * 100.0f, gState.boosting ? " (running fast)" : gState.exhausted ? " (spent)" : "", gState.emptied,
        gFactor, gOtherRoute > 0 ? "; the game sprinted by another route [verify]" : "");
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"active\": {}, \"held\": {}, \"boosting\": {}, \"stamina\": {}, \"exhausted\": {}, \"factor\": {}, "
                       "\"pressesTaken\": {}, \"otherRoute\": {}}}",
        gActive ? "true" : "false", gHeld ? "true" : "false", gState.boosting ? "true" : "false", json::JsonNumber(gState.stamina),
        gState.exhausted ? "true" : "false", json::JsonNumber(gFactor), gSkipped, gOtherRoute);
}

void Shutdown() {
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE3)
    Restore();
#endif
    gActive = false;
    gHeld = false;
#if defined(MEVR_GAME_LE3)
    gControlsAllowed = nullptr; gTickTime = 0; gHeldController = gHeldWorld = nullptr;
#endif
}

} // namespace mevr::game::sprint
