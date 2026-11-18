---
type: Gotcha
title: EA's launcher refuses to start the game from inside a job object
description: EASteamLauncher creates MassEffectLauncher with breakaway; when its parent chain sits in a Windows job object without breakaway (our shell, or the EA app itself), CreateProcess fails with "Access is denied" and the scripted launch times out.
tags: [launch, ea-app, steam, test-loop]
status: stable
timestamp: 2026-11-18T09:35:00Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-24, 2026-10-28, 2026-10-31, 2026-11-03, 2026-11-18]
---

# Symptom

`tools/launch-game.ps1` ends with `no MassEffect1.exe within 120 s`. EA's log
(`%LOCALAPPDATA%\Electronic Arts\EA Desktop\Logs\EADesktopVerbose.log`) says ErrorCreatingProcess, LaunchFailed,
"Access is denied" (0x5). EASteamLauncher.log: `GameRunStatus received ... with error code [ErrorCreatingProcess]`,
`[PROCESS] Create Process: Access is denied. | exe=[...\MassEffectLauncher.exe]` (2026-11-03).
The ME2/ME3 runner (`tools/le2-halcyon-run/launch.ps1`): `No MassEffect2 within 240 seconds` (2026-11-18), Steam's
console_log showing the `steam2ea://launchgame` request "Completed" and EASteamAuthHelper gone 4-6 s later.

# Cause

The chain is Steam's Play, then EASteamLauncher (a child of steam.exe), the EA app, MassEffectLauncher,
MassEffect1.exe. EASteamLauncher creates the launcher with `breakaway=[true]`; if the parent chain is in a job without
breakaway, CreateProcess is denied. Two ways in:

1. The game started from the harness's shell, whose job object the EA app inherits (2026-10-24).
2. The EA app itself (EADesktop.exe, EALocalHostSvc) inside a job, which persists until the EA app is restarted
   (2026-10-28).
3. **steam.exe itself inside a job** (2026-11-18): a session had started Steam from its shell (11-16), and its
   children (the `steam2ea://` handler, EASteamLauncher) inherit the job. Launches worked for a day and a half, then from
   00:54 every `breakaway=[true]` CreateProcess of MassEffectLauncher.exe was denied, with no change in Steam's log;
   the job's breakaway allowance apparently went away with the session that owned it. Quitting the EA app made it worse:
   Steam's helper restarted it inside the same job.

# Check

Whether steam.exe and EADesktop.exe are in a job object (`IsProcessInJob(OpenProcess(0x1000, ...), NULL)` from
PowerShell's Add-Type). The launch error in `EASteamLauncherVerbose.log` (`Create Process: Access is denied`, the
`breakaway=[true]` on the createProcess line) and `EADesktopVerbose.log` (`ErrorCreatingProcess`, `LaunchFailed`).

# What does NOT get the EA app out of its job (2026-10-31, 01:14-01:30, three tries, each "Access is denied" again)

Killing EADesktop.exe and starting EALauncher.exe (a) with `Start-Process explorer.exe <exe>` (the new explorer.exe
sits in our job and launches the child itself), (b) through WMI `Win32_Process.Create` (the new EADesktop still read
IsProcessInJob = true), (c) through the real shell's ShellWindows COM object (`Document.Application.ShellExecute` on the
desktop window). The EA app may put itself in a job of its own, so IsProcessInJob alone is not the test: the EA log's
launch error is. With nobody at the keyboard (and the desktop-control request denied) there was no launch that night;
the only known routes stayed a Play in Steam by a person or computer use, then `-Attach`.

2026-11-03: EASteamLauncher.exe started directly from a scheduled task with Steam's own `steam2ea://` link failed the
same way: Task Scheduler runs its tasks in a job object too.

# Fix in place

**Steam in a job (2026-11-18): restart Steam through Explorer.** `steam.exe -shutdown`, wait for it to exit, open a
folder window (`explorer.exe "C:\Program Files (x86)\Steam"`: the main Explorer hosts it), then
`(New-Object -ComObject Shell.Application).Windows()` -> that window's `Document.Application.ShellExecute(steam.exe)`:
Explorer itself starts Steam, so it sits in no job (checked with IsProcessInJob), logs on by itself, and the next
launch worked (ME2, 01:28). Never start Steam from a harness shell or a scheduled task. Do not quit the EA app while
Steam is in a job.

Never start the game from the harness's shell: `tools/launch-game.ps1` runs Link2EA from a scheduled task. If the EA
app itself is in a job, **`tools/launch-game.ps1 -Steam`** (2026-11-03): it asks the running Steam for the game
(`steam.exe -applaunch 1328670 -game 1 -autoterminate`, what its Play does). The new steam.exe hands the command to the
running one, which is in no job, so EASteamLauncher starts as its child. The game was up in 6 s, the parameters arriving.
Otherwise quit the EA app from the tray and start it from the Start menu, or press Play in Steam and attach with
`tools/launch-game.ps1 -Attach` (it waits 120 s for the process).

# Citations

[1] [research/35, attempt 40 (2026-10-24)](../../../research/35-performance-attempt-log.md).
[2] [tools/launch-game.ps1](../../../tools/launch-game.ps1): the header, both causes, `-Steam`.
[3] [research/50 (2026-11-03)](../../../research/50-citadel-crowds.md): "Reaching a place by the game's own menus".
