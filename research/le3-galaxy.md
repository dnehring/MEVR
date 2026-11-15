<!-- SPDX-License-Identifier: MIT -->
# LE3 native galaxy navigation, search and pursuit

2026-11-15. L3-9 adapter candidate; no native galaxy run or discovery/persistence acceptance.
This follows [the trilogy reuse audit](../docs/le3-port-reuse-audit.md) and
[LE2 table work](le2-galaxy-table.md), with installed LE3 script contracts inspected through
the existing private package scanner. SDK declarations are in LExSDKv2's LE3 SFXGame files.
No extracted game script, save or binary is committed.

## Reuse boundary

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
