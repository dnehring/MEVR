// SPDX-License-Identifier: MIT
#include "game/Holsters.hpp"

#include "core/Log.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/GameAdapter.hpp"
#include "game/ArmIk.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/Throwables.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <string>

namespace mevr::game::holsters {
namespace {

namespace slots = ik::body_slots;
using slots::Weapon;

constexpr int kRight = 1;
// Grip latch, as the pad mapping's (game/gamepad_mapping).
constexpr float kGripOn = 0.6f;
constexpr float kGripOff = 0.4f;
constexpr float kTriggerOn = 0.5f;
// The continuous contact buzz is asked for again each tick and lasts this long unasked (the host
// stops it then: a stalled mod goes quiet).
constexpr float kContactSeconds = 0.15f;
// How long the game gets to show a requested draw or holster before we say it didn't happen.
constexpr double kResultSeconds = 3.0;
// The standing eye height is logged again when it has moved this much (meters).
constexpr double kEyeHeightLogStep = 0.02;

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34): BioPawn.m_oBehavior +0x7D4, BioPawnBehavior.m_oEquipment +0x424,
// BioEquipment m_QuickSlotArray +0x94 and m_eQuickSlotCurrent +0xB8, BioPlayerController.GameModeManager +0xA70,
// SFXGameModeManager.GameModes +0x70 (research/19), found by name; engine/ReflectCheck compares each.
ue3::ClassRef gBioWeaponRanged{"SFXGame.BioWeaponRanged"};
ue3::ClassRef gGameModeDefault{"SFXGame.SFXGameModeDefault"};
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gEquipment{"SFXGame.BioPawnBehavior", "m_oEquipment"};
ue3::FieldRef gQuickSlots{"SFXGame.BioEquipment", "m_QuickSlotArray"};
ue3::FieldRef gQuickSlotCurrent{"SFXGame.BioEquipment", "m_eQuickSlotCurrent"};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes"};
ue3::FunctionRef gSwitchToWeaponFn{"SFXGame.SFXGameModeDefault.SwitchToWeapon"};
ue3::FunctionRef gTryHolsterFn{"SFXGame.SFXGameModeDefault.TryHolster"};
ue3::FunctionRef gIsInCombatActionStateFn{"SFXGame.BioPawn.IsInCombatActionState"};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons"};
ue3::FunctionRef gGetRangedWeaponTypeFn{"SFXGame.BioWeaponRanged.GetRangedWeaponType"};
// Every draw the game makes by itself with the weapon away (research/19 "the callers", research/20): TryMelee,
// UseGrenade, TryUseMappedPower, UseFirstAid, EnterPowerWheel, EnterWeaponWheel, the weapon wheel's close and the
// holster key's toggle all call it. [Holsters] onlyHolstersDraw skips it (engine/call_function's skip handler).
ue3::FunctionRef gDrawPrimaryWeaponFn{"SFXGame.SFXPawn_Player.DrawPrimaryWeapon"};

ue3::Function* gSwitchToWeapon = nullptr;
ue3::Function* gTryHolster = nullptr;
ue3::Function* gIsInCombatActionState = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;
ue3::Function* gGetRangedWeaponType = nullptr;
#endif

bool gStarted = false;
slots::Machine gMachine;
slots::Anchor gAnchor;
ik::BodyYaw gOwnYaw;
bool gOwnYawActive = false;
double gYaw = 0.0;
bool gYawValid = false;
char const* gYawSource = "none";
slots::Frame gFrame;
std::array<ik::Vec3, slots::kSlots> gCenters{};
std::array<double, slots::kSlots> gDistances{-1.0, -1.0, -1.0, -1.0};
bool gNear = false;
bool gActive = false;          // this tick's conditions held
std::string gWhy = "not started";
std::string gLoggedWhy;
bool gOwnGrip = false;         // our latch of the right grip
bool gHaveCount = false;
std::uint32_t gLastCount = 0;
bool gToldOldHost = false;
bool gToldNoHaptics = false;
bool gToldNoContact = false;
bool gToldNoGripHolds = false;
bool gWheelOff = false;
bool gEnabled = false;
// The right grip holds the gun this tick (setting, host support, on foot or in a wheel), and the
// control that is RB then.
bool gHolds = false;
gamepad::RbSource gRbSource = gamepad::RbSource::RightStickClick;
bool gLoggedHolds = false;
bool gLoggedHoldsValue = false;
// The contact buzz last asked for (0: none), and what the heartbeat reports.
float gContactAmplitude = 0.0f;
float gContactPeak = 0.0f;
bool gToldReturnWaits = false;  // this return already said it waits for the trigger
// The eye height that scales the slots: the standing height from the floor, or the setting.
slots::StandingHeight gStanding;
bool gFloorSeen = false;
std::uint32_t gFloorSessions = 0;
std::uint32_t gFloorRecenters = 0;
double gFloorNow = -1.0;       // the latest floor sample (meters above it), -1: none
double gEyeHeight = slots::kReferenceEyeHeight;
std::string gEyeSource = "the setting";
std::string gLoggedEyeSource;
double gLoggedEyeHeight = -1.0;
// Seconds since our last draw request (game/throwables: a draw's reach isn't a melee stroke).
double gSinceDraw = 1e9;
// The right hand holds a grenade (game/throwables): logged when it changes.
bool gLoggedGrenadeHand = false;

// Only holsters draw ([Holsters] onlyHolstersDraw): the game's DrawPrimaryWeapon is skipped for the player's pawn while
// this tick says so, unless one of ours asked for it (AllowGameDraw: a grenade thrown with the weapon away).
bool gDrawSkipReady = false;      // the skip handler is registered
bool gBlockDraws = false;
ue3::Object* gBlockPawn = nullptr;
int gAllowedDraws = 0;            // AllowGameDraw scopes open now
double gSinceAllowedDraw = 1e9;   // seconds since one closed
unsigned gRefusedDraws = 0;       // this heartbeat period
bool gLoggedBlock = false;
bool gLoggedBlockValue = false;
bool gWasDrawn = false;           // last tick's IsInCombatActionState, for draws nobody of ours asked for

// What the player carries, re-read each tick; the type map is rebuilt when the slots change.
struct Carried {
    std::array<std::uintptr_t, slots::kSlots> weapons{};  // compared by address only
    std::array<int, slots::kSlots> typeOfSlot{-1, -1, -1, -1};
    std::array<int, slots::kSlots> slotOfType{-1, -1, -1, -1};
    int current = -1;     // m_eQuickSlotCurrent
    bool drawn = false;   // IsInCombatActionState
    bool switching = false;
};
Carried gCarried;

// A request we made, to report what the game did with it.
struct Pending {
    slots::Action action = slots::Action::None;
    Weapon weapon = Weapon::None;
    double age = 0.0;
};
Pending gPending;

// Heartbeat counters.
unsigned gEntries = 0;
unsigned gDraws = 0;
unsigned gHolsters = 0;
unsigned gRefusals = 0;
double gNearest = 1e9;
int gNearestSlot = -1;

std::string V(ik::Vec3 const& v) {
    return std::format("({:.2f}, {:.2f}, {:.2f})", v.x, v.y, v.z);
}

void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("holsters: {}", gWhy);
    }
}

void Pulse(Link const& link, slots::Buzz const& buzz) {
    if (!(buzz.amplitude > 0.0f) || !(buzz.seconds > 0.0f)) {
        return;
    }
    if (link.hapticHz != nullptr) {
        link.hapticHz(kRight, buzz.seconds, buzz.amplitude, buzz.frequency);
        return;
    }
    if (link.haptic == nullptr) {
        if (!gToldNoHaptics) {
            gToldNoHaptics = true;
            log::Warn("holsters: the host can't vibrate the controllers (services older than v9): no haptics; close the game "
                      "and redeploy the host");
        }
        return;
    }
    link.haptic(kRight, buzz.seconds, buzz.amplitude);  // v9: no frequency
}

// The continuous contact buzz: asked for each tick while the hand is in a slot (amplitude > 0), and
// told to stop once when it leaves. A v9 host has none (the entry pulse stands in).
void Contact(Link const& link, slots::Buzz const& buzz) {
    float const amplitude = buzz.amplitude > 0.0f ? buzz.amplitude : 0.0f;
    gContactPeak = std::max(gContactPeak, amplitude);
    if (link.hapticContinuous == nullptr) {
        if (amplitude > 0.0f && !gToldNoContact && link.haptic != nullptr) {
            gToldNoContact = true;
            log::Warn("holsters: the host (services older than v10) has no continuous vibration: one pulse when the hand "
                      "enters a slot instead of the contact buzz; close the game and redeploy the host");
        }
        gContactAmplitude = 0.0f;
        return;
    }
    if (amplitude <= 0.0f && gContactAmplitude <= 0.0f) {
        return;  // already quiet
    }
    link.hapticContinuous(kRight, amplitude, buzz.frequency, amplitude > 0.0f ? kContactSeconds : 0.0f);
    gContactAmplitude = amplitude;
}

#if defined(MEVR_GAME_LE1)

// Through ProcessEvent with the function's own parameter block (never a generated wrapper).
bool CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

int RangedType(ue3::Object* weapon) {
    if (weapon == nullptr || gGetRangedWeaponType == nullptr || !ue3::IsA(weapon, gBioWeaponRanged)) {
        return -1;
    }
    ue3::Params parms(ue3::runtime::Get(), gGetRangedWeaponType);
    ue3::runtime::Call(weapon, gGetRangedWeaponType, parms);
    std::optional<std::uint8_t> const type = parms.Return<std::uint8_t>();
    return type && *type <= 3 ? static_cast<int>(*type) : -1;
}

struct Game {
    ue3::Object* controller = nullptr;  // a BioPlayerController
    ue3::Object* pawn = nullptr;        // a BioPawn
    ue3::Object* equipment = nullptr;   // its BioEquipment
    ue3::Object* mode = nullptr;        // the SFXGameModeDefault
};

// The player's pawn, equipment and default game mode, live. Nullopt (with `why`) if any is missing.
std::optional<Game> ReadGame(std::string& why) {
    if (!ue3::runtime::Ready()) {
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
    ue3::Object* const pawn = player::BioPawn(pc);
    if (pawn == nullptr) {
        why = "no player pawn (vehicle or cutscene?)";
        return std::nullopt;
    }
    game.pawn = pawn;
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    game.equipment = behavior != nullptr ? gEquipment.Read<ue3::Object*>(behavior).value_or(nullptr) : nullptr;
    if (game.equipment == nullptr) {
        why = "the player pawn has no equipment";
        return std::nullopt;
    }
    if (ue3::Object* const manager = gGameModeManager.Read<ue3::Object*>(pc).value_or(nullptr)) {
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

ue3::Object* QuickSlot(Game const& game, int i) {
    ue3::Object* const* const slot = gQuickSlots.At<ue3::Object*>(game.equipment, i);
    return slot != nullptr ? *slot : nullptr;
}

void ReadCarried(Game const& game) {
    std::array<std::uintptr_t, slots::kSlots> weapons{};
    for (int i = 0; i < slots::kSlots; ++i) {
        weapons[i] = reinterpret_cast<std::uintptr_t>(QuickSlot(game, i));
    }
    if (weapons != gCarried.weapons) {
        gCarried.weapons = weapons;
        gCarried.typeOfSlot.fill(-1);
        gCarried.slotOfType.fill(-1);
        std::string text;
        for (int i = 0; i < slots::kSlots; ++i) {
            ue3::Object* const weapon = QuickSlot(game, i);
            int const type = RangedType(weapon);
            gCarried.typeOfSlot[i] = type;
            if (type >= 0 && gCarried.slotOfType[type] < 0) {
                gCarried.slotOfType[type] = i;
            }
            text += std::format(" {}: {}{};", i, weapon != nullptr ? ue3::runtime::FullName(weapon) : std::string("empty"),
                weapon != nullptr ? std::format(" ({})", slots::Name(static_cast<Weapon>(type))) : std::string());
        }
        bool matches = true;
        for (int i = 0; i < slots::kSlots; ++i) {
            matches = matches && (gCarried.typeOfSlot[i] < 0 || gCarried.typeOfSlot[i] == i);
        }
        log::Info("holsters: the player's quick slots:{} {}", text,
            matches ? "slot = weapon type, as research/19 expects" : "NOT slot = weapon type: drawing by the weapons' own types");
    }
    gCarried.current = gQuickSlotCurrent.Read<std::uint8_t>(game.equipment).value_or(0xFF);
    gCarried.drawn = CallBool(game.pawn, gIsInCombatActionState);
    gCarried.switching = CallBool(game.pawn, gIsSwitchingWeapons);
}

Weapon Held() {
    if (!gCarried.drawn || gCarried.current < 0 || gCarried.current >= slots::kSlots) {
        return Weapon::None;
    }
    int const type = gCarried.typeOfSlot[gCarried.current];
    return type >= 0 ? static_cast<Weapon>(type) : Weapon::None;
}

bool Carries(Weapon weapon) {
    int const type = static_cast<int>(weapon);
    return type >= 0 && type < slots::kSlots && gCarried.slotOfType[type] >= 0;
}

void Draw(Game const& game, Weapon weapon, char const* slot, Link const& link, double strength, bool holds) {
    int const quickSlot = gCarried.slotOfType[static_cast<int>(weapon)];
    ue3::Params parms(ue3::runtime::Get(), gSwitchToWeapon);
    parms.Set("Slot", static_cast<std::uint8_t>(quickSlot));
    log::Info("holsters: grab at {}: drawing the {} (SFXGameModeDefault.SwitchToWeapon({}); was {}{}){}", slot, slots::Name(weapon),
        quickSlot, gCarried.drawn ? slots::Name(Held()) : "holstered", gCarried.switching ? ", the game is mid-switch" : "",
        holds ? "; keep the grip squeezed to hold it" : "");
    ue3::runtime::Call(game.mode, gSwitchToWeapon, parms);  // directly: never a generated wrapper
    gPending = {slots::Action::Draw, weapon, 0.0};
    gSinceDraw = 0.0;
    ++gDraws;
    Pulse(link, slots::DrawClick(strength));
}

void Holster(Game const& game, Weapon weapon, std::string const& why, Link const& link, double strength) {
    if (!gCarried.drawn) {
        log::Info("holsters: {}: the {} is already put away", why, slots::Name(weapon));
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), gTryHolster);
    log::Info("holsters: {}: the {} goes back in its holster (SFXGameModeDefault.TryHolster)", why, slots::Name(weapon));
    ue3::runtime::Call(game.mode, gTryHolster, parms);  // directly: never a generated wrapper
    gPending = {slots::Action::Holster, weapon, 0.0};
    ++gHolsters;
    Pulse(link, slots::HolsterClick(strength));
}

// What the game did with our last request, once.
void FollowPending(double deltaSeconds) {
    if (gPending.action == slots::Action::None) {
        return;
    }
    gPending.age += deltaSeconds;
    bool const done = gPending.action == slots::Action::Draw ? Held() == gPending.weapon : !gCarried.drawn;
    if (done) {
        log::Info("holsters: {} after {:.2f} s", gPending.action == slots::Action::Draw
            ? std::format("the {} is in hand", slots::Name(gPending.weapon))
            : std::format("the {} is holstered", slots::Name(gPending.weapon)), gPending.age);
        gPending = {};
    } else if (gPending.age > kResultSeconds) {
        log::Warn("holsters: the game didn't {} the {} within {:.0f} s (combat disabled here, the pawn busy, or mid-switch); "
                  "now {}", gPending.action == slots::Action::Draw ? "draw" : "holster", slots::Name(gPending.weapon), kResultSeconds,
            gCarried.drawn ? std::format("holding the {}", slots::Name(Held())) : std::string("holstered"));
        gPending = {};
    }
}

#endif  // MEVR_GAME_LE1

void Inactive(std::string why, Link const& link) {
    if (gMachine.InSlot() >= 0) {
        log::Info("holsters: right hand out of every slot ({})", why);
    }
    if (gMachine.ReturnLeft() >= 0.0) {
        log::Info("holsters: the let-go gun stays out ({})", why);
    }
    gMachine.Reset();
    gAnchor.Reset();
    gOwnYawActive = false;
    gYawValid = false;
    gNear = false;
    gActive = false;
    gWasDrawn = gCarried.drawn;  // a draw while inactive isn't reported as unasked when it becomes active
    gDistances.fill(-1.0);
    Contact(link, {});
    SetWhy(std::move(why));
}

// The eye height that scales the slots: the standing height from the floor (taken at a recenter,
// then a slow-rising maximum; reset for a new session), or [Holsters] eyeHeightM. Runs whenever
// holsters are on, so the estimate keeps learning through menus.
void UpdateEyeHeight(hand_calibration::Holsters const& settings, Link const& link, double deltaSeconds) {
    std::string source;
    double height = settings.layout.eyeHeight;
    gFloorNow = -1.0;
    if (auto const calibrated = body_calibration::StandingEyeHeight()) {
        // The guided calibration measured it standing tall (research/25): the player's height, whatever the floor
        // learns later. The floor is still watched (gFloorNow, a new session) for the log and the snapshot.
        if (link.floor && link.floor->valid) {
            gFloorNow = link.floor->eyeHeight;
        }
        height = *calibrated;
        source = "[Calibration] standing eye height";
    } else if (!settings.eyeHeightFromFloor) {
        source = "[Holsters] eyeHeightM (eyeHeightFromFloor = 0)";
    } else if (!link.floor) {
        source = "[Holsters] eyeHeightM (the host has no floor: services older than v10, or a size mismatch; redeploy the host)";
    } else {
        vr::floor::State const& floor = *link.floor;
        if (!gFloorSeen || floor.sessions != gFloorSessions) {
            gStanding.Reset();  // a new session (or the first look): its floor may differ
            gFloorSessions = floor.sessions;
            gFloorRecenters = floor.recenters;
            gFloorSeen = true;
        }
        if (floor.valid) {
            gFloorNow = floor.eyeHeight;
            if (floor.recenters != gFloorRecenters) {
                gFloorRecenters = floor.recenters;
                bool const plausible = floor.eyeHeight >= slots::StandingHeight::kMin && floor.eyeHeight <= slots::StandingHeight::kMax;
                gStanding.Recenter(floor.eyeHeight);
                log::Info("holsters: recentered with the head {:.2f} m above the floor: {}", floor.eyeHeight,
                    plausible ? "taken as the standing eye height" : "not a standing eye height, ignored");
            } else {
                gStanding.Update(floor.eyeHeight, deltaSeconds);
            }
        }
        if (gStanding.Valid()) {
            height = gStanding.Value();
            source = std::format("the floor ({}), standing", vr::floor::Name(floor.source));
        } else if (floor.source == vr::floor::Source::None) {
            source = "[Holsters] eyeHeightM (the runtime offers no floor space)";
        } else if (gFloorNow >= 0.0) {
            source = std::format("[Holsters] eyeHeightM (the head is {:.2f} m above the {} floor: not a plausible eye height, "
                                 "is the floor set up?)", gFloorNow, vr::floor::Name(floor.source));
        } else {
            source = std::format("[Holsters] eyeHeightM (no {} sample yet)", vr::floor::Name(floor.source));
        }
    }
    gEyeHeight = height;
    gEyeSource = source;
    // Logged when the source changes (by kind, not by the sample in the reason) or the height moves.
    std::string const kind = source.substr(0, source.find(" ("));
    if (kind != gLoggedEyeSource || std::abs(height - gLoggedEyeHeight) >= kEyeHeightLogStep) {
        gLoggedEyeSource = kind;
        gLoggedEyeHeight = height;
        log::Info("holsters: eye height {:.2f} m from {} (slot scale {:.2f})", height, source, slots::Scale([&] {
            slots::Layout layout = settings.layout;
            layout.eyeHeight = height;
            return layout;
        }()));
    }
}

char const* SlotOf(hand_calibration::Holsters const& settings, Weapon weapon) {
    for (slots::Slot const& slot : settings.layout.slots) {
        if (slot.weapon == weapon) {
            return slot.name;
        }
    }
    return "its holster";
}

#if defined(MEVR_GAME_LE1)
// engine/call_function's skip handler for SFXPawn_Player.DrawPrimaryWeapon (game thread: the script VM's).
bool OnDrawPrimaryWeapon(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* caller) {
    if (!gBlockDraws || gAllowedDraws > 0 || context == nullptr || context != gBlockPawn) {
        return false;
    }
    ++gRefusedDraws;
    log::Info("holsters: the game wanted to draw your weapon ({}): refused, only a grab at a holster draws "
              "([Holsters] onlyHolstersDraw = 1)",
        caller != nullptr ? ue3::runtime::FullName(caller) : std::string("called from native code"));
    return true;
}
#endif

} // namespace

void AllowGameDraw(bool begin) {
    if (begin) {
        ++gAllowedDraws;
    } else if (gAllowedDraws > 0 && --gAllowedDraws == 0) {
        gSinceAllowedDraw = 0.0;
    }
}

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    gSwitchToWeapon = gSwitchToWeaponFn.Get();
    gTryHolster = gTryHolsterFn.Get();
    gIsInCombatActionState = gIsInCombatActionStateFn.Get();
    gIsSwitchingWeapons = gIsSwitchingWeaponsFn.Get();
    gGetRangedWeaponType = gGetRangedWeaponTypeFn.Get();
    gStarted = gSwitchToWeapon != nullptr && gTryHolster != nullptr && gIsInCombatActionState != nullptr &&
        gIsSwitchingWeapons != nullptr && gGetRangedWeaponType != nullptr;
    log::Info("holsters: script functions {} (SwitchToWeapon {}, TryHolster {}, IsInCombatActionState {}, IsSwitchingWeapons {}, "
              "GetRangedWeaponType {})", gStarted ? "found" : "MISSING: holsters off", gSwitchToWeapon != nullptr,
        gTryHolster != nullptr, gIsInCombatActionState != nullptr, gIsSwitchingWeapons != nullptr, gGetRangedWeaponType != nullptr);
    // Only holsters draw: the game's DrawPrimaryWeapon can be skipped if it's a script function without parameters (the
    // skip consumes just its end-of-parameters token). Registered before the CallFunction hook goes in (Mod.cpp).
    gDrawSkipReady = false;
    if (ue3::Function* const draw = gDrawPrimaryWeaponFn.Get()) {
        ue3::Reflection& reflection = ue3::runtime::Get();
        bool const native = (reflection.FunctionFlags(draw) & ue3::function_flags::kNative) != 0;
        bool const noParameters = reflection.ParmsSize(draw) == 0 && reflection.Parameters(draw).empty();
        if (!native && noParameters) {
            engine::call_function::AddSkipHandler(draw, &OnDrawPrimaryWeapon);
            gDrawSkipReady = true;
        }
        log::Info("holsters: SFXPawn_Player.DrawPrimaryWeapon: {}", gDrawSkipReady
                ? "a script function without parameters: the game's own draws can be refused ([Holsters] onlyHolstersDraw)"
                : std::format("can't be skipped safely ({}{}): the game may still draw by itself", native ? "native" : "",
                      noParameters ? "" : native ? ", has parameters" : "has parameters"));
    } else {
        log::Warn("holsters: SFXPawn_Player.DrawPrimaryWeapon not found: the game may still draw by itself");
    }
    return gStarted;
#else
    return false;
#endif
}

void Tick(double deltaSeconds, Link const& link) {
    hand_calibration::Refresh();  // at most once a second
    hand_calibration::Holsters const settings = hand_calibration::GetHolsters();
    gEnabled = settings.enabled;
    gSinceDraw = std::min(gSinceDraw + std::max(deltaSeconds, 0.0), 1e9);
    gSinceAllowedDraw = std::min(gSinceAllowedDraw + std::max(deltaSeconds, 0.0), 1e9);
    gWheelOff = settings.enabled && !settings.weaponWheel;

    vr::controllers::State const controllers = vr::controllers::Latest();
    vr::controllers::Hand const& right = controllers.right;
    // Our own latch of the right grip, kept whatever else happens, so a squeeze held through a
    // pause or a menu is never taken for a new one.
    bool const wasDown = gOwnGrip;
    gOwnGrip = controllers.active && (gOwnGrip ? right.squeeze > kGripOff : right.squeeze >= kGripOn);
    // A new squeeze the game didn't see as RB: the host counts them; an older host can't, so then
    // any new squeeze (and RB reaches the game).
    bool newSqueeze = false;
    if (link.rightGrip) {
        std::uint32_t const count = *link.rightGrip >> 1;
        newSqueeze = gHaveCount && count != gLastCount;
        gLastCount = count;
        gHaveCount = true;
    } else {
        newSqueeze = gOwnGrip && !wasDown;
        if (!gToldOldHost && settings.enabled) {
            gToldOldHost = true;
            log::Warn("holsters: the host (services older than v9) can't keep a holster's squeeze from the game: grabs still "
                      "work, but the game sees RB too; close the game and redeploy the host");
        }
    }

    // The grip holds the gun: wanted, and the pad mapping can do it. It holds on foot and in the two
    // wheels the game opens from there (the game is paused in them, and a held grip must not turn into
    // RB, nor an R3 press that opened the power wheel stop being RB); in the Mako and menus the grip is
    // RB as always (the Mako's cannon).
    gRbSource = settings.powerWheel;
    bool holdsWanted = settings.enabled && settings.gripHolds;
    if (holdsWanted && !link.gripHoldsSupported) {
        holdsWanted = false;
        if (!gToldNoGripHolds) {
            gToldNoGripHolds = true;
            log::Warn("holsters: gripHolds = 1 needs the redeployed host (services v10): until then the right grip stays RB "
                      "and holsters use the old squeeze-to-draw mapping; close the game and redeploy the host");
        }
    }
#if defined(MEVR_GAME_LE1)
    auto const mode = gStarted ? head_tracking::GameMode() : std::nullopt;
    gHolds = holdsWanted && mode && (*mode == 0 || *mode == 2 || *mode == 3);
#else
    gHolds = false;
#endif
    if (!gLoggedHolds || gHolds != gLoggedHoldsValue) {
        gLoggedHolds = true;
        gLoggedHoldsValue = gHolds;
        log::Info("holsters: {}", gHolds ? std::format("the right grip holds the gun (no RB from it); RB is {}", gamepad::Name(gRbSource))
                                         : std::string("the right grip is RB (gripHolds off, or not on foot)"));
    }

#if defined(MEVR_GAME_LE1)
    // Only holsters draw: on foot (and in its two wheels) in VR, the game's own DrawPrimaryWeapon is refused for the
    // player's pawn (OnDrawPrimaryWeapon).
    gBlockPawn = nullptr;
    if (gDrawSkipReady && settings.enabled && settings.onlyHolstersDraw && gHolds && vr::host_view::Latest().valid) {
        ue3::Object* const pc = player::Controller();
        gBlockPawn = pc != nullptr && player::IsBioPlayerController(pc) ? player::BioPawn(pc) : nullptr;
    }
    gBlockDraws = gBlockPawn != nullptr;
    if (!gLoggedBlock || gBlockDraws != gLoggedBlockValue) {
        gLoggedBlock = true;
        gLoggedBlockValue = gBlockDraws;
        log::Info("holsters: {}", gBlockDraws ? "only a grab at a holster draws a weapon (the game's own draws are refused)"
                                              : "the game may draw the weapon by itself (onlyHolstersDraw off, not on foot, or no VR view)");
    }
#endif

    if (!settings.enabled) {
        Inactive("off ([Holsters] enabled = 0)", link);
        return;
    }
    UpdateEyeHeight(settings, link, deltaSeconds);
#if defined(MEVR_GAME_LE1)
    if (!gStarted) {
        Inactive("waiting for the script functions", link);
        return;
    }
    if (!mode || *mode != 0) {
        Inactive(std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown")), link);
        return;
    }
    vr::host_view::State const view = vr::host_view::Latest();
    if (!view.valid) {
        Inactive("no head-tracked world view (menu, flat screen or VR off)", link);
        return;
    }
    if (!controllers.active || !right.gripValid) {
        Inactive("the right controller isn't tracked", link);
        return;
    }
    std::string why;
    auto const game = ReadGame(why);
    if (!game) {
        Inactive(why, link);
        return;
    }
    ReadCarried(*game);
    // A draw nobody of ours asked for (not a holster grab, not a grenade's): what the hands were doing, so the log says
    // what drew it if DrawPrimaryWeapon wasn't the way.
    if (gCarried.drawn && !gWasDrawn && gSinceDraw > kResultSeconds && gSinceAllowedDraw > kResultSeconds) {
        log::Info("holsters: the {} came out without a holster grab (right trigger {:.2f}, grip {:.2f}; left trigger {:.2f}, "
                  "grip {:.2f}){}", slots::Name(Held()), right.trigger, right.squeeze, controllers.left.trigger,
            controllers.left.squeeze, gBlockDraws ? ": not through DrawPrimaryWeapon, so it wasn't refused" : "");
    }
    gWasDrawn = gCarried.drawn;
    gActive = true;
    SetWhy("active");

    // The body frame. The yaw: arm IK's torso, else our own lagging yaw; held while busy.
    XrPosef const& head = view.head;
    ik::Quat const headRotation{head.orientation.x, head.orientation.y, head.orientation.z, head.orientation.w};
    ik::Vec3 const headPosition{head.position.x, head.position.y, head.position.z};
    double const headYaw = ik::YawOf(headRotation.x, headRotation.y, headRotation.z, headRotation.w);
    if (!gOwnYawActive) {
        gOwnYaw.Reset(headYaw);
        gOwnYawActive = true;
    }
    double const ownYaw = gOwnYaw.Update(headYaw, deltaSeconds);
    std::optional<double> const torso = arm_ik::TorsoYaw();
    if (!gYawValid || !gMachine.Busy()) {
        gYaw = torso ? *torso : ownYaw;
        gYawSource = torso ? "arm IK's torso" : "the head (lagging)";
        gYawValid = true;
    }
    gFrame.yaw = gYaw;
    gFrame.anchor = gAnchor.Update(slots::NeutralEye(headPosition, headRotation), deltaSeconds);

    slots::Layout layout = settings.layout;
    layout.eyeHeight = gEyeHeight;
    double const scale = slots::Scale(layout);
    ik::Vec3 const hand{right.grip.position.x, right.grip.position.y, right.grip.position.z};
    slots::Input input;
    input.exitMargin = settings.layout.exitMargin;
    for (int i = 0; i < slots::kSlots; ++i) {
        slots::Slot const& slot = settings.layout.slots[i];
        gCenters[i] = slots::SlotCenter(gFrame, slot, scale);
        input.weapon[i] = slot.weapon;
        input.radius[i] = slot.radius;
        input.available[i] = Carries(slot.weapon);
        input.distance[i] = slot.weapon != Weapon::None ? ik::Length(hand - gCenters[i]) : -1.0;
        gDistances[i] = input.distance[i];
        if (input.distance[i] >= 0.0 && input.distance[i] - slot.radius < gNearest) {
            gNearest = input.distance[i] - slot.radius;
            gNearestSlot = i;
        }
    }
    input.tracked = true;
    // The right hand holding a grenade (or having just thrown one) still counts as holding the gun, so
    // it doesn't go back to its holster under the throw (research/20). Its claiming squeeze reaches the
    // machine as a squeeze away from every slot: it keeps a gun that's out (a return is cancelled).
    bool const grenadeHand = throwables::RightHandBusy();
    if (grenadeHand != gLoggedGrenadeHand) {
        gLoggedGrenadeHand = grenadeHand;
        log::Info("holsters: {}", grenadeHand ? "the right hand has a grenade: the gun stays out as if held"
                                              : "the right hand is back to holding the gun with the grip");
    }
    input.gripDown = gOwnGrip || grenadeHand;
    input.newSqueeze = newSqueeze;
    input.trigger = right.trigger >= kTriggerOn;
    input.held = Held();
    input.holster = settings.holster;
    input.gripHolds = gHolds;
    input.releaseGrace = settings.releaseGrace;
    input.deltaSeconds = deltaSeconds;
    input.gripKept = gOwnGrip && link.rightGrip && (*link.rightGrip & 1u) != 0;
    slots::Step const step = gMachine.Update(input);
    gNear = step.nearby;
    double const strength = settings.hapticAmplitude;

    auto const name = [&settings](int slot) { return slot >= 0 ? settings.layout.slots[slot].name : "-"; };
    if (step.left >= 0) {
        log::Info("holsters: right hand left {} ({:.2f} m from its center)", name(step.left), input.distance[step.left]);
    }
    if (step.entered >= 0) {
        ++gEntries;
        log::Info("holsters: right hand entered {} ({}, {:.2f} m from its center, radius {:.2f}){}", name(step.entered),
            slots::Name(settings.layout.slots[step.entered].weapon), input.distance[step.entered],
            settings.layout.slots[step.entered].radius, Carries(settings.layout.slots[step.entered].weapon) ? "" : ": you don't carry one");
        Pulse(link, slots::EnterEdge(strength));  // the edge; the contact buzz follows below
    }
    switch (step.action) {
    case slots::Action::Draw:
        Draw(*game, step.weapon, name(step.slot), link, strength, gHolds);
        break;
    case slots::Action::Holster:
        Holster(*game, step.weapon, gHolds ? std::format("let go at {}", name(step.slot)) : std::format("released at {}", name(step.slot)),
            link, strength);
        break;
    case slots::Action::Return:
        gToldReturnWaits = false;
        Holster(*game, step.weapon, std::format("let go {:.2f} s ago away from {}", settings.releaseGrace, SlotOf(settings, step.weapon)),
            link, strength);
        break;
    case slots::Action::Unavailable:
        ++gRefusals;
        log::Info("holsters: grab at {}: you carry no {} (its quick slot is empty)", name(step.slot), slots::Name(step.weapon));
        Pulse(link, slots::Refused(strength));
        break;
    case slots::Action::AlreadyHeld:
        log::Info("holsters: grab at {}: the {} is already in hand{}", name(step.slot), slots::Name(step.weapon),
            gHolds ? ": holding it" : " (holster = 0: it stays)");
        break;
    case slots::Action::HolsterArmed:
        log::Info("holsters: squeezed at {} with the {} in hand: release here to holster it", name(step.slot), slots::Name(step.weapon));
        break;
    case slots::Action::HolsterCancelled:
        log::Info("holsters: holster at {} called off ({})", name(step.slot),
            input.trigger ? "the trigger" : gMachine.InSlot() != step.slot ? "the hand left the slot" : "the gun changed");
        break;
    case slots::Action::Missed:
        log::Info("holsters: squeeze near a slot but in none (nearest {} at {:.2f} m): nothing drawn, no RB", name(gNearestSlot),
            gNearestSlot >= 0 ? input.distance[gNearestSlot] : -1.0);
        break;
    case slots::Action::ReturnArmed:
        gToldReturnWaits = false;
        log::Info("holsters: let go of the {} {}: it goes back in {} unless you squeeze again{}", slots::Name(step.weapon),
            step.slot >= 0 ? std::format("at {}", name(step.slot)) : std::string("away from its slot"),
            settings.releaseGrace >= 0.0 ? std::format("{:.2f} s", settings.releaseGrace) : std::string("once it's out"),
            input.held == Weapon::None ? " (once the draw has finished)" : input.trigger ? " (once the trigger is let go)" : "");
        break;
    case slots::Action::ReturnCancelled:
        log::Info("holsters: the {} stays out ({})", slots::Name(step.weapon),
            input.newSqueeze ? "squeezed again" : input.held == Weapon::None ? "it's already away, or never came out"
                                                                             : "the gun changed");
        break;
    case slots::Action::None:
        break;
    }
    if (step.returnWaitsForTrigger && !gToldReturnWaits) {
        gToldReturnWaits = true;
        log::Info("holsters: the {} waits for the trigger to be let go before it goes back (never mid-burst)", slots::Name(Held()));
    }
    // The contact buzz, while the hand is in a slot: deeper, stronger.
    if (int const in = gMachine.InSlot(); in >= 0) {
        bool const holding = gHolds && gMachine.CurrentPhase() == slots::Phase::Grab && input.held != Weapon::None;
        Contact(link, slots::Contact(slots::Depth(input.distance[in], input.radius[in]), strength, holding));
    } else {
        Contact(link, {});
    }
    FollowPending(deltaSeconds);
#else
    (void)deltaSeconds;
    (void)newSqueeze;
    Inactive("LE1 only", link);
#endif
}

void AdjustPad(gamepad::Options& options) {
    if (!gEnabled) {
        return;
    }
    options.rightGripHolds = gHolds;
    options.rbSource = gRbSource;
    options.rightGripFree = !gHolds && gActive && gNear;
    if (gWheelOff) {
        options.leftTriggerShoulder = false;
        options.gripShoulder = false;
    }
}

double EyeHeight() {
    return gEyeHeight;
}

bool RightHandInSlot() {
    return gActive && gMachine.InSlot() >= 0;
}

double SecondsSinceDraw() {
    return gSinceDraw;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::Holsters const settings = hand_calibration::GetHolsters();
    std::string list;
    for (int i = 0; i < slots::kSlots; ++i) {
        slots::Slot const& slot = settings.layout.slots[i];
        int const type = static_cast<int>(slot.weapon);
        bool const carried = type >= 0 && type < slots::kSlots && gCarried.slotOfType[type] >= 0;
        list += std::format("{}{{\"name\": {}, \"weapon\": {}, \"center\": [{}, {}, {}], \"radius\": {}, \"distance\": {}, "
                            "\"carried\": {}}}", i > 0 ? ", " : "", json::JsonString(slot.name), json::JsonString(slots::Name(slot.weapon)),
            json::JsonNumber(gCenters[i].x), json::JsonNumber(gCenters[i].y), json::JsonNumber(gCenters[i].z),
            json::JsonNumber(slot.radius), json::JsonNumber(gDistances[i]), carried ? "true" : "false");
    }
    char const* phase = "idle";
    switch (gMachine.CurrentPhase()) {
    case slots::Phase::Idle: phase = "idle"; break;
    case slots::Phase::Grab: phase = "grab"; break;
    case slots::Phase::HolsterArmed: phase = "holster armed"; break;
    case slots::Phase::Returning: phase = "returning"; break;
    }
    return std::format("{{\"enabled\": {}, \"active\": {}, \"why\": {}, \"gripHolds\": {}, \"rbSource\": {}, \"inSlot\": {}, "
                       "\"phase\": {}, \"returnLeft\": {}, \"near\": {}, \"contact\": {}, \"yawSource\": {}, \"yaw\": {}, "
                       "\"anchor\": [{}, {}, {}], \"eyeHeight\": {}, \"eyeHeightSource\": {}, \"floorEyeHeight\": {}, "
                       "\"held\": {}, \"drawn\": {}, \"slots\": [{}]}}",
        gEnabled ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy), gHolds ? "true" : "false",
        json::JsonString(gamepad::Name(gRbSource)),
        gMachine.InSlot() >= 0 ? json::JsonString(settings.layout.slots[gMachine.InSlot()].name) : std::string("null"),
        json::JsonString(phase), json::JsonNumber(gMachine.ReturnLeft()), gNear ? "true" : "false",
        json::JsonNumber(gContactAmplitude), json::JsonString(gYawSource), json::JsonNumber(gFrame.yaw),
        json::JsonNumber(gFrame.anchor.x), json::JsonNumber(gFrame.anchor.y), json::JsonNumber(gFrame.anchor.z),
        json::JsonNumber(gEyeHeight), json::JsonString(gEyeSource), json::JsonNumber(gFloorNow),
#if defined(MEVR_GAME_LE1)
        json::JsonString(slots::Name(Held())),
#else
        json::JsonString("none"),
#endif
        gCarried.drawn ? "true" : "false", list);
}

void LogStats() {
    if (!gEnabled && gEntries == 0) {
        return;
    }
    hand_calibration::Holsters const settings = hand_calibration::GetHolsters();
    std::string where;
    if (gActive) {
        for (int i = 0; i < slots::kSlots; ++i) {
            where += std::format(" {} {} hand {:.2f} m;", settings.layout.slots[i].name, V(gCenters[i] - gFrame.anchor), gDistances[i]);
        }
    }
    log::Info("    holsters: {}; {}; yaw from {}; in {}; this period: {} entries, {} draws, {} holsters, {} refusals, {} game draws refused, "
              "nearest {}, strongest contact buzz {:.2f}; eye height {:.2f} m from {}{}{}",
        gWhy, gHolds ? std::format("the grip holds the gun (RB: {})", gamepad::Name(gRbSource)) : std::string("the grip is RB"),
        gYawSource, gMachine.InSlot() >= 0 ? settings.layout.slots[gMachine.InSlot()].name : "no slot", gEntries, gDraws,
        gHolsters, gRefusals, gRefusedDraws,
        gNearestSlot >= 0 ? std::format("{:.2f} m outside {}", gNearest, settings.layout.slots[gNearestSlot].name) : std::string("-"),
        gContactPeak, gEyeHeight, gEyeSource, gFloorNow >= 0.0 ? std::format(" (head now {:.2f} m above the floor)", gFloorNow) : std::string(),
        where.empty() ? std::string() : std::format("; slots from the anchor:{}", where));
    gEntries = gDraws = gHolsters = gRefusals = gRefusedDraws = 0;
    gNearest = 1e9;
    gNearestSlot = -1;
    gContactPeak = 0.0f;
}

void Shutdown() {
    gMachine.Reset();
    gAnchor.Reset();
    gOwnYawActive = false;
    gYawValid = false;
    gNear = false;
    gActive = false;
    gEnabled = false;
    gWheelOff = false;
    gOwnGrip = false;
    gHaveCount = false;
    gToldOldHost = false;
    gToldNoHaptics = false;
    gToldNoContact = false;
    gToldNoGripHolds = false;
    gHolds = false;
    gLoggedHolds = false;
    gContactAmplitude = 0.0f;
    gContactPeak = 0.0f;
    gToldReturnWaits = false;
    gStanding.Reset();
    gFloorSeen = false;
    gFloorNow = -1.0;
    gEyeHeight = slots::kReferenceEyeHeight;
    gEyeSource = "the setting";
    gLoggedEyeSource.clear();
    gLoggedEyeHeight = -1.0;
    gCarried = {};
    gPending = {};
    gSinceDraw = 1e9;
    gLoggedGrenadeHand = false;
    gDrawSkipReady = false;  // engine/call_function's handlers are cleared with the rest at unload
    gBlockDraws = false;
    gBlockPawn = nullptr;
    gAllowedDraws = 0;
    gSinceAllowedDraw = 1e9;
    gRefusedDraws = 0;
    gLoggedBlock = false;
    gWasDrawn = false;
    gStarted = false;
    gWhy = "not started";
    gLoggedWhy.clear();
#if defined(MEVR_GAME_LE1)
    gSwitchToWeapon = gTryHolster = gIsInCombatActionState = gIsSwitchingWeapons = gGetRangedWeaponType = nullptr;
#endif
}

} // namespace mevr::game::holsters
