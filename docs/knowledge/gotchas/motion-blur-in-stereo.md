---
type: Gotcha
title: Motion blur smears each eye its own way, and keeps coming back
description: UE3's motion blur takes its velocity from the other eye's view under Halcyon's stereo, so surfaces smear per eye; three fixes since 2026-10-07 because the game overrides, rewrites and re-arms the setting.
tags: [le1, rendering, stereo, system-settings, recurring]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-07, 2026-10-23, 2026-10-28, 2026-11-02, 2026-11-06]
---

# Symptom

The player described it differently every time, which is why it was not recognised at once:

| date | the report | what the frames showed |
|---|---|---|
| 2026-10-07 | "vertical judder" on head turns | every head turn smeared: the blur reads head rotation as camera motion |
| 2026-10-23 | "shimmering on the floor every few seconds" | 4 of 8 snapshots smeared in a wavy diagonal blur, the rest sharp, alternating every 3-6 s; paused, the LEFT eye only blurred in 4 of 5 frames |
| 2026-10-28 | "the assets shift up and down and in different directions, each its own way, in both eyes, no CPU frame-time spike" | a large surface flips between its speckled look and a smooth look streaked along one diagonal, in runs of 1-4 frames |
| 2026-11-02 | (no player: the test loop) "the cockpit's near surfaces doubled sideways, the scene outside sharp" | after a fresh launch from Steam, two snapshots 20 s apart both smeared the cockpit parts at arm's length; the log had `(a new level: applied again)` 16 s before; `scale set MotionBlur True` then `False` by hand made the next snapshot sharp. A region's mean grey was the same smeared and sharp (within 1.5), so brightness measurements survive it, edges don't |

Frame times do not change. Eye snapshots (`tools/snapshot.ps1 -Count 8`) show the smear directly.

# Cause

Halcyon's Stereo 2 renders the two eyes as two consecutive renders on ONE view state. UE3's motion blur gets its
velocity from the previous frame's view matrix, and here the "previous frame" is the other eye, 6.3 cm away, so near
things (the floor, the hands, a wall at arm's length) smear in one eye and not the other, and every surface smears
along its own screen velocity. The head's sway moves that velocity across the blur's threshold, which is why it comes
and goes in runs of frames.

The blur is meant to be off, and three mechanisms turned it back on (details in
[BIOEngine.ini's SystemSettings precedence](system-settings-precedence.md)):

1. **2026-10-07**: `tools/vr-game-settings.ps1` wrote `MotionBlur=False` into GamerSettings.ini. BIOEngine.ini's
   `[SystemSettings]` wins over GamerSettings' (measured 2026-10-23).
2. **2026-10-23**: the mod (`game/SystemSettings`) ran `scale set MotionBlur False` once a local player exists. The
   title's menu world got it; the save that loaded afterwards rendered with the blur anyway.
3. **2026-10-28**: a second `scale set MotionBlur False` in the loaded level changed nothing, while `True` then
   `False` turned the blur off.

# Check

1. Was the setting re-applied in THIS level? The log after the level loaded:
   `system settings: scale set DynamicShadows False, MotionBlur False, ... (booleans through their opposite first) (a new level: applied again)`.
2. Toggle it by hand and look again:

   ```
   tools/console.ps1 "scale set MotionBlur True" "scale set MotionBlur False"
   ```

   If the smear stops, the apply did not reach this level. If it does not, it is something else.
3. Snapshots: smeared frames among sharp ones, one eye more than the other. Measure it on the part of the scene both eyes
   see, aligned: `tools/snapshot.ps1 -Count 8 -Interval 0.5`, then `python tools/eye-sharpness.py <snapshots> <stamps>`
   (each frame's L/R on the shared region; the blur's signature is frames far from 1.00), **only with two engine renders**
   (`sharePass` 0-3): under one-Draw stereo both eyes smear alike and it reads 1.00 (below). Don't compare the eyes' whole
   images: with the PS VR2's offset eye views their middles hold different content.

**2026-11-06, a fifth time, after cinematics** (the owner in the PS VR2 on Eden Prime: "it looks like the motion blur may be
enabled again", "what looks to be the assets flickering", in gameplay; "this is the exact same behavior"): the level's
five applies were in the log (10:06:12-10:06:38), a manual True/False at 10:12:58 helped, then a cutscene (10:13:10) and a
conversation (10:16:11) played and the flicker was back by 10:18 with no level change. The game window's frames
(`tools/flicker-grab.py`, PrintWindow) showed the near railing streaked diagonally and doubled, the far scene sharp. So a
cinematic re-arms it [verify which: the conversation camera's post-process chain, or the game restoring its own setting
after a scene]. Fix: `game/SystemSettings` applies again on every return to gameplay (modes 0-4) from any other mode, log
`(back to gameplay after mode N: applied again)`.

**The two-eye check is blind under one-Draw stereo.** With `sharePass = 5` (the default since 2026-11-05) the second eye is
a replay of the first eye's render, blur pass included, so both eyes smear alike: `tools/eye-sharpness.py` read L/R
0.99-1.00 in all 12 frames while the owner saw the flicker. That check only works with two engine renders (`sharePass` 0-3).
Trust the player's report, check the log for the applies since the last cinematic, and count the flips over time
(`tools/flicker-grab.py`, or research/35 attempt 49's mirror grab).

Ruled out on 2026-10-28, by the same replayed pan at the same phase, before the blur was found: our D3D11 hooks (none
installed, same flicker), Halcyon's FOV match, FSR, kept shader states, mesh LOD, texture streaming, dynamic
resolution. Do not start there.

# Fix in place

`game/SystemSettings` (the mod, hot-reloaded): once a local player exists, and again whenever `engine::World()`
changes, each boolean goes through its opposite first (`scale set MotionBlur True`, then `False`; the same for
`DynamicShadows`), then `FoliageDrawRadiusMultiplier 0.5`. `[SystemSettings]` in `ASI\LE1MEVR.ini` overrides any key;
`apply = 0` turns it off. Measured clean on 2026-10-28 in the running session: look flips over a 4 s pan 40-57 before,
1-3 after (the 1-3 are the pan's one content change). **From a fresh launch it did not hold** (2026-11-02): the log
said "a new level: applied again" at the save's load and the blur was on; the same two lines by hand turned it off.
Since 2026-11-04 (b3778d8) each level also gets them 3 s and 15 s after it becomes the world, by when it renders.
**Not proven to be the fix:** on 2026-11-04 two cold Steam launches into the same Mako-cockpit autosave on the fake
headset were both sharp, one with the new applies and one without them (a control build), so the 2026-11-02 failure
didn't reproduce. Two traps found on the way: `scale set MotionBlur True` from the console does **not** bring the blur
back here (it only turns it off), so it is no positive control; and a still head or a full head circle shows no
measurable difference in snapshots or in the mirror's frame-to-frame sharpness (research/35 attempt 49's method needs
the real head sway of a headset recording, and even then the forced "on" read like off). Until it reproduces, after a
launch run the two `scale set` lines yourself before judging a picture.

If it comes back a fourth time, measure before reasoning: a replayed head recording, the desktop mirror grabbed for 4 s,
the frame's high-frequency energy thresholded and its transitions counted (research/35 attempt 49 describes the setup).

# Citations

[1] [research/35, attempts 31 and 33 (2026-10-23)](../../../research/35-performance-attempt-log.md): the shimmer, the one-view-state cause, the mod's apply.
[2] [research/35, attempt 49 (2026-10-27 to 28)](../../../research/35-performance-attempt-log.md): the shifting assets, what was ruled out, the fix.
[3] [notes/milestones.md, M3.2 (2026-10-07)](../../../notes/milestones.md): motion blur among the judder's suspects; the settings tool.
[4] [tools/vr-game-settings.ps1](../../../tools/vr-game-settings.ps1): its header line on MotionBlur.
[5] [src/game/SystemSettings.cpp](../../../src/game/SystemSettings.cpp): the apply.
