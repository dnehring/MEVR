// SPDX-License-Identifier: MIT
#include "game/CockpitProbe.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Engine.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace mevr::game::cockpit_probe {

#if defined(MEVR_GAME_LE1)

namespace {

namespace sc = vr::scope_capture;

// Our reflection layer (research/34). Every one of these has a row in engine/ReflectCheck.
ue3::ClassRef gActorClass{"Engine.Actor"};
ue3::ClassRef gInterpActor{"Engine.InterpActor"};
ue3::ClassRef gKActor{"Engine.KActor"};
ue3::ClassRef gKActorSpawnable{"Engine.KActorSpawnable"};
ue3::ClassRef gStaticMeshActor{"Engine.StaticMeshActor"};
ue3::ClassRef gDynamicSmActor{"Engine.DynamicSMActor"};
ue3::ClassRef gStaticMesh{"Engine.StaticMesh"};
ue3::ClassRef gStaticMeshComponent{"Engine.StaticMeshComponent"};
ue3::ClassRef gMaterial{"Engine.Material"};
ue3::ClassRef gMaterialInstance{"Engine.MaterialInstance"};
ue3::ClassRef gMic{"Engine.MaterialInstanceConstant"};
ue3::ClassRef gTextureParameterExpression{"Engine.MaterialExpressionTextureSampleParameter"};
ue3::ClassRef gCaptureActor{"Engine.SceneCapture2DActor"};
ue3::ClassRef gCaptureComponent{"Engine.SceneCapture2DComponent"};
ue3::ClassRef gTargetClass{"Engine.TextureRenderTarget2D"};
ue3::ClassRef gTexture{"Engine.Texture"};
ue3::ClassRef gTexture2D{"Engine.Texture2D"};
ue3::ClassRef gVehicleBase{"SFXGame.BioVehicleBase"};
ue3::ClassRef gBioPawn{"SFXGame.BioPawn"};
ue3::ClassRef gBioController{"SFXGame.BioPlayerController"};
ue3::ClassRef gSaveGame{"SFXGame.SFXSaveGame"};

ue3::FunctionRef gDynamicLoadObject{"Core.Object.DynamicLoadObject"};
ue3::FunctionRef gSpawn{"Engine.Actor.Spawn"};
ue3::FunctionRef gDestroy{"Engine.Actor.Destroy"};
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation"};
ue3::FieldRef gActorLocation{"Engine.Actor", "Location"};
ue3::FieldRef gActorRotation{"Engine.Actor", "Rotation"};
ue3::FunctionRef gSetRotation{"Engine.Actor.SetRotation"};
ue3::FunctionRef gSetDrawScale{"Engine.Actor.SetDrawScale"};
ue3::FunctionRef gSetPhysics{"Engine.Actor.SetPhysics"};
ue3::FunctionRef gSetCollision{"Engine.Actor.SetCollision"};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden"};
ue3::FunctionRef gSetBase{"Engine.Actor.SetBase"};
ue3::FunctionRef gSetHardAttach{"Engine.Actor.SetHardAttach"};
ue3::FunctionRef gForceUpdate{"Engine.Actor.ForceUpdateComponents"};
ue3::FunctionRef gSetActorCollision{"Engine.PrimitiveComponent.SetActorCollision"};
ue3::FunctionRef gSetBlockRigidBody{"Engine.PrimitiveComponent.SetBlockRigidBody"};
ue3::FunctionRef gSetTraceBlocking{"Engine.PrimitiveComponent.SetTraceBlocking"};
ue3::FunctionRef gSetStaticMesh{"Engine.StaticMeshComponent.SetStaticMesh"};
ue3::FunctionRef gGetMaterial{"Engine.MeshComponent.GetMaterial"};
ue3::FunctionRef gSetMaterial{"Engine.MeshComponent.SetMaterial"};
ue3::FunctionRef gGetNumElements{"Engine.MeshComponent.GetNumElements"};
ue3::FunctionRef gGetUnscaledBounds{"Engine.MeshComponent.GetUnscaledBounds"};
ue3::FunctionRef gCreateMic{"Engine.MeshComponent.CreateAndSetMaterialInstanceConstant"};
ue3::FunctionRef gSetParent{"Engine.MaterialInstanceConstant.SetParent"};
ue3::FunctionRef gSetTextureParameter{"Engine.MaterialInstanceConstant.SetTextureParameterValue"};
ue3::FunctionRef gGetTextureParameter{"Engine.MaterialInterface.GetTextureParameterValue"};
ue3::FunctionRef gLightEnvironmentSetEnabled{"Engine.LightEnvironmentComponent.SetEnabled"};
ue3::FunctionRef gCreateTarget{"Engine.TextureRenderTarget2D.Create"};
ue3::FunctionRef gCaptureSetEnabled{"Engine.SceneCaptureComponent.SetEnabled"};
ue3::FunctionRef gCaptureSetFrameRate{"Engine.SceneCaptureComponent.SetFrameRate"};
ue3::FunctionRef gCaptureSetView{"Engine.SceneCapture2DComponent.SetView"};
ue3::FunctionRef gCaptureSetParameters{"Engine.SceneCapture2DComponent.SetCaptureParameters"};
ue3::FunctionRef gEnterVehicle{"SFXGame.BioPlayerController.BioEnterVehicle"};
ue3::FunctionRef gFindClosestVehicle{"SFXGame.SFXSaveGame.FindClosestDrivableVehicle"};

ue3::FieldRef gStatic{"Engine.Actor", "bStatic"};
ue3::FieldRef gNoDelete{"Engine.Actor", "bNoDelete"};
ue3::FieldRef gMovable{"Engine.Actor", "bMovable"};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics"};
ue3::FieldRef gBase{"Engine.Actor", "Base"};
ue3::FieldRef gDynamicComponent{"Engine.DynamicSMActor", "StaticMeshComponent"};
ue3::FieldRef gDynamicLightEnvironment{"Engine.DynamicSMActor", "LightEnvironment"};
ue3::FieldRef gStaticActorComponent{"Engine.StaticMeshActor", "StaticMeshComponent"};
ue3::FieldRef gComponentMesh{"Engine.StaticMeshComponent", "StaticMesh"};
ue3::FieldRef gBounds{"Engine.PrimitiveComponent", "Bounds"};
ue3::FieldRef gParent{"Engine.MaterialInstance", "Parent"};
ue3::FieldRef gStaticPermutation{"Engine.MaterialInstance", "bHasStaticPermutationResource"};
ue3::FieldRef gTextureValues{"Engine.MaterialInstanceConstant", "TextureParameterValues"};
ue3::FieldRef gTextureValueName{gTextureValues, "ParameterName"};
ue3::FieldRef gTextureValueObject{gTextureValues, "ParameterValue"};
ue3::FieldRef gExpressions{"Engine.Material", "Expressions"};
ue3::FieldRef gEmissive{"Engine.Material", "EmissiveColor"};
ue3::FieldRef gEmissiveExpression{gEmissive, "Expression"};
ue3::FieldRef gBlendMode{"Engine.Material", "BlendMode"};
ue3::FieldRef gLightingModel{"Engine.Material", "LightingModel"};
ue3::FieldRef gUsedStaticLighting{"Engine.Material", "bUsedWithStaticLighting"};
ue3::FieldRef gUsedLightEnvironments{"Engine.Material", "bUsedWithLightEnvironments"};
ue3::FieldRef gExpressionParameterName{"Engine.MaterialExpressionTextureSampleParameter", "ParameterName"};
// The capture's own settings, as game/ScopeCapture sets them (research/39 section 2).
ue3::FieldRef gSceneCapture{"Engine.SceneCaptureActor", "SceneCapture"};
ue3::FieldRef gCaptureEnabled{"Engine.SceneCaptureComponent", "bEnabled"};
ue3::FieldRef gEnablePostProcess{"Engine.SceneCaptureComponent", "bEnablePostProcess"};
ue3::FieldRef gUseMainPostProcess{"Engine.SceneCaptureComponent", "bUseMainScenePostProcessSettings"};
ue3::FieldRef gSkipUsersOccluded{"Engine.SceneCaptureComponent", "bSkipUpdateIfTextureUsersOccluded"};
ue3::FieldRef gSkipOwnerOccluded{"Engine.SceneCaptureComponent", "bSkipUpdateIfOwnerOccluded"};
ue3::FieldRef gMaxUpdateDist{"Engine.SceneCaptureComponent", "MaxUpdateDist"};
ue3::FieldRef gPostProcess{"Engine.SceneCaptureComponent", "PostProcess"};
ue3::FieldRef gFrameRate{"Engine.SceneCaptureComponent", "FrameRate"};
ue3::FieldRef gSizeX{"Engine.TextureRenderTarget2D", "SizeX"};
ue3::FieldRef gSizeY{"Engine.TextureRenderTarget2D", "SizeY"};
ue3::FieldRef gPlayer{"Engine.PlayerController", "Player"};
ue3::FieldRef gPlayerPostProcess{"Engine.LocalPlayer", "PlayerPostProcess"};
ue3::FieldRef gDefaultPostProcess{"Engine.Engine", "DefaultPostProcess"};
// On by default: every SetParentToWorld (each update of the component) overwrites ViewMatrix from the actor. Off, SetView holds (research/47).
ue3::FieldRef gUpdateMatrices{"Engine.SceneCapture2DComponent", "bUpdateMatrices"};
ue3::FieldRef gViewMatrix{"Engine.SceneCapture2DComponent", "ViewMatrix"};
ue3::FieldRef gForceLinearGamma{"Engine.TextureRenderTarget2D", "bForceLinearGamma"};
ue3::FieldRef gTargetFormat{"Engine.TextureRenderTarget2D", "Format"};
// What `info` reads.
ue3::FieldRef gMeshMaterials{"Engine.MeshComponent", "Materials"};
ue3::FieldRef gPrimitiveLightEnvironment{"Engine.PrimitiveComponent", "LightEnvironment"};
ue3::FieldRef gLightEnvironmentEnabled{"Engine.LightEnvironmentComponent", "bEnabled"};
ue3::FieldRef gTextureResource{"Engine.Texture", "Resource"};
ue3::FieldRef gTextureSrgb{"Engine.Texture", "SRGB"};
ue3::FieldRef gTextureNeverStream{"Engine.Texture", "NeverStream"};
ue3::FieldRef gTextureLodGroup{"Engine.Texture", "LODGroup"};
ue3::FieldRef gTexture2DSizeX{"Engine.Texture2D", "SizeX"};
ue3::FieldRef gTexture2DSizeY{"Engine.Texture2D", "SizeY"};
ue3::FieldRef gTexture2DFormat{"Engine.Texture2D", "Format"};
ue3::FieldRef gTexture2DRequestedMips{"Engine.Texture2D", "RequestedMips"};
ue3::FieldRef gTexture2DResidentMips{"Engine.Texture2D", "ResidentMips"};
ue3::FieldRef gTexture2DMipTail{"Engine.Texture2D", "MipTailBaseIdx"};
ue3::FieldRef gTexture2DMips{"Engine.Texture2D", "Mips"};
// For `mevr vehicle`.
ue3::FieldRef gWorldInfo{"Engine.Actor", "WorldInfo"};

// EObjectFlags: RF_ClassDefaultObject.
constexpr std::uint64_t kClassDefaultFlag = 0x200;
constexpr std::uint8_t kPhysNone = 0;  // EPhysics
constexpr int kLightingUnlit = 2;      // EMaterialLightingModel: MLM_Unlit
constexpr int kBlendOpaque = 0;        // EBlendMode
constexpr int kBlendMasked = 1;
constexpr std::size_t kListed = 40;
constexpr int kQuarterTurn = 16384;    // Unreal rotation units
// See research/39 section 2: the capture never stops for distance.
constexpr float kMaxUpdateDist = 10000000.0f;

struct LinearColor {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;
};
// Core.Object.BoxSphereBounds.
struct BoxSphereBounds {
    ue3::Vector origin{};
    ue3::Vector extent{};
    float radius = 0.0f;
};
static_assert(sizeof(BoxSphereBounds) == 28);

// The classes the mesh's actor can be. The automatic order is the first kAutomatic: InterpActor is made to be moved and
// has a light environment; KActorSpawnable is the only one meant to be spawned but starts as a rigid body; StaticMeshActor
// is the last resort (a static actor whose flags we clear, lit as static, no light environment). KActor and
// DynamicSMActor only on `spawnclass` (DynamicSMActor is abstract [verify]: Spawn should refuse it).
struct ActorKind {
    char const* label;
    ue3::ClassRef* cls;
    ue3::FieldRef* component;
    ue3::FieldRef* lightEnvironment;
};
std::array<ActorKind, 5> const kKinds{{
    {"InterpActor", &gInterpActor, &gDynamicComponent, &gDynamicLightEnvironment},
    {"KActorSpawnable", &gKActorSpawnable, &gDynamicComponent, &gDynamicLightEnvironment},
    {"StaticMeshActor", &gStaticMeshActor, &gStaticActorComponent, nullptr},
    {"KActor", &gKActor, &gDynamicComponent, &gDynamicLightEnvironment},
    {"DynamicSMActor", &gDynamicSmActor, &gDynamicComponent, &gDynamicLightEnvironment},
}};
constexpr std::size_t kAutomatic = 3;

// What the console lines set: survives `clear` and a new level, reset at the mod's shutdown.
struct Settings {
    float distanceM = 1.5f;
    float scale = 1.0f;
    bool follow = true;
    bool userRotation = false;
    ue3::Rotator rotation{};  // added to "facing the head"; from the mesh's flat axis unless `rot` set it
    float chaseBackM = 6.0f;
    float chaseUpM = 3.0f;
    float captureRate = 30.0f;  // about every third frame at 90 Hz (engine time: research/39 section 3)
    std::string spawnClass;     // `spawnclass`: only that one
    std::string materialName;   // `material`: the parent, by name
    std::string paramName;      // `param`: the texture parameter, by name
    bool aim = true;            // `aim 0`: no more SetView, so the probe lives on and FrameRate throttles it (research/47)
    bool postProcess = true;    // `post 0`: the capture without the post chain
    int element = -1;           // `element N`: our instance on that material slot only (a console's screen); -1 every slot
};
Settings gSettings;

// What's spawned in the current world.
struct State {
    void const* world = nullptr;
    ue3::Object* actor = nullptr;
    ue3::Object* component = nullptr;
    ue3::Object* mesh = nullptr;
    char const* kind = "";
    bool attached = false;
    ue3::Object* mic = nullptr;
    ue3::Object* parent = nullptr;
    ue3::Name parameter{};
    bool parameterSet = false;
    ue3::Object* target = nullptr;
    ue3::Object* shownTexture = nullptr;  // `texture`: put on the instance instead of the render target
    int targetWidth = 512;
    int targetHeight = 288;
    bool targetFloat = true;
    ue3::Object* captureActor = nullptr;
    ue3::Object* captureComponent = nullptr;
    std::uint32_t placedTicks = 0;
    std::uint32_t pointedTicks = 0;
    double lastPointed = -1.0;
    ue3::Vector askedView{};
};
State gState;
ue3::Object* gLoadedMesh = nullptr;  // what `load` last returned; nothing holds it: checked alive before use
std::string gLastError = "none";
bool gLoggedMoveFailure = false;
bool gLoggedFirstPlace = false;
bool gLoggedFlatFallback = false;

double Now() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

ue3::Reflection& R() {
    return ue3::runtime::Get();
}

bool Call(ue3::Object* object, ue3::FunctionRef& function, ue3::Params& params) {
    return object != nullptr && function.Get() != nullptr && ue3::runtime::Call(object, function.Get(), params);
}

ue3::Params ParamsOf(ue3::FunctionRef& function) {
    return ue3::Params(R(), function.Get());
}

std::string Lower(std::string_view text) {
    return ue3::Lower(text);
}

bool Contains(std::string const& lowerText, std::string_view lowerWord) {
    return lowerText.find(lowerWord) != std::string::npos;
}

// A readable answer for the console that is also the last error and a log line.
std::string Refuse(std::string why) {
    gLastError = why;
    log::Warn("cockpit probe: {}", why);
    return why;
}

// The first handle that doesn't resolve, as a readable message.
std::optional<std::string> MissingFunction(std::initializer_list<ue3::FunctionRef*> functions) {
    for (ue3::FunctionRef* const f : functions) {
        if (f->Get() == nullptr) {
            return std::format("{} not found", f->Path());
        }
    }
    return std::nullopt;
}

std::optional<std::string> MissingClass(std::initializer_list<ue3::ClassRef*> classes) {
    for (ue3::ClassRef* const c : classes) {
        if (c->Get() == nullptr) {
            return std::format("{} not found", c->Path());
        }
    }
    return std::nullopt;
}

ue3::Object* DefaultObject(ue3::Class* cls) {
    ue3::Reflection& r = R();
    std::int32_t const count = r.ObjectCount();
    for (std::int32_t i = 0; i < count; ++i) {
        ue3::Object* const object = r.ObjectAt(i);
        if (object != nullptr && r.ClassOf(object) == cls && (r.FlagsOf(object) & kClassDefaultFlag) != 0) {
            return object;
        }
    }
    return nullptr;
}

template <class Fn>
void ForEachLive(ue3::Class const* cls, Fn&& fn) {
    if (cls == nullptr) {
        return;
    }
    ue3::Reflection& r = R();
    std::int32_t const count = r.ObjectCount();
    for (std::int32_t i = 0; i < count; ++i) {
        ue3::Object* const object = r.ObjectAt(i);
        if (object != nullptr && r.IsAlive(object) && r.IsA(object, cls) && (r.FlagsOf(object) & kClassDefaultFlag) == 0) {
            fn(object);
        }
    }
}

bool Still(ue3::Object* object, ue3::ClassRef& cls) {
    return object != nullptr && gState.world == engine::World() && ue3::runtime::IsAlive(object) && ue3::IsA(object, cls);
}

// ---- Spawning (the default-clearing trick of game/ScopeCapture) -----------------------------------------------------

std::string DefaultsText(ue3::Object* defaults) {
    return std::format("bStatic {}, bNoDelete {}, bMovable {}, Physics {}", gStatic.ReadBool(defaults).value_or(false) ? 1 : 0,
        gNoDelete.ReadBool(defaults).value_or(false) ? 1 : 0, gMovable.ReadBool(defaults).value_or(false) ? 1 : 0,
        static_cast<int>(gPhysics.Read<std::uint8_t>(defaults).value_or(0)));
}

// Actor.Spawn on the player controller. A class marked bStatic or bNoDelete can't be spawned: the flags are cleared on its
// default object for the one call (the spawned actor copies them) and put back at once.
ue3::Object* SpawnActor(ue3::Object* controller, ue3::Class* cls, ue3::Vector const& at) {
    ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
    if (defaults == nullptr || gSpawn.Get() == nullptr) {
        return nullptr;
    }
    bool const wasStatic = gStatic.ReadBool(defaults).value_or(false);
    bool const wasNoDelete = gNoDelete.ReadBool(defaults).value_or(false);
    if (wasStatic || wasNoDelete) {
        gStatic.WriteBool(defaults, false);    // journalled by the handle
        gNoDelete.WriteBool(defaults, false);
    }
    ue3::Params p = ParamsOf(gSpawn);
    p.Set("SpawnClass", cls);
    p.Set("SpawnOwner", controller);
    p.Set("SpawnLocation", at);
    p.SetBool("bNoCollisionFail", true);
    Call(controller, gSpawn, p);
    if (wasStatic || wasNoDelete) {
        gStatic.WriteBool(defaults, wasStatic);
        gNoDelete.WriteBool(defaults, wasNoDelete);
    }
    return p.Return<ue3::Object*>().value_or(nullptr);
}

// Destroy, or hide it if the engine refuses (a flag we couldn't clear).
void DestroyActor(ue3::Object* actor, char const* why) {
    ue3::Params p = ParamsOf(gDestroy);
    Call(actor, gDestroy, p);
    bool const destroyed = p.ReturnBool().value_or(false);
    log::Info("cockpit probe: destroying {} ({}): Destroy returned {}", ue3::runtime::FullName(actor), why, destroyed ? "true" : "false");
    if (!destroyed && ue3::runtime::IsAlive(actor)) {
        ue3::Params hide = ParamsOf(gSetHidden);
        hide.SetBool("bNewHidden", true);
        Call(actor, gSetHidden, hide);
    }
}

void ClearAll(char const* why) {
    if (Still(gState.captureComponent, gCaptureComponent)) {
        ue3::Params off = ParamsOf(gCaptureSetEnabled);
        off.SetBool("bEnable", false);
        Call(gState.captureComponent, gCaptureSetEnabled, off);
    }
    if (Still(gState.captureActor, gActorClass)) {
        DestroyActor(gState.captureActor, why);
    }
    if (Still(gState.actor, gActorClass)) {
        DestroyActor(gState.actor, why);
    }
    // The render target and the material instance have nothing to destroy: nothing refers to them now, the garbage takes them.
    gState = State{};
}

// A new level: the old world's objects are gone, never call them. Anything else gone: what is left goes too.
void Refresh() {
    if (gState.actor == nullptr && gState.captureActor == nullptr) {
        return;
    }
    if (gState.world != engine::World()) {
        log::Info("cockpit probe: a new level: forgot what it spawned (its objects went with the old level)");
        gState = State{};
        return;
    }
    bool const meshGone = gState.actor != nullptr && (!Still(gState.actor, gActorClass) || !Still(gState.component, gStaticMeshComponent));
    bool const captureGone = gState.captureActor != nullptr && (!Still(gState.captureActor, gActorClass) ||
                                                                   !Still(gState.captureComponent, gCaptureComponent) || !Still(gState.target, gTargetClass));
    if (meshGone || captureGone) {
        ClearAll("a part of it is gone");
    }
}

// ---- Where the head is ---------------------------------------------------------------------------------------------

struct Eye {
    engine::UnrealVector location{};
    engine::UnrealVector forward{1.0, 0.0, 0.0};
    double unitsPerMeter = engine::kUnitsPerMeter;
};

// The head through the view anchor; without a head-tracked view (flat, a menu) the player camera, so the probe also runs
// on the desktop.
std::optional<Eye> HeadNow() {
    if (auto const frame = view_anchor::Current(); frame && frame->unitsPerMeter > 0.0) {
        return Eye{frame->headWorld, engine::OrientationToWorld(*frame, frame->head.orientation).forward, frame->unitsPerMeter};
    }
    ue3::Object* const camera = player::Camera(player::Controller());
    if (auto const pov = camera != nullptr ? player::CameraPov(camera) : std::nullopt; pov && pov->location != nullptr && pov->rotation != nullptr) {
        if (!gLoggedFlatFallback) {
            gLoggedFlatFallback = true;
            log::Info("cockpit probe: no head-tracked view: placing from the player camera");
        }
        engine::UnrealRotation const rotation{pov->rotation->pitch, pov->rotation->yaw, pov->rotation->roll};
        return Eye{{pov->location->x, pov->location->y, pov->location->z}, engine::ToBasis(rotation).forward, engine::kUnitsPerMeter};
    }
    return std::nullopt;
}

bool FlushActor(ue3::Object* actor, bool transformOnly) {
    ue3::Params update = ParamsOf(gForceUpdate);
    update.SetBool("bCollisionUpdate", false);
    update.SetBool("bTransformOnly", transformOnly);
    return Call(actor, gForceUpdate, update);
}

// `distanceM` ahead of the head, facing it (the mesh's +X at the head, plus the flat axis' or `rot`'s offset).
bool Place() {
    std::optional<Eye> const eye = HeadNow();
    if (!eye) {
        return false;
    }
    double const out = static_cast<double>(gSettings.distanceM) * eye->unitsPerMeter;
    ue3::Vector const location{static_cast<float>(eye->location.x + eye->forward.x * out), static_cast<float>(eye->location.y + eye->forward.y * out),
        static_cast<float>(eye->location.z + eye->forward.z * out)};
    engine::UnrealRotation const facing = engine::ToRotation({-eye->forward.x, -eye->forward.y, -eye->forward.z});
    ue3::Rotator const rotation{facing.pitch + gSettings.rotation.pitch, facing.yaw + gSettings.rotation.yaw, facing.roll + gSettings.rotation.roll};
    // Written, not SetLocation/SetRotation: those go through UWorld::FarMoveActor's collision test, which refuses a spot
    // the actor would encroach on (a cockpit around the player always would; research/47).
    ue3::Vector* const atField = gActorLocation.In<ue3::Vector>(gState.actor);
    ue3::Rotator* const turnField = gActorRotation.In<ue3::Rotator>(gState.actor);
    bool const moved = atField != nullptr;
    bool const turned = turnField != nullptr;
    if (atField != nullptr) {
        *atField = location;
        write_journal::Wrote("CockpitProbe.location", atField, sizeof(*atField));
    }
    if (turnField != nullptr) {
        *turnField = rotation;
        write_journal::Wrote("CockpitProbe.rotation", turnField, sizeof(*turnField));
    }
    FlushActor(gState.actor, true);  // out now, before the next Draw renders it (research/39 section 5)
    if ((!moved || !turned) && !gLoggedMoveFailure) {
        gLoggedMoveFailure = true;
        gLastError = std::format("Location field {}, Rotation field {} on {}", moved, turned, ue3::runtime::FullName(gState.actor));
        log::Warn("cockpit probe: {} (logged once)", gLastError);
    }
    if (!gLoggedFirstPlace) {
        gLoggedFirstPlace = true;
        log::Info("cockpit probe: first placement: {:.0f} {:.0f} {:.0f} rotation {} {} {}, {:.2f} m ahead at {:.0f} units a metre; moved {}, turned {}",
            location.x, location.y, location.z, rotation.pitch, rotation.yaw, rotation.roll, gSettings.distanceM, eye->unitsPerMeter, moved, turned);
    }
    return moved;
}

// ---- Meshes --------------------------------------------------------------------------------------------------------

// The extent of each mesh a live component shows (world units, the component's scale in it): the StaticMesh's own bounds
// aren't a script property in LE1, so the map only knows placed meshes.
std::unordered_map<ue3::Object*, ue3::Vector> PlacedExtents() {
    std::unordered_map<ue3::Object*, ue3::Vector> extents;
    ForEachLive(gStaticMeshComponent.Get(), [&](ue3::Object* component) {
        ue3::Object* const mesh = gComponentMesh.Read<ue3::Object*>(component).value_or(nullptr);
        auto const bounds = gBounds.Read<BoxSphereBounds>(component);
        if (mesh != nullptr && bounds && (bounds->extent.x > 0.0f || bounds->extent.y > 0.0f || bounds->extent.z > 0.0f)) {
            extents.try_emplace(mesh, bounds->extent);
        }
    });
    return extents;
}

// What makes a good stand-in: a name that says panel or box, flat and of a size a screen could be, placed in this level
// (its material is known to draw on a static mesh).
int MeshScore(std::string const& lowerName, ue3::Vector const* extent) {
    int score = 0;
    for (std::string_view const word : {"panel", "screen", "monitor", "display", "terminal", "plane", "quad", "sign", "billboard", "card"}) {
        if (Contains(lowerName, word)) {
            score += 30;
            break;
        }
    }
    for (std::string_view const word : {"crate", "box", "cube"}) {
        if (Contains(lowerName, word)) {
            score += 15;
            break;
        }
    }
    for (std::string_view const word : {"collision", "proxy", "shadow", "blocker", "occlu"}) {
        if (Contains(lowerName, word)) {
            score -= 50;
            break;
        }
    }
    if (extent != nullptr) {
        score += 5;
        float const smallest = std::min({extent->x, extent->y, extent->z});
        float const largest = std::max({extent->x, extent->y, extent->z});
        if (largest >= 20.0f && largest <= 300.0f) {
            score += 10;
            if (smallest < 0.2f * largest) {
                score += 40;
            }
        }
    }
    return score;
}

struct MeshRow {
    ue3::Object* mesh = nullptr;
    std::string path;
    std::optional<ue3::Vector> extent;
    int score = 0;
};

std::vector<MeshRow> MeshRows(std::string const& lowerFilter) {
    std::unordered_map<ue3::Object*, ue3::Vector> const placed = PlacedExtents();
    std::vector<MeshRow> rows;
    ForEachLive(gStaticMesh.Get(), [&](ue3::Object* mesh) {
        std::string path = R().PathName(mesh);
        std::string const lower = Lower(path);
        if (!lowerFilter.empty() && !Contains(lower, lowerFilter)) {
            return;
        }
        MeshRow row;
        row.mesh = mesh;
        if (auto const it = placed.find(mesh); it != placed.end()) {
            row.extent = it->second;
        }
        row.score = MeshScore(lower, row.extent ? &*row.extent : nullptr);
        row.path = std::move(path);
        rows.push_back(std::move(row));
    });
    std::stable_sort(rows.begin(), rows.end(), [](MeshRow const& a, MeshRow const& b) { return a.score > b.score; });
    return rows;
}

std::string ListMeshes(std::string const& filter) {
    if (auto const missing = MissingClass({&gStaticMesh, &gStaticMeshComponent})) {
        return Refuse(*missing);
    }
    std::vector<MeshRow> const rows = MeshRows(Lower(filter));
    std::string out = std::format("{} StaticMesh objects loaded{}; the likeliest stand-ins first (extents: half-sizes in units where a component "
                                  "shows it, with that component's scale; LE1 has no script bounds on the mesh itself):\n",
        rows.size(), filter.empty() ? std::string() : std::format(" matching '{}'", filter));
    for (std::size_t i = 0; i < rows.size() && i < kListed; ++i) {
        MeshRow const& row = rows[i];
        out += std::format("  {:4} {}  {}\n", row.score, row.path,
            row.extent ? std::format("extent {:.0f} {:.0f} {:.0f}", row.extent->x, row.extent->y, row.extent->z) : std::string("(not placed here)"));
    }
    return out;
}

// A package's object through the engine's own loader, by "Package.Object" (research/47 "Loading a package"): the top of the
// path must be a package file the TOC lists. The class only narrows what counts as found. May block for the load; on the
// game thread like everything here. Nothing holds the object until a component shows it: the next garbage collection may
// take it, so `load` and `mesh` belong in one console call.
ue3::Object* LoadObject(std::string const& path, ue3::Class* cls) {
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr || cls == nullptr || gDynamicLoadObject.Get() == nullptr) {
        return nullptr;
    }
    ue3::memory::OwnedString text(ue3::memory::Widen(path));
    if (!text.Valid()) {
        return nullptr;
    }
    ue3::Params p = ParamsOf(gDynamicLoadObject);
    if (!p.Set("ObjectName", text.Get()) || !p.Set("ObjectClass", cls) || !p.SetBool("MayFail", true)) {
        log::Warn("cockpit probe: Core.Object.DynamicLoadObject's parameters aren't (ObjectName, ObjectClass, MayFail)");
        return nullptr;
    }
    Call(controller, gDynamicLoadObject, p);
    return p.Return<ue3::Object*>().value_or(nullptr);
}

// By exact path or object name, else the best row whose path holds the text; a dotted path nothing loaded matches exactly
// goes to the engine's loader; empty: what `load` last returned, else the best row of all.
ue3::Object* FindMesh(std::string const& name) {
    if (name.empty() && gLoadedMesh != nullptr && ue3::runtime::IsAlive(gLoadedMesh) && ue3::IsA(gLoadedMesh, gStaticMesh)) {
        return gLoadedMesh;
    }
    std::string const lower = Lower(name);
    std::vector<MeshRow> const rows = MeshRows(lower);
    for (MeshRow const& row : rows) {
        if (Lower(row.path) == lower || Lower(R().NameText(row.mesh)) == lower) {
            return row.mesh;
        }
    }
    if (name.find('.') != std::string::npos) {
        if (ue3::Object* const loaded = LoadObject(name, gStaticMesh.Get()); ue3::IsA(loaded, gStaticMesh)) {
            log::Info("cockpit probe: loaded {}", ue3::runtime::FullName(loaded));
            gLoadedMesh = loaded;
            return loaded;
        }
    }
    return rows.empty() ? nullptr : rows.front().mesh;
}

// `load <Package.Object> [class]`: only the load, and what it brought. Default class: Engine.StaticMesh.
std::string DoLoad(std::istringstream& words) {
    std::string path;
    std::string className = "Engine.StaticMesh";
    words >> path >> className;
    if (path.empty()) {
        return "usage: mevr cockpit load <Package.Object> [class, default Engine.StaticMesh]";
    }
    if (auto const missing = MissingFunction({&gDynamicLoadObject})) {
        return Refuse(*missing);
    }
    ue3::Class* const cls = R().FindClass(className);
    if (cls == nullptr) {
        return Refuse(std::format("no class {}", className));
    }
    std::int32_t const before = R().ObjectCount();
    ue3::Object* const loaded = LoadObject(path, cls);
    if (loaded == nullptr) {
        return Refuse(std::format("DynamicLoadObject returned None for {} ({}): the package isn't in the TOC, or the object isn't in it, or an import failed: see the game's log and research/47 \"Loading a package\"",
            path, className));
    }
    bool const isMesh = ue3::IsA(loaded, gStaticMesh);
    if (isMesh) {
        gLoadedMesh = loaded;
    }
    std::string report = std::format("loaded {}; {} objects before, {} now", ue3::runtime::FullName(loaded), before, R().ObjectCount());
    if (isMesh) {
        report += "; mevr cockpit mesh (with no name) shows it. Same console call: nothing holds it until then";
    }
    log::Info("cockpit probe: {}", report);
    gLastError = "none";
    return report;
}

// A flat mesh's flat axis decides how it faces the head, until `rot` says otherwise: +Y's normal after a -90 degrees
// yaw, +Z's after -90 degrees of pitch. A one-sided panel may show its back: `rot 0 180 0`.
std::string TakeBounds() {
    ue3::Params p = ParamsOf(gGetUnscaledBounds);
    Call(gState.component, gGetUnscaledBounds, p);
    auto const bounds = p.Get<BoxSphereBounds>("UnscaledBounds");
    if (!bounds) {
        return "its bounds can't be read";
    }
    ue3::Vector const& e = bounds->extent;
    float const largest = std::max({e.x, e.y, e.z});
    float const smallest = std::min({e.x, e.y, e.z});
    char const* flat = "none (not flat)";
    if (largest > 0.0f && smallest < 0.25f * largest) {
        flat = smallest == e.x ? "X" : smallest == e.y ? "Y" : "Z";
        if (!gSettings.userRotation) {
            gSettings.rotation = smallest == e.x ? ue3::Rotator{} : smallest == e.y ? ue3::Rotator{0, -kQuarterTurn, 0} : ue3::Rotator{-kQuarterTurn, 0, 0};
        }
    } else if (!gSettings.userRotation) {
        gSettings.rotation = {};
    }
    return std::format("unscaled half-sizes {:.1f} {:.1f} {:.1f} units, flat axis {}", e.x, e.y, e.z, flat);
}

void ApplyScale() {
    ue3::Params p = ParamsOf(gSetDrawScale);
    p.Set("NewScale", gSettings.scale);
    Call(gState.actor, gSetDrawScale, p);
    FlushActor(gState.actor, false);
}

// No collision of any kind (it would stop the player and catch shots), no physics; its light environment on, if it has one.
void MakeHarmless(ActorKind const& kind, ue3::Object* actor, ue3::Object* meshComponent) {
    ue3::Params physics = ParamsOf(gSetPhysics);
    physics.Set("newPhysics", kPhysNone);
    Call(actor, gSetPhysics, physics);
    ue3::Params collision = ParamsOf(gSetCollision);
    collision.SetBool("bNewColActors", false);
    collision.SetBool("bNewBlockActors", false);
    collision.SetBool("bNewIgnoreEncroachers", true);
    Call(actor, gSetCollision, collision);
    ue3::Params component = ParamsOf(gSetActorCollision);
    component.SetBool("NewCollideActors", false);
    component.SetBool("NewBlockActors", false);
    Call(meshComponent, gSetActorCollision, component);
    ue3::Params rigid = ParamsOf(gSetBlockRigidBody);
    rigid.SetBool("bNewBlockRigidBody", false);
    Call(meshComponent, gSetBlockRigidBody, rigid);
    ue3::Params trace = ParamsOf(gSetTraceBlocking);
    trace.SetBool("NewBlockZeroExtent", false);
    trace.SetBool("NewBlockNonZeroExtent", false);
    Call(meshComponent, gSetTraceBlocking, trace);
    if (kind.lightEnvironment != nullptr) {
        if (ue3::Object* const environment = kind.lightEnvironment->Read<ue3::Object*>(actor).value_or(nullptr)) {
            ue3::Params on = ParamsOf(gLightEnvironmentSetEnabled);
            on.SetBool("bNewEnabled", true);
            Call(environment, gLightEnvironmentSetEnabled, on);
        }
    }
}

// Spawns the mesh's actor: `spawnclass`'s, else the automatic order. Says what each candidate did.
bool SpawnMeshActor(ue3::Object* controller, ue3::Vector const& at, std::string& report) {
    std::size_t const end = gSettings.spawnClass.empty() ? kAutomatic : kKinds.size();
    for (std::size_t i = 0; i < end; ++i) {
        ActorKind const& kind = kKinds[i];
        if (!gSettings.spawnClass.empty() && Lower(gSettings.spawnClass) != Lower(kind.label)) {
            continue;
        }
        ue3::Class* const cls = kind.cls->Get();
        ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
        if (defaults == nullptr) {
            report += std::format("{}: no class or default object\n", kind.label);
            continue;
        }
        std::string const flags = DefaultsText(defaults);
        ue3::Object* const actor = SpawnActor(controller, cls, at);
        if (!ue3::IsA(actor, *kind.cls)) {
            report += std::format("{}: defaults {}: Spawn returned nothing\n", kind.label, flags);
            continue;
        }
        ue3::Object* const component = kind.component->Read<ue3::Object*>(actor).value_or(nullptr);
        if (!ue3::IsA(component, gStaticMeshComponent)) {
            report += std::format("{}: defaults {}: spawned, but no StaticMeshComponent ({}): destroyed\n", kind.label, flags, ue3::runtime::FullName(component));
            DestroyActor(actor, "no StaticMeshComponent");
            continue;
        }
        gState = State{};
        gState.world = engine::World();
        gState.actor = actor;
        gState.component = component;
        gState.kind = kind.label;
        report += std::format("{}: defaults {}: spawned {} with {} (the actor: bStatic {}, bNoDelete {}, bMovable {})\n", kind.label, flags,
            ue3::runtime::FullName(actor), ue3::runtime::FullName(component), gStatic.ReadBool(actor).value_or(false) ? 1 : 0,
            gNoDelete.ReadBool(actor).value_or(false) ? 1 : 0, gMovable.ReadBool(actor).value_or(false) ? 1 : 0);
        MakeHarmless(kind, actor, component);
        return true;
    }
    return false;
}

void ReapplyMaterial();

std::string DoMesh(std::string const& name) {
    if (auto const missing = MissingFunction({&gSpawn, &gDestroy, &gSetLocation, &gSetRotation, &gSetDrawScale, &gForceUpdate, &gSetStaticMesh, &gSetCollision})) {
        return Refuse(*missing);
    }
    if (auto const missing = MissingClass({&gStaticMesh, &gStaticMeshComponent, &gActorClass})) {
        return Refuse(*missing);
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        return Refuse("no player controller");
    }
    ue3::Object* const mesh = FindMesh(name);
    if (mesh == nullptr) {
        return Refuse(name.empty() ? std::string("no StaticMesh is loaded") : std::format("no loaded StaticMesh matches '{}' (mevr cockpit list)", name));
    }
    std::string report;
    if (gState.actor == nullptr) {
        std::optional<Eye> const eye = HeadNow();
        ue3::Vector const at = eye ? ue3::Vector{static_cast<float>(eye->location.x), static_cast<float>(eye->location.y), static_cast<float>(eye->location.z)}
                                   : player::Location(controller).value_or(ue3::Vector{});
        if (!SpawnMeshActor(controller, at, report)) {
            log::Warn("cockpit probe: no actor could be spawned:\n{}", report);
            return Refuse(report + "no actor could be spawned: nothing changed");
        }
    }
    ue3::Params set = ParamsOf(gSetStaticMesh);
    set.Set("NewMesh", mesh);
    set.SetBool("bForce", true);  // the native refuses an owner it thinks static otherwise [verify]
    Call(gState.component, gSetStaticMesh, set);
    bool const returned = set.ReturnBool().value_or(false);
    ue3::Object* const shown = gComponentMesh.Read<ue3::Object*>(gState.component).value_or(nullptr);
    if (shown != mesh) {
        std::string const why = std::format("{}StaticMeshComponent.SetStaticMesh returned {} and the component shows {}, not {}", report, returned,
            ue3::runtime::FullName(shown), ue3::runtime::FullName(mesh));
        ClearAll("its mesh wouldn't take");
        return Refuse(why);
    }
    gState.mesh = mesh;
    std::string const bounds = TakeBounds();
    ApplyScale();
    bool const placed = gState.attached || !gSettings.follow || Place();
    ReapplyMaterial();
    report += std::format("shows {} ({}), scale {:.2f}, {} m ahead{}; mevr cockpit screen next", ue3::runtime::FullName(mesh), bounds, gSettings.scale,
        gSettings.distanceM, placed ? "" : " once a view exists");
    log::Info("cockpit probe: {}", report);
    gLastError = "none";
    return report;
}

// ---- Parts: a cockpit of several meshes in one frame ----------------------------------------------------------------

// The frame: where the head was at `anchor` (or the first `part`), its flat heading. Each part sits at an offset from it in
// metres (right, forward, up), turned by its yaw on top of the frame's, scaled in metres per package unit. A placement
// tool (tools/cockpit/place.py) writes these lines from the models' manifests: the cockpit's eye point on the head.
struct Anchor {
    bool set = false;
    void const* world = nullptr;
    engine::UnrealVector at{};
    double yaw = 0.0;  // radians, the head's flat forward
    double unitsPerMeter = engine::kUnitsPerMeter;
};
Anchor gAnchor;

struct Part {
    ue3::Object* actor = nullptr;
    ue3::Object* component = nullptr;
    ue3::Object* mesh = nullptr;
    engine::UnrealVector offsetM{};  // right, forward, up
    float yawDeg = 0.0f;
    float metersPerUnit = 0.01f;
};
std::vector<Part> gParts;

bool SetAnchor() {
    std::optional<Eye> const eye = HeadNow();
    if (!eye) {
        return false;
    }
    gAnchor = {true, engine::World(), eye->location, std::atan2(eye->forward.y, eye->forward.x), eye->unitsPerMeter};
    return true;
}

void PlacePart(Part const& part) {
    double const c = std::cos(gAnchor.yaw);
    double const s = std::sin(gAnchor.yaw);
    double const upm = gAnchor.unitsPerMeter;
    // Unreal: X forward, Y right, Z up; yaw turns X toward Y.
    ue3::Vector const location{static_cast<float>(gAnchor.at.x + (c * part.offsetM.y - s * part.offsetM.x) * upm),
        static_cast<float>(gAnchor.at.y + (s * part.offsetM.y + c * part.offsetM.x) * upm), static_cast<float>(gAnchor.at.z + part.offsetM.z * upm)};
    constexpr double kUnitsPerRadian = 65536.0 / (2.0 * 3.14159265358979323846);
    ue3::Rotator const rotation{0, static_cast<int>(std::lround(gAnchor.yaw * kUnitsPerRadian + part.yawDeg * 65536.0 / 360.0)), 0};
    if (ue3::Vector* const atField = gActorLocation.In<ue3::Vector>(part.actor)) {
        *atField = location;
        write_journal::Wrote("CockpitProbe.partLocation", atField, sizeof(*atField));
    }
    if (ue3::Rotator* const turnField = gActorRotation.In<ue3::Rotator>(part.actor)) {
        *turnField = rotation;
        write_journal::Wrote("CockpitProbe.partRotation", turnField, sizeof(*turnField));
    }
    ue3::Params scale = ParamsOf(gSetDrawScale);
    scale.Set("NewScale", static_cast<float>(part.metersPerUnit * upm));
    Call(part.actor, gSetDrawScale, scale);
    FlushActor(part.actor, false);
}

// Still() asks about the single mesh's world; the parts have the frame's.
bool PartAlive(Part const& part) {
    return part.actor != nullptr && gAnchor.world == engine::World() && ue3::runtime::IsAlive(part.actor) && ue3::IsA(part.actor, gActorClass);
}

void ClearParts(char const* why) {
    for (Part const& part : gParts) {
        if (PartAlive(part)) {
            DestroyActor(part.actor, why);
        }
    }
    gParts.clear();
    gAnchor = {};
}

// `part <Package.Object> <right m> <forward m> <up m> <yaw deg> <metres per package unit>`: loads the mesh if it isn't, spawns an
// actor of its own for it and places it in the frame.
std::string DoPart(std::istringstream& words) {
    std::string path;
    Part part;
    if (!(words >> path >> part.offsetM.x >> part.offsetM.y >> part.offsetM.z >> part.yawDeg >> part.metersPerUnit) || !(part.metersPerUnit > 0.0f)) {
        return "usage: mevr cockpit part <Package.Object> <right m> <forward m> <up m> <yaw deg> <metres per package unit>";
    }
    if (auto const missing = MissingFunction({&gSpawn, &gDestroy, &gSetDrawScale, &gForceUpdate, &gSetStaticMesh, &gSetCollision})) {
        return Refuse(*missing);
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        return Refuse("no player controller");
    }
    if (gAnchor.set && gAnchor.world != engine::World()) {
        gParts.clear();
        gAnchor = {};
    }
    if (!gAnchor.set && !SetAnchor()) {
        return Refuse("no head or camera to anchor the parts to");
    }
    ue3::Object* mesh = FindMesh(path);
    if (mesh == nullptr || Lower(R().PathName(mesh)) != Lower(path)) {
        mesh = LoadObject(path, gStaticMesh.Get());
    }
    if (!ue3::IsA(mesh, gStaticMesh)) {
        return Refuse(std::format("no StaticMesh {} (is the package installed and in the TOC?)", path));
    }
    ue3::Vector const at{static_cast<float>(gAnchor.at.x), static_cast<float>(gAnchor.at.y), static_cast<float>(gAnchor.at.z)};
    for (std::size_t i = 0; i < kAutomatic && part.actor == nullptr; ++i) {
        ActorKind const& kind = kKinds[i];
        ue3::Object* const actor = SpawnActor(controller, kind.cls->Get(), at);
        ue3::Object* const component = ue3::IsA(actor, *kind.cls) ? kind.component->Read<ue3::Object*>(actor).value_or(nullptr) : nullptr;
        if (!ue3::IsA(component, gStaticMeshComponent)) {
            if (actor != nullptr) {
                DestroyActor(actor, "no StaticMeshComponent");
            }
            continue;
        }
        MakeHarmless(kind, actor, component);
        part.actor = actor;
        part.component = component;
    }
    if (part.actor == nullptr) {
        return Refuse("no actor could be spawned for the part");
    }
    ue3::Params set = ParamsOf(gSetStaticMesh);
    set.Set("NewMesh", mesh);
    set.SetBool("bForce", true);
    Call(part.component, gSetStaticMesh, set);
    part.mesh = mesh;
    PlacePart(part);
    gParts.push_back(part);
    std::string const report = std::format("part {}: {} at right {:.3f} forward {:.3f} up {:.3f} m, yaw {:.1f}, {:.5f} m a unit (draw scale {:.3f}); {} part{}",
        gParts.size(), ue3::runtime::FullName(mesh), part.offsetM.x, part.offsetM.y, part.offsetM.z, part.yawDeg, part.metersPerUnit,
        part.metersPerUnit * gAnchor.unitsPerMeter, gParts.size(), gParts.size() == 1 ? "" : "s");
    log::Info("cockpit probe: {}", report);
    return report;
}

// Every actor of the spawnable kinds whose mesh comes from our package: parts a hot reload or a lost list left behind.
int SweepCockpitActors() {
    std::vector<ue3::Object*> found;
    for (std::size_t i = 0; i < kAutomatic; ++i) {
        ActorKind const& kind = kKinds[i];
        ForEachLive(kind.cls->Get(), [&](ue3::Object* actor) {
            ue3::Object* const component = kind.component->Read<ue3::Object*>(actor).value_or(nullptr);
            ue3::Object* const mesh = component != nullptr ? gComponentMesh.Read<ue3::Object*>(component).value_or(nullptr) : nullptr;
            if (mesh != nullptr && Lower(R().PathName(mesh)).starts_with("biog_mevr_cockpit.") && actor != gState.actor) {
                found.push_back(actor);
            }
        });
    }
    for (ue3::Object* const actor : found) {
        DestroyActor(actor, "mevr cockpit parts sweep");
    }
    gParts.clear();
    gAnchor = {};
    return static_cast<int>(found.size());
}

std::string DoParts(std::string const& what) {
    if (what == "sweep") {
        int const count = SweepCockpitActors();
        return std::format("{} actor{} showing our package's meshes destroyed", count, count == 1 ? "" : "s");
    }
    if (what == "clear") {
        std::size_t const count = gParts.size();
        ClearParts("mevr cockpit parts clear");
        return std::format("{} part{} removed", count, count == 1 ? "" : "s");
    }
    std::string out = gAnchor.set ? std::format("frame at {:.0f} {:.0f} {:.0f}, heading {:.1f} deg, {:.1f} units a metre\n", gAnchor.at.x, gAnchor.at.y, gAnchor.at.z,
                                        gAnchor.yaw * 180.0 / 3.14159265358979323846, gAnchor.unitsPerMeter)
                                  : std::string("no frame yet: the first part or `anchor` takes the head\n");
    for (std::size_t i = 0; i < gParts.size(); ++i) {
        Part const& p = gParts[i];
        out += std::format("  {} {}: right {:.3f} forward {:.3f} up {:.3f} m, yaw {:.1f}, {:.5f} m a unit\n", i + 1, ue3::runtime::FullName(p.mesh), p.offsetM.x, p.offsetM.y,
            p.offsetM.z, p.yawDeg, p.metersPerUnit);
    }
    return out;
}

// ---- Materials -----------------------------------------------------------------------------------------------------

struct MaterialRow {
    ue3::Object* object = nullptr;
    ue3::Object* base = nullptr;  // the Material at the end of the Parent chain
    std::vector<ue3::Name> parameters;
    std::optional<ue3::Name> emissive;  // the texture parameter wired straight into the base's emissive
    int lighting = -1;
    int blend = -1;
    bool staticPermutation = false;
    bool usedStatic = false;       // the base's bUsedWithStaticLighting
    bool usedEnvironment = false;  // the base's bUsedWithLightEnvironments
    int score = 0;
};

void AddName(std::vector<ue3::Name>& names, ue3::Name name) {
    if (std::find(names.begin(), names.end(), name) == names.end()) {
        names.push_back(name);
    }
}

// Its texture parameters by both routes: the instances' TextureParameterValues up the Parent chain, and the base's
// expressions (a cooked Material may have lost them [verify]; the instances' names then still answer).
MaterialRow Analyze(ue3::Object* object) {
    MaterialRow row;
    row.object = object;
    ue3::Object* current = object;
    for (int depth = 0; depth < 8 && current != nullptr; ++depth) {
        if (ue3::IsA(current, gMaterial)) {
            row.base = current;
            break;
        }
        if (ue3::IsA(current, gMic)) {
            if (auto const values = gTextureValues.ElementsOf(current)) {
                for (std::int32_t i = 0; i < values->count; ++i) {
                    if (auto const name = gTextureValueName.Read<ue3::Name>(values->At(i)); name && name->index != 0) {
                        AddName(row.parameters, *name);
                    }
                }
            }
        }
        if (!ue3::IsA(current, gMaterialInstance)) {
            break;
        }
        current = gParent.Read<ue3::Object*>(current).value_or(nullptr);
    }
    row.staticPermutation = ue3::IsA(object, gMaterialInstance) && gStaticPermutation.ReadBool(object).value_or(false);
    if (row.base != nullptr) {
        if (auto const expressions = gExpressions.ElementsOf(row.base)) {
            for (std::int32_t i = 0; i < expressions->count; ++i) {
                ue3::Object* const* const slot = static_cast<ue3::Object* const*>(expressions->At(i));
                ue3::Object* const expression = slot != nullptr ? *slot : nullptr;
                if (ue3::IsA(expression, gTextureParameterExpression)) {
                    if (auto const name = gExpressionParameterName.Read<ue3::Name>(expression); name && name->index != 0) {
                        AddName(row.parameters, *name);
                    }
                }
            }
        }
        if (void const* const emissive = gEmissive.Address(row.base)) {
            ue3::Object* const expression = gEmissiveExpression.Read<ue3::Object*>(emissive).value_or(nullptr);
            if (ue3::IsA(expression, gTextureParameterExpression)) {
                row.emissive = gExpressionParameterName.Read<ue3::Name>(expression);
            }
        }
        row.lighting = gLightingModel.Read<std::uint8_t>(row.base).value_or(255);
        row.blend = gBlendMode.Read<std::uint8_t>(row.base).value_or(255);
        row.usedStatic = gUsedStaticLighting.ReadBool(row.base).value_or(false);
        row.usedEnvironment = gUsedLightEnvironments.ReadBool(row.base).value_or(false);
    }
    if (row.parameters.empty()) {
        return row;
    }
    std::string const lower = Lower(R().PathName(object));
    row.score += row.emissive ? 100 : 0;
    row.score += row.lighting == kLightingUnlit ? 50 : 0;
    row.score += row.blend == kBlendOpaque ? 30 : row.blend == kBlendMasked ? 10 : -30;
    row.score += row.usedStatic ? 10 : 0;
    row.score += row.usedEnvironment || row.lighting == kLightingUnlit ? 10 : 0;
    row.score += row.staticPermutation ? -10 : 0;
    row.score += object == row.base ? 5 : 0;
    for (std::string_view const word : {"screen", "monitor", "display", "holo", "video", "tv_", "hud"}) {
        if (Contains(lower, word)) {
            row.score += 20;
            break;
        }
    }
    return row;
}

std::vector<MaterialRow> MaterialRows(std::string const& lowerFilter) {
    std::vector<MaterialRow> rows;
    auto const add = [&](ue3::Object* object) {
        if (object == gState.mic || (!lowerFilter.empty() && !Contains(Lower(R().PathName(object)), lowerFilter))) {
            return;
        }
        MaterialRow row = Analyze(object);
        if (!row.parameters.empty()) {
            rows.push_back(std::move(row));
        }
    };
    ForEachLive(gMaterial.Get(), add);
    ForEachLive(gMic.Get(), add);
    std::stable_sort(rows.begin(), rows.end(), [](MaterialRow const& a, MaterialRow const& b) { return a.score > b.score; });
    return rows;
}

std::string NamesText(std::vector<ue3::Name> const& names) {
    std::string out;
    for (ue3::Name const name : names) {
        out += (out.empty() ? "" : ",") + ue3::runtime::NameText(name);
    }
    return out;
}

std::string RowText(MaterialRow const& row) {
    return std::format("{:4} {} lighting {} blend {}{}{}{}{} textures: {}{}", row.score, R().PathName(row.object), row.lighting, row.blend,
        row.object != row.base ? " instance" : "", row.staticPermutation ? " static-permutation" : "", row.usedStatic ? " static-lighting" : "",
        row.usedEnvironment ? " light-environments" : "", NamesText(row.parameters),
        row.emissive ? std::format("; wired into emissive: {}", ue3::runtime::NameText(*row.emissive)) : std::string());
}

std::string ListMaterials(std::string const& filter) {
    if (auto const missing = MissingClass({&gMaterial, &gMic, &gMaterialInstance, &gTextureParameterExpression})) {
        return Refuse(*missing);
    }
    std::vector<MaterialRow> const rows = MaterialRows(Lower(filter));
    std::string out = std::format("{} loaded materials with a texture parameter{} (lighting 2 = unlit; blend 0 = opaque, 1 = masked, 2 = translucent, 3 = additive), "
                                  "the likeliest first:\n",
        rows.size(), filter.empty() ? std::string() : std::format(" matching '{}'", filter));
    for (std::size_t i = 0; i < rows.size() && i < kListed; ++i) {
        out += "  " + RowText(rows[i]) + "\n";
    }
    return out;
}

// The texture parameter to try: `param`'s if this material has it; else the one wired into emissive, one whose name says
// emissive or diffuse, anything that isn't plainly a normal or a mask.
std::optional<ue3::Name> PickParameter(MaterialRow const& row) {
    if (!gSettings.paramName.empty()) {
        for (ue3::Name const name : row.parameters) {
            if (Lower(ue3::runtime::NameText(name)) == Lower(gSettings.paramName)) {
                return name;
            }
        }
        return std::nullopt;
    }
    if (row.emissive) {
        return row.emissive;
    }
    for (std::string_view const word : {"emis", "glow", "screen", "video", "diff", "albedo", "tex", "color", "colour"}) {
        for (ue3::Name const name : row.parameters) {
            if (Contains(Lower(ue3::runtime::NameText(name)), word)) {
                return name;
            }
        }
    }
    for (ue3::Name const name : row.parameters) {
        std::string const lower = Lower(ue3::runtime::NameText(name));
        if (!Contains(lower, "norm") && !Contains(lower, "spec") && !Contains(lower, "mask") && !Contains(lower, "rough")) {
            return name;
        }
    }
    return row.parameters.empty() ? std::nullopt : std::optional<ue3::Name>(row.parameters.front());
}

// `material`'s, else the mesh's own first element if it has a texture parameter (it's known to draw on this mesh), else
// the best row of all.
std::optional<MaterialRow> PickParent(std::string& why) {
    if (!gSettings.materialName.empty()) {
        std::string const lower = Lower(gSettings.materialName);
        std::vector<MaterialRow> const rows = MaterialRows(lower);
        for (MaterialRow const& row : rows) {
            if (Lower(R().PathName(row.object)) == lower || Lower(R().NameText(row.object)) == lower) {
                why = "named";
                return row;
            }
        }
        if (!rows.empty()) {
            why = "named (a partial match)";
            return rows.front();
        }
        return std::nullopt;
    }
    ue3::Params get = ParamsOf(gGetMaterial);
    get.Set("ElementIndex", std::max(gSettings.element, 0));
    Call(gState.component, gGetMaterial, get);
    if (ue3::Object* const own = get.Return<ue3::Object*>().value_or(nullptr); own != nullptr && own != gState.mic) {
        MaterialRow row = Analyze(own);
        if (!row.parameters.empty()) {
            why = "the mesh's own";
            return row;
        }
    }
    std::vector<MaterialRow> const rows = MaterialRows({});
    if (rows.empty()) {
        return std::nullopt;
    }
    why = "the best scored";
    return rows.front();
}

// Our instance on the component: MeshComponent.CreateAndSetMaterialInstanceConstant is script doing what script's `new`
// does (nothing else makes one through ProcessEvent). It hands back an instance that already sits there if that lives in
// the map's package, so the element first gets the plain Material under it: a level's own instance is never changed.
bool EnsureMic(ue3::Object* fallbackBase, std::string& failure) {
    if (gState.mic != nullptr && ue3::runtime::IsAlive(gState.mic) && ue3::IsA(gState.mic, gMic)) {
        return true;
    }
    if (fallbackBase == nullptr) {
        failure = "the parent has no Material at the end of its chain";
        return false;
    }
    int const element = std::max(gSettings.element, 0);
    ue3::Params plain = ParamsOf(gSetMaterial);
    plain.Set("ElementIndex", element);
    plain.Set("Material", fallbackBase);
    Call(gState.component, gSetMaterial, plain);
    ue3::Params create = ParamsOf(gCreateMic);
    create.Set("ElementIndex", element);
    Call(gState.component, gCreateMic, create);
    ue3::Object* const mic = create.Return<ue3::Object*>().value_or(nullptr);
    if (!ue3::IsA(mic, gMic)) {
        failure = std::format("CreateAndSetMaterialInstanceConstant returned {}", ue3::runtime::FullName(mic));
        return false;
    }
    gState.mic = mic;
    log::Info("cockpit probe: made {}", ue3::runtime::FullName(mic));
    return true;
}

// Every element of the mesh shows the instance (a mesh of several parts is one picture this way, or the picture many times),
// or only `element`'s (a console whose screen is a material slot of its own).
int MicOnAllElements() {
    ue3::Params count = ParamsOf(gGetNumElements);
    Call(gState.component, gGetNumElements, count);
    int const elements = count.Return<std::int32_t>().value_or(1);
    if (gSettings.element >= 0 && gSettings.element < elements) {
        ue3::Params set = ParamsOf(gSetMaterial);
        set.Set("ElementIndex", gSettings.element);
        set.Set("Material", gState.mic);
        Call(gState.component, gSetMaterial, set);
        return 1;
    }
    for (int i = 0; i < elements && i < 64; ++i) {
        ue3::Params set = ParamsOf(gSetMaterial);
        set.Set("ElementIndex", i);
        set.Set("Material", gState.mic);
        Call(gState.component, gSetMaterial, set);
    }
    return elements;
}

std::string ApplyMaterial() {
    if (auto const missing = MissingFunction({&gGetMaterial, &gSetMaterial, &gGetNumElements, &gCreateMic, &gSetParent, &gSetTextureParameter})) {
        return Refuse(*missing);
    }
    if (auto const missing = MissingClass({&gMaterial, &gMic, &gMaterialInstance, &gTextureParameterExpression})) {
        return Refuse(*missing);
    }
    std::string how;
    std::optional<MaterialRow> const row = PickParent(how);
    if (!row) {
        return Refuse(gSettings.materialName.empty()
                ? std::string("no loaded material has a texture parameter (research/47: the host would have to draw it, or a package brings one)")
                : std::format("no loaded material with a texture parameter matches '{}' (mevr cockpit materials)", gSettings.materialName));
    }
    std::optional<ue3::Name> const parameter = PickParameter(*row);
    if (!parameter) {
        return Refuse(std::format("{} has no texture parameter '{}': it has {}", R().PathName(row->object), gSettings.paramName, NamesText(row->parameters)));
    }
    std::string failure;
    if (!EnsureMic(row->base, failure)) {
        return Refuse(failure);
    }
    ue3::Params parent = ParamsOf(gSetParent);
    parent.Set("NewParent", row->object);
    Call(gState.mic, gSetParent, parent);
    int const elements = MicOnAllElements();
    gState.parent = row->object;
    gState.parameter = *parameter;
    gState.parameterSet = true;
    ue3::Object* const shown = gState.shownTexture != nullptr ? gState.shownTexture : gState.target;
    ue3::Params value = ParamsOf(gSetTextureParameter);
    value.Set("ParameterName", *parameter);
    value.Set("Value", shown);
    Call(gState.mic, gSetTextureParameter, value);
    FlushActor(gState.actor, false);
    // Read back: the instance holds it. It says nothing about the parent using it: the snapshot does.
    ue3::Params read = ParamsOf(gGetTextureParameter);
    read.Set("ParameterName", *parameter);
    Call(gState.mic, gGetTextureParameter, read);
    bool const found = read.ReturnBool().value_or(false);
    ue3::Object* const held = read.Get<ue3::Object*>("OutValue").value_or(nullptr);
    std::string const report = std::format("the instance {} over {} ({}): {}; {} = {} on {} element{}; read back: {}, {}",
        ue3::runtime::FullName(gState.mic), R().PathName(row->object), how, RowText(*row), ue3::runtime::NameText(*parameter),
        shown == gState.target ? std::string("the render target") : ue3::runtime::FullName(shown), elements, elements == 1 ? "" : "s", found ? "found" : "not found",
        held == shown ? "it is what we set" : ue3::runtime::FullName(held));
    log::Info("cockpit probe: {}", report);
    gLastError = "none";
    return report;
}

void ReapplyMaterial() {
    if ((gState.target != nullptr || gState.shownTexture != nullptr) && gState.mic != nullptr) {
        MicOnAllElements();
    }
}

// ---- The screen ----------------------------------------------------------------------------------------------------

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

// A clear colour no scene has: the mesh in this colour means the material took the texture and no capture has landed yet.
ue3::Object* CreateTarget(int width, int height, bool floatTarget) {
    ue3::Class* const cls = gTargetClass.Get();
    ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
    if (defaults == nullptr) {
        return nullptr;
    }
    ue3::Params p = ParamsOf(gCreateTarget);
    std::uint8_t const format = static_cast<std::uint8_t>(floatTarget ? sc::kPixelFormatFloatRGB : sc::kPixelFormatA8R8G8B8);
    p.Set("InSizeX", static_cast<std::int32_t>(width));
    p.Set("InSizeY", static_cast<std::int32_t>(height));
    p.Set("InFormat", format);
    p.Set("InClearColor", LinearColor{0.5f, 0.0f, 0.5f, 1.0f});
    p.SetBool("bOnlyRenderOnce", false);
    Call(defaults, gCreateTarget, p);
    ue3::Object* const target = p.Return<ue3::Object*>().value_or(nullptr);
    return ue3::IsA(target, gTargetClass) ? target : nullptr;
}

template <class T>
void WriteField(ue3::FieldRef& field, ue3::Object* object, T value, char const* tag) {
    if (T* const p = field.In<T>(object)) {
        *p = value;
        write_journal::Wrote(tag, p);
    }
}

// Behind and above the pawn (or the Mako), looking at it: the stand-in for the third-person view.
void PointCapture() {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    auto const at = pawn != nullptr ? player::Location(pawn) : std::nullopt;
    auto const rotation = pawn != nullptr ? player::Rotation(pawn) : std::nullopt;
    if (!at || !rotation) {
        return;
    }
    engine::UnrealVector const forward = engine::ToBasis(engine::UnrealRotation{0, rotation->yaw, 0}).forward;
    double const back = static_cast<double>(gSettings.chaseBackM) * engine::kUnitsPerMeter;
    double const up = static_cast<double>(gSettings.chaseUpM) * engine::kUnitsPerMeter;
    engine::UnrealVector const from{at->x - forward.x * back, at->y - forward.y * back, at->z + up};
    engine::UnrealRotation const look = engine::ToRotation({at->x - from.x, at->y - from.y, at->z - from.z});
    gState.askedView = {static_cast<float>(from.x), static_cast<float>(from.y), static_cast<float>(from.z)};
    ue3::Params view = ParamsOf(gCaptureSetView);
    view.Set("NewLocation", gState.askedView);
    view.Set("NewRotation", ue3::Rotator{look.pitch, look.yaw, look.roll});
    // The update makes the engine's new probe, which carries the view to the render thread (the probe holds a copy). A new
    // probe has no last render time, so it renders at its next chance whatever FrameRate says: how often this runs is what
    // sets the rate of a moving capture (research/47).
    if (Call(gState.captureComponent, gCaptureSetView, view) && FlushActor(gState.captureActor, true)) {
        ++gState.pointedTicks;
    }
    gState.lastPointed = Now();
}

// The capture's post chain, or none (`post 0`): the picture then reaches the eye through the eye's own post only.
void ApplyPost(ue3::Object* controller) {
    ue3::Object* const chain = gSettings.postProcess ? PlayerPostProcessChain(controller) : nullptr;
    gEnablePostProcess.WriteBool(gState.captureComponent, chain != nullptr);
    gUseMainPostProcess.WriteBool(gState.captureComponent, chain != nullptr);
    WriteField(gPostProcess, gState.captureComponent, chain, "CockpitProbe.PostProcess");
}

std::string DoScreen(std::istringstream& words) {
    if (auto const missing = MissingFunction({&gCreateTarget, &gCaptureSetEnabled, &gCaptureSetFrameRate, &gCaptureSetView, &gCaptureSetParameters, &gForceUpdate})) {
        return Refuse(*missing);
    }
    if (auto const missing = MissingClass({&gCaptureActor, &gCaptureComponent, &gTargetClass})) {
        return Refuse(*missing);
    }
    int width = 512;
    int height = 288;
    bool floatTarget = true;
    bool linear = false;
    for (std::string word; words >> word;) {
        if (word == "rgba8") {
            floatTarget = false;
        } else if (word == "float") {
            floatTarget = true;
        } else if (word == "linear") {
            linear = true;
        } else {
            std::istringstream size(word);
            int w = 0;
            int h = 0;
            if (size >> w && words >> h && w >= 16 && h >= 16 && w <= 2048 && h <= 2048) {
                width = w;
                height = h;
            } else {
                return "usage: mevr cockpit screen [W H] [float|rgba8] [linear]";
            }
        }
    }
    std::string report;
    if (gState.actor == nullptr) {
        report = DoMesh({}) + "\n";
        if (gState.actor == nullptr) {
            return report;
        }
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        return Refuse("no player controller");
    }
    if (gState.target == nullptr || gState.targetWidth != width || gState.targetHeight != height || gState.targetFloat != floatTarget) {
        ue3::Object* const target = CreateTarget(width, height, floatTarget);
        if (target == nullptr) {
            return Refuse(report + "TextureRenderTarget2D.Create returned no render target: nothing changed");
        }
        gState.target = target;
        gState.targetWidth = width;
        gState.targetHeight = height;
        gState.targetFloat = floatTarget;
        log::Info("cockpit probe: render target {} ({}x{} {})", ue3::runtime::FullName(target), gSizeX.Read<std::int32_t>(target).value_or(-1),
            gSizeY.Read<std::int32_t>(target).value_or(-1), floatTarget ? "FloatRGB" : "A8R8G8B8");
    }
    gForceLinearGamma.WriteBool(gState.target, linear);
    gState.shownTexture = nullptr;
    if (gState.captureActor == nullptr) {
        ue3::Object* const actor = SpawnActor(controller, gCaptureActor.Get(), player::Location(controller).value_or(ue3::Vector{}));
        if (!ue3::IsA(actor, gCaptureActor)) {
            return Refuse(report + "Actor.Spawn returned no SceneCapture2DActor");
        }
        ue3::Object* const component = gSceneCapture.Read<ue3::Object*>(actor).value_or(nullptr);
        if (!ue3::IsA(component, gCaptureComponent)) {
            std::string const why = std::format("{} has no SceneCapture2DComponent", ue3::runtime::FullName(actor));
            DestroyActor(actor, "no capture component");
            return Refuse(report + why);
        }
        gState.captureActor = actor;
        gState.captureComponent = component;
        // As the scope's capture: nothing skips it; the screen's own mesh is its texture's "user", and may stand occluded.
        gSkipUsersOccluded.WriteBool(component, false);
        gSkipOwnerOccluded.WriteBool(component, false);
        WriteField(gMaxUpdateDist, component, kMaxUpdateDist, "CockpitProbe.MaxUpdateDist");
        gUpdateMatrices.WriteBool(component, false);  // or each component update puts the view back on the actor (research/47)
        ApplyPost(controller);
        log::Info("cockpit probe: capture {} with {}; post process {}", ue3::runtime::FullName(actor), ue3::runtime::FullName(component), gSettings.postProcess);
    }
    ue3::Params parameters = ParamsOf(gCaptureSetParameters);
    parameters.Set("NewTextureTarget", gState.target);
    parameters.Set("NewFOV", 70.0f);
    parameters.Set("NewNearPlane", 20.0f);
    parameters.Set("NewFarPlane", 0.0f);
    Call(gState.captureComponent, gCaptureSetParameters, parameters);
    ue3::Params rate = ParamsOf(gCaptureSetFrameRate);
    rate.Set("NewFrameRate", gSettings.captureRate);
    Call(gState.captureComponent, gCaptureSetFrameRate, rate);
    WriteField(gFrameRate, gState.captureComponent, gSettings.captureRate, "CockpitProbe.FrameRate");
    if (!gCaptureEnabled.ReadBool(gState.captureComponent).value_or(false)) {
        ue3::Params on = ParamsOf(gCaptureSetEnabled);
        on.SetBool("bEnable", true);
        Call(gState.captureComponent, gCaptureSetEnabled, on);
    }
    PointCapture();
    FlushActor(gState.captureActor, false);  // the re-attach too (research/39 section 5)
    report += std::format("capture {} into a {}x{} {} target at {:.0f} renders a second at most\n", ue3::runtime::FullName(gState.captureActor), width, height,
        floatTarget ? "FloatRGB" : "A8R8G8B8", gSettings.captureRate);
    return report + ApplyMaterial();
}

// ---- The rest of the verbs -----------------------------------------------------------------------------------------

std::string DoAttach() {
    if (auto const missing = MissingFunction({&gSetBase, &gSetHardAttach})) {
        return Refuse(*missing);
    }
    if (gState.actor == nullptr) {
        return Refuse("nothing to attach: mevr cockpit mesh first");
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (pawn == nullptr) {
        return Refuse("no pawn to attach to");
    }
    if (!gState.attached) {
        Place();  // where it is relative to the pawn now is where it stays
    }
    ue3::Params hard = ParamsOf(gSetHardAttach);
    hard.SetBool("bNewHardAttach", true);
    Call(gState.actor, gSetHardAttach, hard);
    ue3::Params base = ParamsOf(gSetBase);
    base.Set("NewBase", pawn);
    base.Set("NewFloor", ue3::Vector{0.0f, 0.0f, 1.0f});
    Call(gState.actor, gSetBase, base);
    ue3::Object* const now = gBase.Read<ue3::Object*>(gState.actor).value_or(nullptr);
    gState.attached = now == pawn;
    std::string const report = std::format("SetHardAttach + SetBase on {}: its Base is now {}{}", ue3::runtime::FullName(pawn), ue3::runtime::FullName(now),
        gState.attached ? "; no longer moved by the tick" : "; still moved by the tick");
    log::Info("cockpit probe: {}", report);
    return report;
}

std::string DoDetach() {
    if (gState.actor == nullptr) {
        return "nothing is up";
    }
    ue3::Params base = ParamsOf(gSetBase);
    base.Set("NewFloor", ue3::Vector{0.0f, 0.0f, 1.0f});
    Call(gState.actor, gSetBase, base);
    ue3::Params hard = ParamsOf(gSetHardAttach);
    hard.SetBool("bNewHardAttach", false);
    Call(gState.actor, gSetHardAttach, hard);
    gState.attached = false;
    return std::format("its Base is now {}; the tick moves it again", ue3::runtime::FullName(gBase.Read<ue3::Object*>(gState.actor).value_or(nullptr)));
}

std::string Classes() {
    std::string out = "the actor classes (the first three are tried in order; spawnclass <name> takes one):\n";
    for (ActorKind const& kind : kKinds) {
        ue3::Class* const cls = kind.cls->Get();
        ue3::Object* const defaults = cls != nullptr ? DefaultObject(cls) : nullptr;
        out += std::format("  {}: {}\n", kind.label, defaults != nullptr ? DefaultsText(defaults) : std::string("not found"));
    }
    return out;
}

std::string StatusLine() {
    char const* const mode = gState.actor == nullptr ? "-" : gState.attached ? "attached" : gSettings.follow ? "following the head" : "still";
    std::string target = "no render target";
    if (gState.target != nullptr) {
        target = std::format("{}x{} {}", gSizeX.Read<std::int32_t>(gState.target).value_or(-1), gSizeY.Read<std::int32_t>(gState.target).value_or(-1),
            gState.targetFloat ? "float" : "rgba8");
    }
    return std::format("actor {} ({}, {}), mesh {}, scale {:.2f}, {:.2f} m, our override instance {} over {} (None: the mesh's own material, see `info`), parameter {}, {}, capture {} at {:.0f}/s, "
                       "ticks placed {} pointed {} (renders: the levers line's `captures`), last error: {}",
        ue3::runtime::FullName(gState.actor), gState.kind[0] != '\0' ? gState.kind : "-", mode, ue3::runtime::FullName(gState.mesh), gSettings.scale, gSettings.distanceM,
        ue3::runtime::FullName(gState.mic), ue3::runtime::FullName(gState.parent), gState.parameterSet ? ue3::runtime::NameText(gState.parameter) : std::string("-"), target,
        ue3::runtime::FullName(gState.captureActor), gSettings.captureRate, gState.placedTicks, gState.pointedTicks, gLastError);
}

std::string Rest(std::istringstream& words) {
    std::string rest;
    std::getline(words, rest);
    std::size_t const start = rest.find_first_not_of(" \t");
    return start == std::string::npos ? std::string() : rest.substr(start);
}

constexpr char const* kUsage =
    "mevr cockpit: load <Package.Object> [class] | mesh [name or Package.Object] | part <Package.Object> RIGHT FORWARD UP YAW M_PER_UNIT | parts [clear|sweep] | anchor | list [filter] | classes | spawnclass [name] | screen [W H] [float|rgba8] [linear] | materials [filter] | material [name] | param [name] | "
    "texture [Package.Object] | info | element N|all | scale S | dist M | rot P Y R | follow 0|1 | attach | detach | chase BACK UP | rate N | aim 0|1 | post 0|1 | status | clear";

// ---- `texture`, `info`: separating a bad texture from a bad instance from a bad mesh (research/47) -------------------

// `texture <Package.Object>` shows one texture on a new instance of ours over the plain emissive material, in place of the
// render target; no name: back to the render target. So: our texture on our instance over the game's parent, against the
// package's own instance, on any mesh.
std::string DoTexture(std::string const& path) {
    if (gState.actor == nullptr) {
        return Refuse("nothing to put it on: mevr cockpit mesh first");
    }
    if (path.empty()) {
        gState.shownTexture = nullptr;
        return gState.target != nullptr ? ApplyMaterial() : std::string("back to the render target; there isn't one yet: mevr cockpit screen");
    }
    if (auto const missing = MissingClass({&gTexture})) {
        return Refuse(*missing);
    }
    ue3::Object* const loaded = LoadObject(path, gTexture.Get());
    if (!ue3::IsA(loaded, gTexture)) {
        return Refuse(std::format("DynamicLoadObject returned None for {} as a Texture", path));
    }
    gState.shownTexture = loaded;
    if (gSettings.materialName.empty() && gSettings.paramName.empty()) {
        gSettings.materialName = "EngineMaterials.EmissiveTexturedMaterial";
        gSettings.paramName = "Texture";
    }
    return ApplyMaterial();
}

// A mip's bulk data, by the offsets UTexture2D::Serialize and its bulk data code use (research/47). A read that faults says so.
struct MipBulk {
    std::uint32_t flags = 0;
    std::int32_t elements = 0;
    std::int32_t sizeOnDisk = 0;
    std::int32_t offsetInFile = 0;
    bool holdsData = false;
    bool attached = false;  // to the linker's archive: lazily loaded, and then OffsetInFile is what gets read
    std::int32_t sizeX = 0;
    std::int32_t sizeY = 0;
};

bool ReadMipBulk(void const* const* mipsData, std::int32_t index, MipBulk* out) {
    __try {
        auto const* const mip = static_cast<std::uint8_t const*>(mipsData[index]);
        if (mip == nullptr) {
            return false;
        }
        out->flags = *reinterpret_cast<std::uint32_t const*>(mip + 0x08);
        out->elements = *reinterpret_cast<std::int32_t const*>(mip + 0x0C);
        out->offsetInFile = *reinterpret_cast<std::int32_t const*>(mip + 0x10);
        out->sizeOnDisk = *reinterpret_cast<std::int32_t const*>(mip + 0x14);
        out->holdsData = *reinterpret_cast<void const* const*>(mip + 0x28) != nullptr;
        std::uint64_t archive = 0;
        std::memcpy(&archive, mip + 0x34, sizeof(archive));  // unaligned
        out->attached = archive != 0;
        out->sizeX = *reinterpret_cast<std::int32_t const*>(mip + 0x44);
        out->sizeY = *reinterpret_cast<std::int32_t const*>(mip + 0x48);
        return true;
    } __except (1) {
        return false;
    }
}

std::string TextureText(ue3::Object* texture) {
    if (texture == nullptr) {
        return "None";
    }
    std::string out = std::format("{}: Resource {}, SRGB {}, NeverStream {}, LODGroup {}", ue3::runtime::FullName(texture),
        gTextureResource.Read<void*>(texture).value_or(nullptr) != nullptr ? "set" : "NULL", gTextureSrgb.ReadBool(texture).value_or(false) ? 1 : 0,
        gTextureNeverStream.ReadBool(texture).value_or(false) ? 1 : 0, static_cast<int>(gTextureLodGroup.Read<std::uint8_t>(texture).value_or(255)));
    if (ue3::IsA(texture, gTargetClass)) {
        out += std::format(", render target {}x{} format {} bForceLinearGamma {}", gSizeX.Read<std::int32_t>(texture).value_or(-1), gSizeY.Read<std::int32_t>(texture).value_or(-1),
            static_cast<int>(gTargetFormat.Read<std::uint8_t>(texture).value_or(255)), gForceLinearGamma.ReadBool(texture).value_or(false) ? 1 : 0);
    }
    if (!ue3::IsA(texture, gTexture2D)) {
        return out;
    }
    out += std::format(", {}x{} format {}, RequestedMips {}, ResidentMips {}, MipTailBaseIdx {}", gTexture2DSizeX.Read<std::int32_t>(texture).value_or(-1),
        gTexture2DSizeY.Read<std::int32_t>(texture).value_or(-1), static_cast<int>(gTexture2DFormat.Read<std::uint8_t>(texture).value_or(255)),
        gTexture2DRequestedMips.Read<std::int32_t>(texture).value_or(-1), gTexture2DResidentMips.Read<std::int32_t>(texture).value_or(-1),
        gTexture2DMipTail.Read<std::int32_t>(texture).value_or(-1));
    // Mips is an indirect array: { pointers, count, max }.
    if (auto const* const mips = static_cast<std::uint8_t const*>(gTexture2DMips.Address(texture))) {
        void const* const* const data = *reinterpret_cast<void const* const* const*>(mips);
        std::int32_t const count = *reinterpret_cast<std::int32_t const*>(mips + 8);
        out += std::format(", {} mips", count);
        for (std::int32_t i = 0; data != nullptr && i < count && i < 16; i += (i == 0 ? std::max(count - 1, 1) : count)) {  // the first and the last
            MipBulk bulk;
            out += ReadMipBulk(data, i, &bulk) ? std::format("; mip {}: {}x{}, bulk flags 0x{:X}, {} bytes, on disk {} at {}, data {}, {}", i, bulk.sizeX, bulk.sizeY, bulk.flags, bulk.elements,
                                                     bulk.sizeOnDisk, bulk.offsetInFile, bulk.holdsData ? "held" : "handed to the resource or never read",
                                                     bulk.attached ? "attached to the linker (lazy)" : "not attached (read inline)")
                                               : std::format("; mip {}: unreadable", i);
        }
    }
    return out;
}

// What the element draws: the chain up to the Material, every texture parameter on the way, each texture's state.
std::string MaterialChainText(ue3::Object* material) {
    std::string out;
    ue3::Object* current = material;
    for (int depth = 0; depth < 8 && current != nullptr; ++depth) {
        out += std::format("\n      {}", ue3::runtime::FullName(current));
        if (ue3::IsA(current, gMaterial)) {
            out += std::format(" lighting {} blend {} static-lighting {} light-environments {}", static_cast<int>(gLightingModel.Read<std::uint8_t>(current).value_or(255)),
                static_cast<int>(gBlendMode.Read<std::uint8_t>(current).value_or(255)), gUsedStaticLighting.ReadBool(current).value_or(false) ? 1 : 0,
                gUsedLightEnvironments.ReadBool(current).value_or(false) ? 1 : 0);
            MaterialRow const row = Analyze(current);
            out += std::format(" textures: {}{}", NamesText(row.parameters), row.emissive ? std::format("; wired into emissive: {}", ue3::runtime::NameText(*row.emissive)) : std::string());
            return out;
        }
        if (ue3::IsA(current, gMic)) {
            if (auto const values = gTextureValues.ElementsOf(current)) {
                for (std::int32_t i = 0; i < values->count; ++i) {
                    auto const name = gTextureValueName.Read<ue3::Name>(values->At(i));
                    ue3::Object* const value = gTextureValueObject.Read<ue3::Object*>(values->At(i)).value_or(nullptr);
                    out += std::format("\n        {} = {}", name ? ue3::runtime::NameText(*name) : std::string("?"), TextureText(value));
                }
            } else {
                out += "\n        (no TextureParameterValues)";
            }
        }
        if (!ue3::IsA(current, gMaterialInstance)) {
            return out;
        }
        current = gParent.Read<ue3::Object*>(current).value_or(nullptr);
        if (current == nullptr) {
            out += "\n      its Parent is None: the instance's parent didn't resolve";
        }
    }
    return out;
}

// Where a world-to-view matrix looks from: p * M = 0, M's upper 3x3 a rotation.
ue3::Vector ViewLocation(float const* m) {
    return {-(m[12] * m[0] + m[13] * m[1] + m[14] * m[2]), -(m[12] * m[4] + m[13] * m[5] + m[14] * m[6]), -(m[12] * m[8] + m[13] * m[9] + m[14] * m[10])};
}

// `info`: the live state of the mesh's material and of the capture, read on the game thread.
std::string Info() {
    std::string out;
    if (gState.actor == nullptr) {
        out = "no mesh up\n";
    } else {
        ue3::Params count = ParamsOf(gGetNumElements);
        Call(gState.component, gGetNumElements, count);
        int const elements = count.Return<std::int32_t>().value_or(0);
        auto const overrides = gMeshMaterials.ElementsOf(gState.component);
        ue3::Object* const environment = gPrimitiveLightEnvironment.Read<ue3::Object*>(gState.component).value_or(nullptr);
        out += std::format("mesh {} on {} ({}): {} element{}, {} component override{}, light environment {}{}\n", ue3::runtime::FullName(gState.mesh), ue3::runtime::FullName(gState.component),
            gState.kind, elements, elements == 1 ? "" : "s", overrides ? overrides->count : 0, overrides && overrides->count == 1 ? "" : "s", ue3::runtime::FullName(environment),
            environment != nullptr ? std::format(" enabled {}", gLightEnvironmentEnabled.ReadBool(environment).value_or(false) ? 1 : 0) : std::string());
        for (int i = 0; i < elements && i < 8; ++i) {
            ue3::Params get = ParamsOf(gGetMaterial);
            get.Set("ElementIndex", i);
            Call(gState.component, gGetMaterial, get);
            ue3::Object* const material = get.Return<ue3::Object*>().value_or(nullptr);
            out += std::format("  element {}: {}", i, material != nullptr ? MaterialChainText(material) : std::string("None: GetMaterial returned nothing"));
            // The engine's own answer for each name the chain knows.
            if (material != nullptr) {
                for (ue3::Name const name : Analyze(material).parameters) {
                    ue3::Params read = ParamsOf(gGetTextureParameter);
                    read.Set("ParameterName", name);
                    Call(material, gGetTextureParameter, read);
                    out += std::format("\n      GetTextureParameterValue({}): {}, {}", ue3::runtime::NameText(name), read.ReturnBool().value_or(false) ? "found" : "not found",
                        ue3::runtime::FullName(read.Get<ue3::Object*>("OutValue").value_or(nullptr)));
                }
            }
            out += "\n";
        }
    }
    if (gState.captureComponent != nullptr) {
        out += std::format("capture {}: bUpdateMatrices {}, enabled {}, FrameRate {:.1f}, post process {}, aim {}, pointed {} times\n  target {}\n", ue3::runtime::FullName(gState.captureComponent),
            gUpdateMatrices.ReadBool(gState.captureComponent).value_or(true) ? 1 : 0, gCaptureEnabled.ReadBool(gState.captureComponent).value_or(false) ? 1 : 0,
            gFrameRate.Read<float>(gState.captureComponent).value_or(-1.0f), gEnablePostProcess.ReadBool(gState.captureComponent).value_or(false) ? 1 : 0, gSettings.aim ? 1 : 0,
            gState.pointedTicks, TextureText(gState.target));
        if (void const* const matrix = gViewMatrix.Address(gState.captureComponent)) {
            ue3::Vector const from = ViewLocation(static_cast<float const*>(matrix));
            out += std::format("  ViewMatrix looks from {:.0f} {:.0f} {:.0f}; last asked {:.0f} {:.0f} {:.0f}\n", from.x, from.y, from.z, gState.askedView.x, gState.askedView.y, gState.askedView.z);
        }
    }
    log::Info("cockpit probe: info:\n{}", out);
    return out;
}

// ---- `mevr vehicle` -------------------------------------------------------------------------------------------------

float DistanceSquared(ue3::Vector const& a, ue3::Vector const& b) {
    return (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z);
}

// FindClosestDrivableVehicle is a native of SFXSaveGame: asked of a live one only (what it reads of its object isn't known,
// so never of a default object). Else, or if it answers None: the nearest live BioVehicleBase, whatever state that one is in.
struct VehicleFind {
    ue3::Object* engineAnswer = nullptr;
    bool engineAsked = false;
    ue3::Object* nearest = nullptr;
    float nearestM = 0.0f;
};

VehicleFind FindVehicle(ue3::Object* pawn) {
    VehicleFind find;
    if (pawn == nullptr) {
        return find;
    }
    ue3::Object* save = nullptr;
    ForEachLive(gSaveGame.Get(), [&](ue3::Object* object) {
        save = save != nullptr ? save : object;
    });
    if (save != nullptr && gFindClosestVehicle.Get() != nullptr && ue3::IsA(pawn, gBioPawn)) {
        ue3::Params p = ParamsOf(gFindClosestVehicle);
        p.Set("Pawn", pawn);
        find.engineAsked = Call(save, gFindClosestVehicle, p);
        find.engineAnswer = p.Return<ue3::Object*>().value_or(nullptr);
    }
    ue3::Vector const at = player::Location(pawn).value_or(ue3::Vector{});
    float best = 0.0f;
    ForEachLive(gVehicleBase.Get(), [&](ue3::Object* vehicle) {
        auto const there = player::Location(vehicle);
        if (there) {
            float const distance = DistanceSquared(at, *there);
            if (find.nearest == nullptr || distance < best) {
                find.nearest = vehicle;
                best = distance;
            }
        }
    });
    find.nearestM = std::sqrt(best) / static_cast<float>(engine::kUnitsPerMeter);
    return find;
}

// The top of WorldInfo's path is the persistent map's package.
std::string MapName(ue3::Object* controller) {
    ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
    if (world == nullptr) {
        return "(no WorldInfo)";
    }
    std::string const path = R().PathName(world);
    return path.substr(0, path.find('.'));
}

} // namespace

std::string VehicleCommand(std::string const& line) {
    if (!ue3::runtime::Ready()) {
        return "mevr vehicle: our reflection layer is off (see the 'reflect:' lines)";
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        return "mevr vehicle: no player controller";
    }
    ue3::Object* const pawn = player::Pawn(controller);
    std::istringstream words(line);
    std::string verb;
    words >> verb;
    VehicleFind const find = FindVehicle(pawn);
    ue3::Object* const target = find.engineAnswer != nullptr ? find.engineAnswer : find.nearest;
    if (verb == "enter") {
        if (auto const missing = MissingFunction({&gEnterVehicle})) {
            return Refuse(*missing);
        }
        if (!ue3::IsA(controller, gBioController)) {
            return Refuse(std::format("{} isn't a BioPlayerController", ue3::runtime::FullName(controller)));
        }
        if (target == nullptr) {
            return Refuse(std::format("no vehicle in {}", MapName(controller)));
        }
        ue3::Params p = ParamsOf(gEnterVehicle);
        p.Set("oVehicle", target);
        bool const called = Call(controller, gEnterVehicle, p);
        std::string const report = std::format("BioEnterVehicle({}): called {}, returned {}; `mevr vehicle` says what it did", ue3::runtime::FullName(target), called,
            p.ReturnBool().value_or(false));
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    auto const mode = player::GameMode(controller);
    return std::format("map {}, game mode {}, pawn {} (BioVehicleBase: {}), closest drivable {} ({}), nearest vehicle {} at {:.1f} m", MapName(controller), mode ? std::to_string(*mode) : std::string("?"),
        ue3::runtime::FullName(pawn), pawn != nullptr && ue3::IsA(pawn, gVehicleBase) ? "yes" : "no", ue3::runtime::FullName(find.engineAnswer),
        find.engineAsked ? "FindClosestDrivableVehicle's answer" : "not asked: no live SFXSaveGame or not on foot", ue3::runtime::FullName(find.nearest), find.nearestM);
}

std::string Command(std::string const& line) {
    if (!ue3::runtime::Ready()) {
        return "cockpit probe: our reflection layer is off (see the 'reflect:' lines)";
    }
    Refresh();
    std::istringstream words(line);
    std::string verb;
    words >> verb;
    if (verb == "load") {
        return DoLoad(words);
    }
    if (verb == "mesh") {
        return DoMesh(Rest(words));
    }
    if (verb == "part") {
        return DoPart(words);
    }
    if (verb == "parts") {
        return DoParts(Rest(words));
    }
    if (verb == "anchor") {
        if (!SetAnchor()) {
            return Refuse("no head or camera to anchor the parts to");
        }
        for (Part const& part : gParts) {
            if (PartAlive(part)) {
                PlacePart(part);
            }
        }
        return DoParts({});
    }
    if (verb == "list") {
        return ListMeshes(Rest(words));
    }
    if (verb == "classes") {
        return Classes();
    }
    if (verb == "spawnclass") {
        gSettings.spawnClass = Rest(words);
        return gSettings.spawnClass.empty() ? std::string("the automatic order") : std::format("the next spawn tries only {}", gSettings.spawnClass);
    }
    if (verb == "screen") {
        return DoScreen(words);
    }
    if (verb == "materials") {
        return ListMaterials(Rest(words));
    }
    if (verb == "texture") {
        return DoTexture(Rest(words));
    }
    if (verb == "info") {
        return Info();
    }
    if (verb == "material" || verb == "param") {
        (verb == "material" ? gSettings.materialName : gSettings.paramName) = Rest(words);
        if (gState.target == nullptr && gState.shownTexture == nullptr) {
            return std::format("{} set; it applies on mevr cockpit screen", verb);
        }
        return ApplyMaterial();
    }
    if (verb == "element") {
        std::string which;
        words >> which;
        int index = -1;
        if (which != "all" && (std::from_chars(which.data(), which.data() + which.size(), index).ec != std::errc{} || index < 0)) {
            return "usage: mevr cockpit element <material slot>|all (before screen or texture; clear to start over)";
        }
        gSettings.element = which == "all" ? -1 : index;
        return std::format("our instance goes on {}; it applies on the next screen or texture", gSettings.element < 0 ? std::string("every slot") : std::format("slot {} only", gSettings.element));
    }
    if (verb == "scale" || verb == "dist") {
        float value = 0.0f;
        if (!(words >> value) || !(value > 0.0f) || value > 1000.0f) {
            return std::format("usage: mevr cockpit {} <positive number>", verb);
        }
        (verb == "scale" ? gSettings.scale : gSettings.distanceM) = value;
        if (gState.actor != nullptr && verb == "scale") {
            ApplyScale();
        }
        return StatusLine();
    }
    if (verb == "rot") {
        float pitch = 0.0f;
        float yaw = 0.0f;
        float roll = 0.0f;
        if (!(words >> pitch >> yaw >> roll)) {
            return "usage: mevr cockpit rot <pitch> <yaw> <roll> (degrees, added to facing the head)";
        }
        constexpr float kUnitsPerDegree = 65536.0f / 360.0f;
        gSettings.rotation = {static_cast<int>(pitch * kUnitsPerDegree), static_cast<int>(yaw * kUnitsPerDegree), static_cast<int>(roll * kUnitsPerDegree)};
        gSettings.userRotation = true;
        return StatusLine();
    }
    if (verb == "follow") {
        int on = 1;
        words >> on;
        gSettings.follow = on != 0;
        return StatusLine();
    }
    if (verb == "attach") {
        return DoAttach();
    }
    if (verb == "detach") {
        return DoDetach();
    }
    if (verb == "chase") {
        float back = 0.0f;
        float up = 0.0f;
        if (!(words >> back >> up)) {
            return "usage: mevr cockpit chase <metres behind> <metres above>";
        }
        gSettings.chaseBackM = back;
        gSettings.chaseUpM = up;
        if (gState.captureActor != nullptr) {
            PointCapture();  // even under `aim 0`: one new view
        }
        return StatusLine();
    }
    if (verb == "aim") {
        int on = 1;
        words >> on;
        gSettings.aim = on != 0;
        return std::format("{}: the capture's probe {}", gSettings.aim ? "re-pointed at `rate` a second" : "left where it is",
            gSettings.aim ? "is rebuilt each time, so each one renders" : "lives on, so FrameRate throttles it (the levers line's captures against `rate`)");
    }
    if (verb == "post") {
        int on = 1;
        words >> on;
        gSettings.postProcess = on != 0;
        if (gState.captureComponent != nullptr) {
            ApplyPost(player::Controller());
            FlushActor(gState.captureActor, false);
        }
        return std::format("the capture's post chain {}", gSettings.postProcess ? "on" : "off");
    }
    if (verb == "rate") {
        float rate = 0.0f;
        if (!(words >> rate) || !(rate > 0.0f)) {
            return "usage: mevr cockpit rate <renders a second at most>";
        }
        gSettings.captureRate = rate;
        if (gState.captureComponent != nullptr) {
            ue3::Params p = ParamsOf(gCaptureSetFrameRate);
            p.Set("NewFrameRate", rate);
            Call(gState.captureComponent, gCaptureSetFrameRate, p);
        }
        return StatusLine();
    }
    if (verb == "status") {
        return StatusLine();
    }
    if (verb == "clear") {
        ClearAll("mevr cockpit clear");
        ClearParts("mevr cockpit clear");
        return "cleared (the render target and the instance go with the garbage)";
    }
    return kUsage;
}

void Tick() {
    if (gState.actor == nullptr && gState.captureActor == nullptr) {
        return;
    }
    Refresh();
    if (gState.actor != nullptr && gSettings.follow && !gState.attached && Place()) {
        ++gState.placedTicks;
    }
    // Not every tick unless the rate asks: each SetView makes the engine build a new probe, with a new view state.
    if (gState.captureActor != nullptr && gSettings.aim && gSettings.captureRate > 0.0f && Now() - gState.lastPointed >= 1.0 / static_cast<double>(gSettings.captureRate)) {
        PointCapture();
    }
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"up\": {}, \"actor\": {}, \"kind\": {}, \"mesh\": {}, \"mic\": {}, \"parent\": {}, \"parameter\": {}, \"size\": [{}, {}], \"capture\": {}, "
                       "\"attached\": {}, \"placedTicks\": {}, \"pointedTicks\": {}, \"lastError\": {}}}",
        gState.actor != nullptr || gState.captureActor != nullptr ? "true" : "false", json::JsonString(ue3::runtime::FullName(gState.actor)), json::JsonString(gState.kind),
        json::JsonString(ue3::runtime::FullName(gState.mesh)), json::JsonString(ue3::runtime::FullName(gState.mic)), json::JsonString(ue3::runtime::FullName(gState.parent)),
        json::JsonString(gState.parameterSet ? ue3::runtime::NameText(gState.parameter) : std::string()), gState.target != nullptr ? gState.targetWidth : 0,
        gState.target != nullptr ? gState.targetHeight : 0, json::JsonString(ue3::runtime::FullName(gState.captureActor)), gState.attached ? "true" : "false",
        gState.placedTicks, gState.pointedTicks, json::JsonString(gLastError));
}

void LogStats() {
    if (gState.actor != nullptr || gState.captureActor != nullptr) {
        log::Info("    cockpit probe: {}", StatusLine());
    }
}

void Shutdown(bool exiting) {
    if (!exiting && (gState.actor != nullptr || gState.captureActor != nullptr) && ue3::runtime::Ready()) {
        ClearAll("the mod shuts down");
    }
    if (!exiting && !gParts.empty() && ue3::runtime::Ready()) {
        ClearParts("the mod shuts down");
    }
    gParts.clear();
    gAnchor = {};
    gState = State{};
    gSettings = Settings{};
    gLoadedMesh = nullptr;
    gLastError = "none";
    gLoggedMoveFailure = gLoggedFirstPlace = gLoggedFlatFallback = false;
    for (ue3::ClassRef* const c : {&gActorClass, &gInterpActor, &gKActor, &gKActorSpawnable, &gStaticMeshActor, &gDynamicSmActor, &gStaticMesh, &gStaticMeshComponent,
             &gMaterial, &gMaterialInstance, &gMic, &gTextureParameterExpression, &gCaptureActor, &gCaptureComponent, &gTargetClass, &gTexture, &gTexture2D, &gVehicleBase, &gBioPawn,
             &gBioController, &gSaveGame}) {
        c->Reset();
    }
    for (ue3::FunctionRef* const f : {&gDynamicLoadObject, &gSpawn, &gDestroy, &gSetLocation, &gSetRotation, &gSetDrawScale, &gSetPhysics, &gSetCollision, &gSetHidden, &gSetBase, &gSetHardAttach,
             &gForceUpdate, &gSetActorCollision, &gSetBlockRigidBody, &gSetTraceBlocking, &gSetStaticMesh, &gGetMaterial, &gSetMaterial, &gGetNumElements, &gGetUnscaledBounds,
             &gCreateMic, &gSetParent, &gSetTextureParameter, &gGetTextureParameter, &gLightEnvironmentSetEnabled, &gCreateTarget, &gCaptureSetEnabled, &gCaptureSetFrameRate,
             &gCaptureSetView, &gCaptureSetParameters, &gEnterVehicle, &gFindClosestVehicle}) {
        f->Reset();
    }
    for (ue3::FieldRef* const f : {&gStatic, &gNoDelete, &gMovable, &gPhysics, &gBase, &gDynamicComponent, &gDynamicLightEnvironment, &gStaticActorComponent, &gComponentMesh,
             &gBounds, &gParent, &gStaticPermutation, &gTextureValues, &gTextureValueName, &gExpressions, &gEmissive, &gEmissiveExpression, &gBlendMode, &gLightingModel,
             &gUsedStaticLighting, &gUsedLightEnvironments, &gExpressionParameterName, &gSceneCapture, &gCaptureEnabled, &gEnablePostProcess, &gUseMainPostProcess,
             &gSkipUsersOccluded, &gSkipOwnerOccluded, &gMaxUpdateDist, &gPostProcess, &gFrameRate, &gSizeX, &gSizeY, &gPlayer, &gPlayerPostProcess, &gDefaultPostProcess, &gUpdateMatrices,
             &gViewMatrix, &gForceLinearGamma, &gTargetFormat, &gMeshMaterials, &gPrimitiveLightEnvironment, &gLightEnvironmentEnabled, &gTextureResource, &gTextureSrgb, &gTextureNeverStream,
             &gTextureLodGroup, &gTexture2DSizeX, &gTexture2DSizeY, &gTexture2DFormat, &gTexture2DRequestedMips, &gTexture2DResidentMips, &gTexture2DMipTail, &gTexture2DMips, &gWorldInfo,
             &gTextureValueObject}) {
        f->Reset();
    }
}

#else

std::string Command(std::string const& /*line*/) {
    return "mevr cockpit: LE1 only";
}

std::string VehicleCommand(std::string const& /*line*/) {
    return "mevr vehicle: LE1 only";
}

void Tick() {}

std::string StatusJson() {
    return "{\"up\": false}";
}

void LogStats() {}

void Shutdown(bool /*exiting*/) {}

#endif

} // namespace mevr::game::cockpit_probe
