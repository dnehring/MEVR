<!-- SPDX-License-Identifier: MIT -->
# ME2: the crash on the third same-map reload, and the kept pointers that caused it

Date: 2026-11-19, lane RLD (the coordinator's item 1 after lane SMOKE). ME2's exe 2.0.0.48602 (`MassEffect2.exe`, PE timestamp
`0x60AEEFA5`). Players die and reload all the time, so this one came first. **It was ours.** Fixed for every game in `src/ue3`
(commit `4fac4ce7`); the decompiled text stays in Ghidra's local cache, never in git.

## The crash

Lane SMOKE's run (`%LOCALAPPDATA%\MEVR\sessions\20261119-072839-452-LE2-smoke\crash-run2\`, pid 157536) loaded the owner's
autosave (career `VR_31_Soldier_141126_336072f`, `AutoSave.pcsav`, Freedom's Progress) four times in one process: from the title
at 07:34:03, then the same map at 07:42:28, 07:53:59 and 07:55:43. The last died 175 ms after it was queued:

```
exception 0xC0000005 (access violation) at ucrtbase.dll+0x6A80D, reading 0xFFFFFFFFFFFFFFF8
  [0] ucrtbase.dll+0x6A80D   [1] MassEffect2.exe+0x8C6915   [2] +0x8C6DB4   [3] +0x11D5B5   [4] +0x11BC2F
  [5] +0xB7BAC2   [6] +0x535BAC   [7] +0x546F9B (UGameEngine::Tick)   [8] LE2MEVR.asi+0x55CD7 (GameEngineTickHook)
```

What the frames are (Ghidra, `tools/ghidra/decompile.ps1 -Game LE2 0x8C6915 ...`; the stack's own breadcrumbs agree):
- `ucrtbase+0x6A80D` is `_aligned_free+0xD` (the export table): `free((void**)((uintptr_t)p & ~7)[-1])`. A pointer of 1 to 7
  gives `(p & ~7) - 8 = -8`, the address read. `_aligned_free(NULL)` returns early, so the block freed was a small non-zero
  number (`rdx = 1` in the registers).
- `0x8C6820` (the faulting call at `+0xF5`, `0x8C6910`) is a class's C++ destructor: it frees the class's arrays one by one with
  `appFree`. The call at `0x8C6910` frees **the array at `this + 0x214`** (`mov rcx, [rbx+0x214]`; the bytes before each `E8` in
  the dump: `8b 14 02 00 00`). The object is 0x3AC bytes (its scalar deleting destructor `0x8C6DA0`), with a second vtable at
  `+0x60`.
- `0x11D1D0` is the engine's incremental purge of unreachable objects (a time-sliced destructor loop), `0x11BAC0` the garbage
  collection that calls it, `0xB7AAB0` (11 KB) the map load, `0x5356A0` the load's step inside `UGameEngine::Tick`.

So the purge that every load runs destroyed an object whose empty array at `+0x214` had a data pointer of 1.

## Who wrote the 1

`SFXGame.SFXCameraMode.bIsCameraShakeEnabled` is **at `0x214`, mask `0x1`** (`%LOCALAPPDATA%\MEVR\reflect-check.json`: `"ours":
"0x214 mask 0x1 (bool)"`, LExSDKv2 the same). `game/CameraShake` clears it on the first-person camera modes in VR (the body's camera
shakes off, `[Comfort] viewShake = 0`) and puts it back when the view stops being head-tracked, which a load's tail always is.
The crashed run's log, right after the third load (the second of the same map):

```
[07:54:00.574] camera shake: the game's screen shakes back on (1 of 2 camera modifiers; VR is off)
[07:54:03.148] camera shake: the camera modes' shakes put back on 1 of 2 (the view isn't head-tracked)
```

The camera modes and modifiers it put back were the previous level's: the load had freed them. The module kept them as raw pointers
and checked each with `ue3::runtime::IsAlive` before the write. **IsAlive can't see this**: it reads the object's GObjects index
from the object itself and compares `GObjects[index]` with the pointer. The engine reuses a freed object's memory for the next
object of a similar size and often its GObjects slot too (the object count read `objects=165194` before and after every load of
the run), so the pointer was alive: as another object, of another class. The restore set bit 0 of the dword at `+0x214` of that
object, whose array there was empty (data `NULL`): data `1`. The next load's purge destroyed it and freed `1`.

The load before (07:42:28) had put back `0 of 2` and `0 of 1` (no address reused), and the next load was fine; the one with a reused
address (`1 of 2`, twice) was followed by the crash at the next load. ME1 has the same code path (camera modes, and
`game/CameraCollision`'s `bCollisionEnabled`), and every module that keeps an object across a load and writes a field of it later
has the same hazard.

## The fix (every game, `src/ue3`)

1. **`ue3::Kept`** (`ue3/Runtime.hpp`): a kept object with who it was, `Reflection::Identity` (its GObjects slot, its class and its
   name; the engine counts a class's names up, `SFXCameraMode_Explore_2` then `_3`, so a newcomer of the same class has another
   name). `Live()` gives the object back only while `StillSame` (alive, and still that slot, class and name); the reads are guarded
   as IsAlive's. Tested in `tests/ReflectTests.cpp` `TestKeptIdentity` (another class in the same block and slot, the same class
   with the next name, another slot, and put back exactly).
2. **`FieldRef` refuses a class's field on an object not of that class** (`ue3/Handles` `FieldRef::Fits`, in `In`, `At`, `ReadBool`
   and `WriteBool`; a struct member's or a parameter's field is never checked): whatever module keeps a stale pointer, a write
   through a field handle can no longer land in another class's object. Said once per field: `reflect: field
   SFXGame.SFXCameraMode.bIsCameraShakeEnabled refused on 0x... (Class ...): not an object of that class (a pointer kept after the
   engine freed its object, the memory now another object's?)`. A legitimate use on another class would show the same line; none
   showed in the runs below.
3. `game/CameraShake` (`gModifiersOff`, `gModesCleared`, ME1's `gScaled`) and `game/CameraCollision` (`gCleared`) keep their objects
   as `ue3::Kept`, and the restore line says when a kept one now holds another object: `camera shake: the game's screen shakes back
   on (0 of 2 camera modifiers; VR is off); 1 kept now hold another object (a load freed them and reused the memory): left alone`.

New engine access keeps any object it will write later as a `ue3::Kept` (CLAUDE.md's gotchas,
[gotchas/kept-pointer-another-object](../docs/knowledge/gotchas/kept-pointer-another-object.md)).

## Checked in the game (fake headset, 2026-11-19, `sessions\20261119-083503-624-LE2-rld`)

- One ME2 process, the owner's autosave loaded **13 times** (12 same-map loads: `rld-loads1`, six without controllers; `rld-loads3`,
  six with replayed tracked hands and `mevr weapon pistol` after each, plus one in between), 15-25 s on foot between loads. No crash
  (the old build died on the third same-map load).
- Twice the hazard came up and was caught: `[08:40:02.572]` and `[08:40:51.645] camera shake: the game's screen shakes back on (0 of 2
  camera modifiers; VR is off); 1 kept now hold another object (a load freed them and reused the memory): left alone`. The old code
  would have written `bDisabled = false` into those objects.
- No `reflect: field ... refused` line in the whole run (13 loads, holsters, traversal, the power wheel): no module of the ones that
  ran uses a field handle on another class.
- `python tools/session-judge.py --mod <log>`: SMOKE's crashed log now shows `[bad] engine-free-corrupt-pointer` (the ucrtbase read
  before a pointer of 1-7) besides `crash-report`; this run's log only the fake headset's frame-rate note, and with `--all` the notes
  `kept-object-reused: 2` and `holster-load-draw: 7`.

Not done: an A/B with the old build to the crash. The crash needs the allocator to hand a freed camera mode's block to an object
with an empty array at `+0x214`, which happened once in SMOKE's four loads and in none of my thirteen; the bit, its offset and the
restore line right after the load that preceded the crash name the writer, and the guard has been seen catching the same reuse.
