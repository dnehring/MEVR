<!-- SPDX-License-Identifier: MIT -->
# LE2 Zanethu discovery fixture: native source findings

Date: 2026-11-13. Read-only offline inspection, following [fixture acquisition](le2-fixture-acquisition.md). No game launch, save mutation, plot recipe or fixture promotion. The mission-to-planet mapping and immediate transition effects are now identified; ordinary loading, reachability, scanning and persistence remain untested.

## Effective galaxy source and loading link

The full galaxy is serialized as an object hierarchy in **`BioD_Nor_103aGalaxyMap.pcc`**, not the previously inspected `103bGalaxyMapObjs` scene. LegendaryExplorerCore's `MELoadedFiles.GetFilesLoadedInGame(MEGame.LE2, gameRootOverride: ...)` selected the installed **`DLC_METR_Patch01/CookedPCConsole`** copy. Its mount priority is 2000; other installed DLC copies exist, so selecting the shared DLC copy or the first matching filename is insufficient. This is the resolver's offline mounted-file result, not an observation of packages loaded by a running game.

In that effective package:

- `TheWorld.PersistentLevel.Main_Sequence.BioSeqAct_ActivateGalaxyMap_0` connects its `m_pGalaxyMap` variable link to `SeqVar_Object_8`.
- That variable's `ObjValue` is `BIOG_GalaxyMap_DLC00.GalaxyMap`.
- The galaxy hierarchy contains 29 cluster, 72 system, 312 planet and 4074 feature exports. Those are authored objects, not a count of currently reachable locations. The galaxy's cluster array also contains empty slots; export counts cannot substitute for array indices.

The base `SFXGame.pcc` has actual `BIOG_2DA_GalaxyMap_X.GalaxyMap_Map` and `GalaxyMap_PlotPlanet` tables, at exports 32697 and 32698, decoded with LegendaryExplorerCore's `Bio2DA` reader. They contain only 15 and 13 rows respectively. They do not enumerate the full scannable galaxy and are not the source of the Zanethu planet record.

## Mission, planet and feature mapping

The effective package's **export 143**, `BIOG_GalaxyMap_DLC00.GalaxyMap.SFXCluster_19.SFXSystem_3.BioPlanet_5`, explicitly sets `MapName` to `BioP_N7Crsh`. Its display string reference is 323963. Base English `BIOGame_INT.tlk` resolves this to **Zanethu**; the parent system reference 322588 resolves to **Ploitari**, and cluster reference 191610 to **Hourglass Nebula**. Independently, the installed `Coalesced_INT.bin`'s `BIOGame.ini` level entry associates `biop_n7crsh` with Zanethu and the MSV Estevanico crash site. Effective in-game localization remains a native presentation check.

The planet's first referenced feature is **export 2709**, ending in `BioPlanet_5.SFXPlanetFeature_4298`. It is a `FEATURE_LANDINGSITE`, with:

| Native field | Value / meaning established by source |
|---|---|
| `FeatureTransition` | 4551 |
| `VisibleConditional` | 1418 |
| `UsableConditional` | 1237 |
| Planet `PlanetLandCondition` | 1237 |
| Planet `PlanetPlotLabelCondition` | 1237 |

The declared feature coordinates and distress-signal resources are retained in the private dump; they are unnecessary for an agent to navigate to the scene normally.

**Do not derive a planet ID from object-name suffixes.** `SFXGalaxyMapObject.EnumerateChildren` assigns `TableID` from the child-array index. The inspected `GetChildren` implementations return `Clusters`, `Systems` and `Planets` in their authored array order. The target occupies cluster index 20, system index 1 and planet index 0. `BioPlanet.EnumerateChildren` therefore computes **`ActiveWorld = 200100`** for this hierarchy. `BioSeqAct_OrbitalGame.Activated` indexes the same hierarchy using the camera's active-world value. This is a static derivation of the orbital-selection identifier, not proof that the save file's planet `id` field uses exactly that identifier. Confirm that connection with naturally produced before/after saves; do not insert a saved planet record from this derivation.

## Conditions and transition effects

The effective base `PlotManager.pcc` exposes these `BioAutoConditionals` functions:

| Conditional | Decompiled condition |
|---|---|
| `F1418` | Boolean 4419 is false: landing site remains undiscovered |
| `F1237` | Boolean 4419 is true and boolean 2738 is false: landing open and mission incomplete |
| `F1512` | Boolean 5678 is true; a separate scan-related condition, not the feature's visible or landing condition |

Pinned Trilogy Save Editor's raw database labels 4419 as the `N7_Crsh` landing-open state, 2738 as mission complete, and 5678/5679 as scan/journal states. Native fields above establish that **5678/5679/2738 alone do not describe the discovery gate**.

The missing base transition map is in **`Startup_INT.pcc`, export 1742, `PlotManagerAuto.StateTransitionMap`**, class `BioStateEventMap`. Reading its binary through LegendaryExplorerCore's `ObjectBinary.From<BioStateEventMap>` identifies **event 4551 with exactly two boolean elements**:

- Set boolean **4419** to true, without using the transition parameter.
- Set boolean **5679** to true, without using the transition parameter.

It does **not directly set 5678**. No broader absence of side effects is claimed: journal/notification systems may respond to changed state, and mission entry has its own progression.

`SFXGameModeOrbital.EndImpact` checks feature proximity and visibility before executing the feature transition; a landing-site hit also drives the reveal flow and refreshes planet usability. Therefore resource grants or manually setting either boolean do not prove scan/probe input or the discovery interaction. Preserve the normal game-owned path.

## Private-save finding and next native experiment

A fresh read of the existing manual Normandy save finds **4419, 5678, 5679 and 2738 all false**. It has no saved planet record with `id = 200100`. Together with the authored mapping, this makes Zanethu a concrete candidate for a protected native acquisition run; it does not prove travel access, an untouched resource state or the save-ID mapping. The 80 recorded visited planets do not imply that all 312 authored planets were visited.

Next, under exclusive native-session ownership, normally load a protected copy, enter the galaxy map and navigate to Hourglass Nebula → Ploitari → Zanethu. Verify the rendered destination, anomaly signal, landing availability and resource state. Capture a natural pre-probe save if the game allows it. Launch a legal probe at the anomaly through the actual controls, observe discovery and journal/landing changes, and save/reload. Compare all plot, journal and planet-record changes; specifically check 4419/5679, investigate any 5678 change, and establish which saved planet ID is created. Only then bind the discovery fixture. Zero/insufficient/exact/surplus resource variants may use the existing resource-only adapter after this baseline is proven; no story reset is authorized by this finding.

## Reproduction and source identity

Use the scanner's explicit `--game LE2` path for class dumps of `BioPlanet`, `SFXPlanetFeature`, `SFXGalaxy`, `SFXCluster`, `SFXSystem`, `BioSeqAct_ActivateGalaxyMap` and `SeqVar_Object`, selecting the exact effective `BioD_Nor_103aGalaxyMap.pcc`. Inspect `BioAutoConditionals.F1237`, `.F1418` and `.F1512` in the effective plot package, plus `EnumerateChildren`, `GetChildren`, `BioSeqAct_OrbitalGame.Activated` and `SFXGameModeOrbital.EndImpact` in `SFXGame`. Resolve mounted files before each investigation. Startup/language and DLC state-map enumeration still need generalization in the reusable scanner; this finding identifies the base English map on this installation.

Private helper/dumps remain in ignored `local-tools/galaxy-probe/` and `local-tools/le2-validation-evidence/galaxy-source/`. The helper calls existing LegendaryExplorerCore readers for coalesced config, mounted-file resolution, 2DA, TLK and state-event binaries; it does not implement another binary parser. No extracted scripts, tables, recordings or save bytes are committed.

The reusable scanner now supports the scoped base-map lookup directly:
`conversation-scan <LE2 cooked directory> <new private report.json> --game LE2 --state-events 4551 --only Startup_INT`.
It enumerates map exports in the selected packages, preserves concrete element
data and source hashes, and fails missing requested IDs or parse errors. This
does not resolve mounted precedence or every applicable DLC/language map.

SHA256 anchors for this inspection:

| Source | SHA256 |
|---|---|
| Effective `BioD_Nor_103aGalaxyMap.pcc` | `43cd886c54dc427717255fc2e58ec9fd63363459d694ad6aed8c1803dff84c65` |
| Base `Startup_INT.pcc` | `047bba83d5595c4b83a8a5280d64dc1a187c74bc444fb6e67f200c1191f333f8` |
| Base `PlotManager.pcc` | `0511a3b7a6c7118d4bab34eb870b1c53f683c26d48fcfb42290b11d1f6528281` |
| Base `SFXGame.pcc` | `681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77` |
| Base `Coalesced_INT.bin` | `a9adef3e0f18201dd8f841a6edecdf6ea2392c88dabed372284dc26220383869` |
| LegendaryExplorerCore DLL | `d980a622e225cbc0dd1d1c5719a74ea1cb59284abe0e7855f354afbc6baf7d74` |
