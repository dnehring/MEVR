<!-- SPDX-License-Identifier: MIT -->
# Export an archived diagnostic starting state

`python tools/qa-le2/export-state.py RUN --request UUID --case CASE_ID`

This offline helper reads one archived, completed `observe-resources`,
`observe-weapon`, `observe-orbital` or `observe-dialogue` exchange. The correlated request verb selects the normalizer;
the CLI and frozen-session validation path are shared.
It also accepts the bounded dialogue-history observation/final-stop snapshots
described below, using the same archived exchange checks.
It never launches a game, inspects live processes, sends requests or reads current
resource values from saves. It creates two new files:

- `RUN/artifacts/state-UUID.json`: normalized `runtime-observation` state, full
  scenario identity, unavailable fields and original observation provenance.
- `RUN/artifacts/state-UUID.check.json`: existing `scenario.check_start` results,
  completeness failures and a hash of the exported state.

Exit **0** means the observed starting predicates matched and the exported
observation had no unavailable fields. Exit **2** preserves a partial or mismatching
state and its failed checks. Exit **1** means an invalid archive/binding/contract,
or an existing output. No existing artifact is replaced. If publication is
interrupted between files, preserve the partial pair for review; rerunning does
not overwrite it.

The request/result must have identical run, lease, PID, exact Windows process
start FILETIME, executable/module hashes, request ID, sequence, deadline and verb.
The exporter checks `control/identity.json`, one identical consumed request,
the frozen plan/context, scenario, recipe, prepared fixture and preparation
receipt. It uses `session.load_context(..., require_lock=False)` and the scenario
auditor's frozen-session binding, so restored runs remain usable without a live
game or ownership lock. Current installed files and staged profile saves are not
re-read: restoration may legitimately remove or replace those files.

`deployment.json` must contain a full `candidateCommit` and `asiSha256` matching
both monolithic wire module hashes. This is a reviewed deployment **attestation**,
not independent proof that a binary was compiled from that commit. The explicit
`gameVersion` is `exe-sha256:<hash>`, not an invented release version. Raw archive
hashes, runtime tick/thread/monotonic sample time, wire identity and original
observation are retained. The archive establishes correlation and
integrity against these receipts, not protection against an author forging an
entire set of mutually consistent evidence.

Only available values from the same stable context enter `state.resources`.
Integers must be nonnegative int32 values, fuel finite/nonnegative float32, and
each accepted number must agree with its sampled bits and declared SDK field.
Missing or unavailable values are omitted and recorded separately; they never
become zero. A malformed value claimed as available is an invalid contract
(exit 1), not a partial observation. Map and position come from that same
observation. Malformed JSON/wire data and contradictory field provenance are refused.

This export does **not** prove the rendered scene, loaded fixture identity,
resource persistence, input behavior, VR operation or complete scenario
acceptance. Keep the separate scene review and full evidence audit gates.

## Weapon observations

The [read-only weapon observer](../../research/le2-weapon-observation.md) emits
individually available raw fields and a source-derived loaded count. Only a
completed read-only diagnostic-host `observe-weapon` result with its declared
capability and exact version-1 contract is accepted. A rejected/error/dispatched
result cannot become state evidence through this exporter.

| Producer field | Normalized field | Meaning |
| --- | --- | --- |
| `selectedWeapon` | `state.weapon.identity` | Exact class, name, raw nameNumber, object-table index and heavy ancestry flag. Process-local identity, not a durable weapon ID. |
| `fields.loadedCount` | `state.weapon.loaded` | Verified available integer derived by float32 capacity-minus-used, then truncation toward zero. The exporter checks the declared source, dependencies, range and arithmetic. |
| `fields.magazineCapacity`, `fields.magazineUsed` | Same names under `state.weapon` | Exact finite float32 raw samples. Invalid-for-loaded finite values remain raw evidence; no clamping or legal-capacity assumption. |
| `fields.reserveStored` | `state.weapon.reserveStored` | Raw signed int32 `CurrentSpareAmmo`. Negative samples remain raw evidence, not an assertion that ammunition is legal. Never mapped to `weapon.reserve`. |
| Sampled boolean flags | Same names under `state.weapon` | Exact boolean flags, not reload eligibility, native state-machine or completion proof. |
| `fields.effectiveReserve` | `state.weapon.effectiveReserve` when available | Source-derived float32 conversion of nonnegative raw reserve, only with the exact audited declaration policy, class, SDK/package pins and process-local function/property indices. The exporter verifies conversion and dependencies. No native getter or live bytecode proof; never aliased to `weapon.reserve`. |

Missing/unavailable fields retain their reasons and original samples in
`unavailable`; they are omitted from normalized values. Unavailable context
never produces a zero-filled weapon or a selected identity. Malformed fields
claimed as available, source mismatches and contradictory derivation are invalid
contracts (exit 1). World/controller/pawn/inventory identities and exact selected
weapon metadata remain in `provenance.runtimeObservation`, along with the full
original observation. No addresses or wire handles are added.

`provenance.observationVerb` identifies the query. `normalizedSemantics` records
the loaded derivation and raw-reserve distinction; `combinedObservation=false`
states that this artifact contains one exchange. Separate resource and weapon
queries produce separate artifacts with their own request/tick/time. The
exporter does not merge them, reuse an earlier map sample or claim a shared tick
or coherent simulation snapshot.

Unsupported classes, overrides, malformed/changing declarations or negative raw
reserve retain unavailable effective reserve and return exit 2. Supported samples
can expose the derived field; this does not grant reload eligibility. A scenario requiring map/resources will also report those missing
from this weapon-only artifact. This is a preserved partial observation, not
native scenario acceptance. All existing visual/fixture/persistence/VR claims
remain false; native acceptance is not promoted by exporting.

Run synthetic tests with `python tools/qa-le2/test-export-state.py`. Set
`LE2_CONTROL_CPP` to the matching C++ codec test executable to additionally check
the generated archived frames through the real C++ decoder. Tests use temporary
fake files; none is native evidence.

## Orbital observations

The same command accepts a correlated, empty `observe-orbital` request and
completed read-only diagnostic-host result. The producer contract is
[`ObserveOrbital`](../../src/engine/Le2OrbitalObservation.inc); see the
[scanning acquisition plan](../../research/le2-scanning-readiness.md) for its
source and native acceptance limits. No new live request or exporter is needed.

| Producer field | Normalized field | Meaning |
| --- | --- | --- |
| `resources` | `state.resources` | Same inventory scalar/source/sample-bit validation used for `observe-resources`, sampled inside this orbital observation. Unavailable values stay absent, including probes. |
| `context.currentMode`, `activeWorld` | Same names under `state.orbital` | Validated mode 10 and raw camera active-world integer. The integer is not promoted to a saved planet ID. |
| Context object samples | `state.orbital.context` | Process-local class/index identities. Full original object samples remain in provenance; addresses are not exposed as normalized handles. |
| `planet.object` | `state.orbital.planet.identity` | Process-local selected planet class/index, not a durable galaxy/save identifier. |
| `planet.mapName` | `state.orbital.planet.mapName` | Exact bounded BioPlanet landing-map text, including original casing or legitimate empty text. It is **not** the currently loaded world map. |
| Planet IDs and complete feature rows | `state.orbital.planet` | Raw signed metadata, enum bytes and sequential bounded rows. Conditional/transition IDs and scaled amounts do not establish evaluated availability, discovery, mineral yield or mission unlock. |
| `state` flags and floats | Same names under `state.orbital` | Current scanning/probe/impact/rescale/exit booleans and finite float32 times/distance. No inferred probe completion or fixed cost. |

The normalizer validates the producer's source labels, current mode, object
sample shapes, strict boolean/integer types, float32 values and complete feature
list (at most 64 rows). It requires the producer's discovery, saved-ID,
persistence and durable-identity claims to remain false. The exact minimal
`orbital-payload-budget-exceeded` unavailable shape is also retained, with no
manufactured state. The offline host fixture's intentionally abbreviated
non-engine placeholder is not an accepted production observation contract.

Orbital exports leave `state.map` and `state.position` unavailable: this producer
does not sample those fields. They therefore return exit 2 even when all orbital
and inventory fields are available. Do not substitute the landing map, uppercase
its spelling, or merge a separate resource/world query to make those checks pass.
The full orbital observation, request/tick/time and frozen-session provenance
remain attached to this one exchange; `combinedObservation` and all acceptance
claims remain false. Scene review, discovery evaluation, normal save/reload and
VR acceptance are separate gates.

Focused tests cover a valid archived orbital exchange, exact map casing/empty
text, unavailable/context-budget responses, invalid sources/scalars/object
identities, complete-list bounds/order, existing receipt/consumption guards,
and byte-identical real C++ wire-codec roundtrips. These are synthetic offline
tests, not native orbital evidence.

## Dialogue observations

The existing command also exports a completed, correlated `observe-dialogue`
exchange. It uses the same frozen save/scenario/recipe, process/module identity,
consumed-request and deployment-receipt checks as the other observations. The
producer is [`ObserveDialogue`](../../src/engine/Le2DialogueObservation.inc).

Only stable recorded fields enter `state.dialogue`:

- `context` and `conversation.identity` retain process-local class/index pairs.
  Original pointer strings remain in raw provenance, not normalized handles.
  An unavailable optional pawn remains absent with its reason in `unavailable`.
- `conversation` retains bounded entry/reply counts, current/selected node
  records and the raw finite float32 `nodePlayTimer`. Node index `-1` is preserved
  as explicitly unavailable; no export or transition ID is invented. Available
  node indices must be within the corresponding sampled array of at most 4096.
  Its required `name` and optional direct `package.name` preserve exact case
  with the producer's 1–127 printable ASCII bound. A validated direct package
  also retains its process-local class/index identity; an unavailable package
  stays absent. These labels are not package hashes, loaded-file proof or a
  durable conversation identity, and no additional outer traversal is inferred.
  Required `conversation.nameNumber` retains the raw signed int32 FName instance
  value separately from the base `name`; no displayed suffix is synthesized.
  Missing values are not defaulted to zero. The direct package name remains
  unnumbered under the producer's existing package policy.
- `interrupt` retains the sampled reply, start/window remaining values, raw
  enabled/activated flags and enum/name. Unknown byte enum values stay unknown;
  negative finite timers are not rewritten or classified as an expired window.
- `gui` retains independently available manager/panel/handler identities and
  display/input flags. Missing GUI does not invalidate the conversation sample.
  Visibility, focus and initialization flags are observations, not admission.

The normalizer rejects malformed identities, bool-as-number, wrong scalar
ranges, non-float32/nonfinite timers, contradictory node indices, partial data
inside unavailable samples, or positive producer legality/acceptance/history
claims. The full original observation remains in
`provenance.runtimeObservation`, bound to this request's tick and sample time.
`combinedObservation=false`: no samples are merged into a timeline.

`state.map` and `state.position` remain absent with explicit unavailable reasons;
this producer does not prove world-map containment. A dialogue export therefore
returns exit 2 even with all dialogue fields present. The resulting artifact
does not establish legal input timing, selected-choice acceptance, accepted
interrupts, complete event history, saved progression or visual/VR acceptance.
In particular, `activated=true`, `wasTriggered=true` and a sampled transition ID
are not promoted to an accepted-interrupt event. Scene review and independently
correlated event/transition evidence remain separate gates.

Synthetic tests cover complete and unavailable archives, optional pawn/GUI,
minus-one nodes, count/index/enum/type bounds, negative timers, false acceptance
claims, shared correlation/fixture/consumption guards, nonreplacement and real
C++ wire-codec roundtrips. They do not constitute native dialogue evidence.

## Dialogue sample history

The same offline CLI accepts an empty `observe-dialogue-history` or
`stop-dialogue-history` request with a completed archived result. Observe must
declare `readOnly=true`; stop declares `readOnly=false` because it stops the
recorder, not because it dispatches gameplay. `start-dialogue-history` is not a
state export. None of these commands establishes accepted input.

The compact recorder contract is a ten-second capture at a 125 ms sampling
cadence, with at most 81 rows and four process-local identity epochs. The
consumer checks the exact ordered column schema, strict scalar types/ranges,
finite float32 bit patterns, node sentinel/export pairs, epoch references and
increasing row tick/time offsets. Unavailable rows retain their status and have
no state fields. Unavailable GUI cannot carry display bits. Epoch names and
object identities are descriptive samples, not durable handles.

`state.dialogueHistory` contains expanded rows, identity epochs and original
recorder counters. It preserves unavailable samples, skipped tick/cadence
counters, buffer drops and serialization omissions separately. Serialized rows
plus both omission counts must equal the reported sampled-row count. A compact
snapshot may retain the first and latest rows while omitting middle rows; the
consumer never fills those holes or claims the displayed rows are consecutive
engine samples. `endTimeSource` is `last-valid-tick-call-or-start`, so end clocks
are not asserted to be the exact stop-command time.

`adjacentSamples` reports raw tick/millisecond differences between adjacent
**serialized** rows. `elapsedExceedsCadence` is a comparison with 125 ms, not a
claim that a gameplay event was missed. Changed column names are reported only
when both endpoints are available and share the same recorded identity epoch.
At unavailable samples or epoch boundaries they remain null. Float bits and all
compact rows remain unchanged in `provenance.runtimeObservation`; no values are
interpolated, merged with separate queries or converted into interrupt events.

For started captures, the host's `dialogueHistoryStartRequest` UUID is checked
against its earlier archived request and completed result: same run, lease,
process and module identity; exact wire correlation; unique consumed request;
and matching recorder start clocks/configuration. The report retains both
start-frame hashes and calls this a host-correlated recorder start, not a
gameplay input. A missing, rejected, mismatched or unarchived start fails export.
The start response must describe the newly active recorder with empty rows and
epochs and zero end clocks/counters. A first sample may share the start tick;
the host's response timestamp can precede recorder start after ownership I/O.
It is not used as an upper bound on the recorder clock. Finished snapshots must
bound their final serialized row by their end clocks, and adjacent rows must
respect the minimum sampling cadence.
A genuinely never-started snapshot has zero clocks/counters, no rows/epochs and
no start UUID; it remains explicitly never started rather than a complete empty
event history.

The outer exported tick/time identify the query response, not every recorded
row. Each row retains offsets from the explicit recorder start clocks. A start
tick of zero is valid when `started=true`; it is never inferred to mean absence.

The normal archive/frozen-session checks, exclusive artifact publication and
map/position incompleteness are unchanged. History exports return exit 2 and
keep `historyComplete=false` and `acceptedInterruptProven=false`. Recorded
flags and sampled changes do not establish legal timing, accepted choices,
callback execution, progression persistence or VR acceptance. This is a bounded
review of recorded samples, not a second request/session workflow.
