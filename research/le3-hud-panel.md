<!-- SPDX-License-Identifier: MIT -->
# ME3's gameplay HUD on the body-locked HUD panel (lane HU, 2026-11-16)

ME1 and ME2 put the game's HUD on our body-locked HUD panel (`plugin/HudPanel`, research/15, research/le2-renderer-integration.md
"Step 4"). ME3's renderer already sent each SFR UI draw through the shared seam (`src/halcyon/le3/d3d_capture.cpp` `EmitUiDraw`
calls `MevrSeams::HudPanelDraw`, which is `hud_panel::Redirect`), but its renderer said `hudPanelGameplay = false` and
`Hud().hudPanel = false`, so the panel was armed only for a hand-set interaction crop and the gameplay HUD stayed in the eye
images. That kept ME3's stereo plan in mode 1 (the symmetric window), ME1's rule while UI is in the eyes (research/30).

## What changed

- `plugin/RendererLe3.cpp`: `Caps::hudPanelGameplay = true`; `Hud()` returns ME1's defaults (`renderer::HudSettings{}`: the panel
  on, 1.5 m, 1.3 m wide, 0.25 m below the eyes, following the head past 70 degrees), as ME2's does.
- `plugin/SequelOverlays.cpp`: which game modes are gameplay for the panel (and for hiding the crosshair) is
  `engine::adapter::IsHudGameplayMode`: on foot, the vehicle, the power and weapon wheels, the squad command, and ME3's Atlas and
  injured Shepard. Until now it was ME2's raw numbers 0..4, which on ME3 are on foot, vehicle, Atlas, power wheel and weapon
  wheel (no squad command, 5, nor injured, 6). ME2's set is unchanged.
- Nothing in `d3d_capture.cpp`: the draw site existed. Halcyon ME3's per-element HUD moves (`hud_probe.cpp`'s baked `[HUD]`
  groups, ME2's PcHud counterpart) never run in our host (`[MEVR]` in its `Tick` and `SetEditorEnabled`), so the movie keeps its
  own layout on the panel and `SetAuthoredHudLayout` stays a no-op.

## In the game (fake headset, renderer preview, Save 31, the Normandy's War Room)

Sessions `20261116-215028-548-LE3-hu-me3a`, `20261116-215450-633-LE3-hu-me3b`, `20261116-215851-868-LE3-hu-me3c` (two runs); each
restored with 0 differences.

- `overlays: game UI goes to the body-locked HUD panel`, then `[MEVR-HUD] HUD panel texture 3072x1728 fmt=28`; the UI draws are
  "over" blended (`src=5 dst=6`, and `dstA=7` for the prompt's), none left to the renderer.
- `[MEVR-STEREO] stereo mode 2: life-size, each eye's own frustum` as soon as the panel is armed (`plugin/MetricStereo`:
  `uiInEyes = in.uiInEyes || !hud_panel::Armed()`); before it, at the load, mode 1. Snapshots' `"stereo"."mode": 2`.
- ME3's UI draws mostly come after the renderer's left-eye capture; a few per second come before it (`... N UI draws before the
  renderer's left-eye capture (replaced by the later batch)`): the panel takes both and clears at the first late draw, as ME2's.
  The War Terminal's prompt on the panel was drawn once (no doubled alpha).
- The eyes' world layers carry no UI (`-left-world.png` at the War Terminal: the console, its 3D selection ring, no prompt); the
  `hud` layer has it, and with the label at the object (research/40 section 6) the prompt's band leaves the panel for the `label`
  layer at the console.

## Not verified

- ME3's combat HUD on the panel: no ordinary combat fixture yet (research/le3-combat-fixtures.md). Its health, squad, ammo and
  power clips are not shown on the Normandy (`mevr guimouse hud`: `WeaponAmmo`, `WeaponClip` at the bottom left, the target
  clips at the top centre).
- ME3's crosshair: `screenReticleHide` stays false (ME3's HUD is SFXGUIMovie, `game/ScreenReticle` stands down on ME3), so in
  combat the game's crosshair would be drawn on the panel, not hidden under our hand reticle as on ME1 and ME2.
- The wheels (power, weapon), the squad command and injured Shepard's mode on the panel; the headset.
