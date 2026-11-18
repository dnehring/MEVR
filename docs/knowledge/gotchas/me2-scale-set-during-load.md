---
type: Gotcha
title: In ME2 a console scale set during a level load, with depth of field on, kills the rendering thread at the first gameplay frame
description: The mod's per-level engine settings (game/SystemSettings) sent while a save loads made ME2's rendering thread fault in the level's first gameplay frame whenever the game's depth of field was on; the game then hangs. On LE2 they now wait until the level has been in gameplay for 3 s.
tags: [le2, engine, render-thread, system-settings, halcyon]
status: stable
timestamp: 2026-11-14T18:20:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-14T18:20:00Z }
bitten: [2026-11-14, 2026-11-17]
---

# Symptom

| wording | where |
|---|---|
| "the render thread faulted in the first gameplay frame", "the game hung right after the load" | the first fake-headset runs of Halcyon's ME2 renderer with upstream's `MELE2VR.ini`, 2026-11-14 |
| `first-chance access violation 0xC0000005 on thread <not the game's> at MassEffect2.exe+0x98C25D (reading 0x1C)` | `Logs\LE2MEVR.log`, 15-25 ms after Halcyon's `[VRSTATE] gm 0 live alive -> VR` |
| the heartbeat stops, the process uses no CPU but its window still "responds"; `crash-firstchance.txt` names a stack of `MassEffect2.exe` frames only | `%LOCALAPPDATA%\MEVR` |
| "the galaxy map's first held frames fault the render thread", the same `+0x98C25D` 50-70 ms after `the view held at Shepard's` (a post effect's texture parameter null, the effect at 0x3A9FE0) | ME2's galaxy projector flow, 2026-11-17 (lane GX2): with or without captures; `scale set DepthOfField False` before the map: none |

It looked like first person, the eye size, the body hides, the HUD transforms and the UI scale in turn; none of them was
it (research/le2-renderer-integration.md's run table).

# Cause

`game/SystemSettings` sends `scale set` lines (`DynamicShadows`, `MotionBlur`, `FoliageDrawRadiusMultiplier`,
`LightEnvironmentShadows`) when a new level becomes the world; on a save's load that is during the load, about a second
before the first gameplay frame. With the game's `DepthOfField=True`, any such line there (even a value the engine already
had, directly or through its opposite) faults that frame on the rendering thread, which then
[hangs the game](render-thread-fault-hangs-the-game.md). Halcyon's ME2 writes `DepthOfField=False` at attach unless its
ini says `DisableDof=0`, and upstream's `MELE2VR.ini` does, so the problem only showed once that file was installed. The
same lines 3 s into gameplay are harmless. Whether it needs Halcyon's renderer at all is not known.

# Check

Halcyon's log has `[DOFOFF] [SystemSettings] DepthOfField=True` at attach, and the mod's log a `system settings: scale
set ... (a new level: applied again)` line just before the fault. `[SystemSettings] apply = 0` in `ASI\LE2MEVR.ini` (no
lines) or `DisableDof=1` in `MELE2VR.ini` each make the fault go away.

# Fix in place

`Poll` in `src/game/SystemSettings.cpp`, `#if defined(MEVR_GAME_LE2)`: a new level's settings wait until it has been in
gameplay (mode 0-4) for `kSettleApplyAt[0]` (3 s). LE1 still applies at the world change (its motion blur needs it,
[motion blur in stereo](motion-blur-in-stereo.md)). If it comes back, look for another `scale set` or console line sent
during a load on LE2. The galaxy map's opening (2026-11-17): `game/GalaxyProjectorFlow` turns the depth of field off near the
CIC's projector on foot, 3 s into gameplay (`flow::Adapter::depthOfFieldOff`, `game/DepthOfFieldHold`), and back away from it.

# Citations

- [research/le2-renderer-integration.md](../../../research/le2-renderer-integration.md), "Defaults from ME1": the A/B,
  one variable a run.
- `src/game/SystemSettings.cpp` (`Poll`), `src/halcyon/le2/d3d_capture.cpp` (`ArmResolutionSpoof`, `[DOFOFF]`).
