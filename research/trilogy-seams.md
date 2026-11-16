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

## The common seams (33)

Signatures are in `src/halcyon/MevrSeams.h`; that header is the reference, with the thread each runs on. "Host" names
where each game's definition is. `Seams.cpp` means one definition for all three.

| group | seam | LE1 host | LE2 host | LE3 host | called by |
|---|---|---|---|---|---|
| session | `StaticXrGetInstanceProcAddr()` | HalcyonBridge.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| session | `WrapSessionFunctions(acquire, waitImage, release, waitFrame, beginFrame, endFrame, locateViews)` | HalcyonBridge.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| session | `OnSessionCreated(instance, session)` | HalcyonBridge.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| session | `OnSessionStopped()` | HalcyonOverlays.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| session | `OnFrameBegun(session, space, predictedDisplayTime)` | HalcyonBridge.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| session | `OnRecentered()` | HalcyonOverlays.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| session | `XrRuntimeIsSteamVr()` | Seams.cpp | Seams.cpp | Seams.cpp | LE2, LE3 |
| view | `OnViewBuilt(worldView, headLook, headAim, lookYawUU, lookPitchUU, headOrientation[4], headPosition[3], offsetRightUU, offsetUpUU, offsetForwardUU, unitsPerMeter, floorWorld)` | HalcyonBridge.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| view | `FloorViewTranslation(replay, rawCameraZ, rightZ, upZ, forwardZ, right*, up*, forward*)` | FloorLock.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| view | `RecenterOrientation(head[4], automatic, orientation[4])` | RecenterTilt.cpp | RecenterTilt.cpp | RecenterTilt.cpp | all |
| eye image | `OnEyeSource(eye, source)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `CopyIntoImage(context, image, source, shiftX, eye)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `OnEyeCopied(eye, image, index, acquired, waited)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `OnEyeReleased(eye, published)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| eye image | `OnFactory(factory)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| composition | `OverlayLayers(OverlayFrame, layersOut, capacity)` | HalcyonOverlays.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| composition | `HudPanelDraw(context, draw, user, textBatch, afterLeftEye)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| composition | `CineScreenWanted(conversation)` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| composition | `CineScreenFullRate()` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| composition | `CineScreenLayers(CineScreenFrame, layersOut, capacity)` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| composition | `TransitionFadeLayer(session, viewSpace, context, view)` | TransitionFade.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| composition | `PlacePanel(kind, OverlayFrame, viewPosition[3], widthM, heightM, imageReady)` returns `PanelPose` | WorldPanels.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| input | `MergeMotionControllers(index, state, result)` | HalcyonBridge.cpp | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | all |
| input | `KeyDown(vk)` | Seams.cpp | Seams.cpp | Seams.cpp | all (LE2 through its inline `HotkeyDown`) |
| input | `OnMenuKey(ownMenuOpen)` returns bool | Seams.cpp | Seams.cpp | Seams.cpp | all |
| input | `MainMenuOpen()` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| threads | `OnGameThread()` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| threads | `QueueProcessEvent(object, function, params, size)` | Seams.cpp | Seams.cpp | Seams.cpp | all |
| perf (optional) | `PerfPresentBegin(context)`, `PerfPresentEnd(context)`, `PerfFirstEyeCaptured(context)`, `PerfDraw()` | - | HalcyonLe2Bridge.cpp | HalcyonLe3Bridge.cpp | LE2, LE3 |
| perf (optional) | `PerEyeViewState()` (game thread, the SFR branch of CalcSceneView: each pass its own scene view state, or both the game's one; `[Performance] perEyeViewState`, research/sequel-render-levers.md "Performance research") | - (LE1's tree asks `MELEVR::MevrBridge::PerEyeViewState`, the same lever) | RenderLevers.cpp | RenderLevers.cpp | LE2, LE3 |

A `-` means that game's host does not define the seam, because its renderer does not call it. Calling it there is a
link error, which is the intended signal: define it in that game's bridge first.

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
