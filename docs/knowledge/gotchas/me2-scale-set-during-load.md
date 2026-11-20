---
type: Gotcha
title: A console scale set during a load hangs the rendering (ME2 and ME3)
description: A `scale set` the mod sends while the game loads hangs it. ME2 - the per-level engine settings (game/SystemSettings) sent while a save loads faulted the rendering thread in the level's first gameplay frame whenever the game's depth of field was on. ME3 - the depth of field switched as a conversation handed over to a load (the Citadel's arrival; the rapid transit's cab ride, which loads in game mode 0) left the load spinning on its loading picture. On the sequels every scale set now waits until the world is drawn (game/WorldDrawn); on LE2 a new level's settings also wait 3 s into gameplay.
tags: [le2, le3, engine, render-thread, system-settings, halcyon, depth-of-field, loading]
status: stable
timestamp: 2026-11-14T18:20:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-14T18:20:00Z }
bitten: [2026-11-14, 2026-11-17, 2026-11-19, 2026-11-20]
---

# Symptom

| wording | where |
|---|---|
| "the render thread faulted in the first gameplay frame", "the game hung right after the load" | the first fake-headset runs of Halcyon's ME2 renderer with upstream's `MELE2VR.ini`, 2026-11-14 |
| `first-chance access violation 0xC0000005 on thread <not the game's> at MassEffect2.exe+0x98C25D (reading 0x1C)` | `Logs\LE2MEVR.log`, 15-25 ms after Halcyon's `[VRSTATE] gm 0 live alive -> VR` |
| the heartbeat stops, the process uses no CPU but its window still "responds"; `crash-firstchance.txt` names a stack of `MassEffect2.exe` frames only | `%LOCALAPPDATA%\MEVR` |
| "the galaxy map's first held frames fault the render thread", the same `+0x98C25D` 50-70 ms after `the view held at Shepard's` (a post effect's texture parameter null, the effect at 0x3A9FE0) | ME2's galaxy projector flow, 2026-11-17 (lane GX2): with or without captures; `scale set DepthOfField False` before the map: none |
| "ME2 hung as the map closed after a hot reload", the same `+0x98C25D` 0.2 s after `mevr galaxy exit` | lane GX4, 2026-11-17: the mod rebuilt (hot reload) with ME2's map open; the old generation's unload put the depth of field back on, and the map's closing cinematic faulted |
| "the Citadel's arrival load never finished" with the docking officer's conversation held in the world | ME3's conversation slice, 2026-11-19 20:56 (commit `950abe3f`): the depth of field switched as the conversation handed over to the load |
| "the game sat on the Citadel loading picture, 0 scene renders, objects flat" (`render levers: ... scene renders per frame 0`, `heartbeat ... objects=135284` for six minutes, GWorld unchanged, game mode 0) | ME3, lane FPC-ME3, 2026-11-20 11:22: Save_0090's docking officer's conversation held in the world by hand, the Embassies ride picked on its wheel; `depth of field on again (after 3 s on foot)` 3 s into the cab ride's load, and game/SystemSettings' menu-return `scale set DynamicShadows/LightEnvironmentShadows` at its start |

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

**The sequels, since 2026-11-20 (lane FPC-ME3): no `scale set` while the world isn't drawn.** `game/WorldDrawn`
(`world_drawn::Tick` once a game tick from `Mod.cpp`, the sequels' for now; `world_drawn::Now()`): the player's body drawn within
the last 0.5 s of wall time, its `PrimitiveComponent.LastRenderTime` moving (a loading screen draws no scene, and the world's clock
may stand still, so it is timed on the wall clock). Every sender on the sequels asks it and waits, tried again every poll or tick,
never dropped: `game/SystemSettings`' `Poll` (a new level, a return to gameplay, the shadows, an edit, the first apply;
`#if` LE2/LE3, ME1's compiled code unchanged), `game/DepthOfFieldHold` (`Off`/`Back` say what is wanted, `Step` sends it: the
sequels' conversations in the world and ME2's galaxy projector flow). A conversation opened around a load and held by hand
(`mevr convo world on`) never switches the depth of field at all. ME3's rapid transit loads in game mode 0, so "3 s on foot" alone
never saw it. Tests: `mevr-le2-system-settings`, `mevr-le3-system-settings` (a return to gameplay and a new level wait while the
world isn't drawn and go once it is).

**ME2 before it:**

`Poll` in `src/game/SystemSettings.cpp`, `#if defined(MEVR_GAME_LE2)`: a new level's settings wait until it has been in
gameplay (mode 0-4) for `kSettleApplyAt[0]` (3 s). LE1 still applies at the world change (its motion blur needs it,
[motion blur in stereo](motion-blur-in-stereo.md)). If it comes back, look for another `scale set` or console line sent
during a load on LE2. The galaxy map's opening (2026-11-17): `game/GalaxyProjectorFlow` turns the depth of field off near the
CIC's projector on foot, 3 s into gameplay (`flow::Adapter::depthOfFieldOff`, `game/DepthOfFieldHold`), and back away from it;
the flow's unload (a hot reload) puts it back only on foot (2026-11-17, lane GX4: `flow::Shutdown`), never in the map or its
cinematics.

# Citations

- [research/le2-renderer-integration.md](../../../research/le2-renderer-integration.md), "Defaults from ME1": the A/B,
  one variable a run.
- `src/game/SystemSettings.cpp` (`Poll`), `src/halcyon/le2/d3d_capture.cpp` (`ArmResolutionSpoof`, `[DOFOFF]`).
- [research/le2-conversations-in-world.md](../../../research/le2-conversations-in-world.md) section 10 (ME3's cab-ride hang,
  2026-11-20); `src/game/WorldDrawn.cpp`, `src/game/DepthOfFieldHold.cpp`.
