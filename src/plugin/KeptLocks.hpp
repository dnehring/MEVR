// SPDX-License-Identifier: MIT
#pragma once

// The game's static locks kept from deletion (research/36 "The fault at quit"; LE1). During its teardown the game
// destroys a thread library's global mutex (exe+0x1BA22C0: DeleteCriticalSection through the library's destroy, which
// leaves its own "initialized" byte set) while that library's worker still runs; the worker's next lock enters the
// zeroed critical section, contends, and RtlpWaitOnCriticalSection writes through its freed DebugInfo: an AV at
// ntdll+0x1A12D writing 0x24, on most quits. A pre-hook on ntdll's RtlDeleteCriticalSection skips the deletion of any
// critical section inside the exe's own image (static data, never freed or reused; deleted only by teardown code): the
// lock stays valid until the process is gone. Other modules' locks and every heap lock pass through untouched.

#include <string>

namespace mevr::hook { class Context; }

namespace mevr::host::kept_locks {

// At attach (LE1), before the game can tear anything down.
bool Install(hook::Context& context);
// For the exit line: how many deletions were skipped, where, and how long before now the first one came.
std::string Summary();

} // namespace mevr::host::kept_locks
