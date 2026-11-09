<!-- SPDX-License-Identifier: MIT -->
# 69: First-person conversations for every conversation: the robustness work (2026-11-09)

The owner (2026-11-09, after the walk during the Garrus conversation): "what work is remaining to make this system more robust
so that we could enable it for all conversations with high confidence? (obviously some which don't have Shepard at all should be
the typical cut scene) especially since we have access to the decompilation", then: "since this is a large feature I want you to
have a section just for this work ... and I want you to update it as you make progress."

This note is that section. [research/59](59-conversations-in-world.md) is how the feature was built and measured (the world mode,
keeping people in place, the scan of all 491 conversations, the launcher, flowing conversations, walking). This note is what
stands between that and `[Cinematics] conversations = world` being the default for everything: what could go wrong, what the
game's own code says about it, what was changed, and what is proven. It has a status table, kept current, and a log.

## What "high confidence" means

The mode can be the default when all of these hold, each backed by the game's code or a measurement, not by a guess:

1. **No story damage.** A conversation shown in the world, or flowed as barks, leaves the plot exactly as the game's own
   presentation would: every plot flag set, every script output fired, nothing lost by walking away, a save or a second scene.
2. **Never a wrong presentation.** A scene composed for a camera somewhere else (a set piece, a scene Shepard isn't in) is a
   cutscene on the screen; that decision comes from the conversation's own data when it starts, not only from a list of names.
3. **Nobody snaps, nothing authored is undone.** People stay where they stand because the game is stopped from putting them on
   marks, not because a jump was noticed afterwards; a move the scene means (someone walks in, falls, is carried off) happens.
4. **Walking can't break the level.** The player's steps during a conversation never start another scene, stream a level out or
   skip a trigger the level's script counts on.
5. **Measured on all of them.** Every conversation played once in its plot state by a sweep, each summary inside limits
   (research/59's survey has 28 of 491).

Items 1 to 4 are this note's first round (the owner's "1, 3, 4, 5" of the list given in the session, numbered as there below);
the sweep is item 2 of that list, which the save writer's story-beat rewind makes possible (research/59 "A save instead of
open") and comes after.

## Status

| # | Item | State | Result so far |
|---|---|---|---|
| 1 | Story safety of flowed (ambient) scenes | intentions written | - |
| 3 | Cutscene or world, decided at run time | intentions written | - |
| 4 | Keeping people in place at the source | intentions written | - |
| 5 | Walking and the level's triggers | intentions written | - |
| 2 | The sweep: every conversation in its plot state | not started | 28 of 491 played (research/59) |

## Intentions

Addresses are RVAs in `MassEffect1.exe` 2.0.0.48602, read with Ghidra (`tools/ghidra`); the conversation class's vtable is at
`0x131EF20` (research/59 "Flowing conversations"). Nothing here copies decompiled code: findings are stated as behaviour, with
the address.

### 1. Story safety of flowed scenes

**The risk.** `worldFlow` flags a chain start's entries `bAmbient`, and the game then plays that start through its ambient path:
no conversation mode, no camera, the player free. If that path treats a line differently from the full path in anything but
presentation, a flowed scene could leave a plot flag unset or a script output unfired, and a quest would hang hours later.

**Questions for the code.**

- Where does a line apply its plot effects (`nStateTransition` and its parameter on entries and replies, the conditional's
  side effects, the journal and codex updates), and is that code reached the same way for an ambient line?
- What does `BioSeqAct_StartConversation` fire when an ambient conversation ends, and when it is interrupted: the same output
  links as a full one? (In the game the Garrus chain's end did start the wheel conversation: one data point.)
- What does an interruption do (`UpdateConversation`'s distance test, a second conversation starting, a level transition): are
  the remaining lines' plot effects applied, skipped or deferred?
- What does a save during an ambient conversation keep? (The game refuses saves in conversation mode; ambient is on foot.)
- Skipping: the full path lets the player skip a line; an ambient line can't be skipped. Anything keyed on the skip?

**What will be built from the answers.** If the plot path is shared: record that with the addresses and close the item. If an
interruption drops effects: the flowed chains default to never interrupted (`worldFlowLeashM = -1` exists) and, if a second
conversation can still cut one off, the mod finishes the chain's effects or refuses the flow for chains that carry plot effects
(the scan knows which lines have transitions: add them to its record, so the flow table can leave those out or mark them).

### 3. Cutscene or world, decided at run time

**The risk.** 21 set pieces go to the 3D screen by a generated list of names (`cw::IsSetPiece`). A conversation the scan
misjudged, one a mod adds, or one where Shepard isn't there at all (a scene between other people, a stand-in double) would be
shown from Shepard's place: a view of nothing, or of the wrong spot.

**Questions for the data and the code.**

- How does the game itself know whether the player takes part: the conversation's player pawn, the stage's marks, a speaker
  tag, a hidden pawn? What do the scan's 491 records say (no player line, no player mark, the owner far from the start)?
- At the start of a conversation, what can the mod read cheaply and reliably that the scan's verdict was computed from
  (authored camera tracks per line, the stage type, move tracks on the player's group)?

**What will be built.** A decision at the conversation's start from live data, with the generated list as one input:
Shepard absent, hidden or doubled: cutscene. A fallback while it plays: when the shots turn out far from the player or not
seeable for a stretch (the survey's runtime rule), switch to the screen under the dip from black, once, and stay there.

### 4. Keeping people in place at the source

**The risk.** Today a jump past 30 units within a frame and under 6 m is noticed after the game made it and undone with
`SetLocation` before the draw. A threshold can't tell a mark snap from an authored move, and an undo is a second teleport.

**Questions for the code.** Which native puts the conversation's people on the stage's marks, and when (at the start, at each
camera cut, per line)? Is it one function with the actor and the mark as arguments, so a pre-hook can skip it per actor, as
`engine/cover_acquire` does for cover? What else does it do that must still happen (facing, look-at targets, animation state)?

**What will be built.** A hook on that native: in the world mode after the accepted opening, placements of people are skipped at
the source (counted and logged); the reactive undo stays as the safety net and its count should fall to zero. Authored moves
(matinee move tracks) are a different code path and stay.

### 5. Walking and the level's triggers

**The risk.** The walk moves Shepard's pawn with `Actor.SetLocation` inside a 3 m leash. If that fires touch events as a normal
move does, a step during a conversation could enter a trigger volume: start another scene, stream a level, fire a Kismet touch.
If it doesn't, the opposite: a trigger the player walked into silently never fires, and firing it late (when the conversation
ends inside the volume) may or may not happen.

**Questions for the code.** What does `AActor::SetLocation` (the script native) do about touching actors: the same touch and
untouch notifications as a physics move, or none? What does the game do with a `BioTriggerStream` or a trigger touched in
conversation mode?

**What will be built.** Whichever the answer: the walk must not change what the level's script sees. Either a step whose end
lies in a trigger volume the start isn't in is refused (a wall, in effect), or touches are suppressed during the walk and made
consistent at the end. Decided by the reading.

## Findings

(Filled in per item as the reading and the changes land.)

## Log

- 2026-11-09: the note written; intentions for items 1, 3, 4 and 5.
