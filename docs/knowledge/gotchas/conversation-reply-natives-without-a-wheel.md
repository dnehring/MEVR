---
type: Gotcha
title: BioConversation's reply functions read its lists unchecked, so a call without a standing wheel ends the game
description: GetReplyText, SelectConversationEntry and their kin index the conversation's current reply list and reply nodes with no checks; called between lines or while a conversation is being ended, they read a missing or freed list and the game ends.
tags: [engine, conversations, thespian]
status: stable
timestamp: 2026-11-13T20:30:00Z
generated: { by: claude-code/opus-5.5, at: 2026-11-13T20:30:00Z }
bitten: [2026-11-06, 2026-11-13]
---

# Symptom

| When | What was seen |
|---|---|
| 2026-11-06 12:23 | `mevr convo pick <slot>` between two lines: the game ended inside `SelectConversationEntry`'s script (research/59). |
| 2026-11-13 11:26, 11:33 | Another session's runner ended a conversation from the console (`mevr convo end`, `-EndAfter`) during a line with `[Thespian] on = 1`: the game ended 57 and 82 ms later. The crash report's script frame is `SFXGame.BioConversation.GetReplyText` on the conversation, called from `LE1MEVR.asi` (Thespian's `ReadLive`); the native faults at `MassEffect1.exe+0xC80E72` reading 0x10 or `+0xC80BF4` reading a freed block. |

The game just vanishes or writes `crash-breadcrumbs.txt`; the log's last lines are the console's `convo: EndConversation
called true` or a `convo pick`.

# Cause

The reply functions of `BioConversation` (`GetReplyText`, `GetReplyParaphrase`, `GetReplyCategory`, `GetReplyGUIStyle`,
`SelectConversationEntry`...) take a position in `m_lstCurrentReplyIndices` and go from there to the current entry and
the reply node in `m_ReplyList` without checking that any of it is still there. They are safe only while the wheel
stands. While a conversation is being ended, the wheel handler can say `m_bDisplayingWheel` for a moment with the
current list still filled while the lists behind it are taken away, so a reader that checked only the flag and the
list's count called into freed memory.

# Check

- The crash report's script frames: a `BioConversation` reply function at #0, called from our ASI.
- The log a few lines before: `console: mevr convo end`, a runner's early end or `convo pick`.
- Since 57e298c3: `thespian: a wheel without its lists (current line N, M replies in the list): not read` means the
  guard below refused a half-gone wheel.

# Fix in place

- Thespian's `ReadLive` (`src/game/Thespian.cpp`) calls the reply natives only while the wheel is shown, the
  conversation is alive and has a current line (`m_nCurrentEntry >= 0`) and every current reply index is in
  `m_ReplyList`; a turn begins only on a wheel that stood three ticks on the same conversation, line and replies
  (`gSteady`).
- `mevr convo pick` only with the wheel up and on a used slot (research/59).
- Anything new that calls a reply function: the same checks first, on the game thread, never in the tick a conversation
  was ended.

# Citations

- [research/70 section 10](../../../research/70-thespian-mode.md) (the 2026-11-13 crashes and the guard)
- [research/59](../../../research/59-conversations-in-world.md) (the 2026-11-06 pick between lines)
- [the crash reports](../../../research/36-crash-breadcrumbs.md) (how to read them)
