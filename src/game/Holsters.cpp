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
#include "game/Le2Squad.hpp"
#include "game/Player.hpp"
#include "game/Throwables.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Runtime.hpp"
#if defined(MEVR_GAME_LE2)
#include "core/Ini.hpp"
#include "engine/SettingsFile.hpp"
#endif

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
// ...except from these callers (the lead's decision, 2026-10-26): medi-gel and powers are deliberate actions that need
// combat mode, so their draws go through. Compared with the skip handler's caller (the calling script function).
ue3::FunctionRef gUseFirstAidFn{"SFXGame.SFXGameModeDefault.UseFirstAid"};
ue3::FunctionRef gTryUseMappedPowerFn{"SFXGame.SFXGameModeDefault.TryUseMappedPower"};
ue3::FunctionRef gEnterPowerWheelFn{"SFXGame.SFXGameModeDefault.EnterPowerWheel"};
std::array<ue3::Function*, 3> gAllowedCallers{};
ue3::FunctionRef gEnterWeaponWheelFn{"SFXGame.SFXGameModeDefault.EnterWeaponWheel"};
ue3::FunctionRef gWeaponWheelDeactivatedFn{"SFXGame.SFXGameModeWeaponWheel.Deactivated"};
std::array<ue3::Function*, 2> gSeatedWheelCallers{};
// The trigger's draw (research/19 "The trigger's draw", 2026-11-04): RT is the exec SFXGameModeBase.FireWeapon, which
// draws when the weapon is away. Refused like the rest but without the buzz: a pull with an empty hand is also a fist or
// a thumbs-up now (research/43 "Touch sensors"), not an attempt to draw.
ue3::FunctionRef gFireWeaponFn{"SFXGame.SFXGameModeBase.FireWeapon"};
ue3::Function* gFireWeapon = nullptr;
// Whether the game allows combat here (CombatEnabled): what SwitchToWeapon asks first, a grab's draw asks it before the call
// (the owner, 2026-11-17, question 15: a refused draw is felt as a refusal on every game).
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput"};
ue3::FunctionRef gIsCombatEnabledFn{"SFXGame.BioPlayerInput.IsCombatEnabled"};

ue3::Function* gSwitchToWeapon = nullptr;
ue3::Function* gTryHolster = nullptr;
ue3::Function* gIsInCombatActionState = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;
ue3::Function* gGetRangedWeaponType = nullptr;
// The game's power wheel by holding the right stick click (gamepad::PowerHoldFromStickClick, research/19 "Hold only"):
// EnterPowerWheel after the hold, ExitPowerWheel (what letting go of RB does in the wheel) at the release.
ue3::ClassRef gGameModePowerWheel{"SFXGame.SFXGameModePowerWheel"};
ue3::FieldRef gCurrentMode{"SFXGame.SFXGameModeManager", "CurrentMode"};
ue3::FunctionRef gExitPowerWheelFn{"SFXGame.SFXGameModePowerWheel.ExitPowerWheel"};
ue3::Function* gEnterPowerWheel = nullptr;
ue3::Function* gExitPowerWheel = nullptr;
constexpr char const* kEnterPowerWheelName = "SFXGameModeDefault.EnterPowerWheel";
#else
// The sequels' adapter (research/le2-vrik.md "Holsters and the reload on the sequels"): the same module, each game's
// names. What the player carries is the pawn's inventory chain by weapon family (game/Le2Squad: no quick slots); the
// draw is BioPlayerController.SwitchWeapon(oWpn) (the weapon wheel's pick: SFXInventoryManager.SetWeaponIfAvailable) and,
// on ME2, the pawn's action state made combat (BioPawnBehavior.SetActionState(1): what SFXGameModeBase.FireWeapon and
// NextWeapon do first); the holster is SFXGameModeDefault.TryHolster (ME2: SetActionState(0), refused in combat, mid-reload,
// while a power or an animated transition runs); drawn is BioPawn.ActionState == 1 (ActionState_Combat); mid-switch is the
// weapon's state WeaponEquipping / WeaponPuttingDown (Object.GetStateName), which ME2's NextWeapon tests too. ME3 has no
// action state (BioPawn.ActionState, m_oBehavior and SetActionState aren't in its script): its gun is out whenever one is
// equipped, so there the draw is the switch alone. ME3's own script (read 2026-11-16, lane WP): SFXGameModeDefault.TryHolster
// only breaks the player out of cover (it must never be called for a holster there), and a weapon is put down only inside a
// switch; but the put-down state handles a switch to no weapon (InvManager.PendingWeapon None), and with Pawn.Weapon None
// BioPawn.HandleWeaponFiring returns at once (the trigger fires nothing). So ME3's holster is the game's own put-down into
// no weapon, SFXInventoryManager.SetCurrentWeapon(None), behind [Holsters] le3PutAway (1 by default since 2026-11-18, checked
// on the combat fixture: research/le3-combat-fixtures.md; with 0 a let-go gun stays in the hand and nothing is called).
ue3::ClassRef gGameModeDefault{"SFXGame.SFXGameModeDefault", ue3::kEveryGame};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager, ue3::kEveryGame};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes", ue3::kEveryGame};
ue3::FunctionRef gSwitchWeaponFn{"SFXGame.BioPlayerController.SwitchWeapon", ue3::kEveryGame};
ue3::FunctionRef gTryHolsterFn{"SFXGame.SFXGameModeDefault.TryHolster", ue3::kEveryGame};
ue3::FunctionRef gGetStateNameFn{"Core.Object.GetStateName", ue3::kEveryGame};
#if defined(MEVR_GAME_LE2)
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior", ue3::kEveryGame};
ue3::FieldRef gActionState{"SFXGame.BioPawn", "ActionState", ue3::kEveryGame};
ue3::FunctionRef gSetActionStateFn{"SFXGame.BioPawnBehavior.SetActionState", ue3::kEveryGame};
ue3::Function* gSetActionState = nullptr;
constexpr std::uint8_t kActionStateCombat = 1;  // EActionState.ActionState_Combat
#elif defined(MEVR_GAME_LE3)
// ME3's holster (above): the inventory manager's own SetCurrentWeapon with no weapon, which puts the held one down through
// its WeaponPuttingDown state (the holster animation, CustomAction 11) and leaves Pawn.Weapon None.
ue3::FunctionRef gSetCurrentWeaponFn{"SFXGame.SFXInventoryManager.SetCurrentWeapon", ue3::kEveryGame};
ue3::Function* gSetCurrentWeapon = nullptr;
// ME3's heavy weapon (the owner, 2026-11-17, docs/trilogy-plan.md question 13): it has no slot, and ME3's
// BioPlayerController.SwitchWeapon puts it down with SFXHeavyWeapon.PutDownWeapon, which in single player throws it away
// (bTossOnWeaponSwitch). While one is in hand a grab at a slot is refused (body_slots::Input::switchLosesHeld); only the
// game's weapon wheel switches away. ME2's SwitchWeapon keeps its heavy weapon in the inventory: no such rule there.
ue3::ClassRef gHeavyWeapon{"SFXGame.SFXHeavyWeapon", ue3::kEveryGame};
bool gPutAway = false;          // this tick's [Holsters] le3PutAway
bool gToldNoPutAway = false;    // said once: a let-go gun stays in the hand
#endif
ue3::Function* gSwitchWeapon = nullptr;
ue3::Function* gTryHolster = nullptr;
ue3::Function* gGetStateName = nullptr;
// Only holsters draw, the sequels' hook (the owner, 2026-11-16: "trigger draw a holstered weapon? No, none of the games
// should do that"). ME2 has no DrawPrimaryWeapon: each button's exec function draws by itself with the weapon away
// (`m_oBehavior.SetActionState(1)`): SFXGameModeBase.FireWeapon (the trigger), SFXGameModeDefault.TightAim (the aim trigger),
// TryReload, NextWeapon and PrevWeapon. SetActionState takes a parameter, so ME1's skip doesn't fit it; but each of the five
// begins `if (BioPlayerInput(Outer.PlayerInput).IsCombatEnabled() == FALSE) return;`, and IsCombatEnabled has no parameters:
// answered false for that one call (engine/call_function's answer handler, by the calling function) while the weapon is
// away, the button does nothing, as ME1's refused draw. Powers and the game's own combat start are other routes: let
// through, as ME1 lets medi-gel and powers draw. ME3's five begin with the same test (its script, 2026-11-16): there the
// weapon is away when the pawn carries weapons but has none equipped (le3PutAway's put-down), and the same answer refuses
// the button. ME3's trigger would fire nothing anyway with no weapon (HandleWeaponFiring); the answer makes it one rule.
ue3::FunctionRef gIsCombatEnabledFn{"SFXGame.BioPlayerInput.IsCombatEnabled", ue3::kEveryGame};
ue3::FunctionRef gFireWeaponFn{"SFXGame.SFXGameModeBase.FireWeapon", ue3::kEveryGame};
ue3::FunctionRef gTightAimFn{"SFXGame.SFXGameModeDefault.TightAim", ue3::kEveryGame};
ue3::FunctionRef gTryReloadFn{"SFXGame.SFXGameModeDefault.TryReload", ue3::kEveryGame};
ue3::FunctionRef gNextWeaponFn{"SFXGame.SFXGameModeDefault.NextWeapon", ue3::kEveryGame};
ue3::FunctionRef gPrevWeaponFn{"SFXGame.SFXGameModeDefault.PrevWeapon", ue3::kEveryGame};
constexpr std::size_t kTriggerRoutes = 2;  // the first two of gDrawRoutes are the triggers: refused without a buzz
std::array<ue3::Function*, 5> gDrawRoutes{};
// The game's power wheel by holding the right stick click, ME1's rule (research/19 "Hold only"). ME2's and ME3's RB is ME1's
// binding (Console_PowerWheelAndQuickPower2: OnTap 0.3 TryUsePower 1 | OnHold 0.3 EnterPowerWheel; ME2's BIOInput.ini, ME3's
// Coalesced.bin), so a plain RB from the click quick-cast the second mapped power on a tap until 2026-11-17. The open:
// ME2's SFXGameModeDefault.EnterPowerWheel (GameModeManager2.EnableMode(1)), ME3's SFXGameModeBase.EnterPowerWheel
// (EnableMode(3); the default mode inherits it); both refuse where IsCombatEnabled is false, and ME3's tries again by a
// 0.2 s timer during an animated transition. The close: SFXGameModePowerWheel.ExitPowerWheel (letting go of RB in the wheel).
#if defined(MEVR_GAME_LE3)
ue3::FunctionRef gEnterPowerWheelFn{"SFXGame.SFXGameModeBase.EnterPowerWheel", ue3::kEveryGame};
constexpr char const* kEnterPowerWheelName = "SFXGameModeBase.EnterPowerWheel";
#else
ue3::FunctionRef gEnterPowerWheelFn{"SFXGame.SFXGameModeDefault.EnterPowerWheel", ue3::kEveryGame};
constexpr char const* kEnterPowerWheelName = "SFXGameModeDefault.EnterPowerWheel";
#endif
ue3::ClassRef gGameModePowerWheel{"SFXGame.SFXGameModePowerWheel", ue3::kEveryGame};
ue3::FieldRef gCurrentMode{"SFXGame.SFXGameModeManager", "CurrentMode", ue3::kEveryGame};
ue3::FunctionRef gExitPowerWheelFn{"SFXGame.SFXGameModePowerWheel.ExitPowerWheel", ue3::kEveryGame};
ue3::Function* gEnterPowerWheel = nullptr;
ue3::Function* gExitPowerWheel = nullptr;
#if defined(MEVR_GAME_LE2)
// ME2's TryHolster refuses, silently, mid-reload (IsReloading(TRUE)), in combat (InCombat: the AI's enemies, or the game's
// bInCombat / bForceCombat), while a power runs and in an animated transition (its script, read 2026-11-17); its action state
// turns explore only when the holster animation ends (0.9-1.1 s: research/le2-vrik.md), so the same four questions are
// asked first and a refused holster is felt as one (ME1's refusal), not as the holster's click.
ue3::FunctionRef gIsReloadingFn{"SFXGame.BioPawn.IsReloading", ue3::kEveryGame};
ue3::FunctionRef gInCombatFn{"SFXGame.BioPawn.InCombat", ue3::kEveryGame};
ue3::FunctionRef gIsUsingPowerFn{"SFXGame.BioPawn.IsUsingPower", ue3::kEveryGame};
ue3::FunctionRef gIsInAnimatedTransitionFn{"SFXGame.BioPawn.IsInAnimatedTransition", ue3::kEveryGame};
bool gToldHolsterState = false;
// A gun let go in a fight (research/le2-vrik.md "A gun let go in a fight", the owner 2026-11-18): TryHolster's own last step
// in combat, a let-go that waits while the game is busy, and a draw nobody asked for put back (only holsters draw).
// The wheels' order the game's ApplyTacticalOrders draws for (a power from the game's wheels): BioPlayerController.m_currentOrder.
ue3::FieldRef gCurrentOrder{"SFXGame.BioPlayerController", "m_currentOrder", ue3::kEveryGame};
ue3::FieldRef gOrderPower{gCurrentOrder, "nmPower"};
ue3::FieldRef gOrderWeapon{gCurrentOrder, "oSwitchWeapon"};  // a weapon picked on the game's weapon wheel (the seated fallback)
bool gCombatHolster = true;           // [Holsters] combatHolster
bool gToldCombatHolster = false;
std::uint64_t gCombatHolsterReadMs = 0;
constexpr double kDeferMaxS = 4.0;     // a let-go waits this long at most for a reload, a power or a transition to end
constexpr double kUndoWindowS = 10.0;  // a draw put back more than twice in this long: the game wants it, left alone
struct DeferredHolster {
    bool active = false;
    Weapon weapon = Weapon::None;
    std::string why, busy;
    double age = 0.0;
};
DeferredHolster gDeferred;
std::array<double, 2> gUndoAges{1e9, 1e9};  // seconds since the last two put-backs (newest first)
double gUndoPausedS = 0.0;                   // > 0: put-backs left out this long yet
unsigned gUndone = 0, gUndoLeft = 0, gUndoDeliberate = 0, gCombatHolsters = 0, gDeferredDone = 0;  // heartbeat counters
#endif
#endif
gamepad::PowerHold gPowerHold;
gamepad::PowerWheelWatch gPowerWatch;  // the wheel asked for on this hold, and a late opening after its release
bool gPowerHoldOn = false;   // this tick: the mod opens the wheel from the click's hold (kFlagModPowerHold)
bool gPowerOpened = false;   // we opened the wheel and haven't closed it yet
unsigned gPowerOpens = 0;    // heartbeat counters
unsigned gPowerRefused = 0;

bool gStarted = false;
slots::Machine gMachine;
slots::Anchor gAnchor;
ik::BodyYaw gOwnYaw;
bool gOwnYawActive = false;
double gYaw = 0.0;
bool gYawValid = false;
char const* gYawSource = "none";
slots::Frame gFrame;
double gScaleNow = 1.0;  // this tick's slot scale (game/reload's pouch uses the same)
std::array<ik::Vec3, slots::kSlots> gCenters{};
std::array<double, slots::kSlots> gDistances = [] {
    std::array<double, slots::kSlots> d{};
    d.fill(-1.0);
    return d;
}();
// The slots this game uses: ME1's four quick-slot types, the sequels' five weapon families (hand_calibration).
constexpr int kUsed = hand_calibration::kGameHolsterSlots;
bool gNear = false;
// A right hand lost behind the back at the SMG's lowerBack slot (lane SG): its last place on the body, kept
// [Holsters] lostHandHoldS (never on ME1: no SMG slot), and the heartbeat's counts.
slots::LostHand gLostHand;
unsigned gLostHolds = 0;       // holds begun this period
unsigned gLostDraws = 0;       // draws made at a kept place this period
double gLostLongest = 0.0;     // the longest hold this period (seconds)
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
bool gToldReturnWaitsSwitch = false;  // ...or for the weapon switch
bool gReturnAtSlot = false;     // the return in progress was let go at the gun's own slot (no grace)
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
bool gWasDrawn = false;           // last active tick's IsInCombatActionState, for draws nobody of ours asked for
bool gWasDrawnKnown = false;      // ...read on the tick before this one (only consecutive active ticks are compared)
bool gRefusedBuzz = false;        // a draw was refused since the last tick: the right hand gets a short buzz

// What the player carries, re-read each tick; the type map is rebuilt when the slots change.
struct Carried {
    std::array<std::uintptr_t, slots::kSlots> weapons{};  // compared by address only
    std::array<int, slots::kSlots> typeOfSlot{-1, -1, -1, -1, -1};
    std::array<int, slots::kSlots> slotOfType{-1, -1, -1, -1, -1};
    int current = -1;     // m_eQuickSlotCurrent
    bool drawn = false;   // IsInCombatActionState
    bool switching = false;
    bool switchLosesHeld = false;  // ME3: a heavy weapon is in hand (body_slots::Input::switchLosesHeld); false elsewhere
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

#endif  // MEVR_GAME_LE1

struct Game {
    ue3::Object* controller = nullptr;  // a BioPlayerController
    ue3::Object* pawn = nullptr;        // a BioPawn
    ue3::Object* equipment = nullptr;   // its BioEquipment (ME1: the quick slots)
    ue3::Object* mode = nullptr;        // the SFXGameModeDefault
};

// The player's pawn, equipment and default game mode, live. Nullopt (with `why`) if any is missing.
std::optional<Game> ReadGame(std::string& why) {
    if (!ue3::runtime::Up()) {  // every game's gate (on LE1 the same as Ready()): the sequels' adapter is ported
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
#if defined(MEVR_GAME_LE1)
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
    game.equipment = behavior != nullptr ? gEquipment.Read<ue3::Object*>(behavior).value_or(nullptr) : nullptr;
    if (game.equipment == nullptr) {
        why = "the player pawn has no equipment";
        return std::nullopt;
    }
#endif
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

// ---- The game's power wheel, held open by the right stick click (every game: ME1's rule) ----
// The user, 2026-11-05: the click only opens the wheel while it's held; a tap casts nothing (the cast is the left hand's
// push, research/55). The game's RB binding (OnHold 0.3 EnterPowerWheel | OnTap 0.3 TryUseMappedPower; the sequels' the
// same with TryUsePower 1) casts the mapped power for any RB shorter than 0.3 s, so while gPowerHoldOn the host sends no RB
// from the click and the mod opens and closes the wheel itself, on the game thread: EnterPowerWheel on the default mode
// once the click has been held kPowerHoldMs alone (the game's own hold time), SFXGameModePowerWheel.ExitPowerWheel at the
// release (gamepad::WatchPowerWheel). The modes by each game's numbers (engine::adapter::GameMode).

bool InMode(std::optional<std::uint8_t> mode, engine::adapter::Mode context) {
    return mode && engine::adapter::GameMode(*mode) == context;
}

// The player controller's game mode manager's mode of class `cls`, and the manager's current mode (EGameModes).
ue3::Object* ModeOf(ue3::Object* controller, ue3::ClassRef& cls, std::optional<std::uint8_t>* current = nullptr) {
    ue3::Object* const manager = controller != nullptr ? gGameModeManager.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    if (current != nullptr) {
        *current = manager != nullptr ? gCurrentMode.Read<std::uint8_t>(manager) : std::nullopt;
    }
    ue3::TArray<ue3::Object*> const* const modes = manager != nullptr ? gGameModes.In<ue3::TArray<ue3::Object*>>(manager) : nullptr;
    for (int i = 0; modes != nullptr && i < modes->Count(); ++i) {
        ue3::Object* const mode = modes->data[i];
        if (mode != nullptr && ue3::IsA(mode, cls)) {
            return mode;
        }
    }
    return nullptr;
}

void CallNoParms(ue3::Object* object, ue3::Function* function) {
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);  // directly: never a generated wrapper
}

void TickPowerHold(vr::controllers::State const& controllers) {
    gPowerHoldOn = gHolds && gRbSource == gamepad::RbSource::RightStickClick && gEnterPowerWheel != nullptr &&
        gExitPowerWheel != nullptr && ue3::runtime::Up();
    gamepad::RbClickInput in;
    in.click = controllers.active && controllers.right.stickClick;
    in.stickX = controllers.right.stickX;
    in.stickY = controllers.right.stickY;
    in.otherClick = controllers.active && controllers.left.stickClick;
    in.enabled = gPowerHoldOn;
    in.nowMs = controllers.timeMs;  // GetTickCount64 at the sample: the host's clock for RB's same rules
    gamepad::PowerHoldStep const step = gamepad::PowerHoldFromStickClick(gPowerHold, in);
    using engine::adapter::Mode;
    bool asked = false;
    if (step.open) {
        std::string why;
        std::optional<Game> const game = ReadGame(why);
        std::optional<std::uint8_t> before;
        if (game) {
            ModeOf(game->controller, gGameModePowerWheel, &before);
        }
        if (!game || !InMode(before, Mode::OnFoot)) {
            ++gPowerRefused;
            log::Info("holsters: the right stick click held: the power wheel opens only on foot ({})",
                !game ? why : before ? std::format("game mode {}", *before) : std::string("game mode unknown"));
        } else {
            CallNoParms(game->mode, gEnterPowerWheel);
            asked = true;
            std::optional<std::uint8_t> after;
            ModeOf(game->controller, gGameModePowerWheel, &after);
            gPowerOpened = InMode(after, Mode::PowerWheel);
            gPowerOpens += gPowerOpened ? 1 : 0;
            gPowerRefused += gPowerOpened ? 0 : 1;
            log::Info("holsters: the right stick click held {} ms: {} {}", gamepad::kPowerHoldMs, kEnterPowerWheelName,
                gPowerOpened ? std::string("opened the game's power wheel")
                             : std::format("didn't open it (game mode {} after the call: combat not allowed here?)",
                                   after ? std::to_string(*after) : std::string("unknown")));
        }
    }
    // The wheel now, read only while a hold or a late opening is watched.
    ue3::Object* wheel = nullptr;
    std::optional<std::uint8_t> now;
    if (asked || gPowerWatch.asked || gPowerWatch.lateUntilMs != 0) {
        ue3::Object* const pc = player::Controller();
        wheel = pc != nullptr && player::IsBioPlayerController(pc) ? ModeOf(pc, gGameModePowerWheel, &now) : nullptr;
    }
    bool const wheelOpen = wheel != nullptr && InMode(now, Mode::PowerWheel);
    switch (gamepad::WatchPowerWheel(gPowerWatch, step, asked, wheelOpen, in.click, in.nowMs)) {
    case gamepad::PowerWheelClose::AtRelease:
        CallNoParms(wheel, gExitPowerWheel);
        log::Info("holsters: the right stick click let go: SFXGameModePowerWheel.ExitPowerWheel (the game's power wheel closed)");
        break;
    case gamepad::PowerWheelClose::Late:
        CallNoParms(wheel, gExitPowerWheel);
        log::Info("holsters: the game opened its power wheel after the right stick click was let go (it tries again during an "
                  "animated transition): SFXGameModePowerWheel.ExitPowerWheel, nobody holds it");
        break;
    case gamepad::PowerWheelClose::None:
        if (step.close && gPowerOpened) {
            log::Info("holsters: the right stick click let go: the power wheel had already closed (game mode {})",
                now ? std::to_string(*now) : std::string("unknown"));
        }
        break;
    }
    if (step.close) {
        gPowerOpened = false;
    }
}

#if defined(MEVR_GAME_LE1)

ue3::Object* QuickSlot(Game const& game, int i) {
    ue3::Object* const* const slot = gQuickSlots.At<ue3::Object*>(game.equipment, i);
    return slot != nullptr ? *slot : nullptr;
}

void ReadCarried(Game const& game) {
    std::array<std::uintptr_t, slots::kSlots> weapons{};
    for (int i = 0; i < kUsed; ++i) {
        weapons[i] = reinterpret_cast<std::uintptr_t>(QuickSlot(game, i));
    }
    if (weapons != gCarried.weapons) {
        gCarried.weapons = weapons;
        gCarried.typeOfSlot.fill(-1);
        gCarried.slotOfType.fill(-1);
        std::string text;
        for (int i = 0; i < kUsed; ++i) {
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

#else  // the sequels

// The carried weapon of a family (0..4: body_slots::Weapon, the SMG's thighRight the fifth), found fresh: the
// inventory chain is the game's, never a pointer of ours kept across ticks.
ue3::Object* CarriedOfFamily(ue3::Object* pawn, int family) {
    for (ue3::Object* const item : le2_squad::Weapons(pawn)) {
        if (le2_squad::WeaponType(item) == family) {
            return item;
        }
    }
    return nullptr;
}

// The weapon's script state by name ("WeaponEquipping", "Active"...); empty if it can't be asked.
std::string WeaponState(ue3::Object* weapon) {
    if (weapon == nullptr || gGetStateName == nullptr) {
        return {};
    }
    ue3::Params parms(ue3::runtime::Get(), gGetStateName);
    ue3::runtime::Call(weapon, gGetStateName, parms);
    std::optional<ue3::Name> const name = parms.Return<ue3::Name>();
    return name ? ue3::runtime::NameText(*name) : std::string();
}

#if defined(MEVR_GAME_LE2)
// A function without parameters but its bool return, on `object` (false if it can't be asked).
bool AskBool(ue3::Object* object, ue3::Function* function) {
    if (!ue3::runtime::IsAlive(object) || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    return ue3::runtime::Call(object, function, parms) && parms.ReturnBool().value_or(false);
}

// Why ME2's TryHolster would refuse now for a while only (its own tests, in its order: mid-reload, a power running, an animated
// transition), or empty. Its other test, combat, is asked apart (InCombatNow): a gun let go in a fight goes back by TryHolster's
// own last step (the owner, 2026-11-18: "Letting go of a weapon does not reliably remove it from Shepard's possession; in
// firefights I'd let go and it would still be in my hand"; research/le2-vrik.md "A gun let go in a fight").
std::string HolsterBusy(ue3::Object* pawn) {
    if (ue3::Function* const reloading = gIsReloadingFn.Get()) {
        ue3::Params parms(ue3::runtime::Get(), reloading);
        parms.SetBool("bCheckReloadRequest", true);
        if (ue3::runtime::Call(pawn, reloading, parms) && parms.ReturnBool().value_or(false)) {
            return "mid-reload";
        }
    }
    if (AskBool(pawn, gIsUsingPowerFn.Get())) {
        return "a power is running";
    }
    if (AskBool(pawn, gIsInAnimatedTransitionFn.Get())) {
        return "in an animated transition";
    }
    return {};
}

bool InCombatNow(ue3::Object* pawn) {
    return AskBool(pawn, gInCombatFn.Get());
}

// [Holsters] combatHolster (ME2): 1 (the default) a gun let go in a fight goes back too, 0 ME2's own refusal (lane FX2's rule of
// 2026-11-17). Read from the settings file at most every 2 s.
bool CombatHolster() {
    std::uint64_t const now = GetTickCount64();
    if (gCombatHolsterReadMs == 0 || now - gCombatHolsterReadMs >= 2000) {
        gCombatHolsterReadMs = now;
        ini::Loaded const loaded = vr::settings_file::Load(engine::SettingsFile());
        bool const value = ini::GetNumber(loaded.file, "Holsters", "combatHolster").value_or(1.0) != 0.0;
        if (value != gCombatHolster || !gToldCombatHolster) {
            gToldCombatHolster = true;
            log::Info("holsters: a gun let go in a fight {}", value
                ? "goes back too (ME2's TryHolster refuses in combat: its own last step, BioPawnBehavior.SetActionState(explore); "
                  "[Holsters] combatHolster = 1)"
                : "stays in the hand (ME2's own refusal in combat; [Holsters] combatHolster = 0)");
        }
        gCombatHolster = value;
    }
    return gCombatHolster;
}
#endif

void ReadCarried(Game const& game) {
    std::array<std::uintptr_t, slots::kSlots> weapons{};
    std::string text;
    for (ue3::Object* const item : le2_squad::Weapons(game.pawn)) {
        int const family = le2_squad::WeaponType(item);
        if (family >= 0 && family < slots::kSlots && weapons[family] == 0) {
            weapons[family] = reinterpret_cast<std::uintptr_t>(item);
        }
        text += std::format(" {} ({});", ue3::runtime::FullName(item), le2_squad::WeaponLabel(family));
    }
    if (weapons != gCarried.weapons) {
        gCarried.weapons = weapons;
        for (int i = 0; i < slots::kSlots; ++i) {
            gCarried.typeOfSlot[i] = weapons[i] != 0 ? i : -1;  // a family is its own "slot" here
            gCarried.slotOfType[i] = weapons[i] != 0 ? i : -1;
        }
        log::Info("holsters: the player carries:{}", text.empty() ? std::string(" nothing") : text);
    }
    ue3::Object* const held = le2_squad::equippedWeapon.Read<ue3::Object*>(game.pawn).value_or(nullptr);
    int const family = held != nullptr ? le2_squad::WeaponType(held) : -1;
    gCarried.current = family >= 0 && family < slots::kSlots ? family : -1;
#if defined(MEVR_GAME_LE2)
    gCarried.drawn = held != nullptr && gActionState.Read<std::uint8_t>(game.pawn).value_or(0) == kActionStateCombat;
#else
    gCarried.drawn = held != nullptr;  // ME3 has no holstered action state: an equipped gun is out
    // A heavy weapon in hand: a switch would throw it away, so grabs at the slots are refused while it is (question 13).
    bool const heavy = ue3::runtime::IsAlive(held) && ue3::IsA(held, gHeavyWeapon);
    if (heavy != gCarried.switchLosesHeld) {
        log::Info("holsters: {}", heavy
            ? std::format("a heavy weapon is in hand ({}): a grab at a slot is refused while it is (ME3's SwitchWeapon would "
                          "throw it away: SFXHeavyWeapon.PutDownWeapon); the game's weapon wheel switches away",
                  ue3::runtime::FullName(held))
            : std::string("the heavy weapon is out of the hand: grabs at the slots draw again"));
    }
    gCarried.switchLosesHeld = heavy;
#endif
    std::string const state = WeaponState(held);
    gCarried.switching = state == "WeaponEquipping" || state == "WeaponPuttingDown";
}

#endif  // per game

// Whether the game allows combat here: what each of its own draws asks first (ME1's SFXGameModeDefault.SwitchToWeapon
// returns at once without it; the sequels' FireWeapon, TightAim, NextWeapon, the weapon wheel's EnterWeaponWheel). False in
// the hubs and on the Normandy. The sequels' answer handler (OnIsCombatEnabled) answers only the draw routes' calls, never
// this one. Unknown counts as allowed (the draw as before).
bool CombatEnabled(ue3::Object* controller) {
#if defined(MEVR_GAME_LE1)
    ue3::Object* const input = gPlayerInput.Read<ue3::Object*>(controller).value_or(nullptr);
    ue3::Function* const fn = gIsCombatEnabledFn.Get();
#else
    ue3::Object* const input = le2_squad::inputField.Read<ue3::Object*>(controller).value_or(nullptr);
    ue3::Function* const fn = le2_squad::combatEnabled.Get();
#endif
    if (!ue3::runtime::IsAlive(input) || fn == nullptr) {
        return true;
    }
    ue3::Params parms(ue3::runtime::Get(), fn);
    return !ue3::runtime::Call(input, fn, parms) || parms.ReturnBool().value_or(true);
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

// The draw a grab asks for. False: refused here (the sequels: where the game's own draws are refused), felt as a refusal.
bool Draw(Game const& game, Weapon weapon, char const* slot, Link const& link, double strength, bool holds) {
#if !defined(MEVR_GAME_LE1)
    ue3::Object* const item = CarriedOfFamily(game.pawn, static_cast<int>(weapon));
    if (item == nullptr) {
        log::Info("holsters: grab at {}: the {} is no longer carried", slot, slots::Name(weapon));
        return false;
    }
#endif
    // Every game's own draws ask BioPlayerInput.IsCombatEnabled first: ME1's SwitchToWeapon returns at once without it (the
    // grab used to be felt as a draw while nothing came out), and the sequels' SwitchWeapon and SetActionState don't ask (a
    // grab in a hub pulled a gun out where the game allows none). One rule since 2026-11-17 (the owner, question 15).
    if (!CombatEnabled(game.controller)) {
        ++gRefusals;
        log::Info("holsters: grab at {}: the {} stays in its holster: the game allows no combat here (BioPlayerInput."
                  "IsCombatEnabled false: its own draws are refused too)", slot, slots::Name(weapon));
        Pulse(link, slots::Refused(strength));
        return false;
    }
#if defined(MEVR_GAME_LE1)
    int const quickSlot = gCarried.slotOfType[static_cast<int>(weapon)];
    ue3::Params parms(ue3::runtime::Get(), gSwitchToWeapon);
    parms.Set("Slot", static_cast<std::uint8_t>(quickSlot));
    log::Info("holsters: grab at {}: drawing the {} (SFXGameModeDefault.SwitchToWeapon({}); was {}{}){}", slot, slots::Name(weapon),
        quickSlot, gCarried.drawn ? slots::Name(Held()) : "holstered", gCarried.switching ? ", the game is mid-switch" : "",
        holds ? "; keep the grip squeezed to hold it" : "");
    ue3::runtime::Call(game.mode, gSwitchToWeapon, parms);  // directly: never a generated wrapper
#else
    ue3::Object* const equipped = le2_squad::equippedWeapon.Read<ue3::Object*>(game.pawn).value_or(nullptr);
    bool const switches = equipped != item;
    bool raises = false;
#if defined(MEVR_GAME_LE2)
    ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(game.pawn).value_or(nullptr);
    raises = !gCarried.drawn && behavior != nullptr && gSetActionState != nullptr;
#endif
    log::Info("holsters: grab at {}: drawing the {} ({}{}{}; was {}{}){}", slot, slots::Name(weapon),
        switches ? "BioPlayerController.SwitchWeapon" : "", switches && raises ? ", " : "",
        raises ? "BioPawnBehavior.SetActionState(combat)" : switches ? "" : "already equipped and out",
        gCarried.drawn ? slots::Name(Held()) : "holstered", gCarried.switching ? ", the game is mid-switch" : "",
        holds ? "; keep the grip squeezed to hold it" : "");
    if (switches) {
        ue3::Params parms(ue3::runtime::Get(), gSwitchWeapon);
        parms.Set("oWpn", item);
        ue3::runtime::Call(game.controller, gSwitchWeapon, parms);  // the game thread, through our reflection layer
    }
#if defined(MEVR_GAME_LE2)
    if (raises) {
        ue3::Params parms(ue3::runtime::Get(), gSetActionState);
        parms.Set("eState", kActionStateCombat);
        ue3::runtime::Call(behavior, gSetActionState, parms);  // a native with an opcode: runtime::Call handles it
    }
#endif
#endif
    gPending = {slots::Action::Draw, weapon, 0.0};
    gSinceDraw = 0.0;
    ++gDraws;
    Pulse(link, slots::DrawClick(strength));
    return true;
}

void Holster(Game const& game, Weapon weapon, std::string const& why, Link const& link, double strength) {
    if (!gCarried.drawn) {
        log::Info("holsters: {}: the {} is already put away", why, slots::Name(weapon));
        return;
    }
#if defined(MEVR_GAME_LE3)
    // Never TryHolster here: ME3's only breaks the player out of cover. The put-down into no weapon instead (above).
    ue3::Object* const manager = le2_squad::inventoryManager.Read<ue3::Object*>(game.pawn).value_or(nullptr);
    if (!gPutAway || gSetCurrentWeapon == nullptr || !ue3::runtime::IsAlive(manager)) {
        log::Info("holsters: {}: the {} stays in the hand ({})", why, slots::Name(weapon),
            !gPutAway ? "ME3 keeps a gun out on a mission; [Holsters] le3PutAway = 1 puts it away"
                      : gSetCurrentWeapon == nullptr ? "SFXInventoryManager.SetCurrentWeapon not found" : "the pawn has no inventory manager");
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), gSetCurrentWeapon);
    parms.Set("DesiredWeapon", static_cast<ue3::Object*>(nullptr));
    log::Info("holsters: {}: the {} goes back in its holster (SFXInventoryManager.SetCurrentWeapon(None): ME3's own put-down, "
              "[Holsters] le3PutAway = 1)", why, slots::Name(weapon));
    ue3::runtime::Call(manager, gSetCurrentWeapon, parms);  // the game thread, through our reflection layer
#else
#if defined(MEVR_GAME_LE2)
    // ME2's TryHolster refuses silently mid-reload, while a power runs, in an animated transition and in combat. The first three
    // pass: the let-go waits for them and is tried again each tick (StepDeferred). In combat its own last step puts the gun away
    // (BioPawnBehavior.SetActionState(explore)), unless [Holsters] combatHolster = 0 keeps ME2's refusal (felt as a refusal).
    if (std::string const busy = HolsterBusy(game.pawn); !busy.empty()) {
        if (!gDeferred.active || gDeferred.weapon != weapon) {
            log::Info("holsters: {}: the {} waits ({}: SFXGameModeDefault.TryHolster refuses then); it goes back once that's over, "
                      "unless the grip closes on it again", why, slots::Name(weapon), busy);
        }
        gDeferred = {true, weapon, why, busy, gDeferred.active && gDeferred.weapon == weapon ? gDeferred.age : 0.0};
        return;
    }
    gDeferred = {};
    if (InCombatNow(game.pawn)) {
        if (!CombatHolster()) {
            ++gRefusals;
            log::Info("holsters: {}: the {} stays out: the game won't holster it now (in combat: SFXGameModeDefault.TryHolster refuses "
                      "then; [Holsters] combatHolster = 0)", why, slots::Name(weapon));
            Pulse(link, slots::Refused(strength));
            return;
        }
        ue3::Object* const behavior = gBehavior.Read<ue3::Object*>(game.pawn).value_or(nullptr);
        if (behavior == nullptr || gSetActionState == nullptr) {
            ++gRefusals;
            log::Info("holsters: {}: the {} stays out: in combat, and the pawn's behaviour or SetActionState wasn't found", why,
                slots::Name(weapon));
            Pulse(link, slots::Refused(strength));
            return;
        }
        ue3::Params parms(ue3::runtime::Get(), gSetActionState);
        parms.Set("eState", std::uint8_t{0});  // BIO_ACTION_STATE_EXPLORE
        log::Info("holsters: {}: the {} goes back in its holster (in combat, where SFXGameModeDefault.TryHolster refuses: its own last "
                  "step, BioPawnBehavior.SetActionState(explore))", why, slots::Name(weapon));
        ue3::runtime::Call(behavior, gSetActionState, parms);  // a native with an opcode: runtime::Call handles it
        ++gCombatHolsters;
        gPending = {slots::Action::Holster, weapon, 0.0};
        ++gHolsters;
        Pulse(link, slots::HolsterClick(strength));
        return;
    }
#endif
    ue3::Params parms(ue3::runtime::Get(), gTryHolster);
    log::Info("holsters: {}: the {} goes back in its holster (SFXGameModeDefault.TryHolster)", why, slots::Name(weapon));
    ue3::runtime::Call(game.mode, gTryHolster, parms);  // directly: never a generated wrapper
#if defined(MEVR_GAME_LE2)
    if (!gToldHolsterState) {  // once: why the refusal is asked before the call, not read after it
        gToldHolsterState = true;
        log::Info("holsters: ME2's action state right after TryHolster: {} (1 combat, 0 explore; it turns explore when the "
                  "holster animation ends)", gActionState.Read<std::uint8_t>(game.pawn).value_or(0xFF));
    }
#endif
#endif
    gPending = {slots::Action::Holster, weapon, 0.0};
    ++gHolsters;
    Pulse(link, slots::HolsterClick(strength));
}

#if defined(MEVR_GAME_LE2)
// A let-go that waits (mid-reload, a power, a transition): tried again each tick until the game would take it, for kDeferMaxS at
// most; called off when the grip closes on the gun again, the gun changes or is already away.
void StepDeferred(Game const& game, Link const& link, double strength, double deltaSeconds, bool gripDown, bool trigger) {
    if (!gDeferred.active) {
        return;
    }
    gDeferred.age += std::max(deltaSeconds, 0.0);
    char const* const off = gripDown ? "the grip closed on it again"
        : !gCarried.drawn ? "it's already away"
        : Held() != gDeferred.weapon ? "the gun changed"
        : gDeferred.age > kDeferMaxS ? "the game stayed busy"
        : nullptr;
    if (off != nullptr) {
        log::Info("holsters: the {} stays out ({}; it waited {:.2f} s: {})", slots::Name(gDeferred.weapon), off, gDeferred.age, gDeferred.busy);
        gDeferred = {};
        return;
    }
    if (trigger || !HolsterBusy(game.pawn).empty()) {
        return;  // still busy, or mid-burst: never put away under the trigger
    }
    DeferredHolster const was = gDeferred;
    gDeferred = {};
    ++gDeferredDone;
    Holster(game, was.weapon, std::format("{} (after {:.2f} s: {})", was.why, was.age, was.busy), link, strength);
}

// A draw ME2 makes for something the player asked: a power running (BioPawn.IsUsingPower), or a wheel's order waiting for combat
// mode (BioPlayerController.m_currentOrder.nmPower: ApplyTacticalOrders draws first, then casts). Empty: none.
std::string DeliberateDraw(Game const& game) {
    if (AskBool(game.pawn, gIsUsingPowerFn.Get())) {
        return "a power is running";
    }
    if (void* const order = gCurrentOrder.Address(game.controller)) {
        if (gOrderPower.Read<ue3::Name>(order).value_or(ue3::Name{}).index != 0) {
            return "a power ordered from a wheel waits for it";
        }
        if (gOrderWeapon.Read<ue3::Object*>(order).value_or(nullptr) != nullptr) {
            return "a weapon picked on the game's weapon wheel";
        }
    }
    return {};
}

// The weapon came out on foot in VR with no grab of ours (only holsters draw): ME2 draws by the native SetActionState from routes
// no hook refuses before they run (the wheels' close, a fight's start, a script; research/le2-vrik.md "A gun let go in a fight"),
// so it goes back at once by the same holster. The draws powers need are let through; a game that draws again at once is left
// alone after the second put-back within kUndoWindowS (no flicker).
void PutBackUnasked(Game const& game, Link const& link) {
    Weapon const weapon = Held();
    if (std::string const deliberate = DeliberateDraw(game); !deliberate.empty()) {
        ++gUndoDeliberate;
        log::Info("holsters: the {} came out for a power ({}): let through", slots::Name(weapon), deliberate);
        return;
    }
    if (gUndoPausedS > 0.0) {
        ++gUndoLeft;
        log::Info("holsters: the {} came out again: left in the hand (the game drew it three times within {:.0f} s; put-backs resume "
                  "in {:.1f} s)", slots::Name(weapon), kUndoWindowS, gUndoPausedS);
        return;
    }
    if (gUndoAges[1] < kUndoWindowS) {
        gUndoPausedS = 20.0;
        ++gUndoLeft;
        log::Info("holsters: the {} came out a third time within {:.0f} s: left in the hand for 20 s (the game keeps drawing it)",
            slots::Name(weapon), kUndoWindowS);
        return;
    }
    gUndoAges = {0.0, gUndoAges[0]};
    ++gUndone;
    Holster(game, weapon, "only holsters draw: put back at once", link, 0.0);  // no click: the player did nothing
}

void AgeUndo(double deltaSeconds) {
    double const dt = std::max(deltaSeconds, 0.0);
    gUndoAges = {std::min(gUndoAges[0] + dt, 1e9), std::min(gUndoAges[1] + dt, 1e9)};
    gUndoPausedS = std::max(gUndoPausedS - dt, 0.0);
}
#endif

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

void Inactive(std::string why, Link const& link) {
    if (gMachine.InSlot() >= 0) {
        log::Info("holsters: right hand out of every slot ({})", why);
    }
    if (gMachine.ReturnLeft() >= 0.0) {
        log::Info("holsters: the let-go gun stays out ({})", why);
    }
#if defined(MEVR_GAME_LE2)
    if (gDeferred.active) {
        log::Info("holsters: the let-go {} that waited ({}) stays out ({})", slots::Name(gDeferred.weapon), gDeferred.busy, why);
        gDeferred = {};
    }
#endif
    gMachine.Reset();
    gAnchor.Reset();
    gLostHand.Reset();
    gOwnYawActive = false;
    gYawValid = false;
    gNear = false;
    gActive = false;
    gWasDrawnKnown = false;  // gCarried isn't read while inactive: a draw meanwhile isn't reported as unasked
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
    if (body_calibration::Position() == vr::calibration::PlayPosition::Seated) {
        height = slots::kReferenceEyeHeight;
        source = "seated fixed meters from the neutral head (scale 1)";
    } else if (auto const calibrated = body_calibration::StandingEyeHeight()) {
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

// ---- Only holsters draw: one rule for every game ----
// How the game itself came to want the weapon out. Each game's hook names the route: ME1's skip handler of
// SFXPawn_Player.DrawPrimaryWeapon by its caller, ME2's and ME3's answer to BioPlayerInput.IsCombatEnabled inside the
// buttons' own functions (above).
enum class DrawRoute {
    Trigger,           // the trigger (ME2: the aim trigger too): refused without a buzz (a pull with an empty hand is a fist)
    Button,            // any other route: refused, and the right hand buzzes
    Deliberate,        // medi-gel and powers: let through (they need combat mode; the lead's decision, 2026-10-26)
    SeatedWheelEnter,  // the seated weapon wheel's eager draw: refused without a buzz
    SeatedWheelClose,  // ...its close applies a choice: let through
};

// True: the draw is refused (the hook skips it). `pawn`: whose weapon the game wants out. Game thread (the script VM's).
bool RefuseGameDraw(ue3::Object* pawn, DrawRoute route, ue3::Function* caller) {
    if (!gBlockDraws || gAllowedDraws > 0 || pawn == nullptr || pawn != gBlockPawn) {
        return false;
    }
    // Only closing applies a choice. EnterWeaponWheel's eager draw remains refused.
    if (route == DrawRoute::SeatedWheelClose) {
        log::Info("holsters: seated weapon wheel draw allowed: {}", ue3::runtime::FullName(caller));
        gSinceAllowedDraw = 0.0;
        return false;
    }
    if (route == DrawRoute::Deliberate) {
        log::Info("holsters: the game draws your weapon for {}: let through (medi-gel and powers need combat mode)",
            ue3::runtime::FullName(caller));
        return false;
    }
    ++gRefusedDraws;
    bool const silent = route == DrawRoute::Trigger || route == DrawRoute::SeatedWheelEnter;
    gRefusedBuzz = gRefusedBuzz || !silent;
    log::Info("holsters: the game wanted to draw your weapon ({}): refused, only a grab at a holster draws "
              "([Holsters] onlyHolstersDraw = 1){}",
        caller != nullptr ? ue3::runtime::FullName(caller) : std::string("called from native code"),
        silent ? "; trigger or seated wheel entry, so no buzz" : "");
    return true;
}

#if defined(MEVR_GAME_LE1)
// engine/call_function's skip handler for SFXPawn_Player.DrawPrimaryWeapon (game thread: the script VM's).
bool OnDrawPrimaryWeapon(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* caller) {
    if (!gBlockDraws || gAllowedDraws > 0 || context == nullptr || context != gBlockPawn) {
        return false;
    }
    DrawRoute route = DrawRoute::Button;
    if (caller != nullptr) {
        if (SeatedWeaponWheel() && caller == gSeatedWheelCallers[1]) {
            route = DrawRoute::SeatedWheelClose;
        } else if (std::find(gAllowedCallers.begin(), gAllowedCallers.end(), caller) != gAllowedCallers.end()) {
            route = DrawRoute::Deliberate;
        } else if (caller == gFireWeapon) {
            route = DrawRoute::Trigger;
        } else if (SeatedWeaponWheel() && caller == gSeatedWheelCallers[0]) {
            route = DrawRoute::SeatedWheelEnter;
        }
    }
    return RefuseGameDraw(context, route, caller);
}
#else
// The player's weapon is away in the sense the game's own buttons would draw it: ME2, the pawn's action state isn't combat;
// ME3, the pawn carries weapons but has none equipped (a non-combat pawn, the Normandy's, carries none: nothing to refuse).
// Unknown counts as out (the button's ordinary work).
bool WeaponAway(ue3::Object* pawn) {
#if defined(MEVR_GAME_LE2)
    return gActionState.Read<std::uint8_t>(pawn).value_or(kActionStateCombat) != kActionStateCombat;
#else
    std::optional<ue3::Object*> const held = le2_squad::equippedWeapon.Read<ue3::Object*>(pawn);
    return held && *held == nullptr && !le2_squad::Weapons(pawn).empty();
#endif
}

// engine/call_function's answer handler for BioPlayerInput.IsCombatEnabled (game thread: the script VM's): false for the
// one call a draw route makes while the player's weapon is away. With the weapon out the buttons do their ordinary work.
bool OnIsCombatEnabled(ue3::Object* /*context*/, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* /*callerLocals*/,
    void* result) {
    if (!gBlockDraws || caller == nullptr || gBlockPawn == nullptr || !ue3::runtime::IsAlive(gBlockPawn)) {
        return false;
    }
    auto const at = std::find(gDrawRoutes.begin(), gDrawRoutes.end(), caller);
    if (at == gDrawRoutes.end()) {
        return false;
    }
    if (!WeaponAway(gBlockPawn)) {
        return false;  // the weapon is out (or can't be told): the button's own work
    }
    bool const trigger = static_cast<std::size_t>(at - gDrawRoutes.begin()) < kTriggerRoutes;
    if (!RefuseGameDraw(gBlockPawn, trigger ? DrawRoute::Trigger : DrawRoute::Button, caller)) {
        return false;
    }
    if (result != nullptr) {
        *static_cast<std::uint32_t*>(result) = 0;  // a script bool is 4 bytes: "combat isn't enabled", for this call alone
    }
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

bool BlocksGameDraws() {
    return gBlockDraws;
}

bool SeatedWeaponWheel() {
    auto const settings = hand_calibration::GetHolsters();
    return body_calibration::Position() == vr::calibration::PlayPosition::Seated &&
        settings.enabled && settings.seatedWeaponWheel;
}

std::string DrawByName(std::string_view name) {
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    // `mevr weapon away`: the held gun put away as a let-go at its slot does (ME3's put-down, game/Melee's omni-blade fist; ME2's
    // TryHolster or, in a fight, its own last step: the vault's holstered check, tools/le2-halcyon-run/vault.ps1 -Holster).
    if (name == "away") {
        std::string why;
        std::optional<Game> const game = gStarted ? ReadGame(why) : std::nullopt;
        if (!game) {
            return gStarted ? why : std::string("the holsters aren't started (no script functions yet)");
        }
        ReadCarried(*game);
        Holster(*game, Held(), "the console", Link{}, 0.0);
        return "putting the weapon away (the 'holsters:' log line says how)";
    }
#endif
    std::optional<Weapon> const weapon = slots::ParseWeapon(name);
    if (!weapon || *weapon == Weapon::None) {
        return kUsed > slots::kSmgSlot ? "usage: mevr weapon pistol|shotgun|assault|sniper|smg" : "usage: mevr weapon pistol|shotgun|assault|sniper";
    }
    if (!gStarted) {
        return "the holsters aren't started (no script functions yet)";
    }
    std::string why;
    std::optional<Game> const game = ReadGame(why);
    if (!game) {
        return why;
    }
#if !defined(MEVR_GAME_LE1)
    ReadCarried(*game);  // the console may ask before the holsters' first active tick (no VR view, no controllers)
    if (!Carries(*weapon)) {
        std::string carried;
        for (ue3::Object* const item : le2_squad::Weapons(game->pawn)) {
            carried += std::format("{}{} ({})", carried.empty() ? "" : ", ", ue3::runtime::FullName(item),
                le2_squad::WeaponLabel(le2_squad::WeaponType(item)));
        }
        return std::format("no {} is carried; carried: {}", slots::Name(*weapon), carried.empty() ? std::string("nothing") : carried);
    }
    if (gCarried.switchLosesHeld) {  // ME3's heavy weapon in hand (question 13): as a grab at its slot
        return std::format("the {} wasn't drawn: a heavy weapon is in hand and a switch would throw it away (the game's weapon "
                           "wheel switches away)", slots::Name(*weapon));
    }
#endif
    if (!Carries(*weapon)) {
        return std::format("no {} is carried (the holsters haven't read the quick slots, or it isn't equipped)", slots::Name(*weapon));
    }
    if (!Draw(*game, *weapon, "the console", Link{}, 0.0, false)) {
        return std::format("the {} wasn't drawn (the 'holsters:' log line says why)", slots::Name(*weapon));
    }
    return std::format("drawing the {}", slots::Name(*weapon));
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
    gFireWeapon = gFireWeaponFn.Get();
    gSeatedWheelCallers = {gEnterWeaponWheelFn.Get(), gWeaponWheelDeactivatedFn.Get()};
    gEnterPowerWheel = gEnterPowerWheelFn.Get();
    gExitPowerWheel = gExitPowerWheelFn.Get();
    log::Info("holsters: the right stick click's hold {}", gEnterPowerWheel != nullptr && gExitPowerWheel != nullptr
            ? "opens the game's power wheel (SFXGameModeDefault.EnterPowerWheel, SFXGameModePowerWheel.ExitPowerWheel); a tap casts nothing"
            : "can't open the power wheel itself (EnterPowerWheel or ExitPowerWheel not found): the click stays RB (a tap casts the mapped power)");
    ue3::FunctionRef* const allowed[] = {&gUseFirstAidFn, &gTryUseMappedPowerFn, &gEnterPowerWheelFn};
    std::string allowedText;
    for (std::size_t i = 0; i < gAllowedCallers.size(); ++i) {
        gAllowedCallers[i] = allowed[i]->Get();
        allowedText += std::format("{}{} ({})", i > 0 ? ", " : "", allowed[i]->Path(), gAllowedCallers[i] != nullptr ? "found" : "NOT FOUND");
    }
    if (gDrawSkipReady) {
        log::Info("holsters: the game's own draws are still let through from: {} (a caller not found is refused like the rest); "
                  "the trigger's ({}) refused without a buzz",
            allowedText, gFireWeapon != nullptr ? "SFXGameModeBase.FireWeapon found" : "SFXGameModeBase.FireWeapon NOT FOUND: buzzes");
    }
    return gStarted;
#else
    gSwitchWeapon = gSwitchWeaponFn.Get();
    gTryHolster = gTryHolsterFn.Get();
    gGetStateName = gGetStateNameFn.Get();
    bool const classes = le2_squad::weaponClass.Get() != nullptr && le2_squad::inventoryManager.Get() != nullptr &&
        le2_squad::inventoryChain.Get() != nullptr && le2_squad::equippedWeapon.Get() != nullptr && gGameModeDefault.Get() != nullptr &&
        gGameModeManager.Get() != nullptr && gGameModes.Get() != nullptr;
#if defined(MEVR_GAME_LE2)
    gSetActionState = gSetActionStateFn.Get();
    bool const actionState = gSetActionState != nullptr && gBehavior.Get() != nullptr && gActionState.Get() != nullptr;
    char const* const actionText = actionState ? "found" : "MISSING";
#else
    bool const actionState = true;
    char const* const actionText = "none in this game: an equipped gun is out";
    gSetCurrentWeapon = gSetCurrentWeaponFn.Get();
    log::Info("holsters: ME3's holster is {} (TryHolster is never called here: it only leaves cover)",
        gSetCurrentWeapon != nullptr ? "the game's own put-down into no weapon, SFXInventoryManager.SetCurrentWeapon(None), with "
                                       "[Holsters] le3PutAway = 1 (the default; 0: a let-go gun stays in the hand)"
                                     : "MISSING (SFXInventoryManager.SetCurrentWeapon not found): a let-go gun stays in the hand");
#endif
    gStarted = gSwitchWeapon != nullptr && gTryHolster != nullptr && classes && actionState;
    log::Info("holsters: script functions {} (BioPlayerController.SwitchWeapon {}, SFXGameModeDefault.TryHolster {}, the "
              "inventory chain and game modes {}, the action state (BioPawn.ActionState, BioPawnBehavior.SetActionState) {}, "
              "Object.GetStateName {}{})", gStarted ? "found" : "MISSING: holsters off", gSwitchWeapon != nullptr,
        gTryHolster != nullptr, classes, actionText, gGetStateName != nullptr,
        gGetStateName != nullptr ? "" : ": a holster during the game's weapon switch isn't held back");
    gDrawRoutes = {gFireWeaponFn.Get(), gTightAimFn.Get(), gTryReloadFn.Get(), gNextWeaponFn.Get(), gPrevWeaponFn.Get()};
    std::string routes;
    for (ue3::Function* const route : gDrawRoutes) {
        routes += std::format("{}{}", routes.empty() ? "" : ", ", route != nullptr ? ue3::runtime::FullName(route) : std::string("(one NOT FOUND)"));
    }
    gDrawSkipReady = false;
    ue3::Function* const enabled = gIsCombatEnabledFn.Get();
    // Only for a function whose one parameter is its bool return value (the answer handler's rule).
    ue3::Reflection& reflection = ue3::runtime::Get();
    bool const answerable = enabled != nullptr && reflection.Parameters(enabled).size() == 1 && reflection.ParmsSize(enabled) == 4;
    if (gStarted && answerable && gDrawRoutes[0] != nullptr) {
        engine::call_function::AddAnswerHandler(enabled, &OnIsCombatEnabled);
        gDrawSkipReady = true;
    }
    log::Info("holsters: the game's own draws with the weapon away {} ([Holsters] onlyHolstersDraw): BioPlayerInput.IsCombatEnabled "
              "{} inside {}; the first two (the triggers) without a buzz. Powers and the game's combat start still draw{}",
        gDrawSkipReady ? "can be refused" : "can't be refused: the game may still draw by itself",
        answerable ? "is answered false" : enabled != nullptr ? "has parameters" : "wasn't found", routes,
#if defined(MEVR_GAME_LE3)
        " (the weapon is away only after le3PutAway's put-down)"
#else
        ""
#endif
    );
    gEnterPowerWheel = gEnterPowerWheelFn.Get();
    gExitPowerWheel = gExitPowerWheelFn.Get();
    bool const wheelNames = gEnterPowerWheel != nullptr && gExitPowerWheel != nullptr && gGameModePowerWheel.Get() != nullptr &&
        gCurrentMode.Get() != nullptr;
    if (!wheelNames) {
        gEnterPowerWheel = gExitPowerWheel = nullptr;  // TickPowerHold stands down: the click stays RB
    }
    log::Info("holsters: the right stick click's hold {}", wheelNames
            ? std::format("opens the game's power wheel ({}, SFXGameModePowerWheel.ExitPowerWheel); a tap casts nothing",
                  kEnterPowerWheelName)
            : std::string("can't open the power wheel itself (EnterPowerWheel, ExitPowerWheel, the power wheel mode or the "
                          "manager's CurrentMode not found): the click stays RB (a tap casts the second mapped power)"));
    return gStarted;
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
    // On foot and in its two wheels (ME1's 0, 2, 3; each game's own numbers through the adapter).
    auto const mode = gStarted ? head_tracking::GameMode() : std::nullopt;
    gHolds = holdsWanted && mode && engine::adapter::IsFootOrWheelMode(*mode);
    TickPowerHold(controllers);
    if (!gLoggedHolds || gHolds != gLoggedHoldsValue) {
        gLoggedHolds = true;
        gLoggedHoldsValue = gHolds;
        log::Info("holsters: {}", gHolds ? std::format("the right grip holds the gun (no RB from it); RB is {}", gamepad::Name(gRbSource))
                                         : std::string("the right grip is RB (gripHolds off, or not on foot)"));
    }

    // Only holsters draw: on foot (and in its two wheels) in VR, the game's own DrawPrimaryWeapon is refused for the
    // player's pawn (OnDrawPrimaryWeapon; the sequels: the buttons' own draws, OnIsCombatEnabled). Only while a holster could draw instead: tracked controllers (a pad player in
    // VR, or controllers asleep, keeps the game's draws), and only once the CallFunction hook is in (Mod.cpp puts it in
    // when hand aim first comes on), so the log never claims a refusal that can't happen.
    gBlockPawn = nullptr;
    bool const hooked = engine::call_function::Installed();
    if (gDrawSkipReady && hooked && settings.enabled && settings.onlyHolstersDraw && gHolds && controllers.active &&
        vr::host_view::Latest().valid) {
        ue3::Object* const pc = player::Controller();
        gBlockPawn = pc != nullptr && player::IsBioPlayerController(pc) ? player::BioPawn(pc) : nullptr;
    }
    gBlockDraws = gBlockPawn != nullptr;
    if (!gLoggedBlock || gBlockDraws != gLoggedBlockValue) {
        gLoggedBlock = true;
        gLoggedBlockValue = gBlockDraws;
        log::Info("holsters: {}", gBlockDraws
                ? "only a grab at a holster draws a weapon (the game's own draws are refused, except for medi-gel and powers)"
                : gDrawSkipReady && !hooked && settings.enabled && settings.onlyHolstersDraw
                ? "the game may draw the weapon by itself (the script call hook isn't in yet: it goes in when hand aim comes on)"
                : "the game may draw the weapon by itself (onlyHolstersDraw off, not on foot, no VR view or no tracked controllers)");
    }
    if (gRefusedBuzz) {
        gRefusedBuzz = false;
        Pulse(link, slots::Refused(settings.hapticAmplitude));  // the refusal, felt: no holster was reached
    }

    if (!settings.enabled) {
        Inactive("off ([Holsters] enabled = 0)", link);
        return;
    }
    UpdateEyeHeight(settings, link, deltaSeconds);
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
    // A right hand lost behind the back at the SMG's lowerBack slot (inside-out headsets can't see it there) keeps its last
    // place on the body for [Holsters] lostHandHoldS: the slots stay live and a squeeze there still draws. Never on ME1
    // (no SMG slot) nor at any other slot: there a lost hand turns the slots off as always.
    bool const keptLost = controllers.active && !right.gripValid && !body_calibration::Calibrating() &&
        gLostHand.Keeps(settings.lostHandHoldS, deltaSeconds);
    if (!controllers.active || (!right.gripValid && !keptLost) || body_calibration::Calibrating()) {
        Inactive(gLostHand.Holding() ? std::format("the right controller isn't tracked (its last place at {} was kept {:.2f} s)",
                                           slots::Name(settings.layout.smgPlace), gLostHand.LostSeconds())
                                     : std::string("the right controller isn't tracked"),
            link);
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
#if defined(MEVR_GAME_LE2)
    AgeUndo(deltaSeconds);
    if (gWasDrawnKnown && gCarried.drawn && !gWasDrawn && gSinceDraw > kResultSeconds && gSinceAllowedDraw > kResultSeconds) {
        log::Info("holsters: the {} came out without a holster grab (right trigger {:.2f}, grip {:.2f}; left trigger {:.2f}, "
                  "grip {:.2f}){}", slots::Name(Held()), right.trigger, right.squeeze, controllers.left.trigger,
            controllers.left.squeeze, gBlockDraws ? ": a route no hook refuses (a wheel's close, a fight's start, a script)" : "");
        if (gBlockDraws && !gOwnGrip) {
            PutBackUnasked(*game, link);
        }
    }
#else
    if (gWasDrawnKnown && gCarried.drawn && !gWasDrawn && gSinceDraw > kResultSeconds && gSinceAllowedDraw > kResultSeconds) {
        log::Info("holsters: the {} came out without a holster grab (right trigger {:.2f}, grip {:.2f}; left trigger {:.2f}, "
                  "grip {:.2f}){}", slots::Name(Held()), right.trigger, right.squeeze, controllers.left.trigger,
            controllers.left.squeeze, gBlockDraws ? ": not through DrawPrimaryWeapon, so it wasn't refused" : "");
    }
#endif
    gWasDrawn = gCarried.drawn;
    gWasDrawnKnown = true;
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
    gScaleNow = scale;
    double const lostBefore = gLostHand.LostSeconds();
    ik::Vec3 const hand = keptLost ? gLostHand.Kept(gFrame, deltaSeconds)
                                   : ik::Vec3{right.grip.position.x, right.grip.position.y, right.grip.position.z};
    if (keptLost && lostBefore < 0.0) {
        ++gLostHolds;
        log::Info("holsters: the right controller lost tracking at {}: its last place on the body is kept for up to {:.2f} s "
                  "([Holsters] lostHandHoldS)", slots::Name(settings.layout.smgPlace), settings.lostHandHoldS);
    } else if (!keptLost && lostBefore >= 0.0) {
        log::Info("holsters: the right controller is tracked again after {:.2f} s (its last place at {} was kept meanwhile)",
            lostBefore, slots::Name(settings.layout.smgPlace));
    }
    if (keptLost) {
        gLostLongest = std::max(gLostLongest, gLostHand.LostSeconds());
    }
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
    input.switching = gCarried.switching;
    input.switchLosesHeld = gCarried.switchLosesHeld;  // ME3's heavy weapon in hand (always false on ME1 and ME2)
    input.holster = settings.holster;
#if defined(MEVR_GAME_LE3)
    // Without the put-down ME3 can't put a gun away: no holster at all then (a let-go gun stays in the hand, no grace runs).
    gPutAway = settings.le3PutAway;
    if (!gPutAway || gSetCurrentWeapon == nullptr) {
        input.holster = slots::Holster::Off;
        if (!gToldNoPutAway) {
            gToldNoPutAway = true;
            log::Info("holsters: a gun let go stays in the hand: ME3 keeps a gun out on a mission ([Holsters] le3PutAway = 0; 1, "
                      "the default, puts it away with the game's own put-down)");
        }
    } else {
        gToldNoPutAway = false;
    }
#endif
    input.gripHolds = gHolds;
    input.releaseGrace = settings.releaseGrace;
    input.deltaSeconds = deltaSeconds;
    input.gripKept = gOwnGrip && link.rightGrip && (*link.rightGrip & 1u) != 0;
    slots::Step const step = gMachine.Update(input);
    gNear = step.nearby;
    if (!keptLost) {
        // Tracked: remember the hand's place on the body while it's in or near a slot that keeps a lost hand.
        bool keep = false;
        for (int i = 0; i < slots::kSlots; ++i) {
            keep = keep || (slots::KeepsLostHand(settings.layout, i) && input.distance[i] >= 0.0 &&
                               input.distance[i] <= input.radius[i] + input.exitMargin);
        }
        gLostHand.Tracked(hand, gFrame, keep);
    } else if (step.action == slots::Action::Draw) {
        ++gLostDraws;
        log::Info("holsters: the grab at {} came while the controller was untracked ({:.2f} s): taken at its last place",
            settings.layout.slots[step.slot].name, gLostHand.LostSeconds());
    }
    double const strength = settings.hapticAmplitude;

    auto const name = [&settings](int slot) { return slot >= 0 ? settings.layout.slots[slot].name : "-"; };
    if (step.left >= 0) {
        log::Info("holsters: right hand left {} ({:.2f} m from its center)", name(step.left), input.distance[step.left]);
    }
    if (step.entered >= 0) {
        ++gEntries;
        log::Info("holsters: right hand entered {} ({}, {:.2f} m from its center, radius {:.2f}){}", name(step.entered),
            slots::Name(settings.layout.slots[step.entered].weapon), input.distance[step.entered],
            input.radius[step.entered], Carries(settings.layout.slots[step.entered].weapon) ? "" : ": you don't carry one");
        Pulse(link, slots::EnterEdge(strength));  // the edge; the contact buzz follows below
    }
    switch (step.action) {
    case slots::Action::Draw:
        if (!Draw(*game, step.weapon, name(step.slot), link, strength, gHolds)) {
            gMachine.ForgetDraw();  // refused (the sequels): the release has nothing to send back
        }
        break;
    case slots::Action::Holster:
        Holster(*game, step.weapon, gHolds ? std::format("let go at {}", name(step.slot)) : std::format("released at {}", name(step.slot)),
            link, strength);
        break;
    case slots::Action::Return:
        gToldReturnWaits = false;
        gToldReturnWaitsSwitch = false;
        Holster(*game, step.weapon, gReturnAtSlot ? std::format("let go at {} (once the draw or the burst was over)",
                                                       SlotOf(settings, step.weapon))
                                                  : std::format("let go {:.2f} s ago away from {}", settings.releaseGrace,
                                                       SlotOf(settings, step.weapon)),
            link, strength);
        break;
    case slots::Action::Unavailable:
        ++gRefusals;
#if defined(MEVR_GAME_LE1)
        log::Info("holsters: grab at {}: you carry no {} (its quick slot is empty)", name(step.slot), slots::Name(step.weapon));
#else
        log::Info("holsters: grab at {}: you carry no {}", name(step.slot), slots::Name(step.weapon));
#endif
        Pulse(link, slots::Refused(strength));
        break;
    case slots::Action::SwitchRefused:
        ++gRefusals;
        log::Info("holsters: grab at {}: the {} stays in its holster: a heavy weapon is in hand and a switch would throw it "
                  "away (SFXHeavyWeapon.PutDownWeapon); the game's weapon wheel switches away", name(step.slot),
            slots::Name(step.weapon));
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
        gToldReturnWaitsSwitch = false;
        gReturnAtSlot = step.slot >= 0 && settings.layout.slots[step.slot].weapon == step.weapon;
        log::Info("holsters: let go of the {} {}: it goes back {} unless you squeeze again{}", slots::Name(step.weapon),
            step.slot >= 0 ? std::format("at {}", name(step.slot)) : std::string("away from its slot"),
            gReturnAtSlot ? std::string("at once")
            : settings.releaseGrace >= 0.0 ? std::format("in {:.2f} s", settings.releaseGrace) : std::string("once it's out"),
            input.held == Weapon::None || input.switching ? " (once the draw has finished: the game ignores a holster mid-switch)"
            : input.trigger ? " (once the trigger is let go)" : "");
        break;
    case slots::Action::ReturnCancelled:
        log::Info("holsters: the {} stays out ({})", slots::Name(step.weapon),
            input.newSqueeze ? "squeezed again" : input.held == Weapon::None ? "it's already away, or never came out"
                                                                             : "the gun changed");
        break;
    case slots::Action::None:
        break;
    }
#if defined(MEVR_GAME_LE2)
    StepDeferred(*game, link, strength, deltaSeconds, input.gripDown, input.trigger);
#endif
    if (step.returnWaitsForTrigger && !gToldReturnWaits) {
        gToldReturnWaits = true;
        log::Info("holsters: the {} waits for the trigger to be let go before it goes back (never mid-burst)", slots::Name(Held()));
    }
    if (step.returnWaitsForSwitch && !gToldReturnWaitsSwitch) {
        gToldReturnWaitsSwitch = true;
        log::Info("holsters: the {} waits for the game's weapon switch to finish before it goes back", slots::Name(Held()));
    }
    // The contact buzz, while the hand is in a slot: deeper, stronger.
    if (int const in = gMachine.InSlot(); in >= 0) {
        bool const holding = gHolds && gMachine.CurrentPhase() == slots::Phase::Grab && input.held != Weapon::None;
        Contact(link, slots::Contact(slots::Depth(input.distance[in], input.radius[in]), strength, holding));
    } else {
        Contact(link, {});
    }
    FollowPending(deltaSeconds);
}

void AdjustPad(gamepad::Options& options) {
    if (!gEnabled) {
        return;
    }
    options.rightGripHolds = gHolds;
    options.rbSource = gRbSource;
    options.modPowerHold = gPowerHoldOn;
    options.rightGripFree = !gHolds && gActive && gNear;
    if (gWheelOff) {
        options.leftTriggerShoulder = false;
        options.gripShoulder = false;
    }
}

double EyeHeight() {
    // Grenades use this even when weapon holsters are disabled (UpdateEyeHeight then stands down).
    return body_calibration::Position() == vr::calibration::PlayPosition::Seated
        ? slots::kReferenceEyeHeight : gEyeHeight;
}

std::optional<BodyFrame> SlotFrame() {
    if (!gActive) {
        return std::nullopt;
    }
    BodyFrame frame;
    frame.anchor[0] = gFrame.anchor.x;
    frame.anchor[1] = gFrame.anchor.y;
    frame.anchor[2] = gFrame.anchor.z;
    frame.yaw = gFrame.yaw;
    frame.scale = gScaleNow;
    return frame;
}

void* DefaultGameMode() {
    std::string why;
    std::optional<Game> const game = ReadGame(why);
    return game ? game->mode : nullptr;
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
    for (int i = 0; i < kUsed; ++i) {
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
    // The sequels: where the SMG is and a lost hand kept there (ME1's snapshot unchanged: no SMG slot).
#if defined(MEVR_GAME_LE1)
    std::string const smg;
#else
    std::string const smg = std::format(", \"smgSlot\": {}, \"lostHand\": {{\"holdS\": {}, \"lostFor\": {}}}, \"switchLosesHeld\": {}",
        json::JsonString(slots::Name(settings.layout.smgPlace)), json::JsonNumber(settings.lostHandHoldS),
        json::JsonNumber(gLostHand.LostSeconds()), gCarried.switchLosesHeld ? "true" : "false");
#endif
    return std::format("{{\"enabled\": {}, \"active\": {}, \"why\": {}, \"gripHolds\": {}, \"rbSource\": {}, \"inSlot\": {}, "
                       "\"phase\": {}, \"returnLeft\": {}, \"near\": {}, \"contact\": {}, \"yawSource\": {}, \"yaw\": {}, "
                       "\"anchor\": [{}, {}, {}], \"eyeHeight\": {}, \"eyeHeightSource\": {}, \"floorEyeHeight\": {}, "
                       "\"held\": {}, \"drawn\": {}, \"powerHold\": {{\"on\": {}, \"opened\": {}}}{}, \"slots\": [{}]}}",
        gEnabled ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy), gHolds ? "true" : "false",
        json::JsonString(gamepad::Name(gRbSource)),
        gMachine.InSlot() >= 0 ? json::JsonString(settings.layout.slots[gMachine.InSlot()].name) : std::string("null"),
        json::JsonString(phase), json::JsonNumber(gMachine.ReturnLeft()), gNear ? "true" : "false",
        json::JsonNumber(gContactAmplitude), json::JsonString(gYawSource), json::JsonNumber(gFrame.yaw),
        json::JsonNumber(gFrame.anchor.x), json::JsonNumber(gFrame.anchor.y), json::JsonNumber(gFrame.anchor.z),
        json::JsonNumber(gEyeHeight), json::JsonString(gEyeSource), json::JsonNumber(gFloorNow),
        json::JsonString(slots::Name(Held())),
        gCarried.drawn ? "true" : "false", gPowerHoldOn ? "true" : "false", gPowerOpened ? "true" : "false", smg, list);
}

void LogStats() {
    if (!gEnabled && gEntries == 0) {
        return;
    }
    hand_calibration::Holsters const settings = hand_calibration::GetHolsters();
    std::string where;
    if (gActive) {
        for (int i = 0; i < kUsed; ++i) {
            where += std::format(" {} {} hand {:.2f} m;", settings.layout.slots[i].name, V(gCenters[i] - gFrame.anchor), gDistances[i]);
        }
    }
    // A lost hand kept behind the back (the sequels' lowerBack SMG slot only): said only when it happened.
    std::string const lost = gLostHolds == 0 ? std::string()
        : std::format("; the right controller lost behind the back {} times (kept up to {:.2f} s), {} draws at its kept place",
              gLostHolds, gLostLongest, gLostDraws);
    log::Info("    holsters: {}; {}; yaw from {}; in {}; this period: {} entries, {} draws, {} holsters, {} refusals, {} game draws refused, "
              "nearest {}, strongest contact buzz {:.2f}; eye height {:.2f} m from {}{}{}{}",
        gWhy, gHolds ? std::format("the grip holds the gun ({})", gPowerHoldOn
                                       ? std::format("the right stick click's hold opens the power wheel: {} opened, {} refused",
                                             gPowerOpens, gPowerRefused)
                                       : std::format("RB: {}", gamepad::Name(gRbSource)))
                     : std::string("the grip is RB"),
        gYawSource, gMachine.InSlot() >= 0 ? settings.layout.slots[gMachine.InSlot()].name : "no slot", gEntries, gDraws,
        gHolsters, gRefusals, gRefusedDraws,
        gNearestSlot >= 0 ? std::format("{:.2f} m outside {}", gNearest, settings.layout.slots[gNearestSlot].name) : std::string("-"),
        gContactPeak, gEyeHeight, gEyeSource, gFloorNow >= 0.0 ? std::format(" (head now {:.2f} m above the floor)", gFloorNow) : std::string(),
        where.empty() ? std::string() : std::format("; slots from the anchor:{}", where), lost);
#if defined(MEVR_GAME_LE2)
    if (gCombatHolsters + gDeferredDone + gUndone + gUndoLeft + gUndoDeliberate > 0) {
        log::Info("    holsters: this period {} let-gos put away in a fight (SetActionState(explore)), {} after waiting for a reload, a "
                  "power or a transition; the game's own draws: {} put back, {} left in the hand (drawn again at once), {} let through "
                  "for a power", gCombatHolsters, gDeferredDone, gUndone, gUndoLeft, gUndoDeliberate);
    }
    gCombatHolsters = gDeferredDone = gUndone = gUndoLeft = gUndoDeliberate = 0;
#endif
    gLostHolds = gLostDraws = 0;
    gLostLongest = 0.0;
    gEntries = gDraws = gHolsters = gRefusals = gRefusedDraws = 0;
    gPowerOpens = gPowerRefused = 0;
    gNearest = 1e9;
    gNearestSlot = -1;
    gContactPeak = 0.0f;
}

void Shutdown() {
    gMachine.Reset();
    gAnchor.Reset();
    gLostHand.Reset();
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
    gToldReturnWaitsSwitch = false;
    gReturnAtSlot = false;
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
    gWasDrawnKnown = false;
    gRefusedBuzz = false;
    gStarted = false;
    gWhy = "not started";
    gLoggedWhy.clear();
#if defined(MEVR_GAME_LE1)
    gSwitchToWeapon = gTryHolster = gIsInCombatActionState = gIsSwitchingWeapons = gGetRangedWeaponType = nullptr;
    gEnterPowerWheel = gExitPowerWheel = nullptr;
    gAllowedCallers.fill(nullptr);
    gSeatedWheelCallers.fill(nullptr);
#else
    gSwitchWeapon = gTryHolster = gGetStateName = nullptr;
    gEnterPowerWheel = gExitPowerWheel = nullptr;
    gDrawRoutes.fill(nullptr);
#if defined(MEVR_GAME_LE2)
    gSetActionState = nullptr;
    gToldHolsterState = false;
#elif defined(MEVR_GAME_LE3)
    gSetCurrentWeapon = nullptr;
    gPutAway = false;
    gToldNoPutAway = false;
#endif
#endif
    gPowerHold = {};
    gPowerWatch = {};
    gPowerHoldOn = false;
    gPowerOpened = false;
    gPowerOpens = gPowerRefused = 0;
}

} // namespace mevr::game::holsters
