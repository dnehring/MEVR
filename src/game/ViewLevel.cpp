#include "game/ViewLevel.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/vr/ViewLevel.hpp"
#include "engine/CallFunction.hpp"
#include "engine/Objects.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/SupportGrip.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <utility>
#include <vector>

namespace mevr::game::view_level {
namespace {

namespace vl = vr::view_level;

// A log line once the game moved the pitch (or the roll) this far since the last line: 0.5 degrees.
constexpr int kLogThresholdUnits = 91;
constexpr double kLogMinSeconds = 1.0;
// The hold's state changes (cover, camera modes) are logged at most this often; the heartbeat says the rest.
constexpr double kWhyLogSeconds = 2.0;
// Mod ticks without a PlayerTick post before the mod's tick holds instead.
constexpr int kFallbackTicks = 3;

Link gLink;
bool gStarted = false;
std::vector<UFunction*> gPlayerTicks;
UFunction* gSetWeaponRecoil = nullptr;

vl::Window gWindow;
vl::ChangeLog gChanges{kLogThresholdUnits, kLogMinSeconds};

// The pitch the last controller tick left (after the hold): the next tick's game change is measured from it.
std::uintptr_t gController = 0;  // whose (a key only, never dereferenced)
bool gHaveLeft = false;
int gLeftPitch = 0;
// The rotation as the game left it, the last tick (before the hold).
int gPitch = 0;
int gYaw = 0;
int gRoll = 0;
bool gHaveRotation = false;
vl::Why gWhy = vl::Why::NoView;
vl::Why gLoggedWhy = vl::Why::NoView;
bool gWhyLogged = false;
double gWhyLoggedAt = -1e9;
unsigned gWhyChanges = 0;  // since the heartbeat
std::string gModeName = "none";

unsigned long long gPosts = 0;  // PlayerTick posts handled for the local controller
unsigned long long gPostsAtTick = 0;
int gTicksWithoutPost = 0;
bool gToldFallback = false;
unsigned gFallbackHolds = 0;  // since the heartbeat

// WeaponRecoilOffset.Pitch as last seen (after a PlayerTick's UpdateKickback, or a shot): a shot's kick is
// the rise since.
bool gRecoilSeenValid = false;
int gRecoilSeen = 0;
long long gShots = 0;
long long gRecoilTotal = 0;
bool gZoomSnapPending = false;  // a zoom-snap target was dropped last tick
unsigned gPulses = 0;           // since the heartbeat
bool gToldFirstShot = false;
double gSnapLoggedAt = -1e9;  // the last "took hold of a pitched camera" line

// Per camera mode class: its short name and whether the hold applies in it.
struct ModeEntry {
    UClass* cls;
    std::string name;
    bool allowed;
};
std::vector<ModeEntry> gModes;

double Now() {
    static LARGE_INTEGER const frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

std::string Angle(int units) {
    int const s = vl::Signed(units);
    return std::format("{} UU ({:+.2f} deg)", s, vl::Degrees(s));
}

std::string Delta(long long units) {
    return std::format("{:+.2f} deg ({:+} UU)", static_cast<double>(units) * 360.0 / vl::kUnitsPerTurn, units);
}

#if defined(SDK_TARGET_LE1)

static_assert(offsetof(AActor, Rotation) == 0x114);
static_assert(offsetof(AActor, Physics) == 0x27C);
static_assert(offsetof(AActor, Modules) == 0x70);
static_assert(offsetof(APawn, InvManager) == 0x4A0);
static_assert(offsetof(ABioPawn, CoverType) == 0xA55);
static_assert(offsetof(ABioPawn, CoverAction) == 0xA56);
static_assert(offsetof(ASFXInventoryManager, WeaponRecoilOffset) == 0x2CC);
static_assert(offsetof(ASFXInventoryManager, TotalRecoil) == 0x2D8);
static_assert(offsetof(USFXModule_AimAssist, LastZoomSnapTarget) == 0xB8);
static_assert(offsetof(APlayerController, PlayerCamera) == 0x6B0);
static_assert(offsetof(ASFXCameraNativeBase, CurrentCameraMode) == 0x568);
static_assert(offsetof(USFXCameraMode_Interpolate, To) == 0x168);

constexpr char const* kSetWeaponRecoil = "Function SFXGame.SFXInventoryManager.SetWeaponRecoil";
constexpr unsigned char kPhysLadder = 9;  // EPhysics PHYS_Ladder

void LogWhy(vl::Why why, double now) {
    if (why != gWhy) {
        ++gWhyChanges;
    }
    if (gWhyLogged && why == gLoggedWhy) {
        return;
    }
    if (gWhyLogged && now - gWhyLoggedAt < kWhyLogSeconds) {
        return;  // flapping (cover, camera blends): the heartbeat has the count
    }
    gWhyLogged = true;
    gLoggedWhy = why;
    gWhyLoggedAt = now;
    log::Info("view level: {} (camera mode {}, pitch as the game left it {})", vl::Name(why), gModeName, Angle(gPitch));
}

ModeEntry const& Mode(UClass* cls) {
    for (ModeEntry const& entry : gModes) {
        if (entry.cls == cls) {
            return entry;
        }
    }
    std::string name = cls != nullptr ? engine::ToString(cls->Name) : std::string("none");
    bool const allowed = vl::CameraModeAllows(name);
    gModes.push_back({cls, std::move(name), allowed});
    return gModes.back();
}

// Whether the camera's mode (or the one it blends to) is one the hold applies in; names it in gModeName.
bool CameraModeAllowed(APlayerController* controller) {
    ACamera* const base = controller->PlayerCamera;
    if (base == nullptr || !base->IsA(ASFXCameraNativeBase::StaticClass())) {
        gModeName = "none";
        return false;
    }
    USFXCameraMode* mode = static_cast<ASFXCameraNativeBase*>(base)->CurrentCameraMode;
    if (mode == nullptr || !engine::IsAlive(mode)) {
        gModeName = "none";
        return false;
    }
    std::string prefix;
    if (mode->IsA(USFXCameraMode_Interpolate::StaticClass())) {
        USFXCameraMode* const to = static_cast<USFXCameraMode_Interpolate*>(mode)->To;
        if (to == nullptr || !engine::IsAlive(to)) {
            gModeName = "SFXCameraMode_Interpolate";
            return false;
        }
        prefix = "SFXCameraMode_Interpolate to ";
        mode = to;
    }
    ModeEntry const& entry = Mode(mode->Class);
    gModeName = prefix + entry.name;
    return entry.allowed;
}

ASFXInventoryManager* PlayerInventory(APlayerController* controller) {
    APawn* const pawn = controller != nullptr ? controller->Pawn : nullptr;
    AInventoryManager* const manager = pawn != nullptr ? pawn->InvManager : nullptr;
    if (manager == nullptr || !manager->IsA(ASFXInventoryManager::StaticClass())) {
        return nullptr;
    }
    return static_cast<ASFXInventoryManager*>(manager);
}

// Aim assist's zoom snap (SFXModule_AimAssist.ZoomSnap, from SetZoomed while zoomed: the support grip's
// aimed fire) picks a target that the camera then turns the controller to (SFXCameraTransition_ZoomSnap,
// PC.SetRotation): with the hand aiming that only turns the world. Dropping the target before the camera
// update keeps the transition from starting. True if one was dropped.
bool DropZoomSnap(APlayerController* controller) {
    bool dropped = false;
    TArray<USFXModule*>& modules = controller->Modules;
    for (int i = 0; i < static_cast<int>(modules.Count()); ++i) {
        USFXModule* const module = modules.GetData()[i];
        if (module != nullptr && module->IsA(USFXModule_AimAssist::StaticClass())) {
            auto* const assist = static_cast<USFXModule_AimAssist*>(module);
            if (assist->LastZoomSnapTarget != nullptr) {
                assist->LastZoomSnapTarget = nullptr;
                dropped = true;
            }
        }
    }
    return dropped;
}

// One controller tick: measure what the game did to the rotation, and level it when the hold applies.
void Level(APlayerController* controller, bool fromModTick) {
    double const now = Now();
    if (reinterpret_cast<std::uintptr_t>(controller) != gController) {
        gController = reinterpret_cast<std::uintptr_t>(controller);  // a new level or a new controller
        gHaveLeft = false;
        gRecoilSeenValid = false;
    }
    FRotator& rotation = controller->Rotation;
    int const pitch = rotation.Pitch;
    int const roll = rotation.Roll;
    gPitch = pitch;
    gYaw = rotation.Yaw;
    gRoll = roll;
    gHaveRotation = true;
    int const gameDelta = gHaveLeft ? vl::Signed(pitch - gLeftPitch) : 0;

    vl::Inputs in;
    in.enabled = hand_calibration::LevelCamera();
    in.headTrackedView = gLink.headTrackedView;
    in.headAim = gLink.headAim;
    in.handAim = gLink.handAim;
    in.onFoot = head_tracking::OnFoot();
    APawn* const pawn = controller->Pawn;
    in.pawn = pawn != nullptr;
    if (pawn != nullptr) {
        in.ladder = pawn->Physics == kPhysLadder;
        if (pawn->IsA(ABioPawn::StaticClass())) {
            auto* const bio = static_cast<ABioPawn*>(pawn);
            in.cover = bio->CoverType != 0 || bio->CoverAction != 0;
        }
    }
    in.cameraModeAllowed = CameraModeAllowed(controller);
    vl::Why const why = vl::Decide(in);
    LogWhy(why, now);
    bool const wasHeld = gWhy == vl::Why::Holding;
    gWhy = why;

    bool const held = why == vl::Why::Holding;
    int correction = 0;
    if (held) {
        correction = vl::Correction(pitch);
        // Taking hold of a pitched camera (the setting turned on, back from cover or a conversation): it
        // levels at once, a snap rather than a smooth turn of the world.
        if (!wasHeld && std::abs(correction) >= kLogThresholdUnits && now - gSnapLoggedAt >= kWhyLogSeconds) {
            gSnapLoggedAt = now;
            log::Info("view level: took hold of a camera pitched {}{}: levelled at once", Angle(pitch),
                roll != 0 ? std::format(" and rolled {}", Angle(roll)) : std::string());
        }
        if (pitch != 0 || roll != 0) {
            rotation.Pitch = 0;
            rotation.Roll = 0;
        }
        bool const dropped = DropZoomSnap(controller);
        if (dropped && !gZoomSnapPending) {
            gWindow.ZoomSnap(now);
        }
        gZoomSnapPending = dropped;
        if (fromModTick) {
            ++gFallbackHolds;
        }
    } else {
        gZoomSnapPending = false;
    }
    gLeftPitch = rotation.Pitch;
    gHaveLeft = true;

    gWindow.Frame(now, pitch, roll, gameDelta, correction, held);
    if (gChanges.Feed(now, gameDelta, roll)) {
        vl::ChangeLog::Line const line = gChanges.Take(now);
        log::Info("view level: the game moved the camera's pitch {} in {:.1f} s ({} shots, recoil queued {}){}; ControlRotation as "
                  "the game left it: pitch {}, yaw {}, roll {}; {}",
            Delta(line.pitch), line.seconds, line.shots, Delta(line.recoil),
            line.rollTo != line.rollFrom ? std::format(", roll {} -> {}", Angle(line.rollFrom), Angle(line.rollTo)) : std::string(),
            Angle(pitch), Angle(gYaw), Angle(roll), held ? "put back to level" : std::format("kept ({})", vl::Name(why)));
    }

    if (ASFXInventoryManager* const inventory = PlayerInventory(controller)) {
        gRecoilSeen = inventory->WeaponRecoilOffset.Pitch;
        gRecoilSeenValid = true;
    } else {
        gRecoilSeenValid = false;
    }
}

void OnPlayerTick(UObject* context, UFunction* /*function*/, void* /*parms*/, void* /*result*/) {
    APlayerController* const controller = engine::LocalPlayerController();
    if (controller == nullptr || context != controller) {
        return;
    }
    ++gPosts;
    Level(controller, false);
}

// One of the player's shots (SFXPawn_Player.OnWeaponFired -> SetWeaponRecoil(GetRecoil() * 182.044)):
// count it, measure its kick, and pulse the hand.
void OnSetWeaponRecoil(UObject* context, UFunction* /*function*/, void* /*result*/) {
    APlayerController* const controller = engine::LocalPlayerController();
    ASFXInventoryManager* const inventory = PlayerInventory(controller);
    if (inventory == nullptr || context != inventory) {
        return;
    }
    int const now = inventory->WeaponRecoilOffset.Pitch;
    int const kick = gRecoilSeenValid ? vl::Signed(now - gRecoilSeen) : 0;
    gRecoilSeen = now;
    gRecoilSeenValid = true;
    ++gShots;
    gRecoilTotal += kick;
    double const at = Now();
    gWindow.Shot(at, kick);
    gChanges.Shot(kick);
    if (!gToldFirstShot) {
        gToldFirstShot = true;
        log::Info("view level: first shot seen (SFXInventoryManager.SetWeaponRecoil): pitch kick {} queued for the controller's "
                  "rotation; TotalRecoil pitch {}", Angle(kick), Angle(inventory->TotalRecoil.Pitch));
    }
    double const scale = hand_calibration::RecoilHaptic();
    vl::Pulse const pulse = vl::RecoilPulse(vl::Degrees(kick), scale);
    if (gLink.haptic != nullptr && pulse.amplitude > 0.0f) {
        gLink.haptic(1, pulse.seconds, pulse.amplitude);
        if (support_grip::Held()) {
            gLink.haptic(0, pulse.seconds, pulse.amplitude * 0.6f);
        }
        ++gPulses;
    }
}

#endif

} // namespace

bool TryStartup() {
#if defined(SDK_TARGET_LE1)
    // PlayerTick is an event the native controller tick sends through ProcessEvent; FindFunction picks
    // the current state's version, so every PlayerController / BioPlayerController one is registered.
    gPlayerTicks.clear();
    std::string names;
    for (UFunction* function : engine::FindFunctionsNamed(L"PlayerTick")) {
        std::string const full = engine::FullName(function);
        if (full.starts_with("Function Engine.PlayerController.") || full.starts_with("Function SFXGame.BioPlayerController.")) {
            gPlayerTicks.push_back(function);
            names += std::format("{}{}", names.empty() ? "" : ", ", full.substr(9));
        }
    }
    gSetWeaponRecoil = engine::FindFunction(L"SetWeaponRecoil", kSetWeaponRecoil);
    if (gPlayerTicks.empty()) {
        log::Warn("view level: PlayerController.PlayerTick not found: the camera is levelled from the mod's tick (a frame late)");
    }
    for (UFunction* function : gPlayerTicks) {
        engine::process_event::AddPostHandler(function, &OnPlayerTick);
    }
    if (gSetWeaponRecoil != nullptr) {
        engine::call_function::AddPostHandler(gSetWeaponRecoil, &OnSetWeaponRecoil);
    } else {
        log::Warn("view level: SFXInventoryManager.SetWeaponRecoil not found: shots aren't counted and recoil isn't felt");
    }
    gStarted = true;
    log::Info("view level: handlers on {} ({} PlayerTick functions){}", names.empty() ? std::string("none") : names, gPlayerTicks.size(),
        gSetWeaponRecoil != nullptr ? "; shots from SFXInventoryManager.SetWeaponRecoil" : "");
    return true;
#else
    return false;
#endif
}

void Tick(Link const& link) {
    gLink = link;
#if defined(SDK_TARGET_LE1)
    if (!gStarted) {
        return;
    }
    // No PlayerTick post since the last few mod ticks while there's a controller: hold from here instead.
    if (gPosts == gPostsAtTick) {
        ++gTicksWithoutPost;
    } else {
        gTicksWithoutPost = 0;
    }
    gPostsAtTick = gPosts;
    if (gTicksWithoutPost >= kFallbackTicks) {
        if (APlayerController* const controller = engine::LocalPlayerController()) {
            if (!gToldFallback) {
                gToldFallback = true;
                log::Warn("view level: no PlayerTick seen for {} ticks: levelling from the mod's tick instead (after the camera "
                          "update: a kick shows for one frame)", gTicksWithoutPost);
            }
            Level(controller, true);
        }
    }
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
    vl::Summary const s = gWindow.Last(Now());
    return std::format("{{\"enabled\": {}, \"why\": {}, \"cameraMode\": {}, \"pitchUU\": {}, \"pitchDeg\": {}, \"yawUU\": {}, "
                       "\"rollUU\": {}, \"shots\": {}, \"recoilDeg\": {}, \"lastMinute\": {{\"seconds\": {}, \"gameDriftDeg\": {}, "
                       "\"correctedDeg\": {}, \"recoilDeg\": {}, \"shots\": {}, \"minPitchDeg\": {}, \"maxPitchDeg\": {}, "
                       "\"heldFrames\": {}, \"frames\": {}, \"zoomSnaps\": {}}}}}",
        hand_calibration::LevelCamera() ? "true" : "false", json::JsonString(vl::Name(gWhy)), json::JsonString(gModeName),
        vl::Signed(gPitch), json::JsonNumber(vl::Degrees(vl::Signed(gPitch))), vl::Signed(gYaw), vl::Signed(gRoll), gShots,
        json::JsonNumber(static_cast<double>(gRecoilTotal) * 360.0 / vl::kUnitsPerTurn), json::JsonNumber(s.seconds),
        json::JsonNumber(static_cast<double>(s.gameDrift) * 360.0 / vl::kUnitsPerTurn),
        json::JsonNumber(static_cast<double>(s.corrected) * 360.0 / vl::kUnitsPerTurn),
        json::JsonNumber(static_cast<double>(s.recoil) * 360.0 / vl::kUnitsPerTurn), s.shots, json::JsonNumber(vl::Degrees(s.minPitch)),
        json::JsonNumber(vl::Degrees(s.maxPitch)), s.heldFrames, s.frames, s.zoomSnaps);
}

void LogStats() {
    if (!gStarted) {
        return;
    }
    vl::Summary const s = gWindow.Last(Now());
    std::string const rotation = gHaveRotation
        ? std::format("ControlRotation as the game left it: pitch {}, yaw {}, roll {}", Angle(gPitch), Angle(gYaw), Angle(gRoll))
        : std::string("no controller tick seen");
    std::string const minute = s.any
        ? std::format("last {:.0f} s: the game moved the pitch {} (recoil queued {} by {} shots; aim assist and the rest {}), pitch "
                      "as left {:+.2f}..{:+.2f} deg, roll {:+.2f}..{:+.2f} deg; the hold put back {} in {} of {} ticks; {} zoom "
                      "snaps dropped",
              s.seconds, Delta(s.gameDrift), Delta(s.recoil), s.shots, Delta(s.gameDrift - s.recoil), vl::Degrees(s.minPitch),
              vl::Degrees(s.maxPitch), vl::Degrees(s.minRoll), vl::Degrees(s.maxRoll), Delta(s.corrected), s.heldFrames, s.frames,
              s.zoomSnaps)
        : std::string("no ticks in the last minute");
    log::Info("    view level: {} (camera mode {}; {} state changes); {}; {}; {} shots in all, recoil {}; {} PlayerTick posts{}{}",
        vl::Name(gWhy), gModeName, gWhyChanges, rotation, minute, gShots, Delta(gRecoilTotal), gPosts,
        gFallbackHolds > 0 ? std::format(", {} holds from the mod's tick", gFallbackHolds) : std::string(),
        gPulses > 0 ? std::format(", {} recoil pulses", gPulses) : std::string());
    gWhyChanges = 0;
    gFallbackHolds = 0;
    gPulses = 0;
}

void Shutdown() {
    gLink = {};
    gStarted = false;
    gPlayerTicks.clear();
    gSetWeaponRecoil = nullptr;
    gWindow.Clear();
    gChanges = vl::ChangeLog{kLogThresholdUnits, kLogMinSeconds};
    gController = 0;
    gHaveLeft = false;
    gLeftPitch = 0;
    gPitch = gYaw = gRoll = 0;
    gHaveRotation = false;
    gWhy = vl::Why::NoView;
    gLoggedWhy = vl::Why::NoView;
    gWhyLogged = false;
    gWhyLoggedAt = -1e9;
    gWhyChanges = 0;
    gModeName = "none";
    gPosts = gPostsAtTick = 0;
    gTicksWithoutPost = 0;
    gToldFallback = false;
    gFallbackHolds = 0;
    gRecoilSeenValid = false;
    gRecoilSeen = 0;
    gShots = 0;
    gRecoilTotal = 0;
    gZoomSnapPending = false;
    gPulses = 0;
    gToldFirstShot = false;
    gSnapLoggedAt = -1e9;
    gModes.clear();
}

} // namespace mevr::game::view_level
