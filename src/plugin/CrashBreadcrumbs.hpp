// SPDX-License-Identifier: MIT
#pragma once

// Crash breadcrumbs (research/36): what touched the engine just before a crash. The host records the last
// ProcessEvent dispatches (its hook), the mod's CallFunction handlers and the mod's writes into engine memory
// (HostServices v18), each into a lock-free ring (core/Breadcrumbs). When an exception reaches the unhandled
// exception filter (the one that ends the process), the host writes them out once, names resolved then with
// guarded reads into a static buffer: %LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt and the host log.
// Halcyon's own [CRASH] filter stays: ours chains to whichever filter was there before it.

#include "core/Breadcrumbs.hpp"

#include <atomic>
#include <cstdint>

class UFunction;
class UObject;

namespace mevr::host::crash {

inline constexpr std::size_t kDispatches = 128;
inline constexpr std::size_t kWrites = 128;

// ProcessEvent, all threads: begun in the host's hook before the engine's call (so the one that crashes is
// there), advanced when the engine returns and when the mod's handler does.
extern breadcrumbs::DispatchRing<kDispatches> gProcessEvents;
// The mod's CallFunction post-handlers (script-to-script calls we hook), around each handler.
extern breadcrumbs::DispatchRing<kDispatches> gScriptCalls;
// The mod's writes into engine memory.
extern breadcrumbs::WriteRing<kWrites> gWrites;

// The engine's tick thread (the game thread), for the report.
inline std::atomic<std::uint32_t> gGameThread{0};
inline void NoteGameThread() {
    if (gGameThread.load(std::memory_order_relaxed) == 0) {
        gGameThread.store(breadcrumbs::ThreadId(), std::memory_order_relaxed);
    }
}

// Once, at attach: the unhandled exception filter, a first-chance recorder for fatal codes, and what name
// resolution needs later (LE1: our reflection layout; elsewhere pointers only).
void Install();

// HostServices v18 (mod/ModApi.hpp).
void RecordWrite(char const* tag, void const* address, std::uint32_t size, std::uint64_t value);
std::uint64_t BeginScriptCall(UFunction* function, UObject* object);
void EndScriptCall(std::uint64_t token);

} // namespace mevr::host::crash
