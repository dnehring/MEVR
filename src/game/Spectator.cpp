// SPDX-License-Identifier: MIT
#include "game/Spectator.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Engine.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mevr::game::spectator {
namespace {

namespace sp = vr::spectator;

std::string gStatus = "off";
std::string gLoggedStatus;
sp::Frame gLast{};

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34). Every one of these has a row in engine/ReflectCheck (game/ScopeCapture's and the
// cockpit probe's rows: the same classes, fields and functions).
ue3::ClassRef gActorClass{"Engine.SceneCapture2DActor"};
ue3::ClassRef gComponentClass{"Engine.SceneCapture2DComponent"};
ue3::ClassRef gTargetClass{"Engine.TextureRenderTarget2D"};
ue3::ClassRef gBioPawnClass{"SFXGame.BioPawn"};
ue3::FunctionRef gSpawn{"Engine.Actor.Spawn"};
ue3::FunctionRef gDestroy{"Engine.Actor.Destroy"};
ue3::FunctionRef gForceUpdate{"Engine.Actor.ForceUpdateComponents"};
ue3::FunctionRef gCreate{"Engine.TextureRenderTarget2D.Create"};
ue3::FunctionRef gSetEnabled{"Engine.SceneCaptureComponent.SetEnabled"};
ue3::FunctionRef gSetFrameRate{"Engine.SceneCaptureComponent.SetFrameRate"};
ue3::FunctionRef gSetView{"Engine.SceneCapture2DComponent.SetView"};
ue3::FunctionRef gSetParameters{"Engine.SceneCapture2DComponent.SetCaptureParameters"};
ue3::FunctionRef gPrimitiveSetHidden{"Engine.PrimitiveComponent.SetHidden"};
ue3::FieldRef gSceneCapture{"Engine.SceneCaptureActor", "SceneCapture"};
ue3::FieldRef gStatic{"Engine.Actor", "bStatic"};
ue3::FieldRef gNoDelete{"Engine.Actor", "bNoDelete"};
ue3::FieldRef gOwner{"Engine.Actor", "Owner"};
ue3::FieldRef gEnabled{"Engine.SceneCaptureComponent", "bEnabled"};
ue3::FieldRef gEnablePostProcess{"Engine.SceneCaptureComponent", "bEnablePostProcess"};
ue3::FieldRef gEnableFog{"Engine.SceneCaptureComponent", "bEnableFog"};
ue3::FieldRef gEnableParticles{"Engine.SceneCaptureComponent", "bEnableParticles"};
ue3::FieldRef gUseMainPostProcess{"Engine.SceneCaptureComponent", "bUseMainScenePostProcessSettings"};
ue3::FieldRef gSkipUsersOccluded{"Engine.SceneCaptureComponent", "bSkipUpdateIfTextureUsersOccluded"};
ue3::FieldRef gSkipOwnerOccluded{"Engine.SceneCaptureComponent", "bSkipUpdateIfOwnerOccluded"};
ue3::FieldRef gMaxUpdateDist{"Engine.SceneCaptureComponent", "MaxUpdateDist"};
ue3::FieldRef gPostProcess{"Engine.SceneCaptureComponent", "PostProcess"};
ue3::FieldRef gFrameRate{"Engine.SceneCaptureComponent", "FrameRate"};
ue3::FieldRef gUpdateMatrices{"Engine.SceneCapture2DComponent", "bUpdateMatrices"};
ue3::FieldRef gResource{"Engine.Texture", "Resource"};
ue3::FieldRef gSizeX{"Engine.TextureRenderTarget2D", "SizeX"};
ue3::FieldRef gSizeY{"Engine.TextureRenderTarget2D", "SizeY"};
ue3::FieldRef gFormat{"Engine.TextureRenderTarget2D", "Format"};
ue3::FieldRef gPlayer{"Engine.PlayerController", "Player"};
ue3::FieldRef gPlayerPostProcess{"Engine.LocalPlayer", "PlayerPostProcess"};
ue3::FieldRef gDefaultPostProcess{"Engine.Engine", "DefaultPostProcess"};
ue3::FieldRef gHiddenGame{"Engine.PrimitiveComponent", "HiddenGame"};
std::array<ue3::FieldRef, 5> gHeadMeshes{ue3::FieldRef{"SFXGame.BioPawn", "m_oHeadMesh"}, ue3::FieldRef{"SFXGame.BioPawn", "m_oHairMesh"},
    ue3::FieldRef{"SFXGame.BioPawn", "m_oHeadGearMesh"}, ue3::FieldRef{"SFXGame.BioPawn", "m_oVisorMesh"},
    ue3::FieldRef{"SFXGame.BioPawn", "m_oFacePlateMesh"}};

constexpr std::uint64_t kClassDefaultFlag = 0x200;  // EObjectFlags: RF_ClassDefaultObject
constexpr float kMaxUpdateDist = 10000000.0f;       // never stopped by distance (research/39 section 2)
constexpr double kUnitsPerDegree = 65536.0 / 360.0;

struct LinearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

struct State {
    void const* world = nullptr;
    ue3::Object* actor = nullptr;
    ue3::Object* component = nullptr;
    ue3::Object* target = nullptr;
    int width = 0;
    int height = 0;
    bool targetFloat = false;
    std::uint64_t generation = 0;
    bool enabled = false;
    std::uint32_t enabledTicks = 0;
    float fovDeg = 0.0f;
    float nearUnits = 0.0f;
    float farUnits = -1.0f;
    float rate = 0.0f;
    bool postProcess = true;
    bool reattach = false;
    ue3::Object* owner = nullptr;  // the capture actor's Owner we set (viewActor)
};
State gState;
std::uint64_t gGenerations = 0;
bool gFailed = false;
std::string gFailure;
void const* gFailedWorld = nullptr;
bool gSettingOn = false;
bool gToldNoHost = false;
unsigned gCreated = 0;
unsigned gCapturingTicks = 0;
unsigned gTicksSinceStats = 0;
unsigned gPovTicks = 0;  // ticks filmed from the game's camera (no head-tracked view)

// The camera: settings, the console's overrides, the follow.
std::optional<bool> gEnabledOverride;      // `mevr spectator on|off`, until [Spectator] enabled changes
std::optional<sp::Camera> gCameraOverride; // `head`, `at`, `follow`, until [Spectator] camera changes
bool gLastIniEnabled = false;
sp::Camera gLastIniCamera = sp::kHead;
enum class FreeKind { At, Follow };
FreeKind gFreeKind = FreeKind::At;
ue3::Vector gAtLocation{};
ue3::Rotator gAtRotation{};
bool gAtSet = false;
float gFollowOffset[3] = {-0.6f, 2.2f, 0.4f};  // right, forward, up (metres from the pawn): front left, a little high
float gFollowLookUpM = 1.0f;
sp::Follow gFollow;
sp::Camera gFollowCamera = sp::kHead;  // which camera gFollow is following (a switch re-snaps)
ue3::Vector gLastLocation{};
ue3::Rotator gLastRotation{};
bool gLastFromHead = false;

// The head hide (hideHead): the pawn's head meshes and their HiddenGame before.
ue3::Object* gHiddenPawn = nullptr;
std::vector<std::pair<ue3::Object*, bool>> gHiddenHead;

void Fail(std::string why) {
    gFailed = true;
    gFailedWorld = engine::World();
    gFailure = std::move(why);
    log::Warn("spectator: off: {}; the desktop window keeps Halcyon's mirror", gFailure);
}

ue3::Object* DefaultObject(ue3::Class* cls) {
    ue3::Reflection& r = ue3::runtime::Get();
    std::int32_t const count = r.ObjectCount();
    for (std::int32_t i = 0; i < count; ++i) {
        ue3::Object* const object = r.ObjectAt(i);
        if (object != nullptr && r.ClassOf(object) == cls && (r.FlagsOf(object) & kClassDefaultFlag) != 0) {
            return object;
        }
    }
    return nullptr;
}

bool Still(ue3::Object* object, ue3::ClassRef& cls) {
    return object != nullptr && gState.world == engine::World() && ue3::runtime::IsAlive(object) && ue3::IsA(object, cls);
}

bool Call(ue3::Object* object, ue3::FunctionRef& function, ue3::Params& params) {
    return object != nullptr && function.Get() != nullptr && ue3::runtime::Call(object, function.Get(), params);
}

template <class T>
bool WriteField(ue3::FieldRef& field, ue3::Object* object, T value, char const* tag) {
    T* const p = field.In<T>(object);
    if (p == nullptr) {
        return false;
    }
    *p = value;
    write_journal::Wrote(tag, p);
    return true;
}

bool SetEnabled(bool on) {
    ue3::Params p(ue3::runtime::Get(), gSetEnabled.Get());
    if (!p.SetBool("bEnable", on) || !Call(gState.component, gSetEnabled, p)) {
        return false;
    }
    gState.enabled = on;
    gState.enabledTicks = 0;
    gState.reattach = true;
    return true;
}

// Actor.ForceUpdateComponents: the view (and a re-attach after SetEnabled / SetCaptureParameters) out now, before the next
// Draw (research/39 section 5, research/47: a new probe carries the view).
bool FlushComponents() {
    ue3::Params update(ue3::runtime::Get(), gForceUpdate.Get());
    update.SetBool("bCollisionUpdate", false);
    update.SetBool("bTransformOnly", !gState.reattach);
    if (!Call(gState.actor, gForceUpdate, update)) {
        return false;
    }
    gState.reattach = false;
    return true;
}

ue3::Object* PlayerPostProcessChain(ue3::Object* controller) {
    if (auto const player = gPlayer.Read<ue3::Object*>(controller); player && *player != nullptr) {
        if (auto const chain = gPlayerPostProcess.Read<ue3::Object*>(*player); chain && *chain != nullptr) {
            return *chain;
        }
    }
    if (ue3::Object* const engine = ue3::runtime::GameEngine()) {
        if (auto const chain = gDefaultPostProcess.Read<ue3::Object*>(engine); chain && *chain != nullptr) {
            return *chain;
        }
    }
    return nullptr;
}

ue3::Object* CreateTarget(int width, int height, bool floatTarget) {
    ue3::Class* const cls = gTargetClass.Get();
    ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
    if (defaults == nullptr || gCreate.Get() == nullptr) {
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gCreate.Get());
    std::uint8_t const format = static_cast<std::uint8_t>(floatTarget ? sp::kPixelFormatFloatRGB : sp::kPixelFormatA8R8G8B8);
    bool const ok = p.Set("InSizeX", static_cast<std::int32_t>(width)) && p.Set("InSizeY", static_cast<std::int32_t>(height)) &&
        p.Set("InFormat", format) && p.Set("InClearColor", LinearColor{}) && p.SetBool("bOnlyRenderOnce", false);
    if (!ok || !Call(defaults, gCreate, p)) {
        return nullptr;
    }
    ue3::Object* const target = p.Return<ue3::Object*>().value_or(nullptr);
    return ue3::IsA(target, gTargetClass) ? target : nullptr;
}

ue3::Object* SpawnActor(ue3::Object* controller, ue3::Vector const& at) {
    ue3::Class* const cls = gActorClass.Get();
    ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
    if (defaults == nullptr || gSpawn.Get() == nullptr) {
        return nullptr;
    }
    bool const wasStatic = gStatic.ReadBool(defaults).value_or(false);
    bool const wasNoDelete = gNoDelete.ReadBool(defaults).value_or(false);
    if (wasStatic || wasNoDelete) {  // Spawn refuses those: cleared on the defaults for the call (research/39 section 2)
        gStatic.WriteBool(defaults, false);
        gNoDelete.WriteBool(defaults, false);
    }
    ue3::Params p(ue3::runtime::Get(), gSpawn.Get());
    p.Set("SpawnClass", cls);
    p.Set("SpawnOwner", controller);
    p.Set("SpawnLocation", at);
    p.SetBool("bNoCollisionFail", true);
    Call(controller, gSpawn, p);
    if (wasStatic || wasNoDelete) {
        gStatic.WriteBool(defaults, wasStatic);
        gNoDelete.WriteBool(defaults, wasNoDelete);
    }
    ue3::Object* const actor = p.Return<ue3::Object*>().value_or(nullptr);
    return ue3::IsA(actor, gActorClass) ? actor : nullptr;
}

bool SetParameters(float fovDeg, float nearUnits, float farUnits) {
    ue3::Params p(ue3::runtime::Get(), gSetParameters.Get());
    bool const ok = p.Set("NewTextureTarget", gState.target) && p.Set("NewFOV", fovDeg) && p.Set("NewNearPlane", nearUnits) &&
        p.Set("NewFarPlane", farUnits);
    if (!ok || !Call(gState.component, gSetParameters, p)) {
        return false;
    }
    gState.fovDeg = fovDeg;
    gState.nearUnits = nearUnits;
    gState.farUnits = farUnits;
    gState.reattach = true;
    return true;
}

bool SetFrameRate(float rate) {
    ue3::Params p(ue3::runtime::Get(), gSetFrameRate.Get());
    if (!p.Set("NewFrameRate", rate) || !Call(gState.component, gSetFrameRate, p)) {
        return false;
    }
    gState.rate = rate;
    return true;
}

void ConfigureComponent(ue3::Object* controller, sp::Settings const& settings) {
    ue3::Object* const c = gState.component;
    gUpdateMatrices.WriteBool(c, false);     // else each update puts the actor's transform back (research/47)
    gSkipUsersOccluded.WriteBool(c, false);  // nothing renders our texture
    gSkipOwnerOccluded.WriteBool(c, false);  // nor the invisible actor
    WriteField(gMaxUpdateDist, c, kMaxUpdateDist, "Spectator.MaxUpdateDist");
    gEnableFog.WriteBool(c, true);
    gEnableParticles.WriteBool(c, true);
    ue3::Object* const chain = settings.postProcess ? PlayerPostProcessChain(controller) : nullptr;
    gEnablePostProcess.WriteBool(c, chain != nullptr);
    gUseMainPostProcess.WriteBool(c, chain != nullptr);
    WriteField(gPostProcess, c, chain, "Spectator.PostProcess");
    WriteField(gFrameRate, c, settings.rate, "Spectator.FrameRate");
    gState.postProcess = chain != nullptr;
    log::Info("spectator: post process {}", chain != nullptr ? std::format("on, the player's chain {}", ue3::runtime::FullName(chain))
                                                            : settings.postProcess ? std::string("off: no post-process chain found")
                                                                                   : std::string("off ([Spectator] post = 0)"));
}

bool NewTarget(sp::Settings const& settings) {
    ue3::Object* const target = CreateTarget(settings.width, settings.height, settings.floatTarget);
    if (target == nullptr) {
        return false;
    }
    gState.target = target;
    gState.width = settings.width;
    gState.height = settings.height;
    gState.targetFloat = settings.floatTarget;
    gState.generation = ++gGenerations;
    log::Info("spectator: render target {} ({}x{}, format {}{}, resource 0x{:X})", ue3::runtime::FullName(target),
        gSizeX.Read<std::int32_t>(target).value_or(-1), gSizeY.Read<std::int32_t>(target).value_or(-1),
        static_cast<int>(gFormat.Read<std::uint8_t>(target).value_or(0)), settings.floatTarget ? ": FloatRGB" : ": A8R8G8B8",
        gResource.Read<std::uint64_t>(target).value_or(0));
    return true;
}

void Destroy(char const* why);

bool Create(sp::Settings const& settings, ue3::Vector const& at) {
    if (!ue3::runtime::Ready()) {
        Fail("our reflection layer is off (see the 'reflect:' lines)");
        return false;
    }
    for (ue3::FunctionRef* f : {&gSpawn, &gDestroy, &gForceUpdate, &gCreate, &gSetEnabled, &gSetFrameRate, &gSetView, &gSetParameters}) {
        if (f->Get() == nullptr) {
            Fail(std::format("{} not found", f->Path()));
            return false;
        }
    }
    for (ue3::ClassRef* c : {&gActorClass, &gComponentClass, &gTargetClass}) {
        if (c->Get() == nullptr) {
            Fail(std::format("{} not found", c->Path()));
            return false;
        }
    }
    for (ue3::FieldRef* f : {&gSceneCapture, &gResource, &gSkipUsersOccluded, &gSkipOwnerOccluded, &gMaxUpdateDist, &gUpdateMatrices}) {
        if (f->Get() == nullptr) {
            Fail(std::format("the field {} not found", f->Name()));
            return false;
        }
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        return false;  // no player yet: next tick
    }
    gState = State{};
    gState.world = engine::World();
    if (!NewTarget(settings)) {
        Fail("TextureRenderTarget2D.Create returned no render target");
        return false;
    }
    gState.actor = SpawnActor(controller, at);
    if (gState.actor == nullptr) {
        gState = State{};
        Fail("Actor.Spawn returned no SceneCapture2DActor");
        return false;
    }
    ue3::Object* const component = gSceneCapture.Read<ue3::Object*>(gState.actor).value_or(nullptr);
    if (!ue3::IsA(component, gComponentClass)) {
        Fail(std::format("the spawned {} has no SceneCapture2DComponent ({})", ue3::runtime::FullName(gState.actor),
            ue3::runtime::FullName(component)));
        ue3::Params p(ue3::runtime::Get(), gDestroy.Get());
        Call(gState.actor, gDestroy, p);
        gState = State{};
        return false;
    }
    gState.component = component;
    gState.enabled = gEnabled.ReadBool(component).value_or(true);
    ConfigureComponent(controller, settings);
    if ((gState.enabled && !SetEnabled(false)) || !SetFrameRate(settings.rate) ||
        !SetParameters(settings.fovDeg, settings.nearUnits, settings.farUnits)) {
        Destroy("its setup failed");
        Fail("SetEnabled, SetFrameRate or SetCaptureParameters failed");
        return false;
    }
    FlushComponents();
    ++gCreated;
    log::Info("spectator: created {} with {} ({}x{}, {:.0f} degrees across, near plane {:.0f} units)", ue3::runtime::FullName(gState.actor),
        ue3::runtime::FullName(component), settings.width, settings.height, settings.fovDeg, settings.nearUnits);
    return true;
}

// The head meshes back as they were (game thread, the pawn still ours).
void ShowHead(char const* why) {
    if (gHiddenHead.empty()) {
        gHiddenPawn = nullptr;
        return;
    }
    unsigned restored = 0;
    for (auto const& [component, before] : gHiddenHead) {
        if (ue3::runtime::IsAlive(component) && gPrimitiveSetHidden.Get() != nullptr) {
            ue3::Params p(ue3::runtime::Get(), gPrimitiveSetHidden.Get());
            p.SetBool("NewHidden", before);
            Call(component, gPrimitiveSetHidden, p);
            ++restored;
        }
    }
    log::Info("spectator: the pawn's {} head mesh{} shown again ({})", restored, restored == 1 ? "" : "es", why);
    gHiddenHead.clear();
    gHiddenPawn = nullptr;
}

// The head camera sits inside the head: its meshes hidden for all views (the eyes never see them: Halcyon's head hide,
// bOwnerNoSee, game_camera.cpp [FPSHEAD]), since a capture's view doesn't leave out the pawn's bOwnerNoSee parts.
void HideHead(ue3::Object* pawn) {
    if (pawn == gHiddenPawn) {
        return;
    }
    ShowHead("another pawn");
    if (pawn == nullptr || !ue3::IsA(pawn, gBioPawnClass) || gPrimitiveSetHidden.Get() == nullptr) {
        return;
    }
    gHiddenPawn = pawn;
    for (ue3::FieldRef& field : gHeadMeshes) {
        ue3::Object* const part = field.Read<ue3::Object*>(pawn).value_or(nullptr);
        if (part == nullptr || !ue3::runtime::IsAlive(part)) {
            continue;
        }
        gHiddenHead.emplace_back(part, gHiddenGame.ReadBool(part).value_or(false));
        ue3::Params p(ue3::runtime::Get(), gPrimitiveSetHidden.Get());
        p.SetBool("NewHidden", true);
        Call(part, gPrimitiveSetHidden, p);
    }
    log::Info("spectator: the head camera hides {}'s {} head mesh{} (HiddenGame; the eyes never show them)", ue3::runtime::FullName(pawn),
        gHiddenHead.size(), gHiddenHead.size() == 1 ? "" : "es");
}

void Destroy(char const* why) {
    if (Still(gState.actor, gActorClass)) {
        if (gState.enabled && Still(gState.component, gComponentClass)) {
            SetEnabled(false);
        }
        ue3::Params p(ue3::runtime::Get(), gDestroy.Get());
        Call(gState.actor, gDestroy, p);
        log::Info("spectator: destroyed {} ({})", ue3::runtime::FullName(gState.actor), why);
    }
    gState = State{};
}

void Forget(char const* why) {
    if (gState.actor != nullptr) {
        log::Info("spectator: forgot the capture ({}): its objects went with the old level", why);
    }
    gState = State{};
    gHiddenHead.clear();  // the old level's pawn and meshes are gone with it
    gHiddenPawn = nullptr;
    gFollow = {};
}

// The capture actor owned by the pawn (viewActor; research/49 section 3): Actor.SetOwner through ProcessEvent changes
// nothing (research/47), so the field, journalled; the full re-attach carries it into the next probe.
void SetOwner(ue3::Object* pawn) {
    if (gState.actor == nullptr || pawn == gState.owner) {
        return;
    }
    if (ue3::Object** const field = gOwner.In<ue3::Object*>(gState.actor)) {
        *field = pawn;
        write_journal::Wrote("Spectator.owner", field, sizeof(*field));
        gState.owner = pawn;
        gState.reattach = true;
        log::Info("spectator: the capture is owned by {}", ue3::runtime::FullName(pawn));
    }
}

// The world pose for a tracking pose under `frame`.
void ToWorld(engine::TrackingFrame const& frame, XrPosef const& pose, ue3::Vector& location, ue3::Rotator& rotation) {
    engine::UnrealVector const at = engine::ToWorld(frame, pose.position);
    engine::UnrealRotation const r = engine::BasisRotation(engine::OrientationToWorld(frame, pose.orientation));
    location = {static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)};
    rotation = {r.pitch, r.yaw, r.roll};
}

// The tracking pose for a world pose under `frame` (looks along the rotator's forward, up its up).
std::optional<XrPosef> ToTrackingPose(engine::TrackingFrame const& frame, ue3::Vector const& location, ue3::Rotator const& rotation) {
    engine::Basis const basis = engine::ToBasis(engine::UnrealRotation{rotation.pitch, rotation.yaw, rotation.roll});
    XrVector3f const forward = engine::DirectionToTracking(frame, basis.forward);
    XrVector3f const up = engine::DirectionToTracking(frame, basis.up);
    auto const orientation = vr::scope_capture::CameraOrientation(forward, up);
    if (!orientation) {
        return std::nullopt;
    }
    XrVector3f const position = engine::ToTracking(frame, {location.x, location.y, location.z});
    return XrPosef{*orientation, position};
}

// `follow`'s world pose: the offset in the pawn's heading, looking at the pawn's origin `lookUpM` up.
std::optional<std::pair<ue3::Vector, ue3::Rotator>> FollowPose(ue3::Object* pawn) {
    auto const at = player::Location(pawn);
    auto const rot = player::Rotation(pawn);
    if (!at || !rot) {
        return std::nullopt;
    }
    engine::Basis const heading = engine::ToBasis(engine::UnrealRotation{0, rot->yaw, 0});
    engine::UnrealVector const offset = engine::Apply(heading, {gFollowOffset[1] * engine::kUnitsPerMeter, gFollowOffset[0] * engine::kUnitsPerMeter,
                                                                    gFollowOffset[2] * engine::kUnitsPerMeter});
    engine::UnrealVector const camera{at->x + offset.x, at->y + offset.y, at->z + offset.z};
    engine::UnrealVector const look{at->x - camera.x, at->y - camera.y, at->z + gFollowLookUpM * engine::kUnitsPerMeter - camera.z};
    engine::UnrealRotation const r = engine::ToRotation(look);
    return std::pair{ue3::Vector{static_cast<float>(camera.x), static_cast<float>(camera.y), static_cast<float>(camera.z)},
        ue3::Rotator{r.pitch, r.yaw, 0}};
}

struct Aim {
    ue3::Vector location{};
    ue3::Rotator rotation{};
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    bool poseValid = false;  // the tracking pose (the host composites our layers only with it)
    bool fromHead = false;   // a head-tracked view mapped it (else the game's camera)
};

sp::Camera CameraFor(sp::Settings const& settings) {
    return gCameraOverride.value_or(settings.camera);
}

std::optional<Aim> AimFor(sp::Settings const& settings, float dt, ue3::Object* controller, ue3::Object* pawn) {
    sp::Camera const camera = CameraFor(settings);
    if (camera != gFollowCamera) {
        gFollow = {};
        gFollowCamera = camera;
    }
    auto const frame = view_anchor::Current();
    Aim aim;
    if (frame && frame->unitsPerMeter > 0.0) {
        std::optional<XrPosef> target;
        bool smooth = true;
        if (camera == sp::kHead) {
            target = frame->head;
        } else if (gFreeKind == FreeKind::Follow && pawn != nullptr) {
            if (auto const world = FollowPose(pawn)) {
                target = ToTrackingPose(*frame, world->first, world->second);
            }
        } else if (gAtSet) {
            target = ToTrackingPose(*frame, gAtLocation, gAtRotation);
            smooth = false;  // a fixed camera: the tracking space moves under it, the world doesn't
        }
        if (target) {
            if (smooth) {
                sp::Step(gFollow, *target, dt, settings.smoothS);
            } else {
                sp::Snap(gFollow, *target);
            }
            aim.pose = gFollow.pose;
            aim.poseValid = true;
            aim.fromHead = true;
            ToWorld(*frame, aim.pose, aim.location, aim.rotation);
            if (camera == sp::kFree && !gFollow.valid) {
                return std::nullopt;
            }
            return aim;
        }
    }
    // No head-tracked view (menus, conversations, cutscenes): a fixed camera stays, anything else films the game's camera.
    gFollow = {};
    if (camera == sp::kFree && gFreeKind == FreeKind::At && gAtSet) {
        aim.location = gAtLocation;
        aim.rotation = gAtRotation;
        return aim;
    }
    ue3::Object* const cam = controller != nullptr ? player::Camera(controller) : nullptr;
    auto const pov = cam != nullptr ? player::CameraPov(cam) : std::nullopt;
    if (!pov || pov->location == nullptr || pov->rotation == nullptr) {
        return std::nullopt;
    }
    aim.location = *pov->location;
    aim.rotation = *pov->rotation;
    return aim;
}

bool Point(Aim const& aim) {
    ue3::Params view(ue3::runtime::Get(), gSetView.Get());
    if (!view.Set("NewLocation", aim.location) || !view.Set("NewRotation", aim.rotation) || !Call(gState.component, gSetView, view)) {
        return false;
    }
    return FlushComponents();
}

bool EffectiveEnabled(sp::Settings const& settings) {
    return gEnabledOverride.value_or(settings.enabled);
}

#endif

} // namespace

void Tick(float deltaSeconds, Link const& link) {
#if defined(MEVR_GAME_LE1)
    sp::Settings const settings = hand_calibration::GetSpectator();
    // The ini wins again once it changes.
    if (settings.enabled != gLastIniEnabled) {
        gLastIniEnabled = settings.enabled;
        gEnabledOverride.reset();
    }
    if (settings.camera != gLastIniCamera) {
        gLastIniCamera = settings.camera;
        gCameraOverride.reset();
    }
    bool const on = EffectiveEnabled(settings);
    if (gSettingOn != on) {
        gSettingOn = on;
        gFailed = false;
        if (!on) {
            ShowHead("the spectator is off");
            if (gState.actor != nullptr) {
                Destroy("the spectator is off");
            }
            gFollow = {};
            sp::Frame off{};
            gLast = off;
            if (link.publish != nullptr) {
                link.publish(&off);
            }
            gStatus = "off";
            log::Info("spectator: off");
        }
    }
    if (!on) {
        return;  // nothing spawned, nothing published: no cost
    }
    if (link.publish == nullptr) {
        if (!gToldNoHost) {
            gToldNoHost = true;
            log::Warn("spectator: the host (services older than v24) can't show it: close the game and redeploy the host");
        }
        gStatus = "off (the host is older than v24)";
        return;
    }
    if (gState.actor != nullptr && gState.world != engine::World()) {
        Forget("a new level");
    }
    if (gState.actor != nullptr &&
        (!Still(gState.actor, gActorClass) || !Still(gState.component, gComponentClass) || !Still(gState.target, gTargetClass))) {
        if (Still(gState.actor, gActorClass)) {
            Destroy("its component or render target is gone");
        } else {
            Forget("its actor is gone");
        }
    }
    if (gFailed && gFailedWorld != engine::World()) {
        gFailed = false;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    float const dt = std::clamp(deltaSeconds, 0.0f, 0.25f);
    std::optional<Aim> const aim = !gFailed ? AimFor(settings, dt, controller, pawn) : std::nullopt;
    if (aim && gState.actor == nullptr && !gFailed) {
        Create(settings, aim->location);
    }
    bool capturing = false;
    if (gState.actor != nullptr && aim) {
        if (gState.width != settings.width || gState.height != settings.height || gState.targetFloat != settings.floatTarget) {
            if (NewTarget(settings)) {
                gState.fovDeg = 0.0f;  // re-sent below with the new target
            }
        }
        if (gState.rate != settings.rate) {
            SetFrameRate(settings.rate);
        }
        if (std::abs(gState.fovDeg - settings.fovDeg) > 0.01f || gState.nearUnits != settings.nearUnits || gState.farUnits != settings.farUnits) {
            SetParameters(settings.fovDeg, settings.nearUnits, settings.farUnits);
        }
        if (settings.viewActor) {
            SetOwner(pawn);
        } else if (gState.owner != nullptr) {
            SetOwner(controller);
        }
        if (!gState.enabled && SetEnabled(true)) {
            log::Info("spectator: enabled: the {} camera", CameraFor(settings) == sp::kHead ? "head" : "free");
        }
        capturing = gState.enabled && Point(*aim);
        if (capturing) {
            ++gState.enabledTicks;
            ++gCapturingTicks;
            ++gTicksSinceStats;
            gPovTicks += aim->fromHead ? 0u : 1u;
            gLastLocation = aim->location;
            gLastRotation = aim->rotation;
            gLastFromHead = aim->fromHead;
        }
    }
    // The head camera: the head out of its picture (the eyes never see it).
    if (capturing && settings.hideHead && CameraFor(settings) == sp::kHead && aim->fromHead) {
        HideHead(player::BioPawn(controller));
    } else if (!gHiddenHead.empty()) {
        ShowHead(CameraFor(settings) == sp::kHead ? "no head-tracked view" : "the free camera");
    }
    sp::Frame out{};
    std::string status;
    if (capturing) {
        out.state = sp::kCapturing;
        out.camera = CameraFor(settings);
        out.pixelFormat = gState.targetFloat ? sp::kPixelFormatFloatRGB : sp::kPixelFormatA8R8G8B8;
        out.width = static_cast<std::uint32_t>(gSizeX.Read<std::int32_t>(gState.target).value_or(gState.width));
        out.height = static_cast<std::uint32_t>(gSizeY.Read<std::int32_t>(gState.target).value_or(gState.height));
        out.resource = gResource.Read<std::uint64_t>(gState.target).value_or(0);
        out.generation = gState.generation;
        out.enabledTicks = gState.enabledTicks;
        out.tanHalfX = sp::TanHalfX(gState.fovDeg);
        out.tanHalfY = sp::TanHalfY(gState.fovDeg, static_cast<int>(out.width), static_cast<int>(out.height));
        out.pose = aim->pose;
        out.flags = (settings.mirror ? sp::kFlagMirror : 0u) | (settings.layers && aim->poseValid ? sp::kFlagLayers : 0u);
        status = std::format("capturing: {}x{}, the {} camera{}", out.width, out.height, out.camera == sp::kHead ? "head" : "free",
            aim->fromHead ? "" : " (the game's camera: no head-tracked view)");
    } else if (gFailed) {
        status = "off: " + gFailure;
    } else if (!aim) {
        status = CameraFor(settings) == sp::kFree && !gAtSet && gFreeKind == FreeKind::At
            ? "waiting: the free camera has no pose (mevr spectator at X Y Z YAW PITCH, or follow)"
            : "waiting for a player camera";
    } else {
        status = "waiting for the capture";
    }
    gLast = out;
    link.publish(&out);
    gStatus = status;
    if (status != gLoggedStatus && status.rfind("capturing", 0) != 0) {
        log::Info("spectator: {}", status);
    }
    gLoggedStatus = status;
#else
    (void)deltaSeconds;
    (void)link;
    gStatus = "off (LE1 only)";
#endif
}

std::string Command(std::string const& rest) {
#if defined(MEVR_GAME_LE1)
    std::istringstream words(rest);
    std::string verb;
    words >> verb;
    sp::Settings const settings = hand_calibration::GetSpectator();
    auto const where = [] {
        return std::format("at {:.0f} {:.0f} {:.0f}, yaw {:.1f} pitch {:.1f} degrees", gLastLocation.x, gLastLocation.y, gLastLocation.z,
            gLastRotation.yaw / kUnitsPerDegree, gLastRotation.pitch / kUnitsPerDegree);
    };
    if (verb.empty() || verb == "status") {
        return std::format("spectator: {}; {} ({}); last pose {}", gStatus, EffectiveEnabled(settings) ? "on" : "off",
            gEnabledOverride ? "the console's" : "[Spectator] enabled", where());
    }
    if (verb == "on" || verb == "off") {
        gEnabledOverride = verb == "on";
        return std::format("spectator {} (until [Spectator] enabled changes){}", verb,
            verb == "on" && !settings.enabled ? "; the ini says 0" : "");
    }
    if (verb == "head") {
        gCameraOverride = sp::kHead;
        return "the head camera (smoothed over " + std::format("{:.2f}", settings.smoothS) + " s)";
    }
    if (verb == "at") {
        float x = 0.0f, y = 0.0f, z = 0.0f, yaw = 0.0f, pitch = 0.0f;
        if (!(words >> x)) {
            // No numbers: stay where the camera is now.
            gAtLocation = gLastLocation;
            gAtRotation = {gLastRotation.pitch, gLastRotation.yaw, 0};
        } else if (words >> y >> z >> yaw >> pitch) {
            gAtLocation = {x, y, z};
            gAtRotation = {static_cast<std::int32_t>(std::lround(pitch * kUnitsPerDegree)), static_cast<std::int32_t>(std::lround(yaw * kUnitsPerDegree)), 0};
        } else {
            return "usage: mevr spectator at [X Y Z YAW PITCH] (world units and degrees; no numbers: where the camera is now)";
        }
        gAtSet = true;
        gFreeKind = FreeKind::At;
        gCameraOverride = sp::kFree;
        gFollow = {};
        return std::format("the free camera at {:.0f} {:.0f} {:.0f}, yaw {:.1f} pitch {:.1f} degrees", gAtLocation.x, gAtLocation.y, gAtLocation.z,
            gAtRotation.yaw / kUnitsPerDegree, gAtRotation.pitch / kUnitsPerDegree);
    }
    if (verb == "follow") {
        float r = gFollowOffset[0], f = gFollowOffset[1], u = gFollowOffset[2], lookUp = gFollowLookUpM;
        if (words >> r) {
            if (!(words >> f >> u)) {
                return "usage: mevr spectator follow [RIGHT FORWARD UP [LOOKUP]] (metres from the pawn in its heading; it looks at the pawn LOOKUP "
                       "above its origin, 1 by default)";
            }
            words >> lookUp;
        }
        gFollowOffset[0] = r;
        gFollowOffset[1] = f;
        gFollowOffset[2] = u;
        gFollowLookUpM = lookUp;
        gFreeKind = FreeKind::Follow;
        gCameraOverride = sp::kFree;
        gFollow = {};
        return std::format("the free camera follows the pawn from {:.2f} m right, {:.2f} m ahead, {:.2f} m up, looking at {:.2f} m above it", r, f, u,
            lookUp);
    }
    if (verb == "loc") {
        return std::format("spectator: {}", where());
    }
    return "usage: mevr spectator [status] | on | off | head | at [X Y Z YAW PITCH] | follow [RIGHT FORWARD UP [LOOKUP]] | loc";
#else
    (void)rest;
    return "mevr spectator: LE1 only";
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
#if defined(MEVR_GAME_LE1)
    return std::format("{{\"status\": {}, \"state\": {}, \"camera\": {}, \"actor\": {}, \"owner\": {}, \"enabled\": {}, \"enabledTicks\": {}, "
                       "\"size\": [{}, {}], \"format\": {}, \"resource\": \"0x{:X}\", \"generation\": {}, \"fovDeg\": {}, \"flags\": {}, "
                       "\"headHidden\": {}, \"fromHead\": {}, \"location\": [{}, {}, {}], \"rotation\": [{}, {}, {}], \"created\": {}, "
                       "\"capturingTicks\": {}, \"povTicks\": {}, \"pose\": {{\"position\": [{}, {}, {}], \"orientation\": [{}, {}, {}, {}]}}}}",
        json::JsonString(gStatus), gLast.state, json::JsonString(gLast.camera == sp::kHead ? "head" : "free"),
        json::JsonString(ue3::runtime::FullName(gState.actor)), json::JsonString(ue3::runtime::FullName(gState.owner)),
        gState.enabled ? "true" : "false", gState.enabledTicks, gLast.width, gLast.height, gLast.pixelFormat, gLast.resource, gLast.generation,
        json::JsonNumber(gState.fovDeg), gLast.flags, gHiddenHead.size(), gLastFromHead ? "true" : "false", json::JsonNumber(gLastLocation.x),
        json::JsonNumber(gLastLocation.y), json::JsonNumber(gLastLocation.z), gLastRotation.pitch, gLastRotation.yaw, gLastRotation.roll, gCreated,
        gCapturingTicks, gPovTicks, json::JsonNumber(gLast.pose.position.x), json::JsonNumber(gLast.pose.position.y),
        json::JsonNumber(gLast.pose.position.z), json::JsonNumber(gLast.pose.orientation.x), json::JsonNumber(gLast.pose.orientation.y),
        json::JsonNumber(gLast.pose.orientation.z), json::JsonNumber(gLast.pose.orientation.w));
#else
    return std::format("{{\"status\": {}}}", json::JsonString(gStatus));
#endif
}

void LogStats() {
#if defined(MEVR_GAME_LE1)
    if (gState.actor != nullptr || gFailed) {
        log::Info("    spectator: {}; {} ticks capturing since the last heartbeat ({} in all, {} from the game's camera), pose {:.0f} {:.0f} {:.0f}",
            gStatus, gTicksSinceStats, gCapturingTicks, gPovTicks, gLastLocation.x, gLastLocation.y, gLastLocation.z);
    }
    gTicksSinceStats = 0;
#endif
}

void Shutdown(bool exiting) {
#if defined(MEVR_GAME_LE1)
    if (!exiting && ue3::runtime::Ready()) {
        ShowHead("the mod shuts down");
        if (gState.actor != nullptr) {
            Destroy("the mod shuts down");
        }
    }
    gState = State{};
    gHiddenHead.clear();
    gHiddenPawn = nullptr;
    gFailed = false;
    gFailure.clear();
    gFailedWorld = nullptr;
    gSettingOn = false;
    gToldNoHost = false;
    gCreated = gCapturingTicks = gTicksSinceStats = gPovTicks = 0;
    gFollow = {};
    for (ue3::ClassRef* c : {&gActorClass, &gComponentClass, &gTargetClass, &gBioPawnClass}) {
        c->Reset();
    }
    for (ue3::FunctionRef* f : {&gSpawn, &gDestroy, &gForceUpdate, &gCreate, &gSetEnabled, &gSetFrameRate, &gSetView, &gSetParameters,
             &gPrimitiveSetHidden}) {
        f->Reset();
    }
    for (ue3::FieldRef* f : {&gSceneCapture, &gStatic, &gNoDelete, &gOwner, &gEnabled, &gEnablePostProcess, &gEnableFog, &gEnableParticles,
             &gUseMainPostProcess, &gSkipUsersOccluded, &gSkipOwnerOccluded, &gMaxUpdateDist, &gPostProcess, &gFrameRate, &gUpdateMatrices,
             &gResource, &gSizeX, &gSizeY, &gFormat, &gPlayer, &gPlayerPostProcess, &gDefaultPostProcess, &gHiddenGame}) {
        f->Reset();
    }
    for (ue3::FieldRef& f : gHeadMeshes) {
        f.Reset();
    }
#else
    (void)exiting;
#endif
    gLast = {};
    gStatus = "off";
    gLoggedStatus.clear();
}

} // namespace mevr::game::spectator
