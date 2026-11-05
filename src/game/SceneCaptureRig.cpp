// SPDX-License-Identifier: MIT
// One scene capture of our own (game/SceneCaptureRig.hpp), built as game/Spectator builds its one.
#include "game/SceneCaptureRig.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/Engine.hpp"
#include "game/Player.hpp"
#include "ue3/Handles.hpp"

#include <cmath>
#include <format>

namespace mevr::game::capture_rig {
namespace {

#if defined(MEVR_GAME_LE1)

// The spectator's handles (game/Spectator.cpp; their rows in engine/ReflectCheck).
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
ue3::FieldRef gFormat{"Engine.TextureRenderTarget2D", "Format"};
ue3::FieldRef gPlayer{"Engine.PlayerController", "Player"};
ue3::FieldRef gPlayerPostProcess{"Engine.LocalPlayer", "PlayerPostProcess"};
ue3::FieldRef gDefaultPostProcess{"Engine.Engine", "DefaultPostProcess"};

constexpr std::uint64_t kClassDefaultFlag = 0x200;  // EObjectFlags: RF_ClassDefaultObject
constexpr float kMaxUpdateDist = 10000000.0f;       // never stopped by distance (research/39 section 2)
constexpr std::int32_t kPixelFormatA8R8G8B8 = 2;    // EPixelFormat (research/47: Create takes 2, 3 and 9)
constexpr std::int32_t kPixelFormatFloatRGB = 9;

struct LinearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};

std::uint64_t gGenerations = 0;

ue3::Object* DefaultObject(ue3::Class* cls) {
    ue3::Reflection& r = ue3::runtime::Get();
    for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
        ue3::Object* const object = r.ObjectAt(i);
        if (object != nullptr && r.ClassOf(object) == cls && (r.FlagsOf(object) & kClassDefaultFlag) != 0) {
            return object;
        }
    }
    return nullptr;
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

ue3::Object* PostProcessChain(ue3::Object* controller) {
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

#endif

} // namespace

#if defined(MEVR_GAME_LE1)

bool Rig::Alive() const {
    return actor_ != nullptr && world_ == engine::World() && ue3::runtime::IsAlive(actor_) && ue3::IsA(actor_, gActorClass) &&
        component_ != nullptr && ue3::runtime::IsAlive(component_);
}

void Rig::Forget() {
    if (actor_ != nullptr) {
        log::Info("capture rig {}: forgot it (its objects went with the old level)", tag_);
    }
    actor_ = component_ = target_ = nullptr;
    world_ = nullptr;
    enabled_ = false;
    reattach_ = false;
    enabledTicks_ = 0;
}

bool Rig::Create(char const* tag, Options const& options, ue3::Vector const& at) {
    tag_ = tag;
    options_ = options;
    failure_.clear();
    Forget();
    if (!ue3::runtime::Ready()) {
        failure_ = "our reflection layer is off";
        return false;
    }
    for (ue3::FunctionRef* f : {&gSpawn, &gDestroy, &gForceUpdate, &gCreate, &gSetEnabled, &gSetFrameRate, &gSetView, &gSetParameters}) {
        if (f->Get() == nullptr) {
            failure_ = std::format("{} not found", f->Path());
            return false;
        }
    }
    for (ue3::ClassRef* c : {&gActorClass, &gComponentClass, &gTargetClass}) {
        if (c->Get() == nullptr) {
            failure_ = std::format("{} not found", c->Path());
            return false;
        }
    }
    for (ue3::FieldRef* f : {&gSceneCapture, &gResource, &gSkipUsersOccluded, &gSkipOwnerOccluded, &gMaxUpdateDist, &gUpdateMatrices}) {
        if (f->Get() == nullptr) {
            failure_ = std::format("the field {} not found", f->Name());
            return false;
        }
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        failure_ = "no player";
        return false;
    }
    // The render target.
    ue3::Object* const targetDefaults = DefaultObject(gTargetClass.Get());
    if (targetDefaults == nullptr) {
        failure_ = "no TextureRenderTarget2D defaults";
        return false;
    }
    ue3::Params create(ue3::runtime::Get(), gCreate.Get());
    std::uint8_t const format = static_cast<std::uint8_t>(options.floatTarget ? kPixelFormatFloatRGB : kPixelFormatA8R8G8B8);
    if (!create.Set("InSizeX", static_cast<std::int32_t>(options.width)) || !create.Set("InSizeY", static_cast<std::int32_t>(options.height)) ||
        !create.Set("InFormat", format) || !create.Set("InClearColor", LinearColor{}) || !create.SetBool("bOnlyRenderOnce", false) ||
        !Call(targetDefaults, gCreate, create)) {
        failure_ = "TextureRenderTarget2D.Create failed";
        return false;
    }
    target_ = create.Return<ue3::Object*>().value_or(nullptr);
    if (!ue3::IsA(target_, gTargetClass)) {
        target_ = nullptr;
        failure_ = "TextureRenderTarget2D.Create returned no render target";
        return false;
    }
    generation_ = ++gGenerations;
    // The actor (Spawn refuses a class whose defaults are static or no-delete: cleared for the call, research/39 section 2).
    ue3::Object* const actorDefaults = DefaultObject(gActorClass.Get());
    if (actorDefaults == nullptr) {
        failure_ = "no SceneCapture2DActor defaults";
        return false;
    }
    bool const wasStatic = gStatic.ReadBool(actorDefaults).value_or(false);
    bool const wasNoDelete = gNoDelete.ReadBool(actorDefaults).value_or(false);
    if (wasStatic || wasNoDelete) {
        gStatic.WriteBool(actorDefaults, false);
        gNoDelete.WriteBool(actorDefaults, false);
    }
    ue3::Params spawn(ue3::runtime::Get(), gSpawn.Get());
    spawn.Set("SpawnClass", gActorClass.Get());
    spawn.Set("SpawnOwner", controller);
    spawn.Set("SpawnLocation", at);
    spawn.SetBool("bNoCollisionFail", true);
    Call(controller, gSpawn, spawn);
    if (wasStatic || wasNoDelete) {
        gStatic.WriteBool(actorDefaults, wasStatic);
        gNoDelete.WriteBool(actorDefaults, wasNoDelete);
    }
    actor_ = spawn.Return<ue3::Object*>().value_or(nullptr);
    if (!ue3::IsA(actor_, gActorClass)) {
        actor_ = nullptr;
        failure_ = "Actor.Spawn returned no SceneCapture2DActor";
        return false;
    }
    world_ = engine::World();
    component_ = gSceneCapture.Read<ue3::Object*>(actor_).value_or(nullptr);
    if (!ue3::IsA(component_, gComponentClass)) {
        failure_ = std::format("{} has no SceneCapture2DComponent", ue3::runtime::FullName(actor_));
        component_ = nullptr;
        Destroy("no component");
        return false;
    }
    // The component: never skipped, its own view, the player's post process if asked.
    gUpdateMatrices.WriteBool(component_, false);     // else each update puts the actor's transform back (research/47)
    gSkipUsersOccluded.WriteBool(component_, false);  // nothing renders our texture
    gSkipOwnerOccluded.WriteBool(component_, false);  // nor the invisible actor
    WriteField(gMaxUpdateDist, component_, kMaxUpdateDist, "CaptureRig.MaxUpdateDist");
    gEnableFog.WriteBool(component_, true);
    gEnableParticles.WriteBool(component_, true);
    ue3::Object* const chain = !options.postProcess ? nullptr
        : options.postProcessChain != nullptr       ? options.postProcessChain
                                                    : PostProcessChain(controller);
    gEnablePostProcess.WriteBool(component_, chain != nullptr);
    gUseMainPostProcess.WriteBool(component_, chain != nullptr);
    WriteField(gPostProcess, component_, chain, "CaptureRig.PostProcess");
    WriteField(gFrameRate, component_, options.rate, "CaptureRig.FrameRate");
    enabled_ = gEnabled.ReadBool(component_).value_or(true);
    if (enabled_ && !SetEnabled(false)) {
        failure_ = "SetEnabled failed";
        Destroy("its setup failed");
        return false;
    }
    ue3::Params rate(ue3::runtime::Get(), gSetFrameRate.Get());
    ue3::Params parameters(ue3::runtime::Get(), gSetParameters.Get());
    if (!rate.Set("NewFrameRate", options.rate) || !Call(component_, gSetFrameRate, rate) || !parameters.Set("NewTextureTarget", target_) ||
        !parameters.Set("NewFOV", options.fovDeg) || !parameters.Set("NewNearPlane", options.nearUnits) ||
        !parameters.Set("NewFarPlane", options.farUnits) || !Call(component_, gSetParameters, parameters)) {
        failure_ = "SetFrameRate or SetCaptureParameters failed";
        Destroy("its setup failed");
        return false;
    }
    reattach_ = true;
    Flush();
    log::Info("capture rig {}: {} with {} into {} ({}x{} {}, {:.1f} degrees across, post process {})", tag_, ue3::runtime::FullName(actor_),
        ue3::runtime::FullName(component_), ue3::runtime::FullName(target_), options.width, options.height,
        options.floatTarget ? "FloatRGB" : "A8R8G8B8", options.fovDeg,
        chain == nullptr ? std::string("off") : options.postProcessChain != nullptr ? ue3::runtime::FullName(chain) : std::string("the player's"));
    return true;
}

void Rig::Destroy(char const* why) {
    if (actor_ != nullptr && world_ == engine::World() && ue3::runtime::IsAlive(actor_) && ue3::IsA(actor_, gActorClass)) {
        if (enabled_ && component_ != nullptr && ue3::runtime::IsAlive(component_)) {
            SetEnabled(false);
        }
        ue3::Params p(ue3::runtime::Get(), gDestroy.Get());
        Call(actor_, gDestroy, p);
        log::Info("capture rig {}: destroyed ({})", tag_, why);
    }
    actor_ = component_ = target_ = nullptr;
    world_ = nullptr;
    enabled_ = false;
    enabledTicks_ = 0;
}

bool Rig::SetEnabled(bool on) {
    if (component_ == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), gSetEnabled.Get());
    if (!p.SetBool("bEnable", on) || !Call(component_, gSetEnabled, p)) {
        return false;
    }
    enabled_ = on;
    enabledTicks_ = 0;
    reattach_ = true;
    return true;
}

bool Rig::SetFov(float fovDeg) {
    if (component_ == nullptr || target_ == nullptr || std::fabs(fovDeg - options_.fovDeg) < 0.01f) {
        return component_ != nullptr;
    }
    ue3::Params p(ue3::runtime::Get(), gSetParameters.Get());
    if (!p.Set("NewTextureTarget", target_) || !p.Set("NewFOV", fovDeg) || !p.Set("NewNearPlane", options_.nearUnits) ||
        !p.Set("NewFarPlane", options_.farUnits) || !Call(component_, gSetParameters, p)) {
        return false;
    }
    options_.fovDeg = fovDeg;
    reattach_ = true;
    return true;
}

bool Rig::Flush() {
    ue3::Params update(ue3::runtime::Get(), gForceUpdate.Get());
    update.SetBool("bCollisionUpdate", false);
    update.SetBool("bTransformOnly", !reattach_);
    if (!Call(actor_, gForceUpdate, update)) {
        return false;
    }
    reattach_ = false;
    return true;
}

bool Rig::Point(ue3::Vector const& location, ue3::Rotator const& rotation) {
    if (component_ == nullptr) {
        return false;
    }
    ue3::Params view(ue3::runtime::Get(), gSetView.Get());
    if (!view.Set("NewLocation", location) || !view.Set("NewRotation", rotation) || !Call(component_, gSetView, view)) {
        return false;
    }
    return Flush();
}

std::uint64_t Rig::Resource() const {
    return target_ != nullptr ? gResource.Read<std::uint64_t>(target_).value_or(0) : 0;
}

std::int32_t Rig::PixelFormat() const {
    return target_ != nullptr ? static_cast<std::int32_t>(gFormat.Read<std::uint8_t>(target_).value_or(0)) : 0;
}

#else

bool Rig::Alive() const { return false; }
void Rig::Forget() {}
bool Rig::Create(char const*, Options const&, ue3::Vector const&) { failure_ = "LE1 only"; return false; }
void Rig::Destroy(char const*) {}
bool Rig::SetEnabled(bool) { return false; }
bool Rig::SetFov(float) { return false; }
bool Rig::Flush() { return false; }
bool Rig::Point(ue3::Vector const&, ue3::Rotator const&) { return false; }
std::uint64_t Rig::Resource() const { return 0; }
std::int32_t Rig::PixelFormat() const { return 0; }

#endif

} // namespace mevr::game::capture_rig
