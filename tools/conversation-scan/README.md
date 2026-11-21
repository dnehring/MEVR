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

## Hands census (all three games)

Which scripted scenes have Shepard's hands doing something the player could do with their own (research/74). The scanner
writes the facts, `tools/hands-census.py` classifies:

```powershell
conversation-scan <Game>\ME3\BioGame scratch\hands-census\le3.jsonl --hands-census --game LE3 [--only <file text>] [--threads 4]
python tools/hands-census.py summarize scratch\hands-census\le1.jsonl scratch\hands-census\le2.jsonl scratch\hands-census\le3.jsonl --out scratch\hands-census
python tools/hands-census.py show scratch\hands-census\scenes.jsonl WardsFlux
python tools/hands-census.py names scratch\hands-census\scenes.jsonl drink
python -I tests/HandsCensusTests.py
```

`<BioGame>` is a game's `BioGame` folder: its `CookedPCConsole` and every `DLC\*\CookedPCConsole` are read (or pass one
`CookedPCConsole`). Packages skipped: other languages' `_LOC_`, `_SND`, `_ART`, `_LIGHT`, `_T`, `Textures*`. It reads at below
normal priority with 4 threads (a game may be running; it only opens packages). The whole of ME1 takes about 20 s, ME2 30 s,
ME3 50 s; the JSONL is 70, 100 and 200 MB: keep it in `scratch\hands-census\` (gitignored).

| Record | What it holds |
|---|---|
| `scene` | One per `SeqAct_Interp` (level matinee or conversation line, `inConversation`) and per `InterpData` none plays (`orphanData`): `path`, `sequence`, `data`, `length`, the designer `comment` (level matinees only: a line's comment is its text and is never read); each group's `class`, `name`, `name:m_nmSFXFindActor` and `bound` (what the `SeqAct_Interp` gives the group's variable link: `var` class, find-by-tag tags, `objClass`/`obj`/`tag`/`meshes` of an object, `named` targets of a `SeqVar_Named`, `outer` for a `SeqVar_External`: what the enclosing sequence or its `SequenceReference` is given for that label, three sequences out); each track's `class`, `actor` (ME1's `sActorTag`, the sequels' `m_nmFindActor`), `anims` (`InterpTrackAnimControl` keys: `t`, `end` from the AnimSet's sequence length when the set is in the package, `anim`, `rate`, `loop`), `gestures` (`BioEvtSysTrackGesture` keys: `t`, the gesture, transition and pose sets and anims), `props` (every property of a track named for a prop, an attachment or a weapon: `BioEvtSysTrackProp`'s `m_aPropKeys` with `nmProp`, `pPropMesh`, `nmAction`, `bEquip`), `keyTimes`, move spans, director `cuts`, fades; `upstream` (the nearest events before it, with a use's originator and a conversation node's ID), `downstream` (what its outputs do, three actions deep), `siblings` (the attach, prop and weapon actions in its sequence, with what each variable binds). |
| `use` | A use event (`SeqEvent_Used`, `BioSeqEvt_OnPlayerActivate`, `BioSeqEvt_ArtPlaceableUsed` ...): its originator's class, name, tag and meshes, and the matinees within 12 actions of it. |
| `conversation` | A `BioConversation`'s lines by node: each entry's `nExportID` and speaker tag (`Owner` for -1), the replies' IDs (Shepard's lines). No text. |
| `classes` | Last line: the packages read, failures, seconds, the folders, and every sequence/interp class's export count (how each game names its prop and attach actions). |

`hands-census.py summarize` keeps one copy of a package's scene (a DLC's over the base game's, the later DLC folder over an
earlier one: an approximation of the mount order), and writes `scenes.jsonl`/`scenes.csv` (every scene with Shepard: how he is
bound, his animation windows, props, what else the scene drives, kind, confidence, the names that drove it, `pauseAt`),
`uses.jsonl` and `summary.json`; the rule is in its docstring and research/74.

## LE2 commands

Pass the installed LE2 `CookedPCConsole` directory as `<cooked>`. Choose a new private output for every command; pass `--lex-dir` if the build's recorded path is unavailable.

```text
conversation-scan <cooked> <new-mechanic.jsonl> --game LE2 --only BioD_OmgGrA_112Hangar_LOC_INT
conversation-scan <cooked> <new-track.txt> --game LE2 --class-dump BioEvtSysTrackInterrupt --only BioD_OmgGrA_112Hangar_LOC_INT
conversation-scan <cooked> <new-sequence.txt> --game LE2 --sequence BioD_OmgGrA_112Hangar_LOC_INT:Node_Data_Sequence
conversation-scan <cooked> <new-reload.txt> --game LE2 --sequence BioD_ProCer_100RezRoom:LS10_Reload_Heatsink
conversation-scan <cooked> <new-functions.txt> --game LE2 --script grep:Interrupt --only SFXGame
conversation-scan <cooked> <new-events.json> --game LE2 --state-events 4551 --only Startup_INT
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

## LE2 state-event inspection

`--state-events` requires an explicit nonempty `--only` package-name substring for LE2. It enumerates every `BioStateEventMap` export in matching **top-level** `.pcc` files in the supplied directory, using LegendaryExplorerCore's existing binary converter. It does not apply the LE1 filename heuristic, assume an export index, recursively search DLC, or choose mounted-file precedence. Resolve effective packages before invoking it; [Zanethu source findings](../../research/le2-galaxy-fixture-source.md) explain why this matters.

Unlike LE1's unchanged text output, LE2 emits a JSON report containing `requestedIds`, inspected `packages`, matching `events`, `missingIds`, `duplicateIds`, `errors` and `scopedInspectionComplete`. Each event records the actual package path/hash, map path/export index and concrete element types with their complete serialized fields, including nested element data. It does not reduce an element to a guessed single boolean operation. Duplicate event IDs retain all matches for caller review; no override is selected. `scope.mountedCompleteness` is always false.

Missing IDs, no matching packages, package-game mismatches, parse failures or conversion errors produce a report with `scopedInspectionComplete: false` and exit `1`; invalid IDs or missing `--only` return `2`. Valid IDs are comma-separated nonnegative integers. Exit `0` means all requested IDs were read successfully **within the stated package scope**, not that every mounted dependency or resulting gameplay effect is known. Direct serialized element inspection cannot establish transitive script/journal effects, ordinary progression or runtime acceptance.

The installed base English `Startup_INT.pcc` was checked through this mode. It identifies event 4551 in `PlotManagerAuto.StateTransitionMap` with exactly two boolean elements: 4419 and 5679 become true, both with parameter substitution disabled. No direct 5678 mutation appears in that event. The test also requests an absent ID to verify that a partial match fails, and compares the unchanged LE1 text mode to the previous scanner on existing LE1 events. Synthetic invalid-package and empty-scope tests ensure errors cannot be mistaken for successful lookup. No plot recipe is created by these checks.

## Verified scope and remaining gaps

The installed English mechanic package produced 90 records, including one conversation, one interrupt, 39 cinematic events and 49 matinees. Every entry retained a nonzero text reference; a Renegade interrupt reply edge was present. The visibility track linked through its interpolation data and cinematic event to an authored node. Its two stored times were approximately 5.429 and 7.4299107 seconds. The ProCer reload sequence exposed both a tutorial-reload event and a save action. These are authored-data observations, not native timing, reachability, save validity or controller acceptance.

LE1 default/explicit scans of the installed captain scene produced the same 10 records and were byte-identical to the previous scanner output. The existing synthetic conversation-sweep suite also passed. This is targeted regression evidence, not exhaustive LE1 catalog equivalence.

Still outside this bounded change:

- External LE2 TLK resolution, localization/DLC mount ordering, duplicate-package precedence and Wwise audio extraction. `--wheels` and `--vo-extract` reject LE2 rather than applying LE1 assumptions.
- Automatic mounted LE2 state-map resolution, transitive dependency analysis and coherent plot recipe reconstruction. The new state-event mode requires a supplied directory and package selector; integrating all applicable language/DLC maps remains separate work. The old LE1-oriented `--references` search still rejects LE2.
- Complete conversation execution semantics. Default/custom interrupt windows, skip behavior, late/early acceptance and selected outcome require runtime observation. A visibility track alone is not a legal-input-window guarantee.
- LE2 launch/replay support in the existing LE1 automation. Use the protected LE2 session workflow and ordinary progression. Do not invoke native completion functions to claim actual VR input success.

The JSONL scan covers one supplied package directory and its existing language/file filters; it is not a mounted-game catalog resolver. Keep separate, hashed outputs for base and DLC packages until mount precedence is implemented. Never commit raw scan, decompiler or sequence output.
