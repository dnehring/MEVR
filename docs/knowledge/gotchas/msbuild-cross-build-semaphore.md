---
type: Gotcha
title: A build killed mid-compile leaks MSBuild's machine-wide compile slots, and every later build waits forever
description: CMakeLists.txt sets EnforceProcessCountAcrossBuilds=true, so every build's cl.exe processes take slots from one named semaphore shared by the whole machine; a build killed while holding slots never gives them back, and every build after it hangs with no cl.exe running until every MSBuild process has exited; pass -p:EnforceProcessCountAcrossBuilds=false.
tags: [build, msbuild, tooling, sessions]
status: stable
timestamp: 2026-11-19T19:54:17Z
generated: { by: claude-code/opus-5.5, at: 2026-11-19T19:54:17Z }
bitten: [2026-11-18, 2026-11-19]
---

# Symptom

`cmake --build` never returns. The MSBuild processes stay alive and almost idle, and no `cl.exe` runs under them. The
tool call times out, but the shell and the build keep running, holding their build folders' files open.

| date | where | what was seen |
|---|---|---|
| 2026-11-18 evening | lane RLD's report | "Builds on this machine stalled with 0 cl.exe running", the coordinator's build-trilogy and two other sessions' builds among them; the lane's own builds ran again with the flag below |
| 2026-11-19 midday | the owner: "We may have some locks occurring" | six builds started between 22:12 and 23:35 the evening before by two sessions (build-mono, build, build-mono2, build-trilogy, build-mono3) still hanging 14 hours later, 0.4-38 s of CPU each in all that time; stopping one let its shell go on to a `ctest` it had queued 14 hours earlier |

# Cause

`CMakeLists.txt` (lane BLD, 2026-11-17) writes `UseMultiToolTask=true` and `EnforceProcessCountAcrossBuilds=true` into every
project's Globals. The multi-tool task gives each source its own `cl.exe`, and with the second property each one first takes
a slot from one named semaphore for the whole machine (one slot per logical processor), so builds running at once share the
cores instead of each starting 24 compiles.

A Windows semaphore has no owner. A build killed while it holds slots (a harness timeout, `taskkill`, a crash) never gives
them back. The semaphore lives as long as any process has it open, so while one build still waits on it the lost slots stay
lost. Every new build that enforces the count then waits too, and nothing ends the wait. The leak is inferred from how
MSBuild and Windows semaphores work, not traced with a handle dump. It fits everything seen: no compiler running,
idle MSBuild nodes, builds with the flag unaffected, and builds working again once every stuck one was stopped.

A build started through `cmd /c start "" /b /low /wait` (CLAUDE.md's Idle-priority line) outlives the tool call that
started it: the harness gives up on the call, but its shell stays blocked in the command, and a later command in the same
script runs whenever the build finally ends.

# Check

    Get-CimInstance Win32_Process -Filter "Name='cmake.exe' OR Name='MSBuild.exe' OR Name='ctest.exe'" |
        Select-Object ProcessId, ParentProcessId, CreationDate, CommandLine
    Get-Process cl -ErrorAction SilentlyContinue

A `cmake --build` older than any build takes (a cold build-trilogy is about 6 minutes), with MSBuild nodes and no
`cl.exe`, is stuck. A young build with `cl.exe` running is working; leave it. Walk `ParentProcessId` up to the
`claude.exe` that started it to see whose it is.

# Fix in place

- Every `cmake --build` passes `-p:EnforceProcessCountAcrossBuilds=false` after the `--` (a property on the command line
  overrides the project's Globals): CLAUDE.md's build line. Such a build doesn't touch the semaphore, so it neither waits
  on a leaked one nor leaks one itself.
- When it bites anyway: stop every stuck tree (`taskkill /T /F /PID <the cmake, or its cmd wrapper>`), then check again,
  since a stuck build's shell goes on to its next command (a test run, another build). Once no process has the semaphore
  open, Windows frees it, and the next build gets a fresh one with every slot.
- The `.lock` files in `%LOCALAPPDATA%\MEVR\build-cache` are CMake `file(LOCK)` locks, released when their process ends;
  a leftover file blocks nothing.
- Not changed: `CMakeLists.txt` still sets the property to true, and the scripts in `tools/qa-cafeteria`, `tools/qa-hubs`
  and `tools/package-release.ps1` build without the flag. Turning it off there is the lasting fix, at the cost of
  concurrent builds no longer sharing the cores.

# Citations

[1] [CMakeLists.txt](../../../CMakeLists.txt): the `CMAKE_VS_GLOBALS` block (commit 0e609918, lane BLD).
[2] [tools/cmake/ThirdParty.cmake](../../../tools/cmake/ThirdParty.cmake): the cache's `file(LOCK ...)`.
[3] Microsoft, "Semaphore Objects": a semaphore has no owner; any thread may release it, and a count taken by a thread that
exits is not given back.
