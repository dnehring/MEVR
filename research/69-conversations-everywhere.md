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

Items 1, 3, 4 and 5 below are this note's first round (numbered as in the list given to the owner); item 2 of that list is the
sweep, which the save writer's story-beat rewind makes possible (research/59 "A save instead of open") and comes after.

## Status

"Read" means the game's code was read and the answer is recorded below. "Built" means the change is in the mod, compiles in all
three configurations and passes the offline tests. Nothing in this table has run in the game yet: that needs the game
folder, which the release session holds.

| # | Item | State | Result |
|---|---|---|---|
| 1 | Story safety of flowed (ambient) scenes | read; built; game check pending | A line's plot effects are on the path both kinds of conversation share. An ambient one can be cut short, and then loses the effects of the lines it never reached. 6 conversations had a chain our flags newly exposed to that: they stay conversations (64 flow, was 68). The game's barks can no longer start over a flowed scene. |
| 3 | Cutscene or world, decided at run time | data read; built log-only; game check pending | The packages can't say "Shepard isn't in this scene". Three run-time rules (pawn hidden; far from the owner and out of its sight; most shots framed from far away) name such scenes in the log and the survey's summary; they move a scene to the screen only with `worldAutoScreen = on`. |
| 4 | Keeping people in place at the source | read; built; game check pending | One function places every person on a stage mark, at the opening and at each line's facing track. A pre-hook withholds the place and lets the facing through. The undo after the fact stays as the net. |
| 5 | Walking and the level's triggers | read; built; game check pending | `SetLocation` begins and ends touches like any move, so a walk could fire a trigger. A step that would change which triggers Shepard's cylinder overlaps is refused. |
| 2 | The sweep: every conversation in its plot state | not started | 28 of 491 played (research/59) |

## Findings

Addresses are RVAs in `MassEffect1.exe` 2.0.0.48602, read with Ghidra (`tools/ghidra`, an isolated copy of the project while the
shared one is locked) and named through the natives dump (`%LOCALAPPDATA%\MEVR\dump\natives-LE1-*.csv`: a script native's thunk
names the vtable slot it calls). Vtables: `UBioConversation` `0x131EF20`, `ABioWorldInfo` `0x12969B0`, `ABioStage` `0x1321DC8`,
`UBioSeqAct_StartConversation` `0x1327CD0`. Nothing here copies decompiled code: behaviour is stated in words, with the address.

### 1. Story safety of flowed scenes

**Where a line applies its plot effects.** In the function that starts an entry (`0xC7DCD0`): after the speaker and listener are
resolved it runs the entry's `nStateTransition` through `ABioWorldInfo::ExecuteStateTransition` (slot `0x918`, `0xAB9850`) and
fires the Kismet conversation-node event for its `nScriptIndex` (`0xC7EE20`), and only then looks at the ambient flag. A reply's
transition and event are applied when the selected reply finishes (`0xC7F200`, slot `0x2F8`); an ambient conversation reaches
that by selecting its one reply itself (`0xC7E2B0`, slot `0x2E8`). A reply that auto-continues is passed over in both kinds
(the next entry is found by `0xC7D180` and started). The end (`UBioConversation::EndConversation`, `0xC7E630`, slot `0x2D8`)
fires the conversation-ended events for both. `IsAmbient` (slot `0x310`) is asked only around presentation: the GUI, the
camera, the stage's placement, the game mode. **So a flowed line that plays applies exactly what it would in a conversation.**

**What can end an ambient conversation early, and never a full one.**

- Distance: `UpdateConversation` (`0xC87830`) interrupts when the player is farther from the *current speaker* than the
  conversation's interrupt range (the start action's, else the world's 25 m; `-1` turns the test off).
- The owner gone, or the start action gone while the conversation was started by one.
- **Any other conversation starting.** `ABioWorldInfo::StartConversation` (`0xB65A50`) starts a newcomer when no conversation
  is current, or when the current one is ambient, which it first ends through `InterruptConversation` (`0xB60460`, slot
  `0x9F0`). A current conversation that is not ambient refuses every newcomer (the newcomer's action is marked failed).

An interruption ends the conversation through `EndCurrentConversation` (`0xB58790`): it sets the conversation's "ended" flag and
calls `EndConversation`. **The lines not yet reached never start, so their transitions and node events never happen.**

**What the level's script sees.** `BioSeqAct_StartConversation`'s update (`0xC969B0`) fires its first output when the
conversation carries the "ended" flag, which a natural end and an interruption both set; its second on failure (a re-run
detected, no conversation, the start refused); its third when the preload is done. So after an interruption the script goes on
as after a whole scene. (In the game the Garrus chain's end started the wheel conversation: research/59.)

**How exposed the flowed scenes were.** The scan now records each line's transition and node event
(`tools/conversation-scan/Scan.cs`: `transition`, `script` on entries and replies). Of the 68 flowed conversations: 36 carry no
plot effect on a flowed line; 10 only on the chain's first line, which the game applies the moment the conversation starts; 22
after the first line. Of those 22, 17 start on a line the game's own data already marks ambient, so the unmodded game plays
them as interruptible barks with the same effects: no exposure of ours. The other 5, and one more whose later effect sits on a
line that was already ambient inside a chain we would start, are the exposure: **6 conversations had a chain the game plays as
a conversation and our flags would have turned into interruptible barks with plot effects after the first line**: `jug20_wrex`,
`prc1_human_radio`, `sp123_kahoku`, `war20_faizhou`, `war40_juliannabaynham` (9 effects), `war50_krogan`.

**Built.**

- The flow table leaves those chains out (`conversation-scan.py`'s `plot_effects`, `flow()`'s `plotChains`; their entries are
  kept ordinary like a wheel play's, or a conversation reaching one would turn ambient there). `cw::FlowEntries` is regenerated:
  64 conversations, 249 entries (was 68, 284); unit test `TestFlowEntries`.
- `engine/ConversationHooks` pre-hooks `InterruptConversation`; `game/ConversationFlow` answers it
  (`[Cinematics] worldFlowProtect = 1`): while the current conversation is a scene of ours whose every start was an ordinary
  line before our flags (the game would have played it as a conversation), a newcomer whose every start is ambient by the
  game's own data (a bystander's bark, a squadmate's remark) is refused, as the game refuses every newcomer during the
  conversation that scene was authored as. Another scene of the level (one of ours, or a conversation) is let through:
  refusing it could lose that scene's start and its first-line effects. Log `convo flow: the bark X wanted to start over the
  flowed scene Y: refused`.

**What is left of the risk.** A flowed scene can still be cut short by distance, by another level scene, by a save and reload
or by leaving the area. With the six chains out, what is lost is spoken lines, not plot effects, and the script goes on as
after the whole scene. One consequence to look at in the game: a scene that follows a chain (the Garrus wheel conversation)
then starts wherever the player has walked to, and its stage puts Shepard on his mark under the dip from black.

**What the transitions are** (`conversation-scan <CookedPCConsole> out.txt --state-events 188,189,...`,
`tools/conversation-scan/StateEvents.cs`: the plot manager's `BioStateEventMap` read through Legendary Explorer's converter; LE1
keeps it in `BIOC_Materials` as `PlotManagerAuto.StateTransitionMap`, 3,751 events, and 84 more in `PlotManagerAutoDLC_UNC`).
Transitions 188 and 189, which make up 56 of the uses on flowed lines and sit on the game's own barks too, each set one
*local* integer of the object they run on (0 and 1): a bark's own bookkeeping, nothing of the story's state. The transitions
on the six chains kept as conversations are the story's: global plot booleans (Kahoku's, the krogan's and Fai Zhou's on Feros;
`jug20_wrex`'s was not looked up), four sub-states of one parent and an increment of a global integer by 8 on Juliana Baynham's chain, a global boolean of
Bring Down the Sky. So the rule keeps what matters and the bark bookkeeping flows.

**Not read yet.** The action's output links' names.

### 3. Cutscene or world, decided at run time

**The data can't say it.** A conversation always has a player (the stage's node 0 is his, `ABioStage::PlaceActors` reads the
conversation's player), and the scan's records have no field that separates "Shepard listens" from "Shepard isn't there". The
scenes between other people in the unmodded game are mostly matinees, not conversations: game mode 6, already a cutscene on
the 3D screen.

**So the rules are run-time ones** (`game/ConversationWorld`), each naming the scene with a reason:

- At the start (the stage has placed everyone by then): Shepard's pawn is hidden (`Actor.bHidden`).
- At the start: Shepard is farther than `worldAbsentM` (15 m) from the conversation's owner and out of its sight.
- While it plays: at least three shots, two thirds of them framed from more than 8 m from Shepard's eyes (the survey's
  measure of the framed set pieces, research/59).

**Built log-only.** `[Cinematics] worldAutoScreen = log` (the default) writes `convo: ...: no place for the player in this scene`
and puts the reason in the survey's summary line (`noPlace`, `autoScreened`); `on` moves the scene to the 3D screen (at the
start, or under the dip from black when the shots say so; people are then no longer kept, and the lines' facing tracks put
them back on their marks); `off` is silent. Log-only because the second rule would also name a radio call from far away,
which the world mode handles well (a voice, nobody to see), and none of the three has been seen against a real scene yet. The
sweep's summaries will say which scenes each rule names; then the default can move to `on`.

### 4. Keeping people in place at the source

**One function places everyone.** `UBioConversation::SetPositionRotator` (`0xC862D0`, slot `0x2C0`; the conversation, the
actor, a place, a rotation): it stops the pawn, moves it when the place isn't zero (and puts it back if the new place is not
valid, else drops it to the floor), sets its rotation and its controller's focus. Two callers put people on marks:

- `ABioStage::PlaceActors` (`0xC93B60`, slot `0x818`), called once, from the participants' setup (`0xC81300`) when a
  conversation that is not ambient and has a stage starts: the player on node 0, the owner on 1, the squad on 2 and 3, the
  other speakers on 4 to 7.
- A line's `BioEvtSysTrackSetFacing` track (`0xC95AF0`), per key: when the key names a stage node it calls the function with
  the node's place (research/59's "Anderson back on his mark at nearly every line"), then, if the key says so, again with a
  zero place for the facing alone.

This is why research/59's call trace saw no script call: both callers are native. A matinee's move track (someone walks in)
is another path and is not touched.

**Built.** `engine/ConversationHooks` pre-hooks the function; `game/ConversationWorld` answers
(`[Cinematics] worldStaySource = 1`): in the world mode, after the accepted opening, a placement of a person the mod keeps
runs with a zero place, the game's own facing-only path, so the person turns as the scene wants and doesn't move. The opening
placement is made before the mod knows the conversation, as before. The undo after the fact stays and should count zero; the
walk-to-marks prototype (`worldWalk`) turns the hook's answer off, since it learns each mark from the jump. Logs `convo: at N s
the game's placement of a person onto a mark D m away was withheld at its source`, the end's `N placements onto marks withheld
at their source`; the summary's `sourceKept`.

### 5. Walking and the level's triggers

**`SetLocation` is a real move.** `Actor.SetLocation` (`0x369530`) calls `UWorld::FarMoveActor` (`0x3FFCE0`) with its checks
on: the destination is tested for a free spot and for encroachment, and for an actor that collides the function then finds
the actors it now touches and tells both sides, as any move does. Shepard's pawn collides. So the walk's steps begin and end
touches: a step into a trigger volume would fire its Kismet touch, and a scene that trigger starts would be refused while the
conversation runs (above) and may never fire again; a step out of a streaming volume would stream its area out mid-scene.

**Built.** A step may not change which triggers Shepard's cylinder overlaps (`[Cinematics] worldMoveTriggers = 1`): the
triggers near the conversation are gathered once per conversation and every 5 s (every `Engine.Trigger` and `TriggerVolume`,
which covers `BioTrigger`, `BioTriggerVolume` and `BioTriggerStream`, whose collision is within the leash of the owner), and
each step asks the engine's own point check (`Actor.PointCheckComponent`, `0x505660`, with the cylinder as a box) at both
ends; a difference refuses the step, like a wall. Logs `convo: the walk's triggers: N within the leash of the owner`, `convo: at
N s the walk stops at X: the step would enter it`; the summary's `triggerHits`.

**Limit.** A touch event on an actor that is neither class (a mesh with a Kismet touch event) isn't covered; the sweep can
count how often a walk's touch set changes anyway.

## To check in the game

In this order, on the fake headset, when the game folder is free (saves backed up first, as always):

1. The hooks go in: `hooked UBioConversation::SetPositionRotator`, `hooked ABioWorldInfo::InterruptConversation`, and the
   address table's patterns hold.
2. Item 4: the medical bay conversation (research/59 measured 25 jumps undone there) and the Garrus wheel conversation:
   `sourceKept` above zero, `kept` zero, nobody moves, people still turn to the speaker; then `worldStaySource = 0` for the
   same scene to compare.
3. Item 5: `mevr convo walk` toward a streaming volume's edge inside a leash (the C-Sec hall has several): the walk stops
   with the log line, `mevr stream chunks` unchanged; `worldMoveTriggers = 0` to see the touch it prevents.
4. Item 1: a flowed scene with bystanders' barks in range (the Citadel's wards): a refusal logged, the scene's lines all
   spoken; the six chains left out play as conversations again.
5. Item 3: the survey with `worldAutoScreen = log`; read which scenes each rule names.

## Log

- 2026-11-09: the note written; intentions for items 1, 3, 4 and 5.
- 2026-11-09, later: the natives read for all four items (above). Built: the scan's plot effects and the flow table without
  the six plot-carrying chains; `engine/ConversationHooks` (placement, interruption) with their addresses and patterns in
  `engine/Addresses.hpp`; the placement provider, the walk's trigger guard and the three presence rules in
  `game/ConversationWorld`; the bark refusal in `game/ConversationFlow`. All three builds and the offline tests pass. Not yet
  run in the game.
- 2026-11-09, evening: landed on main (a96ce25). The game folder is not free: the release session holds it for the owner's
  QA of the 0.2.0 candidate (the players' build of 6bb3227 is installed), so the in-game checks wait for the owner's word, and
  the candidate goes back into the folder afterwards. Meanwhile the transitions were read (item 1, "What the transitions
  are"): the two on most bark lines are object-local bookkeeping, the six kept chains carry the story's.
- 2026-11-09, 17:00-17:50, the first in-game session (fake headset, the dev build over the 0.2.0 candidate, which is backed up
  with its hash checked):
  - **Checked:** both hooks install (`hooked UBioConversation::SetPositionRotator`, `hooked ABioWorldInfo::InterruptConversation`),
    61 of 61 addresses at their RVAs, none of the new reflection rows mismatch. The Garrus argument plays as barks and its wheel
    conversation runs to its end with the hooks in.
  - **Not checked yet: the placement hook withholding anything.** The Garrus conversation makes no per-line placement (0 withheld,
    0 undone), so it can't show it. The scene that can, the medical bay's wake-up (25 jumps undone in research/59), would not
    start: through `open` its level never streams in (the known gap), and through the save path too (below).
  - **The rewind and QuickLoad path, evaluated on that scene** (research/59 "Replaying a beat"; from John02_21):
    1. `QuickLoad` answers "The Save currently selected cannot be accessed" when the career has no quicksave the game itself
       knows (John02 had none): the launcher's freshly written quicksave did not load, in a session with levels opened from the
       console and after a clean restart alike. After the console's own `QuickSave` wrote the file, the same launcher command
       with `-ReplaceQuickSave` loaded at once, Shepard at the written place on the Normandy. The launcher should make that
       `QuickSave` itself (told to the save-writer session).
    2. No career has a save from before the wake-up, so there is no before/after pair to diff. The rewind was written by hand
       from the game's data instead: the conversation's transitions (`--state-events`) set booleans 4076, 5713, 6014 and 6177.
    3. With that save loaded, the world's `stream_wakeup` state reads enabled, the medical bay's own levels are loaded and
       visible, and the scene's level, `BIOA_NOR10_07wake_DSG`, is not among the 55 streaming levels and never loads. So what
       blocks this scene is level streaming, not the plot. A save keeps streaming states per map and the save tool can't set
       one; that is the next experiment. Until then the placement hook needs another scene with facing tracks (a staged
       Noveria conversation the launcher reaches, or any scene the sweep finds with `kept` above zero).
