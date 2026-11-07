// SPDX-License-Identifier: MIT
#include "game/CockpitProbe.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Rigid.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "core/vr/ScopeCapture.hpp"
#include "core/vr/ScreenReticle.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Engine.hpp"
#include "core/Ini.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/Skeleton.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/HandCalibration.hpp"
#include "game/Mako.hpp"
#include "game/CockpitPanel.hpp"
#include "game/MakoControls.hpp"
#include "game/Player.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
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
ue3::ClassRef gPawnClassRef{"Engine.Pawn"};
ue3::FieldRef gPawnControllerField{"Engine.Pawn", "Controller"};
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
ue3::FunctionRef gSetOwner{"Engine.Actor.SetOwner"};
ue3::FunctionRef gSetOwnerNoSee{"Engine.PrimitiveComponent.SetOwnerNoSee"};
ue3::FunctionRef gSetOnlyOwnerSee{"Engine.PrimitiveComponent.SetOnlyOwnerSee"};
ue3::FunctionRef gTrace{"Engine.Actor.Trace"};
ue3::FunctionRef gEnterVehicle{"SFXGame.BioPlayerController.BioEnterVehicle"};
ue3::FunctionRef gFindClosestVehicle{"SFXGame.SFXSaveGame.FindClosestDrivableVehicle"};

ue3::FieldRef gStatic{"Engine.Actor", "bStatic"};
ue3::FieldRef gDeleteMe{"Engine.Actor", "bDeleteMe"};  // destroyed, waiting for the garbage collection
ue3::FieldRef gNoDelete{"Engine.Actor", "bNoDelete"};
ue3::FieldRef gMovable{"Engine.Actor", "bMovable"};
ue3::FieldRef gRole{"Engine.Actor", "Role"};
ue3::FieldRef gPhysics{"Engine.Actor", "Physics"};
ue3::FieldRef gBase{"Engine.Actor", "Base"};
ue3::FieldRef gDynamicComponent{"Engine.DynamicSMActor", "StaticMeshComponent"};
ue3::FieldRef gDynamicLightEnvironment{"Engine.DynamicSMActor", "LightEnvironment"};
ue3::FieldRef gStaticActorComponent{"Engine.StaticMeshActor", "StaticMeshComponent"};
ue3::FieldRef gComponentMesh{"Engine.StaticMeshComponent", "StaticMesh"};
ue3::FieldRef gBounds{"Engine.PrimitiveComponent", "Bounds"};
ue3::FieldRef gDrawScale{"Engine.Actor", "DrawScale"};
ue3::FieldRef gDrawScale3D{"Engine.Actor", "DrawScale3D"};
ue3::FieldRef gComponentScale{"Engine.PrimitiveComponent", "Scale"};
ue3::FieldRef gComponentScale3D{"Engine.PrimitiveComponent", "Scale3D"};
ue3::FieldRef gPawnMesh{"Engine.Pawn", "Mesh"};
ue3::FieldRef gOwnerField{"Engine.Actor", "Owner"};
ue3::FieldRef gCameraViewTarget{"Engine.Camera", "ViewTarget"};
ue3::FieldRef gCameraViewTargetActor{gCameraViewTarget, "Target"};
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
ue3::FieldRef gMaxViewDistance{"Engine.SceneCaptureComponent", "MaxViewDistanceOverride"};
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
// `mevr vehicle levels` and `event` (research/47 "Reaching the Mako"): the level's streaming and its script's remote events.
ue3::ClassRef gRemoteEvent{"Engine.SeqEvent_RemoteEvent"};
ue3::FieldRef gRemoteEventName{"Engine.SeqEvent_RemoteEvent", "EventName"};
ue3::FunctionRef gCheckActivate{"Engine.SequenceEvent.CheckActivate"};
ue3::FieldRef gStreamingLevels{"Engine.WorldInfo", "StreamingLevels"};
ue3::FieldRef gStreamPackage{"Engine.LevelStreaming", "PackageName"};
ue3::FieldRef gStreamLoaded{"Engine.LevelStreaming", "LoadedLevel"};
ue3::FieldRef gStreamShouldLoad{"Engine.LevelStreaming", "bShouldBeLoaded"};
ue3::FieldRef gStreamShouldShow{"Engine.LevelStreaming", "bShouldBeVisible"};
ue3::FieldRef gStreamVisible{"Engine.LevelStreaming", "bIsVisible"};
ue3::FieldRef gStreamPending{"Engine.LevelStreaming", "bHasLoadRequestPending"};
// `mevr vehicle party`: the squad screen's own "A on the confirmation" (FinalizeParty), which hides the panel.
ue3::ClassRef gPartySelection{"SFXGame.BioSFHandler_PartySelection"};
ue3::FunctionRef gFinalizeParty{"SFXGame.BioSFHandler_PartySelection.FinalizeParty"};
// `mevr vehicle exit`: B's own exit (Console_VehicleExit), through the level's BioSeqEvt_VehicleExit when it has one.
ue3::ClassRef gGameModeVehicle{"SFXGame.SFXGameModeVehicle"};
ue3::FunctionRef gTryExitVehicle{"SFXGame.SFXGameModeVehicle.TryExitVehicle"};
ue3::FunctionRef gBioExitVehicle{"SFXGame.BioPlayerController.BioExitVehicle"};
// `mevr vehicle fire`: RB's own cannon press and release (Console_FireVehicleCannon), released `kFireHoldS` later.
ue3::FunctionRef gFireCannon{"SFXGame.SFXGameModeVehicle.FireCannon"};
ue3::FunctionRef gStopFiringCannon{"SFXGame.SFXGameModeVehicle.StopFiringCannon"};
ue3::Object* gFiringMode = nullptr;
double gFireReleaseAt = -1.0;
// `mevr vehicle fire mg`: the machine gun, RT's FireWeapon (SFXGameModeBase's exec, the vehicle mode's), released by StopFiringWeapon.
ue3::FunctionRef gFireWeapon{"SFXGame.SFXGameModeBase.FireWeapon"};
ue3::FunctionRef gStopFiringWeapon{"SFXGame.SFXGameModeBase.StopFiringWeapon"};
bool gFiringMg = false;
// `mevr vehicle angvel X Y Z`: SetRBAngularVelocity with that raw vector (replacing the body's), and 0.25 s later the
// vehicle's pitch, yaw and roll change logged: which way each world axis turns it, in which units (research/47).
ue3::FunctionRef gSetAngVel{"Engine.PrimitiveComponent.SetRBAngularVelocity"};
ue3::Object* gAngVelVehicle = nullptr;
double gAngVelAt = -1.0;
ue3::Rotator gAngVelFrom{};
ue3::Vector gAngVelAsked{};
constexpr double kFireHoldS = 0.3;
// `mevr vehicle trace`: does any script trace see the ground?
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace"};
// `mevr vehicle why`: what BioVehicleBase.DriverLeave tests before it lets the driver out.
ue3::FieldRef gVehicleDriver{"Engine.Vehicle", "Driver"};
ue3::FieldRef gPawnHealth{"Engine.Pawn", "Health"};
ue3::FieldRef gPlayersOnly{"Engine.WorldInfo", "bPlayersOnly"};  // BioExitVehicle's native refuses while it's set
ue3::FieldRef gPlayersOnlyPending{"Engine.WorldInfo", "bPlayersOnlyPending"};
ue3::FieldRef gHiddenField{"Engine.Actor", "bHidden"};
ue3::FieldRef gHiddenGame{"Engine.PrimitiveComponent", "HiddenGame"};
ue3::FunctionRef gPlaceExitingDriver{"SFXGame.BioVehicleBase.PlaceExitingDriver"};
// `mevr vehicle damage`: the game's damage (resolved on the pawn's class; this row is the base declaration's check).
ue3::FunctionRef gTakeDamage{"Engine.Actor.TakeDamage"};
ue3::ClassRef gDamageTypeClass{"Engine.DamageType"};
ue3::FieldRef gVehicleBehavior{"SFXGame.BioVehicleBase", "m_oBehavior"};  // `mevr vehicle die`
ue3::FunctionRef gReactToDeath{"SFXGame.BioActorBehavior.ReactToDeath"};
ue3::ClassRef gGameOver{"SFXGame.BioSFHandler_GameOver"};  // `mevr vehicle resume`
ue3::FunctionRef gGameOverEvent{"SFXGame.BioSFHandler_GameOver.HandleEvent"};
// `ride`: the camera's update (a post-handler, registered at startup) and the vehicle's components hidden from the eyes.
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera"};
ue3::ClassRef gPrimitiveComponent{"Engine.PrimitiveComponent"};
ue3::FieldRef gAllComponents{"Engine.Actor", "AllComponents"};
ue3::FieldRef gOwnerNoSee{"Engine.PrimitiveComponent", "bOwnerNoSee"};
// The seated driver (docs/mako-cockpit-goal.md item 7): its primitives eyes-only, its head's meshes hidden.
ue3::FieldRef gOnlyOwnerSee{"Engine.PrimitiveComponent", "bOnlyOwnerSee"};
ue3::FunctionRef gPrimitiveSetHidden{"Engine.PrimitiveComponent.SetHidden"};
ue3::FunctionRef gSetLightEnvironment{"Engine.PrimitiveComponent.SetLightEnvironment"};
std::array<ue3::FieldRef, 5> gHeadMeshes{ue3::FieldRef{"SFXGame.BioPawn", "m_oHeadMesh"}, ue3::FieldRef{"SFXGame.BioPawn", "m_oHairMesh"},
    ue3::FieldRef{"SFXGame.BioPawn", "m_oHeadGearMesh"}, ue3::FieldRef{"SFXGame.BioPawn", "m_oVisorMesh"},
    ue3::FieldRef{"SFXGame.BioPawn", "m_oFacePlateMesh"}};

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
    float viewDistM = 0.0f;     // `capturedist`: the screens' captures draw nothing further (MaxViewDistanceOverride); 0 the engine's
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
    bool part = false;  // `screenpart`: the actor is one of the parts, placed by the frame, never by Place()
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

// `ride`: the cockpit rides in the Mako (research/47 "Riding in the Mako"). In the camera's update, while the Mako is the pawn:
// the game's own chase view is kept for the centre screen, the parts are put in the vehicle's frame (pitch and roll too), and
// the camera moves to the cockpit's eye with the vehicle's rotation, so the head looks round a cockpit that is the Mako.
struct Ride {
    bool on = false;
    engine::UnrealVector eyeM{0.0, 1.0, 0.8};  // the cockpit's eye from the vehicle's origin: right, forward, up (metres)
    bool riding = false;                       // this tick's camera was moved
    // The head's offset from the game camera (the host's view: forward, right, up, world units) when the ride began in this
    // vehicle, or at `ride seat`: where the head was then is the cockpit's eye; leaning moves it from there.
    bool seated = false;
    engine::UnrealVector seat{};
    // Until it's kept, the seat follows the head (seatWaiting): the host's samples counted, those carrying a head offset too.
    bool seatWaiting = false;
    std::uint64_t seatSequence = 0;
    int seatSamples = 0;
    int seatPlaced = 0;
    ue3::Object* vehicle = nullptr;            // the vehicle whose components are hidden from the eyes
    std::vector<std::pair<ue3::Object*, bool>> hidden;  // each primitive component and its bOwnerNoSee before
    bool chaseValid = false;
    ue3::Vector chaseLocation{};
    ue3::Rotator chaseRotation{};
    std::uint32_t cameraTicks = 0;
    std::uint32_t partPlacements = 0;
    // This frame's cockpit in the world: its eye and the vehicle's axes (for the HUD's displays).
    bool haveEye = false;
    engine::UnrealVector eyeWorld{};
    engine::Basis frame{};
    double upm = engine::kUnitsPerMeter;
};
Ride gRide;

// `hud`: where the Mako's HUD goes in the cockpit (docs/mako-cockpit-goal.md item 8): the vehicle's status, the radar and the
// rest of the HUD panel, each a centre (right, forward, up from the cockpit's eye, metres) and a width. place.py --script writes
// it from the manifest's left display.
struct HudLayout {
    bool set = false;
    engine::UnrealVector statusM{};
    float statusWidthM = 0.0f;
    engine::UnrealVector radarM{};
    float radarWidthM = 0.0f;
    engine::UnrealVector panelM{};
    float panelWidthM = 0.0f;
};
HudLayout gHudLayout;

// `seat hip R F U knees R F U R F U heels R F U R F U grips R F U R F U`: the pilot's points from the cockpit's eye (metres: right,
// forward, up; place.py --script from the manifest's pilot): the hips' H-point, each knee and heel, the lever's and the stick's
// grips (left, right), the lever's at neutral.
struct SeatLayout {
    bool set = false;
    engine::UnrealVector hipM{};
    std::array<engine::UnrealVector, 2> kneeM{};
    std::array<engine::UnrealVector, 2> heelM{};
    std::array<engine::UnrealVector, 2> gripM{};
    // `axes`: each handle's grip axis (unit, right forward up; the lever's bar, the stick's centre line), measured from the models
    // by place.py: a held hand's fist wraps it (game/arm_ik).
    bool haveAxes = false;
    std::array<engine::UnrealVector, 2> axisM{};
};
SeatLayout gSeatLayout;

// `panel button R F U in R F U travel M lever R F U axis R F U grip R F U pull DEG lamps FIRST COUNT FIRST COUNT`: the repair
// button's cap top and its press (a unit direction into it), the cap's travel; the extraction lever's hinge, its axis (unit; a
// turn by + swings the handle out), the handle's grip at rest and how far it swings; which of Repair_Base's elements are the
// ring's health lamps and the plate's progress squares. Points from the cockpit's eye, metres (right, forward, up); place.py
// --script writes it from the hull manifest's panels (research/48 section 27, game/CockpitPanel).
struct PanelLayout {
    bool set = false;
    engine::UnrealVector buttonM{};
    engine::UnrealVector buttonInM{};
    float travelM = 0.0f;
    engine::UnrealVector hingeM{};
    engine::UnrealVector hingeAxisM{};
    engine::UnrealVector gripM{};
    float pullDeg = 0.0f;
    int healthFirst = 1;
    int healthCount = 0;
    int progressFirst = 0;
    int progressCount = 0;
};
PanelLayout gPanelLayout;
// The lamps' instances as last set on Repair_Base: its component, each element's colour (-1: not set by us), the instances.
struct PanelLamps {
    ue3::Object* component = nullptr;
    std::array<int, 32> shown{};
    std::array<ue3::Object*, 6> instances{};  // by vr::cockpit_panel::Colour
    bool toldMissing = false;
};
PanelLamps gPanelLamps;

// Shepard in the seat (docs/mako-cockpit-goal.md item 7, research/47 "Shepard in the seat"). In the Mako the game keeps the
// driver hidden at the vehicle's origin, based on it, PHYS_None (`mevr vehicle why`). While riding with [Cockpit] seated and a
// `seat` layout: shown to the eyes only (owned by the vehicle, the eyes' view actor; bOnlyOwnerSee on each primitive keeps it
// out of the captures), the head's meshes hidden (the eye is inside the head), placed each camera update with its hips on the
// H-point and the vehicle's rotation; game/arm_ik poses the legs to the heels and the hands to the controls (CurrentSeat).
struct SeatedDriver {
    ue3::Object* pawn = nullptr;
    ue3::Object* vehicle = nullptr;
    ue3::Object* mesh = nullptr;
    void const* world = nullptr;  // a new level: the old pointers are never called
    bool wasHidden = true;
    ue3::Object* ownerBefore = nullptr;
    bool ownerSet = false;
    std::vector<std::pair<ue3::Object*, bool>> onlyOwner;  // each primitive and its bOnlyOwnerSee before
    std::vector<std::pair<ue3::Object*, bool>> head;       // the head's meshes and their HiddenGame before
    std::vector<std::pair<ue3::Object*, ue3::Object*>> lit;  // each primitive lit by the vehicle's light environment, and its own
    bool haveHips = false;
    ik::Vec3 hips{};  // the hips' midpoint in the mesh's component space (game/arm_ik)
    bool haveHeadBone = false;
    ik::Vec3 headBone{};  // the head bone there
    double slideBackM = 0.0;  // how far PlaceDriver slid the body back of the H-point for the head
    // Shown only while arm IK poses it seated (its last NoteSeatHips within 0.5 s): without tracked controllers arm IK is off and
    // the animation would stand Shepard up through the cockpit's eye (2026-11-03, BIOA_UNC52 with no replay).
    double posedAt = -1.0;
    bool visible = false;
    std::uint32_t placements = 0;
    std::uint32_t shown = 0;
};
SeatedDriver gSeated;

// `aimscreen`: a second capture along the aim, on its own slot of the screen part (Hull_Console's slot 3, the right screen).
struct AimScreen {
    ue3::Object* target = nullptr;
    ue3::Object* captureActor = nullptr;
    ue3::Object* captureComponent = nullptr;
    ue3::Object* mic = nullptr;
    int element = 3;
    int width = 768;
    int height = 306;
    float fovDeg = 30.0f;
    std::uint32_t pointedTicks = 0;
    double lastPointed = -1.0;
    // `aimreticle`: the package's Aim_Reticle at the traced aim point, seen by the captures, not the eyes.
    ue3::Object* reticle = nullptr;
    ue3::Object* reticleComponent = nullptr;
    ue3::Object* reticleOwner = nullptr;  // refreshed when possession or the eye view target changes
    bool showInEyes = false;           // only the explicit diagnostic `aimreticle eyes`
    bool parked = false;
    float reticleDeg = 6.0f;      // its width as seen from the aim capture
    float reticleWidthUnits = 97.0f;  // the mesh's own width (research/48: 97 package units)
    double traceUnits = 0.0;      // the last traced distance
    std::string traceNote;        // what the last full-length trace answered
    float traceExtent = 0.0f;     // `aimreticle ... extent N`: a box trace (level meshes may block only those)
    float rangeM = 200.0f;        // `aimreticle ... range M`: how far the trace looks, and where the reticle sits on a miss
    float pitchDeg = 0.0f;        // `aimreticle ... pitch DEG`: the stand-in aim tilted from the head's view (up positive)
};
AimScreen gAim;
void ClearAim(char const* why);
void HideFromCaptures();
std::string MapName(ue3::Object* controller);
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
void SetOwnerOf(ue3::Object* actor, ue3::Object* owner);

void DestroyActor(ue3::Object* actor, char const* why) {
    // Owned by the pawn (capturehide 2), Destroy answered false and left the parts alive [verify: UWorld::DestroyActor's
    // player-controller test reached through the owner]: no owner first.
    if (gOwnerField.Read<ue3::Object*>(actor).value_or(nullptr) != nullptr) {
        SetOwnerOf(actor, nullptr);
    }
    ue3::Params p = ParamsOf(gDestroy);
    bool const called = Call(actor, gDestroy, p);
    bool const destroyed = p.ReturnBool().value_or(false);
    // UWorld::DestroyActor refuses a bStatic or bNoDelete actor, and one whose Role isn't ROLE_Authority (3).
    log::Info("cockpit probe: destroying {} ({}): Destroy {} returned {} (bStatic {}, bNoDelete {}, Role {}, bDeleteMe now {})", ue3::runtime::FullName(actor), why,
        called ? "called," : "NOT called,", destroyed ? "true" : "false", gStatic.ReadBool(actor).value_or(false) ? 1 : 0, gNoDelete.ReadBool(actor).value_or(false) ? 1 : 0,
        static_cast<int>(gRole.Read<std::uint8_t>(actor).value_or(255)), gDeleteMe.ReadBool(actor).value_or(false) ? 1 : 0);
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
    if (!gState.part && Still(gState.actor, gActorClass)) {  // a part goes with the parts
        DestroyActor(gState.actor, why);
    }
    ClearAim(why);  // it sits on the same mesh
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
        gAim = AimScreen{};
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
    // Riding, the controls' models follow game/MakoControls (research/47): the throttle's handle slides along the seat's
    // forward by the lever's travel, the stick's grip tilts about its pivot (the part's origin) as the hand tilts it, or turns
    // about its own vertical by the steering with [Mako] stickTwist. From the mesh's name.
    // The repair button's cap goes in along its press, the extraction handle swings about its hinge (game/CockpitPanel,
    // research/48 section 27).
    enum class Motion { None, Lever, Stick, Press, Hinge } motion = Motion::None;
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
    if (gState.part) {
        ClearAll(why);  // the screen on one of them, and its capture
    }
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
    std::string const meshName = Lower(R().NameText(mesh));
    part.motion = meshName == "throttle_handle" ? Part::Motion::Lever : meshName == "joystick_grip" ? Part::Motion::Stick
        : meshName == "repair_cap" ? Part::Motion::Press : meshName == "extraction_handle" ? Part::Motion::Hinge : Part::Motion::None;
    PlacePart(part);
    gParts.push_back(part);
    HideFromCaptures();
    std::string const report = std::format("part {}: {} at right {:.3f} forward {:.3f} up {:.3f} m, yaw {:.1f}, {:.5f} m a unit (draw scale {:.3f}); {} part{}",
        gParts.size(), ue3::runtime::FullName(mesh), part.offsetM.x, part.offsetM.y, part.offsetM.z, part.yawDeg, part.metersPerUnit,
        part.metersPerUnit * gAnchor.unitsPerMeter, gParts.size(), gParts.size() == 1 ? "" : "s");
    log::Info("cockpit probe: {}", report);
    return report;
}

// Every actor of the spawnable kinds whose mesh comes from our package: parts a hot reload or a lost list left behind.
int SweepCockpitActors() {
    if (gState.part) {
        ClearAll("mevr cockpit parts sweep");
    }
    std::vector<ue3::Object*> found;
    int destroyedBefore = 0;
    for (std::size_t i = 0; i < kAutomatic; ++i) {
        ActorKind const& kind = kKinds[i];
        ForEachLive(kind.cls->Get(), [&](ue3::Object* actor) {
            ue3::Object* const component = kind.component->Read<ue3::Object*>(actor).value_or(nullptr);
            ue3::Object* const mesh = component != nullptr ? gComponentMesh.Read<ue3::Object*>(component).value_or(nullptr) : nullptr;
            if (mesh == nullptr || !Lower(R().PathName(mesh)).starts_with("biog_mevr_cockpit.") || actor == gState.actor) {
                return;
            }
            if (gDeleteMe.ReadBool(actor).value_or(false)) {
                ++destroyedBefore;  // destroyed already; its object stays until the garbage collection
                return;
            }
            found.push_back(actor);
        });
    }
    for (ue3::Object* const actor : found) {
        DestroyActor(actor, "mevr cockpit parts sweep");
    }
    if (destroyedBefore > 0) {
        log::Info("cockpit probe: parts sweep: {} more were destroyed before (bDeleteMe), waiting for the garbage collection", destroyedBefore);
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
        out += std::format("  {} {}: right {:.3f} forward {:.3f} up {:.3f} m, yaw {:.1f}, {:.5f} m a unit{}\n", i + 1, ue3::runtime::FullName(p.mesh), p.offsetM.x, p.offsetM.y,
            p.offsetM.z, p.yawDeg, p.metersPerUnit, gState.part && gState.actor == p.actor ? " (the screen)" : "");
    }
    return out;
}

// `loaded <Package.Object> ...`: whether each object is in memory now, without loading anything (research/48 section 25: what
// a package may import under RequireImportsAlreadyLoaded).
std::string DoLoaded(std::istringstream& words) {
    std::vector<std::pair<std::string, ue3::Object*>> wanted;
    for (std::string word; words >> word;) {
        wanted.emplace_back(Lower(word), nullptr);
    }
    if (wanted.empty()) {
        return "usage: mevr cockpit loaded <Package.Object> [...]";
    }
    std::int32_t const count = R().ObjectCount();
    for (std::int32_t i = 0; i < count; ++i) {
        ue3::Object* const object = R().ObjectAt(i);
        if (object == nullptr) {
            continue;
        }
        std::string const name = Lower(R().NameText(object));
        for (auto& [path, found] : wanted) {
            if (found == nullptr && path.size() > name.size() && path.ends_with(name) && path[path.size() - name.size() - 1] == '.' &&
                Lower(R().PathName(object)) == path) {
                found = object;
            }
        }
    }
    std::string out = std::format("in {}:", MapName(player::Controller()));
    for (auto const& [path, found] : wanted) {
        out += std::format("\n  {}: {}", path, found != nullptr ? std::format("loaded ({})", ue3::runtime::FullName(found)) : std::string("not loaded"));
    }
    log::Info("cockpit probe: {}", out);
    return out;
}

// `variant lit|glow|alt|base`: each part's elements to the package's `<instance>_Lit_MIC`, `_Glow_MIC` (research/48 section 25) or
// `_Alt_MIC` (the glass's other material, section 26), or
// back to the plain `<instance>_MIC`; an element without that variant keeps what it shows, and our own instances (the screens',
// the aim's) are left alone.
std::string DoVariant(std::string const& which) {
    std::string const suffix = which == "lit" ? "_lit_mic" : which == "glow" ? "_glow_mic" : which == "alt" ? "_alt_mic" : which == "base" ? "_mic" : "";
    if (suffix.empty()) {
        return "usage: mevr cockpit variant lit|glow|alt|base";
    }
    if (auto const missing = MissingFunction({&gGetMaterial, &gSetMaterial, &gGetNumElements})) {
        return Refuse(*missing);
    }
    int swapped = 0;
    int kept = 0;
    std::string swaps;
    for (Part const& part : gParts) {
        if (!PartAlive(part)) {
            continue;
        }
        ue3::Params count = ParamsOf(gGetNumElements);
        Call(part.component, gGetNumElements, count);
        int const elements = count.Return<std::int32_t>().value_or(0);
        bool changed = false;
        for (int i = 0; i < elements; ++i) {
            ue3::Params get = ParamsOf(gGetMaterial);
            get.Set("ElementIndex", i);
            Call(part.component, gGetMaterial, get);
            std::string const path = R().PathName(get.Return<ue3::Object*>().value_or(nullptr));
            std::string base = Lower(path);
            if (!base.starts_with("biog_mevr_cockpit.")) {
                ++kept;
                continue;
            }
            for (std::string_view const variant : {std::string_view("_lit_mic"), std::string_view("_glow_mic"), std::string_view("_alt_mic")}) {
                if (base.ends_with(variant)) {
                    base = base.substr(0, base.size() - variant.size()) + "_mic";
                }
            }
            if (!base.ends_with("_mic")) {
                ++kept;
                continue;
            }
            std::string const target = base.substr(0, base.size() - 4) + suffix;
            if (target == Lower(path)) {
                continue;
            }
            ue3::Object* const material = LoadObject(target, gMic.Get());
            if (!ue3::IsA(material, gMaterialInstance)) {
                ++kept;
                continue;
            }
            ue3::Params set = ParamsOf(gSetMaterial);
            set.Set("ElementIndex", i);
            set.Set("Material", material);
            Call(part.component, gSetMaterial, set);
            ++swapped;
            changed = true;
            if (swaps.size() < 1500) {
                swaps += std::format("\n  {} [{}] -> {}", R().NameText(part.mesh), i, R().PathName(material));
            }
        }
        if (changed) {
            FlushActor(part.actor, false);
        }
    }
    std::string const report = std::format("variant {}: {} element{} swapped, {} kept{}", which, swapped, swapped == 1 ? "" : "s", kept, swaps);
    log::Info("cockpit probe: {}", report);
    return report;
}

// `screenpart <Package.Object>`: the screen goes on that part (the hull's console) instead of a mesh of its own; it stays where the
// frame put it. Then `element`, `screen` as for a mesh.
std::string DoScreenPart(std::string const& path) {
    for (Part const& part : gParts) {
        if (!PartAlive(part) || Lower(R().PathName(part.mesh)) != Lower(path)) {
            continue;
        }
        if (gState.actor != part.actor) {
            ClearAll("the screen moves to a part");
            gState.world = engine::World();
            gState.actor = part.actor;
            gState.component = part.component;
            gState.mesh = part.mesh;
            gState.kind = "part";
            gState.part = true;
        }
        std::string const report = std::format("the screen goes on the part {} ({}); mevr cockpit screen next", ue3::runtime::FullName(part.mesh), ue3::runtime::FullName(part.actor));
        log::Info("cockpit probe: {}", report);
        return report;
    }
    return Refuse(std::format("no placed part shows {} (mevr cockpit parts)", path));
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
ue3::Object* CreateTarget(int width, int height, bool floatTarget, LinearColor clear = LinearColor{0.5f, 0.0f, 0.5f, 1.0f}) {
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
    p.Set("InClearColor", clear);
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

// Behind and above the pawn (or the Mako), looking at it: the stand-in for the third-person view. Riding in the Mako: the
// game's own chase camera, as its camera update left it before `ride` moved the camera into the cockpit.
void PointCapture() {
    if (gRide.riding && gRide.chaseValid) {
        gState.askedView = gRide.chaseLocation;
        ue3::Params view = ParamsOf(gCaptureSetView);
        view.Set("NewLocation", gRide.chaseLocation);
        view.Set("NewRotation", gRide.chaseRotation);
        if (Call(gState.captureComponent, gCaptureSetView, view) && FlushActor(gState.captureActor, true)) {
            ++gState.pointedTicks;
        }
        gState.lastPointed = Now();
        return;
    }
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
        WriteField(gMaxViewDistance, component, static_cast<float>(gSettings.viewDistM * engine::kUnitsPerMeter), "CockpitProbe.MaxViewDistance");
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
    report += ApplyMaterial();
    HideFromCaptures();
    return report;
}

// ---- Keeping the cockpit out of the captures ------------------------------------------------------------------------

// A view leaves out a component with bOwnerNoSee when the view's actor is in the component's owner chain (its actor, that
// actor's Owner, and so on), and one with bOnlyOwnerSee when it isn't. `capturehide`:
//   1: each part owned by the chase capture, the chase capture by the aim capture, bOwnerNoSee. Left the cockpit in the
//      chase capture's picture (2026-11-02): a capture's view actor isn't its own actor.
//   2: each part owned by the pawn, bOnlyOwnerSee: in the eyes, out of both captures (2026-11-03). The eyes' view actor is
//      the pawn (as Halcyon's head hide, bOwnerNoSee on the pawn's components, implies); a capture's is neither the pawn nor
//      the controller. Actor.SetOwner through ProcessEvent changed nothing, so the Owner field is written (SetOwnerOf); until
//      it was, this mode hid the cockpit from the eyes too. In the Mako the pawn is the vehicle: run again on a new pawn.
//   3: as 2 with the player controller as the owner: gone from the eyes.
//   0: neither.
int gCaptureHide = 2;

bool gLoggedOwnerWrite = false;

// Actor.SetOwner through ProcessEvent left every part's Owner at the player controller (2026-11-03): then the field itself.
void SetOwnerOf(ue3::Object* actor, ue3::Object* owner) {
    ue3::Params p = ParamsOf(gSetOwner);
    p.Set("NewOwner", owner);
    Call(actor, gSetOwner, p);
    if (gOwnerField.Read<ue3::Object*>(actor).value_or(nullptr) != owner) {
        if (ue3::Object** const field = gOwnerField.In<ue3::Object*>(actor)) {
            *field = owner;
            write_journal::Wrote("CockpitProbe.partOwner", field, sizeof(*field));
            if (!gLoggedOwnerWrite) {
                gLoggedOwnerWrite = true;
                log::Info("cockpit probe: Actor.SetOwner left {}'s Owner as it was: written into the field", ue3::runtime::FullName(actor));
            }
        }
    }
}

void HideFromCaptures() {
    if (gSetOwner.Get() == nullptr || gSetOwnerNoSee.Get() == nullptr || gSetOnlyOwnerSee.Get() == nullptr) {
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (gCaptureHide == 1 && gState.captureActor != nullptr && gAim.captureActor != nullptr) {
        SetOwnerOf(gState.captureActor, gAim.captureActor);
    }
    ue3::Object* const owner = gCaptureHide == 1 ? gState.captureActor : gCaptureHide == 2 ? pawn : gCaptureHide == 3 ? controller : nullptr;
    if (gCaptureHide != 0 && owner == nullptr) {
        return;
    }
    for (Part const& part : gParts) {
        if (!PartAlive(part)) {
            continue;
        }
        SetOwnerOf(part.actor, owner);
        ue3::Params noSee = ParamsOf(gSetOwnerNoSee);
        noSee.SetBool("bNewOwnerNoSee", gCaptureHide == 1);
        Call(part.component, gSetOwnerNoSee, noSee);
        ue3::Params onlySee = ParamsOf(gSetOnlyOwnerSee);
        onlySee.SetBool("bNewOnlyOwnerSee", gCaptureHide >= 2);
        Call(part.component, gSetOnlyOwnerSee, onlySee);
        FlushActor(part.actor, false);  // the scene's copy of the owner chain is made when the component attaches
    }
}

// ---- Riding in the Mako ---------------------------------------------------------------------------------------------

// The vehicle's own primitives out of the eyes (the eyes' view actor is the pawn, the vehicle: bOwnerNoSee), still in the
// captures' pictures (a capture's view actor is neither): the chase screen shows the Mako, the eyes see the cockpit.
// A component hung on one of the vehicle mesh's Turret_Mount bones: the turret's base, the cannon, the machine gun.
bool OnTurretMount(ue3::Object* mesh, ue3::Object* component) {
    if (mesh == nullptr || component == mesh) {
        return false;
    }
    std::optional<engine::skeleton::Attachment> const attachment = engine::skeleton::FindAttachment(mesh, component);
    if (!attachment || attachment->bone < 0) {
        return false;
    }
    std::optional<engine::skeleton::RefSkeleton> const skeleton = engine::skeleton::ReadRefSkeleton(mesh);
    return skeleton && static_cast<std::size_t>(attachment->bone) < skeleton->names.size() &&
        skeleton->names[static_cast<std::size_t>(attachment->bone)].starts_with("Turret_Mount");
}

void HideVehicle(ue3::Object* vehicle) {
    auto const components = gAllComponents.ElementsOf(vehicle);
    if (!components) {
        return;
    }
    gRide.vehicle = vehicle;
    // The turret stays in view over the cabin ([Cockpit] showTurret; research/47 "The cannon over the cabin"): the cockpit's eye sits
    // under it, so its barrels show at the top of the windshield and the shots come from there.
    bool const keepTurret = hand_calibration::GetCockpit().turret;
    ue3::Object* const mesh = gPawnMesh.Read<ue3::Object*>(vehicle).value_or(nullptr);
    int kept = 0;
    for (std::int32_t i = 0; i < components->count; ++i) {
        ue3::Object* const component = *static_cast<ue3::Object**>(components->At(i));
        if (!ue3::IsA(component, gPrimitiveComponent)) {
            continue;
        }
        if (keepTurret && OnTurretMount(mesh, component)) {
            ++kept;
            continue;
        }
        gRide.hidden.emplace_back(component, gOwnerNoSee.ReadBool(component).value_or(false));
        ue3::Params noSee = ParamsOf(gSetOwnerNoSee);
        noSee.SetBool("bNewOwnerNoSee", true);
        Call(component, gSetOwnerNoSee, noSee);
    }
    log::Info("cockpit probe: ride: {} of {}'s components hidden from the eyes{}", gRide.hidden.size(), ue3::runtime::FullName(vehicle),
        kept > 0 ? std::format(", its turret's {} kept in view over the cabin", kept) : std::string());
}

void ShowVehicle() {
    if (gRide.vehicle != nullptr && ue3::runtime::IsAlive(gRide.vehicle)) {
        for (auto const& [component, before] : gRide.hidden) {
            if (ue3::runtime::IsAlive(component)) {
                ue3::Params noSee = ParamsOf(gSetOwnerNoSee);
                noSee.SetBool("bNewOwnerNoSee", before);
                Call(component, gSetOwnerNoSee, noSee);
            }
        }
    }
    gRide.vehicle = nullptr;
    gRide.hidden.clear();
}

// ---- Shepard in the seat ---------------------------------------------------------------------------------------------

void SetComponentHidden(ue3::Object* component, bool hidden) {
    ue3::Params p = ParamsOf(gPrimitiveSetHidden);
    p.SetBool("NewHidden", hidden);
    Call(component, gPrimitiveSetHidden, p);
}

void SetOnlyOwnerSee(ue3::Object* component, bool only) {
    ue3::Params p = ParamsOf(gSetOnlyOwnerSee);
    p.SetBool("bNewOnlyOwnerSee", only);
    Call(component, gSetOnlyOwnerSee, p);
}

void SetActorHidden(ue3::Object* actor, bool hidden) {
    ue3::Params p = ParamsOf(gSetHidden);
    p.SetBool("bNewHidden", hidden);
    Call(actor, gSetHidden, p);
}

void SetLightEnvironmentOf(ue3::Object* component, ue3::Object* environment) {
    ue3::Params p = ParamsOf(gSetLightEnvironment);
    p.Set("NewLightEnvironment", environment);
    Call(component, gSetLightEnvironment, p);
}

void ShowDriver(ue3::Object* vehicle) {
    ue3::Object* const driver = gVehicleDriver.Read<ue3::Object*>(vehicle).value_or(nullptr);
    if (driver == nullptr || !ue3::runtime::IsAlive(driver)) {
        return;
    }
    gSeated = {};
    gSeated.pawn = driver;
    gSeated.vehicle = vehicle;
    gSeated.world = engine::World();
    gSeated.mesh = gPawnMesh.Read<ue3::Object*>(driver).value_or(nullptr);
    gSeated.wasHidden = gHiddenField.ReadBool(driver).value_or(true);
    gSeated.ownerBefore = gOwnerField.Read<ue3::Object*>(driver).value_or(nullptr);
    for (ue3::FieldRef& field : gHeadMeshes) {
        ue3::Object* const part = field.Read<ue3::Object*>(driver).value_or(nullptr);
        bool const listed = std::any_of(gSeated.head.begin(), gSeated.head.end(), [&](auto const& h) { return h.first == part; });
        if (part != nullptr && !listed && ue3::runtime::IsAlive(part)) {
            gSeated.head.emplace_back(part, gHiddenGame.ReadBool(part).value_or(false));
            SetComponentHidden(part, true);
        }
    }
    if (gSeated.ownerBefore != vehicle) {
        SetOwnerOf(driver, vehicle);
        gSeated.ownerSet = true;
    }
    if (auto const components = gAllComponents.ElementsOf(driver)) {
        for (std::int32_t i = 0; i < components->count; ++i) {
            ue3::Object* const component = *static_cast<ue3::Object**>(components->At(i));
            if (ue3::IsA(component, gPrimitiveComponent)) {
                gSeated.onlyOwner.emplace_back(component, gOnlyOwnerSee.ReadBool(component).value_or(false));
                SetOnlyOwnerSee(component, true);
            }
        }
    }
    // The driver's own light environment, inside the vehicle, drew it black (2026-11-03): lit by the vehicle's instead, as UE3 lights
    // what a pawn carries with the pawn's.
    ue3::Object* const vehicleMesh = gPawnMesh.Read<ue3::Object*>(vehicle).value_or(nullptr);
    ue3::Object* const vehicleLight = vehicleMesh != nullptr ? gPrimitiveLightEnvironment.Read<ue3::Object*>(vehicleMesh).value_or(nullptr) : nullptr;
    if (vehicleLight != nullptr && gSetLightEnvironment.Get() != nullptr) {
        for (auto const& [component, before] : gSeated.onlyOwner) {
            ue3::Object* const own = gPrimitiveLightEnvironment.Read<ue3::Object*>(component).value_or(nullptr);
            if (own != nullptr && own != vehicleLight) {
                gSeated.lit.emplace_back(component, own);
                SetLightEnvironmentOf(component, vehicleLight);
            }
        }
    }
    FlushActor(driver, false);  // the actor stays hidden until arm IK poses it (SeatTick)
    ++gSeated.shown;
    log::Info("cockpit probe: seat: {} shown in the seat (it was {}hidden, owned by {}{}); {} primitives eyes-only, {} of the head's meshes hidden; "
              "its light environment {}",
        ue3::runtime::FullName(driver), gSeated.wasHidden ? "" : "not ", ue3::runtime::FullName(gSeated.ownerBefore),
        gSeated.ownerSet ? ", now the vehicle" : "", gSeated.onlyOwner.size(), gSeated.head.size(),
        gSeated.lit.empty() ? std::string("its own (no vehicle light environment)") : std::format("the vehicle's ({} {})",
            ue3::runtime::FullName(vehicleLight), gSeated.lit.size() == 1 ? "primitive" : std::format("{} primitives", gSeated.lit.size())));
}

// Undone as found. Still the vehicle's driver (the cockpit turned off, a cutscene): hidden again at the vehicle's origin, as the
// game keeps it; out of the vehicle the game has already shown and placed it, so only the components' flags go back.
void HideDriver() {
    if (gSeated.pawn == nullptr) {
        return;
    }
    ue3::Object* const driver = gSeated.pawn;
    if (ue3::runtime::IsAlive(driver)) {
        for (auto const& [part, before] : gSeated.head) {
            if (ue3::runtime::IsAlive(part)) {
                SetComponentHidden(part, before);
            }
        }
        for (auto const& [component, before] : gSeated.onlyOwner) {
            if (ue3::runtime::IsAlive(component)) {
                SetOnlyOwnerSee(component, before);
            }
        }
        for (auto const& [component, own] : gSeated.lit) {
            if (ue3::runtime::IsAlive(component) && ue3::runtime::IsAlive(own)) {
                SetLightEnvironmentOf(component, own);
            }
        }
        ue3::Object* const vehicle = gSeated.vehicle;
        bool const driving = vehicle != nullptr && ue3::runtime::IsAlive(vehicle) && gVehicleDriver.Read<ue3::Object*>(vehicle).value_or(nullptr) == driver;
        if (gSeated.ownerSet && gOwnerField.Read<ue3::Object*>(driver).value_or(nullptr) == vehicle) {
            SetOwnerOf(driver, gSeated.ownerBefore);
        }
        if (driving) {
            if (auto const at = player::Location(vehicle); at) {
                if (ue3::Vector* const atField = gActorLocation.In<ue3::Vector>(driver)) {
                    *atField = {at->x, at->y, at->z};
                    write_journal::Wrote("CockpitProbe.driverLocation", atField, sizeof(*atField));
                }
            }
            SetActorHidden(driver, gSeated.wasHidden);
        }
        FlushActor(driver, false);
        log::Info("cockpit probe: seat: {} out of the seat ({})", ue3::runtime::FullName(driver), driving ? "still the driver: hidden again" : "no longer driving");
    }
    gSeated = {};
}

// Each mod tick from RideTick: `vehicle` is the Mako the cockpit rides in now (null: not riding).
void SeatTick(ue3::Object* vehicle) {
    bool const want = vehicle != nullptr && hand_calibration::GetCockpit().seated && gSeatLayout.set && gPrimitiveSetHidden.Get() != nullptr &&
        gSetOnlyOwnerSee.Get() != nullptr && gSetHidden.Get() != nullptr;
    if (gSeated.pawn != nullptr && gSeated.world != engine::World()) {
        gSeated = {};
    }
    if (gSeated.pawn != nullptr) {
        bool const same = want && gSeated.vehicle == vehicle && gVehicleDriver.Read<ue3::Object*>(vehicle).value_or(nullptr) == gSeated.pawn &&
            ue3::runtime::IsAlive(gSeated.pawn);
        if (!same) {
            HideDriver();
        }
    }
    if (want && gSeated.pawn == nullptr) {
        ShowDriver(vehicle);
    }
    if (gSeated.pawn != nullptr) {
        bool const posed = gSeated.posedAt >= 0.0 && Now() - gSeated.posedAt < 0.5;
        if (posed != gSeated.visible) {
            SetActorHidden(gSeated.pawn, !posed);
            FlushActor(gSeated.pawn, false);
            gSeated.visible = posed;
            log::Info("cockpit probe: seat: Shepard {}", posed ? "in view, posed seated by arm IK" : "hidden again: arm IK isn't posing the seat (no tracked controllers, or arm IK off)");
        }
    }
}

// In the camera's update, after the parts: the driver's hips on the H-point, with the vehicle's rotation. The hips' place in
// the actor's own axes comes from where the mesh is now (before the first pose sample, the actor's origin: its collision
// centre, about the pelvis).
// A head bone's place behind its eyes (Shepard's sits at the skull's base: the eyes are about this far ahead of it), and the
// furthest the seated body slides back of the seat's H-point to get it there.
constexpr double kHeadBoneBehindEyeM = 0.10;
constexpr double kSeatSlideBackM = 0.25;

void PlaceDriver(engine::Basis const& frame, engine::UnrealVector const& eye, double upm, ue3::Rotator const& turn) {
    ue3::Object* const driver = gSeated.pawn;
    if (driver == nullptr || !ue3::runtime::IsAlive(driver)) {
        return;
    }
    ue3::Vector* const atField = gActorLocation.In<ue3::Vector>(driver);
    ue3::Rotator* const turnField = gActorRotation.In<ue3::Rotator>(driver);
    if (atField == nullptr || turnField == nullptr) {
        return;
    }
    // The hips and the head bone from the actor, along its own axes (forward, right, up; world units).
    engine::UnrealVector local{};
    std::optional<engine::UnrealVector> headLocal;
    if (gSeated.haveHips && gSeated.mesh != nullptr) {
        ik::Affine const toWorld = engine::skeleton::LocalToWorld(gSeated.mesh);
        engine::Basis const was = engine::ToBasis(engine::UnrealRotation{turnField->pitch, turnField->yaw, turnField->roll});
        auto const fromActor = [&](ik::Vec3 const& component) {
            ik::Vec3 const w = ik::TransformPoint(toWorld, component);
            return engine::Apply(engine::Inverse(was), {w.x - atField->x, w.y - atField->y, w.z - atField->z});
        };
        local = fromActor(gSeated.hips);
        if (gSeated.haveHeadBone) {
            headLocal = fromActor(gSeated.headBone);
        }
    }
    // The actor from the cockpit's eye, along the vehicle's axes: the hips on the seat's H-point; along forward, with the head
    // bone known, slid back until it sits kHeadBoneBehindEyeM behind the eye (a head's bone is behind its eyes), at most
    // kSeatSlideBackM behind the H-point and never forward of it.
    engine::UnrealVector offset{gSeatLayout.hipM.y * upm - local.x, gSeatLayout.hipM.x * upm - local.y, gSeatLayout.hipM.z * upm - local.z};
    if (headLocal) {
        double const byHead = -kHeadBoneBehindEyeM * upm - headLocal->x;
        offset.x = std::clamp(byHead, offset.x - kSeatSlideBackM * upm, offset.x);
        gSeated.slideBackM = (gSeatLayout.hipM.y * upm - local.x - offset.x) / upm;
    }
    engine::UnrealVector const placed = engine::Apply(frame, offset);
    *atField = {static_cast<float>(eye.x + placed.x), static_cast<float>(eye.y + placed.y), static_cast<float>(eye.z + placed.z)};
    write_journal::Wrote("CockpitProbe.driverLocation", atField, sizeof(*atField));
    *turnField = turn;
    write_journal::Wrote("CockpitProbe.driverRotation", turnField, sizeof(*turnField));
    FlushActor(driver, true);
    ++gSeated.placements;
}

// `seat hip R F U knees R F U R F U heels R F U R F U grips R F U R F U`
std::string DoSeat(std::istringstream& words) {
    SeatLayout layout;
    std::string name;
    int found = 0;
    auto const read = [&](engine::UnrealVector& v) { return static_cast<bool>(words >> v.x >> v.y >> v.z); };
    while (words >> name) {
        bool ok = false;
        if (name == "hip") {
            ok = read(layout.hipM);
        } else if (name == "knees") {
            ok = read(layout.kneeM[0]) && read(layout.kneeM[1]);
        } else if (name == "heels") {
            ok = read(layout.heelM[0]) && read(layout.heelM[1]);
        } else if (name == "grips") {
            ok = read(layout.gripM[0]) && read(layout.gripM[1]);
        } else if (name == "axes") {
            ok = read(layout.axisM[0]) && read(layout.axisM[1]);
            for (engine::UnrealVector& a : layout.axisM) {
                double const length = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
                ok = ok && length > 0.1;
                if (ok) {
                    a = {a.x / length, a.y / length, a.z / length};
                }
            }
            layout.haveAxes = ok;
            if (ok) {
                continue;  // optional: not one of the four
            }
        }
        if (!ok) {
            return "usage: mevr cockpit seat hip R F U knees R F U R F U heels R F U R F U grips R F U R F U [axes R F U R F U] (metres from the "
                   "cockpit's eye: right, forward, up; left then right; the grips are the lever's and the stick's, the axes their handles')";
        }
        ++found;
    }
    if (found != 4) {
        return Refuse("seat: hip, knees, heels and grips are all needed");
    }
    layout.set = true;
    gSeatLayout = layout;
    std::string const report = std::format("seat: the hips at ({:.2f} {:.2f} {:.2f}), the heels at ({:.2f} {:.2f} {:.2f}) and ({:.2f} {:.2f} {:.2f}) m from the eye; "
                                           "Shepard sits there while riding ([Cockpit] seated = {})",
        layout.hipM.x, layout.hipM.y, layout.hipM.z, layout.heelM[0].x, layout.heelM[0].y, layout.heelM[0].z, layout.heelM[1].x, layout.heelM[1].y,
        layout.heelM[1].z, hand_calibration::GetCockpit().seated ? 1 : 0);
    if (layout.haveAxes) {
        log::Info("cockpit probe: seat: the held hands wrap the lever's axis ({:.2f} {:.2f} {:.2f}) and the stick's ({:.2f} {:.2f} {:.2f})",
            layout.axisM[0].x, layout.axisM[0].y, layout.axisM[0].z, layout.axisM[1].x, layout.axisM[1].y, layout.axisM[1].z);
    }
    log::Info("cockpit probe: {}", report);
    return report;
}

// `panel button ...` (the layout above), or `panel [repair|extract]` (game/CockpitPanel's state, or one of its calls for tests).
std::string DoPanel(std::istringstream& words) {
    std::string first;
    std::streampos const start = words.tellg();
    if (!(words >> first) || first != "button") {
        std::string rest;
        std::getline(words, rest);
        return cockpit_panel::Command(first + rest);
    }
    words.seekg(start);
    PanelLayout layout;
    auto const read = [&](engine::UnrealVector& v) { return static_cast<bool>(words >> v.x >> v.y >> v.z); };
    auto const unit = [](engine::UnrealVector& v) {
        double const length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        if (!(length > 0.1)) {
            return false;
        }
        v = {v.x / length, v.y / length, v.z / length};
        return true;
    };
    std::string name;
    int found = 0;
    while (words >> name) {
        bool ok = false;
        if (name == "button") {
            ok = read(layout.buttonM);
        } else if (name == "in") {
            ok = read(layout.buttonInM) && unit(layout.buttonInM);
        } else if (name == "travel") {
            ok = static_cast<bool>(words >> layout.travelM) && layout.travelM > 0.0f;
        } else if (name == "lever") {
            ok = read(layout.hingeM);
        } else if (name == "axis") {
            ok = read(layout.hingeAxisM) && unit(layout.hingeAxisM);
        } else if (name == "grip") {
            ok = read(layout.gripM);
        } else if (name == "pull") {
            ok = static_cast<bool>(words >> layout.pullDeg) && layout.pullDeg > 0.0f;
        } else if (name == "lamps") {
            ok = static_cast<bool>(words >> layout.healthFirst >> layout.healthCount >> layout.progressFirst >> layout.progressCount) &&
                 layout.healthFirst >= 0 && layout.progressFirst >= 0 && layout.healthCount >= 0 && layout.progressCount >= 0 &&
                 layout.healthFirst + layout.healthCount <= 32 && layout.progressFirst + layout.progressCount <= 32;
        }
        if (!ok) {
            return "usage: mevr cockpit panel button R F U in R F U travel M lever R F U axis R F U grip R F U pull DEG lamps FIRST COUNT FIRST COUNT "
                   "(metres from the cockpit's eye: right, forward, up), or mevr cockpit panel [repair|extract]";
        }
        ++found;
    }
    if (found != 8) {
        return Refuse("panel: button, in, travel, lever, axis, grip, pull and lamps are all needed");
    }
    if (std::abs(layout.hingeAxisM.x) < 0.9) {
        return Refuse("panel: the handle's hinge must run along the seat's right (its model swings about it)");
    }
    // The handle is pulled down (its arrows; the user, 2026-11-05): a swing that lifts its grip is the old script's, made before
    // tools/cockpit/panel_controls.py swung it down. The grip's way for + about the axis is axis x (grip - hinge); its up part:
    {
        engine::UnrealVector const& a = layout.hingeAxisM;
        engine::UnrealVector const v{layout.gripM.x - layout.hingeM.x, layout.gripM.y - layout.hingeM.y, layout.gripM.z - layout.hingeM.z};
        double const up = a.x * v.y - a.y * v.x;
        if (up > 0.0) {
            log::Warn("cockpit probe: panel: the handle's swing lifts its grip while the hand pulls it down: an old cockpit.txt "
                      "(tools/cockpit/place.py --script after panel_controls.py's 2026-11-05 swing)");
        }
    }
    layout.set = true;
    gPanelLayout = layout;
    gPanelLamps.component = nullptr;  // the lamps are set afresh
    std::string const report = std::format("panel: the repair button {:.2f} {:.2f} {:.2f} and the extraction handle {:.2f} {:.2f} {:.2f} m from the eye "
                                           "(right, forward, up), a {:.1f} mm press and a {:.0f}-degree pull; {} lamps from element {}, {} squares from {}",
        layout.buttonM.x, layout.buttonM.y, layout.buttonM.z, layout.gripM.x, layout.gripM.y, layout.gripM.z, layout.travelM * 1000.0f, layout.pullDeg,
        layout.healthCount, layout.healthFirst, layout.progressCount, layout.progressFirst);
    log::Info("cockpit probe: {}", report);
    return report;
}

// The stick's tilt as a rotation in the vehicle's frame: its top to the right (roll) and away from the player (pitch down).
engine::Basis StickTilt(mako_controls::Pose const& pose) {
    auto const units = [](float degrees) { return static_cast<int>(std::lround(degrees * 65536.0 / 360.0)); };
    return engine::ToBasis(engine::UnrealRotation{units(-pose.tiltForwardDeg), 0, units(pose.tiltRightDeg)});
}

// A part in the vehicle's frame: its offset from the eye along the vehicle's axes, its yaw on top of the vehicle's rotation.
void PlacePartIn(Part const& part, engine::Basis const& frame, engine::UnrealVector const& eye, double upm) {
    mako_controls::Pose const pose = part.motion != Part::Motion::None ? mako_controls::CurrentPose() : mako_controls::Pose{};
    vr::cockpit_panel::Output const& panel = cockpit_panel::Current();
    engine::UnrealVector offsetM = part.offsetM;
    if (part.motion == Part::Motion::Press && gPanelLayout.set) {  // the cap in by the press
        double const in = panel.pressM;
        offsetM = {offsetM.x + gPanelLayout.buttonInM.x * in, offsetM.y + gPanelLayout.buttonInM.y * in, offsetM.z + gPanelLayout.buttonInM.z * in};
    }
    double const forwardM = offsetM.y + (part.motion == Part::Motion::Lever ? static_cast<double>(pose.lever * pose.travelM) : 0.0);
    bool const stick = part.motion == Part::Motion::Stick;
    // Twisting, the stick turns by its steering; tilting, it turns about its tilted axis by the hand's twist (the user, 2026-11-05:
    // the twist steered, the model never turned). A twist counter-clockwise from above is a negative UE yaw.
    double const yawDeg = part.yawDeg + (!stick ? 0.0 : pose.tilt ? -static_cast<double>(pose.twistDeg) : static_cast<double>(pose.steer * pose.lockDeg));
    engine::UnrealVector const offset = engine::Apply(frame, {forwardM * upm, offsetM.x * upm, offsetM.z * upm});
    engine::Basis tilted = stick && pose.tilt ? engine::Compose(frame, StickTilt(pose)) : frame;
    if (part.motion == Part::Motion::Hinge && gPanelLayout.set && panel.pullDeg != 0.0f) {
        // The hinge runs along the seat's right (the hull's x): a turn about it is a pitch in the vehicle's frame, forward tipping
        // up for + about +x as in the hull's right-handed axes.
        double const degrees = panel.pullDeg * (gPanelLayout.hingeAxisM.x >= 0.0 ? 1.0 : -1.0);
        tilted = engine::Compose(frame, engine::ToBasis(engine::UnrealRotation{static_cast<int>(std::lround(degrees * 65536.0 / 360.0)), 0, 0}));
    }
    engine::UnrealRotation const turned = engine::BasisRotation(engine::Compose(tilted, engine::ToBasis(engine::UnrealRotation{0, static_cast<int>(std::lround(yawDeg * 65536.0 / 360.0)), 0})));
    if (ue3::Vector* const atField = gActorLocation.In<ue3::Vector>(part.actor)) {
        *atField = {static_cast<float>(eye.x + offset.x), static_cast<float>(eye.y + offset.y), static_cast<float>(eye.z + offset.z)};
        write_journal::Wrote("CockpitProbe.rideLocation", atField, sizeof(*atField));
    }
    if (ue3::Rotator* const turnField = gActorRotation.In<ue3::Rotator>(part.actor)) {
        *turnField = {turned.pitch, turned.yaw, turned.roll};
        write_journal::Wrote("CockpitProbe.rideRotation", turnField, sizeof(*turnField));
    }
    FlushActor(part.actor, true);
}

// The game modes the ride holds in, the Mako the pawn: driving (1) and the game's GUI over it (7: the extraction's "Return to
// Normandy?", the pause menu). Out of it in the GUI mode, the eyes saw bare terrain under the question, no cockpit (2026-11-05,
// research/48 section 27); game/WorldUi puts the screen at the cockpit's windshield then.
bool RideMode(std::optional<std::uint8_t> mode) {
    return mode && (*mode == 1 || *mode == 7);
}

// After the camera's update (the vehicle has moved for this frame; the view isn't built yet).
void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    if (!gRide.on) {
        return;
    }
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) {
        return;
    }
    ue3::Object* const pawn = player::Pawn(controller);
    auto const mode = player::GameMode(controller);
    auto const pov = player::CameraPov(camera);
    auto const at = pawn != nullptr ? player::Location(pawn) : std::nullopt;
    auto const turn = pawn != nullptr ? player::Rotation(pawn) : std::nullopt;
    gRide.riding = false;
    if (!ue3::IsA(pawn, gVehicleBase) || !RideMode(mode) || !pov || pov->location == nullptr || pov->rotation == nullptr || !at || !turn) {
        return;
    }
    gRide.chaseLocation = *pov->location;
    gRide.chaseRotation = *pov->rotation;
    gRide.chaseValid = true;
    engine::Basis const frame = engine::ToBasis(engine::UnrealRotation{turn->pitch, turn->yaw, turn->roll});
    double const upm = gAnchor.set ? gAnchor.unitsPerMeter : engine::kUnitsPerMeter;
    engine::UnrealVector const lift = engine::Apply(frame, {gRide.eyeM.y * upm, gRide.eyeM.x * upm, gRide.eyeM.z * upm});
    engine::UnrealVector const eye{at->x + lift.x, at->y + lift.y, at->z + lift.z};
    for (Part const& part : gParts) {
        if (PartAlive(part)) {
            PlacePartIn(part, frame, eye, upm);
            ++gRide.partPlacements;
        }
    }
    if (gSeated.pawn != nullptr && gSeated.vehicle == pawn) {
        PlaceDriver(frame, eye, upm, *turn);
    }
    if (!gRide.seated) {
        // Until the seat is kept it follows the head, so the eye stays on the cockpit's. It's kept once the host's view has
        // published kSeatSamples samples carrying a head offset, or kSeatGiveUp samples in all (a head right at the centre).
        // The first sample after an entry can carry no offset yet: kept at once, it sat the eye 0.34 m forward, inside the
        // console (the game side, 2026-11-05 08:06). Without a view (the first frames after a load, VR off), no seat.
        constexpr int kSeatSamples = 5;
        constexpr int kSeatGiveUp = 90;
        if (!gRide.seatWaiting) {
            gRide.seatWaiting = true;
            gRide.seatSamples = 0;
            gRide.seatPlaced = 0;
        }
        vr::host_view::State const view = vr::host_view::Latest();
        gRide.seat = view.valid ? engine::UnrealVector{view.offsetForward, view.offsetRight, view.offsetUp} : engine::UnrealVector{};
        if (view.valid && view.sequence != gRide.seatSequence) {
            gRide.seatSequence = view.sequence;
            ++gRide.seatSamples;
            gRide.seatPlaced += view.offsetForward != 0.0f || view.offsetRight != 0.0f || view.offsetUp != 0.0f ? 1 : 0;
        }
        if (view.valid && (gRide.seatPlaced >= kSeatSamples || gRide.seatSamples >= kSeatGiveUp)) {
            gRide.seated = true;
            gRide.seatWaiting = false;
            log::Info("cockpit probe: ride: seated with the head {:.2f} m forward, {:.2f} m right, {:.2f} m up of the tracking centre ({} of {} "
                      "samples with a head offset)",
                gRide.seat.x / upm, gRide.seat.y / upm, gRide.seat.z / upm, gRide.seatPlaced, gRide.seatSamples);
        }
    }
    // The renderer puts the head at the camera plus its offset: the camera goes where that lands the seated head on the eye.
    engine::UnrealVector const seat = engine::Apply(frame, gRide.seat);
    *pov->location = {static_cast<float>(eye.x - seat.x), static_cast<float>(eye.y - seat.y), static_cast<float>(eye.z - seat.z)};
    write_journal::Wrote("CockpitProbe.rideCameraLocation", pov->location, sizeof(*pov->location));
    *pov->rotation = {turn->pitch, engine::Unwind(turn->yaw, pov->rotation->yaw), turn->roll};
    write_journal::Wrote("CockpitProbe.rideCameraRotation", pov->rotation, sizeof(*pov->rotation));
    gRide.riding = true;
    gRide.haveEye = true;
    gRide.eyeWorld = eye;
    gRide.frame = frame;
    gRide.upm = upm;
    ++gRide.cameraTicks;
}

// Each mod tick: the vehicle's body hidden while riding, shown again otherwise; the parts owned by the vehicle (the pawn) so the
// eyes see them and the captures don't.
void RideTick() {
    if (!gRide.on && gRide.vehicle == nullptr && gSeated.pawn == nullptr) {
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    bool const inVehicle = gRide.on && ue3::IsA(pawn, gVehicleBase) && RideMode(player::GameMode(controller));
    if (gRide.vehicle != nullptr && (!inVehicle || pawn != gRide.vehicle)) {
        ShowVehicle();
    }
    if (inVehicle && gRide.vehicle == nullptr) {
        gRide.seated = false;  // a new seat with each entry
        // The centre screen's chase camera hangs on the controller's yaw: behind the Mako from the start, then it follows
        // the Mako's turns ([Cockpit] followCamera, game/MakoControls).
        if (hand_calibration::GetCockpit().followCamera) {
            auto const turn = player::Rotation(pawn);
            if (ue3::Rotator* const control = gActorRotation.In<ue3::Rotator>(controller); control != nullptr && turn) {
                control->yaw = engine::Unwind(turn->yaw, control->yaw);
                write_journal::Wrote("CockpitProbe.controlYaw", control, sizeof(*control));
            }
        }
        HideVehicle(pawn);
        HideFromCaptures();  // the owner is now the vehicle
    }
    SeatTick(inVehicle ? pawn : nullptr);
}

// `hud status R F U W radar R F U W panel R F U W`
std::string DoHud(std::istringstream& words) {
    HudLayout layout;
    std::string name;
    int parts = 0;
    while (words >> name) {
        engine::UnrealVector at{};
        float width = 0.0f;
        if (!(words >> at.x >> at.y >> at.z >> width) || !(width > 0.0f)) {
            return "usage: mevr cockpit hud status R F U W radar R F U W panel R F U W (metres from the cockpit's eye: right, forward, up; width)";
        }
        if (name == "status") {
            layout.statusM = at, layout.statusWidthM = width;
        } else if (name == "radar") {
            layout.radarM = at, layout.radarWidthM = width;
        } else if (name == "panel") {
            layout.panelM = at, layout.panelWidthM = width;
        } else {
            return Refuse(std::format("hud: no piece called {}", name));
        }
        ++parts;
    }
    if (parts != 3) {
        return Refuse("hud: status, radar and panel are all needed");
    }
    layout.set = true;
    gHudLayout = layout;
    std::string const report = std::format("hud in the cockpit: status at ({:.2f} {:.2f} {:.2f}) {:.2f} m wide, radar at ({:.2f} {:.2f} {:.2f}) {:.2f} m, the rest at "
                                           "({:.2f} {:.2f} {:.2f}) {:.2f} m",
        layout.statusM.x, layout.statusM.y, layout.statusM.z, layout.statusWidthM, layout.radarM.x, layout.radarM.y, layout.radarM.z, layout.radarWidthM,
        layout.panelM.x, layout.panelM.y, layout.panelM.z, layout.panelWidthM);
    log::Info("cockpit probe: {}", report);
    return report;
}

std::string DoRide(std::istringstream& words) {
    std::string first;
    if (!(words >> first)) {
        ue3::Object* const controller = player::Controller();
        ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
        auto const at = pawn != nullptr ? player::Location(pawn) : std::nullopt;
        auto const turn = pawn != nullptr ? player::Rotation(pawn) : std::nullopt;
        auto const deg = [](int units) { return units * 360.0 / 65536.0; };
        return std::format("ride {}: eye at right {:.2f} forward {:.2f} up {:.2f} m from the vehicle's origin; {} camera ticks moved, {} part placements, riding now {}, {} components hidden\n"
                           "  vehicle at {:.0f} {:.0f} {:.0f}, pitch {:.1f} yaw {:.1f} roll {:.1f}; the game's chase camera at {:.0f} {:.0f} {:.0f}, pitch {:.1f} yaw {:.1f} roll {:.1f}",
            gRide.on ? "on" : "off", gRide.eyeM.x, gRide.eyeM.y, gRide.eyeM.z, gRide.cameraTicks, gRide.partPlacements, gRide.riding, gRide.hidden.size(),
            at ? at->x : 0.0f, at ? at->y : 0.0f, at ? at->z : 0.0f, turn ? deg(turn->pitch) : 0.0, turn ? deg(turn->yaw) : 0.0, turn ? deg(turn->roll) : 0.0,
            gRide.chaseLocation.x, gRide.chaseLocation.y, gRide.chaseLocation.z, deg(gRide.chaseRotation.pitch), deg(gRide.chaseRotation.yaw), deg(gRide.chaseRotation.roll)) +
            std::format("\n  seat: {}{}, {} placements, the hips {}", gSeatLayout.set ? "" : "no layout; ",
                gSeated.pawn != nullptr ? ue3::runtime::FullName(gSeated.pawn) + " shown" : std::string("nobody shown"), gSeated.placements,
                gSeated.haveHips ? std::format("at ({:.1f} {:.1f} {:.1f}) in the mesh", gSeated.hips.x, gSeated.hips.y, gSeated.hips.z) : std::string("not sampled yet"));
    }
    // Three numbers are the eye's place (and on); a lone 0 or `off` stops, a lone 1 or `on` starts.
    engine::UnrealVector eye{};
    std::istringstream again(first);
    bool const placed = static_cast<bool>(again >> eye.x) && static_cast<bool>(words >> eye.y >> eye.z);
    if (first == "seat") {
        gRide.seated = false;
        return "ride: the head's place now becomes the cockpit's eye at the next frame";
    }
    if (!placed && (first == "0" || first == "off")) {
        gRide.on = false;
        ShowVehicle();
        return "ride off: the camera is the game's again, the Mako shown";
    }
    if (!placed && first != "1" && first != "on") {
        return "usage: mevr cockpit ride [on | off | <right m> <forward m> <up m>]";
    }
    if (auto const missing = MissingFunction({&gSetOwnerNoSee, &gForceUpdate, &gUpdateCamera})) {
        return Refuse(*missing);
    }
    if (placed) {
        gRide.eyeM = eye;
    }
    gRide.on = true;
    std::string const report = std::format("ride on: the camera at the cockpit's eye, right {:.2f} forward {:.2f} up {:.2f} m from the vehicle's origin, while the Mako is the pawn",
        gRide.eyeM.x, gRide.eyeM.y, gRide.eyeM.z);
    log::Info("cockpit probe: {}", report);
    return report;
}

// ---- The aim screen -------------------------------------------------------------------------------------------------

// On foot, the stand-in for the Mako's aim: from the head along its view. The capture's centre is the aim point.
void PointAim() {
    if (!vr::screen_reticle::AimCaptureActive(gAim.parked)) return;
    std::optional<Eye> eye = HeadNow();
    if (gRide.riding) {
        // In the Mako: along the turret's aim (game/mako), else straight ahead of the vehicle from about the turret.
        if (auto const turret = mako::CurrentTurretAim()) {
            eye = Eye{turret->from, turret->direction, engine::kUnitsPerMeter};
        } else if (ue3::Object* const vehicle = player::Pawn(player::Controller())) {
            auto const at = player::Location(vehicle);
            auto const turn = player::Rotation(vehicle);
            if (at && turn) {
                engine::Basis const frame = engine::ToBasis(engine::UnrealRotation{turn->pitch, turn->yaw, turn->roll});
                eye = Eye{{at->x + frame.up.x * 150.0, at->y + frame.up.y * 150.0, at->z + frame.up.z * 150.0}, frame.forward, engine::kUnitsPerMeter};
            }
        }
    }
    if (!eye || gAim.captureComponent == nullptr) {
        return;
    }
    if (gAim.pitchDeg != 0.0f) {
        engine::UnrealRotation tilted = engine::ToRotation(eye->forward);
        tilted.pitch += static_cast<int>(gAim.pitchDeg * 65536.0f / 360.0f);
        eye->forward = engine::ToBasis(tilted).forward;
    }
    engine::UnrealRotation const look = engine::ToRotation(eye->forward);
    ue3::Params view = ParamsOf(gCaptureSetView);
    view.Set("NewLocation", ue3::Vector{static_cast<float>(eye->location.x), static_cast<float>(eye->location.y), static_cast<float>(eye->location.z)});
    view.Set("NewRotation", ue3::Rotator{look.pitch, look.yaw, look.roll});
    if (Call(gAim.captureComponent, gCaptureSetView, view) && FlushActor(gAim.captureActor, true)) {
        ++gAim.pointedTicks;
    }
    gAim.lastPointed = Now();
    if (gAim.reticle == nullptr || !Still(gAim.reticle, gActorClass)) {
        return;
    }
    // bOwnerNoSee compares the scene proxy's owner chain with the eye's view actor. Possession and
    // re-entering a parked cockpit can change it after aimreticle ran; reattach after changing it.
    ue3::Object* const controller = player::Controller();
    ue3::Object* const camera = player::Camera(controller);
    ue3::Object* const target = gCameraViewTargetActor.Read<ue3::Object*>(camera).value_or(nullptr);
    ue3::Object* const eyeActor = target != nullptr ? target : player::Pawn(controller);
    // During a level load the camera's view target can still name an actor that's gone: Actor.SetOwner walks the new owner's
    // chain and faulted on it (the owner's QA, 2026-11-07: two crashes loading UNC20 from Resume, black screen with sound).
    if (!Still(eyeActor, gActorClass)) {
        return;
    }
    if (gAim.reticleOwner != eyeActor || gOwnerField.Read<ue3::Object*>(gAim.reticle) != eyeActor ||
        gOwnerNoSee.ReadBool(gAim.reticleComponent) != !gAim.showInEyes) {
        SetOwnerOf(gAim.reticle, eyeActor);
        ue3::Params noSee = ParamsOf(gSetOwnerNoSee);
        noSee.SetBool("bNewOwnerNoSee", !gAim.showInEyes);
        Call(gAim.reticleComponent, gSetOwnerNoSee, noSee);
        FlushActor(gAim.reticle, false);
        gAim.reticleOwner = eyeActor;
    }
    // Where the aim meets the world (Actor.Trace on the pawn, which it ignores; our parts have no collision), else 200 m out;
    // the reticle just short of it, square to the aim, its width `reticleDeg` as the capture sees it.
    // On a miss the reticle sits at most 50 m out: the trace passes through background meshes without collision (2026-11-03,
    // Eden Prime's far buildings), and a reticle beyond them is hidden behind them.
    double const kRangeUnits = static_cast<double>(gAim.rangeM) * engine::kUnitsPerMeter;
    double distance = std::min(kRangeUnits, 50.0 * engine::kUnitsPerMeter);
    ue3::Object* const pawn = player::Pawn(player::Controller());
    if (pawn != nullptr && gTrace.Get() != nullptr) {
        engine::UnrealVector const& o = eye->location;
        engine::UnrealVector const& f = eye->forward;
        // The actor hit up to `length` along the aim, and where if the native's out parameter came back (it may not, as
        // game/sights found): then bisect with the hit/no-hit answer.
        auto const traceTo = [&](double length, ue3::Vector* hit) {
            ue3::Params trace = ParamsOf(gTrace);
            trace.Set("TraceEnd", ue3::Vector{static_cast<float>(o.x + f.x * length), static_cast<float>(o.y + f.y * length), static_cast<float>(o.z + f.z * length)});
            trace.Set("TraceStart", ue3::Vector{static_cast<float>(o.x), static_cast<float>(o.y), static_cast<float>(o.z)});
            trace.SetBool("bTraceActors", true);
            trace.Set("Extent", ue3::Vector{gAim.traceExtent, gAim.traceExtent, gAim.traceExtent});
            bool const called = Call(pawn, gTrace, trace);
            ue3::Object* const what = trace.Return<ue3::Object*>().value_or(nullptr);
            ue3::Vector const where = trace.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{});
            if (hit != nullptr) {
                *hit = where;
                gAim.traceNote = std::format("called {}, hit {}, HitLocation ({:.0f} {:.0f} {:.0f}), from ({:.0f} {:.0f} {:.0f}) along ({:.2f} {:.2f} {:.2f}) by {}",
                    called, ue3::runtime::FullName(what), where.x, where.y, where.z, o.x, o.y, o.z, f.x, f.y, f.z, ue3::runtime::FullName(pawn));
            }
            return called && what != nullptr;
        };
        ue3::Vector at{};
        if (traceTo(kRangeUnits, &at)) {
            double const along = (at.x - o.x) * f.x + (at.y - o.y) * f.y + (at.z - o.z) * f.z;
            if ((at.x != 0.0f || at.y != 0.0f || at.z != 0.0f) && along > 0.0 && along <= kRangeUnits) {
                distance = along;
            } else {
                double lo = 0.0;
                double hi = 1.0;
                for (int i = 0; i < 10; ++i) {
                    double const mid = 0.5 * (lo + hi);
                    (traceTo(kRangeUnits * mid, nullptr) ? hi : lo) = mid;
                }
                distance = kRangeUnits * hi;
            }
        }
    }
    gAim.traceUnits = distance;
    double const placed = std::max(distance - 10.0, 40.0);
    if (ue3::Vector* const atField = gActorLocation.In<ue3::Vector>(gAim.reticle)) {
        *atField = {static_cast<float>(eye->location.x + eye->forward.x * placed), static_cast<float>(eye->location.y + eye->forward.y * placed),
            static_cast<float>(eye->location.z + eye->forward.z * placed)};
        write_journal::Wrote("CockpitProbe.reticleLocation", atField, sizeof(*atField));
    }
    if (ue3::Rotator* const turnField = gActorRotation.In<ue3::Rotator>(gAim.reticle)) {
        *turnField = {look.pitch, look.yaw, look.roll};  // the mesh faces +X: along the aim, two-sided
        write_journal::Wrote("CockpitProbe.reticleRotation", turnField, sizeof(*turnField));
    }
    constexpr double kRadiansPerDegree = 3.14159265358979323846 / 180.0;
    ue3::Params scale = ParamsOf(gSetDrawScale);
    // `reticleDeg` is its width at the screen's 30-degree field; zoomed (R3) it keeps its size on the screen.
    double const reticleDeg = static_cast<double>(gAim.reticleDeg) * gAim.fovDeg / 30.0;
    scale.Set("NewScale", static_cast<float>(2.0 * placed * std::tan(0.5 * reticleDeg * kRadiansPerDegree) / gAim.reticleWidthUnits));
    Call(gAim.reticle, gSetDrawScale, scale);
    FlushActor(gAim.reticle, true);
}

// `aimreticle [DEG]`: the package's Aim_Reticle at the aim point, owned by the pawn with bOwnerNoSee: the captures see it, the
// eyes (whose view actor is the pawn) don't; the hand reticle is theirs.
std::string DoAimReticle(std::istringstream& words) {
    if (auto const missing = MissingFunction({&gSpawn, &gSetStaticMesh, &gSetDrawScale, &gSetOwnerNoSee, &gTrace})) {
        return Refuse(*missing);
    }
    float deg = gAim.reticleDeg;
    bool eyes = false;  // `eyes`: the eyes see it too (to find it)
    for (std::string word; words >> word;) {
        if (word == "eyes") {
            eyes = true;
        } else if (word == "pitch") {
            if (!(words >> gAim.pitchDeg) || gAim.pitchDeg < -89.0f || gAim.pitchDeg > 89.0f) {
                return "usage: mevr cockpit aimreticle ... pitch <degrees>";
            }
        } else if (word == "range") {
            if (!(words >> gAim.rangeM) || !(gAim.rangeM >= 1.0f) || gAim.rangeM > 2000.0f) {
                return "usage: mevr cockpit aimreticle ... range <metres>";
            }
        } else if (word == "extent") {
            if (!(words >> gAim.traceExtent) || gAim.traceExtent < 0.0f || gAim.traceExtent > 100.0f) {
                return "usage: mevr cockpit aimreticle ... extent <half size in units>";
            }
        } else if (std::from_chars(word.data(), word.data() + word.size(), deg).ec != std::errc{} || !(deg > 0.1f && deg < 60.0f)) {
            return "usage: mevr cockpit aimreticle [width in degrees, as the aim capture sees it] [eyes]";
        }
    }
    gAim.reticleDeg = deg;
    gAim.showInEyes = eyes;
    if (gAim.captureActor == nullptr) {
        return Refuse("no aim screen: mevr cockpit aimscreen first");
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (pawn == nullptr) {
        return Refuse("no pawn");
    }
    if (gAim.reticle == nullptr) {
        constexpr char const* kPath = "BIOG_MEVR_Cockpit.Aim_Reticle";
        ue3::Object* mesh = FindMesh(kPath);
        if (mesh == nullptr || Lower(R().PathName(mesh)) != Lower(kPath)) {
            mesh = LoadObject(kPath, gStaticMesh.Get());
        }
        if (!ue3::IsA(mesh, gStaticMesh)) {
            return Refuse(std::format("no StaticMesh {} (a package from b486139 on)", kPath));
        }
        ue3::Vector const at = player::Location(pawn).value_or(ue3::Vector{});
        for (std::size_t i = 0; i < kAutomatic && gAim.reticle == nullptr; ++i) {
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
            gAim.reticle = actor;
            gAim.reticleComponent = component;
        }
        if (gAim.reticle == nullptr) {
            return Refuse("no actor could be spawned for the reticle");
        }
        ue3::Params set = ParamsOf(gSetStaticMesh);
        set.Set("NewMesh", mesh);
        set.SetBool("bForce", true);
        Call(gAim.reticleComponent, gSetStaticMesh, set);
    }
    SetOwnerOf(gAim.reticle, pawn);
    ue3::Params noSee = ParamsOf(gSetOwnerNoSee);
    noSee.SetBool("bNewOwnerNoSee", !eyes);
    Call(gAim.reticleComponent, gSetOwnerNoSee, noSee);
    FlushActor(gAim.reticle, false);
    PointAim();
    std::string const report = std::format("aim reticle {} at the aim point ({:.1f} m out), {:.1f} degrees wide in the aim picture; {}; the trace: {}",
        ue3::runtime::FullName(gAim.reticle), gAim.traceUnits / engine::kUnitsPerMeter, gAim.reticleDeg, eyes ? "the eyes see it too" : "hidden from the eyes", gAim.traceNote);
    log::Info("cockpit probe: {}", report);
    return report;
}

void ClearAim(char const* why) {
    if (Still(gAim.captureComponent, gCaptureComponent)) {
        ue3::Params off = ParamsOf(gCaptureSetEnabled);
        off.SetBool("bEnable", false);
        Call(gAim.captureComponent, gCaptureSetEnabled, off);
    }
    if (Still(gAim.captureActor, gActorClass)) {
        DestroyActor(gAim.captureActor, why);
    }
    if (Still(gAim.reticle, gActorClass)) {
        DestroyActor(gAim.reticle, why);
    }
    gAim = AimScreen{};
}

// `aimscreen [W H] [fov DEG]`: the screen part's slot `gAim.element` (Hull_Console's right screen) shows a capture along the aim.
std::string DoAimScreen(std::istringstream& words) {
    if (auto const missing = MissingFunction({&gCreateTarget, &gCaptureSetEnabled, &gCaptureSetFrameRate, &gCaptureSetView, &gCaptureSetParameters, &gForceUpdate,
            &gGetMaterial, &gSetMaterial, &gCreateMic, &gSetParent, &gSetTextureParameter})) {
        return Refuse(*missing);
    }
    if (!gState.part || gState.component == nullptr) {
        return Refuse("no screen part: mevr cockpit screenpart <Package.Object> first");
    }
    int width = gAim.width;
    int height = gAim.height;
    float fov = gAim.fovDeg;
    for (std::string word; words >> word;) {
        if (word == "fov") {
            if (!(words >> fov) || !(fov > 1.0f) || fov > 170.0f) {
                return "usage: mevr cockpit aimscreen [W H] [fov DEG] [slot N]";
            }
            continue;
        }
        if (word == "slot") {  // another of the part's slots (the centre's, 1, to look at the picture)
            if (!(words >> gAim.element) || gAim.element < 0 || gAim.element > 15) {
                return "usage: mevr cockpit aimscreen [W H] [fov DEG] [slot N]";
            }
            continue;
        }
        std::istringstream size(word);
        int w = 0;
        int h = 0;
        if (!(size >> w && words >> h && w >= 16 && h >= 16 && w <= 2048 && h <= 2048)) {
            return "usage: mevr cockpit aimscreen [W H] [fov DEG]";
        }
        width = w;
        height = h;
    }
    ue3::Object* const controller = player::Controller();
    if (controller == nullptr) {
        return Refuse("no player controller");
    }
    if (gAim.target == nullptr || gAim.width != width || gAim.height != height) {
        ue3::Object* const target = CreateTarget(width, height, true);
        if (target == nullptr) {
            return Refuse("TextureRenderTarget2D.Create returned no render target: nothing changed");
        }
        gAim.target = target;
        gAim.width = width;
        gAim.height = height;
    }
    gAim.fovDeg = fov;
    if (gAim.captureActor == nullptr) {
        ue3::Object* const actor = SpawnActor(controller, gCaptureActor.Get(), player::Location(controller).value_or(ue3::Vector{}));
        ue3::Object* const component = ue3::IsA(actor, gCaptureActor) ? gSceneCapture.Read<ue3::Object*>(actor).value_or(nullptr) : nullptr;
        if (!ue3::IsA(component, gCaptureComponent)) {
            if (actor != nullptr) {
                DestroyActor(actor, "no capture component");
            }
            return Refuse("Actor.Spawn returned no SceneCapture2DActor with a capture component");
        }
        gAim.captureActor = actor;
        gAim.captureComponent = component;
        gSkipUsersOccluded.WriteBool(component, false);
        gSkipOwnerOccluded.WriteBool(component, false);
        WriteField(gMaxUpdateDist, component, kMaxUpdateDist, "CockpitProbe.AimMaxUpdateDist");
        WriteField(gMaxViewDistance, component, static_cast<float>(gSettings.viewDistM * engine::kUnitsPerMeter), "CockpitProbe.AimMaxViewDistance");
        gUpdateMatrices.WriteBool(component, false);
        gEnablePostProcess.WriteBool(component, false);  // as `post 0`: the eye's own post is enough for a screen in the world
        gUseMainPostProcess.WriteBool(component, false);
        WriteField(gPostProcess, component, static_cast<ue3::Object*>(nullptr), "CockpitProbe.AimPostProcess");
    }
    ue3::Params parameters = ParamsOf(gCaptureSetParameters);
    parameters.Set("NewTextureTarget", gAim.target);
    parameters.Set("NewFOV", fov);
    parameters.Set("NewNearPlane", 20.0f);
    parameters.Set("NewFarPlane", 0.0f);
    Call(gAim.captureComponent, gCaptureSetParameters, parameters);
    ue3::Params rate = ParamsOf(gCaptureSetFrameRate);
    rate.Set("NewFrameRate", gSettings.captureRate);
    Call(gAim.captureComponent, gCaptureSetFrameRate, rate);
    WriteField(gFrameRate, gAim.captureComponent, gSettings.captureRate, "CockpitProbe.AimFrameRate");
    if (!gCaptureEnabled.ReadBool(gAim.captureComponent).value_or(false)) {
        ue3::Params on = ParamsOf(gCaptureSetEnabled);
        on.SetBool("bEnable", true);
        Call(gAim.captureComponent, gCaptureSetEnabled, on);
    }
    PointAim();
    FlushActor(gAim.captureActor, false);
    // Our instance over the slot's own (Hull_Console_Aim_MIC), its texture parameter the render target. As EnsureMic: the
    // slot gets the plain Material under it first, so CreateAndSetMaterialInstanceConstant makes a new instance.
    if (gAim.mic == nullptr || !ue3::runtime::IsAlive(gAim.mic)) {
        ue3::Params get = ParamsOf(gGetMaterial);
        get.Set("ElementIndex", gAim.element);
        Call(gState.component, gGetMaterial, get);
        ue3::Object* const own = get.Return<ue3::Object*>().value_or(nullptr);
        MaterialRow const row = own != nullptr ? Analyze(own) : MaterialRow{};
        std::optional<ue3::Name> const parameter = row.object != nullptr ? PickParameter(row) : std::nullopt;
        if (row.base == nullptr || !parameter) {
            return Refuse(std::format("slot {} shows {}, which has no texture parameter to put the picture on", gAim.element, ue3::runtime::FullName(own)));
        }
        ue3::Params plain = ParamsOf(gSetMaterial);
        plain.Set("ElementIndex", gAim.element);
        plain.Set("Material", row.base);
        Call(gState.component, gSetMaterial, plain);
        ue3::Params create = ParamsOf(gCreateMic);
        create.Set("ElementIndex", gAim.element);
        Call(gState.component, gCreateMic, create);
        ue3::Object* const mic = create.Return<ue3::Object*>().value_or(nullptr);
        if (!ue3::IsA(mic, gMic)) {
            return Refuse(std::format("CreateAndSetMaterialInstanceConstant returned {}", ue3::runtime::FullName(mic)));
        }
        ue3::Params parent = ParamsOf(gSetParent);
        parent.Set("NewParent", own);
        Call(mic, gSetParent, parent);
        gAim.mic = mic;
        ue3::Params value = ParamsOf(gSetTextureParameter);
        value.Set("ParameterName", *parameter);
        value.Set("Value", gAim.target);
        Call(mic, gSetTextureParameter, value);
    }
    FlushActor(gState.actor, false);
    HideFromCaptures();
    std::string const report = std::format("aim screen: {} on slot {} of {}, a {}x{} FloatRGB capture {} at {:.0f} degrees, {:.0f} renders a second at most; the cockpit {} the captures",
        ue3::runtime::FullName(gAim.mic), gAim.element, ue3::runtime::FullName(gState.mesh), width, height, ue3::runtime::FullName(gAim.captureActor), fov,
        gSettings.captureRate, gCaptureHide != 0 ? std::format("is kept out of (capturehide {})", gCaptureHide) : std::string("shows in"));
    log::Info("cockpit probe: {}", report);
    return report;
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
    char const* const mode = gState.actor == nullptr ? "-" : gState.part ? "in the parts' frame" : gState.attached ? "attached" : gSettings.follow ? "following the head" : "still";
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
    "mevr cockpit: ride [on|off|RIGHT FORWARD UP] | load <Package.Object> [class] | mesh [name or Package.Object] | part <Package.Object> RIGHT FORWARD UP YAW M_PER_UNIT | parts [clear|sweep] | screenpart <Package.Object> | variant lit|glow|alt|base | loaded <Package.Object> ... | aimscreen [W H] [fov DEG] | aimreticle [DEG] | capturehide [0|1|2|3] | anchor | list [filter] | classes | spawnclass [name] | screen [W H] [float|rgba8] [linear] | materials [filter] | material [name] | param [name] | "
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

std::vector<ue3::Object*> gFoundPawns;  // `mevr vehicle pawns`' list, for `hit`

// `mevr vehicle guns` (measuring for research/47 "The cannon over the cabin"): the ridden Mako's weapons (its behavior's
// m_aVehicleWeaponInfo), each with its turret index, its actor's place and each primitive component's from the cockpit's eye
// (right, forward, up in metres along the vehicle's axes), whether the eyes can see it (HiddenGame, bOwnerNoSee,
// bOnlyOwnerSee, its owner), and the shots' last start from the eye.
ue3::FieldRef gGunsInfo{"SFXGame.BioVehicleBehaviorBase", "m_aVehicleWeaponInfo"};
ue3::FieldRef gGunsInfoWeapon{gGunsInfo, "oVehicleWeaponPtr"};
ue3::FieldRef gGunsInfoTurret{gGunsInfo, "nTurretIndex"};
ue3::FieldRef gGunsLocalToWorld{"Engine.PrimitiveComponent", "LocalToWorld"};
ue3::FunctionRef gGunsMuzzle{"SFXGame.BioWeaponRanged.GetMuzzleLoc"};

engine::UnrealVector ToUnreal(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}

std::string FromEye(engine::UnrealVector const& at) {
    if (!gRide.haveEye) {
        return std::format("({:.0f}, {:.0f}, {:.0f}) in the world (no cockpit eye)", at.x, at.y, at.z);
    }
    engine::UnrealVector const d{at.x - gRide.eyeWorld.x, at.y - gRide.eyeWorld.y, at.z - gRide.eyeWorld.z};
    auto const along = [&](engine::UnrealVector const& axis) { return (d.x * axis.x + d.y * axis.y + d.z * axis.z) / gRide.upm; };
    return std::format("right {:+.2f} forward {:+.2f} up {:+.2f} m from the eye", along(gRide.frame.right), along(gRide.frame.forward),
        along(gRide.frame.up));
}

std::string Guns() {
    ue3::Object* const vehicle = gRide.vehicle != nullptr && ue3::runtime::IsAlive(gRide.vehicle) ? gRide.vehicle : nullptr;
    if (vehicle == nullptr) {
        return "mevr vehicle guns: not riding in a vehicle (the cockpit's ride)";
    }
    ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(vehicle).value_or(nullptr);
    auto const infos = behavior != nullptr ? gGunsInfo.ElementsOf(behavior) : std::nullopt;
    if (!infos) {
        return "mevr vehicle guns: the vehicle's behavior or its weapons' info can't be read";
    }
    std::string out = std::format("{}: {} weapon{}; the vehicle's origin {}", ue3::runtime::FullName(vehicle), infos->count, infos->count == 1 ? "" : "s",
        player::Location(vehicle) ? FromEye(ToUnreal(*player::Location(vehicle))) : std::string("?"));
    for (std::int32_t i = 0; i < infos->count; ++i) {
        ue3::Object* const weapon = gGunsInfoWeapon.Read<ue3::Object*>(infos->At(i)).value_or(nullptr);
        int const turret = gGunsInfoTurret.Read<std::int32_t>(infos->At(i)).value_or(-1);
        out += std::format("\n[{}] turret {} {}", i, turret, ue3::runtime::FullName(weapon));
        if (weapon == nullptr || !ue3::runtime::IsAlive(weapon)) {
            continue;
        }
        if (auto const at = player::Location(weapon)) {
            out += std::format(": actor at {}", FromEye(ToUnreal(*at)));
        }
        if (gGunsMuzzle.Get() != nullptr) {
            ue3::Params p = ParamsOf(gGunsMuzzle);
            Call(weapon, gGunsMuzzle, p);
            if (auto const muzzle = p.Return<ue3::Vector>()) {
                out += std::format("; GetMuzzleLoc {}", FromEye(ToUnreal(*muzzle)));
            }
        }
        auto const components = gAllComponents.ElementsOf(weapon);
        for (std::int32_t c = 0; components && c < components->count; ++c) {
            ue3::Object* const component = *static_cast<ue3::Object**>(components->At(c));
            if (!ue3::IsA(component, gPrimitiveComponent)) {
                continue;
            }
            std::string where = "?";
            if (float const* const m = gGunsLocalToWorld.In<float>(component)) {
                where = FromEye({m[12], m[13], m[14]});
            }
            out += std::format("\n    {} at {}; HiddenGame {}, bOwnerNoSee {}, bOnlyOwnerSee {}, owner {}", R().ClassOf(component) != nullptr
                    ? R().NameText(R().ClassOf(component)) : std::string("?"), where, gHiddenGame.ReadBool(component).value_or(false) ? 1 : 0,
                gOwnerNoSee.ReadBool(component).value_or(false) ? 1 : 0, gOnlyOwnerSee.ReadBool(component).value_or(false) ? 1 : 0,
                ue3::runtime::FullName(gOwnerField.Read<ue3::Object*>(weapon).value_or(nullptr)));
        }
    }
    // The vehicle mesh's turret: every bone and socket whose name holds turret, cannon, gun, barrel or muzzle, with its parent.
    if (ue3::Object* const mesh = gPawnMesh.Read<ue3::Object*>(vehicle).value_or(nullptr)) {
        auto const lower = [](std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text;
        };
        auto const wanted = [&](std::string const& name) {
            std::string const l = lower(name);
            return l.find("turret") != std::string::npos || l.find("cannon") != std::string::npos || l.find("gun") != std::string::npos ||
                l.find("barrel") != std::string::npos || l.find("muzzle") != std::string::npos;
        };
        std::optional<engine::skeleton::RefSkeleton> const skeleton = engine::skeleton::ReadRefSkeleton(mesh);
        std::span<engine::skeleton::BoneAtom> const pose = engine::skeleton::SpaceBases(mesh);
        ik::Affine const toWorld = engine::skeleton::LocalToWorld(mesh);
        {
            ue3::Params count = ParamsOf(gGetNumElements);
            Call(mesh, gGetNumElements, count);
            int const elements = count.Return<std::int32_t>().value_or(0);
            out += std::format("\nthe vehicle mesh's {} material element{}:", elements, elements == 1 ? "" : "s");
            for (int i = 0; i < elements; ++i) {
                ue3::Params get = ParamsOf(gGetMaterial);
                get.Set("ElementIndex", i);
                Call(mesh, gGetMaterial, get);
                out += std::format(" [{}] {}", i, R().PathName(get.Return<ue3::Object*>().value_or(nullptr)));
            }
        }
        if (skeleton && pose.size() == skeleton->names.size()) {
            out += std::format("\nthe vehicle mesh {}: {} bones", ue3::runtime::FullName(mesh), skeleton->names.size());
            for (std::size_t b = 0; b < skeleton->names.size(); ++b) {
                if (!wanted(skeleton->names[b])) {
                    continue;
                }
                ik::Transform const local = engine::skeleton::ToTransform(pose[b]);
                ik::Vec3 const world = ik::TransformPoint(toWorld, local.translation);
                int const parent = skeleton->parents[b];
                out += std::format("\n  bone {} ({}, parent {}) at {}", skeleton->names[b], b,
                    parent >= 0 ? skeleton->names[static_cast<std::size_t>(parent)] : std::string("none"), FromEye({world.x, world.y, world.z}));
            }
            for (engine::skeleton::NamedSocket const& socket : engine::skeleton::Sockets(mesh)) {
                std::string const name = ue3::runtime::NameText(socket.name);
                if (!wanted(name) || socket.socket.bone < 0 || static_cast<std::size_t>(socket.socket.bone) >= pose.size()) {
                    continue;
                }
                ik::Transform const bone = engine::skeleton::ToTransform(pose[static_cast<std::size_t>(socket.socket.bone)]);
                ik::Vec3 const inComponent = ik::Apply(bone, socket.socket.relative.translation);
                ik::Vec3 const world = ik::TransformPoint(toWorld, inComponent);
                out += std::format("\n  socket {} on {} at {}", name, skeleton->names[static_cast<std::size_t>(socket.socket.bone)],
                    FromEye({world.x, world.y, world.z}));
            }
        } else {
            out += "\nthe vehicle mesh's skeleton or pose can't be read";
        }
    }
    // Every component of the vehicle (the muzzle flashes, the lights...): its class, where it is, and how it hangs on the mesh.
    if (auto const components = gAllComponents.ElementsOf(vehicle)) {
        ue3::Object* const mesh = gPawnMesh.Read<ue3::Object*>(vehicle).value_or(nullptr);
        std::optional<engine::skeleton::RefSkeleton> const skeleton = mesh != nullptr ? engine::skeleton::ReadRefSkeleton(mesh) : std::nullopt;
        out += std::format("\nthe vehicle's {} components:", components->count);
        for (std::int32_t c = 0; c < components->count; ++c) {
            ue3::Object* const component = *static_cast<ue3::Object**>(components->At(c));
            if (component == nullptr || !ue3::runtime::IsAlive(component)) {
                continue;
            }
            std::string line = std::format("\n  {} {}", R().ClassOf(component) != nullptr ? R().NameText(R().ClassOf(component)) : std::string("?"),
                R().NameText(component));
            if (ue3::IsA(component, gPrimitiveComponent)) {
                if (float const* const m = gGunsLocalToWorld.In<float>(component)) {
                    line += " at " + FromEye({m[12], m[13], m[14]});
                }
                line += std::format(", hidden {} ownerNoSee {}", gHiddenGame.ReadBool(component).value_or(false) ? 1 : 0,
                    gOwnerNoSee.ReadBool(component).value_or(false) ? 1 : 0);
            }
            if (mesh != nullptr && component != mesh) {
                if (auto const attachment = engine::skeleton::FindAttachment(mesh, component)) {
                    std::string const bone = skeleton && attachment->bone >= 0 && static_cast<std::size_t>(attachment->bone) < skeleton->names.size()
                        ? skeleton->names[static_cast<std::size_t>(attachment->bone)] : std::string("?");
                    line += std::format(", on bone {} at ({:.1f}, {:.1f}, {:.1f})", bone, attachment->relative.translation.x,
                        attachment->relative.translation.y, attachment->relative.translation.z);
                }
            }
            out += line;
        }
    }
    if (auto const start = mako::LastShotStart()) {
        out += std::format("\nthe shots' last start: {}", FromEye(*start));
    } else {
        out += "\nno shot start in the last 5 s (`mevr vehicle fire` first)";
    }
    log::Info("mevr vehicle guns: {}", out);
    return out;
}

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
    if (verb == "size") {
        // The vehicle's own mesh, unscaled (its local axes) times every scale on the way: the body's size to compare the
        // cockpit with (research/48 section 22). The world box alone grows with the vehicle's yaw.
        if (target == nullptr) {
            return Refuse(std::format("no vehicle in {}", MapName(controller)));
        }
        ue3::Object* const mesh = gPawnMesh.Read<ue3::Object*>(target).value_or(nullptr);
        if (mesh == nullptr) {
            return Refuse(std::format("{} has no Mesh", ue3::runtime::FullName(target)));
        }
        ue3::Params p = ParamsOf(gGetUnscaledBounds);
        Call(mesh, gGetUnscaledBounds, p);
        auto const local = p.Get<BoxSphereBounds>("UnscaledBounds");
        auto const world = gBounds.Read<BoxSphereBounds>(mesh);
        float const actorScale = gDrawScale.Read<float>(target).value_or(1.0f);
        ue3::Vector const actorScale3D = gDrawScale3D.Read<ue3::Vector>(target).value_or(ue3::Vector{1.0f, 1.0f, 1.0f});
        float const componentScale = gComponentScale.Read<float>(mesh).value_or(1.0f);
        ue3::Vector const componentScale3D = gComponentScale3D.Read<ue3::Vector>(mesh).value_or(ue3::Vector{1.0f, 1.0f, 1.0f});
        auto const rotation = gActorRotation.Read<ue3::Rotator>(target);
        float const upm = static_cast<float>(engine::kUnitsPerMeter);
        std::string report = std::format("{} ({}), yaw {:.1f} deg; scales: actor {:.3f} x ({:.3f} {:.3f} {:.3f}), mesh {:.3f} x ({:.3f} {:.3f} {:.3f})\n",
            ue3::runtime::FullName(target), ue3::runtime::FullName(mesh), rotation ? rotation->yaw * 360.0 / 65536.0 : 0.0, actorScale, actorScale3D.x,
            actorScale3D.y, actorScale3D.z, componentScale, componentScale3D.x, componentScale3D.y, componentScale3D.z);
        if (local) {
            float const sx = actorScale * actorScale3D.x * componentScale * componentScale3D.x;
            float const sy = actorScale * actorScale3D.y * componentScale * componentScale3D.y;
            float const sz = actorScale * actorScale3D.z * componentScale * componentScale3D.z;
            report += std::format("the mesh's own box: {:.2f} x {:.2f} x {:.2f} m (its X, Y, Z: full sizes, scaled), centre ({:.1f} {:.1f} {:.1f}) units unscaled\n",
                2.0f * local->extent.x * sx / upm, 2.0f * local->extent.y * sy / upm, 2.0f * local->extent.z * sz / upm, local->origin.x, local->origin.y, local->origin.z);
        } else {
            report += "the mesh's own box can't be read\n";
        }
        if (world) {
            report += std::format("the world box: {:.2f} x {:.2f} x {:.2f} m (world X, Y, Z)", 2.0f * world->extent.x / upm, 2.0f * world->extent.y / upm,
                2.0f * world->extent.z / upm);
        }
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "levels") {
        // The persistent level's streaming levels: which are loaded and shown (a direct `open` of a Mako level stalls
        // before the rover drop, research/47 "Reaching the Mako").
        ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
        auto const levels = gStreamingLevels.ElementsOf(world);
        if (!levels) {
            return Refuse(std::format("no streaming levels in {}", MapName(controller)));
        }
        std::string report = std::format("{}: {} streaming levels (loaded / should load / visible / should show / load pending)\n", MapName(controller), levels->count);
        for (std::int32_t i = 0; i < levels->count; ++i) {
            ue3::Object* const level = *static_cast<ue3::Object**>(levels->At(i));
            if (level == nullptr) {
                continue;
            }
            auto const name = gStreamPackage.Read<ue3::Name>(level);
            auto flag = [&](ue3::FieldRef& field) { return field.ReadBool(level).value_or(false) ? '1' : '0'; };
            report += std::format("  {} {} {} {} {} {}\n", name ? ue3::runtime::NameText(*name) : std::string("?"),
                gStreamLoaded.Read<ue3::Object*>(level).value_or(nullptr) != nullptr ? '1' : '0', flag(gStreamShouldLoad), flag(gStreamVisible), flag(gStreamShouldShow),
                flag(gStreamPending));
        }
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "trace") {
        // Straight down through the ground under the pawn, from 3 m above it to 50 m below: every script trace should hit.
        if (auto const missing = MissingFunction({&gTrace, &gFastTrace})) {
            return Refuse(*missing);
        }
        auto const at = pawn != nullptr ? player::Location(pawn) : std::nullopt;
        if (!at) {
            return Refuse("no pawn");
        }
        ue3::Vector const start{at->x, at->y, at->z + 300.0f};
        ue3::Vector const end{at->x, at->y, at->z - 5000.0f};
        ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
        std::string report = std::format("traces from ({:.0f} {:.0f} {:.0f}) down 53 m:", start.x, start.y, start.z);
        for (ue3::Object* const source : {pawn, controller, world}) {
            for (bool const actors : {false, true}) {
                ue3::Params trace = ParamsOf(gTrace);
                trace.Set("TraceEnd", end);
                trace.Set("TraceStart", start);
                trace.SetBool("bTraceActors", actors);
                bool const called = Call(source, gTrace, trace);
                ue3::Vector const hit = trace.Get<ue3::Vector>("HitLocation").value_or(ue3::Vector{});
                report += std::format("\n  Trace by {} (actors {}): called {}, returned {}, HitLocation ({:.0f} {:.0f} {:.0f})", R().NameText(source), actors, called,
                    ue3::runtime::FullName(trace.Return<ue3::Object*>().value_or(nullptr)), hit.x, hit.y, hit.z);
            }
            ue3::Params fast = ParamsOf(gFastTrace);
            fast.Set("TraceEnd", end);
            fast.Set("TraceStart", start);
            bool const called = Call(source, gFastTrace, fast);
            ue3::Params up = ParamsOf(gFastTrace);
            up.Set("TraceEnd", ue3::Vector{start.x, start.y, start.z + 5000.0f});
            up.Set("TraceStart", start);
            Call(source, gFastTrace, up);
            report += std::format("\n  FastTrace by {}: called {}, down returned {}, up into the sky {} (true: nothing in the way)", R().NameText(source), called,
                fast.ReturnBool().value_or(false), up.ReturnBool().value_or(false));
        }
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "playersonly") {
        // A direct `open` of a Mako level can leave the world frozen but for the players (the squad screen puts back whatever
        // it found, research/47): nothing but the player ticks and BioExitVehicle refuses. `playersonly 0` clears it.
        std::string value;
        words >> value;
        ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
        if (world == nullptr) {
            return Refuse("no WorldInfo");
        }
        if (value == "0" || value == "1") {
            bool const on = value == "1";
            gPlayersOnly.WriteBool(world, on);
            gPlayersOnlyPending.WriteBool(world, on);
            if (void* const field = gPlayersOnly.Address(world)) {
                write_journal::Wrote("CockpitProbe.bPlayersOnly", field, 4);
            }
        }
        std::string const report = std::format("WorldInfo.bPlayersOnly {}, bPlayersOnlyPending {}", gPlayersOnly.ReadBool(world).value_or(false) ? 1 : 0,
            gPlayersOnlyPending.ReadBool(world).value_or(false) ? 1 : 0);
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "why") {
        if (!ue3::IsA(pawn, gVehicleBase)) {
            return Refuse("the pawn isn't a vehicle");
        }
        ue3::Object* const driver = gVehicleDriver.Read<ue3::Object*>(pawn).value_or(nullptr);
        ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
        std::string report = std::format("driver {} (health {}); WorldInfo.bPlayersOnly {}; ", ue3::runtime::FullName(driver),
            driver != nullptr ? std::to_string(gPawnHealth.Read<std::int32_t>(driver).value_or(-1)) : std::string("-"),
            world != nullptr ? (gPlayersOnly.ReadBool(world).value_or(false) ? "1" : "0") : "?");
        if (driver != nullptr) {
            // Where the game keeps Shepard while driving (item 7 of docs/mako-cockpit-goal.md): hidden, based on what, where.
            auto const at = player::Location(driver);
            auto const vat = player::Location(pawn);
            ue3::Object* const mesh = gPawnMesh.Read<ue3::Object*>(driver).value_or(nullptr);
            report += std::format("the driver: bHidden {}, Base {}, Physics {}, {:.0f} units from the vehicle's origin ({:.0f} {:.0f} {:.0f}), mesh {} (HiddenGame {}); ",
                gHiddenField.ReadBool(driver).value_or(false) ? 1 : 0, ue3::runtime::FullName(gBase.Read<ue3::Object*>(driver).value_or(nullptr)),
                static_cast<int>(gPhysics.Read<std::uint8_t>(driver).value_or(255)),
                at && vat ? std::sqrt(DistanceSquared(*at, *vat)) : -1.0, at ? at->x - (vat ? vat->x : 0.0f) : 0.0f, at ? at->y - (vat ? vat->y : 0.0f) : 0.0f,
                at ? at->z - (vat ? vat->z : 0.0f) : 0.0f, ue3::runtime::FullName(mesh), mesh != nullptr && gHiddenGame.ReadBool(mesh).value_or(false) ? 1 : 0);
        }
        std::string place;
        words >> place;
        if (place == "place" && gPlaceExitingDriver.Get() != nullptr) {  // it moves the driver where it would get out
            ue3::Params p = ParamsOf(gPlaceExitingDriver);
            bool const called = Call(pawn, gPlaceExitingDriver, p);
            report += std::format("PlaceExitingDriver: called {}, returned {} (true: a place found, the driver moved there)", called, p.ReturnBool().value_or(false));
        }
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "damage") {
        // The game's own damage on the pawn (its class's TakeDamage, so the vehicle's override and its death and game-over run),
        // for testing what the cockpit does when the Mako dies (docs/mako-cockpit-goal.md item 1).
        float amount = 100.0f;
        words >> amount;
        ue3::Reflection& r = R();
        ue3::Function* const take = r.FindFunction(r.ClassOf(pawn), "TakeDamage");
        if (take == nullptr || gTakeDamage.Get() == nullptr) {
            return Refuse("no TakeDamage on the pawn's class");
        }
        auto const at = player::Location(pawn);
        ue3::Params p(r, take);
        p.Set("Damage", amount);
        p.Set("DamageAmount", amount);
        p.Set("EventInstigator", static_cast<ue3::Object*>(nullptr));
        p.Set("HitLocation", at ? ue3::Vector{at->x, at->y, at->z} : ue3::Vector{});
        p.Set("DamageType", static_cast<ue3::Object*>(gDamageTypeClass.Get()));
        bool const called = ue3::runtime::Call(pawn, take, p);
        std::string const report = std::format("{}.TakeDamage({:.0f}) called {}; health now {}", r.NameText(r.OuterOf(take)), amount, called,
            gPawnHealth.Read<std::int32_t>(pawn).value_or(-1));
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "pawns") {
        // The level's live pawns but the player's, nearest first: who could shoot the Mako (`hit <amount> <n>` uses the n-th).
        ue3::Reflection& r = R();
        auto const at = player::Location(pawn);
        std::vector<std::pair<double, ue3::Object*>> found;
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || o == pawn || !r.IsAlive(o) || !ue3::IsA(o, gPawnClassRef) || (r.FlagsOf(o) & 0x200) != 0) {  // not a class default
                continue;
            }
            auto const where = player::Location(o);
            if (!where || !at) {
                continue;
            }
            double const dx = where->x - at->x, dy = where->y - at->y, dz = where->z - at->z;
            found.emplace_back(std::sqrt(dx * dx + dy * dy + dz * dz) / engine::kUnitsPerMeter, o);
        }
        std::sort(found.begin(), found.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        gFoundPawns.clear();
        std::string out = std::format("{} pawns:", found.size());
        for (std::size_t i = 0; i < found.size() && i < 25; ++i) {
            gFoundPawns.push_back(found[i].second);
            ue3::Object* const c = gPawnControllerField.Read<ue3::Object*>(found[i].second).value_or(nullptr);
            out += std::format("\n  {}: {} at {:.0f} m, controller {}", i, r.FullName(found[i].second), found[i].first, r.NameText(c));
        }
        log::Info("mevr vehicle: {}", out);
        return out;
    }
    if (verb == "hit") {
        // A hit as a shot lands one: the vehicle behaviour's own TakeDamage (BioActorBehavior's, through the Mako behaviour's
        // ProxyTakeDamage), so the game's damage model, health and death run as under fire (research/47).
        float amount = 200.0f;
        int shooter = -1;
        std::string route;
        words >> amount >> shooter >> route;
        ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
        ue3::Reflection& r = R();
        ue3::Function* const take = behavior != nullptr ? r.FindFunction(r.ClassOf(behavior), "TakeDamage") : nullptr;
        ue3::Object* const by = shooter >= 0 && static_cast<std::size_t>(shooter) < gFoundPawns.size() && r.IsAlive(gFoundPawns[static_cast<std::size_t>(shooter)])
                                    ? gFoundPawns[static_cast<std::size_t>(shooter)] : nullptr;
        ue3::Object* const byController = by != nullptr ? gPawnControllerField.Read<ue3::Object*>(by).value_or(nullptr) : nullptr;
        // ME1's hits carry a BioDamageType object (pDamage); one the game has loaded (a weapon's), else none.
        ue3::Object* damage = nullptr;
        std::string damageName = "none";
        for (std::int32_t i = 0, n = r.ObjectCount(); i < n && damage == nullptr; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !r.IsAlive(o) || r.NameText(o).starts_with("Default__")) {
                continue;
            }
            for (ue3::Struct* c = r.ClassOf(o); c != nullptr; c = r.SuperOf(c)) {
                if (r.NameText(c) == "BioDamageType") {
                    damage = o;
                    damageName = r.FullName(o);
                    break;
                }
            }
        }
        if (take == nullptr) {
            return Refuse("no vehicle behaviour with TakeDamage");
        }
        auto const at = player::Location(pawn);
        ue3::Params p(r, take);
        p.Set("Damage", amount);
        p.Set("HitLocation", at ? ue3::Vector{at->x, at->y, at->z + 50.0f} : ue3::Vector{});
        p.Set("DamageType", static_cast<ue3::Object*>(gDamageTypeClass.Get()));
        p.Set("InstigatedBy", byController);
        p.Set("pDamageCausingActor", by);
        p.Set("pDamage", damage);
        bool called = false;
        if (route == "pawn") {
            // The pawn's own TakeDamage, as a projectile calls it (the vehicle's override hands it to the behaviour).
            ue3::Function* const pawnTake = r.FindFunction(r.ClassOf(pawn), "TakeDamage");
            if (pawnTake == nullptr) {
                return Refuse("no TakeDamage on the pawn");
            }
            ue3::Params q(r, pawnTake);
            q.Set("Damage", static_cast<std::int32_t>(amount));
            q.Set("Damage", amount);
            q.Set("DamageAmount", static_cast<std::int32_t>(amount));
            q.Set("EventInstigator", byController);
            q.Set("HitLocation", at ? ue3::Vector{at->x, at->y, at->z + 50.0f} : ue3::Vector{});
            q.Set("DamageType", static_cast<ue3::Object*>(gDamageTypeClass.Get()));
            q.Set("DamageCauser", by);
            q.Set("pDamage", damage);
            called = ue3::runtime::Call(pawn, pawnTake, q);
        } else {
            called = ue3::runtime::Call(behavior, take, p);
        }
        std::string const report = std::format("{}.TakeDamage({:.0f}) by {} with {} called {}; the pawn's health {}, game mode {}",
            r.NameText(r.OuterOf(take)), amount, r.FullName(by), damageName, called, gPawnHealth.Read<std::int32_t>(pawn).value_or(-1), player::GameMode(player::Controller()).value_or(-1));
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "bring") {
        // `bring <n> [metres]`: the n-th of `pawns` put that far ahead of the Mako (Actor.SetLocation), so its own AI fights it: a death
        // by enemy fire without driving across the map (research/47).
        int index = -1;
        float metres = 30.0f;
        words >> index >> metres;
        ue3::Reflection& r = R();
        if (index < 0 || static_cast<std::size_t>(index) >= gFoundPawns.size() || !r.IsAlive(gFoundPawns[static_cast<std::size_t>(index)])) {
            return Refuse("usage: mevr vehicle bring <n from `mevr vehicle pawns`> [metres ahead]");
        }
        ue3::Object* const who = gFoundPawns[static_cast<std::size_t>(index)];
        auto const at = player::Location(pawn);
        auto const turn = player::Rotation(pawn);
        ue3::Function* const setLocation = r.FindFunction(r.ClassOf(who), "SetLocation");
        if (!at || !turn || setLocation == nullptr) {
            return Refuse("no place for the Mako, or no SetLocation");
        }
        engine::UnrealVector const ahead = engine::ToBasis(engine::UnrealRotation{0, turn->yaw, 0}).forward;
        double const units = metres * engine::kUnitsPerMeter;
        ue3::Params p(r, setLocation);
        p.Set("NewLocation", ue3::Vector{static_cast<float>(at->x + ahead.x * units), static_cast<float>(at->y + ahead.y * units), at->z + 150.0f});
        bool const called = ue3::runtime::Call(who, setLocation, p);
        auto const now = player::Location(who);
        std::string const out = std::format("{} moved {:.0f} m ahead of the Mako: SetLocation {} {}, now {:.0f} m away", r.FullName(who), metres,
            called, p.ReturnBool().value_or(false) ? "true" : "false",
            now ? std::sqrt((now->x - at->x) * (now->x - at->x) + (now->y - at->y) * (now->y - at->y)) / engine::kUnitsPerMeter : -1.0);
        log::Info("mevr vehicle: {}", out);
        return out;
    }
    if (verb == "sethealth") {
        // The vehicle behaviour's own SetCurrentHealth (a test: a low Mako that the next real hits finish).
        float health = 1.0f;
        words >> health;
        ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
        ue3::Reflection& r = R();
        ue3::Function* const set = behavior != nullptr ? r.FindFunction(r.ClassOf(behavior), "SetCurrentHealth") : nullptr;
        if (set == nullptr) {
            return Refuse("no vehicle behaviour with SetCurrentHealth");
        }
        ue3::Params p(r, set);
        p.Set("currentHealth", health);
        bool const called = ue3::runtime::Call(behavior, set, p);
        log::Info("mevr vehicle: SetCurrentHealth({:.0f}) called {}", health, called);
        return std::format("SetCurrentHealth({:.0f}) called {}", health, called);
    }
    if (verb == "state") {
        // The vehicle behaviour's own health (ME1 keeps it there, not in Pawn.Health), and whether it can die.
        ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
        if (behavior == nullptr) {
            return Refuse("no vehicle behaviour");
        }
        ue3::Reflection& r = R();
        auto const call = [&](char const* name) -> std::string {
            ue3::Function* const f = r.FindFunction(r.ClassOf(behavior), name);
            if (f == nullptr) {
                return "?";
            }
            ue3::Params p(r, f);
            if (!ue3::runtime::Call(behavior, f, p)) {
                return "failed";
            }
            if (auto const b = p.ReturnBool()) {
                return *b ? "yes" : "no";
            }
            if (auto const v = p.Return<float>()) {
                return std::format("{:.1f}", *v);
            }
            return "?";
        };
        std::string shields = "?";
        if (ue3::Function* const f = r.FindFunction(r.ClassOf(behavior), "GetHealthAndShieldsScaled")) {
            ue3::Params p(r, f);
            if (ue3::runtime::Call(behavior, f, p)) {
                shields = std::format("health {:.2f}, shields {:.2f} (scaled)", p.Get<float>("fHealth").value_or(-1.0f), p.Get<float>("fShields").value_or(-1.0f));
            }
        }
        std::string const out = std::format("health {} of {}, {}, invulnerable {}, held at 1 health {}", call("GetCurrentHealth"), call("GetMaxHealth"),
            shields, call("IsInvulnerable"), call("IsMin1Health"));
        log::Info("mevr vehicle: {}", out);
        return out;
    }
    if (verb == "funcs") {
        // The vehicle behaviour's (or `pawn`: the pawn's) functions whose name holds the text, with their parameters: finding the
        // game's own paths (a hit's damage, research/47).
        std::string text;
        std::string which;
        words >> text >> which;
        ue3::Reflection& r = R();
        ue3::Object* const listed = which == "pawn" ? pawn : gVehicleBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
        if (listed == nullptr) {
            return Refuse("no vehicle behaviour");
        }
        auto const lower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return s;
        };
        std::string const want = lower(text);
        std::string out = std::format("{}:", r.FullName(listed));
        int found = 0;
        for (ue3::Struct* s = r.ClassOf(listed); s != nullptr && found < 40; s = r.SuperOf(s)) {
            for (ue3::Field* f = r.ChildrenOf(s); f != nullptr && found < 40; f = r.NextOf(f)) {
                if (r.ClassOf(f) != r.FunctionClass() || lower(r.NameText(f)).find(want) == std::string::npos) {
                    continue;
                }
                std::string params;
                for (ue3::ParamSpec const& p : r.Parameters(reinterpret_cast<ue3::Function*>(f))) {
                    params += (params.empty() ? "" : ", ") + p.name;
                }
                out += std::format("\n  {}.{}({})", r.NameText(s), r.NameText(f), params);
                ++found;
            }
        }
        log::Info("mevr vehicle: {}", out);
        return out;
    }
    if (verb == "die") {
        // ME1's vehicles take damage through their behaviour (TakeDamage on the pawn left Health as it was): its own death reaction,
        // the vehicle behaviour's override of BioActorBehavior.ReactToDeath, as a fatal hit would run it.
        ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(pawn).value_or(nullptr);
        ue3::Reflection& r = R();
        ue3::Function* const react = behavior != nullptr ? r.FindFunction(r.ClassOf(behavior), "ReactToDeath") : nullptr;
        if (react == nullptr || gReactToDeath.Get() == nullptr) {
            return Refuse("no vehicle behaviour with ReactToDeath");
        }
        auto const at = player::Location(pawn);
        ue3::Params p(r, react);
        p.Set("Damage", 10000.0f);
        p.Set("fDamage", 10000.0f);
        p.Set("fDeathDamage", 10000.0f);
        p.Set("HitLocation", at ? ue3::Vector{at->x, at->y, at->z} : ue3::Vector{});
        p.Set("vHitLocation", at ? ue3::Vector{at->x, at->y, at->z} : ue3::Vector{});
        p.Set("clsDamageType", static_cast<ue3::Object*>(gDamageTypeClass.Get()));
        bool const called = ue3::runtime::Call(behavior, react, p);
        std::string const report = std::format("{}.ReactToDeath called {}", r.NameText(r.OuterOf(react)), called);
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "angvel") {
        // `angvel body F R U`: along the vehicle's own forward, right and up at this moment instead of the world's axes.
        std::string first;
        words >> first;
        bool const body = first == "body";
        if (!body) {
            words.seekg(0);
            words >> first;  // the verb again
        }
        ue3::Vector v{};
        if (!(words >> v.x >> v.y >> v.z)) {
            return "usage: mevr vehicle angvel X Y Z (raw, as SetRBAngularVelocity takes it; the turn 0.25 s later goes to the log)";
        }
        ue3::Object* const mesh = gPawnMesh.Read<ue3::Object*>(pawn).value_or(nullptr);
        auto const turn = player::Rotation(pawn);
        if (mesh == nullptr || !turn || gSetAngVel.Get() == nullptr) {
            return Refuse("no vehicle mesh or SetRBAngularVelocity");
        }
        if (body) {
            engine::Basis const axes = engine::ToBasis(engine::UnrealRotation{turn->pitch, turn->yaw, turn->roll});
            ue3::Vector const local = v;
            v = {static_cast<float>(axes.forward.x * local.x + axes.right.x * local.y + axes.up.x * local.z),
                static_cast<float>(axes.forward.y * local.x + axes.right.y * local.y + axes.up.y * local.z),
                static_cast<float>(axes.forward.z * local.x + axes.right.z * local.y + axes.up.z * local.z)};
        }
        ue3::Params p = ParamsOf(gSetAngVel);
        p.Set("NewAngVel", v);
        p.SetBool("bAddToCurrent", false);
        Call(mesh, gSetAngVel, p);
        gAngVelVehicle = pawn;
        gAngVelAt = Now() + 0.25;
        gAngVelFrom = {turn->pitch, turn->yaw, turn->roll};
        gAngVelAsked = v;
        return std::format("angular velocity ({}, {}, {}) set at pitch {:.1f} yaw {:.1f} roll {:.1f}: the change follows in the log", v.x, v.y, v.z,
            turn->pitch * 360.0 / 65536.0, turn->yaw * 360.0 / 65536.0, turn->roll * 360.0 / 65536.0);
    }
    if (verb == "jets") {
        double seconds = 2.0;
        words >> seconds;
        return mako_controls::TestJets(seconds);
    }
    if (verb == "guns") {
        return Guns();
    }

    if (verb == "fire") {
        // `mevr vehicle fire [cannon|mg] [seconds]`: RB's FireCannon or RT's FireWeapon (the machine gun), released after the time.
        std::string which;
        double hold = kFireHoldS;
        words >> which >> hold;
        bool const mg = which == "mg";
        hold = std::clamp(hold > 0.0 ? hold : kFireHoldS, 0.05, 10.0);
        if (auto const missing = mg ? MissingFunction({&gFireWeapon, &gStopFiringWeapon}) : MissingFunction({&gFireCannon, &gStopFiringCannon})) {
            return Refuse(*missing);
        }
        ue3::Object* mode = nullptr;
        ForEachLive(gGameModeVehicle.Get(), [&](ue3::Object* candidate) {
            if (mode == nullptr && R().OuterOf(candidate) == controller) {
                mode = candidate;
            }
        });
        if (mode == nullptr) {
            return Refuse("no vehicle game mode of the player's controller");
        }
        ue3::Params p = ParamsOf(mg ? gFireWeapon : gFireCannon);
        Call(mode, mg ? gFireWeapon : gFireCannon, p);
        gFiringMode = mode;
        gFiringMg = mg;
        gFireReleaseAt = Now() + hold;
        std::string const report = std::format("{} called (as {}), released in {:.1f} s: the `mako: shot` lines say where it went",
            mg ? "FireWeapon" : "FireCannon", mg ? "RT" : "RB", hold);
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "exit") {
        std::string how;
        words >> how;
        if (how == "force") {
            // BioPlayerController.BioExitVehicle(bForceExit): past whatever refused TryExitVehicle (research/47 [verify]).
            if (auto const missing = MissingFunction({&gBioExitVehicle})) {
                return Refuse(*missing);
            }
            ue3::Params p = ParamsOf(gBioExitVehicle);
            p.SetBool("bForceExit", true);
            bool const called = Call(controller, gBioExitVehicle, p);
            // The native forwards to a virtual of the controller (vtable +0xB90) and the vehicle's BioCanExitVehicle to +0xC20:
            // their addresses, to read in Ghidra.
            auto const slot = [](ue3::Object* object, std::size_t offset) -> std::uintptr_t {
                auto const* const table = object != nullptr ? *reinterpret_cast<std::uintptr_t const* const*>(object) : nullptr;
                return table != nullptr ? table[offset / sizeof(std::uintptr_t)] - reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) : 0;
            };
            std::string const report = std::format("BioExitVehicle(force): called {}, returned {}; the controller's +0xB90 is exe+0x{:X}, the vehicle's +0xC20 exe+0x{:X}", called,
                p.ReturnBool().value_or(false), slot(controller, 0xB90), slot(pawn, 0xC20));
            log::Info("mevr vehicle: {}", report);
            return report;
        }
        if (auto const missing = MissingFunction({&gTryExitVehicle})) {
            return Refuse(*missing);
        }
        int called = 0;
        ForEachLive(gGameModeVehicle.Get(), [&](ue3::Object* mode) {
            if (called == 0 && R().OuterOf(mode) == controller) {
                ue3::Params p = ParamsOf(gTryExitVehicle);
                called += Call(mode, gTryExitVehicle, p) ? 1 : 0;
            }
        });
        std::string const report = called > 0 ? "TryExitVehicle called (as B): `mevr vehicle` says what it did" : "no vehicle game mode of the player's controller";
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "party") {
        // Accepts the squad screen as its confirmation box's A does: the members picked so far go into the party (none
        // picked: Shepard alone) and the panel hides, so the level's script goes on past ShowPartySelectionGUI.
        if (auto const missing = MissingFunction({&gFinalizeParty})) {
            return Refuse(*missing);
        }
        int handlers = 0;
        int called = 0;
        ForEachLive(gPartySelection.Get(), [&](ue3::Object* handler) {
            ++handlers;
            ue3::Params p = ParamsOf(gFinalizeParty);
            p.SetBool("bAPressed", true);
            p.Set("nContext", 0);
            p.SetBool("bYPressed", false);
            called += Call(handler, gFinalizeParty, p) ? 1 : 0;
        });
        std::string const report = std::format("squad screen: {} handler{}, FinalizeParty(A) called on {}", handlers, handlers == 1 ? "" : "s", called);
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "resume") {
        // The game-over screen's "Resume Last Save", as its button sends it: BioSFHandler_GameOver.HandleEvent(3).
        if (auto const missing = MissingFunction({&gGameOverEvent})) {
            return Refuse(*missing);
        }
        int handlers = 0;
        int called = 0;
        ForEachLive(gGameOver.Get(), [&](ue3::Object* handler) {
            ++handlers;
            ue3::Params p = ParamsOf(gGameOverEvent);
            p.Set("nCommand", static_cast<std::uint8_t>(3));
            called += Call(handler, gGameOverEvent, p) ? 1 : 0;
        });
        std::string const report = std::format("game over: {} handler{}, Resume Last Save (HandleEvent 3) called on {}", handlers, handlers == 1 ? "" : "s", called);
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    if (verb == "event") {
        // Fires the level script's remote events of that name, as a SeqAct_ActivateRemoteEvent would (originator the
        // WorldInfo, instigator the pawn). For stepping a level's script past a stall; nothing else uses it.
        std::string wanted;
        words >> wanted;
        if (wanted.empty()) {
            return Refuse("mevr vehicle event <EventName>");
        }
        if (auto const missing = MissingFunction({&gCheckActivate})) {
            return Refuse(*missing);
        }
        ue3::Object* const world = gWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
        int found = 0;
        int fired = 0;
        ForEachLive(gRemoteEvent.Get(), [&](ue3::Object* event) {
            auto const name = gRemoteEventName.Read<ue3::Name>(event);
            if (!name || Lower(ue3::runtime::NameText(*name)) != Lower(wanted)) {
                return;
            }
            ++found;
            ue3::Params p = ParamsOf(gCheckActivate);
            p.Set("InOriginator", world);
            p.Set("InInstigator", pawn != nullptr ? pawn : controller);
            if (Call(event, gCheckActivate, p) && p.ReturnBool().value_or(false)) {
                ++fired;
            }
        });
        std::string const report = std::format("remote event {}: {} listener{}, {} activated", wanted, found, found == 1 ? "" : "s", fired);
        log::Info("mevr vehicle: {}", report);
        return report;
    }
    auto const mode = player::GameMode(controller);
    return std::format("map {}, game mode {}, pawn {} (BioVehicleBase: {}), closest drivable {} ({}), nearest vehicle {} at {:.1f} m", MapName(controller), mode ? std::to_string(*mode) : std::string("?"),
        ue3::runtime::FullName(pawn), pawn != nullptr && ue3::IsA(pawn, gVehicleBase) ? "yes" : "no", ue3::runtime::FullName(find.engineAnswer),
        find.engineAsked ? "FindClosestDrivableVehicle's answer" : "not asked: no live SFXSaveGame or not on foot", ue3::runtime::FullName(find.nearest), find.nearestM);
}

// ---- The hazard lights (research/47 "Hazard lights", 2026-11-04) ------------------------------------------------------
// With the Mako's hull health low (vr::cockpit_hazard), every element of the cockpit's parts that shows one of the package's
// own instances (not the glass, not a screen) gets an instance of ours over the package's glow parent,
// EngineBuildings.ProcBuildingSimpleMaterial (unlit: DiffuseTexture x LightTexture x LightmapBrightness): the element's own
// image as DiffuseTexture, a 4x4 render target cleared red as LightTexture (a render target nothing draws into shows its clear
// colour), and the beacon's brightness each tick. The lamps flash bright red, the rest of the cabin washes red with them. When
// the alarm ends, each element gets its own material back.
ue3::FunctionRef gSetScalarParameter{"Engine.MaterialInstanceConstant.SetScalarParameterValue"};
ue3::FunctionRef gHealthAndShields{"SFXGame.BioEpicPawnBehavior.GetHealthAndShieldsScaled"};
ue3::FieldRef gPawnAttributes{"SFXGame.BioEpicPawnBehavior", "m_PawnAttributes"};
ue3::FieldRef gHealthCurrent{"SFXGame.BioAttributesEpicPawn", "m_HealthCurrent"};
ue3::FieldRef gScalarValues{"Engine.MaterialInstanceConstant", "ScalarParameterValues"};
ue3::FieldRef gScalarValueName{gScalarValues, "ParameterName"};
constexpr char const* kGlowParentPath = "EngineBuildings.ProcBuildingSimpleMaterial";

struct HazardElement {
    ue3::Object* actor = nullptr;
    ue3::Object* component = nullptr;
    int index = 0;
    ue3::Object* original = nullptr;  // the element's material before
    ue3::Object* mic = nullptr;       // ours on it
    bool lamp = false;
};

struct Hazard {
    enum class Force { Auto, On, Off } force = Force::Auto;
    bool alarm = false;
    bool applied = false;
    void const* world = nullptr;  // a new level: the old elements are gone with it, never touched
    std::vector<HazardElement> elements;
    ue3::Object* red = nullptr;
    // The glow parent's parameter names, learnt from the package's own lamp instances (there is no name-from-text lookup).
    bool namesKnown = false;
    ue3::Name diffuseName{};
    ue3::Name lightName{};
    ue3::Name brightnessName{};
    double since = 0.0;      // when this alarm began
    double lastRead = -1e9;  // the hull's health, read every 0.1 s while riding
    double retryAt = 0.0;    // an apply that failed tries again from here
    std::optional<float> health;
    std::optional<float> shields;
    unsigned episodes = 0;
    int lamps = 0;
    int washed = 0;
    int skipped = 0;
    std::string why = "off";
};
Hazard gHazard;

char const* ForceName(Hazard::Force force) {
    return force == Hazard::Force::On ? "on" : force == Hazard::Force::Off ? "off" : "auto";
}

// The hull's health and shields (0..1) through the vehicle's behavior's own GetHealthAndShieldsScaled.
std::optional<std::pair<float, float>> ReadHull(ue3::Object* vehicle) {
    if (vehicle == nullptr || !ue3::runtime::IsAlive(vehicle) || gHealthAndShields.Get() == nullptr) {
        return std::nullopt;
    }
    ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(vehicle).value_or(nullptr);
    if (behavior == nullptr || !ue3::runtime::IsAlive(behavior)) {
        return std::nullopt;
    }
    ue3::Params p = ParamsOf(gHealthAndShields);
    if (!Call(behavior, gHealthAndShields, p)) {
        return std::nullopt;
    }
    std::optional<float> const health = p.Get<float>("fHealth");
    if (!health) {
        return std::nullopt;
    }
    return std::pair{*health, p.Get<float>("fShields").value_or(0.0f)};
}

// The words of an instance's path ("biog_mevr_cockpit.hull_shell_lamp2_glow_mic" -> biog, mevr, cockpit, hull, shell, lamp2...).
bool PathHas(std::string const& lowerPath, std::string_view word, bool prefix) {
    std::size_t start = 0;
    while (start <= lowerPath.size()) {
        std::size_t const end = lowerPath.find_first_of("._", start);
        std::string_view const token(lowerPath.data() + start, (end == std::string::npos ? lowerPath.size() : end) - start);
        if (prefix ? token.starts_with(word) : token == word) {
            return true;
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return false;
}

// The glow parent's parameter names, from a lamp instance of the package (its texture and scalar arrays hold them).
bool LearnNames(ue3::Object* lampInstance) {
    if (gHazard.namesKnown) {
        return true;
    }
    bool diffuse = false;
    bool light = false;
    bool brightness = false;
    if (auto const values = gTextureValues.ElementsOf(lampInstance)) {
        for (std::int32_t i = 0; i < values->count; ++i) {
            std::optional<ue3::Name> const name = gTextureValueName.Read<ue3::Name>(values->At(i));
            if (!name) {
                continue;
            }
            std::string const text = ue3::runtime::NameText(*name);
            if (text == "DiffuseTexture") {
                gHazard.diffuseName = *name;
                diffuse = true;
            } else if (text == "LightTexture") {
                gHazard.lightName = *name;
                light = true;
            }
        }
    }
    if (auto const values = gScalarValues.ElementsOf(lampInstance)) {
        for (std::int32_t i = 0; i < values->count; ++i) {
            std::optional<ue3::Name> const name = gScalarValueName.Read<ue3::Name>(values->At(i));
            if (name && ue3::runtime::NameText(*name) == "LightmapBrightness") {
                gHazard.brightnessName = *name;
                brightness = true;
            }
        }
    }
    gHazard.namesKnown = diffuse && light && brightness;
    return gHazard.namesKnown;
}

// The texture an element's instance shows: the base's `Texture`, the glow's `DiffuseTexture` or the lit's `Emissive`.
ue3::Object* ElementTexture(ue3::Object* material) {
    if (auto const values = gTextureValues.ElementsOf(material)) {
        for (std::int32_t i = 0; i < values->count; ++i) {
            std::optional<ue3::Name> const name = gTextureValueName.Read<ue3::Name>(values->At(i));
            if (!name) {
                continue;
            }
            std::string const text = ue3::runtime::NameText(*name);
            if (text == "Texture" || text == "DiffuseTexture" || text == "Emissive") {
                if (ue3::Object* const texture = gTextureValueObject.Read<ue3::Object*>(values->At(i)).value_or(nullptr)) {
                    return texture;
                }
            }
        }
    }
    return nullptr;
}

void SetBrightness(ue3::Object* mic, float value) {
    ue3::Params p = ParamsOf(gSetScalarParameter);
    p.Set("ParameterName", gHazard.brightnessName);
    p.Set("Value", value);
    Call(mic, gSetScalarParameter, p);
}

void SetTextureParameter(ue3::Object* mic, ue3::Name name, ue3::Object* texture) {
    ue3::Params p = ParamsOf(gSetTextureParameter);
    p.Set("ParameterName", name);
    p.Set("Value", texture);
    Call(mic, gSetTextureParameter, p);
}

// Our instance on every eligible element; false (with gHazard.why) if nothing could be lit.
bool HazardApply(vr::cockpit_hazard::Settings const& settings) {
    if (auto const missing = MissingFunction({&gGetMaterial, &gSetMaterial, &gGetNumElements, &gCreateMic, &gSetParent, &gSetTextureParameter,
            &gSetScalarParameter})) {
        gHazard.why = *missing;
        return false;
    }
    ue3::Object* const glowParent = LoadObject(kGlowParentPath, gMaterial.Get());
    if (glowParent == nullptr) {
        gHazard.why = std::format("{} isn't loaded", kGlowParentPath);
        return false;
    }
    // Which elements, first: the names come from a lamp's own instance before anything is swapped.
    struct Pick {
        Part const* part;
        int index;
        ue3::Object* material;
        bool lamp;
    };
    std::vector<Pick> picks;
    gHazard.skipped = 0;
    for (Part const& part : gParts) {
        if (!PartAlive(part)) {
            continue;
        }
        ue3::Params count = ParamsOf(gGetNumElements);
        Call(part.component, gGetNumElements, count);
        int const elements = count.Return<std::int32_t>().value_or(0);
        for (int i = 0; i < elements; ++i) {
            ue3::Params get = ParamsOf(gGetMaterial);
            get.Set("ElementIndex", i);
            Call(part.component, gGetMaterial, get);
            ue3::Object* const material = get.Return<ue3::Object*>().value_or(nullptr);
            std::string const path = Lower(R().PathName(material));
            if (!path.starts_with("biog_mevr_cockpit.") || !ue3::IsA(material, gMic) || PathHas(path, "glass", false) || PathHas(path, "screen", false) ||
                PathHas(path, "display", false) || PathHas(path, "aim", false) || PathHas(path, "light", false)) {
                ++gHazard.skipped;  // our own screens' instances, the glass and the console's displays keep theirs
                continue;
            }
            bool const lamp = PathHas(path, "lamp", true);
            if (lamp && PathHas(path, "glow", false)) {
                LearnNames(material);
            }
            if (!lamp && !settings.wash) {
                continue;
            }
            picks.push_back({&part, i, material, lamp});
        }
    }
    if (!gHazard.namesKnown) {
        gHazard.why = "no lamp instance of the package to learn the glow parent's parameter names from (is the cockpit's package current?)";
        return false;
    }
    if (picks.empty()) {
        gHazard.why = "no element of the cockpit shows one of the package's instances";
        return false;
    }
    float const* const red = vr::cockpit_hazard::kRed;
    gHazard.red = CreateTarget(4, 4, false, LinearColor{red[0], red[1], red[2], red[3]});
    if (gHazard.red == nullptr) {
        gHazard.why = "TextureRenderTarget2D.Create returned no render target";
        return false;
    }
    vr::cockpit_hazard::Levels const levels = vr::cockpit_hazard::LevelsAt(1.0f);
    gHazard.lamps = 0;
    gHazard.washed = 0;
    std::vector<ue3::Object*> touched;
    for (Pick const& pick : picks) {
        ue3::Object* const texture = ElementTexture(pick.material);
        if (texture == nullptr) {
            ++gHazard.skipped;
            continue;
        }
        ue3::Params create = ParamsOf(gCreateMic);
        create.Set("ElementIndex", pick.index);
        Call(pick.part->component, gCreateMic, create);
        ue3::Object* const mic = create.Return<ue3::Object*>().value_or(nullptr);
        if (!ue3::IsA(mic, gMic)) {
            ++gHazard.skipped;
            continue;
        }
        ue3::Params parent = ParamsOf(gSetParent);
        parent.Set("NewParent", glowParent);
        Call(mic, gSetParent, parent);
        SetTextureParameter(mic, gHazard.diffuseName, texture);
        SetTextureParameter(mic, gHazard.lightName, gHazard.red);
        SetBrightness(mic, pick.lamp ? levels.lamp : levels.wash);
        gHazard.elements.push_back({pick.part->actor, pick.part->component, pick.index, pick.material, mic, pick.lamp});
        ++(pick.lamp ? gHazard.lamps : gHazard.washed);
        if (std::find(touched.begin(), touched.end(), pick.part->actor) == touched.end()) {
            touched.push_back(pick.part->actor);
        }
    }
    for (ue3::Object* const actor : touched) {
        FlushActor(actor, false);
    }
    if (gHazard.elements.empty()) {
        gHazard.why = "no element took our instance";
        return false;
    }
    gHazard.why = std::format("red: {} lamp{}, {} element{} washed, {} kept", gHazard.lamps, gHazard.lamps == 1 ? "" : "s", gHazard.washed,
        gHazard.washed == 1 ? "" : "s", gHazard.skipped);
    log::Info("cockpit hazard: the cabin's lights turn red ({}; over {})", gHazard.why, kGlowParentPath);
    return true;
}

// Each element its own material back (those still in this world).
void HazardRestore(char const* why) {
    std::vector<ue3::Object*> touched;
    int restored = 0;
    bool const sameWorld = gHazard.world == engine::World();
    for (HazardElement const& element : gHazard.elements) {
        if (!sameWorld || !ue3::runtime::IsAlive(element.component) || element.original == nullptr || !ue3::runtime::IsAlive(element.original)) {
            continue;
        }
        ue3::Params set = ParamsOf(gSetMaterial);
        set.Set("ElementIndex", element.index);
        set.Set("Material", element.original);
        Call(element.component, gSetMaterial, set);
        ++restored;
        if (std::find(touched.begin(), touched.end(), element.actor) == touched.end()) {
            touched.push_back(element.actor);
        }
    }
    for (ue3::Object* const actor : touched) {
        if (ue3::runtime::IsAlive(actor)) {
            FlushActor(actor, false);
        }
    }
    if (gHazard.applied) {
        log::Info("cockpit hazard: the cabin's own lights back ({}; {} of {} elements restored)", why, restored, gHazard.elements.size());
    }
    gHazard.elements.clear();
    gHazard.red = nullptr;  // nothing holds it now: the next garbage collection takes it
    gHazard.applied = false;
}

void HazardTick() {
    vr::cockpit_hazard::Settings const settings = hand_calibration::GetCockpit().hazard;
    if (gHazard.world != engine::World()) {
        gHazard.elements.clear();  // the old level's objects are gone: nothing to put back
        gHazard.red = nullptr;
        gHazard.applied = false;
        gHazard.alarm = false;
        gHazard.health.reset();
        gHazard.world = engine::World();
    }
    bool const cockpit = std::any_of(gParts.begin(), gParts.end(), [](Part const& part) { return PartAlive(part); });
    ue3::Object* const vehicle = Riding() && gRide.vehicle != nullptr && ue3::runtime::IsAlive(gRide.vehicle) ? gRide.vehicle : nullptr;
    if (vehicle != nullptr && Now() - gHazard.lastRead >= 0.1) {
        gHazard.lastRead = Now();
        if (auto const hull = ReadHull(vehicle)) {
            gHazard.health = hull->first;
            gHazard.shields = hull->second;
        } else {
            gHazard.health.reset();
        }
    } else if (vehicle == nullptr) {
        gHazard.health.reset();
        gHazard.shields.reset();
    }
    bool want = false;
    switch (gHazard.force) {
    case Hazard::Force::On: want = cockpit; break;
    case Hazard::Force::Off: want = false; break;
    case Hazard::Force::Auto: want = cockpit && vr::cockpit_hazard::Alarm(gHazard.alarm, gHazard.health, vehicle != nullptr, settings); break;
    }
    if (want != gHazard.alarm) {
        gHazard.alarm = want;
        if (want) {
            gHazard.since = Now();
            ++gHazard.episodes;
            log::Info("cockpit hazard: alarm on ({}; hull {}, shields {})", gHazard.force == Hazard::Force::On ? "forced" : "low hull",
                gHazard.health ? std::format("{:.0f}%", *gHazard.health * 100.0f) : std::string("?"),
                gHazard.shields ? std::format("{:.0f}%", *gHazard.shields * 100.0f) : std::string("?"));
        } else {
            log::Info("cockpit hazard: alarm off (hull {})", gHazard.health ? std::format("{:.0f}%", *gHazard.health * 100.0f) : std::string("unknown"));
        }
    }
    if (gHazard.alarm && !gHazard.applied && Now() >= gHazard.retryAt) {
        gHazard.applied = HazardApply(settings);
        if (!gHazard.applied) {
            HazardRestore("the apply failed part way");
            gHazard.retryAt = Now() + 2.0;
            log::Warn("cockpit hazard: no red lights: {} (tries again in 2 s)", gHazard.why);
        }
    }
    if (!gHazard.alarm && gHazard.applied) {
        HazardRestore(cockpit ? "the alarm is over" : "the cockpit is gone");
        gHazard.why = "off";
    }
    if (!gHazard.applied) {
        return;
    }
    // A part rebuilt under us (a new seat, the script ran again): put back what's left and apply afresh next tick.
    for (HazardElement const& element : gHazard.elements) {
        if (!ue3::runtime::IsAlive(element.component) || !ue3::runtime::IsAlive(element.mic)) {
            HazardRestore("the cockpit's parts changed");
            return;
        }
    }
    vr::cockpit_hazard::Levels const levels =
        vr::cockpit_hazard::LevelsAt(vr::cockpit_hazard::Flash(Now() - gHazard.since, settings.periodS));
    for (HazardElement const& element : gHazard.elements) {
        SetBrightness(element.mic, element.lamp ? levels.lamp : levels.wash);
    }
}

std::string HazardJson() {
    namespace json = vr::test_request;
    return std::format("{{\"force\": {}, \"alarm\": {}, \"applied\": {}, \"health\": {}, \"shields\": {}, \"lamps\": {}, \"washed\": {}, "
                       "\"elements\": {}, \"episodes\": {}, \"why\": {}}}",
        json::JsonString(ForceName(gHazard.force)), gHazard.alarm ? "true" : "false", gHazard.applied ? "true" : "false",
        gHazard.health ? json::JsonNumber(*gHazard.health) : std::string("null"), gHazard.shields ? json::JsonNumber(*gHazard.shields) : std::string("null"),
        gHazard.lamps, gHazard.washed, gHazard.elements.size(), gHazard.episodes, json::JsonString(gHazard.why));
}

std::string HazardLine() {
    return std::format("hazard {} ({}): alarm {}, {}; hull {}, shields {}; {} episode{}", ForceName(gHazard.force),
        vr::cockpit_hazard::Describe(hand_calibration::GetCockpit().hazard), gHazard.alarm ? "on" : "off", gHazard.why,
        gHazard.health ? std::format("{:.0f}%", *gHazard.health * 100.0f) : std::string("?"),
        gHazard.shields ? std::format("{:.0f}%", *gHazard.shields * 100.0f) : std::string("?"), gHazard.episodes, gHazard.episodes == 1 ? "" : "s");
}

// `mevr cockpit hazard [on|off|auto|state|health <fraction>]`: force the red lights on or off, back to the hull's health, or
// (for tests) set the ridden Mako's hull health to a fraction of its maximum (journalled).
std::string DoHazard(std::string const& rest) {
    std::istringstream words(rest);
    std::string sub;
    words >> sub;
    if (sub == "on" || sub == "off" || sub == "auto") {
        gHazard.force = sub == "on" ? Hazard::Force::On : sub == "off" ? Hazard::Force::Off : Hazard::Force::Auto;
        gHazard.retryAt = 0.0;
        return std::format("cockpit hazard: {} (it takes effect this tick)", ForceName(gHazard.force));
    }
    if (sub == "health") {
        double fraction = -1.0;
        words >> fraction;
        ue3::Object* const vehicle = gRide.vehicle != nullptr && ue3::runtime::IsAlive(gRide.vehicle) ? gRide.vehicle : nullptr;
        if (vehicle == nullptr || !(fraction >= 0.0 && fraction <= 1.0)) {
            return "usage: mevr cockpit hazard health <0..1> (while riding in the Mako)";
        }
        auto const hull = ReadHull(vehicle);
        ue3::Object* const behavior = gVehicleBehavior.Read<ue3::Object*>(vehicle).value_or(nullptr);
        ue3::Object* const attributes = behavior != nullptr ? gPawnAttributes.Read<ue3::Object*>(behavior).value_or(nullptr) : nullptr;
        float* const current = attributes != nullptr ? gHealthCurrent.In<float>(attributes) : nullptr;
        if (!hull || current == nullptr || !(hull->first > 0.0f)) {
            return "cockpit hazard: the Mako's health can't be read (or is 0)";
        }
        float const max = *current / hull->first;
        float const before = *current;
        *current = static_cast<float>(fraction) * max;
        write_journal::Wrote("CockpitProbe.hazardHealth", current, sizeof(float));
        gHazard.lastRead = -1e9;  // read again at once
        return std::format("cockpit hazard: the Mako's hull {:.0f} -> {:.0f} of {:.0f} ({:.0f}%)", before, *current, max, fraction * 100.0);
    }
    return HazardLine();
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
    if (verb == "screenpart") {
        return DoScreenPart(Rest(words));
    }
    if (verb == "variant") {
        return DoVariant(Rest(words));
    }
    if (verb == "hazard") {
        return DoHazard(Rest(words));
    }
    if (verb == "loaded") {
        return DoLoaded(words);
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
    if (verb == "aimscreen") {
        return DoAimScreen(words);
    }
    if (verb == "aimreticle") {
        return DoAimReticle(words);
    }
    if (verb == "capturehide") {
        int mode = 2;
        words >> mode;
        if (mode < 0 || mode > 3) {
            return "usage: mevr cockpit capturehide 0|1|2|3 (none; owned by the chase capture, bOwnerNoSee; owned by the pawn or the controller, bOnlyOwnerSee)";
        }
        gCaptureHide = mode;
        HideFromCaptures();
        // What the owner test compares: the first part's owner now, and the camera's view target (UE3's view actor for the
        // eyes [verify]).
        ue3::Object* const controller = player::Controller();
        ue3::Object* const camera = controller != nullptr ? player::Camera(controller) : nullptr;
        ue3::Object* const firstOwner = !gParts.empty() && PartAlive(gParts.front()) ? gOwnerField.Read<ue3::Object*>(gParts.front().actor).value_or(nullptr) : nullptr;
        return std::format("capturehide {} applied to {} part{}; the first part's Owner {}; pawn {}, controller {}, the camera's view target {}", gCaptureHide,
            gParts.size(), gParts.size() == 1 ? "" : "s", ue3::runtime::FullName(firstOwner), ue3::runtime::FullName(controller != nullptr ? player::Pawn(controller) : nullptr),
            ue3::runtime::FullName(controller), ue3::runtime::FullName(camera != nullptr ? gCameraViewTargetActor.Read<ue3::Object*>(camera).value_or(nullptr) : nullptr));
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
    if (verb == "capturedist") {
        // How far the screens' captures draw (each capture renders the world again: at heavy views they took frames over budget).
        float metres = -1.0f;
        if (!(words >> metres) || metres < 0.0f) {
            return "usage: mevr cockpit capturedist <metres: the screens draw nothing further; 0 = the engine's own>";
        }
        gSettings.viewDistM = metres;
        float const units = static_cast<float>(metres * engine::kUnitsPerMeter);
        for (ue3::Object* const component : {gState.captureComponent, gAim.captureComponent}) {
            if (component != nullptr) {
                WriteField(gMaxViewDistance, component, units, "CockpitProbe.MaxViewDistance");
            }
        }
        return std::format("the screens' captures draw up to {}", metres > 0.0f ? std::format("{:.0f} m", metres) : std::string("the engine's distance"));
    }
    if (verb == "status") {
        return StatusLine();
    }
    if (verb == "ride") {
        return DoRide(words);
    }
    if (verb == "hud") {
        return DoHud(words);
    }
    if (verb == "seat") {
        return DoSeat(words);
    }
    if (verb == "panel") {
        return DoPanel(words);
    }
    if (verb == "clear") {
        ClearAll("mevr cockpit clear");
        ClearParts("mevr cockpit clear");
        return "cleared (the render target and the instance go with the garbage)";
    }
    return kUsage;
}

// ---- The cockpit by itself ([Cockpit], docs/mako-cockpit-goal.md item 1) --------------------------------------------

// Entering the Mako runs the cockpit's script once (the same lines as the console, without `mevr cockpit`); leaving it, a
// level change or `enabled = 0` takes everything away. A script that fails isn't run again until the next entry.
// Leaving parks it (the parts hidden, the captures off) rather than destroying it: the next entry in the same level shows the
// parked one again at once, with the screen's render target it already has. (Until ue3::runtime::Call cleared a native's
// opcode for the call, Actor.Destroy never ran at all: research/47 "Why Trace never hit".)
struct Auto {
    ue3::Object* vehicle = nullptr;
    void const* world = nullptr;
    bool ran = false;
    bool failed = false;
    bool parked = false;
};
Auto gAuto;
bool gLoggedNoScript = false;

void ShowCockpit(bool show) {
    gAim.parked = !show;
    if (Still(gAim.reticle, gActorClass)) {
        SetActorHidden(gAim.reticle, !show);
        FlushActor(gAim.reticle, false);
    }
    for (Part const& part : gParts) {
        if (PartAlive(part)) {
            ue3::Params hide = ParamsOf(gSetHidden);
            hide.SetBool("bNewHidden", !show);
            Call(part.actor, gSetHidden, hide);
        }
    }
    for (ue3::Object* const capture : {gState.captureComponent, gAim.captureComponent}) {
        if (Still(capture, gCaptureComponent)) {
            ue3::Params enable = ParamsOf(gCaptureSetEnabled);
            enable.SetBool("bEnable", show);
            Call(capture, gCaptureSetEnabled, enable);
        }
    }
}

void Park(char const* why) {
    gRide.on = false;
    ShowVehicle();
    ShowCockpit(false);
    gAuto.parked = true;
    log::Info("cockpit: {}: the cockpit is parked (hidden, its captures off)", why);
}

bool Unpark() {
    bool const alive = !gParts.empty() && std::all_of(gParts.begin(), gParts.end(), [](Part const& part) { return PartAlive(part); });
    if (!gAuto.parked || !alive) {
        return false;
    }
    ShowCockpit(true);
    gRide.on = true;
    gAuto.parked = false;
    log::Info("cockpit: back in the Mako: the parked cockpit is shown again");
    return true;
}

bool RunScript(std::filesystem::path const& path) {
    std::ifstream in(path);
    if (!in) {
        if (!gLoggedNoScript) {
            gLoggedNoScript = true;
            log::Warn("cockpit: no script at {} (tools/cockpit/place.py --script writes it): no cockpit", ini::Utf8(path));
        }
        return false;
    }
    int lines = 0;
    for (std::string line; std::getline(in, line);) {
        std::size_t const start = line.find_first_not_of(" \t\r");
        if (start == std::string::npos || line[start] == ';' || line[start] == '#') {
            continue;
        }
        line = line.substr(start);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        for (std::string_view const prefix : {std::string_view("mevr cockpit "), std::string_view("cockpit ")}) {
            if (line.starts_with(prefix)) {
                line = line.substr(prefix.size());
            }
        }
        std::string const answer = Command(line);
        log::Info("cockpit: {} -> {}", line, answer.substr(0, answer.find('\n')));
        ++lines;
    }
    bool const ok = !gParts.empty() && gRide.on;
    log::Info("cockpit: script {}: {} lines, {} parts, ride {}{}", ini::Utf8(path), lines, gParts.size(), gRide.on ? "on" : "off",
        ok ? "" : ": no cockpit (the package installed and in the TOC? the script's last line `ride`?)");
    return ok;
}

void AutoTick() {
    hand_calibration::Cockpit const settings = hand_calibration::GetCockpit();
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    bool const inMako = ue3::IsA(pawn, gVehicleBase);
    if (gAuto.world != nullptr && gAuto.world != engine::World()) {
        gAuto = {};  // a new level: the old one's cockpit went with it (the probe forgets it on its own)
    }
    if (!settings.enabled || !inMako) {
        if (gAuto.ran && !gAuto.parked) {
            Park(!settings.enabled ? "[Cockpit] enabled = 0" : "out of the Mako");
        }
        gAuto.vehicle = nullptr;
        gAuto.failed = false;
        return;
    }
    if ((gAuto.ran && !gAuto.parked) || (gAuto.failed && gAuto.vehicle == pawn)) {
        return;
    }
    if (player::GameMode(controller) != 1) {
        return;  // the drop's cinematic: once it hands over
    }
    gAuto.vehicle = pawn;
    gAuto.world = engine::World();
    if (Unpark()) {
        return;
    }
    bool const ok = RunScript(engine::SettingsFile().parent_path() / std::filesystem::path(std::u8string(settings.script.begin(), settings.script.end())));
    gAuto.ran = ok;
    gAuto.failed = !ok;
    gAuto.parked = false;
}

bool TryStartup() {
    ue3::Function* const update = ue3::runtime::Ready() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("cockpit probe: Engine.Camera.UpdateCamera not found: `ride` can't move the camera");
        return false;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);  // inert until `mevr cockpit ride`
    return true;
}

bool Riding() {
    return gRide.on && gRide.riding;
}


std::optional<Seat> CurrentSeat() {
    if (gSeated.pawn == nullptr || gSeated.mesh == nullptr || !Riding() || !gRide.haveEye || !gSeatLayout.set || gSeated.world != engine::World()) {
        return std::nullopt;
    }
    auto const at = [](engine::UnrealVector const& m) {
        engine::UnrealVector const off = engine::Apply(gRide.frame, {m.y * gRide.upm, m.x * gRide.upm, m.z * gRide.upm});
        return engine::UnrealVector{gRide.eyeWorld.x + off.x, gRide.eyeWorld.y + off.y, gRide.eyeWorld.z + off.z};
    };
    Seat seat;
    seat.driver = gSeated.pawn;
    seat.mesh = gSeated.mesh;
    mako_controls::Pose const pose = mako_controls::CurrentPose();
    seat.held[0] = pose.leverHeld;
    seat.held[1] = pose.stickHeld;
    seat.haveAxes = gSeatLayout.haveAxes;
    seat.eye = gRide.eyeWorld;
    for (int side : {0, 1}) {
        seat.knee[side] = at(gSeatLayout.kneeM[static_cast<std::size_t>(side)]);
        seat.heel[side] = at(gSeatLayout.heelM[static_cast<std::size_t>(side)]);
        engine::UnrealVector grip = gSeatLayout.gripM[static_cast<std::size_t>(side)];
        engine::UnrealVector axis = gSeatLayout.axisM[static_cast<std::size_t>(side)];  // right, forward, up
        if (side == 0) {
            grip.y += static_cast<double>(pose.lever * pose.travelM);  // the lever's grip travels with it, as its model
        } else if (pose.tilt) {
            // The stick's grip and axis tilt about the stick model's pivot, as its model does.
            for (Part const& part : gParts) {
                if (part.motion == Part::Motion::Stick) {
                    engine::UnrealVector const p = part.offsetM;
                    engine::UnrealVector const turned = engine::Apply(StickTilt(pose), {grip.y - p.y, grip.x - p.x, grip.z - p.z});
                    grip = {p.x + turned.y, p.y + turned.x, p.z + turned.z};
                    engine::UnrealVector const axisTurned = engine::Apply(StickTilt(pose), {axis.y, axis.x, axis.z});
                    axis = {axisTurned.y, axisTurned.x, axisTurned.z};
                    break;
                }
            }
        }
        seat.grip[side] = at(grip);
        seat.axis[side] = engine::Apply(gRide.frame, {axis.y, axis.x, axis.z});
    }
    // The cockpit's panel (game/CockpitPanel, research/48 section 27). The extraction handle held: that hand closes on its bar
    // (the U's middle, where the grab is measured), swung out as far as the handle is, the knuckles along the bar (it runs along
    // the hinge's axis). The handle takes a new squeeze at itself overhead, so it never shares a hand with the lever or the stick.
    vr::cockpit_panel::Output const& panel = cockpit_panel::Current();
    if (gPanelLayout.set && panel.held && (panel.holdHand == 0 || panel.holdHand == 1)) {
        engine::UnrealVector const& h = gPanelLayout.hingeM;  // right, forward, up from the eye (metres)
        engine::UnrealVector const& a = gPanelLayout.hingeAxisM;  // unit (the `panel` line's parse)
        engine::UnrealVector const v{gPanelLayout.gripM.x - h.x, gPanelLayout.gripM.y - h.y, gPanelLayout.gripM.z - h.z};
        // v turned about a by the swing, right-handed as the panel's rule in tracking space (the eye's right, forward, up are a
        // right-handed frame there too): + swings the bar the way it's pulled (down, as its arrows point).
        double const t = static_cast<double>(panel.pullDeg) * 3.14159265358979323846 / 180.0;
        double const c = std::cos(t);
        double const s = std::sin(t);
        double const dot = a.x * v.x + a.y * v.y + a.z * v.z;
        engine::UnrealVector const cross{a.y * v.z - a.z * v.y, a.z * v.x - a.x * v.z, a.x * v.y - a.y * v.x};
        engine::UnrealVector const swung{v.x * c + cross.x * s + a.x * dot * (1.0 - c), v.y * c + cross.y * s + a.y * dot * (1.0 - c),
            v.z * c + cross.z * s + a.z * dot * (1.0 - c)};
        int const side = panel.holdHand;
        seat.held[side] = true;
        seat.grip[side] = at({h.x + swung.x, h.y + swung.y, h.z + swung.z});
        seat.axis[side] = engine::Apply(gRide.frame, {a.y, a.x, a.z});
    }
    if (panel.pressHand == 0 || panel.pressHand == 1) {
        seat.pointing[panel.pressHand] = !seat.held[panel.pressHand];
    }
    return seat;
}

void NoteSeatHips(ue3::Object const* mesh, ik::Vec3 const& hips, std::optional<ik::Vec3> const& head) {
    if (mesh != nullptr && mesh == gSeated.mesh) {
        gSeated.hips = hips;
        gSeated.haveHips = true;
        gSeated.haveHeadBone = head.has_value();
        if (head) {
            gSeated.headBone = *head;
        }
        gSeated.posedAt = Now();
    }
}

std::optional<vr::mako_controls::Seat> ControlSeat() {
    if (!Riding() || !gRide.haveEye || !gSeatLayout.set) {
        return std::nullopt;
    }
    auto const anchor = view_anchor::Current();
    if (!anchor) {
        return std::nullopt;
    }
    auto const at = [&](engine::UnrealVector const& m) {
        engine::UnrealVector const off = engine::Apply(gRide.frame, {m.y * gRide.upm, m.x * gRide.upm, m.z * gRide.upm});
        return engine::ToTracking(*anchor, {gRide.eyeWorld.x + off.x, gRide.eyeWorld.y + off.y, gRide.eyeWorld.z + off.z});
    };
    XrVector3f const forward = engine::DirectionToTracking(*anchor, gRide.frame.forward);
    float const length = std::hypot(forward.x, forward.z);
    if (!(length > 0.1f)) {
        return std::nullopt;
    }
    vr::mako_controls::Seat seat;
    seat.forward = {forward.x / length, 0.0f, forward.z / length};
    seat.right = {-seat.forward.z, 0.0f, seat.forward.x};  // forward x up, as PlaceSeat
    seat.leverBase = at(gSeatLayout.gripM[0]);
    seat.stickBase = at(gSeatLayout.gripM[1]);
    return seat;
}

std::optional<vr::cockpit_panel::Layout> PanelLayout() {
    if (!Riding() || !gRide.haveEye || !gPanelLayout.set) {
        return std::nullopt;
    }
    auto const anchor = view_anchor::Current();
    if (!anchor) {
        return std::nullopt;
    }
    auto const at = [&](engine::UnrealVector const& m) {
        engine::UnrealVector const off = engine::Apply(gRide.frame, {m.y * gRide.upm, m.x * gRide.upm, m.z * gRide.upm});
        return engine::ToTracking(*anchor, {gRide.eyeWorld.x + off.x, gRide.eyeWorld.y + off.y, gRide.eyeWorld.z + off.z});
    };
    auto const direction = [&](engine::UnrealVector const& m) {
        XrVector3f const d = engine::DirectionToTracking(*anchor, engine::Apply(gRide.frame, {m.y, m.x, m.z}));
        float const length = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
        return length > 1e-6f ? XrVector3f{d.x / length, d.y / length, d.z / length} : d;
    };
    vr::cockpit_panel::Layout layout;
    layout.buttonTop = at(gPanelLayout.buttonM);
    layout.buttonIn = direction(gPanelLayout.buttonInM);
    layout.travelM = gPanelLayout.travelM;
    layout.hinge = at(gPanelLayout.hingeM);
    layout.hingeAxis = direction(gPanelLayout.hingeAxisM);
    layout.grip = at(gPanelLayout.gripM);
    layout.pullDeg = gPanelLayout.pullDeg;
    layout.pullDir = direction({0.0, 0.0, -1.0});  // the cabin's down: the handle's arrows (the user, 2026-11-05: pulled, not pushed)
    return layout;
}

std::optional<float> HullHealth() {
    return Riding() ? gHazard.health : std::nullopt;
}

bool ShowPanelLamps(vr::cockpit_panel::Colour const* colours, int count) {
    if (!gPanelLayout.set || gSetMaterial.Get() == nullptr || gMic.Get() == nullptr) {
        return false;
    }
    Part const* base = nullptr;
    for (Part const& part : gParts) {
        if (PartAlive(part) && part.mesh != nullptr && Lower(R().NameText(part.mesh)) == "repair_base") {
            base = &part;
            break;
        }
    }
    if (base == nullptr) {
        return false;
    }
    if (gPanelLamps.component != base->component) {
        gPanelLamps.component = base->component;
        gPanelLamps.shown.fill(-1);
    }
    bool changed = false;
    int const lamps = std::min(count, gPanelLayout.healthCount + gPanelLayout.progressCount);
    for (int i = 0; i < lamps; ++i) {
        int const element = i < gPanelLayout.healthCount ? gPanelLayout.healthFirst + i : gPanelLayout.progressFirst + i - gPanelLayout.healthCount;
        int const colour = static_cast<int>(colours[i]);
        if (element < 0 || element >= static_cast<int>(gPanelLamps.shown.size()) || colour < 0 || colour >= static_cast<int>(gPanelLamps.instances.size()) ||
            gPanelLamps.shown[static_cast<std::size_t>(element)] == colour) {
            continue;
        }
        ue3::Object*& instance = gPanelLamps.instances[static_cast<std::size_t>(colour)];
        if (instance == nullptr || !ue3::runtime::IsAlive(instance)) {
            vr::cockpit_panel::Colour const c = colours[i];
            std::string const path = c == vr::cockpit_panel::Colour::Off ? std::string("BIOG_MEVR_Cockpit.Light_Off_MIC")
                                                                        : std::format("BIOG_MEVR_Cockpit.Light_{}_Glow_MIC", vr::cockpit_panel::Name(c));
            instance = LoadObject(path, gMic.Get());
            if (instance == nullptr) {
                if (!gPanelLamps.toldMissing) {
                    gPanelLamps.toldMissing = true;
                    log::Warn("cockpit panel: no {} in the cockpit's package (is it current?): the lamps stay as they are", path);
                }
                return false;
            }
        }
        ue3::Params set = ParamsOf(gSetMaterial);
        set.Set("ElementIndex", element);
        set.Set("Material", instance);
        Call(base->component, gSetMaterial, set);
        gPanelLamps.shown[static_cast<std::size_t>(element)] = colour;
        changed = true;
    }
    if (changed) {
        FlushActor(base->actor, false);
    }
    return true;
}

void FillHud(vr::hud_pieces::Pieces& pieces) {
    if (!Riding() || !gRide.haveEye || !gHudLayout.set) {
        return;
    }
    auto const anchor = view_anchor::Current();
    if (!anchor) {
        return;
    }
    // A point of the cockpit (metres from its eye: right, forward, up) in tracking space.
    auto const at = [&](engine::UnrealVector const& m) {
        engine::UnrealVector const off = engine::Apply(gRide.frame, {m.y * gRide.upm, m.x * gRide.upm, m.z * gRide.upm});
        return engine::ToTracking(*anchor, {gRide.eyeWorld.x + off.x, gRide.eyeWorld.y + off.y, gRide.eyeWorld.z + off.z});
    };
    pieces.cockpit = true;
    pieces.cockpitUp = engine::DirectionToTracking(*anchor, gRide.frame.up);
    pieces.statusAnchor = at(gHudLayout.statusM);
    pieces.statusWidthM = gHudLayout.statusWidthM;
    pieces.radarAnchor = at(gHudLayout.radarM);
    pieces.radarWidthM = gHudLayout.radarWidthM;
    pieces.panelAnchor = at(gHudLayout.panelM);
    pieces.panelWidthM = gHudLayout.panelWidthM;
}

// R3 (the right stick's click) steps the aiming screen's zoom while riding: its field 30, 12, 5 degrees and round again
// (docs/mako-cockpit-goal.md item 4). Read from the controllers, so it works whether or not the game takes the pad.
void ZoomTick() {
    static bool wasDown = false;
    bool const down = vr::controllers::Latest().active && vr::controllers::Latest().right.stickClick;
    bool const pressed = down && !wasDown;
    wasDown = down;
    if (!pressed || !gRide.riding || !Still(gAim.captureComponent, gCaptureComponent) || gAim.target == nullptr) {
        return;
    }
    constexpr std::array<float, 3> kFields{30.0f, 12.0f, 5.0f};
    auto const next = std::find_if(kFields.begin(), kFields.end(), [](float f) { return f < gAim.fovDeg - 0.5f; });
    gAim.fovDeg = next != kFields.end() ? *next : kFields.front();
    ue3::Params parameters = ParamsOf(gCaptureSetParameters);
    parameters.Set("NewTextureTarget", gAim.target);
    parameters.Set("NewFOV", gAim.fovDeg);
    parameters.Set("NewNearPlane", 20.0f);
    parameters.Set("NewFarPlane", 0.0f);
    Call(gAim.captureComponent, gCaptureSetParameters, parameters);
    log::Info("cockpit: R3: the aiming screen's field is {:.0f} degrees", gAim.fovDeg);
}

void Tick(std::optional<std::uint32_t> recenters) {
    AutoTick();
    RideTick();
    HazardTick();
    ZoomTick();
    if (gAngVelVehicle != nullptr && Now() >= gAngVelAt) {
        if (ue3::runtime::IsAlive(gAngVelVehicle)) {
            if (auto const turn = player::Rotation(gAngVelVehicle)) {
                auto const d = [](int a, int b) { return std::remainder((a - b) * 360.0 / 65536.0, 360.0); };
                log::Info("mevr vehicle: angvel ({}, {}, {}) for 0.25 s: pitch {:+.1f} yaw {:+.1f} roll {:+.1f} deg (from pitch {:.1f} yaw {:.1f} roll {:.1f})",
                    gAngVelAsked.x, gAngVelAsked.y, gAngVelAsked.z, d(turn->pitch, gAngVelFrom.pitch), d(turn->yaw, gAngVelFrom.yaw),
                    d(turn->roll, gAngVelFrom.roll), gAngVelFrom.pitch * 360.0 / 65536.0, gAngVelFrom.yaw * 360.0 / 65536.0, gAngVelFrom.roll * 360.0 / 65536.0);
            }
        }
        gAngVelVehicle = nullptr;
    }
    if (gFiringMode != nullptr && Now() >= gFireReleaseAt) {
        if (ue3::runtime::IsAlive(gFiringMode)) {
            ue3::Params p = ParamsOf(gFiringMg ? gStopFiringWeapon : gStopFiringCannon);
            Call(gFiringMode, gFiringMg ? gStopFiringWeapon : gStopFiringCannon, p);
        }
        gFiringMode = nullptr;
        gFiringMg = false;
    }
    static std::optional<std::uint32_t> seenRecenters;
    if (recenters && seenRecenters && *recenters != *seenRecenters && gRide.on) {
        gRide.seated = false;  // the player recentered in the seat: where the head is now is the cockpit's eye
        log::Info("cockpit: a recenter: the seat is taken again");
    }
    seenRecenters = recenters;
    if (gState.actor == nullptr && gState.captureActor == nullptr) {
        return;
    }
    Refresh();
    if (gState.actor != nullptr && !gState.part && gSettings.follow && !gState.attached && Place()) {
        ++gState.placedTicks;
    }
    // Not every tick unless the rate asks: each SetView makes the engine build a new probe, with a new view state.
    // The two screens never render in the same frame: on the same clock they did, and that frame drew the world three times (16% of
    // a drive's frames over 11.1 ms against 0.7% without the cockpit, research/47 "The cockpit's cost over a drive").
    bool pointed = false;
    if (gState.captureActor != nullptr && gSettings.aim && gSettings.captureRate > 0.0f && Now() - gState.lastPointed >= 1.0 / static_cast<double>(gSettings.captureRate)) {
        PointCapture();
        pointed = true;
    }
    if (!pointed && gAim.captureActor != nullptr && gSettings.captureRate > 0.0f && Now() - gAim.lastPointed >= 1.0 / static_cast<double>(gSettings.captureRate)) {
        PointAim();
    }
}

std::string ReticleReport() {
    if (!Still(gAim.reticle, gActorClass)) return "cockpit Aim_Reticle: absent";
    ue3::Object* const controller = player::Controller();
    return std::format("cockpit BIOG_MEVR_Cockpit.Aim_Reticle actor={} component={} owner={} pawn={} viewTarget={} bHidden={} bOwnerNoSee={} parked={} diagnosticEyes={}",
        ue3::runtime::FullName(gAim.reticle), ue3::runtime::FullName(gAim.reticleComponent),
        ue3::runtime::FullName(gOwnerField.Read<ue3::Object*>(gAim.reticle).value_or(nullptr)),
        ue3::runtime::FullName(player::Pawn(controller)),
        ue3::runtime::FullName(gCameraViewTargetActor.Read<ue3::Object*>(player::Camera(controller)).value_or(nullptr)),
        gHiddenField.ReadBool(gAim.reticle).value_or(false), gOwnerNoSee.ReadBool(gAim.reticleComponent).value_or(false),
        gAim.parked, gAim.showInEyes);
}

std::string StatusJson() {
    namespace json = vr::test_request;
    return std::format("{{\"up\": {}, \"actor\": {}, \"kind\": {}, \"mesh\": {}, \"mic\": {}, \"parent\": {}, \"parameter\": {}, \"size\": [{}, {}], \"capture\": {}, "
                       "\"attached\": {}, \"placedTicks\": {}, \"pointedTicks\": {}, \"hazard\": {}, \"panel\": {}, \"lastError\": {}}}",
        gState.actor != nullptr || gState.captureActor != nullptr ? "true" : "false", json::JsonString(ue3::runtime::FullName(gState.actor)), json::JsonString(gState.kind),
        json::JsonString(ue3::runtime::FullName(gState.mesh)), json::JsonString(ue3::runtime::FullName(gState.mic)), json::JsonString(ue3::runtime::FullName(gState.parent)),
        json::JsonString(gState.parameterSet ? ue3::runtime::NameText(gState.parameter) : std::string()), gState.target != nullptr ? gState.targetWidth : 0,
        gState.target != nullptr ? gState.targetHeight : 0, json::JsonString(ue3::runtime::FullName(gState.captureActor)), gState.attached ? "true" : "false",
        gState.placedTicks, gState.pointedTicks, HazardJson(), cockpit_panel::StatusJson(), json::JsonString(gLastError));
}

void LogStats() {
    if (gState.actor != nullptr || gState.captureActor != nullptr) {
        log::Info("    cockpit probe: {}", StatusLine());
    }
    if (gHazard.alarm || gHazard.applied || gHazard.health) {
        log::Info("    cockpit: {}", HazardLine());
    }
}

void Shutdown(bool exiting) {
    gRide.on = false;
    if (!exiting && ue3::runtime::Ready()) {
        HazardRestore("the mod shuts down");  // the cabin's own materials back before the parts go
    }
    gHazard = Hazard{};
    if (!exiting && ue3::runtime::Ready()) {
        ShowVehicle();
        if (gSeated.world == engine::World()) {
            HideDriver();
        }
    }
    gSeated = {};
    gRide = Ride{};
    gAuto = {};
    gLoggedNoScript = false;
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
    for (ue3::ClassRef* const c : {&gActorClass, &gPawnClassRef, &gInterpActor, &gKActor, &gKActorSpawnable, &gStaticMeshActor, &gDynamicSmActor, &gStaticMesh, &gStaticMeshComponent,
             &gMaterial, &gMaterialInstance, &gMic, &gTextureParameterExpression, &gCaptureActor, &gCaptureComponent, &gTargetClass, &gTexture, &gTexture2D, &gVehicleBase, &gBioPawn,
             &gBioController, &gSaveGame, &gRemoteEvent, &gPartySelection, &gPrimitiveComponent, &gGameModeVehicle}) {
        c->Reset();
    }
    for (ue3::FunctionRef* const f : {&gDynamicLoadObject, &gSpawn, &gDestroy, &gSetLocation, &gSetRotation, &gSetDrawScale, &gSetPhysics, &gSetCollision, &gSetHidden, &gSetBase, &gSetHardAttach,
             &gForceUpdate, &gSetActorCollision, &gSetBlockRigidBody, &gSetTraceBlocking, &gSetStaticMesh, &gGetMaterial, &gSetMaterial, &gGetNumElements, &gGetUnscaledBounds,
             &gCreateMic, &gSetParent, &gSetTextureParameter, &gGetTextureParameter, &gLightEnvironmentSetEnabled, &gCreateTarget, &gCaptureSetEnabled, &gCaptureSetFrameRate,
             &gCaptureSetView, &gCaptureSetParameters, &gEnterVehicle, &gFindClosestVehicle, &gCheckActivate, &gFinalizeParty, &gUpdateCamera, &gTryExitVehicle, &gBioExitVehicle, &gFastTrace, &gPlaceExitingDriver, &gFireCannon, &gStopFiringCannon,
             &gPrimitiveSetHidden, &gSetLightEnvironment, &gTakeDamage, &gReactToDeath, &gGameOverEvent, &gSetAngVel}) {
        f->Reset();
    }
    for (ue3::FieldRef* const f : {&gStatic, &gDeleteMe, &gNoDelete, &gMovable, &gRole, &gPhysics, &gBase, &gDynamicComponent, &gDynamicLightEnvironment, &gStaticActorComponent, &gComponentMesh,
             &gBounds, &gParent, &gStaticPermutation, &gTextureValues, &gTextureValueName, &gExpressions, &gEmissive, &gEmissiveExpression, &gBlendMode, &gLightingModel,
             &gUsedStaticLighting, &gUsedLightEnvironments, &gExpressionParameterName, &gSceneCapture, &gCaptureEnabled, &gEnablePostProcess, &gUseMainPostProcess,
             &gSkipUsersOccluded, &gSkipOwnerOccluded, &gMaxUpdateDist, &gMaxViewDistance, &gPostProcess, &gFrameRate, &gSizeX, &gSizeY, &gPlayer, &gPlayerPostProcess, &gDefaultPostProcess, &gUpdateMatrices,
             &gViewMatrix, &gForceLinearGamma, &gTargetFormat, &gMeshMaterials, &gPrimitiveLightEnvironment, &gLightEnvironmentEnabled, &gTextureResource, &gTextureSrgb, &gTextureNeverStream,
             &gTextureLodGroup, &gTexture2DSizeX, &gTexture2DSizeY, &gTexture2DFormat, &gTexture2DRequestedMips, &gTexture2DResidentMips, &gTexture2DMipTail, &gTexture2DMips, &gWorldInfo,
             &gTextureValueObject, &gRemoteEventName, &gStreamingLevels, &gStreamPackage, &gStreamLoaded, &gStreamShouldLoad, &gStreamShouldShow, &gStreamVisible,
             &gStreamPending, &gAllComponents, &gOwnerNoSee, &gVehicleDriver, &gPawnHealth, &gPlayersOnly, &gPlayersOnlyPending, &gHiddenField, &gHiddenGame,
             &gOnlyOwnerSee, &gVehicleBehavior, &gPawnControllerField}) {
        f->Reset();
    }
    for (ue3::FieldRef& f : gHeadMeshes) {
        f.Reset();
    }
}

#else

std::string ReticleReport() { return "cockpit Aim_Reticle: LE1 only"; }

std::string Command(std::string const& /*line*/) {
    return "mevr cockpit: LE1 only";
}

bool TryStartup() {
    return false;
}

bool Riding() {
    return false;
}

void FillHud(vr::hud_pieces::Pieces& /*pieces*/) {}

std::optional<Seat> CurrentSeat() {
    return std::nullopt;
}

std::optional<vr::mako_controls::Seat> ControlSeat() {
    return std::nullopt;
}

std::optional<vr::cockpit_panel::Layout> PanelLayout() {
    return std::nullopt;
}

std::optional<float> HullHealth() {
    return std::nullopt;
}

bool ShowPanelLamps(vr::cockpit_panel::Colour const* /*colours*/, int /*count*/) {
    return false;
}

void NoteSeatHips(ue3::Object const* /*mesh*/, ik::Vec3 const& /*hips*/, std::optional<ik::Vec3> const& /*head*/) {}

std::string VehicleCommand(std::string const& /*line*/) {
    return "mevr vehicle: LE1 only";
}

void Tick(std::optional<std::uint32_t> /*recenters*/) {}

std::string StatusJson() {
    return "{\"up\": false}";
}

void LogStats() {}

void Shutdown(bool /*exiting*/) {}

#endif

} // namespace mevr::game::cockpit_probe
