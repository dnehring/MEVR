<!-- SPDX-License-Identifier: MIT -->
# LE2 engine access: bounded P1 discovery

Date: 2026-11-13. Owner: engine discovery worker. Base: `b5811bf52378a5d7d7e019821d1b538b5734b41c`. **Offline source inspection only; no LE2 runtime, call, travel or headset proof.** No game launch, deployment, profile/save edit, request file or lock change was performed. ME1 testing remains owned by its current operator; elapsed time does not authorize a native run.

The smallest useful implementation is **P1a: validated, game-thread, read-only player/world/weapon observation**, followed by **P1b: one scalar getter call**. Do not turn on the entire LE1 feature set just because a new LE2 reflection layout resolves. LE2 already has an SDK-backed compatibility path, but the shared reflection layer and its checks are disabled. The SDK declares materially different UFunction offsets and a different ProcessEvent vtable slot. Copying LE1's layout would be unsafe.

This extends the [implementation plan](../docs/le2-port-implementation-plan.md), [observer audit](le2-runtime-observer.md) and [ammo source findings](le2-ammo-fixture-source.md). P0 owns shared contracts, CMake and integration; movement/cover discovery has a separate owner. No implementation or ABI changes are included here.

## Evidence and provenance

| Source | What this inspection establishes | What it cannot establish |
|---|---|---|
| Current C++ engine, host and game source at the base above | Existing compile gates, access routes and validation behavior | That an LE2 target was compiled, loaded or exercised in this task |
| Pinned `external/LExSDKv2`, revision `6bd05809cccb1a099a520371a95779d4a56bb246` | Generated declarations for LE2 `2.0.0.48602`, candidate layout/RVA/signature facts | Correct live memory, native behavior or distribution permission |
| Installed base `SFXGame.pcc`, SHA-256 `681f3fc72c08c66bc6d77ca91102e08cead7c6a5586c22bad0a8f7085be38d77` | Inspectable package script for the five functions below | Mounted DLC precedence, native executable bodies or actual timing |

The additional reference checkout under `reference/LExASIs/External/LExSDKv2` is revision `3885be14113816ff1d2a0d941aa96bba6df9dc58`, **not** the project's SDK pin. Its LE2 `Core_classes.hpp` is byte-identical to the project's pinned file: SHA-256 `4b0817f26d3e488904bd96e0fd9caee181b0819272b8d81f5619fc6440bf1ee3`. The tables below use the project pin; other files are not assumed identical.

Read-only scanner used: existing `local-tools/le2-inspection-root/conversation-scan.exe`, SHA-256 `918a095735f00998e5ff99871902253c47c66069a6cce3e6d6b0d474addd6f01`. Separate new outputs remain ignored in this worktree's `local-tools/engine-access/`. Five exact selectors succeeded with one result each:

- `SFXWeapon.GetMagazineSize`
- `SFXWeapon.GetCurrentSpareAmmo`
- `SFXWeapon.GetAmmoCountInMagazine`
- `SFXGameModeDefault.TryReload`
- `SFXWeapon.TryReload`

Command shape: `conversation-scan <LE2-base-CookedPCConsole> <new-private-output> --game LE2 --script <Class.Function> --only SFXGame`. The script output does not embed a content hash; the package was hashed separately above. `--only` is a substring filter, not a mounted resolver. A search for `SFXWeapon.GetReloadAnimation` returned zero results/exit 1 and supplies no evidence; it is not a proposed dependency. A comma-separated selector was rejected before inspection; use one exact selector per command. No extracted code or game data is committed.

## Existing access and the missing boundary

| Area | Existing route | P1 consequence |
|---|---|---|
| Startup | `mod/Mod.cpp` initializes `engine::legacy` on non-LE1. `ScriptPackagesLoaded` waits for `Engine.Camera.UpdateCamera` via Legacy, then calls reflection initialization once | One camera function existing does not prove required LE2 classes/fields or an active player. New P1 readiness must wait for its own dependencies |
| Globals | `Legacy::Init` resolves SDK module-relative addresses for allocator, objects, names, engine and world; resolution checks nonnull addresses | A nonnull module-plus-RVA is not a valid global. Validate array bounds, slot/index agreement, names and classes before reads |
| World | `engine::World` delegates to `legacy::World`, which dereferences `GWorld`. `ResolveWorld` is false for LE2 | No validated reflected world path or travel generation. Reuse the reflected local-player/WorldInfo/outer-World route only after proving each link |
| Player | `legacy::LocalPlayerController`: engine → `GamePlayers[0]` → `Player.Actor`. Pawn location reads `Controller.Pawn` | Null checks exist, but no per-link liveness/class/generation checks. The shared `game::player` path uses reflection and therefore does not work merely because Legacy works |
| Mode/camera | `GameAdapterSdk.hpp`: `GameModeManager2`, `CurrentMode`, camera cache and view parameter spelling bridge | These existing SDK reads are a comparison oracle, not a reason to introduce new SDK-backed feature code |
| Weapon | No Legacy weapon accessor. `game/Weapon.cpp` startup/tick is LE1-only | Establish the equipped pawn's weapon identity before any firing/reload work; do not treat a save inventory entry as the live equipped object |
| Reflection | `GameAdapter.hpp` returns nullopt for LE2 layout/globals. `Addresses.hpp` leaves LE2 reflection/allocator globals zero. `ReflectCheck.cpp` reference implementation is compiled for LE1 development builds only | Add bounded LE2 validation rows and an explicitly gated access slice; enabling a layout alone supplies no comparisons |
| Lifetime | `runtime::IsAlive` guards reads and checks GObjects slot/index plus pending-kill. `ClassRef`, `FunctionRef`, `FieldRef` cache resolutions until `Reset`; they have no automatic world epoch | Alive means currently listed, not the same logical incarnation. Same-address, same-class recycling can defeat pointer/slot checks. Reset owned caches and invalidate outstanding identities across context transitions |

Source: [Legacy](../src/engine/Legacy.cpp), [GameAdapter](../src/engine/GameAdapter.hpp), [Engine](../src/engine/Engine.cpp), [Player](../src/game/Player.cpp), [Runtime](../src/ue3/Runtime.cpp), [Handles](../src/ue3/Handles.hpp).

## Candidate layout facts, not live offsets

The following are SDK declaration evidence from `Src/LESDK/LE2/Core_classes.hpp` and `Common/SFXName.hpp`, not permission to hardcode gameplay fields. Feed an original, reviewed LE2 layout into the existing reflection infrastructure; derive feature field offsets from live properties. Do not copy SDK implementation code. `CLAUDE.md` requires new engine access through `ue3/` and eventual SDK-free sequel releases.

| Reflection component | LE2 candidate declaration | Difference/risk |
|---|---|---|
| UObject index/flags/outer/name/class | `0x08 / 0x0C / 0x40 / 0x48 / 0x50` | Unaligned packed flags; preserve full 64 bits. Pending-kill bit 61 and template flags need validation |
| UField super/next; UStruct children/property size | `0x60 / 0x68; 0x70 / 0x78` | Declaration agrees with current LE1 numeric values; not proof of LE2 live layout |
| UProperty array dimension/element size/flags/offset/typed member | `0x70 / 0x74 / 0x78 / 0x94 / 0xD0` | Typed member depends on property kind; BoolProperty mask and StructProperty type must be verified |
| UFunction flags/native index/parameter size/return offset/function pointer | **`0xD0 / 0xD4 / 0xE2 / 0xE4 / 0xF0`** | LE1 uses `0xD8 / 0xDC / 0xEA / 0xEC / 0xF8`; the apparent eight-byte shift cannot be applied to every other structure |
| UObject ProcessEvent vtable byte slot | **`0x240`** (slot 72) | LE1 is `0x250`. Wrong adjacent virtual methods can still point into executable text |
| Names | 29-bit offset, 3-bit pool selector, text at entry `0x0C`, wide flag `0x40000000` | SDK declaration; validate representative ANSI/Unicode/numbered names and pool bounds |
| Script frame | Existing LE2 `GameAdapter::kFrame`: Node/Object/Code/Locals/Previous `0x14/0x1C/0x24/0x2C/0x34` | Separate packed FFrame layout; do not infer from UObject or LE3 |

`Src/LESDK/Init.hpp` LE2 supplies candidate RVAs: GObjects `0x173CC48`, name pools `0x1668A10`, GEngine `0x175EF28`, GWorld `0x1762340`, GMalloc `0x1682378`, name initializer `0x78640`, GNatives `0x16A99B0`. Existing project LE2 hook entries are ProcessEvent `0xD8D50`, CallFunction `0xD6770`, GameEngineTick `0x546980`. Hook RVAs have existing `.pdata` checks; that does not validate data globals. Do not fill unknown `appMalloc/appFree` with GMalloc or name-initializer addresses. P1a needs neither engine allocations nor name creation.

Reuse `runtime::Locate`'s guarded object/name/core-property checks and startup retry distinction. Strengthen the P1 call gate: current Locate only requires the selected virtual method to point into `.text`; a mismatch with expected ProcessEvent RVA is recorded, not rejected. Require agreement with the already validated host hook target/original chain, allowing only identified host detours. A raw equal-to-RVA test must not misclassify the known hook as a new engine version. No executable decompilation is necessary to write this gate; a later native mismatch is a concrete reason to investigate before calling.

## P1a minimum read-only candidate

Implement a separate disabled-by-default LE2 observer capability, not blanket success for all `FieldRef`s. Every required field/function/parameter gets a validation row, as required by `CLAUDE.md`. Missing means an explicit reason and no sample/action; never synthesize zero ammo or stale previous values.

| Required handle | Expected kind/shape and purpose |
|---|---|
| `Engine.Engine.GamePlayers` → first valid `Engine.LocalPlayer`; `Engine.Player.Actor` | Array of object references → local controller; validate count/max/data, element type and each object before dereference |
| `Engine.Controller.Pawn`; `Engine.Actor.WorldInfo`; WorldInfo's outer World | Object references, expected class chains; world/pawn availability defines context validity, with no pawn/world-list traversal |
| `SFXGame.BioPlayerController.GameModeManager2`; `SFXGame.SFXGameModeManager.CurrentMode` | Object then byte enum; on-foot mode 0 is a declared candidate, validate against observed context |
| `Engine.Pawn.Weapon` → `SFXGame.SFXWeapon` | Object/class/outer identity of the equipped weapon; null/unarmed is a valid unavailable state, not a reflection failure |
| `SFXGame.SFXWeapon.AmmoUsedCount`, `CurrentSpareAmmo` | Float32 used count and Int32 reserve; preserve fractional used count |
| `SFXGame.SFXWeapon.MagSize` → nested `Value` | Struct then Float32. SDK declares `ScaledFloat` size `0x28`, Value offset `0x20`; inspect live struct metadata rather than treating MagSize as an object |
| `SFXGame.SFXWeapon.bEarlyReload` | Reflected boolean mask; declaration mask `0x200` at `0x968`, not a standalone byte. This flag alone does not identify reload phase |

Include core class/property metadata, object flags/index/outer/name/class and explicit live-vs-template distinction in validation. Do not use these declaration offsets as a second handwritten weapon struct. Read one coherent game-thread sample after the original engine tick, copy only scalar/identity data to consumers, and never let Present/XInput traverse Unreal pointers.

Suggested snapshot contract for P0 review: host/mod generation, monotonically increasing observation context epoch, tick/time, valid/reason, world/controller/pawn/weapon identity tokens, weapon class, mode, magazine capacity float, used count float, reserve int, early-reload boolean with independent validity. Identity tokens are ephemeral diagnostics scoped to an epoch, never save IDs or durable object serial numbers. Stamp invalidation on missing/changed world, controller, pawn or weapon, load/travel callbacks where established, observer disable, mod reload and session end. A pointer-only world comparison can miss address reuse; if no reliable lifecycle signal exists, leave generation proof unresolved and allow no deferred action using the old token.

P1a establishes observation, not the full P4 reload phase trace. Preserve the [ammo report's](le2-ammo-fixture-source.md) separate request/accept/transfer/cancel/finish requirements. Script timers and state-local functions still need a validated runtime seam; `bEarlyReload`, tutorial dispatch or one ammo delta cannot substitute for all of them.

## Calls: exact first probe and unresolved hazards

Installed scripts establish that `SFXWeapon.GetMagazineSize()` returns float `MagSize.Value`; `GetCurrentSpareAmmo()` returns the integer reserve converted to float; `GetAmmoCountInMagazine()` subtracts used count from capacity, then truncates the result to an integer. These are useful comparisons, with no invented constants or weapon-specific capacities.

**P1b's first call should be `SFXGame.SFXWeapon.GetMagazineSize` on the same validated active weapon as P1a.** Resolve the runtime function, validate declaring class, native index, flags, parameter size and its FloatProperty ReturnValue at the reflected offset. Build `ue3::Params` from metadata and compare the returned float to the direct nested field in the same tick. Do not use a zero-filled result as proof that a call happened: record dispatch and valid return metadata separately. Restrict this first probe to the inspected final script getter with native index zero; any mismatch disables it. No string/out-array/engine allocator support is needed.

| Hazard | Required treatment |
|---|---|
| Thread route | `plugin/Entry.cpp::GameEngineTickHook` records game-thread identity, invokes original tick, then `host::Tick` for all games. Use that existing lifecycle for P1. `runtime::Call` itself currently checks neither thread ownership nor liveness; enforce both at the agreed boundary |
| Present queue | `plugin/GameThreadCalls.cpp` is currently Halcyon/LE1-only in CMake. Its initializer needs reflection layout/globals. It copies parameter bytes and checks listed pointers/class, not a world epoch; this is not an LE2 dispatcher ready to reuse unchanged |
| Pointer parameters and returns | A byte-copy queue is not deep ownership of strings, arrays, objects or out/return storage. Keep initial calls synchronous on the game thread; future intent queues carry copied plain data plus epoch tokens, then resolve targets at execution |
| Opcode natives | Existing `runtime::Call` temporarily clears `iNative`, dispatches then restores it, based on LE1 findings. LE2 behavior and fault/reentrancy restoration are unproven. Exclude opcode natives from the first call; defer movement/Trace/SetLocation calls until validated separately |
| Generated wrappers | LE2 generated functions also contain `FunctionFlags |= ~0x400`; do not call them. Reflection-derived parameters plus the controlled runtime route avoid that corruption |
| Acceptance | `runtime::Call` returning true means it reached dispatch, not that gameplay accepted a request. The game-mode and weapon reload methods below return **void** |
| Optional/default values | `SFXWeapon.TryReload(optional bool bDisplayHint = TRUE)` is a different signature from game-mode `TryReload()`. A zeroed generated block supplies false, not the authored true default. Validate and set optional arguments deliberately |
| Mode/weapon authority | Inspected `SFXGameModeDefault.TryReload()` checks combat input, pawn action state and `CanReload`; the first request can change action state and return without reloading. Future reload intents use the active validated native mode/owner, not a direct DoReload ammo transfer |
| Handler ownership | Host ProcessEvent and CallFunction observation have existing owners; do not replace their single callback slots. Integrate through existing mod handler registration or an agreed P0 multiplexer, with unload cleanup |

For future mode dispatch, validate `SFXGameModeManager.GameModes` and selected mode object/class/outer in addition to `CurrentMode`; a numeric mode alone is not a callable object. The weapon script's TryReload calls the native firing-state route after `CanReload`; its return gives no success bit. State-local reload functions may require CallFunction/FFrame observation rather than a top-level ProcessEvent handler; an event capture experiment must establish coverage before relying on it.

Allocator/name construction, native state-frame interpretation, generic asynchronous calls, cover, weapon transforms, aim, reload mutation and all LE1 feature enablement remain outside the initial candidate. If future movement needs the native-only `BioPlayerController.CanStorm` body, inspect that exact native implementation only after source-level callers fail to answer a bounded question; do not expand P1 into general executable decompilation.

## Minimum tests and serialized native handoff

Implementation checks to add/reuse before deployment:

1. Extend existing `tests/ReflectTests.cpp` synthetic universes for the LE2 candidate shape without SDK classes: correct/incorrect UFunction parameter offsets, return type/size, bool mask, nested ScaledFloat, short/malformed arrays, wrong object class and template rejection. Keep the existing deliberately non-LE1 synthetic layout tests. Matching numeric declarations alone is not runtime validation.
2. Exercise P1 lifecycle policy with synthetic identities: load gap, pawn/weapon switch, same-address reuse after epoch change, stale queued intent, disabled observer, module reload, lost game-thread identity, unavailable ammo and nonfinite values. Every stale/invalid case must suppress dependent calls and invalidate the sample; fields must not silently retain old values.
3. Verify call policy rejects wrong thread/class/function metadata, nonzero native index and unexpected return type before dispatch; valid scalar probe returns a typed result. Distinguish dispatch failure, result unavailable and a legitimate zero. Check no mutation path is enabled by observation readiness.
4. Add scoped LE2 ReflectCheck rows and offline build checks through root integration. Run focused CTest/reflection/parameter tests plus required development, monolithic and trilogy builds; preserve LE1 gates and SDK-free policy. A project compile cannot prove vtable or live layout correctness. This discovery-only change did not compile those candidates or run native tests.

Native work remains a **future separately coordinated experiment**, after ME1's operator releases ownership and root approves the exact candidate/session. Record executable build/hash, source/build/loaded host and shadow-mod hashes, settings, effective package scope, scenario and fixture hashes, and the P1 validation report. Use the protected QA preparation/restoration workflow; do not bypass ordinary progression or edit plot state to manufacture acceptance.

First run order: start with observer disabled → enable only layout/name/object checks → require exact validated method identity → wait for the normal local player → capture scalar observations → make the single getter comparison → disable and re-enable observer → load the same save and a different available world → unequip/switch weapon where ordinary play allows → verify every epoch transition and stale-token rejection → exit cleanly. Never continue to a call after a failed reflection row. No reload/fire/console action is necessary for the first legal-call proof. A later ordinary firing/reload run correlates values to visible/native behavior using the already defined QA variants.

Stop conditions: unknown ProcessEvent target, incomplete class/field/parameter validation, off-thread script call, invalid pointer chain, stale context surviving a load, unexplained ammo values or exception. Preserve diagnostic evidence and restore through the session owner. Do not promote P1's completion until live checks, one legal call and unload/load behavior are recorded. This report is ready for implementation planning; native, VR, headset and release gates remain open.

Discovery checks: five successful read-only package inspections and the negative selector noted above; SDK core-file hash comparison; all ten relative document links resolve; `git diff --check` clean. No new validator was added: the existing synthetic reflection tests are the right extension point, and a declaration-only checker would not close the runtime blocker.
