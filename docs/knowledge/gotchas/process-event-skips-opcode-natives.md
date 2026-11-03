---
type: Gotcha
title: ProcessEvent skips every native that has an opcode of its own
description: UObject::ProcessEvent returns at once for a UFunction whose iNative isn't 0, so Trace, Destroy, SetLocation, SetOwner and their kind did nothing when the mod called them; ue3::runtime::Call now clears the index for its call.
tags: [engine, script, reflection]
status: stable
timestamp: 2026-11-03T11:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-03T11:00:00Z }
bitten: [2026-10-24, 2026-10-31, 2026-11-03]
---

# Symptom

| when | what it looked like |
|---|---|
| 2026-10-24 | `Actor.SetLocation` through ProcessEvent "returns without moving the player"; blamed on FarMoveActor, "it is our call, unresolved" ([research/35](../../../research/35-performance-attempt-log.md) attempt 42) |
| 2026-10-31 | `SetLocation`/`SetRotation` "return false" on the cockpit's actor; blamed on FarMoveActor's encroachment test, worked round by writing `Location` ([research/47](../../../research/47-cockpit-in-game.md) "First run") |
| 2026-11-03 | `Actor.SetOwner` "changed nothing"; `Actor.Trace` "never hits" (Sights, the Mako's aim, Throwables, the aim reticle); `Actor.Destroy` false 458 times, nothing destroyed, hidden leftovers piling up |

Common to all: the call "succeeds" (no error, `Call` returns true), and every out value and the return value stay what they were before.

# Cause

LE1's `UObject::ProcessEvent` (`0x12DD20`), like stock UE3's, runs a function only if `UFunction.iNative` (u16 at +0xDC) is 0. A
native declared `native(N)` has an opcode of its own: the script VM calls it through `GNatives[N]` and ProcessEvent is not meant
for it, so it returns at once. Among them: `Trace` 277, `Destroy` 279, `FastTrace` 548, and in stock UE3 `SetLocation` 267,
`SetRotation` 299, `SetCollision` 262, `SetBase` 298, `SetOwner` 272, `SetPhysics` 3970. Natives without an opcode (`Spawn` here,
`SetHidden`, every `event`) and script functions run normally, which is why most calls worked and these looked like individual
refusals.

# Check

The function's `iNative`: in Ghidra the decompiled exec's comment says `iNative N`; in the game `ue3::Reflection::NativeIndex(f)`.
Or call the function on something with a known answer: `mevr vehicle trace` traces 53 m down from above the Mako (it must hit
`Terrain_*`) and `FastTrace` up into the sky (must be true).

# Fix in place

`ue3::runtime::Call` ([src/ue3/Runtime.cpp](../../../src/ue3/Runtime.cpp)) sets the function's `iNative` to 0 for its call
(journalled `ue3.Call.iNative`) and puts it back after; the VM never reads the field, it dispatches by opcode. Every mod call goes
through `Call`, so nothing else needs to change. A call made some other way (a raw `CallProcessEvent`, a host-side ProcessEvent)
still meets the old refusal.

When a native "does nothing" again: check `iNative` before reading its body, and check the call goes through `ue3::runtime::Call`.

# Citations

- [research/47 "Why Trace never hit"](../../../research/47-cockpit-in-game.md): the reading and the measurements.
- [research/35 attempt 42](../../../research/35-performance-attempt-log.md): the first time, SetLocation on the player.
