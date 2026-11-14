<!-- SPDX-License-Identifier: MIT -->
# Export an archived resource starting state

`python tools/qa-le2/export-state.py RUN --request UUID --case CASE_ID`

This offline helper reads an archived, completed `observe-resources` exchange.
It never launches a game, inspects live processes, sends requests or reads current
resource values from saves. It creates two new files:

- `RUN/artifacts/state-UUID.json`: normalized `runtime-observation` state, full
  scenario identity, unavailable fields and original observation provenance.
- `RUN/artifacts/state-UUID.check.json`: existing `scenario.check_start` results,
  completeness failures and a hash of the exported state.

Exit **0** means the observed starting predicates matched and all resource/map/
position fields were available. Exit **2** preserves a partial or mismatching
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
resource observation are retained. The archive establishes correlation and
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

Run synthetic tests with `python tools/qa-le2/test-export-state.py`. Set
`LE2_CONTROL_CPP` to the matching C++ codec test executable to additionally check
the generated archived frames through the real C++ decoder. Tests use temporary
fake files; none is native evidence.
