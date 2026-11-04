# 51: The galaxy map as a hologram, and ME2's planet scanning (2026-11-03)

The question (user, 2026-11-03): the game's full-screen experiences should be better in VR. First the galaxy map on the
Normandy: instead of a full-screen view steered with the left stick, show the map as a hologram over the CIC's projector and
travel by pointing and clicking. For ME2, planet scanning could become an automatic scan that takes a planet's resources at once.

Everything below is read from our code and the generated SDK headers (`external/LExSDKv2/Src/LESDK/LE1|LE2/`, class layouts and
function signatures; nothing disassembled). **[inferred]** marks what the headers only suggest; nothing here was tried in the game.

## 1. What we do today (LE1)

- Halcyon reads the engine's game mode (`le1_game.h:82-88`: 9 = galaxy) and, with `galaxyFlat` on (the default), shows the whole
  backbuffer on one flat quad, 2.35 m wide at 1.4 m (`xr_session.cpp:3645`, `:4908-4915`, `:5825-5864`; `galaxyMonoQuadWidth`),
  locked in the room by our `WorldPanels` (`PlacePanel`, kind GalaxyMap = 2).
- With `galaxyFlat` off, Halcyon renders the map in its normal stereo mode and `galaxyVrZoom` scales the declared field of view
  (`xr_session.cpp:4763-4776`): the map's own 3D scene already renders in VR, from the map's camera.
- The game pointer (research/42) turns a controller ray on the flat quad into the game window's mouse (trigger = click, right stick =
  wheel) for menus and the galaxy map. **It has never been tried on the galaxy map** (research/42 item 5; `notes/next-vr-session.md`).
- `docs/trilogy-design.md` row "Galaxy map / scanning": "Panel to start; holographic tabletop is a stretch".

Note: LE1's map is a **cursor map** (the PC cursor or the left stick moves a cross-hair; select, zoom in, travel). The small Normandy
steered with the stick is ME2's and ME3's map (section 4).

## 2. How LE1's galaxy map works

- **A real 3D scene with a Flash layer on top.** The game mode `SFXGameModeGalaxy` (`SFXGame_classes.hpp:30082`) only switches the
  camera to `BioCameraBehaviorGalaxy` (`:12841`, a `SFXCameraMode`), which holds the map: selectable actors (`m_pScaleformObjects`,
  `m_pDynamicObjects`, `m_pDynamicClusterPath`), a template planet (a `DynamicSMActor`), the cross-hair, centre and camera objects,
  relay lines with 3D positions (`FBioMassRelayLine`), the camera's zoom location and rotation, and the selection:
  `m_pSelectedObject/Cluster/System/Planet` (`:12856-12862`, `:12852-12855`). Its `Tick` is native. The labels, the planet panel and
  the buttons are a Scaleform movie (`BioSFHandler_GalaxyMap`, `:11427`; `BioSeqAct_ActivateGalaxyMap` names the movie).
- **No script function selects or travels.** `BioSFHandler_GalaxyMap.TravelConfirm(bAPressed, nContext)` is native final; the
  movie's commands come through `BioSFHandler.HandleEvent(nCommand, ...)` (constants Close 1, Select 3, Back 4, Scan 5, ClosePlanet 6
  [inferred: they sit with the map's other constants]). Kismet's `BioSeqAct_HighlightGalaxyMapObject` sets a highlighted world.
- **[inferred]** Three ways to make a pick: (a) the mouse: put the PC cursor on the system (what the game pointer already does on the
  flat quad); (b) write the camera behaviour's selection fields and send the movie's Select command; (c) `TravelConfirm(1, ...)` with
  the right context. Only (a) uses nothing but the game's own input path.

## 3. Ways to show it as a hologram (LE1)

| Stage | What the player sees | How | Cost and risk |
| --- | --- | --- | --- |
| A. Panel (today) | The flat full-screen map in the room; point and click with the controller | `galaxyFlat`, the game pointer | Nothing new; **verify the pointer on the map first** |
| B. Tabletop miniature | The map's own 3D scene in stereo, shrunk to a table-sized model in front of you, the labels and planet panel on a panel beside it | `galaxyFlat = 0`; while the game mode is 9, render the stereo pair at a larger units-per-metre (Halcyon's metric stereo, research/30) so the map reads as a miniature; anchor its camera to a fixed place in the room (head motion = parallax, as in gameplay); cut the Scaleform layer out of the eye images onto its own quad as the HUD panel does (research/15). Pointing: intersect the controller ray with the miniature, project the hit through the game's map camera to a window pixel, move the cursor there, the trigger clicks | One `[MEVR]` seam in Halcyon's galaxy branch; no extra scene render (the map scene is light); the Scaleform cut needs the map's UI crop like the wheel's |
| C. On the CIC's projector | You stay in the CIC; the map floats over the projector as a hologram | The CIC is the normal level; the map scene lives elsewhere. Two scene captures of the map (a stereo pair, research/47's capture, ~2 ms each at 512x288) drawn by the host as a per-eye quad over the projector, additive, with a hologram tint and scanlines; or (simpler, flat) one capture on a mesh at the projector | Two extra renders while the map is open; the game must keep rendering the CIC rather than switching the camera to the map (it switches today: the map is a camera mode). The hardest stage |

The recommendation: **A now** (one headset check, no code), **B as the real hologram** (most of the effect for one seam and our
existing pieces: metric stereo, the HUD-style cut, the pointer), C only if keeping the CIC around the map matters after B.

## 4. ME2's map and planet scanning (LE2; ME2 isn't on Halcyon's renderer yet)

- **The map.** `SFXGalaxy.Clusters -> SFXCluster.Systems -> SFXSystem.Planets -> BioPlanet` (`LE2/SFXGame_classes.hpp:5289`,
  `:5262`, `:5335`, `:5176`), each a `SFXGalaxyMapObject` with a tag, position and table ID (`:5141`). The camera behaviour
  (`:12222`) holds the current galaxy, cluster, system and planet, the selected actors, the ship's desired direction and speeds, the
  state (`EBioGalaxyMapState`: Galaxy 1, Cluster 2, System 3, Planet 4, PlanetScan 5) and **ready-made lists of the pickable targets
  with their 3D actors** (`m_SystemSelectors`, `m_ClusterSelectors`, `m_GalaxySelectors`: `FSFXGalaxyMapSelector`). The PC handler has
  `bMoveToMouse` (`BioSFHandler_PCGalaxyMap`, `:10219`): **[inferred]** on PC the ship already flies toward the mouse, so pointing can
  be the cursor again, or a ray against the selectors' actors plus the native `HandleSelectPlanet` / `TriggerEvent`.
- **Scanning.** `SFXGameModeOrbital` (`:24153`): `ScanResources`, `LaunchProbe`, `ProbeImpact`, `GiveResource(FeatureType,
  MineralType, Amount)` (script), `fResourceRetrievalFactor`. A planet's deposits are `BioPlanet.Features`, each a `SFXPlanetFeature`
  (`Amount`, `ScaledAmount`, `Position`, `FeatureType`: mineral 1, probes 3, artifact 4, landing site 5; `MineralType`: the four
  minerals). The player's totals are on `SFXInventoryManager` (`Iridium`, `Palladium`, `Platinum`, `Eezo`, `Probes`, `CurrentFuel`,
  `AdjustResource`).
- **Auto scan [inferred].** On entering a planet's orbit (state 4 or 5): for each mineral feature, `GiveResource(1, MineralType,
  ScaledAmount)` and a probe impact at its `Position` (`BioPlanet.SaveProbeImpact`): the save keeps only probe impacts and the visited
  flag (`FPlanetSaveRecord`), so the depletion must be recorded as impacts or it comes back on load. Artifacts and anomalies
  (`FeatureType` 4, the missions) are left to a probe, or revealed. Whether probes are spent, and how many, is a design choice (a
  mechanic change for docs/trilogy-design.md's change log). Fuel stays as it is.

## 5. Other full-screen moments to look at later

- The bypass (decryption) minigames: game mode 10, which Halcyon has no flat rule for, so they render in VR today
  (`docs/trilogy-design.md`: "Panel plus laser pointer" [verify]); Back is `SFXGameModeMinigame.ExitMinigame` (research/28).
- Elevators and load screens, the Mako's map (a menu, mode 7: the flat panel), ME2's hacking minigames, ME3's galaxy map and Reaper
  chase: not looked at.

## 6. Next steps

1. In the headset (or the fake headset with a replayed ray): open the map on the Normandy and check stage A, the pointer clicking a
   cluster, a system, a planet and Travel. If the cursor path doesn't reach Scaleform, that decides the pointing for every stage.
2. Stage B: read Halcyon's galaxy branch for the seam; measure the map scene's scale (its actors' bounds) to pick the miniature's units
   per metre; find the map UI's crop in the HUD capture.
3. ME2: when its renderer arrives, the auto scan from section 4 (unit-testable: which features, how much, which impacts).
