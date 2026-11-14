<!-- SPDX-License-Identifier: MIT -->
# Bounded LE2 diagnostic observation

This is an original read-only SDK-backed diagnostic slice for the run-bound LE2
control producer. It adds starting-state evidence without enabling the broader
LE1 reflection/gameplay stack. The `59b5e775` diagnostic returned a live entry-menu
sample through the game-thread channel, including matching ConsoleCommand
metadata. See [native evidence](le2-validation-evidence.md). Loaded-save identity,
object lifetime across transitions and gameplay remain unvalidated.
The later `152c9597` live sample matched the getter metadata and owned base-hook
chain, but its controller slot differed from that base target. Keep this failed
historical finding explicit; see the [override investigation](le2-native-console-reuse.md#observed-controller-target-disagreement).
Candidate `eede67a1` subsequently proved the same-controller forwarding route
passively and returned one explicitly requested scalar getter with intact
canaries and identity. General console calls remain disabled.

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

All observation dispatch/allocation capability fields remain false. These fields provide no
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
readiness. Observation dispatch capability remains false.

The actual-SDK standalone tests use fake providers and never invoke a game
function. Added cases cover five-object agreement/discrepancy, invalid-object
vtable refusal, optional pawn, inspector identity mismatch, passive scope and
expired status, exact counter serialization, stale witness refusal, invalid
context disarming, and no provider calls off-thread. A complete synthetic
comparison/metadata/witness payload remains below 16 KiB. Release and Debug
compile/tests pass with `/W4 /WX`; the read-only live comparison is a separate
experiment.

Representative full-provider payloads measured 12,640 bytes (Release) and 12,691
bytes (Debug); with a 1,024-byte reserved host-envelope string and wrapper keys,
the larger serialized envelope was 13,757 bytes. The test enforces that envelope
within 16,384 bytes. These are measured fixtures, not a mathematical maximum for
all possible names/metadata; the production wire limit remains authoritative.

## Explicit bounded scalar probe

`ProbeFov(thread, services)` is separate from `Observe`: it admits at most one
entry per process into the hardcoded `Engine.PlayerController.GetFOVAngle`
scripted event. It consumes no JSON addresses and exposes no arbitrary-function
dispatcher. The existing providers plus `readHookSnapshot` supply current host
ownership/chain, target bounds and passive evidence. The host protocol owns
explicit request admission; merely requesting an observation never calls it.

Before entry, require the exact getter metadata, validated world/local player/
controller, current owned base hook/normal next hop, checked LE2 base RVA,
recognized owned hook chain and exact bounded target function. The controller
slot must either equal the installed base target or have a same-controller,
same-class/index/world/local-player/target/thread passive witness with at least
one matching **normal original return** inside identical target bounds. A
completed, expired ten-second window is usable as historical evidence only when
its scope still matches; the probe never renews the window. A recognized jump
alone is not an admission route. The complete guarded identity and metadata are
resampled immediately before entry, including the getter's UFunction and return
property pointer/index/class, so replacing a property with an identical layout
still rejects the call.

The actual SDK four-byte parameter block starts with a NaN sentinel and has
adjacent 32-bit canaries verified by compile-time offsets. The production path
calls the validated controller's virtual ProcessEvent just as Legacy does,
without a generated wrapper, function-flag/native-index mutation, engine
allocation or FString. The admitted process budget is spent before dispatch.
The dispatch is deliberately outside the read-only SEH wrapper: there is no
native-fault recovery claim and no retry after entry. Ordinary C++ exceptions
produce uncertainty. Engine state is not traversed after an exception or a
damaged canary; otherwise a guarded post-call sample checks identity/metadata.

Pre-entry rejection returns status `rejected` and spends no native entry.
A normal finite changed result, intact canaries and unchanged post-call identity
returns **`dispatched`**, never `completed`; the payload records raw return bits,
canaries, admitted identity, historical witness scope and post-read outcome.
Other entered outcomes return `error` with `outcome: uncertain`; further probes
in that process reject. Even `dispatched` proves only this bounded scalar return,
not arbitrary console calls, allocator ownership, story/gameplay readiness or VR.

Standalone tests substitute dispatch only under `MEVR_LE2_OBSERVATION_TEST`;
the reset/substitution API does not exist in production. The same project also
compiles the fixed production SDK virtual route into an unexecuted object
target. Release/Debug tests cover malformed metadata/read faults, missing or
changed host provenance, incorrect base/bounds, absent/stale passive returns,
expired same-scope admission, same-layout return-property replacement before
and after entry, one-entry enforcement, exception/nonfinite/unchanged return,
both canaries, context change, and no post-failure engine traversal. These are
offline checks, not a native getter result.


## Normal save-menu observation (offline foundation)

`ObserveSaveMenu(gameThreadId)` is a separate read-only payload; the general
`Observe` response is unchanged. It follows validated local-player, viewport,
and `MassEffectGuiManager` references, compares a nonnull controller manager
cache, and walks at most 32 reachable panels and 32 handlers per matching panel.
It never scans GObjects to find a menu. Every dereferenced instance must have a
live, non-template table slot and a bounded validated class chain. Panel-manager
and handler-panel references must agree. Controller WorldInfo, manager
oBioWorldInfo and each selected handler oWorldInfo must agree; nonnull references
must also be typed/live. All-null references remain explicit unknown world
linkage, not a proven current world. Duplicate panel entries/tags, competing
handlers, foreign focus and malformed headers invalidate the context, including
any earlier candidate's apparent readiness.

The snapshot distinguishes Splash, MainMenu and Load, recording visibility,
initialization, removal, input/events and focus. It also records splash pressed
state/finite legal timeout, main-menu save/DLC/checking/connection flags, and load
mode/career-index/enumeration/message/checking flags. `CurrentCareerIndex=-1` is
preserved as a legitimate default. A structurally interactive panel is **not**
a capability or a proof that an asynchronous native operation completed.
[The menu source investigation](le2-menu-load-source.md) explains the normal
transition and pending filename mapping.

Exact-owner metadata reuse rejects nearer subclass declarations before reporting
PCSplash.AdvanceScreen and
MainMenu.MoveToLoadGui. The latter expects only `LoadMode`, one ByteProperty at
zero with flags `0x80`, total size/count 1; the former has no parameters. The
observed void ReturnValueOffset is reported without inventing a constant. No
function is called, no engine string is constructed, and no engine state is
written. These declarations do not validate a menu object's call route.

For an interactive Load panel with enumeration/message waits clear and checking
complete, copy at most 12 `FSaveGUIRecord` descriptors. Only Career, SaveName,
Index and Type are read; FriendlyName, SaveGame and image pointers are not needed.
The strict bounded UTF-16 helper allows 256 code units including termination,
capacity 4096 and at most 1024 UTF-8 bytes per string. It rejects malformed text
instead of normalizing it. A second context/header snapshot and string copy must
agree. This is a sampled stability check, not a GC lock, atomic body read or
durable generation identity.

Each row exposes its UI `row` separately from descriptor `index`, plus a
`descriptor-v1:` content key: hexadecimal bytes of little-endian u32 UTF-8 career
length, career bytes, little-endian u32 UTF-8 SaveName length, SaveName bytes,
little-endian i32 Index bit pattern and one Type byte. This is an injective
comparison encoding, **not** a file hash, authorization token or object handle.
It contains the descriptor text and is not privacy-redacted. Duplicate complete
descriptors are ambiguous and invalidate the copied list. Empty strings remain
observable; they do not establish a disk mapping.

Oversized lists are explicitly incomplete with no partial selectable rows.
The final **compact** JSON is capped at 14 KiB, reserving 2 KiB of the wire limit
for the host. On overflow, all records are cleared while menu context is retained;
if context alone exceeds that limit, a small unavailable response replaces it.
This guard is enforced at runtime, including JSON escaping. Root host integration
must retain compact serialization. The next exact-fixture step should accept a
host-owned frozen descriptor filter and scan a bounded larger list (for example
512 rows) to return the unique match, rather than make 12 a permanent selection
limit. Filename-to-descriptor conversion remains unproven; no load is authorized
by this snapshot.

Synthetic tests compile the actual pinned SDK, exercise valid menu/descriptor
chains, Unicode, malformed text/read faults, duplicate/foreign/template objects,
wrong focus, asynchronous state, changed second sample, exact menu metadata and
payload overflow. Release and Debug each passed the existing standalone suite with these new cases,
with both production/test translation units compiled under `/W4 /WX`. The
representative 12-row compact response was 8,338 bytes; the escaped-string budget
fallback was 5,989 bytes. These measured cases supplement the runtime size guard. Production dispatch compilation remains covered by the separate existing
object target, without executing it. None of these tests is native menu evidence.
