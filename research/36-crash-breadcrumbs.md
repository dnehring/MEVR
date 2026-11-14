# 36. Crash breadcrumbs, and script off the game thread

## Why

The game-thread crash of 2026-10-23 21:05 (WER minidump `MassEffect1.exe.56576.dmp`, Halcyon's
`[CRASH] code=0xC0000005 ... VCRUNTIME140.dll rva=0x1E579 access=write`) died in `memset` inside
`TArray::AddZeroed`, called from the script VM's `execLet` under `ProcessEvent`. A default WER minidump has
no heap pages, so the UFunction and UObject names could not be read. The breadcrumbs make the next crash
name what touched the engine last.

The read-only audit of that dump (scratchpad `crash-audit.md`, summarized here) then found the likely cause:
- The array pointer the VM used (GPropAddr, exe+0x16B6798) pointed into the **rendering thread's dead
  stack**, and GPropObject was 0.
- GPropAddr, GProperty, GPropObject and GRuntimeUCFlags (exe+0x16B6734) are process-wide globals. Every
  bytecode handler writes them without a lock, and EX_NativeParm sets GPropAddr = Locals + offset and
  GPropObject = 0.
- **Halcyon calls ProcessEvent on the Present (rendering) thread:**
  - `head_aim.cpp` `ProcessEventSEH`, used by `FpApplyHides`: SetHidden, SetOwnerNoSee, Hide/UnHideBoneByName,
    whenever the hide signature changes (weapon draw, holster, swap). Also the Mako turret's
    SetDesiredTurretAimPoint, every frame in the Mako.
  - `game_camera.cpp` `ProcessEventSEH`: the same setters, plus `GetPrimarySkelMeshComponentSEH`'s fallback.
- A native running there overwrote GPropAddr between the game thread's execLet setting it and reading it.
  The game thread then read a garbage Num from the dead stack and memset about 1 GB.
- Upstream's own comment (head_aim.cpp, "2026-07-08 crash fix") met this family of bugs earlier and made the
  writes edge-driven instead of moving them.

**Rule (CLAUDE.md): never call ProcessEvent (or anything that runs UnrealScript) off the game thread.**

## What is recorded

Each record is a few plain stores, a timestamp (`__rdtsc`), the thread id read from the TEB, and one
interlocked add (`core/Breadcrumbs.hpp`). A sequence number is written last, so a reader can tell a slot that
was overwritten, or is being written, from the record it expected. Nothing on the record path allocates,
locks or follows a pointer.

| ring | size | where it's recorded | what |
|---|---|---|---|
| ProcessEvent dispatches | 128 | host `ProcessEventHook`, **before** the engine's call | UFunction, object, thread, time; state IN ENGINE (no return yet) / IN MOD (the mod's handler running) / returned |
| the mod's CallFunction handlers | 128 | mod `engine/CallFunction`, around each post-handler (HostServices v18 `beginScriptCall` / `endScriptCall`) | the same, IN MOD / returned |
| the mod's writes into engine memory | 128 | `write_journal::Wrote(tag, address, size)` after each write (HostServices v18 `recordWrite`) | tag (text copied: hot reload can unmap the DLL), address, size, first 8 bytes, thread, first and last time, count |
| fatal-code first-chance exceptions | 16 | host vectored handler (last in line, records only) | code, address, AV target and operation, thread |

The write journal folds a write with the same tag, address and size as one of the last 16 entries into that
entry: its count goes up and its last time moves. Per-tick writes (the control rotation, SpaceBases) then keep
one entry each instead of flushing the ring.

Journalled sites:
- HandAim: the aim rotator, from CallFunction (`HandAim.aim (CF result)`) and ProcessEvent (`(PE return)`);
  the start-trace ReturnValue.
- Sights: the aim error (CallFunction result).
- Weapon: bIsZoomed, through `FieldRef::WriteBool`, which journals every bool write under the field's name.
- ViewLevel: ControlRotation, the zoom-snap target, and the adhesion and friction results.
- Comfort: ControlRotation (the pawn turns through SetRotation, a ProcessEvent in the dispatch ring).
- SquadWeapons: the HUD's nLastAngle and nAngle.
- Throwables: the disc's target location, target, heat-seek target and start location; the grenade's
  projectile class and its restore; the launch ReturnValue swap.
- HeadTracking (our own renderer's paths): the view point rotation and location, the camera FOV, the
  GetFOVAngle return.
- BonePose: SpaceBases (one entry per write, the bone range).

Holsters, Posture and MenuPause write nothing directly: they act through ProcessEvent calls, which are in the
dispatch ring. CoverAcquire skips a native and writes nothing.

## The report

The host installs an unhandled-exception filter at attach (`plugin/CrashBreadcrumbs`). It chains to the
previous filter, so Halcyon's `[CRASH]` line and WER's dump still happen, whichever was installed first.

When an exception reaches that filter, the host writes the report **once** per process. It skips a stack
overflow, which has no stack left to format on. Handled AVs (our probes, Halcyon's guarded reads: thousands
per session) never write a report. Only the first-chance ring sees them.

The report goes to `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt` (the previous one is kept as
`crash-breadcrumbs.prev.txt`) and to the host log (`LE1MEVR.log`). Its sections:
1. the exception: code, address as module+offset, AV target, the faulting thread, the game thread;
2. the registers;
3. the native stack, unwound from the fault with `RtlVirtualUnwind`;
4. the **script frames**: the faulting thread's stack, scanned from rsp to its base for FFrames, innermost
   first. Each is an 8-aligned slot whose vtable points into the exe's `.rdata`, whose Node is a listed
   Function, State or Class, and whose Object is listed. Each frame shows the function, the object, and the
   bytecode offset into the node's Script (UStruct+0x7C, the SDK's offset [verify]);
5. the first-chance exceptions before it;
6. the three rings, oldest first (the writes sorted by their latest time), times in ms before the report.

Names are resolved at crash time (LE1: our `ue3::Layout`, GObjects and name pools from `engine/GameAdapter`)
by `ue3/SafeNames`:
- every read goes through a reader that checks the page with VirtualQuery, then copies under `__try`;
- text goes into a 512 KB static buffer, never the heap;
- objects not in GObjects are flagged `(not in GObjects)`.

LE2 and LE3 print pointers only.

## ProcessEvent off the game thread: detection and the fix

- **Detection:** the host hook compares the caller with the game thread (the first `UGameEngine::Tick`'s
  thread). Calls from other threads are counted per function; the game thread logs each new one by name,
  each function at most every 10 s: `process event: off the game thread: Function Engine.Actor.SetHidden ...
  on thread N (count)`.
- **The fix, a `[MEVR]` seam in Halcyon** (`head_aim.cpp` and `game_camera.cpp` `ProcessEventSEH`): off the
  game thread, the call is queued (`MevrBridge::QueueProcessEvent`, `core/CallQueue`, `plugin/GameThreadCalls`).
  - What's queued: the object, the function, the object's class, and a copy of the function's ParmsSize bytes
    of parameters (at most 64).
  - The queue holds 64 calls and drops the oldest when full (counted).
  - The host runs it at the start of the next `UGameEngine::Tick`, on the game thread, skipping a call whose
    object left GObjects, is pending kill, or now has another class.
  - All the queued callers are setters (SetHidden, SetOwnerNoSee, Hide/UnHideBoneByName,
    SetDesiredTurretAimPoint, whose returned rotator is only logged and now logs as 0 when queued).
  - Cost: one frame of latency for a hide toggle or the turret's aim point.
- `GetPrimarySkelMeshComponentSEH` needs its return value, so off the game thread it uses the Pawn.Mesh field
  only (no ProcessEvent fallback).
- The other Halcyon ProcessEvent callers (`pchud.cpp`) call from inside its ProcessEvent hook, so they run on
  the thread of the call they hook. The detection line shows if that is ever not the game thread.

Log: `game thread calls:` (the first queued call by name, then the counts every 10 s while they change).

## Cost

Measured offline (`mevr-tests`, Release, this dev machine's CPU, one thread):

| what | per call |
|---|---|
| ProcessEvent: Begin + two state updates | about 15 ns |
| the off-game-thread check (a TEB read, one relaxed load, one compare) | under 1 ns (estimated, not measured) |
| journal write folded into a recent entry (the per-tick case) | about 8 ns |
| journal write as a new entry (the tag copied) | about 27 ns |

The game thread's ProcessEvent rate isn't measured yet [verify]. At an assumed 3,000 per frame and 90 fps that is about
4 ms of CPU a second, roughly 0.04 ms per frame. The mod's writes are a handful per tick. The render thread
pays nothing (it never runs ProcessEvent, now), and nothing is formatted or written until a crash.

## Reading it

Open `crash-breadcrumbs.txt` and work through it in this order:
- **The exception line:** module+offset. Use `tools/ghidra/decompile.ps1 exe+0x...` for the exe.
- **The script frames:** the innermost one is the function whose bytecode was running, its object, and the
  bytecode offset. To find the statement there, decompile it with Legendary Explorer.
- **ProcessEvent:** the last entries, especially any still IN ENGINE (no return seen) on the faulting thread,
  are the native-to-script calls in progress. An entry on another thread close to the crash is a red flag
  (see the rule above).
- **The writes:** anything of ours that landed in the last few ms, with its address and first 8 bytes.

## Risks

- The report runs inside a crashing process. Every read is page-checked and SEH-guarded, and the report itself
  is under `__try`, so a fault in it still lets Halcyon's filter and WER run. A corrupt loader lock would hang
  `GetModuleHandleEx` in the stack and module names.
- The rings tolerate races by design. A slot reused by a writer that lags a whole ring behind can mix two
  records; its seq usually flags it. Folding can race between threads and miscount by one.
- The FFrame scan can miss frames (an FFrame not 8-aligned, a Node not in GObjects) or find a stale one in dead
  stack below rsp. It starts at rsp, so dead frames below it are skipped.
- Another filter installed later without chaining would hide ours. Halcyon chains. The 21:05 crash reached
  Halcyon's filter, so the unhandled path is the one these crashes take.
- The queue changes Halcyon's behaviour by one frame: the hide state applies at the next tick.

## The fault at quit (2026-11-13)

**Symptom.** Every quit with MEVR installed ended in a crash: the host log's `game exiting (code 0)` was followed
within milliseconds by `first-chance access violation ... at ntdll.dll+0x1A12D (writing 0x24)` and a crash report,
the main menu offered "The game crashed last session" at the next start, and Windows logged an APPCRASH (Event 1000,
MassEffect1.exe, ntdll.dll, offset 0x1a12d). The game had already saved what it saves, so no progress was lost.

**History, from Event 1000 and the session logs.** 7-17 a day on 2026-10-07/08 and 10-21 to 10-26 (the mod's shutdown
inside our ExitProcess hook: the 2026-10-25 lesson in `Entry.cpp`), **none from 10-27 to 11-04**, then 15-43 a day from
11-05. On 11-05 `host::SetProcessExiting` began stopping the voice recognizer and the model tier (49acde0f): thread
joins and the WASAPI capture's stop, inside the same hook. Every fake-headset session with a recorded quit since then
crashed this way: 38 of 38 (30 console `exit`, 8 other), `exit-crash-scan.py` over `%LOCALAPPDATA%\MEVR\sessions`.

**Cause** (read in the decrypted exe; Ghidra's project was held by another process, so dumpbin's disassembly).
`rcx` at the fault is `exe+0x1BA22C0`, a global mutex of a thread library linked into the exe: a critical section, a
recursion count at +0x28, an "initialized" byte at +0x2C, a flag at +0x30.
- Its destroy (`exe+0xD084F0` with no handle -> `exe+0xD1BDC0`) calls DeleteCriticalSection and leaves +0x2C set.
- ntdll's delete frees the section's DebugInfo and zeroes the section, and a zeroed section reads as held.
- The library's worker (thread start `exe+0xD1CA80`, the faulting stack `+0xD08D4F` ... `+0xCF4E00` -> `+0xD1C7A2`)
  still runs. Its lock (`exe+0xD1C780`: EnterCriticalSection for an infinite wait, else TryEnter with Sleep(1) until a
  timeout) trusts the stale byte, contends, and `RtlpWaitOnCriticalSection` increments `DebugInfo->ContentionCount`
  (+0x24) through NULL.
- The deletion is part of the game's teardown before ExitProcess. ExitProcess, like TerminateProcess, kills the worker
  first if it comes soon enough. Our hook decided how soon.

**Measured on the fake headset** (`tools/fake-headset-session.ps1`, a run script that quits, relaunches through Steam
and quits again, keeping each launch's log; Event 1000 counted from Windows' Application log):

| build | quits | faulted (log) | crash report / offer | Event 1000 | the hook's work |
|---|---|---|---|---|---|
| main + timing (voice joins in the hook) | 2 console `exit` + 1 WM_CLOSE | 3 | 3 | 3 | never finished: the fault came 2, 12 and 8 ms after "game exiting", inside the joins |
| no joins, TerminateProcess at once | 5 `exit` + 3 WM_CLOSE | 1 (quit 1, same ms as the exit line) | 0 | 0 | 0.117-0.168 ms |
| + kept locks (what landed) | 5 `exit` + 3 WM_CLOSE | 0 | 0 | 0 | 0.138-0.205 ms |

The middle row shows that a fast hook narrows the race without removing it: the worker can reach the lock as the
process ends, before the hook even runs. Hence the lock itself is kept.

**The fix.**
1. `plugin/KeptLocks` (LE1 host): a pre-hook on ntdll's `RtlDeleteCriticalSection` skips the deletion of any
   critical section inside the exe's image. Static data is never freed or reused, so the section stays valid until the
   process ends, and the worker waits on a live lock or takes it (the library's state is static too). Locks of other
   modules and of the heap pass through. The exit line reports what was kept (`kept locks: N deletion(s) of the game's
   static locks skipped, the first X ms ago: exe+0x...`). In every quit of the final run it kept 30 (exe+0x1BBBD68, 0x16B6690, 0x1BA7580, ...). The first came 5.1 s before the exit line, about 0.1 s after the console's `exit`: all of them in the teardown, none during play.
2. `Entry.cpp` `ExitProcessHook` waits for nothing. It sets the flags, starts Setup if asked, tells Halcyon, logs one
   line with its own duration, then calls TerminateProcess itself (Halcyon's [EXITKILL] hook, under ours, did the same;
   now it no longer depends on the hooks' order). The voice and model stops moved to `host::StopWorkersForUnload`, run
   only from LEBinkProxy's `SpiOnDetach`, where the game's threads are already gone.
3. `CrashBreadcrumbs`: after `crash::NoteProcessExit` (first thing in the hook), an unhandled fault is no crash of
   play. It is written to `%LOCALAPPDATA%\MEVR\crash-exit.txt` (`.prev.txt` before it), never to
   `crash-breadcrumbs.txt`, so there is no crash offer (`install/ReportMarks` reads only that file). The log gets `a
   fault while the game quits (...)`, and the process ends with the game's exit code before Windows' error reporting
   records an APPCRASH. No `crash-firstchance.txt` while exiting. Problem reports include `logs/crash-exit.txt` as a
   note, never as the report's crash.

**Not covered: the game thread's null read at `exe+0x12DF33` during the teardown.** It comes 1.4-6.5 s after the
console's `exit`, and the game ends there: ExitProcess (and `NoteProcessExit`) is never reached. It hit 6 of the 48
sessions since 11-12, all in conversation scenes. The script frame is `SFXGame.SFXGameModeManager.DisableMode` on the player controller's mode
manager, called natively through our pass-through ProcessEvent hook. It still writes `crash-breadcrumbs.txt` and the
offer. Its own investigation: does a mod-held game mode (the conversation world's, Thespian's) outlive the controller?

Sessions: `%LOCALAPPDATA%\MEVR\sessions\2026-11-13_16-17-41-quit-before`, `...16-25-50-quit-after`, `...16-43-27-quit-final` (each quit's log `quit-<n>-<how>.log`). Not exercised by them: `crash-exit.txt` itself, since no fault came.
