<!-- SPDX-License-Identifier: MIT -->
# LE2 selected weapon observation

`le2_diagnostic::ObserveWeapon(uint32_t verifiedGameThread)` in
`src/engine/Le2WeaponObservation.hpp` is a read-only diagnostic, included in the
existing guarded observer translation unit. Host/wire integration and live
weapon validation are separate work. This change enables no native action.

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
| `effectiveReserve` | Unavailable: selected class's effective virtual/script getter has not been resolved. Base `SFXWeapon.GetCurrentSpareAmmo` returns the stored integer as float, but inspected `SFXHeavyWeapon.GetCurrentSpareAmmo` returns zero. Inherited storage is not universally effective reserve. |
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
Native validation remains pending.
