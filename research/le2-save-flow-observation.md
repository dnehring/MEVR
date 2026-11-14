<!-- SPDX-License-Identifier: MIT -->
# LE2 pause and Save-menu diagnostic slice

Offline implementation following the normal new-save source handoff
(`ec546b38`, `research/le2-normal-save-source.md`). Native validation is pending.
This slice opens the normal pause and Save menus and provides a separately
gated normal New Save event. It does not call CanSave independently, choose a
filename or prove persistence. Native Save-panel readiness and enabled New Save
capture review remain required before the new event is used. It extends
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
function name. Default/offline producer gates must remain disabled. The `SaveNew` API described below remains disabled until its separate host/client
integration and live readiness checks pass; menu permission alone cannot authorize
a write.

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
consistent manager/world references, world browser permission, and no existing
pause/Save panel. Focus must be absent or the exact validated closed persistent
PC PowerWheel HUD described below. This supports the ordinary on-foot starting
state only; other focused panels remain unsupported. The metadata must
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
review artifact. Native New Save validation, callback completion, resulting-file decoding,
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
representative browser observation remained below 7 KiB; runtime compact JSON
is capped at 14 KiB. Existing observer/resource tests continue to pass. No native
pause/Save-menu or save-write evidence is claimed by these synthetic fixtures.


## Normal New Save API and evidence contract

`SaveNew(thread, services)` uses the same serialized game-thread owner and returns
`ProbeResult`. The explicit empty `save-new` request requires a separate native
save permission, caller review of the actual enabled New Save control, and a
sealed before-save disk inventory bound to that request/run/lease/candidate.
The client/collector owns disk evidence; the engine does no file I/O and reports
`diskInventoryVerifiedByEngine=false`. It does not request a slot or filename.

The current exact PCSave must descend from this process's successful
OpenSaveMenu. The prior browser's sampled eligibility, world/controller/pawn,
manager, mode array and active paused GUI state must remain consistent. A fresh
bounded observation captures all descriptor headers and UTF-16 bodies, validating
nonempty bounded career/name components, index range, defined type range,
consistent career and duplicate identity absence. The complete list has at most
512 entries and must have room below the observed positive MaxSaves. Any maximum
index of INT_MAX is rejected before the game's increment. The reported source
calculation is 1 for an empty list, otherwise max(-1, all indices)+1. It is not a
filename mapping or proof of disk absence. Larger lists remain unsupported.

The current Save panel must be focused, visible, initialized and interactive in
GuiMode 0, with data checking complete and no enumeration, message/checking
modal, or other observed context change. These are structural predicates, **not**
a reconstruction of GFx `loadSave.SetInputEnabled`. The caller's capture review
is labelled `uiReviewAttestation`; no engine image inspection is claimed. A
changed ready panel/list/body invalidates the observation receipt; restoring old
values cannot rearm it. All descriptors and metadata are compared again after
host target providers, immediately before entry.

`ObserveSaveFlow` adds these fields beneath `save`:

- `handleEvent` and `processEventSlot`: current verified structural metadata.
- `recordsComplete`, `recordsReason`, `observedRowCount`, and `records`, whose
  entries contain actual `career`, `saveName`, `index`, `type` values.
- `candidateNextIndex` and `candidateIndexIsFilename=false`.
- `structuralNewSaveSupported`, `sameOpenedSaveFlow`, `newSaveObservationBound`.

Incomplete data never emits a complete record subset. If the whole observation
exceeds 14 KiB, it returns unavailable and invalidates write admission. Top-level
`saveNewEntrySpent` reports the independent process latch; observation never
sets `saveNewCapability=true` or grants host permission.

The effective declaration must be SFXSFHandler_Save.HandleEvent, with flags
0x00C20002, native index 0, two parameters and 20-byte storage: byte Command at 0
(flags 0x80), array lstArguments at 4 (flags 0x400182), with live Core.StrProperty
inner metadata of size 16/dimension 1 owned by the array property. Nearer
unsupported overrides and malformed/cyclic metadata remain rejected.

The call uses the actual pinned SDK parameter type, Command 11 and an explicitly
constructed empty `TArray<FString>`. SDK Common/TArray.hpp initializes its data,
count and capacity to null/zero; the empty destructor's Clear/Shrink path performs
no allocation/free. The generated parameter constructor only zeroes the inactive
union, so the adapter explicitly begins its lifetime through a Microsoft
`__unaligned` pointer at packed offset 4. Size/alignment assertions preserve the
20-byte pack(4) ABI and 16-byte array. Adjacent guards and all input bytes must
remain unchanged. Ordinary cleanup destroys the array only while that complete
local frame is unchanged. A changed out-array header has unknown ownership and
is never traversed, cleared or freed. This is no native-fault/SEH recovery claim.

The independent latch is spent immediately before ProcessEvent. Normal return
with intact frame is `dispatched`, meaning event submission returned. Exceptions
or changed storage are `error` with uncertainty and no retry. Neither path reads
engine memory after entry; both invalidate the write receipt. Payload fields
include `entered`, `returnedNormally`, `parameterFrameIntact`,
`emptyArrayDestructionSafe`, `entrySpent`, `retryAllowed=false`,
`saveCompleted=false`, `persistenceVerified=false` and the disk boundary above.
No callback is invoked or fabricated. The game's normal event clears the
pending overwrite index and runs its preparation/free-space/new-slot sequence.

The collector must preserve prior-save hashes, identify actual stable new files,
archive and independently decode them, and distinguish unrelated/autosave changes
from the requested manual output. A returned event or closed panel is insufficient.
A real correlated successful callback would be additional evidence; this slice
adds no callback hook. A later protected normal load of the generated file and
fresh resource/state/scene checks establish reload persistence separately.

New synthetic SDK tests exercise normal and empty-list submission, exact metadata,
corrupt string/list/inner property, full capacity and integer overflow, changed
provider-time descriptors, restored string bodies, oversized evidence, independent
one-entry admission, and exceptions/header/canary changes. Linked allocation/free
stubs abort if called, verifying the actual SDK empty-array path does not allocate
or release storage. Both production and fake dispatch paths compile under the
standalone project's /W4 /WX settings. These tests do not validate live GFx state,
game callbacks, output files or native save completion.


## Focused persistent PowerWheel HUD

The protected diagnostic run `f402b0a6-a183-4b2b-a16f-b6a36995a824` exposed an
adapter restriction: the loaded scene reported current active Default mode 0,
allow-pause/browser permissions, unpaused world, and focus tag PowerWheel, so the
original null-focus rule refused admission. The run owner reviewed a readable
normal Normandy crew-deck scene with no visible wheel overlay. That image and tag
alone do not establish handler lifetime or closed-state fields.

Read-only inspection of the same pinned SFXGame package clarifies the normal
route. `MassEffectGuiManager.RecreateHudPanel` creates Reticle, PowerWheel and HUD
panels together, caching the wheel's default handler in m_oSFPowerWheelHandler.
`SFXGameModeDefault.EnterPowerWheel` enables mode 1 and the wheel mode's exit
disables it. Neither inherited ShowMenu nor ShowBrowserWheel requires null focus.
The generic focus rejection was an adapter assumption, not a game precondition.

`SFXSFHandler_PowerWheel.WheelVisibilityChanged(false)` starts the OUT animation
and sets m_bInOutTransition. `ExInt_WheelTransitionOutDone` sets wheel mode NONE
and clears that transition flag. SDK mode NONE is 0; m_bVisible and
m_bInOutTransition occupy masks 1 and 8 at 0x518, with mode byte at 0x51c.
SetWheelVisible is native and its body was not inspected, so visibility remains a
conservative SDK-state predicate rather than independent visual proof. The whole
panel can remain visible as persistent HUD; radar visibility is a separate flag.

The alternate admission requires a reachable focused panel tagged PowerWheel,
its exact live SFXSFHandler_PCPowerWheel default handler, reciprocal panel and
manager/world references, the manager's cached wheel identity, and current
controller/Shepard pawn matches. The panel must remain initialized, visible,
focused, not removing, and input/events enabled. Wheel mode must be NONE 0,
wheel-visible false and transition false. All existing Default-mode, active,
permission, unpaused-world and absent pause/Save-panel checks remain. An unknown
focus, wrong class, missing/foreign cache, changed player/pawn, active or
transitioning wheel, or malformed object cannot use this route.

`focusedPowerWheel` reports panel/handler metadata plus `stateAvailable`,
`reciprocalsMatch`, `wheelVisible`, `inOutTransition`, `wheelMode` and
`closedIdleFocusSupported`. `visualProof` stays false. These fields participate
in the stable observation and final guarded resample immediately before entry;
they are not cached dereference handles. Synthetic tests cover the focused closed
HUD with independent radar visibility, all rejection cases above, unknown tag,
and a transition change during the target provider. A protected fresh live
observation and normal menu transition still need to validate this candidate.

Provenance remains SDK pin `6bd05809cccb1a099a520371a95779d4a56bb246`, installed
SFXGame SHA256 `681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`,
and scanner SHA256 `918a095735f00998e5ff99871902253c47c66069a6cce3e6d6b0d474addd6f01`.
Reproduce with the existing read-only scanner's `--game LE2 --only SFXGame`
`--script` queries for the exact functions above; keep raw script output private.
No game/script mutation or focus-clearing input is used by this change.


## Save-panel initialization and rejected-row diagnostics

The next protected run reached the normal Save panel, exposing two separate
adapter limitations. Immediately after ShowSelectedPanel returned, the same Save
panel and PCSave handler existed with an empty list, initialization false and no
focused panel. A later observation had the same identities, initialization/focus
complete, and three rows. Comparing the configured Save tag to the *focus* tag
had invalidated the flow during that ordinary asynchronous initialization.

The continuation now compares the panel's own bounded tag to the configured
browser Save tag and binds its first observed panel/handler identities. The same
owned paused GUI context may progress through initialization and acquire focus.
Before the first panel appears, absent focus is allowed; after it appears,
disappearance, hidden state or replacement invalidates the flow. Initialization
and focus progress are monotonic: after either is observed true, its loss
invalidates the pending flow even if a rejected list has prevented a ready
receipt. A foreign focused panel,
wrong tag, removing panel or changed world/player/pawn/mode/eligibility also
invalidates it, and restoring old values cannot rearm it. The pending identity
never grants write readiness: complete stable descriptors and focused initialized
interactive Save UI remain mandatory. `save.panelTag` and
`save.pendingPanelIdentityBound`, `save.pendingInitializedSeen` and
`save.pendingFocusedSeen` make the distinction observable.

The three-row live list coincided with an externally reviewed Create New Save
entry followed by two real manual saves. This suggests a synthetic row, but does
not establish its exact descriptor representation. SDK BeginInitializeSaveList
and EndInitializeSaveList are native declarations; their internal row construction
is not available from the inspected script source. No marker is guessed or
silently skipped. The native GetNewSaveIdx script iterates every descriptor's
index, so dropping a row without proof would also change the source calculation.

An invalid list still reports `recordsComplete=false`, an empty authoritative
`records` array, no candidate next index and no write receipt. It additionally
reports `save.rejectedRowDiagnostics` with `authoritative=false`,
`listComplete=false`, `snapshotCoherenceVerified=false` and
`syntheticMarkerRecognized=false`. At most eight diagnostic rows and 3 KiB of row
JSON contain actual uiRow/index/type, career/saveName/FriendlyName read status,
signed-32-bit header count/capacity values, and whether the SaveGame pointer is null. That pointer is
never dereferenced. Decoded UTF-8 values over 256 bytes are explicitly omitted,
not cut. `firstRejectedRow`, precise per-row reasons and `truncated` explain the
boundary. Diagnostic rows can include individually valid later descriptors, but
they are never promoted to a complete list or used to select a file or permit a
write. Overall observations retain the existing 14 KiB limit.

Tests reproduce an unfocused uninitialized panel becoming ready without changing
identity, and reject changed/restored pending tags, panel replacement/disappearance,
foreign focus and loss of pause, plus hidden/initialization/focus regressions
while descriptor rejection prevents a ready receipt. A synthetic marker-shaped row remains refused
while bounded diagnostics expose its empty fields and the following manual rows.
Malformed strings and oversized-list/evidence tests continue to refuse admission.
The representative three-row diagnostic observation is below 9 KiB. Fresh native
diagnostic evidence is required before defining any supported synthetic marker.
