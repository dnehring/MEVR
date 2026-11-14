<!-- SPDX-License-Identifier: MIT -->
# ME2 run-bound control channel

This client and its C++ codec provide the transport for the
[scripted testing slice](../../docs/le2-scripted-testing.md). They do not install
a runtime producer, enable a renderer, select a save or prove a native operation.
The runtime consumer is implemented in `engine/Le2ControlHost.cpp` behind the
opt-in monolithic diagnostic build. Until it publishes
an authenticated-by-session `control/identity.json`, commands fail without writing
the existing shared `console.request` file. The protocol is coordination between
cooperating local tools, not a security boundary against another process owned by
the same user.

## Building the diagnostic host

Run in a clean isolated worktree of the selected commit, with the repository's
pinned submodules available:

```powershell
cmake -S . -B local-tools/le2-diagnostic-build -A x64 -DMEVR_BUILD_LE1=OFF -DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=OFF -DMEVR_HOT_RELOAD=OFF -DMEVR_LE2_DIAGNOSTIC=ON -DMEVR_HALCYON=OFF -DMEVR_MAIN_MENU=OFF
cmake --build local-tools/le2-diagnostic-build --config Release --target MEVR-LE2 mevr-le2-control-host-tests mevr-le2-diagnostic-tests
ctest --test-dir local-tools/le2-diagnostic-build -C Release -R "mevr-le2-(control-host|control-producer-smoke|diagnostic)$" --output-on-failure
```

This produces `Release/LE2MEVR.asi` inside the build directory. Record its SHA256
and embedded commit/version; deploy that exact file only after protected session
preparation. The configuration rejects mixed games and hot reload. It retains
the existing host/engine tick but skips renderer entry, gameplay polling and
the unqualified console channel. It is a development diagnostic, not the ME2 VR
renderer or a distributable sequel release; LE2 still uses the SDK submodule.

`ping` reports the actual host tick/thread and capabilities without engine-memory
reads. `observe` additionally returns the [structural observer](../../research/le2-diagnostic-observation.md)
under `payload.observation`. Authoritative `observeWorld` and `nativeConsole`
capabilities stay false; `observeStructuralWorld` identifies this diagnostic
sample. It does not establish a loaded save or permit gameplay actions.
`observe-save-menu` is a separate empty-input, read-only structural query under
`payload.saveMenu`. It follows the current player's manager/panel relationships
and reports menu state and bounded save-list evidence. It neither initializes a
menu nor selects a fixture. Missing, ambiguous or incomplete state stays unknown.
`observe-orbital` is an empty-input, read-only query under
`payload.orbitalObservation`. It samples the current orbital context, authored
planet/features, scan/probe/impact flags and available live resources. It cannot
enter the galaxy map, select a planet, launch a probe or evaluate a plot
conditional. Outside a coherent orbital mode it reports an unavailable reason.
The [observer and acquisition contract](../../research/le2-scanning-readiness.md)
records the structural camera evidence and unresolved native/VR gates.

`observe-weapon` is an empty-input, read-only request for the selected weapon's
structural identity and available ammo fields. It accepts no weapon target,
reload command or edits and requires no native-action permission. Transport
support alone does not establish runtime availability, reload eligibility,
effective reserve semantics or native/VR acceptance; the host must supply the
guarded observer and retain unavailable reasons.

`observe-input` is an empty-input, read-only query for the diagnostic input
publisher's context, poll and neutral-state evidence. It does not publish input.
The existing client can submit one bounded `pad-pulse` with a separate explicit
`--allow-native-input` flag and `--pulse-json FILE`. The file is a local JSON
argument source, not another runtime request channel. The pulse travels through
the same owned, correlated `request.control` exchange and request archive.

The exact ten-field schema is:

```json
{"schemaVersion":1,"context":"party-select","durationMs":100,"buttons":4096,"leftTrigger":0,"rightTrigger":0,"leftX":0,"leftY":0,"rightX":0,"rightY":0}
```

`context` is `default`, `party-select`, `party-confirm` or `loadout`; the runtime must independently admit the
actual supported context. `durationMs` is an integer from 1 to 1000 and measures
the hold starting at the first eligible nonconflicting controller poll. There is
a fixed 5000 ms activation wait from publication; failure to start within it
produces `start-timed-out` and neutral release, never a delayed press. `buttons` is
uint16 with only mask `0xF3FF` permitted; bits `0x0400` and `0x0800` are reserved.
Triggers are integers 0..255 and the four stick axes integers -32768..32767.
The pulse JSON is limited to 4096 UTF-8 bytes and must not have a byte-order mark.
`party-confirm` retains the same explicit native-input opt-in, numeric bounds,
correlation and no-retry rules. The name requests a context check; it does not
authorize an arbitrary modal or prove that a squad confirmation is ready.
`loadout` likewise requests an exact runtime context check and uses the same
input permission and bounds. Transport acceptance does not admit an arbitrary
ChoiceGUI panel, establish initialized controls, or prove a loadout selection;
the host's guarded context implementation and later observation remain required.
All keys are required. Unknown/duplicate keys, booleans or floats used as integers,
unsupported contexts and an entirely neutral pad are rejected. JSON key order and
whitespace do not affect acceptance; the client emits compact JSON.

For an already reviewed, owned session and reviewed pulse file:

```powershell
python tools/qa-le2/control.py C:\private\le2-run observe-input
python tools/qa-le2/control.py C:\private\le2-run pad-pulse --allow-native-input --pulse-json C:\private\pulse.json
python tools/qa-le2/control.py C:\private\le2-run observe-input
```

The Python API is `submit(run, "pad-pulse", pulse=fields,
allow_native_input=True)`. Console/menu/save permissions do not authorize a pad
pulse. Neither `--command` nor a pulse attached to another verb is accepted.
`dispatched` means the state was **published**, not that XInput consumed it or
that gameplay changed. Subsequent read-only input observations and appropriate
state/capture evidence establish those separate facts. A result claiming
`completed` for this action is refused. Timeout, uncorrelated response, or a
producer error preserves pending uncertainty; use `collect` to reconcile, never
resubmit automatically. The ordinary archived request already pins the full
pulse and run identity, so no additional action sidecar or runner is introduced.
Offline client/codec tests do not establish native input acceptance.

The diagnostic host implements these verbs through the existing `GamepadBridge`.
No hook is installed by `observe-input`: the first explicit, owned `pad-pulse`
attempt installs only the game's already loaded XInput exports. Admission needs
a stable supported engine context and a completely available bridge. Each pulse
has a process-local, never-reused token correlated with the ordinary request's
run, lease and sequence. A second pulse is refused until the first has completed
its bounded neutral-output window. There is no pulse queue or automatic retry.

`payload.inputObservation` contains `context`, `bridge` and `cancellationReason`.
An explicit `observe-input` also attaches read-only `partyCompletion` diagnostics
for the source-backed party-selection completion action. This is unavailable
outside the exact current callback relation. It reports a validated reflected
completion flag, inherited action state and bounded parent membership; none of
these observations changes pulse admission or invokes the callback. Post-pulse
responses and per-tick checks omit this additional inspection. See the
[source contract](../../research/le2-party-confirm-input.md) and
[SDK checks](../../tests/le2-observation/PartyCompletion.md).
The bridge's `source` reports publication, activation deadline, actual hold
expiry (`0` until the first delivered press), requested duration/pad, eligible
poll counts and neutral writes. `neutralFirstMs`, `neutralLastMs` and
`neutralDeadlineMs` describe a fixed 250 ms window starting with the first safe
neutral output; the deadline saturates at the clock's representable limit.
`neutralMs` remains the latest neutral-write timestamp. `output-written` and
`neutral-output-window-complete` describe hook output, not engine consumption. Compare
normal scene captures and authoritative state before and after each action.
An empty poll history remains unproven, even after a dispatched response.

The host compares freshly sampled player/mode/panel context every engine tick
while input is pending, cancelling permanently on a change. Ownership is also
rechecked during throttled request polling (approximately 250 ms). Hook-side
expiry uses monotonic time independently of that polling. Physical pad activity
or errors stop injection and preserve the original result. Neutral release
is repeated for eligible nonconflicting polls during that fixed window, with a
stable release packet. Completion requires another safe neutral write at or after
the deadline; elapsed time alone does not unblock replacement. Closing/disabling
the endpoint cancels admission while resident hooks
remain available to deliver neutral release. Diagnostic file hashing and context
sampling are excluded from performance measurements. Native delivery, bindings,
menu effects and movement still require a protected run; these code paths alone
do not promote any scenario.

`observe-resources` is an empty-input, read-only query under
`payload.resourceObservation`. It samples the current player's live inventory
twice, with independent availability for credits, medi-gel, four minerals,
probes and fuel. It never substitutes the serialized save cache. See the
[source and identity limits](../../research/le2-resource-observation.md) and
[offline state exporter](export-state.md). The integrated diagnostic host
advertises `observeResources`; the offline producer fixture keeps it false.
An available value is a point-in-time observation, not save/reload proof.
The diagnostic host separately opts into `probe-fov`, an explicit one-entry
native scalar query described below. Its availability is not `nativeConsole`
permission and does not make `observe` a native call. A client/codec build alone
does not enable the host; default gates and the offline producer fixture keep
the probe disabled. Reviewed builds can additionally enable `nativeMenuActions`
for the two explicit menu actions below. Only the integrated host advertises
these runtime capabilities; a new client alone cannot enable them. Default gates
and offline producer fixtures keep native menu actions disabled.
The later pause/Save-menu slice advertises `observeSaveFlow` and
`nativeSaveMenuActions` separately; it shares the explicit menu action gate.
The subsequent New Save slice advertises `nativeSaveNew` with a separate
permission from menu opening. These capabilities describe implemented routes,
not eligibility of the currently loaded scene.

The host currently hashes the executable and module synchronously around each
request. Keep these setup queries outside performance windows. Admission checks
the current UTC time after file validation, so slow IO cannot admit an expired
request; an admitted request may publish a late result for `collect` to reconcile.

## Protected native sequence

1. Coordinate the game slot and run `session.py prepare PLAN NEW_RUN`. The plan
   should protect the complete profile, LE2 configuration and Win64, plus the
   launcher directory if it may write logs/configuration during the run. A failed
   preflight is not permission to close another game's process.
2. Inspect LE2's actual Bink/ASI loader before deploying. Stock RAD Bink cannot
   load the ASI; a loader installed in the trilogy launcher does not install one
   in ME2. The successful diagnostic reused our clean-built `MEVR-Loader`, with
   stock Bink preserved as `bink2w64_original.dll`, entirely within the backed-up
   Win64 directory. Record and verify both loader and original hashes. Then
   deploy the hash-verified diagnostic ASI inside LE2 Win64/ASI. Preserve the
   build receipt and prove actual mapping; file presence is insufficient.
   Do not run the LE1 deploy/session
   wrapper unchanged or modify OpenXR settings for this diagnostic.
3. Use [launch.ps1](launch.md) with a reviewed capability receipt, or its explicit
   one-attempt selection probe. It records and binds the exact new ME2 process.
   If automatic selection fails, retain the attempt and use the documented
   operator boundary; never retry an uncertain launch blindly.
4. Publish the attachment invitation below, then issue `ping` and `observe`.
   Preserve response bytes, actual loaded module identity and the host log.
   No response means no handshake proof, regardless of an ASI being present.
5. If the bounded native FOV probe is part of the authorized test, follow its
   explicit opt-in and one-entry rules below after observing the current target.
   Use the save-menu observation/action sequence below only when its capabilities
   are present in the reviewed host. Obtain [run-bound window captures](capture.md)
   and inspect them alongside each state observation. Selected-fixture/career
   mapping and loading that exact save require the separate frozen-fixture
   workflow below, now proven for one bounded manual fixture. Native save/reload and
   scenario actions remain separate implementation/validation boundaries. A title-screen handshake
   or opened load panel is not a fixture load. The
   [normal menu source handoff](../../research/le2-menu-load-source.md) identifies
   the route and remaining mapping gaps; source inspection is not call validation.
6. Close only the owned game through its normal exit path, verify it is closed,
   run `session.py reset NEW_RUN`, then run attachment `cleanup`. Preserve new
   saves and test artifacts in the run archive. If the game/ownership cannot be
   resolved, retain backups and report the exact blocker instead of restoring
   files into a live process.

## Using a connected diagnostic host

After protected preparation, launch, exact process binding and runtime attachment:

```powershell
python tools/qa-le2/control.py C:\private\le2-run ping
python tools/qa-le2/control.py C:\private\le2-run observe
python tools/qa-le2/control.py C:\private\le2-run observe-save-menu
python tools/qa-le2/control.py C:\private\le2-run collect
```

For a reviewed, already mapped monolithic diagnostic ASI, publish one invitation:

```powershell
python tools/qa-le2/control-attach.py C:\private\le2-run invite --host C:\game\ASI\LE2MEVR.asi --sha256 REVIEWED_BUILD_SHA256
```

The path/hash are placeholders for the actual controlled build. This command
checks the frozen session and mapped module, creates `control/attach-intent.json`,
then exclusively publishes `MEVR/le2-control.attach.json` under the verified
desktop user's local application-data directory. Its success means only
`invitation-published-handshake-pending`; the host must independently validate it
and publish its identity before `ping` can succeed. Do not fabricate `identity.json`.
The first producer supports a monolithic host/mod only: both identity hashes and
paths name that one ASI. A missing consumer times out/fails without enabling one.

After the owned game closes and `session.py reset` reports verified restoration:

```powershell
python tools/qa-le2/control-attach.py C:\private\le2-run cleanup
```

Cleanup requires a quiet desktop and a restored session. It removes only an
invitation byte-identical to this run's preserved intent. It retains another
owner's invitation, all run-local evidence and any unresolved command history.

`collect` reconciles a late result; it does not resend the request. Both ends must
explicitly permit native console commands. The diagnostic host keeps general
console dispatch disabled. `--allow-console` only opts the client in; it cannot enable the
host. Never use `streamlevelin`: both the client and the C++ dispatch gate reject
it. A console result marked `dispatched` proves only that dispatch returned, not
that a save loaded, ammunition changed or gameplay completed.

### Observe and open the normal pause and Save menus

After a proven normal fixture load, `observe-save-flow` returns current mode,
focused panel, pause/browser and Save-panel state. Check the
[source and supported-context contract](../../research/le2-save-flow-observation.md).
The supported pause route requires ordinary on-foot Default mode with no
focused panel or a verified closed, idle PowerWheel HUD. Inspect
`focusedPowerWheel` for its exact ownership, visibility and transition checks;
the tag alone cannot authorize the action. Active, transitioning and unsupported
focus contexts remain explicit refusals. Native acceptance of this additional
idle-HUD case remains pending.

```powershell
python tools/qa-le2/control.py C:\private\le2-run observe-save-flow
python tools/qa-le2/control.py C:\private\le2-run open-pause-menu --allow-menu-actions
```

After a `dispatched` return, observe again, capture the actual normal pause
browser, and review that it has initialized and Save is enabled. A zero raw
alert value alone cannot prove initialization. Keep the same paused world,
player, mode array, browser and effective Save page; a changed observed flow
invalidates its comparison receipt. Then request:

```powershell
python tools/qa-le2/control.py C:\private\le2-run open-save-menu --allow-menu-actions
python tools/qa-le2/control.py C:\private\le2-run observe-save-flow
```

The explicit request attests the caller reviewed the fresh UI/capture. It does
not require another user confirmation when the current task already authorizes
the action. The adapter derives the unique configured Save-type row and tag,
then uses the normal browser route. It accepts no caller-supplied target.
Capture and inspect the resulting Save panel and enumeration separately.
Each opener has an independent one-entry allowance and the same uncertainty
rules as the load-menu actions. **Neither opener submits New Save.** Continue
with the explicitly separate action below; do not bypass its normal preparation
and free-space checks with a console save.

### Submit New Save and preserve its output

After the same normal Save panel is initialized and its actual New Save control
has been reviewed as enabled, collect `observe-save-flow` and a current run-bound
capture. Require complete descriptors, supported structural readiness and the
same opened Save flow. The game owns its destination index and filename.

Use [save-evidence.py](save-evidence.md) to record the complete career directory
before submission. The baseline binds the current lease, frozen scenario,
observation and reviewed capture. Then request the empty-input action:

```powershell
python tools/qa-le2/control.py C:\private\le2-run save-new --allow-save-new
```

The client revalidates that baseline while holding its request lock and records
its SHA256 in `control/history/REQUEST_ID.save-baseline.json` before publishing
the request. Menu, fixture-load and console opt-ins do not grant this action.
The host separately enables its one-entry New Save route. The engine does not
read or authenticate the client's disk inventory; that responsibility stays
with the collector and caller, with explicit provenance.

The action sends the normal New Save event with a constructed empty SDK argument
array. The native path resets overwrite state, prepares the save, checks free
space and chooses its slot. No callback is fabricated or invoked directly.
`dispatched` means only intact normal return from submission. Error, timeout or
lost correlation retains uncertainty and forbids retries; the client will not
reuse a baseline that already has a subsequent SaveNew request.

Observe the actual newly created filename, then collect it with the bounded
post-save command described in [save-evidence.md](save-evidence.md). It checks
stable bytes, prior-file preservation and LE2 parsing, and writes a new private
artifact. Independently reopen that artifact using the pinned TSE oracle. Close
and restore the protected run, then use a fresh run to load the generated save
normally and compare scene/resources/story state. A parseable file or closed
Save panel alone does not prove callback completion or persistence after reload.

### Observe and open the normal load menu

First obtain `observe-save-menu` with no payload or native opt-in flag. Inspect
the exact current panel, its visibility/focus/input/initialization state, its
handler relationship and any pending enumeration or modal state. A cached
MainMenu handler, an entry-menu controller, or a nonempty stale list is
insufficient. Keep observation evidence associated with this process/lease;
addresses are not reusable client action handles.

If the current active panel is the ready PC splash and this action is authorized:

```powershell
python tools/qa-le2/control.py C:\private\le2-run advance-splash --allow-menu-actions
```

This requests the normal splash advance, including its authored transition.
After a terminal `dispatched` response, use `observe-save-menu` again and allow
the game's asynchronous transition to complete. Do not immediately chain the
next action. Only after a fresh observation identifies the ready active main
menu, with no unresolved traffic, request:

```powershell
python tools/qa-le2/control.py C:\private\le2-run open-load-menu --allow-menu-actions
```

This requests the normal Default-mode load-menu opener. It does not select a
career or save. Observe again until the actual Load panel and its asynchronous
enumeration are understood. The game may initially show career selection; an
empty or different list does not justify reopening the panel or manufacturing
completion callbacks. Do not infer a disk filename from a list index. The
[source handoff](../../research/le2-menu-load-source.md) distinguishes list
position, descriptor `Career`/`SaveName`/`Index`/`Type`, and unresolved file mapping.
The separately gated [frozen-fixture workflow](#reviewed-frozen-fixture-loading)
below can target a reviewed manual-save descriptor and has one bounded native
load proof. Opening this panel alone does not exercise that load path.

Both native actions accept **empty input only** and require the client's
`--allow-menu-actions` plus the host's independent permission. Each has its own
one-entry-per-process allowance; advancing the splash does not spend the
load-opener allowance or the FOV probe allowance. An allowance is spent just
before native entry and is never restored by success, error, timeout, a new
request ID or hot reload. Preflight rejection does not spend it, but requires
reviewing the reason and current state before any newly authorized attempt.

For either action, `dispatched` records a returned bounded native invocation;
it does **not** mean that the next panel is ready or that gameplay completed.
`rejected` is pre-entry refusal. Post-entry `error`, timeout, lost reply or
correlation failure retains uncertainty: preserve intent/evidence, do not repeat
the action, and use `collect` only for a late terminal response. `completed` is
not accepted for these native actions. Resolve uncertain sessions through the
protected lifecycle rather than deleting pending files to continue.

For a rendered-scene artifact at the observed stage:

```powershell
python tools/qa-le2/capture.py C:\private\le2-run --name load-menu-001
```

Review the PNG and its [capture receipt](capture.md), then associate both with
the same-run menu observation. A `captured` status proves only the capture checks;
`suspect-black`, `suspect-constant`, and failed frames remain evidence for human
inspection, not visual or gameplay acceptance. No capture result selects a save,
confirms its loaded state or validates stereo/headset behavior.

### Reviewed frozen-fixture loading

**One bounded native fixture load proven.** Candidate `71f17c7d` matched the
frozen manual descriptor, called the normal load path once, then observed the
Normandy world/player pawn and a reviewed image of Shepard on the crew deck.
See the [run evidence](../../research/le2-validation-evidence.md#frozen-manual-fixture-loaded-through-the-normal-menu).
This does not establish resource values, persistence, other fixtures or VR.
The narrow path reuses the initialized menu's
`SFXSFHandler_Load.LoadGame(index, GuiMan)`. It does not use QuickLoad,
DebugName matching, a console command or map travel. A reviewed integration must
supply both the engine helper and host binding; a client update alone cannot
enable it. Default gates and offline producer fixtures keep native loading off.

Before preparing a new protected run, add optional `fixtureSelection` to its
plan. The following schema illustration uses placeholders, not usable hashes or
an established career/filename mapping:

```json
{
  "fixtureSelection": {
    "schemaVersion": 1,
    "descriptor": {
      "career": "OBSERVED_CAREER",
      "saveName": "OBSERVED_SAVE_NAME",
      "index": 99,
      "type": 0
    },
    "fixtureSha256": "64 lowercase hexadecimal characters",
    "targetRelativePath": "Save/ME2/OBSERVED_CAREER/OBSERVED_SAVE_NAME.pcsav",
    "exeSha256": "64 lowercase hexadecimal characters",
    "evidence": {
      "runId": "UUID of the reviewed observation run",
      "observationSha256": "SHA256 of the preserved observation artifact",
      "captureSha256": "SHA256 of the reviewed PNG",
      "reviewedBy": "reviewer identity and concise mapping basis"
    }
  }
}
```

The reviewer must establish the complete descriptor-to-file mapping from the
preserved observation, rendered menu and exact staged file. Evidence metadata is
a **reviewer attestation**: the host validates its shape and binds its values;
it does not read the image or independently prove what it depicts. Keep those
private artifacts available for inspection. The earlier observation run may be
different from the new run, but current process, file and menu checks still apply.

Only manual Type 0 and a nonnegative descriptor Index are supported. Career and
SaveName must be nonempty bounded UTF-8 filename components; separators,
controls, reserved device names and trailing dots/spaces are refused. The target
must be exactly `Save/ME2/<career>/<saveName>.pcsav`, with forward slashes, and
must match the plan's existing `fixture.targetRelativePath`. This is a
reviewed binding constraint, **not** a rule for generating SaveName from Index.
Do not infer a filename from either descriptor Index or UI row.

Before engine admission the host verifies the selected fixture hash against
`plan.fixture.sha256`, the frozen session hash, staged target bytes and
`run/fixture.pcsav`; it also checks the profile/target identities and admitted
executable hash. Selection and evidence are frozen with the protected plan;
do not patch an existing run to repair a rejected binding. Missing or invalid
binding rejects `observe-fixture` and `load-fixture` while leaving unrelated
ping/menu diagnostics usable. A valid binding does not prove current menu
readiness or a loaded fixture.

Use the normal menu workflow above and freshly observe the active Load panel.
This slice does not select another career or navigate a career-picker screen.
`CurrentCareerIndex=-1` is valid for the default career; that value, list contents
or a cached handler alone cannot establish the current UI phase. Retain current
panel/capture evidence and require the supported initialized main-menu/default
load context, with no unresolved enumeration or modal state.

```powershell
python tools/qa-le2/control.py C:\private\le2-run observe-fixture
```

This empty-input read-only query matches the frozen complete descriptor against
a bounded list of at most 512 rows. It requires a unique current match; duplicate,
missing, malformed or oversized state is not a selectable fixture. It does not
use a client-supplied row, pointer, path or descriptor. Review the returned
binding/context/match evidence, including `candidateAvailable=true` and
`ownedBaseRouteObserved=true`, before a separately authorized request:

```powershell
python tools/qa-le2/control.py C:\private\le2-run load-fixture --allow-fixture-load
```

This also accepts empty input only. The independent client opt-in and host
permission are both required; `--allow-menu-actions` does not authorize loading.
The host rechecks its file binding and the engine re-finds the exact current
match and revalidates its handler, mode and metadata before the normal call.
There is one admitted fixture-load entry per process, separate from the splash,
menu-opener and FOV allowances. It is spent immediately before entry and cannot
be reset by timeout, request IDs, success, error or hot reload.

`rejected` is pre-entry refusal. `dispatched` means the bounded native call
returned with its checks satisfied; it does **not** mean the save loaded.
`completed` is not a valid terminal result for this native action. Post-entry
`error`, timeout, lost reply or correlation failure preserves uncertainty and
must not trigger a retry. Preserve the result and pending intent; `collect`
only reconciles an independently arriving late response. An error disables
further host admission. Follow protected close/restoration if reconciliation
cannot resolve the outcome.

After `dispatched`, obtain fresh state and a new [run-bound capture](capture.md)
while the asynchronous load completes. Compare observed world/player/starting
state with the frozen fixture and scenario, preserving provenance and unknowns.
Neither the matching descriptor nor the call return substitutes for this proof.
The successful run also had two nearly black pre-load captures whose machine
status was `captured`; review rejected them as visual proof. Always inspect the
actual image. Next validation must establish normal save/reload persistence and
bounded resource values. Scenario behavior and VR acceptance remain separate gates.

### Explicit scalar FOV probe

Use this only within an authorized protected native session with the reviewed
diagnostic host mapped and attached. First use `ping` and `observe` to record the
current build, target and structural evidence. Then explicitly request:

```powershell
python tools/qa-le2/control.py C:\private\le2-run probe-fov --allow-probe-fov
```

`probe-fov` accepts no command payload. The client flag and the host's independent
`Gate(..., allowConsole=false, allowProbeFov=true)` are both required. Neither flag
grants general console access. The host invokes only the reviewed `GetFOVAngle`
path after its current thread, controller, metadata and passive target-witness
checks pass. This invokes game script; it is not a read-only memory observation.
Inspect the correlated `payload.probe` evidence rather than assuming the command
name proves safety or success.
An advertised `probeFov` capability identifies the implementation; it does not
prove current prerequisites or an unused native-entry allowance.

There is at most **one admitted native entry per process**. The host spends its
one-shot immediately before entering the call; successful return, failure or
lost reply never resets it. Preflight rejection does not spend the native-entry
allowance, but does not justify a retry loop. Preserve the reason and resolve its
prerequisite explicitly before any newly reviewed request.

- `rejected` means the probe did not enter native code. Inspect the admission
  reason; a disabled capability, unavailable target or missing proof is not a
  successful FOV observation.
- `dispatched` means the scalar call returned and the host's bounded return
  checks passed. Preserve the value and provenance. This is evidence for that
  one call, not gameplay completion, save loading, general native-console safety,
  stereo correctness or headset acceptance.
- `error` after entry means the result is uncertain. The host disables further
  admission; the client archives the error and retains pending intent. A timeout,
  missing reply or correlation failure is also uncertain. Do not resend, delete
  intent files, reset the gate or hot-reload to regain the allowance.

Use `collect` only to reconcile an independently arriving late terminal result;
it does not rerun the call or turn an error into success. The client refuses
`completed` for this verb and preserves uncertainty. If no authoritative terminal
result can resolve it, retain the evidence and close/reset the owned protected
session through the documented lifecycle. A new process is a separate reviewed
attempt, never an automatic retry. Ordinary `ping`/`observe` and the negative
helper's empty `ping` probes do not spend the native-entry allowance.

### Live rejection checks

Once a normal ping succeeds, use the bounded negative-control helper:

```powershell
python tools/qa-le2/control-negative.py C:\private\le2-run wrong-lease
python tools/qa-le2/control-negative.py C:\private\le2-run expired
python tools/qa-le2/control.py C:\private\le2-run ping
```

Both probes send only an empty `ping`: one with a deliberately different lease,
one with an expired deadline. Success requires the exact rejection reason, the
producer's actual session identity, correlated request fields and consumption of
the request. A reply that echoes the invalid lease or reports completion fails.
These checks establish admission behavior, not gameplay or native-call safety.

The helper shares the ordinary client lock, sequence and pending-intent files.
Unresolved traffic blocks both clients. After timeout, use
`control-negative.py C:\private\le2-run collect`; it inspects the archived
request and late response without resending. Unexpected responses retain their
bytes and pending intent. Do not delete those files to retry. The normal client's
`collect` refuses a negative-control intent so it cannot mistake an unexpected
completion for a successful rejection. Run the final normal ping only after both
negative controls finish successfully. Live results must be recorded separately
from the temporary-files/C++ endpoint regression checks.

The client checks the owned session lock, frozen plan, actual process PID/start/
path, executable hash, and mapped host/mod paths and file hashes before sending
and accepting a response. Windows process-start identity preserves all seven
fractional digits as an integer FILETIME. Hashes describe the files at mapped
paths, not a hash of relocated process memory. Controlled deployment and the
runtime's independent identity checks remain required.

## Producer contract

`control/identity.json` contains `identity` with exactly these string fields:
`run`, `lease`, `pid`, `start`, `exe`, `host`, `mod`, `game`; `game` is `LE2`.
It also contains absolute `hostPath` and `modPath` naming the actual loaded
modules. The runtime derives its own process/module identity and checks the
protected run's ownership. It must not merely echo identity supplied in a request.
Each new gate/process generation uses a new lease; reusing a lease after losing
its replay history is forbidden. Register the consumer before the first host tick
and run it on that game thread. Do not call the legacy unqualified request poller.

Wire files use `MEVR-CONTROL/1\n`, then ordered `key=value\n` headers:

```text
kind,run,lease,pid,start,exe,host,mod,id,sequence,deadline,verb,status,bytes,game
```

One blank line separates the headers from exactly `bytes` payload bytes. UUIDs
and SHA256 strings are lowercase; numbers are canonical unsigned decimal strings.
`deadline` is UTC Unix milliseconds, bounded to 30 seconds by the gate. Payloads
are at most 16 KiB without NUL; a console command is at most 4 KiB without ASCII
control characters. `ping`, `observe`, `observe-save-menu`, `observe-resources`, `observe-orbital`, `observe-input`, `observe-weapon`, `observe-save-flow`,
`observe-fixture`, `load-fixture`, `probe-fov`, `advance-splash`, and
`open-load-menu`, `open-pause-menu`, `open-save-menu`, `save-new` requests require an empty payload.
Unknown, duplicated, reordered, partial or trailing data is
rejected. C++ and Python implementations have executable byte-conformance tests.

Publish `request.control` and `result.control` atomically without replacement.
The runtime consumes the complete request before executing, calls `Gate::Claim`
before dispatch, and never releases a claimed sequence for retry. It checks run
ownership before every operation. Responses retain all correlation fields and
use the runtime's verified identity. Status meanings:

- `completed`: a read-only ping/observation completed; forbidden for `console`,
  `probe-fov`, `advance-splash`, and `open-load-menu`, and never gameplay acceptance.
- `dispatched`: native dispatch returned, or a `pad-pulse` was published to the
  existing bridge (not yet proof of a poll, release or gameplay effect); for `probe-fov`, the host's bounded
  scalar checks also passed. For menu actions it does not establish panel
  readiness. None establishes gameplay completion.
- `rejected`: authoritative rejection before dispatch; no native effect occurred.
- `error`: execution outcome uncertain. Preserve pending intent and block new
  commands until an authoritative terminal response can reconcile it.

The client archives intent and responses under `control/history`, keeps an
exclusive `client.lock`, and retains `outstanding.json` after timeout, mismatched
identity or error. Error evidence uses a separate `.error.result` file so later
reconciliation cannot erase it. An interrupted client lock or unresolved outcome
requires explicit recovery; do not delete it and resend a command. A new protected
run can be created after the owned game closes and restoration is verified.

## Offline verification

```powershell
cmake -S tests/le2-control -B local-tools/le2-control-build -A x64
cmake --build local-tools/le2-control-build --config Release
ctest --test-dir local-tools/le2-control-build -C Release --output-on-failure
$env:LE2_CONTROL_CPP = (Resolve-Path local-tools/le2-control-build/Release/le2-control-tests.exe).Path
python -B tools/qa-le2/test-control.py
python -B tools/qa-le2/test-control-attach.py
```

The tests use temporary files and fake peers. They verify exact identity,
malformed frames, replay rejection, timeouts, error recovery, mapped-file checks
and cross-language encoding. They neither launch ME2 nor prove game-thread calls,
scene loading, visuals or VR controls.
