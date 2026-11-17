---
type: Gotcha
title: A key binding's exec function reaches the script through ProcessEvent, never through a script call
description: A button's command (an exec like StormOn, TryAcquireCover, or a timer an OnHold/OnTap alias set) is called from native code through ProcessEvent, so a CallFunction skip, answer or post-handler on it never runs; the sprint's faster run never started on ME1 for ten days.
tags: [engine, script, input, hooks]
status: stable
timestamp: 2026-11-16T19:00:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-16T19:00:00Z }
bitten: [2026-11-14, 2026-11-16]
---

# Symptom

| when | what it looked like |
|---|---|
| 2026-11-06 to 2026-11-16 | ME1's sprint as a faster run (`[Hands] sprint = speed`, the default) never ran: holding A gave the game's own sprint (the storm posture, its camera, the gun lowered); the heartbeat said `sprint: a faster run; 0 presses taken` and, once, `the game sprints by a route other than StormOn` |
| 2026-11-14 | ME2's cover snapping: a skip of `BioPlayerController.TryAcquireCover` (A's alias) can't be built in the script hook; lane F blocked one level down instead (`FindPlayerCover`'s answer inside it, research/23 section 7.1) |

No error, no warning: the handler is registered and simply never called for the button's route.

# Cause

`engine/call_function` hooks `UObject::CallFunction`: one script function calling another. A binding's command is executed by
native input code: a plain command through the console exec path (`CallFunctionByNameWithArguments`, then ProcessEvent), and
BioWare's alias prefixes (`OnHold 0.3 StormOn`, `OnTap ...`, `OnRelease ...`) parse the time and set an engine timer on the
controller whose object is the game mode (ME1's native alias parser, the `ONHOLD` branch, calls `SetTimer(time, false, name, object)`);
timers fire through ProcessEvent. Neither passes CallFunction. `StormOn` has no script caller in any of the three games, so a
CallFunction skip on it could never fire. The host's ProcessEvent hook has post-handlers only: nothing can skip such a call.

# Check

- `mevr calltrace 5 storm` while holding A: each `call trace:` line names its kind (a script call or a ProcessEvent) and its
  caller; a function seen only as a ProcessEvent is reached from native code.
- Read the function's callers in the script (the scanners of research/23 section 7): an exec function with no script caller is
  only ever reached from native code.

# Fix in place

- `game/Sprint` (2026-11-16): StormOn runs and its intent is undone right after, in **post-handlers on both hooks**
  (`engine::process_event::AddPostHandler` and `engine::call_function::AddPostHandler`): `BioPlayerController.bWantsToStorm = 0`
  before the movement reads it. research/trilogy-sprint.md.
- For a binding's exec, intervene in what it calls from script (an answer or frame post-handler on the inner call, as lane F does
  with `FindPlayerCover`), or undo its effect in a ProcessEvent post-handler. A skip handler on the exec itself does nothing.

# Citations

- [research/trilogy-sprint.md](../../../research/trilogy-sprint.md): the three games' StormOn, the binding, the measurement.
- [research/23 section 7.1](../../../research/23-cover.md): ME2's TryAcquireCover through ProcessEvent.
- [src/game/Sprint.cpp](../../../src/game/Sprint.cpp), [src/engine/CallFunction.hpp](../../../src/engine/CallFunction.hpp).
