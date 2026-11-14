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
