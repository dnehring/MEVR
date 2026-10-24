# 34. Our own UE3 reflection layer (replacing LExSDKv2)

Started 2026-10-23 (the user approved the plan).
- **Stage 1 is verified in the game:** two sessions on 2026-10-23 logged `reflect: ready: 140232 objects
  (GObjects by rva, names by rva), ProcessEvent at RVA 0x12DD20` and `reflect: 405 of 405 checks agree`, with no
  `notReflected` rows: SpaceBases, RefSkeleton, LocalToWorld and Attachments are all in the reflection, at the
  research/14 offsets.
- **Stage 2 (every `src/game` module, section 6.2) is built and unit-tested, not yet run in the game.** Its new
  pieces (the allocator, engine strings, the five new self-check rows) are **[verify]** until the next session's
  `reflect-check.json` (notes/next-vr-session.md, step 32).

## 1. Why

- **Licensing.** We want to ship a standalone GPL-3.0 installer on GitHub. LExSDKv2
  (`external/LExSDKv2`) has no licence. Its README says it is mostly data extracted from the games,
  and its author would "prefer to avoid testing that theory". So we can't count on permission to
  ship it.
- **Patches.** The mod fails closed on any exe except 2.0.0.48602, because LExSDKv2's class layouts
  and RVAs are compiled in.
- **The design.** A small, clean-room GPL layer (`src/ue3/`) that finds everything at run time
  through the engine's own reflection:
  - GObjects, the name pools and the UField chains;
  - only the core header offsets are compiled in, from our own Ghidra reading;
  - a byte-pattern fallback for the two globals it needs.
  - No class layouts of game classes ship in our code: `PropertyOffset(BioPawn, "CoverType")` asks
    the engine.

## 2. What the engine showed us (clean room)

All of this is from Ghidra over the decrypted LE1 exe (`tools/ghidra`, the GhidraMCP server),
2.0.0.48602. The functions are recorded in `tools/ghidra/symbols-le1.csv` (tag `research/34`). We
did not read LExSDKv2's headers to derive any of it. The run-time self-check (section 5) compares the
result with LExSDKv2's compiled offsets.

### 2.1 UObject, UField, UStruct, UProperty, UFunction (LE1)

| Field | Offset | Seen in |
|-------|--------|---------|
| vtable | +0x00 | `ProcessEvent` is slot **+0x250** of UObject's vtable (`0x140FCBE18`), `IsPendingKill` +0x88 |
| object index (its GObjects slot) | +0x08 (int32) | `UObject::~UObject` (`0x15C170`) clears `GObjects[index]`; `execIsA` prints `<uninitialized>` for -1 |
| object flags | +0x0C (uint64, 4-aligned) | `IsPendingKill` (`0x79F10`) tests bit 61 of this field, so PendingKill is bit 61 |
| state frame | +0x24 | `IsProbing` (`0x12D190`) |
| Outer | +0x40 | `GetPathName` (`0x1631C0`) |
| Name (8 bytes: index, number) | +0x48 | `GetPathName`; `execIsA` compares names as 8 bytes |
| Class | +0x50 | `execIsA` |
| UField: super struct / next field | +0x60 / +0x68 | `execIsA` walks the class chain at +0x60; `ProcessEvent` walks a function's fields at +0x68 (ME1's UField still holds SuperField) |
| UStruct: Children / PropertySize | +0x70 / +0x78 | `ProcessEvent`: the first field, and the frame it allocas |
| UProperty: ArrayDim / ElementSize / PropertyFlags / Offset | +0x70 / +0x74 / +0x78 (uint64) / +0x94 | `ProcessEvent`'s two parameter loops (copy-in by `Offset`, `ElementSize * ArrayDim` bytes) |
| typed property field | +0xD0 | `execLetBool` (`GNatives[0x14]`, `0x1252B0`): BoolProperty's mask is the uint32 at +0xD0; ArrayProperty's inner property is there too |
| UFunction: FunctionFlags / iNative / ParmsSize / ReturnValueOffset / Func | +0xD8 / +0xDC / +0xEA (uint16) / +0xEC (uint16) / +0xF8 | `ProcessEvent`: it tests the flags (native or defined), sizes its frame by PropertySize, copies ParmsSize bytes of the caller's block, and calls Func with the return value's place at ReturnValueOffset in that block |

Property flags, from `ProcessEvent`'s loop:
- `0x80` = Parm;
- `0x100` = OutParm (not copied back);
- `0x400` = ReturnParm;
- `0x10` = OptionalParm (it runs the default-value bytecode).

ProcessEvent itself, as the engine runs it:
- it copies `ParmsSize` bytes in, and the rest of the frame is zeroed;
- after the call, it copies the parameters (not out-parameters) back into the caller's block and
  destroys the locals;
- so a caller's block must be exactly the function's parameter layout, and the caller owns any
  strings or arrays in it.

### 2.2 Names (SFXName)

`FName::AppendString` (`0xD62F0`) and the entry reader (`0xD6250`) show the format:
- **Index:** a name's index picks pool `(index >> 29) & 7` of the 8 pool base pointers at
  `0x16A2090`. The entry is at `pool + (index & 0x1FFFFFFF)`. Index 0 has no entry and reads as
  "None".
- **Entry:** a uint32 header. Bit 30 means UTF-16, otherwise Latin-1. The length is in bits 20+ and
  a hash in the low bits (`SFXName::Init`, `0xDD800`). The text follows at **+0xC**, zero-terminated.
- **Number:** a non-zero number prints as `_` and number - 1 (`Foo_2` is `{Foo, 3}`).
- **Comparison:** names compare as the 8 raw bytes (`execIsA`). Text lookups are ASCII
  case-insensitive, as UE3's.

### 2.3 Globals

| Global | LE1 RVA | Fallback pattern (the instruction that references it) |
|--------|---------|------------------------------------------------------|
| GObjects (`TArray<UObject*>`: data, count, max) | `0x1770670` | `~UObject`'s load of GObjects, where it clears the object's slot (the bytes are in `engine/GameAdapter.hpp`) |
| Name pools (8 pointers) | `0x16A2090` | `AppendString`'s load of the pool array, followed by the index split (the bytes are in `engine/GameAdapter.hpp`) |
| ProcessEvent | `0x12DD20` | Not needed: the layer calls UObject's vtable slot +0x250, as the engine does (hooks see it). The RVA is only a cross-check. |
| GEngine | (not used) | The layer finds the live `Engine.GameEngine` instance by class (not its `Default__` object) |
| GMalloc / appMalloc / appFree (ue3/Memory, stage 2) | `0x16B6700` / `0xF2330` / `0xF22F0` | `appMalloc(size, align, flags)` = GMalloc vtable +0x8; `appFree(ptr, flags)` = +0x20. Each function's own first bytes are its pattern (both unique in .text, checked over the dump); each loads GMalloc RIP-relatively (appMalloc at +0x14, appFree at +0xF), and the two loads must name the same GMalloc. `0x148240` = `appMalloc(size, 16, 0)`, so our allocations ask for 16-byte alignment, flags 0 |

## 3. The layer (`src/ue3/`)

| File | What |
|------|------|
| `Types.hpp` | Pointer tags (`Object`, `Field`, `Struct`, `Class`, `Function`, `Property`; never instantiated), `Name`, `TArray<T>`, `FString`, `Vector`, `Vector2D`, `Rotator` |
| `Layout.hpp` | The offsets above as a struct (one per game, from `engine/GameAdapter.hpp`), property and function flags |
| `Names.*` | Name pool decoding and case-insensitive comparison without allocating |
| `Reflect.*` | `Reflection` over a `Universe` (layout, GObjects, pools): pure over memory, so the tests build synthetic engines |
| `Params.*` | A parameter block built from the UFunction's own parameter properties |
| `Pattern.*` | Byte patterns with wildcards and RIP-relative targets (pure) |
| `Runtime.*` | The game's side: finds and checks GObjects and the pools, calls ProcessEvent, finds the engine and the local player controller; access violations in the checks are caught |
| `Handles.*` | Cached handles: `ClassRef`, `FunctionRef`, `FieldRef` (resolved on first use, a miss logged once) |
| `Memory.*` | The engine's allocator (appMalloc/appFree, installed by `runtime::Init`), `OwnedString` (an FString in the engine's memory, freed when it goes), `FreeString` (frees one the engine returned), UTF-8 conversion. Pure: the tests install a fake allocator (stage 2) |

The API:
- **Finding things:**
  - `Reflection::FindClass("SFXGame.BioPawn")`: by package and name. If the class isn't in that
    package but its name is found elsewhere, it is used, with a log note.
  - `IsA(object, cls)`;
  - `FindFunction(cls, "SwitchToWeapon")` (the class or its supers); `FindFunction("Engine.Actor.SetRotation")`
    (exactly that class); `FindFunctionsNamed("PlayerTick")`;
  - `PropertyOf(owner, "bIsZoomed")` returns offset, element size, array dim, flags, kind, bool
    mask, and the typed pointer (struct, class, inner). It is cached per (owner, name).
  - `StructOf(info)` gives a struct property's script struct, so struct members resolve too
    (`FieldRef{location, "X"}`).
- **Reading fields:** `FieldRef::In<T>(object)` returns `T*` (null if unresolved or the size
  isn't `sizeof(T)`); `Read<T>`; `ReadBool` / `WriteBool` by the mask.
- **Calling functions:** `Params p(reflection, fn); p.Set("NewRotation", r); p.SetBool("bPause",
  true); runtime::Call(obj, fn, p); p.ReturnBool()`. Sizes are checked against the property;
  there are no generated `*_Parms` structs.
- **Helpers:** `runtime::LocalPlayerController()` (GameEngine.GamePlayers[0].Actor, offsets cached),
  `runtime::IsAlive`, `Reflection::FullName` ("Function Engine.Actor.SetRotation", '.'-joined like
  the old logs); `runtime::FullName(object)` and `runtime::NameText(name or raw key)` ("None" while the
  layer is off).
- **Added in stage 2:**
  - **Parameters inside a hook's block:** `FieldRef{gGetFovAngleFn, "ReturnValue"}` resolves a parameter of
    a `FunctionRef`'s function (a local is refused), so `In<float>(parms)` reads or rewrites the engine's
    own block in a ProcessEvent post-handler. Unaligned slots (LaunchGrenadeAtPoint's ReturnValue at 0xC)
    are copied through `Address(parms)` with memcpy.
  - **Static arrays:** `At<T>(object, i)` (BioEquipment.m_QuickSlotArray[4]: element size and index
    checked).
  - **Arrays of structs:** `ElementsOf(object)` gives data, count and the element size from the array's
    inner property; `FieldRef{lists, "ControlHead"}` resolves a member of the element struct
    (AnimTree.SkelControlLists).
  - **Struct values in place:** `Address(object)` for a struct field, so its members' handles read it
    (Camera.CameraCache.POV.FOV, SkeletalMesh.Bounds.Origin).
  - **Bool return values:** `Params::ReturnBool()` tests the whole 32-bit word (a native writes a UBOOL,
    script tests it for non-zero), as the generated `*_Parms` blocks did; input bools still go in by mask.
  - **Engine strings:** `ue3::memory` (table above). ConsoleCommand's Command is an `OwnedString`, its
    ReturnValue is read with `Params::Return<FString>` and freed with `FreeString`.
  - `game/Player.*`: the handles most modules share (controller, pawn, BioPawn, game mode, camera and its
    POV, location, rotation, meshes).
- **Game differences** go in `engine/GameAdapter.hpp`, which is SDK-free now:
  - `ReflectionLayout()`, `ReflectionGlobalsForGame()`;
  - `kGameModeManager` ("GameModeManager" on LE1, "GameModeManager2" on LE2/LE3);
  - `MEVR_GAME_NAME_W`;
  - `MEVR_GAME_LE1/2/3`, from CMake.
  - The LExSDKv2-typed accessors moved to `engine/GameAdapterSdk.hpp`.
  - Case-insensitive lookups make LE3's lower-case spellings (`location`, `out_rotation`) a
    non-issue.
- **Hooks:** `engine::process_event::AddPostHandler` and `call_function::AddPostHandler` take
  `ue3::Function*` with handlers on `ue3::Object*` too.

LE2 and LE3: `ReflectionLayout()` is empty for them, so the layer reports itself off there. Their
layouts need their own Ghidra pass (stage 5); until then the all-game modules use `engine/Legacy` there
(section 6.2).

Unit tests (`tests/ReflectTests.cpp`, run from `tests/Main.cpp`) build a synthetic engine with its
own layout. They cover:
- names: numbered, wide, None, a missing pool;
- class bootstrap, packages and the wrong-package fallback, late-loaded classes;
- IsA, IsAlive, inherited and exact function lookups;
- property kinds, bool masks, struct members, the cache;
- parameter blocks: sizes, bools, return values, a ParmsSize shorter than the block;
- byte patterns and RIP targets.

## 4. Inventory: what the mod uses from LExSDKv2 today

38 source files include `<LESDK/Headers.hpp>` or `<LESDK/Init.hpp>` (before this stage). A few
headers forward-declare SDK types: `game/BodyFollow.hpp`, `game/Weapon.hpp`, `engine/Objects.hpp`,
`engine/Skeleton.hpp`, `engine/BonePose.hpp`, `mod/ModApi.hpp`.

Totals (LE1, deduplicated across files):
- **Classes:** about 60, through `StaticClass`/`IsA`/casts, or as the type a field is read through.
- **Object fields:** about 110. 10 of them are bitfield bools, 7 TArrays, 1 fixed array, 3 FStrings.
- **Struct members:** about 30, inside FRotator, FVector, FVector2D, FTPOV, FTCameraCache,
  FBoxSphereBounds, FSkelControlListHead, FAttachment, FMemberData, FMatrix/FPlane.
- **UFunctions looked up:** 57 by full name, plus every `PlayerTick`.
- **ProcessEvent parameter blocks:** 44 generated `*_Parms` structs, 64 members, and 9 hand-built
  blocks.
- **Hooks:** 13 named hooks, plus the XInput ones.
  - Host: ProcessEvent, GameEngine::Tick, ExitProcess, Present, and the 4 RenderLevers hooks.
  - Mod: ProcessEvent (fallback), CallFunction, CalcSceneView, UpdateSkelPose, TryAcquireCover.
  - All go through `core/Hook` → LEBinkProxy's SPI (`ISharedProxyInterface`, `SPI.h` from
    LExSDKv2's `External/`).
- **RVAs:**
  - 10 of LExSDKv2's `BUILTIN_*_RVA`: GMalloc, GObjects, name pools, SFXName init, GEngine, GWorld,
    ProcessEvent, CallFunction, GNatives, GameEngineTick.
  - 29 raw LE1 RVAs of our own: cover 2, scene view 4, bone pose 1, render levers 22.
  - LE2 and LE3 have 3 each (scene view).
- **Helpers:**
  - `UObject::GObjObjects`, `GEngine`, `GWorld`, `GMalloc`;
  - `SFXName(wchar_t*)`: it *inserts* missing names into the pool;
  - `SFXName::Instanced`, `GetFullName`;
  - `FString` (its constructor and destructor allocate through GMalloc), `TArray` destructors;
  - `LESDK::Initializer` (`Resolve`, `ResolveTyped`, `ModuleBase`, `OwnerName`, `ProxyInstance`),
    `Address::FromOffset`;
  - the macros `SDK_TARGET_LE1/2/3` and `SDK_TARGET_NAME_A/W`;
  - the SPI plugin macros in `plugin/Entry.cpp`.

By file. Names only; the offsets are the self-check's business:

| File | Uses | Stage |
|------|------|-------|
| `game/ViewLevel.cpp` | Actor Rotation/Physics/Modules, Controller.Pawn, Pawn.InvManager, BioPawn CoverType/CoverAction, SFXInventoryManager WeaponRecoilOffset/TotalRecoil, SFXModule_AimAssist.LastZoomSnapTarget, PlayerController PlayerCamera/PlayerInput, SFXCameraNativeBase.CurrentCameraMode, SFXCameraMode_Interpolate.To, PlayerInput RawJoyLook*, Object.Outer, the class name; PlayerTick (all), SetWeaponRecoil, ViewFriction, ViewAdhesion | **1 (done)** |
| `game/Comfort.cpp` | as above plus SFXCameraMode.Input, SFXCameraInput (4 fields, CameraSensitivity.X/Y), Pawn.Mesh, Actor.Location; Actor.SetRotation(NewRotation, ReturnValue) | **1 (done)** |
| `game/Posture.cpp` | BioPlayerController GameModeManager/bStorming, SFXGameModeManager.GameModes, BioPawn CoverType/CoverAction, Pawn bIsCrouched/bWantsToCrouch, Actor.Location; ShouldCrouch, TryExitCover, IsInCover, LeaveCover | **1 (done)** |
| `game/MenuPause.cpp` | Actor.WorldInfo; WorldInfo.PauseGame(bPause) | **1 (done)** |
| `game/HandCalibration.cpp`, `game/BodyCalibration.cpp` | only `SDK_TARGET_NAME_W` (now `MEVR_GAME_NAME_W`) | **1 (done)** |
| `game/ConsoleRequest.cpp` | ConsoleCommand(Command, bWriteToLog, ReturnValue): two FStrings (one ours, one the engine's to free) | **2 (done)** |
| `game/BodyFollow.cpp` | Actor.Rotation, PlayerController.PlayerCamera, Camera.CameraCache.POV.Rotation (all games) | **2 (done)** |
| `game/ViewAnchor.cpp` | Camera.CameraCache.POV Location/Rotation (all games) | **2 (done)** |
| `game/HeadTracking.cpp` | Camera PCOwner/CameraCache.POV.FOV, BioPlayerController.GameModeManager(2), SFXGameModeManager.CurrentMode; UpdateCamera, GetPlayerViewPoint (out_Location, out_Rotation), GetFOVAngle (all games) | **2 (done)** |
| `game/HandAim.cpp` | Controller.Pawn, Actor.Location; GetAdjustedAimFor, GetAdjustedAim, GetWeaponStartTraceLocation (2) | **2 (done)** |
| `game/Sights.cpp` | Controller.Pawn; Actor.Trace, GetPlayerAimError | **2 (done)** |
| `game/CameraProbe.cpp`, `game/SkeletonProbe.cpp` | camera modes (m_pov, Offset, HookOffset, LastHookPos, HookName, bFirstPerson), skeletal mesh, anim tree, skel controls (7 SkelControlLimb fields), BioSFHandler_PCHUD/BioSFPanel; GObjects walk | **2 (done)** |
| `game/Reflections.cpp` | ObjectFlags, Class, SceneCaptureComponent.FrameRate; SetFrameRate; GObjects walk (all games; off on LE1 with Halcyon) | **2 (done)** |
| `game/Holsters.cpp` | BioPawn.m_oBehavior, BioPawnBehavior.m_oEquipment, BioEquipment m_QuickSlotArray/m_eQuickSlotCurrent, GameModeManager/GameModes; SwitchToWeapon, TryHolster, IsInCombatActionState, IsSwitchingWeapons, GetRangedWeaponType | **2 (done)** |
| `game/Throwables.cpp` | grenades (BioGrenade, 3 projectile classes, Frisbee targets), WorldInfo PawnList/WorldGravityZ, Pawn.NextPawn, behaviours; 18 functions, 3 post-handlers that rewrite ReturnValue | **2 (done)** |
| `game/SquadWeapons.cpp` | squad (BioBaseSquad, FMemberData), behaviours and names (FString), HUD handler (6 fields), GUI manager, BioSFPanel.bUsingGamepad; 13 functions | **2 (done)** |
| `game/Weapon.cpp` | Pawn Weapon/Mesh, weapon meshes, m_MuzzleSocketName, bIsZoomed, SkeletalMesh.Bounds, UFunction.Func; GetMuzzleLoc, GetSocketWorldLocationAndRotation, GetRangedWeaponType, GetPlayerAimError, TickFire, GetWeaponStartTraceLocation; GWorld | **2 (done)** |
| `game/ArmIk.cpp` | Pawn Mesh/Rotation, SkeletalMeshComponent SkeletalMesh/Animations, AnimTree.SkelControlLists, SkelControlBase (5 fields); BioPlayerController GameModes (all games, unguarded) | **2 (done)** |
| `game/GamepadBridge.cpp` | only `LESDK::Initializer&` for its XInput hooks (its include is gone since stage 2; the type is forward-declared, an opaque hook context) | 4 |
| `engine/Objects.*` | FindFunction(s), FullName, ToString(SFXName), LocalPlayerController (GEngine), IsAlive | 3 (becomes a thin wrapper over ue3, then goes) |
| `engine/Skeleton.*` | SkeletalMeshComponent SpaceBases/LocalToWorld/ParentAnimComponent/Attachments (FAttachment), SkeletalMesh RefSkeleton/Sockets, SkeletalMeshSocket (5); GetBoneNames, GetBoneLocation, MatchRefBone; GNatives; FMatrix/FPlane; TArray<SFXName> freed by `~TArray`. Since stage 2 its interface takes `ue3::Object` meshes and raw name keys (no SDK type in the header); the body still reads through LExSDKv2's types | 3 (all four "native" fields turned out reflected, at the research/14 offsets: one named accessor each, through the layer) |
| `engine/BonePose.*` | ActorComponent.Owner, GWorld, raw RVA hook; `Request::mesh` is a `ue3::Object` since stage 2 | 3 / 4 |
| `engine/Legacy.*` (new in stage 2) | LE2/LE3 only (not built for LE1): LocalPlayerController, FindFunction, PlayerController IsA, the game mode, the pawn's location, Camera PCOwner and FOV, GetPlayerViewPoint/GetFOVAngle/GetAdjustedAimFor blocks, ConsoleCommand, the reflection fix's GObjects pass | 5 (goes when LE2/LE3 have layouts) |
| `engine/CoverAcquire.*` | Actor.Acceleration, BioPawn.CoverTestAngle, 2 raw RVAs, hook | 3 / 4 |
| `engine/NativesDump.*` | UFunction FunctionFlags/Func/iNative, Field.SuperField, GNatives, Class/Function/Actor classes | 3 |
| `engine/ProcessEvent.*`, `engine/CallFunction.*` | `BUILTIN_PROCESSEVENT_RVA`, `BUILTIN_CALLFUNCTION_RVA`, `FFrame`, hooks | 4 |
| `engine/SceneView.*` | `Initializer::ModuleBase`, 4 raw RVAs per game, hook | 4 |
| `engine/Engine.*` | `ResolveGlobals` (the 6 globals); `World()` (GWorld as an identity, for game/weapon's caches; stage 2) | 3 (goes) |
| `engine/GameAdapterSdk.hpp` | the 4 SDK-typed per-game accessors | goes with the last user |
| `mod/Mod.cpp`, `mod/ModApi.hpp` | `Initializer` construction, GWorld, `GObjObjects->Count()` (heartbeat), `UObject`/`UFunction` forward declarations in the host interface; since stage 2 the script-package gate (`engine::FindFunction` for Engine.Camera.UpdateCamera, before `ue3::runtime::Init`) | 3 / 4 |
| `core/Hook.*` | `Initializer` (`OwnerName`, `ProxyInstance`) → SPI `InstallHook`/`UninstallHook` | 4 |
| `plugin/Entry.cpp`, `plugin/ProcessEventHook.*`, `plugin/PresentHook.*`, `plugin/ModLoader.*` | SPI plugin macros, `Initializer`, `BUILTIN_GAMEENGINETICK_RVA`, `BUILTIN_PROCESSEVENT_RVA`, `SDK_TARGET_NAME_*` | 4 (host: needs a redeploy) |
| `plugin/RenderLevers.*` | only `Initializer` and `ResolveTyped(FromOffset(rva))` for 22 raw RVAs; no UObject types | 4 (the performance session's file: coordinate) |

## 5. The run-time self-check

`engine/ReflectCheck.cpp` runs once per mod load, right after the script packages are in
(`Mod.cpp`, `TryInstallScriptHooks`), after `ue3::runtime::Init()`. Every row is resolved through
our layer and compared with LExSDKv2's compiled offsets or its own lookups. 405 rows in stage 1, 410 since
stage 2 (the five marked "stage 2" below):

| Kind | What is compared |
|------|------------------|
| `layout` (22) | Our `Layout` constants with LExSDKv2's offsets of UObject, UField, UStruct, UProperty, Bool/Struct/Object/ArrayProperty, UFunction |
| `global` (6) | GObjects and the name pools (address, and how we found them: rva or pattern), ProcessEvent (our vtable slot against `BUILTIN_PROCESSEVENT_RVA`), the GameEngine object against `*GEngine`, the local player controller against `engine::LocalPlayerController()`; **`allocator` (stage 2):** the GMalloc appMalloc and appFree load against LExSDKv2's `GMalloc`, with both functions' RVAs and how they were found |
| `type` (5) | Our `Rotator`/`Vector` against the engine's own struct sizes and LExSDKv2's; `Name`, `TArray`, `FString` sizes |
| `class` (61) | `FindClass("Pkg.Name")` against `T::StaticClass()` (same pointer) |
| `field` (111) | `PropertyOf` against `offsetof` and `sizeof`. Bitfield bools: the offset and mask LExSDKv2 compiles, found by setting the bit in a zeroed buffer. |
| `path` (28) | Struct members through struct properties (`Camera.CameraCache.POV.Location`), and array elements (`Attachments[].RelativeRotation`, element sizes) |
| `function` (58) | `FindFunction("Engine.Actor.SetRotation")` against `engine::FindFunction` (same pointer); the PlayerTick set |
| `param` (70) | Each parameter's offset and size against the `*_Parms` member, and the 5 hand-built blocks' assumption (a bool ReturnValue at 0); **stage 2:** `SFXGame.BioPawn.GetWeaponStartTraceLocation(ReturnValue)` (hand aim now reads BioPawn's override's block by its own parameter; the old code assumed Engine.Pawn's layout) |
| `parms` (45) | ParmsSize against `sizeof(*_Parms)`: equal, or less by under 8 bytes of padding; **stage 2:** `EnterWeaponWheel` and `ExitWeaponWheel` (a 64-byte zeroed block before, the function's own now) |
| `sample` (4) | Name text over ~20,000 objects, full names over ~2,000, IsA against 5 classes and IsAlive over ~5,000; **`engine strings` (stage 2):** an `OwnedString` built and read back, a block from appMalloc freed through LExSDKv2's `GMalloc->Free`, and LExSDKv2's own FString (allocated through GMalloc) freed by `FreeString`: the two paths are one heap. Only tried when the `allocator` row agrees |

**How to read it:**
- **The log line:**
  `reflect: N of M checks agree[; mismatches: kind what, ...][; K native fields not in the reflection] (T ms; ...)`.
  Then one `reflect:   kind what: ours ..., LExSDKv2 ...` line per disagreeing row.
- **The table:** `%LOCALAPPDATA%\MEVR\reflect-check.json`. It holds `status`, the counts and every
  row (`kind`, `what`, `agree`, `notReflected`, `ours`, `sdk`).
- **Expected:**
  - Every row agrees. (Stage 1 allowed up to 4 `notReflected` fields: `PrimitiveComponent.LocalToWorld`,
    `SkeletalMeshComponent.SpaceBases` and `Attachments`, `SkeletalMesh.RefSkeleton`. The game showed all four
    reflected, at the research/14 offsets.)
  - A `notReflected` row is not a mismatch: those reads keep their research/14 offsets.
- **If the layer is off,** the line before it says why:
  `reflect: our reflection layer is off: ...`, for example "GObjects not found (RVA ...: ...;
  pattern: ...)" or "the reflection layout doesn't check out: ...".
  - Then every row is missing, and since stage 2 every game module stands down with a log line
    ("our reflection layer is off"): head tracking registers no handlers, holsters, grenades, the squad wheel,
    the weapon and hand aim stay off. The mod itself still starts (the hooks, the menus, calibration).
- **If the allocator isn't found** (`reflect: the engine's allocator isn't found ...`), only the console request
  stands down (`console: the engine's allocator wasn't found`); the `allocator` and `engine strings` rows
  disagree.
- **A `class` mismatch whose `ours` names another package** means a path in our code has the wrong
  package. Fix the string. The fallback already used the right class.
- **A `field` or `param` mismatch:** a module is wrong: the handles use exactly these names. Every class,
  field, function and parameter `src/game` resolves through the layer is a row (stage 2 checked them against
  the list); new ones need a row.

## 6. Stage 1: what was migrated

These files no longer include LExSDKv2. Behaviour is meant to be identical:
- `game/ViewLevel.cpp`;
- `game/Comfort.cpp` (it gains one reason, "our reflection layer is off");
- `game/Posture.cpp`;
- `game/MenuPause.cpp`;
- `game/HandCalibration.cpp` and `game/BodyCalibration.cpp` (only the ini name).

How they work now:
- **Lookups:** their classes, fields and functions are `ClassRef` / `FieldRef` / `FunctionRef`
  handles, resolved once.
- **Calls:** ProcessEvent calls build their parameter blocks from the UFunction:
  - `SetRotation(NewRotation)`, `PauseGame(bPause)`, `ShouldCrouch(bCrouch)`;
  - the bool-returning `IsInCover`;
  - the void `TryExitCover` and `LeaveCover`.
  - These replace the 64-byte hand-built blocks.
- **Hook handlers:** they receive `ue3::Object*` / `ue3::Function*`.
- **One deliberate difference:** Posture and ViewLevel's `TryStartup` stand down with a warning when
  the layer is off. Before, they couldn't be off.

## 6.2 Stage 2: every `src/game` module (2026-10-23)

**No file in `src/game` includes LExSDKv2 any more** (`<LESDK/Headers.hpp>`, `<LESDK/Init.hpp>`,
`engine/GameAdapterSdk.hpp` or `engine/Objects.hpp`). Migrated: ConsoleRequest, BodyFollow, ViewAnchor,
Reflections, Sights, HandAim, CameraProbe, SkeletonProbe, Holsters, Weapon, ArmIk, Throwables, SquadWeapons and
HeadTracking. GamepadBridge's include went too: it only passes the `LESDK::Initializer&` it is handed on to
`core/Hook` (a forward-declared, opaque type until stage 4).

How:
- **Handles everywhere:** every class, field and function is a `ClassRef` / `FieldRef` / `FunctionRef`. The
  shared ones (the controller, pawn, game mode, camera POV, location, rotation, meshes) live in `game/Player`.
- **Calls:** every ProcessEvent call builds its block from the UFunction (`ue3::Params`); the generated
  `*_Parms` structs and the 64-byte hand-built blocks are gone. Values the old code preset keep their presets
  (GetRangedWeaponType's 0xFF, GetMuzzleLoc's FLT_MAX). Bool results are read as UBOOLs (section 3).
- **Hook handlers** take `ue3::Object*` / `ue3::Function*` and read or rewrite the engine's block through
  parameter handles (GetPlayerViewPoint, GetFOVAngle, GetAdjustedAimFor, GetAdjustedAim, both
  GetWeaponStartTraceLocation blocks, the three grenade launches).
- **Native-only fields:** none in `src/game`: SpaceBases, RefSkeleton, LocalToWorld and Attachments are read only
  by `engine/Skeleton` (one function each), whose interface now takes our object pointers. The self-check's
  `field` rows cross-check all four against LExSDKv2 (and the game showed them reflected).
- **A failed lookup stands the feature down:** a handle that doesn't resolve returns nullptr / nullopt (logged
  once by the handle), and every call site treats that as "not there" (the old code's null checks). With the
  layer off, each module's status says "our reflection layer is off".
- **LE2/LE3** have no layout yet (stage 5). What runs there of the all-game modules (head tracking, the console
  request, the reflection fix, hand aim's aim query) uses `engine/Legacy`: the same LExSDKv2 reads as before, in
  our types, behind an SDK-free header, built only for LE2/LE3. Everything else was LE1-only or stops before
  touching the game there (arm IK and the body need the bone-pose hook, which is LE1 only; the view anchor's host
  path is Halcyon's, LE1 only).

Deliberate differences:
- **The startup gate** (`mod/Mod.cpp`): `ue3::runtime::Init()` and the self-check now run as soon as
  `Engine.Camera.UpdateCamera` exists (before: once head tracking had found its three Engine functions; all
  three are in Engine.u, and the modules start in the same tick as before). Head tracking needs the layer now.
- **Layer off on LE1:** head tracking stands down (no handlers) and the mod starts anyway, so nothing that
  needs the game mode (on foot) runs. Before, the modules ran on LExSDKv2 regardless.
- **Bool results:** `Params::ReturnBool` tests the whole word, as the old `*_Parms` reads did. Stage 1's modules
  (Posture's IsInCover, Comfort's SetRotation log) read by the mask until now; they return to the old semantics.
- **LE2/LE3 diagnostics:** the skeleton probe (it logged the LE2/LE3 pawn, mesh and AnimTree, whose RefSkeleton
  it couldn't read there) and the torso log's pawn, controller and camera yaws (never reached there: arm IK
  stops first) need the layer, so they say nothing on LE2/LE3 until stage 5.
- **New stand-downs** where the old code would have read through: the grenade arc needs
  BioGrenade.m_clsProjectile and the ballistic class to resolve; arm IK's body axes need Actor.Rotation.

## 7. The plan for the rest

| Stage | Work | Effort |
|-------|------|--------|
| 2 | **Done (section 6.2), built and unit-tested; one in-game session to verify** (notes/next-vr-session.md, step 32: the `reflect:` lines, then a smoke test of each feature). | - |
| 3 | `engine/`: Objects (gone: its last users are `engine/Legacy`, Skeleton, ReflectCheck and Mod.cpp's startup gate, which becomes `ue3::runtime::Init` retrying until Engine.Camera exists), Skeleton (its interface is ours already; its body reads SpaceBases, RefSkeleton, LocalToWorld, Attachments, Sockets and the socket fields through FieldRefs, all four "native" fields being reflected; MatchRefBone/GetBoneLocation/GetBoneNames through `Params`; SFXName gone), NativesDump (UFunction fields from the layout), BonePose/CoverAcquire field reads, Engine.cpp's ResolveGlobals gone. GWorld (`engine::World()`, game/weapon's caches and BonePose's request check): the `Engine.GameEngine` instance's world through reflection, or its RVA with a pattern; add a `global` row comparing it with `*GWorld` first. | 1 session |
| 4 | **RVAs and hooks, without `LESDK::Initializer` and SPI.h** (touches the host and the performance session's `plugin/RenderLevers`; coordinate before merging). See below. | 1-2 sessions, a host redeploy |
| 5 | LE2/LE3 layouts: a Ghidra pass per exe (the same functions: `execIsA`, `GetPathName`, `ProcessEvent`, `execLetBool`, `AppendString`, `~UObject`), their RVAs and patterns in `GameAdapter.hpp` (with appMalloc/appFree in `AllocatorGlobalsForGame`), the self-check run once per game. Then `engine/Legacy` goes: head tracking, the console request, the reflection fix and hand aim take their LE1 path (`#if defined(MEVR_GAME_LE1)` blocks become unconditional), and `game/Player` serves all three games (LE2/LE3 spell the game-mode manager `GameModeManager2`: `adapter::kGameModeManager` already does). | 1 session per game |
| 6 | Remove LExSDKv2: the submodule, `add_subdirectory`, `LESDK-${game}` from `mevr_configure`, `SDK_TARGET_*` (CMake defines `MEVR_GAME_*` already), `engine/ReflectCheck` (or keep its pure checks as a ue3 self-test: Actor.Location is a 12-byte struct...), `GameAdapterSdk.hpp` (its last user is `engine/Legacy`); NOTICE.md updated. Stage 4 must first give `core/Hook` and the `Install(init)` signatures our own hook context (`game/GamepadBridge.hpp` still names `LESDK::Initializer`). Also the `/permissive` and SDK warning suppressions in `mevr_configure`. | half a session |

### Stage 4 design: our own RVA resolution and hook installation

What LExSDKv2 provides there today:
- **`Initializer`** = `{ModuleBase, OwnerName, ProxyInstance}` plus `Resolve(Address)`
  (base + RVA, pattern, RIP-relative).
- **`SPI.h`:** LEBinkProxy's `ISharedProxyInterface` declaration (shipped in LExSDKv2's
  `External/`).
- **The SPI plugin-side macros**, which make the ASI loadable by LEBinkProxy (`SPI_PLUGINSIDE_*`,
  `SPI_IMPLEMENT_ATTACH`).
- **The hooks themselves:**
  - Every one of our hooks goes through `core/Hook` →
    `ProxyInstance->InstallHook(name, target, detour, &original)`.
  - That is LEBinkProxy's shared MinHook, one hook ID per name, so several plugins can stack on
    one target.
  - Names are global, so ours carry the owner prefix (`LE1MEVR_`, `LE1MEVRmod_`).
  - A MinHook failure inside LEBinkProxy is sticky: every later install and uninstall by every
    plugin fails.

The replacement:
1. **`core/Exe`** (ours):
   - the module base and the `.text` range (as `ue3/Runtime` does);
   - `Resolve(rva)`, `ResolvePattern(pattern, instruction, displacement, length)` (the
     `ue3::pattern` code), and `IsFunctionStart` (`RtlLookupFunctionEntry`, as BonePose and
     CoverAcquire do now);
   - a table of named RVAs per game with an optional pattern each, in `engine/GameAdapter.hpp`
     next to `ReflectionGlobalsForGame`.
   - RenderLevers' 22 RVAs become entries of that table (patterns for the hooked ones first).
2. **The SPI interface: write our own declaration of it.** That is an interoperability interface
   (the vtable LEBinkProxy hands every plugin), not LExSDKv2 code. It has `GetVersion`,
   `InstallHook`, `UninstallHook`, and the plugin-side exports LEBinkProxy looks up.
   - The method order and the export names must match LEBinkProxy's. Read them from its binary
     behaviour and its documentation, not from `SPI.h`.
   - Then ask ME3Tweaks whether they'd license `SPI.h` (CLAUDE.md: "ask ME3Tweaks before copying").
   - Alternative: load without LEBinkProxy at all (our own loader stub, the installer job's
     `src/loader/`), and hook with our vendored MinHook (`third_party/minhook`, BSD-2).
     - Risk: coexistence. Another ASI hooking the same function through LEBinkProxy's MinHook,
       and ours through our MinHook, works in install order: each copies the other's jump into its
       trampoline, as Halcyon's private MinHook already does on ProcessEvent.
     - But removing a hook that isn't on top corrupts the chain. The host's hooks are never
       removed, so only the mod's (CallFunction, CalcSceneView, UpdateSkelPose, TryAcquireCover,
       XInput) need care.
   - Recommended: keep LEBinkProxy's SPI (other mods rely on it, and its stacking is tested), with
     our own interface declaration.
3. **`core/Hook` takes our `Exe`/owner context** instead of `LESDK::Initializer&`.
   - The signatures of `engine::*::Install(init)` change from `LESDK::Initializer&` to `HookContext&`.
   - A mechanical change across 9 files (ProcessEvent, CallFunction, SceneView, BonePose,
     CoverAcquire, GamepadBridge, PresentHook, ProcessEventHook, RenderLevers).
   - Behaviour identical: the same hook names, the same order.
4. **The host:** `plugin/Entry.cpp`'s SPI plugin macros become ours. `BUILTIN_GAMEENGINETICK_RVA`
   (`0x3CA090` on LE1) goes into the RVA table with a pattern; `UGameEngine*` becomes `void*`.

## 8. Risks

- **Unverified in the game.**
  - The layout matches our reading of the code and the synthetic tests. The self-check is the
    proof.
  - The checks before use can't catch everything. They cover GObjects indices, readable names,
    `Core.Class`, `Actor.Location` as a 12-byte struct, `SetRotation`'s parameter, and ProcessEvent
    in `.text`.
  - A wrong field offset in a migrated module would read the wrong field. The self-check's `field`
    rows exist to catch that before anyone relies on it.
- **Native-only fields.** The four we feared native-only (SpaceBases, RefSkeleton, LocalToWorld, Attachments)
  are reflected (stage 1's session). Their element layouts (FMeshBone 0x50, FBoneAtom 0x20) are still ours from
  research/14 and version-specific.
- **Stage 2:**
  - **Unverified in the game.** Every handle it resolves is a self-check row that agreed in stage 1's session,
    except the five new rows; the behaviour (the same calls with the same values) is a code review's claim
    until step 32's smoke tests.
  - **The allocator.** If appMalloc/appFree were wrong but plausible, a string freed through the wrong path
    could corrupt the heap rather than fault. Guards: the function bytes must match their pattern, both must
    load the same GMalloc, the self-check compares it with LExSDKv2's before its own round trip. Only the
    console request allocates, but since main's `game/SystemSettings` (merged into stage 2) that is every
    session (`scale set` for shadows, motion blur, foliage), not only the test loop's tool.
  - **Bool results** now test the whole word, as the generated blocks did (a native may write any non-zero
    UBOOL). A bool *parameter* still goes in by its mask (1 for every one we pass).
  - **Unaligned reads.** Some fields and parameters aren't naturally aligned (Frisbee.m_oIntendedTarget at
    +0x374, LaunchGrenadeAtPoint's ReturnValue at +0xC). x64 reads them fine; the launch handler's return slot
    is copied with memcpy, as before.
  - **A layer failure now turns every game feature off** on LE1 (before stage 2 most ran on LExSDKv2 anyway).
    That is the intended stand-down; the `reflect:` line says why.
- **Name lookups:**
  - Wrong-package fallbacks hide a typo in a package name. The note and the self-check show it.
  - Names are matched case-insensitively and ASCII only, which is all our code asks for.
- **Lookup cost:**
  - Startup: the class index is built once, about one pass over GObjects.
  - The self-check takes some hundreds of ms once per mod load [verify: its log line gives the
    time].
  - Per tick: the handles are cached. `LocalPlayerController` reads two cached offsets.
- **Hot reload:** the mod DLL's statics (handles, caches) go with it. `runtime::Shutdown` runs at
  mod shutdown.
- **Threads:** the caches aren't locked. All lookups happen on the game thread: TryStartup, ticks,
  and ProcessEvent/CallFunction handlers, as before.
- **A post-handler's first lookup** may resolve a handle inside the hook (allocations, a field-chain
  walk). The SDK's lazy `StaticClass` did the same.
- **Stage 4 risks:** LEBinkProxy's SPI ABI (if we declare it ourselves), and hook ordering if we
  ever leave its MinHook.
- **LE2/LE3** have no layout yet. Until stage 5, the all-game modules use `engine/Legacy` there (LExSDKv2).

## 9. Provenance

- **Written from:**
  - our Ghidra reading of the LE1 exe (section 2);
  - the engine's own reflection at run time;
  - UE3 knowledge in research/08, 09, 14, 18.
- **No LExSDKv2 code in `src/ue3/`.** No LExSDKv2 code, header or generated layout was copied into
  it.
- **LExSDKv2's role:**
  - It is compiled into `engine/ReflectCheck.cpp`, as the reference to compare against, into
    `engine/Legacy.cpp` (LE2/LE3 only), and into the not-yet-migrated `engine/`, `mod/`, `core/Hook` and
    `plugin/` files (stages 3 and 4). No `src/game` file includes it since stage 2.
  - The inventory in section 4 lists names our own code uses, not LExSDKv2's layouts.
- **Prior art credited in `NOTICE.md`:** LExSDKv2 and TheFeckless' UE3 SDK generator.
