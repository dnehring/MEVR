# 09: ME3Tweaks native-mod code survey (from cloned source)

Read-only survey of `reference/LExASIs` (`aaad03a`; submodules LExSDKv2 `3885be1`, spdlog `27cb4c7`,
simdutf `cd79a25`) and `reference/LEBinkProxy` (`1c435d8`), 2026-10-07. Paths are relative to
`reference/`. Everything here was read from code; none of it is runtime-verified yet.

## Highlights for the VR mod

| Finding | Why it matters |
|---------|----------------|
| One source tree builds per-game binaries via `SDK_TARGET_LE1/LE2/LE3` | Matches our trilogy design exactly |
| ProcessEvent, GObjects, GEngine and GWorld are at **hardcoded RVAs** for exe build `2.0.0.48602` | Must confirm our installed exe matches (`tools/inventory-game.ps1`). A mismatch aborts the game |
| ConsoleExtension overrides the camera POV in `Engine.Camera.UpdateCamera` | **M1 pattern already exists** |
| ConsoleExtension hooks **`ULocalPlayer::CalcSceneView` on LE3** and rewrites FSceneView matrices | Confirms the H1 approach on LE3. **No LE1 RVA yet** |
| ConvoSniffer hooks `UBioConversation::StartConversation/EndConversation` on LE1 (by pattern) | Conversation detection for cinema-screen mode |
| `show scaleform` console command hides Scaleform UI | Quick way to test the HUD on/off |
| **No D3D11/DXGI/ImGui code anywhere** | We bring our own (REFramework-based) |
| The proxy's SPI `InstallHook` (MinHook multihook fork) can hook any address | We can route D3D11 Present hooks through it too, so multiple mods coexist |

## 1. LExASIs build system

- **Per-game targets:** LExSDKv2's CMake defines static libs `LESDK-LE1/LE2/LE3/LEL` with PUBLIC
  defines `SDK_TARGET_LE1` etc. (`LExSDKv2/CMakeLists.txt:140,170,198,226`).
  `Src/LESDK/Headers.hpp:14-55` derives `SDK_TARGET` (1/2/3, 0 = LEL) and
  `SDK_TARGET_NAME_A/W` ("LE1"), and errors on zero or multiple targets. Plugins branch with
  `#if defined(SDK_TARGET_LE1)`.
- **Root CMake** (`LExASIs/CMakeLists.txt`):
  - **Requirements:** CMake ≥ 3.27, C++20, MSVC only.
  - **Generator:** must be "Visual Studio 17 2022" or "Visual Studio 18 2026". The preset uses
    VS 18 2026 (`CMakePresets.json:22`).
  - **Required env var:** `LEASI_CMAKE_PRESET`.
  - **Flags:** `/MP /utf-8 /Zi`. Debug `/MDd /Od /JMC`, Release `/MD /O1`. Per-target
    `/W4 /WX /permissive` with C4121, C4127 and C4702 disabled. `SPDLOG_ACTIVE_LEVEL=0`.
  - **Output:** `Build/<CONFIG>/<GAME><Name>.asi`, e.g. `LE1FunctionLogger.asi` (lines 131-134).
  - **Optional copy and debugger:** if `LEASI_GAME1_WIN64` (and `_GAME2_`, `_GAME3_`) are set, a
    post-build step copies the ASI to `<Win64>/ASI/`. The VS debugger is set to `MassEffect1.exe`
    with `-NOHOMEDIR -enableminidumps -nosplash` (139-236).
- **Plugin declaration:**
  `asi_target(Name SHARED GAMES "LE1" "LE2" "LE3" SOURCES ... VLINKS "Common" "LESDK")`, plus
  `add_subdirectory` in the root.
- **Smallest full example:** `FunctionLogger/` (about 174 lines across `CMakeLists.txt`,
  `SharedVersion.h`, `.rc`, `Entry.cpp`, `Hooks.cpp/.hpp`). The entry pattern is
  `SPI_PLUGINSIDE_SUPPORT(...)`, `SPI_PLUGINSIDE_POSTLOAD`, `SPI_PLUGINSIDE_ASYNCATTACH`,
  `SPI_IMPLEMENT_ATTACH { LESDK::Initializer Init{InterfacePtr, ...}; ... }` and
  `SPI_IMPLEMENT_DETACH`.
- **For us:** LExASIs has no license, so we write our own equivalent CMake function. We consume
  `LExSDKv2` as a submodule and link `LESDK-LE1` and the others directly.

## 2. LExSDKv2

- **Layout:**
  - `Src/LESDK/Common/`: hand-written core (`Core.hpp`, `Frame.hpp`, `FString`, `TArray`, `TMap`,
    `SFXName`, `Math`, `Misc`).
  - `Src/LESDK/LE1|LE2|LE3/`: generated SDKs per game (`*_classes.hpp`, `*_structs.hpp`,
    `*_functions.cpp`), all stamped **"2.0.0.48602"**.
  - `Includes.LEx.hpp` (with layout `static_assert`s, e.g. LE1 `sizeof(UObject)==0x60`) and
    `Headers.hpp`, the public entry point.
- **ProcessEvent:** `virtual void ProcessEvent(UFunction*, void* pParms, void* pResult)`. The
  vtable slot is **LE1 0x250**, LE2 0x240, LE3 0x230 (`LE1/Core_classes.hpp:627`).
- **GObjects:** `UObject::GObjObjects` must be set by the plugin
  (`Common::InitializeRequiredGlobals`).
- **Names:** no GNames TArray. ME uses `SFXName` name pools (`GBioNamePools`, `GInitMethod` via
  `BUILTIN_SFXNAMEINIT_RVA`). `SFXName::TryFind` walks the pools.
- **Lookup helpers:**
  - `UObject::FindObject<T>(L"Class Pkg.Name")`, a linear scan.
  - `FindClass`, `IsA`, `StaticClass()`, `Cast<T>`, `CastDirect<T>`, `GetFullName`.
  - **No `FindFunction`.** Use `FindObject<UFunction>(L"Function X.Y.Z")` and cache the pointer.
- **Engine globals by hardcoded RVA** (`Init.hpp:124-276`). LE1 values:

  | Global or function | LE1 RVA |
  |---|---|
  | ProcessEvent | `0x12dd20` |
  | ProcessInternal | `0x12e060` |
  | CallFunction | `0x12ae40` |
  | GEngine | `0x17813e8` |
  | GObjects | `0x1770670` |
  | GWorld | `0x1783b48` |
  | GMalloc | `0x16b6700` |
  | GNatives | `0x16dd3c0` |
  | SFXNamePools | `0x16a2090` |
  | SFXNameInit | `0xdd800` |
  | GameEngineTick | `0x3ca090` |
  | UEngine::Exec | `0x3a3010` |
  | UGameEngine::Exec | `0x3BD5D0` |
  | UWorld::SpawnActor | `0x409690` |

  The pattern alternatives are commented out.
- **`LESDK::Address` modes:** `FromAbsolute`, `FromOffset` (RVA), `FromPattern` (via the proxy's
  `FindPattern`), `FromInstrRelative` (RIP-relative MOV/LEA), `FromPostHook`. The main module is
  the first module whose name starts with "MassEffect".
- **Hooks:** `Initializer::InstallHook(Name, Target, Detour)` forwards to the proxy's SPI.
  There's no hooking library in the SDK itself.
- **`BUILTIN_CALCSCENEVIEW_RVA` is not defined** at this commit, although LE3 ConsoleExtension
  references it (it probably doesn't compile against this pin).

## 3. LExASIs `Common/`

- **`Base`:**
  - Logging macros `LEASI_INFO`, `LEASI_WARN` etc. (spdlog).
  - `LEASI_VERIFYA` and `CHECK_RESOLVED`, which **`std::abort()` on failure**.
  - `LEASI_HOOKDEF(Name, Ret, ...)`.
  - `Common::InitializeRequiredGlobals` resolves GMalloc, GObjObjects and the SFXName pools only.
    GEngine and GWorld are resolved per plugin.
- **`Memory`:** `AllocateThroughEngine` (new/delete via GMalloc). No pattern-scan or hook helpers.
- **`Objects`:** `TypedObjectIterator<T>`, `FindFirstObject<T>()`, `IsDefaultObject`, `IsDestroyed`.
- **`Utils`:** `FindSeqObjectsByClass<T>` (Kismet), `CauseRemoteEvent(ABioPlayerController*, SFXName)`.
- **`DefaultLogger`:** console plus a file at `Logs/<name>.log` relative to Win64. The
  `-trace-log-level` flag raises verbosity.

## 4. Hook patterns

- **Standard flow:** `Init.ResolveTyped<t_X>(RVA)`, then `Init.InstallHook("name", target, hook)`,
  keeping the returned trampoline as `X_orig`. ProcessEvent is hooked as a free function
  `void(UObject* ctx, UFunction* fn, void* parms, void* result)`.
- **Filtering:** existing plugins compare `Function->GetFullName()` strings on every call. That's
  slow. **For VR, cache the `UFunction*` pointers once and compare pointers.**
- **Per-frame tick:** `GameEngineTick`, `void(UGameEngine*, float dt)` (LEXInterop).
- **Camera** (ConsoleExtension `Hooks.cpp:192-226`): caches
  `PlayerController->PlayerCamera->CameraCache.POV` on `PlayerTick`, and on
  `"Function Engine.Camera.UpdateCamera"` writes location and rotation. `FTPOV` is
  `{FVector Location; FRotator Rotation; float FOV}` (LE1 `Core_structs.hpp:386-391`).
- **`ULocalPlayer::CalcSceneView` (LE3 only)** (ConsoleExtension `Hooks.hpp:33-37`,
  `Hooks.cpp:246-373`). Hand-mapped LE3 FSceneView offsets:

  | Field | LE3 offset |
  |---|---|
  | ViewMatrix | `0x90` |
  | ProjectionMatrix | `0xD0` |
  | ViewProjectionMatrix | `0x260` |
  | InvViewProjectionMatrix | `0x2E0` |
  | ViewOrigin | `0x320` |

  We need the LE1 equivalents.
- **ConvoSniffer** (LE1 only):
  - **Pattern hooks:**
    - `UBioConversation::StartConversation(UBioConversation*, AActor* Owner, AActor* Player)`
      (`40 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 ...`)
    - `EndConversation(UBioConversation*)` (`40 56 48 83 EC 40 83 89 ?? ?? ?? ?? 04`)
    - `SelectReply`, `QueueReply`
  - A `CallFunction` hook catches `UpdateConversation`.
  - **State read:** `m_nCurrentEntry`, `m_EntryList`, `m_ReplyList`, `m_lstCurrentReplyIndices`,
    `ABioWorldInfo::m_oCurrentConversation`.
  - Hides the dialogue UI with the console command `show scaleform`. Streams data to a local
    Rust server.

## 5. Rendering

- **No D3D11, DXGI, swapchain or ImGui code** in either repo.
- All on-screen drawing uses the UE3 `UCanvas` from ProcessEvent hooks on
  `"Function SFXGame.BioHUD.PostRender"` / `DrawHUD` (`SetPos`, `DrawText`, `DrawBox`,
  `Draw2DLine`).
- `PNGScreenshots` hooks `appCreateBitmap` (a CPU buffer). RVAs: LE1 `0x154fc0`, LE2 `0xfd810`,
  LE3 `0x119ed0`.

## 6. LEBinkProxy (the loader)

- **Build:** an MSBuild v143 C++17 proxy for `bink2w64.dll`, forwarding to
  `bink2w64_original.dll`. Logs to `bink2w64_proxy.log`, which in practice lands in
  **`Win64/Logs/`** (observed 2026-10-07).
- **Load order** (`dllmain.cpp`), on a new thread at attach:
  1. `MH_Initialize`.
  2. Detect the game from the exe name.
  3. `LoadLibraryW` every `ASI\*.asi` (relative path, max 128).
  4. PRELOAD plugins' `SpiOnAttach`.
  5. **Wait for the DRM to decrypt the exe**: poll for the `UFunction::Bind` pattern.
  6. Console enabler.
  7. **POSTLOAD plugins' `SpiOnAttach`.** All of LExASIs uses POSTLOAD.
- **SPI v3** (`ISharedProxyInterface`): `GetVersion`, `GetBuildMode`, `GetHostGame` (returns
  "unsupported yet"), `FindPattern` (main module, max 100 bytes), `InstallHook(name, target,
  detour, &orig)`, `UninstallHook`. Hooks go through the **multihook fork of MinHook**, so several
  ASIs can hook the same target; hook names must be unique.
- **Required ASI exports:** `SpiSupportDecl`, `SpiShouldPreload`, `SpiShouldSpawnThread`,
  `SpiOnAttach`, `SpiOnDetach`. ASIs without them still get loaded ("RAW").
- **Launcher bypass:** in `MassEffectLauncher.exe`, `-game N [-autoterminate]` launches
  `../ME{N}/Binaries/Win64/MassEffect{N}.exe -NoHomeDir -SeekFreeLoadingPCConsole ...`. The env var
  `LEBINK_DEBUGGER` launches it under a debugger. Autoboot args come via
  `HKCU\SOFTWARE\ME3Tweaks\LEAutobootArgs`.
- **Console enabler:** rebinds `IsShippingPCBuild` and similar via a `UFunction::Bind` hook.
- **No D3D11/DXGI hooks.**

## 7. Licenses

- **LExASIs:** no license file. `.rc` files say "Copyright (C) 2026 ME3Tweaks". **Treat as all
  rights reserved.**
- **LExSDKv2:** no license, deliberately (README).
- **LEBinkProxy:** no top-level license; `Resource.rc` says "Copyright (C) 2021 - 2023
  d00telemental / ME3Tweaks". Its `minhook/` is **BSD-2-Clause** (Tsuda Kageyu; HDE by Vyacheslav
  Patkov).
- **Third-party code inside LExASIs:** spdlog (MIT), simdutf (MIT/Apache-2.0), fpng (Unlicense),
  nlohmann json (MIT), Discord Social SDK notices.

## LExSDKv2 branches (checked 2026-10-07)

| Branch | Head | How addresses are found | Notes |
|--------|------|-------------------------|-------|
| `main` | `000d7c3` (2026-03-30) | **Byte patterns** (`*_RIP` via `FromInstrRelative`, `*_PHOOK` via `FromPostHook`) | Only extra commit: adds LE3 `BUILTIN_CALCSCENEVIEW_RVA` (`0x6cffd0`) |
| `RVA` | `6bd0580` (2026-07-12) | **Hardcoded RVAs** (`*_RVA`) for exe 2.0.0.48602 | Actively maintained. Contains LExASIs' pin `3885be1` plus later fixes (TMap, FArchive layout, LE3 shader decomp) |

**We pin `RVA`** (`.gitmodules` `branch = RVA`, submodule at `6bd0580`). It matches what ME3Tweaks
builds against, and our exes are 2.0.0.48602. The plugin checks the exe version at attach and
won't hook on a mismatch. The LE3 `CalcSceneView` RVA from `main` (`0x6cffd0`) is worth carrying
over when we reach LE3. **LE1 still has no known `CalcSceneView` address.**

## Issues spotted (code reading only)

1. **Hardcoded RVAs** only match exe build 2.0.0.48602. Unverified against our install.
2. `BUILTIN_CALCSCENEVIEW_RVA` is missing at this SDK pin.
3. **Possible loader bug** (`asi_loader.h:86,108,130`): a function-local `static bool ranCall` is
   shared across plugins, so only the first plugin's `SpiShouldPreload` / `SpiShouldSpawnThread`
   answer is honored. Later plugins default to POSTLOAD with sequential attach. Worth reporting
   upstream if confirmed.
4. `GetHostGame` returns a failure code even though it writes the value.
5. The proxy's `ScanProcess` may return before checking the last pattern byte (`memory.h:53-56`).
6. Unknown: whether `Game/Launcher` ships its own `bink2w64.dll` (affects the autoboot path).
7. **Generated native-function wrappers corrupt `FunctionFlags`** (found 2026-10-07). Every LE1
   wrapper in `*_functions.cpp` (5,301 of them) does `pFn->FunctionFlags |= ~0x400;` before
   ProcessEvent and `|= 0x400` after. The intent was clearly `&= ~0x400` (temporarily clear
   FUNC_Native). As written, every flag ends up set (`0xFFFFFFFF`) on the first call. Rule for
   MEVR: don't call generated wrappers; resolve the UFunction and call ProcessEvent directly
   (CLAUDE.md). Worth reporting upstream (llamathings/LExSDKv2). Also open: whether LE's
   ProcessEvent needs FUNC_Native cleared to run a native function at all. Test on
   `SceneCaptureComponent.SetFrameRate` (the reflection-judder fix, [13](13-existing-me-vr-mods.md)).
