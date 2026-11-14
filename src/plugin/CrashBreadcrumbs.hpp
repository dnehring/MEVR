// SPDX-License-Identifier: MIT
#pragma once

// Crash breadcrumbs (research/36): what touched the engine just before a crash. The host records the last
// ProcessEvent dispatches (its hook), the mod's CallFunction handlers and the mod's writes into engine memory
// (HostServices v18), each into a lock-free ring (core/Breadcrumbs). When an exception reaches the unhandled
// exception filter (the one that ends the process), the host writes them out once, names resolved then with
// guarded reads into a static buffer: %LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt and the host log.
// Halcyon's own [CRASH] filter stays: ours chains to whichever filter was there before it.

#include "core/Breadcrumbs.hpp"
#include "core/FixedText.hpp"

#include <atomic>
#include <cstdint>

namespace mevr::ue3 {
struct Object;
struct Function;
}

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

// The process is ending (the host's ExitProcess hook, with the game's exit code; LEBinkProxy's detach, 0). From then
// on a fault is no crash of play: the game has deleted its locks while its workers still run (research/36 "The fault at
// quit"). The unhandled filter writes crash-exit.txt instead of crash-breadcrumbs.txt (so no crash offer, no problem
// report's crash), logs one line and ends the process with that code before Windows' error reporting sees it; no
// crash-firstchance.txt either.
void NoteProcessExit(unsigned exitCode);

// "Class Outer.Name [0x...]" through guarded reads (LE1; elsewhere the pointer), for log lines. Any thread, after
// Install; slow (each read checks its page), so not for hot paths.
void AppendObject(text::Appender& out, void const* object);

// HostServices v18 (mod/ModApi.hpp).
void RecordWrite(char const* tag, void const* address, std::uint32_t size, std::uint64_t value);
std::uint64_t BeginScriptCall(ue3::Function* function, ue3::Object* object);
void EndScriptCall(std::uint64_t token);

} // namespace mevr::host::crash
