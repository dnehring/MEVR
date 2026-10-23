# 35. Performance attempt log (LE1, Halcyon Stereo 2)

A running log of every performance lever tried, with the measurement and the verdict, so no attempt is repeated
and the reasoning is traceable. Research: [32](32-ue3-vr-performance.md) (levers, engine reading), [22](22-performance-levers.md)
(the probe). Tools: `tools/perf-probe.ps1` (`python tools/perf-probe.py`), `tools/perf-ab.ps1`, `tools/sample-render-thread.ps1`,
`tools/console.ps1`, the `render levers:` log line (per scene render: time, the Umbra resolve's parts).

Budget: 11.11 ms at 90 Hz. The render thread sets the pace everywhere measured; the GPU only at the heaviest spot and only when
the CPU lets it (it finishes 0.5 ms after the render thread there).

## Setup for measuring without the headset (2026-10-23)

SteamVR's null driver stands in for the PS VR2 so the game runs its full VR path (two Stereo 2 renders a frame, 90 Hz pacing,
the same probes) with nobody in the headset. Files, backed up before the change:

- `SteamVR\drivers\null\resources\settings\default.vrsettings`: `enable: true`, `renderWidth/Height` 2824x2880 (the PS VR2's eye
  size), `displayFrequency` 90, window 1920x1080.
- `Steam\config\steamvr.vrsettings`: `steamvr.forcedDriver = "null"`, `activateMultipleDrivers = false`,
  `driver_playstation_vr2.enable = false` (else the PS VR2's overlay complains "headset off"), and **`power.pauseCompositorOnStandby
  = false`, `turnOffScreensTimeout = 36000`**: SteamVR puts the null headset in standby 5 s after each wake and pauses the
  compositor, so without this the session never leaves READY and Halcyon never runs a frame (three launches lost to it).
- Then SteamVR restarted (`vrstartup.exe`). `steam://rungameid/1328670` from a shell never produced the launcher; ME3Tweaks Mod
  Manager's Start Game → the launcher's PLAY → Enter at the press-any-key screen (which resumes the last save) does, driven by
  computer use. The mouse does nothing in VR mode (Halcyon decouples it); the keyboard works.
- A motionless player at the save's spot (Eden Prime, mid-combat) gets killed within a minute: `tools/console.ps1 pause` freezes
  the world and keeps rendering. Numbers below marked "paused" are from that state. Without vsync on the null display the frame
  runs free (7.3 ms at the save's spot), so compare render-thread busy times, not frame intervals.
- What the fake headset can't give: a real head pose (static view, no turning spikes) and the compositor's own cost.

## Spots

| spot | render thread busy | draws / frame | notes |
|---|---|---|---|
| first spawn (PS VR2, 13:52) | 10.7 ms median, p90 11.7 | 1800 median, p90 4100 | 2824x2880 target; GPU span 5.3 ms |
| the worst spot (PS VR2, 13:58-14:00, standing still) | 13.3-13.8 ms, p90 15-16 | ~4650-4900 | GPU span 12.4-13.8 but only 0.5 ms behind the CPU: CPU-bound |
| save's spot, paused (null driver, 15:11) | 6.9 ms | 1671 | per eye render 1.85 / 3.4 ms (first / second) |

## Attempts, in order

| # | when | lever / change | measured | verdict |
|---|---|---|---|---|
| 1 | 13:48 | stereo target 2824x2880 (was 3072x3188), `MaxAnisotropy=16` | backbuffer 2824x2880 confirmed; GPU span 5.3 ms at the first spawn | kept |
| 2 | 13:48 | `tools/eye-diff.ps1` | never ran: the recorder waited for Halcyon's hooks in the context vtable; the EA overlay (igo64.dll) had given the context a private copy of the table, so the slot never showed them | fixed (attach without the wait; a stuck request expires after 3 s) |
| 3 | 13:56 | `umbraQueryWait` 1 / 0 (the Umbra results read never spins) | 10.9 / 10.7 vs 10.8 / 10.7 ms | no effect (the spin never triggers) |
| 4 | 13:57 | `UMBRA TOGGLEOCCLUSION` (Umbra's culling off) | second eye's instance step still 2.2 ms; more draws | no gain |
| 5 | 13:59 | `pCoresOnly` 0 / 1 | 13.3-13.4 vs 14.4-13.8 ms (worst spot) | worse; off |
| 6 | 14:00 | `sharePass` 0 / 1 | 13.4-13.6 vs 13.1-13.2 ms, **but the log said 0 renders shared**: the first eye's renderer was never kept | invalid |
| 7 | 14:12 | 0.75 target (2118x2160) + FSR upscale | render 13.6-13.8 ms either way at the worst spot; FSR 0.17 ms GPU per eye | the GPU isn't the limit there; no CPU effect |
| 8 | 14:21 | `sharePass` 1, second attempt (eligibility fixed: show flag 8 is set on every gameplay render, it isn't wireframe) | kept 260 renderers, **all 259 pairings refused** | diagnostics added |
| 9 | 14:35 | pairing diagnostic (null driver) | the only difference between the eyes' views: one 4-byte word at view +0x8C | copied per pass with the matrices |
| 10 | 14:41 | `sharePass` 1, pairing accepted | **the game froze**; render thread idle in the engine's command wait, game thread blocked in Umbra's command ring (0xD4A5B0 waits for space) | see below |
| 11 | 14:56 | same, with both threads' stacks + Ghidra (the other session's read of 0xD4A520/0x8C34C0) | the ring is drained by Umbra's own resolve step (0xD478E0 ← Umbra::Camera::resolveVisibility 0xD41240 ← the commander); a second eye that skips its resolve stops draining; the game thread fills the ring during its next tick and blocks | **lever 1 dead by design; the draw replay (lever 4) must keep the second eye's resolve** |
| 12 | 15:07-15:13 | `show <category>` sweep at the save's spot, paused (StaticMeshes, SkeletalMeshes, Decals, Particles, Foliage, SpeedTrees, Terrain, BSP, Fog, DynamicShadows, Translucency, PostProcess, Lighting) | render 6.80-6.92 ms for all of them; static meshes off removed 426 of 1671 draws and 0.0 ms | at this spot the cost isn't per draw; it's fixed per frame |
| 13 | 15:14 | stack sample, paused | scene renders 66%: Umbra resolve 21%, Umbra base pass 20%, lights 8%; Halcyon's Present path 12.6%; the engine's RHIEndDrawingViewport 12.7% | the resolve is the largest single block, twice a frame |
| 14 | 15:14 | the second eye's extra 1.6 ms (instance step 1.64 vs 0.16 ms), by stack: inside UmbraCommander::InstanceVisible (0x8BE0B0), 37% in FD3D11DynamicRHI::GetQueryData (0x9A04B0) | UE3's per-primitive occlusion waits (bWait) for "last frame's" query; the second eye's is this frame's first eye, still on the GPU (1.3 ms behind) | the spin, located |
| 15 | 15:17 | `toggleocclusion` (UE3's own queries off) | second eye's instance step 1.64 → 0.35 ms, but draws 1671 → 1813 (p90 3833) and the first eye +0.55 ms: frame 6.87 → 7.13 ms | worse: the occlusion is worth more than the spin |
| 16 | 15:21 | `ue3QueryWait` 0 (the second render's GetQueryData never waits; unready = visible) | 6.95 → 7.26 ms; draws median 1552 but p90 3714 (nearly every result unready, so the second eye draws unoccluded) | worse; off |
| 17 | 15:28 | `perEyeViewState` 1 (Halcyon's per-eye FSceneViewStates, cine-only upstream, in gameplay) | second eye's instance step 1.9 → 0.2 ms, first eye 1.8 → 2.1 ms, draws p90 1565 → 3683 (one frame in ten draws 2.4x): render 6.95 → 7.23 ms | worse as is; the extra draws need understanding (each state's history sees every other render?) |

## Where the second eye's time goes (save's spot, paused, 15:29)

| per scene render | first eye | second eye |
|---|---|---|
| total | 1.85 ms | 3.4 ms |
| Umbra resolve | 0.40 | 1.9-2.3 |
| of which instances (code 3: UE3's per-primitive step inside it) | 0.16 | 1.6-1.9 |
| occluder flush + queries (code 5) | 0.2 | 0.2 |
| query results (code 6) | 0.02 | 0.03 |

## Standing conclusions

- The render thread is the limit at every spot; the GPU only matters at p90 in heavy views.
- At a light spot the frame's cost is fixed per frame, not per draw (attempt 12); at the worst spot draws do cost (4700 of them),
  but the biggest single block is still Umbra's resolve, twice a frame, and it can't be skipped for either eye (attempt 11).
- The second eye pays 1.3-1.9 ms a frame for a GPU-side wait on the first eye's occlusion queries (attempt 14). Removing the wait
  by any of the three ways tried (no queries, no wait, a state per eye) costs more in draws than it saves (15-17). The next
  step is reading, not another switch: how UE3 reads "last frame's" query (0x762550, the occlusion history in the view state at
  +0xD10) and why a state per eye still leaves one frame in ten unoccluded.
- Dead: sharePass 1 (deadlock by design), pCoresOnly, umbraQueryWait, UMBRA TOGGLEOCCLUSION, ue3QueryWait. Untested in VR:
  sharePass 2 (the draw replay) needs the second eye's resolve kept first.
