# 50: Crowds on the Citadel, and what they cost in VR (2026-11-03)

The question (user, 2026-11-03): crowd-heavy places are said to be hard on the CPU in VR. How do LE1's most crowded places
on the Citadel measure on our VR path?

**Answer: they're cheap.** The Citadel's crowds are small (at most 23 characters within 10 m and 44 within 25 m of where
Shepard stood). At every crowded spot each thread used under 40% of the 11.1 ms frame at 90 Hz. The characters' own share
is about 1.0-1.2 ms of render thread, 0.3-0.4 ms of game thread and 0.2-0.4 ms of GPU. Their AI costs nothing
measurable. The Citadel's heaviest place is the Presidium, which has few people but 3,200 draws a frame (4,200 at p90).
Its p90 frame time is 9.75 ms, 88% of the budget, so it is scenery, not crowds.

## Setup

- 2026-11-03, 18:55-19:06. Host and mod from main b740e38 plus this note's commands. The fake headset (SteamVR's null
  driver, research/35 "Setup for measuring without the headset"), Stereo 2 at 2344x2392 per eye. `[Performance] sharePass
  = 5` (one Draw a frame, the second eye synthesised, research/37), `keepBoundShaderStates = 1`. The game's VR settings:
  dynamic shadows and motion blur off, foliage radius 0.5 (`game/SystemSettings`).
- The place: the user's John career, **Save 96, "Citadel: Security"** (C-Sec, 14 h played; `John00_96.pcsav`), past the
  first Citadel visit, so its plot has none of the arrival cutscenes. A fresh Shepard's `open BIOA_STA00` lands in the
  arrival cinematic and the Council conversation (notes/cpu-frame-time-plan.md). The save folder was backed up before
  and compared after: every file unchanged.
- The view: `tools/make-look-recording.py` (new): standing still, the head turning a full circle in 20 s, both hands
  tracked and hanging at the sides. Replayed in a loop under a 20 s probe, every direction is in view for the same share
  of the probe, whichever way the pawn faces. Without it the fake headset looks one fixed way, and the recorded heads
  from earlier sessions barely turn.
- Each number is the probe's (`tools/perf-probe.py`): per-frame work, median and p90. "CPU" is fpsVR's CPU frame time
  (poses to submit, wall). "Game" is the game thread's busy time (engine and our tick). "Render" is the render thread's
  work without the XR waits. "GPU" is the frame's whole GPU span. The null driver runs free at about 139 frames a second,
  so compare work, not frame intervals.

## How the Citadel streams

`BIOA_STA00` is the persistent level of the whole station, with 239 streaming levels. The ones measured here are
`STA20_*` (the Presidium), `STA30_*` (C-Sec and the docks), `STA60_*` (the Wards: the market, Flux, Chora's Den) and
`STA70_*` (the Tower). What is shown is decided by 154 `SFXGame.BioTriggerStream` volumes. Each has a list of streaming
states (`StateName`, `InChunkName`, `VisibleChunkNames`, `LoadChunkNames`), and touching one applies its state.

- `mevr stream list [filter]` (new, `game/ConsoleRequest`) prints every volume: its index, name, place and states.
- `mevr stream touch <index|name>` calls the volume's own `Touch` event with the player's pawn, so the game streams that
  area in exactly as when the player walks in. It took about 8 s for a Wards area (checked with `mevr vehicle levels`).
  Then move the pawn there.
- Moving the pawn into an area whose floor isn't loaded drops it. One move near Chora's Den fell to z -148,727 before
  the area streamed in. `tools/spot-survey.ps1` puts the player in ghost mode, which hangs it in place.
- `mevr crowd [cellUU]` (new) counts every live, visible Pawn but the player's. It bins them in cells (default 15 m),
  with each cell's centroid (a place among the characters, usually open floor) and how many rendered in the last second.
  It also prints how many stand within 10 and 25 m of the player. The totals include characters in loaded but hidden
  chunks; the near counts and the rendered counts don't depend on that.

## The spots

| Spot | Place (X Y Z) | Volume touched |
|---|---|---|
| Wards market (Upper Wards, outside Flux) | -11300 5000 -30650 | BioTriggerStream_64 (area 07) |
| Flux (the club, its casino) | -11000 1500 -30060 | BioTriggerStream_77 (area 09) |
| Chora's Den (the club, its dancers) | -7500 -6900 -31220 | BioTriggerStream_49 (area 05) |
| Presidium | -2464 1088 300 | BioTriggerStream_92 (Presidium 02) |
| C-Sec (the save's own spot) | -2010 10009 -22180 | BioTriggerStream_60 |

Volume names, unlike `list` indices, are stable across sessions. Snapshots: `tools/snapshot.ps1` at each spot,
19:05-19:06 (local, not in git).

## Results

`tools/spot-survey.ps1 -Seconds 20 -Recording synthetic-look-around-20s.mevrrec`, about 2,780 frames a spot (2,460 at
the Presidium). Median / p90, ms:

| Spot | Characters within 10 / 25 m | CPU | Game | Render | GPU | Draws |
|---|---|---|---|---|---|---|
| Wards market | 12 / 31 | 3.10 / 4.09 | 2.39 / 2.85 | 2.78 / 3.65 | 2.23 / 2.67 | 656 / 1,287 |
| Flux | 18 / 44 | 4.58 / 5.71 | 3.47 / 4.07 | 4.21 / 5.22 | 2.74 / 3.19 | 1,087 / 1,328 |
| Chora's Den | 23 / 36 | 3.34 / 4.23 | 3.80 / 4.45 | 3.04 / 3.80 | 2.11 / 2.63 | 726 / 859 |
| Presidium | 4 / 11 | 7.80 / 9.75 | 4.22 / 4.78 | 7.36 / 9.26 | 5.31 / 9.00 | 3,201 / 4,234 |
| C-Sec | 9 / 18 | 5.15 / 6.37 | 2.98 / 3.50 | 4.73 / 5.89 | 2.29 / 2.77 | 2,168 / 2,819 |

For comparison, Eden Prime's heaviest cell has 4,100 draws and a 9.1 ms render thread in the same one-Draw mode
(research/37).

## What the characters cost

An A/B at one spot under the same replay: each condition's console lines, 3 s to settle, then a 20 s probe. The
unchanged condition ("A") was repeated between the others to check for drift.

- `show skeletalmeshes` hides every skinned mesh: the characters, their heads and hair, and Shepard's own body. In UE3 a
  skeletal mesh that isn't rendered also skips its animation update, so this condition removes the characters' render
  and animation costs together.
- `DisableAi` / `EnableAi` (`BioCheatManager`) turn the AI off and on.
- `pause` freezes the world but keeps rendering. Nothing ticks.

**Chora's Den** (23 characters within 10 m), median / p90, ms:

| Condition | CPU | Game | Render | GPU | Draws |
|---|---|---|---|---|---|
| A: as played | 3.32 / 4.11 | 3.82 / 4.49 | 3.04 / 3.73 | 2.12 / 2.60 | 726 / 850 |
| B: skeletal meshes hidden | 2.27 / 2.82 | 3.39 / 3.97 | 2.02 / 2.59 | 1.89 / 2.26 | 471 / 670 |
| A again | 3.34 / 4.06 | 3.80 / 4.46 | 3.04 / 3.63 | 2.14 / 2.62 | 728 / 847 |
| C: AI off | 3.31 / 3.95 | 3.81 / 4.49 | 3.04 / 3.60 | 2.13 / 2.61 | 725 / 846 |
| A again | 3.33 / 4.10 | 3.80 / 4.51 | 3.04 / 3.67 | 2.14 / 2.62 | 729 / 848 |

**Flux** (18 within 10 m, 44 within 25 m), median / p90, ms:

| Condition | CPU | Game | Render | GPU | Draws |
|---|---|---|---|---|---|
| A: as played | 4.53 / 5.76 | 3.46 / 4.04 | 4.14 / 5.27 | 2.74 / 3.19 | 1,046 / 1,305 |
| B: skeletal meshes hidden | 3.34 / 4.92 | 3.17 / 3.82 | 3.03 / 4.53 | 2.38 / 2.88 | 762 / 1,091 |
| A again | 4.62 / 5.84 | 3.51 / 4.10 | 4.26 / 5.34 | 2.74 / 3.21 | 1,066 / 1,328 |
| C: AI off | 4.63 / 5.70 | 3.43 / 3.97 | 4.23 / 5.20 | 2.75 / 3.20 | 1,096 / 1,323 |
| A again | 4.64 / 5.74 | 3.47 / 4.00 | 4.27 / 5.32 | 2.74 / 3.20 | 1,098 / 1,327 |
| D: world paused | 2.98 / 4.49 | 0.96 / 1.17 | 2.69 / 4.08 | 2.46 / 2.89 | 741 / 1,032 |
| A again | 4.71 / 5.81 | 3.45 / 4.00 | 4.33 / 5.34 | 2.75 / 3.21 | 1,103 / 1,334 |

What these say:

- The skinned meshes cost 1.0 ms of render thread at Chora's Den and 1.15 ms at Flux, about a third and a quarter of it.
  That is 255-300 draws, 0.3-0.4 ms of game thread and 0.2-0.4 ms of GPU.
- The AI costs nothing measurable. The Citadel's characters are ambient: they stand, sit, dance and talk.
- Everything that ticks in the world costs about 2.5 ms of the game thread at Flux: 3.46 ms against 0.96 ms paused.
  Characters, emitters, physics and script all count here. The skinned meshes' animation is 0.3 ms of that.
- UE3 updates animation once a frame on the game thread whatever the number of eyes. The per-eye cost is the draws, and
  with `sharePass = 5` the second eye is a replay of the first's draws. With two full engine renders (`sharePass = 0`)
  the characters' render-thread share would roughly double. [verify]

## Combat: the final fight with Saren (2026-11-03, 21:16-21:24)

The user then asked for one with combat. The saves on this machine with a fight: VR00's Save 7 (Eden Prime's spaceport)
and John's autosave (Citadel: Tower). The spaceport save is the bomb defusal: a 2:22 timer and the geth 55 m away, too
short for a set of 20 s probes. The tower autosave is the final fight with Saren in the Council chamber's garden
(`BIOA_END00`): Saren on his hover platform, the two squadmates, Shepard (`god`). Only four characters: a small fight, not
a crowd. Measured as above (the look-around replay, 20 s probes, the unchanged condition between the others), median /
p90, ms:

| Condition | CPU | Game | Render | GPU | Draws |
|---|---|---|---|---|---|
| A: as played | 5.28 / 7.24 | 4.75 / 6.24 | 4.95 / 6.87 | 2.77 / 3.60 | 1,636 |
| B: skeletal meshes hidden | 4.85 / 6.85 | 4.90 / 6.09 | 4.50 / 6.47 | 2.45 / 2.91 | 1,435 |
| A again | 5.26 / 7.62 | 4.86 / 6.53 | 4.88 / 7.10 | 2.75 / 3.81 | 1,600 |
| C: AI off | 5.00 / 7.34 | 4.72 / 6.40 | 4.71 / 6.85 | 2.57 / 3.26 | 1,584 |
| A again | 6.01 / 8.19 | 5.55 / 7.28 | 5.48 / 7.26 | 2.64 / 4.87 | 1,532 |
| D: world paused | 4.73 / 6.73 | 1.12 / 1.41 | 4.45 / 6.43 | 2.52 / 2.99 | 1,506 |
| A again | 5.01 / 7.09 | 4.80 / 6.11 | 4.73 / 6.64 | 2.66 / 3.24 | 1,611 |

- Well within the 11.1 ms frame: the worst p90 of any thread was 7.3 ms (the game thread in one A run).
- **The fight's ticking costs more than the Citadel's crowds:** 3.6-4.4 ms of game thread against 1.1 paused, with four
  characters, where Flux's 44 cost 2.5. Combat's projectiles, effects, physics and the boss's own script, not the number of
  characters. The A runs spread 4.75-5.55 ms as the fight's phases change.
- The four characters' skinned meshes cost about 0.4 ms of render thread and 0.3 ms of GPU; the AI's thinking nothing
  measurable (`DisableAi`, 0.1 ms, inside the A runs' spread).
- In one snapshot during the probes the view was smeared with several copies of the lights after a red flash at Shepard:
  most likely the game's own hit effect (`god` stops the damage, not the effect). Not looked into.
- **Not covered:** a big fight (a geth assault with many troopers at once). None of the saves here has one.

## What this doesn't cover

- **Big fights:** many characters running AI, weapons and powers at once (a geth assault). Only a small fight was measured
  (below: Saren and the squad).
- **LE2/LE3:** they have denser hubs (Omega, Illium, the Citadel's Commons and Purgatory). Our renderer there is our own,
  not Halcyon's.
- **The fake headset:** no compositor cost and no real head motion. The real headset adds both.

## Reaching a place by the game's own menus (for the measuring loop)

- `tools/launch-game.ps1 -Steam` (new) asks the running Steam for the game with `steam.exe -applaunch 1328670 ...`, which
  is what its Play does. On 2026-11-03 the EA app was in a job object again (gotchas/ea-launcher-in-a-job-object). The
  Link2EA route failed with ErrorCreatingProcess, and so did EASteamLauncher started from a scheduled task (Task
  Scheduler puts its tasks in a job too). steam.exe is in no job, so `-applaunch` brought the game up in 6 s, with the
  `-game 1 -autoterminate` parameters arriving.
- LE1's menus ignore keys posted as window messages, except Enter. They take real input (`keybd_event`, and
  `SetCursorPos` with `mouse_event` for clicks) once the game window is in front. The Mission Computer's wheel takes the
  mouse, not the arrow keys. The window is 2344x2392 at 0,0 on a 1440-high monitor, so the cursor can't reach rows below
  y 1440: use the keys there.
- The path to a save of another career: Load Career, then Switch Careers (a click), then All Careers (Enter), then the
  save (Down), then Enter. `tools/snapshot.ps1`'s `-quad0.png` shows each menu as the flat quad sees it.

## Reproduce

```
tools/fake-headset.ps1 -On
tools/launch-game.ps1 -Steam            # then the menus above, to John's Save 96
python tools/make-look-recording.py %LOCALAPPDATA%\MEVR\recordings\synthetic-look-around-20s.mevrrec 20
tools/spot-survey.ps1 -Seconds 20 -Recording <that file> -Spots "-11300 5000 -30650 BioTriggerStream_64", ...
tools/launch-game.ps1 -Quit; tools/fake-headset.ps1 -Off
```
