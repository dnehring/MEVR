// SPDX-License-Identifier: MIT
#pragma once

// ProcessEvent only on the game thread (research/36). UnrealScript's VM keeps its state in process-wide globals,
// so a call from another thread can corrupt the game thread's script mid-statement (the 2026-10-23 crash:
// Halcyon's Present-thread hides). Halcyon's ProcessEvent callers ([MEVR] seams in head_aim.cpp and
// game_camera.cpp, through MevrBridge) queue their call here when they run off the game thread; the host runs
// the queue at the start of the next engine tick, on objects still alive. Both Halcyon hosts: LE1's, and ME2's renderer
// (engine_probe.cpp's MevrCallProcessEvent, through its MevrBridge::QueueProcessEvent) since 2026-11-14; each game's
// object layout from engine/GameAdapter's ReflectionLayout.

#include <cstdint>

namespace mevr::host::game_thread {

// The engine tick's thread (crash::gGameThread). False before the first tick.
bool OnGameThread();

// Queues `function` on `object` with a copy of the function's parameter block (its ParmsSize bytes of `parms`).
// True if queued; false if it couldn't be (the block too big, or unreadable).
bool Queue(void* object, void* function, void const* parms);
// The same with the caller's own block size (ME2's renderer passes it): `size` bytes of `parms` are copied.
bool Queue(void* object, void* function, void const* parms, std::uint32_t size);

// Game thread, at the start of each engine tick: runs what was queued, oldest first; a call whose object died
// (left GObjects, pending kill, or its memory now another class's) is skipped. Logs its counts now and then.
void Drain();

} // namespace mevr::host::game_thread
