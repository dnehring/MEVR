---
type: Gotcha
title: ME3's area map closes by an animation its B starts, and every B before the end starts it again
description: The rail's Close sent B every 12 ticks; each one restarted the area map's 0.24 s close animation, so the map stayed up through every back-out and the Mission Computer came up only after the back-out had given up; on ME3 a back-out now gives each screen one B and waits for the next screen.
tags: [ui, me3, omni-tool]
status: stable
timestamp: 2026-11-17T13:10:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-17T13:10:00Z }
bitten: [2026-11-16, 2026-11-17]
---

# Symptom

| when | what it looked like |
|---|---|
| 2026-11-16 | Lane OM2, Save 31: "the map page took about a second to answer B"; six back-outs 4 ticks apart left it up. Read as a slow screen: the back-outs were spaced to 12 ticks and capped at 8. |
| 2026-11-17 | Lane CTL, Save 31: `mevr omni close` from the area map logged `backing out of 'Areamap'` 8 times, then `close the game's screens not done: the game stayed in its GUI mode after 8 backs out`; the Mission Computer (`MenuBrowser`) came up 63 ms later and stayed. A player who opened the map from the wrist and pressed Close stayed in the game's menus. |

# Cause

`SFXSFHandler_AreaMap.HandleInputEvent` answers B's release (`BIOGUI_EVENT_BUTTON_B_RELEASE`, 37 on ME3) with `OnBeginClose`:
the movie's ActionScript close animation, about 0.24 s, whose end calls `CloseMap` (back to the Mission Computer, or out of the GUI
mode when the map was opened on its own). A B before that end starts the animation again. At the fake headset's rate 12 ticks are
190-200 ms, so every back-out restarted the close and the map went 0.25 s after the last one, once the back-out had stopped. A
slow game (a few frames a second, 12 ticks longer than the close) closed it at the first B, which is why it seemed to work on some
runs. The other screens the rail opens (the Mission Computer, the squad record, the journal) have no such script and go 20-50 ms
after their B. Also: the game passes a release on only after its press; a lone `GuiKey(37)` does nothing.

# Check

The log's `omni-tool: backing out of 'Areamap'` more than once in a row, and `'Areamap' gave way to 'MenuBrowser' N ms after its B`
(N about 240). By hand: `mevr omni map`, then `mevr guikey 16 37` (gone in 0.24 s) against `mevr guikey 16 37 w w w w w w w w w w
16 37 ...` (gone 0.24 s after the last B that came before the close ended).

# Fix in place

`vr::game_menu::StepBackOut` (src/core/vr/GameMenuRequest.cpp, unit-tested in
[tests/GameMenuBackOutTests.cpp](../../../tests/GameMenuBackOutTests.cpp)), used by ME3's Close and page switch in
[src/game/GameMenuRequest.cpp](../../../src/game/GameMenuRequest.cpp): a screen (its movie object and tag) gets one B; the next B
goes to the screen that takes its place 250 ms after it has the focus; the same screen gets another only 1.5 s after its B. ME1 and
ME2 keep their B every few ticks. A new ME3 screen with a scripted close: check its close time against `settleMs`/`resendMs`.

# Citations

- [research/le3-native-gui.md "Close from the area map"](../../../research/le3-native-gui.md): the script, the measurements and
  the runs on Save 31 (lane OMX).
- [research/le2-vrik.md](../../../research/le2-vrik.md): lane CTL's run that found it.
