// SPDX-License-Identifier: MIT
#include "game/Posture.hpp"

#include "core/Log.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Posture.hpp"
#include "core/vr/HostView.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/CoverAcquire.hpp"
#include "engine/GameAdapter.hpp"
#include "core/vr/Controllers.hpp"
#include "game/BodyCalibration.hpp"
#include "game/GamepadMapping.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Player.hpp"
#include "game/PowerWheel.hpp"
#include "game/SquadWeapons.hpp"
#include "game/Traversal.hpp"
#include "game/VrCover.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Shared detector/arbiter; each sequel uses its own native admission (research/le3-locomotion-audit.md).
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
#define MEVR_POSTURE 1
#endif

namespace mevr::game::posture {
namespace {

namespace pz = ik::posture;
namespace slots = ik::body_slots;

// A cover TryExitCover hasn't left after this long is left the game's cover-lost way (LeaveCover).
constexpr double kExitFallbackSeconds = 2.0;
// TryExitCover is asked again this often while it refuses.
constexpr double kExitRetrySeconds = 0.25;
// VR counts as gone (a crouch we made is released) after the host's view hasn't come for this long.
constexpr std::uint64_t kVrGoneMs = 2000;
// The view's height above Shepard is logged this long after a crouch or stand (the camera's blend).
constexpr double kViewProbeSeconds = 0.75;
// The standing eye height is logged again when it has moved this much (meters).
constexpr double kStandingLogStep = 0.02;
// The crouch follows the player ([Hands] crouchFollow, research/23 "The crouch follows you"): the game's change within this
// long of a left stick click that reached the game (ME1's L3, its own crouch toggle) is the button's, kept as ME1 keeps it.
constexpr double kButtonWindowSeconds = 0.5;
// ... and a put-back waits this long after the game's change, so a click the host passed a frame before the mod saw it still
// counts as the button's.
constexpr double kButtonGraceSeconds = 0.15;
// A put-back of the game's change waits while the pawn moves faster than this (units a second): ME2's explore mode stands a
// crouched Shepard up to walk (ExploreCrouched's top speed 0; crouched walking only with a weapon out, CombatCrouched 130 u/s:
// the owner's session 2026-11-18), so he crouches again once he stands still.
constexpr double kStillSpeed = 30.0;

bool gStarted = false;
bool gRefusingCover = false;  // VR on foot with noCover: the player is kept out of the game's cover (game/vr_cover)
std::optional<bool> gNoCoverOverride;  // `mevr cover snap on|off|auto` (tests), until the mod reloads
bool gActive = false;          // VR on foot with a player BioPawn, this tick
std::string gWhy = "not started";
std::string gLoggedWhy;
std::uintptr_t gPawn = 0;      // the player's pawn last seen (compared only)

// Cover.
bool gToldNoHook = false;
std::uint8_t gLoggedRefusal = 0;
unsigned gRefusedTests = 0;    // heartbeat
unsigned gRefusedEntries = 0;  // heartbeat
unsigned gRefusedTotal = 0;
bool gInCover = false;
bool gCoverEpisode = false;
double gCoverSeconds = 0.0;
double gExitRetry = 0.0;
bool gToldFallback = false;
unsigned gExits = 0;

// Crouch.
pz::CrouchDetector gDetector;
pz::CrouchArbiter gArbiter;
bool gCrouchActive = false;
bool gRestorePending = false;
bool gGameWants = false;
bool gGameCrouched = false;
slots::StandingHeight gStanding;  // from the floor
slots::StandingHeight gHeadRef;   // no floor: [Holsters] eyeHeightM + the head's tracking-space height when standing
bool gFloorSeen = false;
std::uint32_t gFloorSessions = 0;
std::uint32_t gFloorRecenters = 0;
std::optional<double> gFraction;
double gEye = -1.0;            // the eyes above the floor (measured or estimated), meters
double gStandingHeight = -1.0;
std::string gSource = "none";
std::string gLoggedSource;
double gLoggedStanding = -1.0;
bool gToldDeferred = false;
unsigned gCrouches = 0;        // heartbeat
unsigned gStands = 0;
unsigned gExternal = 0;
std::string gLastChange = "none";
// A lean is not a crouch ([Hands] crouchLeanRatio): the guard, and whether this lean was logged.
pz::LeanGuard gLean;
bool gLeanTold = false;
unsigned gLeans = 0;           // heartbeat: drops held back as leans
// The crouch follows the player ([Hands] crouchFollow): the clicks seen (the left stick click through L3's gate), when the
// last one reached the game, the reason for the put-back now waiting, and the heartbeat's counts.
gamepad::CrouchClick gClick;
double gLastButton = -1e9;     // seconds (GetTickCount64)
double gChangedAt = -1e9;      // the game's last change of the crouch (seconds): a put-back's button grace counts from it
std::string gRestoreWhy;
bool gToldRestoreWait = false;
unsigned gRestores = 0;
unsigned gInsists = 0;
// The sequels' L3 (game::gamepad::Options::modCrouchClick; [Hands] crouchClick): the mod owns the click this tick, and its
// toggles.
bool gClickOwned = false;
unsigned gClickToggles = 0;
unsigned gClickRefused = 0;

// The view's height above Shepard around a change.
struct ViewProbe {
    bool active = false;
    double age = 0.0;
    double before = 0.0;
    bool haveBefore = false;
    char const* what = "";
};
ViewProbe gProbe;

void SetWhy(std::string why) {
    gWhy = std::move(why);
    if (gWhy != gLoggedWhy) {
        gLoggedWhy = gWhy;
        log::Info("posture: {}", gWhy);
    }
}

#if defined(MEVR_POSTURE)

// Our reflection layer (ue3/, research/34): each found once, by name, in the running game (ME1 and ME2: kEveryGame).
ue3::ClassRef gBioPlayerController{"SFXGame.BioPlayerController", ue3::kEveryGame};
ue3::ClassRef gBioPawn{"SFXGame.BioPawn", ue3::kEveryGame};
ue3::ClassRef gGameModeDefault{"SFXGame.SFXGameModeDefault", ue3::kEveryGame};
ue3::FieldRef gPawnField{"Engine.Controller", "Pawn", ue3::kEveryGame};
ue3::FieldRef gGameModeManager{"SFXGame.BioPlayerController", engine::adapter::kGameModeManager, ue3::kEveryGame};
ue3::FieldRef gGameModes{"SFXGame.SFXGameModeManager", "GameModes", ue3::kEveryGame};
#if defined(MEVR_GAME_LE3)
ue3::FieldRef gStorming{"SFXGame.BioPawn", "bStorming", ue3::kEveryGame};
#else
ue3::FieldRef gStorming{"SFXGame.BioPlayerController", "bStorming", ue3::kEveryGame};
#endif
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType", ue3::kEveryGame};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction", ue3::kEveryGame};
ue3::FieldRef gLocation{"Engine.Actor", "Location", ue3::kEveryGame};
ue3::FieldRef gIsCrouched{"Engine.Pawn", "bIsCrouched", ue3::kEveryGame};
ue3::FieldRef gVelocity{"Engine.Actor", "Velocity", ue3::kEveryGame};  // a put-back waits while the pawn moves
ue3::FieldRef gWantsToCrouch{"Engine.Pawn", "bWantsToCrouch", ue3::kEveryGame};

#if defined(MEVR_GAME_LE3)
ue3::FunctionRef gLe3FindCover{"SFXGame.SFXGameModeDefault.FindCover", ue3::kEveryGame};
ue3::FunctionRef gLe3AcquireCover{"SFXGame.SFXGameModeDefault.TryAcquireCover", ue3::kEveryGame};
bool gLe3NoSnapReady = false;
bool gLe3NoCover = true;  // this tick's [Hands] noCover (or `mevr cover snap`): FindCover's answer inside TryAcquireCover
// The player's SFXGameModeDefault while its cover acquisition is refused (game thread): set each tick by the one rule
// (ik::posture::RefuseCoverSnap), kept until the next, as ME2's gRefusedController.
ue3::Object* gLe3RefusedMode = nullptr;
unsigned gLe3Looked = 0;                // FindCover answers inside TryAcquireCover that had found nothing anyway (heartbeat)
std::uint64_t gLe3LastRefusalMs = 0;
std::uint64_t gLe3LastRefusalLogMs = 0;
bool gLe3ToldRefusal = false;
ue3::ClassRef gCoverLinkClass{"Engine.CoverLink", ue3::kEveryGame};  // `mevr cover links`
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo", ue3::kEveryGame};
Link gLink;
std::uint64_t gLinkTime = 0, gGeneration = 0;
ue3::Object* gWorld = nullptr;
#endif

// The crouch: ME1's BioPawn.ShouldCrouch also resets the mount attempt and the movement animation state (research/23
// section 3); ME2 has no override, so the engine's Pawn.ShouldCrouch (bWantsToCrouch, what ME2's BioPawn.SetShouldCrouch
// and its cover and AI call; research/23 section 7).
#if defined(MEVR_GAME_LE1)
constexpr char const* kShouldCrouchPath = "SFXGame.BioPawn.ShouldCrouch";
#else
constexpr char const* kShouldCrouchPath = "Engine.Pawn.ShouldCrouch";
#endif

ue3::Function* gShouldCrouch = nullptr;
ue3::Function* gTryExitCover = nullptr;
ue3::Function* gIsInCover = nullptr;
ue3::Function* gLeaveCover = nullptr;
bool gToldNoCrouchParam = false;

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// The HUD's "take cover" prompt. ME2: BioPlayerController::UpdateCover (0xACE2A0, the controller's native tick) finds cover
// through FindPlayerCover's implementation (a C++ call the refusal never sees) and ProcessEvents the GUI manager's
// ShowCoverWidget or HideCoverWidget each tick. ME3: the same event on its GUI interaction, for a player controller
// (SFXGUIInteraction.ShowCoverWidget(oPlayerController) / HideCoverWidget(oPlayerController)). While the snapping is refused, a
// ShowCoverWidget is followed at once by the game's own HideCoverWidget (a ProcessEvent post-handler: before anything is drawn),
// so no prompt offers what A won't do. The climb/mantle prompt (ShowClimbMantleWidget, shown only in cover) is untouched.
#if defined(MEVR_GAME_LE2)
ue3::FunctionRef gHideCoverWidgetRef{"SFXGame.MassEffectGuiManager.HideCoverWidget", ue3::kEveryGame};
#else
ue3::FunctionRef gShowCoverWidgetRef{"SFXGame.SFXGUIInteraction.ShowCoverWidget", ue3::kEveryGame};
ue3::FunctionRef gHideCoverWidgetRef{"SFXGame.SFXGUIInteraction.HideCoverWidget", ue3::kEveryGame};
ue3::FieldRef gShowCoverWidgetPc{gShowCoverWidgetRef, "oPlayerController"};
#endif
unsigned gPromptsShown = 0;   // ShowCoverWidget calls seen (heartbeat)
unsigned gPromptsHidden = 0;  // ... of which hidden again
std::uint64_t gPromptsHiddenTotal = 0;
bool gToldPrompt = false;
bool gPromptHandlerAdded = false;
#endif

#if defined(MEVR_GAME_LE2)
// No cover snapping on ME2 (research/23 section 7; the owner, 2026-11-14: "We won't snap to cover for VR"). ME2 takes cover
// only through BioPlayerController.TryAcquireCover (the A button's exec through ProcessEvent, and CheckStormToCover from a
// sprint through CallFunction), which first asks the native FindPlayerCover; that native is called from script nowhere else.
// Its answer inside TryAcquireCover is made false for the player's controller, so TryAcquireCover returns FALSE at once:
// no snap, no CancelReload, no explore-to-combat switch, and the rest of A (use, lane E's out-of-cover mantle) untouched.
// A native caller of FindPlayerCover's implementation (the HUD's cover prompt, BioPlayerController::UpdateCover) and any
// call through ProcessEvent are not seen: only script calls pass through CallFunction.
ue3::FunctionRef gFindPlayerCoverRef{"SFXGame.BioPlayerController.FindPlayerCover", ue3::kEveryGame};
ue3::FunctionRef gTryAcquireCoverRef{"SFXGame.BioPlayerController.TryAcquireCover", ue3::kEveryGame};
ue3::FunctionRef gInAnimatedTransitionRef{"SFXGame.BioPawn.IsInAnimatedTransition", ue3::kEveryGame};
ue3::FieldRef gCurrentLink{"SFXGame.BioPawn", "CurrentLink", ue3::kEveryGame};
ue3::FieldRef gCurrentSlotIdx{"SFXGame.BioPawn", "CurrentSlotIdx", ue3::kEveryGame};
ue3::FieldRef gEnableStormToCover{"SFXGame.BioPlayerController", "m_bEnableStormToCover", ue3::kEveryGame};
ue3::ClassRef gCoverLinkClass{"Engine.CoverLink", ue3::kEveryGame};
ue3::Object* gRefusedController = nullptr;  // the player's controller while its cover acquisition is refused (game thread)
bool gRefusalHandlerAdded = false;
unsigned gAcquireLooked = 0;       // FindPlayerCover answers inside TryAcquireCover that had found nothing anyway (heartbeat)
std::uint64_t gLastRefusalMs = 0;
std::uint64_t gLastRefusalLogMs = 0;
bool gToldRefusal = false;

// BioPlayerController.FindPlayerCover returned (a script call): inside TryAcquireCover for the player's controller, its
// "found" is made false. One TryAcquireCover asks twice (FindPlayerCover(FoundCover) || FindPlayerCover(FoundCover, TRUE)).
void OnFindPlayerCover(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* caller, std::uint8_t* /*callerLocals*/,
    void* result) {
    ue3::Object* const refused = gRefusedController;
    if (refused == nullptr || context != refused || result == nullptr || caller == nullptr || caller != gTryAcquireCoverRef.Get()) {
        return;
    }
    auto* const found = static_cast<std::uint32_t*>(result);
    if (*found == 0) {
        ++gAcquireLooked;
        return;
    }
    *found = 0;
    write_journal::Wrote("Posture.findPlayerCover", found, sizeof(*found));
    ++gRefusedTests;
    std::uint64_t const now = GetTickCount64();
    if (now - gLastRefusalMs <= 250) {
        return;  // the second ask of the same press
    }
    gLastRefusalMs = now;
    ++gRefusedEntries;
    ++gRefusedTotal;
    // A sprint against cover asks every 0.3 s (CheckStormToCover): logged at most every 2 s, the heartbeat counts the rest.
    if (now - gLastRefusalLogMs > 2000) {
        gLastRefusalLogMs = now;
        ue3::Object* const pawn = gPawnField.Read<ue3::Object*>(context).value_or(nullptr);
        std::optional<ue3::Vector> const at = pawn != nullptr ? gLocation.Read<ue3::Vector>(pawn) : std::nullopt;
        log::Info("posture: refused a cover entry: BioPlayerController.FindPlayerCover found cover for TryAcquireCover (the A "
                  "button, or a sprint into cover), answered false ([Hands] noCover: no snapping in VR); Shepard stays at "
                  "({:.0f}, {:.0f}, {:.0f}){}",
            at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f,
            gToldRefusal ? "" : "; A's use and the out-of-cover mantle are untouched");
        gToldRefusal = true;
    }
}
#endif

struct Game {
    ue3::Object* controller = nullptr;  // a BioPlayerController
    ue3::Object* pawn = nullptr;        // a BioPawn
    ue3::Object* mode = nullptr;        // its SFXGameModeDefault
};

std::uint8_t CoverType(ue3::Object const* pawn) {
    return gCoverType.Read<std::uint8_t>(pawn).value_or(0);
}

bool WantsToCrouch(ue3::Object const* pawn) {
    return gWantsToCrouch.ReadBool(pawn).value_or(false);
}

std::optional<Game> ReadGame(std::string& why) {
    if (!ue3::runtime::Up()) {
        why = "our reflection layer is off (see the 'reflect:' log lines)";
        return std::nullopt;
    }
    ue3::Object* const pc = ue3::runtime::LocalPlayerController();
    if (pc == nullptr || !ue3::runtime::IsAlive(pc) || !ue3::IsA(pc, gBioPlayerController)) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = pc;
    ue3::Object* const pawn = gPawnField.Read<ue3::Object*>(pc).value_or(nullptr);
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn) || !ue3::IsA(pawn, gBioPawn)) {
        why = "no player pawn (the Mako or a cutscene?)";
        return std::nullopt;
    }
    game.pawn = pawn;
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
    return game;
}

#if defined(MEVR_GAME_LE3)
// Native admission is re-read for every request. Mode 0 alone includes authored actions.
bool NativeMovement(Game const& game) {
    namespace admit = player::admission;  // the one admission read (game/Player): on foot, no authored action, walking, alive
    return player::MovementAdmitted(game.controller, game.pawn,
        admit::kOnFoot | admit::kNoAction | admit::kWalking | admit::kMoveInput | admit::kAlive);
}
bool HostMovement() {
    auto const now = GetTickCount64();
    return gLinkTime && now >= gLinkTime && now - gLinkTime <= 100 &&
        gLink.runtimeOwned && gLink.focused && !gLink.menuOpen && !gLink.calibrating &&
        gLink.sceneGeneration && gLink.controlsAllowed && gLink.controlsAllowed();
}
// SFXGameModeDefault.FindCover returned (a script call): inside the player's mode's TryAcquireCover while the one rule refuses
// the snapping (TickCover's gLe3RefusedMode), its "found" is made false. No other gate: the host's controls, a wheel, a wrist
// screen, a custom action's end or a late tick never let a press through (they had, 2026-11-17's review).
void OnLe3FindCover(ue3::Object* context, ue3::Function*, ue3::Function* caller, std::uint8_t*, void* result) {
    ue3::Object* const refused = gLe3RefusedMode;
    if (refused == nullptr || context != refused || caller == nullptr || caller != gLe3AcquireCover.Get() || !result) return;
    auto* const found = static_cast<std::uint32_t*>(result);
    if (*found == 0) { ++gLe3Looked; return; }
    *found = 0;
    write_journal::Wrote("Posture.LE3.FindCover", found);
    ++gRefusedTests;
    std::uint64_t const now = GetTickCount64();
    if (now - gLe3LastRefusalMs <= 250) return;  // the same press asking again (bWideCheck)
    gLe3LastRefusalMs = now;
    ++gRefusedEntries;
    ++gRefusedTotal;
    if (now - gLe3LastRefusalLogMs > 2000) {  // a sprint against cover asks again and again: the heartbeat counts the rest
        gLe3LastRefusalLogMs = now;
        std::string why;
        auto const game = ReadGame(why);
        std::optional<ue3::Vector> const at = game ? gLocation.Read<ue3::Vector>(game->pawn) : std::nullopt;
        log::Info("posture: refused a cover entry: SFXGameModeDefault.FindCover found cover for TryAcquireCover (A, or a sprint "
                  "into cover), answered false ([Hands] noCover: no snapping in VR); Shepard stays at ({:.0f}, {:.0f}, {:.0f}){}",
            at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f,
            gLe3ToldRefusal ? "" : "; A's other uses untouched, cover already taken is left to the game");
        gLe3ToldRefusal = true;
    }
}
#endif

// Through ProcessEvent with the function's own parameter block.
bool CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

void CallVoid(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
}

// ME1's BioPawn.ShouldCrouch: Pawn.ShouldCrouch sets bWantsToCrouch (the engine crouches or stands on its next move,
// standing only where there's room), then BioPawn resets its movement animation state. ME2: Pawn.ShouldCrouch itself.
bool ShouldCrouch(ue3::Object* pawn, bool crouch) {
#if defined(MEVR_GAME_LE3)
    std::string why;
    auto const game = ReadGame(why);
    if (!game || game->pawn != pawn || gWorldInfo.Read<ue3::Object*>(game->controller).value_or(nullptr) != gWorld || !NativeMovement(*game) ||
        gCoverType.Read<std::uint8_t>(pawn).value_or(255) != 0 ||
        gStorming.ReadBool(pawn).value_or(true)) return false;
    if (crouch && !HostMovement()) return false;
#endif
    ue3::Params parms(ue3::runtime::Get(), gShouldCrouch);
    if (!parms.SetBool("bCrouch", crouch)) {
        if (!gToldNoCrouchParam) {
            gToldNoCrouchParam = true;
            log::Warn("posture: {} has no bool parameter bCrouch: not called", kShouldCrouchPath);
        }
        return false;
    }
    if (!ue3::runtime::Call(pawn, gShouldCrouch, parms)) return false;
    return gWantsToCrouch.ReadBool(pawn) == crouch;
}

bool InCover(ue3::Object* pawn) {
    return gIsInCover != nullptr ? CallBool(pawn, gIsInCover)
                                 : CoverType(pawn) != 0 && gCoverAction.Read<std::uint8_t>(pawn).value_or(0) != 13;
}

// SFXGameModeDefault.TryExitCover, A's own exit. ME2's takes bForceExit (research/le2-movement-source.md): forced, or it
// leaves only with the move stick pushed during a lean or peek.
void ExitCover(ue3::Object* mode) {
    ue3::Params parms(ue3::runtime::Get(), gTryExitCover);
#if defined(MEVR_GAME_LE2)
    parms.SetBool("bForceExit", true);
#endif
    ue3::runtime::Call(mode, gTryExitCover, parms);
}

char const* CoverName(int type) {
    return type == 1 ? "high (standing)" : type == 2 ? "low (crouching)" : "no";
}

// The view's height above Shepard's origin (units), if there's a view.
std::optional<double> ViewAbovePawn(ue3::Object* pawn) {
    auto const frame = view_anchor::Current();
    std::optional<ue3::Vector> const location = gLocation.Read<ue3::Vector>(pawn);
    if (!frame || !location) {
        return std::nullopt;
    }
    return frame->headWorld.z - location->z;
}

void StartProbe(ue3::Object* pawn, char const* what) {
    auto const before = ViewAbovePawn(pawn);
    gProbe = {true, 0.0, before.value_or(0.0), before.has_value(), what};
}

void FollowProbe(ue3::Object* pawn, double deltaSeconds) {
    if (!gProbe.active) {
        return;
    }
    gProbe.age += deltaSeconds;
    if (gProbe.age < kViewProbeSeconds) {
        return;
    }
    gProbe.active = false;
    auto const after = ViewAbovePawn(pawn);
    vr::host_view::State const view = vr::host_view::Latest();
    if (after && gProbe.haveBefore) {
        log::Info("posture: {:.2f} s after the {}: the view is {:.1f} units above Shepard's origin (was {:.1f}: {:+.1f}); "
                  "Halcyon's lean now {:+.1f} units up, Shepard crouched {} [verify: the game's crouch camera]",
            gProbe.age, gProbe.what, *after, gProbe.before, *after - gProbe.before, view.offsetUp, gIsCrouched.ReadBool(pawn).value_or(false) ? "yes" : "no");
    }
}

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// ShowCoverWidget ran (ProcessEvent; ME2's MassEffectGuiManager, ME3's SFXGUIInteraction): hidden again while the snapping is
// refused.
void OnShowCoverWidget(ue3::Object* context, ue3::Function* /*function*/, void* parms, void* /*result*/) {
    ++gPromptsShown;
    ue3::Function* const hide = gHideCoverWidgetRef.Get();
    if (!gRefusingCover || context == nullptr || hide == nullptr) {
        return;
    }
#if defined(MEVR_GAME_LE3)
    // ME3 shows it for a player controller (its parameter) and hides it the same way: only the local player's.
    ue3::Object* const pc = parms != nullptr ? gShowCoverWidgetPc.Read<ue3::Object*>(parms).value_or(nullptr) : nullptr;
    if (pc == nullptr || pc != ue3::runtime::LocalPlayerController()) {
        return;
    }
    ue3::Params hideParms(ue3::runtime::Get(), hide);
    if (!hideParms.Set("oPlayerController", pc) || !ue3::runtime::Call(context, hide, hideParms)) {
        return;
    }
#else
    (void)parms;
    CallVoid(context, hide);
#endif
    ++gPromptsHidden;
    ++gPromptsHiddenTotal;
    if (!gToldPrompt) {
        gToldPrompt = true;
        log::Info("posture: the HUD's cover prompt (the game's ShowCoverWidget) is hidden again with its own HideCoverWidget while "
                  "the snapping is refused ({})", ue3::runtime::Get().FullName(context));
    }
}
#endif

// The cover side, each tick with a game. `refuse`: the one rule (ik::posture::RefuseCoverSnap) says the snapping is refused.
void TickCover(Game const& game, bool refuse, double deltaSeconds) {
#if defined(MEVR_GAME_LE3)
    (void)deltaSeconds;
    // No cover snapping in VR, ME1's and ME2's [Hands] noCover (the owner, 2026-11-14: "We won't snap to cover for VR"):
    // FindCover answered false inside the player's mode's TryAcquireCover (OnLe3FindCover, a CallFunction handler: Mod.cpp
    // puts the hook in while RefusingCover()). VR cover (game/vr_cover, Le3Cover.inc) judges the player's cover meanwhile.
    gRefusingCover = refuse && gLe3NoSnapReady;
    gLe3RefusedMode = gRefusingCover ? game.mode : nullptr;
    gInCover = InCover(game.pawn);
    return; // Existing native cover is never forcibly exited; acquire refusal acts before new entry.
#endif
    gRefusingCover = refuse;  // kept out of cover either way: refused, or left at once without the hook
#if defined(MEVR_GAME_LE2)
    // ME2: TryAcquireCover's FindPlayerCover answered false (a script call: the CallFunction hook, which Mod.cpp puts in while
    // RefusingCover()).
    bool const hooked = gRefusalHandlerAdded && engine::call_function::Installed();
    gRefusedController = refuse && hooked ? game.controller : nullptr;
    if (refuse && !gRefusalHandlerAdded && !gToldNoHook) {
        gToldNoHook = true;
        log::Warn("posture: BioPlayerController.FindPlayerCover or TryAcquireCover wasn't found: the game still takes cover on A, "
                  "and Shepard leaves it at once");
    }
#else
    bool const hooked = refuse && engine::cover_acquire::Installed();
    engine::cover_acquire::Refuse(hooked ? game.pawn : nullptr);
    if (!hooked) {
        gLoggedRefusal = 0;
    }
    if (refuse && !engine::cover_acquire::Installed() && !gToldNoHook) {
        gToldNoHook = true;
        log::Warn("posture: the cover test hook isn't in: the game still takes cover, and Shepard leaves it at once");
    }
    engine::cover_acquire::Stats const stats = engine::cover_acquire::Take();
    gRefusedTests += stats.refused;
    if (stats.refused > 0 && stats.lastType != gLoggedRefusal) {
        gLoggedRefusal = stats.lastType;
        if (stats.lastType != 0) {
            ++gRefusedEntries;
            ++gRefusedTotal;
            log::Info("posture: refused a cover entry: {} cover ahead, the move {:.0f} degrees off its normal (noCover; the "
                      "game would have snapped Shepard into it)", CoverName(stats.lastType), stats.lastAngle);
        }
    }
#endif

    gInCover = InCover(game.pawn);
    if (!gInCover) {
        if (gCoverEpisode) {
            log::Info("posture: Shepard is out of cover after {:.2f} s", gCoverSeconds);
            gCoverEpisode = false;
        }
        return;
    }
    if (!refuse) {
        return;
    }
#if defined(MEVR_GAME_LE2)
    if (CallBool(game.pawn, gInAnimatedTransitionRef.Get())) {
        return;  // a cover move under way: left once it has ended
    }
#endif
    if (!gCoverEpisode) {
        gCoverEpisode = true;
        gCoverSeconds = 0.0;
        gExitRetry = 0.0;
        gToldFallback = false;
        log::Info("posture: Shepard is in {} cover (taken before noCover applied, or by a script): leaving it as A does "
                  "(SFXGameModeDefault.TryExitCover)", CoverName(CoverType(game.pawn)));
    }
    gCoverSeconds += deltaSeconds;
    gExitRetry -= deltaSeconds;
    if (gExitRetry > 0.0) {
        return;
    }
    gExitRetry = kExitRetrySeconds;
    ++gExits;
    if (gCoverSeconds < kExitFallbackSeconds && game.mode != nullptr && gTryExitCover != nullptr) {
        ExitCover(game.mode);  // A's own exit: push off the wall, then LeaveCover
    } else if (gLeaveCover != nullptr) {
        if (!gToldFallback) {
            gToldFallback = true;
            log::Info("posture: TryExitCover didn't leave cover within {:.1f} s (an animated transition, the scope, or no game "
                      "mode): BioPawn.LeaveCover, the game's own cover-lost path", kExitFallbackSeconds);
        }
        CallVoid(game.pawn, gLeaveCover);
    }
}

// The crouch thresholds: [Hands] crouchBelow / standAbove, or with the guided calibration's standing and crouched
// eye heights both measured, a threshold between them (research/25; the dwell stays [Hands]').
// With [Hands] crouchMinDropM the crouch is at least that far under the live standing height (research/23 "A lean is not a
// crouch"), never below the calibrated crouch.
pz::CrouchParams CrouchParamsNow() {
    hand_calibration::Posture const settings = hand_calibration::GetPosture();
    pz::CrouchParams const base = settings.crouch;
    pz::CrouchParams const params = body_calibration::CrouchParams(base).value_or(base);
    return pz::WithMinimumDrop(params, gStandingHeight, settings.crouchMinDropM, body_calibration::CrouchedEyeFraction());
}

// The eyes' height as a fraction of the standing eye height, and where it came from.
std::optional<double> Measure(Link const& link, vr::host_view::State const& view, double deltaSeconds) {
    if (body_calibration::Position() == vr::calibration::PlayPosition::Seated) {
        gStanding.Reset();
        gHeadRef.Reset();
        gFloorSeen = false;
        gFraction.reset();
        gEye = gStandingHeight = -1.0;
        gDetector.Reset();
        gSource = "seated: physical crouch disabled (L3 remains available)";
        return std::nullopt; // a chair must never teach the standing detector
    }
    double const assumed = hand_calibration::GetHolsters().layout.eyeHeight;
    double const headY = view.head.position.y;
    bool recentered = false;
    if (link.floor) {
        vr::floor::State const& floor = *link.floor;
        if (!gFloorSeen || floor.sessions != gFloorSessions) {
            gStanding.Reset();  // a new session (or the first look): its floor may differ
            gHeadRef.Reset();
            gLean.Reset();
            gFloorSessions = floor.sessions;
            gFloorRecenters = floor.recenters;
            gFloorSeen = true;
        }
        if (floor.recenters != gFloorRecenters) {
            gFloorRecenters = floor.recenters;
            recentered = true;
            gLean.Reset();  // the room re-origined: where the head stood is gone
        }
        if (floor.valid) {
            if (recentered) {
                gStanding.Recenter(floor.eyeHeight);
            } else {
                gStanding.Update(floor.eyeHeight, deltaSeconds);
            }
        }
    }
    // The fallback's reference: the head's tracking-space height when standing, as an eye height over the
    // assumed standing one. A recenter re-origins Halcyon's space at the head (xr_session DoRecenter), so
    // the head stood at height 0 then, whichever frame the view we read was built in.
    if (recentered) {
        gHeadRef.Recenter(assumed);
    } else {
        gHeadRef.Update(assumed + headY, deltaSeconds);
    }
    std::optional<double> fraction;
    std::string source;
    if (link.floor && link.floor->valid && gStanding.Valid()) {
        gStandingHeight = gStanding.Value();
        gEye = link.floor->eyeHeight;
        fraction = pz::FractionFromFloor(gEye, gStandingHeight);
        source = std::format("the floor ({})", vr::floor::Name(link.floor->source));
    } else if (gHeadRef.Valid()) {
        gStandingHeight = assumed;
        gEye = assumed + headY - (gHeadRef.Value() - assumed);
        fraction = pz::FractionFromRecenter(headY, gHeadRef.Value() - assumed, assumed);
        source = std::format("the head's drop since the recenter over [Holsters] eyeHeightM ({})",
            !link.floor ? "the host has no floor: services older than v10; redeploy the host"
            : link.floor->source == vr::floor::Source::None ? "the runtime offers no floor space"
                                                             : "no floor sample yet");
    } else {
        gStandingHeight = -1.0;
        gEye = -1.0;
        source = "none (no head height yet)";
    }
    gSource = source;
    if (source != gLoggedSource || (gStandingHeight > 0.0 && std::abs(gStandingHeight - gLoggedStanding) >= kStandingLogStep)) {
        gLoggedSource = source;
        gLoggedStanding = gStandingHeight;
        hand_calibration::Posture const settings = hand_calibration::GetPosture();
        log::Info("posture: standing eye height {:.2f} m, from {}; Shepard crouches below {:.2f} m and stands above {:.2f} m{}{}{}",
            gStandingHeight, source, gStandingHeight * CrouchParamsNow().crouchBelow, gStandingHeight * CrouchParamsNow().standAbove,
            body_calibration::CrouchParams(settings.crouch) ? " (between your calibrated heights)" : "",
            settings.crouchMinDropM > 0.0 ? std::format(", at least {:.2f} m under standing ([Hands] crouchMinDropM)", settings.crouchMinDropM)
                                          : std::string(),
            settings.crouchLeanRatio > 0.0
                ? std::format("; a drop that takes the head {:.1f} times as far forward is a lean ([Hands] crouchLeanRatio)", settings.crouchLeanRatio)
                : std::string());
    }
    gFraction = fraction;
    return fraction;
}

void Ask(Game const& game, bool crouch, std::string const& why) {
    StartProbe(game.pawn, crouch ? "crouch" : "stand");
    bool const accepted = ShouldCrouch(game.pawn, crouch);
    gGameWants = WantsToCrouch(game.pawn);
    gArbiter.Applied(gGameWants);
    gRestorePending = !crouch && !accepted && gArbiter.Owns();
    gLastChange = std::format("{} {} ({})", crouch ? "crouch" : "stand", accepted ? "accepted" : "refused", why);
    log::Info("posture: {} request {} ({}): {}({}); bWantsToCrouch={}", crouch ? "crouch" : "stand",
        accepted ? "accepted" : "refused", why, kShouldCrouchPath, crouch ? "true" : "false", gGameWants);
}

// The crouch side, each tick with a game.
void TickCrouch(Game const& game, Link const& link, vr::host_view::State const& view, bool active, bool enabled,
    double deltaSeconds) {
    gGameWants = WantsToCrouch(game.pawn);
    gGameCrouched = gIsCrouched.ReadBool(game.pawn).value_or(false);
    FollowProbe(game.pawn, deltaSeconds);
    vr::host_view::State const raw = vr::host_view::Latest(~0ull);
    bool const vrGone = raw.timeMs == 0 || GetTickCount64() - raw.timeMs > kVrGoneMs;
    if (!active || !enabled || gShouldCrouch == nullptr) {
        if (gCrouchActive) {
            gCrouchActive = false;
            gToldDeferred = false;
        }
        // A crouch we made goes when the setting does or VR leaves; menus and conversations keep it.
        if (gArbiter.Owns() && gGameWants && (!enabled || vrGone) && gShouldCrouch != nullptr) {
            Ask(game, false, !enabled ? "physicalCrouch is off" : "VR is gone");
        }
        gArbiter.Observe(gGameWants);
        if (view.valid) {
            Measure(link, view, deltaSeconds);  // keep learning the standing height
        }
        return;
    }
    std::optional<double> const fraction = Measure(link, view, deltaSeconds);
    pz::CrouchParams const params = CrouchParamsNow();
    if (!gCrouchActive) {
        gCrouchActive = true;
        gDetector.Reset(gArbiter.Owns() && gGameWants);  // a crouch of ours stands back up if the player did
    }
    hand_calibration::Posture const settings = hand_calibration::GetPosture();
    // A lean is not a crouch ([Hands] crouchLeanRatio): while this drop took the head forward that many times as far as down,
    // the detector sees the threshold itself (not past it), so the crouch's dwell starts over; standing up is never held back.
    std::optional<double> sample = fraction;
    bool const lean = gLean.Update({view.head.position.x, view.head.position.z, fraction, gStandingHeight}, params,
        settings.crouchLeanRatio);
    if (lean && !gDetector.Crouched() && fraction && *fraction < params.crouchBelow) {
        sample = params.crouchBelow;
        if (!gLeanTold) {
            gLeanTold = true;
            ++gLeans;
            log::Info("posture: a lean, not a crouch: eyes {:.2f} m = {:.2f} of the standing {:.2f} m (below {:.2f}), but the head went "
                      "{:.2f} m forward for {:.2f} m down ([Hands] crouchLeanRatio {:.1f}): Shepard stays standing",
                gEye, *fraction, gStandingHeight, params.crouchBelow, gLean.Horizontal(), gLean.Drop(), settings.crouchLeanRatio);
        }
    } else if (!lean) {
        gLeanTold = false;
    }
    pz::CrouchEvent const event = gDetector.Update(sample, deltaSeconds, params);
    std::string physical;
    if (event != pz::CrouchEvent::None) {
        physical = std::format("eyes {:.2f} m = {:.2f} of the standing {:.2f} m, {} for {:.2f} s", gEye, fraction.value_or(-1.0),
            gStandingHeight, event == pz::CrouchEvent::Crouch ? std::format("below {:.2f}", params.crouchBelow)
                                                             : std::format("above {:.2f}", params.standAbove),
            params.dwellSeconds);
        log::Info("posture: physical {}: {}", event == pz::CrouchEvent::Crouch ? "crouch" : "stand", physical);
    }
    pz::ArbiterInput input;
    input.physical = event;
    input.gameWants = gGameWants;
#if defined(MEVR_GAME_LE3)
    bool const storming = gStorming.ReadBool(game.pawn) != false;
#else
    bool const storming = gStorming.ReadBool(game.controller).value_or(false);
#endif
    input.canApply = !storming;  // ToggleCrouch refuses while sprinting too
    // The crouch follows the player ([Hands] crouchFollow): the game's passing reasons for a change, seen this tick.
    std::optional<ue3::Vector> const velocity = gVelocity.Read<ue3::Vector>(game.pawn);
    double const speed = velocity ? std::hypot(static_cast<double>(velocity->x), static_cast<double>(velocity->y)) : 0.0;
    bool const vaulting = traversal::Moving();
    double const now = static_cast<double>(GetTickCount64()) / 1000.0;
    if (std::optional<bool> const seen = gArbiter.LastSeen(); seen && *seen != gGameWants) {
        gChangedAt = now;  // the game changed it this tick
    }
    input.follow = settings.crouchFollow;
    input.physicalCrouched = gDetector.Crouched();
    input.button = now - gLastButton < kButtonWindowSeconds;
    input.canRestore = !vaulting && speed < kStillSpeed && !gInCover && now - gChangedAt >= kButtonGraceSeconds;
    input.explained = vaulting || storming || speed >= kStillSpeed;
    input.nowSeconds = now;
    std::string const reason = vaulting ? std::string("a vault or climb")
        : storming                      ? std::string("a sprint")
        : speed >= kStillSpeed          ? std::format("walking, {:.0f} u/s", speed)
        : gInCover                      ? std::string("cover")
                                        : std::string("no walk, sprint or vault seen: a script or a rule of the game's");
    pz::ArbiterStep const step = gArbiter.Update(input);
    if (gArbiter.Restoring() && gLastButton >= gChangedAt - kButtonWindowSeconds) {
        // A click seen just after the change it made (the host passed it a frame earlier): the button's after all.
        gArbiter.Pressed(gGameWants);
        log::Info("posture: Shepard's crouch changed by L3: now {}; kept until the next physical crouch or stand",
            gGameWants ? "crouching" : "standing");
        return;
    }
    if (step.external) {
        ++gExternal;
        if (step.restoring) {
            ++gRestores;
            gRestoreWhy = reason;
            gToldRestoreWait = false;
            log::Info("posture: the game {} while you {} ({}): {} again {} ([Hands] crouchFollow)",
                gGameWants ? "crouched Shepard" : "stood Shepard up", input.physicalCrouched ? "are crouched" : "stand", reason,
                input.physicalCrouched ? "he crouches" : "he stands",
                vaulting ? "after the vault" : storming ? "after the sprint" : speed >= kStillSpeed ? "once he stands still"
                    : gInCover ? "out of cover" : "in a moment");
        } else if (step.insisted) {
            ++gInsists;
            log::Info("posture: the game {} again soon after we put him back ({}): it wins here; your posture is followed again "
                      "from your next crouch or stand", gGameWants ? "crouched Shepard" : "stood Shepard up", reason);
        } else if (input.button) {
            log::Info("posture: Shepard's crouch changed by L3: now {}; kept until the next physical crouch or stand",
                gGameWants ? "crouching" : "standing");
        } else {
            log::Info("posture: Shepard's crouch changed without us (L3, sprinting or a script): now {}; kept until the next "
                      "physical crouch or stand{}", gGameWants ? "crouching" : "standing",
                settings.crouchFollow ? " (it agrees with your posture)" : " ([Hands] crouchFollow = 0)");
        }
    }
    if (step.alreadyThere) {
        log::Info("posture: Shepard is already {} (L3 or the game got there first): that posture follows yours from now on",
            gGameWants ? "crouching" : "standing");
    }
    if (step.deferred && !gToldDeferred) {
        gToldDeferred = true;
        if (gArbiter.Restoring()) {
            log::Info("posture: putting back the {} waits ({})", gArbiter.Pending().value_or(false) ? "crouch" : "stand", reason);
        } else {
            log::Info("posture: the physical {} waits: Shepard is sprinting", gArbiter.Pending().value_or(false) ? "crouch" : "stand");
        }
    }
    if (!step.deferred) {
        gToldDeferred = false;
    }
    if (step.gaveUp) {
        log::Warn("posture: the game didn't take the {} after {} asks; dropped until the next physical change",
            gGameWants ? "stand" : "crouch", pz::CrouchArbiter::kMaxTries);
    }
    if (step.action != pz::CrouchAction::None) {
        bool const crouch = step.action == pz::CrouchAction::Crouch;
        if (crouch) {
            ++gCrouches;
        } else {
            ++gStands;
        }
        bool const restoring = gArbiter.Restoring();
        Ask(game, crouch, !physical.empty() ? physical
            : restoring ? std::format("putting back the {} the game changed ({}): your posture", crouch ? "crouch" : "stand", gRestoreWhy)
                        : std::string("a physical change that waited"));
    }
}

// The left stick click, every tick (its gate's state stays the player's): ME1's reaches the game as its own crouch toggle, so a
// change the game makes just after is the button's (kept, as ME1 keeps L3's); on the sequels, where the game's L3 is the area
// map's, the click is the crouch toggle the mod makes itself ([Hands] crouchClick; the owner, 2026-11-18), kept from the game
// by the host while the mod owns it (gamepad::Options::modCrouchClick, keepCrouchClick).
void TickCrouchClick(Game const& game, bool active) {
    vr::controllers::State const c = vr::controllers::Latest();
    bool const toggle = gamepad::CrouchClickToggle(gClick, c.active && c.left.stickClick, c.active && c.right.stickClick, c.timeMs);
#if defined(MEVR_GAME_LE1)
    (void)game;
    (void)active;
    gClickOwned = false;
    if (toggle) {
        gLastButton = static_cast<double>(GetTickCount64()) / 1000.0;
    }
#else
    hand_calibration::Posture const settings = hand_calibration::GetPosture();
    gClickOwned = active && settings.crouchClick && gShouldCrouch != nullptr && c.active && !power_wheel::OwnsLeftHand() &&
        !squad_weapons::IsOpen();
    if (!toggle || !gClickOwned) {
        return;
    }
#if defined(MEVR_GAME_LE3)
    bool const storming = gStorming.ReadBool(game.pawn) != false;
#else
    bool const storming = gStorming.ReadBool(game.controller).value_or(false);
#endif
    bool const want = !WantsToCrouch(game.pawn);
    if (storming) {
        ++gClickRefused;
        log::Info("posture: L3: no {} while sprinting (ME1's crouch button refuses then too)", want ? "crouch" : "stand");
        return;
    }
    StartProbe(game.pawn, want ? "crouch" : "stand");
    bool const accepted = ShouldCrouch(game.pawn, want);
    gGameWants = WantsToCrouch(game.pawn);
    gArbiter.Pressed(gGameWants);  // the button's: kept until the next physical crouch or stand, never put back
    gLastButton = static_cast<double>(GetTickCount64()) / 1000.0;
    ++(accepted ? gClickToggles : gClickRefused);
    gLastChange = std::format("{} {} (L3)", want ? "crouch" : "stand", accepted ? "accepted" : "refused");
    log::Info("posture: L3 (the crouch button, as ME1's): {} request {}: {}({}); bWantsToCrouch={}; kept until your next physical "
              "crouch or stand", want ? "crouch" : "stand", accepted ? "accepted" : "refused", kShouldCrouchPath, want ? "true" : "false",
        gGameWants);
#endif
}

#endif  // MEVR_POSTURE

} // namespace

bool TryStartup() {
#if defined(MEVR_POSTURE)
    if (!ue3::runtime::Up()) {
        log::Warn("posture: our reflection layer is off ({}): physical cover and crouch stay off", ue3::runtime::Status());
        return false;
    }
    struct Want {
        ue3::Function** slot;
        char const* path;
    };
    Want const wants[] = {
        {&gShouldCrouch, kShouldCrouchPath},
        {&gTryExitCover, "SFXGame.SFXGameModeDefault.TryExitCover"},
        {&gIsInCover, "SFXGame.BioPawn.IsInCover"},
        {&gLeaveCover, "SFXGame.BioPawn.LeaveCover"},
    };
    std::string missing;
    for (Want const& w : wants) {
        *w.slot = ue3::runtime::Get().FindFunction(w.path);
        if (*w.slot == nullptr) {
            missing += std::format("{}Function {}", missing.empty() ? "" : ", ", w.path);
        }
    }
    gStarted = gShouldCrouch != nullptr || gTryExitCover != nullptr || gIsInCover != nullptr;
#if defined(MEVR_GAME_LE3)
    ue3::Params crouch(ue3::runtime::Get(), gShouldCrouch);
    auto const* value = crouch.Find("bCrouch");
    gStarted = crouch.Valid() && crouch.Size() == 4 && crouch.Specs().size() == 1 && value &&
        value->kind == ue3::Kind::Bool && value->offset == 0 && value->size == 4 &&
        player::MovementAdmissionReady() && gWorldInfo.Get() && gStorming.Get() &&
        gIsCrouched.Get() && gWantsToCrouch.Get() && gCoverType.Get();
    if (!gStarted) missing += " LE3 crouch/admission ABI unavailable";
    if (gStarted && !gLe3NoSnapReady && gLe3FindCover.Get() && gLe3AcquireCover.Get()) {
        ue3::Params find(ue3::runtime::Get(), gLe3FindCover.Get());
        auto const* result = find.ReturnSpec();
        if (find.Valid() && result && result->kind == ue3::Kind::Bool && result->size == 4) {
            engine::call_function::AddFramePostHandler(gLe3FindCover.Get(), &OnLe3FindCover);
            gLe3NoSnapReady = true;
        }
    }
    // The cover prompt (ME2's way, ME3's names): SFXGUIInteraction.ShowCoverWidget and its overrides in the interaction's
    // subclasses only (another class's ShowCoverWidget takes other parameters), each a ProcessEvent post-handler registered
    // before the ProcessEvent hook goes in; its oPlayerController read where the base function has it.
    if (gStarted && !gPromptHandlerAdded && gHideCoverWidgetRef.Get() != nullptr && gShowCoverWidgetRef.Get() != nullptr &&
        gShowCoverWidgetPc.Get() != nullptr) {
        ue3::Reflection& r = ue3::runtime::Get();
        auto* const base = static_cast<ue3::Struct const*>(r.OuterOf(gShowCoverWidgetRef.Get()));
        for (ue3::Function* const show : r.FindFunctionsNamed("ShowCoverWidget")) {
            auto* const owner = static_cast<ue3::Struct const*>(r.OuterOf(show));
            if (base != nullptr && owner != nullptr && r.IsChildOf(owner, base)) {
                engine::process_event::AddPostHandler(show, &OnShowCoverWidget);
                gPromptHandlerAdded = true;
            }
        }
    }
    if (!gPromptHandlerAdded) {
        missing += std::format("{}SFXGUIInteraction.ShowCoverWidget/HideCoverWidget (the cover prompt stays)", missing.empty() ? "" : ", ");
    }
#endif
#if defined(MEVR_GAME_LE2)
    // The refusal of ME2's cover acquisition (a CallFunction handler: registered before the hook goes in).
    ue3::Function* const findCover = gFindPlayerCoverRef.Get();
    if (gStarted && findCover != nullptr && gTryAcquireCoverRef.Get() != nullptr) {
        if (!gRefusalHandlerAdded) {
            engine::call_function::AddFramePostHandler(findCover, &OnFindPlayerCover);
            gRefusalHandlerAdded = true;
        }
    } else {
        missing += std::format("{}BioPlayerController.FindPlayerCover or TryAcquireCover (no cover refusal)", missing.empty() ? "" : ", ");
    }
    // The cover prompt: every ShowCoverWidget (an override in a subclass included), a ProcessEvent post-handler (registered
    // before the ProcessEvent hook goes in).
    if (gStarted && !gPromptHandlerAdded && gHideCoverWidgetRef.Get() != nullptr) {
        for (ue3::Function* const show : ue3::runtime::Get().FindFunctionsNamed("ShowCoverWidget")) {
            engine::process_event::AddPostHandler(show, &OnShowCoverWidget);
            gPromptHandlerAdded = true;
        }
    }
    if (!gPromptHandlerAdded) {
        missing += std::format("{}MassEffectGuiManager.ShowCoverWidget/HideCoverWidget (the cover prompt stays)", missing.empty() ? "" : ", ");
    }
#endif
    log::Info("posture: script functions {}{}", gStarted ? "found" : "MISSING: physical cover and crouch stay off",
        missing.empty() ? std::string() : std::format(" (not found: {}{})", missing,
                                              gShouldCrouch == nullptr ? "; no physical crouch" : ""));
    return gStarted;
#else
    return false;
#endif
}

bool PhysicallyCrouched() {
    return gCrouchActive && gDetector.Crouched();
}

void AdjustPad(gamepad::Options& options) {
    options.modCrouchClick = gClickOwned;  // the sequels' L3 is the mod's crouch toggle (always false on LE1)
}

bool RefusingCover() {
    return gRefusingCover;
}

bool PhysicalMovementAllowed() {
#if defined(MEVR_GAME_LE3)
    if (!gStarted || !HostMovement()) return false;
    std::string why;
    auto const game = ReadGame(why);
    return game && gWorldInfo.Read<ue3::Object*>(game->controller).value_or(nullptr) == gWorld && NativeMovement(*game);
#else
    return false;
#endif
}

bool WantsCoverHook() {
#if defined(MEVR_GAME_LE1)
    return gStarted && hand_calibration::GetPosture().noCover;
#else
    return false;  // ME2 has no automatic cover test: its refusal is a script handler (research/23 section 7)
#endif
}

void Tick(double deltaSeconds, Link const& link) {
    hand_calibration::Refresh();  // at most once a second
    hand_calibration::Posture const settings = hand_calibration::GetPosture();
    gRefusingCover = false;  // until TickCover says so this tick
#if defined(MEVR_POSTURE)
    bool const noCover = gNoCoverOverride.value_or(settings.noCover);
#if defined(MEVR_GAME_LE3)
    gLink = link;
    gLinkTime = GetTickCount64();
    gLe3NoCover = noCover;
#endif
    if (!gStarted) {
        engine::cover_acquire::Refuse(nullptr);
#if defined(MEVR_GAME_LE2)
        gRefusedController = nullptr;
#endif
#if defined(MEVR_GAME_LE3)
        gLe3RefusedMode = nullptr;
#endif
        SetWhy(ue3::runtime::Up() ? std::string("waiting for the script functions")
                                  : std::format("waiting for our reflection layer ({})", ue3::runtime::Status()));
        return;
    }
    std::string why;
    auto const game = ReadGame(why);
    if (!game) {
        engine::cover_acquire::Refuse(nullptr);
#if defined(MEVR_GAME_LE2)
        gRefusedController = nullptr;
#endif
#if defined(MEVR_GAME_LE3)
        gLe3RefusedMode = nullptr;
#endif
        gActive = false;
        gClickOwned = false;
        SetWhy(why);
        return;
    }
#if defined(MEVR_GAME_LE3)
    auto* const world = gWorldInfo.Read<ue3::Object*>(game->controller).value_or(nullptr);
    if (world != gWorld) {
        gWorld = world;
        gPawn = 0; gArbiter.Forget(); gDetector.Reset(); gCrouchActive = false; gProbe = {};
        gRestorePending = false;
        gStanding.Reset(); gHeadRef.Reset(); gFloorSeen = false;
    }
    if (link.sceneGeneration != gGeneration) {
        gGeneration = link.sceneGeneration;
        // Admission, menus, focus and recenter are NOT a new native pawn. Cancel
        // pending gestures and tracking samples while retaining a crouch we own.
        gArbiter.ResetTracking(gDetector, gWantsToCrouch.ReadBool(game->pawn));
        gCrouchActive = false; gProbe = {};
        gStanding.Reset(); gHeadRef.Reset(); gFloorSeen = false;
    }
#endif
    if (reinterpret_cast<std::uintptr_t>(game->pawn) != gPawn) {
        if (gPawn != 0) {
            log::Info("posture: a new player pawn ({}): nothing of the crouch is ours", ue3::runtime::Get().FullName(game->pawn));
        }
        gPawn = reinterpret_cast<std::uintptr_t>(game->pawn);
        gArbiter.Forget();
        gRestorePending = false;
        gDetector.Reset();
        gLean.Reset();
        gCrouchActive = false;
        gCoverEpisode = false;
        gProbe = {};
    }
    auto const mode = head_tracking::GameMode();
    vr::host_view::State const view = vr::host_view::Latest();
    // No cover snapping: the one rule for the three games (VR on foot with noCover), whatever the physical crouch's admission.
    bool const refuseSnap = pz::RefuseCoverSnap({noCover, mode && *mode == 0, view.valid, body_calibration::Calibrating()});
    if (!mode || *mode != 0) {
        gActive = false;
        SetWhy(std::format("not on foot (game mode {})", mode ? std::to_string(*mode) : std::string("unknown")));
#if defined(MEVR_GAME_LE3)
    } else if (!HostMovement() || !NativeMovement(*game) || gInCover ||
        gCoverType.Read<std::uint8_t>(game->pawn).value_or(255) != 0) {
        gActive = false;
        SetWhy("native action, cover, movement admission or host ownership excludes physical crouch");
#endif
    } else if (!view.valid) {
        gActive = false;
        SetWhy("no head-tracked world view (menu, flat screen or VR off)");
    } else if (body_calibration::Calibrating()) {
        gActive = false;  // the calibration's crouch step is the player's, not Shepard's
        SetWhy("the player is calibrating (research/25)");
    } else {
        gActive = true;
        SetWhy("active");
    }
    TickCover(*game, refuseSnap, deltaSeconds);
    TickCrouchClick(*game, gActive);  // first: a click this tick is the button's before the crouch tick judges a change
    TickCrouch(*game, link, view, gActive,
        vr::calibration::PhysicalCrouch(body_calibration::Position(), settings.physicalCrouch), deltaSeconds);
#else
    (void)deltaSeconds;
    (void)link;
    (void)settings;
    SetWhy("LE1 and LE2 only");
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::Posture const settings = hand_calibration::GetPosture();
    std::optional<bool> const pending = gArbiter.Pending();
    return std::format("{{\"noCover\": {}, \"physicalCrouch\": {}, \"active\": {}, \"why\": {}, \"coverHook\": {}, \"inCover\": {}, "
                       "\"refusedEntries\": {}, \"eyeHeight\": {}, \"standingEyeHeight\": {}, \"fraction\": {}, \"source\": {}, "
                       "\"physical\": {}, \"dwell\": {}, \"gameWantsCrouch\": {}, \"gameCrouched\": {}, \"ours\": {}, \"pending\": {}, "
                       "\"restorePending\": {}, \"lastChange\": {}}}",
        settings.noCover ? "true" : "false",
        vr::calibration::PhysicalCrouch(body_calibration::Position(), settings.physicalCrouch) ? "true" : "false",
        gActive ? "true" : "false", json::JsonString(gWhy),
        engine::cover_acquire::Installed() ? "true" : "false", gInCover ? "true" : "false", gRefusedTotal, json::JsonNumber(gEye),
        json::JsonNumber(gStandingHeight), json::JsonNumber(gFraction.value_or(-1.0)), json::JsonString(gSource),
        json::JsonString(gDetector.Crouched() ? "crouched" : "standing"), json::JsonNumber(gDetector.Dwell()),
        gGameWants ? "true" : "false", gGameCrouched ? "true" : "false", gArbiter.Owns() ? "true" : "false",
        pending ? json::JsonString(*pending ? "crouch" : "stand") : std::string("null"),
        gRestorePending ? "true" : "false", json::JsonString(gLastChange));
}

void LogStats() {
    if (!gStarted) {
        return;
    }
    log::Info("    posture: {}; cover: {} tests refused, {} entries refused{}{}; crouch: physical {} (eyes {:.2f} m of {:.2f} m "
              "standing = {:.2f}), Shepard {} (wants {}){}; this period {} crouches, {} stands, {} changes without us",
        gWhy, gRefusedTests, gRefusedEntries, gInCover ? ", Shepard IN COVER" : "", gExits > 0 ? std::format(", {} exit asks", gExits) : "",
        gDetector.Crouched() ? "crouched" : "standing", gEye, gStandingHeight, gFraction.value_or(-1.0),
        gGameCrouched ? "crouched" : "standing", gGameWants ? "crouch" : "stand", gArbiter.Owns() ? ", ours" : "", gCrouches, gStands,
        gExternal);
#if defined(MEVR_GAME_LE2)
    if (gPromptsShown > 0 || gAcquireLooked > 0) {
        log::Info("    posture: this period the HUD's cover prompt shown {} times, hidden again {} times; TryAcquireCover found nothing {} "
                  "times", gPromptsShown, gPromptsHidden, gAcquireLooked);
    }
    gPromptsShown = gPromptsHidden = gAcquireLooked = 0;
#endif
#if defined(MEVR_GAME_LE3)
    if (gPromptsShown > 0 || gLe3Looked > 0) {
        log::Info("    posture: this period the HUD's cover prompt shown {} times, hidden again {} times; TryAcquireCover's FindCover "
                  "found nothing {} times", gPromptsShown, gPromptsHidden, gLe3Looked);
    }
    gPromptsShown = gPromptsHidden = gLe3Looked = 0;
#endif
    gRefusedTests = gRefusedEntries = gExits = gCrouches = gStands = gExternal = 0;
}

bool PrepareUnload() {
#if defined(MEVR_GAME_LE3)
    if (!gArbiter.Owns()) { gRestorePending = false; return true; }
    if (!ue3::runtime::Up()) { gRestorePending = true; return false; }
    auto* const owner = reinterpret_cast<ue3::Object*>(gPawn);
    if (!ue3::runtime::IsAlive(owner) || !ue3::IsA(owner, gBioPawn)) {
        gArbiter.Forget(); gRestorePending = false; return true; // owner is gone, never write a replacement
    }
    auto const ownerWorld = gWorldInfo.Read<ue3::Object*>(owner);
    if (!ownerWorld) { gRestorePending = true; return false; } // unavailable is not destroyed
    if (*ownerWorld != gWorld) {
        gArbiter.Forget(); gRestorePending = false; return true;
    }
    auto const wants = gWantsToCrouch.ReadBool(owner);
    if (gArbiter.FinishRelease(wants)) {
        gRestorePending = false; return true;
    }
    bool const accepted = wants && gShouldCrouch && ShouldCrouch(owner, false);
    if (accepted && gArbiter.FinishRelease(gWantsToCrouch.ReadBool(owner))) {
        gRestorePending = false;
        gLastChange = "owned crouch restore accepted before unload";
        log::Info("posture: unload restore accepted; bWantsToCrouch is false");
        return true;
    }
    if (!gRestorePending) log::Warn("posture: unload deferred; native owned-crouch restore refused or unavailable");
    gRestorePending = true;
    gLastChange = "owned crouch restore pending before unload";
    return false;
#else
    return true;
#endif
}

void Shutdown(bool processExiting) {
#if defined(MEVR_GAME_LE3)
    // The host preflight must call PrepareUnload before it removes callbacks. Keep
    // ownership intact even if a caller misses that contract; never claim a stand.
    if (!processExiting && !PrepareUnload()) return;
#endif
    engine::cover_acquire::Refuse(nullptr);
#if defined(MEVR_POSTURE)
#if defined(MEVR_GAME_LE2)
    gRefusedController = nullptr;
    gRefusalHandlerAdded = false;  // the mod's CallFunction table goes with it (engine::call_function::ClearHandlers)
    gAcquireLooked = 0;
    gLastRefusalMs = gLastRefusalLogMs = 0;
    gToldRefusal = false;
    gPromptHandlerAdded = false;  // and its ProcessEvent table (engine::process_event::ClearHandlers)
    gPromptsShown = gPromptsHidden = 0;
    gPromptsHiddenTotal = 0;
    gToldPrompt = false;
#endif
#if defined(MEVR_GAME_LE3)
    gLe3RefusedMode = nullptr;
    gLe3Looked = 0;
    gLe3LastRefusalMs = gLe3LastRefusalLogMs = 0;
    gLe3ToldRefusal = false;
    gPromptHandlerAdded = false;  // its ProcessEvent table goes with the mod (engine::process_event::ClearHandlers)
    gPromptsShown = gPromptsHidden = 0;
    gPromptsHiddenTotal = 0;
    gToldPrompt = false;
#endif
#if !defined(MEVR_GAME_LE3)
    if (!processExiting && gArbiter.Owns() && gShouldCrouch != nullptr && ue3::runtime::Up()) {
        ue3::Object* const pc = ue3::runtime::LocalPlayerController();
        ue3::Object* const pawn = pc != nullptr ? gPawnField.Read<ue3::Object*>(pc).value_or(nullptr) : nullptr;
        if (pawn != nullptr && reinterpret_cast<std::uintptr_t>(pawn) == gPawn && ue3::IsA(pawn, gBioPawn)) {
            if (WantsToCrouch(pawn)) {
                ShouldCrouch(pawn, false);
                log::Info("posture: the mod is unloading: Shepard stands (the crouch was ours)");
            }
        }
    }
#endif
    gShouldCrouch = gTryExitCover = gIsInCover = gLeaveCover = nullptr;
    gToldNoCrouchParam = false;
#else
    (void)processExiting;
#endif
#if defined(MEVR_GAME_LE3)
    gLink = {}; gLinkTime = gGeneration = 0; gWorld = nullptr; gLe3NoSnapReady = false;
#endif
    gStarted = false;
    gActive = false;
    gWhy = "not started";
    gLoggedWhy.clear();
    gPawn = 0;
    gToldNoHook = false;
    gLoggedRefusal = 0;
    gRefusedTests = gRefusedEntries = gRefusedTotal = gExits = 0;
    gInCover = gCoverEpisode = false;
    gCoverSeconds = gExitRetry = 0.0;
    gToldFallback = false;
    gDetector.Reset();
    gArbiter.Forget();
    gCrouchActive = false;
    gRestorePending = false;
    gGameWants = gGameCrouched = false;
    gStanding.Reset();
    gHeadRef.Reset();
    gFloorSeen = false;
    gFraction.reset();
    gEye = gStandingHeight = -1.0;
    gSource = "none";
    gLoggedSource.clear();
    gLoggedStanding = -1.0;
    gToldDeferred = false;
    gCrouches = gStands = gExternal = 0;
    gLastChange = "none";
    gProbe = {};
}

std::string CoverConsole(std::string_view args) {
    std::istringstream words{std::string(args)};
    std::string sub;
    words >> sub;
    if (sub == "snap") {
        // `mevr cover snap on|off|auto`: the game's cover snapping allowed (on), refused (off) or as [Hands] noCover says, until
        // the mod reloads (an A/B for the no-snapping check).
        std::string mode;
        words >> mode;
        if (mode == "on" || mode == "off") {
            gNoCoverOverride = mode == "off";
        } else if (mode == "auto") {
            gNoCoverOverride.reset();
        } else {
            return "usage: mevr cover snap on|off|auto";
        }
#if defined(MEVR_GAME_LE3)
        return std::format("posture LE3: the game's cover snapping {} until the mod reloads (refused whenever VR is on foot, as ME1's "
                           "and ME2's; cover already taken is left to the game); VR cover's real-slot damage rule follows it",
            gNoCoverOverride ? (*gNoCoverOverride ? "refused" : "allowed") : "as [Hands] noCover says");
#endif
        return std::format("posture: the game's cover snapping {} until the mod reloads", gNoCoverOverride ? (*gNoCoverOverride ? "refused" : "allowed")
                                                                                                         : "as [Hands] noCover says");
    }
#if defined(MEVR_POSTURE)
    std::string why;
    auto const game = ReadGame(why);
    if (!game) {
        return "mevr cover " + sub + ": " + why;
    }
    ue3::Object* const pawn = game->pawn;
    auto where = [&] {
        std::optional<ue3::Vector> const at = gLocation.Read<ue3::Vector>(pawn);
        std::string text = std::format("Shepard at ({:.1f}, {:.1f}, {:.1f}), in cover {}, CoverType {}, CoverAction {}", at ? at->x : 0.0f,
            at ? at->y : 0.0f, at ? at->z : 0.0f, InCover(pawn) ? "yes" : "no", CoverType(pawn),
            gCoverAction.Read<std::uint8_t>(pawn).value_or(0));
#if defined(MEVR_GAME_LE2)
        ue3::Object* const link = gCurrentLink.Read<ue3::Object*>(pawn).value_or(nullptr);
        text += std::format(", CurrentLink {}, CurrentSlotIdx {}; sprinting {} (storm to cover {}); cover prompts hidden {}",
            link != nullptr ? ue3::runtime::Get().NameText(link) : std::string("None"), gCurrentSlotIdx.Read<std::int32_t>(pawn).value_or(-99),
            gStorming.ReadBool(game->controller).value_or(false) ? "yes" : "no",
            gEnableStormToCover.ReadBool(game->controller).value_or(false) ? "enabled" : "off", gPromptsHiddenTotal);
#endif
        text += std::format(", crouched {} (wants {}); cover snapping {}", gIsCrouched.ReadBool(pawn).value_or(false) ? "yes" : "no",
            WantsToCrouch(pawn) ? "yes" : "no", gRefusingCover ? "refused" : "the game's");
        return text;
    };
    if (sub == "where") {
        return "mevr cover where: " + where();
    }
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    if (sub == "links") {
        // `mevr cover links [n]`: the nearest n (8) CoverLink actors to Shepard, for standing next to cover.
        int count = 8;
        words >> count;
        std::optional<ue3::Vector> const at = gLocation.Read<ue3::Vector>(pawn);
        ue3::Class* const cls = gCoverLinkClass.Get();
        if (cls == nullptr || !at) {
            return "mevr cover links: Engine.CoverLink or Shepard's place not found";
        }
        ue3::Reflection& r = ue3::runtime::Get();
        std::vector<std::pair<double, ue3::Object*>> found;
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !r.IsAlive(o) || !r.IsA(o, cls) || r.NameText(o).starts_with("Default__")) {
                continue;
            }
            if (std::optional<ue3::Vector> const there = gLocation.Read<ue3::Vector>(o)) {
                found.emplace_back(std::hypot(static_cast<double>(there->x - at->x), static_cast<double>(there->y - at->y),
                                       static_cast<double>(there->z - at->z)), o);
            }
        }
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        std::string out = std::format("mevr cover links: {} CoverLinks; Shepard at ({:.0f}, {:.0f}, {:.0f})", found.size(), at->x, at->y, at->z);
        for (std::size_t i = 0; i < found.size() && static_cast<int>(i) < count; ++i) {
            std::optional<ue3::Vector> const there = gLocation.Read<ue3::Vector>(found[i].second);
            out += std::format("\n    {} at ({:.0f}, {:.0f}, {:.0f}), {:.1f} m", r.NameText(found[i].second), there->x, there->y, there->z,
                found[i].first / 100.0);
        }
        return out;
    }
#endif
    if (sub == "acquire") {
#if defined(MEVR_GAME_LE2)
        // `mevr cover acquire`: BioPlayerController.TryAcquireCover through ProcessEvent, the exec the A button's alias runs (and
        // CheckStormToCover from a sprint): what A does next to cover. Its answer, and Shepard before and after (the snap's move
        // takes a moment: `mevr cover where` a second later).
        ue3::Function* const tryAcquire = gTryAcquireCoverRef.Get();
        if (tryAcquire == nullptr) {
            return "mevr cover acquire: BioPlayerController.TryAcquireCover not found";
        }
        std::string const before = where();
        unsigned const refusedBefore = gRefusedTests;
        unsigned const lookedBefore = gAcquireLooked;
        bool const took = CallBool(game->controller, tryAcquire);
        return std::format("mevr cover acquire: TryAcquireCover returned {} (FindPlayerCover refused {} times, found nothing {} times); "
                           "before: {}; after: {}",
            took ? "TRUE" : "FALSE", gRefusedTests - refusedBefore, gAcquireLooked - lookedBefore, before, where());
#elif defined(MEVR_GAME_LE3)
        // `mevr cover acquire [wide]`: the player's mode's SFXGameModeDefault.TryAcquireCover(bWideCheck, bTestOnly = true)
        // through ProcessEvent, what A asks next to cover, as a test only (the game acquires nothing): its answer, and whether
        // the refusal's handler saw its FindCover (refused, or found nothing).
        ue3::Function* const tryAcquire = gLe3AcquireCover.Get();
        if (tryAcquire == nullptr || game->mode == nullptr) {
            return "mevr cover acquire: SFXGameModeDefault.TryAcquireCover or the player's mode not found";
        }
        std::string option;
        words >> option;
        bool const wide = option == "wide";
        ue3::Params parms(ue3::runtime::Get(), tryAcquire);
        if (!parms.SetBool("bWideCheck", wide) || !parms.SetBool("bTestOnly", true)) {
            return "mevr cover acquire: TryAcquireCover's parameters bWideCheck/bTestOnly not found";
        }
        std::string const before = where();
        unsigned const refusedBefore = gRefusedTests;
        unsigned const lookedBefore = gLe3Looked;
        ue3::runtime::Call(game->mode, tryAcquire, parms);
        std::optional<bool> const took = parms.ReturnBool();
        return std::format("mevr cover acquire: TryAcquireCover({}, test only) returned {} (FindCover refused {} times, found nothing "
                           "{} times; the refusal {}); before: {}; after: {}",
            wide ? "wide" : "narrow", took ? (*took ? "TRUE" : "FALSE") : "?", gRefusedTests - refusedBefore, gLe3Looked - lookedBefore,
            gLe3NoCover ? "on" : "off", before, where());
#else
        return "mevr cover acquire: ME2 only (ME1 takes cover by itself: its test is engine/CoverAcquire's)";
#endif
    }
    return "usage: mevr cover snap on|off|auto | where | acquire | links [n]";
#else
    return "mevr cover: LE1 and LE2 only";
#endif
}

} // namespace mevr::game::posture
