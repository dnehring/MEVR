---
type: Gotcha
title: EA's launcher refuses to start the game from inside a job object
description: EASteamLauncher creates MassEffectLauncher with breakaway; when its parent chain sits in a Windows job object without breakaway (our shell, or the EA app itself), CreateProcess fails with "Access is denied" and the scripted launch times out.
tags: [launch, ea-app, steam, test-loop]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-24, 2026-10-28]
---

# Symptom

`tools/launch-game.ps1` ends with `no MassEffect1.exe within 120 s`. EA's log
(`%LOCALAPPDATA%\Electronic Arts\EA Desktop\Logs\EADesktopVerbose.log`) says ErrorCreatingProcess, LaunchFailed,
"Access is denied" (0x5).

# Cause

The chain is Steam's Play, then EASteamLauncher (a child of steam.exe), the EA app, MassEffectLauncher,
MassEffect1.exe. EASteamLauncher creates the launcher with `breakaway=[true]`; if the parent chain is in a job without
breakaway, CreateProcess is denied. Two ways in:

1. The game started from the harness's shell, whose job object the EA app inherits (2026-10-24).
2. The EA app itself (EADesktop.exe, EALocalHostSvc) inside a job, which persists until the EA app is restarted
   (2026-10-28). steam.exe is never in one, so Play in Steam works.

# Check

Whether EADesktop.exe is in a job object (IsProcessInJob). The EA log's launch error.

# Fix in place

Never start the game from the harness's shell: `tools/launch-game.ps1` runs Link2EA from a scheduled task. If the EA
app itself is in a job, quit it from the tray and start it from the Start menu, or press Play in Steam and attach with
`tools/launch-game.ps1 -Attach` (it waits 120 s for the process).

# Citations

[1] [research/35, attempt 40 (2026-10-24)](../../../research/35-performance-attempt-log.md).
[2] [tools/launch-game.ps1](../../../tools/launch-game.ps1): the header, both causes.
