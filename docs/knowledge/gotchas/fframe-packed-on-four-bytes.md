---
type: Gotcha
title: The script VM's FFrame is packed on 4 bytes, so its Node is at 0x14 in LE1 and LE2
description: engine/call_function and the crash report read FFrame's Node and Code at LE3's offsets (0x18, 0x28) in every game; in LE1 that read a pointer's high half joined to the next field, so every CallFunction skip failed silently and "only holsters draw" never refused a draw.
tags: [engine, script, reflection]
status: stable
timestamp: 2026-11-04T20:20:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-04T20:20:00Z }
bitten: [2026-11-04]
---

# Symptom

| when | what it looked like |
|---|---|
| 2026-10-25 to 2026-11-04 | `[Holsters] onlyHolstersDraw = 1` logged "only a grab at a holster draws a weapon" but never once "the game wanted to draw your weapon ... refused"; melee, grenades, the wheel, the holster key and the trigger all drew the gun |
| 2026-11-04 | `holsters: the pistol came out without a holster grab (right trigger 1.00 ...): not through DrawPrimaryWeapon, so it wasn't refused`, although the game's FireWeapon calls DrawPrimaryWeapon from script |
| 2026-11-04 | `mevr calltrace` named every caller `<not a listed object: 0x3A321B800000011B>`: a value whose low half (`0x11B`) is the high half of a heap pointer |
| 2026-11-04 | a crash report with `script frames ... (none found on this thread's stack)` while the game thread was deep in script |

Common to all: no error, no crash; the guarded frame read fails (a handled access violation) and the code behaves as if there were no frame.

# Cause

`FFrame` is FOutputDevice's vtable pointer and three DWORDs (four on LE3), then `Node`, `Object`, `Code`, `Locals`,
`PreviousFrame`, declared under `#pragma pack(push, 4)` (LExSDKv2's `Frame.hpp`). With no padding after the DWORDs, `Node` is at
**0x14** in LE1 and LE2 (`Object` 0x1C, `Code` 0x24, `PreviousFrame` 0x34) and at 0x18 only in LE3. Our code assumed natural
alignment ("three DWORDs, still ending at 0x18") for all three games, so in LE1 `Code` was read from 0x28 (its high half and
`Locals`' low half), the dereference faulted, and `TrySkip` ran the call as normal.

# Check

`mevr calltrace 5 drawprimary fireweapon` (game/call_trace) and pull the trigger with the weapon away: the trace must name the
caller (`<- SFXGame.SFXGameModeBase.FireWeapon`), not `<not a listed object: ...>`; and the log must say `holsters: the game wanted
to draw your weapon (Function SFXGame.SFXGameModeBase.FireWeapon): refused`.

# Fix in place

`engine::adapter::kFrame` ([src/engine/GameAdapter.hpp](../../../src/engine/GameAdapter.hpp)): the offsets per game, used by
`engine/call_function` (the skip's Node and Code, the trace's Node) and `plugin/CrashBreadcrumbs` (the crash report's frames).
Any other engine struct read by offset: check the SDK's `#pragma pack` before assuming alignment.

# Citations

- [research/19 "The trigger's draw"](../../../research/19-body-holsters.md): the trace, the bytecode and the measurements.
