<!-- SPDX-License-Identifier: MIT -->
# Bounded LE2 diagnostic observation

This is an original read-only SDK-backed diagnostic slice for the run-bound LE2
control producer. It adds starting-state evidence without enabling the broader
LE1 reflection/gameplay stack. The `59b5e775` diagnostic returned a live entry-menu
sample through the game-thread channel, including matching ConsoleCommand
metadata. See [native evidence](le2-validation-evidence.md). Loaded-save identity,
object lifetime across transitions, native calls and gameplay remain unvalidated.
The later `152c9597` live sample matched the getter metadata and owned base-hook
chain, but its controller slot differed from that base target. Keep this failed
prerequisite explicit; see the [override investigation](le2-native-console-reuse.md#observed-controller-target-disagreement).

The scope follows the bounded SDK request for this task. It is a temporary
diagnostic exception to the normal reflection-first direction in
[engine discovery](le2-engine-access-discovery.md), not a second gameplay memory
framework. The [integration contract](../docs/le2-integration-contract.md) still
requires validated live layouts, lifecycle generations and separate gameplay
acceptance before features may act on observations.

## API and integration

Include `engine/Le2DiagnosticObservation.hpp` and call
`mevr::engine::le2_diagnostic::Observe(std::uint32_t gameThreadId)` after the
original engine tick on the verified hook-owned game thread. It returns an owned
`json::Value`. A zero or mismatching thread ID returns unavailable fields without
reading engine memory. Do not pass the current thread's ID merely to make the
check pass. The producer must supply its already established game-thread ID.

The implementation is compiled only with `MEVR_GAME_LE2`, uses the initialized
Legacy SDK globals, and requires the existing `Legacy`, `ue3::runtime::Guarded`,
and `core/Json` implementations plus the LE2 SDK target. It does not initialize
globals itself. The integrator owns adding the source and producer call to the
shared build/lifecycle. This change does not edit CMake at the repository root,
Legacy, Mod, hooks, or the producer.

Every response has `validationScope: "sdk-structural-diagnostic"`,
`nativeValidation: "pending"`, `ready: false`, and
`durableObjectIdentity: false`. Individual fields report `available` and `reason`:

| Field | Observed content and limit |
|---|---|
| `objectCount` | Value from the bounded SDK object-array header, compared with `legacy::ObjectCount`. It is not a census of validated live objects. |
| `world` | `legacy::World()` after listed-slot, liveness and `Engine.World` class-chain checks. |
| `engine` | SDK GEngine after equivalent `Engine.Engine` checks. |
| `localPlayer` | First entry of a bounded `Engine.GamePlayers` array, checked as `Engine.LocalPlayer`. |
| `player` | That local player's Actor, checked as `Engine.PlayerController`, compared with `legacy::LocalPlayerController()` only after its links have been checked. |
| `pawn` | That controller's Pawn, checked as `Engine.Pawn`. Null is unavailable, not a synthetic gameplay state. |
| `map` | Only the ASCII, non-instanced name of the validated World object's top-level `Core.Package` outer. `source` identifies that route and `loadedSaveOrGameplayProof` is false. No equivalence to a save's map label is asserted. |
| `position` | Finite X/Y/Z from the validated pawn through existing `adapter::Location`. Units remain explicitly `engine-units-unverified`; no metre conversion, gameplay readiness, or world/save association is inferred. |
| `consoleCommandMetadata` | Bounded reflection metadata for the exact `Engine.PlayerController.ConsoleCommand` declaration and its parameters; no dispatch or capability grant. Details below. |

Available objects include a hexadecimal address string, object-table index, and
qualified class label. These describe this sample only. They are not durable
handles, object serial numbers, or permission for a delayed native action. The
producer supplies run/process/build/lease and request correlation outside this
payload. There is no observer cache, no retained UObject pointer, and no attempt
to invent reliable travel or object-recycling generations.

## Read validation and reuse

The code consumes project submodule `external/LExSDKv2` at
`6bd05809cccb1a099a520371a95779d4a56bb246`; no SDK source is vendored. Sources
inspected were its LE2 `Core_classes.hpp`, `Engine_classes.hpp`, and common
`TArray.hpp`/`SFXName.hpp`, alongside the project's
[Legacy](../src/engine/Legacy.cpp),
[GameAdapterSdk](../src/engine/GameAdapterSdk.hpp), and
[Runtime](../src/ue3/Runtime.cpp). SDK field declarations are compile-time
candidates, not live reflection-validation evidence.

The complete traversal uses the existing `runtime::Guarded` access-violation
boundary. Its callback uses fixed buffers and trivially destructible samples;
JSON/string allocation happens after it returns. A guarded access fault marks
every field unavailable, discarding partial successes.

Object counts/capacity are bounded to 64 million, local-player counts/capacity
to 64, superclass walks to 64 links, and ASCII labels to 127 characters. Each
referenced object must match its indexed object-table slot and lack the existing
Legacy pending-kill flag. Dynamic instances also reject class-default/archetype
flags. Each superclass must itself be listed with a self-typed `Core.Class`
metaclass before its next link is read; expected class labels include their
top-level package. Wrong classes, malformed arrays, cycles, missing names and
nonfinite position components fail availability checks.

Existing SDK `SFXName::GetEntry` is used only to read an already present name;
numbered, Unicode, suppressed, nonprintable or unterminated names are unavailable
in this deliberately small ASCII diagnostic. There is no name creation,
`StaticClass` search, SDK generated wrapper, FString allocation, ProcessEvent,
UnrealScript, console request, native mutation, or XR initialization.

These checks do not establish live field metadata/offset correctness, verify the
entire object or name pool, or detect a recycled address with the same slot and
class. They do not prove the world and pawn belong to one completed load, or
distinguish menus, cinematics, gameplay, death, combat, or travel. Null and
unavailable values retain explicit reasons instead of becoming semantic booleans.
The host must validate its installed process/build identity before enabling this
slice. SEH containment alone does not validate a layout.

## Read-only ConsoleCommand metadata

The metadata slice starts from the checked local controller's class chain and
finds the exact `Engine.PlayerController` owner, matching the declaration used
by Legacy. It does not substitute a derived override or another class's function
of the same name. The owner walk is bounded to 64 classes. Its Children/Next list
is bounded to 4096 listed, non-template `Core.Field` objects with the expected
Outer. Missing, duplicate, invalid or foreign declarations leave metadata
unavailable. No SDK `FindFunction`, `StaticClass`, global object scan or new name
is used.

Once the unique declaration has been checked as an exact `Core.Function`, the
payload reports its address, object index, class and discovered declaration,
plus actual `parmsSize`, `numParms`, `returnValueOffset`, `functionFlags` and
`nativeIndex`. The native index is the observed 16-bit value; zero is not assumed
or promoted as validated. Function flags are compared separately to the SDK's
`0x00024400` declaration.

Function children are bounded to 256 listed, owned `Core.Property` objects, with
at most 16 parameters (`CPF_Parm`). Non-parameter locals are traversed but excluded
from the call block. Each recorded parameter has an actual name, address/index,
qualified property class, offset, element size, array dimension and full 64-bit
flags encoded as a hexadecimal string. Exact `Core.BoolProperty` records its
32-bit mask. Parameter dimensions/spans must fit the observed ParmsSize; negative,
oversized, overlapping, duplicate-name or malformed boolean storage is rejected.
An unexpected shared-bitfield layout is not relaxed merely to make a match.

`available` means the function and full parameter traversal passed these
structural checks. It can be true while `layoutMatchesSdkDeclaration` is false.
A malformed traversal keeps `parametersComplete` false; already validated header
or parameter rows may remain as diagnostic evidence. A guarded access violation
still invalidates the entire observation. Schema comparison uses unique names and
offsets, not linked-list order.

The pinned LE2 `Engine_f_structs.hpp` declaration
`APlayerController_execConsoleCommand_Parms` and the existing
[native-host audit](le2-native-host-readiness.md) establish the candidate packed
block below. Compile-time assertions check SDK size and offsets without
constructing a parameter block:

| Name | Exact expected property | Offset | Element size | Property flags |
|---|---|---:|---:|---|
| Command | Core.StrProperty | `0x00` | 16 | `0x00400080` |
| bWriteToLog | Core.BoolProperty | `0x10` | 4 | `0x00000090` |
| ReturnValue | Core.StrProperty | `0x14` | 16 | `0x00400580` |

The comparison requires ArrayDim 1, NumParms 3, ParmsSize `0x24` and
ReturnValueOffset `0x14`. The scalar DWORD boolean comparison additionally
requires mask `0x00000001`; this is an explicit expected-signature policy to
verify in the live metadata, not evidence that the SDK parameter declaration
proves the live UBoolProperty mask. Other nonzero single-bit masks remain visible
as mismatches; zero and multibit masks are malformed for this slice.

Every metadata response keeps `dispatchCapability: false` and the native-index,
ProcessEvent target, allocator/string-lifetime and native-call validation fields
`pending`, even for an exact expected match. Reading this schema does not
validate FString ownership, UTF conversion, allocation/free behavior, the actual
native entry point, or legal ProcessEvent invocation. No command is constructed,
queued, or dispatched. The real producer may use this observation as evidence
for a later reviewed call gate; it must not enable console execution from this
boolean alone.

## Repeatable offline checks

The original standalone test project is
[`tests/le2-observation`](../tests/le2-observation/CMakeLists.txt). It compiles the
actual observer against the SDK declarations, with synthetic object/name memory
and fake Legacy/global/SEH helper definitions. It loads no game code or data.
Tests use always-active checks, including in Release. Initialize the pinned SDK
submodule first, then run from the repository root:

```powershell
cmake -S tests/le2-observation -B local-tools/le2-observation-tests
cmake --build local-tools/le2-observation-tests --config Release
ctest --test-dir local-tools/le2-observation-tests -C Release --output-on-failure
```

An isolated worktree can pass
`-DMEVR_OBSERVATION_SDK_DIR=<initialized-project-SDK-path>` at configure time to
reuse the read-only submodule checkout. This standalone project has no ASI,
hot-reload, deployment, game-launch, or profile-write targets.

Checks cover missing globals; wrong/zero thread; valid typed world, player and
subclassed pawn; bounded JSON payload; invalid object/array slots; templates and
pending kill; wrong class/metaclass and cyclic superclass; null pawn/controller;
nonfinite position; instanced map-package refusal; and access faults invalidating
the complete sample. Reflection fixtures additionally cover exact/reordered
parameter layouts, missing declarations, duplicate functions/parameters, stale or
foreign fields, wrong size/count/return/type/flags/mask, malformed dimensions and
storage spans, excluded locals, cyclic owner and function lists, template
functions, and metadata access faults. Release and Debug builds/tests passed
with MSVC `/W4 /WX`. Compile/test results establish synthetic behavior only.
The monolithic diagnostic producer now compiles and calls this observer for
`observe`; its temporary-file integration tests remain offline evidence. Live
LE2 load/travel, normal HUD/save comparison, object lifecycle, and native
producer verification remain separate gates; current session evidence is in
[validation evidence](le2-validation-evidence.md). See the
[native console reuse audit](le2-native-console-reuse.md) before adding calls.

## Getter, slot and allocator observations

The read-only extension adds `getFovAngleMetadata` using the same bounded exact
`Engine.PlayerController` owner/child/property traversal as ConsoleCommand. The
expected declaration is the SDK's **scripted event** `GetFOVAngle`, not a native
getter: flags `0x20802`, one `Core.FloatProperty` named `ReturnValue`, offset 0,
element size 4, array dimension 1, property flags `0x580`, `ParmsSize=4`,
`NumParms=1`, `ReturnValueOffset=0`. `nativeIndexMatchesExpected` compares the
observed index with zero. `declarationMatchesExpected` requires the complete
layout, flags and index to agree; it never grants dispatch. The SDK parameter
size and offset also have compile-time assertions. No getter is invoked and no
function flags or native index are rewritten.

`processEventSlot` reads the validated local controller's vtable index **72**
(byte offset `0x240`) under a separate guarded boundary. `allocatorObservation`
reads `GMalloc`, its current allocator object/vtable, and LE2's `Malloc` index 1
(`0x08`) and `Free` index 3 (`0x18`). These SDK declarations are observation
coordinates, not validated method identities. Each target reports its pointer,
VirtualQuery page information and `identityValidated: false`. A committed
executable page alone is never accepted as a correct function. Slot availability
only means nonnull pointer values were obtained; null, faulting or nonexecutable
targets remain distinguishable. A slot/allocator read fault invalidates that
sub-observation without discarding an otherwise valid controller sample. An
outer object/metadata traversal fault still invalidates the complete sample.
Wrong-thread requests perform neither traversal.

`Observe` optionally accepts the host's immutable diagnostic `Snapshot` from
`plugin/ProcessEventDiagnostic.hpp`; omitting it explicitly reports
`host-snapshot-not-supplied`. This keeps the standalone observer independent of
the hook implementation. The host supplies installation status, exact installed
target/owned detour/installer-returned next hop, backend, actual exe-check status
and bounded current-chain result. `targetMatchesInstalledHook` requires an
installed nonzero target exactly equal to the observed controller slot;
`slotAndOwnedChainMatch` additionally requires the host chain result to match its
owned detour. Unknown or changing chains remain visible separately from slot
agreement. The next hop is not claimed to be pristine engine code, and
`AtRvaUnchecked` remains a structure-only exe check. Producer wiring must supply
a fresh host snapshot on the verified game thread. No callback registration or
HostServices ABI change is introduced by the observer.

All dispatch/allocation capabilities remain false. These fields provide no
FString allocation/ownership proof, ProcessEvent execution proof or durable
object/allocator identity. No generated SDK method, allocator method, native
function or script is called. Offline tests compile the actual observer against
the pinned SDK with `/W4 /WX`; synthetic executable sentinels abort if called.
They cover missing/malformed getter metadata, wrong counts/offsets/types/flags/
native index, foreign/template/duplicate/cyclic fields, missing and unreadable
controller/allocator vtables, adjacent-slot traps, executable versus data
targets, failed/missing/mismatched host provenance and changing chains. Exact
getter and ConsoleCommand metadata coexist in one payload below the control
channel's 16 KiB limit. Release and Debug test results for this extension are
offline evidence only; native calls and allocator behavior remain unproven.

## Bounded cross-object comparison and passive witness

`processEventSlots` compares **only slot 72** on the already validated world,
engine, first local player, player controller and optional pawn. Each item keeps
its object index/class, vtable and target observation separate. No object-table
scan, adjacent-slot search or extra class discovery is performed. An invalid
object is never followed into its vtable; a null pawn or one vtable access fault
does not invalidate the other validated objects. The existing `processEventSlot`
controller field remains for compatibility, with full host provenance emitted
there once.

Optional `ObserverServices` inject `InspectDiagnosticTarget` and
`ObserveValidatedController` from the host, keeping the standalone SDK fixture
independent of the production hook implementation. Target inspection adds main
module base/RVA, `.text` membership, bounded `.pdata` function span, exact-start
status and recognized jump-to-base facts. An RVA alone does not establish module
membership: require `inMainText`, and preserve unusable/unknown bounds and jump
reasons. A returned target differing from the requested pointer is rejected as
an inspector mismatch. These facts never promote `identityValidated` or dispatch.

After a guarded sample on the verified game thread, the observer supplies the
validated controller pointer/index/class, observed slot target, world, local
player and thread to the passive watcher. Missing world/controller/context or
an outer metadata fault sends an empty identity to disarm; an off-thread request
invokes neither provider. The host owns a fixed ten-second epoch. A repeat
observation of the same scope reads it without renewal; identity or context
changes start a new scope. The observer reports its returned epoch/status and
checks the witness identity against the freshly sampled input before presenting
evidence. This is a short observation scope, not an object incarnation ID.

`controllerCallWitness` distinguishes same-controller/thread base-hook entries
from **normal returns from the original**, and separately counts the subset whose
immediate return address lies strictly inside the observed slot target's exact
function bounds. Epochs, times and counters are decimal strings to preserve all
64 bits. Nothing forces an event or invokes a target. Zero matching counts are
inconclusive (idle/menu behavior or tail calls can hide the path); positive
counts do not prove arbitrary-call safety, allocator ownership or gameplay
readiness. All call capabilities remain false.

The actual-SDK standalone tests use fake providers and never invoke a game
function. Added cases cover five-object agreement/discrepancy, invalid-object
vtable refusal, optional pawn, inspector identity mismatch, passive scope and
expired status, exact counter serialization, stale witness refusal, invalid
context disarming, and no provider calls off-thread. A complete synthetic
comparison/metadata/witness payload remains below 16 KiB. Release and Debug
compile/tests pass with `/W4 /WX`; the read-only live comparison is a separate
experiment.
