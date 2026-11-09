# 51: The galaxy map as a hologram, and ME2's planet scanning (2026-11-03)

The question (user, 2026-11-03): the game's full-screen experiences should be better in VR. First the galaxy map on the
Normandy: instead of a full-screen view steered with the left stick, show the map as a hologram over the CIC's projector and
travel by pointing and clicking. For ME2, planet scanning could become an automatic scan that takes a planet's resources at once.

The initial survey below reads our code and generated SDK headers (`external/LExSDKv2/Src/LESDK/LE1|LE2/`, class layouts and
function signatures). Later sections identify game measurements and native analysis separately. **[inferred]** marks what
the headers only suggest; the initial survey was not tried in the game.

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
- **A disc, not a rectangle** (the user, 2026-11-04: "it needs to be circular like the stock map when we approach the console").
  The captures have the window's aspect (a near-square, carried over from the full screen); with `[Galaxy] windowRound = 1` (the
  default) the host's window shader keeps a disc inscribed in the window, its edge faded over 5% of the diameter. The mask is in
  the quad's own coordinates, the same for both eyes, so the rim sits on the window's plane while the map shows behind it in
  depth. The game's Flash layer (the title, the cursor's cross-hair lines, the buttons) stays on its rectangular panel: its title
  and buttons sit outside the disc.
- **On the projector's hologram** (the user, 2026-11-04: the circle "needs to have the same angle and position as the initial
  state"). The projector's hologram is `BIOA_NOR10_03_PLC.InterpActor_0`, mesh `bioa_galaxymap_s.GalaxyMapProxy`: a square in
  its local XY plane with the galaxy painted as a disc, centred at (-5169, 14338, -36048), 59 units from the console's
  `BioUseable_0`. Its matinee (Physics 7, interpolating) spins it within its plane: two readings (rotation (-5092, 12827, 1715),
  then (-730, 1303, 5299)) give the same up axis, (0.00, 0.49, 0.87), 29 degrees off vertical, toward the console's side. Its
  component's bounds (half extents 376, 339, 205) over what the square's in-plane axes project on each world axis give a half
  side of 301 units (the X axis, which the thickness doesn't reach). With `[Galaxy] windowOnHologram = 1` (the default) the mod
  publishes that disc in tracking space (`Scene.windowDisc`, `discCentre`, `discNormal`, `discRadiusM`, read on foot and frozen
  with the table's anchor while the view is held) and the window is its diameter wide; the host lays the window and the game's
  UI panel on it (`galaxy_holo::DiscPose`: front toward the head, +X level, +Y up the slope), so the labels stay on the map's
  picture and the pointer's ray meets the same plane. Measured in the game: the window 6.02 x 6.19 m at 3.5 m ahead and 0.87 m
  below the eyes, facing (0.00, 0.87, 0.49), its centre (the map's cross-hair) where the stock disc's centre was. Seen from the
  console it is foreshortened as the stock hologram is (text on it too). The compositor's quads have no depth, so the disc's
  near edge draws over the console's front panel, which hides the stock hologram's [open].
- **The map's space as a backdrop** (the user, 2026-11-04: the full screen "had a background image ... so that is easier to see
  the available planets. We would still want a bit of transparency so it will look like it's a hologram"). The full screen's
  background is the map level's own starfield (black with stars, snapshot 2026-11-03 22:15 quad0), which the captures carry too,
  but the window's alpha was its brightness, so space's black let the CIC through: the railing's lights and the crew's orange
  consoles behind the projector ran across the map. The window shader's alpha is now at least `[Galaxy] windowBackdrop` (0.8 by
  default) inside the disc: the starfield's black dims the room behind it to a fifth, the bright galaxy still covers it fully, and
  the rim keeps its fade. Published in the Scene's former pad byte (`windowBackdrop`, percent), so the layout didn't change.
  Measured in the game (snapshot 2026-11-04 07:52): the room shows faintly through the map's space; 1.0 is the full screen's
  opaque look, 0 the old see-through one.
- Open: the game's travel box ignores the pointer's click in this mode while the console's identical press answers it [verify];
  the galaxy level (whether its picture is Flash or scene in the window) and going back out a level; the on-foot hologram is
  still ours (the plan: the game's projector hologram as the visual, our picks on it).

### 8.2 Label flicker and the exit transition (2026-11-07; initial flicker patch insufficient, follow-up below)

Owner: Quest 3, map opened at the CIC console; name tags flicker and exit stutters after the placement change.
Read the `2026-11-07_04-36-52-340` and `04-36-54-201` snapshot JSONs, HUD/window/left-eye PNGs. Both have labels in the HUD,
identical quad poses, crops and sizes (window 3.00892 x 3.36591 m; UI 3.06012 x 3.36697 m), and window-left/right then HUD
in submission order. **No evidence of alternating placement/crop/backdrop.** These are two samples, not a frame trace.
The quads have no shared depth attachment and UI submits last (`HalcyonOverlays::OverlayLayers`); coplanarity is not
a depth fight here. No geometry, crop, alpha, backdrop or windowScale change in this patch.

**Likely flicker mechanism, not yet proven by per-frame telemetry:** `d3d_capture::MevrHudPanelDraw` required
`SfrUiMirrorArmed` (capture armed AND pass 0 already copied). This is the eye-mirroring boundary, not UI ownership.
One Draw replays at scene finish (`RenderLevers::FinishHook`), while the engine draws Flash once; early Flash batches
fail the panel gate and stay in the eye target. The panel is cleared each Present, so missing batches cannot persist.
The patch admits early backbuffer UI only while the galaxy window's panel is armed; size/viewport/blend/stencil
classification remains, other render targets and other modes keep the old boundary. UI cannot be missed merely
because it preceded the eye copy, including during a fallback to Halcyon's second Draw. Captures render inside the
scene render, before the Present-thread panel/window blits; changing their submission order would not recover a lost draw.

**Exit defect confirmed by current logs (a later session than the snapshots):** LE1MEVR.log at 04:43:05.252 releases
the hold; .254 pauses the window, shows the projector and drops the scene; .269 reacquires the hold and hides the projector;
.274 places the table at (-2.78, 64.16, 29.05) m. MELEVR_Log.txt has mode 0 at .250, mode 6 at .272 and mode 0 at .659.
The intervening unheld tick permits camera/anchor resampling from the map's outgoing camera. Presentation also changes
from stereo mode 2 to the symmetric mode 1 at .296, back to mode 2 at .791. The previous host log additionally shows
fallback hologram swapchains being remade immediately after a window pause (04:41:22.878).
`ClosingHold` now bridges the initial mode 0 for at most 150 ms, holds mode 6 for at most 3 s, and releases on the final 0
(or another mode). Pause both captures as soon as mode 9 ends; `Rig::SetEnabled(false)` flushes its pending reattach
without waiting for a future Point. The host suppresses fallback hologram drawing/allocation during the transition.
No GPU/CPU trace of the individual exit frames was provided: residual stalls and the capture-disable cost remain **[verify]**.

**Diagnostic:** `galaxy UI:` every 5 s and on window exit, cumulative per opening: frames with/without redirected UI,
frames with/without non-indexed text batches and longest text gap, min/max draw count, and frames/UI/text draws captured
before the eye copy (these would have failed the old gate). Non-indexed batches are a glyph proxy, not proof that a
particular name passed stencil or was visible. Warm stable-map gaps are suspicious; opening/zoom/closing can differ.
If flicker persists with steady counts and zero early text, the eye-copy hypothesis is insufficient; inspect shader/stencil
coverage next, rather than moving the labels. Existing logs contain no per-frame label counts to decide this retrospectively.

Validation: added exit-sequence, direct-close, timeout, unrelated-mode and flat-map cases to `TestGalaxyMap`.
Ran that function with the core implementation in an isolated MSVC C++20 harness: passed under `/W4 /WX /permissive-`.
Syntax-checked the four changed game/host translation units under those warnings and the monolithic LE1 definitions;
the changed Halcyon translation unit passed its upstream warning setting. `git diff --check` passed.
Full builds, deployment and headset acceptance are left to the owner. In headset: hold still, pan and zoom through
galaxy/cluster/system, check names remain attached in both eyes, open/cancel Travel, then close/reopen repeatedly.
Expect one window pause, one final hold release, no reacquisition or fallback swapchain recreation during closing.

#### 8.2.1 Frame ownership after e134beb (2026-11-07 14:00 Quest retest)

The owner still sees flicker. Read the installed mod log and Halcyon log: at 14:01:00.725 the first opening totals
643 frames, 642 with UI/text, max 45 draws, and 321 early frames with exactly 642 UI / 642 non-indexed draws.
The next opening repeats the exact half-rate pattern (1553 frames, 776 early frames / 1552 early draws).
Single-frame snapshots with labels do not establish temporal completeness. **The prior inference that these two
draws were early Flash glyphs is unproven:** every non-indexed Draw was counted as text, including host/overlay draws.
The new PS `CB6E09E3` at 14:00:46.110 uses src/dst alpha 5/6, unlike the observed Flash family 5/7;
that is supporting evidence for a lookalike, not identification of its owner.

Code establishes the following order on the immediate-context/Present thread:

1. The game's scene and UI commands execute before its Present. In one-Draw mode, `RenderLevers::FinishHook`
   replays the scene tail inside scene finish; `HookFViewportClientDraw` enqueues capture instead of a second Draw.
   This is a stereo pair within one game frame, not alternating left/right Presents. The fallback can still enqueue
   a second Draw; none is driven from `RunFrame` or from the mirror throttle.
2. `d3d_capture::PresentHook` calls `SfrOnPresent`: capture pass 1, publish the pair, **reset pass-0-captured false**.
   Thus `SfrUiMirrorArmed == false` also means *already finished the game's UI, now inside Present*, not just *before
   this game's eye copy*. The e134beb early gate accepted either meaning while the galaxy panel was armed.
3. `XrSession::OnPresent -> RunFrame`: pace/wait/begin (the wait may be late/blocking on streamed Quest), eye copies,
   `OverlayLayers`: exchange the UI counters, blit panel to its swapchain, then clear colour/tint. Later XR work,
   `xrEndFrame`, and any optional next-frame wait/begin all occur before returning from `OnPresent`. Waiting does
   not run the next game render's immediate-context commands; deferred recording is excluded by the context gate.
4. Only **after all of that**, `mirrorPresentEvery` decides whether to return S_OK or call the downstream desktop
   Present. With every=2 the latter runs on alternate calls. Spectator mirror is on that latter path too (its own
   blit is opaque). A blended backbuffer draw made inside downstream Present met the old early-capture gate,
   landed **after the clear**, and polluted the *next* game's panel and counters. It was not lost between copy and
   clear. Those leftover pixels were combined with the next UI, while its 2 non-indexed draws appeared as "early
   text" in that next frame's statistic. This is a real ownership defect whether or not those batches are labels.

**Fix:** a small `[MEVR]` scope brackets the entire Present including `original(...)`. Galaxy-only lookalike
draws inside that scope are classified and counted but execute normally into their original target; they cannot
write the panel. The galaxy colour and tint clear moves to `HudPanelTarget`, immediately before the first game
UI draw after a retired panel. All subsequent batches accumulate until the next Present's copy. A frame with no
game UI retains the previous complete image. No extra texture, swapchain, GPU readback, or wait. On leaving the
galaxy window the old clear path cleans up; ordinary HUD, wheels, world screens and conversations keep their old
capture gate and clear-after-copy behavior. Present ownership is thread-local so recording on another thread does
not inherit its scope.

**Proof for the next owner run:** `galaxy UI order:` logs the first 16 frames of an opening, then adjacent parity
samples every 300 Presents. It runs *after downstream Present returns*. Fields: actual Present number/parity;
mirror 0=no desktop call, 1=throttled, 2=downstream Present; copied UI/text/early counts, start-of-UI clears and shown flag; rejected
UI/text candidates before copy, between copy and retirement, after retirement (remaining XR work/late wait and
spectator mirror), and inside downstream Present, plus first PS hash per phase. "Retirement" is the former clear
location, now the deferred-clear boundary. Rejected candidates never enter the copied counts.

Expected in a stationary warm map: same copied UI/text counts on both parities/mirror paths, start-clears=1, shown=true. If the
two batches came from downstream Present, `desktop 2/2` appears only on mirror=2, early drops to zero, and
copy-retire/after-retire stay 0/0. They would formerly have appeared in the next frame's early counter. Other
outcomes explicitly distinguish the alternatives: nonzero before-copy/copy-retire/after-retire locates reentry
inside XR; remaining early counts outside Present are actual game-render candidates. **The supplied aggregate
logs do not prove which phase or shader owns those two draws, nor that the panel's visible pixels alternate.**
That attribution and visible flicker resolution remain [verify] in the headset; do not report them as measured.
If copied counts still alternate or flicker persists with stable counts, capture consecutive panel images and
investigate actual glyph/stencil coverage instead of inferring visibility from the non-indexed proxy.

Validation: MSVC C++20 syntax checks passed for `HalcyonOverlays.cpp` under `/W4 /WX /permissive-` with monolithic
LE1/main-menu definitions and for `d3d_capture.cpp` at Halcyon's warning setting. `git diff --check` passed.
No build, deployment, runtime injection, or headset test performed; no commit. The owner builds and installs.

### 8.3 Tags projected through a VR-edited auxiliary view (2026-11-07)

Owner: Quest 3, 14:00-14:04, names near their objects but moving independently; setting `windowDepth = 0` did not help.
Inspected both `2026-11-07_14-00-56-615` and `14-00-58-449` left-eye/HUD/window PNG triplets and JSONs. The rings stay
nearly fixed in window UV; the leaders are substantially displaced. For example, Citadel's leader tip is near normalized
X 0.32 on the HUD while its ring is near 0.15 on the window. Identical layer poses, UI 4.264 x 4.691 m and window
4.212 x 4.692 m, cannot explain that offset. The 1.2% crop is far smaller. Two samples do not measure continuous motion.

**Code-confirmed cause: the tag projection gets the renderer's VR edits.** Native analysis of LE1 2.0.0.48602 with
`tools/ghidra` in an isolated, read-only copy of the local project established this chain (no game code copied here):

- The script entry is `SFXGame.BioCameraBehaviorGalaxy.Tick`, native thunk `+0xB95180`, dispatching virtual slot `+0x2D8`.
  The implementation at `+0xC51F20` occupies that slot and calls the label updater at `+0xC40B10`.
- That updater reads `m_pScaleformObjects` (`+0x17C`), projects each actor's Location and batches the resulting pixel
  coordinates to the GalaxyMap movie. Thus this calculation belongs to the camera behaviour's native update, rather
  than a separate script `BioSFHandler_GalaxyMap.Update` or a `Canvas.Project` call that calltrace could intercept.
- The player world-to-pixel helper at `+0xCBB830` calls `ULocalPlayer::CalcSceneView`, returning to **`+0xCBB9F0`**,
  projects through the returned view, then converts normalized coordinates to viewport pixels (Y inverted).
- Our main-view hold recognizes only `+0x4C7A4E`; the label helper does **not** receive Shepard's held POV. However,
  upstream `HookCalcSceneView` runs `PostProcessPerspectiveFsv` on **every** perspective view outside AER. In the owner's
  Stereo 2 session this includes the label helper: target FOV, metric eye/common translation, head rotation and asymmetric
  eye projection. The map captures are built separately from `gMapPov` and do not go through that hook.

This links hypotheses 1 and 3: the wrong *view matrices* reach the labels even though CameraCache was restored correctly.
The 14:00:56.742 `[RENDER_FOV_MATCH]` gives P00/P11 0.726543/0.700208; the galaxy `[XRDECL]` at 14:00:53.825 declares
108/110 degrees. A 72-degree horizontal capture has P00 1.376382 and P11 about 1.251 at 720x792. Horizontal offsets
are compressed to about 0.528 of the capture's, consistent with Citadel. Head rotation/translation on that same helper
also explains swimming while the captures stay on the map camera. XRDECL alone would not have proved the label path.

**Fix:** a narrow `[MEVR]` branch for `+0xCBB9F0` with a fresh, held in-world window. `GalaxyProjectionSwapBegin/End`
temporarily installs the capture centre POV, calls the engine's CalcSceneView and returns its native matrices without
`PostProcessPerspectiveFsv` or any of its global publications. RAII restores the live map POV immediately. Separate swap
storage leaves the main CIC hold intact. Flat maps and other screens keep their previous path. All new POV writes are journaled.

**Camera timing (hypothesis 2):** `plugin/Entry::GameEngineTickHook` calls the mod after the original engine tick;
`game/GalaxyMap::Tick -> StepWindow` sends SetView/SetFov/ForceUpdateComponents then publishes the scene. The next native
label update can see a newer live camera than that configuration. `StepWindow` now puts the exact capture centre POV in
`Scene.camera`, and the label helper uses that published configuration until the next Point/publication. This makes the
tag calculation and the configured captures agree through easing/zoom as well as when still. A temporal mismatch was not
measured in the supplied run; it is handled by camera ownership rather than guessed offsets or a fixed delay.

**Owner log to check: `galaxy projection:`**, every 5 s in LE1MEVR.log. It identifies `+0xCBB9F0`, the capture sequence,
`VR edits bypassed`, raw P00/P11 versus expected capture P00/P11, actual CalcSceneView-output location/rotation delta
against the capture centre, live-to-capture location/FOV difference before the swap, and restoration. For a 72-degree
720x792 capture expect approximately `raw P 1.376382/1.251256, capture P 1.376382/1.251256`, `view read true`,
`delta 0.000 uu / 0 rotator units`, including while moving the head. The client/capture aspect rounding can differ by
about 0.03%; large scale or view deltas indicate an additional native projection modifier and must not be called a pass.
`[XRDECL]` may still say 108/110: it describes the CIC's headset scene, which still needs that projection.

Validation: MSVC syntax checks passed for GamePointer.cpp and GalaxyMap.cpp under C++20 `/W4 /WX /permissive-` with
monolithic LE1/main-menu definitions, and render_hook.cpp under its C++17/upstream warning setting. An isolated harness
executed the actual extracted swap scope with real Scene Publish/Latest/Forget: disabled/incomplete scenes, capture versus
advanced live POV, rotation round trip, nested begin, next capture sequence and restoration all passed. The harness's
synthetic projection log reports zero pose delta and equal scales; this is not a measurement from the game.
`git diff --check` passed. No game build, install, injection, commit or headset test. The owner builds/installs; check
stationary tags, head turns/leans, zoom/selection through galaxy/cluster/system, travel and close/reopen. Keep the existing
window pose, size, crop, depth, backdrop and UI frame-ownership patch. Visible alignment and temporal acceptance remain [verify].

### 8.4 One table through travel and arrival (2026-11-08)

Owner's headset report: entering a system/planet became full screen; after a cinematic the map could not be used there
again. Sources: the owner's `LE1MEVR.log`, copied read-only into ignored scratch, and the source in this worktree.
No game launch, deployment, input, GPU test, binary disassembly or decompilation was performed for this investigation.

**Evidence (log line numbers in that copy):**

- 602-628, 22:08:11.398-.630: the CIC hold starts at (-5168, 14655, -35924) game units; table (-0.04, -0.91, -3.01) m;
  two 1920-square captures start. The head is 3.0 m from the table centre.
- 689-707, 22:08:22.146-25.021: cluster Argos Rho (level 2), then MessageBox at 23.234, then system Hydra (level 3)
  at 24.955. At 24.987 UI goes to the eyes, the host hologram says `down (not the galaxy map)` and window says
  `down (not the map's mode)`. At 25.020 the mod reads **mode 8**, the GalaxyMap panel is invisible; at 25.021 the
  captures pause, the view is released, and the mod says `down (the map closed)`. At 25.028 the 3D cinema screen appears.
- 748-780, 22:08:34.053 onward: **mode 9 is already back**, the same GalaxyMap panel becomes visible and the CIC hold
  restarts at the same correct world coordinates. But captures start then pause after **one tick**, repeatedly.
- 4479-4483: a mode-5 radio conversation starts at 22:09:00.056. 4552-4562, 22:09:04.146-.165: back at level 4,
  still the correct saved CIC camera, but the table is (-12.74, 32.88, -24.08) m, 27.2 m horizontally from the head.
- 5063-5067 and 5101, 22:11:03.379 and 22:11:12.616: the second opening directly at system Hydra, then its planet page,
  works on the table. The planet page has **zero selectable actors**. These levels are supported; a level-3/4 flat rule
  would misdiagnose the failure. There are no `[VRSTATE]` lines in the supplied mod log; those belong to Halcyon's log.

**Cause in code.** `Tick` only holds 9 and the bounded opening/closing 0/6 bridge. Travel's 8 falls through `Forget`:
it pauses captures, clears the publication and invalidates `gLastAnchor` because the hold is inactive. Returning 9 starts
`StepWindow` **before** `PlaceInWorld`. The latter asks `view_anchor::Current()` to rebuild the lost anchor. While the
renderer still presents the cinematic it can return nothing; `Forget` pauses the just-started captures before publishing
a scene. `MevrBridge::GalaxyHologram` then has no scene with which to veto `galaxyFlat`: the recovery depends on its own
successful publication. If Current does become available, it uses the actual player POV, which is still the **map/director
camera**, not the saved CIC view. It maps the console through the wrong frame and freezes that distant placement. This
explains both the start/pause loop and the 27.2 m result without claiming to reconstruct every rendered frame from this log.
The generic 100 ms plain-VR entry delay and the cinematic/movie flat-force can also carry the outgoing presentation into
the first returning map frame. `GalaxyHeld` previously had no game-mode bound, so stale publication could suppress the
start of an unrelated movie/conversation for a frame.

**Fix.** `game/GalaxyMap` records the tracking frame with the actual on-foot camera (using the existing camera-ownership
check), scoped to its world/controller by `core/vr/WorldViewHold`. On interruption it pauses the captures and publishes a
suspended table reservation; it does not teach an anchor from the cinematic. Returning 9 re-projects the console and its
disc from the saved CIC frame, independently of whether Halcyon has resumed head-tracked rendering yet. A different
world/controller invalidates the saved view, console and capture references. Closing still uses the existing bounded
0/6 bridge; a closed map cannot reserve a later unrelated cinematic.

Presentation is independent of capture readiness and the optional selectable-object model: all four levels retain the
window layout and Flash movie while targets warm up. `plugin/HalcyonOverlays` takes UI ownership from the current publication,
not last Present's hologram draw result; `plugin/GalaxyHologram` keeps its UI plane/width while the captures are pending.
`plugin/GamePointer` bounds the held-view swap to the table's modes; the small `[MEVR]` seams in `xr_session` let a returning
table beat the old movie/flat/cinematic decision and accept it immediately. `Scene.suspended` reuses a padding byte; no ABI
size change. The generic camera swap/restore now journals its writes too. No new reflection symbol was needed (Actor.WorldInfo
and the player/camera-ownership accessors already have ReflectCheck rows).

**Interactions.** Galaxy/cluster/system native cross-hair picks keep their existing pointer path. The level-4 planet
information/buttons and the travel question are Flash controls: they now receive the same GameMenu `GetCursorPos` + queued
`GuiKey` events as the world screens (research/57 section 6), including fingertip presses, without monitor-edge clamping.
The normal map camera remains untouched outside the existing render-time swap, including the auxiliary label projection.
No disc size, tilt, backdrop, depth, crop or previous label/frame-ownership fix was changed.

**Offline checks and remaining acceptance.** `tests/TableReturnTests.cpp` exercises all four levels (including an empty
planet model), travel/movie/radio return, first-frame suspended ownership, closing, disabled ownership, wrong-camera samples,
world/controller changes, Flash routing and the immediate presentation handoff. Registered in Main.cpp and CMakeLists.txt.
Build/test results are recorded below after completion. Headset acceptance is pending: follow the dated checklist line,
especially Travel -> movie/radio -> map, planet Land/Survey/Return and a direct system/planet reopening. Check one pause and
one restart per interruption and the same physical console placement, in both eyes. Log-based diagnosis is not visual QA.
