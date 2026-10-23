// SPDX-License-Identifier: MIT
#include "game/ViewLevel.hpp"

#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/vr/ViewLevel.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/SupportGrip.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <optional>
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
std::vector<ue3::Function*> gPlayerTicks;
ue3::Function* gSetWeaponRecoil = nullptr;

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

// Aim assist's friction and adhesion (research/24 section 7): CallFunction post-handlers on
// SFXModule_AimAssist.ViewFriction and ViewAdhesion zero their results while the hand aims.
ue3::Function* gViewFriction = nullptr;
ue3::Function* gViewAdhesion = nullptr;
vl::AssistWhy gAssistWhy = vl::AssistWhy::NoView;
vl::AssistWhy gLoggedAssistWhy = vl::AssistWhy::NoView;
bool gAssistLogged = false;
double gAssistLoggedAt = -1e9;
unsigned gAssistChanges = 0;  // since the heartbeat
unsigned long long gFrictionCalls = 0;  // the local player's, in all
unsigned long long gAdhesionCalls = 0;
bool gToldFriction = false;
bool gToldAdhesion = false;

// Per camera mode class: its short name, whether the hold applies in it, and whether aim assist's
// friction and adhesion are removed in it.
struct ModeEntry {
    ue3::Class* cls;
    std::string name;
    bool allowed;
    bool assistOff;
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

#if defined(MEVR_GAME_LE1)

// Our reflection layer (ue3/, research/34): each found once, by name, in the running game.
ue3::ClassRef gCameraNativeBase{"SFXGame.SFXCameraNativeBase"};
ue3::ClassRef gCameraModeInterpolate{"SFXGame.SFXCameraMode_Interpolate"};
ue3::ClassRef gBioPawn{"SFXGame.BioPawn"};
ue3::ClassRef gSfxInventory{"SFXGame.SFXInventoryManager"};
ue3::ClassRef gAimAssistModule{"SFXGame.SFXModule_AimAssist"};
ue3::FieldRef gRotation{"Engine.Actor", "Rotation"};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics"};
ue3::FieldRef gModules{"Engine.Actor", "Modules"};
ue3::FieldRef gPawn{"Engine.Controller", "Pawn"};
ue3::FieldRef gInvManager{"Engine.Pawn", "InvManager"};
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType"};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction"};
ue3::FieldRef gWeaponRecoilOffset{"SFXGame.SFXInventoryManager", "WeaponRecoilOffset"};
ue3::FieldRef gTotalRecoil{"SFXGame.SFXInventoryManager", "TotalRecoil"};
ue3::FieldRef gLastZoomSnapTarget{"SFXGame.SFXModule_AimAssist", "LastZoomSnapTarget"};
ue3::FieldRef gPlayerCamera{"Engine.PlayerController", "PlayerCamera"};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode"};
ue3::FieldRef gInterpolateTo{"SFXGame.SFXCameraMode_Interpolate", "To"};
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput"};
ue3::FieldRef gRawJoyLookRight{"Engine.PlayerInput", "RawJoyLookRight"};
ue3::FieldRef gRawJoyLookUp{"Engine.PlayerInput", "RawJoyLookUp"};

ue3::Object* ObjectField(ue3::Object const* object, ue3::FieldRef& field) {
    return field.Read<ue3::Object*>(object).value_or(nullptr);
}

ue3::Object* PawnOf(ue3::Object const* controller) {
    return ObjectField(controller, gPawn);
}

ue3::FunctionRef gSetWeaponRecoilRef{"SFXGame.SFXInventoryManager.SetWeaponRecoil"};
ue3::FunctionRef gViewFrictionRef{"SFXGame.SFXModule_AimAssist.ViewFriction"};
ue3::FunctionRef gViewAdhesionRef{"SFXGame.SFXModule_AimAssist.ViewAdhesion"};
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

ModeEntry const& Mode(ue3::Class* cls) {
    for (ModeEntry const& entry : gModes) {
        if (entry.cls == cls) {
            return entry;
        }
    }
    std::string name = cls != nullptr ? ue3::runtime::Get().NameText(cls) : std::string("none");
    bool const allowed = vl::CameraModeAllows(name);
    bool const assistOff = vl::CameraModeRemovesAssist(name);
    gModes.push_back({cls, std::move(name), allowed, assistOff});
    return gModes.back();
}

// The camera's mode, or the one it blends to (`blending` set); nullptr if there's none (or a blend
// toward none, `blending` set too).
ModeEntry const* CurrentMode(ue3::Object* controller, bool& blending) {
    blending = false;
    ue3::Object* const base = ObjectField(controller, gPlayerCamera);
    if (base == nullptr || !ue3::IsA(base, gCameraNativeBase)) {
        return nullptr;
    }
    ue3::Object* mode = ObjectField(base, gCurrentCameraMode);
    if (mode == nullptr || !ue3::runtime::IsAlive(mode)) {
        return nullptr;
    }
    if (ue3::IsA(mode, gCameraModeInterpolate)) {
        blending = true;
        ue3::Object* const to = ObjectField(mode, gInterpolateTo);
        if (to == nullptr || !ue3::runtime::IsAlive(to)) {
            return nullptr;
        }
        mode = to;
    }
    return &Mode(ue3::runtime::Get().ClassOf(mode));
}

// The camera's mode (or the one it blends to), named in gModeName.
ModeEntry const* NamedMode(ue3::Object* controller) {
    bool blending = false;
    ModeEntry const* const entry = CurrentMode(controller, blending);
    if (entry == nullptr) {
        gModeName = blending ? "SFXCameraMode_Interpolate" : "none";
    } else {
        gModeName = blending ? "SFXCameraMode_Interpolate to " + entry->name : entry->name;
    }
    return entry;
}

// Whether aim assist's friction and adhesion are removed for this controller now (`mode`: its camera mode).
vl::AssistWhy DecideAssist(ue3::Object* controller, ModeEntry const* mode) {
    vl::AssistInputs in;
    in.keep = hand_calibration::AimAssist();
    in.headTrackedView = gLink.headTrackedView;
    in.headAim = gLink.headAim;
    in.handAim = gLink.handAim;
    in.onFoot = head_tracking::OnFoot();
    in.pawn = PawnOf(controller) != nullptr;
    in.cameraModeRemoves = mode != nullptr && mode->assistOff;
    return vl::DecideAssist(in);
}

void LogAssistWhy(vl::AssistWhy why, double now) {
    if (why != gAssistWhy) {
        ++gAssistChanges;
    }
    gAssistWhy = why;
    if (gAssistLogged && why == gLoggedAssistWhy) {
        return;
    }
    if (gAssistLogged && now - gAssistLoggedAt < kWhyLogSeconds) {
        return;  // flapping (camera blends): the heartbeat has the count, and the state settles in a line
    }
    gAssistLogged = true;
    gLoggedAssistWhy = why;
    gAssistLoggedAt = now;
    log::Info("aim assist: {} (camera mode {})", vl::Name(why), gModeName);
}

ue3::Object* PlayerInventory(ue3::Object* controller) {
    ue3::Object* const pawn = controller != nullptr ? PawnOf(controller) : nullptr;
    ue3::Object* const manager = pawn != nullptr ? ObjectField(pawn, gInvManager) : nullptr;
    return ue3::As(manager, gSfxInventory);
}

std::optional<int> RecoilPitch(ue3::Object* inventory, ue3::FieldRef& field) {
    std::optional<ue3::Rotator> const rotator = field.Read<ue3::Rotator>(inventory);
    return rotator ? std::optional<int>(rotator->pitch) : std::nullopt;
}

// Aim assist's zoom snap (SFXModule_AimAssist.ZoomSnap, from SetZoomed while zoomed: the support grip's
// aimed fire) picks a target that the camera then turns the controller to (SFXCameraTransition_ZoomSnap,
// PC.SetRotation): with the hand aiming that only turns the world. Dropping the target before the camera
// update keeps the transition from starting. True if one was dropped.
bool DropZoomSnap(ue3::Object* controller) {
    bool dropped = false;
    ue3::TArray<ue3::Object*> const* const modules = gModules.In<ue3::TArray<ue3::Object*>>(controller);
    if (modules == nullptr) {
        return false;
    }
    for (int i = 0; i < modules->Count(); ++i) {
        ue3::Object* const module = modules->data[i];
        if (module != nullptr && ue3::IsA(module, gAimAssistModule)) {
            if (ue3::Object** const target = gLastZoomSnapTarget.In<ue3::Object*>(module); target != nullptr && *target != nullptr) {
                *target = nullptr;
                dropped = true;
            }
        }
    }
    return dropped;
}

// One controller tick: measure what the game did to the rotation, and level it when the hold applies.
void Level(ue3::Object* controller, bool fromModTick) {
    double const now = Now();
    if (reinterpret_cast<std::uintptr_t>(controller) != gController) {
        gController = reinterpret_cast<std::uintptr_t>(controller);  // a new level or a new controller
        gHaveLeft = false;
        gRecoilSeenValid = false;
    }
    ue3::Rotator* const controlRotation = gRotation.In<ue3::Rotator>(controller);
    if (controlRotation == nullptr) {
        return;  // the layer said why, once
    }
    ue3::Rotator& rotation = *controlRotation;
    int const pitch = rotation.pitch;
    int const roll = rotation.roll;
    gPitch = pitch;
    gYaw = rotation.yaw;
    gRoll = roll;
    gHaveRotation = true;
    int const gameDelta = gHaveLeft ? vl::Signed(pitch - gLeftPitch) : 0;

    vl::Inputs in;
    in.enabled = hand_calibration::LevelCamera();
    in.headTrackedView = gLink.headTrackedView;
    in.headAim = gLink.headAim;
    in.handAim = gLink.handAim;
    in.onFoot = head_tracking::OnFoot();
    ue3::Object* const pawn = PawnOf(controller);
    in.pawn = pawn != nullptr;
    if (pawn != nullptr) {
        in.ladder = gPhysics.Read<std::uint8_t>(pawn) == kPhysLadder;
        if (ue3::IsA(pawn, gBioPawn)) {
            in.cover = gCoverType.Read<std::uint8_t>(pawn).value_or(0) != 0 || gCoverAction.Read<std::uint8_t>(pawn).value_or(0) != 0;
        }
    }
    ModeEntry const* const mode = NamedMode(controller);
    in.cameraModeAllowed = mode != nullptr && mode->allowed;
    vl::Why const why = vl::Decide(in);
    LogWhy(why, now);
    // Aim assist's state, for its log line and the heartbeat (the handlers decide again, fresh, each call).
    if (gViewFriction != nullptr) {
        LogAssistWhy(DecideAssist(controller, mode), now);
    }
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
            rotation.pitch = 0;
            rotation.roll = 0;
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
    gLeftPitch = rotation.pitch;
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

    std::optional<int> const recoil = RecoilPitch(PlayerInventory(controller), gWeaponRecoilOffset);
    gRecoilSeenValid = recoil.has_value();
    if (recoil) {
        gRecoilSeen = *recoil;
    }
}

void OnPlayerTick(ue3::Object* context, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const controller = ue3::runtime::LocalPlayerController();
    if (controller == nullptr || context != controller) {
        return;
    }
    ++gPosts;
    Level(controller, false);
}

// One of the player's shots (SFXPawn_Player.OnWeaponFired -> SetWeaponRecoil(GetRecoil() * 182.044)):
// count it, measure its kick, and pulse the hand.
void OnSetWeaponRecoil(ue3::Object* context, ue3::Function* /*function*/, void* /*result*/) {
    ue3::Object* const controller = ue3::runtime::LocalPlayerController();
    ue3::Object* const inventory = PlayerInventory(controller);
    std::optional<int> const recoil =
        inventory != nullptr && context == inventory ? RecoilPitch(inventory, gWeaponRecoilOffset) : std::nullopt;
    if (!recoil) {
        return;
    }
    int const now = *recoil;
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
                  "rotation; TotalRecoil pitch {}", Angle(kick), Angle(RecoilPitch(inventory, gTotalRecoil).value_or(0)));
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

// The local player's controller, if `context` is its aim-assist module (SFXModule_AimAssist's Outer is
// the controller: the script reads self.Outer.Pawn).
ue3::Object* AssistController(ue3::Object* context) {
    ue3::Object* const controller = ue3::runtime::LocalPlayerController();
    if (controller == nullptr || context == nullptr || ue3::runtime::Get().OuterOf(context) != controller) {
        return nullptr;
    }
    return controller;
}

bool LookStickTurns(ue3::Object* controller) {
    ue3::Object* const input = ObjectField(controller, gPlayerInput);
    return input != nullptr
        && (gRawJoyLookRight.Read<float>(input).value_or(0.0f) != 0.0f || gRawJoyLookUp.Read<float>(input).value_or(0.0f) != 0.0f);
}

bool AssistRemoved(ue3::Object* controller) {
    bool blending = false;
    return DecideAssist(controller, CurrentMode(controller, blending)) == vl::AssistWhy::Removed;
}

// SFXModule_AimAssist.ViewAdhesion (from ProcessRotation, each tick with a gamepad and a ranged weapon):
// the turn after the target it adds to the stick's DeltaRot. Zeroed while the hand aims.
void OnViewAdhesion(ue3::Object* context, ue3::Function* /*function*/, void* result) {
    ue3::Object* const controller = AssistController(context);
    if (controller == nullptr || result == nullptr) {
        return;
    }
    ++gAdhesionCalls;
    ue3::Rotator& turn = *static_cast<ue3::Rotator*>(result);
    bool const removed = AssistRemoved(controller);
    gWindow.Adhesion(Now(), turn.pitch, turn.yaw, removed);
    if (!gToldAdhesion && (vl::Signed(turn.pitch) != 0 || vl::Signed(turn.yaw) != 0)) {
        gToldAdhesion = true;
        log::Info("aim assist: first adhesion seen (SFXModule_AimAssist.ViewAdhesion): it would have turned the view pitch {}, yaw "
                  "{} this tick; {}", Angle(turn.pitch), Angle(turn.yaw), removed ? "removed" : "kept");
    }
    if (removed && (turn.pitch != 0 || turn.yaw != 0 || turn.roll != 0)) {
        turn.pitch = 0;
        turn.yaw = 0;
        turn.roll = 0;
    }
}

// SFXModule_AimAssist.ViewFriction (right after ViewAdhesion): the share of the stick's turn taken away
// (ProcessRotation: DeltaRot *= 1 - factor). Zeroed while the hand aims.
void OnViewFriction(ue3::Object* context, ue3::Function* /*function*/, void* result) {
    ue3::Object* const controller = AssistController(context);
    if (controller == nullptr || result == nullptr) {
        return;
    }
    ++gFrictionCalls;
    float& factor = *static_cast<float*>(result);
    bool const removed = AssistRemoved(controller);
    bool const turning = LookStickTurns(controller);
    gWindow.Friction(Now(), factor, turning, removed);
    if (!gToldFriction && factor > 0.0f) {
        gToldFriction = true;
        log::Info("aim assist: first friction seen (SFXModule_AimAssist.ViewFriction): {:.0f}% of the stick's turn taken away "
                  "(the look stick {}); {}", factor * 100.0f, turning ? "turning" : "still", removed ? "removed" : "kept");
    }
    if (removed && factor != 0.0f) {
        factor = 0.0f;
    }
}

#endif

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    if (!ue3::runtime::Ready()) {
        log::Warn("view level: our reflection layer is off ({}): the camera isn't levelled", ue3::runtime::Status());
        return false;
    }
    // PlayerTick is an event the native controller tick sends through ProcessEvent; FindFunction picks
    // the current state's version, so every PlayerController / BioPlayerController one is registered.
    gPlayerTicks.clear();
    std::string names;
    ue3::Reflection& reflection = ue3::runtime::Get();
    for (ue3::Function* function : reflection.FindFunctionsNamed("PlayerTick")) {
        std::string const full = reflection.FullName(function);
        if (full.starts_with("Function Engine.PlayerController.") || full.starts_with("Function SFXGame.BioPlayerController.")) {
            gPlayerTicks.push_back(function);
            names += std::format("{}{}", names.empty() ? "" : ", ", full.substr(9));
        }
    }
    gSetWeaponRecoil = gSetWeaponRecoilRef.Get();
    if (gPlayerTicks.empty()) {
        log::Warn("view level: PlayerController.PlayerTick not found: the camera is levelled from the mod's tick (a frame late)");
    }
    for (ue3::Function* function : gPlayerTicks) {
        engine::process_event::AddPostHandler(function, &OnPlayerTick);
    }
    if (gSetWeaponRecoil != nullptr) {
        engine::call_function::AddPostHandler(gSetWeaponRecoil, &OnSetWeaponRecoil);
    } else {
        log::Warn("view level: SFXInventoryManager.SetWeaponRecoil not found: shots aren't counted and recoil isn't felt");
    }
    // Script calls them from script (ProcessRotation), so only the CallFunction hook sees them.
    gViewFriction = gViewFrictionRef.Get();
    gViewAdhesion = gViewAdhesionRef.Get();
    if (gViewFriction != nullptr && gViewAdhesion != nullptr) {
        engine::call_function::AddPostHandler(gViewAdhesion, &OnViewAdhesion);
        engine::call_function::AddPostHandler(gViewFriction, &OnViewFriction);
    } else {
        log::Warn("aim assist: SFXModule_AimAssist.ViewFriction or ViewAdhesion not found: the game's friction and adhesion stay");
        gViewFriction = gViewAdhesion = nullptr;
    }
    gStarted = true;
    log::Info("view level: handlers on {} ({} PlayerTick functions){}{}", names.empty() ? std::string("none") : names, gPlayerTicks.size(),
        gSetWeaponRecoil != nullptr ? "; shots from SFXInventoryManager.SetWeaponRecoil" : "",
        gViewFriction != nullptr ? "; aim assist from SFXModule_AimAssist.ViewFriction and ViewAdhesion" : "");
    return true;
#else
    return false;
#endif
}

void Tick(Link const& link) {
    gLink = link;
#if defined(MEVR_GAME_LE1)
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
        if (ue3::Object* const controller = ue3::runtime::LocalPlayerController()) {
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
    std::string const assist = std::format(
        "{{\"keep\": {}, \"why\": {}, \"frictionCalls\": {}, \"adhesionCalls\": {}, \"lastMinute\": {{\"frictionTicks\": {}, "
        "\"frictionTurning\": {}, \"frictionRemoved\": {}, \"frictionMean\": {}, \"frictionMax\": {}, \"adhesionTicks\": {}, "
        "\"adhesionRemoved\": {}, \"adhesionYawDeg\": {}, \"adhesionPitchDeg\": {}}}}}",
        hand_calibration::AimAssist() ? "true" : "false", json::JsonString(vl::Name(gAssistWhy)), gFrictionCalls, gAdhesionCalls,
        s.frictionTicks, s.frictionTurning, s.frictionRemoved,
        json::JsonNumber(s.frictionTicks > 0 ? s.frictionSum / s.frictionTicks : 0.0), json::JsonNumber(s.frictionMax),
        s.adhesionTicks, s.adhesionRemoved, json::JsonNumber(static_cast<double>(s.adhesionYaw) * 360.0 / vl::kUnitsPerTurn),
        json::JsonNumber(static_cast<double>(s.adhesionPitch) * 360.0 / vl::kUnitsPerTurn));
    return std::format("{{\"enabled\": {}, \"why\": {}, \"cameraMode\": {}, \"pitchUU\": {}, \"pitchDeg\": {}, \"yawUU\": {}, "
                       "\"rollUU\": {}, \"shots\": {}, \"recoilDeg\": {}, \"lastMinute\": {{\"seconds\": {}, \"gameDriftDeg\": {}, "
                       "\"correctedDeg\": {}, \"recoilDeg\": {}, \"shots\": {}, \"minPitchDeg\": {}, \"maxPitchDeg\": {}, "
                       "\"heldFrames\": {}, \"frames\": {}, \"zoomSnaps\": {}}}, \"aimAssist\": {}}}",
        hand_calibration::LevelCamera() ? "true" : "false", json::JsonString(vl::Name(gWhy)), json::JsonString(gModeName),
        vl::Signed(gPitch), json::JsonNumber(vl::Degrees(vl::Signed(gPitch))), vl::Signed(gYaw), vl::Signed(gRoll), gShots,
        json::JsonNumber(static_cast<double>(gRecoilTotal) * 360.0 / vl::kUnitsPerTurn), json::JsonNumber(s.seconds),
        json::JsonNumber(static_cast<double>(s.gameDrift) * 360.0 / vl::kUnitsPerTurn),
        json::JsonNumber(static_cast<double>(s.corrected) * 360.0 / vl::kUnitsPerTurn),
        json::JsonNumber(static_cast<double>(s.recoil) * 360.0 / vl::kUnitsPerTurn), s.shots, json::JsonNumber(vl::Degrees(s.minPitch)),
        json::JsonNumber(vl::Degrees(s.maxPitch)), s.heldFrames, s.frames, s.zoomSnaps, assist);
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

    // Aim assist: how often friction and adhesion would have turned the world (the game's own values).
    if (gViewFriction == nullptr) {
        return;
    }
    std::string const friction = s.frictionTicks > 0
        ? std::format("friction would have taken {:.0f}% of the stick's turn on average ({:.0f}% at most) in {} ticks with a target "
                      "in the soft margin, {} of them while the look stick turned; removed in {}",
              100.0 * s.frictionSum / s.frictionTicks, 100.0 * s.frictionMax, s.frictionTicks, s.frictionTurning, s.frictionRemoved)
        : std::string("friction never engaged");
    std::string const adhesion = s.adhesionTicks > 0
        ? std::format("adhesion would have turned the view in {} ticks (yaw {:.1f} deg, pitch {:.1f} deg in all); removed in {}",
              s.adhesionTicks, static_cast<double>(s.adhesionYaw) * 360.0 / vl::kUnitsPerTurn,
              static_cast<double>(s.adhesionPitch) * 360.0 / vl::kUnitsPerTurn, s.adhesionRemoved)
        : std::string("adhesion never turned the view");
    log::Info("    aim assist: {} ({} state changes); last {:.0f} s: {}; {}; {} ViewFriction and {} ViewAdhesion calls in all",
        vl::Name(gAssistWhy), gAssistChanges, s.seconds, friction, adhesion, gFrictionCalls, gAdhesionCalls);
    gAssistChanges = 0;
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
    gViewFriction = nullptr;
    gViewAdhesion = nullptr;
    gAssistWhy = vl::AssistWhy::NoView;
    gLoggedAssistWhy = vl::AssistWhy::NoView;
    gAssistLogged = false;
    gAssistLoggedAt = -1e9;
    gAssistChanges = 0;
    gFrictionCalls = 0;
    gAdhesionCalls = 0;
    gToldFriction = false;
    gToldAdhesion = false;
    gModes.clear();
}

} // namespace mevr::game::view_level
