<!-- SPDX-License-Identifier: MIT -->
# ME3 native engine access

Original observations from the protected desktop capture of executable 2.0.0.48602,
timestamp `0x60AEF0C4`, on 2026-11-15. No game code or dump is distributed here.
This extends the shared `ue3` implementation through `GameAdapter`; it does not enable
all ME1 gameplay modules. Runtime cross-check and gameplay acceptance are separate gates.

## Layout evidence

| Native reader (RVA) | Observed layout |
|---|---|
| UObject destructor `0x121250`, execIsA `0xE2BA0` | Object index `08`, class `50`, superclass `60` |
| IsPendingKill `0x879D0` | 64-bit flags at `0C`, pending-kill bit 61 |
| GetPathName `0x127F60` | Outer `40`, name `48` |
| ProcessEvent `0xF4F80`, CallFunction `0xF2C30` | Function flags `E0`, native index `E4`, parameter size `E6`, return offset `E8`, function pointer `D8` |
| ProcessEvent parameter loops | Struct children `70`, size `78`; next field `68`; property array count `70`, element size `74`, flags `78`, offset `94` |
| execLetBool `0xE98A0` | Typed property payload `D0` |
| AppendString `0x90580`, entry text `0x904E0` | Pool shift 29/mask 7, offset mask `1FFFFFFF`, characters `0C`, wide flag `40000000` |
| UObject vtable at `0x10774F8` | ProcessEvent slot `230` |
| CallFunction | Frame object `20`, code `28`; existing LE3 frame layout retained |

The function pointer precedes the flags on ME3. Copying the ME1/ME2 function layout
would therefore call the wrong address even though much of UObject is shared.

Globals: GObjects `0x1887E40`, name pools `0x17B33D0`, GMalloc `0x17CD588`,
GNatives `0x17F4BB0`. Allocators `0xBF110` and `0xBF0D0` use the same arguments as
ME2, with allocation/free virtual slots `08`/`18`. Both load the same GMalloc.
All twelve function/data patterns in the ME3 address table matched exactly once in
the captured text; all four RIP-relative data targets matched the declared globals.
The complete allocator patterns also match, not merely their prologues.
The Draw end is checked structurally against unwind data, without an invented pattern.

Skeleton evidence and remaining live pose gates are in [body pose](le3-body-pose.md).

## Protected engine probe

`tools/le2-halcyon-run` extends its existing LE3 desktop route with `-EngineProbe`.
It retains the session lock, save/install backups, Steam launch, process identity checks,
and hash-verified restoration. Both host and mod read the development-only option.
The host skips Halcyon and render/input initialization; the mod only initializes the
reflection layer and runs the existing independent LExSDKv2 cross-check on the game thread.
No gameplay module starts, and no save is loaded. The check exercises native owned-string
allocation/free in addition to reflected fields and functions.

The runner requires a fresh completed report, consistent per-row totals, and zero
mismatches. Native-only fields explicitly absent from reflection remain separately
counted, not silently treated as passing. A passing probe does not prove a player,
weapon pose, VR renderer, menus or combat.

### Native probe result, 2026-11-15 13:00 local

The protected `LE3-reflection` session launched through Steam with host/mod built from
integration `a202e37f` plus the engine adapter/probe changes. Host and mod address gates
accepted all 13 entries (12 unique byte patterns plus Draw-end structure; the mod recognized
the host's already-installed ProcessEvent hook). The renderer remained disabled.

The live cross-check passed **159/159**, with zero mismatches and zero unreflected rows.
It observed 107,531 objects; sampled names (20,057), full names (1,892), and IsA/liveness
comparisons (28,656) all agreed. Native owned strings were built, read and freed through
both allocator paths. The report completed in 73 ms. This proves the measured reflection
and heap contracts, not a live weapon getter, loaded-player identity or pose writes.

The owned process stopped, all three backup trees restored with zero differences, the
LE1 INI remained unchanged, and the global lock was released. No save was loaded; the
profile had zero changed or added files. No SteamVR/runtime settings were changed.
Private report, log and host/dump hashes remain in the protected session directory.
Next: native descriptor loading, same-tick getter/field agreement and world/actor invalidation,
followed by the renderer and gameplay gates in the implementation plan.

At 13:10 local, clean integration `05bad564` passed the extended **177/177** probe,
including the descriptor queue/getter parameter blocks, all eight nested members, and
their native target identities. The original native targets are queue `0xB4A270` and
getter `0xB4A190`; pointer agreement is not yet a copy-lifetime proof. This second session
also restored with zero differences and released the lock, without loading a save.

Integration validation: all three development host/mod pairs and all three monolithic
player builds passed. The existing 59-test suite passed, followed by five focused
runner/context/shared-test passes after the last GUI mapping integration. SPDX and
whitespace checks passed. Native gameplay, physical-headset review and campaign coverage
remain open; no diagnostic candidate is left installed for the player.

## Historical foundation and capture record

The following records the earlier discovery stages. Statements that the dump/layout were unavailable
are historical and superseded by the native-code evidence above.


2026-11-15, starting from `83539790`. This is L3-1 of the
[unified ME3 plan](../docs/le3-port-implementation-plan.md). No game was launched,
save changed, renderer deployed or native function called by this work.

## Evidence and enablement boundary

The original installed `MassEffect3.exe` was read directly. Its SHA-256 is
`72047e65a819878397c25650ccd9c1dc646db6b2517f88a69acacdc6215b9ec7`, size 26,896,672 bytes,
PE timestamp `60AEF0C4`, `.text` RVA `0x1000`, virtual size `0x1065FC1`.
`tools/verify_rvas.py` confirms ProcessEvent `0xF4F80` and CallFunction `0xF2C30`
are function starts; their exclusive ends are `0xF52A5` and `0xF306D` respectively.
This proves boundaries, not identity, parameters or callable behavior.

The executable's `.text` is encrypted on disk. The existing development dump inventory
contains LE1/LE2 captures, but no `MassEffect-60AEF0C4.text.bin`; there is no LE3 Ghidra
project. SDK declarations and upstream readers therefore remain **candidates**, not
native-code-derived offsets. `ReflectionLayout`, `ReflectionGlobalsForGame` and
`AllocatorGlobalsForGame` remain unavailable for LE3. `kModulesOnLayer` remains false.
No guessed `appMalloc`/`appFree`, pattern, or blanket module enablement was added.

## Exact candidates to verify against native code

The current-build LE3 section of `external/LExSDKv2/Src/LESDK/Init.hpp` declares:

| Candidate | RVA | Independent evidence so far |
| --- | --- | --- |
| GObjects | `0x1887E40` | Upstream `reference/MassEffect3-VR/src/engine_probe.cpp` uses it; original PE places it in writable `.data` |
| Name pools | `0x17B33D0` | Same upstream reader; writable `.data` |
| GMalloc | `0x17CD588` | Writable `.data`; not a callable allocator wrapper |
| GNatives | `0x17F4BB0` | Writable `.data`; opcode entries not yet inspected |
| GEngine | `0x18B31D0` | Writable `.data`; our layer discovers the engine by reflected objects |
| GWorld | `0x18B6638` | Writable `.data`; used as a development comparison, not a second production world reader |

Do not take similarly named RVAs from the SDK's older executable-build section.
Do not confuse GMalloc's pointer slot with an appMalloc function.

`LE3/Core_classes.hpp` declares the familiar UObject/UField/UProperty positions, but
**UFunction is not the LE2 layout**:

| Member | LE3 SDK offset | LE2 implementation offset |
| --- | --- | --- |
| Func | `0xD8` | `0xF0` |
| FunctionFlags | `0xE0` | `0xD0` |
| iNative | `0xE4` | `0xD4` |
| ParmsSize | `0xE6` | `0xE2` |
| ReturnValueOffset | `0xE8` | `0xE4` |
| ProcessEvent virtual slot | `0x230` | `0x240` |

The native pointer precedes flags and parameter metadata in LE3. Verify this against
ProcessEvent and CallFunction, including the native-index behavior; do not infer it by
adding a constant to LE2. Also independently inspect object index/flags/outer/name/class,
super/next/children/property-size, property flags/offset/typed tail, pending-kill bit,
name pool index splitting and character encoding. The SDK lowercases Actor's `location`
C++ member; reflected lookup remains case-insensitive. The development comparison uses
the actual declared member, not a substitute hand-written Actor structure.

## Implemented offline diagnostic preparation

`engine/ReflectCheck.cpp` now has an explicit LE3 table rather than silently compiling
the LE2 feature inventory. It compares core classes and layout, player/world/mode/weapon
fields, nested magazine-size value, a reflected boolean mask, scalar getter metadata and
console FString parameter/return metadata. Shared global, name, type and memory checks
remain available behind the existing `runtime::Up()` gate. It does not execute the weapon
getter or a console command. Skeleton and gameplay-adapter acceptance are outside L3-1.

The existing SDK-free synthetic reflection suite now exercises a function whose pointer
precedes flags/index/parameter metadata. It verifies independent pointer/index/flags reads
and typed parameter/boolean-return handling without making any engine call. The fixture
deliberately uses synthetic offsets, so passing it does not establish the native LE3 layout.

Validation: MSVC C++20 LE3 diagnostic translation unit passed `/Zs /W4 /WX` using the
repository's SDK PCH header and permissive SDK mode. The existing reflection suite plus
the new ordering regression compiled with `/W4 /WX` and ran with exit 0. Full combined
development/player/trilogy builds and native checks belong to integration acceptance.

## Smallest next capture and native discovery

Reuse the existing development host's `engine::code_dump::SaveOnce` path. At ASI attach,
`plugin/Entry.cpp` calls it after the executable gate and before installing hooks. It needs
no save, controller action, console request or reflection layout. A monolithic player build
deliberately does **not** dump code. The existing mod-tick fallback can retry later if code
pages were not readable at attach.

The session owner must acquire the shared run lock (`le2-halcyon-run/run-lock.ps1`, not the game-slot rollback tool) and use protected file manifests and
restoration. Observe the host log's `code dump: saved the decrypted .text` line and the local
dump's paired `.txt` receipt (`timestamp`, `text_rva`, `text_size`, `patched_starts`). Expected
name is `MassEffect-60AEF0C4.text.bin`, expected size `0x1065FC1`. A launch reaching ASI attach
is enough; do not load or edit a save solely to collect this evidence. Keep captures ignored.

Then use the existing `tools/ghidra/setup.ps1 -Game LE3` and `decompile.ps1 -Game LE3` flow
over the original executable plus that capture. Read the minimal natives that establish
each layout field; locate both allocator wrappers and prove their signatures, GMalloc
loads and virtual slots. Generate original byte patterns with the existing RVA-pattern
tool and verify uniqueness/consistent targets against the capture before adding entries.
Do not publish decompiled bodies or the rebuilt executable.

Only then add the LE3 layout/globals/allocator definitions and scoped address tests. Keep
the module rollout gate false. Native acceptance remains separate: matching reflection
rows, safe startup retries, valid world/player/weapon identity, a scalar getter matching
the same-tick field, owned-string allocation/free on the native heap, and load/unload
invalidation. A successful metadata check or ProcessEvent return is not gameplay success.

## Protected launch attempts (2026-11-15)

**Update, 12:33/12:37 local:** the launch blocker cleared. ME3 started through Steam, attached the development host,
and captured `.text` with the expected timestamp/size. The host log records dump-only completion before any renderer
or gameplay hooks. The capture has one pre-existing patched start (`0xCA830`); no other start was recorded patched.
SHA-256: `50c79c1c3e64ceca9af90ce87af919f01361cc9f5adb7efa2de8166e58bcd95d`.
The first successful launch exposed a live-log sharing error in the runner: `File.ReadAllText` did not share the host's
open writer. `Read-RunLog` now uses read access with read/write sharing, tested against a still-open synthetic writer.
The second launch passed the complete marker/dump receipt path and deliberately retained the previous cleaner/equal
capture (`freshDump=false`). Both sessions restored with zero differences and released the lock. Private tags:
`LE3-parity-engine`, `LE3-parity-logproof`. The local Ghidra import is in progress; layout/allocator and VR acceptance
remain pending. The failed attempts below are historical, not the current blocker.

The user authorized the available game window. Two desktop dump-only attempts used the shared runner,
verified backups and the development host. Neither produced an ME3 process or a host/dump receipt:

- The first timed out after 240 seconds. Steam retained a pre-existing Running state despite no game process.
  Steam Stop cleared that state and exposed Play.
- The retry encountered a transient launcher whose identity could no longer be established. The runner refused
  adoption and refused an unowned stop. Fresh process inspection found it had exited; restoration then passed.
  The runner now rechecks PID/path/start time and skips vanished processes, while rejecting live unexpected,
  unreadable or reused identities. Nineteen synthetic safety tests pass; the corrected path is not native-proven.
- Both attempts restored the complete profile and selected-game manifests with **zero differences**, left the LE1
  INI unchanged, and released the global run lock. No save was loaded and no VR runtime setting was changed.

EA's Steam launch helper remained pending after the retry. The UI fallback could inspect Steam, but its Stop
control was occluded by `EASteamLauncher.exe` and that helper exposed no targetable window; activation and one
retry did not resolve it. No runtime dump, reflection acceptance or VR acceptance is claimed. Resolve the pending
Steam/EA launch before the next protected run; do not merely repeat a launch against a stale Running state.
Private attempt receipts remain under `%LOCALAPPDATA%/MEVR/sessions` with tags `LE3-engine-capture` and
`LE3-engine-retry`. The installation is restored, not left deployed as dump-only or candidate VR.
