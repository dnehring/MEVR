<!-- SPDX-License-Identifier: MIT -->
# LE2 diagnostic host readiness

2026-11-13; base `2d90423c7595f15170670379b0fb3526b307ca99`, isolated branch `work/le2-native-host-readiness`. This is the concrete non-XR host slice for the [scripted testing handoff](../docs/le2-scripted-testing.md), not the full [engine access plan](le2-engine-access-discovery.md). No game deployment/launch, live request, save/profile/setting change or shared-lock operation was performed.

## What already runs

`plugin/Entry.cpp::SpiOnAttach` checks the supported executable version and the game's RVA table, installs crash breadcrumbs and the host ProcessEvent hook, starts the mod, then installs `UGameEngine::Tick`. That hook records the game thread, invokes the original tick, and calls `host::Tick`; `ModLoader` dispatches to `MevrModTick` in both monolithic and hot-reload builds. This route does not require XR or a loaded playable pawn. A failed game-tick hook prevents progress; a missing player is a later capability state, not an attach failure.

For LE2, `MevrModStartup` already calls `engine::legacy::Init`. It resolves the SDK globals by module-relative addresses; success proves address resolution only, not valid live globals/objects. `ConsoleRequest::Tick` is called near the beginning of `MevrModTick`, **before** the script/camera startup gate. Therefore a console consumer does not need to make `gHooked` true or enable camera/VR features first.

The old generic channel is not safe for a protected LE2 session: it polls the unqualified `%LOCALAPPDATA%/MEVR/console.request`, waits indefinitely for a player, caches even a failed function lookup, and writes an unbound result. LE1-specific `mevr ...` handlers are compiled out in LE2. A monolithic build defaults developer channels off; `[Debug] requests` can override this, but changing that setting is not a substitute for game/session/request binding. This candidate never polls that channel.

An ordinary non-Halcyon LE2 build still installs Present and calls the old VR output path; the mod also polls hotkeys, adjusts system settings, installs camera/gamepad hooks and can run reflection-capture changes. `MEVR_HALCYON=OFF` alone is therefore insufficient for a diagnostic-only run.

## Implemented gate and callback contract

New CMake option `MEVR_LE2_DIAGNOSTIC` defaults **OFF** and requires an isolated LE2-only configuration (`LE1=OFF`, `LE2=ON`, `LE3=OFF`). Existing target names remain: `MEVR-LE2` produces `LE2MEVR.asi`; hot reload additionally produces `MEVR-LE2-mod` / `LE2MEVR_mod.dll`. The macro is applied to both pieces, including the monolithic case.

- Entry retains version/RVA checks, logging, crash breadcrumbs, the existing host ProcessEvent hook, original game tick and exit handling. It skips the decrypted executable dump and Present hook. No renderer is started by this path.
- Mod startup retains existing host service/log/breadcrumb wiring and Legacy initialization. Its diagnostic tick dispatches the new callback and returns before hotkeys, developer file polling, system-setting changes, script/camera startup, input setup or gameplay feature ticks.
- `MevrModOnPresent` is inert in the diagnostic build even if called accidentally. Diagnostic shutdown clears the callback/thread binding and init context without running cleanup for gameplay features that were never started.
- Existing default LE1/LE2/LE3 paths are unchanged when the option is off. This is a runtime gate in the existing target, not a new stripped binary: it still compiles/links common code, including OpenXR dependencies and the current unlicensed SDK. It is an internal diagnostic artifact, not the SDK-free sequel release.

Registration API in [Le2DiagnosticHost.hpp](../src/engine/Le2DiagnosticHost.hpp):

```cpp
namespace mevr::engine::le2_diagnostic {
using TickCallback = void (*)(float);
bool SetTickCallback(TickCallback callback);
void Tick(float deltaSeconds);
void Shutdown();
}
```

The root-owned bound-channel adapter must register once in `MevrModStartup`, **after successful Legacy initialization and before the first host tick**. Registration failure must disable the adapter, not fall back to the shared request file. No callback is registered in this commit: the startup log truthfully says there is no bound consumer. When integration adds registration, update that message to report its actual result.

`core/qa/DiagnosticTickGate.hpp` owns the small dispatch policy: null, duplicate/replacement and post-first-tick registration fail; the first host tick binds a nonzero thread ID; other-thread and nested ticks cannot execute the consumer. Reset while a callback is running fails. The callback executes synchronously outside the gate mutex, preventing reentrant reset from deadlocking. C++ unwinding restores the running flag; this is not a Windows access-violation recovery mechanism. Host shutdown/unload must remain serialized with ticks; the wrapper logs a refused reset, but cannot make concurrent DLL unloading safe.

The callback slot is **not** a protocol validator, object validator or observer. It must not expose raw engine pointers to worker/Present/XInput threads. It supplies no success receipt, no automatic retry and no native readiness assertion. Root owns the session-bound protocol/transport and its initialization/cleanup; integration must identify the loaded candidate, process start identity, run/request IDs and deadlines before issuing any engine action.

## Legacy console risks still requiring an admission gate

The existing `Legacy::ConsoleCommand` constructs a game-allocated string, sets `bWriteToLog=false`, invokes ProcessEvent directly and frees the command/return strings. It avoids the generated wrapper that corrupts FunctionFlags. Pinned LE2 SDK declarations give `Engine.PlayerController.ConsoleCommand` a packed `0x24` parameter block: Command FString at `0x00` (16 bytes), bWriteToLog at `0x10` (4 bytes), ReturnValue FString at `0x14` (16 bytes). These are declaration facts; no live parameter layout/native index/vtable/allocator validation is supplied by this gate.

Before consuming a request, the adapter still needs guarded globals and array validation, listed/live/non-template controller and function checks, exact class/function identity, native index and parameter-size/flags/type/offset checks, and a validated ProcessEvent target. Cached pointers must be invalidated across context changes; pointer equality is not generation proof. `LocalPlayerController` and `FindFunction` currently perform raw SDK reads. `ConsoleRequest::RunCommand` returning true, or an empty native string, does not prove a requested gameplay outcome.

The bounded Legacy change allocates the UTF-8 conversion buffer including its terminating NUL, checks the Win32 conversion result, then removes the terminator from the returned string. It makes the API's full output extent explicit; it is not evidence of corrected native parameter behavior. No SDK wrappers or new SDK access paths were added.

Initial integration should expose handshake/capability failure without touching engine objects, then validated read-only observation, then a separately admitted harmless native query. Load/save/quit must remain unavailable until these foundations are demonstrated. See the earlier [engine discovery](le2-engine-access-discovery.md) for the unresolved LE2 reflection layout/ProcessEvent-slot differences. The diagnostic gate does not resolve them.

## Reproduce the isolated build

Initialize the recorded LExSDKv2 and OpenXR submodules in the worktree; this run used local clones of the already present pinned checkouts, without modifying them. Configure a new ignored output directory:

```powershell
cmake -S . -B local-tools/diagnostic-build -G "Visual Studio 17 2022" -A x64 `
  -DMEVR_BUILD_LE1=OFF -DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=OFF `
  -DMEVR_LE2_DIAGNOSTIC=ON -DMEVR_HOT_RELOAD=OFF `
  -DMEVR_HALCYON=OFF -DMEVR_MAIN_MENU=OFF -DMEVR_BUILD_TESTS=ON
cmake --build local-tools/diagnostic-build --config Release --target MEVR-LE2 mevr-le2-diagnostic-tests --parallel 4
ctest --test-dir local-tools/diagnostic-build -C Release -R '^mevr-le2-diagnostic$' --output-on-failure
```

The focused synthetic tests cover absent consumers, registration timing/replacement, missing/wrong thread IDs, synchronous delivery, reentrant tick/reset, context reset and exception cleanup. They use invented IDs and callbacks, not native pointers or saves. Compilation/tests do not prove a DLL will attach to the game or a native query will return correctly.

Validation completed with MSVC 19.44.35228.0 and Windows SDK 10.0.22621.0: the Release monolithic `MEVR-LE2` compiled and linked, the dispatch test executable compiled, and focused CTest passed 1/1. `dumpbin /exports` confirmed the expected loader exports in the produced ASI without loading it. A separate configuration with LE1 and LE2 enabled deliberately failed at the diagnostic isolation check. All four document links resolved and `git diff --check` passed. The generated SDK emitted its existing C4789 cover-wrapper warnings; those wrappers were not called or changed.

Build/log/test artifacts stay in ignored `local-tools/diagnostic-build*`. The first compile used this worktree's uncommitted candidate (`2d90423c75-dirty` build identity), so it is compile evidence, not a deployable identity receipt. Rebuild after integration and pin the resulting binary hashes before any native session. At that initial checkpoint, ordinary regression builds and the larger CTest suite had not run; the follow-up below closes those offline checks. Live attach, query output, lifecycle cleanup and headset checks remain unperformed. No native channel consumer or read-only engine observer is implemented by this commit.

## Ordinary configuration regression checks

Follow-up tested the clean implementation commit `a936506094c1836e20119505f02950743a581c63`, including a clean rebuild of the diagnostic ASI. **No source regression or additional code fix was found.** All builds remained in this isolated worktree; nothing was deployed or launched in an installed game.

| Configuration | Result |
|---|---|
| Default LE1 development: hot reload ON, Halcyon/menu ON, LE1 SDK reference checks ON, diagnostic OFF | Full default Release build succeeded, including `LE1MEVR.asi`, `LE1MEVR_mod.dll`, tools and test fixtures |
| Ordinary LE1 monolithic: hot reload OFF, Halcyon/menu ON, LE1 SDK OFF, diagnostic OFF | Release `MEVR-LE1` compiled/linked; its linker project contains no `LESDK-LE1.lib` dependency |
| Ordinary trilogy: LE1/LE2/LE3 ON, hot reload ON, Halcyon/menu ON, diagnostic OFF | `MEVR-LE1`, `MEVR-LE2` and `MEVR-LE3` succeeded, producing all three hosts and all three mod DLLs |
| Full default-development CTest | **35/35 passed**, 106.55 seconds; includes reflection/core tests, diagnostic dispatch, save/session/scenario/story tooling, loader, temporary fake-install/localhost setup tests and shader checks |
| Source checks | SPDX and `git diff --check` passed. Ordinary LE1/LE2/LE3 mod projects exclude the diagnostic flag and diagnostic host source |

Commands used the normal configure/build flow above, with separate `local-tools/ordinary-dev-build` and `local-tools/ordinary-mono-build` directories. Development used the default options and a full `cmake --build ... --config Release --parallel 3`; monolithic added `-DMEVR_HOT_RELOAD=OFF` and built `--target MEVR-LE1 --parallel 2`. After preserving the development cache/hashes, its directory was reconfigured with `-DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=ON` and built with `--target MEVR-LE1 MEVR-LE2 MEVR-LE3 --parallel 3`. The full test command was `ctest --test-dir local-tools/ordinary-dev-build -C Release --output-on-failure` before the trilogy reconfiguration. Tests were not rerun merely for that configuration change.

For bounded compiler parallelism, the local caches used `CMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc /MP4` and `CMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /MP4`, preserving CMake's normal platform/exception flags. An initial acceleration configuration accidentally omitted `/EHsc`; it was stopped and corrected before the successful full build. Its C4530/C2220 diagnostics were configuration errors, not source failures.

Private logs are `local-tools/ordinary-dev-build-fixed.txt`, `ordinary-mono-build.txt`, `ordinary-trilogy-build.txt` and `ordinary-dev-ctest.txt`. `local-tools/ordinary-build-evidence.json` pins the seven ordinary binary hashes and tested source commit. The source/build identities stayed `a936506094` throughout these checks. This follow-up documents offline regression evidence only; it does not validate the native request channel or promote any runtime gate.
