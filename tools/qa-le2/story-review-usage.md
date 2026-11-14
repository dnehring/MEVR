<!-- SPDX-License-Identifier: MIT -->
# Offline story-state source review

`story-review.py` compares two LE2 saves with explicitly selected events from the scanner's scoped `--state-events` JSON report. It reuses the strict [LE2 reader and semantic diff](../le2-save.py); it never modifies saves, invokes game functions, stages a profile, acquires a native-session lock or launches a game. ME1 conversation testing can continue independently.

The result answers a narrow question: **are the saved endpoint values consistent with the selected, supported direct scalar effects?** A matching value cannot establish that an event fired, that the player performed an interaction, that a story recipe is coherent, or that progression is valid. Unchanged/idempotent values receive a distinct comparison label. The output always sets `eventFiredEstablished`, `nativeAcceptance` and `storyValidityEstablished` to false.

## Inputs and invocation

Keep saves, source reports, selections and review outputs private. First obtain a source report using the [read-only scanner](../conversation-scan/README.md), selecting the actual package directory and package name. Resolve effective mounted packages separately; the report does not imply whole-game completeness. For the verified Zanethu event, see [source findings](../../research/le2-galaxy-fixture-source.md).

```text
conversation-scan <LE2-cooked> <new-source.json> --game LE2 --state-events 4551 --only Startup_INT
python tools/qa-le2/story-review.py <before.pcsav> <after.pcsav> --source-report <source.json> --selection <selection.json> --output <new-review.json>
```

The selection pins the exact input bytes and the exact event/map/package identity. This illustrative selection is deliberately unbound; replace every placeholder with the inspected input's SHA-256. `id`, `mapPath` and `mapExportIndex` must come from that source report, not a guessed flag lookup.

```json
{
  "schemaVersion": 1,
  "beforeSha256": "BEFORE_SAVE_SHA256",
  "afterSha256": "AFTER_SAVE_SHA256",
  "sourceReportSha256": "SOURCE_REPORT_SHA256",
  "events": [
    {
      "id": 4551,
      "packageSha256": "INSPECTED_PACKAGE_SHA256",
      "mapPath": "PlotManagerAuto.StateTransitionMap",
      "mapExportIndex": 1742
    }
  ]
}
```

Optional `parameter` on a selected event supplies an explicit typed scalar operand for its `UseParam` effects. A boolean requires JSON `true`/`false`; an integer requires a signed 32-bit JSON integer; a float requires a finite numeric value representable as float32. The tool does not guess native conversions such as integer `1` to boolean `true`. If one parameter would need different unverified conversions across an event's elements, those comparisons remain unresolved. An unused supplied parameter is also flagged.

Selections may include multiple events, including the same event number in different maps when qualified by package hash, map path and export index. Repeated selection of the same qualified event is rejected. Multiple writes to the same plot field are unresolved: the tool does not infer execution order, repeated firing or intermediate values from two endpoint saves.

Output must be a new file distinct from every input. Existing output is refused. Inputs are read only. Review JSON records absolute private input paths, before/after/source-report/selection/adapter hashes, selected package paths/hashes and map IDs. Package identities are checked for agreement within the pinned source report; the tool does not reopen installation packages or claim to authenticate the report's producer. `packageBytesReverified` remains false.

## Supported direct-effect model

Field definitions were checked against LegendaryExplorerCore's [BioStateEventMap declarations at revision 95cc2891116e9c0e195e3d8feb670cbb1c944191](https://github.com/ME3Tweaks/LegendaryExplorer/blob/95cc2891116e9c0e195e3d8feb670cbb1c944191/LegendaryExplorer/LegendaryExplorerCore/Unreal/BinaryConverters/BioStateEventMap.cs). This documentation pin does not assert that the installed DLL was built from that revision.

| Element class | Accepted data fields | Review model |
|---|---|---|
| `BioStateEventElementBool` | `GlobalBool`, `NewState`, `UseParam`, `InstanceVersion`, `Type` | Assignment to `plot.booleans.<index>`; discriminator 0 |
| `BioStateEventElementInt` | `GlobalInt`, `NewValue`, `UseParam`, `Increment`, `InstanceVersion`, `Type` | Assignment or addition to `plot.integers.<index>`; discriminator 4 |
| `BioStateEventElementFloat` | `GlobalFloat`, `NewValue`, `UseParam`, `Increment`, `InstanceVersion`, `Type` | Assignment or float32 addition to `plot.floats.<index>`; discriminator 2 |

These exact field shapes are interpreted at instance version 0. Integer
elements also accept version 1, which appears in the pinned LE2 Veetor
records. The pinned LegendaryExplorerCore serializer reads the same four
integer-effect fields after the version header. Other versions, unknown fields,
wrong discriminators, non-boolean flags, invalid indices and numeric overflow
remain unresolved. Float operands and results are rounded to float32.

Function, consequence, local-variable, substate, conditional and other element types are retained as unresolved diagnostics, including their raw source data. Nothing silently drops an unsupported element or follows a guessed transitive dependency. An incomplete/error-bearing source report remains unresolved even if some selected effects can be compared. A selected source event must match exactly one report event and its package/map inventory entry.

## Reading the result

Each supported `directEffects` row contains the event identity, element index, exact source data, plot path, operation/operand, before/after values, expected value and comparison:

- `changed-to-expected-value`: the saved endpoint changed to the modeled value; the cause is unproven.
- `already-at-expected-value`: the before-save already had that value. This is idempotent agreement, not execution evidence.
- `missing-or-different-effect`: the after-save differs from the modeled value. It may indicate no execution, a different sequence or later activity; the tool does not choose a cause.
- `unresolved-multiple-writes`: endpoint saves cannot establish the ordering of overlapping effects.

`changeGroups` preserves **every semantic diff** returned by the existing save tool: selected direct fields, other plot/imported plot fields, journal, world and other fields. World includes map, transform, streaming, Kismet, doors, pawns and galaxy records. Resources, player/squad state and remaining metadata appear under other. Changes outside selected direct fields may be legitimate indirect effects or unrelated activity; the grouping is not a causal or safety judgment.

The top-level diagnostic is `unresolved` when any source/effect interpretation remains unresolved; otherwise it is `direct-effect-mismatch` or `direct-values-consistent`. `missingEffects` remains available even when another unresolved issue takes precedence. Other plot/journal/world changes are always listed; `direct-values-consistent` does not approve them or establish story validity.

Exit `0` means only that the supported direct values agree and no interpretation issue was detected. Exit `2` means mismatch or unresolved diagnostics; both still produce a review report. Exit `1` means invalid input/binding, read failure or output refusal. Do not use exit `0` as a gameplay acceptance gate. Correlate actual run identity, input, rendered scene, native logs, selected-save identity and ordinary progression separately.

## Taken and missed interrupt saves

For Veetor, start both attempts from the same prepared bytes and capture the
chosen reply or missed prompt. Save at equivalent points after dialogue. Select
event 4366 for Paragon or 4370 for Renegade in the existing source selection,
and add the missed outcome save's SHA-256 as `controlSha256`.

```text
python tools/qa-le2/story-review.py <start.pcsav> <taken.pcsav> --control <missed.pcsav> --source-report <source.json> --selection <selection.json> --output <new-review.json>
```

The report retains the two-save comparison and adds `controlComparison` with
both changes from the shared start and the taken-minus-missed difference. Exit
0 in this mode means that the differences match the selected source increments.
The comparison supports one direct integer increment per field. A shared delta
can cancel in the subtraction, which is why a matched runtime start, equivalent
saving point, rendered prompt and observed reply are still required. The report
does not establish that those conditions held or that the event fired.
See the [Veetor source evidence](../../research/le2-interrupt-acquisition.md#veetor-direct-transition-and-saved-outcome-review).

## Checks and limits

```text
python tools/qa-le2/test-story-review.py
```

The tests reuse the existing independently constructed synthetic LE2 wire fixture. They cover scalar assignment/increment, parameter requirements, idempotence, float32 comparison, unsupported effects/versions/fields, ambiguous writes, exact source/hash binding, CRC rejection, additional plot/journal/world changes and output preservation.

Optional `LE2_STORY_SAVE` and `LE2_STORY_SOURCE_REPORT` enable a test using a private real save and an actual scoped event-4551 report. The test copies the **same** save into both endpoints and expects missing or already-present effects, never proof of execution; it verifies the original remains unchanged. This is offline format/source integration evidence, not a naturally produced before/after run. No RON parser, plot-label importer, recipe writer, native observer or fixture promotion is included.
