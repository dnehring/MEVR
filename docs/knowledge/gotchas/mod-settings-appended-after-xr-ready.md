---
type: Gotcha
title: The mod appends its default settings blocks on its first tick, seconds after the XR marker, so a load started at once saw its settings file change
description: After a prep the mod's settings file holds only the runner's [Debug] line; the mod appends its defaults once its engine access is ready, about 2 s after ME2's XR session at the title, and load.ps1, which had already hashed that file, refused the first load although the save loaded fine.
tags: [test-loop, runner, settings, le2]
status: stable
timestamp: 2026-11-17T09:20:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-17T09:20:00Z }
bitten: [2026-11-15, 2026-11-16, 2026-11-17]
---

# Symptom

| where | words |
|---|---|
| `tools/le2-halcyon-run/load.ps1`, the first load after a prep (or a redeploying prep) | `Save or runtime artifact changed during load` |
| its receipt (`<session>\load-*\receipt.json`) | `"passed": false`, `"saveUnchanged": true`, `"artifactsUnchanged": false`, with `transitionObserved`, `gameplayObserved` and `descriptorMatched` all true: the save loaded |
| the lanes' workaround | "wait about 45 s at the title", or load again (the second load passed) |
| the mod's log at the same seconds | `hands: added the [Controls] defaults to ...\ASI\LE2MEVR.ini` ... `[Reload]`, `power wheel: added the [PowerWheel] defaults`, `vr cover: added the [Cover] defaults` |

21 of the 39 ME2 loads in the runner's sessions of 2026-11-15 to 11-17 were refused so, each with the save unchanged
and loaded; ME3's renderer preview once, on 2026-11-15, before its launch waited (below).

# Cause

`prep.ps1` writes `ASI\LE2MEVR.ini` afresh on every prep: `[Debug] openxrRuntime` only. The mod fills it in on its
first tick (`game/HandCalibration.cpp` `Refresh`: every missing block appended, then key migrations by
`WritePrivateProfileStringW`; the host's Colour writes `[Colour]`; `game/PowerWheel` and `game/VrCover` append their own
blocks), and that tick waits for the mod's engine access (`reflect: ready`), which on ME2 comes about 2 s after the
renderer's `VR session ready` at the title (20 s later in another run). `launch.ps1` returned at the XR marker; `load.ps1`
hashes the exe, the host, the mod and this ini before its first console request and refuses the load if any of them
differs at its end. On 2026-11-17 00:56 the hashes were taken at :21.05, the blocks went in from :22.10 to :22.35, and
two blocks the busy file refused (`couldn't add the [Holsters] defaults ... (read-only?)`) went in a second later, at
:23.19. A later launch with the file complete has nothing to append, hence the passing second load.

ME3's renderer preview met it first (2026-11-15 17:10) and its `launch.ps1` route has waited for the settings since
(`Wait-PreviewInitializedConfig`, commit 0d150ddf: this launch's `hands: calibration pitch` and `colour: profile`
markers, every known block present and 1.25 s of quiet text); none of its loads has been refused so since. ME2's VR
route had no such wait.

# Check

The receipt's `artifactsUnchanged` (false with the save unchanged), then the mod's log
(`ME2\Binaries\Win64\Logs\LE2MEVR.log`; the session's `logs\` keeps the last one after a restore): `hands: added the [`
lines later than the receipt's `startedUtc` (the log is local time). Since the fix, `launch.ps1` prints
`mod settings settled: LE2MEVR.ini unchanged for ...` or warns `mod settings did not settle within ...`; both lines are
also in the session's `mod-settings.txt`, and the settled file in `effective-mod.ini`.

# Fix in place

`launch.ps1`, in ME2's VR mode, waits after the XR marker (`Wait-RunModSettings` in
`tools/le2-halcyon-run/session-common.ps1`, 2026-11-17): this launch's `hands: calibration pitch` and `colour: profile`
lines in the mod's log (HandCalibration logs its pitch after its appends and migrations), then 3 s without a write to the
file, at most 90 s. It reads only the file's attributes (length, last write), so its polls can't make an append fail the
way a reader without write sharing can. A timeout warns and returns: the protection stays where it was. `load.ps1` is
unchanged in what it accepts: any change to a recorded artifact during a load, an appended settings block included,
still refuses it (`Test-LoadArtifactsUnchanged` in `load-common.ps1`). Offline tests: `test-session.ps1` (ctest
`mevr-protected-session`) and `test-load.ps1` (`mevr-protected-load`).

If it comes back: a module that appends its block only later (on foot, in a level) would change the file during a
load again; give it its default in `HandCalibration`'s first `Refresh` or append it at the title, never loosen
`load.ps1`'s check.

# Citations

- [tools/le2-halcyon-run/README.md](../../../tools/le2-halcyon-run/README.md), "The mod's first-run settings".
- [src/game/HandCalibration.cpp](../../../src/game/HandCalibration.cpp): `AppendBlock`, `Refresh`.
- [A file just written is busy for a moment](fresh-file-busy-for-a-moment.md): the refused appends retried a second later.
