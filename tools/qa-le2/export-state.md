<!-- SPDX-License-Identifier: MIT -->
# Export an archived resource or weapon starting state

`python tools/qa-le2/export-state.py RUN --request UUID --case CASE_ID`

This offline helper reads one archived, completed `observe-resources` or
`observe-weapon` exchange. The correlated request verb selects the normalizer;
the CLI and frozen-session validation path are shared.
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
| `fields.effectiveReserve` | `unavailable.weapon.effectiveReserve` | The current producer does not resolve the effective getter. An available value is rejected; neither effective reserve nor `weapon.reserve` is fabricated. |

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

Current weapon exports necessarily retain unavailable effective reserve. They
therefore return exit 2, even when the available weapon values match selected
predicates. A scenario requiring map/resources will also report those missing
from this weapon-only artifact. This is a preserved partial observation, not
native scenario acceptance. All existing visual/fixture/persistence/VR claims
remain false; native acceptance is not promoted by exporting.

Run synthetic tests with `python tools/qa-le2/test-export-state.py`. Set
`LE2_CONTROL_CPP` to the matching C++ codec test executable to additionally check
the generated archived frames through the real C++ decoder. Tests use temporary
fake files; none is native evidence.
