---
type: Gotcha
title: The console answers on a loading screen, so a load that never ends looks like a working game to a script
description: A Mass Effect stuck on its loading picture keeps ticking and answering console commands (mevr state, mevr instances) for as long as it is left; a run that polls the console never notices, and on 2026-11-20 the owner saw the hang before the lane did. Judge a load by whether the world is drawn (the host's world-drawn clock in testloop.status, tools/game-watch.ps1), never by console answers.
tags: [tooling, test-loop, sessions, loads, le3, le2]
status: stable
timestamp: 2026-11-20T12:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-20T12:00:00Z }
bitten: [2026-11-19, 2026-11-20]
---

# Symptom

| wording | where |
|---|---|
| "the load never finished" with the docking officer's conversation held; the lane kept waiting | ME3's slice, 2026-11-19 20:56 (research/le3-conversations-in-world.md) |
| "game's not loading, this was a problem yesterday" (the owner, watching the desktop) | lane FPC-ME3, 2026-11-20 11:22-11:28: the cab ride after Save_0090's docking officer |
| `mevr state` and `mevr instances` answered normally for 6 minutes; the snapshot showed the Citadel loading picture; the heartbeat said 239 fps, `objects=135284` unchanged; `render levers: ... scene renders per frame 0 (x300)` | `Logs\LE3MEVR.log`, `%LOCALAPPDATA%\MEVR\snapshots` |

# Cause

A loading screen is a game that runs: the game thread ticks, the console runs commands, Present shows the loading picture
at full rate. Nothing a console command returns says the level never arrived. The hang itself had another cause (a
`scale set` sent during the load: [scale set during a load](me2-scale-set-during-load.md)); this gotcha is about why our
runs didn't see it.

# Check

`tools/game-watch.ps1` prints the state now: `drawn`, `no-world` (presenting with no scene render: a loading screen or a
movie), `blocked` (the game thread stopped writing its status: a hard map load or a hang), `no-frames`, `starting`,
`no-game`, and how long it has lasted. By hand: `testloop.status`'s `"world"` block (`msSinceScene`, `unixMs`).

# Fix in place

- The host writes a `"world"` block into `testloop.status` every second from the game thread in development builds
  (`src/plugin/TestLoop.cpp` `WorldJson`, `render_levers::WorldDrawn`: the last Present and the last Present with a scene
  render, all three games).
- `tools/game-watch.ps1 -UntilWorld [-TimeoutS 120]` after any action that can start a load (a reply picked, a travel, a
  save's load): exit 0 once the world is drawn, exit 2 with evidence (`%LOCALAPPDATA%\MEVR\game-watch\<stamp>...`: the
  status, the logs' tails, a snapshot, crash reports) when it isn't by the timeout, exit 4 if the game went away.
- `tools/game-watch.ps1 -Watch` runs until stopped and prints a `STUCK` line once a state without a drawn world outlasts
  `-LimitS` (120 s): the coordinator runs it under its Monitor during lanes' game runs.
- `tools/console.ps1` and `tools/steer-common.ps1`'s `Steer-Ask` warn (`game-watch: ...`) on every command once no world
  has been drawn for 30 s.

# Citations

- [src/plugin/TestLoop.cpp](../../../src/plugin/TestLoop.cpp) (`WorldJson`), [src/plugin/RenderLevers.cpp](../../../src/plugin/RenderLevers.cpp)
  (`WorldDrawn`), [tools/common.ps1](../../../tools/common.ps1) (`Get-MevrWorldVerdict`), [tools/game-watch.ps1](../../../tools/game-watch.ps1).
