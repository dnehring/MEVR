---
type: Gotcha
title: EA's Steam launcher starts MassEffectLauncher suspended, so for up to 14 s it has no readable path
description: Steam's -applaunch goes through EASteamLauncher, which creates MassEffectLauncher.exe suspended and resumes it 0.2-14 s later; a suspended process reports an empty Path, a one-shot identity check refuses it, and the launcher's exit after starting the game looks like a game that started twice.
tags: [launch, ea-app, steam, test-loop]
status: stable
timestamp: 2026-11-17T01:30:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-17T01:30:00Z }
bitten: [2026-11-16]
---

# Symptom

| where | words |
|---|---|
| `tools/le2-halcyon-run/launch.ps1` (ME2 12:42, ME3 17:47, local time) | `Live process identity unavailable or changed; not adopted (pid=... path='' startedUtc=...)` |
| the lanes' reading of it | "MassEffect2.exe (and MassEffect3.exe) starts twice: the first exits within seconds, a second starts 15-17 s later and runs" |
| afterwards | the session records no process, so `launch.ps1 -Stop` and `load.ps1` refuse the running game as unowned |

Intermittent: other launches the same day adopted normally.

# Cause

Steam's `-applaunch 1328670 -game N -autoterminate` starts EA's `EASteamLauncher.exe`
(`steam2ea://launchgame/1328670`), which asks the EA app to authorize the launch and then creates
`MassEffectLauncher.exe` **suspended** (its log: `Game process created suspended, processId=[...]`), resumes it
0.2-13.5 s later (`Game process resumed successfully`), and the launcher starts the game and exits (`Game exited`). The
"first process" in the reports was this launcher, not a second game: ME2 at 12:42 was launcher 158844, created suspended
at 12:42:42.6, resumed at 12:42:56.1, gone at 12:43:00, with the game 49596 from 12:42:59; ME3 at 17:47 was launcher
136656, created 17:47:05.9, resumed 17:47:06.97, gone 17:47:19.3, with the game 94880 from 17:47:18.

A suspended process has not run its loader yet, so `Get-Process`'s `Path` (its main module) is empty while
`StartTime` reads; checked with a `ping.exe` created suspended: `path=''` until resumed, then the full path. The runner's
launcher check (`Get-RunLaunchIdentity`) judged each poll by two reads of the same moment and threw on two unreadable
paths; it hit only when its 2 s poll landed inside the suspension, which is why most launches passed and the 13.5 s one
at 12:42 failed.

# Check

The EA Steam launcher's log of the launch (UTC timestamps):

```powershell
$f = "$env:LOCALAPPDATA\Electronic Arts\EA Desktop\Logs\EASteamLauncherVerbose.log"
$r = New-Object IO.StreamReader([IO.File]::Open($f, 'Open', 'Read', 'ReadWrite')); $t = $r.ReadToEnd(); $r.Dispose()
$t -split "`r?`n" | Select-String 'created suspended|resumed successfully|Game exited' | Select-Object -Last 6
```

The runner's `launch-candidates.txt` in the session folder names every candidate, its parent and what was decided.

# Fix in place

`launch.ps1` tracks every `MassEffectLauncher` and game process across polls (`Read-RunCandidate`, `Step-RunAdoption`
in `tools/le2-halcyon-run/session-common.ps1`, 2026-11-16): an identity not readable yet is waited for, at most 60 s;
a candidate that exits is dropped; the launcher is recorded once it reads; the game is adopted only as the single live
process whose path and start time have read the same for 5 s. Refusals are kept: wrong path, a start before the request,
a PID whose identity changes or that comes back after exiting, a process another open session records, two live game
processes. Offline tests in `test-session.ps1` (ctest `mevr-protected-session`). Anything else that adopts a process
right after a Steam launch of the Legendary Edition must allow for the same suspension.

# Citations

- [tools/le2-halcyon-run/README.md](../../../tools/le2-halcyon-run/README.md), "A launch shows two processes".
- [EA's launcher refuses to start the game from inside a job object](ea-launcher-in-a-job-object.md): the same
  EASteamLauncher step, when its CreateProcess is denied.
