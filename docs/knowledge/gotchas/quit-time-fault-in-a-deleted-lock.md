---
type: Gotcha
title: Every quit crashes in a lock the game's teardown deleted, if anything delays the process's end
description: LE1's teardown destroys a thread library's global mutex (exe+0x1BA22C0) while that library's worker still runs; the worker's next lock writes through the freed DebugInfo (AV at ntdll+0x1A12D writing 0x24) within milliseconds; anything that waits in our ExitProcess hook (the mod's shutdown, the voice recognizer's joins) turned that into a crash on every quit, with a crash report and the menu's crash offer; plugin/KeptLocks keeps the exe's static critical sections from deletion and the hook ends the process at once.
tags: [exit, crash, threads, host]
status: stable
timestamp: 2026-11-13T17:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-13T17:00:00Z }
bitten: [2026-10-07, 2026-10-25, 2026-11-05, 2026-11-13]
---

# Symptom

| date | wording |
|---|---|
| 2026-10-07 to 10-26 | Windows' Application log: Event 1000, MassEffect1.exe, faulting module ntdll.dll, offset 0x1a12d, 7-17 a day |
| 2026-10-25 | the host log: an AV at ntdll+0x1A12D writing 0x24 on exe+0x1BA22C0, 27 ms after "game exiting" (the mod's shutdown in the hook) |
| 2026-11-05 to 11-13 | the same Event 1000 again, 15-43 a day; every fake-headset session's log ends `game exiting (code 0)` then `first-chance access violation ... at ntdll.dll+0x1A12D (writing 0x24)` and `crash breadcrumbs` (38 of 38 quits) |
| 2026-11-13 | the conversations session: "every quit faults on a lock the game has already deleted"; the menu says "The game crashed last session" after every normal quit |

# Cause

The game's exit (`exit()`'s teardown, before ExitProcess) destroys the global mutex of a thread library linked into
the exe: `exe+0xD084F0(nullptr)` -> `exe+0xD1BDC0`, which calls DeleteCriticalSection on `exe+0x1BA22C0` but leaves
the mutex's own "initialized" byte (+0x2C) set. ntdll's delete frees the section's DebugInfo and zeroes the section,
and a zeroed section reads as held. One of that library's workers (thread start `exe+0xD1CA80`, user function in
`exe+0xD08...`) is still running; its next lock (`exe+0xD1C780`, trusting the stale byte) calls EnterCriticalSection,
contends, and `RtlpWaitOnCriticalSection` increments `DebugInfo->ContentionCount` (+0x24) through the NULL pointer.

The worker reaches the lock 2-27 ms after the deletion. The real ExitProcess, like TerminateProcess, kills it first if
it comes soon enough. So any wait between the game's teardown and the process's end turns the race into a crash on
every quit:
- 2026-10-25: the mod's shutdown inside our ExitProcess hook (unhooking freezes and thaws every thread).
- 2026-11-05: `host::SetProcessExiting` began stopping the voice recognizer and the model tier (thread joins, the
  WASAPI capture's stop) inside the same hook. The quits were clean from 10-27 to 11-04, between the two.

Measured 2026-11-13 (research/36 "The fault at quit"): with the joins, 3 of 3 quits crashed and the hook never reached
its next log line. Without them (0.12-0.17 ms hook, then TerminateProcess), 8 of 8 ended without a report or an Event
1000, but one of the 8 still logged the worker's fault in the same millisecond, beaten only by TerminateProcess.

# Check

Grep the session's `LE1MEVR.log` after `game exiting` for `ntdll.dll+0x1A12D`. The exit line says how long the hook
took and whether the game deleted a static lock (`kept locks: 1 deletion(s) ... exe+0x1BA22C0`). In Windows: Event
Viewer, Application, Event 1000 for MassEffect1.exe. The side-session script `exit-crash-scan.py` tabulates every
session's quit.

# Fix in place

- `src/plugin/KeptLocks.cpp`: a pre-hook on ntdll's `RtlDeleteCriticalSection` skips the deletion of any critical
  section inside the exe's image (static data, never freed). The lock stays valid until the process is gone, and the
  worker waits on a live lock or takes it. Other modules' and heap locks pass through.
- `src/plugin/Entry.cpp` `ExitProcessHook`: nothing that waits. It sets the exiting flags, starts Setup if asked,
  tells Halcyon, logs one line and calls TerminateProcess itself (Halcyon's [EXITKILL] hook does the same under ours).
  The voice stops moved to `host::StopWorkersForUnload`, called only from LEBinkProxy's detach, where the game's
  threads are already gone.
- `src/plugin/CrashBreadcrumbs.cpp`: after `crash::NoteProcessExit`, an unhandled fault writes `crash-exit.txt`
  (never `crash-breadcrumbs.txt`, so no crash offer), logs `a fault while the game quits`, and ends the process with
  the game's exit code before Windows' error reporting sees it.
- When it comes back: something new waits in the exit hook (a join, a flush, a DLL's shutdown), or the game deletes
  another lock that lives outside the exe's image (a heap object: KeptLocks doesn't cover it). Read the fault's `rcx`
  against the exe's range.

# Citations

[1] [research/36](../../../research/36-crash-breadcrumbs.md) "The fault at quit".
[2] [src/plugin/Entry.cpp](../../../src/plugin/Entry.cpp) `ExitProcessHook`; [src/plugin/KeptLocks.cpp](../../../src/plugin/KeptLocks.cpp).
[3] Halcyon's [EXITKILL] comment, `src/halcyon/le1/d3d_capture.cpp`.
