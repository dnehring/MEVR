<!-- SPDX-License-Identifier: MIT -->
# LE3 native ammo observation and combat fixture gate

2026-11-15. This increment replaces the scalar responsibility of
`game/ConsoleRequest.cpp::WeaponProbeLe3` with `game/Le3WeaponAmmo.hpp::Observe`.
It is an acquisition/read-only adapter, not production physical reload. Native acceptance
of the new queries and metadata is still required.

## Reuse and caller contract

Use the existing `mevr weaponprobe` request, game-thread transport and protected session.
The console owner can return `"mevr weaponprobe: " + json::Write(le3_weapon_ammo::Observe(pc), 0)`
after including the header.
No extra runner, inventory, native lifecycle or dispatch framework is introduced.
The LE2 observer remains unchanged: its SDK layouts, float reserve result and reviewed
class-chain policy cannot be relabeled as LE3.

The snapshot brackets controller, pawn, WorldInfo, inventory, selected weapon, pending
weapon and mode around its calls; pointers are accompanied by object index, name bits and
class. Ownership must agree with native pawn/controller, weapon instigator and inventory.
Raw scalar fields must remain equal across the calls. These are short-lived observation
identities, not persistent identities or a coherent engine transaction. Use on the game
thread after reflection validation only, never as an asynchronous retained weapon handle.

Each raw/effective field has separate availability. `available` at the top means the
selected-weapon context survived observation, not that all queries succeeded. `gameplayMode`
is only a mode observation. `ordinaryCombatVerified`, `dispatchCapability`,
`reloadCompletionVerified`, `durableObjectIdentity` and `coherentSimulationSnapshot` remain
false. Mode 6 injured observations may prove a scalar contract, never ordinary combat.

`ue3/UnambiguousMethod.hpp` resolves the nearest actual class method and conservatively
refuses any same-named state method, including inherited state methods, malformed chains,
duplicate declarations and bounded-walk exhaustion. Passing a base UFunction directly to
ProcessEvent would not prove that UnrealScript's active state override had been selected.
No opaque StateFrame offsets are guessed. An unavailable getter is not replaced with a
guessed effective count. The existing synthetic reflection harness tests these cases.

## Installed LE3 contracts

Audited from the installed `SFXGame.pcc` with the existing private package scanner; the SDK
parameter declarations are in LExSDKv2 `LE3/SFXGame_f_structs.hpp`. Only original summaries
are committed; extracted script text remains private.

| Query/field | Meaning and limit |
| --- | --- |
| `SFXWeapon.MagSize.Value` | Stored float capacity; not automatically the effective count for every class. |
| `SFXWeapon.AmmoUsedCount`, `CurrentSpareAmmo` | LE3 integer storage, unlike LE2's float ammo-used field. |
| `SFXWeapon.GetMagazineSize` | Final integer truncation of the stored magazine size. |
| `SFXWeapon.GetAmmoCountInMagazine` | Base method subtracts integer ammo used from native magazine size; overrides remain authoritative. |
| `SFXWeapon.GetCurrentSpareAmmo` | LE3 integer result; base returns stored spare ammo, overrides can differ. |
| `SFXWeapon.CanReload` | Base combines pawn admission, reload flag, ammo used and native spare-ammo availability. |
| `BioPlayerController.IsReloading` | Final query delegates to the pawn with pending reload requests included. |
| `bInfiniteAmmo`, `bWeaponCanBeReloaded` | Raw flags, not a complete family classification or admission decision. |

The adapter records actual getter declarations. Base-only scalar cross-checks distinguish
a mismatch from a legitimate overriding reserve/loaded-count method. Negative native counts
are unavailable, while raw values remain diagnostic. Nonfinite raw capacity is unavailable.
The adapter never writes capacity, used count, reserve, flags, weapon state or resources.

`TryReload` is the normal admission/request path: it checks native eligibility and requests
the reload fire mode. It returns no outcome. `DoReload` performs the transfer and native
effects and must not be called directly by VR insertion. The base reserve limit also depends
on whether current magazine usage is included, so a guessed reserve cap is inappropriate.

Keep `core/vr/PhysicalReloadRequest.hpp` and `tests/le2-reload-core` as the shared coordinator
prototype. Production hand ownership, supported weapon profiles, native request/transfer/
completion/cancel observation and ordinary combat conservation tests remain open. No call to
TryReload, StartFire or DoReload is made by this adapter. Lancer regeneration, charged fire,
heavy weapons and special shotgun behavior are not declared conventional thermal-clip
profiles merely because a getter returned a number.

## Existing fixture evidence

Reuse `tools/le3-save.py` full v59 decode/roundtrip, its independent TSE semantic comparison,
and `tools/le2-halcyon-run/load.ps1 -Game LE3 -Holder ... -Save ...`. The protected runner
requires owned session/process evidence and descriptor plus world/pawn transition and
ordinary gameplay, then verifies unchanged source saves. See `research/le3-save-foundation.md`.

The six known saves were decoded without modification:

| Fixture group | Save evidence | What it can establish |
| --- | --- | --- |
| Four Normandy saves, including Save31 | BioP_Nor, Vanguard noncombat class, noncombat flag, empty saved player weapon array | Normal load and same-world reload were reported passed for Save31 by the coordinator; do not infer combat or galaxy availability from the other three. |
| ChapterSave | Biop_End001, Vanguard, three saved weapons; loaded beam/facility sublevels and active ConduitStumble streaming state | A late beam/conduit exception fixture. Coordinator observed cinematic mode 8 followed by injured mode 6. It does not satisfy ordinary combat. |
| NewGamePlusSave | BioP_End002, injured flag and endgame state | Import/NG+ artifact, not an ordinary native-load fixture. |

ChapterSave's saved weapon classes include Lancer, Assault_GUN02 shotgun and Silencer.
That is evidence of serialization, not proof of the live selected weapon, functioning ammo
or an available inventory. Native read-only weapon observations in its exception mode are
useful only with that context recorded explicitly.

The installed End001 package has authored startup/debug paths that initialize party,
consumables and progression. A raw map open, debug remote event or changed streaming/plot
bit would bypass normal progression and is not a justified combat fixture. The source
contains a native cinematic-skip command, but skipping a cinematic does not undo this late
checkpoint or prove an ordinary combat branch. No such command is proposed as a workaround.

Current editor mutations intentionally support bounded resources, not arbitrary level,
streaming or story restoration. There is no justified ordinary-combat clone from this
corpus yet. Next acquisition must use an established normal travel/mission path from a
coherent save, or an independently sourced compatible checkpoint with reviewed provenance.
A Citadel range, if actually acquired, can prove weapon presentation and reload observations;
it still cannot stand in for hostile AI, squad actions, damage, class melee or story tests.

## Acceptance still required

1. Integrate the console replacement and pass the additional LE3 metadata table.
2. Observe ordinary and exception/no-weapon contexts, actual class overrides, pending weapon
   changes and unavailable state overrides. Preserve per-field refusals in private receipts.
3. Acquire an ordinary mode-0 combat fixture through normal loading/travel; keep the current
   load runner's gate unchanged. Query availability alone does not clear this requirement.
4. Only then implement/test a native request adapter against the shared reload coordinator:
   empty, partial, denied, cancelled and interrupted reloads with observed native transfer and
   conservation, profile-specific exceptions and no synthetic ammo writes.
