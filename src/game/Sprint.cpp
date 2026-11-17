// SPDX-License-Identifier: MIT
#include "game/Sprint.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Sprint.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Engine.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/HandCalibration.hpp"
#include "core/vr/TestRequest.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <format>
#include <string>

// One sprint for the trilogy (lane GR, 2026-11-16, the owner: "Unified sprint"): ME1's faster run, on ME1, ME2 and ME3 with the
// same rule (core/vr/Sprint) and the same native seam, `SFXGameModeDefault.StormOn` / `StormOff`, which all three games have
// and which all three bind to A held (`OnHold StormOn`, `OnRelease StormOff`). Each game's StormOn does its own checks and then
// sets `BioPlayerController.bWantsToStorm = 1`, the sprint's intent the native movement acts on; StormOff schedules
// TurnStormOff, which clears it. The binding's hold is a timer, so StormOn arrives through ProcessEvent (native code calling
// script), not through a script call: here it runs as the game wrote it and, right after, while the faster run applies, its
// intent is withdrawn (`bWantsToStorm = 0`, journalled) before the movement reads it. So the storm posture, its camera and
// the lowered gun never come, and the press is ours. (Until 2026-11-16 the module skipped StormOn in the script hook only,
// which no caller reaches: the game's own sprint ran.) Per-game adapters: ME3's native admission and fresh-owner checks
// (game/Player's movement admission), ME2 not in the game's own cover, LE1 its drawn weapon's CombatStormSpeed. The faster run
// writes the run speed fields the game reads (Pawn.GroundSpeed and BioPawn.CombatGroundSpeed, by one factor: the game's sprint
// speed over the run speed in effect, its own Pawn.GetMaxSpeed), put back after.
namespace mevr::game::sprint {
namespace {

namespace sp = vr::sprint;

ue3::FunctionRef gStormOnFn{"SFXGame.SFXGameModeDefault.StormOn", ue3::kEveryGame};
ue3::FunctionRef gStormOffFn{"SFXGame.SFXGameModeDefault.StormOff", ue3::kEveryGame};
ue3::FieldRef gWantsToStorm{"SFXGame.BioPlayerController", "bWantsToStorm", ue3::kEveryGame};
ue3::FieldRef gGroundSpeed{"Engine.Pawn", "GroundSpeed", ue3::kEveryGame};
ue3::FieldRef gCombatGroundSpeed{"SFXGame.BioPawn", "CombatGroundSpeed", ue3::kEveryGame};
ue3::FieldRef gStormSpeed{"SFXGame.BioPawn", "StormSpeed", ue3::kEveryGame};
ue3::FunctionRef gGetMaxSpeed{"Engine.Pawn.GetMaxSpeed", ue3::kEveryGame};
ue3::FieldRef gVelocity{"Engine.Actor", "Velocity", ue3::kEveryGame};
#if defined(MEVR_GAME_LE1)
ue3::FieldRef gCombatStormSpeed{"SFXGame.BioPawn", "CombatStormSpeed"};
#endif
#if defined(MEVR_GAME_LE3)
ue3::FieldRef gStorming{"SFXGame.BioPawn", "bStorming", ue3::kEveryGame};
#else
ue3::FieldRef gStorming{"SFXGame.BioPlayerController", "bStorming", ue3::kEveryGame};
#endif
#if defined(MEVR_GAME_LE2)
ue3::FunctionRef gIsInCover{"SFXGame.BioPawn.IsInCover", ue3::kEveryGame};
#endif

#if defined(MEVR_GAME_LE3)
ue3::FunctionRef gStormDisabled{"SFXGame.BioPlayerController.IsStormDisabled", ue3::kEveryGame};
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
    // The one admission read (game/Player): walking on foot, out of cover, standing, not already storming, input taken,
    // alive; then the sprint's own question, whether the game has the storm disabled.
    namespace admit = player::admission;
    return player::MovementAdmitted(pc, pawn, admit::kOnFoot | admit::kNoAction | admit::kWalking | admit::kOutOfCover |
            admit::kNotCrouched | admit::kNotStorming | admit::kMoveInput | admit::kAlive) &&
        NativeBool(pc, gStormDisabled) == false;
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
bool gReady = false;      // the press's routes registered and the intent can be withdrawn
bool gActive = false;     // the last tick: sprint = speed, VR, on foot (StormOn's intent is withdrawn)
bool gHeld = false;       // the sprint button, from StormOn / StormOff
sp::State gState;
ue3::Object* gPawn = nullptr;  // the pawn whose run speeds we wrote
void const* gPawnWorld = nullptr;
float gBaseGround = 0.0f, gBaseCombat = 0.0f;    // its run speeds as the game set them
float gWroteGround = 0.0f, gWroteCombat = 0.0f;  // what we wrote (0: nothing)
float gRunInEffect = 0.0f;  // GetMaxSpeed before the boost (the run the game moves Shepard at)
float gSprintSpeed = 0.0f;
float gFactor = 1.0f;
unsigned gPresses = 0;      // StormOn seen
unsigned gNativePresses = 0, gScriptPresses = 0;
unsigned gWithdrawn = 0;    // ...whose intent we withdrew
unsigned gOtherRoute = 0;   // the game sprinted by a route other than StormOn while we were on
bool gLoggedFirst = false;
bool gLoggedOther = false;
bool gWasBoosting = false;
std::uint64_t gBoostStartMs = 0;
float gPeak = 0.0f;

bool ControllerOf(ue3::Object* mode, ue3::Object* pc) {
    return mode != nullptr && pc != nullptr && ue3::runtime::IsAlive(mode) && ue3::runtime::Get().OuterOf(mode) == pc;
}

// After StormOn (either route): the press noted; while the faster run applies, the game's sprint intent withdrawn.
void AfterStormOn(ue3::Object* context, bool fromScript) {
#if defined(MEVR_GAME_LE3)
    if (!gActive || !FreshOwner(context)) {
        gHeld = false;
        return;  // the game's own sprint
    }
    gHeldController = player::Controller();
    gHeldWorld = gWorldInfo.Read<ue3::Object*>(gHeldController).value_or(nullptr);
#endif
    ue3::Object* const pc = player::Controller();
    if (!ControllerOf(context, pc)) {
        return;  // another controller's game mode
    }
    gHeld = true;
    ++gPresses;
    ++(fromScript ? gScriptPresses : gNativePresses);
    if (!gActive) {
        return;  // the game's own sprint
    }
    std::uint8_t* const wants = gWantsToStorm.In<std::uint8_t>(pc);
    if (wants == nullptr || *wants == 0) {
        return;  // the game's StormOn refused it (the stick not forward, cover): nothing to withdraw
    }
    *wants = 0;
    write_journal::Wrote("Sprint.wantsToStorm", wants, sizeof(*wants));
    ++gWithdrawn;
    if (!gLoggedFirst) {
        gLoggedFirst = true;
        log::Info("sprint: the sprint button ({}): the game's sprint withdrawn (BioPlayerController.bWantsToStorm), a faster run "
                  "instead ([Hands] sprint = speed)", fromScript ? "a script call" : "the game's binding, through ProcessEvent");
    }
}

void OnStormOnScript(ue3::Object* context, ue3::Function*, void*) {
    AfterStormOn(context, true);
}
void OnStormOnNative(ue3::Object* context, ue3::Function*, void*, void*) {
    AfterStormOn(context, false);
}
void OnStormOffScript(ue3::Object*, ue3::Function*, void*) {
    gHeld = false;  // StormOff itself runs: harmless when not sprinting; ends the game's own sprint when it is
}
void OnStormOffNative(ue3::Object*, ue3::Function*, void*, void*) {
    gHeld = false;
}

// The run speeds back where the game left them, each if what's there is still ours.
void Restore() {
    // A transition may destroy the old pawn before this tick. Never form a field address until validated.
    if (gPawn != nullptr && (gWroteGround > 0.0f || gWroteCombat > 0.0f) && gPawnWorld != nullptr &&
        gPawnWorld == engine::World() && ue3::runtime::IsAlive(gPawn) && player::IsBioPawn(gPawn)) {
        if (float* const speed = gGroundSpeed.In<float>(gPawn); speed != nullptr && gWroteGround > 0.0f && *speed == gWroteGround) {
            *speed = gBaseGround;
            write_journal::Wrote("Sprint.groundSpeed(restore)", speed, sizeof(float));
        }
        if (float* const speed = gCombatGroundSpeed.In<float>(gPawn); speed != nullptr && gWroteCombat > 0.0f && *speed == gWroteCombat) {
            *speed = gBaseCombat;
            write_journal::Wrote("Sprint.combatGroundSpeed(restore)", speed, sizeof(float));
        }
    }
    gWroteGround = gWroteCombat = 0.0f;
    gPawn = nullptr;
    gPawnWorld = nullptr;
}

// The run the game moves the pawn at now (Pawn.GetMaxSpeed: its own choice between the explore and combat speeds), or
// nothing if the call isn't there.
std::optional<float> MaxSpeed(ue3::Object* pawn) {
    ue3::Function* const fn = gGetMaxSpeed.Get();
    if (fn == nullptr) {
        return std::nullopt;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !ue3::runtime::Call(pawn, fn, p)) {
        return std::nullopt;
    }
    std::optional<float> const v = p.Return<float>();
    return v && std::isfinite(*v) && *v > 0.0f ? v : std::nullopt;
}

#if !defined(MEVR_GAME_LE3)
bool GameCover(ue3::Object* pawn) {
#if defined(MEVR_GAME_LE2)
    // ME2's StormOn breaks a lean out of the game's own cover into a sprint: that stays the game's.
    ue3::Params p(ue3::runtime::Get(), gIsInCover.Get());
    return p.Valid() && ue3::runtime::Call(pawn, gIsInCover.Get(), p) && p.ReturnBool().value_or(false);
#else
    (void)pawn;
    return false;  // ME1's StormOn refuses in cover itself (ME3's admission requires out of cover)
#endif
}
#endif

} // namespace

bool TryStartup() {
    if (gStarted) {
        return true;
    }
    if (!ue3::runtime::Up()) {
        return false;
    }
    ue3::Function* const on = gStormOnFn.Get();
    ue3::Function* const off = gStormOffFn.Get();
    if (on == nullptr || off == nullptr) {
        return false;
    }
#if defined(MEVR_GAME_LE3)
    if (!gGroundSpeed.Get() || !gStormSpeed.Get() || !gVelocity.Get() || !gStorming.Get() ||
        !gStormDisabled.Get() || !player::MovementAdmissionReady() || !gWorldInfo.Get()) return false;
#endif
    gStarted = true;
    ue3::PropertyInfo const* const wants = gWantsToStorm.Get();
    gReady = wants != nullptr && wants->kind == ue3::Kind::Byte && wants->elementSize == 1 && gGroundSpeed.Get() != nullptr &&
        gStormSpeed.Get() != nullptr && gVelocity.Get() != nullptr && gStorming.Get() != nullptr;
    if (gReady) {
        engine::call_function::AddPostHandler(on, &OnStormOnScript);
        engine::call_function::AddPostHandler(off, &OnStormOffScript);
        engine::process_event::AddPostHandler(on, &OnStormOnNative);
        engine::process_event::AddPostHandler(off, &OnStormOffNative);
    }
    log::Info("sprint: SFXGameModeDefault.StormOn / StormOff {}; the run speeds {} ({})",
        gReady ? "are watched on both routes (the binding's ProcessEvent and script calls): their sprint intent "
                 "(BioPlayerController.bWantsToStorm) is withdrawn for a faster run ([Hands] sprint)"
               : "found, but bWantsToStorm or a speed field isn't: the game's own sprint stays",
        gCombatGroundSpeed.Get() != nullptr ? "GroundSpeed and CombatGroundSpeed" : "GroundSpeed only",
        gGetMaxSpeed.Get() != nullptr ? "the run in effect from Pawn.GetMaxSpeed" : "no Pawn.GetMaxSpeed: GroundSpeed is the run");
    return true;
}

void Tick(double deltaSeconds, bool headTrackedView, bool controlsAllowed, bool (*freshControlsAllowed)()) {
#if defined(MEVR_GAME_LE3)
    gControlsAllowed = freshControlsAllowed;
    gTickTime = GetTickCount64();
#else
    (void)controlsAllowed; (void)freshControlsAllowed;
#endif
    hand_calibration::Sprint const settings = hand_calibration::GetSprint();
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    std::optional<std::uint8_t> const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    gActive = gReady && settings.rules.enabled && headTrackedView && pawn != nullptr && mode && *mode == 0;
#if defined(MEVR_GAME_LE3)
    gActive = gActive && controlsAllowed && gControlsAllowed && gControlsAllowed() && NativeAllowed(controller, pawn);
    auto* const currentWorld = ue3::runtime::IsAlive(controller) ? gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    if (controller != gHeldController || currentWorld != gHeldWorld) gHeld = false;
#else
    gActive = gActive && !GameCover(pawn);
#endif
    if (!gActive) {
        Restore();
        gState.boosting = false;
        gWasBoosting = false;
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
    float* const ground = gGroundSpeed.In<float>(pawn);
    float* const combat = gCombatGroundSpeed.In<float>(pawn);
    if (ground == nullptr || !std::isfinite(*ground) || *ground <= 0.0f) {
        return;
    }
    void const* const world = engine::World();
    if (pawn != gPawn || world != gPawnWorld) {
        Restore();
    }
    std::uint64_t const now = GetTickCount64();
    if (!boosting) {
        if (gWasBoosting) {
            gWasBoosting = false;
            log::Info("sprint: back to the run after {:.1f} s: the pawn's peak {:.0f} u/s, now {:.0f} u/s, stamina {:.0f}%{}",
                static_cast<double>(now - gBoostStartMs) / 1000.0, gPeak, horizontal, gState.stamina * 100.0f,
                gState.exhausted ? " (spent)" : "");
        }
        Restore();
        // The run in effect, read while nothing of ours is in the speeds (for the factor at the next boost).
        gRunInEffect = MaxSpeed(pawn).value_or(*ground);
        return;
    }
    if (gWroteGround == 0.0f || *ground != gWroteGround) {
        gBaseGround = *ground;  // the game set (or reset) the run speed
    }
    if (combat != nullptr && std::isfinite(*combat) && *combat > 0.0f && (gWroteCombat == 0.0f || *combat != gWroteCombat)) {
        gBaseCombat = *combat;
    }
    if (!(gRunInEffect > 0.0f)) {
        gRunInEffect = gBaseGround;
    }
#if defined(MEVR_GAME_LE1)
    // ME1 has a sprint speed of its own with a weapon drawn.
    bool const drawn = weapon::Type() != weapon::RangedType::Unknown;
    gSprintSpeed = (drawn ? gCombatStormSpeed : gStormSpeed).Read<float>(pawn).value_or(0.0f);
#else
    // ME2 and ME3 declare only StormSpeed, never LE1's CombatStormSpeed.
    gSprintSpeed = gStormSpeed.Read<float>(pawn).value_or(0.0f);
#endif
    gFactor = sp::SpeedFactor(gRunInEffect, gSprintSpeed);
    gPawn = pawn;
    gPawnWorld = world;
    gWroteGround = gBaseGround * gFactor;
    if (*ground != gWroteGround) {
        *ground = gWroteGround;
        write_journal::Wrote("Sprint.groundSpeed", ground, sizeof(float));
    }
    if (combat != nullptr && gBaseCombat > 0.0f) {
        gWroteCombat = gBaseCombat * gFactor;
        if (*combat != gWroteCombat) {
            *combat = gWroteCombat;
            write_journal::Wrote("Sprint.combatGroundSpeed", combat, sizeof(float));
        }
    }
    if (!gWasBoosting) {
        gWasBoosting = true;
        gBoostStartMs = now;
        gPeak = horizontal;
        log::Info("sprint: running fast: x{:.2f} (the run in effect {:.0f} u/s, the game's sprint {:.0f} u/s; GroundSpeed {:.0f} -> {:.0f}{}), "
                  "the pawn at {:.0f} u/s",
            gFactor, gRunInEffect, gSprintSpeed, gBaseGround, gWroteGround,
            gWroteCombat > 0.0f ? std::format(", CombatGroundSpeed {:.0f} -> {:.0f}", gBaseCombat, gWroteCombat) : std::string(),
            horizontal);
    }
    gPeak = std::max(gPeak, horizontal);
}

void LogStats() {
    hand_calibration::Sprint const settings = hand_calibration::GetSprint();
    log::Info("    sprint: {}; {} presses ({} by the binding, {} by script), {} withdrawn, stamina {:.0f}%{}, emptied {} times; speed x{:.2f}{}",
        !settings.rules.enabled ? std::string("the game's own ([Hands] sprint = game)")
            : !gReady         ? std::string("the game's own (the intent can't be withdrawn: see the startup line)")
            : gActive         ? std::string("a faster run")
                              : std::string("a faster run when on foot in VR"),
        gPresses, gNativePresses, gScriptPresses, gWithdrawn, gState.stamina * 100.0f,
        gState.boosting ? " (running fast)" : gState.exhausted ? " (spent)" : "", gState.emptied, gFactor,
        gOtherRoute > 0 ? "; the game sprinted by another route [verify]" : "");
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"active\": {}, \"ready\": {}, \"held\": {}, \"boosting\": {}, \"stamina\": {}, \"exhausted\": {}, "
                       "\"factor\": {}, \"runInEffect\": {}, \"sprintSpeed\": {}, \"presses\": {}, \"nativePresses\": {}, "
                       "\"scriptPresses\": {}, \"withdrawn\": {}, \"otherRoute\": {}}}",
        gActive ? "true" : "false", gReady ? "true" : "false", gHeld ? "true" : "false", gState.boosting ? "true" : "false",
        json::JsonNumber(gState.stamina), gState.exhausted ? "true" : "false", json::JsonNumber(gFactor),
        json::JsonNumber(gRunInEffect), json::JsonNumber(gSprintSpeed), gPresses, gNativePresses, gScriptPresses, gWithdrawn,
        gOtherRoute);
}

void Shutdown() {
    Restore();
    gActive = false;
    gHeld = false;
#if defined(MEVR_GAME_LE3)
    gControlsAllowed = nullptr; gTickTime = 0; gHeldController = gHeldWorld = nullptr;
#endif
}

} // namespace mevr::game::sprint
