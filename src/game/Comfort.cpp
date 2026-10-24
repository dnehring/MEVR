// SPDX-License-Identifier: MIT
#include "game/Comfort.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/Comfort.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "core/vr/ViewLevel.hpp"
#include "engine/Skeleton.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/SquadWeapons.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <numbers>
#include <optional>
#include <string>
#include <vector>

namespace mevr::game::comfort {
namespace {

namespace cf = vr::comfort;
namespace vl = vr::view_level;

// The pawn's facing follows the controller's (combat modes: UpdateRotation's FaceRotation) when they are this close:
// it turns with ours at once.
constexpr int kFollowUnits = 364;  // 2 degrees
constexpr double kWhyLogSeconds = 2.0;
constexpr unsigned kSnapLines = 20;  // each snap logged up to this many; the heartbeat counts the rest

enum class Why {
    Off,          // turn = game
    NoHost,       // the host is older than v17
    NoReflection, // our reflection layer is off (research/34)
    NoView,       // no head-tracked world view (Halcyon's menu, flat screens, no VR)
    MainMenu,     // our VR main menu is open
    NotOnFoot,    // the Mako, a wheel, a conversation, a menu, a cutscene
    NoPawn,
    Cover,
    Ladder,
    CameraMode,   // the sniper scope, cover, cinematic or photo modes
    SquadMenu,    // the squad menu has the sticks
    NoControllers,
    Turning,      // the mod turns
};

char const* Name(Why why) {
    switch (why) {
    case Why::Off: return "the game turns ([Comfort] turn = game)";
    case Why::NoHost: return "the game turns (the host ASI is older than v17: close the game and redeploy it)";
    case Why::NoReflection: return "the game turns (our reflection layer is off: see the 'reflect:' log lines)";
    case Why::NoView: return "the game turns (no head-tracked world view)";
    case Why::MainMenu: return "the game turns (our menu is open)";
    case Why::NotOnFoot: return "the game turns (not on foot)";
    case Why::NoPawn: return "the game turns (no pawn)";
    case Why::Cover: return "the game turns (in cover)";
    case Why::Ladder: return "the game turns (on a ladder)";
    case Why::CameraMode: return "the game turns (a camera mode of its own: the scope, cover, cinematics)";
    case Why::SquadMenu: return "the game turns (the squad menu has the sticks)";
    case Why::NoControllers: return "the game turns (no tracked controllers)";
    case Why::Turning: return "the mod turns the player";
    }
    return "?";
}

cf::Settings gSettings;
cf::SnapTurn gSnap;
cf::YawUnits gUnits;
cf::VignetteDrive gVignette;
bool gOwn = false;           // this tick: the mod turns (published as kFlagModTurns)
bool gOwnPublished = false;  // last tick's: the host has kept the stick's X from the game since
bool gHandWalk = false;
Why gWhy = Why::Off;
Why gLoggedWhy = Why::Off;
bool gWhyLogged = false;
double gWhyLoggedAt = -1e9;
float gLastVignette = 0.0f;
bool gToldPawnFollow = false;
std::string gModeName = "none";

// Since the heartbeat, and in all.
struct Counts {
    unsigned snaps = 0;
    double snapDegrees = 0.0;
    double smoothDegrees = 0.0;
    unsigned pawnTurns = 0;
    float vignettePeak = 0.0f;
    unsigned vignetteTicks = 0;
} gCounts;
unsigned gSnapsLogged = 0;
unsigned long long gSnapsTotal = 0;

#if defined(MEVR_GAME_LE1)
// Our reflection layer (ue3/, research/34): each found once, by name, in the running game.
ue3::ClassRef gCameraNativeBase{"SFXGame.SFXCameraNativeBase"};
ue3::ClassRef gCameraModeInterpolate{"SFXGame.SFXCameraMode_Interpolate"};
ue3::ClassRef gBioPawn{"SFXGame.BioPawn"};
ue3::FieldRef gRotation{"Engine.Actor", "Rotation"};
ue3::FieldRef gLocation{"Engine.Actor", "Location"};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics"};
ue3::FieldRef gPawn{"Engine.Controller", "Pawn"};
ue3::FieldRef gMesh{"Engine.Pawn", "Mesh"};
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType"};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction"};
ue3::FieldRef gPlayerCamera{"Engine.PlayerController", "PlayerCamera"};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode"};
ue3::FieldRef gInterpolateTo{"SFXGame.SFXCameraMode_Interpolate", "To"};
ue3::FieldRef gCameraInput{"SFXGame.SFXCameraMode", "Input"};
ue3::FieldRef gMaxCameraRotationSpeed{"SFXGame.SFXCameraInput", "MaxCameraRotationSpeed"};
ue3::FieldRef gCameraSensitivity{"SFXGame.SFXCameraInput", "CameraSensitivity"};
ue3::FieldRef gTimeToReachFullSpeed{"SFXGame.SFXCameraInput", "TimeToReachFullSpeed"};
ue3::FieldRef gStickDeadZone{"SFXGame.SFXCameraInput", "StickDeadZone"};
ue3::FunctionRef gSetRotationRef{"Engine.Actor.SetRotation"};

ue3::Function* gSetRotation = nullptr;
bool gLookedUp = false;
std::vector<ue3::Class*> gModesLogged;  // the camera modes whose game turn rate was logged

ue3::Object* ObjectField(ue3::Object const* object, ue3::FieldRef& field) {
    return field.Read<ue3::Object*>(object).value_or(nullptr);
}
#endif

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

void LogWhy(Why why, double now) {
    gWhy = why;
    if (gWhyLogged && why == gLoggedWhy) {
        return;
    }
    if (gWhyLogged && now - gWhyLoggedAt < kWhyLogSeconds) {
        return;  // flapping (camera blends): it settles in a line
    }
    gWhyLogged = true;
    gLoggedWhy = why;
    gWhyLoggedAt = now;
    log::Info("comfort: {} (camera mode {})", Name(why), gModeName);
}

#if defined(MEVR_GAME_LE1)

// The camera's mode, or the one it blends to; nullptr if none.
ue3::Object* CameraMode(ue3::Object* controller) {
    ue3::Object* const base = ObjectField(controller, gPlayerCamera);
    if (base == nullptr || !ue3::IsA(base, gCameraNativeBase)) {
        return nullptr;
    }
    ue3::Object* const mode = ObjectField(base, gCurrentCameraMode);
    if (mode == nullptr || !ue3::runtime::IsAlive(mode)) {
        return nullptr;
    }
    if (ue3::IsA(mode, gCameraModeInterpolate)) {
        ue3::Object* const to = ObjectField(mode, gInterpolateTo);
        return to != nullptr && ue3::runtime::IsAlive(to) ? to : nullptr;
    }
    return mode;
}

// The game's own stick turn in this camera mode (BioPlayerInput.ComputeStickResponse reads the mode's SFXCameraInput):
// logged once per mode, so a session tells what "the game's turn rate" is.
void LogGameTurnRate(ue3::Object* mode) {
    ue3::Class* const cls = mode != nullptr ? ue3::runtime::Get().ClassOf(mode) : nullptr;
    if (mode == nullptr || std::find(gModesLogged.begin(), gModesLogged.end(), cls) != gModesLogged.end()) {
        return;
    }
    gModesLogged.push_back(cls);
    ue3::Object* const input = ObjectField(mode, gCameraInput);
    if (input == nullptr || !ue3::runtime::IsAlive(input)) {
        log::Info("comfort: the game's own stick turn in {}: no SFXCameraInput", gModeName);
        return;
    }
    ue3::Vector2D const sensitivity = gCameraSensitivity.Read<ue3::Vector2D>(input).value_or(ue3::Vector2D{});
    log::Info("comfort: the game's own stick turn in {}: SFXCameraInput MaxCameraRotationSpeed {:.3f}, CameraSensitivity ({:.3f}, "
              "{:.3f}), TimeToReachFullSpeed {:.3f} s, StickDeadZone {:.3f} (BioPlayerInput.ComputeStickResponse; [Comfort] turn = "
              "game uses it)",
        gModeName, gMaxCameraRotationSpeed.Read<float>(input).value_or(0.0f), sensitivity.x, sensitivity.y,
        gTimeToReachFullSpeed.Read<float>(input).value_or(0.0f), gStickDeadZone.Read<float>(input).value_or(0.0f));
}

double MeshYawDegrees(ue3::Object const* pawn) {
    ue3::Object* const mesh = pawn != nullptr ? ObjectField(pawn, gMesh) : nullptr;
    if (mesh == nullptr) {
        return 0.0;
    }
    ik::Affine const m = engine::skeleton::LocalToWorld(mesh);
    return std::atan2(m.m[0][1], m.m[0][0]) * 180.0 / std::numbers::pi;
}

// Turns the player by `units` (+ right): ControlRotation, the pawn with it if it faces where the controller does,
// and the view anchor told (the camera only shows it after the next update).
void Turn(ue3::Object* controller, int units) {
    if (!gLookedUp) {
        gLookedUp = true;
        gSetRotation = gSetRotationRef.Get();
        if (gSetRotation == nullptr) {
            log::Warn("comfort: Engine.Actor.SetRotation not found: in combat the pawn turns a tick after the view (the game's own "
                      "UpdateRotation)");
        }
    }
    ue3::Rotator* const rotation = gRotation.In<ue3::Rotator>(controller);
    if (rotation == nullptr) {
        return;
    }
    int const before = rotation->yaw;
    rotation->yaw = before + units;
    write_journal::Wrote("Comfort.ControlRotation", rotation);
    ue3::Object* const pawn = ObjectField(controller, gPawn);
    std::optional<ue3::Rotator> const pawnRotation = pawn != nullptr ? gRotation.Read<ue3::Rotator>(pawn) : std::nullopt;
    bool pawnTurned = false;
    double pivotX = 0.0;
    double pivotY = 0.0;
    if (pawnRotation && gSetRotation != nullptr && std::abs(vl::Signed(pawnRotation->yaw - before)) <= kFollowUnits) {
        double const meshBefore = MeshYawDegrees(pawn);
        ue3::Params parms(ue3::runtime::Get(), gSetRotation);
        ue3::Rotator turned = *pawnRotation;
        turned.yaw += units;
        parms.Set("NewRotation", turned);
        ue3::runtime::Call(pawn, gSetRotation, parms);  // the native through ProcessEvent
        pawnTurned = true;
        ue3::Vector const at = gLocation.Read<ue3::Vector>(pawn).value_or(ue3::Vector{});
        pivotX = at.x;
        pivotY = at.y;
        ++gCounts.pawnTurns;
        if (!gToldPawnFollow) {
            gToldPawnFollow = true;
            double const meshTurn = std::remainder(MeshYawDegrees(pawn) - meshBefore, 360.0);
            log::Info("comfort: the pawn faced where the controller does, so it turned with it (Actor.SetRotation{}): its mesh "
                      "turned {:+.1f} deg at once (the turn {:+.1f} deg){}",
                parms.ReturnBool().value_or(false) ? "" : " refused", meshTurn, static_cast<double>(units) * 360.0 / vl::kUnitsPerTurn,
                std::abs(meshTurn - static_cast<double>(units) * 360.0 / vl::kUnitsPerTurn) < 1.0
                    ? ""
                    : " [the mesh's transform waits for the game's update: the arms may lag a frame in combat]");
        }
    }
    view_anchor::NoteTurn(units, pawnTurned, pivotX, pivotY);
}

#endif

} // namespace

void Tick(double deltaSeconds, Link const& link) {
    double const now = Now();
    gSettings = hand_calibration::GetComfort();
    vr::controllers::State const controllers = vr::controllers::Latest();
    bool const tracked = controllers.active;
    float const stickX = tracked ? controllers.right.stickX : 0.0f;
    float const moveX = tracked ? controllers.left.stickX : 0.0f;
    float const moveY = tracked ? controllers.left.stickY : 0.0f;
    bool const onFoot = head_tracking::OnFoot();
    bool const squad = squad_weapons::IsOpen();
    Why why = Why::Turning;
    bool turnedThisTick = false;
    bool snapped = false;
#if defined(MEVR_GAME_LE1)
    ue3::Object* const controller = ue3::runtime::LocalPlayerController();
    ue3::Object* const pawn = controller != nullptr ? ObjectField(controller, gPawn) : nullptr;
    ue3::Object* const mode = controller != nullptr ? CameraMode(controller) : nullptr;
    gModeName = mode != nullptr ? ue3::runtime::Get().NameText(ue3::runtime::Get().ClassOf(mode)) : std::string("none");
    bool cover = false;
    bool ladder = false;
    if (pawn != nullptr) {
        ladder = gPhysics.Read<std::uint8_t>(pawn) == std::uint8_t{9};  // PHYS_Ladder
        if (ue3::IsA(pawn, gBioPawn)) {
            cover = gCoverType.Read<std::uint8_t>(pawn).value_or(0) != 0 || gCoverAction.Read<std::uint8_t>(pawn).value_or(0) != 0;
        }
    }
    if (gSettings.turn == cf::TurnStyle::Game) {
        why = Why::Off;
    } else if (!link.supported) {
        why = Why::NoHost;
    } else if (!ue3::runtime::Ready()) {
        why = Why::NoReflection;
    } else if (!link.headTrackedView) {
        why = Why::NoView;
    } else if (link.mainMenuOpen) {
        why = Why::MainMenu;
    } else if (!onFoot) {
        why = Why::NotOnFoot;
    } else if (controller == nullptr || pawn == nullptr) {
        why = Why::NoPawn;
    } else if (cover) {
        why = Why::Cover;
    } else if (ladder) {
        why = Why::Ladder;
    } else if (mode == nullptr || !vl::CameraModeAllows(gModeName)) {
        why = Why::CameraMode;
    } else if (squad) {
        why = Why::SquadMenu;
    } else if (!tracked) {
        why = Why::NoControllers;
    }
    if (onFoot && link.headTrackedView && mode != nullptr) {
        LogGameTurnRate(mode);
    }
    bool const own = why == Why::Turning;
    // Only once the host has kept the stick from the game (last tick's flags): never both turning in one tick.
    bool const turnNow = own && gOwnPublished;
    if (turnNow) {
        double degrees = 0.0;
        if (gSettings.turn == cf::TurnStyle::Snap) {
            degrees = cf::StepSnap(gSnap, stickX, now, gSettings);
            snapped = degrees != 0.0;
        } else {
            degrees = cf::SmoothTurnDegrees(stickX, deltaSeconds, gSettings);
            gSnap.Reset();
        }
        int const units = gUnits.Take(degrees);
        if (units != 0) {
            Turn(controller, units);
        }
        turnedThisTick = degrees != 0.0;
        if (snapped) {
            ++gCounts.snaps;
            ++gSnapsTotal;
            gCounts.snapDegrees += std::abs(degrees);
            if (gSnapsLogged < kSnapLines) {
                ++gSnapsLogged;
                log::Info("comfort: snap {} {:.0f} deg (stick {:+.2f}; ControlRotation yaw now {:+.1f} deg){}", degrees > 0.0 ? "right" : "left",
                    std::abs(degrees), stickX, vl::Degrees(vl::Signed(gRotation.Read<ue3::Rotator>(controller).value_or(ue3::Rotator{}).yaw)),
                    gSnapsLogged == kSnapLines ? "; further snaps are counted in the heartbeat" : "");
            }
        } else {
            gCounts.smoothDegrees += std::abs(degrees);
        }
    } else {
        gSnap.Reset();
        gUnits.Reset();
    }
    gOwn = own;
    gOwnPublished = own;  // support_grip publishes it later this tick
#else
    (void)deltaSeconds;
    why = Why::NoHost;
    gOwn = gOwnPublished = false;
#endif
    LogWhy(why, now);

    // Walking toward the left hand (the host turns the stick): on foot, in the view, outside menus and wheels.
    gHandWalk = link.supported && gSettings.walk == cf::WalkToward::LeftHand && link.headTrackedView && !link.mainMenuOpen && onFoot &&
        !squad;

    // The vignette: while the stick turns the player (ours or the game's), or moves him; and after each snap.
    bool const inPlay = link.headTrackedView && !link.mainMenuOpen && onFoot && !squad && tracked;
    bool const turning = inPlay && (gOwn ? (gSettings.turn == cf::TurnStyle::Smooth ? turnedThisTick : false)
                                         : cf::StickTurns(stickX, gSettings));
    bool const moving = inPlay && std::hypot(moveX, moveY) > gSettings.moveThreshold;
    float const vignette = gVignette.Target(turning, moving, snapped, now, gSettings);
    if (link.setVignette != nullptr) {
        link.setVignette(vignette);
    }
    gLastVignette = vignette;
    if (vignette > 0.0f) {
        ++gCounts.vignetteTicks;
        gCounts.vignettePeak = std::max(gCounts.vignettePeak, vignette);
    }
}

void AdjustPad(gamepad::Options& options) {
    options.modTurns = gOwn;
    options.handWalk = gHandWalk;
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"turn\": {}, \"snapDeg\": {}, \"turnSpeed\": {}, \"why\": {}, \"modTurns\": {}, \"cameraMode\": {}, "
                       "\"snaps\": {}, \"vignette\": {{\"on\": {}, \"strength\": {}, \"when\": {}, \"target\": {}}}, \"walk\": {}, "
                       "\"handWalk\": {}}}",
        json::JsonString(cf::Name(gSettings.turn)), json::JsonNumber(gSettings.snapDegrees), json::JsonNumber(gSettings.turnSpeed),
        json::JsonString(Name(gWhy)), gOwn ? "true" : "false", json::JsonString(gModeName), gSnapsTotal,
        gSettings.vignette ? "true" : "false", json::JsonNumber(gSettings.vignetteStrength),
        json::JsonString(cf::Name(gSettings.vignetteWhen)), json::JsonNumber(gLastVignette), json::JsonString(cf::Name(gSettings.walk)),
        gHandWalk ? "true" : "false");
}

void LogStats() {
    log::Info("    comfort: {} (camera mode {}); last 5 s: {} snaps ({:.0f} deg), smooth {:.0f} deg, the pawn turned with it {} times; "
              "vignette wanted in {} ticks (peak {:.0f}%); walking toward {}",
        Name(gWhy), gModeName, gCounts.snaps, gCounts.snapDegrees, gCounts.smoothDegrees, gCounts.pawnTurns, gCounts.vignetteTicks,
        gCounts.vignettePeak * 100.0f, gHandWalk ? "the left hand" : "where the player looks");
    gCounts = Counts{};
}

void Shutdown() {
    gSettings = {};
    gSnap = {};
    gUnits = {};
    gVignette = {};
    gOwn = gOwnPublished = false;
    gHandWalk = false;
    gWhy = gLoggedWhy = Why::Off;
    gWhyLogged = false;
    gWhyLoggedAt = -1e9;
    gLastVignette = 0.0f;
    gToldPawnFollow = false;
    gModeName = "none";
    gCounts = Counts{};
    gSnapsLogged = 0;
    gSnapsTotal = 0;
#if defined(MEVR_GAME_LE1)
    gSetRotation = nullptr;
    gLookedUp = false;
    gModesLogged.clear();
#endif
}

} // namespace mevr::game::comfort
