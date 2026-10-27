// SPDX-License-Identifier: MIT
#include "game/ScopeCapture.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Engine.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <string>
#include <utility>

namespace mevr::game::scope_capture {
namespace {

namespace sc = vr::scope_capture;
namespace sv = vr::sights;

std::string gStatus = "off";
std::string gLoggedStatus;
sc::Capture gLast{};

#if defined(MEVR_GAME_LE1)

// Our reflection layer (research/34). Every one of these has a row in engine/ReflectCheck.
ue3::ClassRef gActorClass{"Engine.SceneCapture2DActor"};
ue3::ClassRef gComponentClass{"Engine.SceneCapture2DComponent"};
ue3::ClassRef gTargetClass{"Engine.TextureRenderTarget2D"};
ue3::FunctionRef gSpawn{"Engine.Actor.Spawn"};
ue3::FunctionRef gDestroy{"Engine.Actor.Destroy"};
ue3::FunctionRef gForceUpdate{"Engine.Actor.ForceUpdateComponents"};
ue3::FunctionRef gCreate{"Engine.TextureRenderTarget2D.Create"};
ue3::FunctionRef gSetEnabled{"Engine.SceneCaptureComponent.SetEnabled"};
ue3::FunctionRef gSetFrameRate{"Engine.SceneCaptureComponent.SetFrameRate"};
ue3::FunctionRef gSetView{"Engine.SceneCapture2DComponent.SetView"};
ue3::FunctionRef gSetParameters{"Engine.SceneCapture2DComponent.SetCaptureParameters"};
ue3::FieldRef gSceneCapture{"Engine.SceneCaptureActor", "SceneCapture"};
ue3::FieldRef gStatic{"Engine.Actor", "bStatic"};
ue3::FieldRef gNoDelete{"Engine.Actor", "bNoDelete"};
ue3::FieldRef gLocation{"Engine.Actor", "Location"};
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
ue3::FieldRef gTextureTarget{"Engine.SceneCapture2DComponent", "TextureTarget"};
ue3::FieldRef gFieldOfView{"Engine.SceneCapture2DComponent", "FieldOfView"};
ue3::FieldRef gResource{"Engine.Texture", "Resource"};
ue3::FieldRef gSizeX{"Engine.TextureRenderTarget2D", "SizeX"};
ue3::FieldRef gSizeY{"Engine.TextureRenderTarget2D", "SizeY"};
ue3::FieldRef gFormat{"Engine.TextureRenderTarget2D", "Format"};
ue3::FieldRef gPlayer{"Engine.PlayerController", "Player"};
ue3::FieldRef gPlayerPostProcess{"Engine.LocalPlayer", "PlayerPostProcess"};
ue3::FieldRef gDefaultPostProcess{"Engine.Engine", "DefaultPostProcess"};

// EObjectFlags: RF_ClassDefaultObject.
constexpr std::uint64_t kClassDefaultFlag = 0x200;
// MaxUpdateDist: the capture stays where it was spawned while the player walks on; a distance this large never stops
// it, whether the engine reads 0 as "no limit" or as "never" [verify].
constexpr float kMaxUpdateDist = 10000000.0f;
constexpr int kLoggedEnables = 5;

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
    int targetSize = 0;
    bool targetFloat = true;
    std::uint64_t generation = 0;
    bool enabled = false;
    std::uint32_t enabledTicks = 0;
    float fovDeg = 0.0f;
    float nearUnits = 0.0f;
    float farUnits = -1.0f;
    float rate = 0.0f;
    bool postProcess = true;
    bool reattach = false;  // SetEnabled / SetCaptureParameters re-attach the component (deferred): the next flush is full
};
State gState;
bool gFailed = false;       // stood down until the level changes or the setting is turned off and on
std::string gFailure;
void const* gFailedWorld = nullptr;
double gLastWantS = -1.0;
double gClockS = 0.0;
unsigned gEnables = 0;
unsigned gEnablesSinceStats = 0;
unsigned gCapturingTicks = 0;
unsigned gCreated = 0;
bool gSettingOn = false;
bool gToldNoHost = false;

void Fail(std::string why) {
    gFailed = true;
    gFailedWorld = engine::World();
    gFailure = std::move(why);
    log::Warn("scope capture: off: {}; the scope keeps magnifying the other eye's picture", gFailure);
}

// A class's default object (Default__X): found by class and flag, once per call (only at creation).
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

// The object is still the one we made: alive, of its class, in the world we made it in.
bool Still(ue3::Object* object, ue3::ClassRef& cls) {
    return object != nullptr && gState.world == engine::World() && ue3::runtime::IsAlive(object) && ue3::IsA(object, cls);
}

bool Call(ue3::Object* object, ue3::FunctionRef& function, ue3::Params& params) {
    return object != nullptr && function.Get() != nullptr && ue3::runtime::Call(object, function.Get(), params);
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

// Actor.ForceUpdateComponents: what SetView, SetEnabled and SetCaptureParameters left for the actor's next update goes
// out now (to the render thread, before the next Draw): the transform only, or everything after a re-attach [verify:
// the engine defers both; an actor it doesn't tick might never send them].
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

// FieldRef::WriteBool journals the write under the field's name; `tag` is for the reader of this file.
bool WriteBool(ue3::FieldRef& field, ue3::Object* object, bool value, char const* tag) {
    (void)tag;
    return field.WriteBool(object, value);
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

// The post-process chain the player's own view uses (LocalPlayer.PlayerPostProcess), else the engine's default.
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

// TextureRenderTarget2D.Create (static: called on its class's default object, as script's `.static.` does).
ue3::Object* CreateTarget(int size, bool floatTarget) {
    ue3::Class* const cls = gTargetClass.Get();
    ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
    if (defaults == nullptr || gCreate.Get() == nullptr) {
        return nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gCreate.Get());
    std::uint8_t const format = static_cast<std::uint8_t>(floatTarget ? sc::kPixelFormatFloatRGBA : sc::kPixelFormatA8R8G8B8);
    bool const ok = p.Set("InSizeX", static_cast<std::int32_t>(size)) && p.Set("InSizeY", static_cast<std::int32_t>(size)) &&
        p.Set("InFormat", format) && p.Set("InClearColor", LinearColor{}) && p.SetBool("bOnlyRenderOnce", false);
    if (!ok || !Call(defaults, gCreate, p)) {
        return nullptr;
    }
    ue3::Object* const target = p.Return<ue3::Object*>().value_or(nullptr);
    return ue3::IsA(target, gTargetClass) ? target : nullptr;
}

// Actor.Spawn on the player controller. A class marked bStatic or bNoDelete can't be spawned: those flags are cleared on
// its default object for the one call (the spawned actor copies them from it) and put back.
ue3::Object* SpawnActor(ue3::Object* controller, ue3::Vector const& at) {
    ue3::Class* const cls = gActorClass.Get();
    ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
    if (defaults == nullptr || gSpawn.Get() == nullptr) {
        return nullptr;
    }
    bool const wasStatic = gStatic.ReadBool(defaults).value_or(false);
    bool const wasNoDelete = gNoDelete.ReadBool(defaults).value_or(false);
    if (wasStatic || wasNoDelete) {
        log::Info("scope capture: SceneCapture2DActor's defaults say{}{}: cleared for the spawn and put back", wasStatic ? " bStatic" : "",
            wasNoDelete ? " bNoDelete" : "");
        WriteBool(gStatic, defaults, false, "ScopeCapture.default bStatic");
        WriteBool(gNoDelete, defaults, false, "ScopeCapture.default bNoDelete");
    }
    ue3::Params p(ue3::runtime::Get(), gSpawn.Get());
    p.Set("SpawnClass", cls);
    p.Set("SpawnOwner", controller);
    p.Set("SpawnLocation", at);
    p.SetBool("bNoCollisionFail", true);
    Call(controller, gSpawn, p);
    if (wasStatic || wasNoDelete) {
        WriteBool(gStatic, defaults, wasStatic, "ScopeCapture.default bStatic");
        WriteBool(gNoDelete, defaults, wasNoDelete, "ScopeCapture.default bNoDelete");
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

// The capture's own settings on its component, before SetCaptureParameters re-attaches it (the probe takes them then).
void ConfigureComponent(ue3::Object* controller, sc::Settings const& settings) {
    ue3::Object* const c = gState.component;
    WriteBool(gSkipUsersOccluded, c, false, "ScopeCapture.bSkipUpdateIfTextureUsersOccluded");  // nothing renders our texture
    WriteBool(gSkipOwnerOccluded, c, false, "ScopeCapture.bSkipUpdateIfOwnerOccluded");        // nor the invisible actor
    WriteField(gMaxUpdateDist, c, kMaxUpdateDist, "ScopeCapture.MaxUpdateDist");
    WriteBool(gEnableFog, c, true, "ScopeCapture.bEnableFog");
    WriteBool(gEnableParticles, c, true, "ScopeCapture.bEnableParticles");
    ue3::Object* const chain = settings.postProcess ? PlayerPostProcessChain(controller) : nullptr;
    WriteBool(gEnablePostProcess, c, chain != nullptr, "ScopeCapture.bEnablePostProcess");
    WriteBool(gUseMainPostProcess, c, chain != nullptr, "ScopeCapture.bUseMainScenePostProcessSettings");
    WriteField(gPostProcess, c, chain, "ScopeCapture.PostProcess");
    WriteField(gFrameRate, c, settings.rate, "ScopeCapture.FrameRate");
    gState.postProcess = chain != nullptr;
    log::Info("scope capture: post process {}", chain != nullptr ? std::format("on, the player's chain {}", ue3::runtime::FullName(chain))
                                                                 : settings.postProcess ? std::string("off: no post-process chain found")
                                                                                        : std::string("off ([Sights] scopeCapturePost = 0)"));
}

bool NewTarget(sc::Settings const& settings) {
    ue3::Object* const target = CreateTarget(settings.size, settings.floatTarget);
    if (target == nullptr) {
        return false;
    }
    gState.target = target;
    gState.targetSize = settings.size;
    gState.targetFloat = settings.floatTarget;
    ++gState.generation;
    log::Info("scope capture: render target {} ({}x{}, format {}{}, resource 0x{:X})", ue3::runtime::FullName(target),
        gSizeX.Read<std::int32_t>(target).value_or(-1), gSizeY.Read<std::int32_t>(target).value_or(-1),
        static_cast<int>(gFormat.Read<std::uint8_t>(target).value_or(0)), settings.floatTarget ? ": FloatRGBA" : ": A8R8G8B8",
        gResource.Read<std::uint64_t>(target).value_or(0));
    return true;
}

void Destroy(char const* why);

bool Create(sc::Settings const& settings, ue3::Vector const& at, float fovDeg, float nearUnits) {
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
    for (ue3::FieldRef* f : {&gSceneCapture, &gResource, &gSkipUsersOccluded, &gSkipOwnerOccluded, &gMaxUpdateDist}) {
        if (f->Get() == nullptr) {
            Fail(std::format("the field {} not found", f->Name()));
            return false;
        }
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        return false;  // no player yet: try again next tick, not a failure
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
    // Off until an eye is behind the scope (the defaults may have it on: it would render with no target set yet).
    if ((gState.enabled && !SetEnabled(false)) || !SetFrameRate(settings.rate) || !SetParameters(fovDeg, nearUnits, settings.farUnits)) {
        Destroy("its setup failed");
        Fail("SetEnabled, SetFrameRate or SetCaptureParameters failed");
        return false;
    }
    FlushComponents();
    ++gCreated;
    log::Info("scope capture: created {} with {} on {} (render target {}x{}; field {:.2f} degrees, near plane {:.0f} units, far {}; {:.0f} "
              "renders a second at most); disabled until an eye is behind the scope",
        ue3::runtime::FullName(gState.actor), ue3::runtime::FullName(component), ue3::runtime::FullName(controller), settings.size,
        settings.size, fovDeg, nearUnits, settings.farUnits > 0.0f ? std::format("{:.0f} units", settings.farUnits) : std::string("the engine's"),
        settings.rate);
    return true;
}

// Disables and destroys the capture (game thread, its objects still ours).
void Destroy(char const* why) {
    if (Still(gState.actor, gActorClass)) {
        if (gState.enabled && Still(gState.component, gComponentClass)) {
            SetEnabled(false);
        }
        ue3::Params p(ue3::runtime::Get(), gDestroy.Get());
        Call(gState.actor, gDestroy, p);
        log::Info("scope capture: destroyed {} ({})", ue3::runtime::FullName(gState.actor), why);
    }
    gState = State{};
}

void Forget(char const* why) {
    if (gState.actor != nullptr) {
        log::Info("scope capture: forgot the capture ({}): its objects went with the old level", why);
    }
    gState = State{};
}

// Where the capture looks from this tick: the sight's centre along its line (world), the camera's pose in tracking space
// from the rotator actually sent (roll 0: the picture's up is the world's), its field and near plane.
struct Aim {
    ue3::Vector location{};
    ue3::Rotator rotation{};
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float fovDeg = 0.0f;
    float nearUnits = 0.0f;
    float tanHalf = 0.0f;
};

std::optional<Aim> AimFor(sv::Sight const& sight, vr::aim_ray::Ray const& ray, sc::Settings const& settings) {
    auto const frame = view_anchor::Current();
    if (!frame || !(frame->unitsPerMeter > 0.0)) {
        return std::nullopt;
    }
    engine::UnrealVector const center = engine::ToWorld(*frame, sight.center);
    engine::UnrealVector const axis = engine::Apply(frame->rotation, {-static_cast<double>(sight.axis.z), sight.axis.x, sight.axis.y});
    engine::UnrealRotation const rotation = engine::ToRotation(axis);
    engine::Basis const basis = engine::ToBasis(rotation);
    XrVector3f const forward = engine::DirectionToTracking(*frame, basis.forward);
    XrVector3f const up = engine::DirectionToTracking(*frame, basis.up);
    auto const orientation = sc::CameraOrientation(forward, up);
    if (!orientation) {
        return std::nullopt;
    }
    Aim aim;
    aim.location = {static_cast<float>(center.x), static_cast<float>(center.y), static_cast<float>(center.z)};
    aim.rotation = {rotation.pitch, rotation.yaw, rotation.roll};
    aim.pose = {*orientation, sight.center};
    aim.fovDeg = sc::FovDeg(sc::PictureDeg(sight.scopeFieldDeg, sight.radius), sight.zoom, settings.margin);
    aim.tanHalf = sc::TanHalf(aim.fovDeg);
    XrVector3f const toMuzzle{ray.origin.x - sight.center.x, ray.origin.y - sight.center.y, ray.origin.z - sight.center.z};
    float const alongM = toMuzzle.x * sight.axis.x + toMuzzle.y * sight.axis.y + toMuzzle.z * sight.axis.z;
    aim.nearUnits = sc::NearPlaneUnits(alongM * static_cast<float>(frame->unitsPerMeter), settings.nearUnits);
    return aim;
}

bool Point(Aim const& aim) {
    ue3::Params view(ue3::runtime::Get(), gSetView.Get());
    if (!view.Set("NewLocation", aim.location) || !view.Set("NewRotation", aim.rotation) || !Call(gState.component, gSetView, view)) {
        return false;
    }
    // Out now, before the next Draw renders it (research/39 section 5), not at the actor's next tick.
    return FlushComponents();
}

#else

void PublishOff(Link const& link) {
    sc::Capture off{};
    off.state = sc::kOff;
    gLast = off;
    if (link.publish != nullptr) {
        link.publish(&off);
    }
}

#endif

} // namespace

void Tick(float deltaSeconds, sv::Sight const& sight, vr::aim_ray::Ray const& ray, std::uint32_t hostView, Link const& link) {
#if defined(MEVR_GAME_LE1)
    gClockS += std::clamp(static_cast<double>(deltaSeconds), 0.0, 0.25);
    sc::Settings const settings = hand_calibration::GetScopeCapture();
    std::string status;
    if (gSettingOn != settings.enabled) {
        gSettingOn = settings.enabled;
        gFailed = false;  // turning it off and on retries after a failure
        if (!settings.enabled && gState.actor != nullptr) {
            Destroy("[Sights] scopeCapture = 0");
        }
    }
    if (link.publish == nullptr) {
        if (settings.enabled && !gToldNoHost) {
            gToldNoHost = true;
            log::Warn("scope capture: the host (services older than v20) can't take it: close the game and redeploy the host");
        }
        gStatus = "off (the host is older than v20)";
        return;
    }
    // A new level: the old world's objects are gone (never call them); a failure there may not repeat here.
    if (gState.actor != nullptr && gState.world != engine::World()) {
        Forget("a new level");
    }
    if (gState.actor != nullptr && (!Still(gState.actor, gActorClass) || !Still(gState.component, gComponentClass) ||
                                       !Still(gState.target, gTargetClass))) {
        Forget("its actor, component or render target is gone");
    }
    if (gFailed && gFailedWorld != engine::World()) {
        gFailed = false;
    }
    bool const scope = sight.kind == static_cast<std::int32_t>(sv::Kind::Scope) && sight.radius > 0.0f;
    bool const eyeBehind = (hostView & (sv::kViewLeft | sv::kViewRight)) != 0;
    bool const want = settings.enabled && scope && eyeBehind && !gFailed;
    if (want) {
        gLastWantS = gClockS;
    }
    bool const keep = settings.enabled && !gFailed && gLastWantS >= 0.0 && gClockS - gLastWantS < settings.holdSeconds;
    std::optional<Aim> const aim = settings.enabled && scope ? AimFor(sight, ray, settings) : std::nullopt;
    if (want && aim && gState.actor == nullptr) {
        Create(settings, aim->location, aim->fovDeg, aim->nearUnits);
    }
    bool capturing = false;
    if (gState.actor != nullptr) {
        if (want && aim) {
            // A new size or format: a new render target (the old one goes with the garbage).
            if (gState.targetSize != settings.size || gState.targetFloat != settings.floatTarget) {
                if (NewTarget(settings)) {
                    gState.fovDeg = 0.0f;  // re-sent below with the new target
                }
            }
            if (gState.rate != settings.rate) {
                SetFrameRate(settings.rate);
            }
            if (std::abs(gState.fovDeg - aim->fovDeg) > 0.05f || std::abs(gState.nearUnits - aim->nearUnits) > 5.0f ||
                gState.farUnits != settings.farUnits) {
                SetParameters(aim->fovDeg, aim->nearUnits, settings.farUnits);
            }
            if (!gState.enabled && SetEnabled(true)) {
                ++gEnables;
                ++gEnablesSinceStats;
                if (gEnables <= kLoggedEnables) {
                    log::Info("scope capture: enabled (the eye is behind the {}x scope): field {:.2f} degrees, near plane {:.0f} units{}",
                        sight.zoom, gState.fovDeg, gState.nearUnits, gEnables == kLoggedEnables ? "; further enables are counted in the heartbeat" : "");
                }
            }
            // The view, then everything pending out at once (a re-attach from the calls above included).
            bool const pointed = gState.enabled && Point(*aim);
            capturing = pointed;
            if (capturing) {
                ++gState.enabledTicks;
                ++gCapturingTicks;
            }
        } else if (gState.enabled && !keep) {
            SetEnabled(false);
            FlushComponents();
            if (gEnables <= kLoggedEnables) {
                log::Info("scope capture: disabled (no eye behind the scope for {:.1f} s): no probe, nothing rendered", settings.holdSeconds);
            }
        }
    }
    sc::Capture out{};
    if (capturing) {
        out.state = sc::kCapturing;
        out.weapon = sight.weapon;
        out.pixelFormat = gState.targetFloat ? sc::kPixelFormatFloatRGBA : sc::kPixelFormatA8R8G8B8;
        out.width = static_cast<std::uint32_t>(gSizeX.Read<std::int32_t>(gState.target).value_or(gState.targetSize));
        out.height = static_cast<std::uint32_t>(gSizeY.Read<std::int32_t>(gState.target).value_or(gState.targetSize));
        out.resource = gResource.Read<std::uint64_t>(gState.target).value_or(0);
        out.generation = gState.generation;
        out.enabledTicks = gState.enabledTicks;
        out.tanHalf = aim->tanHalf;
        out.zoom = sight.zoom;
        out.pose = aim->pose;
        status = std::format("capturing: {}x{}, field {:.2f} degrees, near plane {:.0f} units", out.width, out.height, gState.fovDeg,
            gState.nearUnits);
    } else if (!settings.enabled) {
        status = "off ([Sights] scopeCapture = 0)";
    } else if (gFailed) {
        status = "off: " + gFailure;
    } else if (gState.actor != nullptr) {
        status = gState.enabled ? "enabled, held after the eye left" : "disabled (no eye behind a scope)";
    } else {
        status = "none yet (no eye has been behind a scope)";
    }
    gLast = out;
    link.publish(&out);
    gStatus = status;
    // The reasons it's off, once each (enables and disables have their own lines).
    if (status.rfind("off", 0) == 0 && status != gLoggedStatus) {
        log::Info("scope capture: {}", status);
    }
    gLoggedStatus = status;
#else
    (void)deltaSeconds;
    (void)sight;
    (void)ray;
    (void)hostView;
    PublishOff(link);
    gStatus = "off (LE1 only)";
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
#if defined(MEVR_GAME_LE1)
    return std::format("{{\"status\": {}, \"state\": {}, \"actor\": {}, \"enabled\": {}, \"enabledTicks\": {}, \"size\": [{}, {}], "
                       "\"format\": {}, \"resource\": \"0x{:X}\", \"generation\": {}, \"fovDeg\": {}, \"nearUnits\": {}, \"tanHalf\": {}, "
                       "\"postProcess\": {}, \"enables\": {}, \"capturingTicks\": {}, \"created\": {}, \"pose\": {{\"position\": [{}, {}, {}], "
                       "\"orientation\": [{}, {}, {}, {}]}}}}",
        json::JsonString(gStatus), gLast.state, json::JsonString(ue3::runtime::FullName(gState.actor)), gState.enabled ? "true" : "false",
        gState.enabledTicks, gLast.width, gLast.height, gLast.pixelFormat, gLast.resource, gLast.generation, json::JsonNumber(gState.fovDeg),
        json::JsonNumber(gState.nearUnits), json::JsonNumber(gLast.tanHalf), gState.postProcess ? "true" : "false", gEnables, gCapturingTicks,
        gCreated, json::JsonNumber(gLast.pose.position.x), json::JsonNumber(gLast.pose.position.y), json::JsonNumber(gLast.pose.position.z),
        json::JsonNumber(gLast.pose.orientation.x), json::JsonNumber(gLast.pose.orientation.y), json::JsonNumber(gLast.pose.orientation.z),
        json::JsonNumber(gLast.pose.orientation.w));
#else
    return std::format("{{\"status\": {}}}", json::JsonString(gStatus));
#endif
}

void LogStats() {
#if defined(MEVR_GAME_LE1)
    if (gState.actor != nullptr || gFailed) {
        log::Info("    scope capture: {}; enabled {} times since the last heartbeat ({} in all), {} ticks capturing in all", gStatus,
            gEnablesSinceStats, gEnables, gCapturingTicks);
    }
    gEnablesSinceStats = 0;
#endif
}

void Shutdown(bool exiting) {
#if defined(MEVR_GAME_LE1)
    if (!exiting && gState.actor != nullptr && ue3::runtime::Ready()) {
        Destroy("the mod shuts down");
    }
    gState = State{};
    gFailed = false;
    gFailure.clear();
    gFailedWorld = nullptr;
    gLastWantS = -1.0;
    gClockS = 0.0;
    gEnables = gEnablesSinceStats = gCapturingTicks = gCreated = 0;
    gSettingOn = false;
    gToldNoHost = false;
    for (ue3::ClassRef* c : {&gActorClass, &gComponentClass, &gTargetClass}) {
        c->Reset();
    }
    for (ue3::FunctionRef* f : {&gSpawn, &gDestroy, &gForceUpdate, &gCreate, &gSetEnabled, &gSetFrameRate, &gSetView, &gSetParameters}) {
        f->Reset();
    }
    for (ue3::FieldRef* f : {&gSceneCapture, &gStatic, &gNoDelete, &gLocation, &gEnabled, &gEnablePostProcess, &gEnableFog,
             &gEnableParticles, &gUseMainPostProcess, &gSkipUsersOccluded, &gSkipOwnerOccluded, &gMaxUpdateDist, &gPostProcess, &gFrameRate,
             &gTextureTarget, &gFieldOfView, &gResource, &gSizeX, &gSizeY, &gFormat, &gPlayer, &gPlayerPostProcess, &gDefaultPostProcess}) {
        f->Reset();
    }
#else
    (void)exiting;
#endif
    gLast = {};
    gStatus = "off";
    gLoggedStatus.clear();
}

} // namespace mevr::game::scope_capture
