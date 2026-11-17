<!-- SPDX-License-Identifier: MIT -->
# One seam vocabulary for the three renderers

2026-11-16, lane U1 of the trilogy unification. Sources: the code as of this note's commit (`src/halcyon/MevrSeams.h`,
the three `src/halcyon/le*/mevr_bridge.h`, `src/plugin/Seams.cpp`, the three bridges, `src/plugin/Renderer.hpp`) and
`docs/trilogy-parity-ledger.md` "Seams", whose generated table lists which tree calls what.

## What a seam is

Halcyon's renderers (`src/halcyon/le1`, `le2`, `le3`: static libraries, GPL-3.0) call into our host (`src/plugin`) at
marked points (`[MEVR]`). Each such call is a **seam**: a free function declared in a header the renderer includes and
defined in the host. One host ASI links exactly one renderer, so each seam has one definition per ASI.

Until this lane each tree had its own list (`MELEVR::MevrBridge` with 111 functions, `ME2VR::MevrBridge` with 32 and 29).
They shared 18 names, and of those six differed in signature, so no host module could be written against them once.

## The rule now

- A seam that more than one renderer has (or should have) is declared **once**, in `src/halcyon/MevrSeams.h`, namespace
  `MevrSeams`, with one signature. Every tree calls it as `MevrSeams::X(...)`.
- A seam only one renderer has stays in that tree's `mevr_bridge.h`, in that tree's `MevrBridge` namespace
  (`MELEVR::MevrBridge` for LE1, `ME2VR::MevrBridge` for LE2 and for LE3, which is upstream's ME2 code renamed), and gets a
  capability bit in `plugin/Renderer.hpp` if a shared host module needs to know about it.
- The host implements a common seam **once for all games** in `src/plugin/Seams.cpp` when the answer is the same
  everywhere; otherwise in that game's bridge, in `namespace MevrSeams { ... }`.

## The common seams (41)

Signatures are in `src/halcyon/MevrSeams.h`; that header is the reference, with the thread each runs on. "Host" names
where each game's definition is. `Seams.cpp` means one definition for all three.

| group | seam | LE1 host | LE2 host | LE3 host | called by |
|---|---|---|---|---|---|
| session | `StaticXrGetInstanceProcAddr()` | HalcyonBridge.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| session | `WrapSessionFunctions(acquire, waitImage, release, waitFrame, beginFrame, endFrame, locateViews)` | HalcyonBridge.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| session | `OnSessionCreated(instance, session)` | HalcyonBridge.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| session | `OnSessionStopped()` | HalcyonOverlays.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| session | `OnFrameBegun(session, space, predictedDisplayTime)` | HalcyonBridge.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| session | `OnRecentered()` | HalcyonOverlays.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| session | `XrRuntimeIsSteamVr()` | Seams.cpp | Seams.cpp | Seams.cpp | LE2, LE3 |
| view | `OnViewBuilt(worldView, headLook, headAim, lookYawUU, lookPitchUU, headOrientation[4], headPosition[3], offsetRightUU, offsetUpUU, offsetForwardUU, unitsPerMeter, floorWorld)` | HalcyonBridge.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| view | `FloorViewTranslation(replay, rawCameraZ, rightZ, upZ, forwardZ, right*, up*, forward*)` | FloorLock.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| view | `RecenterOrientation(head[4], automatic, orientation[4])` | RecenterTilt.cpp | RecenterTilt.cpp | RecenterTilt.cpp | all |
| view | `PlanStereo(StereoPlanInput, StereoPlan*)` (moved from LE1's mevr_bridge.h, 2026-11-16, lane LS) | MetricStereo.cpp | MetricStereo.cpp | MetricStereo.cpp | all |
| view | `NoteStereoSubmit(mode, declaredPosition[2][3], declaredOrientation[2][4], declaredFov[2][4])` (moved likewise) | MetricStereo.cpp | MetricStereo.cpp | MetricStereo.cpp | all |
| view | `LifeSizeStereo()` returns int: life-size on (bit 0), the window widened to hold the eyes (bit 1, on with bit 0) | MetricStereo.cpp | MetricStereo.cpp | MetricStereo.cpp | LE2, LE3 |
| eye image | `EyeImageSize(sourceW, sourceH, recommendedW, recommendedH, &w, &h)` | - | Seams.cpp | Seams.cpp | LE2, LE3 |
| eye image | `OnEyeSource(eye, source)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `CopyIntoImage(context, image, source, shiftX, eye)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `OnEyeCopied(eye, image, index, acquired, waited)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `OnEyeReleased(eye, published)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `OnFactory(factory)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| composition | `OverlayLayers(OverlayFrame, layersOut, capacity)` | HalcyonOverlays.cpp | SequelOverlays.cpp | SequelOverlays.cpp | all |
| composition | `HudPanelDraw(context, draw, user, textBatch, afterLeftEye)` | - | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | LE2, LE3 |
| composition | `CineScreenWanted(conversation)` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| composition | `CineScreenFullRate()` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| composition | `CineScreenLayers(CineScreenFrame, layersOut, capacity)` | - | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | LE2, LE3 |
| composition | `TransitionFadeLayer(session, viewSpace, context, view)` | TransitionFade.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| composition | `PlacePanel(kind, OverlayFrame, viewPosition[3], widthM, heightM, imageReady)` returns `PanelPose` | WorldPanels.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| composition | `WorldUiOwnsGui(mode)`: a game screen of ours stands in the GUI mode, which then stays in stereo (moved from ME2's mevr_bridge.h, 2026-11-16, lane OM2) | - | MenusLe2.cpp | MenusLe2.cpp | LE2, LE3 |
| view | `WorldViewHoldBegin()`, `WorldViewHoldEnd()`: game thread, around the primary player's view build in CalcSceneView: Shepard's view on foot in the camera's POV while a game screen of ours stands (added 2026-11-17, lane OM3; LE1's `GalaxyViewSwapBegin/End` is the same contract and calls the same hold for its world UI) | WorldUi.cpp | WorldUi.cpp | WorldUi.cpp | LE3 |
| view | `WorldViewHeld(mode)` returns int: 0 no hold, 1 a wrist screen's (first person's hides kept), 2 a room screen's (the body hidden too) (added 2026-11-17, lane OM3) | WorldUi.cpp | WorldUi.cpp | WorldUi.cpp | LE3 |
| input | `MergeMotionControllers(index, state, result)` | HalcyonBridge.cpp | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | all |
| input | `KeyDown(vk)` | Seams.cpp | Seams.cpp | Seams.cpp | all (LE2 through its inline `HotkeyDown`) |
| input | `OnMenuKey(ownMenuOpen)` returns bool | Seams.cpp | Seams.cpp | Seams.cpp | all |
| input | `MainMenuOpen()` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| threads | `OnGameThread()` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| threads | `QueueProcessEvent(object, function, params, size)` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| perf (optional) | `PerfPresentBegin(context)`, `PerfPresentEnd(context)`, `PerfFirstEyeCaptured(context)`, `PerfDraw()` | - | HalcyonSequelBridge.cpp | HalcyonSequelBridge.cpp | LE2, LE3 |
| perf (optional) | `PerEyeViewState()` (game thread, the SFR branch of CalcSceneView: each pass its own scene view state, or both the game's one; `[Performance] perEyeViewState`, research/sequel-render-levers.md "Performance research") | - (LE1's tree asks `MELEVR::MevrBridge::PerEyeViewState`, the same lever) | RenderLevers.cpp | RenderLevers.cpp | LE2, LE3 |

A `-` means that game's host does not define the seam, because its renderer does not call it. Calling it there is a
link error, which is the intended signal: define it in that game's bridge first.

Added 2026-11-16 by lane EY (research/27 section 9): `EyeImageSize`, which the sequels' `EnsureAerSwapchains` asks for
the eye images' size, so ME1's FSR upscale in `CopyIntoImage` has something to scale to. LE1 still calls none of the
eye-image group: its blit keeps its own `EyeColour*`/`EyeUpscale` seams onto the same shared core (section 9.3 says why).

### What changed against the audit's list of 26, and why

The ledger's audit recommended 26 functions. The code showed four places where it was off:

1. **`OnViewBuilt`.** The audit said LE1 passes a head quaternion and the sequels pass yaw and pitch. The sequels pass
   both (their signature is a superset). The one signature is the sequels': LE1's tree now passes
   `RenderHook::HeadLookYawUU()` and `HeadLookPitchUU()`, which its host used to read itself at the same moment on the same
   thread.
2. **`WrapSessionFunctions`.** The audit said five pointers (four on LE3). LE1 and LE2 pass seven; the one signature has
   seven, and a renderer passes `nullptr` for a slot it does not route through a pointer.
3. **`MainMenuOpen` and `CineScreenFullRate` added (28).** Both were already in all three trees with the same signature
   and the same body; leaving them out would have kept three declarations of each.
4. **A perf group added (32)**, at lane PL's request: the frame-breakdown probe's steps on the sequels. Optional, behind
   capability `perfSeams`.
5. **Life-size stereo's plan moved in (37 with lane EY's `EyeImageSize` and lane SP's `PerEyeViewState`)**, lane LS (2026-11-16, research/30 section 9): `PlanStereo` and
   `NoteStereoSubmit` with their structs, moved from LE1's `mevr_bridge.h` unchanged but for one field
   (`StereoPlanInput::windowHoldsEyes`, default true: LE1's window is the eyes' union), and `LifeSizeStereo` for the
   sequels' baselines. The render-side edits of the sequels are one header both trees include
   (`src/halcyon/MevrMetricView.h`), not a seam: they run inside the renderer on its own FSceneView.

Three signatures needed a decision the audit did not make:

- **`OnMenuKey(bool ownMenuOpen)` returns bool** (the sequels' form). LE1's was `void OnMenuKey()` and toggled Halcyon's
  menu inside the host; now the host answers "ours took it" and LE1's tree toggles its own menu on false, as upstream's
  line did. Same behaviour, and the body is the same for every game.
- **`QueueProcessEvent(..., size)`.** LE1's callers do not know the parameter block's size; they pass
  `MevrSeams::kFunctionParmsSize` and the host reads the function's own `ParmsSize`, as before.
- **`PlacePanel(kind, frame, viewPosition, widthM, heightM, imageReady)` returns `PanelPose`.** It replaces LE1's
  `PlacePanel`, ME2's `PlaceGalaxyPanel` and ME3's `PlaceNativePanel`. `PanelPose::placed` false means the renderer's own
  placement stands (ME2 outside the galaxy table). `PanelKind::ByGameMode` lets the host pick the kind from
  `frame.gameMode` (the sequels have one native panel for every mode). `nativeMap` is ME3's old return value.

`CopyIntoImage` takes the eye from the caller on ME2 too (ME3's form). ME2's host used to infer it from the source
`OnEyeSource` last named; the two agree except on a frame whose eye acquire failed, where the flat panel's copy could
have used an eye's dither pattern.

## The frame struct

`MevrSeams::OverlayFrame` is the one frame type (there were two). It is the union of both: `viewSpace`, `eyesValid` and
the valid flags from the sequels', `floatWhiteScale`, `eyeOrientation`, `pairArm` and the `screen*` fields from LE1's.
A renderer leaves what it does not have at the default. LE1's `sfr2` is `stereo`. `MevrSeams::CineScreenFrame` is
unchanged. `src/halcyon/SequelPresentation.h` only includes `MevrSeams.h` now.

## Capabilities

`mevr::host::renderer::Capabilities()` (`plugin/Renderer.hpp`) returns a `Caps` struct of bools that each
`plugin/RendererLeN.cpp` fills. A shared host module asks it instead of `#if defined(MEVR_WITH_HALCYON_LEx)`.

| bit | LE1 | LE2 | LE3 | meaning |
|---|---|---|---|---|
| `eyeImageSeams` | - | yes | yes | the eye-image group is called; `plugin/EyeCopy` owns each eye image |
| `hudPanelDraw` | - | yes | yes | `MevrSeams::HudPanelDraw` from the draw hooks (LE1 has its own `HudPanel*` seams) |
| `hostCineScreen` | - | yes | yes | the host composes the 3D cinema screen (`CineScreenLayers`) |
| `recenterTilt` | yes | yes | yes | the recenter asks `RecenterOrientation` |
| `panelsInRoom` | yes | - | yes | `PlacePanel` places every flat panel |
| `galaxyTable` | - | yes | yes | `PlacePanel` may put the native map on the table |
| `perfSeams` | yes | yes | yes | the probe's steps are called |
| `oneDraw`, `perEyeFrustum`, `metricStereo` | yes | - | - | LE1's stereo and performance work |
| `hudPanelGameplay` | yes | yes | - | the whole gameplay HUD goes to the panel |
| `hudPieces` | yes | yes | - | the wrist and gun pieces |
| `worldUi` | yes | yes | - | GUI screens can stand in the room |
| `galaxyWindow`, `frontEnd`, `conversationWorld`, `eyeColourInBlit`, `headArmSequence` | yes | - | - | LE1's extensions |
| `screenReticleHide` | yes | yes | - | the game's crosshair can be hidden |
| `floorViewJournal` | yes | yes | - | `JournalFloorViewWrite` |
| `ownMenuRows` | - | yes | - | `GetMenuRows` answers |

Used so far: `ModLoader.cpp`'s `WithRenderer` (the `hideScreenReticle` service only where `screenReticleHide`) and
`WithHudPieces` (`hudPieces`). `BuildServices` still has `#if`s for modules that are not linked into a host at all; a
bit replaces an `#if` only when the code under it compiles in every host.

## How to add a seam

1. **Decide where it lives.** If a second renderer has it or will, it is common: declare it in
   `src/halcyon/MevrSeams.h` in the matching group, with the thread and what a renderer without the data passes. If it is
   one renderer's detail, declare it in that tree's `mevr_bridge.h`.
2. **One signature.** No game-specific parameters. Handles cross as `void*` (the renderer's `xr_types.h` and the OpenXR
   SDK define the same names, so neither header may include the other's). Plain structs and arrays only; no engine
   object pointers except as opaque `void*`.
3. **Define it in the host.** Same answer for every game: `src/plugin/Seams.cpp`. Otherwise in each calling game's
   bridge inside `namespace MevrSeams { }`. A game whose renderer does not call it needs no definition.
4. **Call it** from the renderer as `MevrSeams::X(...)`, the line marked `[MEVR]`, inside `#if defined(MEVR_ASI)` where
   the tree keeps upstream's build working (LE2 does).
5. **Capability.** If a shared module must know whether the renderer calls it, add a bool to `Caps` in
   `plugin/Renderer.hpp` (default false) and set it in the `RendererLeN.cpp` of each renderer that does.
6. `python tools/parity-ledger.py --write`, then reread the ledger's Seams section. ctest `mevr-parity-ledger` fails
   until that is done.
7. Build all three (`build-trilogy`, `build`, `build-mono`): a seam declared but defined in only some hosts fails at
   link in the host that calls it.

To move a per-renderer seam into the common set (a second renderer takes it): move the declaration from the tree's
`mevr_bridge.h` into `MevrSeams.h` unchanged, change the namespace of the host's definition, and rename the call sites.
The one-Draw seams (`g_oneDrawStereo`, `RecordPass1View`, `FindPass1View`, `StereoFrameActive`, `Record*`) are reserved
for that.

## One sequel bridge

2026-11-16, lane U2. Until then `plugin/HalcyonLe2Bridge.cpp` (778 lines) and `plugin/HalcyonLe3Bridge.cpp` (565 lines)
each defined every common seam. Now:

- `plugin/HalcyonSequelBridge.cpp` defines the common seams once for both sequels: `StaticXrGetInstanceProcAddr`,
  `WrapSessionFunctions`, the perf group, `OnSessionCreated`, `OnSessionStopped`, `OnFrameBegun`, `OnViewBuilt`,
  `FloorViewTranslation`, `OnRecentered`, `CineScreenLayers`, `TransitionFadeLayer`, `HudPanelDraw`,
  `MergeMotionControllers`, and each tree's `ME2VR::MevrBridge::CineScreenHalfSeparationUU` (the same in both).
- `plugin/SequelOverlays.cpp` defines `OverlayLayers` once for both ("One overlay composition" below).
- `plugin/HalcyonSequelBridge.hpp` declares the **adapter** each game implements, namespace
  `mevr::host::sequel_bridge::game`, in its own file (`HalcyonLe2Bridge.cpp`, 408 lines; `HalcyonLe3Bridge.cpp`, 389
  lines). A game's file also keeps its `PlacePanel`, the eye-image seams (lane EY's, bodies untouched) and that
  renderer's own seams (`JournalFloorViewWrite`, `OnSwapChainPresent` on ME2; `OnPresent` on ME3).

**Rule: a fix or a feature on the host's side of a common seam goes in the shared file.** A new per-game fact goes in
the adapter: a bool in `game::Facts` when it is a yes/no about the game, a function when the game must do something.

The adapter, and what each game does in it:

| adapter | ME2 | ME3 |
|---|---|---|
| `Get()` (`Facts`) | log prefix `halcyon le2 bridge`; see the facts below | `halcyon le3 bridge` |
| `SessionCreated` | forgets the galaxy table, its pointer surface and the galaxy trigger latch | resets the galaxy table and the native map's input lease, the spoken-reply context, the traversal presentation |
| `SessionStopped` | nothing | the same resets as at creation, and the native panel's pointer flags |
| `FrameBegun` | nothing | the galaxy table's session and space identity, `gMapTracked` (focused and tracked), the spoken-reply panel's focus |
| `FramePublished` | nothing | `host_view::Reset()` off foot or under Halcyon's menu (those paths do not build a view) |
| `Recentered` | forgets the galaxy table | resets the galaxy table and the native map's input lease, the spoken-reply context, the traversal presentation (its own state; until lane U3 also the shared drops, "One recenter rule") |
| `PadKeeps` | false | the native map is up, or a traversal presentation runs |
| `PadOrbitalTriggers` | the orbital scanner's mode | both map modes while the session owns the map |
| `PadMerged` | the pull that picked a planet is kept from the scanner until let go (`gGalaxyTriggerLatch`) | the native map's input lease (`NativeMapInputLease::Apply`) |
| `PadFiltered` | nothing | the traversal presentation turns the left stick; a cinematic gets no left stick |
| `UiDraw` | the UI draws' order for the log (`UiTrace`) | nothing |
| `Overlay*` | see below | see below |

`Facts`: `invalidViewAtOnce` (ME3 yes: `OnViewBuilt` publishes a view that is not the world's without waiting for the
floor lock's 100 ms of quiet; ME2 waits), and two for the composition (below). Lane U3 removed three (2026-11-16: "One
overlay composition", "ME3 as ME1").

**What changed in behaviour, by game.** The bodies were merged, not rewritten; where the two differed, the choice is
listed here so a regression can be traced.

- *Session created and stopped: the superset for both.* ME3's bridge reset more than ME2's. ME2 now also, at a new
  session: resets the interaction label, re-places the world panels, resets the floor lock and the pad merge's latches,
  and publishes "no controllers" until the first sync. At a stopped session ME2 now also: forgets the spoken-reply
  panel, the HUD pieces, the vignette and the traversal cue (their swapchains died with the session), closes our menu
  and the calibration (`menus_le2::OnSessionStopped`), drops the haptics, publishes "no controllers" and resets the pad
  merge. ME3 gains the two log lines (`... the session stopped`, `... first controller sync`).
- *`OnFrameBegun`: an inactive sample is always published* (ME3's way). ME2 returned early when the input was not
  ready (attach failed) and no replay stood in, which left the last replayed sample published. ME1 still returns early
  there (`HalcyonBridge.cpp`); the same latent case. Both clear this frame's cinema screen at the frame's start (ME2
  cleared it only at the end of `OverlayLayers`).
- *`OnRecentered`: ME1's list for both* (`recenter_tilt::Committed`, `xr_host::NoteRecentered`, the squad's layers, the
  HUD panel, the world panels, our menus, the spoken-reply panel). ME3 gains `hud_panel::Recentered()` (ME1 and ME2
  had it). ME3's extra drops stayed ME3's, in its adapter, until lane U3 removed them ("One recenter rule").
- *`MergeMotionControllers`: one skeleton.* The pad's lock is taken before the early return on both (ME2 took it
  after). The conversation triggers' dev log line (`conversation pad 0: LT .., RT ..`) is both games' now.
- *`WrapSessionFunctions`:* the log line names the renderer (`halcyon-le2`, `halcyon-le3`); ME3 had none.
- *Galaxy table settings:* one reader (`sequel_bridge::GalaxyTableSettings`), ME3's with the finite check.

Kept apart on purpose:

- **`PlacePanel`.** ME2 places only the native map on the table (`placed` false otherwise: its other panels are the
  renderer's head-locked ones; capability `panelsInRoom` false). ME3 places every native panel in the room through
  `plugin/WorldPanels`, the map on the table over that, and publishes a GUI panel as the pointer's surface. They also
  differ in what makes the table "ready" (ME2: the renderer's `imageReady`; ME3: its own session and space identity from
  `OnFrameBegun`) and in `nativeMap` (ME2: the table was placed; ME3: a map mode with an image). One function needs
  ME2's renderer to take `panelsInRoom`, which is a renderer change with its own check.
- **The native map's input.** ME2: the table is the game pointer's surface, plus a trigger latch between galaxy and
  orbit. ME3: no pointer; an input lease that passes the triggers to the native map while tracked and focused. Two
  designs (ledger Forks 3); the galaxy lanes own the choice.
- **The eye-image group** (`OnEyeSource`, `CopyIntoImage`, `OnEyeCopied`, `OnEyeReleased`, `OnFactory`): lane EY's.
  ME2's `OnEyeCopied` has a diagnostic (the copy's formats and counts each minute) ME3's lacks; otherwise identical.

## One overlay composition

`plugin/SequelOverlays.cpp` is `MevrSeams::OverlayLayers` for ME2 and ME3. One order, LE1's:

1. the comfort vignette;
2. the HUD panel (with the game's UI in front of the 3D cinema screen when that is up), then the interaction label,
   then the wrist and gun pieces;
3. the sights and the hand reticle (`SequelWeaponLayers`), and the game's crosshair hidden where the ring replaces it;
4. the traversal cue;
5. the power wheel, its hint and lock, the squad wheel;
6. the squad's placement arc and panel (`SequelSquadLayers`);
7. the spoken-reply panel (Thespian);
8. the game's own part (`game::OverlayGameLayers`);
9. our main menu, the calibration and the omni-tool's layers (`menus_le2::Layers`).

What a renderer cannot do is a capability (`plugin/Renderer.hpp`), read in the composition:

| capability | effect in `OverlayLayers` |
|---|---|
| `hudPanelGameplay` | the panel takes the whole gameplay HUD and follows the `hudPanel` setting; without it the panel is armed only for the interaction label's crop and is off under a menu |
| `hudPieces` | the wrist and gun pieces are planned, blitted before the panel clears and put on top; room for 4 layers is reserved instead of 2 |
| `worldUi` | `menus_le2::BeforePanel` and `PanelLayers`: a game screen of ours owns the panel and is shown at its place |
| `screenReticleHide` | `SetScreenReticleHidden` does something (it is called on every renderer; a no-op without it) |

What only a game knows is in its adapter:

| adapter | ME2 | ME3 |
|---|---|---|
| `Facts::panelColour` | yes: the panel's UI gets the colour correction, as LE1's | yes since lane EY (research/27 section 9) |
| `Facts::hudLayout` | ME2's bands | unused (no pieces) |
| `OverlayBegin` | releases the galaxy table's pointer surface when the table is gone | resets the map lease and the spoken-reply context on an unusable frame; releases a native panel's pointer surface |
| `OverlayLabel` | no screen of ours has the panel | the mod's label is visible and has a measured crop |
| `OverlayReticleMode` | the last readable game mode | 0 on foot, else -1 |
| `OverlayGameLayers` | the galaxy table as the pointer's surface | the galaxy window's two layers (`plugin/GalaxyHologram`) |
| `OverlaySpokenBlocked`, `OverlaySpokenContext` | never; nothing | the calibration or no input focus; the context's lease |
| `OverlayEnd` | nothing | publishes the native map's input lease |

What changed in behaviour: on ME2 the power wheel's module is called every frame (with `worldFrame` false off foot)
instead of only on foot, and the galaxy table's pointer surface is published after the squad's panel and the
spoken-reply panel instead of before them (neither touches the game pointer). On ME3 the composition also needs a
session (it ran without one before, which no layer can use), and the unusable-frame reset happens then too. Every
other gate is the one each game had, expressed once.

**LE1 is not on it.** `plugin/HalcyonOverlays.cpp`'s `OverlayLayers` (about 980 lines of the file's 1,805) composes the
same modules but reads Halcyon LE1's own state throughout, and has layers the sequels do not have yet. Before LE1 can
call the shared function, each of these needs a module or a capability of its own:

1. the HUD panel's inputs: LE1 arms and fills it through its own `HudPanel*` seams (targets, blends, the tint
   texture for non-"over" blend modes), not through `MevrSeams::HudPanelDraw`;
2. the game's wheel on its own quad (`core/vr/WheelCrop`, measured per session, `gWheelRecentered`);
3. LE1's own label quad (`gLabelSwapchain`, `LabelWanted`): it must become `HudPieceLayers::Interaction` with ME1's
   default crop (ledger Forks 1), which changes pixels and needs the same-pose check on its own;
4. the conversation-in-world subtitle and wheel quads, the Mako's radar on the cockpit display, the galaxy window and
   its early UI, the world UI panel and the wrist beam, the sights' release tap and window, the scope capture;
5. `menus_le2::Layers` against LE1's main menu, calibration, omni-tool rail and pad, which LE1 composes in
   `HalcyonOverlays.cpp` and `HalcyonBridge.cpp` directly (ledger Forks 13: `MenusLe2` renamed `Menus`).

The way there is one layer at a time: move a block of LE1's function into the shared one behind a capability, with an
LE1 same-pose pair per move. This lane did not start it: no line LE1 compiles changed.

### Lane U2's checks

**ME2** (fake headset, 2026-11-16 15:07, session `20261116-150654-945-LE2-u2-le2`, the merged bridge and composition):
`launch.ps1` adopted the game this time, `load.ps1` loaded Save 4, snapshot `2026-11-16_15-09-30-764` has both eyes
(4096x2304), the `hud` layer with the game's UI (the power bar, the door's prompt, the weapon panel, the squad) and the
source and copied eye images. `LE2MEVR.log` has the same lines as lane U1's reference run, in the same order: the session
functions wrapped (the line now names `halcyon-le2`), the actions attached, the first controller sync, the yaw-only
recenter, `recentered`, the first world view, the eye copies (4096x2304 format 28 into 27; 2,365 per eye a minute, none
refused), the title's cutscene on the 3D screen with its backdrop and the dips from black, the HUD panel's swapchain on
entering gameplay. No hand reticle in the picture: the fake headset has no tracked controllers and no replay ran.
`restore.ps1`: 0 differences. Not exercised: a stopped session, a recenter by the player, the galaxy table, the
conversation triggers, our menu over gameplay.

**ME3** (renderer preview on the fake headset, 2026-11-16 15:12, session `20261116-151138-682-LE3-u2-le3`): the
candidate host started, the session came up, `LE3MEVR.log` has the session functions wrapped (`halcyon-le3`), the actions
attached and the first controller sync (two lines ME3 did not have before), `mevr state` answered at the entry menu, and
snapshot `2026-11-16_15-13-21-312` shows the title on the native panel placed in the room (ME3's `PlacePanel`), the same in
both eyes. No save was loaded: the on-foot layers, the pad merge with tracked controllers, a recenter and the map lease
were not exercised on ME3. `restore.ps1`: 0 differences.

**The probe's first-eye mark on ME2** (lane PL's finding: `PerfFirstEyeCaptured` fired 61 draws before Present, after
895). It was called at Halcyon's pass-0 snapshot, which waits for the second clear of a pass's opening group and so lands
late in the second pass. It is now called at the first scene-sized clear after this present's first backbuffer draw
(`[MEVR]` in `src/halcyon/le2/d3d_capture.cpp`, `g_mevrFirstEyeMarked`), where the second pass opens. Measured at
Freedom's Progress (fake headset, `2026-11-16_15-27-50-u2-le2-probe.json`, 467 frames): 1,129 draws before the mark and
1,661 after (median; 3,038 a frame), 6.87 ms busy before and 4.27 ms after. Not half the draws: the second pass carries
the UI and the HUD's second batch; by time the mark splits the frame 62/38. The snapshot itself is unchanged.

**The probe on ME3** (lane PL's fallback: the device context taken from the projection layer, f1a1bcbb): at the title
(renderer preview, `20261116-152836-917-LE3-u2-le3`) the probe was armed (`perf probe: armed for 8 s`) and no capture
started within 60 s. The title submits only the native panel's quad, no projection layer, so the fallback had no context
to find there; it is untried in gameplay, which needs a loaded save.

**LE1**: no line LE1 compiles changed (the new files are in the sequel hosts' source lists only), so no same-pose pair.

## ME3 as ME1: the menu, the ring, the cue (lane U3)

2026-11-16. The owner: "Ideally I want everything consistent. Me3 I'd prefer other elements stay open if it helps with
immersion." Lane U2's merge had left facts only ME3 answered yes to, and ME3's own recenter drops ("One recenter rule"
below). Each was traced to the commit that made it, then removed unless ME3's game forces it:

| ME3's difference | where it came from | forced by ME3's game? | now |
|---|---|---|---|
| `menusHideGameplay`: our menu and the calibration hid the gameplay layers | `bc8d7922` (2026-11-15, "One submit owner. Suppress gameplay presentation under either menu or guided calibration"), `b4cecb55` the same for the calibration; written for the candidate host before the shared composition; no fault measured behind it | no | removed: the layers are given only the renderer's own menu, as on ME1 and ME2 |
| `weaponsNeedPublishedAim`: the ring only with a fresh valid aim ray | `bc8d7922`, research/le3-dialogue-cinema.md "Shared gameplay overlay candidate": "simply connecting a controller does not claim working weapon aim" | no: the mod reports hand aim only when its aim works (`src/mod/Mod.cpp`, `aiming`) and sends an aim ray every tick it does (`PublishAimRay`), so the ring was already on the mod's valid ray; the fact differed only for a host and mod that don't match (the ring then follows the right controller's aim pose) | removed; both sequels take LE1's `ControllerAimActive` (the mod's hand aim and the right controller's aim tracked within 1 s: `sequel_bridge::ControllerAimActive`), which ME2 lacked too |
| `cueOnFootOnly`: the traversal cue only in the on-foot mode | `62b84917` ("authored scenes own traversal") | no: `game/Le3Traversal` shows a cue only when its `Guard` passes (game mode 0, no authored seated scene, no menu) and a blink only through its own move outside authored scenes, as `game/Le2Traversal` does on ME2 | removed: the cue on any world frame, ME2's way |
| `panelColour` | lane EY's eye-image pipeline | no | yes on ME3 too since lane EY (both games now answer yes: the fact could go; EY's call) |
| `invalidViewAtOnce` | ME3's flat and menu paths stop calling the floor lock's pass without a last invalid view | yes (the renderer's own paths) | kept |

**What a player sees under our menu, on every game:** our menu, or the running calibration, is the only quad of ours while
it is drawn. `plugin/MenusLe2::Layers` (both sequels) and LE1's `HalcyonOverlays` drop everything composed before it
(`count = 0`; LE1's comment: "SteamVR may sort quads by distance"), and Halcyon's projection (the world) stays under it.
So the change adds nothing to ME3's picture under the menu; it changes what the modules are given there, as on ME1 and
ME2: the HUD panel stays armed (the game's UI no longer falls back into the eye images when the menu opens, where the
menu's frosted backdrop and, in a conversation, the 3D screen under the menu showed it), the galaxy window keeps its
captures (`plugin/GalaxyHologram`'s `Frame::menuOpen` is "Halcyon's menu (ours takes no frame here)"), and the sights and
the ring keep their state. Keeping our own quads (the HUD, the ring, the wheels) visible under our menu was put to the owner
(it would remove the two `count = 0` on all three games): **no, the owner (2026-11-16): they stay hidden under our menu on
all three games**, today's behaviour.

## One recenter rule

The same on all three games (lane U3, 2026-11-16). A recenter re-places what stands in the room (the HUD panel, the world
panels and the 3D cinema screen, the squad's panel, our menu, the calibration, the spoken-reply panel; LE1 also its wheel
quad) and raises the floor's recenter count (`xr_host::NoteRecentered`; the mod's body follow and Mako controls re-anchor on
it, ME3's traversal presentation expires on it). Nothing published is dropped. A game's adapter resets only its own state
placed in the old space: ME2 its galaxy table; ME3 its galaxy table and the native map's lease, the spoken-reply lease and
the traversal presentation. LE1: `HalcyonOverlays.cpp`'s `OnRecentered`; the sequels: `HalcyonSequelBridge.cpp`'s.

Until then ME3 also dropped the published controllers, the floor lock's views, the interaction label, the traversal cue and
the frame's cinema screen ("old app-space poses must not mix with the new head space"); ME1 and ME2 never did.

The decision, **inferred from the code and a host log line, not seen in a picture**:

1. **The frame order is the same in all three Halcyons.** `OnFrameBegun` samples the controllers in the app space; the
   recenter replaces the app space later in the same frame; the composition comes after it (LE1's `xr_session.cpp`
   `RunFrame`: `OnFrameBegun`, then `DoRecenter`, then `OverlayLayers`; ME2's and ME3's `me2_xr.cpp`: `OnFrameBegun`, then
   `RecenterAppSpace` at the top of the render path, then `CineScreenLayers` and `OverlayLayers`). So in a recenter's frame,
   on every game, poses sampled in the old space are composed in the new one, for that frame; what the mod published from
   them (the aim ray, the label, the cue, the HUD pieces) stays in the old space until its first tick after the next sample.
   ME3's drop removed neither: the ring, the label and the cue come from the mod's last tick.
2. **What the drop cost.** Until the next sample the mod read no controllers, which it takes as every held grip and trigger
   let go: `game/SquadWeapons` (`gTrigger = controllers.active && ...`; a let-go gives the highlighted weapon),
   `game/Throwables` (inactive controllers stand it down); the holsters keep a latch of their own for this. The sequels' recenter
   gesture holds the left grip (Back is the left menu button with the left grip), so the drop read as a let-go and then a
   new squeeze. The host's line below counts the engine ticks from the recenter to the first sample in the new space.
3. **The other drops did nothing the frame needs.** The cinema screen is cleared at `OnFrameBegun` and placed by
   `CineScreenLayers` after the recenter, so its reset was a no-op; the label and the cue are sent again by the mod every
   tick; the floor lock's views are paired with a floor sample whose recenter count changes with the same sample.

**Not measurable on the fake headset:** its head never moves and a replay's poses are app-space numbers, so a recenter there
is the identity, and no snapshot can show a frame in the old space. In a headset it is one frame (11 ms at 90 Hz) of the
hands' quads turned by the recenter's own turn, in the frame where the world turns by it too. ME1 has shipped with it and no
report names it.

The log line, once per recenter on both sequels (`HalcyonSequelBridge.cpp`, the first `OnFrameBegun` after a recenter):
`halcyon le<n> bridge: recenter: that frame's controllers had been sampled X ms before it, in the old app space (A), and its
layers went in the new one (B); the first sample in the new space came Y ms after it, N engine ticks later`.

### Lane U3's checks

On the fake headset, 2026-11-16, the merged branch (main `4c17807b` with lane EY's pipeline, plus this lane's commits), each
stretch restored with `verification: 0 differences`. Both runs replayed `make-walk-recording.py --plan
stand:10,back-button:0.12,stand:0.12,back-button:0.12,stand:25 --live-head --hands view` with its inputs (tracked hands, then
Back twice within 400 ms with the left grip held: the sequels' recenter), then a looping hands-only replay, our menu opened
and closed through `mainmenu.request`, and a snapshot at each step (the recordings and the script in
`%LOCALAPPDATA%\MEVR\u3-lane`).

**ME3** (renderer preview, Save 31 on the Normandy, session `20261116-173100-560-LE3-u3-le3`):
- the recenter: `halcyon le3 bridge: recenter: that frame's controllers had been sampled 0.2 ms before it, in the old app
  space (0x1349f290b10), and its layers went in the new one (0x1354a7e7080); the first sample in the new space came 19.2 ms
  after it, 1 engine tick later`. Under ME3's former rule that tick read no controllers (the Back gesture's held left grip
  let go and squeezed again).
- the ring by LE1's rule: `the right controller aims weapons: the hand reticle is on its aim` with the replay's hands,
  `controller aim stopped` when it ended. Snapshot `2026-11-16_17-35-38-801` has the `reticle` layer; the mod's aim ray was
  1,093 ms old there (the game thread stalled after the Back double tap; that snapshot took 25 s), so the ring stood on the
  right controller's aim pose: `plugin/HandReticle`'s fallback, the same on LE1 and ME2 ("Found" below).
- our menu over gameplay: snapshot `2026-11-16_17-36-22-137` has the world (Shepard on the Normandy's deck) in both eyes and
  the `mainmenu` layer alone of ours, as on ME1 and ME2; before (`17-36-07-831`) and after (`17-36-34-247`) the omni-tool's
  wrist target is there. ME3 shows no HUD panel in on-foot gameplay yet (capability `hudPanelGameplay` off), so the armed
  panel under the menu has nothing to show on this deck.

**ME2** (Save 4, Freedom's Progress, session `20261116-174137-798-LE2-u3-le2`; `launch.ps1` could not adopt the game, the
known two-process start: the shared notes' workaround, the save loaded with `mevr load`, the game stopped by its pid):
- the recenter: `halcyon le2 bridge: recenter: ... sampled 1.8 ms before it, in the old app space (0x18e4920d8b0), and its
  layers went in the new one (0x18e33c159c0); the first sample in the new space came 11.0 ms after it, 1 engine tick later`.
- snapshots: `2026-11-16_17-44-15-358` the HUD panel; `17-44-41-268` with hands: the HUD panel, the wrist piece, the gun's
  sight (the ring hides behind it, LE1's rule); `17-45-33-171` our menu alone over the world; `17-45-46-666` after it, the HUD
  panel and the wrist piece again. ME2's behaviour is unchanged except the ring's one-second tracking hold.

**LE1:** no line LE1 compiles changed in this lane (the files are in the sequel hosts' lists only), so no same-pose pair; its
frame order is read from `src/halcyon/le1/xr_session.cpp` (`OnFrameBegun`, then `DoRecenter`, then `OverlayLayers`).

**Found, and fixed for all three games:** `plugin/HandReticle` fell back to the right controller's aim pose whenever the
mod's aim ray was over 1 s old, also after the mod had published one: a stalled game thread in game mode 0 moved the ring
from the gun's line to the controller's for the stall, on all three games (seen on ME3 above; ME3's removed
`weaponsNeedPublishedAim` used to hide it). Since commit dd8e0ca8 (the coordinator's follow-up, 2026-11-16) the ring
reads the mod's last ray whatever its age (`aim_ray::Latest(UINT64_MAX)`): through a stall it stays where the mod last
aimed it, or stays hidden if the mod's last ray was no aim; the controller's aim pose is the fallback only before the mod's
first ray (`sequence == 0`: an older mod, or a host and mod that don't match). Log `overlays: the mod's aim ray stopped
coming ...` / `... comes again`.
- LE1 pair (sessions `2026-11-16_18-37-59-u3-before`, build at `ad0db88c`, and `2026-11-16_18-41-55-u3-after`, at `dd8e0ca8`;
  hands replayed with the right grip held, `mevr weapon pistol`, snapshots live, then with the world `pause`d): live, both
  have the same layers (`hud`, `label`, `wrist`, `gun`, `reticle`) and the same view offset, the ring 10.55 m out on the gun's
  line (a few centimetres apart, the pistol's idle sway, as between two frames of one run); paused, LE1's mod keeps sending
  a ray ("no aim" while paused), so no stall: no ring in either, and the HUD panel, both eyes and the wrist piece are
  identical pixel for pixel.
- ME3 (session `20261116-184546-351-LE3-u3-le3b`, Save 31, the same sequence): the Back double tap did not stall the game
  this time, so the stall case was not seen again; the ring stayed hidden on the mod's "no aim" (`overlays: reticle hidden
  (the mod reports no aim ray)`), the recenter line again 1 engine tick (14.7 ms), our menu alone over the world. The stall
  case itself is covered by the code: no age test is left between the mod's last ray and the ring.

## State

All five steps are made (2026-11-16): `MevrSeams.h`, the host's definitions, `Seams.cpp`, the capability set, all three
trees' call sites on `MevrSeams::`, and the old names (with `SequelPresentation.h`) removed. Both sequels' recenters ask `RecenterOrientation`: `plugin/RecenterTilt` and
`core/vr/Recenter` are in both sequel hosts, each bridge calls `recenter_tilt::Committed()` in `OnRecentered` and
`recenter_tilt::NoteHead()` each `OverlayLayers`, and a player's recenter is told from a render path's first frame by
`g_mevrRecenterAsked` in each `me2_xr.cpp` (set in `Recenter()`, taken in `RecenterAppSpace`); capability `recenterTilt`.

The perf group is wired on both sequels (after lane PL's `mevr::host::perf` landed, b3e91e4e): each sequel bridge
defines the four over `perf::SeamPresentBegin`, `SeamPresentEnd`, `SeamFirstEyeCaptured` and `NoteDraws`, and calls
`perf::WrapFrameFunctions(waitSwapchainImage, waitFrame, beginFrame, endFrame)` after testloop's wrap; each tree has
`MevrSeams::PerfPresentScope` at the top of its `PresentHook`, `PerfFirstEyeCaptured` after the SFR pass-0 capture
(`g_sfrPass0Caps.fetch_add` in `d3d_capture.cpp`) and `PerfDraw()` once per draw: in the inline `MhDrawIndexed`,
`MhDrawIndexedInstanced` and `MhDraw` hooks (the vtable hook's call lands there too) and in the vtable hooks of the
methods without an inline one (`DrawInstancedHook`, `DrawAutoHook`, the two indirect ones). Capability `perfSeams`.
Built, not run in the game. `testloop::WrapSessionFunctions` still wraps xrEndFrame for the probe on sequel builds
(lane PL's line, a no-op now).

Not yet:
- one more LE1 same-pose pair on a quiet machine, and an ME3 run ("Checks" below).

## Checks

Builds: `build-trilogy` (all three hosts), `build`, `build-mono` at every step; ctest at the first and the last.

**ME2** (fake headset, 2026-11-16 12:43, session `20261116-123754-740-LE2-u1-le2`, the build of the whole stack): the
session came up, Save 4 (Freedom's Progress) loaded, snapshot `2026-11-16_12-48-58-367` has both eyes (4096x2304, the
world without the HUD) and the `hud` layer with the game's UI (the power bar, the door's prompt, the weapon panel, the
squad), plus the source and copied eye images. `LE2MEVR.log` shows each seam group at work: the session functions
wrapped, our actions attached, the first controller sync, `[MEVR-RECENTER] yaw-only recenter (upstream), automatic` (the
recenter seam, new on ME2), `recentered`, the first world view, the eye copies counted (4096x2304 format 28 into format
27), and at the title the cutscene on the 3D screen with its backdrop and the transition fades.
`tools/le2-halcyon-run/launch.ps1` could not adopt the game that day (MassEffect2 started as two processes, the first
one gone before its path could be read: `Live process identity unavailable or changed; not adopted`), so the save was
loaded with `tools\console.ps1 "mevr load <career> Save_0004 4 0"` and the game stopped by hand inside the stretch;
`restore.ps1` verified 0 differences.

**ME3**: no run. The renderer preview (`-RendererPreview`) depends on the same adoption by `launch.ps1`, which failed
twice for ME2 that day. Evidence is the build (the LE3 host links with every common seam defined; a seam the tree calls
without a definition is a link error) and that the LE3 bridge's bodies are unchanged: the namespace, four signatures
and the `PlacePanel` wrapper around the untouched `PlaceNativePanel`. New on ME3 and unseen in the game: the recenter
seam (`RecenterOrientation` in `RecenterAppSpace`), the same lines as ME2's.

**LE1 same-pose** (fake headset, 2026-11-16; before = main `f67d10dd` built apart, session `2026-11-16_13-06-51-u1-before`;
after = this stack, session `2026-11-16_13-11-07-u1-after`; lane B's script: a standing replay, `pause`, two snapshots):
- `hostView` identical (offset -43.3 / -4.1 / -18.1 uu, 100 uu/m, head look 0/0);
- the same layers at the same places (`hud`, `wrist`, the eyes: 0.0 mm apart; the `label` follows an NPC and was 154 mm
  apart; the `omnitarget` 76 mm);
- the HUD panel identical pixel for pixel (0.00% of pixels differ, `13-10-45-317` against `13-15-19-251`);
- the eyes 1.5% and 0.9% of pixels apart by more than 32/255, all inside the lower left where the omni-tool's target
  ring was drawn in one snapshot and not the other.
That day's runs were noisy on the base build too: the mod lost and regained the head-tracked view 19 times in the
"before" run (main without this work) and 42 times in the "after" run, against once per run the day before (lane PL's
runs the same hour: 2 and 31). Each loss drops the arms, the ring and the label for a moment, which is what the two
snapshots differ by. So the comparison shows nothing this work changed, but it is weaker than a quiet day's
(0.1% of eye pixels): worth one more pair when the machine is idle.
