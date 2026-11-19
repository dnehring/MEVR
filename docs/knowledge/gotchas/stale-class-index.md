---
type: Gotcha
title: The reflection's class index outlives a load, and a level's freed class faults the next lookup by name
description: ue3::Reflection indexed classes by name and rebuilt the index only when GObjects' count changed; a save's reload freed a level's own class and reused its slot at the same count, so FindClass followed the freed class's garbage outer and ME2 crashed in the console's `mevr instances`; entries are now checked and the index dropped on a world change.
tags: [engine, reflection, crash]
status: stable
timestamp: 2026-11-17T12:30:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-17T12:30:00Z }
bitten: [2026-11-17]
---

# Symptom

| when | what it looked like |
|---|---|
| 2026-11-17 | ME2 vanished on the fake headset (Save 4, Freedom's Progress, reloaded once in the same process) right after `mevr activate` of a drone factory; every later console request timed out ("No result within 10 s"). `crash-breadcrumbs.txt`: an access violation in `LE2MEVR_mod.1.dll` reading `0xFFFFFFFFFFFFFFFF`, stack `ue3::Reflection::PathName` <- `FindClassNamed` (the "class ... not found; using ..." note) <- `FindClass` <- the console's `InstancesLe2` (`mevr instances SFXGame.SFXPawn_AssaultDrone`). |

# Cause

`ue3::Reflection` keeps a multimap of every class by lower-case name and rebuilt it only when a lookup missed and GObjects had grown
(`indexedCount_ == ObjectCount()` meant "nothing new"). A level's own classes live in the level's package (ME2's
`SFXPawn_AssaultDrone` in `BioD_ProFre_200Vorcha`): a load frees them with the level and the engine reuses their GObjects slots for
the new level's objects, so the count can come back the same while the index still holds the freed classes. A lookup that asked for
the class in another package (`SFXGame.SFXPawn_AssaultDrone`) fell back to "another package's class of that name", the freed one,
and `PathName` walked its outer chain through freed memory. Any lookup by name could have taken the freed class on any game; the
console's `mevr instances` was simply the first to name one after a reload. `powers.ps1 -Drone` makes the same calls; it ran on a
freshly launched game.

# Check

The crash report's stack names `FindClassNamed`/`PathName`; or the log has `class X.Y not found; using <a level's path>` with a
level package. In tests: `TestStaleClassIndex` in [tests/ReflectTests.cpp](../../../tests/ReflectTests.cpp) frees a level class, reuses
its slot and asks again.

# Fix in place

[src/ue3/Reflect.cpp](../../../src/ue3/Reflect.cpp) `FindClassNamed` checks each entry before using it: alive (`IsAlive`: the slot
still holds it, not pending kill), still a class, still that name; one that isn't is skipped and the index rebuilt on the second pass
whatever the count says. `Reflection::ForgetClassIndex` drops the index, and `ue3::runtime::NoteWorld` calls it whenever the world
changes; the mod's tick passes `engine::World()` every tick on every game (Mod.cpp). Handles (`ClassRef`) resolve once and keep their
class: a handle on a level's own class has the same problem and must be resolved again after a load.

# Citations

- Lane MEL's ME2 melee run, 2026-11-17: `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt` of that run (pid 149212) and
  [research/20 "The trilogy's one path"](../../../research/20-melee-and-grenades.md).
