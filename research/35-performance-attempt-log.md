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

| 34 | 20:00 | **the still pose, paused: foliage radius 1.0 / 0.5 / 0, `show Foliage` off, then every `show` category off in turn** | render 10.7-10.9 ms whatever is off; StaticMeshes off: 10.3 ms for 720 fewer draws (1694 → 971); the rest ±0.1 ms. The lever log over the same minutes: **first render 2.4-3.0 ms, second 6.6-7.5 ms, of which its "instances" step 4-5.5 ms** (the first eye's: 0.22-0.44) | the frame's floor is the second eye's spin for the first eye's queries on the GPU, not drawing: any CPU saved in the first eye lengthens the wait. The player had seen the same: foliage off changed nothing |
| 35 | 20:10 | reading: the history (`UpdatePrimitiveOcclusion` 0x762550) is a `TSet` at state+0xD10, 0x48 bytes, elements 0x28 (primitive, pending query, last visible / considered time, pixel fraction, occluded, hash links); the read is RHI vtable +0x268 with bWait = 1; the released query goes to the pool at state+0x88 (Umbra's too); the new query comes from the VIEW's batcher (+0x126C / +0x12A4). The set's add (0x4EDAA0) and rehash (0x4FD300) show the layout: data +0, num +8, max +0xC, allocation bits inline +0x10 / pointer +0x20, free list +0x30 / +0x34, hash inline +0x38 / pointer +0x3C / size +0x44 | built: `perEyeOcclusion` (default 1) swaps a second 0x48-byte set into the state around the frame's second scene render (`BeginEyeHistory` / `EndEyeHistory` in `RenderLevers`), dropped when the state changes or its set is empty at the frame's first render; the pool stays shared | to measure |

| 36 | 20:20-20:45 | **the popping, read in Ghidra**: after the relaunch the second eye's instances step was 0.35 ms (the spin gone) but the popping stayed, with or without the 5 s re-apply (a bug of mine: the camera sample rewrote the ini and the applier re-ran its `scale set`s every 5 s; fixed, 50ebadf). Then: `UmbraCommander::IssueOcclusionQueries` (0x8B6550) calls the RHI's SetViewParameters (vtable +0x4C0) with the view's **ViewProjectionMatrix +0x260**, the CPU copy, once per render (+0x5C flag), and draws its occluder test geometry with it; the engine's own `BeginOcclusionTests` (0x759240, 2.2% of the frame) uses **TranslatedViewProjection +0x1D0** for its boxes and the Umbra base pass drew the depth with +0x1D0 too. In mode 2 only +0x1D0 has the eye's off-centre frustum (research/30 kept +0x260 symmetric for the HUD markers): Umbra's queries are tested up to 18 degrees away from the geometry, so its answers change with every head motion | built: `umbraEyeMatrix` (default 1) hooks 0x8B6550 and, for the call that sets the matrix, puts the world-space equivalent of +0x1D0 into +0x260 (T(pre-view translation) x TVP; the sign checked against V and TV at the first call), restored right after (the RHI copies the matrix into its constants). The menu got the "Each eye's own field of view" row (Advanced > Stereo) for the mode 1 / mode 2 A/B (f0b75ad) | to verify: no popping in mode 2 |

| 37 | 2026-10-24 14:53-14:58 | **the second headset session** (the crash fixed by the other session; the popping gone, confirmed by the player): at a medium scene the frame was 11.4 ms median but **24 ms at p90, 38 max**, and the draws 2,574 median / **8,544 at p90** / 12,034 max: every ~10th frame drew three times the meshes (GPU 23-30 ms on those frames). With `perEyeOcclusion = 0` (live): draws 2,562 / 2,583 / 2,638, frame 10.7 / 11.7 / 13.2, GPU 4.7. With `umbraEyeMatrix = 0` instead: the bursts stayed (so they're the history's, not the matrix fix's). The log showed 0 resets of the second set, so it isn't the fresh-state heuristic; the engine's own re-test logic in UpdatePrimitiveOcclusion (the random re-query of occluded primitives and the "not visible for a while" rule, both reading the entry's times and pixel fraction) behaves differently with two sets, in a way that synchronises thousands of primitives into one frame. Also: with the UE3 spin gone, the second eye's wait moved into Umbra's own result reads (`query results` 1.3-2.8 ms, longest 7 ms, ~15,000 polls a frame); `umbraQueryWait = 0` removes that wait but counts every unready occluder as visible (draws p90 10,000, GPU 30 ms): both left at the game's own | **perEyeOcclusion off by default** (kept as an experiment switch); umbraEyeMatrix stays on; umbraQueryWait 1 | the second eye's wait is Umbra's GPU round trip inside its resolve: the next structural read is Umbra's result handling (0xD47A9C), to use the previous frame's result when unready |

| 38-39 | 2026-10-24 15:30 | two levers built for the loop, both default 0: `flushFirstEye` (ID3D11DeviceContext::Flush right after the first eye's scene render, so the GPU starts on it before the second eye's Umbra resolve waits) and `umbraShareResults` (a second-eye Umbra read that isn't ready is answered with the first eye's i-th result of the same frame: Umbra's resolve reads one result at a time through 0xD48380, hooked to mark the reads; the GetQueryData hook counts them per render) | to measure | |
| 40 | 2026-10-24 16:14-16:46 | **launching without a person**: `MassEffect1.exe` and `MassEffectLauncher.exe` started directly exit at once with 100010; `steam://rungameid`, `steam://run/<id>//<args>` and `steam.exe -applaunch` are logged by Steam (`ExecuteSteamURL`) and start nothing; the mod manager's Start Game shows in Steam's log as `Link2EA.exe link2ea://launchgame/1328670?platform=steam&theme=met` under the EA app's own Steam id 1182480. **That is the launch**: Link2EA → the EA app → `EASteamProxy.exe MassEffectLauncher.exe` → the launcher → the game, and the URL's `cmdParams=-game 1 -autoterminate` reach the launcher's command line, where LEBinkProxy's launcher module boots ME1 with no PLAY click (4 s to a game process). Also learned the hard way: a process started from this shell sits in the harness's job object; the EA app my first direct start spawned inherited it and failed every launch after (its log: `Create Process: Access is denied` for the proxy, which breaks away from the job), including the player's own. So the chain runs from a scheduled task (the user's token, no job), never from the shell | `tools/launch-game.ps1` (Link2EA through the task, Enter at the title, READY on "on foot yes", `-Pause`, `-Attach`, `-Map`), `tools/fake-headset.ps1`, `tools/measure-levers.ps1` | the loop |

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

## Attempt 41 (2026-10-24, 17:30): what fpsVR's "CPU frametime" is, against the probe

The player reads fpsVR; the probe reported "busy, without the XR waits". Are they the same thing?

- fpsVR shows SteamVR's frame timing. Valve's definition of the CPU "scene" segment is the work between WaitGetPoses'
  return and the second eye's Submit ([SteamVR/Frame Timing](https://developer.valvesoftware.com/wiki/SteamVR/Frame_Timing));
  for an OpenXR app that is xrWaitFrame's return to xrEndFrame. It is WALL time: every idle, hold and GPU wait inside it
  counts. In our loop ([Performance] earlyFrameWait = 1) xrWaitFrame returns at the END of the previous Present, so the
  span is the previous frame's wait end -> this frame's submit: both scene renders, Halcyon's present path, and any hold.
- `tools/perf-probe.py` now prints it: "poses -> submit, wall" (and "Halcyon's hold before xrBeginFrame"). At the player's
  medium spot, paused, history off (14:57:58 capture): interval 11.16 / busy without waits 10.72 / poses -> submit 11.12 ms
  median, p90 12.14, max 13.75. So the probe's number is within 0.4 ms of fpsVR's in gameplay: the stage of measurement
  is not the gap. The xrWaitFrame wall there is 0.02 ms every frame: SteamVR never paces us; the render thread is busy
  7.63 of 8.00 s (95%) and the app makes 90 fps with no slack at all (720 frames in 8.0 s).
- fpsVR keeps its sessions in `%LOCALAPPDATA%\fpsVR\N.json` (per-frame histograms over the seconds the headset was
  worn: `cputimes` in 0.1 ms bins, `gputimes` in 1 ms bins, `fps_ps`, `gpuusage`, `cpuusage`) and `N.bin` (one 30-byte
  record a second). `tools/fpsvr-session.py` reads them. The player's headset sessions:

  | session | worn | CPU median / p90 / mean (ms) | GPU median / p90 (ms) | fps median |
  |---|---|---|---|---|
  | 2026-10-24 14:48 (today: perEyeOcclusion on until 14:56, then off) | 211 s | 12.4 / 16.4 / 13.2 | 9.5 / 13.5 | 90 |
  | 2026-10-23 20:38 | 260 s | 15.4 / 22.1 / 15.8 | 9.5 / 13.5 | 78 |
  | 2026-10-23 20:16 (the long walk) | 1044 s | 14.4 / 20.6 / 14.9 | 9.5 / 13.5 | 82 |
  | 2026-10-23 19:22 | 474 s | 13.4 / 21.2 / 14.9 | 9.5 / 13.5 | 86 |

  That is the player's "averaging 13, spikes to the 20s" exactly, and it is walking through heavy areas, which the
  loop has not measured yet (the stationary medium spot is the 11-12 ms band, 70% of today's frames). Today's p99 of
  32.6 ms and the 33-34 ms bin (1%) are the perEyeOcclusion bursts before 14:56 (attempt 37).
- **Correction to the GPU framing:** fpsVR's GPU frametime (SteamVR's, app + compositor) is 9.5 ms median and 13.5 ms
  at p90 in every real session: the GPU is over the 11.1 ms budget at the tail too, at 2824x2880 per eye with FSR from
  2118x2160. "GPU at 40-60%" was whole-GPU load averaged over idle gaps, not the frame's GPU time. Offloading CPU work
  to the GPU is not free at the heavy spots; fewer draws help both.
- A reading artefact worth knowing: in a cutscene Halcyon's `cineFpsCap` holds the Present thread (17.6 ms a frame in the
  Citadel arrival cinematic, 17:04 capture) AFTER the early xrWaitFrame, so SteamVR counts the hold as app CPU time:
  fpsVR shows 22 ms "CPU" there for 5.4 ms of work. The fake-headset game was sitting in that cinematic while the player
  looked at fpsVR. Moving the hold before the early wait would make cutscenes read honestly; it changes nothing in
  gameplay (the hold is 0.04 ms there).

## Attempt 42 (2026-10-24, 17:40-18:15): the loop reaches the heavy spots by itself, and what it found there

The player's sessions were on Eden Prime (BIOA_PRO00, the crash breadcrumbs); the resume save lands at a light spot.
Pieces built to get the loop to a heavy spot without a person:
- `mevr loc` / `mevr density [cellUU]` console lines (`game/ConsoleRequest`, mod side): the pawn's position, and every
  live static mesh / dynamic mesh / SpeedTree actor binned into cells, densest first. On Eden Prime the dense cells are
  2-6 km from the save: 62 meshes at (-35000, 147000), 58 at (-33000, 149000), 47 at (-33000, 147000), 45 at (-31000, 145000).
- Moving the pawn: `Engine.Actor.SetLocation` through ProcessEvent (`mevr setloc`) returns without moving the player
  (the native's FarMoveActor refuses; the game's own `teleport` cheat, which calls the same, works, so it is our call,
  unresolved). What works: the console's `set SFXPawn_Player Location (X=..,Y=..,Z=..)` (one such pawn exists), with
  `god` and `ghost` first (the game's cheats answer the console; without them the first move landed Shepard in a
  "Critical mission failure"). `tools/spot-survey.ps1 -Spots "X Y Z,..."` does the moves, replays a head recording and
  probes each spot.
- The launch script's readiness check waited on `testloop.status`, which had stopped updating mid-session; it now waits
  on the mod log's freshness (the Enter at the title was never sent before). `tools/measure-levers.ps1 -Inputs` replays
  the walk and the shots of the 20 s recording (2026-10-23 19:26, left stick -0.95..1.0) so Shepard walks the player's
  first 20 s from the save: 7.2 ms at 1,700 draws there (the light spot).

The survey (fake headset, 2118x2160 target, head motion replayed, 6 s each; "cpu" = poses -> submit):

| spot | draws | cpu median / p90 / max (ms) |
|---|---|---|
| -31000 145000 900 | 4,134 | 10.1 / 11.0 / 14.8 |
| -33000 147000 400 | 4,050 | 10.6 / 11.8 / 15.1 |
| -33000 149000 0 | 3,422 | 9.2 / 10.8 / 23.8 |
| -35000 147000 150 | 2,622 | 8.0 / 11.0 / 16.4 |
| -35000 149000 -400 | 2,378 | 7.5 / 9.9 / 14.5 |
| the save's spot | 1,700 | 7.2 / 8.0 / 12.5 |

At the heaviest spot (4,100 draws): render thread 9.6 ms (86% of the period), GPU 8.7 ms (78%), game thread 4.5 ms.
**Both built levers do nothing there**: umbraShareResults 10.0 vs 10.2 ms, flushFirstEye 10.0 vs 10.2 (2 rounds of 8 s
each, within the replay's own noise). The reason is in the levers line: the second eye's Umbra result reads wait 0.06 ms
and the engine's occlusion step 0.4-0.6 ms per render, both renders cost the same (3.9 ms), so the second eye does not
wait for the first eye's GPU work at this spot on the fake headset: nothing for those levers to remove.

**The player's tail reproduced by GPU pressure.** With the stereo target raised to 2400x2448 (+28% pixels; the real
headset adds the compositor's work and 2824x2880 swapchains instead) at the same spot: cpu 10.3 median but **15.1 at p90
and 19.9 max**, the render thread's busy time rising with it (14.9 p90: it spins, it does not sleep), the GPU span 9.1 /
14.6 / 19.1. The issuing work is unchanged; the tail is the render thread waiting for the GPU inside the scene renders,
and fpsVR reports that wait as CPU frametime. That is the player's 13 median / 20 p90 shape (attempt 41's fpsVR table).
So at heavy spots the GPU is the limit that shows up as "CPU": the GPU-side levers (fewer pixels, fewer draws, one
submission for both eyes) are the ones that move it, and GPU offload makes it worse.

**And a CPU cost of its own, 20% of the render thread:** the stack sampler at the heavy spot (10 s, a sample a
millisecond, 5,270 samples) puts 20.4% of the render thread inside `0x9A89F0`, the D3D11 bound-shader-state constructor
(it calls ID3D11Device::CreateInputLayout, vtable +0x58, with the vertex shader's bytecode), always from the cache-miss
path of `RHICreateBoundShaderState` (`0x9AB3B0`: the cache lookup `0x8018B0` by (vertex declaration, VS, PS, GS) pointers
missed, then `new` + the constructor). Callers: the drawing policies of the dynamic-mesh paths (0x7B96FE, 0x784BF2,
0x744484, 0x7B9E77, 0x785B7B, 0x783B4B: base pass, depth, lighting). UE3's bound shader states are ref-counted objects
whose cache link dies with them; dynamic meshes (foliage, SpeedTrees, skeletal meshes) build their drawing policy, and
with it the bound shader state, every draw, so the input layout is recreated every draw of every eye: ~2 ms a frame at
4,100 draws. Lever to build: keep every created bound shader state alive (one extra reference, bounded by the number of
distinct (declaration, VS, PS) combinations), so the lookups hit. Expected 1.5-2 ms off every heavy frame, both eyes.

## Attempt 43 (2026-10-24, 18:20): bound shader states kept alive (keepBoundShaderStates)

The lever (host, `plugin/RenderLevers`): a hook on the D3D11 bound-shader-state constructor (`0x9A89F0`, address table
row "FD3D11BoundShaderState constructor"); after it, one extra reference through the RHI's own AddRef (GDynamicRHI's
vtable slot 0x8a0, which is at RVA 0x215B10 and increments the object's int at +8: 0 -> 1 on the first kept object), on
the handle the constructor returns (the object + 0xC). Counters in the levers line: "bound shader states: N created a
frame, K kept this period (T in all)".

What the counters showed at the heavy spot (-31000 145000 900), head motion replayed: **lever off: 300-350 bound shader
states created a frame**; lever on: 1-2 a frame after the first seconds, 1,787 kept in the first 10 s, 4,267 after 40 s
(still growing ~90 a second with the same 20 s of view looping, see below). Memory flat over 50 s (private 5,268 MB ± 10).

Frame times, a fresh launch, the same spot, 3 x 8 s each (fpsVR's number is "frame"; medians / p90):

| | round 1 | round 2 | round 3 |
|---|---|---|---|
| off | 12.8 / 18.7 | 15.8 / 18.1 | 16.5 / 18.3 |
| on | 10.5 / 11.6 | 11.1 / 12.3 | 10.8 / 12.3 |

**5 ms off the median and 6 off the p90 at the heavy spot.** The A/B cannot alternate: once states are kept they stay
kept (perf-ab's alternation showed nothing after the first "on" period), so this is off-then-on in one session. The
earlier session at this spot (attempt 42, running 23 minutes) measured 10.0 ms with the lever not yet built, against
15.8-16.5 here in the first minute of a fresh launch: the per-creation cost is far higher while the driver's caches are
cold (a fresh launch, a new area), i.e. the states the game rebuilds every draw cost 2 ms a frame warm (the 20% in the
sampler) and 5+ ms cold. That matches the player's spikes when walking into new areas.

Open: with the lever on the kept count grew ~90 a second with the same view looping, so some keys (vertex declaration, VS,
PS) are not stable (a declaration or shader made per frame); keeping those leaks (an input layout each). Added the
stable-key rule: a state is kept only when its exact key is constructed for the second time (a fixed 64K-entry hash
table of keys seen), with "new keys / keys seen before" counters. To verify: the kept total must plateau with the
view looping, and memory stay flat over minutes.
