---
type: Gotcha
title: "The LE console prints less than UE3's: what works and what is silent"
description: "`scale set` and `set` take effect, but `getall` and the streaming stats print nothing, so a `set` cannot be read back; `god`, `ghost` and `pause` are toggles."
tags: [le1, console, test-loop]
status: stable
timestamp: 2026-10-30T19:53:43Z
generated: { by: claude-code/fable-5.1, at: 2026-10-30T19:53:43Z }
bitten: [2026-10-24, 2026-10-28]
---

# Symptom

A command sent through `tools/console.ps1` returns nothing, and you cannot tell whether it took. An A/B built on
`set` says nothing about the setting, only about the symptom.

# What is known

| command | effect |
|---|---|
| `scale set <key> <value>` | sets a `[SystemSettings]` key live: `DynamicShadows`, `MotionBlur`, `FoliageDrawRadiusMultiplier`; not `DetailMode` or the LOD distance scales. The game rewrites BIOEngine.ini after it. The value already stored: [SystemSettings precedence](system-settings-precedence.md). |
| `set <class> <property> <value>` | works: `set SFXPawn_Player Location (X=..,Y=..,Z=..)` moves Shepard (`Actor.SetLocation` through ProcessEvent refuses the player). Nothing prints. |
| `getall <class> <property>` | prints nothing, so a `set` cannot be read back: the 2026-10-28 LOD and mip-resident A/Bs show only that those settings did not change the flicker, not that they took. |
| `LISTSTREAMINGTEXTURES`, `STREAMINGSTATS`, `stat streaming` | print nothing and draw nothing visible. |
| `show <category>` | toggles a draw category: a frame's cost by category. |
| `god`, `ghost`, `pause`, `TOGGLEOCCLUSION`, `UMBRA OCCLUSIONCULLING` | toggles: a second call undoes the first (a script that sends `god` as a no-op leaves it off). `pause` freezes the world but keeps rendering. |
| `mevr density 2000` | ours: the densest mesh cells. |
| `exit` | quits the game (`tools/launch-game.ps1 -Quit`). |

# Check

`console.result` next to the request holds what the command printed (the mod's `game/ConsoleRequest`, log `console:`).
For a `set`, verify through a side effect (a snapshot, the levers line, the frame time), never a read-back.

# Citations

[1] [research/35, attempts 40-43 (measuring without a person) and 49](../../../research/35-performance-attempt-log.md).
[2] [src/game/ConsoleRequest.cpp](../../../src/game/ConsoleRequest.cpp).
