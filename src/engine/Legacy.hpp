// SPDX-License-Identifier: MIT
#pragma once

// LE2 and LE3 until their reflection layouts exist (research/34, stage 5): the few engine reads and
// calls the all-game modules (head tracking, the console request, the reflection fix, hand aim's
// aim query) need there, still from LExSDKv2, but in our layer's types so the modules themselves
// carry no LExSDKv2 code. LE1 never uses this: our reflection layer (ue3/) serves it, and these
// functions aren't even built for it. Stage 5 deletes this file.
//
// Game thread only, as the modules calling it.

#include "ue3/Types.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace mevr::hook {
class Context;
}

namespace mevr::engine::legacy {

#if !defined(MEVR_GAME_LE1)

// Resolves the LExSDKv2 globals the functions below read (GMalloc, GObjObjects, the name pools, GEngine,
// GWorld; engine/Engine's ResolveGlobals until stage 3) with LExSDKv2's own resolver, built here from the hook
// context's loader interface (stage 4: nothing outside this file and ReflectCheck makes one). Mod startup; false
// (logged) if one fails.
bool Init(hook::Context& context);

// *GWorld, as an identity (engine::World()).
void const* World();
// GObjObjects' count (the heartbeat).
std::int32_t ObjectCount();

// GEngine->GamePlayers[0]->Actor, or nullptr.
ue3::Object* LocalPlayerController();
// "Engine.Camera.UpdateCamera": that class's own function, or nullptr (its package not loaded yet).
ue3::Function* FindFunction(char const* path);
// "Function Engine.Camera.UpdateCamera", for the log.
std::string FullName(ue3::Object const* object);

bool IsPlayerController(ue3::Object const* object);
// A BioPlayerController's game mode (SFXGameModeManager.CurrentMode, EGameModes), if it has a manager.
std::optional<std::uint8_t> GameMode(ue3::Object const* controller);
// The controller's pawn's location, if it has a pawn.
std::optional<ue3::Vector> PawnLocation(ue3::Object const* controller);

// Engine.Camera: its PCOwner, and CameraCache.POV.FOV (writable).
ue3::Object* CameraOwner(ue3::Object const* camera);
float* CameraFov(ue3::Object* camera);

// Parameter blocks of the hooked functions, inside the engine's block (writable: out-parameters and
// return values go back to the caller).
ue3::Vector* ViewPointLocation(void* parms);   // Controller.GetPlayerViewPoint(out_Location)
ue3::Rotator* ViewPointRotation(void* parms);  // ...(out_Rotation)
float* FovAngleReturn(void* parms);            // PlayerController.GetFOVAngle's ReturnValue
ue3::Rotator* AdjustedAimReturn(void* parms);  // BioPlayerController.GetAdjustedAimFor's ReturnValue

// PlayerController.ConsoleCommand(command, bWriteToLog false) through ProcessEvent: what it printed.
std::string ConsoleCommand(ue3::Object* controller, ue3::Function* function, std::string const& command);

// The reflection fix's pass over GObjects: every live (not a template, not pending kill)
// SceneCaptureReflectComponent among objects [begin, begin + limit) whose FrameRate is below `rate`
// gets SceneCaptureComponent.SetFrameRate(rate). Returns where the pass continues, counts what it
// saw, and says whether that was the end of GObjects.
struct CaptureCounts {
    unsigned seen = 0;
    unsigned raised = 0;
    unsigned failed = 0;
    bool done = false;
};
int RaiseReflectionCaptures(ue3::Function* setFrameRate, float rate, int begin, int limit, CaptureCounts& counts);

#endif

} // namespace mevr::engine::legacy
