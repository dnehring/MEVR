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
| 18 | 15:36 | query-read counters per render (the GetQueryData hook counts reads, waits, reads without a result) | shared state: 151 reads an eye a frame, 91 waiting, 0 without a result. Per-eye states: the second eye makes **7,378 reads a frame, 7,202 without a result**, in the Umbra commander's results read (code 6: 1.0 ms, longest 6 ms) | with a state per eye the second eye's Umbra queries are never ready: Umbra's results read polls them for its 10 ms budget |
| 19 | 15:37 | the four combinations of `perEyeViewState` and `umbraQueryWait`, paused | (0,1) 6.92 ms / 1561 draws; (1,1) 7.22 / 1573 (p90 3743); (1,0) 7.58 / **2927** draws (the unready Umbra queries count as visible every frame); (0,0) 6.93 / 1561 | per-eye states: the spin goes (second eye's instance step 2.1 → 0.2 ms) but Umbra's own occlusion breaks for that eye |
| 20 | 15:44 | why: SceneRenderB picks the Umbra commander per render through 0x8C1AE0 and always passes 0, so both eyes' Umbra cameras share commander 0's query pool; with one camera (the shared state) the pool double-buffers across the two resolves (each eye reads the other's queries, 3-7 ms old: ready), with two cameras they re-issue each other's pending queries. The engine creates a second commander it never uses; hooked 0x8C1AE0 to give the second render commander 1 with `perEyeViewState` | **the game hung**: the render thread inside commander 1's resolve reading the command ring (0xD4A120) for a resolve command the game thread only queues for commander 0; the game thread waiting on the render thread | commander 1 isn't usable from the render side alone; hook left in but it should go |

| 21 | 15:50 | reading: where the view's Umbra camera comes from | `ULocalPlayer::CalcSceneView` copies it from the ULocalPlayer (+0x474), not from the view state (+0x46C): both eyes keep ONE Umbra camera even with a state per eye. So the per-eye breakage (18) isn't two cameras: the commander's occluder queries are allocated from and read back through the current view's state (its occlusion query pool at +0x88), so with a state per eye the second eye reads, by index, queries that live in the other state's pool. Separating UE3's histories per eye while sharing Umbra's queries would need the commander's pool decoupled from the view state | not pursued: the spin is worth ~0.1 ms at the heavy spots (below) |

| 22 | 17:00 | **a heavy spot** (the player's later save, Eden Prime spaceport, paused): 9.2-9.5 ms render, 3720 draws, GPU 8.3-8.5 (close to the CPU here) | `UMBRA OCCLUSIONQUERIES` off: 15.2 ms (far worse). `scale set DetailMode 1/0`, `MassiveLODDistanceScale 0.5`, `DecalCullDistanceScale 0.5`: no change in draws (3721-3724), the time differences noise | Umbra's queries stay; those three scalability keys do nothing here |
| 23 | 17:05 | `show` category sweep at the heavy spot (render ms / draws / GPU ms against 9.2 / 3723 / 8.3) | Foliage off **6.9 / −90 / 5.3**; StaticMeshes 7.3 / −1790 / 6.7; SkeletalMeshes 7.4 / −650 / 6.7; Terrain 7.3 / +420 / 6.7; DynamicShadows 8.3 / −366 / 7.5; decals, particles, trees, lighting, translucency, post ±0.3 | foliage costs 2.4 ms CPU + 3.1 GPU for 90 draws (instanced grass rebuilt per view, twice a frame); dynamic shadows were still drawing although GamerSettings says off |
| 24 | 17:09 | the engine's knobs for those, live via `scale set`, interleaved with baselines | `FoliageDrawRadiusMultiplier` 0.5: −1.0 ms CPU, −0.8 GPU; 0.25: −1.4..−1.8 CPU, −1.0 GPU (draws −70). `DynamicShadows False`: −0.5..−0.7 ms, −366 draws. `SkeletalMeshLODBias 1`: nothing | **two live levers** |
| 25 | 17:11 | both together, and the ini: BIOEngine.ini had `DynamicShadows=True` and `FoliageDrawRadiusMultiplier=1.0`, GamerSettings `DynamicShadows=False`: the engine's [SystemSettings] value wins | **9.35 → 6.88 ms render (−27%), GPU 8.4 → 4.9**; the two eye snapshots differ only in the squad's ground shadows and the far grass clumps | `tools/vr-game-settings.ps1` now writes both keys into BIOEngine.ini too (research/32 levers table) |

Where the spin matters: it is the time between the first eye's queries and the second eye's read, so it's largest where the CPU is fast (1.6 ms of 6.9 at the save's spot) and smallest where the CPU is slow (about 0.1 ms at the worst spot: 0.51 vs 0.47 ms instance steps). Fixing it raises the light-spot headroom, not the heavy-spot frame.

## Where the second eye's time goes (save's spot, paused, 15:29)

| per scene render | first eye | second eye |
|---|---|---|
| total | 1.85 ms | 3.4 ms |
| Umbra resolve | 0.40 | 1.9-2.3 |
| of which instances (code 3: UE3's per-primitive step inside it) | 0.16 | 1.6-1.9 |
| occluder flush + queries (code 5) | 0.2 | 0.2 |
| query results (code 6) | 0.02 | 0.03 |

## The real-headset session (19:22, the PS VR2 back; the player in the headset, then away with it still)

| # | when | lever / change | measured | verdict |
|---|---|---|---|---|
| 26 | 19:24 | the launch itself | BIOEngine.ini had `DynamicShadows=True`, `FoliageDrawRadiusMultiplier=1.0` again (identical to the 17:12 backup; the tool's values gone), GamerSettings still `False` / `0.25`; render 8.3 + 8.1 ms per render, 10.8 ms a frame (p90 11.8), draws 2084, GPU 4.9 | the engine ini is rewritten: the game writes its [SystemSettings] back (after each `scale set` at least: the file's stamp followed the command); whatever reset them at launch, a file edit can't be trusted |
| 27 | 19:25 | `scale set DynamicShadows False`, `scale set FoliageDrawRadiusMultiplier 0.25`, live | per render 4.0 + 4.9 ms; the frame still 10.8 (p90 11.6-12.4) with the player looking at more (2234-3188 draws); BIOEngine.ini rewritten with the values at 19:25:15 | the per-render costs fell as on the fake headset; the frame sits at the budget at this spot |
| 28 | 19:27 | render thread stack sample, the player moving | SceneRender 72% (InitViews 57%: Umbra resolve 31%, UE3 occlusion 9%; base pass 26%), RHIEndDrawingViewport 14%, GetQueryData self 9% | the known shape; a moving head doesn't change it |
| 29 | 19:30 | eye snapshots while the player looked around: Kaidan and a structure "missing" in the left eye | **misread**: in the right eye they were at 39-53 degrees right, beyond the left eye's 43.4 degree edge (the PS VR2's eye frusta are asymmetric: left -61.5/+43.4, right -43.4/+61.5). Both eyes carry the same frame number and head pose in the JSON, so a snapshot pair is one frame; compare positions in tangent space, not pixels | no eye disagreement shown |
| 30 | 19:32 | `TOGGLEOCCLUSION` with the player watching | the "flicker" stayed; eye 1's query reads 10479 a frame (10325 without a result), 1.4 ms of results: Umbra's reads break like the per-eye state (attempt 17) | reverted at 19:44 |
| 31 | 19:36 | **the "shimmer every few seconds on the floor"**: 8 snapshots with the headset still on the desk, unpaused: 4 of 8 frames smeared in a wavy diagonal blur (both eyes), the rest sharp, alternating every ~3-6 s; `scale set MotionBlur False` → every frame sharp (paused, 0.0% change frame to frame); `True` again, paused → the **left eye only** blurred in 4 of 5 frames, the right sharp | **motion blur**: BIOEngine.ini `MotionBlur=True` wins over GamerSettings' `False` (same as the shadows); with one view state the "previous frame" matrix the blur's velocity comes from is the other eye's, 6.3 cm away, so near things (the floor, the hands) smear in one eye and not the other. Off for good: the mod applies it (below) |
| 32 | 19:41-19:44 | replay of the player's 20 s head recording with 8 snapshots each, UE3 occlusion off / Umbra culling off | no eye disagreement in either set (the arm "discrepancy" at 480 px was parallax of a 40 cm object: full-size crops agree) | neither culling system shows a per-eye miss on this save's spot |
| 33 | 19:50 | `game/SystemSettings`: the mod runs `scale set DynamicShadows False`, `MotionBlur False`, `FoliageDrawRadiusMultiplier 0.5` once a local player exists (and on a change of `[SystemSettings]` in `ASI\LE1MEVR.ini`, which overrides any key; `apply = 0` off) | hot-reloaded into the running game | the settings no longer depend on which file the launch leaves behind |

What the player described and what each was: "shimmering on the floor every few seconds" = motion blur from the other eye's
matrix (31); "foliage pops in close" = `FoliageDrawRadiusMultiplier` 0.25 (now 0.5); "hands flicker in and out" = most
likely the same blur (a 40 cm object smears to nothing in one eye); "CPU timing poor" = the render thread at 10.8 ms median
with 1 frame in 10 late (27). HDR: with `DynamicRange_HDR10_Float` and no Windows HDR on the desktop the game's backbuffer
is `R8G8B8A8_UNORM` (fmt 28): no float source, the tonemap idle (research/27 7b's [verify] answered: Windows HDR is needed).

## Standing conclusions

- The render thread is the limit at every spot; the GPU only matters at p90 in heavy views.
- At a light spot the frame's cost is fixed per frame, not per draw (attempt 12); at the worst spot draws do cost (4700 of them),
  but the biggest single block is still Umbra's resolve, twice a frame, and it can't be skipped for either eye (attempt 11).
- The second eye pays 1.3-1.9 ms a frame for a GPU-side wait on the first eye's occlusion queries (attempt 14). Removing the wait
  by any of the three ways tried (no queries, no wait, a state per eye) costs more in draws than it saves (15-17). The next
  step is reading, not another switch: how UE3 reads "last frame's" query (0x762550, the occlusion history in the view state at
  +0xD10) and why a state per eye still leaves one frame in ten unoccluded.
- Dead: sharePass 1 (deadlock by design), pCoresOnly, umbraQueryWait, UMBRA TOGGLEOCCLUSION, ue3QueryWait, perEyeViewState
  as is (Umbra's queries break), commander 1 for the second eye (deadlock). Untested in VR: sharePass 2 (the draw replay)
  needs the second eye's resolve kept first.
- The second eye's spin (attempt 14) is a light-spot cost: 1.6 ms of 6.9 at the save's spot, about 0.1 ms at the worst spot.
  Fixing it buys headroom where there already is some. Not worth more restarts now.

## Next, for the heavy spots (the ones that matter)

At the worst spot (13.3-13.8 ms, ~4700 draws, two renders of 6-7 ms each) the costs by the stack sample and the levers line:
Umbra resolve ~1.9 ms per eye (of which the occluder flush + ~380 query draws ~1.0-1.1 ms per eye), the base pass's draws,
lights, Halcyon's present path (~0.7-1.0 ms "before the wait" plus copies and the submit). Candidates, each measurable on the
fake headset from a save at a heavy spot:
1. Fewer Umbra occlusion queries and occluder draws per eye (Umbra's console settings / commander limits): ~380 query draws
   an eye at the worst spot are DrawIndexedPrimitiveUP calls each.
2. Fewer draws at heavy spots: the engine's LOD and cull distance settings (SystemSettings), measured per setting.
3. Halcyon's present path: what the 0.7-1.0 ms before xrWaitFrame does (the stereo capture's copies, probe-and-promote).
Needed from the player: a save (quicksave) standing at the worst spot, so the heavy-spot numbers can be taken alone.
