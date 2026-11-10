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
- 2026-11-09, 20:20-21:10, **the sweep's phase 1 built** ("The sweep: progress"): `tools/game-slot.ps1`, the catalogue's
  level from the streaming lists (36 starts moved), `tools/conversation-sweep.py predict | plan | judge` with its tests,
  and the flowed scene's summary line in the mod. No game run. Next: phase 2, the reach matrix, which is the first use of
  the game slot on the real folder.
