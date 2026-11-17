---
type: Gotcha
title: fpsVR starts SteamVR again after the fake headset is turned off, and the runner refuses to restore
description: SteamVR auto-launches the owner's fpsVR overlay; when a run's fake headset stops SteamVR, fpsVR (still running) asks Steam to start it again, and the ME3 preview runner's restore refuses the unrecorded VR processes.
tags: [steamvr, fake-headset, test-loop, launch]
status: stable
timestamp: 2026-11-16T23:20:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-16T23:20:00Z }
bitten: [2026-11-16]
---

# Symptom

| where | words |
|---|---|
| `tools/le2-halcyon-run/restore.ps1 -Game LE3 -RendererPreview`, first try | `VR still runs; exact environment restore refused` |
| the same, a minute later | `Unrecorded/replaced VR process; refusing to adopt or stop it` (session-common.ps1) |
| the machine | no game running, the fake headset's `undo.json` gone ("fake headset OFF ... SteamVR stopped"), yet `vrmonitor`, `vrserver` and two `vrwebhelper` run again, started seconds after the Off |

The lock stays held and the prepared game files stay unrestored until it is resolved.

# Cause

The owner's fpsVR (Steam app 908520) is registered to start with SteamVR. A run's fake headset (`tools/fake-headset.ps1
-On`, called by `prep.ps1`) starts SteamVR, which starts fpsVR. `fake-headset.ps1 -Off` stops SteamVR's own processes
but not fpsVR, which then asks Steam to start SteamVR again: the new `vrmonitor`'s parent is `fpsVR.exe`, its command
line `-startupappid 908520 -startupreason applaunch_steam`. The ME3 preview runner records every VR process its session
started and refuses to stop one it did not record, so its restore stops there. Nothing of the owner's is involved: no game
and no headset session.

# Check

```powershell
Get-Process | Where-Object { $_.ProcessName -match '^vr|fpsVR$' } | Select-Object ProcessName, Id, StartTime
Get-CimInstance Win32_Process -Filter "Name='vrmonitor.exe'" | Select-Object ParentProcessId, CommandLine
```

A `vrmonitor` whose parent is `fpsVR.exe` and whose command line names `908520` is this.

# Fix in place

By hand (lane EY, 2026-11-16, the coordinator's order): close fpsVR first (its `CloseMainWindow`, then `Stop-Process`
if it does not exit in 10 s), else it starts SteamVR again; then stop SteamVR the way `fake-headset.ps1`'s `Stop-SteamVR`
does (`vrmonitor`, `vrserver`, `vrcompositor`, `vrdashboard`, `vrwebhelper`, `vrstartup`); confirm no `vr*` process is
left; rerun `restore.ps1` with the same holder and flags and confirm `verification: 0 differences` and the lock released.
Leave fpsVR's and SteamVR's settings alone, and leave `fpsvrCPUTempCounterService` (a service) running. Not automated
yet: `fake-headset.ps1 -Off` could close fpsVR before it stops SteamVR.

# Citations

- [research/27-colour.md](../../../research/27-colour.md) section 9.5 (the run it bit in).
- [SteamVR's room setup kills the game at the title on the fake headset](steamvr-room-setup-kills-the-game.md), the other
  SteamVR side effect of the fake headset.
