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
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/ViewAnchor.hpp"
#include "game/Player.hpp"
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
ue3::FieldRef gWantsToCrouch{"Engine.Pawn", "bWantsToCrouch", ue3::kEveryGame};

#if defined(MEVR_GAME_LE3)
ue3::FunctionRef gLe3FindCover{"SFXGame.SFXGameModeDefault.FindCover", ue3::kEveryGame};
ue3::FunctionRef gLe3AcquireCover{"SFXGame.SFXGameModeDefault.TryAcquireCover", ue3::kEveryGame};
bool gLe3NoSnapReady = false;
ue3::FunctionRef gDead{"Engine.Pawn.IsDead", ue3::kEveryGame};
ue3::FunctionRef gMoveIgnored{"Engine.PlayerController.IsMoveInputIgnored", ue3::kEveryGame};
ue3::FieldRef gCurrentAction{"SFXGame.BioPawn", "CurrentCustomAction", ue3::kEveryGame};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics", ue3::kEveryGame};
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
// The HUD's "take cover" prompt: BioPlayerController::UpdateCover (0xACE2A0, the controller's native tick) finds cover through
// FindPlayerCover's implementation (a C++ call the block above never sees) and ProcessEvents the GUI manager's
// ShowCoverWidget or HideCoverWidget each tick. While the snapping is refused, a ShowCoverWidget is followed at once by the
// game's own HideCoverWidget (a ProcessEvent post-handler: before anything is drawn), so no prompt offers what A won't do.
// The climb/mantle prompt (ShowClimbMantleWidget, shown only in cover) is untouched.
ue3::FunctionRef gHideCoverWidgetRef{"SFXGame.MassEffectGuiManager.HideCoverWidget", ue3::kEveryGame};
unsigned gPromptsShown = 0;   // ShowCoverWidget calls seen (heartbeat)
unsigned gPromptsHidden = 0;  // ... of which hidden again
std::uint64_t gPromptsHiddenTotal = 0;
bool gToldPrompt = false;
bool gPromptHandlerAdded = false;
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
    if (player::GameMode(game.controller) != 0 ||
        gCurrentAction.Read<std::int32_t>(game.pawn).value_or(-1) != 0 ||
        gPhysics.Read<std::uint8_t>(game.pawn).value_or(255) != 1) return false; // PHYS_Walking only
    ue3::Params dead(ue3::runtime::Get(), gDead.Get());
    if (!dead.Valid() || !ue3::runtime::Call(game.pawn, gDead.Get(), dead) || dead.ReturnBool() != false) return false;
    ue3::Params params(ue3::runtime::Get(), gMoveIgnored.Get());
    return params.Valid() && ue3::runtime::Call(game.controller, gMoveIgnored.Get(), params) &&
        params.ReturnBool() == false;
}
bool HostMovement() {
    auto const now = GetTickCount64();
    return gLinkTime && now >= gLinkTime && now - gLinkTime <= 100 &&
        gLink.runtimeOwned && gLink.focused && !gLink.menuOpen && !gLink.calibrating &&
        gLink.sceneGeneration && gLink.controlsAllowed && gLink.controlsAllowed();
}
void OnLe3FindCover(ue3::Object* context, ue3::Function*, ue3::Function* caller, std::uint8_t*, void* result) {
    if (!gNoCoverOverride.value_or(false) || !gLe3NoSnapReady || !vr_cover::CandidateReady() || !HostMovement() ||
        caller != gLe3AcquireCover.Get() || !result) return;
    std::string why;
    auto const game = ReadGame(why);
    if (!game || context != game->mode || !NativeMovement(*game)) return;
    auto* const found = static_cast<std::uint32_t*>(result);
    if (*found) { *found = 0; ++gRefusedEntries; ++gRefusedTotal; write_journal::Wrote("Posture.LE3.FindCover", found); }
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

#if defined(MEVR_GAME_LE2)
// MassEffectGuiManager.ShowCoverWidget ran (ProcessEvent): hidden again while the snapping is refused.
void OnShowCoverWidget(ue3::Object* context, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ++gPromptsShown;
    ue3::Function* const hide = gHideCoverWidgetRef.Get();
    if (!gRefusingCover || context == nullptr || hide == nullptr) {
        return;
    }
    CallVoid(context, hide);
    ++gPromptsHidden;
    ++gPromptsHiddenTotal;
    if (!gToldPrompt) {
        gToldPrompt = true;
        log::Info("posture: the HUD's cover prompt (BioPlayerController::UpdateCover's ShowCoverWidget) is hidden again with "
                  "MassEffectGuiManager.HideCoverWidget while the snapping is refused ({})", ue3::runtime::Get().FullName(context));
    }
}
#endif

// The cover side, each tick with a game.
void TickCover(Game const& game, bool active, bool noCover, double deltaSeconds) {
#if defined(MEVR_GAME_LE3)
    (void)noCover; (void)deltaSeconds;
    // Explicit diagnostic opt-in only until native protection/slot/collision acceptance.
    gRefusingCover = active && gNoCoverOverride.value_or(false) && gLe3NoSnapReady && vr_cover::CandidateReady() && engine::call_function::Installed();
    gInCover = InCover(game.pawn);
    return; // Existing native cover is never forcibly exited; acquire refusal acts before new entry.
#endif
    gRefusingCover = active && noCover;  // kept out of cover either way: refused, or left at once without the hook
#if defined(MEVR_GAME_LE2)
    // ME2: TryAcquireCover's FindPlayerCover answered false (a script call: the CallFunction hook, which Mod.cpp puts in while
    // RefusingCover()).
    bool const hooked = gRefusalHandlerAdded && engine::call_function::Installed();
    gRefusedController = active && noCover && hooked ? game.controller : nullptr;
    if (active && noCover && !gRefusalHandlerAdded && !gToldNoHook) {
        gToldNoHook = true;
        log::Warn("posture: BioPlayerController.FindPlayerCover or TryAcquireCover wasn't found: the game still takes cover on A, "
                  "and Shepard leaves it at once");
    }
#else
    bool const refuse = active && noCover && engine::cover_acquire::Installed();
    engine::cover_acquire::Refuse(refuse ? game.pawn : nullptr);
    if (!refuse) {
        gLoggedRefusal = 0;
    }
    if (active && noCover && !engine::cover_acquire::Installed() && !gToldNoHook) {
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
    if (!(active && noCover)) {
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
pz::CrouchParams CrouchParamsNow() {
    pz::CrouchParams const base = hand_calibration::GetPosture().crouch;
    return body_calibration::CrouchParams(base).value_or(base);
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
            gFloorSessions = floor.sessions;
            gFloorRecenters = floor.recenters;
            gFloorSeen = true;
        }
        if (floor.recenters != gFloorRecenters) {
            gFloorRecenters = floor.recenters;
            recentered = true;
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
        log::Info("posture: standing eye height {:.2f} m, from {}; Shepard crouches below {:.2f} m and stands above {:.2f} m{}",
            gStandingHeight, source, gStandingHeight * CrouchParamsNow().crouchBelow, gStandingHeight * CrouchParamsNow().standAbove,
            body_calibration::CrouchParams(hand_calibration::GetPosture().crouch) ? " (between your calibrated heights)" : "");
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
    pz::CrouchEvent const event = gDetector.Update(fraction, deltaSeconds, params);
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
    input.canApply = gStorming.ReadBool(game.pawn) == false;
#else
    input.canApply = !gStorming.ReadBool(game.controller).value_or(false);
#endif
    //  // ToggleCrouch refuses while sprinting too
    pz::ArbiterStep const step = gArbiter.Update(input);
    if (step.external) {
        ++gExternal;
        log::Info("posture: Shepard's crouch changed without us (L3, sprinting or a script): now {}; kept until the next "
                  "physical crouch or stand", gGameWants ? "crouching" : "standing");
    }
    if (step.alreadyThere) {
        log::Info("posture: Shepard is already {} (L3 or the game got there first): that posture follows yours from now on",
            gGameWants ? "crouching" : "standing");
    }
    if (step.deferred && !gToldDeferred) {
        gToldDeferred = true;
        log::Info("posture: the physical {} waits: Shepard is sprinting", gArbiter.Pending().value_or(false) ? "crouch" : "stand");
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
        Ask(game, crouch, physical.empty() ? std::string("a physical change that waited") : physical);
    }
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
    ue3::Params ignored(ue3::runtime::Get(), gMoveIgnored.Get());
    gStarted = crouch.Valid() && crouch.Size() == 4 && crouch.Specs().size() == 1 && value &&
        value->kind == ue3::Kind::Bool && value->offset == 0 && value->size == 4 &&
        ignored.Valid() && ignored.Size() == 4 && ignored.Specs().size() == 1 &&
        gCurrentAction.Get() && gPhysics.Get() && gWorldInfo.Get() && gStorming.Get() && gDead.Get() &&
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
#endif
    if (!gStarted) {
        engine::cover_acquire::Refuse(nullptr);
#if defined(MEVR_GAME_LE2)
        gRefusedController = nullptr;
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
        gActive = false;
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
        gCrouchActive = false;
        gCoverEpisode = false;
        gProbe = {};
    }
    auto const mode = head_tracking::GameMode();
    vr::host_view::State const view = vr::host_view::Latest();
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
    TickCover(*game, gActive, noCover, deltaSeconds);
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
        return std::format("posture LE3: real-slot cover candidate {}; no native cover exit; runtime acceptance pending",
            gNoCoverOverride.value_or(false) ? "requested (requires fresh ownership and complete adapter metadata)" : "disabled");
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
#if defined(MEVR_GAME_LE2)
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
