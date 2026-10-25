# The CPU frame time plan (LE1 VR, written 2026-10-24 before a context compaction)

The goal: the render thread inside the 11.1 ms budget at 90 Hz on the PS VR2 at the spots the player walks, with
the spikes gone. Everything below is measured on this machine (RTX 4090, Stereo 2, 2118x2160 target with FSR);
the attempt-by-attempt log is `research/35-performance-attempt-log.md` (attempts 1-40).

## What the frame is made of (evidence)

- The render thread sets the pace everywhere; the game thread is at 45-55% and the GPU at 40-60% (every
  `tools/perf-probe.py` verdict today).
- Two scene renders per frame, one per eye, plus Halcyon's present path (~1.2 ms) and ~0.5 ms of ours.
- At a **light spot** (the player's resume save, 1,400 draws): first render 1.5 ms, second 3.8 ms, frame 7.3 ms. The
  second render's extra 2.3 ms is the engine's per-primitive occlusion step spinning in `GetQueryData(bWait=1)` for the
  first eye's queries of the same frame (one view state, one occlusion history: the "previous frame's" query is the
  other eye's; levers log `instances` 2.37 ms vs 0.12 ms).
- At a **medium spot** (the player's, 2,500-2,600 draws): first render 2.4-3.2 ms, second 6.8-7.5 ms, frame 10.7-10.8
  ms median, p90 11.7. With the engine spin removed (perEyeOcclusion) the second render's wait moved into Umbra's own
  result reads: `query results` 1.3-2.8 ms, longest single read 7 ms, ~15,000 polls a frame. With Umbra's wait also
  removed (umbraQueryWait=0) both renders cost the same, 4.3 ms each (14:53 probe: #0 4.38, #1 4.33), but Umbra then
  counts unready occluders as visible and draws double (p90 10,000 draws, GPU 30 ms): not usable as is.
- At the player's **heavy spots** (3,800-4,900 draws, 13 ms, spikes to the 20s while walking): each render is honest
  drawing work, ~0.7 us per draw, twice. Settings move it by ±0.1 ms (the 20:00 sweep: every `show` category off in
  turn; foliage radius 1.0 / 0.5 / 0 identical). Only fewer draws or less per-draw work can help there.
- The spikes the player felt on 2026-10-24 were mine: perEyeOcclusion made every ~10th frame draw three times the
  meshes (draws 2,574 median / 8,544 p90, frames 24 ms p90). Off by default since bf38a6c. The remaining spikes
  during walking are unmeasured (a walking probe with the stack sampler is still owed).

## What the player's number is (added 17:30, research/35 attempt 41)

fpsVR shows SteamVR's CPU frametime: wall time from xrWaitFrame's return to xrEndFrame. The probe prints the same span as
"poses -> submit, wall"; in gameplay it is within 0.4 ms of "busy, without the XR waits" (11.12 vs 10.72 ms at the medium
spot), so the numbers above are the player's numbers at those spots. fpsVR's own session files (`tools/fpsvr-session.py`)
say what the player lived: 2026-10-23's long walk 14.4 ms median, 20.6 p90; today 12.4 / 16.4. The loop's missing piece
is still the heavy, moving spot. And fpsVR's GPU frametime is 9.5 median / 13.5 p90 ms in every real session: the GPU is
over budget at the tail too, so items 5 and 6 (fewer draws, one submission) matter for both sides, and GPU offload is not
free.

## What the loop found on its own (18:00-18:40, attempts 42-43)

The loop now reaches Eden Prime's heavy cells by itself (`mevr density`, `set SFXPawn_Player Location`, god and ghost,
`tools/spot-survey.ps1`). At the heaviest (4,100-4,500 draws): render thread 9.6-10.7 ms, GPU 8.7-9.8, game thread 4.5;
items 1 and 2 below do nothing there (the second eye does not wait on the fake headset); with the target raised to
2400x2448 the GPU goes over budget and the fpsVR-style CPU time shows the player's tail (10.3 median, 15.1 p90, 19.9
max): at heavy spots the GPU is the limit that fpsVR reports as CPU. The stack sampler found 20% of the render thread
rebuilding D3D11 bound shader states (input layouts) every draw: the `keepBoundShaderStates` lever (attempt 43) took the
heavy spot from 15.8-16.5 to 10.5-11.1 ms in a fresh session, about 1 ms in a warm one. It is item 0 and on by default.

## Where the heavy spot stands after attempts 44-45 (20:10)

Warm, at the heaviest Eden Prime cell, fake headset at 2118x2160: 10.6-11.0 ms fpsVR-style CPU (budget 11.1), GPU 10 ms.
The frame: the opaque base pass 27% (4,500 draws, 2,500 of them static meshes, UE3's standard per-draw D3D11 path),
Umbra's resolve 21% (twice a frame; sharePass 1 hangs the game: the resolve cannot be skipped), lights 15%, below the
game d3d11 17% + driver 11% + ntdll 12%. No [SystemSettings] key moves the draw count; the size cull (minScreenRadiusPx)
finds few tiny objects (8 px: 0.3 ms, invisible; 32 px: 0.8 ms with pop-in). What is left is deep: Umbra's resolve
shared between the eyes, or the second eye's draws replayed with its resolve kept (items 5 and 7 below).

## The deep levers, settled (22:25, attempt 46)

Umbra's resolve cannot be shared between the eyes: its query pipeline runs across consecutive resolves regardless of
eye (issued in one, read in the next), so a second eye that issues no queries, or answers its reads with anything but
the GPU's results, opens the occlusion state for both eyes (draws double); skipping its resolve deadlocks the command
ring (attempts 10-11). And the opaque base pass is the flush batches inside that resolve, so the second eye's draws
cannot be replayed without skipping it. Both deep CPU levers are closed. What remains at heavy spots is the game's own
per-view work twice, at ~10.7-11 ms warm on the fake headset: `CompositeDynamicLights True` buys 0.3-0.5 ms (a look
trade-off), the size cull 0.3 ms invisibly, and the GPU side is the render scale. The architectural answer for the
heaviest areas stays Depth mode.

## The sequence, with the expected gain and the evidence for each

1. **umbraShareResults** (built, a4dffb5, default 0, unmeasured). The second eye's Umbra reads that aren't ready get
   the first eye's i-th result of the same frame instead of waiting. Evidence: the two eyes test the same occluders
   in the same order from cameras 6 cm apart; the first eye's reads are ready in 0.05-0.4 ms (the GPU is idle at
   frame start), the second's wait is 1.3-2.8 ms because the GPU is busy with the first eye's draws. Expected: the
   medium spot from 10.7 to ~8.5 ms. Risk: index drift between the eyes' query orders gives a wrong occluder a wrong
   answer for a frame (a pop); the measure is draws p90 (must stay ~2,600, not 8,000) and a snapshot pair.
2. **flushFirstEye** (built, d151c78, default 0, unmeasured). `ID3D11DeviceContext::Flush` right after the first
   eye's render so the GPU starts on it at once. Evidence: the probes show the GPU 1-1.3 ms behind the CPU at the
   first eye; the second eye's wait is GPU latency. Expected 0.5-1 ms off the wait where it exists; nothing at heavy
   spots. Free to try.
3. **The engine's own spin** (perEyeOcclusion, off). It removed 2.3-4.5 ms at light spots but caused the draw bursts.
   The burst mechanism is in `UpdatePrimitiveOcclusion`'s re-test logic (the random re-query of occluded primitives
   and the "not visible for a while" rule read the entry's times and pixel fraction; with two sets they synchronise
   thousands of primitives into one frame). A fix needs that logic read in Ghidra (0x762550) and the second set's
   entries seeded from the first eye's (same primitive, same times) rather than empty. Expected: light spots from
   7.3 to ~5 ms, medium spots another 1-2 ms; nothing at heavy spots.
4. **Halcyon's present path** (~1.2 ms: the desktop mirror every frame, the stereo capture copies, probe-and-promote).
   Mirror every 2nd frame or off, copies trimmed. Expected 0.5-0.8 ms everywhere, certain, no visible cost.
5. **The draw replay for the second eye** (sharePass 2, `plugin/DrawReplay`, exists, never run in VR). Keep the second
   eye's Umbra resolve (the command ring must drain or the game thread deadlocks: attempts 10-11) and replay the first
   eye's recorded D3D11 calls with the view constants swapped instead of the engine's draw submission. Expected: the
   second render from ~4.3 to ~2.5 ms at heavy spots (the resolve stays). This is the only lever for draw-bound
   frames. Risk: correctness of the constant substitution across every shader (the eye-diff tool checks it).
6. **Fewer draws** at heavy spots: LOD bias and cull distance keys that `scale set` didn't take live, set at launch
   through the mod's SystemSettings applier. Visible trade-offs; 0.5-1.5 ms.

Targets: the medium spot from 10.7 to ≤ 9 ms (1 + 2 + 4), the heavy spots from 13 to ≤ 11 (5 + 6), light spots
comfortable already. If 1 and 5 both land, full stereo at 90 Hz in most of the game is realistic; if 5 fails, the
honest end state is full stereo in most areas and Depth mode (one render, the GPU makes the other eye) or SteamVR's
half-rate smoothing in the heaviest.

## The measuring loop (so the player never has to launch)

Works: `tools/fake-headset.ps1 -On` (SteamVR's null driver as the PS VR2); `tools/launch-game.ps1` starts the game
through EA's Link2EA tool from a scheduled task (the only thing that reaches the EA app; never from this shell: a
process from here sits in the harness's job object and breaks the EA app's launches with "Access is denied"), presses
the launcher's PLAY by keyboard when its autoboot flags don't arrive, Enter at the title (resumes the last save),
waits for "on foot yes", `-Pause` freezes the world; `tools/measure-levers.ps1 -Key k -Values 0,1` replays the
player's 20 s head recording in a loop around `tools/perf-ab.ps1`. Keyboard input (SendKeys) only works from a
foreground shell call, not a background one.

Missing: a heavy, repeatable spot. The resume save lands at a light spot (1,400 draws, 7.3 ms); `open BIOA_STA00`
lands in the Citadel arrival cinematic (game modes 5/6/8, GPU 22 ms); the Presidium/Wards names (BIOA_STA20/30/60) are
not packages (only BIOA_STA00/PRO00/NOR00/ICE00 exist as `.pcc`, the rest stream). Best fix: a quicksave made by the
player at a heavy spot, which `-RESUME` then loads at every launch. Until then, the loop measures light-spot levers
(3 above) and the second eye's wait only where it is small.

The 16:57-17:00 round was invalid: perf-ab got "0,1" as one value (fixed in measure-levers.ps1, uncommitted until
verified). State at compaction: the game is running on the fake headset in the arrival cinematic after `open
BIOA_STA00`; `tools/launch-game.ps1 -Quit` or a kill ends it; `tools/fake-headset.ps1 -Off` gives the PS VR2 back.
