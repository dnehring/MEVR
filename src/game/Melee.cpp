// SPDX-License-Identifier: MIT
#include "game/Melee.hpp"

#include "core/Log.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Throwing.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ArmIk.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/Holsters.hpp"
#include "game/Player.hpp"
#include "game/SupportGrip.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <numbers>

namespace mevr::game::melee {
namespace {

namespace slots = ik::body_slots;
namespace thr = ik::throwing;
using ik::melee::Intent;

constexpr int kLeft = thr::kLeft;
constexpr int kRight = thr::kRight;
// The pad mapping's grip latch and trigger (game/gamepad_mapping): a fist is both held.
constexpr float kGripOn = 0.6f;
constexpr float kTriggerOn = 0.5f;
// A melee's damage is looked for this long after the strike.
constexpr double kHitWatchSeconds = 1.2;
// The right hand can't melee this long after a draw from a holster (a fast reach).
constexpr double kNoMeleeAfterDraw = 0.5;

char const* HandName(int hand) {
    return hand == kLeft ? "left" : "right";
}

double SteadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

ik::Vec3 FromXr(XrVector3f const& v) {
    return {v.x, v.y, v.z};
}

engine::UnrealVector Sub(engine::UnrealVector a, engine::UnrealVector b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

double Len(engine::UnrealVector v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

// ---- Every game's state ------------------------------------------------------------------------------------------------
bool gStarted = false;
ik::melee::Gestures gGestures;
double gTime = 0.0;            // the physical rules' clock: the newest controller sample's time
double gYaw = 0.0;             // the body's front at that sample (the enemy's cone)
bool gPhysicalActive = false;  // ME1: game/Throwables was active this tick (on foot in VR): B's rule there
bool gKeptB = false;           // the last AdjustPad kept B from the game
bool gButtonFlags = false;     // the host's pad mapping knows kFlagNoMeleeButton
bool gToldNoButtons = false;
bool gToldNoHaptics = false;
Haptics gHaptics;
double gReadyAt = 0.0;         // both hands' cooldown (meleeCooldownS), from the last strike the game swung
std::string gLast = "none";
std::string gStatus = "off";   // the last state worth a status line (ME3's staged adapter; the sequels' feed)
std::string gLoggedStatus;

// A strike's target, watched for damage.
struct HitWatch {
    bool active = false;
    int hand = -1;
    double age = 0.0;
    std::uintptr_t target = 0;
    double health = 0.0;
    std::string name;
};
HitWatch gHitWatch;

// Heartbeat counters.
unsigned gStrikes = 0;
unsigned gHits = 0;
unsigned gMeleeRefusals = 0;
unsigned gStrokeRefusals = 0;

void SetStatus(std::string status) {
    gStatus = std::move(status);
    if (gStatus != gLoggedStatus) {
        gLoggedStatus = gStatus;
        log::Info("melee: {}", gStatus);
    }
}

void Pulse(Haptics const& haptics, int hand, thr::Buzz const& buzz) {
    if (!(buzz.amplitude > 0.0f) || !(buzz.seconds > 0.0f)) {
        return;
    }
    if (haptics.pulseHz != nullptr) {
        haptics.pulseHz(hand, buzz.seconds, buzz.amplitude, buzz.frequency);
    } else if (haptics.pulse != nullptr) {
        haptics.pulse(hand, buzz.seconds, buzz.amplitude);
    } else if (!gToldNoHaptics) {
        gToldNoHaptics = true;
        log::Warn("melee: the host can't vibrate the controllers (services older than v9): no haptics");
    }
}

// ---- The adapter: each game's native names -----------------------------------------------------------------------------
// The player's objects a strike needs.
struct Game {
    ue3::Object* controller = nullptr;  // a BioPlayerController
    ue3::Object* pawn = nullptr;        // a BioPawn
    ue3::Object* behavior = nullptr;    // its m_oBehavior (ME1, ME2)
    ue3::Object* mode = nullptr;        // the SFXGameModeDefault (ME1, ME2; ME3 asks the controller at dispatch)
};

// The enemy a melee would hit: the nearest living hostile in reach and in front of the body.
struct Enemy {
    ue3::Object* pawn = nullptr;
    double meters = 1e9;
    double degrees = 0.0;
};

// The game's own melee power for this strike, with its live cooldown (ME1, ME2: a BioPower; ME3 has none).
struct MeleePower {
    ue3::Object* power = nullptr;  // nullptr: the player has none of that name
    ue3::Name name{};
    float cooldown = 0.0f;         // seconds left
    std::string label;             // its name (ME1: Pistol_Whip or Rifle_Butt; ME2: the held weapon's MeleePowerName)
};

// Every game: the buttons' exec functions, the player's input, the pawn list.
ue3::ClassRef gBioPlayerInput{"SFXGame.BioPlayerInput", ue3::kEveryGame};
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput", ue3::kEveryGame};
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gPawnList{"Engine.WorldInfo", "PawnList", ue3::kEveryGame};
ue3::FieldRef gNextPawn{"Engine.Pawn", "NextPawn", ue3::kEveryGame};
ue3::FunctionRef gIsCombatEnabledFn{"SFXGame.BioPlayerInput.IsCombatEnabled", ue3::kEveryGame};
ue3::FunctionRef gTryMeleeFn{"SFXGame.SFXGameModeDefault.TryMelee", ue3::kEveryGame};
#if defined(MEVR_GAME_LE3)
ue3::FunctionRef gIsHostileFn{"Engine.Pawn.IsHostile", ue3::kEveryGame};  // ME3's BioPawn has none of its own
#else
ue3::FunctionRef gIsHostileFn{"SFXGame.BioPawn.IsHostile", ue3::kEveryGame};
#endif
#if defined(MEVR_GAME_LE1)
ue3::FunctionRef gIsDeadFn{"SFXGame.BioPawn.IsDead", ue3::kEveryGame};
#else
ue3::FunctionRef gIsDeadFn{"Engine.Pawn.IsDead", ue3::kEveryGame};
#endif

ue3::Object* ObjectOf(ue3::FieldRef& field, ue3::Object const* object) {
    return object != nullptr ? field.Read<ue3::Object*>(object).value_or(nullptr) : nullptr;
}

engine::UnrealVector LocationOf(ue3::Object const* actor) {
    ue3::Vector const v = player::Location(actor).value_or(ue3::Vector{});
    return {v.x, v.y, v.z};
}

// Through ProcessEvent with the function's own parameter block (never a generated wrapper).
bool CallBool(ue3::Object* object, ue3::FunctionRef& ref) {
    ue3::Function* const function = ref.Get();
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

[[maybe_unused]] void CallVoid(ue3::Object* object, ue3::FunctionRef& ref) {
    ue3::Function* const function = ref.Get();
    if (object == nullptr || function == nullptr) {
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
}

bool CombatEnabled(Game const& game) {
    ue3::Object* const input = ObjectOf(gPlayerInput, game.controller);
    return input != nullptr && ue3::IsA(input, gBioPlayerInput) && CallBool(input, gIsCombatEnabledFn);
}

#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)
// ME1 and ME2: the default game mode (the buttons' exec functions), the behaviour's melee powers, the storm.
ue3::ClassRef gGameModeDefault{"SFXGame.SFXGameModeDefault", ue3::kEveryGame};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager, ue3::kEveryGame};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes", ue3::kEveryGame};
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior", ue3::kEveryGame};
ue3::FieldRef gStorming{"SFXGame.BioPlayerController", "bStorming", ue3::kEveryGame};
// The melee powers (research/20 section 2: ME1's Pistol_Whip and Rifle_Butt; ME2's the held weapon's MeleePowerName, which its
// TryMelee casts and its SFXCustomAction_Melee.Impact finds in the same list), read live for their cooldown.
ue3::FieldRef gPowers{"SFXGame.BioEpicPawnBehavior", "m_powers", ue3::kEveryGame};
ue3::FieldRef gPowerName{"SFXGame.BioPower", "m_nmPower", ue3::kEveryGame};
ue3::FieldRef gCooldownRemaining{"SFXGame.BioPower", "m_fCooldownRemaining", ue3::kEveryGame};
// The right fist's punch with the weapon away: the melee power through the engine's own "cast this" (research/55 section 2).
ue3::FunctionRef gActivatePowerFn{"SFXGame.BioPlayerInput.ActivatePower", ue3::kEveryGame};

std::optional<Game> ReadGame(std::string& why) {
    if (!ue3::runtime::Up()) {
        why = "our reflection layer is off (see the 'reflect:' log lines)";
        return std::nullopt;
    }
    ue3::Object* const pc = player::Controller();
    if (pc == nullptr || !player::IsBioPlayerController(pc)) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = pc;
    game.pawn = player::BioPawn(pc);
    if (game.pawn == nullptr) {
        why = "no player pawn (vehicle or cutscene?)";
        return std::nullopt;
    }
    game.behavior = ObjectOf(gBehavior, game.pawn);
    if (game.behavior == nullptr) {
        why = "the player pawn has no behavior";
        return std::nullopt;
    }
    if (ue3::Object* const manager = ObjectOf(gGameModeManager, pc)) {
        if (ue3::TArray<ue3::Object*> const* const modes = gGameModes.In<ue3::TArray<ue3::Object*>>(manager)) {
            for (int i = 0; i < modes->Count(); ++i) {
                ue3::Object* const mode = modes->data[i];
                if (mode != nullptr && ue3::IsA(mode, gGameModeDefault)) {
                    game.mode = mode;
                    break;
                }
            }
        }
    }
    if (game.mode == nullptr) {
        why = "no SFXGameModeDefault in the player controller's game modes";
        return std::nullopt;
    }
    return game;
}

bool Storming(Game const& game) {
    return gStorming.ReadBool(game.controller).value_or(false);
}

MeleePower PowerNamed(Game const& game, std::string label) {
    MeleePower out;
    out.label = std::move(label);
    auto const list = gPowers.ElementsOf(game.behavior);
    for (std::int32_t i = 0; list && i < list->count && i < 64; ++i) {
        void* const slot = list->At(i);
        ue3::Object* const power = slot != nullptr ? *static_cast<ue3::Object**>(slot) : nullptr;
        if (power == nullptr || !ue3::runtime::IsAlive(power)) {
            continue;
        }
        ue3::Name const name = gPowerName.Read<ue3::Name>(power).value_or(ue3::Name{});
        if (!out.label.empty() && ue3::runtime::NameText(name) == out.label) {
            out.power = power;
            out.name = name;
            out.cooldown = std::max(0.0f, gCooldownRemaining.Read<float>(power).value_or(0.0f));
            return out;
        }
    }
    return out;
}
#endif

#if defined(MEVR_GAME_LE1)
// ME1 (research/20): drawn is the pawn's combat action state; the melee power by the quick slot (TryMelee's own choice).
ue3::FieldRef gEquipment{"SFXGame.BioPawnBehavior", "m_oEquipment", ue3::kEveryGame};
ue3::FieldRef gQuickSlotCurrent{"SFXGame.BioEquipment", "m_eQuickSlotCurrent", ue3::kEveryGame};
ue3::FunctionRef gIsInCombatActionStateFn{"SFXGame.BioPawn.IsInCombatActionState", ue3::kEveryGame};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons", ue3::kEveryGame};
ue3::FunctionRef gIsThrowingGrenadeFn{"SFXGame.BioPawn.IsThrowingGrenade", ue3::kEveryGame};
ue3::FunctionRef gIsUsingPowerFn{"SFXGame.BioPawn.IsUsingPower", ue3::kEveryGame};
ue3::FunctionRef gGetPercentHealthFn{"SFXGame.BioEpicPawnBehavior.GetPercentHealth", ue3::kEveryGame};

bool Drawn(Game const& game) {
    return CallBool(game.pawn, gIsInCombatActionStateFn);
}

bool UsingPower(Game const& game) {
    ue3::Function* const function = gIsUsingPowerFn.Get();
    if (function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    parms.SetBool("bIgnoreWeaponPowers", false);
    ue3::runtime::Call(game.pawn, function, parms);
    return parms.ReturnBool().value_or(false);
}

char const* Busy(Game const& game, int /*hand*/) {
    if (CallBool(game.pawn, gIsSwitchingWeaponsFn) || UsingPower(game) || CallBool(game.pawn, gIsThrowingGrenadeFn)) {
        return "switching weapons, using a power or throwing a grenade";
    }
    return nullptr;
}

// The quick slot in hand (BioEquipment.m_eQuickSlotCurrent; 0 the pistol), if there's equipment.
std::optional<std::uint8_t> CurrentQuickSlot(Game const& game) {
    ue3::Object* const equipment = ObjectOf(gEquipment, game.behavior);
    return equipment != nullptr ? gQuickSlotCurrent.Read<std::uint8_t>(equipment) : std::nullopt;
}

MeleePower FindMeleePower(Game const& game, bool drawn) {
    std::optional<std::uint8_t> const slot = CurrentQuickSlot(game);
    bool const whip = !drawn || (slot && *slot == 0);  // TryMelee's choice: the pistol whip for slot 0, the rifle butt otherwise
    return PowerNamed(game, whip ? "Pistol_Whip" : "Rifle_Butt");
}

// The B button's exec, directly (never a generated wrapper): the pistol whip or the rifle butt.
std::optional<std::string> DispatchWeapon(Game const& game, MeleePower const& power, Intent, int, std::string&) {
    CallVoid(game.mode, gTryMeleeFn);
    return std::format("SFXGameModeDefault.TryMelee ({})", power.label == "Pistol_Whip" ? "pistol whip" : "rifle butt");
}

std::optional<std::string> DispatchFist(Game const& game, MeleePower const& power, Enemy const& enemy, std::string& why) {
    ue3::Object* const input = ObjectOf(gPlayerInput, game.controller);
    ue3::Function* const activate = gActivatePowerFn.Get();
    if (power.power == nullptr || input == nullptr || activate == nullptr) {
        why = power.power == nullptr ? "the player has no Pistol_Whip power to punch with" : "no BioPlayerInput.ActivatePower to punch through";
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), activate);
    parms.Set("nmPower", power.name);
    if (enemy.pawn != nullptr) {
        parms.Set("oTarget", enemy.pawn);
        ue3::Vector const at = player::Location(enemy.pawn).value_or(ue3::Vector{});
        parms.Set("vTargetLocation", at);
    }
    ue3::runtime::Call(input, activate, parms);  // directly: never a generated wrapper
    if (!parms.ReturnBool().value_or(false)) {
        why = "the game refused the punch (BioPlayerInput.ActivatePower(Pistol_Whip) returned false)";
        return std::nullopt;
    }
    return std::string("BioPlayerInput.ActivatePower(Pistol_Whip) (a punch, the weapon away)");
}

// The target's health, 0..1 (its behaviour's GetPercentHealth); negative: unknown.
double Health(ue3::Object* pawn) {
    ue3::Object* const behavior = ObjectOf(gBehavior, pawn);
    ue3::Function* const function = gGetPercentHealthFn.Get();
    if (pawn == nullptr || behavior == nullptr || function == nullptr) {
        return -1.0;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(behavior, function, parms);
    return parms.Return<float>().value_or(-1.0f);
}

constexpr bool kWatchHits = true;

bool StartupNatives(std::string& missing) {
    for (ue3::FunctionRef* ref : {&gTryMeleeFn, &gIsInCombatActionStateFn, &gIsSwitchingWeaponsFn, &gIsThrowingGrenadeFn,
             &gIsCombatEnabledFn}) {
        if (ref->Get() == nullptr) {
            missing += std::format("{}{} (needed)", missing.empty() ? "" : ", ", ref->Path());
        }
    }
    return missing.empty();
}

#elif defined(MEVR_GAME_LE2)
// ME2 (its SFXGameModeDefault.TryMelee, read 2026-11-17): Pawn.Weapon, not zoomed, not in the cover state, the weapon neither
// coming out nor going away, then the weapon's MeleePowerName through BioPlayerInput.ActivatePower; the power is a BioPower in
// the behaviour's m_powers (SFXCustomAction_Melee.Impact finds it there), with its own cooldown. Drawn is the action state
// (BioPawn.ActionState == ActionState_Combat), as game/Holsters reads it.
ue3::FieldRef gActionState{"SFXGame.BioPawn", "ActionState", ue3::kEveryGame};
ue3::FieldRef gWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
ue3::ClassRef gSfxWeapon{"SFXGame.SFXWeapon", ue3::kEveryGame};
ue3::FieldRef gMeleePowerName{"SFXGame.SFXWeapon", "MeleePowerName", ue3::kEveryGame};
ue3::FunctionRef gIsZoomedFn{"SFXGame.BioPlayerController.IsZoomed", ue3::kEveryGame};
ue3::FunctionRef gIsInCoverStateFn{"SFXGame.BioPlayerController.IsInCoverState", ue3::kEveryGame};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons", ue3::kEveryGame};
ue3::FunctionRef gIsUsingPowerFn{"SFXGame.BioPawn.IsUsingPower", ue3::kEveryGame};
ue3::FunctionRef gBlockingActionFn{"SFXGame.BioPawn.IsPerformingBlockingAction", ue3::kEveryGame};
ue3::FunctionRef gGetStateNameFn{"Core.Object.GetStateName", ue3::kEveryGame};
ue3::FunctionRef gCurrentHealthFn{"SFXGame.BioPawn.GetCurrentHealth", ue3::kEveryGame};
ue3::FunctionRef gMaxHealthFn{"SFXGame.BioPawn.GetMaxHealth", ue3::kEveryGame};
ue3::FunctionRef gCurrentShieldsFn{"SFXGame.BioPawn.GetCurrentShields", ue3::kEveryGame};
ue3::FunctionRef gMaxShieldsFn{"SFXGame.BioPawn.GetMaxShields", ue3::kEveryGame};
constexpr std::uint8_t kActionStateCombat = 1;  // EActionState.ActionState_Combat

bool Drawn(Game const& game) {
    return gActionState.Read<std::uint8_t>(game.pawn).value_or(0) == kActionStateCombat;
}

ue3::Object* HeldWeapon(Game const& game) {
    ue3::Object* const weapon = ObjectOf(gWeapon, game.pawn);
    return weapon != nullptr && ue3::runtime::IsAlive(weapon) && ue3::IsA(weapon, gSfxWeapon) ? weapon : nullptr;
}

std::string StateName(ue3::Object* object) {
    ue3::Function* const function = gGetStateNameFn.Get();
    if (object == nullptr || function == nullptr) {
        return {};
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    auto const name = parms.Return<ue3::Name>();
    return name ? ue3::runtime::NameText(*name) : std::string();
}

char const* Busy(Game const& game, int /*hand*/) {
    // TryMelee's own refusals first (silent in the game: said here), then what makes the pawn busy.
    if (CallBool(game.controller, gIsZoomedFn)) {
        return "aiming down the sights (zoomed: the game's melee refuses then)";
    }
    if (CallBool(game.controller, gIsInCoverStateFn)) {
        return "in cover";
    }
    ue3::Object* const weapon = HeldWeapon(game);
    if (weapon == nullptr) {
        // Seen on Save 4, 2026-11-17: holstered, ME2's pawn holds no weapon at all, and its melee is the held weapon's (TryMelee
        // casts its MeleePowerName; SFXCustomAction_Melee.Impact finds the power through it again): no punch with the weapon away.
        return "no weapon in hand: ME2's melee is the held weapon's own (holstered, Pawn.Weapon is None), so no punch with the "
               "weapon away";
    }
    std::string const state = StateName(weapon);
    if (state == "WeaponEquipping" || state == "WeaponPuttingDown") {
        return "the weapon is coming out or going away";
    }
    ue3::Function* const switching = gIsSwitchingWeaponsFn.Get();
    bool switchingNow = false;
    if (switching != nullptr) {
        ue3::Params parms(ue3::runtime::Get(), switching);
        parms.SetBool("bBlendOut", false);
        ue3::runtime::Call(game.pawn, switching, parms);
        switchingNow = parms.ReturnBool().value_or(false);
    }
    if (switchingNow || CallBool(game.pawn, gIsUsingPowerFn) || CallBool(game.pawn, gBlockingActionFn)) {
        return "switching weapons, using a power or in an action that blocks";
    }
    return nullptr;
}

MeleePower FindMeleePower(Game const& game, bool) {
    ue3::Object* const weapon = HeldWeapon(game);
    std::optional<ue3::Name> const name = weapon != nullptr ? gMeleePowerName.Read<ue3::Name>(weapon) : std::nullopt;
    std::string const label = name ? ue3::runtime::NameText(*name) : std::string();
    return PowerNamed(game, label == "None" ? std::string() : label);
}

// The B button's exec, directly (never a generated wrapper): its own checks, then the held weapon's melee power.
std::optional<std::string> DispatchWeapon(Game const& game, MeleePower const& power, Intent, int, std::string& why) {
    if (power.label.empty()) {
        why = "the held weapon has no melee power (MeleePowerName None)";
        return std::nullopt;
    }
    CallVoid(game.mode, gTryMeleeFn);
    return std::format("SFXGameModeDefault.TryMelee ({})", power.label);
}

// The weapon away: the held weapon's melee power through the engine's "cast this", at the enemy (as the power wheel's cast on
// ME2, with the head's view as the camera).
std::optional<std::string> DispatchFist(Game const& game, MeleePower const& power, Enemy const& enemy, std::string& why) {
    ue3::Object* const input = ObjectOf(gPlayerInput, game.controller);
    ue3::Function* const activate = gActivatePowerFn.Get();
    if (power.power == nullptr || input == nullptr || activate == nullptr) {
        why = power.power == nullptr
            ? std::format("the player has no {} power to punch with", power.label.empty() ? std::string("melee") : power.label)
            : std::string("no BioPlayerInput.ActivatePower to punch through");
        return std::nullopt;
    }
    ue3::Params parms(ue3::runtime::Get(), activate);
    parms.Set("nmPower", power.name);
    if (enemy.pawn != nullptr) {
        parms.Set("oTarget", enemy.pawn);
        parms.Set("vTargetLocation", player::Location(enemy.pawn).value_or(ue3::Vector{}));
    }
    if (auto const view = view_anchor::Current()) {
        auto const rotation = engine::ToRotation(engine::OrientationToWorld(*view, view->head.orientation).forward);
        parms.Set("vOriginalCameraLocation", ue3::Vector{static_cast<float>(view->headWorld.x),
            static_cast<float>(view->headWorld.y), static_cast<float>(view->headWorld.z)});
        parms.Set("rOriginalCameraRotation", ue3::Rotator{rotation.pitch, rotation.yaw, 0});
    }
    ue3::runtime::Call(input, activate, parms);  // directly: never a generated wrapper
    if (!parms.ReturnBool().value_or(false)) {
        why = std::format("the game refused the punch (BioPlayerInput.ActivatePower({}) returned false)", power.label);
        return std::nullopt;
    }
    return std::format("BioPlayerInput.ActivatePower({}) (a punch, the weapon away)", power.label);
}

float CallFloat(ue3::Object* object, ue3::FunctionRef& ref, bool& ok) {
    ue3::Function* const function = ref.Get();
    if (object == nullptr || function == nullptr) {
        ok = false;
        return 0.0f;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    auto const value = parms.Return<float>();
    ok = ok && value.has_value() && std::isfinite(*value);
    return value.value_or(0.0f);
}

// The target's health and shields together, 0..1 (a melee takes the shields first); negative: unknown.
double Health(ue3::Object* pawn) {
    bool ok = true;
    float const health = CallFloat(pawn, gCurrentHealthFn, ok);
    float const healthMax = CallFloat(pawn, gMaxHealthFn, ok);
    float const shields = CallFloat(pawn, gCurrentShieldsFn, ok);
    float const shieldsMax = CallFloat(pawn, gMaxShieldsFn, ok);
    double const total = static_cast<double>(healthMax) + std::max(0.0f, shieldsMax);
    if (!ok || !(total > 0.0)) {
        return -1.0;
    }
    return std::clamp((static_cast<double>(health) + std::max(0.0f, shields)) / total, 0.0, 1.0);
}

constexpr bool kWatchHits = true;

bool StartupNatives(std::string& missing) {
    for (ue3::FunctionRef* ref : {&gTryMeleeFn, &gIsCombatEnabledFn, &gIsZoomedFn, &gIsInCoverStateFn}) {
        if (ref->Get() == nullptr) {
            missing += std::format("{}{} (needed)", missing.empty() ? "" : ", ", ref->Path());
        }
    }
    for (ue3::FieldRef* field : {&gActionState, &gWeapon, &gMeleePowerName, &gPowers}) {
        if (field->Get() == nullptr) {
            missing += std::format("{}a field (needed)", missing.empty() ? "" : ", ");
        }
    }
    return missing.empty();
}

#elif defined(MEVR_GAME_LE3)
// ME3's native adapter (research/le3-melee-audit.md; installed SFXGame scripts and the pinned LE3 SDK): light and heavy
// requests are separate native entry points (TryMelee, TryHeavyMelee: void); an action's start is observed separately from
// the request (BioPawn.GetCurrentCustomAction), never taken as impact or damage. Its admission is ME3's own (native action,
// reload and zoom, the weapon's state, cover and storm) and the host's hand ownership (the staged Link). No LE2 controller
// action byte or copied damage implementation.
ue3::ClassRef gModeClass{"SFXGame.SFXGameModeDefault", ue3::kEveryGame};
ue3::ClassRef gWeaponClass{"SFXGame.SFXWeapon", ue3::kEveryGame};
ue3::ClassRef gActionClass{"SFXGame.BioCustomAction", ue3::kEveryGame};
ue3::ClassRef gLightClass{"SFXGame.SFXCustomAction_PlayerMeleeBase", ue3::kEveryGame};
ue3::ClassRef gHeavyClass{"SFXGame.SFXCustomAction_PlayerHeavyMeleeBase", ue3::kEveryGame};
ue3::FieldRef gWorld{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
ue3::FieldRef gWeapon{"Engine.Pawn", "Weapon", ue3::kEveryGame};
ue3::FieldRef gCurrentAction{"SFXGame.BioPawn", "CurrentCustomAction", ue3::kEveryGame};
ue3::FieldRef gActionPawn{"SFXGame.BioCustomAction", "m_oPawn", ue3::kEveryGame};
ue3::FunctionRef gGetMode{"SFXGame.BioPlayerController.GetGameModeDefault", ue3::kEveryGame};
ue3::FunctionRef gGetAction{"SFXGame.BioPawn.GetCurrentCustomAction", ue3::kEveryGame};
ue3::FunctionRef gReloading{"SFXGame.BioPlayerController.IsReloading", ue3::kEveryGame};
ue3::FunctionRef gZoomed{"SFXGame.BioPlayerController.IsZoomed", ue3::kEveryGame};
ue3::FunctionRef gState{"Core.Object.GetStateName", ue3::kEveryGame};
ue3::FunctionRef gHeavy{"SFXGame.SFXGameModeDefault.TryHeavyMelee", ue3::kEveryGame};
// The omni-blade with the weapon away (docs/trilogy-plan.md "Answered 2026-11-18, later", option A): TryHeavyMelee does nothing
// without a weapon (its first test is Pawn.Weapon), and with one it starts the pawn's custom action 58 through this function
// (the class's own heavy melee: SFXCustomAction_PlayerHeavyMeleeBase's subclass, the biotic one for Vanguards and Adepts). The
// fist starts the same action itself.
ue3::FunctionRef gStartSynced{"SFXGame.BioPlayerController.StartCustomActionWithSyncPartner", ue3::kEveryGame};
constexpr std::int32_t kHeavyMeleeAction = 58;  // TryHeavyMelee's own, out of cover, not storming, not aiming back
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
Link const* gLink = nullptr;  // the staged Link of the call in progress (Tick, Request): the host's context for admission
ue3::Object* gObserved = nullptr;
bool gAwaiting = false;
Intent gIntent = Intent::Light;
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
ue3::Object* Live(ue3::FieldRef& field, ue3::Object* owner) {
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
    if (out.pawn) out.weapon = Live(gWeapon, out.pawn);
    out.world = Live(gWorld, out.pc);
    out.mode = player::GameMode(out.pc).value_or(255);
    return out;
}
void RefreshContext(Link const& link) {
    auto const context = ReadContext(link);
    if (!(context == gContext)) {
        if (gAwaiting || gObserved != nullptr) SetStatus("observation cancelled: scene/actor/mode changed (native action not modified)");
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
    if (!ue3::runtime::IsAlive(action) || !ue3::IsA(action, gActionClass) || Live(gActionPawn, action) != gContext.pawn)
        return std::nullopt;
    return action;
}
// The staged admission in full (the order of reasons the adapter has always given): the Tick's availability of each hand.
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
    auto* input = Live(gPlayerInput, gContext.pc);
    if (!ue3::IsA(input, gBioPlayerInput) || Bool(input, gIsCombatEnabledFn) != true) return "combat input unavailable";
    if (!player::MovementAdmitted(gContext.pc, gContext.pawn, admit::kOutOfCover | admit::kNoCoverAction | admit::kNotStorming))
        return "contextual cover/storm melee requires separate intent";
    auto const current = CurrentAction();
    if (!current || *current != nullptr || gAwaiting || gObserved != nullptr) return "native action or unresolved admission pending";
    if (Bool(gContext.pc, gReloading) != false || Bool(gContext.pc, gZoomed) != false) return "reload or zoom blocks melee";
    auto* weapon = Live(gWeapon, gContext.pawn);
    if (weapon == nullptr && hand == kRight) return nullptr;  // the weapon away: the right fist's omni-blade (DispatchFist)
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
        SetStatus("native action observation unavailable");
        gAwaiting = false;
        gObserved = nullptr;
        return;
    }
    if (gObserved != nullptr && *current != gObserved) {
        SetStatus("observed action left current slot; impact/damage/completion not established");
        gObserved = nullptr;
    }
    if (gAwaiting && *current != nullptr) {
        bool const matches = ue3::IsA(*current, gIntent == Intent::Heavy ? gHeavyClass : gLightClass);
        SetStatus(std::format("observed {} action {} id {} (impact not established)", matches ? "requested family" : "different family",
            ue3::runtime::FullName(*current), gCurrentAction.Read<std::int32_t>(gContext.pawn).value_or(-1)));
        gObserved = *current;
        gAwaiting = false;
    } else if (gAwaiting && Now() >= gDeadline) {
        SetStatus("dispatched; no action start observed in admission window");
        gAwaiting = false;
    }
}

std::optional<Game> ReadGame(std::string& why) {
    if (!gStarted || !ue3::runtime::Up()) {
        why = "adapter not ready";
        return std::nullopt;
    }
    if (gLink == nullptr) {
        why = "no host context (ME3's staged melee asks through Tick or Request)";
        return std::nullopt;
    }
    RefreshContext(*gLink);
    if (gContext.pc == nullptr || gContext.pawn == nullptr) {
        why = "not ordinary Shepard gameplay";
        return std::nullopt;
    }
    return Game{gContext.pc, gContext.pawn, nullptr, nullptr};
}

bool Drawn(Game const& game) {
    return ue3::IsA(Live(gWeapon, game.pawn), gWeaponClass);
}

bool Storming(Game const& game) {
    return !player::MovementAdmitted(game.controller, game.pawn, player::admission::kNotStorming);
}

char const* Busy(Game const&, int hand) {
    return gLink != nullptr ? Refusal(*gLink, hand) : "no host context";
}

MeleePower FindMeleePower(Game const&, bool) {
    return MeleePower{nullptr, {}, 0.0f, "melee"};  // ME3's melee is a custom action, not a power with a cooldown
}

std::optional<std::string> DispatchWeapon(Game const&, MeleePower const&, Intent intent, int hand, std::string& why) {
    Link const& link = *gLink;
    auto* get = gGetMode.Get();
    ue3::Params modeParams(ue3::runtime::Get(), get);
    if (!modeParams.Valid() || !ue3::runtime::Call(gContext.pc, get, modeParams)) {
        why = "no native game mode";
        return std::nullopt;
    }
    auto* mode = modeParams.Return<ue3::Object*>().value_or(nullptr);
    if (!ue3::runtime::IsAlive(mode) || !ue3::IsA(mode, gModeClass) || ue3::runtime::Get().OuterOf(mode) != gContext.pc) {
        why = "native game mode owner mismatch";
        return std::nullopt;
    }
    // Same-thread final ownership/mode check; the functions above were reads, Try* below is the first attack mutation.
    if (!(ReadContext(link) == gContext) || link.allowRequest == nullptr || !link.allowRequest(hand)) {
        why = "context changed before dispatch";
        return std::nullopt;
    }
    auto* fn = (intent == Intent::Heavy ? gHeavy : gTryMeleeFn).Get();
    ue3::Params params(ue3::runtime::Get(), fn);
    if (!params.Valid() || params.Size() != 0) {
        why = "unexpected request signature";
        return std::nullopt;
    }
    gIntent = intent;
    gAwaiting = true;
    gDeadline = Now() + 0.35;
    if (!ue3::runtime::Call(mode, fn, params)) {
        gAwaiting = false;
        why = "dispatch failed";
        return std::nullopt;
    }
    SetStatus(intent == Intent::Heavy ? "heavy request dispatched; admission unobserved" : "light request dispatched; admission unobserved");
    Observe();
    return std::string(intent == Intent::Heavy ? "SFXGameModeDefault.TryHeavyMelee" : "SFXGameModeDefault.TryMelee") + " (" + gStatus + ")";
}

std::optional<std::string> DispatchFist(Game const&, MeleePower const&, Enemy const&, std::string& why) {
    Link const& link = *gLink;
    // Same-thread final ownership/mode check (as DispatchWeapon's); the weapon must still be away.
    if (!(ReadContext(link) == gContext) || link.allowRequest == nullptr || !link.allowRequest(kRight)) {
        why = "context changed before dispatch";
        return std::nullopt;
    }
    if (gContext.weapon != nullptr) {
        why = "a weapon came into the hand";
        return std::nullopt;
    }
    auto* fn = gStartSynced.Get();
    ue3::Params params(ue3::runtime::Get(), fn);
    if (!params.Valid() || !params.Set("NewAction", kHeavyMeleeAction)) {
        why = "unexpected StartCustomActionWithSyncPartner signature";
        return std::nullopt;
    }
    gIntent = Intent::Heavy;
    gAwaiting = true;
    gDeadline = Now() + 0.35;
    if (!ue3::runtime::Call(gContext.pc, fn, params)) {
        gAwaiting = false;
        why = "dispatch failed";
        return std::nullopt;
    }
    bool const started = params.ReturnBool().value_or(false);
    SetStatus(std::format("the omni-blade's heavy melee (custom action {}) {} with the weapon away", kHeavyMeleeAction,
        started ? "started" : "refused by the game"));
    Observe();
    if (!started) {
        gAwaiting = false;
        why = std::format("the game refused custom action {} (StartCustomActionWithSyncPartner returned false)", kHeavyMeleeAction);
        return std::nullopt;
    }
    return std::format("BioPlayerController.StartCustomActionWithSyncPartner({}): the omni-blade, the weapon away ({})",
        kHeavyMeleeAction, gStatus);
}

double Health(ue3::Object*) {
    return -1.0;
}

constexpr bool kWatchHits = false;  // the action's own observation stands in (Observe)

bool StartupNatives(std::string& missing) {
    for (auto* cls : {&gModeClass, &gBioPlayerInput, &gWeaponClass, &gActionClass, &gLightClass, &gHeavyClass})
        if (cls->Get() == nullptr) missing = "a class";
    for (auto* field : {&gWorld, &gWeapon, &gPlayerInput, &gCurrentAction, &gActionPawn})
        if (field->Get() == nullptr) missing = "a field";
    for (auto* fn : {&gGetMode, &gGetAction, &gReloading, &gZoomed, &gIsCombatEnabledFn, &gState, &gTryMeleeFn, &gHeavy, &gStartSynced})
        if (fn->Get() == nullptr) missing = "a function";
    if (!missing.empty()) return false;
    // Parameter/type guards in addition to named block construction. No guessed offsets. The movement facts' names and
    // shapes (cover, storming, ignored input, death) are game/Player's to check.
    if (!Shape(gCurrentAction, ue3::Kind::Int, 4) || !player::MovementAdmissionReady()) return false;
    for (auto* field : {&gWorld, &gWeapon, &gPlayerInput, &gActionPawn})
        if (!Shape(*field, ue3::Kind::Object, 8)) return false;
    for (auto* fn : {&gReloading, &gZoomed, &gIsCombatEnabledFn})
        if (!ReturnShape(*fn, ue3::Kind::Bool, 4)) return false;
    if (!ReturnShape(gGetMode, ue3::Kind::Object, 8) || !ReturnShape(gState, ue3::Kind::Name, 8)) return false;
    {
        ue3::Params synced(ue3::runtime::Get(), gStartSynced.Get());
        auto const* action = synced.Find("NewAction");
        auto const* started = synced.ReturnSpec();
        if (!synced.Valid() || action == nullptr || action->kind != ue3::Kind::Int || started == nullptr ||
            started->kind != ue3::Kind::Bool)
            return false;
    }
    ue3::Params actionParams(ue3::runtime::Get(), gGetAction.Get());
    auto const* actionOut = actionParams.Find("pAction");
    auto const* actionReturn = actionParams.ReturnSpec();
    if (!actionParams.Valid() || actionParams.Size() != 12 || actionParams.Specs().size() != 2 ||
        actionOut == nullptr || actionOut->kind != ue3::Kind::Object || actionOut->size != 8 || actionOut->offset != 0 ||
        actionReturn == nullptr || actionReturn->kind != ue3::Kind::Bool || actionReturn->size != 4 || actionReturn->offset != 8)
        return false;
    for (auto* fn : {&gTryMeleeFn, &gHeavy}) {
        ue3::Params params(ue3::runtime::Get(), fn->Get());
        if (!params.Valid() || params.Size() != 0 || !params.Specs().empty()) return false;
    }
    return true;
}
#endif

// Every game: the nearest living hostile in reach and in front of the body (the body's front: the last sample's yaw; no head
// frame: any direction). `nearestHostile`: the nearest one at all, for the refusal's words.
Enemy FindEnemy(Game const& game, engine::TrackingFrame const* frame, ik::Vec3 headTracking, double reachMeters,
    double coneDegrees, Enemy& nearestHostile) {
    Enemy best;
    ue3::Object* const world = ObjectOf(gWorldInfo, game.pawn);
    ue3::Function* const hostileFn = gIsHostileFn.Get();
    if (world == nullptr || hostileFn == nullptr) {
        return best;
    }
    // The body's front in the world (horizontal).
    engine::UnrealVector front{1.0, 0.0, 0.0};
    bool haveFront = false;
    if (frame != nullptr) {
        ik::Vec3 const fwd = slots::YawForward(gYaw);
        XrVector3f const a{static_cast<float>(headTracking.x), static_cast<float>(headTracking.y), static_cast<float>(headTracking.z)};
        XrVector3f const b{static_cast<float>(headTracking.x + fwd.x), static_cast<float>(headTracking.y + fwd.y),
            static_cast<float>(headTracking.z + fwd.z)};
        front = Sub(engine::ToWorld(*frame, b), engine::ToWorld(*frame, a));
        front.z = 0.0;
        haveFront = true;
    }
    double const frontLength = std::max(Len(front), 1e-6);
    double const upm = frame != nullptr && frame->unitsPerMeter > 0.0 ? frame->unitsPerMeter : engine::kUnitsPerMeter;
    engine::UnrealVector const me = LocationOf(game.pawn);
    int guard = 0;
    for (ue3::Object* p = ObjectOf(gPawnList, world); p != nullptr && guard < 512; p = ObjectOf(gNextPawn, p), ++guard) {
        if (p == game.pawn || !player::IsBioPawn(p)) {
            continue;
        }
        ue3::Object* const other = p;
        if (CallBool(other, gIsDeadFn)) {
            continue;
        }
        ue3::Params hostile(ue3::runtime::Get(), hostileFn);
        hostile.Set("Other", other);
        ue3::runtime::Call(game.pawn, hostileFn, hostile);
        if (!hostile.ReturnBool().value_or(false)) {
            continue;
        }
        engine::UnrealVector const d = Sub(LocationOf(other), me);
        double const flat = std::sqrt(d.x * d.x + d.y * d.y);
        double const meters = flat / upm;
        double const cosine = haveFront && flat > 1e-6 ? (d.x * front.x + d.y * front.y) / (flat * frontLength) : 1.0;
        double const degrees = std::acos(std::clamp(cosine, -1.0, 1.0)) * 180.0 / std::numbers::pi;
        if (meters < nearestHostile.meters) {
            nearestHostile = {other, meters, degrees};
        }
        if (meters <= reachMeters && degrees <= coneDegrees && std::abs(d.z) / upm < 2.0 && meters < best.meters) {
            best = {other, meters, degrees};
        }
    }
    return best;
}

// The one dispatcher (research/20 "Gesture only"): a stroke that passed the gesture's own rules becomes the game's melee here,
// or is refused (logged, counted, no haptic). With the weapon out either hand's stroke is the game's melee with it; with it away
// only the right hand's FIST (grip and trigger held) punches, through the melee power directly. No spamming: both hands share one
// cooldown (meleeCooldownS) and wait out the game's own melee cooldown, read live from the power, so every strike the gesture
// makes is one the game swings; a stroke refused for it is spent (the next one needs a new stroke).
void Strike(Game const& game, int hand, thr::SwingStep const& stroke, hand_calibration::Throwables const& settings,
    engine::TrackingFrame const* frame, ik::Vec3 headTracking, bool fist, double coneDegrees, Intent intent) {
    std::string const what = std::format("the {} hand's {:.1f} m/s stroke ({:.2f} m, {:.2f} toward the front{}{})", HandName(hand),
        stroke.speed, stroke.travel, stroke.forward, fist ? ", a fist" : "", intent == Intent::Heavy ? ", heavy" : "");
    auto const refuse = [&](std::string const& why) {
        ++gMeleeRefusals;
        gLast = std::format("refused: {}", why);
        log::Info("melee: {}: no strike ({})", what, why);
    };
    if (!settings.melee) {
        return;
    }
    if (!CombatEnabled(game)) {
        return refuse("combat is disabled here");
    }
    bool const drawn = Drawn(game);
    thr::MeleeRoute const route = thr::RouteMelee(drawn, hand == kRight, fist, settings.meleeFist);
    if (char const* const no = thr::Refusal(route)) {
        return refuse(no);
    }
    if (Storming(game)) {
        return refuse("sprinting");
    }
    if (char const* const busy = Busy(game, hand)) {
        return refuse(busy);
    }
    MeleePower const power = FindMeleePower(game, drawn);
    if (double const wait = thr::MeleeWait(gTime, gReadyAt, power.cooldown); wait > 0.0) {
        return refuse(std::format("cooldown, {:.2f} s left ({})", wait,
            power.cooldown > gReadyAt - gTime ? std::format("the game's {}", power.label) : std::string("meleeCooldownS")));
    }
    Enemy nearest;
    Enemy const enemy = FindEnemy(game, frame, headTracking, settings.meleeReach, coneDegrees, nearest);
    if (settings.meleeNeedsEnemy && enemy.pawn == nullptr) {
        return refuse(nearest.pawn != nullptr
            ? std::format("no enemy within {:.1f} m and {:.0f} degrees; the nearest hostile is {} at {:.1f} m, {:.0f} degrees",
                  settings.meleeReach, coneDegrees, ue3::runtime::FullName(nearest.pawn), nearest.meters, nearest.degrees)
            : std::string("no hostile around"));
    }
    std::string why;
    std::optional<std::string> const how = drawn ? DispatchWeapon(game, power, intent, hand, why) : DispatchFist(game, power, enemy, why);
    if (!how) {
        return refuse(why);
    }
    gReadyAt = gTime + settings.meleeCooldown;
    ++gStrikes;
    Pulse(gHaptics, hand, thr::StrikeTick(settings.hapticAmplitude));
    double const health = enemy.pawn != nullptr ? Health(enemy.pawn) : -1.0;
    std::string const target = enemy.pawn != nullptr
        ? std::format("{} at {:.1f} m, {:.0f} degrees{}", ue3::runtime::FullName(enemy.pawn), enemy.meters, enemy.degrees,
              health >= 0.0 ? std::format(", health {:.0f}%", 100.0 * health) : std::string())
        : std::string("no enemy in reach (meleeNeedsEnemy = 0)");
    gLast = std::format("{} -> {}; {}", what, *how, target);
    log::Info("melee: {} -> {}; target {}", what, *how, target);
    if (kWatchHits && enemy.pawn != nullptr) {
        gHitWatch = {true, hand, 0.0, reinterpret_cast<std::uintptr_t>(enemy.pawn), health, ue3::runtime::FullName(enemy.pawn)};
    }
}

hand_calibration::Throwables gSettings;  // this tick's [Throwables]

// The physical rules' strokes on a new sample (ME1 and ME2): each hand's detector with its suppression, then each strike
// through Strike, each refused stroke logged.
void PhysicalStrokes(Physical const& tick) {
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
    std::string why;
    std::optional<Game> const game = ReadGame(why);
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    if (!game || !frame) {
        return;  // the feed made sure of both (ME1: game/Throwables' active)
    }
    vr::controllers::State const controllers = vr::controllers::Latest();
    std::array<vr::controllers::Hand const*, 2> const hands{&controllers.left, &controllers.right};
    bool const drawn = Drawn(*game);
    ik::melee::Input in;
    in.rules = ik::melee::Rules::Physical;
    in.allowed = true;
    for (int h = 0; h < 2; ++h) {
        ik::melee::Hand& hand = in.hands[static_cast<std::size_t>(h)];
        hand.motion.now = gTime;
        hand.motion.tracked = hands[h]->gripValid;
        hand.motion.position = FromXr(hands[h]->grip.position) - tick.head;
        hand.motion.bodyForward = slots::YawForward(gYaw);
        bool suppressed = tick.suppressed[static_cast<std::size_t>(h)];
        if (h == kRight) {
            // The trigger held is firing only with the weapon out; with it away it's half of the fist that punches.
            suppressed = suppressed || (drawn && hands[h]->trigger >= kTriggerOn) || holsters::RightHandInSlot() ||
                holsters::SecondsSinceDraw() < kNoMeleeAfterDraw;
        } else {
            suppressed = suppressed || support_grip::Held();
        }
        hand.motion.suppressed = suppressed;
        thr::SwingParams params;
        params.minSpeed = h == kRight && drawn ? settings.meleeGunSpeed : settings.meleeSpeed;
        params.minTravel = settings.meleeTravel;
        params.cooldown = settings.meleeCooldown;
        hand.params = params;
    }
    ik::melee::Strokes const strokes = gGestures.StepAll(in);
    if (!settings.melee) {
        return;
    }
    for (int h = 0; h < 2; ++h) {
        thr::SwingStep const& stroke = strokes.hands[static_cast<std::size_t>(h)];
        if (strokes.requests[static_cast<std::size_t>(h)]) {
            bool const fist = hands[h]->squeeze >= kGripOn && hands[h]->trigger >= kTriggerOn;
            Strike(*game, h, stroke, settings, &*frame, tick.head, fist, settings.meleeConeDeg, Intent::Light);
        } else if (stroke.refused != nullptr) {
            ++gStrokeRefusals;
            log::Info("melee: the {} hand's {:.1f} m/s stroke ({:.2f} m) isn't a strike: {}", HandName(h), stroke.speed, stroke.travel,
                stroke.refused);
        }
    }
}

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// The sequels: on foot in VR, the player there, the controllers ours ([Throwables] enabled): what game/Throwables' active is on
// ME1, without the grenades (n/a on the sequels). B's rule asks it each tick; ME2's strokes run on it.
bool SequelOnFoot(std::string& why) {
    if (!gSettings.enabled) {
        why = "off ([Throwables] enabled = 0)";
        return false;
    }
    if (!ue3::runtime::Up()) {
        why = "our reflection layer is off";
        return false;
    }
    ue3::Object* const pc = player::Controller();
    std::optional<std::uint8_t> const mode = pc != nullptr ? player::GameMode(pc) : std::nullopt;
    if (!mode || *mode != 0 || player::BioPawn(pc) == nullptr) {
        why = std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown"));
        return false;
    }
    if (!vr::host_view::Latest().valid) {
        why = "no head-tracked world view (menu, flat screen or VR off)";
        return false;
    }
    if (!vr::controllers::Latest().active || body_calibration::Calibrating()) {
        why = "the controllers aren't active";
        return false;
    }
    if (!view_anchor::Current()) {
        why = "no view anchor (the hands can't be placed in the world)";
        return false;
    }
    return true;
}
#endif

#if defined(MEVR_GAME_LE2)
// ME2's feed of the physical rules: game/Throwables' frame on ME1 (the sample clock, arm IK's torso yaw else our own lagging
// one), without the grenade slot.
thr::SampleClock gClock;
ik::BodyYaw gOwnYaw;
bool gOwnYawActive = false;

void Le2Tick(double deltaSeconds, Link const& link) {
    Haptics const haptics{link.haptic, link.hapticHz};
    WatchHit(deltaSeconds, haptics);
    Physical tick;
    tick.haptics = haptics;
    tick.buttonFlags = link.buttonFlags;
    std::string why;
    tick.active = SequelOnFoot(why);
    std::string game;
    if (tick.active && (!gStarted || !ReadGame(game))) {
        why = !gStarted ? std::string("waiting for the script functions") : game;
        tick.active = false;
    }
    if (tick.active) {
        vr::host_view::State const view = vr::host_view::Latest();
        XrPosef const& head = view.head;
        double const headYaw = ik::YawOf(head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w);
        if (!gOwnYawActive) {
            gOwnYaw.Reset(headYaw);
            gOwnYawActive = true;
        }
        double const ownYaw = gOwnYaw.Update(headYaw, deltaSeconds);
        std::optional<double> const torso = arm_ik::TorsoYaw();
        tick.bodyYaw = torso ? *torso : ownYaw;
        tick.head = {head.position.x, head.position.y, head.position.z};
        tick.sampleTime = gClock.Update(vr::controllers::Latest().sequence, SteadySeconds());
        SetStatus("active (the physical rules, ME1's)");
    } else {
        gOwnYawActive = false;
        SetStatus(why);
    }
    StepPhysical(tick);
}
#endif

#if defined(MEVR_GAME_LE3)
// ME3's staged rules (on by default since 2026-11-17, the heavy chamber opt-in): the host's hands, each one's availability from
// the full admission, the requests through Strike.
void Le3Tick(Link const& link) {
    std::string why;
    gButtonFlags = link.buttonFlags;
    gHaptics = {link.haptic, link.hapticHz};
    if (!gStarted || !ue3::runtime::Up()) {
        gGestures.Reset();
        return;
    }
    gLink = &link;
    RefreshContext(link);
    Observe();
    auto input = link.input;
    input.rules = ik::melee::Rules::Staged;
    input.allowed = input.allowed && link.gestures && link.enabled && link.focused && !link.menuOpen && !link.authoredSeated;
    if (!input.allowed) {
        gGestures.StepAll(input);
        gLink = nullptr;
        return;
    }
    for (int hand = 0; hand < 2; ++hand) {
        input.hands[static_cast<std::size_t>(hand)].available &= Refusal(link, hand) == nullptr;
    }
    auto const strokes = gGestures.StepAll(input);
    if (gGestures.HeavyArmedNow() && link.haptic && link.allowRequest && link.allowRequest(kRight)) {
        link.haptic(kRight, 0.045f, 0.35f);  // readiness only; native admission/impact are separate observations
    }
    gSettings = hand_calibration::GetThrowables();
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    vr::host_view::State const view = vr::host_view::Latest();
    ik::Vec3 const head{view.head.position.x, view.head.position.y, view.head.position.z};
    for (int h = 0; h < 2; ++h) {
        auto const& request = strokes.requests[static_cast<std::size_t>(h)];
        if (!request) continue;
        // No asynchronous queue: Strike rechecks every admission at once, without calling Try* as a probe.
        std::optional<Game> const game = ReadGame(why);
        if (game) {
            gTime = std::max(gTime, SteadySeconds());
            Strike(*game, request->hand, strokes.hands[static_cast<std::size_t>(h)], gSettings, frame ? &*frame : nullptr, head,
                FistAway(request->hand), gSettings.meleeConeDeg, request->intent);
        } else {
            gLast = "refused: " + why;
        }
        log::Info("melee: stroke {} hand {}: {}", request->stroke, request->hand, gLast);
    }
    gLink = nullptr;
}
#endif

} // namespace

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
bool PhysicalControlsOnFoot(std::string& why) {
    return SequelOnFoot(why);
}
#endif

bool TryStartup() {
    std::string missing;
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    if (!ue3::runtime::Up()) {
        return false;
    }
    gStarted = StartupNatives(missing);
#endif
    log::Info("melee: script functions {}{}", gStarted ? "found" : "MISSING: melee by gesture stays off",
        missing.empty() ? std::string() : std::format(" (not found: {})", missing));
#if defined(MEVR_GAME_LE3)
    if (gStarted) SetStatus("ready (ME3's stroke melee on by default; the heavy chamber opt-in; gameplay/headset acceptance pending)");
#endif
    return gStarted;
}

void StepPhysical(Physical const& tick) {
    gHaptics = tick.haptics;
    gButtonFlags = tick.buttonFlags;
    gPhysicalActive = tick.active;
    gSettings = hand_calibration::GetThrowables();
    if (!tick.active) {
        ik::melee::Input in;
        in.rules = ik::melee::Rules::Physical;
        in.allowed = false;
        gGestures.StepAll(in);  // the strokes start over
        return;
    }
    if (!tick.sampleTime) {
        return;  // the strokes run on new samples only (their velocities need real time steps)
    }
    gTime = *tick.sampleTime;
    gYaw = tick.bodyYaw;
    if (!gStarted) {
        return;
    }
    PhysicalStrokes(tick);
}

void WatchHit(double deltaSeconds, Haptics const& haptics) {
    if (!gHitWatch.active) {
        return;
    }
    gHitWatch.age += deltaSeconds;
    auto* const pawn = reinterpret_cast<ue3::Object*>(gHitWatch.target);
    bool const alive = ue3::runtime::IsAlive(pawn) && player::IsBioPawn(pawn);
    double const health = alive ? Health(pawn) : -1.0;
    bool const dead = alive && CallBool(pawn, gIsDeadFn);
    if (alive && (dead || (health >= 0.0 && health < gHitWatch.health - 1e-4))) {
        ++gHits;
        Pulse(haptics, gHitWatch.hand, thr::HitThump(hand_calibration::GetThrowables().hapticAmplitude));
        log::Info("melee: hit {} after {:.2f} s (health {:.0f}% -> {:.0f}%{})", gHitWatch.name, gHitWatch.age,
            100.0 * gHitWatch.health, 100.0 * std::max(health, 0.0), dead ? ", dead" : "");
        gLast += std::format("; hit ({:.0f}% -> {:.0f}%)", 100.0 * gHitWatch.health, 100.0 * std::max(health, 0.0));
        gHitWatch = {};
    } else if (!alive || gHitWatch.age > kHitWatchSeconds) {
        log::Info("melee: no damage seen on {} within {:.1f} s (a miss, out of the game's melee cone, or blocked)", gHitWatch.name,
            kHitWatchSeconds);
        gLast += "; no damage seen";
        gHitWatch = {};
    }
}

void Tick(double deltaSeconds, Link const& link) {
    gSettings = hand_calibration::GetThrowables();
#if defined(MEVR_GAME_LE2)
    Le2Tick(deltaSeconds, link);
#elif defined(MEVR_GAME_LE3)
    (void)deltaSeconds;
    Le3Tick(link);
#else
    (void)deltaSeconds;
    (void)link;  // ME1: game/Throwables feeds the strokes (StepPhysical) and watches the hits
#endif
}

void AdjustPad(gamepad::Options& options) {
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
#if defined(MEVR_GAME_LE1)
    bool const onFoot = gPhysicalActive;  // game/Throwables' active this tick (as ME1's rule always was)
#else
    std::string why;
    gSettings = settings;
    bool const onFoot = SequelOnFoot(why);
#endif
    gKeptB = onFoot && !settings.meleeButton;
    if (!gKeptB) {
        return;
    }
    if (!gButtonFlags && !gToldNoButtons) {
        gToldNoButtons = true;
        log::Warn("melee: meleeButton = 0 needs the redeployed host (services v14): until then B still reaches the game as its "
                  "melee; close the game and redeploy the host");
    }
    options.meleeButton = false;
}

bool BIsGameButton() {
    return !(gKeptB && gButtonFlags);
}

bool Busy() {
#if defined(MEVR_GAME_LE3)
    return gAwaiting || gObserved != nullptr;
#else
    return false;
#endif
}

#if defined(MEVR_GAME_LE3)
bool FistAway(int hand) {
    if (hand != kRight || !gStarted || !ue3::runtime::Up()) {
        return false;
    }
    vr::controllers::Hand const& right = vr::controllers::Latest().right;
    if (right.squeeze < kGripOn || right.trigger < kTriggerOn || right.primary || right.secondary || right.menu || right.stickClick) {
        return false;
    }
    ue3::Object* const pawn = player::BioPawn(player::Controller());
    return pawn != nullptr && Live(gWeapon, pawn) == nullptr;
}

std::string Punch(Link const& link) {
    if (!gStarted || !ue3::runtime::Up()) return "melee: adapter not ready";
    gLink = &link;
    gHaptics = {link.haptic, link.hapticHz};
    gSettings = hand_calibration::GetThrowables();
    std::string why;
    std::optional<Game> const game = ReadGame(why);
    if (!game) {
        gLink = nullptr;
        return "melee: refused: " + why;
    }
    thr::SwingStep stroke;
    stroke.strike = true;
    stroke.speed = 4.0;
    stroke.travel = 0.20;
    stroke.forward = 1.0;
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    vr::host_view::State const view = vr::host_view::Latest();
    ik::Vec3 const head{view.head.position.x, view.head.position.y, view.head.position.z};
    gTime = std::max(gTime, SteadySeconds());
    // A fist (the console's: no controllers), any direction: the enemy within meleeReachM.
    Strike(*game, kRight, stroke, gSettings, frame ? &*frame : nullptr, head, true, 180.0, Intent::Light);
    gLink = nullptr;
    return "melee: " + gLast;
}
#endif

std::string Request(Intent intent, int hand, Link const& link) {
#if defined(MEVR_GAME_LE3)
    if (!gStarted || !ue3::runtime::Up()) return "melee: adapter not ready";
    gLink = &link;
    gHaptics = {link.haptic, link.hapticHz};
    gSettings = hand_calibration::GetThrowables();
    std::string why;
    std::optional<Game> const game = ReadGame(why);
    if (!game) {
        gLink = nullptr;
        return "melee: refused: " + why;
    }
    thr::SwingStep stroke;
    stroke.strike = true;
    stroke.speed = 4.0;
    stroke.travel = 0.20;
    stroke.forward = 1.0;
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    vr::host_view::State const view = vr::host_view::Latest();
    ik::Vec3 const head{view.head.position.x, view.head.position.y, view.head.position.z};
    gTime = std::max(gTime, SteadySeconds());
    // Any direction (a diagnostic has no stroke): the enemy within meleeReachM.
    Strike(*game, hand, stroke, gSettings, frame ? &*frame : nullptr, head, false, 180.0, intent);
    gLink = nullptr;
    return "melee: " + gLast;
#else
    (void)intent;
    (void)hand;
    (void)link;
    return "melee: light|heavy are ME3's staged diagnostics; here: mevr melee punch|strike";
#endif
}

std::string Console(std::string_view args) {
    std::string verb(args);
    verb.erase(0, verb.find_first_not_of(' '));
    verb.erase(verb.find_last_not_of(' ') + 1);
    if (verb != "punch" && verb != "strike") {
        return std::format("mevr melee punch|strike: the right hand's strike now, as a fast stroke would make it (punch: a fist, "
                           "strike: an open hand), through the same rules; {}", Status());
    }
#if defined(MEVR_GAME_LE3)
    return "melee: on ME3 `mevr melee light|heavy` (the staged diagnostics)";
#else
    if (!gStarted) {
        return "melee: the script functions aren't found yet";
    }
    std::string why;
    std::optional<Game> const game = ReadGame(why);
    std::optional<engine::TrackingFrame> const frame = view_anchor::Current();
    vr::host_view::State const view = vr::host_view::Latest();
    if (!game || !frame || !view.valid) {
        return std::format("melee: no strike: {}", !game ? why : "no head-tracked view (needs VR on foot)");
    }
    gTime = std::max(gTime, SteadySeconds());  // no controller samples without tracked controllers: the clock moves on anyway
    thr::SwingStep stroke;
    stroke.strike = true;
    stroke.speed = 4.0;
    stroke.travel = 0.20;
    stroke.forward = 1.0;
    ik::Vec3 const head{view.head.position.x, view.head.position.y, view.head.position.z};
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
    // Any direction: the body's front is only kept while the controllers are tracked.
    Strike(*game, kRight, stroke, settings, &*frame, head, verb == "punch", 180.0, Intent::Light);
    return std::format("melee: {}", gLast);
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::Throwables const settings = hand_calibration::GetThrowables();
    return std::format("{{\"enabled\": {}, \"peakSpeed\": [{}, {}], \"last\": {}, \"cooldownLeft\": {}, \"bKept\": {}, \"status\": {}}}",
        settings.melee ? "true" : "false", json::JsonNumber(gGestures.Detector(kLeft).PeakSpeed()),
        json::JsonNumber(gGestures.Detector(kRight).PeakSpeed()), json::JsonString(gLast),
        json::JsonNumber(std::max(gReadyAt - gTime, 0.0)), gKeptB ? "true" : "false", json::JsonString(gStatus));
}

std::string Status() {
    char const* phase = "release";
    switch (gGestures.HeavyState()) {
    case ik::melee::HeavyChamber::Phase::Release: break;
    case ik::melee::HeavyChamber::Phase::Idle: phase = "idle"; break;
    case ik::melee::HeavyChamber::Phase::Holding: phase = "holding"; break;
    case ik::melee::HeavyChamber::Phase::Armed: phase = "armed"; break;
    }
    return gStatus + "; heavy chamber " + phase + "; B " + (gKeptB ? "kept from the game (squad placement's)" : "reaches the game") +
        "; last " + gLast;
}

void LogStats() {
    if (gStrikes == 0 && gMeleeRefusals == 0 && gStrokeRefusals == 0 && !gKeptB && !gPhysicalActive) {
        return;
    }
    log::Info("    melee: {}; B {}; this period: {} strikes, {} hits, {} refused at the game, {} strokes refused; peak hand speeds "
              "(head-relative) {:.1f} / {:.1f} m/s; last {}",
        gStatus, gKeptB ? "kept from the game (squad placement's)" : "reaches the game", gStrikes, gHits, gMeleeRefusals,
        gStrokeRefusals, gGestures.Detector(kLeft).PeakSpeed(), gGestures.Detector(kRight).PeakSpeed(), gLast);
    gStrikes = gHits = gMeleeRefusals = gStrokeRefusals = 0;
    gGestures.ResetPeaks();
}

void Shutdown() {
    gStarted = false;
    gGestures.Reset();
    gGestures.ResetPeaks();
    gTime = 0.0;
    gYaw = 0.0;
    gPhysicalActive = false;
    gKeptB = false;
    gButtonFlags = false;
    gToldNoButtons = gToldNoHaptics = false;
    gHaptics = {};
    gReadyAt = 0.0;
    gLast = "none";
    gStatus = "off";
    gLoggedStatus.clear();
    gHitWatch = {};
    gStrikes = gHits = gMeleeRefusals = gStrokeRefusals = 0;
#if defined(MEVR_GAME_LE2)
    gClock.Reset();
    gOwnYawActive = false;
#endif
#if defined(MEVR_GAME_LE3)
    gContext = {};
    gLink = nullptr;
    gAwaiting = false;
    gObserved = nullptr;
#endif
}

} // namespace mevr::game::melee
