<!-- SPDX-License-Identifier: MIT -->
# Thespian mode: the player speaks Shepard's lines

A plan, 2026-11-12 (the owner's idea of the same day). Facts about the game are from the class layouts and the mod's
existing conversation code; what has not been seen in the running game is marked **[verify]**.

**Status (2026-11-13):** built through phase 4 and run in the game on the fake headset with a synthetic voice; off by
default (`[Thespian] on`, the menu's Voice page). What was built, how it differs from this plan, the measurements and the
checks still open are in [research/70](../research/70-thespian-mode.md) (section 8, the guide, and section 9, the log).
Left: phase 5, the owner's acceptance in the headset (notes/headset-checklist.md, "Thespian mode"); the "said in your
own words" tier is not built (the alignment decides between a wheel's few, differently worded replies). Differences
from the plan below: the panel is rows after the owner's concept art (3.5), rows never grow or move, there is no hover
from gaze yet, and on the 3D screen the panel stands under the screen while the game's wheel stays on it.

## 1. The experience

In a conversation the player is Shepard, voice included. When it is Shepard's turn:

1. A small microphone mark shows that the game is listening. No button, no gesture.
2. Where the wheel was, the replies stand as **script cards**: the wheel's short label and the opening words of the line
   Shepard would say. Looking or pointing at a card unfolds the whole line.
3. The player starts reading one of them aloud. Within a few words the game knows which one: that card unfolds into a
   teleprompter, the words said lit, the words to come waiting, the other cards dimmed.
4. The player doesn't have to be word-perfect or finish. When they stop, or reach the line's end, the reply is taken,
   **Shepard's recorded line is not played**, and the other person answers at once, as in a real exchange.
5. Investigate questions are spoken straight from the top level: no sub-menu to open first.
6. The stick and the pointer still work. A reply picked by hand plays with Shepard's recorded voice (the understudy).

Everything runs on the player's machine with the recognizer and models the squad's voice orders already use.

## 2. What the game already gives us

Seen in the class layouts and already used by `game/ConversationWorld` unless marked.

| Need | The game's own piece |
|---|---|
| The replies on offer right now | `BioConversation.m_lstCurrentReplyIndices`; `WaitingForReply()`, `NeedToDisplayReplies()` |
| The full line Shepard would say | `BioConversation.GetReplyText(nIndex, bParse)` (the reply node's `srText`, gender and tokens resolved) |
| The wheel's short label | `GetReplyParaphraseText(nIndex)` |
| Where a reply sits, and its kind | `GetReplyCategory(nIndex)`, `GetReplyGUIStyle(nIndex)` (charm, intimidate...), the reply node's `bIllegal` |
| The wheel's slots | `BioSFHandler_Conversation.m_aReplyLocations[6]`, `m_bSlotsUsed[6]`, `m_bDisplayingWheel` |
| Investigate | a GUI grouping, not dialogue nodes: `m_aInvestigateLocations[6]`, `m_bDisplayInvestigateSubMenu`, `GetInvestigateReplyFromWheelLocation`, `ChangeConversationMenu` |
| Taking a reply | `BioSFHandler_Conversation.SelectConversationEntry(location)` (what `mevr convo pick` calls); under it `BioConversation.SelectReply` / `QueueReply` |
| Not playing Shepard's line | `BioSFHandler_Conversation.SkipConversation()` / `BioConversation.SkipNode()`: the player's own skip, so plot effects are safe |
| When the wheel comes up | `m_fShowRepliesOffset`, `m_fRemainingVOLength`: the wheel shows shortly before the other person's line ends |
| Silencing one speaker | `engine/VoicePlace`: a source whose audio component's owner is a named actor gets our gain (0 for the player's pawn) |
| Speech to text, streaming | `ASI\voice\MEVR-Voice.dll` (Moonshine, one thread): partials every `intervalMs`, word timestamps, key-term biasing, a level meter; Moonshine's own voice-activity detection splits lines |
| A second opinion | `plugin/VoiceModel` (llama.cpp under a grammar), asked today only when the order rules are unsure |
| Fuzzy words | `core/vr/SquadParser`'s `Tokens`, `Similarity`, `Lemmas`/`SameWord` (2026-11-09) |
| A drawn panel in the room | RmlUi contexts (`menu::Ui`), `plugin/Swapchains`, the game pointer's ray and fingertip |
| Scenes without a person | `tools/conversation-from-save.ps1`, the fake headset, `voice.request`'s WAV feed |

Two consequences shape the design. The candidates are **known exactly and are few** (at most six on the wheel plus the
investigate topics), so this is a choice among known scripts, far easier than the open orders the squad parser handles.
And taking a reply and skipping a line are the game's own calls, so the mode adds no new route through the dialogue.

## 3. Design

### 3.1 Perform first, then commit

The wheel is up and the game waits, as it does for the stick. The player performs; when the choice is settled the mod
calls the pick and, in the same tick, mutes the player's pawn and skips Shepard's line. The other person's answer starts
within a frame or two of the commit.

*Alternative considered:* take the reply as soon as it is recognized and let the game play Shepard's line silently under
the player's voice. The game's line and the player's delivery never have the same length, so the other person would
answer over a slow reader. Kept as a later experiment only.

### 3.2 The microphone is open only on Shepard's turn

A stream opens when the replies are on offer and closes at the commit. This keeps the other person's recorded voice out
of the recognizer (it could otherwise match a card's words through open headset speakers), bounds the CPU cost to the
moments the game is idle anyway, and makes the privacy story simple: the mark is lit exactly while audio is read, and
nothing leaves the machine. The wheel appears a moment before the other person's line ends **[verify how long, per
`m_fShowRepliesOffset`]**; words heard before that line's end count only once they also continue after it.

### 3.3 Knowing when the player is done

Generic end-of-speech detection has to wait out a long pause to be sure. Here the script is known, so the end can be
called early:

- **The line's end was reached** (its last two or three content words matched): commit after 150-250 ms of silence.
- **The player stopped part-way** with the choice already clear: commit after about 0.9 s of silence.
- **The choice is not clear yet:** keep listening; nothing is ever committed on a guess.

The first case is what makes the reply land like conversation. All three delays are settings, tuned in phase 2 from
recordings.

### 3.4 Two tiers of understanding

**Tier 1, script alignment (rules, about a millisecond).** Every partial transcript is aligned against each candidate's
full line and its label:

- Words are normalised as the order parser does (lower case, base forms, numbers) and compared exactly, then fuzzily,
  then by sound (a phonetic key), because the recognizer writes "Sarin" for Saren.
- The alignment is monotone and tolerant: skipped words, repeated starts and inserted fillers cost little.
- Each word is weighted by how much it tells the candidates apart on **this** wheel. "What can you tell me about" is
  worth nothing when three questions start with it; "Protheans" settles it.
- A candidate leads once it has enough distinguishing weight and a margin over the runner-up; the card unfolds then. It
  may still change while the player speaks. It commits only at an end as in 3.3.
- The recognizer is biased per turn toward the rare words of the cards on offer (`MevrVoiceSetKeyterms`): names and
  places are exactly what it mishears, and here we know which ones are coming.

**Tier 2, meaning (a model, only when tier 1 is unsure after the player stopped).** For a player who says it in their
own words ("so what's the deal with the geth?" for the card *The geth?*). Two candidates to measure in phase 0, the
better one ships:

- a small sentence-embedding model on the ONNX Runtime the voice DLL already loads (a few milliseconds a turn), cosine
  similarity between what was said and each card's line and label;
- the llama.cpp tier under a grammar that allows only a card's number or "none" (about a second a turn today; the
  order model's fine-tune is for order lines, so the base model and a small second adapter are both tried).

Tier 2 never overrules a confident tier 1, and "none" is a legal answer: the cards stay up and the mark shows it didn't
catch that.

**How much must be said** is the player's setting:

| Setting | Commits when |
|---|---|
| Relaxed | the card is identified (the label alone, or a paraphrase, is enough) |
| Performer (default) | identified, and about half of the line's content words were said |
| Method | identified, and nearly all of the line was said in order |

One-word lines ("Yes.", "Goodbye.") are a special case in every setting: they count only when they are the whole
utterance, so a "yes" inside other talk never picks a reply.

### 3.5 The teleprompter

A panel of ours (RmlUi, the menu's look, one swapchain through `plugin/Swapchains`) where the wheel's quad stands today;
the game's wheel art is cut out while the mode is on, and the game's wheel keeps working underneath for the stick.

- **Before speaking:** up to six cards in the wheel's own arrangement (right side top to bottom, left side), each with
  the label and the line's first words in bold as the cue. Charm and intimidate keep their colour; an unavailable reply
  is greyed and not a candidate. Investigate topics are a column of smaller cards. Gaze or the pointer unfolds a card.
- **While speaking:** the leading card grows into the script in large type. Words said are lit, the next word is
  marked, the rest wait. Between partials the lit edge advances at the player's measured pace so it doesn't jump.
- **The microphone mark:** idle, hearing (it moves with the level), understood (the card's colour), didn't catch that.
- **After the commit:** the panel folds away in about 0.15 s.

Its look is checked offline first, as the menu's is (`mevr-menu-preview` renders of each state).

**The owner's concept art (2026-11-12)** sets the layout; the picture itself is kept locally
(`scratch/thespian/concept-ux-2026-11-12.png`, not in the repository). What it shows, and what we take from it:

- **One wide glass panel between the hands, below the speaker's face.** The person talking stays fully visible above
  it; the panel is at chest height, tilted toward the eyes, in our menu's glass look (dark blue glass, cyan edges, cut
  corners). Taken as is. It replaces "where the wheel's quad stands": same anchor, wider and lower.
- **A header row:** the speaker's emblem, name and role on the left, the line they just said on the right. Taken: the
  other person's current subtitle moves into the panel while it is up (name where the game gives one).
- **One full-width row per reply, stacked:** an icon and a short coloured tag on the left, a divider, the whole line in
  large white type on the right. Taken, with the game's own data: the tag is the wheel's label (the paraphrase, which is
  also the short thing a player may say instead of the line), coloured by the reply's tone (below); the line is the
  script. This replaces "cards in the wheel's arrangement": rows read faster than a ring and every line is visible
  without unfolding.
- **One row lit with a bright outline.** In the art it is a selection made first ("Speak selected line"). Ours is not
  a step the player takes: the lit row is the card the matcher is leading with while the player talks (before that, the
  row the gaze or the pointer rests on, as a reading aid only). The other rows dim while one leads. The leading row is
  the teleprompter: its said words bright, the next word marked, the rest dim; **rows stay where they are** (this
  replaces "the leading card grows", which would move text under the reader's eyes).
- **A footer bar:** the microphone in a ring, a prompt, a live waveform and "Listening...". Taken as the microphone
  mark: idle "Read a line aloud", hearing (the waveform moves with the level), understood (the row's colour), "Didn't
  catch that".
- **A hint strip under the panel** (speak, select with controller, scroll). Taken as one line of help for the hand
  pick: the stick or the pointer chooses a row and A takes it (Shepard's recorded voice then plays). No button starts
  the listening: the microphone is open whenever the panel is up.

Tone colours and icons, from the reply's category and GUI style (the art's four tones are examples, ME1's are these):
agree and friendly blue (the upper right of the wheel), default white, disagree and hostile red-orange (the lower
right), investigate cyan with the document icon, charm blue with a mark, intimidate red with a mark, unavailable grey
and not a row to read.

More rows than fit: the main replies (at most six) are always shown; investigate topics follow under a thin divider in
smaller type, and past eight rows in all the group scrolls (the art's "Scroll options": the right stick, or the
pointer). A line longer than two rows of text is cut with an ellipsis until it leads.

### 3.6 Investigate, flattened

The investigate replies are ordinary replies that the handler files under its sub-menu (phase 0: confirmed in the data,
they are the entry's offers with the category `REPLY_CATEGORY_INVESTIGATE`), so they are candidates from the start
**[verify that `m_lstCurrentReplyIndices` holds them while the main wheel shows]**. Speaking one commits it
through the handler's own path (open the sub-menu, pick the slot) in consecutive ticks. After the answer the game
returns to the hub as it always does, and the cards come back with the topics already asked marked.

### 3.7 Lines Shepard says without a choice

**Phase 0: there are none.** In LE1's 490 conversations no reply that continues by itself has text and no entry is
spoken by the player (research/70 section 3): every line Shepard says is a picked card. The rest of this section is kept
only as the record of what was considered.

Some of Shepard's lines are not picked: a reply that continues by itself, or a line the conversation gives the player
between the other person's. Three ways to handle them, in the order they'd be built:

1. **Shepard's recorded voice** (first build): nothing changes for these lines.
2. **Dub-along:** the line is muted and scrolls on the teleprompter at the recording's own pace; the player reads along.
   No recognition, no risk to the conversation's timing.
3. **Hold until performed:** the conversation waits for the player as it does at the wheel. This needs the line's timer
   held back, a new write into the conversation's state **[verify]**; only if dub-along feels wrong in the headset.

Phase 0 counts how many such lines the game has, which decides how much this matters.

### 3.8 When recognition is not possible

The mode stands down, with one line in the log and the plain wheel, when: no microphone or recognizer; the game's text
is not English (the recognizer is English-only); the conversation is one of the set pieces played on the 3D screen and
the owner prefers those untouched (a setting; the mode itself works on the screen too, with the panel under it).

## 4. Architecture

```
game thread (mod)                         host                               voice DLL
-----------------                         ----                               ---------
game/Thespian                             plugin/Thespian                    Voice.cpp
  reads live replies, texts, slots   -->    opens/closes the stream    -->     open-mic stream
  core/voice/ScriptMatch  <-- partials <--  takes lines + activity     <--     partials, words, voiced/silent
  core/vr/Thespian (turn state machine)     plugin/ThespianDraw (RmlUi)
  pick + skip, mutes the player's pawn      teleprompter quad, mic mark
  asks tier 2 when unsure            -->    plugin/VoiceModel or embedder
```

- **`core/voice/ScriptMatch`** (new, game-independent, unit-tested): normalisation, the phonetic key, the weighted
  monotone alignment, per-candidate progress (which words are lit), the lead and margin rule. The word helpers move out
  of `SquadParser` into a shared `core/voice/Words` so both use one copy.
- **`core/vr/Thespian`** (new, unit-tested): the turn's state machine (off, armed, hearing, leading, committed,
  fallen back), the end-of-turn rules of 3.3, the strictness rules of 3.4, the one-word rule.
- **`game/Thespian`** (mod): the live replies each tick while the wheel is up; publishes the cards and progress to the
  host; commits; names the player's pawn to `voice_place` with gain 0 for the skipped line; console `mevr thespian
  [cards | say <words> | on | off]` (the typed stand-in for speech, as `mevr say` is for orders).
- **Host interface** (next HostServices version; host and mod deploy together): `setThespian(State)` with the cards'
  texts and per-card progress, and the listen request with a mode (push-to-talk or open).
- **Voice DLL** (interface v3; host and `ASI\voice` deploy together): an open-mic listen that stays up across
  Moonshine's line splits; a voiced/silent signal with the time since the last voiced frame; key terms per listen.
  The recognizer stays on one thread. A shorter partial interval during a turn (150 ms) is tried against the cost.
- **`plugin/Thespian` + `ThespianDraw`** (host): the panel, its placement from the conversation's wheel placement, the
  mic mark; `assets/menu/thespian.rml` / `.rcss`.
- **Settings** (`[Thespian]` in `ASI\LE1MEVR.ini`; the menu's Voice page): `on` (default 0), `strictness`,
  `unchosenLines = voice | dub`, `handPick = voice | silent`, the end-of-turn delays, `tier2 = embed | llm | off`.
- **Logs** `thespian:` (one line per turn: the cards, what was heard, each card's score, why and when it committed,
  the gap from the last word to the answer's first frame); snapshot `"mod"."thespian"`, layer `thespian`.

## 5. The gap, budgeted

From the player's last word to the other person's voice:

| Step | Target |
|---|---|
| Silence needed after a finished line | 150-250 ms |
| The last partial that confirms it (already running) | 0-150 ms |
| Pick, mute, skip on the game thread | 1-2 frames |
| The game starting the answer's line (its own preload) | **[measure]** |
| **Total, finished line, tier 1** | **about 0.3-0.5 s** |
| Stopped part-way | about 1.0 s |
| Tier 2 by embedding | adds a few ms |
| Tier 2 by the language model | adds about 1 s (rare path) |

The turn log records every one of these, so the tuning is done on numbers.

## 6. Risks and edge cases

| Risk | Handling |
|---|---|
| Two cards with the same spoken line (ME1 has them) | Phase 0 counts them. Same line and same outcome: one card. Same line, different outcome: both cards stay, and the label or the stick decides. |
| The recognizer mishears names | Per-turn key terms, the phonetic key, and weights that never rest on one word. |
| The room talks (someone else, a cough, the player thinking aloud) | Needs distinguishing words in order; the one-word rule; nothing commits on a guess. |
| The other person's voice in the microphone | Stream opens on Shepard's turn; early words must continue past the line's end; the game's audio is already ducked during listens. |
| Skipping a line that carries an animation or camera beat | It is the game's own skip, which players use on every line; a reply flagged unskippable (`bUnskippable`) is committed without the skip and plays muted. |
| A pick at the wrong moment crashes the script (seen 2026-11-06) | Commits only while `m_bDisplayingWheel` and the slot is used, the existing guard. |
| The recognizer's cost | One thread, only during the wheel; measured in phase 2 against frame timing. |
| A player who doesn't want to speak just now | The stick and pointer are never taken away; a held grip could pause listening for a turn if asked for. |
| Long silent thinking | No timeout: the wheel waits as it always has. |
| LE2 and LE3 | Interrupts and a different wheel; out of scope until LE1's mode is accepted. |

## 7. Phases

Each ends with something measured; the next starts only if it holds.

**Phase 0: the data and the matcher, offline (no game running).**
- Extend `tools/conversation-scan` to dump every wheel: each reply's line, label, category, style, and where it leads.
  Census: wheels, cards per wheel, line lengths, identical lines on one wheel, unchosen Shepard lines, unskippable ones.
- Build `core/voice/ScriptMatch` with unit tests and a bench over every wheel in the game, with three kinds of input:
  the lines through text-to-speech and the recognizer (`tools/voice-tts-corpus.ps1`); **Shepard's own recordings from
  the player's game files** through the recognizer (local only, never committed: thousands of real performances of
  exactly these lines, both voices); and generated rewordings for tier 2.
- Report: the share of wheels decided correctly after 3, 5, 8 words and at the full line; wrong commits (must be near
  zero); which tier 2 wins and what it adds.
- *Exit:* tier 1 right on at least 97% of full lines with under 0.5% wrong commits, or the design changes here.

**Phase 1: in the game by typing.** `game/Thespian` reads the cards and `mevr thespian say <words>` commits through the
match, pick, mute and skip. Run on the fake headset over a sample of scenes from validation saves, investigate topics
and a charm reply included. Settles the **[verify]** items of sections 2 and 3.
- *Exit:* 20 scenes of different kinds, every reply taken as typed, no Shepard audio heard (checked with
  `mevr-audio-loopback`), no crash, plot effects identical to a stick pick (save diff with `tools/le1-save.py`).

**Phase 2: the open microphone and the end of the turn.** The DLL's open listen and activity signal, the turn state
machine, the three end rules. Unattended first with WAV feeds (`voice.request`), then the owner's microphone.
- *Exit:* the measured gap within the budget of section 5 on the feeds; no commit from the other person's voice played
  through speakers next to the microphone.

**Phase 3: the teleprompter.** The panel, cards, unfolding, lit words, the mark; offline renders first, then the
headset. The menu's Voice page rows.
- *Exit:* the owner's headset session: readable at conversation distance, the lit edge feels in step.

**Phase 4: the rest.** Tier 2 in the game, strictness, dub-along for unchosen lines, repeated topics marked, the 3D
screen's placement, the stand-downs of 3.8, the changelog and the controller guide's text.

**Phase 5: acceptance.** A played hour across the Citadel with the mode on; `tools/conversation-session.py` extended to
report each turn's gap and outcome.

## 8. Decisions for the owner

Recommended defaults in bold; none blocks phase 0.

1. ~~Lines Shepard says without a choice~~: settled by phase 0, there are none.
2. A reply picked by hand: **Shepard's recorded voice**, or silence?
3. How much must be said by default: Relaxed, **Performer**, or Method?
4. The 3D-screen set pieces: **on, with the panel under the screen**, or first person only?
5. May the bench read Shepard's recordings from the local game files (kept local, as game art is)? **Yes** is assumed.
