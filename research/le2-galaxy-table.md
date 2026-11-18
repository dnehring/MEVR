<!-- SPDX-License-Identifier: MIT -->
# ME2 native galaxy and orbital image on the shared table

> **Since 2026-11-17 (lane GX2) ME2's map is ME1's projector flow; the table below is retired** ("The projector flow").

## The projector flow (lane GX2, 2026-11-17)

**ME2 now runs ME1's design on the sequels' one flow** (docs/galaxy-projector-handoff.md step 5; Forks 3): the map opens over the
CIC's projector, the view held at Shepard's at the console, the CIC in stereo around it, the game's map a stereo disc lying on
the projector, its Flash UI on our panel on the same disc. The flat table above is **retired** (below).

### ME2's facts (read offline from the installed packages with `tools/conversation-scan --game LE2 --class-dump`)

- **The projector is ME3's actor.** `BioA_Nor_110.pcc` (the CIC's art) has `TheWorld.PersistentLevel.InterpActor_0`, `Tag =
  GalaxyMapProxy`, its `StaticMeshComponent_5` showing `BIOA_GalaxyMap_S.GalaxyMapProxy`, at (-0.481, 3146.528, 46.952),
  `Rotation` roll 4321 (24 degrees, as ME3's), `DrawScale3D` (0.625, 0.625, 0.5). The console's use target is
  `BioD_Nor_103CIC.pcc`'s `SFXPointOfInterest_6`, `Tag = placu_galaxyMap`, at (1.856, 3130.68, 97): the same tag as ME3's. So the
  flow's finder (`FindProjector` in `game/GalaxyProjectorFlow.cpp`: one InterpActor tagged GalaxyMapProxy with that mesh, one
  point of interest tagged placu_galaxyMap, in the player's world) is one rule for both sequels.
- **The decks.** The elevator's arrivals are `BioD_Nor.pcc`'s `BioStartLocation`s `start_elevDeck0` (2.31, 4487.8, 689),
  `start_elevDeck1` (the CIC, 2.31, 4487.8, 89), `start_elevDeck2` (the crew deck, -1.7, 4510.4, -480.7): 600 units a deck.
  The career's Normandy saves (John's `AutoSave`, `Save_0036`) stand on the crew deck (z about -460).
- **The map's game mode** is 9 (galaxy) and 10 (orbit), `SFXGameModeGalaxy.GalaxyCam` a `BioCameraBehaviorGalaxy` with
  `m_pCenterObject` (ME2's SDK), the same fields as ME3's: the flow's `MapCentre` finds the galaxy mode among
  `GameModeManager2.GameModes` by its class, no index.

### What the code does

- `game/GalaxyProjectorFlow` (new; the sequels' flow, lifted from ME3's first adapter `game/Le3GalaxyCapture`): the UpdateCamera
  post-handler (Shepard's view on foot remembered, the map camera's POV), the projector measured on foot every 2 s while the
  adapter says the CIC's art is visible, the hold in the map's modes, the room's own hologram hidden, the stereo window
  (`game/GalaxyProjector`'s `StereoWindow`), the status. The adapter (`flow::Adapter`): the CIC's art visible, and an optional
  retention of the room (ME3's; ME2 needs none, below). ME3's `Le3GalaxyCapture` moves onto it after lane GX3's step.
- `game/Le2GalaxyCapture` (ME2's adapter): the CIC's art is `BioA_Nor_110` loaded and visible; `mevr galaxy state | projector |
  capture ... | place X Y Z yawDeg` (`place`: the controller's own `ClientSetLocation`, a fixture's way to the console from
  another deck).
- `game/SceneCaptureRig` on LE2 (its rows: `engine/ReflectCheck`'s `CheckLe2CaptureRig`).
- ME2's renderer (`src/halcyon/le2`, `[MEVR]`): `d3d_capture.cpp`'s `GetMenuMode` answers the map's modes 9 and 10 not flat while
  `MevrSeams::GalaxyOwnsPresentation`; `calcview_hook.cpp` calls `MevrSeams::WorldViewHoldBegin/End` around the primary player's
  build in those modes only; `engine_probe.cpp`'s first person hides Shepard's head and body at the held eyes there
  (`MevrSeams::WorldViewHeld` 2, as ME3's).
- The host: ME2's host serves `setGalaxyHologram` (ModLoader); the window and the UI panel are `plugin/SequelOverlays`'.
- The flow's adapter flag `depthOfFieldOff` (ME2's): the engine's depth of field off near the projector (below).
- **No pointer on the map's UI** (`Facts::galaxyUiPointer` 0, as ME3's): ME2's map keeps its own controls (the stick moves
  the Normandy, A selects, X exits, Y buys probes, LB/RB the tabs; orbit: LT scans, RT launches a probe, Back toggles). A ray
  click on the map's Exit prompt through the game screens' GUI events (`SFXGameModeGalaxy.GuiKey(48)`) changed nothing (run
  `galaxy-final-201028`: still mode 9 until the replayed X); posted mouse messages (ME1's route, `galaxyUiPointer` 1) were
  not tried. Where it would act on the projector (a ray steering the ship) is the owner's question, as on ME3. The
  galaxy-mode-only rule in `plugin/SequelOverlays` (orbit never takes the pointer) stays for a game that sets one.

### In the game (fake headset, 2026-11-17)

Final build: run `galaxy-final-201028` (the same five verdicts PASS, the depth of field handled by the flow, the Exit click
through GUI events doing nothing, X closing the map). Not yet seen: travel (a relay's movie, a system to another), orbit (mode
10) and the scanner through the window, a briefing interrupting the map, recenter inside it, the headset.

Stretch `%LOCALAPPDATA%\MEVR\sessions61117-192445-759-LE2-gx2` (John's `Save_0036`, the crew deck, loaded read only;
`tools/le2-halcyon-run/galaxy.ps1 -Game LE2 -Place '0 3360 160 -90'` after `mevr galaxy place 2.31 4487.8 110 -90`):

- **Reaching the CIC by script.** The decks stream: on the crew deck `BioA_Nor_110` isn't loaded. Shepard put at the CIC's
  elevator arrival (`mevr galaxy place 2.31 4487.8 110 -90`, the controller's `ClientSetLocation`) touches `BioA_Nor`'s
  `BioTriggerStream_9` (0, 4560, 100), which streams the CIC in (`BioA_Nor_100/105/110/120`, `BioD_Nor_103CIC`, ...); then at the
  console (0, 3360, 131 on the platform). A replayed A at the console (head pitched -25 degrees, ME2's gaze selection on
  `SFXPointOfInterest_6`) opens the map (mode 9).
- **The projector measured live**: centre (-0.48, 3166.42, 92.2), radius 188.3 units (1.88 m), normal (0, 0.40, 0.92): 24
  degrees off vertical, tilted toward the console. ME3's numbers to the unit (research/le3-galaxy.md): the same CIC art.
  The map's own level `BioA_Nor_103aGalaxyMap` is loaded but hidden on foot; **the map's opening keeps the CIC's art loaded**
  (no retention needed, unlike ME3).
- **The render thread faulted** in the first held frames (`MassEffect2.exe+0x98C25D`, reading 0x1C; the game hangs: a
  post-process effect's texture parameter null, called from the effect at 0x3A9FE0), in three runs: with the captures, with
  the Umbra share off, with a CaptureScene hook giving captures no eye index, and with the view held and no captures at all
  (`mevr galaxy capture hold-only`). With `scale set DepthOfField False` before the map: none. The same signature as
  [gotchas/me2-scale-set-during-load](../docs/knowledge/gotchas/me2-scale-set-during-load.md) (upstream's `MELE2VR.ini` keeps
  the game's depth of field on). Fix: the flow turns the depth of field off near the projector on foot (3 s into gameplay,
  never in a load; `game/DepthOfFieldHold`, ME1's conversation world's rule) and back away from it.
- **The flow** (run `galaxy-full1-200157`, all five verdicts PASS): the projector measured on foot; A opens the map; the view
  held at Shepard's (`LE2 galaxy projector: the map is open in the CIC: the view held at Shepard's (0, 3325, 218 ...)`), the
  room's hologram hidden (2 actors), `galaxy window: up: the game's map 2.64 x 1.51 m (a disc) ... on the projector's
  hologram`, each eye 2009x1152 of a 2048x1152 capture; the map's Flash UI (the system's name, the Exit prompt) on the panel
  on the same disc; out of the map the hold and the window released, on foot again. Snapshot
  `2026-11-17_20-02-35-479-*` (left/right eyes, `galaxy-window-left/right`, `hud`).
- **ME2's map exits with X** (its prompt: "Exit" with the X glyph); B does nothing at the system level. The runner replays X,
  then falls back to `mevr galaxy exit` (`BioSFHandler_GalaxyMap.CloseGalaxyMap`, the Exit button's own).
- **The game autosaves when the map opens**: the career's `AutoSave.pcsav` was rewritten at the console (a later load of it
  starts in the map, mode 9); `restore.ps1` puts it back. Load `Save_0036`, never the AutoSave, for these runs.
- The reflection's `SceneCapture2DComponent.bUpdateMatrices` is at +0x174 mask 1 (as the generated header's comment says);
  the compiled LE2 SDK struct is four bytes late, as ME3's: a documented-layout row in `CheckLe2CaptureRig`.

### Retired: the opt-in flat table

`HalcyonLe2Bridge::PlacePanel`'s table (`vr::galaxy_map::NativeTablePanel`, `[Galaxy] nativeTable`, off by default) placed the
renderer's whole flat map image on ME1's table in front of the head. It was Forks 3's third design and is not ME1's: ME1's map in
the CIC is the projector flow, and where that flow can't hold (no tracking, no view on foot) the map is the renderer's flat panel.
ME2 now does exactly that, so the table, its pointer registration and `galaxyTable` capability are gone from ME2 (ME3's bridge
still has its copy: lane GX3's). The trigger latch across the galaxy-to-orbit entry (`PadMerged`) and the orbit's native triggers
(`PadOrbitalTriggers`) stay.

## The flat table (2026-11-15, retired 2026-11-17)

2026-11-15. M6 G's first presentation increment, based on `b74989a4`.

## Source audit and implementation

ME1's `game/GalaxyMap` reads LE1 galaxy tables, its camera behavior, a native crosshair and specific Normandy
projector actors. ME2's galaxy hierarchy, ship navigation and orbital mode need their own game adapter.
The existing [galaxy research](51-galaxy-map.md) and [orbital fixture work](le2-scanning-readiness.md)
remain the native-system references for the following increments.

Halcyon's ME2 renderer already presents modes 9 (galaxy) and 10 (orbital) using the complete native game
image. This increment places that image in app space using ME1's `galaxy_holo::PlaceTable` and `DiscPose`.
The complete image preserves native labels, travel questions and scanner readouts at their authored coordinates.
`NativeTablePanel` in `core/vr/GalaxyMap` keeps one placement through the galaxy/orbit transition, leaves
the original renderer placement in use while tracking or the copied image is unavailable, and resets on
other modes, disable, session replacement or recenter.
It preserves the native image's aspect ratio. The table's width is twice `tableRadiusM`.

`MevrBridge::PlaceGalaxyPanel` runs immediately before the submitted layers are assembled, using the head
located at the same predicted display time. It changes the native quad's pose and size. The original image
and swapchain remain owned by the renderer. The feature uses no game-thread camera writes and makes no
gameplay calls. It presents a fixed table against the existing flat-mode backdrop. Keeping the actual CIC
world around the table and capturing stereo depth from the native map remain future increments.

In mode 9 the existing game pointer receives exactly that submitted quad. Its native mouse route remains
subject to foreground-window and game input handling, which need a native run. In mode 10 no pointer is
registered, leaving the native analog scanner controls available. The shared pad merge now accepts an
orbital-native-trigger flag to retain LT/RT instead of the on-foot left-trigger squad remapping. The existing
close latch requires a trigger held across galaxy-to-orbit entry to be released before reaching the scanner.
The custom main menu/calibration and their close latch also withhold the galaxy pointer surface, so its
mouse path cannot bypass the existing pad release gate. No new probe-launch command or resource write is introduced.

## Candidate configuration

The presentation is opt-in while native acceptance is pending. In `ASI/LE2MEVR.ini` use the existing
`[Galaxy]` section with `nativeTable = 1`. Existing settings `tableDistanceM`, `tableBelowM`, `tableRadiusM`
and `worldTiltDeg` tune the placement. Defaults for this candidate are 0.70 m ahead, 0.45 m below, 0.48 m
radius and 35 degrees tilt. Settings are read at most once per second. Reopen the map or recenter after
changing position or tilt. Set `nativeTable = 0` to use the renderer's ordinary panel placement.

The native-trigger mapping applies in orbital mode regardless of table presentation, so scan/probe controls
remain available on the ordinary panel too. Test those bindings through actual controller replay.

## Validation and next native run

Offline tests extend the existing galaxy tests for galaxy/orbit continuity, missing tracking, leave/reopen,
recenter, disabled presentation and invalid aspect. Pad-merge tests cover native LT/RT despite the on-foot
squad mapping and independent trigger release across entry. These tests establish deterministic rules only.

The isolated Release `MEVR-LE2` host/mod build passed, including a final incremental rebuild after the
session-reset and image-readiness guards. Existing CTest targets `mevr-tests` and `mevr-pad-merge` passed
(2/2). Whitespace and repository SPDX checks passed. No game was launched or deployed for this increment.
The companion [native acceptance checklist](../docs/le2-m6-native-checks.md) audits the existing HUD,
comfort and power commands/recordings; its generator examples were checked offline.

Use `tools/le2-halcyon-run` under exclusive ownership, load the protected Normandy fixture through normal
save loading, enter its map normally and enable the candidate. Capture both eyes with the hands visible.
Check table placement, native labels, pointer selection and travel confirmation, then enter orbit and verify
scan motion and a single launch from the prepared one-probe fixture. Compare same-planet resource/probe
state before and after. Repeat the zero-probe refusal and save/reload discovery baseline using the existing
[Zanethu investigation](le2-galaxy-fixture-source.md). The diagnostic orbital observer requires integration
or verification against the deployed Halcyon host before relying on its outputs.

Check a trigger held while entering orbit, both-stick main menu, tracking loss, recenter, map exit and a
load transition. Run the existing restoration verification. Table presentation, successful native picking,
resource accounting, discovery persistence and headset comfort are pending native acceptance.
