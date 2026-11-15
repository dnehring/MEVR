<!-- SPDX-License-Identifier: MIT -->
# LE3 engine access: foundation discovery receipt

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

The session owner must acquire the shared game slot and use protected file manifests and
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
