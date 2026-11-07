---
type: Gotcha
title: A file just written is busy for a moment, so the next rename of it fails now and then
description: Right after a file is written another process (most likely Defender's on-access scan) opens it for a moment, and a rename, replace, copy or delete in that moment is refused with access denied (5) or a sharing violation (32); mevr-tests failed 3 runs in about 80 on the installer's record file and passed on a rerun.
tags: [install, tests, windows, antivirus, flaky]
status: stable
timestamp: 2026-11-06T20:30:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-06T20:30:00Z }
bitten: [2026-11-06, 2026-11-06, 2026-11-06]
---

# Symptom

`ctest` says `mevr-tests (Failed)` and passes when run again; the failing output is one line among hundreds:

    FAIL InstallTests.cpp line 1146: Install(layout, payload, options).ok

always an Install, Repair or Uninstall of the fake game, usually the Install right after an Uninstall. Since the fix the
failed Outcome's errors are printed too, and they named the installer's own record:

    its error: couldn't write ...\ASI\MEVR-install.json (error 5)

| date | where | rate |
|---|---|---|
| 2026-11-06 morning | two sessions' merges: "an intermittent mevr-tests failure", passed on rerun, the log overwritten by the rerun | - |
| 2026-11-06 evening | looped on purpose: 3 failures in about 80 runs alone on the machine, all the same check; the two with their errors printed both `MEVR-install.json`, the code shown once: 5 | ~1 in 27 |
| 2026-11-06 evening | main's own test binary, 1 failure in 2 runs | - |

# Cause

Windows lets another process hold a file open without sharing delete; a rename onto it, over it, or of it fails with
`ERROR_ACCESS_DENIED` (5) or `ERROR_SHARING_VIOLATION` (32) until that handle closes, a few milliseconds later. Defender's
real-time protection opens files as they are written and closed (on-access scanning was on on the dev machine; that it is
the holder is inferred, not traced). The installer writes its record (`WriteFileAtomic`: a `.mevr-writing` file renamed over
`MEVR-install.json`) after every step, dozens of times an install, so that file loses the race most. A player's antivirus
does the same to Setup, on DLLs most of all.

A second, separate cause was waiting: every install test used fixed names in `%TEMP%` (`mevr-install-test`,
`mevr-install-firstrun`...), shared by every worktree's build, so two sessions running ctest at once deleted each other's
fake game.

# Check

Loop the tests until one fails and keep its output (a rerun overwrites `LastTest.log`):
`for i in $(seq 1 60); do ./Release/mevr-tests.exe > run.txt 2>&1 || cp run.txt fail-$i.txt; done` from `build`. The
failing check's `its error:` line gives the file and the Windows error.

# Fix in place

- `src/install/Manifest.cpp`: every replace, copy, rename and delete of the installer goes through `Retrying` (or
  `RemoveFile`): on access denied, a sharing violation or a lock violation it waits 10, 20 ... 640 ms (1.27 s in all)
  and tries again; a read-only target fails at once (no wait helps it). `WriteFileAtomic` is Win32 calls now, and a failure
  keeps its error code (`GetLastError`), which the "couldn't write" messages print.
- `tests/InstallTests.cpp`: `Ok(outcome)` prints a failed Outcome's errors.
- `tests/TestTemp.hpp`: every test's scratch path is under `mevr::tests::TempRoot()`, `%TEMP%\mevr-tests-<pid>`, removed at
  the end of the run. New tests that write files use it, never a fixed name in `%TEMP%`.
- Measured after the fix: see research/33 "Files busy for a moment".

# Citations

[1] [src/install/Manifest.cpp](../../../src/install/Manifest.cpp): `Retrying`, `RemoveFile`, `WriteFileAtomic`.
[2] [tests/TestTemp.hpp](../../../tests/TestTemp.hpp): the run's own temp folder.
[3] Microsoft, MoveFileExW: the destination "cannot be ... in use"; GetLastError codes 5 and 32.
