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
| 1 | Story safety of flowed (ambient) scenes | read; built; the first protection failed its game check (log, 18:39); rebuilt as "hold" (the owner's decision) and **checked in the game** (log, 19:35) | A line's plot effects are on the path both kinds of conversation share. An ambient one can be cut short, and then loses the effects of the lines it never reached. 6 conversations had a chain our flags newly exposed to that: they stay conversations (64 flow, was 68). The game's barks can no longer start over a flowed scene. |
| 3 | Cutscene or world, decided at run time | data read; built log-only; game check pending | The packages can't say "Shepard isn't in this scene". Three run-time rules (pawn hidden; far from the owner and out of its sight; most shots framed from far away) name such scenes in the log and the survey's summary; they move a scene to the screen only with `worldAutoScreen = on`. |
| 4 | Keeping people in place at the source | read; built; **checked in the game** | One function places every person on a stage mark, at the opening and at each line's facing track. A pre-hook withholds the place and lets the facing through. In the medical bay's wake-up scene: 28 placements withheld, 0 left for the undo after the fact (research/59 had 25 undone there). |
| 5 | Walking and the level's triggers | read; built; **checked in the game** (9 triggers found by the medical bay, the walk stopped at the first one's edge) | `SetLocation` begins and ends touches like any move, so a walk could fire a trigger. A step that would change which triggers Shepard's cylinder overlaps is refused. |
| 6 | Authored scenes (the game walks Shepard, frames the scene) | **proposal** ("Authored scenes: a treatment per kind of track"): about thirty moments in the game move Shepard by a track; nothing built | the council moment read from the level's script |
| 2 | The sweep: every conversation in its plot state | planned ("The sweep: plan"); **phase 1 built** ("The sweep: progress"): the game slot, the catalogue's levels, predictions, the judge, the flowed scene's summary line | 28 of 491 played by the survey (research/59), 5 more by hand on 2026-11-09 |

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

## Scene rules: toward a scene's description as data

The owner, deciding the newcomer question (2026-11-09): hold it until the scene ends, and "we likely would codify this as a
part of our new scene builder so we can try to avoid these one-off fixes. Is that possible?" Yes, and the hold is built that
way. No scene builder exists in the repository yet (nor in the other worktrees or the shared notes), so this section is what
it would take over.

**What "codified" means here.** A fix is a one-off when the game module decides something about one situation in engine
code. It is a rule when (1) the decision is a named function in `core/vr/ConversationWorld` over plain facts, unit-tested
without the game, (2) the game module only gathers the facts and carries the decision out, and (3) the rule's choice per
scene can come from data. The first such rule is the newcomer's:

- `cw::NewcomerRule { Through, Refuse, Hold }`, `cw::DecideNewcomer(facts, scenes)` over `cw::NewcomerFacts` (is the current
  conversation a flowed scene of ours; is the newcomer the scene itself, a bark by the game's own data, started by a level's
  action), and the held starts' queue (`cw::HoldStart`, `cw::ReleaseStart`: first come first fired, a start held twice counted
  once, eight at most, dropped after three minutes). Tests: `TestSceneRules`.
- `game/ConversationFlow` reads the facts inside the interruption hook and acts: Through lets the game end the scene; Refuse
  and Hold keep it; a held start (the newcomer's `BioConversation.m_pKismetStart`) is fired again with
  `SequenceOp.ForceActivateInput(0)` once no conversation, or only a bark of the game's own, has been playing for a second
  (so the level's own follow-up to the scene, like Garrus's wheel conversation, plays first).
- Today the choice for "another scene of the level" is one setting for every scene: `[Cinematics] worldFlowNewcomers = hold |
  refuse | through` (hold by default; `worldFlowProtect = 0` turns the protection off). A scene builder's table would give
  `DecideNewcomer` a scene's own value instead.

**What a flowed scene is, corrected.** The first protection asked whether every start of the conversation was an ordinary line
before our flags. `sta70_garrus` has three starts: two we flag and one the game itself marks ambient (a later one-liner), so
the test said no and the argument was unprotected: that is why the banter cut it. Now a scene is ours when the conversation
is ambient and **the line it is on is one we flagged** (`OurScene`).

**The other candidates for the same treatment**, all decided in the game module today and all per scene in nature: where a
scene is shown (world, screen, flow: today the generated `cw::IsSetPiece` and `cw::FlowEntries`, plus the run-time presence
rules), whether its people are kept or placed (`worldStaySource`), the walk's leash, clearance and trigger rule, the interrupt
range, and the opening (`accept` or `keep`). One generated table per conversation, with the scan's verdict as its default and
hand overrides beside it, is the scene builder's natural core; the mod would read every per-scene choice from it.

**Not known yet.** What a refused newcomer's action does in between: the game marks its conversation failed and fires the
action's second output, as it would have during the original conversation; then our re-fire starts it over. Whether any level
wires that output to something that should not happen twice is for the sweep.

## Authored scenes: a treatment per kind of track, not per scene (proposal, 2026-11-09)

The owner: "when Shepard meets Anderson before the council, I would want to approach Anderson and then he'd tell me to
follow him and we'd both walk to the podium with the council already talking, like it is intended to play out. I don't know
how we'd do those scenes in a scalable way."

**What that moment is in the game's data** (`conversation-scan --sequence BIOA_STA70_02captain_DSG:SarenCouncil`): using
Anderson fires the level's own script, not a conversation: the council's streaming state on, cinematic mode on, the player
and squad positioned, then **a 7.5 s matinee** with a move track and an animation track on the group `Player`, the same on
`Captain`, a moving camera and a director track with a fade (Anderson's "The hearing's already started. Come on." is a
voice-over action); a black screen; that Anderson deactivated; a teleport into the council chunk; and there the
conversation `sta90_council_saren` on its own stage, with authored cameras on 14 of its 31 lines and **no move track on
anyone**. So the walk the owner remembers exists, as a short cinematic before the conversation; today the mod shows both
on the 3D screen (a cinematic mode, then a set piece by the scan's camera rule).

**Where authored content lives, counted** (the scan's `matinee` records: every `SeqAct_Interp` of a level script):

| Where | Count | Of interest |
|---|---|---|
| a conversation's lines (a matinee per line) | 34,912 matinees in 277 conversations | Shepard moved by a track in 4 conversations, an NPC in 22, authored cameras on a quarter of the lines or more in 18 |
| level-script matinees with a director track, outside animated cutscenes | 323 | **27 move Shepard by a track** (368 s in all: the walk to the council 7.5 s, becoming a Spectre 17.8 s, Benezia's entrance, Liara's rescue, the Thorian's first sight ...), 75 animate him or move an NPC, 221 are cameras only |
| animated cutscenes (`ANIMCUTSCENE` sequences) | 164 matinees | nobody to place: the 3D screen |
| the rest (doors, lifts, props) | 5,362 | not scenes |

So the moments where the game itself walks Shepard are about thirty in the whole game. That is a list one can test whole,
and too many to stage by hand one by one.

**The scalable way: one treatment per kind of track, chosen by rule; a per-scene table only for exceptions.**

| What the game authored | First person |
|---|---|
| a camera's move, a director's cut, depth of field, FOV | ignored: the head is the camera |
| an NPC's move track or animation | played as authored, in the world (already so inside conversations: smooth moves are let through; to be confirmed at full length) |
| Shepard's move track | never drags the player: **he walks it himself** (the NPC walks ahead, the player follows; the scene goes on and its wheel waits for him). A setting may offer being carried along the path with the comfort vignette |
| a teleport of the player, and the fade or black screen that hides it | **withheld when the place it leads to is the same space** (below): the player walks there and nothing is cut. **Kept, as a dip from black, only when the composition changes** |
| a stage's placement of Shepard at a conversation's start | the same rule: his mark is where the player is wanted; he walks in (outside the leash a step may only bring him nearer, the rule the walk already has), no cut. Today's dip at the opening (`worldOpening = accept`) becomes the case for a changed composition only |
| cinematic mode switched on | not by itself a reason for the screen |
| the conversation after it | in the world when Shepard has a body and a place there (the presence rules), whatever its cameras do |

**No dip unless the composition is completely different** (the owner, 2026-11-09: "the dip would just require us to not
walk as long"). Measured for the council: Anderson stands at (-6906, -7285, 61816); the script's teleport ("Put player at
top of stairs", its own comment) takes the player **7.5 m on and 2.9 m up**; the hearing's stage, where the conversation's
opening placement then puts him, is at (-10068, -7077, 62107), **32 m from Anderson in the same hall**. Two cuts, both
within one walkable space: the black screen there hides nothing but a shorter walk. So a teleport (or an opening
placement) is withheld when all of these hold, checked at run time, and kept under a dip otherwise:

- the same space: the place can be walked to from where the player stands (the walk's own floor and wall checks along the
  way; a first bound of some tens of metres), in the level that is loaded;
- the same moment: the script puts no time between the two (a "morning after" is another composition);
- the same people: who is with the player before is there after (the council chunk has its own Anderson at the podium:
  the one who walked is swapped for him on arrival, which the black screen did in the original).

Another area, another time, another cast, or Shepard put where nobody can walk (a seat, the Mako): the dip.

**The game's teleports, counted** (the scan's `teleport` and `event` records): 691 `SeqAct_Teleport` in level scripts, 237
with the player among their targets. Only 33 of those name a placed destination (the others find it at run time: by tag,
from a list), and for 26 of them an event of the player's own doing in the same sequence says where he stands when it
runs: **8 take him under 5 m, 9 between 5 and 15 m, 5 between 15 and 60 m, 4 over 60 m** (Bring Down the Sky's combat
start 36 m, a side world's base 30 m and 18 m up, Therum's first troop drop 20 m, Zhu's Hope's first creeper 9 and 20 m,
Kirrahe's camp 13 m, the two Citadel Tower moments 7.5 m). So most scripted teleports of the player that can be measured
from the data are short hops inside one space; the rule has to measure the rest when they run (a log line per teleport
of the player: from, to, the distance, whether a walk could make it), which is the first thing to build.

With those, the council moment plays as the owner describes without anything written for it: use Anderson; he says his
line and walks ahead up the stairs; the player walks with him; the hearing's conversation starts as the script starts it,
so the council and Saren are already talking as he climbs; its wheel waits until he stands at the podium. No cut anywhere.

**What it needs** (none of it built):
1. A world mode for a level-script matinee (game mode 6 with Shepard's pawn in it), as conversations have: today every
   cinematic mode goes to the 3D screen. The presence rules decide which matinees qualify.
2. The player's own move track withheld (the placement hook's way: find the native that applies a move track to the
   player's pawn), and the level script's teleport of the player and its black screen withheld when the rule above says
   the same space. The walk during a cinematic mode (today's walk is for the conversation mode).
3. An escort: the NPCs the clip moved with Shepard walk on to where they are wanted (the walking-to-marks prototype) and are
   swapped for their counterpart there. The teleports of the whole game and how far each takes the player are to be
   counted from the scan first (a `teleport` record): that list says how many moments fall on each side of the rule.
4. The scan's verdict "screen" split: authored cameras alone no longer make a set piece; `worldSetPieces = world` and a run
   of the 18 framed conversations say which have no place for the player.
5. The stage's marks in the scan (also wanted by the predictions).
The thirty moments are the test list; the sweep's runner can fire them like any start once it knows their events.

### The natives behind an authored moment (read 2026-11-09 by a subagent in the isolated Ghidra project and from the game's
script packages; **its claims, not yet checked by a hook in the game**)

- **A move track places its actor through one native: `AActor::MoveWithInterpMoveTrack` 0x4B75F0** (actor vtable +0x558,
  the same pointer for Actor, Pawn, InterpActor and the SFXGame pawns; `this` is the actor moved; arguments the track, the
  track instance, the time, the delta). `UInterpTrackMove::UpdateTrack` is an empty stub in the game; the track instance's
  `InitTrackInst` 0x3E31A0 saves the actor's physics and sets PHYS_Interpolating, and each tick `physInterpolating`
  0x4BCC00 finds the track and calls 0x4B75F0, which takes the keyed place, sets the pawn's desired rotation and calls
  `UWorld::MoveActor` 0x402E30 (not FarMoveActor, no direct write). `SeqAct_Interp.SetPosition` and the cinematic skip go
  through the same function. No BioWare subclass of the move track. So **a pre-hook on 0x4B75F0 that leaves the call out
  when `this` is the player's pawn is the one place to withhold Shepard's authored walk.**
- **What withholding it leaves to handle:** the pawn stays in PHYS_Interpolating for the matinee (no walking physics, no
  input movement: the mod's own steps must move him, as in a conversation); with the matinee's `bResetPositionOnFinish`
  the track instance's end (`TermTrackInst` 0x3EFA90) puts the actor back at its start place; a skipped cinematic places
  actors at their end keys through the same function; the pawn's animation is the matinee's (`BeginAnimControl`), not his
  own locomotion, until it ends; the same function sets his rotation.
- **A matinee ends by time alone** (`USeqAct_Interp::StepInterp` 0x3EEE60: position past the length); it never looks at
  where anyone is.
- **`SeqAct_Teleport` is script**: `USequenceAction::Activated` 0x5188D0 calls the target's handler by ProcessEvent:
  `Engine.Controller.OnTeleport` (which calls the pawn's as a script call) or `SFXGame.BioPawn.OnTeleport`, which sets
  `BioWorldInfo.m_bHasPlayerTeleported`, syncs the camera's yaw, and ends in `Pawn.OnTeleport`: `SetLocation(destination)`,
  the rotation, `OnTeleportArrived`. Two ProcessEvent pre-handlers (the controller's when it is the player's, the pawn's
  when it is his) cover a scripted teleport of the player. Not covered: `SeqAct_SetLocation` (native, 0x516A20).
- **`BioSeqAct_BlackScreen` is script** (a latent action: `Activated` shows or hides the GUI manager's black screen with a
  fade, `UpdateOp` returns when no fade runs): with its `Activated` left out the action completes at once.
- **`BioSeqAct_BioToggleCinematicMode` is script**: `PlayerController.SetCinematicMode` sets `bCinematicMode`, hides the
  pawn if the action says so, ignores move and look input, hides the HUD; **only with `bCinematicInputMode` does it enable
  game mode 6**. `SFXEngine.IsInCinematicMode` is true during a conversation that is not ambient or while the local
  controller's `bCinematicMode` is set.
- Not determined: whether a matinee's "Player" group is the pawn or the controller in each case; how often scripts move
  the player with `SeqAct_SetLocation`; the director track's own fade (`InterpTrackFade`).

## The sweep: plan

The owner (2026-11-09): "How can we scale up evaluating the scenes?", then "make a plan for how you would do this fan out and
validation". Today a scene costs two to four minutes of one person's attention and the game folder is borrowed by hand. The
plan turns that into a batch that runs unattended, judges itself, and hands people only the exceptions.

### What fans out, and what cannot

- **Scenes fan out inside one game.** 491 conversations; 182 are barks that never enter the conversation mode (sampled, not
  swept); 309 remain: 262 judged fit for the world, 26 marked "check", 21 for the screen. 64 conversations have a flowed chain.
- **The game does not fan out.** One machine, one GPU, one Steam instance, one SteamVR null driver, and the folder is shared
  with the release candidate and the owner's QA. So there is exactly one **runner**, and it runs when the owner isn't playing.
- **Everything without the game fans out freely:** building the tools (several sessions at once, each on its own files),
  the predictions from the game's data, and above all **triage**: reading the exceptions of a finished pass is read-only work
  over logs, summaries and snapshots, which parallel agents can split by area or by kind of failure.

### The layers of validation

| Layer | What it answers | Covers | Needs the game | Cost |
|---|---|---|---|---|
| 0. Predictions from data | which scenes carry which risk (plot effects after the first line, triggers within the leash of the marks, cameras far from Shepard's mark, no mark for someone, authored walks) | all 491 | no | minutes |
| 1. Smoke | does the scene start, run to its end, and end inside every limit | all 309, and a sample of barks | yes | under a minute each once batched by area |
| 2. Branches | the lines the smoke run's path did not reach, where the data says they differ in kind (a facing track, a camera, a walk, a plot effect) | scenes with such lines | yes | one more run per uncovered kind |
| 3. Rules | each scene rule on a scene that exercises it (placements withheld, the trigger guard, a newcomer held, a presence rule naming a scene) | a fixed list, grown with every rule | yes | ten minutes: the canary set |
| 4. Headset | does it feel right | about ten scenes, one per kind, picked from the pass | the owner | one sitting |

**Limits a run is judged by** (from the mod's `convo: summary` line; a scene outside any of them is an exception):

- it started, and ended by itself (no time-out, no crash, no hang, the mod's log still advancing);
- `kept` 0 and `drawn` 0 (nothing was undone after the fact, nothing jumped in a drawn frame);
- the lines played match the path the driver took through the scan's graph, and every wheel it opened was answered;
- `eyeToViewM` under 0.8, or the scene is marked "after a cutscene" by the data;
- `noPlace` empty unless the verdict is screen; for a screen scene, that it went to the screen;
- for a flowed chain: every line of the chain spoken, and the follow-up began after it (**needs a summary line the flow
  module does not write yet**: a chain plays in game mode 0, where today only "plays as barks" is logged);
- then, from the second pass on: no difference from the baseline that the change under test doesn't explain.

**Validating the validator.** The canary set runs before every pass and must come out as known: the wake-up scene (28
withheld, 0 undone), the walk stopping at `BioTriggerVolume_1`, Chorban held over the Garrus argument, two barks refused, one
set piece on the screen. And one run with a rule switched off on purpose (`worldStaySource = 0`: the wake-up scene must then
fail on `kept`), so a judge that passes everything is caught.

### Phases

1. **Tools, no game (parallel; about a day).**
   - `tools/game-slot.ps1 take | return`: what was done by hand four times on 2026-11-09 as one command (refuse if the game
     runs; back up the ASI folder, the Win64 folder, the engine ini and the saves with hashes; deploy; fake headset; and back,
     verifying every hash and the candidate's SHA-256; a lock file other sessions can see).
   - The catalogue's map for each start taken from the persistent levels whose streaming lists name its package (the wake-up
     scene's was wrong: its level is the docked Normandy's, under `BIOA_STA00`).
   - The judge: `tools/conversation-sweep.py judge <results>` (the limits above, a baseline, a report with the exceptions
     grouped by kind) and the layer-0 predictions as one more table of the scan.
   - The flow module's end-of-chain summary line.
2. **The reach matrix (one evening with the game).** For each of the game's dozen areas: does `open` reach gameplay; if not, does a
   save written for that level from John00 (end of the game, six maps kept) or John02 (after the Citadel's arrival); which
   streaming volumes or states show each chunk. Research/59 found Feros, Virmire and Therum unreachable by `open`. The
   result is a table, and the list of scenes no route reaches yet, which is the honest size of the gap.
3. **The area runner (two to three days, most of it without the game).** `tools/conversation-survey.ps1` grown into a
   resumable batch: per level, load once; per chunk, stream; per conversation, put its plot back (the rewind from the
   conversation's own transitions), stand Shepard, fire, drive with replies chosen for coverage rather than at random, one
   snapshot at a fixed moment, the summary appended to `results.jsonl` under `%LOCALAPPDATA%\MEVR\sweep\<date>`. A watchdog
   restarts the game after a hang or a crash and resumes at the next scene, marking the one that died. Nothing of it goes
   into the repository but the tools and the counts.
4. **The first full pass (one night), then triage fanned out.** Estimate: 309 scenes at about 45 s, a dozen level loads, a few
   restarts: five to six hours. The judge's report groups the exceptions; parallel agents each take a group with its logs
   and snapshots and return one of three answers per scene: a rule or table entry that fixes it, a tool fault (the scene
   was not really reached), or "needs the owner's eyes". Their answers are claims, so each proposed fix is checked by
   re-running its scenes.
5. **Fixes as scene rules, re-run by difference.** Each fix lands as a rule in the core or a row of the per-scene table
   (the scene builder's table, above), never as a special case in engine code; only the scenes whose baseline it touches
   run again, plus the canary set.
6. **The headset sample.** Ten scenes the pass picks, one per kind (a plain stage, a wide stage, an authored walk, a flowed
   chain with a follow-up, a scene after a cutscene, a radio call, a set piece on the screen, a crowded room, a walk near a
   trigger, a held newcomer), with what to look for in `notes/headset-checklist.md`.

### What is not known and could change the plan

- How many scenes no route reaches (phase 2 answers it). A scene that needs a story point no career has may need a save
  built by hand from the game's data, one per scene.
- Whether resetting plot between scenes of one loaded level is enough, or some scenes change the level itself (someone
  dies, a door opens) so the level must be reloaded: that costs time, not correctness.
- Whether the game stays up for five hours of this. The watchdog assumes not.
- The 182 barks: a sample per area is planned; if the sample shows a problem class, they get their own pass.

## The sweep: progress

### Phase 1: the tools that need no game (2026-11-09, built)

- **`tools/game-slot.ps1 take | return | status`.** `take -Holder <who>` refuses while the game runs or the slot is held,
  copies the ASI folder, the top-level files of `Binaries/Win64` (the exe hashed only), `BioGame/Config` and the ME1 saves
  to `%LOCALAPPDATA%/MEVR/game-slot/backups/<stamp>` with the SHA-256 of every file, writes `lock.json`, deploys this
  checkout's build, writes any `-Set Section.key=value` into the deployed `LE1MEVR.ini` and turns the fake headset on.
  `return` turns it off, puts back every file that is missing or differs, moves files that were not there before (the
  run's saves, the hot-reload path) into the backup's `extras` folder, hashes everything again and fails with the lock
  kept on any difference; it prints the ASI's SHA-256 before and after. `tools/deploy-asi.ps1` now refuses to deploy over
  a held slot (`-IgnoreSlot` overrides): a return would undo that deploy without a word. Tried on a fake game folder
  (changed, deleted and new files all put back; a second take refused). Not yet used on the real folder.
- **The level to open comes from the streaming lists.** The scan writes a `level` record per package whose WorldInfo has
  `StreamingLevels` (54; 43 of them are persistent: no other level streams them), and the catalogue's `map` is a
  persistent level that streams the start's package (`maps`: all of them; `mapByName`: what the name alone said). **36
  starts moved**: the Normandy's plot chunks belong to the level the Normandy is docked at or flying to
  (`BIOA_NOR10_07wake_DSG`, `_06wake`, the lockers and `_01patton` under `BIOA_STA00`; the `_DS1` chunks under Eden
  Prime's `BIOA_PRO00`; the `_DS2` chunks under Ilos's `BIOA_LOS00`). With the right level's volumes, 95 of the 104 starts
  whose chunk no plain touch shows now have a named streaming state (88 before), and 9 are listed by no volume state at
  all (16 before).
- **Layer 0, `tools/conversation-sweep.py predict`.** Per conversation: kind, verdict, level, chunk, and its risks from the
  data. The scan also writes every placed trigger (2,362: `BioTriggerStream` 970, `BioTriggerVolume` 705, `BioTrigger`
  603, `DynamicTriggerVolume` 40, `BioSaveTriggerVolume` 20, `TriggerVolume` 19, `Trigger` 5). Of the 309 conversations
  that enter the conversation mode (288 for the smoke pass, 21 set pieces):

  | Risk | Conversations | What it means for the run |
  |---|---|---|
  | plot-effects | 266 | lines set plot values or fire level script: the scene changes the story for what plays after it |
  | trigger-near | 138 | a trigger's placed origin within 6 m of where it plays (a volume's extent is not in the scan) |
  | flowed | 64 | a chain start plays as barks |
  | owner-spawned | 54 | the owner is not placed in the level: the plot spawns it |
  | named-state | 44 | the chunk shows only under a named streaming state |
  | no-start | 34 | no level script starts it (code does): the sweep cannot fire it |
  | conditional | 30 | every start entry has a plot conditional |
  | npc-walk | 23 | an authored walk |
  | set-piece | 21 | must go to the 3D screen |
  | remote | 19 | a transmission |
  | plot-chain | 15 | a chain kept as a conversation for its plot effects |
  | wide-stage | 6 | a stage 4 m or more across |
  | no-route | 5 | no volume or state lists its chunk |
  | shepard-walk | 4 | the game walks Shepard |

  Only 51 smoke scenes carry no risk but plot effects or conditional starts. **Not predicted** (the plan listed them): a
  camera far from Shepard's mark and a stage with no mark for someone need the stage's marks, which the scan does not
  read yet; the stage's size stands in (`wide-stage`).
- **The work list, `conversation-sweep.py plan`:** 289 scenes in 26 levels (275 conversations and 14 sampled barks, two
  per area with a placed owner); the Citadel 84 scenes in 43 chunks, Noveria 47 in 17, the Normandy 40 in 9, Feros 30 in
  12, Virmire 23 in 11, Eden Prime 17 in 10, the side worlds one to twelve each. 34 conversations no level script starts
  are listed as unreachable by this route.
- **The judge, `conversation-sweep.py judge`:** reads the runner's results (the record's shape is in the tool's header)
  and names each scene's exceptions: `not-reached`, `no-start`, `crash`, `hang`, `forced-end`, `undone` (kept above 0),
  `drawn-jump`, `eye-far` (0.8 m), `no-place`, `not-on-screen`, `not-in-world`, `lines-short`, `wheel-unanswered`,
  `chain-not-played`, `chain-cut`. `--baseline` names what differs from the last accepted pass, `--write-baseline`
  accepts one, `--expect` checks the canary set (kinds, minimums, maximums per named scene) and exits 1 on a miss, which
  is how a run with a rule switched off must fail. `--predictions` adds a table of each risk against the run, so layer 0
  is graded by every pass. Run over research/59's survey files (made before the rules existed) it names what those runs
  showed: the wake-up scene `undone` (kept 26) and `eye-far` (2.36 m), two set pieces in the world, three flowed scenes
  played as conversations, one forced end.
- **The flowed scene's summary line** (`game/ConversationFlow`): `convo flow: summary {"name", "why", "seconds",
  "flagged", "entries": [...], "next", "nextAmbient", "barksRefused", "scenesHeld", "scenesRefused"}` when a chain of ours
  ends: the entries it passed through in order, what the world's conversation was right after (often the same
  conversation again, as a conversation), and what was refused or held over it. Whether the entries are a whole play is
  the judge's to say from the graph (`chain-cut`: it ended on an entry the graph goes on from, or another conversation
  began over it). Builds; not seen in the game yet.
- Tests: ctest `mevr-conversation-sweep` (`tests/ConversationSweepTests.py`: a synthetic scan with a wake-up chunk only
  the Citadel streams, a flowed chain, a trigger; every exception kind, the baseline, the canary set both ways).

### Phases 2 and 3: the runner and the canary set (2026-11-09, built)

- **`tools/conversation-sweep.ps1 -Plan plan.json [-ReachOnly] [-Resume] [-Maps ...] [-Only ...] [-Seed n]`.** Per level:
  `disconnect`, `open`, the wait for gameplay (the party screen sent on with `FinalizeParty`; a conversation that starts by
  itself is played and recorded as `(by itself)`, and the plan's own scene of it is not fired again). Per chunk: the
  loaded volume whose plain state shows it is touched, or its named state set first, then the wait until `mevr stream
  chunks` says loaded and visible. Per scene: Shepard 1.5 m in front of the owner (else on the stage), the start action
  fired, the conversation driven to its end (a reply while the wheel is up, in an order seeded by the conversation's name
  and `-Seed`, so a re-run takes the same replies and another seed is the branch pass; the line skipped otherwise), a
  flowed scene waited out by its summary line and its follow-up driven too. One result per scene in `results.jsonl` (the
  judge's record), the scene's stretch of the mod's log in `logs/<conversation>.log`, the levels and chunks in
  `reach.jsonl`. A watchdog: the process gone is a `crash`, a console silent three times a `hang` (the process is ended);
  the game is started again, the level and chunk brought back, the run goes on. `-ReachOnly` fires nothing: the reach
  matrix (`conversation-sweep.py reach <folder>` prints it).
- **The mod's part:** `convo: summary` has `"entries"`, the entries the conversation passed through in order (the judge's
  `lines-short` reads them), and `mevr convo` says the current entry and whether the wheel is up (the runner's driver).
- **The canary set, `tools/conversation-canary.json`** (`conversation-sweep.py canary plan.json tools/conversation-canary.json
  --out-plan ... --out-expect ... [--broken]`), all at the Citadel, one level load:
  - the wake-up scene, which starts by itself, with six walks of the stick during it: nothing undone or drawn, at least
    20 placements withheld, at least one hold at a trigger's edge;
  - the Garrus argument with two barks and Chorban's start fired half a second after it (a scene's `alsoFire`): no
    exception, two barks refused, one scene held; and Chorban's conversation then played by itself;
  - a set piece (`sta60_stripperseat`): on the screen;
  - **broken**: the same wake-up scene with `worldStaySource = 0` must be `undone` with nothing withheld. A judge that
    passes that run passes everything.
- A lesson from the first minutes: the runner wrote its log with `Add-Content`, which wants the file to itself, and a
  `tail` of that log from another shell ended the run after four levels. Its files are now opened for append with readers
  let in, tried again for ten seconds.

### In the game: the reach matrix and the canary set (2026-11-09, 20:55-23:05, the first use of the game slot)

- **The reach matrix** (`-ReachOnly`; stopped by hand after 16 of 26 levels, the rest being time-outs): **215 of 237 scenes
  reached**. Every scene of Noveria (47), the Normandy (40) and Eden Prime (17), 71 of 84 at the Citadel, 21 of 23 on
  Virmire, 8 of 9 on Ilos, the five freighters, the two end levels. Therum does not reach gameplay by `open`; the side
  worlds tried (UNC17, UNC24, UNC31) end in the squad screen and then the Mako's game mode, which the runner does not
  take for gameplay [verify]; Feros and the other side worlds were not tried. At the Citadel 8 scenes are in chunks that
  never became visible from a fresh level (Chora's Den, the strip club), 4 in chunks that load without their start action.
- **What the first run taught:** a volume touched from afar streams out the floor Shepard stands on; he falls to the kill
  plane, the level has no player (`mevr stream list` answers "(no pawn)") and every later scene there fails: Noveria 21
  of 47 and the Citadel 15 of 64 in that run. Shepard now floats while chunks stream (`ghost`), walks again by the owner
  (`walk`), and a level without a pawn is opened again. The scan also says every chunk is one streaming hop from its
  level's own volumes, so the hop-by-hop streaming the runner got (the plan's `streamVia`) is a safeguard, not a need.
- **The canary set, at full length** (`-Pace full`):
  - the wake-up scene, by itself after the Citadel opens: 315 s, 55 lines, 20 wheels answered, ended by itself; **30
    placements withheld, 0 undone, 0 drawn**; the six walks moved the player 2.19 m and were held 403 times at
    `BioTriggerVolume_1`. One limit broken, the known one: Shepard's head bone up to 2.65 m from the held eyes
    (`eye-far`; the snap after the cutscene);
  - the Garrus argument with `TowerSpecificBanters`, `AmbientDiplomats` and Chorban's start fired half a second after it:
    the flow module's new line reads `convo flow: summary {"name": "sta70_garrus", "why": "it began again as a
    conversation", "seconds": 14.7, "flagged": 2, "entries": [0, 1], "next": "sta70_garrus", "nextAmbient": false,
    "barksRefused": 3, "scenesHeld": 1, "scenesRefused": 0}`; the wheel conversation followed (19.0 s, 4 lines), and one
    second after it Chorban's held start fired and his conversation played (6.6 s). No exception;
  - **broken** (`worldStaySource = 0`, the wake-up scene again): `undone`, kept 23. The judge catches the run it must
    catch; the key is back to 1.
  - No set piece is in the set yet: the council hearing fired from its bare start action starts nothing (the level's script
    activates its people before it), and the strip club's chunk does not stream in. A fired start skips the script before
    it; firing a scene's own event instead (the first action behind `BioSeqEvt_OnPlayerActivate`) would play it as
    authored, teleports and matinees included, and is the route for the authored scenes.
- Two runner faults found by the first canary run and fixed: the wait for gameplay ran out behind the five-minute scene
  that started by itself (the level was taken for one that never opened), and "not reached" was then written over that
  scene's own result.

### Sampling: not every scene of a kind (2026-11-09; the owner: "ensure your runs aren't wasteful and consider sampled runs for more basic experiences")

- **A conversation's make-up, from the data** (`conversation-sweep.py predict`: `tier` and `shape`). Of the 277 that enter the
  conversation mode: **A, simple, 127** (two people on a small stage or nobody placed, and a wheel; 95 of them one shape),
  **B, staged and more, 110** (a flowed chain, a chain kept for its plot, a wide stage, a transmission, a stage for three
  or more), **C, authored, 40** (a set piece, an authored walk, Shepard walked). 76 shapes (kind, stage size, wheel or
  watched, those marks). Scenes of one shape exercise the same rules. Where a scene plays is its own risk and is not in
  the shape: 193 of the 277 have a trigger near, a spawned owner, a named streaming state or no route.
- **The 32 others** that the first count took for unreachable never enter the conversation mode: their lines are
  voice-over actions (`BioSeqAct_FaceOnlyVO`, the scan's `vo` records), 9 under animated cutscenes (Saren and Benezia after
  Eden Prime, Sovereign's arrival, the approach fly-bys) and 23 on foot (a squadmate's remark, radio chatter). Found with
  `conversation-scan --references`; the placeables' packages (`_PLC`), skipped until then, hold one start (Ilos's archive
  door).
- **`plan --sample`**: every tier C scene; `--per-shape` of each B shape (2) and one of each A shape; with `--per-place 1`
  one scene of each level's each place risk per tier; `--reach` leaves out what a reach run could not get to; `--also`
  adds an earlier pass's exceptions. With `--per-place 1` and the reach run: **36 of 127 A, 55 of 110 B, 35 of 40 C: 133
  scenes** (116 in the levels that open), against 290. A full pass was stopped after six minutes for this one.
- **The rule for widening a sample:** a shape whose sampled scenes do not come out alike is run whole; a fix is re-run on
  its scenes, one of each shape and the canary set, not on everything.

### A scene through its own event; the authored moments (2026-11-09, built, not run)

- The scan traces every start back through the level's script to the nearest event (`via` on a start: the event, its
  originator's place, the first action behind it, what lies between). **710 of 763 starts have one**: the player used
  someone 315, a touch 124, a placeable used 86, the level loaded 68, a remote event 58. Between the event and the start:
  plot checks before 226 and 152 of them, a cinematic mode switch 64, a black screen 28, a teleport 12, a matinee 6.
- **`conversation-sweep.ps1 -Fire event`** fires that first action instead of the bare start (the plan's `via`: the
  player's own events, at most ten steps away), so the script runs as authored; without a conversation in 40 s the bare
  start is fired after all, and the record says which (`fired`). The council hearing is the case that showed the need:
  its bare start starts nothing.
- **The moments** (`conversation-sweep.py moments scan.jsonl --out moments.json`): the directed level-script matinees that
  move the player by a track and have an event upstream, **21 in 7 levels** (the walk to the council 7.5 s, becoming a
  Spectre 17.8 s, Benezia's entrance and her fight's phases, Liara's rescue, the Thorian's geth, Zhu's Hope's first
  creeper, the end). The runner plays one like a scene (its `actionClass`), samples the game modes it passes through and
  drives the conversation it leads to. This is the test list for the authored-scenes work.

### The opening without a cut (2026-11-09, built, not run in the game)

`[Cinematics] worldOpening = auto` (`accept`, today's dip at every staged opening, stays the default until this is seen in
the game; `keep` as before). `cw::DecideOpening` (unit-tested) from what the mod measures when the opening ends: how far
the stage put Shepard from where the player stood on foot (the last on-foot view, at most 5 s old), the rise, a line of
sight from the player's eyes to Shepard's head at the mark, a floor there, whether the pawn is hidden, and how far the
stage moved the people who were in the player's view (the conversation's own saved places).

| The stage put Shepard | The opening |
|---|---|
| within 1.5 m of where the player stood | **stay**: Shepard is put back where he stood, the view is the one on foot, nobody is arranged, nothing dips |
| up to 40 m away, at most 6 m up or down, in sight, on a floor | **walk in**: the same, and the mark is where he is wanted: he walks there himself (outside the leash a step may only bring him nearer); the lines go on, the wheel waits |
| anywhere else, or the pawn hidden, or people in view put more than 3 m away, or no fresh place on foot (after a cutscene) | **cut**: at the mark under a dip from black, everyone brought into view, as before |

The summary line carries `"opening"`, `"markM"` and `"cuts"`, and the judge counts them: how many conversations open
without a dip is a number of every pass from now on. Not in it yet: the people of a walk-in walking to their marks rather
than standing on them at once (the walking-to-marks prototype), and the squad.

**In the game (fake headset, 2026-11-09 23:38-23:50, `worldOpening = auto`):** three Noveria conversations fired with
Shepard 1.5 m in front of their owners: Opold (the mark 1.59 m from where the player stood: walk in), Anoleis (2.81 m,
0.73 m lower: walk in), Lilihierax (0.80 m: stay). **No dip in any of them, the view stays where it was** (0 re-anchors,
the head bone at most 0.53 m from the held eyes at Opold's). Two faults on the way, both in how his head bone was read
right after the stage placed him: read after the pawn was put back it already had the move in it (the follower took the
difference for a jump and moved the view to the mark after all), and for about a second his mesh is up to 1.3 m from his
pawn (body follow, a pose not yet updated), which the follower also read as a jump: the anchor now just takes the bone's
place for 1.5 s after such an opening, and the eye-to-body measure starts after it.

### A moment in the world: the walk to the council (2026-11-10, built and seen in the game)

`[Cinematics] worldMoments = log | walk | carry | off` (default `log`: measured only; `game/AuthoredMoment` measures,
`game/ConversationWorld` holds the view). The natives' reading held up: the hook on `AActor::MoveWithInterpMoveTrack` goes
in and is asked about Shepard every tick of the matinee; the script's teleport, black screens and cinematic switches
arrive at the ProcessEvent handlers.

**Measured as authored** (`log`, the moment fired through Anderson's own event with the runner's moments plan): the
cinematic mode on (game mode 6, the pawn shown; the 3D screen); the track moves Shepard for 7.4 s, 11.70 m along its way,
7.25 m from start to end and 2.33 m up; a black screen; the cinematic mode off and on again in one tick; the teleport, 2.89
m on from the track's end (9.76 m from where he first stood), in sight; the council chunk's own black screen; the hearing's
conversation on its stage.

**With `worldMoments = walk`:** the mod holds the view where the player stood on foot (the cinematic game mode shown as
the world: three more `[MEVR]` seams in `xr_session.cpp` and one in `head_aim.cpp`, so the host must be deployed with the
mod); the track's move of his pawn is withheld (the hook answers "not moved"); the stick walks him, without the trigger
guard (the script wants him through the triggers on the way). A script's move of him (the teleport; a matinee's reset at
its end) is judged by the opening's rule: in the same space it is not cut: he is put back where his own walk has him and
the place becomes where he is wanted (the walk's leash is around it); when he is within a step of it, or after
`worldMomentPatienceS` (12 s), or at once with `carry`, he is carried there in a straight line at `worldMomentCarryMS` (3
m/s) and then stands exactly there, as the teleport would have put him. To another composition the view goes with him
under a dip. The on-foot record is kept fresh, so the conversation after it opens by the opening's rule.

**Why "wanted" and not just "stays":** the first try only withheld the teleport, and the scene never went on: the place
the script teleports him to is inside the volume that streams the council's chunk in. A scripted teleport of the player
is also what the next piece of script counts on.

**The run (2026-11-10 00:06, the wait set to 4 s, nobody at the stick, `worldSetPieces = world`):** the view held at
(-6806, -7285, 61908); the track withheld (0.00 m); the teleport of 9.76 m and 2.85 m up withheld, "the same space, no
cut; he is wanted at (-7692, -7010, 62090)"; carried 3.06 m when the hearing began (its chunk had streamed in); **the
hearing opened as a walk-in: "Shepard's mark is 21.08 m from where the player stood (+3.16 m up), in sight ... no cut"**;
it ran 64.7 s in the world, 26 lines, 4 wheels answered from where the player stood, 0 dips, nothing undone. Then the
script put everyone 15 m away for Anderson's next conversations: a cut (no fresh place on foot).

**What that run leaves open:**
- with nobody walking, the player hears the hearing from 21 m away: nothing carries him to a walk-in's mark (only a
  script's own move has the wait and the carry);
- at 54 s and 55 s of the hearing Shepard's head bone moved 0.88 m twice (his own animation at the mark) and the view
  went with it: during a walk-in the follower should stand down;
- Anderson walks his track while the player stands; when the script deactivates him and the council chunk's own Anderson
  appears at the podium, nothing hides the swap (the black screen did);
- the black screen itself and the director track's fade still show (0.3 s here);
- Shepard's body plays the matinee's animation (walking) where he stands;
- the presence rule named the hearing: "2 of the first 3 shots are framed from more than 8 m away (the farthest 38 m)".
  It is in the generated set-piece list for its cameras alone; in the world it had a place for the player. The list
  wants splitting (Shepard moved by a track, or no place for him) from "authored cameras";
- the other 20 moments are not run.

### The sampled pass (2026-11-10 00:10-02:27; skips; `worldOpening = auto`)

`plan --sample --per-place 1 --reach ...` over the 13 levels that open (the credits level left out: its scene ends the
game): 109 scenes fired, 120 judged with the ones that started by themselves or inside another scene's stretch, **no
crash, no hang, no restart, nothing drawn mid-jump**, 2 h 17 min (about 75 s a scene with the level loads). Feros opens
with `open` after all.

| | Scenes | Note |
|---|---|---|
| inside every limit | 64 | |
| **openings in the world** | stay 30, walk in 29, cut 11 | **8 dips from black in all**; with `accept` every staged opening dipped |
| no-start | 29 | tool side: the bare start action fires nothing (its level script sets the scene up first) |
| chain-not-played | 9 | by design: the game's start choice took a wheel start (its plot conditional) |
| not-reached | 6 | a chunk that never became visible (2 on Feros, the locker), an action gone after Shepard was placed (3) |
| eye-far | 9 | mod side, below |
| undone | 4 | mod side, below |
| forced-end, wheel-unanswered | 1 | Vigil's 123 entries against the runner's 150 s: the runner now gives a long conversation more time |

**The mod-side exceptions, by cause:**
- **An authored walk undone every tick** (`undone`: Wrex on Virmire kept 240, the salarian prisoner 144). A matinee's move
  track puts the actor where its keys say each tick; keep-in-place took each of those for a stage placement and put him
  back, and the track moved him again. Fixed: an actor a move track moved in the last 0.3 s (the new hook sees every
  actor) is left to it (`trackWalks` in the summary).
- **The view moved by Shepard's own head bone after an opening without a cut** (`eye-far`, and the 0.85-1.2 m "JUMPED"
  lines: Talitha four times, the Normandy's debriefs). His body stands where the player left it; the scene's animation
  moves his head there and the follower moved the view with it. Fixed: after such an opening only a move past 6 m (the
  script put him elsewhere) is followed, under a dip.
- **The runner's own doing**: Shepard placed 0.5 m above the owner's height hovered there after `ghost` and `walk`
  (Anderson at the Tower, Anoleis: the mark "0.76 m lower"). Now 0.1 m.
- The wake-up scene's 2.19 m is the known snap after its cutscene.

**How the sample held up.** By make-up: tier A 41 scenes with 2 mod-side exceptions, B 52 with 4, C 27 with 5: the
authored scenes fail most, as the tiers say. Of the 12 shapes with two or more scenes played, 10 came out alike; the two
that did not are the NPC-walk shape (2 of 3: the undone walks) and the four-person stage (1 of 4: a debrief). So one
scene of a shape does stand for the others where no authored track is involved, and every authored scene is worth
running. The predictions against the run (share of scenes with that risk that ended as any exception): named-state 77%,
npc-walk 71%, flowed 70% (mostly "the chain did not play": plot), trigger-near 58%, plot-effects 48%; remote 0 of 11,
shepard-walk 0 of 2.

**The 12 flagged scenes again, with the fixes (02:29-02:56 and 02:58):** Wrex's walk kept 240 -> 0 and the prisoner's
144 -> 0, each with `trackWalks` 1 ("is moved by a matinee's track ...: an authored walk, left to it"); no re-anchor of
the view in any walk-in or stay (Talitha had 4); Vigil played whole (125 lines, 28 wheels answered). Anoleis without the
hover: the head bone at most 0.54 m from the held eyes (1.13 m before). What stays over the 0.8 m limit is Shepard's own
body at his mark-less place: the Normandy's debriefs (0.9-1.4 m: a four-person stage whose animation carries his head
that far) and Talitha's scene (0.84 m): the view no longer follows, so this is how far his body strays under a player
who stands still, a thing for the headset. Two small ones remain `undone` (the debriefs: a person put 0.4 m and 3 m away
once each, by neither the stage's placement function nor a track). A far-shot count taken from a walk-in's start named
Wrex's scene as "no place" (the player 29 m from a scene he had not walked into): far shots are now measured from
Shepard's mark while a walk-in is on.

### The canary again, the moments, and two more faults of the new opening (2026-11-10 03:00-03:40)

- **The canary set with every change of the night: 3 of 3 as expected** (the wake-up scene, the Garrus argument with its
  barks and held scene, Chorban after it).
- **The 16 moments in the levels that open, `worldMoments = walk`, each fired through the first action behind its
  event:** the walk to the council held in first person as before; Chellick's bump (8.0 s, the track moves Shepard 4.34
  m) runs in the on-foot game mode, so it is not a moment by the rule (the cinematic mode) and the game carries the
  player in his own first-person view; the other fourteen did not play from a fresh level (eight fire nothing: their
  first action is a plot check the story's start does not pass; six were not reached: a chunk, an action gone, the level
  already in a cinematic). **A fresh level reaches two of the sixteen**: the moments need a career's plot (a save route),
  which the runner does not have yet.
- **A fight with the floor** (found because Benezia's conversation started by itself during that run: `undone` 465,
  `drawn-jump` 466). Not a track: after the opening without a cut Shepard was put back at the height the on-foot record
  had, which was 0.52 m too low (the record was taken while the runner had him floating), the engine lifted him onto
  the floor every tick and keep-in-place pushed him down again, each time in a drawn frame. Now he is put back as high
  above that floor as he stands above the mark's (the view corrected by the same), and for the 1.5 s his mesh needs to
  settle his kept place is wherever he comes to rest. The same scene after: 0 undone, 0 drawn, the head bone 0.59 m from
  the eyes.
- **A track's move of the player in a first-person conversation is withheld** (the summary's `trackHeld`): his place in a
  conversation shown in the world is his own. (Asked twice in Benezia's scene; it was not the cause there.)
- `worldOpening = auto` is now the default of the first-person mode.

### Set pieces in the world (2026-11-10 03:40-04:25)

The 21 conversations the scan sends to the 3D screen (authored cameras on a quarter of the lines or more, or Shepard
walked by the game), played with `worldSetPieces = world`, each through the first action behind its own event where it
has one (`-Fire event`), the bare start otherwise. 15 were in levels that open; with the ones that started by
themselves:

| Scene | In the world | |
|---|---|---|
| the council hearing (`sta90_council_saren`) | 26 lines, a walk-in 21 m from the podium, nothing undone | a place |
| Anderson and Nihlus in the comm room (`nor10_captain`, `nor10_comm_room`) | 33 and 9 lines, the opening a stay, no far shot of 41; 5 of 12 from 11 m | a place |
| the approach to Ilos (`nor20_ilos_approach`) | 20 lines, a cut (it follows a cutscene), no far shot | a place |
| the archive door (`los10_trig_door00`) | 4 lines, a walk-in 2.6 m | a place |
| Wrex at Fist's (`sta60_wrex_at_fists`) | 6 lines, a walk-in 2.4 m | a place |
| Kirrahe's camp (`jug20_sal_captainkirrahe`) | 43 lines through its event, a walk-in 17 m, an authored walk left to its track; started by itself: 22 of 23 shots from afar, Shepard's own track withheld 12 times, his head 2.4 m from the eyes | the game walks Shepard: the screen |
| Liara and Kaidan getting up (`nor20_start_liara`, `nor20_start_kaidan`) | no line of their own; Shepard's track withheld 167 times, his head 2 m from the eyes | the screen |
| the strip club seat | no line, 0.94 m | the screen (not seen whole) |
| Sovereign, the Ilos open, the Thorian, leaving the Citadel, the empty locker, the scenic view | did not start or were not reached | the screen until seen |

**The rule that came of it:** authored cameras alone do not make a set piece; the head is the camera. The scan's
verdict takes the six that had a place out of "screen" (`SEEN_IN_WORLD` in `tools/conversation-scan.py`, a list that
grows only by a run), `cw::IsSetPiece` is the 15 that remain (the game walks Shepard, or not yet seen in the world), and
the flow table is unchanged (their chains stay conversations until seen as barks). Re-run with the default settings:
see the log.

### The headset sample (phase 6): what to look at, one of each kind

With `[Cinematics] conversations = world` (the sub-defaults are the ones this note arrived at: `worldOpening = auto`,
placements withheld at their source, authored walks left to their tracks, flowed chains held and protected); the two
opt-ins for the council are `worldMoments = walk` and nothing else. `notes/headset-checklist.md` has what to watch for.

| Kind | Scene | Where | What the run showed |
|---|---|---|---|
| a plain stage, the mark a step away | Opold | Noveria, Port Hanshan | stay: no dip, the view where it was |
| a mark across the room | Anoleis | Noveria, his office | walk in 2.8 m, no dip |
| a long conversation | Vigil | Ilos | 125 lines, 28 wheels |
| an authored walk of an NPC | Wrex on the beach | Virmire | his walk left to its track (was undone 240 times) |
| a flowed chain and its follow-up, with others wanting to start | Garrus and the Executor, then Chorban | C-Sec | barks refused, Chorban held and started after |
| a scene after a cutscene | the wake-up | the Normandy at the Citadel | 30 placements withheld; the body snap of 2.2 m is still there |
| a four-person stage | a debrief | the Normandy's comm room | Shepard's body strays up to 1.4 m from a player who stands still |
| a walk-in from a moment | Anderson, then the council hearing | the Citadel Tower | Anderson's walk in first person, no screen, no cut; the hearing then opens with a dip to the podium (since 2026-11-10: "The hearing that ended at once") |
| a framed scene now in the world | Anderson and Nihlus | the Normandy's comm room (Eden Prime's level) | a stay, 33 lines |
| a scene still on the screen | Kirrahe's camp | Virmire | the game walks Shepard |

### The morning round: defaults, the side worlds, a walk's last step, walk-ins behind plot checks (2026-11-10 05:15-06:30)

- **The council sequence on the default settings** (only `[Cinematics] conversations = world` set): `worldMoments = walk`
  is the default now. Anderson's walk held in first person, both black screens taken down, the teleport withheld, the
  hearing opened as a walk-in 21.5 m from the podium, 26 lines. What made `walk` safe as a default: the mod asks the
  track itself where it has Shepard while its move is withheld (`engine/ConversationHooks::MoveTrackPlace`, the track's
  own place function, vtable +0x390), and when the track lets him go that place is where he is wanted, as after a
  teleport: he walks there or is carried after `worldMomentPatienceS`. Seen in the game on Feros (the geth's entrance:
  "the track let Shepard go 4.67 m from where it had him").
- **The side worlds** (the sample's one scene per world plus Bring Down the Sky's, 8 levels, 26 minutes): the runner's
  Mako exit worked every time (10 of 10). 18 scenes judged: 7 inside every limit, 11 exceptions, no crash or hang;
  openings walk in 6, stay 3, cut 2, one dip in all. The exceptions: 4 not reached (three chunks of Bring Down the Sky
  the scan has no route for, one start action not loaded), 4 `eye-far` (Shepard's head bone 0.81 to 1.19 m from the
  eyes: the open item "Shepard's body strays"), a bare start that fires nothing, a chain that played as a conversation,
  a trigger's scene that began with Shepard a kilometre away (the runner's doing), and one real fault:
- **A walk's last step, fought 512 times** (Lieutenant Durand, `sp105_mariedurand`): she walks 4.6 m by herself (steps
  under the jump limit: left alone), and the walk's end snaps her 0.33 m onto its goal, every tick, for as long as it
  is undone: 512 put-backs in 22 s. The same shape as Wrex's track and Benezia's floor: **a placement the game repeats
  is the game's own**. Rule (`cw::Insists`, unit-tested): a placement of at most 1 m that comes back at the same place
  the tick after it was undone is accepted (the summary's `yielded`); farther ones are never accepted this way (a stage
  mark is what keeping people in place is for). With three askings the two undone steps each reached a drawn frame (a
  flicker of 0.35 m), so it is two. The judge allows one undone step and one drawn frame per accepted move. After: kept
  4, then 0.
- **A hot reload ended the game** (05:57, the only crash of the round, and the mod's own): the flow module's restore
  asked `IsAlive` of conversations flagged eight levels earlier, and the engine had given their memory back to the
  system: the read faulted. `ue3::Reflection::IsAlive` now tests that the object's header can be read at all (a
  guarded read; unit-tested with a pointer into unmapped memory). The next reload, two levels after its flags: "8
  flags put back on 10 conversations (6 of them gone with their level)".
- **Walk-ins behind plot checks** (the moments a fresh level could not play): the scan records the whole way from a
  matinee's event to it, action by action (`via.chain`), and the runner, when nothing comes of the first action in 8 s
  and a plot check, gate or condition is on the way, fires the action behind the last of them (a test rig's shortcut:
  the plot is not set). Six tried, three then played:
  - **becoming a Spectre** (17.8 s): held in first person 22.7 s, the track withheld, the black screen taken down, the
    script's teleport 9.76 m and 2.85 m up the stairs judged the same space: no cut, he is wanted there, carried 2.5 m
    by the time the council's conversation began. The second moment to run whole in first person.
  - **the geth's entrance on Feros**: held; the game ended its cinematic mode at once (the scene's actors are not there
    without the plot), 1.0 s.
  - **the first rachni on Noveria** (20.2 s): **not held**. Its script turns the cinematic mode on, goes black,
    teleports Shepard 5.66 m, comes back from black and only then starts the track; the rule wants the track within 3 s
    of a fresh on-foot view. It plays on the screen as before. Holding it would mean trusting an on-foot view through
    several seconds of cinematic mode, which would also catch real cutscenes that end by placing the player: left as
    it is until a scene needs it.
  - not played: two whose first action was gone once Shepard stood there, one whose chunk never became visible.
- **The menu has First person again** (Comfort > Conversations: First person (preview), In VR, 3D screen, Flat
  screen). The settings code never lost the choice; the button was taken out on 2026-11-07 (the owner: not ready for the
  release). Back as a labelled preview so the mode can be tried without editing a file; the default stays the 3D screen.
  **The owner's call**: one button in `assets/menu/main.rml`.
- The canary set on the final build: 3 of 3 as expected.

### The guided walk-in, the hearing that ended at once, Therum from a save (2026-11-10 06:40-08:46)

- **A walk-in nobody walks is guided** (`[Cinematics] worldWalkInS` 5; 0 at once, -1 never; `cw::StepGuide`,
  unit-tested): after an opening without a cut that wants the player at Shepard's mark, a player who leaves the stick
  alone for five seconds is walked there at the walk's own pace (1.4 m/s) by the same step as the stick's: floor,
  walls, people, the leash, the triggers. The stick takes the walk back at once and the wait starts again; blocked for
  2 s in a row, the guide gives up for that conversation ("he hears the scene from here"). The summary's `guidedM`.
  In the game, nobody at the stick: Virmire's first conversation (the mark 7.1 m away) guided 5.3 m in the 3.8 s it
  lasted; Wrex on the beach from 29 m: 10.7 m, then a ledge, given up 17.9 m short; the council's hearing (while it
  still opened as a walk-in): 8.6 m up the stairs to the edge of `BioTriggerVolume_0`, 13 m from the podium. That last
  one is the walk's trigger guard, and it stops the player's own stick there too: **a walk-in cannot reach a mark that
  stands inside a trigger**. Letting such a touch through was written and taken out again the same morning (below).
- **"In sight" is three lines now** (`InSight`: to the mark, to a point 80 units above it, and that from 60 units above
  the eyes): one line grazed the stairs' edge in the council chamber and said "out of sight" in one run of four.
- **The hearing that ended at once.** The council's hearing ended in its first tick ("the game left its conversation
  mode", 35 to 44 ms after it began; the rest played as a cutscene on the 3D screen) in 9 of the 16 runs that opened it
  as a walk-in, and in none of the 4 that kept the stage's placement. What was found, in order:
  - Not carried-over plot (it happens in a fresh game; but a second council run in one game process is no clean test:
    every run needs a fresh process), not the day's code (the guide, `Insists`, `IsAlive`: taken out one by one in
    thought and by the pattern below).
  - The hearing begins when Shepard, carried or walking toward where the script's teleport wanted him, crosses into
    the chamber's streaming volume (`BioTriggerStream_106`) 2.3 m into the walk. The runs split by how far past that
    edge the on-foot record was when the conversation began: up to 13 units, the hearing lived; 18 units or more
    (three ticks or more between the touch and the conversation), it died.
  - Where he was put back made no difference: in place; 0.65 m back, out of the streaming volume (found through the
    engine's own `Actor.Touching` list); 1.5 m back, out of `BioTriggerVolume_4`. Dead in the late runs, alive in the
    early ones. So it is the put-back itself (a far move of the pawn from the podium, with every touch it ends and
    begins) arriving while the level's streaming has settled, not a particular edge.
  - **The rule that holds:** a conversation that begins while a moment's walk is still on its way to where the script
    wanted Shepard keeps the stage's placement (a cut, with its dip). Two of two after it, one of them in the failing
    zone; with the two earlier cuts, four of four.
  - **What it costs:** the hearing opens with a dip again, always (it begins 2.3 m into the walk, never after he has
    arrived). Anderson's walk before it is still first person and uncut.
  - **What would give it back:** the view walks and the pawn stays where the scripts have it (no `SetLocation`, so no
    touch is ever ended or begun by the mod; Shepard's body hidden while the two are apart). That removes the put-back,
    the trigger guard and this whole class of fault, at the price of no body under the player during a walk-in. Not
    built: the next step for walk-ins and moments.
  - Taken out again: stepping back behind a trigger's edge at the opening (two versions), and letting a walk-in pass a
    trigger that holds the mark (never seen in the game; what a touch sets off is the level's to say).
- **Therum from a career save.** `tools/conversation-launch.ps1 lav70_trig03_krogan_boss -Save John02_21 -World on`:
  John02 (1 h 34 min in, before Therum) had never been there; its quicksave rewritten to `BIOA_LAV00` at the scene's
  owner loaded on foot at the written place, and the battlemaster's conversation played in first person: 7 lines, 2
  wheels, 4 placements withheld, nothing undone or drawn, the head bone 0.31 m from the eyes; the opening a cut (the
  stage's mark is 11 m off and out of sight of where the launcher stood him). **A save loads into a level its career
  has not visited**, so the save route reaches Therum and, with a career at the right point, the plot-bound scenes.
  The sweep's runner has no save mode yet: the launcher does one scene at a time.
- Two lessons about the rig. After about a dozen game restarts in a row the EA app stopped starting the game ("Add
  game" in its log, then nothing): quitting `EADesktop.exe` and asking Steam again started it. And the launcher's
  `-RestoreSaves` after the game slot's return put three files (the profile, the launcher's profile, John02's autosave)
  back from its own backup, taken mid-session, over the ones the slot had just restored and hash-checked: they were
  restored a second time from the slot's backup (14 of 14 files identical to before the session). With the slot held,
  the slot's return is the restore; the launcher's is not to be run after it.
- The canary set on the final build: 3 of 3 as expected.

### The view walks; a moment really shown; saves for the headset (2026-11-10 08:55-10:10, paused for the owner's test)

- **A walk-in by the view** (`cw::DecideWalkIn`, `[Cinematics] worldWalkInWay = auto | body | view`). The pawn is not moved at
  all: Shepard stays where the stage has him, hidden (the player would see his own headless body at the mark, and arm IK
  would draw its arms from there), and only the held view walks: by the stick or the guide, over the floor, around
  people, with no trigger guard, because nothing of the level's is touched. Within a metre of him the rest is walked for
  the player and the view takes its place over the pawn: the two are one again, his body under the player. If the
  conversation ends first, Shepard is brought to where the player stands (an ordinary move, the conversation over)
  instead of the view jumping to him. `auto`: the view when a moment's walk was still on its way as the conversation
  began (the council), or when the mark stands inside a trigger the player is outside of; the body (put back under the
  player, the path validated in the sampled pass) otherwise.
  **The council's hearing with it: 4 of 4 runs, two of them in the zone that ended it before.** No cut, 21.3 to 21.5 m
  guided up the stairs and along the walkway, arrived beside Udina, 25 or 26 lines. This replaces the morning's rule
  (a cut) for that case; with `worldWalkInWay = body` the cut is back.
- **A moment's first-person view had never been rendered.** The first snapshots ever taken of a moment showed the game's
  own camera: Shepard from behind, headless, for the twelve seconds between Anderson's walk and the hearing. The mod took
  the game's camera pointers in the conversation mode only, so after its first half second a moment published no view to
  hold; the logs said "held" all night. **A claim about what the eyes show needs a snapshot, not a log line.** Fixed
  (the pointers are taken in the cinematic mode too) and seen: Anderson turns, walks up, the view stays at the foot of
  the stairs in first person.
- **Anderson and the script's black screen.** His authored walk is the first flight only (7.4 s). Then the script goes
  black, teleports Shepard up the stairs and switches Anderson off; another Anderson stands at the podium. With the
  black screen taken down the player saw him vanish from the stairs. Now, where the script turns black **on** during a
  held moment (`m_eBlackScreenAction` 0 or 2), the mod's own dip from black takes its place, the view where it is
  (the owner: a forced placement needs a fade); where it turns black off (the hearing's start), nothing. The published
  dip count no longer falls back to zero between a moment and the conversation after it (the host dips on any change:
  that was a second dip at the hearing's start).
- **The owner's direction for what comes next** (2026-11-10): a forced placement always under a fade; NPCs walking to
  where the scene wants them with the game's own movement instead of appearing there (a narrow rule, not an animation
  system; the walk-to-marks prototype is the start); as a fallback only, the scene pausing until the player has arrived
  and a marker on the floor at the mark (the view-walk needs no pawn move, so the game's own pause would do; the squad
  placement's ring is the marker). None of the three is built.
- **Opening-only runs** (`conversation-sweep.ps1 -Pace opening`; the owner: "we just care about the transition"): a scene is
  ended by the runner once its opening has settled (a stay or a cut 6 s in, a walk-in on arrival or when the guide gives
  up, at most 45 s; a flowed chain 15 s), and the judge does not count the early end, the unanswered wheel or the short
  line count. Its first run was stopped after nine scenes (a chain's wait was still 720 s: fixed) and is not judged.
- **Validation saves** (`conversation-sweep.py saves scan.jsonl tools/conversation-headset-sample.json --base <career save>`):
  for each scene of a list, the career's save rewritten with the scene's level, the player a few metres in front of the
  person (or the event's originator) facing it, before anything engages, and every plot check on the scene's way set as
  the way wants it (the scan now records each `BioSeqAct_PMCheckState` on the chain with its plot index and the output
  taken, and the originator's facing). The council: 4 m in front of Anderson, plot 4398 ("council meeting happened?")
  back to 0 in a career where it was 1. Numbered slots of the career (the Load screen reads the folder once, at the
  game's start), a table of slot, scene and how it starts, `--remove` deletes them. Ten scenes written to John02's
  slots 80 to 89 for the owner's test. **Not yet loaded once**: a plot conditional (Chorban's) cannot be set, and the
  conversations' own entry conditions are the career's.
- Seen and not fixed: a flash of the 3D cinema screen (0.15 s, two dips) as a moment begins, before its track first
  moves Shepard.

### Who turns, and how (2026-11-10, after the owner's first headset sessions)

The owner played the players' build of the branch on his headset (the validation saves: the council's hearing, then the
Spectre ceremony) and said: the dip at the Citadel scene bothers him less than he expected; **everyone turning on the spot to
face each speaker is not wanted where the layout is good, and looks unnatural**; if a body twists, it should look like the
body's own animation; better still a composition in which nobody needs to turn; and whether the voices can sound spatial
(below, "The voices").

**What turned them.** Keep-in-place's turn (research/59 "Keeping people in place"): every tick, everything in the
conversation but Shepard was rotated (`Actor.SetRotation`, 120 degrees a second) toward whom the game's look-at list has it
look at, **else toward the speaker**. No animation plays for it. In the ceremony's log the three councillors, who look at
nobody by the game's list, swung 89 to 170 degrees toward whichever of them spoke, Udina 180, Anderson 139, and so did the
dais (a `BioInert`, 177 degrees).

**What the game already does.** Every humanoid has a look-at chain (`BioPawn.m_oLookAtData`, a `BioLookAt` with a
`BioLookAtDefinition`; read from the packages with the scan tool's new `--class-dump BioLookAtDefinition`: three definitions
in the whole game, the humanoid one `HMM_PLR_lookAt_Def` in 232 packages): the eyes turn up to 25 degrees (15 up and down),
the head, the neck and `Chest1` up to 50 each, at speed factors 6, 4, 2 and 1 (the eyes first, the chest last) and, in a
conversation, at strengths 1, 0.55, 0.25 and 0.125. That is the twist the owner asks for, animated by the game, and the
conversation's own look-at list drives it. The game also has a turn on the spot of its own:
`BioPawn.RequestTurnInPlace(rotation)` (a final native, `m_bTurnInPlaceRequested`), and Kismet's `BioSeqAct_TurnTowards`.

**The rule now** (`[Cinematics] worldTurn = look | always | off`, default look; `cw::StepAttention`, unit-tested):

- A body keeps the facing the scene composed: the stage's opening placement and facing (accepted as before), an arc seat's
  facing, or how the person stood when the conversation began. Whoever the game's look-at list gives nobody to look at is
  left entirely to the game (the councillors; the stage's own facings stand, as on a screen). Only people are ever turned
  (`BioPawn`), never a prop of the conversation.
- A facing the stage sets **at once** mid-conversation on someone who has a look-at target is put back: it was authored for
  the stage's mark and for a camera cut (the summary's `facingsHeld`). A turn the game makes gradually (under 360 degrees a
  second) is its own and is followed.
- The body re-faces only when the one it looks at has been more than `worldTurnComfortDeg` (60) off its facing for
  `worldTurnDwellS` (1 s), and then only until that one is `worldTurnSettleDeg` (25) off: the body ends between the people
  it attends to, so the next change of speaker needs no turn. (Walk round someone who looks at you: this is the case.)
- The turn itself is asked of the game (`RequestTurnInPlace`, `worldTurnWay = game`); where the game refuses, or the body
  has not moved after 0.75 s or not arrived after 3 s, the rest is an eased turn of ours (`cw::TurnStepDeg`, at most
  `worldTurnDegPerS`, slower near the end); after two turns of the game's that came to nothing in a session, ours at once.
- The summary counts `bodyTurns`, `gameTurns`, `turnsSlid`, `facingsHeld`; each body turn is a log line (`convo: ... turns
  its body: <whom> has been N deg off its facing ...`).

**Not seen in the game yet** (built, unit-tested, all three builds): whether the look-at chain reaches 60 degrees in practice
with the conversation strengths (the "speaks to" log line's head and body angles say it per line), what
`ValidTargetAngleRange` is (the definition leaves it at the class default), whether `RequestTurnInPlace` turns an NPC that
stands in a conversation and with which animation, and how often a scene needs a body turn at all (the summary's counts over
the sampled set). The composition side (the owner's "better solution") is so far what the stage authored plus the arc seats;
a rule that composes a group so that everyone's partners are within the chain's reach (`cw::ArcSeats` facing the group's
middle instead of the player, seats chosen for the look-at pairs) is the next step if the counts say bodies still turn.

### The ceremony in the headset: a body that vanished, a lurch after every reply, a blink for nothing (2026-11-10)

The owner's second session (10:43-10:49, the players' build `0.2.0+180324d`, slot 81: the Spectre ceremony, 212 s, 32 lines,
7 wheels; the mod's log and Halcyon's are the record). Three complaints, three causes, all read from the logs; the fixes are
built and unit-tested, **not yet run in the game**.

1. **"When the council members talk, the assets near me disappear; when it is my turn, we reappear."** His own body and
   arms. Halcyon's first-person tick (`head_aim.cpp FirstPersonTick`) reads the current camera mode before anything else;
   on a far shot (the director's cameras stood up to 29.6 m from the player, 27 of 41 shots "far") the read fails, and the
   branch for an unreadable mode clears the first-person hides after its 45-frame grace: the anti-cull that keeps Shepard's
   body drawn around the camera goes, the head comes back. `[FPHIDE] cleared (camera-mode unreadable)` at 10:45:57.5, half
   a second into Sparatus's first line, `applied` again at 10:46:09.9 as the camera came back for the wheel; again from
   10:47:59 to 10:48:05. Our seam that keeps the hides up in a world conversation sat **after** that early return. Fix: a
   second `[MEVR]` seam inside the unreadable-mode branch: while `MevrBridge::ConversationInWorld()` the hides are applied
   and held there too (log `[FPHIDE] applied (a conversation in the world; the camera mode unreadable)`).
2. **"Shepard is teleported forward for different responses."** Not the stage's emphasis placements (3 were asked, all
   withheld at their source): the pawn moved 0.4 to 0.5 m at walking speed in the 0.3 s after each of his own lines began
   (50.8 s, 65.5 s, 86.2 s, 109.9 s in the log). The left stick steers the wheel; as a reply is picked the wheel closes with
   the stick still out, and the conversation walk took it as a walk. Fix: the stick is the wheel's until it has been let go
   (`cw::StickWalks`: under 0.25 once, after any wheel; a walk-in walked with the stick held is not affected). Log `convo:
   ... the stick that picked the reply is still out`, the summary's `wheelSticks`.
3. **"Two blinks: one as we started up the steps, one while I was already walking on the pathway, and it teleported me to
   the end of the pathway."** The first is the known flash of the 3D screen at a moment's start (the cinematic mode shows
   for 30 ms before the track's first move makes it a moment: `the world -> the 3D cinema screen` at 10:45:35.889, back at
   36.033): still open. The second is the ceremony script's black screen at the end of Shepard's walk (17.8 s, the player
   having walked the 21.5 m himself): the mod dipped in its place, as built for the hearing, where the black screen covers
   Anderson being switched off. Here it covered only a teleport the first-person view never makes: by the log the view
   stood within 0.5 m of where it was (3.76 m from the mark, the walk-in that followed went on under the stick), and nobody
   in view moved. A blink in the middle of his own walk, with the podium's people then in front of him, read as a teleport.
   Fix: the dip is decided by **what the black screen covered in the player's view** (`cw::CoveredChanges`: the people
   shown anywhere but behind him within 30 m, before the black screen and for 0.3 s after: someone gone, moved over 1.5 m
   at once, or new): a change dips, nothing changed does not (logs `moment: the script's black screen covered a change in
   view ...: a dip` / `covered nothing in view ...: no dip`). [verify]: the hearing still dips (Anderson), the ceremony
   does not.

Also in that log: at 10:48:28 the controllers lost tracking and the conversation's state went unpublished for a moment; the
host showed the 3D screen for 0.1 s (two dips). Not looked into yet.

### A played session, judged (2026-11-10)

Reading the ceremony's 2,700 log lines by hand took an hour and still missed something. `tools/conversation-session.py
[--log <LE1MEVR.log>] [--halcyon <MELEVR_Log.txt>] [--strict]` does it in a second, the same way for every run: per scene
shown in the world (a conversation or a moment) what a player would have noticed, each with its time and, where the log
says so, its cause: `flash` (the 3D screen shown and taken away within a second), `dip` (and what asked for it),
`body-hidden` (Halcyon's first-person hides cleared, and for how long), `screen` (the scene fell back to the 3D screen),
`pivot` (bodies that turned 60 degrees or more), `reply-move` (Shepard moving in the half second after his own line began),
`drawn-jump`, `body-strayed`. On the owner's ceremony session it names everything he reported and one thing nobody had
seen in the log: **his body was not drawn for the whole 17 s walk up the stairs either** (the camera mode is unreadable in
the cinematic mode too; the same seam covers it), and four more stretches of 5 to 26 s during the conversation. Tests in
`tests/ConversationSweepTests.py` over a synthetic session (ctest `mevr-conversation-sweep`). **After every headset session:
run it first, then read the owner's notes against it.**

### The script's Shepard, the player's view; the fast loop (2026-11-10, 11:25-13:20, on the fake headset)

The owner: "resume, test in headset. I won't be available for validation", then, watching a run: "is there any way you can
have a better/faster validation loop?" and "there's the strange blur that is applied to keep some characters in focus that
we would most certainly want to remove".

**The fast loop: scenes from validation saves** (`tools/conversation-from-save.ps1`). The runner's `open <level>` **resets
the plot**: two runs of the ceremony on it played the hearing's conversation and a second script on top (a plot check on
the way failed and the step past it mixed two sequences). The saves written for the headset carry the plot each scene
wants, so the loop is now: the save's bytes over the loaded career's quicksave (the game's own `QuickSave` first),
`QuickLoad`, the scene's first action fired (`conversation-sweep.py actions` gives it per scene), lines skipped two seconds
in, the first reply picked, a conversation ended after `-EndAfter` seconds if only its opening is wanted, then
`conversation-session.py --since` judges that stretch of the logs. No restart, no `open`, no streaming by hand: **ten scenes
in nine minutes** (`-All`), against four to eight minutes a scene before. A message box of the game's (a new talent after
the ceremony, a refused load) is answered with A first. `-Walk N` holds the stick forward (a player who walks along).

What the loop found and what was changed, each seen again in a later pass:

1. **The body stays drawn** (the seam in `head_aim.cpp`): no `[FPHIDE] cleared` during any scene of the passes.
2. **No flash of the 3D screen as a scripted walk begins** (the owner's first blink). The cinematic mode shows a few
   frames before the track's first move made it a moment. Now the script's cinematic switch itself
   (`BioSeqAct_BioToggleCinematicMode`'s handler, the game thread) starts the view at once and publishes it
   (`conversation_world::MomentMayStart`): held where the player stands until a track moves Shepard (a moment: 31 to 47 ms
   later in the runs) or a quarter of a second passes (a cutscene: it goes to the screen as before, 0.25 s late).
3. **No flash as an ordinary conversation begins either.** The judge showed it on every plain conversation of the pass:
   `the world -> the 3D cinema screen -> the world` in 0.13 s, since the mode was built: the host reads the game's mode
   on its own thread, 3 ms after the conversation began and before the mod's first tick of it had published anything.
   Now the mod says **"armed"** every tick on foot (`State::holdPad[0]` with `active` 0: no struct change), the host's
   decision counts a scene as coming for 150 ms after the last armed word (`MevrBridge::ConversationWorldOrComing`, the six
   `[MEVR]` seams of `xr_session.cpp`; `head_aim.cpp` keeps the strict question), the camera update's handler holds the
   on-foot view for the frames drawn in that gap, and the mod disarms the moment it knows the scene is not its own (a
   set piece: Kirrahe's camp still goes to the screen). Three plain conversations after it: nothing found.
4. **The script's Shepard is the script's** (`worldMoments = view`, the default now; `walk` and `carry` remain). The
   ceremony never began for a player who stood still: with the track's move withheld (the morning's design: "the stick
   walks him"), Shepard never passed the trigger on the walkway that streams the council's chunk in, and the event that
   starts the ceremony is fired once, into a chunk that was not there. The owner had walked the whole way and never saw
   it. Putting Shepard where the script's teleport wanted him was not enough either (the chunk loads too late). So the
   track now moves his pawn as authored, **hidden**, through every trigger and onto every mark, and only the view is the
   player's: it stays where he stands, walks by the stick, is guided after `worldWalkInS`, and joins the pawn where it
   comes to rest (never while the track still moves it). A script's teleport within the same space leaves the pawn where
   the script put it. The conversation that begins takes it from there as a walk-in by the view. Both council scenes from
   their saves, standing still: the hearing 25 lines, the ceremony 40 lines.
   **The cost**: while the view is away from the pawn the player has no body (the pawn is hidden): the whole scripted
   walk and the walk-in after it, unless he keeps up.
5. **The guided walk follows the way Shepard went** (`cw::TrailTarget`): the places the pawn passed through, half a metre
   apart, then the mark; a straight line met a ledge or a railing, and the walk's leash (around the mark: only nearer)
   refused the way's first steps sideways, so the leash is around the next place on the way. The ceremony standing still:
   17.9 m guided up the stairs and along the walkway during Shepard's walk, 7.8 m more in the conversation, `the player's
   view reached Shepard ... the two are one again` 10.5 s in; the snapshots show the podium, Udina beside the player, the
   council across the chamber, sharp. (The first saves stood the player on the entrance steps, 1.1 m above the path: 4 m in
   front of Anderson is on them, and a 60-unit drop is refused; the two council saves now stand 2.5 m from him.)
6. **The script's black screen**: the hearing dips (1 of 6 people in view gone: Anderson), and so does the ceremony for a
   player who stands at the stairs (1 of 5 gone). **And for one who walks along** (`-Walk 19`: 13.6 m walked): 1 gone
   and 9 new in view at that instant (the ceremony's people are shown as the script goes black). So the blink the owner
   had on the walkway covers something real, by this rule; what he disliked is still there, and the way out is not to dip
   less but to have the nine there earlier (shown when their chunk streams in) or brought in out of sight: open. A dip
   that came at the first scene after every hot reload was the reload's own (the published dip count starts again at 0
   while the host remembers the last).
7. **The game's turn in place does nothing for people in a conversation**: `RequestTurnInPlace` answered true twice and
   turned nobody in 0.75 s (Anderson and Udina, 72 degrees off each other after the hearing); the rest was the eased turn,
   and after two such answers a session no longer asks. In the council's scenes the game's look-at list is empty
   throughout, so no rule of ours turns anyone there; what turned (Udina 149 to 180 degrees, Benezia's commando 150) was
   the stage's own facing, set at once for a camera cut: now taken as a turn (`facingsEased`) instead of in one frame.
   How far the look-at chain really brings a head is logged per look-at target (`convo: ... looks at ...: N deg off its
   body's facing, its head M deg off after 1.5 s`; the summary's `lookWide`, `lookMaxDeg`, `headShortDeg`): no sample yet
   (no conversation of the passes had a look-at target more than 30 degrees aside).
8. **The blur** the owner saw is the depth of field of the matinee's camera, focused for its own shot: it was switched
   off for conversations only; now for a moment's view too (`convo: depth of field off for the world view` at its start).
   Motion blur was off throughout (the guard's lines).
9. **`tools/fake-headset.ps1 -Off` restored a copy of the owner's SteamVR settings from 2026-10-24** (the first `-On`'s
   backup of the whole file), which no longer had his new headset driver's section (`driver_ivry`). It now puts back only
   the keys `-On` set, as they were (`undo.json`), and leaves the rest of the file alone.

Not started from its save: slot 84 (Wrex on Virmire's beach: nothing came of the start action in 25 s). Slot 88
(Kirrahe's camp) is a set piece and went to the screen, as it should.

### The body in scenes in the world (2026-11-10, 14:10-14:55; unfinished, paused by the owner for usage)

The owner, on the list of known problems: start with the missing body while the view is away from Shepard's ("this one
will be very immersion breaking"), and: could the animated Shepard simply be removed from the scene and only the VRIK
avatar be there, since nothing needs third person in those instances?

**The answer in principle: yes, in this form.** The script needs Shepard's *actor* (it is what the track moves through the
triggers), not his visible body. So the actor stays the script's and the body is drawn where the player stands. Two ways
were tried; the second is in the tree. **Neither shows a body yet, for a reason that turned out to be more basic** (below).

**How the walk animates him** (read from the packages, `conversation-scan --class-dump InterpGroup / InterpTrackAnimControl
--only STA70_02captain`): the ceremony's matinee has a `Player` group with an `InterpTrackMove` and an
`InterpTrackAnimControl` playing `EX_Walk` and `EX_WalkAscend` (anim sets `HMM_EX_Pistol`, `HMM_DP_WanderSouth`). The
engine applies it through the pawn's events `BeginAnimControl` / `SetAnimPosition` / `FinishAnimControl` (ProcessEvent
from native code). Our ProcessEvent hook has post-handlers only (`engine/ProcessEvent.hpp`), so the event cannot be
withheld; the walk animation would have to be taken out at the pose (a slot node's weight around `UpdateSkelPose`, as
`game/CombatStance` does for the combat node, or a graft of a standing pose over the legs, `core/ik/PoseGraft`). Not built.

**Way 1, abandoned: the mesh component placed in the world by itself** (`PrimitiveComponent.SetAbsolute(true, true,
false)`, `SetTranslation`, `SetRotation` on `Pawn.Mesh`, every camera update). What it ran into, each seen in the log:
- **The game re-fits the mesh's height to the floor every tick** (BioPawn's mesh adjustment: `bEnableMeshAdj`,
  `m_fMeshAdjRate`, `MeshAdjSpeed`, `m_oMeshAdjLastBase` in the SDK headers): it rewrote `Translation.z` between our
  writes; with the translation absolute (world numbers) the body ended 44 to 115 m under the player
  (`before this tick's placement the component's translation was (-7816, -7050, 53478)` against a wanted z of 62002).
- A component's new transform is taken up with the next tick's deferred updates, after that re-fit: the frame was drawn
  with the dragged height. `Actor.ForceUpdateComponents(false, true)` right after the write fixed the transform (the head
  bone then read at the player's place) but the body still did not show.
- Giving the mesh back in the middle of a tick (as a conversation began) left the head bone readable at a nonsense place
  for that tick (z 103): the conversation took its place from it, decided "out of sight: cut", and put the view in the
  void. Anything that reads Shepard's head bone must not see a transform in flux.
  Reverted (`git checkout` of the two files); nothing of it is in the tree.

**Way 2, in the tree: his bones are slid there** (the body follower, which already slides the whole body under the view
on foot, at most 0.75 m):
- `conversation_world::BodyAway()` (`game/ConversationWorld`): on while a scene in the world has the view detached from
  the pawn and `[Cinematics] worldBody = player` (the default; `hidden` is the old behaviour); gives the rise, the
  player's floor above the pawn's. `HidePawn` no longer hides the pawn then.
- `game/BodyFollow.cpp`: while away, `TorsoGoal::maxBodyShift` has no limit, `TorsoGoal::bodyRise` (new, `core/ik/TorsoPose`,
  unit-tested in `tests/Main.cpp`) lifts the body by the floors' difference, and the legs do not take the pawn's speed
  (it is the script's walk, not the player's).
- `game/ArmIk.cpp`: a view held away from the pawn counts as first person whatever its distance from his animated head
  (else, past 0.75 m, arm IK chose the third-person anchor and the body follower did nothing).
- The log then says what was wanted: `torso: large correction (first person: the whole body where the player stands,
  away from the pawn): ... the view 7.61 m from where the game put Shepard, body slid 7.61 m; Shepard's head 0.01 m from
  the head's pivot`. All three builds were not run on this; `build` (mod and tests) compiles and mevr-tests passes.

**What the snapshots show, and the real blocker.** The test: the ceremony from its save with a replayed pose, the head
pitched 50 degrees down and both hands in front of the chest (`make-walk-recording.py look-down.mevrrec 4 --stick 0
--pitch -50`; `conversation-from-save.ps1 -All -Only 81 -Replay ... -Snapshots 4,13,32`), three left-eye snapshots made
into a contact sheet.
- **On foot the test sees the body**: arms, hands and chest fill the lower half of the frame (the baseline, after a
  quick load of the same save).
- **In a scene in the world it sees none**: not during the scripted walk (way 1 or way 2), not after the view has
  joined Shepard at the podium, and **not in an ordinary conversation where the view never leaves him** (Avina, slot 89,
  `-Pace full`, snapshot 8 s in), in a fresh game process too. So the player's body is not drawn in the conversation and
  cinematic modes at all in these runs, and this, not the distance, is what has to be solved first. It also fits the
  owner's first note better than my first reading did ("the assets near me disappear when the council talks, reappear
  when it is my turn"): I had put that on Halcyon clearing its hides in far shots and called it fixed from the log alone.
  **It was never checked with a picture. The lesson of the morning, again.**
- In the first snapshot of a walk, a pair of legs is on the stairs where Shepard's actor is: the drawn mesh was at the
  actor while the body follower's log said it had slid the body to the player. So in the cinematic mode the bone write
  may not be what is drawn either (a second pose update after ours, e.g. the matinee's `SetAnimPosition` re-posing the
  mesh): a second unknown, separate from the first.

**What is known about the first unknown.** Halcyon's first-person code (`head_aim.cpp FpApplyHides`): "the engine hides
these when the camera sits inside the pawn; the IgnoreHiddenActorsMembership bit makes them render anyway"; it shows the
pawn (`SetActorHidden(false)`) and sets that bit on the pawn's components, the body mesh and the attached actors,
**once, when its signature changes**, not every frame. During the runs its log says `[FPHIDE] cleared (mode
'BioCameraBehaviorConversation')` at each conversation's first frame and `applied (anti-cull on ...)` 30 ms later, and
stays applied. So the hides are up and the body is still not there: either something hides the pawn again after that one
application (the conversation or cinematic camera's own handling, a per-view hidden-actors list that the bit does not
cover in these modes), or the body is culled by something that follows the director's camera. Reading the flags over the
console (`mevr instances SkeletalMeshComponent <pawn>`, `mevr props`: `HiddenGame`, `bOwnerNoSee`,
`bIgnoreHiddenActorsMembership`, `LastRenderTime` against `WorldInfo.TimeSeconds`) was inconclusive: on foot the body
mesh reads shown with a `LastRenderTime` far behind the world's time (it is only "rendered" while in the frustum: the
fake head looks straight ahead), and mid-conversation the query came back without the mesh rows (the pawn's name had
changed with the load).

**What to do next, in this order:**
1. **Settle the baseline with pictures**: the same look-down test in an ordinary conversation on the players' build of
   19dad44 (before today's body work), at several times across lines of the NPC and of Shepard. If the body shows on
   Shepard's lines and not on the NPC's, visibility follows the director's camera; if never, the pawn is hidden in the
   mode.
2. **A log line instead of console guesses**: every two seconds while a scene is in the world, the pawn's `bHidden`, the
   body mesh's `HiddenGame`, `bOwnerNoSee`, the ignore-hidden bit and `LastRenderTime` beside `WorldInfo.TimeSeconds`
   (new `FieldRef`s with their `ReflectCheck` rows), and the shot's camera distance.
3. **If the engine hides him again**: apply the anti-cull every camera update while `ConversationInWorld()` (a seam in
   `FirstPersonTick` making the hides non-edge-driven there, or the same bits written by the mod), and check with the
   picture.
4. **Whether the bone write is what is drawn in the cinematic mode**: log Shepard's head bone in the world right after
   our write and again at the next tick's start; if a later pose update undoes it, find it (the matinee's anim control)
   and write after it, or force the pose once more.
5. Only then judge way 2 by eye (the body under the player during the walk, on stairs: `bodyRise`), and then the legs
   (the script's walk animation plays in place under a standing player: a standing graft, or the slot's weight).

#### The plan for the blocker (2026-11-10 evening; read from the code, nothing run)

The owner: plan how to address the blocker, decompiling the game where the fault may lie. Read with Ghidra (the isolated copy:
the shared project was locked by another session's idle MCP server from 2026-11-09 13:10 until the owner had its process stopped that evening; `tools/ghidra/decompile.ps1` works on it again) and the natives dump. Behaviour in
words, with addresses; nothing renamed in the project (the names are in `tools/ghidra/symbols-le1.csv`).

**Every way the game stops drawing a pawn, as the code has it:**

| Way | Where | What it does | Covered today? |
|---|---|---|---|
| `Actor.bHidden` (actor +0x270, bit 2) | `AActor::SetHidden`'s body `0x2BF170` | sets the bit and re-registers the actor's components (`0x2D6130`), so the renderer learns of it; a raw write of the bit would not reach it. 23 native callers: the squad's move marker (`0xC76580`), squad orders, the grenade and projectile code (`0xC3F360`-`0xC54060`); none in the camera or conversation code by their headers (`0xB32000`/`0xB32C20`, `0xB4F7A0`, `0xA7CED0`, `0x3F7FE0` not read through: **[verify]**) | Halcyon shows the pawn **once**, when its signature changes |
| `BioPawn.RequestHidden` (`m_nHiddenRequestCount`, pawn +0x9F4) | body `0xA9F3B0`, vtable slot `0xC48`; script thunk `0xB923F0` | a **counted** hide: the first request hides the pawn (the row above) and his weapons (`0x9F34B0` on the equipment), the last release shows them. No native caller: **only UnrealScript asks** | no: Halcyon's `SetHidden(false)` and our `HidePawn` go around the count, so a request made after Halcyon's one application hides him until its own release |
| `PlayerController.HiddenActors` (+0x5C8) | filled by `USFXCameraMode::DoCameraCollisionNative`'s body `0xCB0040` (pawns with `m_bHideWithCameraCollision` the camera is inside of), emptied by `SFXCameraNativeBase.ResetHiddenActors` (`0xCB7A40`: camera +0x4B8 is the controller) | per view, in gameplay camera modes only; the conversation camera is not one of them, so the list stays as the last on-foot frame left it | yes: Halcyon's `bIgnoreHiddenActorsMembership` bit |
| component flags (primitive +0x168) | `HiddenGame` 0x4, `bOwnerNoSee` 0x10, `bOnlyOwnerSee` 0x20, **`bIgnoreOwnerHidden` 0x40** (script native `PrimitiveComponent.SetIgnoreOwnerHidden`, `0x25E2E0`) | per component; the last one draws a component whose actor is hidden | Halcyon uses `bOwnerNoSee` for the head only |
| not hidden, not drawn | visibility decided for another camera (Umbra's resolve, the frustum, draw distance), bounds left at the actor while the bones are elsewhere (way 2 moves bones, not bounds), a skeleton not updated while "not rendered" | | unknown |

`SFXCameraNativeBase.TraceCamera` (`0xCB9BF0`) is the aim trace, not a hide. The three other callers of a vtable slot `0xC48`
(`0xC724F0`, `0xC72720`, `0xC78C60`) are the player controller's own slots, not the pawn's.

**What this says about the two leads.** The owner's morning note ("the assets near me disappear when the council talks and
reappear on my turn") is about more than the body and follows **who the director's camera looks at**: that is the last row
(visibility for another camera), and far shots are exactly where our hold of the game's camera is weakest (the camera mode is
unreadable there: a camera actor is the view target). The look-down pictures (no body in an ordinary conversation, where the
view never leaves Shepard) fit either a hide by flag (rows 1-2: something hides him after Halcyon's one application) or the
same camera problem. One run separates them.

**Phase A: one instrumented run that names the family** (fake headset; mod only, no Halcyon change; about an hour).
1. `convo: body:` once a second while a scene is in the world, and for the last second on foot before it: the pawn's `bHidden`
   and `m_nHiddenRequestCount`; the body mesh's `HiddenGame`, `bOwnerNoSee`, `bOnlyOwnerSee`, `bIgnoreOwnerHidden`, the
   ignore-membership bit; whether `HiddenActors` holds the pawn; the mesh's `LastRenderTime` beside `WorldInfo.TimeSeconds`;
   the mesh's bounds origin and Shepard's head bone in the world beside the held view's place; the director's camera (its
   place, its distance from the held view, the view target's class); who speaks. New `FieldRef`s with `ReflectCheck` rows;
   the same block on `mevr convo body`.
2. `mevr calltrace 20 sethidden requesthidden setcinematicmode setownernosee sethiddengame` across each scene's start, one
   line of the NPC's and one of Shepard's: every script caller with its frame. This is the only way to see row 2's callers
   short of the offline search below.
3. Pictures at the same instants: the look-down recording, a snapshot on foot, on an NPC's line, on Shepard's line; the Avina
   conversation (slot 89), the comm room (slot 87: far and near shots), the ceremony (slot 81: the walk, the podium, a
   councillor's line). First on the players' build of 19dad44 (the baseline the note still owes), then on the branch.
4. Offline, alongside (30 s a run): a `conversation-scan --script-calls RequestHidden,SetHidden,SetCinematicMode` mode that
   lists every UnrealScript function calling them (Legendary Explorer's decompiler), so scenes the run did not play are covered.

| The log and the pictures say | Family | Go to |
|---|---|---|
| `bHidden` 1 or the count above 0 during the scene | hidden by flag | B1 |
| flags clear, `LastRenderTime` stale, body back on Shepard's lines or when the director's camera is near | culled for another camera | B2 |
| flags clear, `LastRenderTime` fresh, still no body in the picture | drawn, but not where the view is (bones, bounds, the pose) | B3 |

**Phase B1: hidden by flag.**
- The fix that does not care who hides him: `SetIgnoreOwnerHidden(true)` (the game's own function, on the game thread, so the
  render side is updated) on the body mesh, its pose followers, the head group and the drawn weapon while a scene is in the
  world; put back at its end and at unload. Halcyon's head hide (`bOwnerNoSee`) stays as it is. Before building: read
  `0x25E2E0` and `0x2D6130` to confirm the flag reaches the render proxy and where the proxy takes the owner's `bHidden`.
  Read since: `SetHidden`'s `0x2D6130` marks every component of the actor for a deferred update (or re-attaches them), and
  `SetIgnoreOwnerHidden` is the primitive's virtual at vtable +0x538; the proxy's own read of the two is still to do.
- If script requests the hide (`RequestHidden` from the conversation or the cinematic switch): leave the game's count alone
  (other code releases it later) and rely on the flag above; only if the weapons' hide matters, answer the request for the
  player's pawn through `engine/call_function`'s skip handler during scenes in the world.
- Only if both fail: Halcyon's anti-cull applied every camera update while `ConversationInWorld()` (a third `[MEVR]` seam;
  its own comment says why it is edge-driven: writes into freed combat actors).

**Phase B2: culled for another camera.**
- Measure first: per frame, the POV the scene view is built from against the held place (the `CalcSceneView` seam already
  sees both), split by shot kind. If far shots show the director's place, the `UpdateCamera` post-handler's write is not what
  the view is built from there (a camera actor as view target, or the cache rewritten after us): find the later writer by
  logging `CameraCache.POV` at the handler's end and again at the seam, then hold it there.
- Then Umbra: `UMBRA OCCLUSIONCULLING` off through the console during a councillor's line. Body and assets back: the query
  camera is the director's. Read `UmbraResolveViews` (`0x8BFBB0`), the main view's counterpart of the capture's camera update
  (`0x4FD550`) and what `BeginRenderingViewFamily` hands them; feed them the held view (the galaxy map's and the scope
  capture's seams are the pattern). Not Umbra: the frustum and draw-distance tests in the scene renderer's visibility pass
  with the same method.
- Streaming and LOD follow the same camera: the owner's "assets" may be those; check texture streaming's view list the same way.

**Phase B3: drawn, but not where the view is** (also the second unknown: legs seen at the actor while the log said "slid").
- Log Shepard's head bone in the world right after our write and at the next tick's start, and count `UpdateSkelPose`
  (`0x592440`) calls per frame for his mesh with which of them our writer wrote: a matinee's `SetAnimPosition` may pose him
  again after us. Write after the last, or force the pose once more.
- Bounds: the bone slide leaves the component's bounds at the actor, 7 to 20 m away; a mesh whose bounds are off screen is
  not drawn. Read the bounds update (`bConsiderAllBodiesForBounds` is already set by ArmIk; `BoundsScale`, the parent-bounds
  fields) and grow or move the bounds with the body while away.
- If the pose cannot be held against the matinee: a body of our own instead of his (a second pawn spawned as the UI-world
  figure code does, posed by arm IK, the script's Shepard hidden). Heavier; only if the two above fail.

**Phase C: by picture, then the rest.** The same contact sheet (on foot, an NPC's line, Shepard's line, the walk, the podium)
until every frame shows arms, hands and chest; then way 2's rise on the stairs and the standing graft for the legs (the
script's walk plays in place under a standing player); all three builds and the tests; the checklist's line for the owner.

#### Phase A, run (2026-11-10, 15:50-16:03, fake headset): he is culled, not hidden; and one of the culls is ours

The owner: "Yes, start on phase A". Built as planned (`convo: body:`, `[Cinematics] worldBodyLog = 1` or `mevr convo body log
on`; `mevr convo body` prints the line once; `conversation-from-save.ps1 -Before "<console line>"` runs a command just before
a scene is fired, here the call trace). Avina's conversation from its save (slot 89), seven runs of a minute each in one game
process, under the look-down recording (the head 50 degrees down, the hands up; the recording's head stands 0.43 m behind the
room's centre), left-eye snapshots at about 6 and 12 s. The baseline on the players' build of 19dad44 was not run: the answer
came without it.

**No flag hides him.** In every run, on every line: `bHidden` 0, hide requests 0, the mesh's `HiddenGame`, `bOwnerNoSee`,
`bOnlyOwnerSee`, `bIgnoreOwnerHidden` 0, Halcyon's ignore-membership bit 1, `HiddenActors` empty. The call trace over the
first 25 s saw no script hide at all: `ResetHiddenActors` every frame from `SFXPlayerCamera.UpdateViewTarget` (on foot), 12
`SetOwnerNoSee` and 2 `SetHidden` from native code (Halcyon's own application). `BioPawn.RequestHidden` was never called.
So rows 1 to 4 of the table above are out for this scene; phase B1 is not needed.

**He is not rendered** (the mesh's and the pawn's `LastRenderTime` stand still), and what decides it is which camera the
visibility is worked out for:

| Run (the head 50 degrees down unless said) | First shot (the director's camera 3 m from the player) | From the second shot on (the director's camera 0.53 m from the player) |
|---|---|---|
| as shipped (the game's camera held, Umbra on) | not rendered; **the floor under the player is not drawn either** (the lake shows through) | **not rendered**, 25 s on end; the floor is there |
| Umbra's occlusion culling off (`UMBRA OCCLUSIONCULLING`) | not rendered; floor missing | **rendered** (0.00 s ago): his feet in the picture |
| `worldCamera = 0` (the game's camera left to the director), Umbra on | not rendered; floor missing | **rendered** |
| `worldCamera = 0`, occlusion culling off | not rendered; floor missing | **rendered** |
| `[Performance] umbraEyeMatrix = 0` (the camera held, Umbra on) | not rendered | **rendered** |
| the head level, or 25 degrees down (as shipped) | not rendered | not rendered (nor on foot afterwards: at those pitches the pose has nothing of the body in view, so these two say nothing) |

What that says:
1. **From the second shot on, our own lever culls him.** With the game's camera held at the player's place, Umbra's occlusion
   queries drawn "with the eye's own matrix" (`umbraEyeMatrix`, research/35 attempt 36, on by default) reject his body; with
   the lever off, or the occlusion test off, or the game's camera left where the director has it, he is drawn. On foot the
   same lever is on and he is drawn, so it is the pair (the held camera and the eye's matrix) that disagrees, not either alone.
   **[verify] which two matrices differ in a held frame** (the lever takes the view's +0x1D0 and its origin; the hold writes
   the POV level, at the hold, and the render-time swap and the head come after): read `plugin/RenderLevers`' lever beside the
   `CalcSceneView` swap, log both matrices for a held frame.
2. **In the first shot nothing tried brings him or the floor back.** The director's camera is 3 m away then (the wheel is
   up over Shepard's face), and the floor piece under the player's feet is gone with or without the occlusion test and with or
   without the held camera. This is the owner's morning note in one picture ("the assets near me disappear when the council
   talks"), and it is not the occlusion test: something earlier in the visibility (Umbra's own camera or cell, or the
   frustum) is the director's in that shot. The log said `the game's camera held at the player's place too` from the scene's
   first tick, so the POV write alone does not govern it. **Open: the next thing to measure** (phase B2's first step: the
   POV at our handler's end against what the scene view and Umbra's camera are built from, per frame, in that shot).
3. **Where he is drawn, he is not under the player.** The feet are 0.6 m ahead of the view and no arm is in the picture,
   while on foot the same pose shows both arms and the chest: the body follower and arm IK put him under the view on foot and
   do not in a conversation (the view is "the view on foot", the recording's head 0.43 m behind the pawn; his head bone stays
   0.60 m from the view all conversation). The body follower does run there, and believes the two agree: `torso: ... the
   view 0.06 m from where the game put Shepard, body slid 0.06 m` at the conversation's start. So the frame arm IK and the
   body follower are given for a held view (`view_anchor::Hold`) and the place the eyes are drawn from differ by the head's
   offset in the room (half a metre here; a player at the room's centre would see little of it): the hold is taken from the
   on-foot eye, lean included, and the frame is not. **[verify]** by logging both in one line.

**What to do next** (phase B2, in this order): (a) the two matrices of point 1, and the lever made to agree with a held view
(or stood down while a view is held: one line, to be measured against the popping it was built to stop); (b) the first shot's
camera, point 2; (c) the body under the player, point 3; then the same three pictures again, and the ceremony (slot 81) and
the comm room (slot 87), which were not run.

### The voices (2026-11-10: read from the code and the packages by a research agent; nothing built, nothing run)

The owner: "is there anything we can do to make it sound more like spatial audio?"

- **LE1 has no Wwise** (that is LE2 and LE3): it is ISACT over OpenAL (`AudioDeviceClass=ISACTAudio.ISACTAudioDevice`,
  3,942 `.isb` files, Creative's `OpenAL32.dll` router with the "Generic Software" device: no HRTF).
- **A line's voice** is played by `USkeletalMeshComponent::PlayFaceFXAnim` (0x587F40; also from
  `UBioActorBehavior::PlayVOElements`, 0xBFA6C0) on the speaker's facial audio component. **Whether it is placed is decided
  per sound cue**: a cue whose first node is not a `SoundNodeAttenuation` is played with spatialization off and forced to
  the centre (`FISACTSoundSource::Update`, 0x7BA90, puts it at the listener's front vector). Of the 32,373 dialogue cues in
  the localized packages 26,703 (82%) have no attenuation node: **centred, wherever the speaker stands**; 4,408 are placed
  with a 60 m falloff (barks), 661 placed without falloff, 601 other radii.
- **The listener** is set once a frame by `FViewportClient::Draw` (0x4C71A0) through `UAudioDevice::SetListener`
  (0x3043B0) after `CalcSceneView`: its place is the camera's (our held place, without the headset's offset), its
  orientation the view matrix's, which Halcyon has already turned by the head's yaw and pitch. So the listener should
  already turn with the head (read, not measured); `BioCheatManager.m_fMicPos` can move its place toward the pawn.
- **The cheapest real fix**: pre-hook `FISACTSoundSource::Update` and, for the wave instances that belong to the
  conversation's speakers (a table the mod publishes each tick: their audio components and head places), set
  spatialization on, the forced centre off and the place to the speaker's head. The flags are re-sent every update, so it
  works mid-line; Shepard's own lines stay centred. That gives left/right panning that follows the head; distance
  loudness is ours to add. Offsets are from the decompile; that ISACT honours them for these waves is [verify], as is the
  thread the update runs on. New addresses need rows in `engine/Addresses.hpp`.
- **Real binaural** (height, front against back) would mean OpenAL Soft with HRTF in place of the Creative router (the game
  loads `OpenAL32.dll` by name): a shipped DLL and a check of the reverb's EAX property sets. The largest step.
- No setting does it: there is no "3D dialogue" switch, and the sound groups' per-group speaker settings are not set for
  dialogue.
- To measure first: the OpenAL listener's front vector under a replayed head sweep; a centred line against a placed one
  (most of `sta20_avina_keeper` against `sta20_hanar_religious`) as left/right loudness in a loopback recording.

## To check in the game

In this order, on the fake headset, when the game folder is free (saves backed up first, as always):

1. The hooks go in: `hooked UBioConversation::SetPositionRotator`, `hooked ABioWorldInfo::InterruptConversation`, and the
   address table's patterns hold.
2. Item 4: the medical bay conversation (research/59 measured 25 jumps undone there) and the Garrus wheel conversation:
   `sourceKept` above zero, `kept` zero, nobody moves, people still turn to the speaker; then `worldStaySource = 0` for the
   same scene to compare.
3. Item 5: `mevr convo walk` toward a streaming volume's edge inside a leash (the C-Sec hall has several): the walk stops
   with the log line, `mevr stream chunks` unchanged; `worldMoveTriggers = 0` to see the touch it prevents.
4. Item 1: `bark-refuse2` again (the Garrus argument, then `TowerSpecificBanters` and `AmbientDiplomats` fired half a second
   later): the banter `held`, the diplomats `refused` (a bark), the argument's 13 s all spoken, the wheel conversation after
   it, and the banter's start `fired again` a second after the wheel conversation ends. Then the six chains left out play as
   conversations again.
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
- 2026-11-09, 17:50-17:56: **the wake-up scene lives under the Citadel's map, and item 4 is checked.** The owner pointed at
  the old end-game career as a basis (John00: September 2024, 15 h, level 41 to 45; `John00_96` at the Citadel with six maps
  kept, `John00_AutoSave` at the final battle). Reading what each career's saves keep per map settled the question the
  streaming could not: all three careers keep `BIOA_NOR10_07wake_DSG` and `BIOA_NOR10_06wake_DSG` under **`BIOA_STA00`**, not
  under `BIOA_NOR00`. The Normandy Shepard wakes up on is the one docked at the Citadel, so the launcher's catalogue names the
  wrong persistent level for that scene (it derives the level from the chunk's area prefix), and `BIOA_NOR00` has no such
  streaming level to show. `open BIOA_STA00` and nothing else starts the scene by itself 23 s later (its story start is the
  arrival after Eden Prime; the launcher's teleport to its target had always cut it off after a second or two).
  - **The scene, played to its end in the world mode** (195.1 s, 40 lines, 43 shots, 12 wheels, driven by `mevr convo
    pick`/`skip`): `28 placements onto marks withheld at their source (the farthest mark 2.91 m away)`, `0 jumps onto marks
    undone`, every tracked person ends 0.00 m from where it stood, and people still turned (the facing applies). The first
    withheld call came at 23.1 s.
  - Seen beside it: one person (BioPawn_3) was placed 3.93 m away 0.1 s after the opening ended, before the mod had seen him
    once, so neither the hook nor the undo knew him: part of the opening in effect, but later than the player's place was taken.
    And Shepard's own body snapped 2.25 m back onto his pawn at 40.7 s, the known one jump of a conversation straight after a
    cutscene (research/59), which moves the player once.
  - **For the tools:** the catalogue should take a start's persistent level from the persistent levels whose streaming lists
    name its package, not from the name; then this scene and any other of the docked Normandy launch like the rest. An
    end-game save is a good basis for scenes in the six maps it has visited with a full squad, but it has played every beat, so
    early scenes need the rewind from the game's data either way; for this scene no save was needed at all.
- 2026-11-09, 18:30-18:45, the rest of the in-game checks (the owner: one try and one retry each, then stop and coordinate):
  - **Item 5, checked.** In the wake-up scene the walk gathered `9 triggers within the leash of the owner` (two
    BioTriggerVolumes, a TriggerVolume, a BioTrigger, five streaming volumes) and 0.1 s into the first walk `the walk stops at
    BioTriggerVolume_1: the step would enter it`. Over six walk commands in four directions: `the player walked 2.50 m in 196
    steps; ... a trigger's edge 953 (BioTriggerVolume_1), 2479 refused (no floor, a ledge, a wall, SetLocation)`; the scene ran on
    and its placements stayed withheld. The medical bay is cramped and that one trigger sits right beside Shepard's mark, so the
    guard leaves little room there: it may want to let through triggers that have nothing left to fire (not looked at).
  - **Item 1, the bark refusal: not shown, and the opposite seen.** With the Garrus argument playing as barks, a level banter's
    start (`BIOA_STA70_01_PLC ... TowerSpecificBanters.BioSeqAct_StartConversation_0`) fired half a second later. No refusal
    was logged; the argument ended at once (0.5 s after it began, against 13 s uncut) and the level's script, told the scene
    was over, started Garrus's wheel conversation. So a newcomer the rule lets through cut the scene short before it was known
    whether the newcomer would even start (it did not start). Which side of the rule it fell on is not established (the scan
    has no record of that action's conversation; the rule lets through anything that is not a bark by the game's own data). The
    first try missed the window (the launcher waits 12 s after firing).
    **Open decision:** what a newcomer does to a flowed scene. Refusing every newcomer is what the game does during the
    conversation the scene was authored as, but with the player free to walk into another scene's trigger it could lose that
    scene's start for good; letting newcomers through loses the flowed scene's remaining lines, as seen; a third way is to hold
    the newcomer and start it when the flowed scene ends (refuse now, fire the same start action again at the end).
  - **Item 3:** no presence rule named any of the eight conversations played this session (the wake-up scene five times,
    two of them in full; the Garrus wheel conversation three times), so no false alarm on ordinary staged scenes; none of them is a scene the rules should
    name, so the rules themselves are still unseen.
  - The game folder is back to the 0.2.0 candidate (102 files in the ASI folder, the Win64 folder, BIOEngine.ini and the 14
    saves all match their pre-run hashes; the candidate's SHA-256 as the release session gave it).
- 2026-11-09, evening: the owner chose **hold**, to be codified for a scene builder. Built as the first scene rule ("Scene
  rules", above): the decision and the queue in the core with unit tests, the facts and the re-fire in `game/ConversationFlow`.
  The cause of the 18:39 failure found on the way: the scene test was "every start ordinary", and `sta70_garrus` has an
  ambient start of the game's own; the test is now the line the scene is on. Builds and tests pass; not run in the game.
- 2026-11-09, 19:30-19:40, **the hold checked in the game** (fake headset; the candidate put back afterwards, every hash
  matching). At C-Sec, with the Garrus argument playing as barks and other starts fired half a second after it:
  - *Barks:* `TowerSpecificBanters` (which starts `sp_news_vids`, the news terminal's narration) and `AmbientDiplomats` were
    `refused` (three log lines), the argument ran its full length and the wheel conversation began 14.6 s after the
    argument's start. So the 18:39 cut was the scene test alone: that newcomer was a bark by the game's data all along.
  - *Another scene:* `ChorbanWithKeeper.BioSeqAct_StartConversation_1` (`sta20_chorban`, every start ordinary) logged `held,
    its start fires again when the scene and what follows it are over` at 19:37:18.962; the argument ran in full; the wheel
    conversation ran from 19:37:33.0 to 19:37:49.2; at 19:37:50.265 `the held start of sta20_chorban ... fired again
    (ForceActivateInput(0) called true); 0 still held`, and 17 ms later `conversation 15768 began: sta20_chorban`. Nothing was
    lost on either side.
  - Not looked at: what the held action's "failed" output did in between (nothing visible here), and a held scene whose
    owner is far from the player when it finally starts (Chorban's stage is 85 m from C-Sec's desk: the game's stage placed
    Shepard there, as for any conversation started from afar).
- 2026-11-09, 19:50-20:50, **the sweep's phase 1 built** ("The sweep: progress"): `tools/game-slot.ps1`, the catalogue's
  level from the streaming lists (36 starts moved), `tools/conversation-sweep.py predict | plan | judge` with its tests,
  and the flowed scene's summary line in the mod. No game run. Next: phase 2, the reach matrix, which is the first use of
  the game slot on the real folder.
- 2026-11-09, 20:50-23:25, **the sweep in the game, and what it changed in the plan** ("The sweep: progress" has each part):
  the game slot used for the first time (held since 20:54, the 0.2.0 candidate backed up); the reach matrix (215 of 237
  scenes in 16 levels; Shepard must float while chunks stream); the canary set validated both ways (the wake-up scene and
  the Garrus argument pass, the run with `worldStaySource = 0` is caught as `undone`); passes are sampled by a
  conversation's make-up and each level's place risks (133 of 290 scenes) and paced (`skip`, `shepard`, `full`); a scene
  can be fired through its own event; the 21 moments where a level-script matinee walks Shepard are a plan of their own;
  the opening without a cut is built (`worldOpening = auto`, not seen in the game).
  **Running at 23:25:** the sampled first pass with skips (`%LOCALAPPDATA%/MEVR/sweep/2026-11-09-sample1`, 116 scenes in
  the levels that open, then its judge's `report.md`), and a reading of the natives for move tracks, teleports, the black
  screen and the cinematic mode (question list in the session; nothing written yet).
  **Next, in this order:** judge the sampled pass and triage its exceptions; rebuild the mod (the game is using the DLL
  from 20:51: no mod build while a pass runs) and try `worldOpening = auto` on Chorban (stay) and the council hearing
  (walk in), then make it the default; play the moments with `-Fire event` to see what the cinematic mode shows today;
  build the authored-scene rules from the natives' reading (the player's move track and a same-space teleport withheld,
  the walk during a cinematic mode); the full-length pass on `plan --sample` plus the exceptions; the side worlds (the
  runner now leaves the Mako) and Therum (a save); the headset sample; land on main (all three builds and ctest); return
  the game folder with `tools/game-slot.ps1 return -Quit` and tell the release session.
- 2026-11-10, 00:00-05:15, **the night in the game** (each part is under "The sweep: progress"): the opening without a cut
  seen on Noveria and made the default; the walk to the council held in first person through the host's new seams, its
  teleport withheld, the hearing opened as a walk-in; the sampled pass (120 scenes, no crash, 8 dips in all) and its
  exceptions fixed and re-run (authored walks left to their tracks, the view no longer moved by Shepard after such an
  opening, the floor fight); the canary set three times, as expected each time; the moments (two of sixteen reachable
  from a fresh level); the set pieces in the world (six now play there). At full length (`-Pace full`): the walk of Wrex
  49 s and the prisoner 52 s with `trackWalks` 1 and nothing undone, the wake-up scene 311 s with 30 placements
  withheld, Talitha 86 s. All three builds and ctest (24 of 24) pass on the state that lands.
  **Open, in the order they matter for play:** (1) a save route for the runner: Therum, the side worlds (the Mako exit
  is built, not run) and fourteen of the sixteen moments need a career's plot; (2) `worldMoments` stays `log` by default:
  `walk` is seen only on the council sequence, and a moment whose track carries Shepard somewhere the script counts on
  without a teleport after it would leave him behind; `carry` is built and not run; (3) in a walk-in nobody carries a
  player who does not walk, and the people of the scene stand on their marks at once instead of walking there; (4)
  Anderson's swap at the podium, the black screen (taken down by the mod now: not seen since) and the director track's
  fade; (5) the nine set pieces not yet seen in the world; (6) 29 scenes whose bare start fires nothing want the event
  route in a pass; (7) the headset: everything in "The headset sample".
- 2026-11-10, 05:15-06:30, **the morning round** ("The morning round" under "The sweep: progress"): `worldMoments = walk`
  the default and the council sequence seen on defaults; the side worlds sampled (the Mako exit works); a walk's last
  step accepted instead of fought (`cw::Insists`); `IsAlive` safe on freed memory after a hot reload ended the game; the
  scan's `via.chain` and the runner's step past a plot check (three more walk-ins fired, becoming a Spectre whole in
  first person); the menu's First person (preview) button. Canary 3 of 3; all three builds and ctest on the state that
  lands.
  **Open, in the order they matter for play:** (1) the headset: "The headset sample", now reachable from the menu;
  (2) Shepard's body strays 0.8 to 1.4 m from a player who stands still in some scenes (`eye-far`: the Normandy's
  debriefs, four side-world scenes); (3) in a walk-in nobody carries a player who does not walk during the
  conversation itself, and the scene's people stand on their marks at once; (4) a walk-in whose track starts more than
  3 s into the cinematic mode is not held (the rachni); (5) Therum does not open and the walk-ins and scenes that need
  a career's plot (a save route for the runner: `conversation-launch.ps1 -Save` has the pieces); (6) nine set pieces
  not seen in the world, 29 scenes whose bare start fires nothing; (7) Anderson's swap at the podium and the director
  track's fade; `carry` is built and not run.
- 2026-11-10, 06:40-08:46, **the guided walk-in and the council's hearing** ("The guided walk-in, the hearing that
  ended at once, Therum from a save"): a walk-in nobody walks is guided to Shepard's mark; the hearing that ended in its
  first tick in 9 of 16 walk-in openings traced to the mod's put-back of Shepard after a streaming volume's touch, and
  ruled out by keeping the stage's placement when a conversation begins mid-walk (the hearing has its dip again);
  Therum's battlemaster scene played from a rewritten career save. Canary 3 of 3; all three builds and ctest on the
  state that lands.
  **Open, in the order they matter for play:** (1) the headset: "The headset sample", from the menu's First person
  (preview); (2) walk-ins and moments where the view walks and the pawn stays put (no touch of the mod's making: the
  hearing without its dip, marks inside triggers reachable); (3) Shepard's body strays 0.8 to 1.4 m in some scenes
  (`eye-far`); (4) a save mode for the sweep's runner (Therum and the plot-bound scenes: the launcher's `-Save` works);
  (5) a walk-in whose track starts more than 3 s into the cinematic mode is not held (the rachni); (6) nine set pieces
  not seen in the world, 29 scenes whose bare start fires nothing; (7) Anderson's swap at the podium; `carry` is built
  and not run.
- 2026-11-10, 08:55-10:10, **the view walks, and the pause** ("The view walks; a moment really shown; saves for the
  headset"): the council's hearing without a cut by a walk-in of the view alone (4 of 4); a moment's view found never to
  have been rendered and fixed, by the first snapshots of one; a dip in place of the script's black screen; opening-only
  runs; validation saves for the headset (ten written, none loaded yet). **Paused at 10:10 at the owner's word for his
  headset test**: the dev build and the saves are in the game folder, the game slot is still held, the fake headset is
  off. The work is a checkpoint on the branch (501cf33), not on main.
  **Owed when the work resumes:** all three builds and ctest on this state; the canary set and a sampled opening-only
  run on it; one validation save loaded on the fake headset; land on main; return the game folder (unless the owner
  keeps this build in). **Open after that:** NPCs walking to their marks; the pause and floor marker as a fallback; a
  fade wherever a person in view is placed; the cinema screen's flash at a moment's start; Shepard's body straying
  (eye-far); a save mode for the runner; the unseen set pieces.
- 2026-11-10, 10:20-10:31, **the players' build for the owner's headset test** (the owner: "base your build off of the now
  released 0.2.0 candidate so that I can get the improved performance as well"). The branch already contains the released
  commit (`v0.2.0` = 6bb3227 = origin/main; the candidate's ASI says `0.2.0+6bb322770a`), and the game folder's
  `LE1MEVR.ini`, `MELEVR.ini`, `GamerSettings.ini` and `BIOEngine.ini` are the candidate's except what the owner's own
  session wrote (a calibration, `conversations=world`). The one difference was the build type: the folder had the
  development (hot-reload) host and mod, the candidate is the single-file players' build without LExSDKv2. So the branch
  was built as `build-mono` and deployed (`0.2.0+180324d4f2`, the `.modpath` gone; the request files still work, the
  owner's ini has `[Debug] requests = 1`). **Not measured**: whether the development build was slower at all (the owner's
  session before it ran 900 frames per 10 s at 90 Hz). Then, at idle priority: `build`, `build-trilogy` and ctest on this
  state, 24 of 24. Still owed: the canary set and a sampled opening-only run, one validation save loaded on the fake
  headset, the landing on main, the game folder's return. For the fake-headset runs the development build goes back in
  (`deploy-asi.ps1 -IgnoreSlot`), game closed.
- 2026-11-10, 10:45-12:00, **the owner's headset notes, two sessions** ("Who turns, and how", "The ceremony in the
  headset", "The voices"). The dip at the council is acceptable; nobody should pivot on the spot toward each speaker: the
  game's own look-at chain does the looking now, bodies keep their composed facing and re-face only past 60 degrees held a
  second, by the game's `RequestTurnInPlace` (an eased turn if refused); whoever the game gives nobody to look at is left
  alone (the councillors had been swung up to 170 degrees toward each speaker). From the ceremony's logs: his own body
  vanished on the council's lines (Halcyon cleared the first-person hides when a far shot's camera mode could not be read:
  a second seam keeps them), Shepard lurched after every reply (the wheel's stick, still out, walked him: latched until let
  go), and the script's black screen became a dip that covered nothing (now decided by what changed in view). The voices:
  82% of dialogue cues are played centred by design; a hook on the sound source's update could place the speakers' lines.
  All of it unit-tested and built three ways; **none of it run in the game yet**. Still open from his notes: the flash of
  the 3D screen as a moment begins (his first blink), a composition rule for groups, the 0.1 s of 3D screen when the
  controllers lost tracking mid-conversation.
  11:07: the players' build of f339cec is in the game folder for the owner's next run (`0.2.0+f339cecee4`; the one he
  played, 180324d, is kept in `%LOCALAPPDATA%\MEVR\game-slot\previous-asi`). The game slot is still held.
- 2026-11-10, 11:25-13:20, **resumed on the fake headset at the owner's word** ("The script's Shepard, the player's
  view; the fast loop"). A loop of scenes from the validation saves (ten in nine minutes, judged from the logs) found, and
  saw fixed: the flash of the 3D screen at the start of every scripted walk **and of every ordinary conversation**; the
  ceremony that never began for a player who stood still (the script's Shepard is now moved by the script, hidden, and
  the view walks to him); the depth of field left on during walks; authored about-faces in one frame. The game's own
  turn in place does nothing in a conversation. The fake headset's `-Off` no longer restores a weeks-old settings file.
  The guided walk follows the way Shepard went and arrives. Open: the ceremony's dip stays for a player who walks along
  (nine people appear at that instant), the body missing while the view is away from the pawn, slot 84.
- 2026-11-10, 13:30-13:50, **the canary on the players' build, the headset back** (the owner still away). The canary set
  on the single-file build of 19dad44 (`0.2.0+19dad44ebc`, the request files through his ini's `[Debug] requests = 1`): 3 of
  3 as expected, so that build runs in the game. **The look-at chain's reach, first numbers** (the comm room with Anderson
  and Nihlus, 54 lines, the first scene of the day whose people have look-at targets): 54 targets stood 30 degrees or more
  off a body; at 35 off, a head was still 30 off after 1.5 s; at 45 to 56 off, 11 to 49 off: **the chain brings a head
  about half the way in a conversation** (its strengths: head 0.55), the eyes (25 degrees) do the rest, and past about 50
  degrees of body angle the gaze falls short. The rule's 60 degrees is at that edge: 13 bodies re-faced in 190 s there
  (all by the eased turn), against one at every change of speaker before. Not tuned further without the owner's eye.
  The fake headset is off (`-Off` put back its five keys; his settings file kept its `driver_ivry` section), SteamVR
  stopped. His VR00 quicksave, which the save loop wrote over, is back from the game slot's backup (hash checked). The
  ten validation saves are in John02's slots 80 to 89 (80 and 81 now 2.5 m from Anderson). The game folder holds the
  players' build of 19dad44 and is still held by this session's game slot. **A deploy piped into `Select-Object -First
  N` is cut off after N lines**: the fonts, the menu's files and the `.modpath` clean-up never ran (seen as a `.modpath`
  left beside the single-file build; run again without the cut).
- 2026-11-10, 13:44-14:00, **the real headset, and the stick** (the owner away: "test in headset. I won't be available").
  **No headset is connected**: SteamVR, started with his own settings, reports `PlayStation VR2 HMD is not found` and
  `VirtualDesktop: Streamer did not return a connected headset` (the machine's active OpenXR runtime is Virtual Desktop's),
  so nothing can run on a real headset until someone switches one on; every check of this day is the fake headset's.
  **The wheel's stick, in the game**: the ceremony from its save with a recording replayed with its inputs from the moment
  the scene is fired (`conversation-from-save.ps1 -Replay`, `make-walk-recording.py`: the left stick out 16 s of every 20):
  40 lines, 9 wheels, `the stick that picked the reply is still out` 7 times (`wheelSticks` 7), and the judge finds no
  move after any of Shepard's lines (the owner's session: 4 of 7); with the stick let go and pushed again he walks (85 m
  in all). A first try with the replay running while the save loaded was void: the stick walked Shepard out of the
  chamber before the scene was fired, the level changed and the game was left without a pawn (restarted). The canary's
  `open` had written a VR00 autosave and the save loop a VR00 quicksave: both are back from the game slot's backup (the
  folder is byte for byte the backup's); the profile file (`GamerProfile.pcsav`, written by the game at 13:56) is the
  game's latest and was left. Fake headset off, SteamVR stopped, the players' build of 19dad44 in the folder.
- 2026-11-10, 14:10-14:55, **the body while the view is away, begun and paused** ("The body in scenes in the world").
  The owner's idea (the script keeps Shepard's actor, the player keeps the body) is right in principle. A first way (the
  mesh component placed in the world) fought the engine and was dropped; a second (the body follower slides his bones to
  the player, `worldBody = player`) is in the tree and does what its log says, but **no body is drawn in any scene in the
  world, the ordinary conversation included**, while on foot the same test shows arms, hands and chest: the body's
  visibility in the conversation and cinematic modes is the thing to solve first, and my "fixed" for the owner's
  vanishing-body note this morning was a log line, not a picture. Paused at the owner's word (usage).
  **Where everything stands at the pause:**
  - On main (eaa15cf), run on the fake headset: no flash at a scene's start, the script's Shepard with the view walking
    to him (guided along his path), the wheel's stick latch, depth of field off in walks, eased authored facings, the
    look-at rule, the black screen's dip by what it covers, the fast loop from saves and the session judge.
  - On the branch only, unverified: the body with the player (way 2). Default on; it only stops the pawn being hidden
    while the view is away, and the body does not show either way until the visibility is solved.
  - **Not solved, from the owner's notes:** the body in scenes in the world (above: the first thing to do); the blink
    on the ceremony's walkway (nine people are shown at that instant: have them there earlier); bodies that re-face
    slide (the game's turn in place does nothing in a conversation), 13 in the comm room; heads reach about half the way
    to their target; the comm room's conversation after a load opens on the screen for a moment and Shepard strays 2.2 m
    there; 0.1 s of 3D screen when the controllers lose tracking; slot 84 does not start from its save, Chorban's needs a
    plot conditional; the old runner's `open` resets the plot.
  - **Not built:** spatial voices (the hook is known: "The voices"), a composition rule for groups, people walking to
    their marks, the pause and floor marker fallback.
  - **Needs the owner in a headset:** everything of today (no real headset was connected all day).
- 2026-11-10, 15:50-16:03, **phase A of the plan for the missing body, run** ("Phase A, run"). No flag hides Shepard and no
  script asks for it: he is not rendered. From a conversation's second shot on it is our own Umbra lever (`umbraEyeMatrix`)
  together with the held game camera that culls him (lever off, occlusion test off or camera not held: drawn); in the first
  shot, with the director's camera 3 m away, he and the floor under the player are gone whatever was tried (the owner's
  "assets near me disappear"); and where he is drawn he stands 0.6 m ahead of the view with his arms down. Nothing fixed
  yet; the instrument (`convo: body:`) is in the tree, off by default. Mod built; build-mono, build-trilogy and the tests not run.
