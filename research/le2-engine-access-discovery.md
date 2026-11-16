<!-- SPDX-License-Identifier: MIT -->
# LE2 engine access: bounded P1 discovery

**Since 2026-11-14 (M1 of [the continuation plan](../docs/le2-continuation-plan.md)) the layout is read from ME2's code:
[M1: what ME2's code showed](#m1-what-me2s-code-showed) at the end of this note.** The sections before it are the
SDK-only discovery of 2026-11-13; its candidate offsets turned out right.

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


## M1: what ME2's code showed

Date: 2026-11-14, lane A of [the continuation plan](../docs/le2-continuation-plan.md) (M1). Clean room as research/34
section 2: every offset below is read from the ME2 native that uses it, in our Ghidra project over ME2's decrypted
`.text`; LExSDKv2 is only the run-time reference the self-check compares with. ME2's exe: `MassEffect2.exe`
2.0.0.48602, PE timestamp `0x60AEEFA5`, image base `0x140000000`. All RVAs are of that exe.

### How to reproduce it (a weaker model can follow this)

1. The dump: `%LOCALAPPDATA%\MEVR\dump\MassEffect-60AEEFA5.text.bin` (the host writes it at attach, `engine/CodeDump`;
   `MassEffect-60AEEFA5.txt` says `text_rva=0x1000`, `text_size=0xFA6271`, one start another ASI patched: `0xAEF10`).
2. In a worktree: the submodules are junctions to the main checkout's (`New-Item -ItemType Junction` for
   `external\LExSDKv2`, `OpenXR-SDK`, `RmlUi`, `freetype`; `git submodule update` refuses the local `file` transport), and
   `local-tools\splice_text.py` is copied from the main checkout (it is never published).
3. `powershell -File tools\ghidra\setup.ps1 -Game LE2 [-Force]` (about 10 minutes: splice, import, analysis, seeding from
   `tools/ghidra/symbols-le2.csv`). Project `%LOCALAPPDATA%\MEVR\ghidra\LE2.gpr`, program `/MassEffect2.exe`. One Ghidra
   process per project at a time (the LE1 project is separate).
4. `tools\ghidra\decompile.ps1 -Game LE2 <rva> ...` prints the C. To find ME2's copy of an LE1 function, take the LE1
   function's first 16-40 bytes from `MassEffect-60AEEFA2.text.bin` (LE1's dump) and search ME2's dump for them: the
   engine's core is the same compiled code, so most core functions match on their first 16-24 bytes (execIsA, IsPendingKill,
   GetPathName, AppendString and execLetBool were found that way, then decompiled to confirm).
5. Patterns for `engine/Addresses.hpp`: `python -I tools/rva-patterns.py <ME2 dump> --exe <MassEffect2.exe>
   name=0xRVA:function|data|end ...` (prints a pattern unique in ME2's `.text`; takes a few minutes for a dozen).

### The layout (engine/GameAdapter.hpp `ReflectionLayout()` for `MEVR_GAME_LE2`)

| Field | ME2 | LE1 | Read in (ME2 RVA) |
|---|---|---|---|
| UObject index | +0x08 | same | `UObject::~UObject` 0x104F80 (clears `GObjects[index]`); `execIsA` 0xC7860 (`<uninitialized>` when -1) |
| UObject flags, PendingKill | +0x0C (uint64), bit 61 | same | `UObject::IsPendingKill` 0x9B080: `(*(u64*)(this+0xC) >> 0x3D) & 1` |
| Outer, Name | +0x40, +0x48 | same | `UObject::GetPathName` 0x10C3D0 |
| Class | +0x50 | same | `execIsA` 0xC7860; ProcessEvent reads a property's class's ClassFlags at class +0x134 |
| UField SuperField / Next | +0x60 / +0x68 | same | `execIsA` walks the class chain at +0x60; ProcessEvent walks parameters at +0x68 |
| UStruct Children / PropertySize | +0x70 / +0x78 | same | `UObject::ProcessEvent` 0xD8D50: first field, the frame it allocas |
| UStruct Script (TArray data) | +0x7C | same | ProcessEvent's frame Code (`*(u8**)(fn+0x7C)`): plugin/CrashBreadcrumbs' `structScript` holds for ME2 |
| UProperty ArrayDim / ElementSize / PropertyFlags / Offset | +0x70 / +0x74 / +0x78 (u64) / +0x94 | same | ProcessEvent's two parameter loops (`& 0x480 == 0x80` Parm not Return; bit 8 OutParm; 0x10 OptionalParm) |
| Typed property field | +0xD0 | same | `UObject::execLetBool` 0xCDA20: BoolProperty's mask and ArrayProperty's inner |
| UFunction FunctionFlags | **+0xD0** | +0xD8 | ProcessEvent: `& 0x402`, `& 0x400` (native), `& 0x404000` |
| UFunction iNative | **+0xD4** (u16) | +0xDC | ProcessEvent returns unless `*(short*)(fn+0xD4) == 0` |
| UFunction ParmsSize / ReturnValueOffset | **+0xE2 / +0xE4** (u16) | +0xEA / +0xEC | ProcessEvent: copies ParmsSize bytes in, zeroes the rest; result at block + ReturnValueOffset |
| UFunction Func | **+0xF0** | +0xF8 | ProcessEvent calls it |
| ProcessEvent's vtable slot | **+0x240** | +0x250 | ProcessEvent 0xD8D50 is at +0x240 of 20 vtables (UObject's 0xFB6BF0 + 0x240, ...) |
| Name index split | pool `(i >> 29) & 7`, offset `i & 0x1FFFFFFF` | same | `FName::AppendString` 0x75820, `FName ==` 0x744D0 |
| Name entry text | +0xC, UTF-16 if header bit 30 (`0x40000000`) | same | entry compare 0x74620 (`puVar2 + 3`), entry text 0x75780 |
| FFrame Object / Code | +0x1C / +0x24 | same | `execLetBool` and `execIsA` (engine/GameAdapter's `kFrame` for LE1 and LE2) |

So ME2's UStruct is 8 bytes shorter than LE1's (UFunction's own fields start 8 bytes earlier); everything before it is
LE1's. The SDK-only candidates in "Candidate layout facts" above were all right.

### Globals and the allocator (engine/Addresses.hpp `le2`, engine/GameAdapter.hpp)

| Global | RVA | How our code finds it | Pattern in `engine/Addresses.hpp` (unique in ME2's `.text`) |
|---|---|---|---|
| GObjects | 0x173CC48 | LE1's `~UObject` pattern (`ReflectionGlobalsForGame`) matches once, at 0x104FBF | data site 0x9B24B `C9 74 5B EB F0 4C 8B 05 ?? ?? ?? ?? 4B 8B 04 06` |
| Name pools | 0x1668A10 | LE1's AppendString pattern matches 33 times, all naming 0x1668A10 | data site 0x7450B `74 2B 48 8B D8 48 8D 15 ?? ?? ?? ?? 48 C1 F8 1D` |
| GMalloc | 0x1682378 | the loads in appMalloc (+0x14) and appFree (+0xF) | data site 0x96B03 `48 85 DB 74 2A 48 8B 0D ?? ?? ?? ?? 48 85 C9 75` (LE1's pattern) |
| GNatives | 0x16A99B0 | the research diagnostics (`NativesTableRva`) | data site 0xAEF81 `74 13 0F B7 C0 48 8D 0D ?? ?? ?? ?? 48 8B 04 C1` (LE1's pattern) |
| appMalloc | 0x9C900 | LE1's whole-function pattern, byte for byte; GMalloc vtable +0x8 (size, alignment, flags) | the same bytes |
| appFree | 0x9C8C0 | LE1's pattern with its last instruction `48 FF 60 18`: **GMalloc's Free is vtable +0x18 in ME2** (LE1 +0x20) | the same bytes |

The four data RVAs equal LExSDKv2's LE2 `BUILTIN_*_RVA` (the self-check's `rva` rows compare them at run time). The hook
functions got patterns too (ProcessEvent 0xD8D50, CallFunction 0xD6770, UGameEngine::Tick 0x546980,
ULocalPlayer::CalcSceneView 0x6CEB70, FViewportClient::Draw 0x6D0A00); `tests/ExeTests.cpp` `TestAgainstLe2Dump` checks
every LE2 entry against the dump when it is on the machine.

### What turned on, and what didn't (ue3/Runtime, ue3/Handles)

- `ue3::runtime::Up()` (new): the layer is found and checked. `Ready()` is now `Up()` and
  `engine::adapter::kModulesOnLayer` (LE1 true, LE2 false). **Every ME1 module keeps asking `Ready()`, so on ME2 it stands
  down exactly as before** ("ME2 continues ME1" ports them one by one: lane C's M3 and M6, not this milestone). The log says
  `reflect: engine access only on this game: the ME1 modules not yet ported to it stand down`.
- Handles (`ClassRef`, `FunctionRef`, `FieldRef`) take a reach: by default they resolve only where `Ready()`; declared
  `ue3::kEveryGame` they resolve wherever `Up()`. A struct member's or a parameter's handle reaches as far as its outer
  field or its function. **To port a module to ME2:** make its `Ready()` checks `Up()` (or drop an `#if
  defined(MEVR_GAME_LE1)`), declare its handles `kEveryGame`, add a ReflectCheck row per class, field, function and
  parameter it resolves (the LE2 tables in `engine/ReflectCheck.cpp`), and check the row agrees in the game.
- Moved off engine/Legacy onto the layer for LE2 (each with LExSDKv2 as the fallback while the layer is off, and Legacy
  still LE3's): the script-package gate (`Mod.cpp ScriptPackagesLoaded`), `engine::World()`/`ObjectCount()`, the console
  request (`PlayerController.ConsoleCommand` through `ue3::runtime::Call`, an `OwnedString` in, the returned FString freed
  with `FreeString`), head tracking's reads (UpdateCamera, GetPlayerViewPoint, GetFOVAngle and their parameters, the
  controller, its game mode, the camera's owner and FOV), hand aim's aim query (`GetAdjustedAimFor`'s ReturnValue, the
  pawn's location) and the reflection fix (our renderer only). game/Player's handles behind them are `kEveryGame`:
  `Engine.PlayerController`, `SFXGame.BioPlayerController`, `Controller.Pawn`, `BioPlayerController.GameModeManager2`,
  `SFXGameModeManager.CurrentMode`, `Camera.PCOwner`, `Camera.CameraCache` (and its POV), `Actor.Location`.
- engine/NativesDump works on ME2 (`[Debug] dumpNatives = 1` or `MEVR_DUMP_NATIVES=1`): its CSV seeds the LE2 Ghidra
  project with every native's name (`seed.ps1 -Game LE2`).
- The host's crash breadcrumbs (`plugin/CrashBreadcrumbs`) read `ReflectionLayout()` too: on ME2 they now name objects and
  script frames.

### The self-check on ME2 (engine/ReflectCheck, `reflect: N of M checks agree`)

LE1's generic rows run unchanged (layout 22, globals 8, `rva` 7 and one `exe` row per LE2 table entry, types 5, samples:
names, full names, IsA and IsAlive, engine strings); LE2's own tables replace LE1's: 30 classes (Core's reflection classes,
the engine's actor/controller/camera/engine/world classes, BioPlayerController, SFXGameModeManager, the game modes, BioPawn,
SFXPawn_Player, BioWorldInfo, SFXWeapon), 18 fields (everything above plus `Pawn.Weapon` and SFXWeapon's
`AmmoUsedCount`, `CurrentSpareAmmo`, `MagSize`, `bEarlyReload`: the P1a observer's first reads for lane C), 8 paths
(CameraCache.POV and its members, `SFXWeapon.MagSize.Value`, GamePlayers' element), 8 functions and their 18 parameter
rows (ConsoleCommand, GetPlayerViewPoint, GetFOVAngle, GetAdjustedAimFor, SetFrameRate, SetRotation, GetMagazineSize).
The skeleton rows stay LE1's (ME2's bones are M3's). Output: the log line and `%LOCALAPPDATA%\MEVR\reflect-check.json`.

### In the game (fake headset, 2026-11-14 19:30, ME2's title screen)

`tools/le2-halcyon-run` (`prep.ps1 -Holder me2-lane-a`, `launch.ps1`, `restore.ps1`; session
`%LOCALAPPDATA%\MEVR\sessions\20261114-192935-m1-reflect`, the run's `LE2MEVR.log` moved aside there):

- `exe (mod): 12 of 12 addresses at their RVAs (10 by their bytes, 1 hooked by another module, 1 by structure only)`.
- `reflect: ready: 109336 objects (GObjects by rva, names by rva), ProcessEvent at RVA 0xD8D50`, the allocator by its RVAs,
  then **`reflect: 140 of 140 checks agree (48 ms)`**: layout 22, globals 8, rva 7, exe 12, types 5, classes 30, fields 18,
  paths 8, functions 8, params 11, parms 7, samples 4. The `GNatives` row reads entry 0x14 as 0xCDA20 (execLetBool, as
  found in Ghidra); the local player controller is `SFXPlayerController entrymenu.TheWorld.PersistentLevel.
  SFXPlayerController_0`, the engine `SFXEngine Transient.SFXEngine_0`, the world `World entrymenu.TheWorld` (WorldInfo's
  outer World, equal to GWorld). A hot reload of the mod (a rebuild) ran the check again: 140 of 140.
- Head tracking started through our layer (`head tracking ready (...UpdateCamera, ...GetPlayerViewPoint, ...GetFOVAngle)`);
  the ME1 modules stood down as before M1 (`squad context: our reflection layer is off`, the world UI's and conversation
  module's handles not found).
- The console request through `ue3::runtime::Call`: `scale set FoliageDrawRadiusMultiplier 0.5` reached the engine (the game
  rewrote `BIOEngine.ini` at the same millisecond, as it does after every `scale set`). **ME2's engine commands return no
  text**: `getall`, `get`, `obj list`, `obj gc`, `help` and an unknown word all came back empty, as LE1's engine commands do
  (only our own `mevr ...` lines print on LE1). ME2's native (`execConsoleCommand` 0x4B1470) copies whatever the actor's
  virtual +0x2C0 returns into the FString ReturnValue; these commands write to the log, not to that string. The string
  path itself (an `OwnedString` in, the returned FString freed through `FreeString`) is the self-check's `engine strings`
  row, which agrees: one heap with LExSDKv2's GMalloc both ways.
- `[Debug] dumpNatives = 1` wrote `%LOCALAPPDATA%\MEVR\dump\natives-LE2-60AEEFA5.csv` (4,453 natives, local research data);
  `seed.ps1 -Game LE2` named 4,568 functions in the LE2 Ghidra project with it. Lanes B and C can ask for any native by
  name now (`decompile.ps1 -Game LE2 -Search <name>`).

### A save loaded by script (M1.6)

The owner (2026-11-14): no computer use for driving the game; every lane must reach gameplay by script. ME2's routes,
read from SFXGame.pcc's script (`local-tools/le2-inspection-root/conversation-scan.exe <CookedPCConsole> <new file> --game
LE2 --script <Class.Function> --only SFXGame`, the output kept out of git):

| Route | What it does | Usable? |
|---|---|---|
| The console's `QuickLoad` (`BioPlayerController.QuickLoad`) | `ResumeGame()` → `SFXEngine.ResumeGame`: `QueueSaveGameCommand(1, GetCurrentSaveDescriptor(), FastResumeGameCallback)`, only while `TravelURL` is empty | loads whatever save the game would resume (the newest), not a named one; LE1's quicksave route would mean writing a save |
| The cheat `LoadGame <name>` (`BioCheatManager.LoadGame`) | enumerates the careers (`QueueSaveGameCommand(7)`) and loads the save whose `SFXSaveGame.DebugName` matches | needs the cheat manager and debug names; not tried |
| The Load screen (`SFXSFHandler_Load.LoadGame(SaveIdx, GuiMan)`, tools/qa-le2's `load-fixture`) | `Callback_ConfirmLoadGame` → `Engine.QueueSaveGameCommand(1, SaveList[i].SaveDescriptor, LoadGameCallback)` | works (the validation session's scripted load), but needs the main menu's Load screen opened and its list read first |
| **`SFXEngine.QueueSaveGameCommand(SaveGame_Load = 1, descriptor, None)` directly** | what the two above end in; `ESFXSaveGameAction`: 0 nothing, 1 load, 2 save, 3 delete, 4-5 careers, 6-7 enumerate, 8-10 space and prepared saves | **the route taken**: no menu, no file written, any save by its descriptor |

The descriptor (`SFXEngine.SFXSaveDescriptor`, 0x28 bytes): `Career` (FString, the career folder's name), `SaveName`
(FString, the file's name without `.pcsav`), `Index` (int: a manual save's number, -1 for the others), `Type` (byte,
`ESFXSaveGameType`: 0 manual, 1 quick, 2 auto, 3 chapter, 4 legacy), as the validation session saw the Load screen list
them (`Save_0099` index 99 type 0, `AutoSave` and `ChapterSave` index -1 types 2 and 3; research/le2-validation-evidence.md).
The callback is an optional delegate left None (zeroed): the Load screen's `LoadGameCallback` only re-enables its input.

Built: the mod's console lines on ME2 (`game/ConsoleRequest`, `RunOursLe2`): `mevr load <career> <save name> <index>
<type>` (the parameter block built from the UFunction, the descriptor's members through `kEveryGame` handles on the
parameter, both strings `OwnedString`s: the queued command copies the descriptor), `mevr savedesc`
(`GetCurrentSaveDescriptor`: the save QuickLoad would load) and `mevr state` (the world, the pawn and its place, the game
mode). ReflectCheck rows: `SFXGame.SFXEngine`, `SFXGame.SFXSFHandler_Load` and its `SaveList[].SaveDescriptor` members, both
functions and their parameters. `tools/le2-halcyon-run/load.ps1` derives the descriptor from the file's path, checks the
file's hash before and after, sends `mevr load` and waits on `mevr state` for gameplay.

**In the game (fake headset, 2026-11-14 20:12, session `%LOCALAPPDATA%\MEVR\sessions\20261114-201145-m1-load`):** at the
title `mevr savedesc` gave `career 'VR_31_Soldier_141126_336072f' save '' index 4 type 0` (the resume save's descriptor
carries no SaveName; the Load screen's list does) and `mevr state` `world World entrymenu.TheWorld; pawn None; game mode
7`. `load.ps1` with no arguments queued `career 'VR_31_Soldier_141126_336072f' save 'Save_0004' index 4 type 0` at
20:12:43.4; at 20:12:45.99 `mevr state` read `world World BioP_ProFre.TheWorld; pawn SFXPawn_PlayerSoldier
BioP_ProFre.TheWorld.PersistentLevel.SFXPawn_PlayerSoldier_0 at (-1748, 21300, -11909); game mode 0` (Freedom's Progress,
on foot), the same 15 s later; the save file's hash unchanged. The self-check with the new rows: 155 of 155. `restore.ps1`:
`profile: 0 restored, 0 moved aside` (the run wrote no save at all), `verification: 0 differences`. The test loop's eye
snapshots in ME2 had no world layer yet (both projection layers without a file: M2's work), so the gameplay evidence is
`mevr state`.

### Addresses for the next lanes (candidates, [verify] before a hook)

Found the same way (LE1's first bytes searched in ME2's dump, or a native that calls it), not yet hooked or checked in the
game; `tools/ghidra/symbols-le2.csv` has them with a `_guess` suffix:

| LE1 function (RVA) | ME2 candidate | Evidence |
|---|---|---|
| `USkeletalMeshComponent::UpdateSkelPose` (0x592440) | **0x79B8A0** | `execForceSkelUpdate` (0x77D750, from the natives dump) calls it as `(this, 0, 1)`, as LE1's does; the same prologue (`48 8B C4 44 89 40 18 55 56 57 41 54 41 55 41 56 41 57`), a smaller frame |
| `USkeletalMeshComponent::UpdateBounds` (0x58EEC0) | 0x798350 | LE1's first 24 bytes, one match |
| `FName::FName(const TCHAR*, EFindName)` (0xD1150) | 0x72BD0 | LE1's first 24 bytes, one match; ProcessEvent calls it with `L"<uninitialized>", 1` |
| `AActor::MoveWithInterpMoveTrack` (0x4B75F0) | 0x6B9C50 | LE1's first 32 bytes, one match |
| `FISACTSoundSource::Update` (0x7BA90) | not found | LE1's first 16 bytes match four functions |

### Left after M1

- The LE2 diagnostic observers (`engine/Le2DiagnosticObservation.cpp` and its `.inc` files, the `MEVR_LE2_DIAGNOSTIC`
  build only, about 4,000 lines, the ME2 validation session's) still read through LExSDKv2. Each read they make needs a
  `kEveryGame` handle and a ReflectCheck row (the weapon's are in the table already).
- **Done 2026-11-16 (T04, research/34 section 6.4):** engine/Legacy is no longer LE3's path nor LE2's fallback. It
  serves the LE2 diagnostic host only, and a monolithic LE2 build (M7.1) links no LExSDKv2; the observers keep it in
  their own build, which is never packaged.
