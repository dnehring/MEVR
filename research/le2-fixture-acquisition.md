<!-- SPDX-License-Identifier: MIT -->
# LE2 native fixture acquisition findings

Date: 2026-11-13. Read-only inspection; no game launch, save edit or fixture promotion. This narrows the acquisition tasks in the [coverage catalog](../docs/le2-validation-coverage.md). Package declarations and authored sequences identify useful experiments; they do not establish a working runtime hook or a legally reachable scene.

Follow-up: the [scanner now supports explicit LE2 inspection](../tools/conversation-scan/README.md), including string references, reply categories and interrupt-to-node linkage. The scanner limitations below describe the initial investigation; its current README identifies which remain unresolved. Use `--game LE2` for all new sequel inspections.

The [2026-11-14 timed-interrupt follow-up](le2-interrupt-acquisition.md) checks the existing late-game story state and Liara/Citadel alternatives. Clear Liara interrupt outcomes do not establish replayability: met-Liara, DLC completion and date states are already set. No current-save timed fixture is promoted. Freedom's Progress provides a concrete dual-polarity earlier-progression candidate that can share the normal ProCer reload acquisition playthrough.

## Current reload acquisition decision, 2026-11-14

The first native baseline needs a **quiet place to fire and reload an ordinary
multi-round firearm**, not an endgame encounter or a purpose-built range. The
inspected reload entry requires enabled combat input and legal weapon/ammo state;
it does not require an enemy target. Establish normal drawing, firing, partial
reload and saving at the chosen location before preparing variants. A map name,
populated inventory or successful load alone does not establish those actions.

The owner confirmed on 2026-11-14 that no earlier save is known. Plan the
separate protected new-game route below; do not leave acquisition waiting for
an unidentified Lazarus/Freedom's Progress save.

The first protected New Game attempt reached the Normandy evacuation corridor.
There the current pawn was `SFXGame.BioPawn`, outside the existing scripted
input guard's `SFXPawn_Player` requirement. The archive contained no new career
save. Before repeating the opening, obtain a verified early Legendary Edition
save or arrange normal controls that can hold movement through the evacuation.
A candidate from another career needs the same immutable archive, format check
and normal-load scene verification as one acquired locally. See the
[native acquisition record](le2-validation-evidence.md#normal-new-game-reached-normandy-evacuation).

The search did not verify a public PC LE2 checkpoint before the reload or
Veetor interrupt. Trilogy Save Editor's bundled `ME2LeSave.pcsav` decodes as
`Biop_Nor` at about 36 hours. The
[RAGING save set](https://playersquared.com/threads/mass-effect-2-legendary-edition-%E2%80%93-complete-save-set-cusa19500.6302/)
lists a Lazarus Research Station save for PS4 CUSA19500. Its exact tutorial
position and PC compatibility remain unverified. The author of
[Skip Mass Effect 2 Prologue](https://www.nexusmods.com/masseffectlegendaryedition/mods/1004?tab=description)
describes a route to character creation, bypassing the Normandy evacuation
that blocked scripted movement. Consider the mod for acquisition only if the
checkpoint can subsequently be loaded and verified on the stock test install.
No external save or mod was installed during the search.

1. **Preferred reusable route: normal Lazarus/ProCer progression.** Installed
   base and Patch01 content includes `BioP_ProCer` and
   `BioD_ProCer_100RezRoom`. Existing sequence inspection identifies pistol
   acquisition, `LS10_Reload_Heatsink`, the tutorial reload event and a save
   action. Use a separate protected normal new-game profile; preserve the first
   legally available checkpoint around that tutorial and a quiet post-tutorial
   save. Verify which checkpoint actually precedes reload: the authored save
   action is not that proof. Continue the same natural progression to the
   [Veetor interrupt candidate](le2-interrupt-acquisition.md), supplying two
   acquisition lanes without resetting story flags or teleporting saves.
2. **Bounded opportunity: the existing chapter.** Its normal load and squad-menu
   progression are recorded in the [evidence log](le2-validation-evidence.md).
   If the ordinary menu exit yields a safe controllable location, acquire a
   normal save there with an existing multi-round weapon. Do not require
   completing the endgame. If more scripted progression blocks that small
   objective, use ProCer rather than extending the menu route indefinitely.
   The chapter is not yet an accepted quiet reload fixture.
3. **Optional dedicated range: The Normandy Project LE2.** The mod's
   [official changelog](https://www.nexusmods.com/masseffectlegendaryedition/mods/1779?tab=logs)
   describes a Cargo Bay range requiring Cargo Master crew, a 15,000-credit
   purchase and construction, then the armor-change/equipment-station route.
   It supplies a weapon locker, ordinary ammo refill (not heavy ammo) and target
   dummy. Treat this as a separately installed and hash-bound modded fixture,
   not an already available stock room. The inspected installed DLC directory
   has no identifiable TNP folder; this is not an exhaustive installation audit.
   No mod was downloaded or installed for this investigation.

The installed Kasumi DLC also contains `BioA_PtyMtL_360ShootingRange.pcc`.
That filename is only a content lead: player usability, normal access from the
current story state and repeatable saving have not been established.

### Existing weapons and ammo preparation

Fresh strict-reader checks of the immutable original Normandy, resource-reload
Normandy and chapter saves found existing Collector assault rifle, Geth shotgun,
MassCannon and LaserPistol records, all with saved used-ammo count zero. Their
reserves are respectively 280/20/12/24 on Normandy and 280/7/10/24 in the chapter.
Blackstorm heavy-weapon records also exist, with zero reserve. These are saved
values, not fresh live capacity, selection or reloadability observations.

Reuse `tools/le2-save.py prepare`: its allowlist already supports existing
`player.weapons.N.ammo_used_count` and `ammo_total` scalars with pinned input and
expected-value checks. The fields mean used magazine ammunition and spare
reserve; see [native ammo mapping](le2-ammo-fixture-source.md). No new weapon
editor, record insertion or selected-weapon mutation is needed. First observe
the actual selected class and capacity, then bind partial/empty and reserve
variants to that existing record and verify post-load values. Ammo edits do not
enable combat input on Normandy. Prefer a multi-round weapon for the first
manual reload: the [observed one-round MassCannon](le2-weapon-observation.md)
can enter automatic reload after a shot, confounding sparse before/after checks.

## Current private saves

The three existing saves were freshly parsed with `tools/le2-save.py info --json`. The manual save and autosave report `Biop_Nor`; **the chapter save reports `BioP_EndGm2`**, so it must not be treated as another Normandy fixture. All three have the same 80 recorded planet entries, all marked visited, with 599 probe-impact coordinates in total, 30 probes and 1500 fuel. They are a narrow late-game corpus, not three independent scenario states. Raw inspection and hashes stay private.

The Normandy saves are a practical starting point for a protected normal-load/galaxy-menu baseline and controlled probe/fuel variants. They are not proven untouched-planet or pre-interrupt fixtures. Twenty-three recorded planet entries have no probe coordinates, but this does not prove they are scannable, contain an unclaimed anomaly or are available to land on. A planet absent from the saved list also needs its native definition and conditions checked; absence alone is insufficient.

Current saves already have the upstream-labeled Garrus recruitment mechanic, green-recruit and dying-merc interrupt flags set. Resetting those booleans alone would not recreate their conversations. Liara intro-related booleans are clear, but that does not prove the intro remains reachable; DLC replacement and other conditions must be resolved first.

## Concrete acquisition routes

For an operator playing the opening, the precise stopping cue is the first
reload prompt after picking up the pistol, armor and first thermal clip at
Lazarus Research Station. Stop before pressing Reload. Use a fresh manual save
only if the normal Save menu is enabled. Base and Patch01 both grant
`SFXGameContent_Inventory.SFXWeapon_HeavyPistol`. Stock configuration lists
12 magazine rounds and 60 maximum spare ammo, which the fixture must confirm
against its live weapon. The earlier LS5 save occurs before pistol pickup.
The LS10 tutorial reload event advances the sequence to state 15 and triggers
another save, so keep any earlier checkpoint separately. If the game only
allows a save after the tutorial, use it for ordinary ammo transfer and retain
the tutorial progression case separately.

| Scenario | Locally verified candidate | Next acquisition action |
|---|---|---|
| Reload/tutorial | Installed `BioD_ProCer_100RezRoom.pcc` contains `LS10_Reload_Heatsink`, `SFXSeqEvt_TutorialReload_0`, a comparison following that event, and `SFXSeqAct_SaveGame_1`. The same package contains the pistol/armor pickup and cover tutorial sequences. `BioP_ProCer.pcc` is installed. | Start a separate protected normal new-game profile and progress naturally through the opening. Capture the first legally available save before the thermal-clip/reload sequence and another before completion. Determine exactly when the authored save action runs; its presence does not guarantee a pre-reload save. Inspect tutorial event routing, counter and downstream sequence before accepting the physical reload. Do not teleport a late-game Normandy save into this map. |
| Timed interrupt | English `BioD_OmgGrA_112Hangar_LOC_INT.pcc` contains the mechanic conversation package `omggra_bluesuns_mechanic_d_d` and `BioEvtSysTrackInterrupt_5`. Its paired visibility keys are approximately 5.429 s on / 7.430 s off in the associated interpolation data. | Acquire a naturally reached save immediately before the Garrus recruitment mechanic encounter using an earlier/new progression profile. Inspect the conversation's reply category, condition, transition and node-to-interpolation links; then record native taken/missed/early input. Current saves have TSE's mechanic-interrupted boolean 2660 set, so they are unsuitable for replay without a larger, independently validated state reconstruction. This candidate establishes one authored interrupt, not both Paragon and Renegade coverage. |
| Planet scanning | Normandy galaxy packages are installed. Decompiled `SFXGameModeOrbital.LaunchProbe` checks scanning and in-flight/impact/rescale/exit state, requires a positive probe resource, deducts one on legal launch and has a separate no-probe path. TSE separately serializes each planet's ID, visited flag and impact coordinates. | First use a protected Normandy copy for normal galaxy entry and resource/refusal behavior. Select an actually scannable planet through the UI, capture a legal pre-launch save, and keep probe-history state intact. For discovery, resolve an unclaimed native feature and its conditions before creating a separate pre-anomaly fixture. Compare naturally produced before/after saves, including planet records, journal and plot changes; resource-only edits cannot reset discovery. |

The interrupt times above come from the existing dumper's three-decimal rendering of authored track values. They are not measured runtime window bounds. Default/custom window logic, line playback, skips and localization still require native observation. The script inspection exposes native declarations for `BioConversation.SelectInterruption`, `TickInterruptionData`, `NeedToDisplayInterrupt`, `EnableCustomInterruptWindow` and `SetCustomInterruptWindow`; it does not reveal their native implementations or prove safe call order. Acceptance must use player controls, not call these functions to manufacture an outcome.

## Historical scanner audit: initial missing LE2 support

This section records the 2026-11-13 starting state, not the current tool contract. Explicit LE2 mode, node/interrupt linkage and scoped state-event inspection are now implemented; see the [current scanner README](../tools/conversation-scan/README.md). The effective transition-map and galaxy investigation also progressed in [galaxy fixture source findings](le2-galaxy-fixture-source.md). Retain the list below as historical acquisition context, not a request to reimplement those changes.

The current [scanner](../tools/conversation-scan/Program.cs) has no game selection. `Scan`, `ClassDump`, `ScriptDump`, `SequenceDump`, `StateEvents` and `Wheels` initialize LegendaryExplorerCore for LE1. Nevertheless, actual read-only execution of `--class-dump`, `--script` and `--sequence` successfully opened installed LE2 packages in this inspection. They are useful targeted research tools today; that is not validation of the complete LE1 conversation workflow on LE2.

Required bounded changes before trusting a full LE2 catalog:

1. Add explicit `--game LE1|LE2` initialization and reject package/game mismatches. Enumerate mounted base and DLC package roots, language and override priority. Preserve package path/hash and export identity instead of deduplicating solely by conversation name or formatted property body. The current broad class dump can report a German or Polish copy as the first match; use an exact `_LOC_INT` filter for English evidence.
2. Add `StringRefProperty` handling. Installed LE2 conversation `srText`/`srParaphrase` properties are string references; `Scan.cs` currently reads `srText` as `IntProperty`, and `ClassDump.Text` prints only `StringRefProperty`. A successful process exit therefore does not establish correct text extraction. Resolve the installed external `BIOGame_INT.tlk`, update TLK and DLC TLKs with the proper LE2 lookup/override rules. `Wheels.cs` instead uses the ME1 TLK namespace and embedded `BioTlkFile` plus ISACT/`SoundNodeWave` assumptions; it is not a drop-in LE2 text/voice solution. LE2 inventory contains external TLKs and `.afc` audio.
3. Export each `ReplyListNew` edge's `Category`, `srParaphrase` and index, plus node `nExportID`, `nStateTransitionParam`, existing conditions and reply type. `ReplyType` already exists in the scanner, but it is insufficient to identify timed interrupt edges. Follow `MatineeSequence`/node links into `BioEvtSysTrackInterrupt`, including paired `m_aInterruptData` and `m_aTrackKeys`, and preserve full float precision. Record default/custom window sources separately. Do not infer the runtime legal window from a visible prompt track alone.
4. Generalize sequence inspection beyond paths containing `Main_Sequence`. Conversation interpolation data lives under its `Node_Data_Sequence`; the current `--sequence` filter omits it. Link input/window state and chosen branch to the same conversation/node during native capture.
5. Replace the LE1 state-map filename filter with export discovery over the effective LE2 package set. The inspected `PlotManager.pcc` contains `BioAutoConditionals` functions; inspected `PlotManagerMap*.pcc` and `BioP_Global.pcc` did not expose a `BioStateEventMap`. This investigation has **not** located LE2's effective transition map. Find it explicitly rather than interpreting an empty `--state-events` result as no side effects. Retain missing/failed package counts and fail requested lookups that produce no target.
6. Keep launch/replay separate: [conversation-from-save.ps1](../tools/conversation-from-save.ps1) explicitly targets the ME1 profile, `MassEffect1` process and LE1 log/control path. Use the protected LE2 session workflow and ordinary Load Game until a verified LE2 runtime adapter exists.

## Discovery and story dependency investigation

Pin TSE at `6451f23d1fba62358328dce4e87733970f36ebf1`. Its `databases/me2_raw_plot_db.ron` provides concrete **search anchors**, not complete recipes:

| TSE boolean | Source label meaning | Use / limitation |
|---|---|---|
| 2660 | Garrus acquisition mechanic interrupted | Correlate the mechanic candidate with natural before/after saves and its native transition; currently set in all three saves. |
| 1550 / 1551 | Liara intro Renegade / Paragon | Alternate candidate search only. Both clear in current saves; base Liara packages include placeholders and the relevant conversation was not located in the inspected base hub package. Resolve DLC/mount replacements before assuming availability. |
| 5678 / 5679 | `N7_Crsh` planet scanned / corresponding journal state | Search anchors for an anomaly-to-journal dependency. Both are clear in the inspected manual save; `BioP_N7Crsh`, `BioA_N7Crsh` and `BioS_N7Crsh` are installed. The native planet/table mapping and legal entry remain unverified. |
| 2738 | `N7_Crsh` mission complete | Also clear in the manual save; inspect together with scan/journal/streaming state rather than changing one flag. |

Do not equate an editor-friendly mission label with completion. For example, `me2_plot_db.ron` places 5838 under **rewards**, and the raw database labels it `XP_n7mmnt4`. That is not an established mission-entry/completion flag, nor evidence that its marketing title maps to `N7_Crsh`.

To resolve discovery, inspect the native galaxy data loader and effective table entries that populate `SFXGalaxyMapObject.TableID`, visible/usable conditionals, `BioPlanet.MapName`/`PlanetEvent`, and `SFXPlanetFeature.FeatureTransition`/parameter. These members are visible in the locally available LE2 SDK declarations; offsets and runtime semantics are unverified. Trace the actual planet ID to its feature, inspect the referenced `BioAutoConditionals.F<id>` in the effective plot package and resolve the transition's effects. Then compare normal scan/landing saves. No planet-ID-to-name mapping is asserted here.

Targeted `BioPlanet`/`SFXPlanetFeature` class dumps of the six `GalaxyMapObjs` packages found no instances. Those scene packages are not a verified source for the galaxy table. The next offline task is an export/binary-data inventory of the effective base+DLC package set plus loader inspection, using LegendaryExplorer's appropriate table/binary converter once the format is identified. Existing `StateEvents` demonstrates use of a binary converter, but its LE1 filter cannot substitute for that discovery.

## Reproducing the read-only inspection

Run the rebuilt scanner executable with the installed LE2 `CookedPCConsole` as the first argument, a **new private output file outside that directory** second, `--game LE2`, and the local LegendaryExplorer directory via `--lex-dir` when needed. Verified selectors in this investigation:

```text
--sequence BioD_ProCer_100RezRoom
--class-dump BioEvtSysTrackInterrupt --only BioD_OmgGrA_112Hangar_LOC_INT
--class-dump BioConversation --only BioD_TwrHub_LOC_INT
--script grep:Interrupt --only SFXGame
--script grep:Probe --only SFXGame
--script exports --only PlotManager
```

Generated game-derived dumps stay in ignored `local-tools/le2-validation-evidence/fixture-acquisition/`; do not commit their text. The inspected scanner executable SHA-256 was `a02180d2611b30ad1793a5a50cbb9f112c8d17b937f0749a1b2e2aa19736a48c`; LegendaryExplorerCore DLL was `d980a622e225cbc0dd1d1c5719a74ea1cb59284abe0e7855f354afbc6baf7d74`. Recheck tool/package hashes before relying on later runs. Only this original research summary is committed.

The initial November 13 acquisition order started with protected Normandy load/resource proof, then ProCer, Garrus and separate opposite-polarity/discovery fixtures. Subsequent native resource proof and interrupt research supersede that ordering: use the current reload decision and Veetor follow-up above. Exclusive native-session ownership is still required before play; the gameplay scenario definitions remain draft.
