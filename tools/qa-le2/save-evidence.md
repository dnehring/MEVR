<!-- SPDX-License-Identifier: MIT -->
# LE2 New Save disk evidence

`save-evidence.py` reads an owned, game-bound diagnostic run and its current
career directory. It never sends a game request or writes the profile. Its only
outputs are exclusive private files under the run's `artifacts` directory.

This collector supports the existing reviewed fixture workflow. The frozen
`plan.fixtureSelection` must bind a manual descriptor to exactly
`Save/ME2/<career>/<saveName>.pcsav`, matching the staged file and frozen
`fixture.pcsav` hashes. The complete observed Save list must contain that exact
descriptor once. An empty list cannot establish this collector's career binding,
even when the engine could otherwise create a first save. A descriptor index is
never converted to a filename.

Before the one normal New Save dispatch, obtain a completed `observe-save-flow`
response and a fresh capture with the existing run-bound capture tool. Review
the actual initialized Save screen and enabled New Save action, then run:

```powershell
python tools/qa-le2/save-evidence.py before RUN --case CASE --observation OBSERVATION_UUID --capture captures/REVIEW/capture.json --reviewed-by "Reviewer and short review note"
```

This publishes **`artifacts/save-baseline.json` once per run**. Existing baseline
files are never replaced. The receipt binds the scenario/case, preparation
receipt, fixture, frozen plan, deployment attestation, runtime lease/process
start and executable/module hashes, exact archived/consumed observation,
reviewed capture receipt and image hashes, client sequence cutoff, and a complete
career-tree inventory. The observation must report available, complete records,
the same opened Save flow, a bound New Save observation and structural support.
Manual, auto and chapter descriptors are retained with their actual types and
indices. The capture must have `captured` status and the same before/after
run/process/plan identity. The code verifies bytes and provenance; the reviewer
attests image contents and UI eligibility. It does not independently interpret
the image or prove that the descriptor semantically names the file.

The complete list may separately identify the reviewed UI-only New Save entry.
`observedRowCount` continues to count **all UI rows**, while `recordCount` counts
only the validated save descriptors in `records`. The optional `newSaveRow`
object must match this exact contract, including scalar types and field set:

```json
{
  "recognized": true,
  "recognitionPolicy": "le2-pc-save-empty-row0-v1",
  "uiRow": 0,
  "index": 0,
  "type": 0,
  "career": "",
  "saveName": "",
  "careerCount": 0,
  "careerCapacity": 0,
  "saveNameCount": 0,
  "saveNameCapacity": 0,
  "saveGameIsNull": true
}
```

This policy follows repeated native observations plus review of the corresponding
Save screen. The runtime must classify the first row using the full reviewed
signature and resample the entire list, including marker storage, before
reporting completeness. Empty names alone, a display label, or rejected-row
diagnostics do not authorize recognition. FriendlyName is not required to be
the UI's New Save label and is not a collector recognition predicate.

With a recognized marker, `recordCount` is mandatory and must equal the number
of records; total UI rows must equal that count plus exactly one, at most 512.
Without a marker, legacy complete observations may omit `recordCount`, and the
total must equal the record count. Unknown policies, null/duplicate/nonzero-row
markers, inconsistent counts and extra skipped rows are rejected. The exact
reviewed fixture tuple still must occur once among ordinary records, whose
nonempty names, career, types and uniqueness checks are unchanged. A marker-only
list cannot establish the fixture binding. UI descriptors are not independently
verified disk files; disk inventory and the reviewed fixture mapping remain
separate. The archived response hash seals this classification in the existing
baseline provenance, without changing the baseline or client-sidecar schema.

The control client calls `verify_baseline(run, *, bind=control.binding)` inside
its client lock before a separately authorized `save-new`. This function only
reads: it tolerates the caller's `client.lock`, checks frozen evidence and the
unchanged complete career inventory, and refuses pending, unarchived or retained
error/uncertain traffic and a prior SaveNew after the cutoff. Read-only traffic
may advance the sequence. The client records the exact baseline SHA in
`control/history/UUID.save-baseline.json` before publishing the action request.
The engine does not independently validate this disk baseline.

After a correlated, normally returned dispatch, identify the **actual newly
observed filename**, rather than guessing from `candidateNextIndex`:

```powershell
python tools/qa-le2/save-evidence.py after RUN --request SAVE_NEW_UUID --candidate ObservedNewFile.pcsav --reviewed-by "Reviewer observed this new filename"
```

The run must still be owned and game-bound to the same lease/process. Collection
requires the exact archived and uniquely consumed empty `save-new` request,
`dispatched` result, matching client baseline sidecar and a sequence after the
baseline. The result must say entered, normally returned and intact parameter
frame, while explicitly withholding engine disk verification and save completion.
An error or uncertain result is refused. There is no automatic retry or game call.

The candidate must be an ordinary bounded leaf absent from the baseline, and
the sole new file. All previous files and directory names must remain unchanged.
The collector reports changed/removed prior files, unexpected new files and
directory changes in `artifacts/save-UUID.json`, exits **2**, and does not archive
an accepted candidate. It treats changed modification times conservatively as
changes even when bytes match. Missing candidates also produce a failure report.
No failure report or partial archive is overwritten on another invocation.

On success, two bounded reads with before/between/after stat comparison must
agree byte-for-byte. The existing `tools/le2-save.py` adapter validates LE2 version
30, CRC32/BZIP2, schema bounds and full EOF. A final complete inventory and live
binding comparison precedes exclusive publication of:

- `artifacts/save-UUID.pcsav`: private captured bytes, with hash readback.
- `artifacts/save-UUID.json`: source identities, baseline/sidecar/request/result
  hashes, actual filename, changes and parser provenance.

Exit **0** means only a new candidate was archived and parsed by our existing
adapter. Exit **1** means invalid/changed contracts, uncertain control traffic,
unstable reads, parser rejection or filesystem failure. Outputs from interrupted
publication remain private evidence; the tool never replaces them. It does not
hold file handles to prevent subsequent engine writes: stable reads are a
bounded observation, not proof that asynchronous saving has completed.

Every career file is inventoried, including non-save files and nested entries:
at most 4096 entries, 32 MiB per file, 256 MiB total. Symlink/reparse paths,
case-insensitive collisions, device names and path traversal are refused. Control
archive scans are also bounded. Private save/image data and receipts stay outside
version control. The deployment receipt's candidate commit is a reviewer
attestation, not a cryptographic proof that the binary was built from that commit.

Callback completion, independent Trilogy Save Editor decoding, resource
persistence, normal reload, visible scene acceptance and VR acceptance remain
separate gates. This tool never marks those claims true. Preserve the run's normal
close and complete profile restoration receipts with any later evidence.

Offline verification uses synthetic temporary profiles and independently
constructed save bytes, injected process bindings, real protocol archives,
the real parser and the real dynamic client preflight import:

```powershell
python tools/qa-le2/test-save-evidence.py
# Optional existing codec executable; no game is launched:
$env:LE2_CONTROL_CPP = 'PATH_TO_LE2_CONTROL_TEST_EXECUTABLE'
python tools/qa-le2/test-save-evidence.py
```
