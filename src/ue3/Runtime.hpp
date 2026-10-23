// SPDX-License-Identifier: MIT
#pragma once

// The game's side of our reflection layer (research/34): finds GObjects and the name pools in the
// running exe (the RVA from engine/GameAdapter, checked, else a byte pattern), checks the layout
// against what it reads, and then serves lookups and ProcessEvent calls. Nothing here crashes on a
// wrong guess: every read during the checks is guarded, a failed check leaves the layer off
// (Ready() false, Status() says why), and the modules using it stand down.
//
// Game thread only (the caches aren't locked). Init after the script packages are loaded.

#include "ue3/Params.hpp"
#include "ue3/Reflect.hpp"

#include <cstdint>
#include <string>

namespace mevr::ue3::runtime {

// Finds and checks everything once; later calls return the first result. Logs what it found.
bool Init();
bool Ready();
std::string const& Status();
// Drops the layer (the mod unloads); Init finds it again.
void Shutdown();

// Valid only while Ready().
Reflection& Get();

// How each global was found, for the self-check and the log.
struct Found {
    std::uintptr_t module = 0;
    std::uintptr_t objects = 0;    // &GObjects (the TArray)
    std::uintptr_t namePools = 0;  // the pools' base pointer array
    std::uintptr_t processEvent = 0;  // from the vtable slot
    char const* objectsBy = "none";   // "rva", "pattern" or "none"
    char const* namePoolsBy = "none";
    bool processEventMatchesRva = false;
};
Found const& Where();

// Runs fn(context) with access violations caught (false if one happened). For reads whose
// layout isn't proven yet; fn mustn't rely on destructors running if it faults.
bool Guarded(void (*fn)(void*), void* context);

// UObject::ProcessEvent through the object's vtable (the way the engine itself calls it, so the
// host's hook and Halcyon's see it). `parms` must be the function's full parameter block.
void CallProcessEvent(Object* object, Function* function, void* parms);
// The same with a Params block; false (nothing called) if not Ready or either pointer is null.
bool Call(Object* object, Function* function, Params& params);

// Engine.GameEngine's live instance (not its Default__ object), found once by class.
Object* GameEngine();
// GameEngine.GamePlayers[0].Actor, or nullptr.
Object* LocalPlayerController();
// A live GObjects entry, not pending kill (see engine::IsAlive for when that check means anything).
bool IsAlive(Object const* object);

// Logs and clears the reflection's notes (a class found in another package...).
void FlushNotes();

} // namespace mevr::ue3::runtime
