<!-- SPDX-License-Identifier: MIT -->
# ME2's picture in its dark scenes, ME1's way (2026-11-14)

**Since 2026-11-16 (lane EY, research/27 section 9):** the eyes of all three games go through one pipeline (the same
colour, tonemap and FSR code and keys as ME1's); the measurements below were repeated through it at the same place
and agree within the noise floor (research/27 9.6, with ME3's Normandy). The recommendation in section 5 stands.

**The owner (2026-11-14):** "We can take the HDR lane out of play. I'm just trying to figure out how we can get the best
picture, when we know that me2 has a lot of dark scenes." So the HDR work stopped (research/le2-hdr.md stays as the study;
nothing of its Steps 1-2 landed), both games answer the game's HDR question SDR by default
([gotchas/windows-hdr-desktop](../docs/knowledge/gotchas/windows-hdr-desktop.md)), and ME1's colour pipeline
([research/27](27-colour.md)) comes to ME2.

**[verify]** marks what a headset still has to settle; every number here is the eye image as the headset gets it
(the test loop's snapshot of the swapchain image after our copy), at one still pose on the fake headset.

## 1. What ME2 had, and what it has now

ME2's eyes were Halcyon ME2's `CopyResource` of the game's 8-bit frame into the eye images (or `plugin/EyeCopy`'s draw of
the float frame, research/le2-renderer-integration.md "The black eyes"): no correction, no dither, no per-headset profile.

**Now (this note's commit):** ME1's pipeline runs in that copy.
- `plugin/Colour` (the settings, the headset, the profiles, the constants) compiles into both hosts; its LE1-only parts
  (Halcyon LE1's eye-blit seams, the classic menu's section) are under `MEVR_WITH_HALCYON`. The settings are ME1's keys in
  ME2's file: `[Colour]` and `[Colour.<headset>]` in `ASI\LE2MEVR.ini` (`profile = auto | neutral | quest | vivid | custom`,
  `brightness`, `contrast`, `saturation`, `gamma`, `black`, `gainR/G/B`, `dither`, `compare`), re-read within a second of a
  change. No new keys.
- The headset comes from ME2's session as on LE1 (`MevrBridge::OnSessionCreated` -> `colour::SetHeadset`): Auto is
  `quest` for a streamed Quest, `neutral` otherwise (core/vr/Colour `Detect`). Log `colour: headset: ...`,
  `[MEVR-COLOUR]` in `MELE2VR_Log.txt`.
- `plugin/EyeCopy::Copy` (called for every copy into an eye and the flat panel through `MevrBridge::CopyIntoImage`): with
  a profile that isn't neutral, the copy is a corrected pass of core/vr/Colour's prelude (`MEVR_EYE_OUT`, the same shader
  text LE1's eye blit and HUD panel use and `mevr-colour-shader` checks): an 8-bit source written through the eye image's
  UNORM view (its encoded values, LE1's encoded path), a float one through its sRGB view (linear). The ±0.5-step dither
  comes with it, per eye (`OnEyeSource` names the eye). **Neutral is the copy as before** (Halcyon's `CopyResource`), so the
  default on a PS VR2 or any headset that isn't a streamed Quest changes nothing. `plugin/EyeCopy`'s log: `eye copy: the
  colour correction draws ...`. The snapshot's `"colour"` block on ME2 too.
- The UI panel: when lane B3's HUD panel lands on ME2, its blit takes `colour::BeginPanel`/`EndPanel` as LE1's
  `HalcyonOverlays` does (not done here).

## 2. Method

One ME2 stretch on the fake headset (`tools/le2-halcyon-run`: `prep.ps1`, `launch.ps1`, `load.ps1`), Windows HDR on,
`hdrDisplay = -1` (the game's 8-bit frame, format 28): two dark places from saves, **Freedom's Progress at night** (the VR
Shepard career's Save 4, `load.ps1`'s default) and **the Normandy's crew deck** (`John_12_Soldier_190924_4ee650d\Save_0036`).
In each, with the head still, the `[Colour]` keys of `ASI\LE2MEVR.ini` rewritten between snapshots (re-read within a
second, logged `colour: profile ...`) and the game's own gamma set through the console (`gamma <value>`, UE3's
`Client.DisplayGamma`). Each setting's left eye (the swapchain image after our copy) measured with `tools/picture-stats.py`
(research/27 7d's method plus two numbers for bands):

- the frame: mean luma, the share of pixels with luma under 8 ("black") and under 13 (Meta: a Quest LCD can't tell steps
  below 13/255 apart, research/27 section 5);
- the darkest 400x400 window (Freedom's Progress: the dark building's facade; the Normandy: the floor grating) and the
  smoothest dim window (mean luma 12..90, the least horizontal gradient: the sky, the ceiling): mean, black share, distinct
  luma levels, the mean run of equal luma along rows and the mean non-zero step between neighbours (how visible a band edge
  is); the brightest window's mean for the highlights.

The world doesn't hold still (`pause` doesn't freeze ME2's lights and particles): two neutral frames 3 minutes apart differ
by 2.9-3.6 on average. So each setting was also applied to the neutral frame offline with core/vr/Colour's grade in numpy
(scratch), which matches the game's corrected snapshots in the aggregate (mean luma within 0.05, black share within 0.3
points: the port draws ME1's grade exactly) and compares settings on one identical frame.

## 3. Results

Normandy crew deck (Freedom's Progress in brackets; "game" rows are the game's snapshots, the rest the model on the neutral
frame):

| setting | mean luma | luma < 8 | luma < 13 | darkest window: mean, black | dark steps (game) | highlights (mean) |
|---|---|---|---|---|---|---|
| neutral (the copy as before) | 34.5 (54.7) | 13.3% (14.0%) | 24.7% (22.4%) | 4.5, 87% (4.5, 88%) | 1.80 (2.04) | 149 (125) |
| ME1's **Quest** (contrast 1.06, saturation 1.12, mid-tones 0.90) | 27.5 (46.4) | **23.8% (21.5%)** | **39.4% (28.6%)** | 2.7, 95% (2.6, 99%) | 1.75 (1.98) | 143 (116) |
| ME1's **Vivid** (1.12, 1.25, 0.85) | 23.5 (41.4) | 33.6% (25.9%) | 49.4% (32.7%) | 1.9, 98% (1.8, 100%) | 1.49 (1.62) | 139 (110) |
| Quest, dither off | 27.5 (46.4) | 24.0% (21.7%) | 39.0% (28.5%) | 2.7, 96% (2.6, 99%) | 1.67 (1.78) | 143 (116) |
| mid-tones 1.10 | 40.0 (61.1) | 7.9% (8.9%) | 17.0% (17.5%) | 6.3, 73% (6.3, 69%) | **2.67** (2.38) | 155 (133) |
| mid-tones 1.20 | 45.5 (67.2) | 5.0% (5.0%) | 11.2% (12.1%) | 8.5, 50% (8.5, 45%) | **5.37** (4.52) | 160 (140) |
| black level +0.03 | 40.9 (60.5) | **0.6% (0.5%)** | **8.3% (9.1%)** | 11.8, 11% (11.7, 6%) | 2.19 (2.14) | 152 (129) |
| mid-tones 1.15, black +0.02 | 47.9 (68.7) | 0.0% (0.0%) | 4.0% (3.8%) | 14.5, 0% (14.4, 0%) | 3.97 (3.29) | 160 (139) |
| **the game's gamma 2.6** (`gamma 2.6`; 2.2 default) | 41.2 (62.3) | 7.0% (7.9%) | 16.0% (16.6%) | 6.2, 78% (6.3, 68%) | **1.87** (2.52) | 157 (136) |
| the game's gamma 2.0 | 27.9 (46.6) | 22.6% (20.8%) | 38.0% (27.9%) | 3.2, 94% (3.0, 98%) | 2.21 (1.83) | 141 (113) |
| model: Quest with mid-tones 1.0 | 32.8 (53.1) | 15.5% (16.0%) | 28.2% (24.0%) | 4.1, 90% | | 150 (125) |
| model: Quest, mid-tones 1.0, black +0.03 | 38.9 (58.8) | 0.8% (1.3%) | 11.4% (12.1%) | 10.6, 18% | | 153 (129) |
| model: mid-tones 1.10, black +0.02 | 44.9 (65.4) | 0.4% (0.7%) | 5.8% (6.8%) | 12.6, 10% | | 157 (136) |

What that says:
- **ME1's Quest and Vivid profiles darken ME2's dark scenes.** Their mid-tones below 1 (Virtual Desktop's direction, for
  ME1's washed-out look on a Quest) move a tenth of the frame under luma 8 and, on a Quest's LCD, 39% of the Normandy frame
  under the 13/255 it can't resolve (25% neutral). For ME2 that is the wrong direction.
- **The lift that keeps the most shadow detail without new steps is the black level** (+0.03: black 13% -> 0.6%, the dark
  window's mean 4.5 -> 11.8, its steps 1.80 -> 2.19 in the game and unchanged in the model), at the price of contrast: black
  becomes grey (code 7), which an OLED shows and a Quest LCD hides under its own floor.
- **Mid-tones above 1 brighten the darks but widen the 8-bit steps** (the Normandy's dark window: step 1.80 -> 2.67 at 1.10,
  5.37 at 1.20: a lift after the game's 8-bit store stretches its codes apart). **In the Normandy the game's own gamma lifts
  as much without that** (gamma 2.6: mean 34.5 -> 41.2, black 13% -> 7%, steps 1.87): it is applied in the game's float post
  pass, before the 8-bit store, and the eyes copy that frame. At Freedom's Progress the two measured alike (2.38 against
  2.52: its facade is textured and the scene moves, research's noise floor above). So the game's gamma is at least as good a
  mid-tone lever, and the better one where the darks are smooth; ours is the one that is per headset.
- **Dither** (ME1's ±0.5 step): no visible difference in either scene (the same means; a third more distinct colours in the
  dark windows; no flat bands to break: the mean run of equal luma is 1.1-1.3 px in every window, the game's frame is
  textured and noisy enough). Keep ME1's 0.5: it costs nothing and matters where a grade meets a smooth gradient.
- Highlights move little with any of them (the brightest window's mean +6 to +10; under 1% of pixels at 255 in all).

## 4. ME2's own brightness

- **The setting:** `BIOEngine.ini [Engine.Client] DisplayGamma` (ME2's 2.2 on this machine; ME1's file here says 2.0).
  `GamerSettings.ini` has no brightness key. The console's `gamma <value>` sets it live (UE3's client exec), and VR sees it
  at once: the eyes are copies of the game's frame (measured above: higher is brighter in LE). The game's options menu's
  brightness slider most likely writes the same value **[verify]** (not driven: no computer use for the game).
- **What it does in VR:** a brighter or darker world and UI in the eye images alike; the darks lifted before the 8-bit
  store (the steps above).

## 5. Recommendation (the owner decides; nothing is flipped here)

ME1's defaults stay ME2's (the plan's rule): Auto is `neutral` for the PS VR2 and every headset that isn't a streamed Quest,
`quest` for streamed Quests, dither 0.5.

| headset | recommendation | why |
|---|---|---|
| PS VR2 and other OLED / wired (Auto = neutral) | keep **neutral**; for brighter dark scenes the game's own gamma (`DisplayGamma` 2.4-2.6) rather than our mid-tones | true blacks are the panel's strength; the game's gamma lifts the shadows without widening the 8-bit steps |
| Quest streamed (Auto = quest) | for ME2, a Quest profile **without the mid-tone darkening and with a black lift**: contrast 1.06, saturation 1.12, mid-tones 1.0, black +0.03 (an ME2 `quest` preset, or `[Colour.<headset>]` custom values) | ME1's `quest` puts 39% of a dark ME2 frame under the LCD's 13/255 floor; this one 11-12%, with ME1's contrast and saturation kept |

Both need the owner's eye in the headsets **[verify]** (`notes/headset-checklist.md`): numbers say where the shadow detail
goes, not whether the owner likes it. A per-game preset would be a new code path in `core/vr/Colour` (`Preset` per game);
not built until the owner says yes.

## 6. Not done here

- The HUD panel on ME2 (lane B3): its blit should take `colour::BeginPanel`/`EndPanel` (LE1's `HalcyonOverlays` way) when it
  lands, so the UI gets the same correction as on ME1.
- The cost of the corrected copy on ME2 (one full-screen pass per eye instead of a `CopyResource`; LE1's eye blit measured
  +0.02-0.06 ms per eye offline, research/27 section 7): `tools/perf-probe.ps1` in an ME2 run with a profile on.
- The menu's Display rows on ME2 belong to lane D (`MainMenuSettings`): the keys are ME1's (section 1), none added.
- A third dark place (Purgatory, Omega's back streets, the Collector ship): no save there on this machine.
