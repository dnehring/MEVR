<!-- SPDX-License-Identifier: MIT -->
# LE2 pause and Save-menu diagnostic slice

Offline implementation following the normal new-save source handoff
(`ec546b38`, `research/le2-normal-save-source.md`). Native validation is pending.
This slice opens the normal pause and Save menus; it does not submit New Save,
call CanSave independently, choose a save slot or prove persistence. It extends
the existing SDK observer, guarded reads, reflection metadata checks and owned
ProcessEvent route. Root owns protocol/host integration separately.

## API and caller contract

- `ObserveSaveFlow(thread)` returns bounded JSON and performs no engine writes
  or script calls. Owner-local comparison receipts may change after a sample.
- `OpenPauseMenu(thread, services)` returns the existing `ProbeResult` and calls
  the current Default mode's effective inherited `ShowMenu` once.
- `OpenSaveMenu(thread, services)` returns `ProbeResult` and calls the current
  browser's `ShowSelectedPanel` for its unique configured Save-type row once.

The verified game-thread owner must serialize all three APIs. The explicit
empty-input save-menu request means the caller has reviewed the fresh normal UI
and actual capture; engine code cannot inspect image content. Read-only
`rawSaveState=0` or `effectiveSaveState=0` is **not** labelled as proof that
InitializeBrowser ran. `uiInitializationProof` remains
`external-review-required-not-inferred-from-alert-zero`, with
`imageContentsIndependentlyVerified=false`. No additional user confirmation is
implied when the ongoing goal already authorizes the reviewed action.

Integration verbs are `observe-save-flow`, `open-pause-menu`, `open-save-menu`;
the two actions require the producer's existing independent menu permission and
client opt-in. The core APIs accept no client object pointer, UI index, tag or
function name. Default/offline producer gates must remain disabled. No native
SaveNew verb is implemented by this slice; that next action follows verified
fresh Save UI state rather than becoming a permanent manual-only boundary.

## Source-backed roots and supported context

LExSDKv2 pin `6bd05809cccb1a099a520371a95779d4a56bb246` supplies
[classes](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_classes.hpp),
[function layouts](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_f_structs.hpp) and
[record layouts](../external/LExSDKv2/Src/LESDK/LE2/SFXGame_structs.hpp).
Additional private installed-package inspection confirmed that
`SFXGameModeManager.CanPause`, `GetCameraMode`, `EnableMode` and `DisableMode`
index `GameModes` directly by the enum. No LE1 class-order/highest-priority
fallback is reused. `SFXGameModeDefault` inherits `ShowMenu`; nearer overrides
are rejected rather than bypassed.

Pause admission requires current mode 0, exact live Default class, active and
allow-pause bits, controller ownership, validated player-pawn reciprocals,
consistent manager/world references, world browser permission, and no current
focused panel or existing pause/Save panel. This deliberately supports the
ordinary on-foot starting state only. If a live HUD retains focus, its observed
semantics must be reviewed before widening that condition. The metadata must
match the parameterless script declaration (`0x20202`); dispatch receives null
parameters. Exact owned base route and hook-chain checks are reused.

After normal return, the owner retains world/controller/pawn/manager/mode-array
identity comparisons for this opened flow. A subsequent stable browser sample
can provide the comparison receipt for OpenSaveMenu. The current GUI mode must
be 7 with `bPlayersOnly`, a focused initialized visible interactive exact
PCPauseMenu, no first-frame wait, and complete bounded pages/overrides. Save
Type 1 must occur exactly once and have supported effective state normal 0.
Matching overrides are applied in order, while a disabled CanSave sample cannot
be re-enabled by an override. The configured row/tag is derived from current
engine data, not assumed to be row 1 or a literal Save tag.

`ShowSelectedPanel` metadata is `0x824803`, int nPanel at 0 and optional
Engine.BioSFManager object pointer at packed offset 4, total 12 bytes. The
actual SDK parameter struct is copied into an aligned guarded frame. The optional
manager is null, matching the ordinary UI route that uses its own manager.
Both adjacent four-byte guards and the full input bytes must remain unchanged.
Before entry, fresh guarded reads recheck roots, mode/array, current panel,
all bounded page/override values, function/property identities and slot target.
No engine flags are rewritten and no stored address is dereferenced as a handle.

Observation also discovers the concrete PCSave handler through reachable panels,
without mistaking PCLoad for Save. It reports GUI mode, enumeration/checking/
modal flags, bounded list header/count, MaxSaves and PendingOverwriteIndex.
It does not claim those values prove that GFx save input is enabled or a slot
can safely be written. The actual New Save path uses a separate GFx input state.

## Result and evidence boundaries

Each action has its own process-lifetime entry latch. Pre-entry `rejected` leaves
the latch unused. Normal return produces `dispatched` plus a separate bounded
post-observation; the next UI may still be transitioning. Exceptions or changed
parameter storage produce `error`, spend the allowance, invalidate the flow and
perform no further engine reads. The host must retain uncertainty and disable
further admission as it does for existing native actions. No timeout, reload of
the plugin or new request ID authorizes retry. The production diagnostic is
monolithic; test-only reset/dispatcher APIs are unavailable in production.

A changed/failed observed context invalidates the stored comparison receipt;
restoring old field values does not restore it. These point-in-time object
index/class comparisons are not durable incarnation IDs or proof that every
engine field was immutable between samples. A capture remains an external
review artifact. Normal New Save, callback completion, resulting-file decoding,
reload persistence, resource assertions and VR remain separate next gates.

## Offline verification

The standalone `tests/le2-observation` project compiles both the test-dispatched
observer and the production fixed virtual route against the pinned SDK. Supply
`MEVR_OBSERVATION_SDK_DIR` if the isolated worktree has no initialized submodule.
Use a private build directory; no runtime ASI target or game launch is required.
Release and Debug suites passed with `/W4 /WX`. Tests cover exact enum-indexed
mode/owner, nearer overrides, permissions, wrong thread, malformed pointers,
first-frame wait, duplicate Save pages, disabled/overridden states, declared
object-parameter class, provider mutations, packed null-manager input, adjacent
canaries/input corruption, exception/no-post-read behavior, independent spent
latches, stale flow/pawn changes, otherwise-valid Save-tag replacement without
rearming, and guarded second-sample invalidation. The
representative browser observation remained below 6 KiB; runtime compact JSON
is capped at 14 KiB. Existing observer/resource tests continue to pass. No native
pause/Save-menu or save-write evidence is claimed by these synthetic fixtures.
