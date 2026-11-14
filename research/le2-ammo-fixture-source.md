<!-- SPDX-License-Identifier: MIT -->
# LE2 reload fixture and observation requirements

Read-only inspection on 2026-11-13 of installed LE2 `SFXGame.pcc`, SHA256
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`,
using LegendaryExplorerCore through the [conversation scanner](../tools/conversation-scan/README.md).
These are original summaries of named script functions, not runtime results.
Private inspection outputs are under `local-tools/le2-validation-evidence/ammo-source/`;
do not commit extracted code. No game was launched or modified for this inspection.

## Save fields and runtime meaning

| Save adapter field | Inspected native meaning | Validation consequence |
|---|---|---|
| Existing weapon `ammo_used_count` | `SFXSaveGame.SaveWeapons` converts runtime `AmmoUsedCount` to an integer. `LoadWeapons` restores it, clamping values above the current magazine size. | This is ammunition used from the magazine, not ammunition remaining. Observe actual post-load capacity and count. Do not invent a capacity from the save alone. |
| Existing weapon `ammo_total` | `SaveWeapons` stores `GetCurrentSpareAmmo()`; `LoadWeapons` assigns it to `CurrentSpareAmmo`. | This is reserve ammunition, despite the field name. Do not subtract loaded ammunition from it. |
| Current/last weapon and ammo-power fields | `LoadWeapons` matches records by weapon class, restores selection and invokes ammo-power restoration. | Bind the tested class and selected weapon; changing a different record is not a valid encounter setup. |

`SFXWeapon.GetMagazineSize` reads the runtime magazine attribute. Its
`GetAmmoCountInMagazine` returns the integer conversion of magazine size minus
used count. Runtime used count and magazine size are floats; saved used count is
an integer. `GetCurrentSpareAmmo` returns the integer reserve as a float.
`GetMaxSpareAmmo` can include used count unless asked to ignore the current
magazine, so it must not be mislabeled as fixed reserve capacity.

These findings support existing scalar editing; they do not validate insertion
of weapons, arbitrary ammo values, every subclass, upgrade or heavy weapon.
Use naturally acquired weapons and establish capacity/configuration in game.

## Lazarus tutorial save-state anchors

Read-only reinspection on 2026-11-14 used the existing LE2 sequence scanner on
both installed `BioD_ProCer_100RezRoom.pcc` copies:

| Package scope | SHA256 |
| --- | --- |
| Base CookedPCConsole | `336d96ee6b40c1ab4a70306ddec689460b051da3fa4311d7c8fa3ddb1ca81f52` |
| DLC_METR_Patch01 CookedPCConsole | `505c63168579f9a8b80e6999be79f99d33a59d6dc256cfb5ef8a3ec6adc7cd78` |

Both copies explicitly name `ProCer_LevelState` on
`LS10_Reload_Heatsink.BioSeqVar_StoryManagerInt_1` with `m_nIndex=477`.
`SFXSeqEvt_TutorialReload_0` feeds `SeqCond_CompareInt_1`, whose other operand is
`SeqVar_Int_3` (10). Only its equality output advances this route: it disables
tutorial events and starts `SeqAct_Delay_3`, then `SequenceReference_1` receives
`NewState` from `SeqVar_Int_1` (15). Inside the referenced sequence,
`SeqAct_SetInt_0` targets another explicit index-477 story variable. Its finished
output leads to `SFXSeqAct_SaveGame_1`. The internal sequence is `Sequence_4063`
in base and `Sequence_1081` in Patch01; the relevant field/edge semantics agree.
This is direct authored wiring, not a checkpoint ID inferred from an editor label.

| Existing save-adapter field | Source-backed interpretation and limits |
| --- | --- |
| `plot.integers.477` = 10 | The authored phase in which this reload event is accepted. It is a useful pre-reload candidate discriminator, but does not prove the event has not already fired during the downstream delay, that the weapon is ready, or that a manual save is allowed. |
| `plot.integers.477` = 15 | The state assigned by this tutorial-event route before its save request. Compare a naturally obtained before/after pair; this value alone does not prove an ammo transfer, input count or successful save. Later progression may change it again. |
| `plot.booleans.4049` | Both copies explicitly identify `LS10_Reload_Heatsink.BioSeqVar_StoryManagerBool_0` as `Heatsink_acquired`, index 4049. `SeqCond_CompareBool_0` reads it to gate a hint. Record it as a separate acquisition-state anchor; this inspection does not establish its write timing or make it a reload-completion flag. |
| `plot.booleans.4050` | Pinned TSE raw database labels this `Weapon_reloaded`, but the inspected LS10 route does not establish a native read/write to 4050. The label is a search lead, not an accepted completion predicate. |

The save path is the LE2 `plot` table, **not** `me1_plot`: pinned TSE
`6451f23d1fba62358328dce4e87733970f36ebf1`,
`src/save_data/mass_effect_2/mod.rs::Me2LeSaveGame` and
`src/save_data/shared/plot.rs::PlotTable` retain integer arrays and LSB-first
packed boolean arrays. Its `databases/me2_raw_plot_db.ron` independently labels
477/4049/4050; the native package reference supplies the stronger evidence for
477 and 4049. Missing or truncated entries are unavailable, never assumed zero.

`SFXWeapon.StartReload.TransitionToReload` emits the tutorial event before entering
`Reloading`; the ammo transfer follows its own native timing/cancel path.
Therefore the plot transition cannot substitute for before/after weapon counts.
For acquisition, archive exact normal saves and compare integer 477, boolean
4049, map/streaming context, the pistol record and actual post-load weapon
observations alongside the scene. Retain the full semantic diff and unexplained
changes. An authored SaveGame node is a request, not evidence that a file was
created. No plot reset/write recipe follows from these anchors.

Raw base/Patch01 sequence output remains private under
`local-tools/le2-validation-evidence/tutorial-reload-source/`; no native game
was run or modified for this audit. Mounted-package precedence, live tutorial
progression, save timing and repeatable checkpoint acceptance remain separate
validation gates.

## Recovering a natural onboarding checkpoint without another collector

2026-11-14 offline audit at `54a731e1`: the existing probe batch (11 indexed
files) and chapter ammo batch (14 indexed files) still match every recorded
hash/size. Their saves all retain integer 477 = 120 and boolean 4049 = false;
none is an LS10 pre-reload checkpoint. The first protected opening attempt's
reset archive contains four ME2 saves; its only save addition was the staged
chapter fixture, not a new natural career. Do not mistake that added slot for
successful onboarding. Scanning still needs normal Crew Deck-to-CIC/galaxy
navigation and orbital capture; its one-probe bytes already exist and need no
new preparation. See [scanning acquisition](le2-scanning-readiness.md).

There is an earlier recovery opportunity in both inspected RezRoom copies:
`LS5_Go_Get_Pistol.SeqEvent_RemoteEvent_2` (event LS5) reaches
`SFXSeqAct_SaveGame_0`, whose **Out** link reaches `BioSeqAct_ToggleSave_1`.
Its Enable variable is `SeqVar_Bool_1`, serialized true. This authored route
requests an early save and enables saving; it does **not** prove disk completion
or that the current pause menu permits a manual save. Capture the actual LS5
checkpoint if produced, then the pre-reload prompt and a post-tutorial quiet
checkpoint. Preserve the first before continuing: a later autosave can replace
it. LS10's later state-15 save cannot be assumed to preserve the pre-event state.

Reuse the existing scripted reset archive for collection:

1. The protected session owner progresses normally and records the actual new
   career/slot and scene. At a useful checkpoint, stop further progression and
   close the owned game normally. The existing `session.py reset RUN` requires
   quiet ownership; follow its normal refusal/restoration rules. This is a future
   owner action, not an instruction to reset a foreign or already restored run.
2. `session.reset` already snapshots each protected tree into a unique
   `RUN/after/<index>-<uuid>` before restoration. It records the original
   `protected[].path`, `archives[].path`, and each relative filename's SHA-256,
   size and timestamps in `archives[].state.files`. It verifies unchanged source
   during archival, preserves retry archives and marks restoration. After reset,
   require recorded `phase=restored` and `restorationVerified=true`; resolve the
   archive entry for the protected profile root, not an assumed numeric index.
3. Run `python tools/qa-le2/session.py archived-saves RUN`. It checks each
   archived ME2 save's hash and size and compares it with the original snapshot
   and staged-file identity. Use the scene and slot evidence to select the
   actual `Save/ME2/<career>/<filename>.pcsav`. Multiple reset attempts remain
   separate entries. Keep the context hash, run ID, archive entry and original
   relative path alongside the recovered bytes. Use the recorded path in the
   archive even if the restored live profile has since changed.
4. Use the existing commands below, with a new private output directory and
   actual resolved archive paths. `roundtrip` validates LE2 v30/CRC and writes
   unchanged bytes plus a hash receipt exclusively. The pinned TSE oracle checks
   decoding and byte-identical reserialization. Require both Boolean results
   true, the expected upstream revision, and agreement of map, plot anchors and
   saved weapon fields with `le2-save.py info`. Preserve unexplained state.

```powershell
python tools/le2-save.py roundtrip "$ArchivedSource" "$NewPrivate/checkpoint.pcsav" --receipt "$NewPrivate/checkpoint.receipt.json"
python tools/le2-save.py info "$NewPrivate/checkpoint.pcsav" --json
& $PinnedTseOracle "$NewPrivate/checkpoint.pcsav" "$NewPrivate/checkpoint-tse.json"
```

These commands operate on archived bytes; they do not supply PID/career authority
on their own. `check-save-oracle.py` without an explicit prepared triple also
creates a synthetic edited format probe; that generated `prepared.pcsav` is
**not** the recovered checkpoint. Keep the unchanged archived source as the
acquisition candidate. No dedicated post-reset extraction helper is needed.

The normal `save-evidence.py before/after` collector is deliberately narrower:
`inputs` requires a game-bound lease plus an existing frozen manual
`fixtureSelection`, and `observation` requires that exact descriptor in the same
career's complete list. A new empty career or unrelated first autosave cannot be
admitted merely by adding a baseline. Do not alter a frozen run to force it.
Once a natural save is archived and its actual manual mapping reviewed, prepare
a **fresh** hash-bound session for that career. Use ordinary career selection
where the scripted menu route cannot select it, verify the current descriptor
and scene, and reuse the existing collector for subsequent normal New Save
trials. The archive/decoder proof is not a substitute for that normal-load proof.

Private evidence identifier: `le2-recovery-handoff/recovery-audit`.
`audit.json` SHA-256 is
`9005d1489779494d9cf38bcf2d3cdfbef4d93022f4743acaa800ca149545966f`;
`reset-recovery.json` is
`1a1a58a2f9d1153e1d7d8a6cc9260ec0aa02e25e74913a99ab212b9d0711533d`.
The existing reset mapping and exclusive archive command were exercised offline
on already archived chapter bytes; pinned TSE accepted the unchanged base.
The scripted inventory matched 92 saves across 23 completed reset archives:
61 unchanged originals, 23 unchanged staged entries and eight modified
originals. None had a new unstaged path. The native New Save from the resource
cycle was collected separately and preserved through qualified recovery,
so its original context falls outside this completed-reset inventory.
No new Lazarus fixture, gameplay, input, deployment or restoration occurred.

## Native reload sequence that VR must preserve

Sources: `SFXGameModeDefault.TryReload`, `SFXWeapon.CanReload`, `TryReload`,
`ShouldAutoReload`, `StartReload`, `Reloading`, `DoEarlyReload` and `DoReload`.

- The game-mode entry checks combat input/action state before requesting reload.
  Weapon eligibility includes pawn blocking actions, reloadability, used ammo,
  spare ammo and heat configuration. AI/infinite-ammo paths differ from ordinary
  finite-reserve player behavior.
- Reload can wait for cover leaning or an animated transition to finish.
  Transitioning into reload emits the tutorial event before the ammo transfer.
  Tutorial progression alone therefore cannot prove that a clip was replenished.
- Reloading schedules an early-cancel threshold, the ammo transfer and finish as
  separate timer events. The transfer invokes the weapon reload delegate and
  performs native effects as well as changing ammunition.
- For ordinary finite-reserve player reloads with positive capacity/reserve and
  integral counts, transfer is the smaller of reserve and used count. Used count
  and reserve decrease by that transfer; loaded count increases correspondingly.
  Treat this as a bounded expected relationship, not a substitute for observing
  the native function, upgrades, delegates and actual loaded weapon.
- **Cancellation can commit ammunition.** `Reloading.CancelReload` calls the
  native transfer when the early-reload flag is set, clears the timers and exits.
  The transfer clears that flag. Thus a test asserting every cancellation leaves
  ammunition unchanged would encode the wrong baseline. State exit also clears
  reload timers. Cover changes, melee and mapped powers contain cancellation
  call sites; their complete behavior remains to be exercised.

The Alyx-style physical eject/insert interaction needs an explicit design for
when it enters and completes this native sequence. Calling transfer directly
would bypass entry gates and tutorial sequencing; retaining an autonomous timer
without coordinating the gesture could commit before insertion. First measure
the native sequence, then document intentional VR differences and prove exactly
one commit. Do not infer a physical detachable-magazine system from the native
thermal-clip implementation.

## Fixture and observer work

Use the [reload tutorial acquisition lead](le2-fixture-acquisition.md) to obtain
a natural pre-reload save. Before editing, identify the existing weapon class,
actual magazine capacity, selected weapon, ammo power and finite-ammo settings.
Prepare empty and partial magazines with zero, insufficient, exact and surplus
reserves relative to the missing rounds; include a full-magazine refusal case.
Hash-bind each recipe and verify post-load values. The existing
[variant preparer](../tools/qa-le2/variant-usage.md) now accepts a reload ammo
spec with the saved weapon class and record, measured magazine capacity and
finite-ammo policy. Cases that prepare identical inputs share one output.

The final batch implementation was checked against the archived chapter save
and the one-round MassCannon capacity observed in run `1722Z-loadout`. All four
unique outputs passed the exact recipe and independent Trilogy Save Editor
checks. Only the bound weapon's ammo fields changed. The batch index SHA256 is
`3bdcd95c1ac17c76f74d40ece84fda3c96d4d666d1a4b430ffd52ab26e77c00c`.
The saved selection is a different weapon and capacity one has no partial case.
This verifies the file preparation and the explicit aliases, with loading and
reload behavior pending a suitable in-game checkpoint.

Capture input intent, eligibility/rejection, class/object generation, used and
loaded counts, magazine capacity, reserve, reload state, early-reload flag,
transfer/delegate count and tutorial events on one aligned game-thread timeline.
Do not claim event counts from sparsely sampled final values. Pair state evidence
with the visible weapon and tutorial, then check save/reload persistence using
the native save mappings above.

Exercise cancellation before the early threshold, between that threshold and
the scheduled transfer, and after transfer. Add weapon switching, cover changes,
melee/power interruption, auto reload, repeated gestures and depleted reserves.
Keep native-input and VR-input results separate. Actual timers, overrides,
tutorial completion, visual alignment and controller usability remain pending.
See the [observer design](le2-runtime-observer.md) and [coverage matrix](../docs/le2-validation-coverage.md).
