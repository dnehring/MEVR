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

- **A real 3D scene with a Flash layer on top. REFUTED in the game (section 7): the visible map is drawn independently of the
  scene view.** The game mode `SFXGameModeGalaxy` (`SFXGame_classes.hpp:30082`) only switches the
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

## 7. Stage B tried in the game: the map is not a 3D scene (2026-11-03 evening)

**Refuted: section 2's "a real 3D scene with a Flash layer on top" and section 1's "the map's own 3D scene renders in VR".** Built
as planned (commit 6b3305d, HostServices v25: `game/GalaxyMap` measures `BioCameraBehaviorGalaxy`'s camera and centre, 46,111
units apart, so 51,235 units a metre put the centre 0.9 m away; Halcyon's galaxy branch skips the flat panel and renders the pair at
that scale), then measured on the fake headset at the Normandy's map (`mevr used` on the CIC's `BioArtPlaceableBehavior`):

- **Zero disparity.** Left against right, block matching over ±80 px at the centre, the lower half, the stars and the title: best
  shift 0 px everywhere (mean difference 0.09 of 255). First with one Draw (sharePass 5 replays the first eye), then with
  Halcyon's second Draw forced on in mode 9 (`SfrModeAllowsReplay`, which keeps 7/8/9 mono, temporarily let 9 through): two scene
  renders a frame, 0.65 and 1.1 ms, the pair streamed to the eyes, and the log confirmed each pass moved by its eye's offset
  (`[MEVR-STEREO] pass 0 ... right -1613.89 uu`, 3,228 units between the eyes). At 46,111 units that is about 80 px of disparity.
  Measured: 0.
- **The view doesn't reach the picture.** Raising the view by 1.2 m (61,000 units, `[Galaxy] belowM` 0.30 -> 1.50) changed **no
  pixel** of the left eye (mean difference exactly 0.0).
- No `SceneCapture` component or `TextureRenderTarget` exists while the map is open (`mevr find`), so it is not a capture either.

So everything on LE1's map (the disc, the systems' rings, the axis lines, the labels, the stars) is drawn independently of the
scene view: by the Scaleform movie (`BioCameraBehaviorGalaxy.m_pScaleformObjects`), as far as we can tell. **Partly wrong
(section 8): at the cluster level the nebula, the axis lines and the rings are the map level's 3D scene; this measurement was at
the galaxy level.** The scene renders
under it, but contributes nothing visible. **No camera-based hologram (stage B or C) is possible.** What remains:

- **Stage A** (the flat panel, Halcyon's `galaxyFlat`) works: `[Galaxy] hologram = 0`, now the default and the deployed value. The
  pointer on it is still unverified in the headset (the fake headset's controllers aren't tracked).
- **A real hologram means drawing the map ourselves**: the clusters', systems' and planets' positions are in the 2DA tables
  (`BIOG_2DA_GalaxyMap_X.GalaxyMap_Cluster/System/Planet/Map/Relay`, loaded while the map is open), and a pick would go through the
  game's own path (the cursor on the hidden flat map, or the selection fields plus the movie's Select). A project of its own.

The hologram code (`core/vr/GalaxyMap`, `game/GalaxyMap`, the `xr_session` seams, HostServices v25) stays in, inert while
`hologram = 0`; the mode-9 replay gate and the one-Draw suspension it needed were not kept. Side finding the same evening: a crash at
`open BIOA_NOR00` from the title (the Mako controls' `runtime::IsAlive` read a freed pawn's index across the level change);
`runtime::IsAlive` now guards the read.

## 6. Next steps

1. In the headset (or the fake headset with a replayed ray): open the map on the Normandy and check stage A, the pointer clicking a
   cluster, a system, a planet and Travel. If the cursor path doesn't reach Scaleform, that decides the pointing for every stage.
2. Stage B: read Halcyon's galaxy branch for the seam; measure the map scene's scale (its actors' bounds) to pick the miniature's units
   per metre; find the map UI's crop in the HUD capture.
3. ME2: when its renderer arrives, the auto scan from section 4 (unit-testable: which features, how much, which impacts).

## 8. Our own hologram in the CIC, and what the map is made of (2026-11-03 night)

The user asked for a hologram of our own (section 7 ruled out the camera), then for one used where the game already shows its
own: walk up to the CIC's projector and pick from the hologram there, never a full-screen map. Built and tested on the fake
headset with replayed controller rays (`tools/make-galaxy-pick-recording.py`); commits 72d7839, ea03088, 4adb7af, 601a938.

**What the player gets ([Galaxy] `inWorld = 1`).** On foot within 900 units of the console, the galaxy from the tables
(`GalaxyMap_Cluster` X/Y, 630 units a step round (0.5, 0.5); visible by `VisibleConditional`/`CheckConditional` or the flags
the open map showed last, kept in `%LOCALAPPDATA%\MEVR\galaxy\visible.txt`; relay routes from `GalaxyMap_Relay`, codes / 10000 =
cluster rows) stands over the projector (`worldRadiusM` 1.4, `worldTiltDeg` 35), the game's own projector hologram
(`BIOA_NOR10_03_PLC` InterpActor_0 + Emitter_71) hidden meanwhile. A trigger pick opens the map behind the scenes
(`BioArtPlaceableBehavior.OnArtPlaceableUsed` on the console's behaviour, the GalaxyMap sequence's originator), the mod holds
the view at Shepard's (a `Camera.UpdateCamera` post-handler) and hides the map's Flash movie (`BioSFPanel.SetMovieVisibility`),
and once the map is up the pick goes on: the map's cross-hair actor is moved onto the object (`Actor.SetLocation`), the
behaviour's native tick selects the object nearest it, and a posted click opens it. Halcyon presents mode 9 (and the map's
opening and closing, mode 6) as the world on foot through `MevrBridge::GalaxyHologram`/`GalaxyHeld`. Walking 1.6 m past the
table's edge closes the map (`exitGalaxyMap`).

**The opening is a wait, not the animation.** The 2.5 s from the console's use to game mode 9 is `BioSeqAct_Delay_0` in the
GalaxyMap sequence (Duration 2.5), not the matinee `SeqAct_Interp_0` (2.51 s; PlayRate 10 changed nothing). With the delay at
0.05 s the map is up 0.2 s after the use and the picked cluster on the hologram 0.74 s after the trigger (was 3.1 s); the rest
is the map's own zoom into the cluster (0.5 s). The mod shortens it while `inWorld` is on and puts it back otherwise. Closing
runs 9 -> 0 -> 6 (about 0.4 s) -> 0.

**The travel question is the game's own box.** `BioSFHandler_GalaxyMap.m_bWaitingForMsgBox` is set while
`BioSFHandler_MessageBox` (a separate Flash movie, `Transient.BioSFHandler_MessageBox_0`) asks "Travel to another system?". In
the CIC the game's UI goes to the HUD panel, which then stands over the table; the game pointer clicks it. Two findings:
- **Messages posted from the Present callback were ignored** by the box; the same SetCursorPos + WM_MOUSEMOVE + WM_LBUTTONDOWN
  + WM_LBUTTONUP from any other thread were taken (both logged with `[Pointer] logMessages`, the only difference the posting
  thread). The pointer now posts from a worker thread (`postThread = 1`). Why the render thread's posts are dropped is
  [verify]; it likely affected the pointer on every flat menu, which had never been checked in a headset.
- **A held right trigger blocks the box's mouse click** (the controller's trigger is also the pad's RT): the pad's triggers
  are kept from the game while a ray is on the game's flat screen or the hologram.

**Correction to section 7: the cluster view is a 3D scene.** With the map's Flash movie hidden, Halcyon's flat panel still
showed the cluster level's nebula, the orange axis lines and the system's ring (snapshot 2026-11-03 23:41, quad0); only the
labels, the title and the panels went. The map level `BIOA_NOR10_03_GM_LAY` holds a StaticMeshComponent ring per selectable
object (`<id> Circle`) and the package `BIOA_GalaxyMap_T` (loaded on the Normandy) holds the art: `galaxy`, `galaxy01`,
`Cluster01`..`Cluster20` (the clusters' nebulae), `Circle`, `GalaxyGuiProxy`, and the materials `Galaxy_Holo_Mat`,
`Galaxy_Holo_GUI_Mat`, `Glow_Mat`, `CloudMask_Mat`. `GUI_SF_GalaxyMap` holds the movie and the picture tiles `GalaxyMap_G0`..`GF`,
`_IAD`, `_I70`. Section 7's zero disparity was measured at the galaxy level, whose picture may be the movie's tiles [verify which
levels are scene and which Flash]. The cluster table's `CloudTile`, `SphereIntensity`, `SphereSize`, `NebularDensity`,
`Colour`/`Colour2` and the planet table's material colours are the parameters of that scene.

**Next (the user, 2026-11-03: "it looked like the actual game instead of us trying to re-create the experience"):** show the
game's own map in the CIC instead of our drawing. For the 3D levels, render the map level's scene in stereo over the table: a
pair of scene captures from the map camera (research/39's `SceneCapture2DActor` and the host's texture lookup), placed as one
stereo layer at the table, the map's real nebula planes, rings and axes at their own depths; the Flash layer (labels, title,
the planet panel) on the UI panel or cut into pieces. For a Flash level, the movie's own drawing on a panel over the table.
Picks then go through the game's own cursor (the pointer on the captured picture, projected through the map camera) rather
than our hologram's objects.

### 8.1 The game's own map over the table (2026-11-04)

The user: keep the game's own map, its art and its stacked layers, rather than our drawing. With the map open in the CIC
(`[Galaxy] window = 1`) the mod runs two scene captures (`game/SceneCaptureRig`, the spectator's recipe) from the game's own map
camera, side by side as the 3D cinema screen does (core/vr/CineScreen: parallel cameras, the map's centre on the window's plane,
infinity `windowDepth` of the width behind), and the host shows each eye its capture on one quad over the table (`windowWidthM`
2.0 m), with the game's UI (its Flash map: the title, the labels, the travel box) on the panel at the same place. The cluster
level's nebula, axes and rings come out as the game draws them, in stereo; the controller ray on the window is the game's cursor
(a click on Newton selected it and brought up the game's travel question).

- **The view is held at render time only.** Writing the held view into `CameraCache.POV` after `Camera.UpdateCamera` hid the
  map's Flash labels (they project through the player camera; "Newton" appeared once the POV was left alone). The host now swaps
  the held view into the POV around the main view's `CalcSceneView` alone (a `[MEVR]` seam in Halcyon's hook,
  `MevrBridge::GalaxyViewSwapBegin/End`, the POV's addresses from the mod) and puts the map camera back: the game keeps its map
  camera for everything else. The table's place is frozen while the view is held (the player camera no longer says where the
  CIC is).
- **The capture targets' texture search.** `scope_capture_source::FindTargetTexture` missed the render targets' textures (the
  scope capture's too, research/39 section 11): the texture sits at `resource +0xA0 -> +0x8`, inside the search's window, but the
  pointer at +0xA0 isn't 8-aligned and `sc::FindTexture`'s walk skips unaligned pointers. A fallback walks the same window without
  the alignment test and accepts only objects with the vtable of a texture the host made on the game's device (`NoteDevice`),
  asked nothing but GetDesc and AddRef. The window's targets are found at once.
- Open: the game's travel box ignores the pointer's click in this mode while the console's identical press answers it [verify];
  the galaxy level (whether its picture is Flash or scene in the window) and going back out a level; the on-foot hologram is
  still ours (the plan: the game's projector hologram as the visual, our picks on it).
