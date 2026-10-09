# 16: LE1 VR performance with Halcyon's renderer (2026-10-08)

**Question.** In Stereo 2 (two full renders per present, 3072x3188 per eye) on an RTX 4090 with a
Quest over Steam Link / SteamVR, the game often runs well below the headset's 90 Hz. Is it CPU-
or GPU-bound, and what limits it?

**Data** (all from 2026-10-08, ten sessions in one Halcyon log):

- Halcyon's log `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`: `[PRESENTHZ]` fps, `[GPU] avgMs` (the
  frame interval, not GPU busy time), `[VRSTATE]`, `[XR] session state`, `[RELIEF]`, `[HEADCTRL]`.
- `tools/perf-monitor.ps1` CSVs in `%LOCALAPPDATA%\MEVR\perf\` (1 s samples; 24 logical cores).
- Our log `...\Binaries\Win64\Logs\LE1MEVR.log` (latest session only). Its heartbeats carry fps
  and counters, **not** per-tick timings.
- `MELEVR.ini` (profile Custom 1), `GamerSettings.ini`, `BIOEngine.ini`, Halcyon's source.

## Verdict

Neither the GPU nor the game thread is the limit. Most of the missing frame rate comes from three
things:

1. Frames are slow **while the OpenXR session is FOCUSED**. The same scene runs at 90 when the
   session is only VISIBLE (SteamVR dashboard up). In FOCUSED the process burns about twice the
   CPU per frame on one thread, which looks like spinning or waiting, not rendering.
2. Halcyon's **depth capture** (depth pop / depth map) costs about 30% while it runs.
3. Its **cutscene cap** paces cutscenes to half the display rate: 45 fps at 90 Hz.

Our mod's game-thread work (ProcessEvent handlers, arm IK, hand aim, weapon) does not limit the
frame rate.

## Sessions

| Start | Target | VR fps (median) | Notes |
|---|---|---|---|
| 14:18 | 2490x1401 | 75.5 / 57.5 with depth capture | |
| 14:24 | 2490x1401 | 81.0 / 53.0 with depth capture | |
| 14:48 | 2490x1401 | 47.5 FOCUSED, then 80-90 VISIBLE | the "47 fps" session |
| 15:18 | 2009x2009 | ~49 FOCUSED, then ~90 VISIBLE | Default profile |
| 15:23 | 2009x2009 | 47-49 FOCUSED for 6 minutes | depth capture on much of the time |
| 15:47 | 3072x3188 | 70-73 FOCUSED, 89.5-90 VISIBLE | perf CSV `perf-...-154247` |
| 15:51 | 3072x3188 | 80.5 | |
| 15:54 | 3072x3188 | 64-76 or 88-90 depending on the scene; 41-49 with depth capture; 45.0 in cutscenes | latest |

The flat (no headset) sessions run at 89.5-90.

`python tools/perf-summary.py --session N` reproduces the per-state medians.

## Findings

### 1. Not GPU-bound

- `nvidia-smi` shows 53% median GPU use during FOCUSED VR at about 72 fps, at a 2790 MHz clock.
  When the same frozen scene reached 90 fps, it showed 62%. This figure covers the whole GPU,
  including SteamVR's compositor; the encoder is separate hardware.
- Stereo at 3072x3188 holds 90.0 fps for whole minutes in the latest session (15:58:14-15:59:10
  and 15:59:52-16:00:04), with the arm IK driving and a gun in hand.
- So lowering the resolution, AO or DOF won't buy much while the limit is elsewhere.
  - Exception: if submission serializes CPU and GPU work (see finding 2), GPU time counts toward
    the frame time even at 50% use. [verify]

### 2. The FOCUSED vs VISIBLE gap (the largest effect)

The same pattern appears in three sessions, each time with an unchanged scene and settings:

- **14:48:** FOCUSED 46-49 fps for about 3 minutes. At `14:51:57 [XR] session state -> 4`
  (VISIBLE) it jumps to 75, then 85-90.
- **15:18:** FOCUSED 47-51 fps. At `15:19:50 session state -> 4` it rises to 81.5, then 89.5-90.
  Back to state 5 at 15:22:06 drops it to 54-63; state 4 at 15:22:27 restores 88.5.
- **15:47:** Halcyon's menu was open throughout (`menu=1`, which blocks game input, so the scene
  was frozen). FOCUSED gave 71-73 fps. At `15:48:59 state -> 4` it went to 89.5-90.

The perf CSV for the 15:47 session:

| Medians | FOCUSED (15:47:34-15:48:56) | VISIBLE (15:49:00-15:49:29) |
|---|---|---|
| fps | ~72 | ~90 |
| GPU use | 53% | 62% |
| busiest thread (tid 33172) | 95.6% of a core | 69.1% |
| next thread | 40.5% | 29.6% |
| game process (of 24 cores) | 8.25% (~2.0 cores) | 5.1% (~1.2 cores) |

Per frame, that is about 27 ms of process CPU when FOCUSED against about 13.6 ms when VISIBLE,
for the same rendering. The extra time is spent waiting or spinning, not drawing. The busiest
thread uses about 13.2 ms per frame when FOCUSED and 7.4 ms when VISIBLE.

**Which thread.** The busiest thread is neither the game thread nor the startup present thread:

- The game thread is `[FVCDRAW] thread=42096` in that session, stable all session. It is
  probably the 40% "next" thread.
- The present (render) thread at startup was `[SFR] threadcheck fvcDraw=42096 present=37400`.
- tid 33172 only becomes the busiest thread when VR starts.
- Candidates [verify]:
  - the UE3 render thread, if it was restarted after startup;
  - the NVIDIA driver's worker thread;
  - a SteamVR client thread.

**Upstream saw the same thing in ME2.** Halcyon's comment at `d3d_capture.cpp` `[MIRRORTHROTTLE]`
reports "focused 59.6-60.2 locked vs 76-112 unfocused". It was fixed by presenting the desktop
mirror only one frame in N: ME2 went from 87 to 119.7 fps. Upstream measured the ME1 cause as
back-pressure on the flat mirror's Present ("average frame time jumped 5.8ms -> 14.2ms with
identical engine work"). LE1's lever is `mirrorPresentEvery`. It defaults to 1 (present every
frame) and our Custom 1 profile has 1.

**Other candidates** [verify]:

- SteamVR's keyed-mutex texture sync at submit. Halcyon strips `D3D11_CREATE_DEVICE_SINGLETHREADED`
  to make it work (`[SVRFIX]`), and it may stall the render thread on the GPU only while the
  compositor is reading the scene app.
- Our controller path, which runs only when FOCUSED:
  - one `xrSyncActions`, 14 action reads and 4 `xrLocateSpace` per frame
    (`HalcyonBridge::OnFrameBegun`);
  - the Touch → pad merge in the XInput hooks;
  - arm IK, hand aim, the HUD panel and the reticle.
  - These look cheap on paper, and finding 4 shows the mod at 90 fps with everything on.

### 3. Depth capture costs about 30%

- In the latest session, Halcyon's depth path came on at 15:55:52.8 (`[DIBR_VIZ] color copy
  created 3072x3188`, `[RELIEF] mode=aer ... copies (380.0/s)`). The cause was the menu's depth
  map or the `P` depth-pop key; `reliefEnabled = 0` is saved in the ini.
  - fps fell from 70.5-71.0 to 48.5 within two seconds and stayed at 41-50.
  - `[RELIEF]` stopped after 15:56:42, and fps climbed 46.5 → 61 → 83.5 → 90.
- While on, it:
  - copies the 3072x3188 depth buffer 380-425 times a second (about 5 per frame);
  - runs `NoteDepthDraw` on every draw (about 270,000 draws per second);
  - adds a warp pass per eye.
- `depthPopKeyEnabled = 1` leaves `P` live in game.

### 4. Our mod's cost

- **ProcessEvent hook** (`plugin/ProcessEventHook.cpp`, `engine/ProcessEvent.cpp`). Per call:
  - the original call;
  - two uncontended atomic increments, a `__try` (free on x64), and a loop of 7 pointer compares.
  - At tens of thousands of calls per second, that is well under 0.1 ms per frame.
- **Arm IK, hand aim and weapon.** At 15:58:20-15:59:11 the heartbeats show 89.7-90.0 fps with
  `arm IK: driving: left and right following the controllers` and 450 player-mesh writes per
  5 s. The `UpdateSkelPose` hook sees about 6,500 calls a second and compares pointers to reject
  all but the player's mesh.
  - The game thread is the "next" thread at 30-45% of a core (about 4-6 ms per frame). It is not
    the bottleneck.
- **Render-thread seam: `MevrHudPanelDraw`** (`[MEVR]` in `d3d_capture.cpp`).
  - It runs on every draw of the second pass while the HUD panel is armed.
  - For every full-viewport draw it makes about 8 COM calls: `RSGetViewports`,
    `OMGetRenderTargets`, the RTV and texture `GetDesc`, `GetResource`, `QueryInterface`, and three
    `Release`s. Scene-sized 8-bit targets add `OMGetBlendState` and `PSGetShader`.
  - On a device made thread-safe by `[SVRFIX]`, each of those calls takes a lock.
  - At about 2,800 draws per pass this is an estimated 0.5-2 ms per frame. [verify: A/B with
    `hudPanel = 0`]
  - If it matters, a cheap prefilter on Halcyon's cached `g_currentRtvWidth` and
    `g_currentRtvFormat` would skip most scene draws before the live checks.
- There are no per-tick timings in our heartbeats. Adding ms per handler would settle this part
  directly.

### 5. Cutscenes run at exactly 45.0

- `[VRSTATE] ... cutscene` stretches read 44.5-45.0 flat (15:57:24-15:57:44, 15:59:14-15:59:46).
- This is `cineFpsCap = 1` (`[CINEPACE60]`): it paces staged cutscenes to display/2. Upstream
  tuned it on a 120 Hz headset (60 fps); at 90 Hz it gives 45.
- Its comment says uncapped cutscene content stepped visibly. Conversations and gameplay aren't
  capped.

### 6. Scene-dependent 70-76 vs 90 in FOCUSED gameplay

- Same session, same settings, weapon drawn: 69-76 at 15:57:50-15:58:12, then 88-90 from 15:58:14.
- Reflections: Halcyon's `[REFLRATE]` boosts each planar `SceneCaptureReflectComponent` to re-render
  every frame (`boosted #1 ... rate 60.0 -> 1000.0` in Eden Prime). That is one extra scene render
  per reflective surface per pass. Kill switch: a file named `MELEVR_DISABLE_REFLRATE.txt` next to
  the exe. Cost of turning it off: about 30 Hz reflection judder on head turns.

### 7. The desktop mirror, SteamVR and Steam Link

- **The mirror.**
  - The game's window is a 3072x3188 borderless window, larger than the monitor (`[SCREQ]
    clientRect=2560x720`), so DWM composes it.
  - Halcyon presents it every frame with tearing allowed (`forcedTear=1 swapTearOK=1`).
  - Upstream's evidence above makes this present the prime suspect for the FOCUSED stall.
  - The XR frame is already submitted before the mirror's Present (`XrSession::OnPresent` runs
    first), so throttling the mirror can't affect the headset.
- **SteamVR and Steam Link.**
  - Encoding is not the cap: the pipeline delivers 90.0 fps in some scenes.
  - The 6.4 fps at 16:00:06+ is state 4 → 3 (headset idle), which is SteamVR's normal throttle.
  - SteamVR's recommended eye size changed between sessions (2880x2956 at 14:43-15:23, 2204x2352
    otherwise), which means SteamVR's resolution setting moved.
- **Image (not fps).**
  - Halcyon renders a symmetric 108° FOV (`halfH=0.942`). The headset's frustum is asymmetric:
    `hmdFov=(-0.942,0.698,0.733,-0.942)`.
  - The submitted region per eye is therefore `rect=1774x1945` of the 2204x2352 swapchain: about
    80% of SteamVR's recommended size in each axis.
  - The 3072x3188 render supersamples that region about 1.39x linearly.
  - About 34% of the rendered pixels fall outside the headset's view.

### 8. What upstream expects

- `MELE-VR.bat` recommends Stereo + Balanced, 4096x4250. Our target is Performance, 3072x3188.
- Comments in `vr_config.h` and `d3d_capture.cpp`:
  - ME1 at "85-95" fps once present tearing was forced;
  - ME2 at 119.7 fps at 120 Hz after the mirror throttle.
  - The old 60 fps ceiling was `MaxSmoothedFrameRate=60` in BIOEngine.ini.
- Halcyon re-asserts TRUE/120 at attach (`[ENGSMOOTH]`); the file on disk reads `FALSE/60` again
  after the game rewrote it.
  - `tools/vr-game-settings.ps1` sets frame smoothing off, which works against that assert. It
    doesn't cap fps either way, since the cap is at least 120 or smoothing is off.
- Halcyon also re-enables depth of field each launch (`[DOF] ... set to True`, `disableDof = 0`),
  which overrides our tool's DOF off.

## Recommended changes, ranked

1. **`mirrorPresentEvery = 2`** (then 3) in `MELEVR.ini`.
   - This is Halcyon's own ini-only lever, with no code change.
   - Expected gain: FOCUSED gameplay closer to the VISIBLE rate, roughly 70-76 → 85-90 in heavier
     scenes and 48 → 80+ where the old stall appeared. Upstream got 87 → 119.7 in ME2.
   - Cost: a choppy desktop mirror only.
   - To confirm: compare the same spot with the session FOCUSED. `[MIRRORTHROTTLE]` should appear
     once in the log. Compare `[PRESENTHZ]`, and the busiest thread and `cpu_game` in
     perf-monitor; the busy thread should drop from about 95% toward 70%.
2. **Keep depth pop and the depth map off, and set `depthPopKeyEnabled = 0`.**
   - Expected gain: avoids the ~30% hit (71 → 48).
   - Cost: none unless you want depth pop.
   - To confirm: no `[RELIEF]` lines in a session.
3. **Cutscene cap.**
   - Option A: set `cineFpsCap = 0`. Expected: 45 → up to 90 in cutscenes. Risk: the cutscene
     content judder Halcyon's comment describes.
   - Option B: run the Quest at 120 Hz in Steam Link. Cutscenes then sit at 60, but gameplay is
     less likely to hold the refresh rate.
   - To confirm: A/B one cutscene and judge the judder in the headset.
4. **Measure our HUD seam:** compare `hudPanel = 0` with 1 (and `handReticle`) in a fixed spot.
   - If the gap is over about 1 ms per frame (a few fps near 90), add the prefilter from finding 4.
     That is our code, not Halcyon's core.
5. **`MELEVR_DISABLE_REFLRATE.txt`** in levels with reflective floors.
   - Expected gain: one scene render per reflector per pass.
   - Cost: 30 Hz reflection judder.
   - To confirm: `[REFLRATE] boosted` lines absent, and an fps A/B in a reflective level.
6. **GPU-side settings, only after 1-3:**
   - Low (2048x2124), or `dynResFraction` below 1;
   - `AmbientOcclusion=False`;
   - `disableDof = 1`.
   - These help only if the GPU is on the critical path. Test: does fps change with resolution in
     a fixed FOCUSED spot?
7. **Don't turn on `stereoFramePacing` at 90 Hz.** It locks to whole display periods, so anything
   below 90 would become 45 (or 30).

## Measurements that would close the open questions

- **Which thread is busy.** Use Process Explorer → Threads (TID, start address,
  `MassEffect1.exe`, `nvwgf2umx.dll` or `vrclient_x64.dll`) during FOCUSED gameplay, alongside
  `tools/perf-monitor.ps1`. Adding each thread's start module to perf-monitor would make this
  automatic.
- **CPU vs GPU per frame.** PresentMon on `MassEffect1.exe`: CPUBusy, CPUWait, GPUBusy and the time
  inside the Present call, FOCUSED vs VISIBLE. A long time inside Present when FOCUSED confirms
  mirror back-pressure.
- **SteamVR's own view.** SteamVR's Advanced Frame Timing (app CPU and GPU time, late or dropped
  frames, throttling) and Steam Link's statistics overlay for encode time and bitrate.
- **Input-driven or not.** While FOCUSED in a fixed spot, set the controllers down until they sleep.
  Our input, VRIK and the pad merge then go idle while the session stays FOCUSED. If fps jumps to
  90, the cost is input-driven; if not, it's SteamVR's focused-scene path.
- **Our per-tick cost.** Add ms timings per handler to our heartbeats (ArmIk, HandAim, Weapon,
  ProcessEvent dispatch).

## Quest Link (Oculus runtime), 2026-10-08 evening

- **Measured:** 45.0 fps in VR cutscenes (Halcyon's `cineFpsCap`) and 46.5-49 fps in gameplay,
  from `tools/perf-summary.py` on the 20:47 session.
- **Resolution is not overshot:** the runtime recommends 2880x2956 per eye ([XR] line); we render
  3072x3188, about 1.07x per axis, roughly 15% more pixels.
- **Suspected cause:** gameplay sits at almost exactly half of 90 Hz, which matches the Oculus
  runtime's Asynchronous Spacewarp (ASW) locking the app to half rate once frames run late.
  On SteamVR / Steam Link the same build held 90 fps for minutes. **[verify]**
- **Checks:**
  - Oculus Debug Tool, ASW = Disabled: does gameplay rise above 45?
  - Link's own render resolution and the encode bitrate.
  - `vr-game-settings.ps1 -Quality Low` (2048x2124) for comparison.
  - The focused-mirror cost applies on Link too; `mirrorPresentEvery = 2` is already set.
