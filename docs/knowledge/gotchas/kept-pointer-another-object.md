---
type: Gotcha
title: A pointer kept across a load can be alive as another object, and a field put back there corrupts it
description: IsAlive compares the object's own GObjects index with the slot, and the engine reuses a freed object's memory and slot, so a kept pointer passes as another object of another class; CameraShake's restore set a bit in such an object's empty array and ME2's next load crashed in the purge; keep objects as ue3::Kept, and field handles refuse another class.
tags: [engine, reflection, crash]
status: stable
timestamp: 2026-11-19T17:30:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-19T17:30:00Z }
bitten: [2026-11-19]
---

# Symptom

| when | what it looked like |
|---|---|
| 2026-11-19 | ME2 crashed on the third load of the same save in one process (lane SMOKE): `exception 0xC0000005 (access violation) at ucrtbase.dll+0x6A80D, reading 0xFFFFFFFFFFFFFFF8`, the stack in the engine under `UGameEngine::Tick` (`MassEffect2.exe+0x8C6915` <- `+0x8C6DB4` <- `+0x11D5B5` <- `+0x11BC2F` <- `+0xB7BAC2` <- `+0x535BAC` <- `+0x546F9B`), only the tick hook of ours on it. 81 ms before, the HUD still called `GetGUIIcon` on `SFXPawn_PlayerSoldier_2 (not in GObjects)`: a red herring (the breadcrumbs mark what is gone at the report's time). |

# Cause

`ue3::runtime::IsAlive(p)` reads the index stored in the object at `p` and checks `GObjects[index] == p` and pending kill. A load
frees the level's objects; the engine hands the same memory to the next objects of a similar size and reuses free GObjects slots, so a
pointer kept from before the load can point at a live object of another class: IsAlive says yes. `game/CameraShake` then put
`SFXCameraMode.bIsCameraShakeEnabled` (`+0x214`, mask `0x1`) back into that object: its empty array at `+0x214` got data pointer 1,
and the next load's purge called `_aligned_free(1)` (it reads 8 bytes before the pointer). Any module that keeps an object across a
load and writes a field of it later can do the same; the reflection's class index had the same reuse problem
([stale-class-index](stale-class-index.md)).

# Check

- The crash: `ucrtbase.dll+0x6A80D` (`_aligned_free`) reading `0xFFFFFFFFFFFFFFF8` (or `...F0`-`...FF`) under the engine's purge;
  `session-judge.py` names it `engine-free-corrupt-pointer`.
- The log before it: a restore of kept objects right after a load that put back some (`put back on 1 of 2`), not none.
- Since the fix: `reflect: field X.Y refused on 0x... (Class ...): not an object of that class` (judge rule `reflect-field-misfit`)
  names a module that still keeps a raw pointer; `... kept now hold another object ...: left alone` is the guard working.

# Fix in place

- `ue3::Kept` (`src/ue3/Runtime.hpp`): keep an object you will write later as `ue3::Kept{object}` and write only through
  `kept.Live()` (alive and still the same slot, class and name). `game/CameraShake` and `game/CameraCollision` do.
- `ue3::FieldRef` refuses a class's field on an object not of that class (`FieldRef::Fits`), logged once per field: a stale pointer
  can no longer corrupt another class's object through a field handle. Raw-offset writes are not covered: don't write through a kept
  pointer without a field handle.
- When it comes back: find the module from the refused field's name, keep its pointer as a `Kept`, and forget kept objects on a
  world change where nothing needs putting back (a load frees them anyway).

# Citations

- [research/le2-reload-crash.md](../../../research/le2-reload-crash.md): the crash, the decompiled frames, the fix and the 13-load run.
- [src/ue3/Runtime.hpp](../../../src/ue3/Runtime.hpp), [src/ue3/Handles.cpp](../../../src/ue3/Handles.cpp),
  [tests/ReflectTests.cpp](../../../tests/ReflectTests.cpp) `TestKeptIdentity`.
