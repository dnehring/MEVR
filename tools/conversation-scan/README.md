<!-- SPDX-License-Identifier: MIT -->
# Read-only conversation inspection

This tool reads installed packages with LegendaryExplorerCore and writes private research output. It does not launch a game, edit a package, prepare a save or prove native/VR behavior. Raw output contains game data and must stay outside version control. See [fixture acquisition findings](../../research/le2-fixture-acquisition.md) for candidate scenes and unresolved story dependencies.

LE1 remains the default. Explicit `--game LE2` enables bounded LE2 package inspection, checks each opened package's game identity, and adds LE2 conversation/interrupt information. It requires a **new output file outside the input package directory**, preventing ordinary accidental overwrite of package files or previous evidence. Keep outputs in a private directory without links into the installation. Failure or no results in LE2 scan/class/sequence modes returns a nonzero status; partial output after an error is not a complete catalog.

## Build and check

Use .NET 10, the Windows desktop runtime and the locally installed LegendaryExplorer assemblies. `LexDir` points to the directory containing `LegendaryExplorerCore.dll`. Build into a new private directory rather than replacing another agent's scanner. The project references external assemblies without copying them into source control.

```powershell
dotnet build tools/conversation-scan/ConversationScan.csproj -c Release -o <private-build> -p:LexDir=<lex-directory> -p:BaseIntermediateOutputPath=<private-absolute-obj-directory>
<private-build>/conversation-scan.exe --self-test --game LE2 --lex-dir <lex-directory>
python -I tests/ConversationScanTests.py --scanner <private-build>/conversation-scan.exe --lex-dir <lex-directory>
python -I tests/ConversationSweepTests.py
```

`InspectionTests.cs` uses original synthetic property fixtures: string references, reply categories, node/transition IDs, high-precision timing, mismatched/missing arrays and sequence scope. No copyrighted dialogue or package binary is embedded. `ConversationScanTests.py` additionally accepts `--le2-cooked`, `--le1-cooked` and `--baseline-scanner` for read-only installed-corpus checks. Its generated output is temporary; these optional checks must report skipped corpus coverage when paths are omitted. The C++ runtime is unchanged.

## LE2 commands

Pass the installed LE2 `CookedPCConsole` directory as `<cooked>`. Choose a new private output for every command; pass `--lex-dir` if the build's recorded path is unavailable.

```text
conversation-scan <cooked> <new-mechanic.jsonl> --game LE2 --only BioD_OmgGrA_112Hangar_LOC_INT
conversation-scan <cooked> <new-track.txt> --game LE2 --class-dump BioEvtSysTrackInterrupt --only BioD_OmgGrA_112Hangar_LOC_INT
conversation-scan <cooked> <new-sequence.txt> --game LE2 --sequence BioD_OmgGrA_112Hangar_LOC_INT:Node_Data_Sequence
conversation-scan <cooked> <new-reload.txt> --game LE2 --sequence BioD_ProCer_100RezRoom:LS10_Reload_Heatsink
conversation-scan <cooked> <new-functions.txt> --game LE2 --script grep:Interrupt --only SFXGame
```

The default JSONL scan retains existing record kinds and graph adjacency. In LE2 mode it additionally emits:

| Record/field | Meaning |
|---|---|
| All records: `game`, `packageSha256`, `packageFile`, `exportIndex` | Selected/verified package game, actual file hash, full source path and package-local export index. Existing `package` names the package. The private source path distinguishes base/DLC copies; it does not assert mounted precedence. |
| Conversation node `text` | Numeric `StringRefProperty` ID, with integer-property fallback; this is a reference, not resolved dialogue text. |
| Node `exportId`, `transitionParam` | Authored node ID and state-transition parameter; missing properties remain null. These are not save-plot indices. |
| Entry `replyEdges` | Preserves each edge's `index`, `category` and numeric `paraphrase` string reference. Existing `replies` adjacency is unchanged. |
| Conversation `matineeSequence` | Exact package-local sequence reference `{path,uIndex}`. |
| New `interrupt` record | Exact track export, containing `interpData`, `group`, `sequence` references and matching `conversations` found through their actual `MatineeSequence` property. |
| Interrupt `keys` | Original paired-array index, `timeSeconds` and `showInterrupt`. Floats retain roundtrip precision. Missing/misaligned/nonfinite data emits warnings and null values rather than inferred bounds. |
| Cinematic/matinee `data` | Actual `InterpData` references reached through the interpolation action's `Data` variable link. |

Join within the same package hash: `interrupt.interpData` to a cinematic's `matinees[].data[]`, then `cinematic.props.m_nNodeID` to `conversation.nodes[]` or `replyNodes[]` `exportId`. Confirm that the conversation also appears in `interrupt.conversations`. This associates the authored track with a concrete dialogue node rather than guessing from an object name. The existing cinematic traversal follows at most four downstream action edges; a missing join remains unresolved, never inferred. A `uIndex` can identify an import (negative) or export (positive); do not assume every reference is an export.

`--sequence` additionally includes exact `Node_Data_Sequence` path components for LE2, so conversation interpolation sequences are visible. LE1's level-sequence filtering remains unchanged. LE2 `--class-dump` prints numeric string-reference values and roundtrip float precision. Its generic body deduplication remains; use exact package/language filters when collecting evidence.

## Verified scope and remaining gaps

The installed English mechanic package produced 90 records, including one conversation, one interrupt, 39 cinematic events and 49 matinees. Every entry retained a nonzero text reference; a Renegade interrupt reply edge was present. The visibility track linked through its interpolation data and cinematic event to an authored node. Its two stored times were approximately 5.429 and 7.4299107 seconds. The ProCer reload sequence exposed both a tutorial-reload event and a save action. These are authored-data observations, not native timing, reachability, save validity or controller acceptance.

LE1 default/explicit scans of the installed captain scene produced the same 10 records and were byte-identical to the previous scanner output. The existing synthetic conversation-sweep suite also passed. This is targeted regression evidence, not exhaustive LE1 catalog equivalence.

Still outside this bounded change:

- External LE2 TLK resolution, localization/DLC mount ordering, duplicate-package precedence and Wwise audio extraction. `--wheels` and `--vo-extract` reject LE2 rather than applying LE1 assumptions.
- Effective LE2 state-transition map resolution and full dependency/plot recipe reconstruction. `--state-events` and the old LE1-oriented `--references` search reject LE2. Parallel research located a base `Startup_INT.pcc` state-map export and a patched galaxy package; integrating those findings with all applicable mounted DLC maps remains separate work. Script/class/sequence inspection remain available.
- Complete conversation execution semantics. Default/custom interrupt windows, skip behavior, late/early acceptance and selected outcome require runtime observation. A visibility track alone is not a legal-input-window guarantee.
- LE2 launch/replay support in the existing LE1 automation. Use the protected LE2 session workflow and ordinary progression. Do not invoke native completion functions to claim actual VR input success.

The JSONL scan covers one supplied package directory and its existing language/file filters; it is not a mounted-game catalog resolver. Keep separate, hashed outputs for base and DLC packages until mount precedence is implemented. Never commit raw scan, decompiler or sequence output.
