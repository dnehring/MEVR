<!-- SPDX-License-Identifier: MIT -->
# LE3 native galaxy navigation, search and pursuit

2026-11-15. L3-9 adapter candidate; bounded native entry/navigation now observed below.
Discovery, pursuit, retrieval and persistence acceptance remain open.
This follows [the trilogy reuse audit](../docs/le3-port-reuse-audit.md) and
[LE2 table work](le2-galaxy-table.md), with installed LE3 script contracts inspected through
the existing private package scanner. SDK declarations are in LExSDKv2's LE3 SFXGame files.
No extracted game script, save or binary is committed.

## First native entry and navigation

Protected run `20261115-194205-273-LE3-navigation` stages the independently
verified native CIC checkpoint only after the original profile backup. Its normal
load passes. At the saved position, a slight rightward gaze with about 20 degrees
downward pitch selects the console; a short forward approach is also needed for
Use. At approximately (-50, 3384, 130), one A press enters native mode 11.
The quad capture shows Cronos Station's details, rather than the CIC's decorative
hologram. `mevr galaxy state` resolves the owning mode/camera and planet/system,
with camera state 4 and active-world ID 120300.

The existing neutral-stick B recording leaves orbit. Native left-stick movement
leaves the Anadius system for the Horsehead Nebula cluster. A later steering
trial exhausts fuel and ends in Pax; its generated autosave records zero fuel,
so this is **not** accepted as a clean, adequately fueled travel route. At Pax,
the observer reports camera state 3, active-world ID 120200, a different system
object, no selected planet, scan enabled and zero alert. Steering to the visible
relay followed by A opens the full galaxy map: camera state 1, with null system
and planet. Those nulls are legitimate for the galaxy-level context.

The screen transitions verify native A/B/left-stick operation and a captured
native map image through the shared presentation path. They do not prove
system scan cadence, discovery, pursuit, orbital probes, trigger release latches,
physical-headset readability or full stereo CIC/table presentation. No scan or
probe was dispatched in this run. All 13 captures report no snapshot errors.

The private Pax autosave has SHA-256
`eb244233cc9f9f8731162bbbc5ea9cbbdfe33706719f03c38cd6da79d748366f`.
The existing codec and independent pinned TSE oracle agree on its full structure
and exact reconstruction; native reload/persistence of this autosave remains
unverified. Cleanup restores the original QuickSave, AutoSave and Local_Profile
hashes, reports zero differences, restores the four checked VR/LE1 environment
hashes and releases the shared lock. See the
[fixture audit](le3-combat-fixtures.md) before attempting Citadel travel.

## Projector parity correction

The owner clarified that ME1 opens the map at the physical preview hologram in
the CIC. That is the required sequel outcome. The native image on a large screen
or a smaller head-placed table does not meet it.

Protected run `20261115-204835-705-LE3-navigation` reproduces the gap without
changing presentation settings. Capture `2026-11-15_20-50-16-494` is native mode
11, with one 3.2 by 1.8 metre quad 2.2 metres ahead and no published galaxy
hologram. The live observer resolves camera state 3 and system 120300. The
capture has no errors; the owned session exits normally and restores with zero
differences. This is direct evidence of the flat fallback, not projector parity.

The code agrees: `HalcyonLe3Bridge::ReadGalaxySettings` defaults `nativeTable`
off. Its optional `NativeTablePanel` only repositions the same native image.
`Mod.cpp` excludes ME1's `galaxy_map::Tick` from LE3 because its native bindings
and raw mode constants are ME1-specific. LE3 has no equivalent CIC camera hold
and projector-scene producer. Changing the default alone cannot repair this.

Read-only LegendaryExplorer inspection identifies a concrete anchor in the
installed `BioA_Nor_210CIC.pcc`: `InterpActor_0`, tagged `GalaxyMapProxy`, uses
`BIOA_GalaxyMap_S.GalaxyMapProxy` through `StaticMeshComponent_5`. The adjacent
`Emitter_0` supplies the decorative systems effect. The interaction target is
separate: `BioD_Nor_203CIC`'s `SFXPointOfInterest_6`, tag `placu_galaxyMap`;
the live capture selects that same target. Never use the interaction point as
the projector centre or confuse the distant map actors with the CIC preview.
The inspected CIC package SHA-256 is
`cc8a7cbe1698f68a6803d489357446c00622e5899300b2579a2c51e48fa9d811`.
Serialized transforms identify candidates, not accepted runtime geometry.

Implement through the existing architecture in this order:

1. Reuse ME1's `FindConsole`/`PlaceInWorld` design with LE3-native discovery of
   the use target and projector mesh. Read its live transform and bounds while
   still on foot, before the map camera takes over. Reuse the floor/view anchor,
   `world_view::Reference` and `galaxy_holo::DiscPose`; invalidate on world,
   pawn, session, recenter or actor replacement. No copied LE1 actor names,
   hard-coded measured position or assumed 301-unit mesh extent.
2. Adapt the main-view-only `GalaxyViewSwapBegin/End` scope and native camera
   publication to LE3. The game's map camera must remain available to native
   navigation, selection and source capture. Keep restoration on every return
   and exception, and reject conversation, cinematic, loading and stale frames.
   Do not simply disable the mono-map gate and render the distant map world.
3. Connect the existing galaxy scene/window renderer and native image at the
   measured projector, with CIC stereo surroundings. Share geometry, placement,
   pointer and alpha/crop contracts; do not create a second UI renderer. Native
   map/orbit and scan/pursuit semantics stay in the LE3 adapter.
4. Verify the same physical projector before/open/close, left/right world
   images, tracked head translation and hands, then map/orbit transitions,
   travel confirmation, briefing interruption, recenter, tracking loss and
   save reload. Preserve native steering and deliberate scan/probe input.

The whole-image table remains a fallback until these outcomes are demonstrated.
These are required L3-9 gates, not optional polish or a claim that the fix has
already been implemented. Source dumps and native capture files remain private.

## Live projector measurement increment

`mevr galaxy projector` now reads the native CIC geometry on the game thread.
It resolves a unique same-world `GalaxyMapProxy` actor and a separate
`placu_galaxyMap` use target, checks component/mesh ownership, and reads the
component's actual `LocalToWorld` and bounds. It rejects ambiguous actors,
nonfinite or degenerate geometry, shear and unequal in-plane scale. ME1's
existing bounds-to-radius calculation is extracted as shared
`galaxy_holo::ProjectorRadius`; ME1 retains its existing fallback and limits.
LE3 has no guessed-radius fallback. The component transform, rather than the
actor's serialized location/rotation, supplies the measured pivot and normal.

This diagnostic publishes no scene, caches no object pointers, writes no camera
or streaming state and does not enable the table. A tracking-space result is
allowed only from an actual unheld on-foot VR view, never from the native map
camera. Both `cameraHoldActive` and `presentationVerified` remain false.

Protected run `20261115-210528-483-LE3-navigation` passes **525/525** independent
SDK/reflection comparisons and the ordinary original AutoSave load. On foot it
finds exactly one projector and use target. The live component centre is about
(-0.48, 3166.42, 92.20) game units, differing from the serialized actor position;
its measured radius is 188.22 units. The current 100-units-per-metre tracking
frame puts its centre at approximately (-0.029, -1.262, -2.575) metres with a
1.882-metre radius. These are measurements from this fixture, not constants to
put in the adapter.

After normal map entry, mode 11 retains the same world/pawn and use target, but
the query finds **zero matching live projector actors**. It correctly refuses
both geometry and a new tracking anchor. Do not weaken discovery or substitute
the use point. This proves why the anchor must be acquired before entry, and
adds a room-residency investigation before a camera hold can be accepted. It
does not yet establish whether the actor was unloaded, replaced or retagged.
The installed sequence changes `stream_inGalaxyMap` on entry/exit; the CIC's
art streaming rules also distinguish normal and mission-introduction states.
Inspect native loaded/visible chunk state before introducing a reversible room
retention adapter. Never blindly enable a campaign state or use `streamlevelin`.

Both captures have no errors. The owned run exits normally, restores profile,
game and VR files with zero differences, and releases the shared lock. All three
development host/module pairs and monolithic player builds pass; the full
71-test suite passes. New shared geometry cases cover a tilted/thick projector
and invalid extents/axes. The sandbox-only installer-fixture failure on the
initial focused test disappears under the existing unsandboxed test route.
Rendering at the projector, room retention, stereo map capture and camera
handback remain unimplemented/unaccepted by this increment.

### CIC residency confirmed

Protected run `20261115-211620-664-LE3-navigation` extends the same read-only
command with the native streaming descriptors. It reuses the fields read by
ME1's `mevr stream chunks/states`; no streaming setter or campaign mutation is
introduced. All **538/538** SDK/reflection checks agree. Normal AutoSave loading
and normal console use reproduce this transition:

| Art package | On foot | Map mode 11 |
|---|---|---|
| `BioA_Nor_200` | Loaded, visible, requested | Loaded, visible, requested |
| `BioA_Nor_210CIC` | Loaded, visible, requested | Unloaded, invisible, neither load nor visibility requested |
| `BioA_Nor_203aGalaxyMap` | Unloaded, invisible, not requested | Loaded, visible, requested |

The streaming descriptor identities remain the same, as do the player/world
and use target. Neither snapshot has a pending load/unload request. This proves
that the missing projector is caused by its art level being unloaded, rather
than merely failing a tag lookup. The two observed world-state flags
`stream_inGalaxyMap` and `stream_GalaxyMap` are false at both sample points;
these endpoint samples do not establish their transient values or the native
mechanism selecting the art chunks. Do not infer that toggling either flag
would retain the room safely.

Next implementation must retain the identified room art across normal map
entry, without freezing all streaming or preventing native mission departure.
Acquire geometry while the original component exists; never reuse a stale
component pointer after unloading. Pair residency with ME1's scoped main-view
hold and independent native map capture. Verify normal exit, briefing/mission
interruption and save reload before making this the default. A cached anchor
alone cannot render missing room geometry.

Captures `2026-11-15_21-17-19-063` and `2026-11-15_21-17-39-133` have no reported
errors. The development LE3 host/module and monolithic player build pass. The
owned game closes, restoration reports zero differences including the VR
configuration, and the shared lock is released. This increment diagnoses the
remaining presentation work; it does not enable projector rendering.

### Bounded CIC retention candidate

`game/Le3GalaxyRetention` now provides `mevr galaxy retain on|off|state` as an
explicit diagnostic. Normal play does not arm it. Arming requires a measured,
tracked on-foot projector and the native CIC art already loaded and visible.
The lease waits at most 20 seconds for normal map entry and then lasts at most
120 seconds. It runs only on the arming command's game thread and checks the
current controller, pawn, world and unique streaming descriptor identities.

The installed native-function dump and existing Ghidra tools identify the
streaming evaluation boundary at RVA `0xAC67C0` for executable `60AEF0C4`.
The address table names it `UBioTierManager::ReevaluateStreaming`; that name is
an inferred role, not an exported symbol. The native streaming driver at
`0xBDFFC0` invokes it before classifying loading/visibility queues at `0xBDEDC0`.
The hook calls the original evaluator first, then requests only
`BioA_Nor_210CIC`'s load and visibility during semantic map modes 11/12.
It does not freeze streaming, set plot states or redirect the native map camera.
The byte pattern has one match and agrees with the executable's function-start
metadata. Native source/dumps remain private.

The adapter records the original requests. Off, timeout or leaving map releases
its requests only against the same live descriptor, and a fresh native rebuild
takes precedence over saved requests. World/player/descriptor replacement drops
the old lease. Writes use the existing journal. Hook removal participates in the
mod's existing unload/pin contract; normal unload runs on the game thread.

Protected run `20261115-214310-005-LE3-navigation` passes **541/541** reflection
checks and loads the original AutoSave normally. The on-foot projector and its
component remain the same live objects in mode 11; CIC art and the native galaxy
art are both loaded/visible. The diagnostic reports one override. One native
**X** press exits to mode 0; status becomes unarmed, with no owned requests and
reason `left-map`. Native galaxy art unloads and CIC art is loaded/visible again.
The projector actor/component have **new identities after exit**: the next open
must reacquire them. Do not keep a cached UObject pointer across that transition.

The preceding run `20261115-213912-340-LE3-navigation` proved retention but its
exit assertion failed: the reused B replay backs out of an orbital view, whereas
this system view visibly binds X to Exit. The corrected recording changes only
that button to left-controller primary, retaining its existing timing and poses.
This distinction belongs in later navigation scenarios.

Captures `2026-11-15_21-44-08-204`, `2026-11-15_21-44-29-297` and
`2026-11-15_21-44-45-358` cover before, map and exit. They report no snapshot
errors. **The map capture still shows the flat native panel.** Room residency
and normal exit are proven for this fixture; projector rendering, camera hold,
stereo map capture, timeout/off/reload, mission departure and physical-headset
acceptance remain separate gates. This diagnostic must not become the default
until the renderer owns and validates the complete lifecycle.

The protected preview now obtains native symbols with the existing
`dumpNatives` option in its exact prepared configuration. Its process discovery
also handles an exited SteamVR startup helper without accepting a live unreadable
or replaced process. The offline runner tests cover both refusal cases.
All three development host/module pairs and monolithic player builds pass.
All **71 regression tests** pass, including the **37 offline runner cases**.
The successful native run restores the original profile/game files with zero
differences, verifies exact VR/LE1 environment restoration and releases the
shared game lock. It installs no persistent player-facing behavior change.

### Independent native captures (2026-11-15, diagnostic)

The LE3 adapter now uses the existing `game/SceneCaptureRig`,
`core/vr/CineScreen` parallel-camera baseline/crop, host publication service and
`plugin/GalaxyHologram::WindowLayers`. The shared renderer uses semantic map
modes for LE3 (11/12), retaining ME1's gate. Nothing copies ME1's native map
adapter or enables the synthetic ME1 galaxy model on LE3.

`mevr galaxy capture on` explicitly arms the bounded retention lease while on
foot at the measured projector. It publishes two independent 1024x576 capture
resources at that anchor. `off`, lease expiry, native map exit, invalid floor,
recenter/session change or missing native camera stop publication and destroy
the captures. This remains a diagnostic: native fullscreen presentation stays
up, `roomViewHeld` and `presentationVerified` remain false. Do not ship both
presentations as the finished interface. Production resolution/aspect, complete
camera hold, UI projection and transition acceptance remain open.

Native reflection passes **603/603** checks in protected run
`20261115-220151-721-LE3-navigation`. An earlier check correctly stopped startup:
the generated SDK's compiled `SceneCapture2DComponent.bUpdateMatrices` is four
bytes later than reflection. Native `SetCaptureParameters` at `0x700500` reads
and temporarily sets **+0x174, mask 1**, then restores the bit. The comparison
now uses that independently verified native contract; the SDK is not edited.
`SetView` at `0x6FE0A0` writes the view matrix and requests component update;
`0x6FFC20` builds the target-aspect projection when that bit is set.

The first run proves object/resource creation, two eye-specific layer submissions
and native X-exit cleanup, **but both images are black**. Snapshot
`2026-11-15_22-03-11-103` is not a visual pass despite zero snapshot errors.
Reusing ME1's research/47 diagnosis, `capture on-raw` disables post-processing
only for the diagnostic. Protected run `20261115-220823-943-LE3-navigation`
produces visible native galaxy imagery in **both** capture layers (snapshot
`2026-11-15_22-09-43-357`), with normal exit and zero restoration differences.
This isolates the post-process path; the bright raw image is not the intended
appearance or proof of complete stereo/projector alignment.

The native-code audit identifies the same target-routing problem fixed for ME1:

- `0x368850` loops through captures via the leaf thunk `0x344540` before the eye
  render. Capture probes call `0x365370` or `0x3685C0` with their own renderer.
- Final post effects `0x3D1F80` and `0x3D2D60` call `0x36D760`, which binds a
  global backbuffer through `GDynamicRHI` (`0x1894018`) rather than the family's
  target, then mark the view's `+0x4B4` as completed. The normal target finish
  (`0x3534B0`) returns when that marker is set.
- The verified family target contract matches ME1: renderer `+0x18`, target
  surface virtual `+0x8`, dimensions `+0x18/+0x20`, RHI target/viewport virtuals
  `+0x520/+0x340`. Only these facts and our implementation are tracked; the
  decompilation stays private.

`plugin/SceneCaptureTarget` extracts ME1's existing guarded target read/bind
operation for both games. `plugin/Le3SceneCapture` adapts the capture/render
scope and uses the existing `core/vr/SceneCaptureScope` target decision. It
requires exact native address checks before attaching, installs the capture
depth hook last, restores thread-local state on every return, and leaves
ordinary eye binding native. Native validation of this correction is pending:
run `20261115-221710-284-LE3-navigation` never obtained a game process within
240 seconds after the Steam launch request. It restored with zero differences
and released the lock; no corrected-image result can be claimed. All three
development pairs and monolithic builds pass. All 71 tests pass across the
suite and a focused unsandboxed rerun of three sandbox-affected installer/local
HTTP tests. The [complete handoff](../docs/galaxy-projector-handoff.md) records
the remaining camera, UI, lifecycle and ME2 work.

### Camera/capture reuse contract for the next increment

The existing native dump identifies `Camera.GetCameraViewPoint`'s exec wrapper
at `0x4E3FE0`. Its body at `0x46FC60` reads location at camera `+0x448` and
rotation at `+0x454`, matching `CameraCache.POV`. This supports reusing
`player::CameraPov` and ME1's scoped swap, rather than inventing another camera
source. It does not prove the complete LE3 main-view caller chain or a successful
swap. Add independent nested location/rotation metadata checks beside the
existing LE3 FOV check before enabling writes. FOV must use the existing
`EngineProbe::CameraFovEdit` getter selection: locked FOV can override the cache.

The capture contracts, shared rig, publication service, semantic map gate and
LE3 overlay wiring are now connected as described above. The shared spawn path
restores temporary CDO flags with a finally scope, including exceptional exits.
The LE3 main-view hook already checks both the native caller and primary local
player; retain both checks when inserting the scoped hold. Native map camera
state must remain intact for selection, labels and independent map captures.

## The projector flow (lane GX, 2026-11-17)

ME1's design on ME3, on one copy of ME1's code (`game/GalaxyProjector`; docs/galaxy-projector-handoff.md "2026-11-17").
Runs on the fake headset, renderer preview, the career's original AutoSave (Shepard at the console, (-2, 3389, 131); loaded
read-only, its hash unchanged), `tools/le2-halcyon-run/galaxy.ps1` (the head turned 10 degrees right and 20 down, one A):

1. **On foot** (`gx1`): the projector measured as before (centre (-0.48, 3166.42, 92.20), radius 188.2 units; tracking
   (-0.03, -1.26, -2.57) m, normal (0.01, 0.92, 0.40): 24 degrees off flat, 1.88 m); the room's retention armed near it
   (`galaxy retention` waits 20 s for the map and is armed again while the player stays near).
2. **A at the console:** mode 11 in about 4 s; `LE3 galaxy projector: the map is open in the CIC: the view held at Shepard's
   (-3, 3424, 218; floor lock true, raw Z 186.2)`, the room's GalaxyMapProxy and its emitter hidden (2 actors), `galaxy
   retention: CIC art requested after native streaming evaluation (load false, show false)` (the native evaluation dropped
   the CIC; the lease kept it), `galaxy hologram: the view held at Shepard's for the main view's build only`.
3. **The eyes:** the CIC around the galaxy map's pit in stereo (no `MENU_MONO`: `MevrSeams::GalaxyOwnsPresentation`), the
   map's window a disc on the projector (`galaxy window: up: the game's map 2.64 x 1.94 m (a disc) ... facing (0.01, 0.92,
   0.40)`), two 1552x1120 captures (the game window's aspect, 3072x2216), the map's Flash UI on the HUD panel on the same
   disc (`hud` layer 2.69 x 1.94 m: Anadius, Pax, Scan, Exit, Cronos Station's card, labels on their objects).
4. **The captures' post-process:** with the player's chain, `Engine.DefaultPostProcess` or `DefaultUIScenePostProcess` both
   window layers are black (`gx1`, `gx2`'s `map-on-default`, `map-on-ui`), although `LE3 scene capture: final post effect
   bound its own 1552x1120 target` is logged; without a chain (`gx2`'s `map-on-raw`) the starfield, the nebula band, the orbit
   arc, the Normandy and its reticle, at a normal exposure (the 2026-11-15 raw image at 1024x576 was overexposed: not seen
   again). ME3's captures go without a chain by default (`mevr galaxy capture on-post|on-default|on-ui` for the
   diagnosis). Ghidra (LE3, read-only): the final effects 0x3D2D60 and 0x3D1F80 bind scene color (0x36DA30) when the
   family's +0x48 is 0, the LDR scene color (0x36DAC0) when the effect isn't the final one or the upscale test 0x3B1EC0
   (GSystemSettings: a screen percentage under 100) passes, else the backbuffer (0x36D760, the hooked one) and set the view's
   +0x4B4; the finish 0x3534B0 draws scene color into the family's target through 0x3520F0 unless +0x4B4 is set. Where the
   chain's picture is lost is open.
5. **X** (`gx1`): mode 0; the window down, the retention released (`left-map`), the projector shown again, the view the
   game's (`the map closed`); armed again near the projector.

Open (in order): the pointer's picks on the map's UI (the panel is a steered game-menu surface since `gx4`, the mod's GUI
events allowed in the map modes); travel and its movie; orbit (12) and the scan; the opening and closing frames (a native
mono frame before the hold publishes is possible, not measured); recenter and session changes inside the map; save reload;
the headset (alignment, legibility, comfort, GPU cost).

## Native adapter reuse boundary

ME1 `game/GalaxyMap` is a native camera/projector/table adapter for ME1. Its mode 9, Normandy
actors, camera hold and opening-delay writes must not execute in ME3, where mode 9 is GUI.
Do not widen that module's offset/class assumptions. Exclude its startup/tick from ME3
callers while using the separate native adapter below. Full CIC surroundings and stereo
map depth remain separate work, not acceptance obtained by showing a flat image on a table.

Reuse `core/vr/GalaxyMap::NativeTablePanel`, `GalaxyHologram::PlaceTable` and `DiscPose` for the
native image. The current shared helper accepts semantic `mapActive`; LE3 galaxy/orbit are
11/12, through `engine::adapter::IsMapMode/IsGalaxyMode/IsOrbitalMode`. Preserve the table
through that transition and its existing unavailable-image/tracking fallback. The renderer
continues owning the source image and swapchain. Do not create another galaxy scene engine.

`game/Le3Galaxy.hpp` provides:

- `Observe(pc)`: existing JSON type, read-only mode/camera/system/planet/unique orbital latent
  context, native scan enable/cooldown, separate alert/chase/active/detected/grace fields,
  scanned-planet count, orbital scanning/probe/impact/rescale/exit flags, and bounded actual
  native key/command bindings. Per-field availability stays explicit. Bindings are borrowed
  properties; no returned FString ownership is assumed and none is freed.
- `Request(pc, Action, inputAllowed)`: original mode `ScanSystem`, `StartScanning`,
  `StopScanning` or `LaunchProbe` through reflection. Input permission defaults false and must
  come from the caller's existing scene/menu/input ownership checks. It verifies active
  mode, camera and context, native scan cooldown/enable, or unique orbital latent action and
  matching planet/world. Probe dispatch requires scanning with no probe/impact/rescale/exit
  already active. Return true means one method invocation, never discovery or retrieval.
- `game/NativeMapInput.hpp`: the existing `CloseLatch` reused across semantic map/orbit,
  menu/tracking and session/recenter generation boundaries. Held triggers require independent
  release; neutral menu output is not counted as a physical release. Sticks and ordinary
  buttons are untouched, so this guard does not stop pursuit steering. Outside map modes it
  leaves the pad unchanged. Existing menu owners still consume their controls normally.

No resource, alert, cooldown, selection, plot, world ID, latent action, chase flag or saved
discovery is written by the adapter. Observation identities are local to the sample, not
durable handles. `available` is bracketed context availability; coherent simulation,
persistence and scan outcome flags remain false. This is not a new QA/request transport.

## Native mechanics verified in installed source

| Path | Original behavior and consequence |
| --- | --- |
| `SFXGameModeGalaxy.ScanSystem` | Final exec wrapper calls the native galaxy-camera scan. Keep native scan eligibility, propagation, discoveries and effects authoritative. Its native target is requested in the metadata probe for deeper audit. |
| `BioCameraBehaviorGalaxy.ReapersCanChasePlayer` | Requires system-level camera state, the ship crosshair and no active cinematic mode. It is not equivalent to a stored alert threshold or one chase flag. |
| `SFXSystem.AddReaperAlertValue` | Native script events distinguish initial alert and crossing the pursuit threshold. It calls the alert setter; VR must not reproduce that accounting. |
| `SFXSystem.SetReaperAlertLevel` | Updates native alert/detection/UI and calls native `SetSaveData`. Merely leaving the map is not proof that alert resets. |
| `SFXSystem.ReaperChaseStart/End`, `PlayerEscapedReapers` | Own native events, timers, sound and accomplishment handling. Chase end does not simply clear every chase/active flag in its script body; report flags independently and judge visible/native behavior. |
| `SFXSystem.ResetGlobalReaperAlertLevels` | Native static reset. Invocation conditions and persistence still need native/source evidence; the adapter never invokes it to make a test convenient. |
| `SFXGameModeOrbital.GetGameData` | Finds the pawn's orbital latent action. Reuse LE2's stricter unique-action and matching current-planet proof instead of searching GObjects for an arbitrary instance. |
| `StartScanning`, `LaunchProbe`, `ProbeImpact` | Respect original scan/rescale/exit/probe/impact states and drive the authored sequence outputs and presentation. The inspected LE3 launch function does not decrement an ME2-style probe balance. Do not import the ME2 mining resource recipe. |
| `BioCameraBehaviorGalaxy.GetRemainingFuel/GetNumCredits` | Fuel comes from the player's native inventory and credits from native resource index 0. Keep native movement/travel and purchases; no synthetic fuel consumption or free travel. |

`m_nScannedPlanets` is a native observation, not an independently reconstructed set of
discoveries. A getter/flag or method return cannot prove a war asset, retrieval reward or
save-persistent discovery by itself.

## Exact integration work for the coordinator

The game/core files and metadata are scoped to this commit. Mod and host wiring is separate:

1. Exclude `game::galaxy_map::TryStartup/Tick` from ME3 in `mod/Mod.cpp`; those are ME1 paths.
   Do not publish ME1 hologram picks against LE3 objects.
2. Include `game/Le3Galaxy.hpp` in the existing console request implementation. Reuse a
   `mevr galaxy state` command returning `json::Write(le3_galaxy::Observe(pc), 0)` and explicit
   diagnostic action subcommands, if desired. Keep existing request ownership/transport.
   Pass `inputAllowed` only after the caller's existing menu/scene/input checks. Never poll
   `Request` continuously while a trigger is held.
3. In `HalcyonLe3Bridge::PlaceNativePanel`, use `NativeTablePanel` for semantic map modes and
   the existing opt-in `[Galaxy] nativeTable` settings. Reuse LE2 settings limits and defaults.
   Reset placement on session/app-space replacement and recenter; retain the original native
   panel if source image, tracking or placement is unavailable. Feed the helper the actual
   image aspect and predicted head pose. Do not suppress the fallback image.
4. In the existing LE3 pad merge, preserve native LT/RT for map/orbit instead of the on-foot
   squad remapping. `Renderer::orbitalTriggers` already selects that shared mapping, despite
   its historical name. Apply one `NativeMapInput` under the bridge's existing pad mutex after
   merge, with actual semantic context and the bridge's session/recenter generation; call
   Reset on teardown. `inputAllowed` must include tracking/focus and all existing main, wrist,
   calibration, Thespian and squad ownership. Existing menu close latches remain in force.
5. Use either native pad actions or explicit adapter dispatch for a given input, never both.
   Keep native ship/reticle sticks. Observe the actual `Bindings` and `LocalizedBindings` in
   the loaded game before claiming which controller button produces a pulse; configuration
   and native controller remaps can change them.
6. Pointer picking must use the exact submitted table quad and existing input owner. The LE3
   bridge currently registers pointer only for GUI, deliberately not galaxy/orbit. Do not
   register orbital as a mouse-click surface. Adding galaxy pointer must prevent one trigger
   from simultaneously becoming a pointer click and a native scan; validate the binding and
   ownership first. The shared trigger-map bypass of `ownsTriggers` is not pointer arbitration.

No CMake registration is needed for the headers; the added regressions live in the existing
pad-merge test target. `ReflectCheck` adds classes, fields, binding element/member layouts,
zero-parameter action contracts and the native scan RVA probe. Compile checks are not native
proof. The original LE1 pad comparison still passes 20,000 random steps with zero mismatches.

## Host integration candidate (2026-11-15)

`HalcyonLe3Bridge` now connects the shared placement and trigger policy. This is an offline
candidate, not native galaxy acceptance. The existing coordinator already excluded the
ME1 galaxy startup/tick from LE3 and wired the read-only `mevr galaxy state` command.

- `PlaceNativePanel` keeps its normal `WorldPanels` fallback, then optionally replaces its
  pose **and dimensions** with `NativeTablePanel`. It takes the ready native image's actual
  height/width aspect and the renderer's predicted tracked head pose. `[Galaxy] nativeTable`
  remains off by default. Distance/below/radius/tilt use LE2's existing defaults and limits;
  there is no new map renderer or recreated galaxy scene. Table placement survives map/orbit,
  is hidden through unavailable tracking/image, and resets on leaving map, session/app-space
  replacement or recenter. No galaxy/orbital pointer surface is registered.
- If the renderer's optional full-viewport UI extraction is enabled, its map/orbit UI quad
  follows the exact native panel space/pose/size (including the flat fallback). Its texture,
  subimage and source-alpha flags remain unchanged. Native visual alignment still needs proof.
- The existing shared pad merge uses `orbitalTriggers` for **both** LE3 semantic map modes.
  That policy preserves native LT/RT and removes stale gameplay hand-walk, turn, squad and
  wheel remaps. Original ship/reticle stick axes remain intact. Native bindings still decide
  which actions those inputs invoke; no `Le3Galaxy::Request` action is dispatched alongside
  the pad.
- `NativeMapInputLease` wraps the existing `NativeMapInput`/`CloseLatch`, under the bridge's
  existing pad mutex. The XR/render owner renews a maximum 250 ms lease only with a ready
  native map image, matching session/app-space, valid head tracking, live controller focus,
  and no main/developer menu, calibration, wrist/omni pointer, world UI, Thespian, squad or
  power-wheel owner. The input poll rechecks current owners and semantic mode. It performs
  no XR query. Tracking loss, invalid frame, mode change or epoch reset requires a fresh
  publication and independent trigger releases, including when no input poll occurred during
  the interruption. The map gate runs after merge but **before** other owners' final pad
  filters: their synthetic zeroes never count as a physical release. Existing menu close
  latches remain authoritative.

The existing pad-merge executable passes the new lease regressions: held entry, independent
release, continuous valid frame renewal, expired/future publication, lost ownership between
polls, same-poll owner takeover, map/orbit transitions, session/recenter generation and reset.
Pursuit axes are retained under stale gameplay flags. The original 20,000-step LE1 comparison
still reports zero mismatches. Shared affected code syntax-checks under all three game defines;
the LE3 host bridge passes `/W4 /WX` and its renderer caller passes its existing `/W3` policy.
No game, headset, settings, native resources or saves were changed for these checks.

Native gates remain: actual loaded key bindings; usable native image/table aspect and UI;
map/orbit navigation and pursuit steering; one deliberate scan/probe with native admission;
held entry and menu/focus/recenter/session recovery; retrieval/alert/fuel and normal save/reload
persistence. This host wiring does not prove any scan, reward, class method or save outcome.

## Staged proof using existing tools

Use the owned protected session, full LE3 save codec and ordinary load runner. Acquire a
galaxy entry through the normal Normandy console; do not force mode, active world or story
flags. The existing Normandy Save31 is a loading candidate, not a proven galaxy fixture.

Capture the native image and controls using existing snapshot/record/replay tools. Correlate
every before/after observation to the same system/planet/world and session artifact. Cover
native navigation, selection and confirmation, one intended scan, a held trigger on entry,
cooldown refusal, pursuit steering, escape/re-entry, orbital start/stop and exactly one probe
launch/impact. Repeat menu, tracking, recenter and save/load boundaries. Inspect actual
retrieval/discovery results and native resources; do not infer outcomes from invocation.

For persistence, use the ordinary in-game save flow, bind the actual produced descriptor and
bytes, independently decode with `tools/le3-save.py`, reload normally and compare the same
system/planet state. Compare alert/discoveries across save/load, escape/re-entry and mission
transitions according to observed native behavior. No blanket reset-on-exit assumption and
no blind story or galaxy-state editor mutation. Existing ME2 `planet-scanning.json` remains
LE2-specific and must not be relabeled as ME3 acceptance.
