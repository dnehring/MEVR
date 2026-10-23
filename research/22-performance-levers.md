# 22: LE1 VR performance: where the frame goes, and what could speed it up (2026-10-22)

**Question (user, 2026-10-22).** LE1 is heavy in VR: even the RTX 4090 barely keeps up, though
the game runs fine flat. What can make it faster?

This builds on [16](16-performance.md) (2026-10-08, Quest over Steam Link), which found that
neither the GPU nor the game thread looked like the limit and blamed the focused-session stall,
Halcyon's depth capture and the cutscene cap. Those three are dealt with (`mirrorPresentEvery =
2`, depth pop off, `depthPopKeyEnabled = 0`); the game is still slow. Sources: today's and
yesterday's logs (Halcyon's, ours, SteamVR's), the deployed ini files, Halcyon's and our source.
Web sources are at the end and in [sources.md](sources.md). **[inference]** marks our reasoning,
**[verify]** what one measurement would settle.

## Summary

1. **The 45 fps is a lock, not the game's speed.** Today (PS VR2, 90 Hz) VR gameplay sat at
   45.0 fps for four minutes, and SteamVR's own stats say 57% of its frames were reprojections.
   Yesterday (Quest 3 over Steam Link, 72 Hz) it sat at about 37, again half the refresh. When
   the game misses frames, SteamVR halves its frame rate and reprojects (Valve's design: a steady
   45 judders less than an uneven 65). So the game's real speed is somewhere between 45 and 90
   (and below 72 yesterday); nobody has measured it, because the lock hides it. SteamVR's
   per-application "Throttling Behavior" setting can lift the lock for a measurement, and the
   PS VR2 at 120 Hz would make the same lock 60 fps.
2. **Two full renders sit near the 11.1 ms budget.** On 2026-10-08 the same Stereo 2 setup held
   90.0 fps for whole minutes in lighter scenes and 64-76 in heavier ones; today's level is the
   Normandy (`BIOA_PRO00`). Stereo 2 renders the whole scene twice: the second render costs only
   about 0.5 ms on the game thread, so the doubling lands on the render thread (1,400-3,900 draws
   per frame on the eye-sized targets alone) and the GPU.
3. **Most likely limit: the render thread (CPU), [verify].** UE3 on D3D11 is draw-call bound,
   the draw count doubles, Halcyon's hooks run on every draw, and on 2026-10-08 one thread that
   only becomes the busiest in VR ran at 95.6% of a core, while the GPU was at 53%.
4. **Nothing measures the frame's parts yet.** Halcyon's `[GPU] avgMs` is the interval between
   presents, SteamVR's "application GPU time" only sees the copy after `xrWaitFrame` (1.0 ms
   today), and our heartbeat only counts frames. One short instrumented session would say which
   of game thread, render thread, GPU or the wait in `xrWaitFrame` sets the pace, and so which of
   the levers below can pay off.
5. **Small, certain losses found in the code:** a blocking GPU readback every 120 frames
   (Halcyon's `[BANDPROBE]` diagnostic); a storm of caught access violations from Halcyon's
   guarded memory probes; our HUD panel seam's per-draw COM queries in the second pass.
6. **The frame loop waits late.** Halcyon renders both eyes first and calls `xrWaitFrame`,
   `xrBeginFrame` and `xrEndFrame` back to back in Present. One other app that did this reported
   being "gummed" to 45 fps; REFramework (MIT) offers the same placement as its "Very Late" mode
   and defaults to waiting early. Whether it costs us frames is one of the things the frame
   breakdown has to show (finding 9).
7. **FSR 1 is practical, DLSS isn't (yet).** A spatial upscaler fits where Halcyon already
   stretches each eye image into SteamVR's swapchain; DLSS and FSR 2/3 need jitter and motion
   vectors this renderer doesn't make. Both only help if the GPU is the limit.

## The numbers

| Session | Headset, refresh | VR gameplay fps | SteamVR: frames reprojected | Flat panel (one render) |
|---|---|---|---|---|
| 2026-10-22 19:52 | PS VR2, 90 Hz | 45.1 (139 samples, flat 45.0 for minutes) | 15,373 of 26,830 (57%) | 89.5 |
| 2026-10-21 21:06 | Quest 3, Steam Link, 72 Hz | 37.0-37.6 | 12,322 of 28,717 (43%) | 72.0 |
| 2026-10-08 20:47 | Quest, SteamVR, 90 Hz | 45.0-46.5 (cutscenes 45.0) | 24,685 of 47,875 (52%) | 47-50 |
| 2026-10-08 15:47-15:58 | Quest 3, Steam Link, 90 Hz | 64-76, or 88-90 in light scenes | not logged | 89.5-90 |

- The 2026-10-08 20:47 session ran on SteamVR's OpenXR runtime (`[XRRUNTIME] name='SteamVR/OpenXR'`),
  not Oculus as [16](16-performance.md)'s last section assumed: its 45 fps is the same SteamVR lock,
  not ASW.
- "Flat panel" is Halcyon's `[VRSTATE] FLAT panel`: menus and movies shown on a panel in the
  headset (Stereo 2's second render never runs there). Today's were movies, so they say nothing
  about what one 3D render costs.
- Fps from `tools/perf-summary.py` (Halcyon's `[PRESENTHZ]`). Reprojection counts from SteamVR's
  `Steam\logs\vrcompositor.txt` ("Cumulative stats for pid ..."), which also gives, today,
  "ApplicationTime CPU: 20.291ms / GPU: 1.003ms" and compositor 0.98 / 0.44 ms.
- SteamVR's numbers for this app are not frame costs. Halcyon renders first and calls
  `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame` back to back inside Present
  (`xr_session.cpp:3898-3922`, `:5955`), so SteamVR's "GPU" window holds only the copy into the
  swapchains, and its "CPU" time is roughly the whole frame interval.
- Today's game-thread cost of the second render (`[SFR] ... pass1Us=`, every 600 frames):
  0.22-0.85 ms.
- Draws per frame on the three eye-sized (3072x3188) render targets (`[REFLCEN]`, both passes):
  1,412-3,879. Shadow depths, post-processing chains and the UI are on other targets, so the total
  is higher.
- Settings that cost twice per frame in Stereo 2 (`GamerSettings.ini`, and `BIOEngine.ini` for
  anisotropy and detail mode): `AmbientOcclusion=True`,
  `DepthOfField=True` (Halcyon re-enables it at each launch, `disableDof = 0`),
  `LightEnvironmentShadows=True` (each character's shadow map, rendered again for each pass),
  `MaxAnisotropy=4`, `DetailMode=2`. Already off: dynamic shadows, motion blur, film grain, AA,
  VSync, quality bloom.

## How a Stereo 2 frame is made

**[inference]** from Halcyon's source and the logs:

1. **Game thread** (`UGameEngine::Tick`, hooked by us in `plugin/Entry.cpp`): the game's tick,
   our mod's handlers (ProcessEvent, arm IK through `UpdateSkelPose`, weapon, holsters,
   grenades, squad, sights), then `FViewportClient::Draw` twice
   (`render_hook.cpp:2597`: pass 0 = left eye, then the replay with the right eye's offset). Each
   Draw builds a view family and queues a whole scene render for the render thread.
   `OneFrameThreadLag=True`: the game thread runs at most one frame ahead.
2. **Render thread:** both scene renders in full (visibility and occlusion, shadow depths, depth,
   base pass, lights, translucency, AO, DOF, bloom, tone mapping), each eye captured into a bank
   texture as it completes (`[PASSCAP]`), then the UI. Every draw goes through Halcyon's
   D3D11 hooks (`d3d_capture.cpp:5419-5580`).
3. **Present (still the render thread):** pacing holds if configured (none in gameplay),
   `xrWaitFrame` (blocks until SteamVR wants the next frame), `xrBeginFrame`, our controllers
   (`MevrBridge::OnFrameBegun`: `xrSyncActions`, 14 action reads, 5 `xrLocateSpace`), the menu and
   overlays, the copies into 4164x4244 swapchains, `xrEndFrame`, the desktop mirror on every
   second frame.
4. **GPU:** two 3072x3188 scenes (9.8 MP each) plus post-processing, the copies, SteamVR's
   compositor.

So the frame holds 90 Hz only if each of the game thread, the render thread and the GPU fits in
11.1 ms. Two full renders did in lighter scenes on 2026-10-08, and don't on the Normandy today.

## Findings

### 1. SteamVR's half-rate lock

- With motion smoothing off (`steamvr.vrsettings` `motionSmoothing: false`), SteamVR answers
  missed frames by throttling the app to half the refresh and reprojecting every other frame.
  That is the flat 45.0 today and the ~37 at 72 Hz yesterday. Valve's reasoning: an intermediate
  rate such as 65 Hz "is a worse artifact" than a steady 45 (GDC 2016).
- `steamvr.vrsettings` has no section for the game, so it gets the default, automatic throttling.
- SteamVR's per-application overrides (since 1.16.1; SteamVR > Settings > Video > Per-Application
  Video Settings, pick the game once it has run) control this:
  - **Throttling Behavior:** Auto, or Fixed with a slider of whole divisors of the refresh rate
    (stored as `framesToThrottle`; rate = refresh / (framesToThrottle + 1)). Fixed at the full
    rate lets the game free-run (60-85, say) with asynchronous reprojection filling the gaps:
    the true speed shows, at the price of uneven motion. Fixed at half is a steady 45.
  - **Motion Smoothing** override: 45 plus synthesized frames in between.
  - **Legacy Reprojection Mode** and **Additional prediction** sit next to them.
  - These apply to OpenXR apps too (MSFS players use "Fixed 45" through SteamVR).
- **PS VR2 at 120 Hz** (Sony's PC app offers 90 and 120): the same automatic lock would then hold
  the game at 60 instead of 45, if it manages 60. Halcyon was tuned on a 120 Hz headset, which is
  why its cutscene cap gives 60 there and 45 at 90 **[verify: that the game holds 60]**.
- On 2026-10-08 the "FOCUSED vs VISIBLE" gap of [16](16-performance.md) fits the same
  explanation **[inference]**: with the SteamVR dashboard up (VISIBLE), the same frozen scene ran
  at 81-90; with the game focused it dropped to 47-51 or 72. The rendering work is the same in
  both states; the throttling is not.
- So the first payoff of any speed-up is getting out of the lock: a game that can do 80 fps
  shows 45 today.

### 2. The render thread carries the doubling

- The second `FViewportClient::Draw` costs the game thread under 1 ms; the scene work it queues
  runs on the render thread and the GPU.
- UE3's render thread issues every draw on one D3D11 immediate context. At a few µs per draw with
  state changes **[inference]**, 3,000-5,000 draws per frame is 10 ms or more on that thread
  alone.
- 2026-10-08 (perf-monitor): in FOCUSED VR at ~72 fps the busiest thread (tid 33172) used 95.6%
  of a core, about 13.2 ms per frame; it only becomes the busiest thread once VR starts. Its
  identity was never checked. It matches the render thread if Halcyon's startup thread check
  (`[SFR] threadcheck ... present=`) caught the startup movie's thread rather than the render
  thread **[verify]**.
- GPU use was 53% (FOCUSED) and 62% (VISIBLE at 90 fps) that day.

### 3. Rendering pixels the headset never shows

- Halcyon renders both eyes symmetrically: `[RENDER_FOV_MATCH] applied halfH=1.073377
  halfV=0.925725`. The PS VR2's eye frustum is asymmetric: left eye
  `fov=(-1.073, 0.758, 0.926, -0.926)` (`[XRSUBMIT]`), the mirror for the right eye.
- Horizontally each eye needs 1.831 of the 2.147 tangent units rendered: **15% of the rendered
  pixels fall outside the eye's view** (34% on the Quest 3, [16](16-performance.md)). Culling
  uses the wider frustum too, so the draws for objects in that strip are also wasted.
- Halcyon sets the projection per pass already (`ApplyTargetFovOverrideRawSEH`,
  `render_hook.cpp`, writes `proj[0]` / `proj[5]` and rebuilds the view-projections), so an
  off-centre term per eye is a small edit in code. The risk is elsewhere: Halcyon tried an off-axis
  shift per pass for convergence and backed it out (`[SFRCONV 2026-07-20 FINAL]`,
  `render_hook.cpp:2311`), because ME1 places its world-anchored HUD in each pass from the CPU
  matrices and the copies split apart. An asymmetric frustum has to keep the CPU and GPU matrices
  consistent per eye, and the submit has to map each eye's image with its own field of view.

### 4. Resolution is set by the game, not SteamVR

- Halcyon renders at the game's backbuffer size (3072x3188, the "Performance" preset of its
  installer; it recommends Balanced 4096x4250) and creates the eye swapchains at SteamVR's
  recommended size, upscaling into them.
- That size, 4164x4244 today, is SteamVR's automatic 150% (`gpuSpeedRenderTargetScale 1.5`) of
  the PS VR2's 100%, 3400x3468 (a forum figure). `supersampleScale 1.28` is dormant: there is no
  `supersampleManualOverride`.
- So the game renders **83% of the pixels of SteamVR's 100%** for this headset. It isn't asking
  the 4090 for an unusual resolution.
- Raising or lowering SteamVR's resolution slider therefore barely changes the game's cost; it
  changes the swapchain, the upscale copy and the compositor (0.44 ms GPU today).
- Halcyon's `dynResFraction` (`render_hook.cpp:655`) writes the engine's own
  `ULocalPlayer::DynamicResolutionFraction`, which scales the scene's internal render size and
  leaves the backbuffer (and the UI) at full size. Below 1.0 it is a ready GPU lever. It is read
  from `MELEVR.ini` at launch and isn't in Halcyon's menu, so each value costs a restart; it is
  applied every frame, so a menu slider would make it live **[verify: that Stereo 2 honours it,
  and how soft 0.8 looks]**.

### 5. Halcyon's per-draw hooks and our HUD seam

- Every `DrawIndexed`, `Draw`, `DrawIndexedInstanced` and `DrawInstanced` runs Halcyon's checks
  before the real call: the pass-0 dirty gate, the render-target census (`[REFLCEN]`), the cine
  capture trigger, the UI duplication and mirror tests, `UberPostTrace`, the scissor and post-quad
  fixes. Most return after reading a cached value; they cost a little each, on thousands of draws.
- **Our seam `MevrHudPanelDraw`** (`d3d_capture.cpp:4449`) is the expensive one: during the
  second pass and the UI it calls `RSGetViewports` on every draw, and for each full-viewport draw
  (most scene draws) `OMGetRenderTargets`, the RTV's and texture's `GetDesc`, `GetResource`,
  `QueryInterface` and three `Release`s. [16](16-performance.md) estimated 0.5-2 ms per frame.
  Scene draws go to the FP16 scene target (`fmt=10`) or other non-backbuffer targets, so a
  prefilter on Halcyon's cached `g_currentRtvFormat` / `g_currentRtvWidth` (as `IsUiDrawNow`
  already does) would skip nearly all of them before any COM call.
- Correction to [16](16-performance.md): stripping `D3D11_CREATE_DEVICE_SINGLETHREADED`
  (`[SVRFIX]`) makes the *device* thread-safe, not the immediate context. Microsoft: context
  calls take a lock only if `ID3D11Multithread::SetMultithreadProtected(TRUE)` is called, and
  that protection is off by default. So these Get calls don't take a lock each; they are just
  calls into the runtime.

### 6. A blocking GPU readback every 120 frames

- Halcyon's `[BANDPROBE]` (`xr_session.cpp:1986`, a "dark-panel hunt" diagnostic from
  2026-07-12) copies two pixel columns of the backbuffer to a staging texture and `Map`s it with no
  `DO_NOT_WAIT` flag, every 120 presents. A blocking `Map` waits until the GPU has finished all
  queued work: the render thread stalls for up to a frame, once every 2.7 s at 45 fps.
- It logs every time (112 lines today). It is a diagnostic, not a feature: it can go, or move
  to a non-blocking read two frames later.

### 7. Caught access violations

- At session start Halcyon's first-chance logger recorded 200 access violations in 0.56 s
  (19:52:58.077-58.638) and then stopped logging (its cap is 200). 173 came from
  `head_aim.cpp:105` (`ReadObjectSlotSEH`), 27 from `pchud.cpp:845` (`ReadPtrAtSEH`):
  guarded probes that read through stale or invalid pointers on purpose.
- Each one is a kernel trap, a vectored-handler call and an SEH unwind: microseconds each. At
  hundreds a second that is under 1% of a frame, but the steady rate after the cap is unknown.
  A counter in the vectored handler would settle it **[verify]**.

### 8. Stereo 2 is the brute-force way to render two eyes

- Valve's GDC 2015 talk ranks the ways to render stereo: running the CPU code twice is "BAD",
  resubmitting the command buffer "GOOD", instancing "BETTER. Half the API calls". Calling the
  engine's Draw twice is the first. UE4 later added Instanced Stereo for exactly this cost.
- UE3's own split-screen guide counts render-thread cost as processed primitives plus mesh draw
  calls: the two numbers Stereo 2 doubles.
- Whether UE3 shares shadow depths and visibility between the views of one view family (Halcyon's
  older SBS mode) is not published; Ghidra would answer it.

### 9. The wait comes after the rendering

- OpenXR expects: `xrWaitFrame` (the runtime says when to start and for which display time),
  then render with poses for that time, then `xrEndFrame`. Halcyon renders first and runs wait,
  begin and end back to back inside Present (`xr_session.cpp:3898-3922`, `:5955`).
- What that does **[inference]**: each frame is rendered with the pose from the previous wait and
  displayed at least one period later than predicted (reprojection corrects the rotation, at
  some latency); the runtime sees almost no work between wait and end, so its timing feedback
  learns nothing; and render, copy and wait run in series on one thread.
- One app on the Khronos forum that called wait/begin/end back to back reported its frame rate
  "gummed" down to 45 from its 60 target. A SteamVR user reports that SteamVR blocks in
  `xrBeginFrame` rather than `xrWaitFrame`, against the spec's advice.
- The spec allows a fix: `xrWaitFrame` "must be callable from any thread, including a different
  thread than xrBeginFrame/xrEndFrame", and a wait for the next frame may return once the
  previous one has begun. REFramework (MIT, so we may copy it) has Early / Late / Very Late sync
  modes and defaults to Early; Very Late is Halcyon's placement.
- This is Halcyon's core frame loop, so it is only worth touching if the measurement shows time
  lost in the wait while the threads and the GPU have room.

### 10. What we know is not the problem

- Halcyon's depth capture: off (no `[RELIEF]` lines).
- Planar reflections boosted to every frame (`[REFLRATE]`): none in today's level (no `boosted`
  lines).
- Our scope copy (`SightLayers.cpp:432`): only while looking through a scope.
- The test loop's copies and readbacks: only while a snapshot or recording is armed.
- The cutscene cap (`cineFpsCap = 1`): cutscenes only.

## Levers

Grouped by what they need. Expected gains are **[inference]** until the measurement in the next
section says where the time goes.

### No code: settings to try in one session

| Lever | How | Helps if | Cost |
|---|---|---|---|
| See the true speed | SteamVR per-app Throttling Behavior: Fixed, full rate (finding 1) | Always: for measuring | Uneven motion while it's on |
| A steady 60 instead of 45 | PS VR2 at 120 Hz in Sony's PC app; throttling on Auto | The game holds 60 | Drops to 40 where it can't |
| Smoother 45 | SteamVR per-app Motion Smoothing on | The game holds 45 | Warping artifacts at edges |
| Scene render scale | `dynResFraction = 0.8` in `MELEVR.ini` (0.64x the pixels) | GPU-bound | Softer image; UI stays sharp |
| Lower backbuffer | `tools/vr-game-settings.ps1 -Quality Low` (2048x2124) | GPU-bound | Softer image, UI too; restart |
| AO off | `AmbientOcclusion=False` | GPU-bound | Flatter lighting |
| DOF off | `disableDof = 1` in `MELEVR.ini` | GPU-bound, conversations | None in VR (DOF blurs what you look at) |
| Character shadows off | `LightEnvironmentShadows=False` | Render thread or GPU | No character shadows |
| Fewer draws | `DetailMode=1`, `SkeletalMeshLODBias=1`, `ParticleLODBias=1` | Render thread | Less detail |
| SteamVR resolution to 100% | SteamVR video settings | Never much (see finding 4) | None: saves VRAM and the upscale |

The A/B that tells GPU from CPU without new code: `dynResFraction` 1.0 vs 0.6 in the same spot
(one restart each), with `tools/perf-monitor.ps1` running (GPU use, and the busiest game thread as
% of a core). If fps moves, the GPU matters; if not, it's the CPU side or the wait. The SteamVR
lock has to be off for this (or the frame rate only reads 45 or 90).

### Measure first: a frame breakdown in the test loop

**Built (2026-10-22):** `plugin/PerfProbe` with `core/vr/FrameProbe`, `tools/perf-probe.ps1` and
`tools/perf-probe.py` (CLAUDE.md, "Test loop"). It can't time the GPU's own work while the CPU sets the
pace (timestamps only say when the GPU finished, and an idle GPU finishes as soon as the CPU issues);
then it reports how far the GPU lags behind the CPU instead, and `tools/perf-monitor.ps1` gives the
GPU's load. Built at the same time: the `MevrHudPanelDraw` prefilter (a copy of the immediate context's
bound target, with one live check in 256 that turns it off on a disagreement) and the non-blocking
`[BANDPROBE]` read (findings 5 and 6).

What one short session should record, per frame, into `%LOCALAPPDATA%\MEVR\perf\`:
- **Render thread** (QPC, plus `QueryThreadCycleTime` for busy time): Present entry, pass 0 and
  pass 1 captured, the time inside `xrWaitFrame`, our controller sync, the copies, `xrEndFrame`,
  the mirror present.
- **GPU** (D3D11 timestamps, read back with `DONOTFLUSH` like Halcyon's `[GPU]`): pass 0, pass 1,
  UI, the copies.
- **Game thread** (our `UGameEngine::Tick` hook): tick duration and busy cycles, so waiting on
  the render thread separates from work.
- **Counters:** draws per pass, caught exceptions.

A `tools/perf-probe.ps1` request (like `snapshot.ps1`) and a summary script would print medians
and the 95th percentile per segment, and say which thread or the GPU sets the pace.

### Code, by what the measurement finds

| If the limit is | Lever | Expected gain | Effort, risk |
|---|---|---|---|
| Any | Prefilter in `MevrHudPanelDraw` (our seam) | 0.5-2 ms render thread | Small, low |
| Any | `[BANDPROBE]` off or non-blocking | One stall every 120 frames | Tiny (Halcyon bug fix) |
| Any | Stop the probe AV storm at its source | Unknown, small | Small |
| GPU and render thread | Asymmetric per-eye frustum matching the runtime's FOV | ~15% of pixels (PS VR2), plus culling | Medium-high: Halcyon's projection and submit; the per-pass HUD broke the last time (finding 3) |
| GPU | Fixed foveated rendering through NVIDIA VRS on the scene targets (vrperfkit's and OpenXR Toolkit's approach, both MIT; NVAPI is MIT) | 15-20% fps in MSFS (anecdotal), more with heavy pixel shaders | Medium: target heuristics, the UI and post passes must stay full rate |
| GPU | FSR 1 in place of Halcyon's upscale into the swapchain, with a lower render resolution (see "Upscaling") | Up to ~40% fewer pixels shaded at 0.77 per axis | Medium, mostly our seam |
| Frame loop | `xrWaitFrame` early, or on its own thread (finding 9; REFramework's sync modes, MIT) | Whatever the late wait costs | Medium: Halcyon's core |
| GPU | `dynResFraction` driven by frame time (dynamic resolution) | Holds 90 in heavy scenes | Small once the A/B works |
| Render thread | Halcyon's diagnostic hooks off in gameplay (census, traces) | Small per draw, many draws | Small, but in Halcyon's core |
| Render thread | `xrSyncActions` and the locates off the render thread | Its measured time | Small |
| Render thread | Share work across the eyes: one view family with two views (UE3's split-screen path; Halcyon's older SBS "Stereo" mode) shares visibility, occlusion and shadow depths | Large | Large: Halcyon left that mode for image defects |
| Game thread | Time our mod's handlers per tick and trim | Its measured time | Small |

### Upscaling: FSR 1, NIS, DLSS

- **Spatial upscalers (FSR 1, NVIDIA's NIS, CAS sharpening): practical.** They need only the
  finished image, so the engine's age doesn't matter. Halcyon already resamples each eye's
  3072x3188 image into SteamVR's 4164x4244 swapchain at submit; FSR 1 (EASU upscale, then RCAS
  sharpen) would replace that filter, and the game would render at less, e.g. 0.77 per axis
  (AMD's "Ultra Quality", 59% of the pixels) through `dynResFraction` or a smaller backbuffer.
  Our HUD sits on its own panel and stays sharp. The order is right for these filters: NVIDIA
  asks for NIS after tone mapping, bloom and motion blur, which is where an injector sits. FSR 1,
  NIS and CAS are MIT, and this is what vrperfkit and openvr_fsr do for VR games.
- Limits: they don't anti-alias (the game's AA is off), the upscaler's own cost grows with the
  output size (4164x4244 here), and Valve warns that text gets hard to read below about 0.8.
- **Temporal upscalers (DLSS 2+, FSR 2/3, XeSS): a big, risky job.** They need a sub-pixel camera
  jitter every frame, motion vectors, depth, and to run before bloom, DOF and tone mapping. ME LE's
  UE3 has no temporal AA, so it makes neither jitter nor full motion vectors. We would jitter the
  projection per eye (Halcyon rewrites it per pass already), build camera motion from depth
  (moving characters would still smear), and find a hook point inside UE3's post-processing, for
  each eye. Shimmer and ghosting show more in a headset. FSR 2/3 is MIT; DLSS ships under NVIDIA's
  own license, and whether that can sit next to GPL-3.0 code is unchecked **[verify]**.
- **Frame generation** (DLSS 3, FSR 3) doesn't apply to VR; SteamVR's Motion Smoothing is the
  VR equivalent.
- All of this cuts GPU pixel work only. If the render thread is the limit, none of it raises the
  frame rate; the `dynResFraction` A/B decides.

## First measurement: the throttle holds the GPU (2026-10-22 21:39)

The frame-breakdown probe's baseline (`perf\2026-10-22_21-39-02-baseline-*`), 20 s of Stereo 2 gameplay
on the PS VR2 at 90 Hz, standing on foot; `tools/perf-monitor.ps1` alongside. Read with
`tools/perf-probe.py`:

- **SteamVR throttles the game:** `xrWaitFrame` reports a 22.22 ms display period (2 x 11.11) on 883 of
  905 frames; frames come every 22.18 ms (45.1 fps).
- **The throttle works through the GPU, not the CPU.** `xrWaitFrame` returns in 0.02 ms and
  `xrBeginFrame` in 0.01 ms. Instead the GPU is held a median **13.5 ms after each `xrEndFrame`**
  (the same on frames with and without the mirror's present), and reaches the next frame's start
  14.9 ms after the CPU issued it.
- **The render thread spins while the GPU is held.** It is busy 97% of the time (21.1 ms a frame), but
  its busy time doesn't follow the work: 18.6 ms + 0.5 µs per draw (r² 0.01; 1,500 draws: 19.1 ms,
  3,200 draws: 19.9 ms). Most of it is waiting for the held GPU, either in the driver (its limit on
  queued frames) or in UE3's occlusion-query readback **[verify which]**.
- **What the frame costs, at most:** GPU 8.7 ms (median, all work between frame start and
  `xrEndFrame`; it catches up to within 2.3-2.6 ms of the CPU); game thread 8.2 ms busy (the engine
  8.0, ours 0.2); render thread: its real issuing work is the 21.1 ms minus the wait, about 7 ms if
  the wait covers the hold **[inference]**. All three fit in 11.1 ms.
- Our own steps cost little: controllers 0.18 ms, quads 0.07 ms, Halcyon's copies 0.34 ms,
  `xrEndFrame` 0.37 ms. No access violations during the capture (the startup storm, finding 7, is
  over by then). The capture's first eye is taken 4 times a frame, so its "left eye" split isn't the
  boundary between the eyes.
- perf-monitor: GPU 78% (the whole GPU, the compositor included; whether a held queue counts as busy
  is unclear), busiest game thread 92% of a core.
- This matches 2026-10-08: with the SteamVR dashboard up (VISIBLE, no throttle) the same scene ran at
  90 fps with the busiest thread at 69% (7.7 ms a frame).

**The likely loop [inference]:** once SteamVR throttles (a load hitch is enough), it holds the GPU; the
render thread then spins until the GPU is free, so the game looks CPU-bound at ~20 ms a frame (SteamVR's
own log: "ApplicationTime CPU: 20.291ms"), so SteamVR keeps throttling. Halcyon's late wait (finding 9)
may feed it: the game never waits in `xrWaitFrame`, so SteamVR never sees idle time.

**Next:** lift the throttle (Throttling Behavior: Fixed, full rate) and measure again. If the frame
rate rises toward 90 and holds, the fix is that setting (and possibly an early `xrWaitFrame` so the
automatic mode recovers by itself). If SteamVR still halves it, the frame loop is next.

## The settings didn't free it; the early wait did (2026-10-22, 23:20-23:51)

| Capture | Setting | fps | GPU held after `xrEndFrame` | GPU work | Game thread | Render thread busy |
|---|---|---|---|---|---|---|
| 21:39 baseline | Auto throttling | 45.1 | 13.5 ms | ≤ 8.7 ms | 8.2 ms | 21.1 ms |
| 23:20 unthrottled | Throttling Fixed, full rate (`framesToThrottle 0`); SteamVR at 46% | 45.2 | 14.8 ms | ≤ 7.2 ms | 6.1 ms | 21.6 ms |
| 23:25 reprojection | + Legacy Reprojection Mode off (no key seen in the file) | 45.5 | 15.7 ms | ≤ 6.3 ms | 4.6 ms | 21.3 ms |
| **23:50 earlywait** | **`[Performance] earlyFrameWait = 1`** (b73e360) | **89.7** | 4.9 ms | ≤ 6.2 ms | 4.8 ms | 10.6 ms |

- SteamVR's per-application settings changed nothing; lighter work only lengthened the hold.
- **The cause was Halcyon's late wait** (finding 9). RunFrame now calls `xrWaitFrame` for the next
  frame right after `xrEndFrame` (before the game renders it) and begins that frame at the next
  Present. SteamVR then paces the game at the full 90 Hz: the median frame is 11.15 ms (p90 12.06).
  `xrWaitFrame` itself still returns at once (0.02 ms); what changed is what SteamVR sees.
- **Headroom is thin:** the render thread is busy 10.6 ms of 11.1 (p90 11.5); part of that is still
  waiting for the GPU (held 4.9 ms after each frame, busy time not following the draws), so its real
  work is less. GPU work ≤ 6.2 ms, game thread 4.8 ms. Heavier scenes (combat, effects) can miss
  frames; the probe in those is the next measurement.
- Switch: `[Performance] earlyFrameWait` in `ASI\LE1MEVR.ini`, live; 0 = upstream's order (45).
  Not yet checked in the headset: head-pose latency and judder with the new order **[verify]**.

## At 90 fps the render thread is the limit, and it is real work (2026-10-23 00:01)

- With the early wait: median frame 11.13 ms, p90 12.5 ms; the render thread busy 10.6 ms of 11.1, so
  heavier moments miss (fpsVR: 14-16% reprojected, 60s-70s fps in busy spots). NVIDIA's "Virtual
  Reality pre-rendered frames" 1 -> 2 changed little.
- **Correction:** the probe's "spinning" reading (busy time not following the draws) was wrong at 90
  fps. An external stack sampler (`tools/sample-render-thread.ps1`, `mevr-stack-sampler`: 4,855
  samples of the render thread over 8 s) puts the top frame in the game's exe 44%, d3d11 18%, ntdll
  (heap, locks, timers) 15%, the NVIDIA driver 11%, our ASI 3%; waiting (`NtWaitForSingleObject`) only
  3%. On the stack: Halcyon's Present work 11% (1.2 ms, the XR frame included), SteamVR's runtime 6%.
  The d3d11/ntdll names are the nearest exports (no Microsoft symbols downloaded).
- Found there: file-system calls on the render thread (~2.4%): Halcyon's `IsOpenXrDisabledForBoundary`
  stats a marker file twice per Present, uncached. Now cached for a second. Halcyon's render-target
  census (`[REFLCEN]`, every draw of every Stereo 2 frame) now counts only around the presents it
  logs. The other per-draw diagnostics are already off by default.
- Next: the settings A/B (`DetailMode=1`, `LightEnvironmentShadows=False`, `AmbientOcclusion=False`)
  to price the scene's draws; then, if needed, sharing the eyes' CPU work (one view family).

## Next session: the no-code checks (about 15 minutes)

Same spot each time (e.g. the Normandy's CIC, standing still, same direction), 30 s per step:
1. Start the monitor before the game and leave it running:
   `powershell -ExecutionPolicy Bypass -File "H:\ME VR Attempt\tools\perf-monitor.ps1"`. At each step
   below, after 10 s in place, take a frame breakdown: the VR hands tab's "Measure frame timing", or
   Claude runs `tools/perf-probe.ps1 -Tag <step>`.
2. SteamVR > Settings > Video > Per-Application Video Settings > the game: Throttling Behavior
   Fixed, at the full refresh rate. Baseline: note the fps, or let Claude read
   `tools/perf-summary.py` and the perf CSV afterwards.
3. `dynResFraction = 0.6` (restart): GPU-bound if the fps rises clearly.
4. Back to 1.0; `AmbientOcclusion=False`, `LightEnvironmentShadows=False` (restart): the cost of
   the two most expensive per-eye settings.
5. Controllers down until they sleep (session still focused): if fps jumps, the input path costs.
6. Optional: PS VR2 at 120 Hz, throttling back on Auto: does it hold a steady 60?

## Other options noted

- **Eye-tracked foveated rendering on the PS VR2.** Sony's PC adapter has no eye tracking, but
  the community driver PSVR2Toolkit (MIT; its eye data carries a non-commercial clause) exposes
  it to OpenXR apps through SteamVR since v0.1.3. The foveation itself would still be ours (VRS
  with a moving centre).
- **NVIDIA driver settings:** Low Latency Mode "Ultra" is for GPU-bound DX11 games at 60-100 fps;
  hardware-accelerated GPU scheduling has mixed VR reports; nothing authoritative on VR. A driver
  frame cap would also cap the headset, because the XR submit runs inside the game's Present.

## Sources

- Local: `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt` (sessions of 2026-10-08, -21, -22),
  `<Win64>\Logs\LE1MEVR.log`, `Steam\logs\vrcompositor*.txt` and `vrserver.txt`,
  `Steam\config\steamvr.vrsettings`, `BioGame\Config\GamerSettings.ini` and `BIOEngine.ini`,
  `ASI\MELEVR.ini`; Halcyon's `xr_session.cpp`, `d3d_capture.cpp`, `render_hook.cpp`,
  `head_aim.cpp`, `pchud.cpp`; `reference/MassEffect1-VR/release/MELE-VR.bat`.
- The access-violation addresses were resolved with the deployed `LE1MEVR.pdb` (dbghelp
  `SymGetLineFromAddrW64`).
- SteamVR throttling and reprojection: [Aaron Leiby (Valve) on half-time mode](https://steamcommunity.com/app/358720/discussions/0/385429254937377076),
  [Valve GDC 2016, Advanced VR Rendering Performance](https://media.steampowered.com/apps/valve/2016/Alex_Vlachos_Advanced_VR_Rendering_Performance_GDC2016.pdf),
  [SteamVR 1.16.1 notes (per-app throttling)](https://forum.il2sturmovik.com/topic/68744-steam-vr-beta-update-jan-14th/),
  [per-app UI described](https://forums.flightsimulator.com/t/steamvr-1-16-1-and-valve-index-observation/351754/11),
  [an MSFS setup using it](https://forums.flightsimulator.com/t/msfs-vr-experience-and-setup-tips-steamvr/614218),
  [openvr.h (setting keys, frame-timing flags)](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h),
  [per-app keys in steamvr.vrsettings](https://steamcommunity.com/sharedfiles/filedetails/?id=2805545613),
  [Road to VR on Motion Smoothing](https://roadtovr.com/steamvr-motion-smoothing-asw-alex-vlachos/).
- PS VR2 on PC: [UploadVR, 90/120 Hz](https://www.uploadvr.com/sony-fixes-playstation-vr2-on-pc-distortion-issue/),
  [Mixed, adapter limits](https://mixed-news.com/en/playstation-vr-2-to-become-pc-vr-headset-via-adapter-in-august-but-without-eye-tracking/),
  [PSVR2Toolkit](https://github.com/BnuuySolutions/PSVR2Toolkit),
  [SteamVR 100% = 3400x3468 (forum)](https://forums.flightsimulator.com/t/psvr2-is-coming-to-pc/631379?page=2).
- OpenXR frame loop: [xrWaitFrame](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrWaitFrame.html),
  [xrBeginFrame](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrBeginFrame.html),
  [Khronos forum: runtimes tune the wait](https://community.khronos.org/t/disabling-automatic-xrwaitframe-delay/108147),
  [Khronos forum: SteamVR blocks in xrBeginFrame](https://community.khronos.org/t/need-help-retrieving-frame-timings-from-openxr/110914),
  [Khronos forum: back-to-back calls gummed to 45](https://community.khronos.org/t/speeding-up-xrwaitframe/108028),
  [REFramework VR.cpp (sync modes)](https://github.com/praydog/REFramework/blob/master/src/mods/VR.cpp),
  [Valve GDC 2015, Advanced VR Rendering](https://media.steampowered.com/apps/valve/2015/Alex_Vlachos_Advanced_VR_Rendering_GDC2015.pdf).
- VRS and upscaling: [vrperfkit](https://github.com/fholger/vrperfkit) (`src/d3d11/d3d11_variable_rate_shading.cpp`),
  [OpenXR Toolkit foveated rendering](https://github.com/mbucchia/OpenXR-Toolkit/blob/gh-pages/fr.md),
  [NVIDIA: Turing VRS](https://developer.nvidia.com/blog/turing-variable-rate-shading-vrworks),
  [NVAPI (MIT)](https://github.com/NVIDIA/nvapi),
  [Varjo: 20% with OpenXR Toolkit](https://varjo.com/blog/dynamic-foveated-variable-shader-rate-with-openxr-toolkit-and-aero-a-k-a-foveated-rendering),
  [FSR 1](https://github.com/GPUOpen-Effects/FidelityFX-FSR/releases/tag/v1.0.2),
  [NIS](https://github.com/NVIDIAGameWorks/NVIDIAImageScaling),
  [CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS),
  [openvr_fsr](https://github.com/fholger/openvr_fsr).
- Engines: [UE3 split-screen optimization](https://docs.unrealengine.com/udk/Three/OptimizingForSplitscreen.html)
  (search snippets only), [UE XR performance features](https://dev.epicgames.com/documentation/unreal-engine/xr-performance-features-in-unreal-engine).
- D3D11 and drivers: [D3D11_CREATE_DEVICE_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_create_device_flag),
  [D3D11 multithreading](https://learn.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-render-multi-thread-intro),
  [ID3D11Multithread](https://learn.microsoft.com/en-us/windows/win32/api/d3d11_4/nn-d3d11_4-id3d11multithread),
  [NVIDIA Low Latency Mode](https://www.nvidia.com/en-us/geforce/news/gamescom-2019-game-ready-driver/),
  [hardware-accelerated GPU scheduling](https://devblogs.microsoft.com/directx/hardware-accelerated-gpu-scheduling/).
- Not found: published millisecond costs of FSR 1 at VR resolutions; what SteamVR's
  `motionSmoothingOverride` values mean; whether UE3 shares shadow or visibility work across the
  views of one view family; the DLSS SDK license's compatibility with GPL-3.0 (not searched).
