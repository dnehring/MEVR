<!-- SPDX-License-Identifier: MIT -->
# LE2 selected weapon observation

`le2_diagnostic::ObserveWeapon(uint32_t verifiedGameThread)` in
`src/engine/Le2WeaponObservation.hpp` is a read-only diagnostic, included in the
existing guarded observer translation unit. The initial implementation enables
no native action. The later bounded native observation below is separate from
reload behavior or scenario acceptance.

## Source and meaning

This extends [the ammo source audit](le2-ammo-fixture-source.md), using LExSDKv2
submodule pin `6bd05809cccb1a099a520371a95779d4a56bb246` and installed
`SFXGame.pcc` SHA256
`681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77`.
Package inspection used the existing LegendaryExplorer scanner; extracted
function bodies remain private in `local-tools/le2-validation-evidence/ammo-source`.

| JSON field under `fields` | Verified source and meaning |
| --- | --- |
| `magazineCapacity` | `SFXWeapon.MagSize.Value`; final `GetMagazineSize` returns this scaled runtime value, not the config endpoints. SDK `SFXGame_classes.hpp` `ASFXWeapon.MagSize` at `0x400`, `SFXGame_structs.hpp` `FScaledFloat.Value` at `0x20`. |
| `magazineUsed` | `SFXWeapon.AmmoUsedCount` at `0x90C`, a float used count, not loaded ammunition. |
| `loadedCount` | Final `SFXWeapon.GetAmmoCountInMagazine`: integer conversion of float capacity minus used. Truncates toward zero. Available only for finite, nonnegative capacity/used, used no greater than capacity, and result below `2^31`. |
| `reserveStored` | Signed `SFXWeapon.CurrentSpareAmmo` at `0x908`, raw stored value. Negative values remain visible as raw evidence, not a legal ammo assertion. |
| `effectiveReserve` | Source-derived float32 conversion of nonnegative `CurrentSpareAmmo` only for the audited declaration policy below. Unknown classes/overrides or unavailable/changing metadata remain unavailable. `SFXHeavyWeapon` remains unsupported because its getter returns zero. No native getter is invoked. |
| `infiniteAmmo`, `weaponCanBeReloaded`, `earlyReload`, `drawingWeapon`, `holsteringWeapon`, `playerUsable` | Corresponding `ASFXWeapon` bitfields at `0x968`. These are sampled flags, not a replacement for `CanReload`, reload-state evaluation, transfer events, or completion. |

Each field has `available`, `reason`, `source` where applicable, and `value`
only when available. Nonfinite floats are omitted from JSON. Stable finite raw
values remain observable even when invalid for the derived loaded count. The
observer does not clamp values, read a save cache, infer an ammo ledger or run
weapon functions. Heavy ancestry is reported as a structural class fact.

## Identity and sampling boundary

The existing resource-root sampler proves current local player/controller/pawn
reciprocals, player inventory and actor WorldInfo agreement. From that pawn,
the observer reads `APawn.Weapon` (`Engine_classes.hpp`, `0x480`) and requires
live object-table membership, `SFXGame.SFXWeapon` ancestry, and matching weapon
`Instigator`, `InvManager` and `WorldInfo`. No inventory scan or arbitrary
weapon fallback is used. The pending weapon pointer is sampled for change
detection and reported only as presence, not dereferenced or selected instead.

Two individually guarded scalar passes are followed by a fresh context sample.
Changed roots, object-table header, selected weapon/class/name or pending
selection invalidate the observation. A changed or unreadable scalar is
unavailable independently; loaded count requires both capacity and used.
Numbered runtime names use bounded base-name text plus the raw `nameNumber`;
no engine name construction or suffix convention is assumed. JSON identities
contain class/index and weapon base name/number, not addresses or wire handles.

This is a single verified game-thread API. It performs no writes, allocations
through the SDK allocator, `StaticClass`, `FindFunction`, generated wrappers or
`ProcessEvent`. Ordinary host JSON allocation is outside guarded readers.
SEH catches read faults; it does not establish lifetime ownership. Repeated
matching reads are not a transactional simulation snapshot or protection from
unobserved change-and-restore/object reuse. `durableObjectIdentity` and
`coherentSimulationSnapshot` remain false. Actor WorldInfo and the separately
sampled GWorld are the existing resource context's structural boundary, not a
new map-containment proof.

There is no claim of current reload eligibility, finite-reserve policy,
state-machine label, reload invocation/transfer count, persistence or gameplay
acceptance. Establish those with a protected live fixture and before/after
observations plus scene evidence. The immediate purpose is honest selected
weapon/ammo evidence for the reload baseline.

## Offline validation

The existing standalone `tests/le2-observation` project includes
`WeaponObservationTests.inc`. Original synthetic SDK objects cover selected
identity/numbered names, used-versus-loaded semantics, fractional truncation,
integer conversion boundary, negative/nonfinite samples, heavy/infinite flags,
null/wrong/template/foreign ownership, selection and scalar mutations between
passes, and a real protected-memory ammo-page fault preserving readable
capacity without manufacturing ammo. All checks remain active in Release.
The production translation unit is compiled separately without test seams.

From a repository checkout with its pinned SDK submodule initialized:

```powershell
cmake -S tests/le2-observation -B local-tools/weapon-sdk
cmake --build local-tools/weapon-sdk --config Release --parallel 4
ctest --test-dir local-tools/weapon-sdk -C Release --output-on-failure
cmake --build local-tools/weapon-sdk --config Debug --parallel 4
ctest --test-dir local-tools/weapon-sdk -C Debug --output-on-failure
```

An isolated worktree can pass `-DMEVR_OBSERVATION_SDK_DIR=<initialized SDK path>`.
No SDK source is vendored, and this project neither builds nor deploys an ASI.
## First native sample and installed MassCannon source

Protected run `0ad9e033-a97d-4108-bbe7-e38a58812386` on 2026-11-14 used candidate
`d1cada86a839be82a58f9094c4a8f4b5f51b3aad`. Completed `observe-weapon` request
`cff8ccbc-52d3-435a-8a7b-ac856eca6c46` reported exact selected class
`SFXGameContent_Inventory.SFXWeapon_MassCannon`, base name
`SFXWeapon_MassCannon`, nameNumber 2 and object index 122640. These last values
identify only this process-local sample. Capacity was 1, used count 0,
source-derived loaded count 1, raw stored reserve 10, and infinite-ammo and
heavy-weapon-ancestry flags were false. The runtime effective reserve remained
unavailable. This proves a bounded live observation, not a reload event.

An independent offline audit verified the archived request/result identities,
unique consumed request, frozen session/fixture/scenario receipts, deployment
attestation, PID/start binding and exported state hash. Recomputing normalization
and starting-state checks reproduced the artifact exactly. Export exit 2 is
correct: effective reserve is unavailable, and this weapon-only query contains
no map or resource observation. No separate query was merged into it. The
session binding reported verified restoration. Private export and source-audit
receipts preserve the full hashes; no runtime field was promoted by this audit.

Installed `SFXWeapon_SniperRifles.pcc` SHA256
`d80041e9bada2dc84f1e5d3e4c4fdbc0a335512c5b8299e76a3f8fec3455f2ea`
establishes this superclass chain through package export metadata:

| Declaration | Package/export | Own function/state declarations relevant to inheritance |
| --- | --- | --- |
| `SFXGameContent_Inventory.SFXWeapon_MassCannon` | `SFXWeapon_SniperRifles.pcc`, 57 | Four decal/impact initialization/effect functions; no ammo/reload/state override. |
| `SFXGameContent_Inventory.SFXWeapon_SniperRifle` | Same package, 21 | Three decal/impact functions and `SetZoomed`; no ammo/reload/state override. |
| `SFXGame.SFXWeapon_SniperRifle_Base` | Pinned `SFXGame.pcc`, 32232 | `ModifyDamage` only. |
| `SFXGame.SFXWeapon` | Pinned `SFXGame.pcc`, 26245 | Audited magazine/reserve getters and reload implementation. |

The concrete MassCannon class is absent from the SDK class list; the package
metadata supplies that missing link. It does not derive from `SFXHeavyWeapon`.
For this installed chain, the base `GetCurrentSpareAmmo` implementation converts
`CurrentSpareAmmo` to float, making 10 the **source-derived interpretation** of
the sampled stored reserve. No native getter was executed, and installed source
inspection does not independently prove unchanged live function bytecode.

The sampled magazine was full. The audited base `CanReload` requires a positive
used count, so that predicate was unsatisfied in this sample. This is source
analysis, not an observed eligibility-call result. `TryReload` also has state
overrides in `StartReload`/`Reloading`; `DoReload` retains pawn, infinite-ammo,
heat and reserve branches. Early/cancel paths can transfer ammunition. A
one-round magazine can become eligible for automatic reload immediately after
firing, so before/after counts alone cannot establish a manual reload sequence.
Native reload initiation, timing, transfer count, finite-policy acceptance and
VR handling remain pending.

## Audited effective reserve extension (offline validated; native pending)

The existing observer now checks an exact chain for HeavyPistol, AssaultRifle,
CollectorAssaultRifle_Player, LaserPistol or MassCannon down to `SFXWeapon`.
It walks existing class children and all declared/inherited state children with
an aggregate 8192-field budget and 64-level limits. Nearer/state-local getters,
invalid membership/ownership, cycles, missing declarations and unexpected return
metadata refuse the derived field. Parent class state declarations are checked
as well. The opaque SDK StateFrame is only compared as a pointer value, never
interpreted as a state-machine label. This is structural declaration evidence,
not execution of the VM resolver or proof of unchanged mounted script bytes.

The base declaration must be exact Core.Function, flags `0x20102`, native index
zero, one four-byte float return at offset zero; its sole child must be exact
Core.FloatProperty `ReturnValue`, dimension one, size four, flags `0x580`.
Metadata and scalar reads run in two guarded passes, followed by a fresh weapon
context read. Class/super/child links, getter/return-property identity and
StateFrame pointer must agree. Each
walk revalidates the complete override exclusion and layout. Unobserved
change-and-restore and object reuse remain outside the proof boundary.

Policy `le2-audited-base-reserve-v1` emits source
`SFXGame.SFXWeapon.GetCurrentSpareAmmo: float(CurrentSpareAmmo)` with process-local
function/property indices, selected class and pinned SDK/base-package provenance.
`nativeGetterExecuted` and `mountedScriptBodyProven` are explicitly false.
Negative stored values remain raw evidence only. Positive values are converted
using float32, not copied as integers: 16777217 becomes 16777216; INT_MAX becomes
2147483648. This does not assert legal reserves or finite reload eligibility.
Existing exporter correlation and frozen-artifact checks remain unchanged; it
validates this derivation before publishing `state.weapon.effectiveReserve`.
Old unavailable-field archives remain readable. No cross-query merge occurs.

Additional installed source pins, inspected with the existing LegendaryExplorer
scanner and metadata reader (raw extracts remain private):

| Package | SHA256 | Relevant exact inheritance |
| --- | --- | --- |
| `SFXWeapon_HeavyPistols.pcc` | `ddf46d391b0a940b510268d99dba91172984355e8aa238a985357c09c0614fe4` | HeavyPistol -> HeavyPistol_Base -> SFXWeapon; no reserve/reload override. |
| `SFXWeapon_AssaultRifles.pcc` | `545017a63deaf1d129f12159632954edc9a19136c7376809f081a57b02fc9e56` | AssaultRifle -> AssaultRifle_Base -> SFXWeapon; no reserve/reload override. |
| `SFXPlayerWeapons_Collectors.pcc` | `987c71405c22c8cd459e9fc475a382f96e526bcabfce14dcd6ee6db9ce9462c2` | CollectorAssaultRifle_Player -> AssaultRifle. Reload/consume overrides delegate to SFXWeapon before muzzle effects; reserve getter inherited. |
| `SFXWeapon_LaserPistol.pcc` | `d6391ccdb3c465a2598ba9e4e746263134d94ae33bfc8f51ee1d1bdd25abc35c` | LaserPistol -> HeavyPistol; no reserve/reload override. Custom firing timing remains separately unverified. |

The existing natural fixtures contain the Collector rifle. Selecting it through
normal controls and observing a partially used magazine is a useful next reload
baseline; capacity and finite-ammo policy still require current evidence.
`SFXDroppedAmmo.GiveAmmo` distributes independent awards to carried weapons;
`SFXWeapon.AddAmmo` may first refill an inactive magazine, then its own reserve.
These inspected paths establish shared pickup distribution, not a shared reserve
ledger. Exclude pickups, grants, replacement and concurrent firing from a reload
conservation interval. `GetMaxSpareAmmo(false)` includes used magazine count;
`bInfiniteAmmo=false` alone does not establish heat policy or reload eligibility.

Synthetic tests cover float32 rounding/zero/negative bounds, unknown classes,
nearer/state overrides, cycles, missing/wrong metadata, metadata replacement and
raw-value changes. Existing protected-page tests preserve unavailable ammo;
Release/Debug production compilation excludes test seams. Native effective
reserve and reload accounting acceptance remain pending.
