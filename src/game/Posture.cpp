#include "game/Posture.hpp"

#include "core/Log.hpp"
#include "core/ik/BodySlots.hpp"
#include "core/ik/Posture.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/CoverAcquire.hpp"
#include "engine/Objects.hpp"
#include "game/BodyCalibration.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/ViewAnchor.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <string>

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

#if defined(SDK_TARGET_LE1)

static_assert(offsetof(ABioPawn, CoverType) == 0xA55);
static_assert(offsetof(ABioPawn, CoverAction) == 0xA56);

UFunction* gShouldCrouch = nullptr;
UFunction* gTryExitCover = nullptr;
UFunction* gIsInCover = nullptr;
UFunction* gLeaveCover = nullptr;

struct Game {
    ABioPlayerController* controller = nullptr;
    ABioPawn* pawn = nullptr;
    USFXGameModeDefault* mode = nullptr;
};

std::optional<Game> ReadGame(std::string& why) {
    APlayerController* const pc = engine::LocalPlayerController();
    if (pc == nullptr || !pc->IsA(ABioPlayerController::StaticClass())) {
        why = "no player controller";
        return std::nullopt;
    }
    Game game;
    game.controller = static_cast<ABioPlayerController*>(pc);
    if (pc->Pawn == nullptr || !pc->Pawn->IsA(ABioPawn::StaticClass())) {
        why = "no player pawn (the Mako or a cutscene?)";
        return std::nullopt;
    }
    game.pawn = static_cast<ABioPawn*>(pc->Pawn);
    if (USFXGameModeManager* const manager = game.controller->GameModeManager) {
        for (int i = 0; i < static_cast<int>(manager->GameModes.Count()); ++i) {
            USFXGameModeBase* const mode = manager->GameModes.GetData()[i];
            if (mode != nullptr && mode->IsA(USFXGameModeDefault::StaticClass())) {
                game.mode = static_cast<USFXGameModeDefault*>(mode);
                break;
            }
        }
    }
    return game;
}

// Directly through ProcessEvent, never the SDK's wrappers (they set every flag on the UFunction).
bool CallBool(UObject* object, UFunction* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    struct {
        unsigned long ReturnValue;
        unsigned char pad[60];
    } parms{};
    object->ProcessEvent(function, &parms, nullptr);
    return parms.ReturnValue != 0;
}

void CallVoid(UObject* object, UFunction* function) {
    if (object == nullptr || function == nullptr) {
        return;
    }
    struct {
        unsigned char pad[64];
    } parms{};
    object->ProcessEvent(function, &parms, nullptr);
}

// BioPawn.ShouldCrouch: Pawn.ShouldCrouch sets bWantsToCrouch (the engine crouches or stands on its
// next move, standing only where there's room), then BioPawn resets its movement animation state.
void ShouldCrouch(ABioPawn* pawn, bool crouch) {
    struct {
        unsigned long bCrouch;
        unsigned char pad[60];
    } parms{};
    parms.bCrouch = crouch ? 1u : 0u;
    pawn->ProcessEvent(gShouldCrouch, &parms, nullptr);
}

bool InCover(ABioPawn* pawn) {
    return gIsInCover != nullptr ? CallBool(pawn, gIsInCover) : pawn->CoverType != 0 && pawn->CoverAction != 13;
}

char const* CoverName(int type) {
    return type == 1 ? "high (standing)" : type == 2 ? "low (crouching)" : "no";
}

// The view's height above Shepard's origin (units), if there's a view.
std::optional<double> ViewAbovePawn(ABioPawn* pawn) {
    auto const frame = view_anchor::Current();
    if (!frame) {
        return std::nullopt;
    }
    return frame->headWorld.z - pawn->Location.Z;
}

void StartProbe(ABioPawn* pawn, char const* what) {
    auto const before = ViewAbovePawn(pawn);
    gProbe = {true, 0.0, before.value_or(0.0), before.has_value(), what};
}

void FollowProbe(ABioPawn* pawn, double deltaSeconds) {
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
            gProbe.age, gProbe.what, *after, gProbe.before, *after - gProbe.before, view.offsetUp, pawn->bIsCrouched ? "yes" : "no");
    }
}

// The cover side, each tick with a game.
void TickCover(Game const& game, bool active, bool noCover, double deltaSeconds) {
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
    if (!gCoverEpisode) {
        gCoverEpisode = true;
        gCoverSeconds = 0.0;
        gExitRetry = 0.0;
        gToldFallback = false;
        log::Info("posture: Shepard is in {} cover (taken before noCover applied, or by a script): leaving it as A does "
                  "(SFXGameModeDefault.TryExitCover)", CoverName(game.pawn->CoverType));
    }
    gCoverSeconds += deltaSeconds;
    gExitRetry -= deltaSeconds;
    if (gExitRetry > 0.0) {
        return;
    }
    gExitRetry = kExitRetrySeconds;
    ++gExits;
    if (gCoverSeconds < kExitFallbackSeconds && game.mode != nullptr && gTryExitCover != nullptr) {
        CallVoid(game.mode, gTryExitCover);  // A's own exit: push off the wall, then LeaveCover
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
    ShouldCrouch(game.pawn, crouch);
    gGameWants = game.pawn->bWantsToCrouch != 0;
    gArbiter.Applied(gGameWants);
    gLastChange = std::format("{} ({})", crouch ? "crouch" : "stand", why);
    log::Info("posture: Shepard {} ({}): BioPawn.ShouldCrouch({}){}", crouch ? "crouches" : "stands", why, crouch ? "true" : "false",
        gGameWants == crouch ? "" : " DIDN'T TAKE (bWantsToCrouch unchanged)");
}

// The crouch side, each tick with a game.
void TickCrouch(Game const& game, Link const& link, vr::host_view::State const& view, bool active, bool enabled,
    double deltaSeconds) {
    gGameWants = game.pawn->bWantsToCrouch != 0;
    gGameCrouched = game.pawn->bIsCrouched != 0;
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
    input.canApply = !game.controller->bStorming;  // ToggleCrouch refuses while sprinting too
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

#endif  // SDK_TARGET_LE1

} // namespace

bool TryStartup() {
#if defined(SDK_TARGET_LE1)
    struct Want {
        UFunction** slot;
        wchar_t const* name;
        char const* full;
        bool needed;
    };
    Want const wants[] = {
        {&gShouldCrouch, L"ShouldCrouch", "Function SFXGame.BioPawn.ShouldCrouch", false},
        {&gTryExitCover, L"TryExitCover", "Function SFXGame.SFXGameModeDefault.TryExitCover", false},
        {&gIsInCover, L"IsInCover", "Function SFXGame.BioPawn.IsInCover", false},
        {&gLeaveCover, L"LeaveCover", "Function SFXGame.BioPawn.LeaveCover", false},
    };
    std::string missing;
    for (Want const& w : wants) {
        *w.slot = engine::FindFunction(w.name, w.full);
        if (*w.slot == nullptr) {
            missing += std::format("{}{}", missing.empty() ? "" : ", ", w.full);
        }
    }
    gStarted = gShouldCrouch != nullptr || gTryExitCover != nullptr || gIsInCover != nullptr;
    log::Info("posture: script functions {}{}", gStarted ? "found" : "MISSING: physical cover and crouch stay off",
        missing.empty() ? std::string() : std::format(" (not found: {}{})", missing,
                                              gShouldCrouch == nullptr ? "; no physical crouch" : ""));
    return gStarted;
#else
    return false;
#endif
}

bool WantsCoverHook() {
    return gStarted && hand_calibration::GetPosture().noCover;
}

void Tick(double deltaSeconds, Link const& link) {
    hand_calibration::Refresh();  // at most once a second
    hand_calibration::Posture const settings = hand_calibration::GetPosture();
#if defined(SDK_TARGET_LE1)
    if (!gStarted) {
        engine::cover_acquire::Refuse(nullptr);
        SetWhy("waiting for the script functions");
        return;
    }
    std::string why;
    auto const game = ReadGame(why);
    if (!game) {
        engine::cover_acquire::Refuse(nullptr);
        gActive = false;
        SetWhy(why);
        return;
    }
    if (reinterpret_cast<std::uintptr_t>(game->pawn) != gPawn) {
        if (gPawn != 0) {
            log::Info("posture: a new player pawn ({}): nothing of the crouch is ours", engine::FullName(game->pawn));
        }
        gPawn = reinterpret_cast<std::uintptr_t>(game->pawn);
        gArbiter.Forget();
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
    TickCover(*game, gActive, settings.noCover, deltaSeconds);
    TickCrouch(*game, link, view, gActive, settings.physicalCrouch, deltaSeconds);
#else
    (void)deltaSeconds;
    (void)link;
    (void)settings;
    SetWhy("LE1 only");
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
    hand_calibration::Posture const settings = hand_calibration::GetPosture();
    std::optional<bool> const pending = gArbiter.Pending();
    return std::format("{{\"noCover\": {}, \"physicalCrouch\": {}, \"active\": {}, \"why\": {}, \"coverHook\": {}, \"inCover\": {}, "
                       "\"refusedEntries\": {}, \"eyeHeight\": {}, \"standingEyeHeight\": {}, \"fraction\": {}, \"source\": {}, "
                       "\"physical\": {}, \"dwell\": {}, \"gameWantsCrouch\": {}, \"gameCrouched\": {}, \"ours\": {}, \"pending\": {}, "
                       "\"lastChange\": {}}}",
        settings.noCover ? "true" : "false", settings.physicalCrouch ? "true" : "false", gActive ? "true" : "false", json::JsonString(gWhy),
        engine::cover_acquire::Installed() ? "true" : "false", gInCover ? "true" : "false", gRefusedTotal, json::JsonNumber(gEye),
        json::JsonNumber(gStandingHeight), json::JsonNumber(gFraction.value_or(-1.0)), json::JsonString(gSource),
        json::JsonString(gDetector.Crouched() ? "crouched" : "standing"), json::JsonNumber(gDetector.Dwell()),
        gGameWants ? "true" : "false", gGameCrouched ? "true" : "false", gArbiter.Owns() ? "true" : "false",
        pending ? json::JsonString(*pending ? "crouch" : "stand") : std::string("null"), json::JsonString(gLastChange));
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
    gRefusedTests = gRefusedEntries = gExits = gCrouches = gStands = gExternal = 0;
}

void Shutdown(bool processExiting) {
    engine::cover_acquire::Refuse(nullptr);
#if defined(SDK_TARGET_LE1)
    if (!processExiting && gArbiter.Owns() && gShouldCrouch != nullptr) {
        APlayerController* const pc = engine::LocalPlayerController();
        if (pc != nullptr && reinterpret_cast<std::uintptr_t>(pc->Pawn) == gPawn && pc->Pawn->IsA(ABioPawn::StaticClass())) {
            auto* const pawn = static_cast<ABioPawn*>(pc->Pawn);
            if (pawn->bWantsToCrouch) {
                ShouldCrouch(pawn, false);
                log::Info("posture: the mod is unloading: Shepard stands (the crouch was ours)");
            }
        }
    }
    gShouldCrouch = gTryExitCover = gIsInCover = gLeaveCover = nullptr;
#else
    (void)processExiting;
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

} // namespace mevr::game::posture
