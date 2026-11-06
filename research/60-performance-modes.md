<!-- SPDX-License-Identifier: MIT -->
# 60: Performance modes: Quality, Balanced, Performance, measured (2026-11-06)

The owner (2026-11-06, in the PS VR2): "I think we should have better performance modes in our settings. I think we're
defaulting to halcyon settings". Until then the menu's Display page had a Render resolution row (the headset's eye size,
x 0.85, x 0.75, and Halcyon's ladder 2048 to 6144) and an Upscale row; the levers that decide frame time (research/22, 32,
35, 37) were file-only. This note is what each lever costs at Eden Prime's heaviest cells with today's renderer (one Draw a
frame, `sharePass = 5`), the three bundles chosen from it, and how the menu's Performance row sets them.

## Summary

- **The render size is the lever that matters**; at these cells the live levers each move the frame by less than the
  machine's noise, and only together, in the dense direction, by about 1.7 ms. With one Draw a frame (research/37) Eden
  Prime's heaviest cells are light on this machine: Balanced's worst second is 8-9.5 ms of render thread and 7-9 ms of GPU,
  and all three modes hold 90 Hz there and in a 60 s fight on the fake headset.
- **Quality** = the headset's full eye size, grass 75%, cull under 4 px. **Balanced** = today's defaults (× 0.85, FSR,
  grass 50%, cull under 8 px). **Performance** = × 0.75 with FSR, grass 35%, cull under 24 px, no ambient occlusion.
- **Dynamic shadows cost about 5 ms in the dense direction** and are in no mode.
- **The menu works live**: a mode's levers apply within about a second, the size at the next launch (the row says so), and
  any bundled setting changed by hand shows Custom with what differs. Checked in the game on the fake headset.
- The headroom is for the real headset (its compositor and full-size swapchains), heavier places and slower machines; the
  owner's check in the PS VR2 is the checklist's.

## Method

- **Fake headset** (`tools/fake-headset.ps1 -On`: SteamVR's null driver at the PS VR2's 2824 x 2880 eye, 90 Hz), the
  deployed main build (host and mod from `H:\ME VR Attempt\build`, built 2026-11-06 10:49), the owner's Eden Prime autosave
  resumed, `god` and `ghost`, the pawn put at the two heaviest cells of research/35 attempt 42: **A** (-31000, 145000, 900)
  and **B** (-33000, 147000, 400).
- **A head turning a full circle in 20 s** (`tools/make-look-recording.py`, replayed in a loop), probed for 20 s (one turn), so
  every direction counts once. The resume save's cells draw 1,500 a frame at the median direction and 3,300-3,600 at p90
  (the earlier 4,100-4,500 were a still view toward the dense side).
- **The world paused** (`tools/console.ps1 pause`) for the lever comparisons: the first pass, unpaused, had one-off hitches
  after setting changes (a 16 ms p90 on the probe right after a grass change) larger than the levers' effects. Paused, the
  render thread and the GPU depend only on the view. The game thread's numbers come from the unpaused probes.
- Each config writes every bundled key into `ASI\LE1MEVR.ini` (the mod applies `[SystemSettings]` by `scale set` within a
  second; `plugin/RenderLevers` reads `[Performance]` every second), waits, then `tools/perf-probe.ps1 -Seconds 20`, read
  with `tools/perf-probe.py`. **The wait is a whole turn (22 s) after any `[SystemSettings]` change**, 6 s otherwise: the mod
  sends every boolean through its opposite first (gotchas/motion-blur-in-stereo), so new shader combinations are built
  cold in the seconds after a change, and a probe started 6 s after one read the 35% grass as 1.9 ms *heavier* than
  Balanced in the dense direction. Balanced is measured at the start, the middle and the end of each pass as the control.
  The render resolution needs a restart: one launch per size.
- **The sizes measured are absolute**: 2118 x 2160, 2400 x 2448 and 2824 x 2880, that is 0.75, 0.85 and 1.0 of the PS VR2's
  native eye (research/22: SteamVR's 100%). The menu's chips are relative to what the runtime recommends, and SteamVR scales
  its recommendation by its resolution setting: this machine's recommends 2344 x 2392 today (the owner's own target, so it
  reads as the Headset chip), where a mode's × 0.85 is 1992 x 2034. A mode on a lower SteamVR setting renders that much less
  than the size measured here.
- **What the numbers mean.** On the null driver the frame runs free: the frame interval's median sits on a ~7.3 ms floor
  (the desktop mirror's present), so a preset's headroom is read from the **render thread's busy time** ("busy, without the
  XR waits") and the **GPU's span** for the whole frame, each against the 11.1 ms of 90 Hz, and from the share of frames
  longer than 11.1 ms. The real headset adds its compositor and eye swapchains at the full eye size.
- **The worst second** of each probe (`perf-probe.py --per-second`, the second with the most render-thread work) is the turn's
  dense direction: that is what a mode has to hold under 11.1 ms. The medians are mostly the light half of the turn.
- **The machine was not quiet:** Windows Defender (about half a core, scanning the probes' CSVs) and another agent's
  processes (`codex`, about 0.7 of a core) ran throughout. Paused, the game thread does the same work every frame, so its
  busy time is a gauge of the contention: it went from 1.26 to 1.55 ms during one stretch of the single-lever pass, and the
  render thread rose with it. Single levers within about ±0.4 ms (median) and ±0.9 ms (p90) of Balanced are noise here;
  the bundles were measured three rounds interleaved (Balanced, Performance, Quality, repeated) for that reason.

## Balanced's resolution (headset × 0.85, 2400 x 2448), the live levers

### One lever at a time (cell A, paused, warmed up; Balanced is the mean of three controls)

| change from Balanced | render ms, median (p90) | GPU ms, median (p90) | worst second: render, GPU | draws, median (p90) |
|---|---|---|---|---|
| Balanced (8 px, grass 50%, AO on, shadows off) | 4.44 (8.58) | 3.17 (8.23) | 9.24, 8.81 | 1,680 (3,763) |
| cull under 16 px | 4.38 (7.93) | 3.14 (7.12) | 8.21, 7.60 | 1,656 (3,677) |
| cull under 32 px | 4.73 (8.19) | 3.27 (7.95) | 9.35, 8.91 | 1,581 (3,382) |
| cull under 4 px | 5.92 (9.85) | 3.41 (9.32) | 9.87, 9.47 | 1,687 (3,809) |
| grass 35% | 4.31 (7.95) | 3.01 (7.04) | 8.49, 8.14 | 1,693 (3,764) |
| grass 25% | 4.02 (7.75) | 3.00 (6.82) | 8.18, 7.73 | 1,683 (3,763) |
| grass 75% | 5.33 (9.90) | 3.07 (9.49) | 10.57, 10.13 | 1,646 (3,756) |
| ambient occlusion off | 5.10 (9.06) | 2.91 (8.62) | 9.33, 9.03 | 1,674 (3,763) |
| light-environment (character) shadows off | 4.75 (8.37) | 3.36 (8.03) | 8.68, 8.32 | 1,684 (3,777) |
| dynamic shadows on | 5.36 (9.85) | 3.10 (9.18) | 14.35, 13.45 | 1,798 (3,895) |

The 4 px, 75% and shadows rows fell in the stretch where the paused game thread read 1.4-1.55 ms (the machine busy), so
their medians are high by about the same 0.9-1.5 ms as the stretch's own Balanced would have been; their worst seconds are
the comparison that holds: **dynamic shadows on cost about 5 ms in the dense direction** (130 more draws a frame; whether
both eyes show them right under one Draw is research/37's open [verify], not checked here), the others within 1.3 ms. No
mode turns them on. Grass shorter than half and
culling at 16 px each take about 1 ms off the worst second; culling at 32 px removes 300 draws a frame but nothing measurable
(the draws it removes are cheap; research/35 attempt 45 found the same: the mass is mid-sized and large objects). Ambient
occlusion off is the GPU's 0.26 ms at the median, as expected of a screen-space pass at this size.

### The bundles, three rounds interleaved (paused, warmed up)

| bundle's live levers | cell | render ms (p90) | GPU ms (p90) | worst second: render, GPU | draws (p90) |
|---|---|---|---|---|---|
| Balanced: 8 px, grass 50% | A | 4.19 (8.29) | 3.15 (7.97) | 9.42, 8.83 | 1,544 (3,590) |
| Performance: 24 px, grass 35%, AO off | A | 4.19 (7.50) | 2.99 (6.52) | 7.72, 6.92 | 1,496 (3,385) |
| Quality: 4 px, grass 75% | A | 4.41 (8.39) | 3.19 (7.96) | 8.80, 8.44 | 1,577 (3,684) |
| Balanced | B | 5.28 (7.56) | 3.70 (6.25) | 8.01, 6.67 | 1,626 (3,383) |
| Performance | B | 4.97 (7.34) | 3.43 (5.05) | 7.85, 5.88 | 1,597 (3,232) |
| Quality | B | 5.19 (7.90) | 3.72 (7.37) | 8.69, 8.31 | 1,632 (3,422) |

At the same render size, Performance's live levers take **1.7 ms of render thread and 1.9 ms of GPU off cell A's worst
second** and 1.2-1.5 ms off the GPU's p90 at both cells; Quality's cost nothing measurable at A and 0.7 / 1.6 ms at B's worst
second. With one Draw a frame these cells are light at this size: the worst second is 8-9.5 ms of render thread, inside the
11.1 ms of 90 Hz, so the levers are headroom for heavier places and the real headset's extra GPU work, not a rescue here.

## Performance's resolution (headset × 0.75, 2118 x 2160, FSR)

Paused, warmed up, two rounds interleaved:

| live levers | cell | render ms (p90) | GPU ms (p90) | worst second: render, GPU | draws (p90) |
|---|---|---|---|---|---|
| Balanced's | A | 3.75 (7.17) | 2.66 (4.08) | 7.28, 4.96 | 1,509 (3,441) |
| Performance's | A | 3.59 (6.82) | 2.51 (3.40) | 7.11, 3.33 | 1,454 (3,206) |
| Balanced's | B | 4.50 (7.10) | 3.01 (4.22) | 7.83, 6.52 | 1,554 (3,328) |
| Performance's | B | 4.52 (6.94) | 2.82 (3.46) | 7.18, 3.36 | 1,535 (3,170) |

The smaller target takes the GPU's p90 from 6.3-8.0 ms (Balanced at 0.85) to about 4 and its worst second from 6.7-8.8 to
5.0-6.5: 22% fewer pixels, but the dense direction's GPU time falls by more than that (the fill there is what the GPU was
short of). The render thread falls 0.4-0.8 ms at the median [inference: the waits on the GPU inside the frame shorten with it]. Performance's
live levers on top halve the GPU's worst second again (3.3 ms) and keep the render thread's at 7.1-7.2.

## Quality's resolution (the headset's full eye size, 2824 x 2880)

Paused, warmed up, two rounds interleaved:

| live levers | cell | render ms (p90) | GPU ms (p90) | worst second: render, GPU | draws (p90) |
|---|---|---|---|---|---|
| Balanced's | A | 4.09 (7.62) | 4.57 (7.48) | 7.94, 7.80 | 1,496 (3,424) |
| Quality's | A | 4.17 (7.87) | 4.57 (7.81) | 8.21, 8.27 | 1,522 (3,476) |
| Balanced's | B | 5.31 (7.36) | 4.88 (6.34) | 14.85, 9.30 (one hitch in one round) | 1,533 (3,278) |
| Quality's | B | 5.40 (7.58) | 4.90 (6.75) | 8.03, 7.83 | 1,573 (3,306) |

The full size costs the GPU about 1.3 ms at the median against 0.85 (4.6-4.9 against 3.2-3.7 ms); Quality's live levers
(4 px, grass 75%) add 0.1-0.5 ms. **The GPU's p90 and worst second do not rise with the pixels the way the medians do**
(0.75: 3.3-6.5, 0.85: 6.3-8.8, 1.0: 6.3-8.3 ms): at these loads the RTX 4090 runs well below its limit and its clocks float
with the load, so a lighter frame can take nearly as long at a lower clock. The medians order cleanly; read the tails as
"comfortably under 11.1 ms" rather than as differences. [inference: the clock behaviour, from the shape; the probe doesn't
read clocks]

## Under fire (the owner's suggestion: a long span with enemies shooting, Shepard invulnerable and not firing)

The resume save's own spot is a fight: at load about nine geth troopers and assault drones are around Shepard at (-36580,
150836, -350) and more arrive (eleven at the end). Each launch: the mode's levers in the files before the game starts, the
save resumed, `god` (Shepard only; the squad fights as it likes), the head turning a full circle every 20 s, nobody at the
controls, one 60 s probe starting 46-48 s after the game reaches gameplay. Every mode meets the same fight from the same
start. Unpaused, so the game thread is real.

| mode (its size, its levers) | render ms, median (p90) | GPU ms (p90) | game thread ms (p90) | draws (p90) | frames over 11.1 ms |
|---|---|---|---|---|---|
| Performance (2118 x 2160) | 4.54 (6.26) | 2.24 (2.62) | 4.31 (5.07) | 1,521 (2,437) | 0.0% |
| Balanced (2400 x 2448) | 5.34 (7.63) | 2.97 (5.88) | 4.79 (6.27) | 1,573 (2,561) | 1.0% |
| Quality (2824 x 2880) | 4.77 (6.56) | 3.50 (4.02) | 4.11 (4.75) | 1,529 (2,542) | 0.0% |

All three hold 90 Hz in this fight with room to spare. The GPU orders by size at the median (2.2, 3.0, 3.5 ms). The
Balanced run's render and game threads are the highest of the three, but its game thread is 0.5-0.7 ms above the others,
and nothing in a mode changes the game thread's work, so that run caught a busier machine (the other agent's processes);
the ordering between the modes on the CPU side is inside the machine's noise. The fight costs the game thread 4.1-4.8 ms
(the AI, the weapons' effects), against 1.25 ms paused at the empty cells: it is the game thread, not the render thread,
that a fight loads.

## The modes chosen

| mode | render size | FSR | small-object cull | grass | ambient occlusion | dynamic shadows |
|---|---|---|---|---|---|---|
| Quality | the headset's full size | on (idle at full size) | under 4 px | 75% | on | off |
| **Balanced** (the shipped defaults) | × 0.85 | on | under 8 px | 50% | on | off |
| Performance | × 0.75 | on | under 24 px | 35% | off | off |

- **Balanced is today's defaults** exactly (the owner's tuned settings, CLAUDE.md "The owner's dev settings are the shipped
  defaults"): a fresh install's files read as Balanced (`tests/PerformanceModesTests.cpp` checks it).
- **Performance** buys its headroom on the GPU (the size: the GPU's p90 at the cells from 6.3-8.0 to about 3.4 ms with its
  levers) and in the dense direction (the cull and the shorter grass: about 1.7 ms of the worst second on both threads). Its
  price is a softer picture (FSR from 0.75), grass ending closer (35% is short of research/35's "too close" 25%), far details
  culled under 24 px (a 48 px-wide object on a 2160-high eye), and flatter corners without ambient occlusion.
- **Quality** spends on pixels: the full eye size costs the GPU about 1.3 ms at the median against Balanced; its levers (more
  grass, a 4 px cull) cost 0.1-0.5 ms more. It stays inside 11.1 ms at every probe here.
- **Not in any mode:** dynamic shadows (about 5 ms in the dense direction, and both eyes' correctness under one Draw is
  research/37's open [verify]); motion blur (never: gotchas/motion-blur-in-stereo); `eyeScissor` (1% of the GPU at the
  heavy cell, research/37); `oneDrawWindow = 0` (a missed object in one eye's wedge, research/37); light-environment shadows
  (no measurable cost); 32 px culling (no gain over 24 here).
- What a mode writes: `[Upscale] method`, `[Performance] minScreenRadiusPx`, `[SystemSettings] FoliageDrawRadiusMultiplier`,
  `DynamicShadows`, `AmbientOcclusion`, then the render resolution as the Render resolution row sets it (Halcyon's profile and
  GamerSettings.ini, applied at the next launch). FSR sharpness is no mode's (a matter of taste).

## The menu in the game (fake headset, 14:40-14:45)

The row driven by its own pointer (`tools/make-menu-recording.py`: a recording whose right aim ray goes to document points
and pulls the trigger; the menu opened with its key, since the both-sticks hold counts only live controllers):

- Picking Performance wrote the five keys and the size at once (`main menu: performance mode performance: render size
  Headset × 0.75, small objects culled under 24 px, grass at 35%, ambient occlusion off (against Balanced)`); `RenderLevers`
  took the cull 0.6 s later and the mod's `scale set` the grass and the occlusion 1.1 s later. The row then showed
  Performance, its description and "Live, except the render resolution: it applies after a restart" (the target was no
  longer the size rendering).
- Balanced, Quality and Balanced again, 5 s apart: each written and applied within 0.75 s.
- `[Performance] minScreenRadiusPx = 16` written into the file (as Advanced's row or a hand edit would): within 3 s the row
  showed Custom, "Your own mix: Balanced with small objects culled under 16 px".
- Two lessons for scripted menu tests: RmlUi's smooth scrolling keeps the page moving for a while after the stick, so a
  click right after a scroll lands on whatever moved under the pointer (wait a few seconds after scrolling); and a click on a
  chip that is already selected writes nothing (the model skips an unchanged choice).
- A side effect found by the Codex review session: before main's 303238d, Halcyon's global low-level input hooks swallowed
  the desktop's keyboard and mouse while our menu was open, whatever window was in front; the owner's desktop was dead from
  14:40:49 to 14:46 during this test. Fixed on main; merge it before scripting the menu again.

## What the headset has to say

The real PS VR2 adds the compositor and eye swapchains at the full eye size, and the owner's sessions have been GPU-bound at
p90 (research/37's fpsVR 13.5 ms). Whether Balanced holds 90 Hz in the owner's heaviest places, how soft Performance looks,
and whether its 24 px cull and 35% grass pop in noticeably are the checklist's (notes/headset-checklist.md section 6).
