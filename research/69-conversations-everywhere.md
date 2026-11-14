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

## What remains for a complete dialogue system (2026-11-10, late evening; supersedes the Status table above for planning)

The owner: "The ultimate goal is to get the dialogue system complete." Everything below is first-person conversations
(`[Cinematics] conversations = world`) unless marked. "Built" means in the tree, compiled in all three configurations, tested
offline and, where said, run on the fake headset; nothing on this list has been seen in a real headset unless marked.

**Built, waiting for the owner's headset pass** (`notes/headset-checklist.md` has each, with what to look for)

1. **People walk to their marks** (2026-11-11, "People walking to their marks" below; the owner: more valuable than the
   audio). A mark the stage asks for during a line is walked to by the game's own AI and locomotion (`worldWalk = 1`, now the
   default with the world mode), the player never; seen on the fake headset (Anderson mid-stride in the medical bay). C-Sec's
   Wrex, who didn't walk, was hidden in the test's plot state (not a limit of play). Open: the squad's own marks (henchmen use
   `BioAI_Henchman`, which asks `AllowsHenchmenMovement`: answered too, not yet seen walking).
   The other ways stay test options, never defaults: `worldWalk = 3` (the engine's walking physics, an acceleration toward the
   mark) **moves nobody** (measured 2026-11-11 07:21-07:25 in the wake-up scene: four walks, 0 units in 0.75 s each, all fell
   back to the slide, ankles still; the table in "People walking to their marks"); `2` is the old slide. Since 2026-11-11 nobody whose animation
   tree has no walk is walked ("People animated by gestures only").

2. The body in conversations and scripted walks (near plane, culling pad, the body with the player, standing legs).
3. The walkway's blink only for changes within 8 m; the 1.5 s trust over game-thread stalls.
4. Who looks at whom by part in the line; bodies turning only for the player and back; the pair's head reach.
5. Voices placed within 8 m; the game's audio through OpenAL Soft for two ears at the game's own level.
6. Then: First person stops being a labelled preview (the owner's call; one button in `assets/menu/main.rml`).
   **Decided 2026-11-11 by the owner:** "We can release this feature as experimental. I'll do the audit later today." The
   menu's row now says First person (experimental), still off unless chosen (the 3D screen the default); the owner's headset
   audit of items 2-6 and 13 follows. An adversarial review of the day's changes by a second model (Codex, read-only) was asked
   for at the same time.

**Open, in the order they matter**

7. **Composition of groups**: seat people so each one's partners are within the look-at chain's reach (`cw::ArcSeats`
   facing the group's middle, seats chosen for the speaker/listener pairs), and the squad beside the player rather than
   wherever they stood. Ties in with item 1 (the seats are the marks they walk to). **Partly built 2026-11-11** ("The
   composition of groups, first part" below): seats face the group's middle (seen), the squad's seats beside the player
   (unit-tested, no squad in the scenes run), people out of view walk into seats in an opening without a cut (planned and
   started at C-Sec, whose people the AI doesn't move). Open: seats chosen for the speaker/listener pairs, the squad's walk
   (squadmates outside the conversation have no walk state), and a seat walk seen arriving.
   **Seat walks seen arriving 2026-11-11** ("People animated by gestures only" below): at Han Olar's on Noveria two
   bystanders walked 7.6 and 8 m into seats and arrived, **in their reference pose**: their animation tree has no walk. Such
   people now keep their place (seen: the two stay at the window, posed). A seat walk by someone who can walk is still to see.
   **Seats for the pairs built 2026-11-11** ("Seats for the pairs" below): the seats go to people in the order that lets those
   who talk to each other look at each other (every line's speaker and listener); seen in Joker's cockpit scene, with a refused
   seat now swapped. Open: the squad's walk, and a seat walk by someone who can walk.
   **2026-11-11 09:46-09:53, two more tries for a seat walk** (`-Only ice20_anoleis,sta60_black_market`): Anoleis's office opened
   without a cut (a 2.8 m walk-in), but its one person out of view stood 18.4 m from a seat (`worldSeatWalkMaxM` 8: it stays);
   the black market's people were all in view (a 5.7 s scene). A seat walk needs an opening without a cut and someone of the
   conversation out of view within 8 m: in every sampled log since the AI walk became the default only C-Sec has it (whose
   people the AI doesn't move). The walk itself is item 1's path (`PushMoveToPoint` with the four holds lifted): in the same
   run the medical bay's people walked 3.0, 1.7 and 2.2 m to the stage's marks and arrived 0.24 m from them (one more walk
   the AI didn't take up: that person stayed). A seat walk arriving is still unseen.
   **10:00-10:04, Bring Down the Sky's batarian leader** (`BIOA_UNC52`, chosen from the logs: Ashley stood out of view 1.3 m
   away in its cut opening): opened without a cut (the view's 12.8 m walk-in), but its conversation has two people, Shepard and
   the batarian, and seats are planned for the conversation's people only; Ashley is squad (squadmates outside the conversation
   have no walk). No seat walk. The scenes that would show one (an opening without a cut, someone of the conversation out of view
   within 8 m, a person the AI walks) have not turned up in any sampled run; this is for the owner's play or a scene he names.
   **Seen arriving 2026-11-11 11:31** (the comm room's handover to Anderson, fake headset; "The handover, run again" below): the
   handover now opened Anderson's conversation without a cut, Nihlus stood out of view 0.8 m behind the player, was given a seat
   in view and walked there with the game's AI (1.96 m in 1.3 s, his place moving 0.1-0.4 m a tenth of a second: a walk, not a
   slide), arrived 0.25 m from it and went back into the conversation's gesture mode; 5.7 s later his body re-faced the player
   (90 degrees off for a second). No snapshot of the second-long walk itself: [verify] its look in the headset.
8. **The pause and floor marker**: when the player is wanted at a mark the guided walk can't reach (inside a trigger, the
   way blocked, too far), hold the conversation and show a marker on the floor; it goes on when the player stands there.
   Today such a scene is a cut or stays put.
   **Done 2026-11-11: decided by the owner, nothing to build** ("Is the player's place ever necessary" below): "if it's necessary for me to be in a certain location then we should create a mini
   floor marker to indicate where I need to stand. I do doubt it's necessary." No pause; a small marker only where the place
   is necessary. **Not built: nothing has shown it necessary** ("Is the player's place ever necessary" below): of 37 walk-ins
   logged since the view walk (2026-11-10), 29 arrived, 2 were blocked (the Garrus argument at C-Sec at its start, now a flowed
   scene; Wrex on Virmire 18 m short of a 29 m mark), the rest ended before arriving, and every conversation ran on wherever
   the player stood (nothing ejects a conversation by distance; with the view walk Shepard's pawn stays on his mark, so the
   triggers there are touched). Built the day a scene needs the player's place (one that waits for him, or a mark whose
   trigger only the player's own pawn can touch).
9. **The comm room's conversation after a load**: it opens on the 3D screen for a moment and Shepard strays 2.2 m (the
   launcher's Normandy case; not in the validation saves: needs a reproduction first).
   **Shepard's stray found and fixed 2026-11-11** ("The handover between two conversations" below): the comm room hands
   over to Anderson's conversation, and the last one's keep-in-place undid the new stage's placement of Shepard. The 3D
   screen for a moment was not seen in the launcher's run (the level's intro and Joker's scene use it, then the world).
10. **Scenes with no place for the player** (`worldAutoScreen`): read which scenes the three rules name over a sampled
    pass and decide whether `on` (to the 3D screen) is right for them. **Done 2026-11-11** ("Which scenes have no place for the
    player" below): the presence rules act by default (`on`), the far shots only log.
11. **Authored moments beyond the council walk**: 21 level-script matinees walk Shepard (`conversation-sweep.py moments`);
    only the walk to the Council has been run. Run the rest from saves as a sampled set.
    **First sample run 2026-11-11** ("Moments from saves, first sample" below): 4 moments, 2 played (the geth intro on Feros
    in first person, carried 5 m after one dip; Chellik's bump led into Jenna's contact with Shepard's body up to 1.73 m from the
    view), 2 saves didn't come up on foot (Noveria's Benezia phase 2, Liara's intro on Therum).
    **Second run 2026-11-11 05:00-05:23** ("A moment on foot" below): 91 and 92 start as their saves load (the runner drives them
    now); Liara's 10 lines play in first person; **Chellik's bump was a matinee with its own cameras in game mode 0**, shown in the
    eyes letterboxed and third person, and taken for the player's view by the conversation after it: now held in first person
    like a cinematic moment, nothing found by the judge. **Third sample 2026-11-11 05:27-05:42** ("A moment after a conversation"
    below): Benezia's intro (after her conversation) now plays in first person instead of on the 3D screen; Therum's troop drop
    save dies on load (GameOver); the Feros tower's start waits on a plot check. 15 moments are still unrun.
    **Fourth sample 2026-11-11 06:40-06:58** through the sweep runner, which streams the level's parts hop by hop (the
    save generator writes only a level, a place and plot flags: a spot in a part not yet streamed fell through the world):
    Virmire's deployment, judged clean ("Virmire and the cockpit" below); Eden Prime's train is not reached by opening the
    level (its prologue plays). On the way, Joker's cockpit scene showed the re-facing rule turning a seated pilot: fixed.
    **Fifth sample 2026-11-11 07:09-07:13** (Therum through the runner): the troop drop held in first person, judged clean; the
    krogan boss's entrance on the 3D screen by the rule (its track starts 4 s into the cinematic mode), its conversation in the
    world after it. Seen now: 10 of the 21 (Chellik, Benezia's phases 2 and 3, her intro, Liara's intro, the geth intro, the
    council walk, the Spectre ceremony, Virmire's deployment, the troop drop, the krogan's entrance); left: the ending's two,
    Noveria's double homicide and Death-started scene, the Normandy's and the gambler's remote events, Eden Prime's train and
    the Feros tower's two (all behind a plot point or a start the runner can't make yet).
    **2026-11-11 08:13** ("The scripted walks, named" below): the moment's first line now names its matinee (the track
    instance's path: its group instance lives in the `SeqAct_Interp` playing it). The Normandy's (`BIOA_NOR10_13_LAY`'s
    `SeqAct_Interp_1`) is the prologue's opening as `BIOA_PRO00` loads: it places Shepard 34.7 m away (no walk: path 0 m)
    inside the load's black screen and plays on the 3D screen; every run that opened the level played it unnamed. The ending's
    two are left out on purpose: their script updates the end-game statistics and the Tactician achievement and saves the
    character before the matinee (the owner's account and career).
    **Done 2026-11-11 12:01: all 21 seen** (the ending's second branch from a save with plot integer 46 over 47, held in first
    person; its platform black from his eyes, [verify]: "The scripted walks, named"). **20 of 21 seen, 2026-11-11 11:02** (the ending's first: "The scripted walks, named"); **19 of 21 seen, 2026-11-11 09:38** (the creeper from the Mako on the 3D screen: "The scripted walks, named"); **18 of 21 seen, 2026-11-11 09:27** ("The scripted walks, named"): Noveria's scene after the squad falls, the double homicide,
    Feros's transition (screen) and first creeper, Eden Prime's train (from the John01 save) and the gambler. Four faults fixed
    on the way: a flash of the screen while the script fades to black first (`cw::StepProvisional`), a cut that kept the old
    facing and offset, a ride taken for teleports (`cw::IsRideStep`), a moment's first move on foot never judged. Left: the
    ending's two (opening the level runs the end-game statistics, achievement and character save: the owner's call) and the
    creeper from the Mako (its script waits for Shepard to leave the Mako there).
12. **Coverage and the test harness**: about 120 of 290 reachable scenes sampled; slot 84 (Wrex on Virmire) doesn't start from
    its save; Chorban's scene needs its plot flag set in its save; the 15 set pieces on the 3D screen have not been seen in the
    world to confirm the list.
    **Slot 84, 2026-11-11:** it does start: the trace shows the game updating `jug20_wrex_dlg` every tick, but as Wrex's ambient
    line. A conversation begins with the first starting entry whose conditional holds (`starting` 0, 1, 6: entry 0, the scene,
    needs conditional 3684; entry 1 is ambient, conditional 3015; entry 6 has none), and the save doesn't satisfy 3684. Firing the
    chain from its first op (the use event's plot check) or past it (`OverrideStagePlacement`) changes nothing. The save
    generator sets the plot the script checks on the way to the action, not the starting entry's conditional; conditional
    3684 isn't among `PlotManager.pcc`'s conditional functions (`BioAutoConditionals.F<id>`, 1066 of them): **it is a plot flag.**
    An entry's `nConditionalFunc` is a conditional function only with `bFireConditional` set; clear, it is the plot flag of that
    number, checked against `nConditionalParam` (1 set, 0 clear). The scan now records both (`fireConditional`,
    `conditionalParam`), and the save generator sets **the first non-ambient starting entry's flag** as it wants it, and any
    flag-checking ambient entry before it the other way (`conversation-sweep.py`'s `starting_plot`, tested in
    `tests/ConversationSweepTests.py`). **Tried in the game, it isn't enough:** with flag 3684 written into slot 84, loading the
    save started Kirrahe's briefing at once (the flag is a plot milestone the level's script reacts to), so the save never came
    up on foot; Chorban's slot 85 with flag 5316 still played its 2 lines in 4 s. Both saves were put back. The generator sets
    starting flags only when asked (`--starting-flags`) and otherwise lists them in its table. A scene's plot needs the
    milestones before it, not only its own flag: still open for slots 84 and 85.
    **Coverage counted 2026-11-11 11:50** (every run's `results.jsonl` against the 290 reachable scenes of `plan-all.json`):
    **108 played** in some run, 182 never: 40 tried and not played (28 no conversation, 8 not reached, 4 action not loaded),
    142 never tried; by level the Citadel 64, Noveria 37, Feros 24, the Normandy 16, Virmire 15; by shape 113 stages, 38 free,
    14 ambient. The next sampled pass is ready, two per level and shape from different chunks, the ending's levels left out (their
    script runs the end-game statistics and saves the character as they open): 52 scenes in 14 levels
    (`%LOCALAPPDATA%\MEVR\sweep\plan-next-sample.json`, the list of the 182 in `never-played.json`), about two hours with
    `tools/conversation-sweep.ps1 -Plan <it> -Pace opening -EndUnplanned` once the game is free after the owner's audit.
    **Prepared from saves, 2026-11-11 12:15:** the runner's `open` resets the plot, so the pass goes through the fast loop instead
    (`conversation-from-save.ps1 -All`, about a minute and a half a scene with `-EndAfter 25`): `conversation-sweep.py saves`
    wrote 47 of the 52 (slots 100-151 from John02's own save 21, the plot each scene's way and starting entry check; kept outside
    the career until the run); 5 have no placed owner or originator to stand the player by (Therum's elevator ride, Hackett's
    transmission, the Citadel's cab, Bring Down the Sky's batarians, the combat banter), and `actions` finds every scene's first
    action in the plan. About an hour and a quarter, after the owner's audit.
    **Run 2026-11-11 12:45-14:28** (the owner: "Run the 47 scene coverage pass now"; "The coverage pass from saves" below): 16
    conversations played in the world and judged, 7 bark scenes heard playing; **131 of 290 reachable scenes played** in some
    run now. On the way, **a Halcyon fault behind the eyes**: after 16 save loads without the pause menu its first-person table
    was full and the camera stayed the game's third-person one, 2 m behind Shepard; fixed (dead entries dropped), and seen fixed
    after 17 loads. The loop now finalizes a landing's squad screen, streams a chunk in by its plot state, and tells barks by
    their voice. Still not started: 4 saves on uncharted worlds (they start a landing), 4 chunks whose state doesn't stream
    them, 8 starts behind plot conditions a save doesn't set, 3 flowed and 4 voiceless (popup text) scenes not confirmed.
    **Third pass 2026-11-11 15:20-16:04** ("The third sampled pass" below): 39 untried scenes, 1-6 per level and kind from
    different chunks and every Normandy transmission: 17 conversations played in the world, 6 bark scenes heard; **154 of 290
    reachable scenes played** now. Every level and kind has been sampled; what is left untried is mostly staged scenes of
    the Citadel (43), Noveria (16) and Feros (8), the kinds already seen.
    **Fourth pass 2026-11-11 17:22-17:48** ("The fourth sampled pass" below), after the camera-collision fix: 29 untried
    scenes (12 of the Citadel's staged ones, 6 of Noveria's, 4 of Feros's...): 14 conversations in the world, 4 bark scenes
    heard; **172 of 290 reachable scenes played**. Every opening began from the first-person eye; nothing new found.
    **Fifth pass 2026-11-11 18:14-18:41** (one untried scene per chunk, 31): 15 conversations in the world, 3 bark scenes
    heard; **190 of 290 reachable scenes played**; every opening a stay or a walk-in from the first-person eye, no cut;
    nothing new found (two `reply-move`, a 95-degree re-face by the rule, a 0.96 m `body-strayed` on the fake headset).
    Left untried: 31 scenes in chunks already sampled.
13. **Flowed (ambient) scenes**: the presence rules (barks refused, newcomers held) were seen at C-Sec once; the ambient
    subtitle on the HUD is [verify]. **Narrowed 2026-11-11** ("The subtitle of a line played as a bark" below): a flowed scene's
    lines behave exactly like the game's own barks: the line's text is built, nothing is drawn, in this profile (its subtitle
    option at the game's default, 0). What the owner sees with subtitles on is the headset's question.
    **Built 2026-11-11** ("Subtitles for flowed lines" below): the same profile draws the ordinary conversation's subtitle (seen),
    so a flowed scene lost the subtitles it had as a conversation. The mod now hands each flowed line to the game's own subtitle
    display, in the bottom style, which reaches the HUD panel (seen); `worldFlowSubtitles = 0` turns it off. Legibility in the
    headset is the owner's to judge.
14. **Bodies that must turn slide** (no turn-on-the-spot animation exists for these people; rare now). Accepted unless the
    headset says otherwise.
15. **A stage animation that carries Shepard's head away from his pawn** (new 2026-11-11, "The coverage pass from saves"):
    Bring Down the Sky's surveyor (`prc1_surveyor`): Shepard's head bone stands 2 m from his pawn as the scene starts, the
    opening cuts (his mark out of sight), and the view, following the head bone's jumps, ends inside his torso (a snapshot at
    8 s: the back of his armour fills the lower half). Find what moves the skeleton (root motion or a stage offset) before
    choosing the view's anchor there.
    **Done 2026-11-11 14:46, not a fault in play** ("Hackett's call and the surveyor, run again"): with tracked hands (a replayed
    recording at rest) the body follower takes Shepard's body under the view and the eyes see the room at 6, 12 and 17 s; the
    torso in front was the fake headset without controllers (no arm IK, no body follower), as Chellik's bump found.
16. **A flash of the flat panel at a transmission's end** (new 2026-11-11, same section): Hackett's call in the comm room
    (`sp111_transmission`, 13 s): the cinematic's flat panel shown for 0.05 s with two dips as it ended. [verify] what
    switches there (the judge's `flash`).
    **Done 2026-11-11 15:17** ("Hackett's call and the surveyor, run again"): the call ends into the galaxy map, whose first
    frame fell to Halcyon's flat panel because the console's use (4 s) had expired during the call; the use's window now lasts
    through a conversation it began (`vr::galaxy_map::ConsoleUse`, unit-tested). Seen twice through the real path (the
    console's use: Joker's line, Hackett's call, the map): no flat frame, nothing found. On the way, the call's opening cut at
    1.2 m when putting Shepard back was refused: the view now walks to him instead (`WalkInFacts::putRefused`).
17. **Scenes on uncharted worlds from a save** (harness, 2026-11-11): a save written on foot on an uncharted world loads into
    the landing (the squad screen, then the drop's cinematic, which didn't end in two minutes); four sampled scenes need a
    save made after the landing, or the runner's `open` and `mevr vehicle party` (research/47 "Reaching the Mako").
    **Done 2026-11-11 18:12** ("Uncharted worlds from a save"): the shared world-load script checks plot 5470 ("Has the Rover
    Landed?") as the exterior loads and plays the landing when it is clear; the generator sets it on an uncharted world
    (`conversation-sweep.py`'s `level_plot`, tested). Both asteroid scenes then loaded on foot and played (their lines heard);
    the third save (`sp114_triggers`, UNC17) loads to the game-over screen: Shepard dies where it stands him (beside a hostile
    squad), the place, not the landing.
18. **An opening that starts from a view at Shepard's hips** (new 2026-11-11, "The third sampled pass"): in 3 of about 50
    openings from generated saves (the surveyor, twice; Noveria's guards; Garrus in the Normandy's hold) the view on foot the
    opening took stood at the pawn's place, 0.26 m below its centre, instead of the first-person eye (0.5 m ahead, 0.9 m up),
    and each opening cut (Shepard's mark "out of sight" from there, 2.4-6.1 m away). The floor lock had no floor under him
    (`pawnFloorZ` 0) at the guards. Probably the generator's place (a spot the pawn doesn't stand on as a player would),
    [verify]: what the game's camera is in those saves before the scene, and whether a player standing there ever sees it.
    **Done 2026-11-11 17:20** ("The camera that fell to Shepard's knees"): **the game's camera collision, in first person**: on
    foot before any scene the eyes were behind his knees looking at his legs, the camera 0.62 m above the floor (1.79 m 1.5 m
    away). Each camera mode pulls the camera toward its hook (the Camera bone at the pawn's centre) when anything stands
    between them, and Halcyon's first person puts the explore mode's camera 0.5 m ahead of that bone; now cleared in first
    person on foot (`game/CameraCollision`, `[Game] cameraCollision`): the three saves' camera 1.82-1.83 m. Two of the spots
    also stand Shepard against a pillar and crates (the generator's place), which keeps their openings' "out of sight" cuts.
19. **The council's hearing went third person in the headset** (new 2026-11-11 22:35, the owner's first slot-80 run; "The hearing
    that went third person" below): with the player walking during Anderson's walk, the hearing opened as a walk-in by the view,
    and in its first tick Shepard's hidden, unposed head bone "jumped" 21.5 m and 17 m; the follower took it, joined the view
    to Shepard there and left it 4 m behind and 2 m above his body for the whole hearing. **Fixed 2026-11-11 23:00**
    (`cw::HeadFollowFor`, unit-tested): his head bone never moves the view while the view walks to a hidden Shepard, and is not
    read for 1.5 s after he is shown again. [verify] slot 80 again in the headset, walking during Anderson's walk.
20. **Kirrahe's camp in first person** (the owner, 2026-11-11 22:43, slot 88: "nothing about that scene ... should have just
    been third person. That one could have been fully supported in the first person experience"; "Kirrahe's camp" below):
    `jug20_sal_captainkirrahe` is on the set-piece list (`cw::IsSetPiece`) because the game walks Shepard in it: a move track
    of its own takes him 6.3 m in the conversation's first 4.5 s, and a node's script teleports the party 16.5 m onto the
    stage. Both are things the world mode now handles (a track's move of Shepard during a conversation, a teleport within
    one space). Next: run it in the world on the fake headset (`worldSetPieces = world`, slot 88), then add it to the scan's
    `SEEN_IN_WORLD` and regenerate the list.
21. **The game's black screen on foot is a black panel in front of the player** (same run; "Kirrahe's camp" below): a script's
    `BioSeqAct_BlackScreen` that turns black on while the player is on foot and no moment is held goes, like all of the game's
    UI, to the body-locked HUD panel: a 1.3 m black square 1.5 m ahead, with holes where the panel's crops are cut out (the
    use prompt, the wrist status), and the use prompt's quad a black bar beside it ("there's black boxes where I'm looking").
    In the game it is the whole screen. Decide: while it is up on foot, the transition fade's black over the whole view
    (as the game means it), the panel as now, or nothing. Here it never came down only because of item 22.
22. **Slot 88's plot was not the scene's** (harness; "Kirrahe's camp" below): Kirrahe's script plays the speech to his troops
    after the planning conversation only with plot 3039 ("Mission started") and the squadmate sent (3825/3826, which the
    conversation sets); the generator set the flags on the way into the scene (3953 "Talked To Captain", 3015 "Wrex Success")
    but not 3039, which the conversation's first starting entry also wants. So the first use played his arrival content
    (staged for an arrival that never happened: Kirrahe 11 m from his mark, the camera framing grass on his lines, "he actually
    disappeared"), the second the planning one, whose last node faded to black for a speech that could not start ("it's also
    paused"). **Fixed in the save 2026-11-11 23:20**: John02_88 rewritten with 3039 set (time kept). [verify] in the game. The
    generator should also set the flags a scene's own follow-up checks (the outputs after its `BioSeqAct_StartConversation`).
    **22:57, the owner again with the fixed save**: the first use still played the arrival content (with 3039 set): the
    conversation picks its arrival entries first whatever 3039 says, and the planning comes on the second use. So the arrival
    talk is only right from the landing (the Mako reaching the camp: the level script's `SeqEvent_Touch` on `BioTriggerVolume_0`
    while driving, which fades out, takes the player out of the vehicle and stages everyone); a save on foot by Kirrahe can test
    the planning talk and the speech (second use), not the arrival.
23. **The dialogue wheel cut long replies** (the owner, 2026-11-11; "The owner's session, judged" below): the wheel's band
    ended at stage x 950 and "The Council won't help us." lost "us.", which stayed on the HUD panel as stray letters. **Fixed
    2026-11-11 23:40** (`cw::DefaultWheelCrop` 100..1180, `cw::WheelQuadWidth` keeps the text's size; host). [verify] in the
    headset.
24. **Head movement judders in conversations** (the owner: "when I move my head, everything kind of jitters"; same section):
    the frame rate held 89-90 fps, but a frame of 40-90 ms of GPU time came within a second or two of most of the director's
    camera cuts (the hearing: 9 such frames in 170 s; in 40-76% of two-second windows in conversations against 15% walking on
    Virmire). A cut is hidden in the flat game; in first person the view doesn't cut, so each is a visible stall. Suspect: the
    culling and streaming camera jumps with the director's (`worldCamera = 0`, the default since 2026-11-10). **Measured on the
    fake headset 2026-11-13 ("Frame timing at the director's cuts" below): no stall at any of 99 cuts in the hearing, with
    `worldCamera` 0 or 1**; the GPU's scene work after a cut is what it is elsewhere, and `worldCamera = 1` changes nothing
    measurable. Not reproduced: the next step is a capture in the headset (notes/headset-checklist.md).
25. **The right stick doesn't turn in a first-person conversation** (the owner; same section): the left stick is the wheel's and
    walks; the right stick does nothing (`[Comfort] turn` applies on foot only, and the game's conversation camera takes no
    turn). Build: the right stick turns the held view in a world conversation as `[Comfort] turn` says (snap, or smooth at the
    explore turn's rate), the body following as on foot.
26. **The tests must walk like a player** (the owner: "our testing system also needs to simulate left analog movement ... yours
    are just relying on an auto movement system"): every run so far left the stick alone and let the guided walk or nothing
    move the player; the hearing's fault (item 19) needed the stick during Anderson's walk. The from-save runner drives the left
    stick (`mevr convo walk`, or a replayed recording with `-Inputs`) at a scene's start, during a moment's walk and away from
    the speaker mid-conversation, and plays each scene to the player's control back (black screen down, game mode 0).
27. **A scene that relocates its whole cast needs its dip** (the owner, Kirrahe's arrival in first person, 22:57: "I'm fine if
    we need to do a temporary black fade to be able to support the scene properly, but right now it's just kind of not playable
    at all"; same section): the stage asked the player onto a mark 16.4 m away and the opening chose a walk-in to a nearer mark
    (11.1 m, where a move track had put Shepard), withheld every placement, and the guided walk was blocked 8.7 m short; Kirrahe
    and the squad stayed where they stood, the shots framed empty marks, and nobody could get anywhere. Rule to build
    (`cw::DecideOpening`): when the stage's first placements take the player's mark and the conversation's owner (or most of its
    people) beyond a walk, it is another composition: a cut, the stage's placements accepted under the dip, everyone on their
    marks. Then Kirrahe's camp (item 20) can be run in the world from the landing.
28. **The conversational circle** (the owner's design, 2026-11-11 23:30; "The conversational circle" below; supersedes the
    opening-time seats of item 7 and the guided walk-in): people stay where they stand until they engage; whoever first speaks or
    is spoken to walks slowly into a circle with the player; silent ones stay; the circle gathers round the player again after the
    player has walked off and stood; scenes whose authored places matter are staged (their marks walked to, the opening under a
    dip); the player keeps the place where the conversation began, walks freely (slower) except while the wheel is up, and is
    never walked by the game; the game's own animation (nods, gestures) is kept. **Built 2026-11-11** (`cw::MakeCircle`,
    `cw::JoinCircle`, `cw::ShouldRegather`, `cw::IsStagedScene`, unit-tested; `game/ConversationWorld` `StepCircle`);
    [verify] in the headset. **Second design 2026-11-12** after the owner's answers (the owner stays, early walk-ins read ahead
    in the dialogue, a 4 m boundary, the right stick turns, staged scenes keep their facings; "The conversational circle" below):
    validated on the fake headset except the squad's formation, which the AI doesn't walk reliably yet (off by default).
    **The squad's formation, held (2026-11-12, "The squad's formation, held" below)**: the conversation's start had moved the squad
    onto the stage's marks beside Shepard's mark, 6-8 m from a player who stays where he began; a squadmate that stood with him is
    put back before anything is drawn, takes the spot on its own side (`cw::AssignSpots`) and walks there held by the squad's own
    HOLD formation (written quietly into its member data), or is put there under a cut's dip; released at the end. Validated on the
    fake headset (Khalisah's interview, the embassy, Anderson's office): `worldSquadGather` **on by default**.
29. **Stances from above** (the owner, 2026-11-12: "capture the conversational stances from a top/bottom view so that we can more
    easily judge the conversations. If the conversation doesn't require any mid scene movement then those can be skipped early
    after capturing the screenshot"; "Stances from above" below): **built** (`mevr convo stance`, `worldStances`,
    `tools/conversation-stance.py`, `tools/conversation-from-save.ps1 -Stances`); a plan view drawn from the game's places and a ray
    scan of the room, not a render (a camera above the room would see its ceiling, and Umbra culls from the camera). Since
    the owner's question (item 30) each face's direction and where the game's look-at aims it are in it too.
30. **Heads that look past the player** (the owner, 2026-11-12, of the drawings' "you" and "Shepard": "Are there conflicts
    with where their heads are pointed during conversation? I noticed weird behaviors like that in some missions like virmire
    intro"; "Heads that look past the player" below): in a walk-in by the view Shepard's body stays on his mark and every look at
    "Shepard" (the game's own at rank 99999, and ours) aimed at it, metres from the player: Girard's face 7-16 degrees off the
    empty mark and 58-78 off the player. **Fixed**: a look-at target of the game's own kind kept at the player's eyes takes
    Shepard's place while his body is away (`worldLookEyes`); Girard's face 12-27 degrees off the player since.
31. **A staged scene's new composition under its own black screen** (the owner, 2026-11-12: "the more difficult scene like the
    start of virmire where we start on the beach and then move into a different position ... the overhead view, it will be
    different throughout the scene"; then: "instead of forcing Shepard to move, can you program a simulated analog stick move to
    go into the desired position"; "Virmire's start from above" below): the stage's moves of the others under the script's black
    screen are taken at once (Kaidan and Ashley had walked 10 m across the camp during Kirrahe's speech); Shepard is never moved:
    a simulated left stick walks the player to his new mark, the player's own stick taking over. Built and run on the fake
    headset; the stances follow a whole scene (its start, each dip, each change of place or facing).
32. **Heads at the drawn eyes** (the owner, 2026-11-12: audit every kind of conversation for heads facing Shepard's hidden body
    instead of the player; "Heads at the drawn eyes: the audit" below): the player is where the host draws the eyes (0.43 m
    behind and 0.18 m below the held place, the game's camera); fixed: an opening that cuts to Shepard's mark put the eyes
    0.34-0.37 m behind his head (every staged scene: the Council, Kirrahe's camp at Virmire's start), the right stick's turn
    swung them up to 0.85 m off, the eyes stand-in, the re-face rule, the circle and the squad's formation took the held place;
    and in a walk-in by the view the stage's facing toward Shepard's hidden body is turned to the player's eyes. The moments
    (scripted walks) audited too, their records and the stand-in extended to them: nobody in them looks at Shepard through the
    look-at system; an opening that read the frame from before the world view (Benezia after phase 2) fixed with the last
    first-person view's offset and a check of the eyes after the opening.
33. **Walking along** (the owner, 2026-11-12: Shepard walks alongside others in scripted walks; "If we fall too behind the
    conversation should likely pause"; "Walking along" below): a scripted walk with someone beside Shepard pauses while the
    player is more than 3 m behind (at a line's end), on again within 1.5 m. Run on the fake headset 2026-11-12: both Council
    walks wait for a player who stands still; walking along, a 4 m wall (the conversation's boundary) fixed; the player's
    walk 1.4 m/s again (the owner, 2026-11-13; at 0.9 the Spectre's walk waited three times, 11.9 s; at 1.4 only on its stairs,
    5.4 s).
34. **Auditing scenes by rule** (the owner, 2026-11-12: a better, more efficient way to audit scenes; "Auditing scenes by rule"
    below): the day's judgements as rules over the records (`tools/scene_audit.py`), a baseline to compare runs with, a
    regression set of ten scenes (`tools/conversation-audit.ps1`), and the same rules live in every scene's summary. Run in the
    game 2026-11-12 (three passes): the live numbers in every summary, two false alarms of theirs fixed, the baseline written
    (12 scenes, 0 regressions against itself); the passes found a crash of ours, a wall in scripted walks and a stuck sidestep.

38. **Obvious bugs, measured** (the owner, 2026-11-13: "I want my time spent on fine tuning, not finding obvious bugs";
    "Obvious bugs, measured" below): the audit now sees what is drawn (`body-not-drawn`, `person-not-drawn`, `near-plane`), the
    stick as a player holds it (`moved-by-pick`, `turn-ignored`), the pictures (`eye-flat`, `hud-black`, `wheel-cut`), the
    script's black screen on foot, a flash, and which scene a save played; five broken runs, all caught. Found on its first
    run: a near-black frame in the Spectre walk, a flash of the 3D screen into Opold's shop, Anderson 0.2 m from the eyes in
    the Council walks. Open: the fake headset's 31.3 fps cap (not the window, not the display), item 21's decision.

**Audio (handed to a separate agent, 2026-11-10; the owner: less valuable than item 1, done in parallel)**: its branch is
merged on main (`72fc6cb`, A1-A3) and its in-game checks were run (`adc20e8`, "To check in the game" item 9); A4-A6 below.

- **A1. Overheard conversations from where they are**: on foot, 82% of dialogue cues still play in the centre (no
  attenuation node), so NPCs talking near the player (flowed scenes, ambient conversations, overheard chains) sound inside his
  head. `engine/VoicePlace` places by the audio component's owner; it needs a second publisher (today only
  `game/ConversationWorld` names voices, and `Set` replaces the whole list) and an on-foot module naming nearby speaking
  pawns with their head places, under the same 8 m rule (`cw::PlaceVoice`). Not during cutscenes on the 3D screen.
  **Built 2026-11-11, offline** ("The audio list: A1, A2, A3 built; A5 read; A4's plan" below): `game/SoundPlace`.
- **A2. The player's own gun from his hand**: the game plays it at the pawn, which is the listener, so it sounds inside the
  head; with HRTF a source at the listener is the least natural. Place the player's weapon's sounds at the gun model
  (`weapon::Current`'s muzzle) through the same hook, if the weapon's audio component is told apart from the pawn's (its
  owner: the weapon actor or the pawn: to find out). **Built 2026-11-11 for the weapon actor; which actor owns the shot's
  source is [verify] with `mevr voices census`** (below). **2026-11-11, the census in a fight:** every shot sound there is a
  one-shot with no owner, placed by the game; if the player's is too, the Gun list never matches it (not fired yet: the
  launcher's and arena's Shepard carry no drawable gun). Next: pick the player's shot out by its cue and place.
- **A3. Settings in the menu**: `[Audio] spatial` (Headphones 3D / Speakers / The game's own) and `gainDb` as rows (they apply
  at the next start; the row says so). Players can't be asked to edit the ini. **Built 2026-11-11**: the menu's Sound page.
- **A4. Measured in combat**: other people's gunfire, explosions and footsteps under OpenAL Soft: the delay between the ears
  and the balance following the head (`tools/audio-balance.py`); confirm the game positions them at all. **Run
  2026-11-11** ("To check in the game" item 9): the fight's fire, impacts and footsteps are placed by the game; the balance
  swings -1.7 to +2.7 dB with the head; the per-window delay and the A/B against `spatial = game` not yet.
  **Done 2026-11-11 18:05** ("A4: combat for two ears, measured"): under OpenAL Soft the fight's sounds reach the ears with
  a real delay between them (up to 0.83 ms, changing side as the head turns, 10 of 21 seconds); the game's own OpenAL pans
  them by level alone (0.00 ms wherever both ears hear the sound, up to 20 dB apart).
- **A5. An intercom's colour for far voices** (the owner: the Council "a little like an intercom"): with OpenAL Soft the
  EFX filters exist (a band-pass on the source, `AL_DIRECT_FILTER`); whether ISACT's sources can be reached by their AL ids
  from the sound source's update is to find out. Optional. **Read 2026-11-11** (below): the AL ids are reachable, through
  ISACT's own OpenAL layer in the exe; nothing built.
- **A6. Signing**: SignPath signs only binaries whose product name is "MEVR"; `soft_oal.dll` says "OpenAL Soft" (a second
  `pe-file-set` in `.signpath/artifact-configuration.xml`, the owner's process).

  **A6, the note (2026-11-11; the file is not changed).** `soft_oal.dll` is built by `tools/fetch-openal-soft.ps1` from
  OpenAL Soft's own source with its own version resource, whose product name is "OpenAL Soft", and that is right: it is
  their library, unmodified, under the LGPL. `.signpath/artifact-configuration.xml` has one `pe-file-set` with
  `product-name="MEVR"` and the release's `product-version`, which lists our four binaries; SignPath refuses a file in a set
  whose product name or version doesn't match. To sign OpenAL Soft too, the configuration would need a second `pe-file-set`
  with `product-name="OpenAL Soft"` and its own version (1.25.2, not the release's parameter) that includes
  `audio/soft_oal.dll`, and `release.yml`'s upload for signing would have to carry the DLL (today it uploads our four).
  Whether SignPath Foundation's terms let an open-source project sign a third party's unmodified binary under its
  certificate is the owner's question for SignPath; until then the DLL ships unsigned beside the signed ones, as it does now.

**Release housekeeping**: the branch is on local main (fceeaf9); the owner pushes; the game slot is still held by this
session (`tools/game-slot.ps1 return` when the in-game work is done).

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

**The rule then** (`[Cinematics] worldTurn = look | always | off`; `cw::StepAttention`, unit-tested; since the evening `roles` is the default: "Who attends to whom" below):

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

#### The first shot: the director's near plane (2026-11-10, 16:10-16:30, fake headset; fixed, in the tree)

The owner, on phase A's unexplained first shot: "we may need to solve the unexplained scenario since it may have downstream
consequences". It had: it was the cause of most of the rest.

**What it was.** The cut in the first shot's picture is a straight line across the frame, the floor gone below it: a plane
across the view, not a mesh's outline. A conversation's camera carries a near plane per shot
(`BioCameraBehaviorConversation.m_fNearPlane`, from the stage camera, `BioStage.GetStageCamNearPlane`) and the game writes
it into `LocalPlayer.NearClipPlane` at each shot. Logged beside the body's line (`near plane N (LocalPlayer)`, each
conversation camera's `near` and `fov`): **279.5 units (2.8 m) in Avina's first shot, 177.5 on Shepard's line, 67.5 after;
30 to 200 in the council chamber; 10 on foot.** From a lens metres away that is harmless; from the player's own eyes it
removes everything within reach: the floor underfoot, the player's body, whoever stands beside the player. This is the
owner's "assets near me disappear when the council talks and reappear on my turn", and very likely the 2026-11-06 report
(assets missing in the comm room behind a 10-degree lens) that the camera hold was built for.

**The fix** (`game/ConversationWorld`, mod only): while a world view is held (a conversation or a moment) the
`Camera.UpdateCamera` post-handler writes the on-foot plane back into `LocalPlayer.NearClipPlane`, after the camera's own
update and before the draw (`[Cinematics] worldNearPlane = 1`, default; `convo: the director's near plane is N units ...
the on-foot plane is kept instead`, once per new value; journalled). The game writes its value once per shot, not every
tick: 3 replacements in Avina's conversation, 23 in the ceremony.

**What it changed, by picture** (the same look-down pose):

| Scene | Before | With the near plane held |
|---|---|---|
| Avina, first shot (slot 89) | no floor under the player, no body | the floor and the keeper there; **arms, hands and chest in view** once the body is drawn (below) |
| Avina, later shots | no body (feet only with the lever off) | arms, hands and chest |
| Wrex at Fist's (slot 87) | not run | body rendered on every line (`rendered 0.00 s ago`), director 0.7 to 3 m away |
| The Spectre ceremony (slot 81): the walk, the podium | not run today with this pose | walkway, glass, trees and Udina beside the player all there in far shots (director 11 to 29 m away); **the body is still missing**: below |

**The second cull, and the camera hold stood down.** With the plane held the body was still not rendered as shipped. The
table, all with the plane held: the game's camera held and the Umbra eye-matrix lever on: not rendered; the lever off:
rendered; the camera not held (`worldCamera = 0`), lever on: rendered, and nothing else missing in the first shot although
the director's camera is 3 m away then. So the main view's build (the render-time swap) already carries culling for the
whole scene, which is what stock UE3 does (Umbra's camera is set from the scene view's own matrices in
`BeginRenderingViewFamily`, `0x7655A0`: the view matrix at view +0x90 and the projection at +0xD0; the streaming and the
listener take the scene view too), and the camera hold added nothing but the conflict. **`worldCamera` is 0 by default
now** (the key stays). Why the hold and the lever disagree is not read: **[verify]** if the hold is ever wanted again.
The point 3 of phase A (the body 0.6 m ahead, arms down) is gone with the same two changes: that was a body that had
not been posed because it was never drawn.

**Still open then, solved in the next section: the body in the ceremony.** With all of the above, during the scripted walk the mesh is rendered and its
bounds are at the player (way 2's slide: `bounds 1.1 m from the view` with the pawn 17 m away) but no arm shows in the
look-down pictures; and from the conversation's first tick at the podium it is not rendered at all (flags clear, plane
10, lever on or off, the director near or far), through the walk-in by the view and after the view has joined Shepard.
The two ordinary conversations do not have this, so it belongs to the path a moment leaves behind (the view away from the
pawn, way 2's bone slide, the walk-in by the view), not to conversations as such. Not found; next: the same body line
across the moment's end with the pose writer's own counters (is `UpdateSkelPose` still called for his mesh there, and
which mesh), and way 2 switched off (`worldBody = hidden`) to see whether the body then shows after the join.

Built: the mod (hot-reloaded through these runs) and, after the commit, the players' build; mevr-tests 24 of 24. Not built: the trilogy build;
branch only.

#### The body that stayed culled: the culling camera is ahead of the eye (2026-11-10, 17:00-18:00, fake headset; fixed, in the tree)

The owner: "Keep going" (the body in the ceremony). What it was not, each tried in the ceremony from its save (slot 81) with
the head 50 degrees down, facing forward: the director's far camera (the game camera held at the player: same), the Umbra
eye-matrix lever (off: same), Umbra's occlusion test and the engine's own occlusion queries (off: same), way 2's bone slide
(`worldBody = hidden`: the body was missing at the podium all the same), a stale box in the renderer (the component update
forced every tick: same; the scene's copy of the bounds, read out of the scene info at +0x54, was the component's).

**What it was.** Head directions at the podium, the same conversation: yaw 0 with the pitch at -50, -35, -20: not rendered;
pitch -65, -80: rendered; pitch -50 with the yaw at 30, -30, 60, 90 or 180: rendered; and rendered facing forward again
after one look back. A frustum test, against a small box just behind the frustum's lower plane. The engine culls with the
game's camera (Umbra's camera is set from the scene view's CPU matrices in `BeginRenderingViewFamily`, `0x7655A0`), and
Halcyon draws the eye behind it: its render-side view offset (the camera offset, the "view back" framing
`flatScreenDistance = 0.674` of the shipped settings, the lean) is in the GPU matrices only: **the eye is 0.43 m behind and
0.18 m below the culling camera** (`hostView.offsetUU` -43/-4/-18). From there the body's box (the physics bodies' box,
27 x 32 x 92 units around the pawn, directly under the eye) lay 4 units behind the lower plane when it was culled and 3
units inside it when a look back had changed the pose. So the player's own body, the nearest thing there is, was the one
thing the culling camera could not see; in the two ordinary conversations it had happened to stay inside. On foot the same
geometry holds (the earlier "the arms and gun dropped out after a look up and back" may be this).

**The fix** (`engine/BodyBounds`, mod only): `USkeletalMeshComponent::UpdateBounds` (`0x58EEC0`, vtable slot +0x368; the
box of the physics bodies, `0x638E20`) is post-hooked; for the player's body mesh and its pose followers (five meshes; named
each tick by `game/ArmIk`'s `ProtectBodyBounds`) the box and sphere grow by the view offset's length plus 15 units (62 here)
and **Umbra's own box is made again from the padded bounds** by the engine's function for it (`0x25CFE0`, the last call of
`UpdateBounds`: a unit box scaled to the extents, given to Umbra, `0xD43030`). Padding `Bounds` alone did nothing: Umbra
tests the box it was handed inside `UpdateBounds`, not the scene's copy. A box that holds the culling camera cannot be
outside its frustum.

**By picture and by the log, the head 50 degrees down, facing forward:**

| Scene | Before | With the pad |
|---|---|---|
| The ceremony's scripted walk (slot 81) | rendered for 12 s, then not; no arm in any picture | arms, hands and chest on the stairs and the walkway; the body walks with the player while Shepard's actor is 10 to 17 m ahead (way 2, now seen working) |
| The podium, the Council's lines | not rendered for the whole conversation | rendered at every sample; arms, hands and chest, Udina beside the player |
| Avina (slot 89), Wrex at Fist's (slot 87) | rendered | rendered at all 42 and 48 samples |

The body line now also prints the box's size and the renderer's copy (`the renderer's box +0x54: ...`). Removed again: the
per-tick component update and a `bUpdateSkelWhenNotRendered` write (the flag is already set on the player's mesh).

**Open:** the legs under a standing player during a scripted walk (the script's walk animation plays in place; a
standing graft); the same check on foot with a look-around (the pad is on there too); everything in a real headset.

#### The legs under the player, the walkway's blink, a stall of the game thread (2026-11-10, 18:10-18:30, fake headset)

The owner: "modify the legs to see if we can just use the VRIK and then proceed to the other items on the list".

**The legs** (`worldLegs = stand | walk`, default stand). While the view is away from the pawn the matinee plays its walk
(`EX_Walk`) on the body that now stands with the player. The pose writer already had a leg solve (the Mako's seated
driver: the same two-bone solve as an arm, `bone_pose::Leg`); it takes a standing goal now (`Leg::stand`): the ankle
straight under the hip as the torso left it, 9 units above the floor of the animated pose moved by the torso's body shift
(so on the player's floor, not the pawn's), the knee bending forward, blended by `weight`. `game/ConversationWorld` gives
the weight from the held view's speed (`BodyAwayState::legsStand`: 1 under 15 units a second, 0 over 60, eased over a
quarter of a second): **at rest the legs stand by leg IK, while the view moves (the stick, the guided walk) they keep the
script's walk**, which then looks like walking. Run in the ceremony at 65 and 80 degrees down, both ways: the body is
whole either way; with `walk` a knee swings into view under the chest plate at rest, with `stand` none does. **From the
first-person eye the chest armour hides nearly all of the legs**, so this was not judged from outside (the spectator camera
could: not done); [verify] in the headset, leaning forward.

**The walkway's blink** (`worldDipWithinM`, default 8; negative: the old rule). The script's black screen became a dip
whenever anything in view changed; in the ceremony that is the Council appearing at the far end (1 gone, 9 new), in the
middle of the owner's own walk. `cw::NearestChange` (unit-tested) says how near the nearest person gone, moved or new is,
and the dip is taken only within 8 m. The ceremony from its save: **walking along (13.6 m by the stick): the nearest change
10.1 m away, no dip**; standing at the stairs: 7.3 m, the dip stays. People appearing 10 m and more away now pop in without
a blink: whether that reads better than the blink is the owner's to judge.

**A stall of the game thread** (the 0.1 s of 3D screen when the controllers lost tracking mid-conversation): the host
trusted the mod's world state for 300 ms; it is 1.5 s now (`plugin/ConversationWorld` `kFreshMs`; the mod itself says when
a scene ends, so the longer trust only bridges stalls). Built; not reproduced (the fake headset's controllers never drop).

**Not done of the list, and what each needs:** bodies that re-face still slide (the game's turn in place does nothing in
a conversation; a turn animation would have to be played on the body: a node in the AnimTree to find); heads reach about
half way (the look-at definition's strengths: how they combine is not read); the comm room's start after a load (not in
the validation saves: the launcher's Normandy case); slot 84 and Chorban's conditional (the test harness, not the player);
spatial voices (the hook is known, "The voices"; nothing can be judged by snapshot, only by ear); the composition rule,
people walking to their marks, the pause and floor marker.

### Who attends to whom: looks by each person's part in the line (2026-11-10, 18:40-19:25, fake headset; in the tree)

The owner: "Let's do the turns and head reach. When I think about the scene, when Udina speaks there's no reason that
Anderson would need to face him as much, so I'm curious how we'd handle that for a scalable rule."

**What was there.** `worldTurn = look` read the conversation's own look-at list (`BioConversation.m_LookAtList`) and turned
a body only for whoever the list gave a target. In every staged scene of the passes the list's entries have no targets
(the ceremony, Avina, Wrex at Fist's: `who looks at whom: ... -> none` throughout), so the rule did nothing there: nobody
looked at anybody except by an authored look-at track, and nobody's body turned. The look-at chain's own target is on the
pawn (`BioPawn.m_oLookAtData`, a `BioLookAt`: `m_oTarget`, `m_nRank`, a controller per link), not in that list.

**The game has this rule itself, switched off in staged scenes** (read from the code by a subagent in the isolated Ghidra
project; addresses in `tools/ghidra/symbols-le1.csv`). `UBioConversation::UpdateFacing` (`0xC88160`, at each entry line
and each reply) gives the speaker the listener to look at after 0.5 s and everyone else the speaker after 0.6 to 1.15 s,
within 5 m of the speaker (`BioWorldInfo.m_fLookAtNoticeMaxRange`), and `UpdateLookAtTargets` (`0xC88620`, every
conversation tick) hands each to `BioPawn.SetLookAtTarget` when its delay is up. It runs only without a stage, or with
`m_bEnableLookAtWithStage` and the stage's and the start action's `m_bLookAtActive` set. An authored look-at track
(`BioEvtSysTrackLookAt`, applier `0x9F9D50`) cancels the pawn's entry and sets or clears its target itself; it reads only
the key's tag, duration and enabled bit (the three weights and the eyes/head/torso bits are never read).
`SetLookAtTarget` (`0xAA02A0`) refuses a rank below the one held; every caller of the game's passes 99999 except its
look at the player in passing (rank 1 or 2). The cinematic game mode clears every look-at each tick (`bAllowLookAt`), the
conversation mode does not.

**The rule now** (`worldTurn = roles`, the default; `cw::RoleOf`, `PartnerOf`, `AttendAs`, unit-tested; game side
`RoleLook` in `game/ConversationWorld`). The game names each line's speaker and whom they speak to (`m_pSpeaker`,
`m_pListener`; a line of someone else's without a listener is taken as spoken to the player). Everyone else in the scene
is a bystander. No data per scene:

| Part in the line | Looks (the game's own look-at chain: eyes, head, neck, chest) | Body |
|---|---|---|
| The speaker, and the one spoken to | at each other, 0.15 s into the line; not when the other is more than 120 degrees off and the body cannot turn (the head would crank over the shoulder) | may re-face, see below |
| A bystander | at the speaker only where a glance reaches: the speaker within `worldGlanceDeg` (70) of the body's facing, kept to 85; 0.45 to 1.05 s into the line, each at their own moment | never turns: it keeps the facing the scene composed |

A look-at target the game itself gives (the conversation's list, an authored track) stands in place of the part's
(`looks at ... by the game's own look-at: left to it`). Ours are given through the game's `SetLookAtTarget` at rank 100
(`worldLookRank`): above the game's passing look at the player, below everything authored; a refused one is read back and
asked again every half second. They are taken back at the conversation's end (only where the pawn still looks at ours).

**Bodies: only for the player, only near, only past the chain's reach, and back afterwards.** The first version let
either of the pair re-face: at the ceremony's first line Udina turned 120 degrees toward a player 8 m behind him and
stood with his back to the Council for the rest of the scene. The scene composed everyone but the player, so:

- a body re-faces only as one of the pair, only toward the player, only while the player stands within
  `worldTurnWithinM` (4 m);
- only when he has been more than `worldTurnComfortDeg` off its facing for a second, now 80 (60 until today), and only
  until he is `worldTurnSettleDeg` off, now 40 (25): people who stand side by side talk with their heads turned;
- `worldTurnHomeS` (3 s) after its last line with the player it goes back to the facing the scene composed (`goes back to
  the facing the scene composed`, the summary's `turnsHome`).

**How far a head turns** (`0xC0D670`, `0xC1F7B0`, `0xC0BEC0`, read). Each link of the chain turns its bone toward the
target by its definition's `m_fConversationStrength` times what the links before it left, that remainder taken as at most
the link's limit; the strength path is always on (not only in conversations), a target outside `ValidTargetAngleRange`
(135 degrees) is pulled to the zone's edge, a bone at its limit stays there. With the humanoid definition (chest 0.125,
neck 0.25, head 0.55, all limited to 50 degrees; the eyes 25) the head is left a third of the way short of someone 79
degrees aside (`cw::HeadShortDeg`: 33; measured 21 to 36), more than the eyes cover past about 71 degrees: the "heads
reach about half way" of the earlier notes is the definition, a glance. For a bystander that is right. **For the pair it
is now stronger** (`worldLookReach` 0.4, 0: off; `ApplyReach`): the strength is read through each controller's pointer
into the appearance's shared definition (`mc_pDefinition`) every frame, so the controllers of one of the pair are pointed
at copies of ours with 0.4 of the rest added (0.125, 0.25, 0.55 become 0.47, 0.55, 0.73), journalled, and put back when
the person is a bystander again, when the game makes new controllers, and at the end. No weight on the pawn or the
look-at data exists for this, and the track's weights are dead data.

**Why a body did not turn at all, and why half way.** Two faults of the eased turn found on the way: (1) a rotation set
with `Actor.SetRotation` is turned back by the pawn's own physics toward its `DesiredRotation` within the tick (the
ceremony's Anderson: 6 s of "turning" at 180 degrees), so the eased turn now also sets the desired rotation
(`BioPawn.SetDesiredRotation`); (2) steps under a degree were not applied and the kept facing started over from the
body's every tick, so at 90 frames a second a turn stopped half way (at 159 degrees on the way from 180 to 128). **The game has no turn on the
spot for these people** (`RequestTurnInPlace` `0xA9F510`: it primes the move-start animation's state machine, which
needs walking physics, a controller and `m_bEnableStartAnim`, and then snaps the rotation in one tick; the turn blend
nodes belong to a combat-only body turn, `0xAA50A0`; `SetDesiredRotation` alone turned nobody in 0.8 s): `worldTurnWay =
slide` is the default, at 60 degrees a second (120 before), and the rule above makes it rare.

**In the game (the Spectre ceremony from its save, slot 81; Anderson is `BioPawn_9`, 1.8 m to the player's right and 77
degrees off him, Udina `BioPawn_7` 1.6 m ahead with his back to both, the Council 17 m ahead):**

| Line | Before (`look`) | Now |
|---|---|---|
| A councillor speaks to the player | nobody looks | Anderson and Udina glance at the councillor (3 to 9 degrees), the other councillors do not (90 degrees off each other) |
| Anderson speaks to the player | nothing | his head and chest turn to the player (head 12 degrees off after 1.5 s; 31 to 36 with the game's strengths), his feet stay; the councillors glance at him; Udina, 147 degrees off him, "hears ...: no look, its body stays" |
| The player speaks | nothing | the one spoken to looks at the player; Udina hears (126 degrees) |
| Body turns | 0 | 0 (`bodyTurns` 0, `looksSet` 38, `glances` 33, `heardOnly` 25, `reaches` 6, `looksRefused` 0) |

With `worldTurnComfortDeg = 60` (to exercise it): Anderson's body turned 37 degrees in 1.5 s for his lines with the
player, stayed through the reply and his next line, and went back 3.0 s after the last. Pictures: Anderson's feet toward
the Council, his chest and head toward the player; Udina's back.

**Not done, and not known:** a turn animation (none exists to borrow; a body turn is still a slide, now rare and slow);
whether the pair's stronger chest twist reads well on every skeleton (asari, turians, krogan have their own definitions:
three in the game; the copies are made per controller whatever the definition); scenes with authored look-at tracks (none
in this pass: `looksGame` 0); the comm room (its people turned 13 times in the owner's session under the old rule);
squadmates behind the player. `worldTurnLog = 1` prints each looking or turning person's yaw twice a second.

### The voices (2026-11-10: read from the code and the packages by a research agent; built and measured in "The voices, placed" below)

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

### The voices, placed (2026-11-10, 20:20-21:05, fake headset; in the tree; measured, not heard)

The owner: "For the council we would expect it to sound a little like an intercom. But for other scenes it should be
spatial."

**The rule** (`[Cinematics] worldVoices = spatial | game`, default spatial with the first-person mode; `cw::PlaceVoice`,
unit-tested; game side `PublishVoices` in `game/ConversationWorld`, the hook `engine/VoicePlace`). In a conversation in
the world everyone of the conversation but the player is judged each tick by how far their head is from the player's
view:

| The speaker's head | The voice |
|---|---|
| Within `worldVoiceWithinM` (8 m; kept until past `worldVoiceKeepM`, 10 m, so a step back mid-line changes nothing) | from where the head is, following the player's head and walk mid-line |
| Further away | left exactly as the game mixes it: in the centre, at full level (the Council across its chamber: the intercom) |

Shepard's own lines stay in the centre. Barks and flowed scenes are not touched (the game places those itself).

**How** (the sound source's update, `0x7BA90`, read again from the decompile). Once a frame per playing source the game
sends ISACT the volume, the pitch, a "relative to the listener" flag and a position; for a cue without an attenuation node
the flag is 1 and the position the listener's front. The source holds its wave instance, the wave instance its audio
component, and the component's `Owner` is the pawn that speaks (reflection: `ActorComponent.Owner`). The function is
post-hooked (mod side, installed with the conversation hooks): for a source whose component is owned by an actor the
game layer named, the flag is sent again as 0 and the position as the head's (ISACT's axes: the engine's X, Z, -Y in
metres), through the same two virtual calls of the ISACT interface the game just used (two kinds of handle, each with its
own pair of slots; the slots are checked to be code in a loaded image once per vtable). Everything our part reads is under
an SEH guard; three faults and it places nothing more. The update runs on the game thread (0 calls seen on another).

**Measured** (`mevr-audio-loopback`, a new tool: the default playback device's mix recorded over WASAPI loopback, and
`tools/audio-balance.py`: each channel's level and right minus left per second; the scenes from their saves under a fixed
head):

| Scene, line | The game's mix | Placed |
|---|---|---|
| The ceremony, Anderson 1.8 m away and 77 degrees to the right, the head facing the Council | +0.1 / -0.1 dB (centre) | **+7.6 / +8.9 dB to the right** (his two lines), the same total level (+1.3 dB) |
| The same, the head turned right round (yaw 180) | | **-8.3 / -9.7 dB (to the left)**: the listener turns with the head |
| The ceremony, a councillor 25 m away | +0.1 dB | +0.1 dB (not placed: the intercom); -0.2 with the head turned round |
| Avina 1.5 m straight ahead (slot 89) | 0.0 dB, -25.5 dB a channel | -0.1 / +0.3 dB, -25.3 dB a channel: centred because she is ahead, the same level |
| The ceremony's first line, Udina placed at his true 8.6 m (`worldVoiceWithinM = 14` for the test) | -29.8 / -29.5 dB | -29.3 / -27.9 dB: **no quieter for the distance** |
| Avina with `worldVoiceGain = 0.5` | | 7.4 to 7.9 dB quieter (6 expected: ISACT's volume is a little steeper than amplitude) |

So ISACT honours a position for these cues but only pans; loudness with distance is ours: full within `worldVoiceNearM`
(2 m), `worldVoiceFalloffDb` (3) quieter per doubling of the distance, at most `worldVoiceFloorDb` (6), sent as the
game's own volume for the source times that (`worldVoiceGain` on top). At the 8 m edge a placed voice is 6 dB under the
centred one beyond it: a step heard only by walking across that distance mid-line.

**Not done, and not known:** an intercom's colour for the far voices (band-limiting or a slight echo): the dialogue path
to ISACT carries volume, pitch, position and the sound entity's variables, no filter was found, so "a little like an
intercom" is so far the game's own centred, full-level voice; Wrex at Fist's could not be measured (the bar's music is
6 dB louder than the voices on the recording; the hook placed both speakers by the log); whether 8 m is the right edge
and whether transmissions (holograms on a comm, the scan's "remote" class) should be intercom whatever the distance;
how the pan sounds in headphones (ISACT's software device has no HRTF: left and right only, nothing for above or behind
beyond what the level tells); the listener's place is the held view without the headset's lean.

#### Real 3D audio: OpenAL Soft in place of the game's OpenAL (2026-11-10, 21:10-21:30, fake headset; the test: how it ships is "OpenAL Soft as the mod ships it" below)

The owner: "I'm curious if there are any open source 3d audio libraries to use instead? Or is this already available in
game?", then, to trying OpenAL Soft: "Yes please".

**What the game has.** Its audio engine (ISACT) plays through OpenAL: the game ships and loads Creative's router
(`OpenAL32.dll` 6.14.357.24) and Creative's software device (`wrap_oal.dll` 2.2.0.5, over DirectSound), seen in the
running process. That device pans between two channels and nothing more (above: a voice 77 degrees aside is in one
channel almost alone: the two channels are 0.1 alike); no head-related filter, no setting for one.

**OpenAL Soft** (openal-soft.org; the Windows binaries `openal-soft-1.25.2-bin.zip`, 18.6 MB, SHA-256 `67a0c4b8...e265434`;
`COPYING` is the GNU Library GPL version 2) is a software OpenAL with head-related filters for headphones. Its 64-bit
`soft_oal.dll`, named `OpenAL32.dll` in the game's `Binaries\Win64` in place of Creative's, with an `alsoft.ini` beside the
exe (`[general] stereo-mode = headphones`, `stereo-encoding = hrtf`, `hrtf = true`): the game starts, plays, and the
process has OpenAL Soft and no `wrap_oal.dll`. The same ceremony, the same measurement:

| Anderson, 1.8 m away, 77 degrees to the right | Creative's device | OpenAL Soft, HRTF |
|---|---|---|
| Head facing the Council: balance | +7.6 / +8.9 dB right | +4.1 / +4.2 dB right |
| ... the far (left) ear against the near one | nothing in common (0.08 to 0.14 alike at any shift) | **0.92 / 0.94 ms later**, 0.77 / 0.84 alike at that shift |
| Head turned right round: balance | -8.3 / -9.7 dB | -3.4 / -3.7 dB |
| ... the far (right) ear | nothing in common | **0.88 / 0.92 ms later**, 0.79 / 0.85 alike |
| A councillor (not placed) | 0.0 dB | 0.0 dB, 0.00 ms, 0.97 alike |

So ISACT hands OpenAL real positioned sources, and OpenAL Soft renders them for two ears: the voice reaches both, the far
ear later and duller, which is what tells front from back and near from far. Everything the game positions gets it, the
voices we place among them. The whole mix is about 3 to 4 dB quieter in this mode (the councillor: -29.3 against -26.2 dB a
channel).

**Only in the router's place.** With Creative's `OpenAL32.dll` left alone and `soft_oal.dll` merely added beside it, the
router loads it (the process has all three) but the game still opens Creative's device: the measurement is the old one
(+7.6 / +8.8 dB, nothing in common). So it means either replacing the game's `OpenAL32.dll` (the installer can record and
undo that as it does for a replaced `bink2w64.dll`; Steam's "verify files" would put Creative's back, which Repair
handles), or adding the file and having the host answer the game's device request with OpenAL Soft's device (a hook on
the router's `alcOpenDevice`; not tried).

**The game folder is as it was**: `OpenAL32.dll` back (SHA-256 `87802322...6454039`, compared), `soft_oal.dll` and
`alsoft.ini` removed.

**For the owner to decide before anything ships:** the licence (Library GPL 2: an unmodified DLL beside the game with its
licence text is what it is made for; `NOTICE.md` and the installer's licences folder would name it, and the source's
"or later" clause is [verify]); that it changes every sound in the game for headphones, not only conversations, and is
wrong for loudspeakers (so a setting, off for speakers); how it sounds: nobody has heard it, the numbers only say the
cues are there. Other libraries (Steam Audio, Resonance Audio: Apache 2.0) would mean taking each voice's samples away
from the game and playing them ourselves: not needed while this works.

#### OpenAL Soft as the mod ships it (2026-11-10, 22:00-23:00, fake headset; in the tree)

The owner: "feel free to setup openAL soft", and "remember, this is a mod that we're wanting to distribute, so installation
of the module needs to be generalizable". (The "module blocked" alert of the same evening was Windows' LSA protection
refusing Apple Bonjour's `mdnsNSP.dll` in `svchost`, `lsass` and Defender: 966 times that day since 10:20, nothing of the
game's or the mod's; Code Integrity events 3033, Security-Mitigations 12.)

**What ships.** `tools/fetch-openal-soft.ps1` builds OpenAL Soft from its source release (`openal-soft-1.25.2.tar.bz2`,
SHA-256 pinned) with our compiler, only the library, the C runtime linked in (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`:
its own `FORCE_STATIC_VCRT` does nothing under CMake's runtime policy, and the first build needed `MSVCP140_ATOMIC_WAIT.dll`),
into `external/openal-soft/` (gitignored): `soft_oal.dll` (2 MB, only Windows' own DLLs behind it), its licence texts and the
tarball. `tools/deploy-asi.ps1` copies them to `ASI\audio`; `tools/package-release.ps1` puts them in `payload\audio` and the
licences and the source tarball in `licenses\` (LGPL-2.0-or-later: the source from the same place as the binary; `-NoAudio`
leaves it out); MEVR Setup installs `payload\audio` into `ASI\audio` on the manifest, Repair puts back a missing file and
gives an install from before it the folder, Uninstall takes it out (`install/Installer`, `TestAudio`). **No game file is
replaced**, so Steam's "verify files" has nothing to undo. Not signed yet: SignPath's configuration names our four binaries
by product name "MEVR"; OpenAL Soft's says "OpenAL Soft".

**How the game is pointed at it** (`plugin/OpenAlSoft`, host, LE1; `[Audio] spatial = hrtf | soft | game`, default hrtf, read
at attach). Three tries, two lessons:

1. The exe's original import table (rebuilt by the DRM stub; the PE headers name only `Core/Activation64.dll`) imports 34
   OpenAL functions from `OpenAL32.dll` (IAT at exe+0xF9D7E8). Changing those 34 slots to OpenAL Soft's functions left the
   dialogue silent and Creative's driver (`wrap_oal.dll`) loaded: the game's audio engine reaches the router by
   GetProcAddress as well, so device and sources ended up in two libraries.
2. Detouring each of the router's 93 exported functions to OpenAL Soft's covers every route, but installed one by one
   through the loader's hook interface it took 9 s, and the game set up its audio half way through (its context made with
   OpenAL Soft on a device opened by the router: no sound).
3. **Now:** every detour created first, then all enabled at once under one freeze of the other threads (MinHook's queue,
   the host's own MinHook), at the very start of attach, before Halcyon's; if Creative's driver is loaded once the switch is
   in, the router opened a device first, and everything is put back (the game keeps its own OpenAL, logged). The device and
   its context go through two wrappers: the device asked for, else OpenAL Soft's default (the game asks for
   "DirectSound3D"); the game's attributes plus `ALC_HRTF_SOFT` (true for hrtf, false for soft). The switch came 6 s before
   the game opened its device on this machine (`audio: alcOpenDevice called from MassEffect1.exe+0x799B1`); the device is
   "OpenAL Soft", HRTF on with its built-in data set (`"Built-In HRTF"`), no `wrap_oal.dll` in the process.
   `[Audio] trace = 1` logs every OpenAL or DirectSound library load with who asked.

**Measured** (the ceremony from its save; Anderson 1.8 m to the right, 77 degrees off the view), the same as with Creative's
DLL replaced: his lines 4.0 / 4.4 dB to the right and the left ear 0.92 / 0.94 ms behind (0.77 / 0.83 alike there, -0.27 /
-0.35 with no shift); the head turned round: -3.5 / -3.8 dB and the right ear 0.88 / 0.92 ms behind; a councillor (not placed)
0.0 dB, 0.00 ms, 0.97 alike.

**What it gives the rest of the game, without any work per sound:** everything the game positions (other people's
gunfire and footsteps, explosions, the world's emitters, barks: of the dialogue cues 4,408 have a 60 m falloff and 1,262 other
attenuation) is now rendered for two ears; with Creative's driver it was panned. Not measured in combat yet (no save puts the
fake headset in a fight). What it does not change: sounds the game plays unplaced (music, ambient beds: balance 0 with the
head sweeping during the walk to the Council, either driver), the 82% of dialogue cues without attenuation (placed by
`engine/VoicePlace` only in a first-person conversation, within 8 m), and the player's own gun (played at the pawn, so at
the listener).

**The mix's level** (`[Audio] gainDb`, default 3, the owner: "the 3 dB will need a boost in general"): OpenAL Soft's mix
was 3.1 dB under Creative's (a councillor, unplaced: -29.4 against -26.3 dB a channel). Its only knob is its config's
`volume-adjust`, so the host writes `%LOCALAPPDATA%\MEVR\openal-soft.ini` at each start and names it in `ALSOFT_CONF` before
the library loads (its static C runtime copies the environment then; a player's own `%AppData%\alsoft.ini` is read
first), and asks for its output limiter (`ALC_OUTPUT_LIMITER_SOFT`, off by default for float output). Measured: the
councillor -26.3 dB a channel, as with Creative's; Anderson's line 4.1 dB right, 0.92 ms; the loudest sample of the
scene 0.75 of full scale, none clipped.

### People walking to their marks (2026-11-10 23:30 to 2026-11-11 02:20, fake headset; in the tree, the world mode's default)

Item 1 of "What remains". The stage's moves between marks during lines come through `UBioConversation::SetPositionRotator`
(item 4's source hook), which withholds them; each withheld ask for a place more than 0.3 m away is now logged (`convo: at N s
the stage asks X onto a mark N m away (N asks for it so far; Y speaking): withheld`) and, with `worldWalk`, becomes a mark the
person walks to. What the asks are, over the walk runs: single lasting moves of 1.5 to 3 m (the medical bay's wake-up scene:
Ashley 2.91 m, a crew member 1.50 / 1.72 / 2.63 m, asked again at each later line for the same mark; C-Sec's Wrex 2.22 m;
the salarian at Noveria 9.06 / 9.47 m), and the player's own pawn (always kept). Whoever the stage asks back onto where it
already stands before its walk began drops its pending walk (the earlier mark was the camera's).

**The game's AI, and the four things that held it** (read with Ghidra and Legendary Explorer's decompiler, through
`conversation-scan --script Class.Function | grep:<text>`, which now also prints enums and greps every function of a package;
the game's code stays out of the repository):

1. **The AI's enable counters.** `BioAiController.PushMoveToPoint`'s native (exec `0xB7B690`, the controller's vtable slot
   `+0xAD8` = `0xC60670`) returns no move unless slot `+0xA50` (`0xC677C0`, `CanUseStack`) says yes: the pawn's behaviour
   (`Actor.oBioComponent`, a `BioEpicPawnBehavior`) must have `m_nALLEnabled > 0` and `m_nStackEnabled > 0`. They are
   counters: `AbilityEnable(bEnable, DT_AI, subtype)` (`0x9E6770`, names `ALL`, `Queue`, `Stack`, `CombatTransition`) counts
   them down and up, and fires `DoDisabledEvent`/`DoEnabledEvent` on the way through 0 (the controller's `OnAbilityEnabled`
   only tells the squad). At C-Sec seven people start with `ALL` at 0 (Wrex and the officers of `WrexHasseledByOfficer`;
   that sequence's own `BioSeqAct_AbilityEnable` actions name the subtype `None`, which matches nothing, so they come from the
   pawns' own setup). A walk raises a counter that is at or below 0 to exactly 1 (`cw::AiEnableDeficit`, unit-tested) and
   lowers it by as much after, so any change the game made meanwhile stands.
2. **The conversation's ban on AI movement.** `BioAiController.ShouldCancelMove` asks `DoesGameModeAllowMovement`, which is
   false in the conversation mode (`SFXGameModeManager.AllowsAIMovement`, bit 8 of the active mode's flags; the game's own
   `BioAI_Ambient` answers true). `engine/call_function`'s answer handler says true for a walker's controller. Measured: never
   asked; only the Kismet movement state (`BioSeqAct_Movement`) asks it, not the move-to-point state. Kept as a guard.
3. **The paused speed.** The conversation pauses its people's controllers (`PauseController`: `SetDesiredSpeed(0, true)`),
   and the move-to-point state never raises the speed (normally `AdjustGroundSpeed` does once the pawn accelerates, which it
   can't at speed 0): `BioPawn.SetDesiredSpeed(1, false)` each tick of a walk (`0xAA0090`: the behaviour's
   `m_fDesiredMaxSpeed`; the pawn's speed state, `0xAA1260`, forces a standing pawn's `DesiredSpeed` to 0 while that is 0),
   0 again after it, as the state's own `Cleanup`.
4. **The gesture mode, the one that mattered.** With 1 to 3 done the controller sat in `BioSeqAct_MoveToPoint` with the
   latent move running (the engine asked the state's `GetNextPredictedMoveLocation` every tick) and the pawn still never
   accelerated. Its animation tree (dumped with `worldWalkLog`) showed why: the top node, `Actions`
   (`BioAnimNodeBlendByAction`), had child 10 `Gestures` at weight 1 and child 0 `Posture`, where the locomotion lives
   (`CombatMode > NonCombat > EX_Posture > Standing > MoveStart > SkidTurn > MoveStop`), at 0. The node's update (`0xBFD3C0`)
   picks `Gestures` whenever `BioPawn.m_bInGestures` is set, and a conversation's gesture track sets it natively for each actor
   it animates (`0xC904A0`, when the actor's gesture starts and the flag is clear). A walker leaves the gesture mode for its
   walk (`m_bInGestures` cleared, journalled, `worldWalkGestures = 1`); the next gesture the scene gives it puts it back, the
   track's own way. Then the AI walks it with the game's locomotion: start, walk cycle, stop.

**What it looks like in numbers** (`worldWalkLog = 1`: the walker's ankles in its mesh's space ten times a second for 3 s,
after a `ForceSkelUpdate`, so the view's direction doesn't matter; `convo: walk: gait ...`):

| Walk | Ankles over the walk | Speed | End |
|---|---|---|---|
| Slide (`worldWalk = 2`), gesture mode on or off | still: L (-7, -9, 11), R (-7, 10, 11) every sample | 0 (the engine zeroes the slide's velocity between ticks) | on the mark, gliding |
| AI, gesture mode on | still | 0 | never moved (deadline) |
| AI, gesture mode off (wake-up scene) | alternate: 29, 66, 73, 28, 31, 55, 45 units apart; forward reach -47 to +30 | 197-389 at the start step, then 130 (`WalkSpeed`) | 0.18-0.27 m from the mark |
| The engine's walking (`worldWalk = 3`; 2026-11-11, four walks in the wake-up scene) | still: L (-7, -9, 11), R (-7, 10, 11) | 0: the acceleration moved nobody in 0.75 s | the slide's fallback: on the mark, gliding |

Six AI walks in the wake-up scene with the arrival rule below: five arrived 0.24-0.25 m from their marks in 0.7-2.0 s,
one ended 1 m short at its deadline and finished on the stage's next ask for the same mark. **Seen** (the head held 40
degrees left of the held view by a replayed recording, `make-look-recording.py --yaw`, and the game's camera at the player's
place, `worldCamera = 1`, for that run only): Anderson mid-stride, one heel lifted, arms swinging, walking away to his mark,
and 0.4 s later standing on it, feet together (`2026-11-11_02-00-00-395` and `-797`). With the defaults (`worldCamera = 0`):
four of five walks arrived; one took the long way round the bed (to 1.2 m past the mark at its deadline) and came back on
the next ask; one stopped 0.7 m short. **C-Sec's Wrex** (his `ALL` counter raised from 0) entered the move state with speed 1
and out of the gesture mode, yet his move timed out without the state's loop running (`MoveTimer` -9.7, the state's
`vMoveTarget` never set): he was hidden (below). A walk the AI hasn't moved 8 units in 2 s (1 s cut the bed path short) leaves the
person where it got to: a 2 m glide with planted feet looked worse than staying (the slide's gait, above), and that mark
isn't tried again in the conversation (the stage asks again at every line: without that Wrex restarted every 4 s).

**The rules now** (`game/ConversationWorld`, `worldWalk = 1`, the default with the world mode since this round): an AI walk
arrives within 0.25 m on the floor (the move's own tolerance is 0.20; the stage's mark lies 0.83 m lower than the pawn's
centre, so a 3D distance never closed), then turns to the facing the stage gave with the mark; its deadline is twice the
walk's time plus 5 s (paths go round things); the AI's counters, its speed and the answered question are let go at any end.
An AI that can't take the walk at all (no `BioAiController`, no behaviour) also leaves the person where it stands.

**Why C-Sec's people didn't walk: they were hidden.** Their controller entered `BioSeqAct_MoveToPoint` but the state's code
never ran (its `InitState` never set `m_oBioPawnCache`, its `vMoveTarget` stayed 0), with the AI switched on the game's own way
too (`BioActorBehavior.AbilityEnable(true, DT_AI, 'ALL')` through the engine's name lookup, `engine/NameLookup`, `0xD1150`
in find-only mode; it fires the enabled event a written counter skips). Both controllers were in their default state before
the move; the pawns differed in `bHidden`: Wrex (`BioPawn_0` of `STA30_01_DSG`) was hidden. The runner opens the level with
the plot reset, so the standoff (`WrexHasseledByOfficer`) isn't due and its people are hidden; the game runs no state code for
them. Not a limit of real play. A hidden person is now never walked or given a seat (`it is hidden`).

**The validation saves, run (2026-11-11 03:16-03:43, `tools/conversation-from-save.ps1 -All`, the plot each save set):** nine of
ten scenes started (slot 84, Wrex on Virmire, still doesn't). Walks in them: the Spectre ceremony's officials were asked 0.78-
0.95 m steps, and the AI turned BioPawn_7 179 degrees to walk his step back and turned him round again; Benezia's mark at Noveria
(2 m) wasn't walked (the AI didn't move her: she stays). New rules from it: **a mark is walked only if it is 1.5 m or more away,
or a step within 60 degrees of where the person faces** (`cw::WalkWorthIt`, unit-tested; `worldWalkLongM`, `worldWalkStepDeg`):
a step back or aside keeps its place, logged `(a step N deg off its facing: it keeps its place)`; re-run, BioPawn_7 kept his
place at all eight asks. **A walk the AI never made puts the gesture mode back** (`back in the conversation's gesture mode`), so a
person whose walk fails isn't left in its standing pose. The other bodies the session judge flags as pivots in these scenes (the
ceremony's 79-86 degree turns, Benezia's 140) are the re-face-for-the-player rule ("Who attends to whom"), not walking: the same
before and after.

### Moments from saves, first sample (2026-11-11 04:40-04:50, fake headset; item 11)

Four of the 21 moments (`conversation-sweep.py moments`), one per level, each started by a use or a touch at a placed spot, written
as validation saves into slots 90-93 of the test career (`conversation-sweep.py saves ... --first-slot 90 --manifest
validation-saves-moments.json`; `saves --remove --manifest ...` deletes them) and played with `conversation-from-save.ps1 -All
-FirstSlot 90 -List <the list>`:

| Slot | Moment | Result |
|---|---|---|
| 90 | Chellik bumps into Shepard (Citadel, a use) | fired; it led into `sta60_trig_02_jennas_contact_dlg` (3 lines, a stay), where the judge found Shepard's head up to 1.73 m from the view |
| 91 | Benezia, phase 2 (Noveria, a touch) | the save didn't come up on foot in two minutes |
| 92 | Liara's intro (Therum, a touch) | the save didn't come up on foot in two minutes |
| 93 | The geth intro (Feros, a touch) | played in first person: the script moved Shepard at its start (one dip, "the script moved Shepard to another place"), the view carried 5.0 m with him, nobody walked (no stick in the run) |

Open: why 91 and 92 don't come up on foot (a load into a cinematic or the Mako, as slot 84 with its flag?), and Shepard's 1.73 m
stray after Chellik's bump (the moment's own move of Shepard, then the conversation's stay). Both answered in "A moment on foot".

### A moment on foot (2026-11-11 05:00-05:23, fake headset; item 11)

**91 and 92 start as their saves load.** Their trigger is at the player's spot, so the scene began during the load and the
runner, waiting for the player on foot, gave up. `conversation-from-save.ps1` now takes a scene that is already in the
conversation or cinematic mode as begun by itself and drives it. Liara's intro (92) then played: 10 lines, a walk-in by the
view of 4.1 m, nothing found. Benezia's phase 2 (91) goes to the 3D screen as before (its conversation and the cutscene after it).

**Chellik's bump is a moment without the cinematic mode.** The judge's 1.73-1.82 m `body-strayed` came from the opening's record of
the player's view on foot: 1.64 m behind Shepard's pawn. Snapshots during the bump (4 s after the use) showed why: the eyes held
a **third-person, letterboxed shot of Shepard from behind**. The bump's matinee has a director track whose camera actors
(`BIOA_STA60_05A_DSG...CameraActor_10`, `_11`, `_12`, cut between) become the player camera's view target **in game mode 0**,
while its move track walks Shepard 2.9 m in 8 s. The moments were held only in the cinematic mode (6), so Halcyon showed
the matinee camera as gameplay. The on-foot record (`gFoot`, taken every camera update in mode 0) took that camera for the
player's eyes, and the conversation that began as the track let go opened from it. Its view walk joined Shepard with the camera's
offset kept, which left the view 1.6 m behind him for the scene. In the conversation, Shepard's body stood in the view
1.7 m ahead; with tracked hands (a replayed recording) the body follower takes it under the player as designed.

The fix, in three parts:

- **The camera post-handler reads the camera's view target** (`Engine.Camera.ViewTarget.Target`, a member of the struct; a
  nested `FieldRef` is read from the struct's address, not the object's). In game mode 0 with a target that is neither the
  player's controller nor his pawn, it records no view on foot. While such a camera or a moment holds, it keeps only the POV's
  addresses for the render-time hold. Log `moment: on foot the game's camera is a matinee's: <camera actor>` and `... the
  player's again`.
- **A moment begins in game mode 0 too**: a track moving Shepard while a matinee's camera has the view
  (`moment: a matinee walks Shepard with its own camera on foot (no cinematic mode)`). It continues while that camera does,
  plus the usual second, and ends as "the matinee's camera let go" or when a conversation begins. With the view walk (the default),
  the view starts **where the player stood**: the moment's first tick comes after the track's first move, which had already
  taken Shepard 2 m on, and the view went with him until this fix.
- **The host drops the letterbox for it** (`[MEVR]` seam in xr_session.cpp: the `[NOLETTERBOX]` clear also in game mode 0
  while the mod's state is published, `ConversationInWorld()`; host and mod deploy together).

Seen on the fake headset with a replayed head pitched 25 degrees down and tracked hands (`2026-11-11_05-18-21-126`,
`05-18-25-164`): the bump in first person, full frame, from the player's place; the conversation after it with the wheel in
front and no body in the view. Judge: nothing found (a 0.84 m guided view walk, the two one again at 0.6 s). 91 and 92
unchanged. A matinee camera in game mode 0 that **doesn't** move Shepard (a door's shot, a fly-by) is still shown as
gameplay; none has been seen yet.

### People animated by gestures only (2026-11-11 05:55-06:11, fake headset, Noveria's ICE60_06; items 1 and 7)

Han Olar's conversation (`conversation-launch.ps1 ice60_hanolar`, a stay opening) planned two seat walks: the scientists at the
window (BioPawn_3, BioPawn_4) were out of view, 7.6 and 8.0 m from their seats. Both arrived by the game's AI (8.4 s and 16.3 s;
`convo: walk: arrival ...`), but the snapshot during the conversation (`2026-11-11_05-55-31-205`) shows them **standing in the
reference pose, arms out**, where before the scene they stood talking at the window (`05-50-21-987`). With `worldWalkLog = 1`
the walk probe gave the reason (`walk-probe/BioPawn_4-tree-*.txt`): their tree's action node (`BioAnimNodeBlendByAction`) has
**nothing under any child**, `Posture` included. They are animated through the conversation's gestures only, so out of the
gesture mode (the walk clears it, as for Anderson) they show the reference pose, and the gait trace had their ankles fixed at
1.3 m/s: a glide. Anderson's tree, which walked, has its locomotion under `Posture`.

**Fix:** before an AI walk the walker's tree is read (`HasPosture`: the first action node's first child). Someone with
nothing there is not walked (`convo: walk: X ai unavailable: its animation tree has no walk (nothing under Posture: animated
by gestures only); it stays where it stands`). Run again: both stay at the window, posed, through the conversation
(`06-10-44-423`). A tree with no action node at all is walked as before.

### The saves and their tests (2026-11-11 11:15; the owner: "a dictionary mapping all of the saves to their respective dialogue tests")

`tools/save-dictionary.py` reads every `.pcsav` under the Save folder (`tools/le1-save.py`) and says what each is for. A
validation save stands Shepard where `conversation-sweep.py saves` puts him for one scene (in front of its owner or trigger, facing
it, on its level): matched within half a metre and two degrees against every scene of the scan, the scene is named; several
scenes can start at one place (Anderson starts the walk to the Council and the Spectre ceremony; Avina's keeper and her
triggers), so the list the save was written from decides (`--list file@firstSlot`: the headset sample from 80, the moments
sample from 90, now `tools/conversation-moments-sample.json`) and the others are named. A career's own save lists the scenes
whose owner or trigger stands within 40 m of it. Each entry carries the command that plays it (`conversation-from-save.ps1 -All
-List save-dictionary-list-<career>.json -FirstSlot 80 -Only <slot>`, or `conversation-launch.ps1 <scene> -Save <file>`). Output in
`%LOCALAPPDATA%\MEVR` (`save-dictionary.md`, `.json`, the slot lists): the player's saves, never the repository.

On this machine (27 files): John02_80 to 93 are the fourteen validation saves (80 the walk to the Council, 81 the Spectre
ceremony, 82 Opold, 83 Anoleis, 84 Wrex on Virmire, 85 Chorban, 86 Benezia, 87 Wrex at Fist's, 88 Kirrahe's camp, 89 Avina, 90
Chellik's bump, 91 Benezia's phase 2, 92 Liara's intro, 93 the geth intro), all from John02_21 (the Citadel); John01_03 and its
autosave stand 12.6 m from Powell on Eden Prime (the train's test); John00's saves are the Citadel and the ending; VR00's the
Mako on UNC20; `Char_01` is a character export.

### An adversarial review by a second model (2026-11-11 10:15-11:05; Codex, read-only)

The owner asked for adversarial code reviews by a Codex process at extra-high reasoning. It read `git diff 7c89c3a..dd5b9d2`
(the day's 29 commits, 37 files) with the surrounding code, without building or running anything, and found no off-thread
script call, data race or unjournalled engine write. Its eleven findings, each checked against the code here:

| # | Finding | Verdict | Fix |
|---|---|---|---|
| 1 | `mevr vslot` read a kept object after a level change, and a hand-typed offset past the vtable | real (a diagnostic) | alive check, parsed offset (aligned, under 0x2000), the read under SEH |
| 2 | a stage asking another mark mid-walk retargeted the walk without ending the AI move or lowering the AI counters it raised (the next start recorded nothing to lower: the AI stayed on) | real | the walk ends first; `EndWalk` lets go of what it owns whatever the phase |
| 3 | at a handover the next stage could move Shepard before the old conversation ended, and the place on foot paired the old held view with his new place (the next scene measured no move) | real in principle | the feet that go with the held view are where the old conversation kept him |
| 4 | the guide waiting at a mark the track still holds set "progressed" and then `stop()` cleared it: after 2 s it gave up for good | real | progress set after `stop()` |
| 5 | the gesture mode a walk clears came back only when the AI stalled | real | put back however the walk ends |
| 6 | the flowed subtitles' render mode stayed bottom when the setting went off mid-scene | real | `Restore` puts the mode back too |
| 7 | in a crowd past the overheard list's cap, someone left out who starts speaking could never count as heard | real | the hook notes the owners of centred sources no list names; the module keeps those nearby |
| 8 | the runner took the Mako's game mode 1 for a scene that had begun (the fallback past the checks never fired; the record said played) | real (found here too, the creeper) | modes judged against the mode before the fire; "played" for a moment needs its track |
| 9 | the save runner took any conversation that began on load for the scene asked | real | the conversation's name must match |
| 10 | a scene that began by itself skipped `-Walk` and `-Replay` | real | both applied either way |
| 11 | the seven-people bound of the seat search was not pinned by a test | real | seven reassign, eight keep the seat order |

**A second pass over the fixes** (`dd5b9d2..dcc8087`, 11:05-11:40): no new crash, threading or journalling finding; five more,
two of them in the fixes themselves, all fixed:

| # | Finding | Fix |
|---|---|---|
| 1 | the handover fix took Shepard as brought to the view even when that move was refused, and paired the view with his new place again | whether he came is what the move returned; if not, the view's own feet go with it |
| 2 | (suspected) putting the gesture mode back while the conversation itself ends could leave a person in the gesture branch, with no locomotion, after the scene | not while the conversation is torn down (the game's teardown decides) |
| 3 | a prerequisite conversation's summary, or another matinee's move of Shepard, still made a moment "played" | a moment is judged by its own matinee first (the moment's line names it); another one is "another matinee moved Shepard" |
| 4 | the save runner took a cinematic on load (mode 6) for a conversation's scene | a conversation's test waits through it; only its conversation, by name, counts |
| 5 | flowed subtitles switched off mid-scene left their render mode set | put back when the option is off, at once |

**The handover, run again** (11:30-11:33, the dev build on the fake headset, `conversation-launch.ps1 nor10_comm_room -World on`):
the comm room's conversation (a stay, 9 lines) handed over to Anderson's as before; at the handover `Shepard was moved 4.18 m as
the conversation ended (a next stage's mark): the view's place on foot keeps the feet this conversation had him at`, so
Anderson's opened as a **walk-in of 4.2 m in the same room, no cut** (until the fix it was taken from the head bone, a
re-placement); the player was guided 2.7 m, Shepard's head never more than 0.57 m from the view, the one dip at the end the
script's move to the next part of the ship. Nihlus's seat walk arrived (item 7). The judge counted body turns of 160-180
degrees (Nihlus in both conversations, Anderson in his): the re-facing rule for a player who walks in from behind them, as
designed; whether that much turning is wanted is the owner's audit.

### The scripted walks, named (2026-11-11 08:13-, fake headset, the sweep runner; item 11)

`moment: a matinee's move track takes Shepard from ... : <path>` now names the matinee: the move track's instance lives in an
`InterpGroupInst` inside the `SeqAct_Interp` that plays it, so its path is the matinee's (`AuthoredMoment`, the hook's
`instance`, named on the game thread with `IsAlive` first). Opening `BIOA_PRO00` plays three before Joker's scene:

| Matinee | What it is | Shown |
|---|---|---|
| `BIOA_NOR10_13_LAY ... SeqAct_Interp_1` (the plan's `moment_nor10_13_lay_seqact_interp_1`) | the prologue's opening as the level loads: Shepard placed 34.7 m away (path 0 m) inside the load's black screen, 38.7 s, modes 8 then 6 | the 3D screen (it starts under the front end's rule, a load from the menus) |
| `BIOA_GLO00_A_Opening_Flyby_CIN ... SeqAct_Interp_1` (two more after it) | the opening fly-by: Shepard placed 380 m away, 6-13 s each | the 3D screen; not one of the 21 (a cinematic level's) |

Neither walks Shepard: they are the game's opening cutscene, and the screen is right for them.

**Noveria, after the squad falls** (`moment_ice50_04_dsg_level_events`, `BIOA_ICE50_04_DSG ... Level_Events.SeqAct_Interp_0`;
its event is the squad's death, fired by the runner past its plot check 4181): the script switches the cinematic mode, waits a
second (`BioSeqAct_Delay_0`), fades to black, teleports Shepard 5.7 m (in sight: the same space), autosaves, and only then
plays its matinee, 3.3 s after the switch; the matinee walks him 2.75 m in 20.2 s. The provisional hold let go after its
quarter second (`no track moved Shepard (0.25 s without one)`): **the 3D screen came under a dip and went under another
3 s later, in one space** (the owner's "no flash", "no dip within one space"). Then the moment was held in first person;
the guided walk to him stopped 7.0 m short (blocked) and, the cinematic mode over, Shepard was brought to where the player
stood (no cut). Of every cinematic switch in the sweep logs this lapse happened twice: here and on Therum's krogan entrance,
whose own camera shot comes first (the screen right there). **Fix** (`cw::StepProvisional`, unit-tested): the provisional
hold waits on, up to 6 s, while the game's camera is still the player's (its view target the controller or the pawn: the
script's delays, black screens and moves before its matinee); a camera of the scene's own ends it after the quarter second as
before. Log `moment: no track yet N s after the cinematic switch, and no camera of the scene's own has the view: the view stays held`.

**Feros** (08:27-08:32): **the transition to the tower** (`moment_war20_13`, `cinematic_design_war20-30_transition`) started as the
runner stood Shepard in its trigger: a cutscene with its own camera from its first frame, 80 s on the 3D screen, no track
moving Shepard in that time (it streams the next part of the level in): the screen by the rule, and right. **The first creeper,
on foot** (`moment_war40_11_dsg_on_foot`, `On_Foot.SeqAct_Interp_2`, fired past the scene's check): held in first person from
the cinematic switch (0 ms, no flash), the track walked Shepard 9.0 m in 9.4 s, the player was guided 5.2 m after him, and the
script's teleport of 8.9 m at its end was in sight (the same space: no cut); the judge found nothing. **The same scene from the
Mako** (`moment_war40_11_dsg_in_vehicle`): its script teleports Shepard 18 m and then waits for him to leave the Mako
(`BioSeqAct_ExitVehicle`); on foot that never completes, so its matinee never came: to see it, drive the Mako there.

**Seen now: 20 of the 21** (with Eden Prime's train, the gambler, the creeper from the Mako and the ending's first, below) (the 10 before, the krogan's entrance on the screen, the Normandy's opening on the screen, Noveria's
scene after the squad falls, the Feros transition on the screen, the first creeper on foot, the double homicide: below). Left:
the ending's second (`SeqAct_Interp_2`, the other branch of the final cutscene: this career's choice plays the first).

**Noveria's double homicide** (`moment_ice20_03a`): opening `BIOA_ICE00` leaves Shepard on the docked Normandy, the moment had no
place (its event is the chunk's load, no originator), so he stayed there and the chunk streamed out again once the runner let
its volume go (`action not loaded: gone after Shepard was placed`). Its way begins with Anoleis's conversation, then plot check
2713, then the cutscene. Now the plan stands such a moment at its conversation's owner (`conversation-sweep.py moments`), and the
runner fires past the check when the conversation is over and no track took Shepard in 8 s.

**The rerun** (08:33-08:36, both fixes in): **the double homicide played** (the runner fired the cutscene's sequence past the
check: the conversation start did not answer in 8 s): held in first person from the switch (0 ms, no flash); its matinee moves
Shepard 9.4 m out of sight, from Anoleis's desk to the crime scene, so the view went with him under one dip (another
composition: right by the rule, and where a player stands after Anoleis anyway); he stands there 25 s while the scene plays. **The
snapshot 2 s in showed a wall a hand's breadth from the eyes**: the view had kept the facing it had at the desk. **Fix**: a cut to
another composition inside a moment faces the way the script turns Shepard there (his pawn's yaw at the new place; log
`... the view goes with him under a dip, facing his way there (N deg, was M)`). Run again (08:43): facing the scene (269 degrees,
was 90), Gianna Parasini in view, **and Shepard's own back filling the middle**: the view's offset from him (half a metre
ahead at the desk) had moved with him but not turned, so it stood half a metre behind him. The offset now turns with the facing.
Run again (08:46): the view at his eyes, facing his way, nothing in front of it but the room's corner (the matinee's camera
stands 2.2 m to his right, filming toward him); whether his own facing shows the scene's subject is [verify] in the headset,
where the player looks round anyway.

**The gambler on the Citadel** (`moment_sta60_07toss`, a remote event: no originator, no conversation first): the runner now stands
such a moment in the streaming volume that brought its chunk in; at the volume's own place (-10144, 5824, -30416) the chunk
streamed out again 4 s later (the place is the brush's pivot, not inside it). The level's script, read with
`conversation-scan --sequence` (local, game data): entering Flux (`BioTriggerVolume_1`, the casino's door) checks two plot
conditionals (802 "Time for the cutscene?", 1409 for the Destiny Ascension variant), turns the named streaming state
`stream_schellsToss` on (or `stream_schellsTossDestiny`), blacks the screen, and once that streaming is done fires
`re_schellsToss`, which starts the cutscene. The chunk shows only under that state, so the plan now gives a moment the named
states its chunk needs, as the catalogue does for conversation starts (`conversation-sweep.py moments`: the gambler
`stream_schellsToss`, the Feros transition `ThorianControlled`, the council's `stream_council`), and the runner turns one on
before it streams. A moment fired by another sequence's remote event still has no place of its own (the scan does not link a
remote event to its activator's trigger): the gambler's run stands Shepard at the Flux door by hand.
**Run 09:25-09:27** (`-Maps BIOA_STA00 -EndUnplanned`, the wake-up conversation that plays by itself ended): the chunk streamed
and stayed, `CD_Gambler_Cutscene` fired; held in first person 219 ms after the cinematic switch (no flash), the script moved
Shepard 6.1 m in sight (the same space: no cut), the player was guided 6.0 m and reached him; 22 s, no dip. The snapshot 4 s in:
Flux's stairwell from the player's eyes, and Shepard's own body standing at his mark 6 m ahead: on the fake headset arm IK draws
no arms (no tracked controllers), so the body follower that puts his body under the player (`worldBody = player`) never runs;
[verify] in the headset that the body walks with the player there.

**Feros's first creeper from the Mako** (`moment_war40_11_dsg_in_vehicle`, 09:35-09:38): Feros has a Mako loaded
(`BIOA_WAR40V_00_DSG`'s `BioVehicleWheeled_1`); the runner now puts Shepard in the level's nearest vehicle before firing a scene
marked `enterVehicle`. Its fallback past the plot checks waits for the game mode to leave 0, which the Mako's mode 1 already
has, so it never fired; started by hand (an object inside `In_Vehicle`, `ref_IsTag_Henchman`, by a loose match: the switch to
the cinematic mode came all the same), the cinematic mode began with Shepard at the wheel: **the 3D screen at once** (a moment is
held only from on foot: `MomentMayStart` needs a fresh view on foot) for 10 s, then the script's black screen and the world;
no track took Shepard. From the Mako the screen is the design; whether the scene should open in the cockpit's view instead is
the owner's to judge.

**The ending** (`BIOA_CRD00`, 10:52-11:02, the owner: "Yes you can run the two"): opening the level runs its script at once
(the end-game statistics, the achievement step, then the Council-alive conversation); the first run ended that conversation
(the runner's `-EndUnplanned`) and nothing followed; run again with it played: the conversation on the 3D screen (a set piece,
151 s), then the final cutscene's walk (`Cutscene_end90_final_cutscene_dlg.SeqAct_Interp_1`) held in first person 15 s: the
screen's dip to the world and the cut to his place 557 m off (another composition) 240 ms apart, then the 3D screen and the
ending's movie. The script's character save wrote `Char_02.pcsav` (moved aside with the saves' restore). `SeqAct_Interp_2` is
the other branch of the same cutscene and did not play with this career's choice.

**The ending's other branch** (2026-11-11 11:42-12:01, the same approval): the scan's way to each branch is the same up to one
check, `BioSeqAct_PMCheckConditional` on the game's conditional function F1289, which (read with the scan's `--script`, kept out
of the repository) holds when plot integer 46 is larger than plot integer 47; `Interp_1` is its False output, `Interp_2` its
True. The John02 career has 15 and 17, and `open` resets the plot (both 0): the first branch both ways. A save written from
John02 on `BIOA_CRD00` with integer 46 set to 18 (`le1-save.py write ... --int 46=18`, in the scratchpad, never in the career)
and loaded through the fast loop (`conversation-from-save.ps1`, the scene begins as the level loads): the Council-alive
conversation on the 3D screen (78 s with lines skipped), then `Interp_2` **held in first person** for 16.9 s: the script moved
Shepard 203 m into the tower (another composition: one dip, facing his way), his pawn stood still there for the whole track
(path 0 m), then the 3D screen and the ending's movie on the flat panel. Run twice (the second time to measure), the same.
**Seen in the snapshots: the platform under him is black.** From his eyes looking along his facing, the tower's atrium is
there and lit, but the bottom third of both eyes is pure black (under 4 of 255) up to a straight edge about 2 m ahead (the two
eyes' disparity), the platform he stands on, a surface the authored shots (the director's camera 2.6 m in front of him and
1.2 m lower, looking back at him) never show. Measured, not the known causes: the near plane was the on-foot 10 units
(`mevr convo body`), and with the game's camera held at the player too (`worldCamera = 1`, so culling and streaming from his
place) the same 51% of the bottom third stayed black. [verify] in the headset whether it reads as a dark floor or as a hole; if a
hole, the ending's walk belongs on the 3D screen (a set built for its cameras only). **All 21 of the scripted walks are now
seen.**

**Eden Prime's train** (`moment_pro10_00_plc_train`, 08:52-09:11): past the prologue, so from the John01 career's own save on Eden
Prime (22 minutes in, near the tram station), copied into the loaded career as a new file (the Load screen lists only the
current career's saves) and loaded through the Load screen (`conversation-launch.ps1 pro20_powell -Save VR00_08 -Load menu`,
Shepard in front of Powell, the dockworker by the tram). The scene is the train's arrival at the far station (`train.SeqAct_Interp_1`,
"Train arriving at the far station"): after the Saren cutscene the remote event `re_SarenCutsceneDone` fades in
(`BioSeqAct_BlackScreen_0`) and the matinee carries Shepard, Ashley and Kaidan on the tram under its own cameras, no cinematic
mode: a moment on foot. Fired at that fade-in, it showed two faults:

- **A ride taken for teleports.** The track carries Shepard 0.30-0.45 m a tick for 11 s; each step is past a jump (30 units), so
  each was a move in the same space: the view stayed and the guided walk set off after him (50 log lines a second). **Fix**
  (`cw::IsRideStep`, unit-tested): a step past a jump that follows another step the tick before (both under 2 m) is motion, and
  the view rides with him (log `moment: the track carries Shepard N m a tick, tick after tick: a ride`; the view summary's `rideM`).
- **The first move never judged.** A moment seen on foot begins a tick after its track's first move, and took that place as
  where Shepard was: the first key put him 1,141 m off (the test stood him at Powell, not on the tram), the view stayed at
  Powell and was guided toward a mark a kilometre away. **Fix**: his place before that move is the moment's first, so the same
  tick's step logic judges it: here another composition (out of sight), the view going with him under a dip.

Run again (09:10): the 1,141 m move a cut to the far station (facing 90 degrees, his way), the ride carried the view, the
script's jump to the stage 1.8 m in sight (no cut), its black screen a dip in its place (a person new 1.1 m away: the rule).
The snapshot 2 s in: the far station's tracks and the spaceport's tower from the player's eyes. A player boards the tram at
the near station, so the first cut is the test's; the ride and the arrival are play's. **Noveria after the squad falls, again**:
`held in first person ... since the cinematic mode began 3203 ms ago (no flash of the screen)`: no 3D screen, no fade; the
script's own black screen covered only the move in sight (no dip of ours); the player guided 8.6 m and arrived (`dips 0`).
The snapshot 3 s in: the corridor from the player's eyes.

### Seats for the pairs (2026-11-11 07:40-08:30, offline and on the fake headset; item 7)

The arc's seats went to the people brought into view in the order they were tracked. Now (`cw::CountPairs`, `cw::SeatsForPairs`,
unit-tested; `game/ConversationWorld`'s `SeatsByPairs`) the order is the one in which the people who talk to each other can look
at each other most easily: every line of the conversation counts (each entry's `nSpeakerIndex` and `nListenerIndex`, each
reply's listener with the player speaking; indices -2 the player, -1 the owner, 0.. `m_SpeakerList`'s `aSpeaker`), each person's
turn away from its seat's facing toward its partner costs its angle, four times over past `worldGlanceDeg` (70, what the look-at
chain reaches), the player's own turn costs nothing (the head is his), and every order of up to 7 people is tried; a tie keeps
the seat order. Log `convo: seats for the pairs (<pairs> lines): <who> seat <n> (the look's cost a -> b)`.

**The data** (the scan now records each line's listener, `tools/conversation-scan`): of 12,907 entries, 12,114 are said to the
player (-2), 375 to the owner, 418 to someone on the speaker list; 784 lines in 99 conversations pass between two other people
(most of them ambient: the market's buyer and seller, the Wards' couples; staged ones: Anderson and Udina in the Council's
ending, 26 lines; Kaidan and Ashley at Kirrahe's briefing, 10; Joker and Kaidan in the cockpit, 12). A reply's listener is -3
in 10,912 of 11,238 replies: "whoever spoke before", left out (the entry it answers already pairs them).

**In the game, Joker's cockpit scene** (it plays by itself as `BIOA_PRO00` opens the prologue; a cut opening, four people
brought into view): the pairs Joker~Kaidan 13, the player~a crewman 4 and five of one line; the new order lowered the look's
cost from 7541 to 5932. Two faults showed: Joker is both the owner and on the speaker list, so his pair with Kaidan came twice
(now merged after the indices are mapped to people), and one person's new seat was refused by the engine (`SetLocation`'s
encroachment check: 10 of 159 seat placements in the sweeps before were refused too, where the old order put them). **A refused
seat is now swapped** with someone of the same kind already placed, when both moves are taken (log `convo: X's seat n was
refused: it takes Y's seat m`). The judge: the opening's dip (a cut, as before) and two bodies turned 90 and 79 degrees by the
arrangement (104 and 115 in the run before the rule; the look-at rule turned nobody). **Run again with both fixes**
(08:15:56): Kaidan stood in view at the opening this time (three to seat; cost 1210 -> 1052); the crewman's new seat was
refused again and swapped (`BioInert_2's seat 1 was refused: it takes BioPawn_4's seat 3`): all three placed, none refused.

### Is the player's place ever necessary (2026-11-11 08:05, from the sweep logs; item 8)

The owner's rule for item 8: a small floor marker only where the player must stand somewhere. Every scene shown in the world
since the summary carries `arrived` (the view walk's day, 2026-11-10) was read from `%LOCALAPPDATA%\MEVR\sweep\*\logs`: 291
scenes in the world opened as 127 walk-ins, 117 cuts and 47 stays; of the 37 walk-ins with the field, **29 arrived** (the
council's hearing 21.4-21.7 m by the view walk four times, Wrex at C-Sec 2.9 m twelve times), **2 were blocked** (the Garrus
argument at C-Sec at once, 3.65 m, a flowed scene now; Wrex on Virmire with the body walk, stopped 18 m short of a 29.3 m mark)
and the rest ended before arriving (Kirrahe's check-in 8.2 of 10.4 m in a 10.8 s scene; the deployment 7.6 of 7.5 m; the
Spectre ceremony's, 2.6 of 21.4 m, twice). In none did the scene wait for the player or go wrong for
his absence: conversations are ended by nothing at a distance (the interrupt range is ambient-only), and with the view walk
Shepard's own pawn stays on his mark, so a trigger there is touched by him. **Nothing to build until a scene needs the place.**

### Virmire and the cockpit (2026-11-11 06:40-06:58, fake headset, the sweep runner; items 4, 7 and 11)

`conversation-sweep.ps1 -Plan moments.json -Maps <level> -Only <moment> -Pace shepard`, judged by `conversation-session.py`.

**Virmire** (`moment_jug40_01_dsg_jug40_00_player_deployment`): opening the level played the fly-by, the landing's
`jug20_trigger` (a walk-in, 7 m), the landing (the script moved Shepard 2.4 km to the site: the one dip, another composition),
Kirrahe's check-in (a walk-in by the view, 10.4 m, walked 8.2 m), the deployment walk held in first person (5.4 m walked, no
dip) and the check-in again (7.6 m): **nothing found** by the judge in any. No snapshots taken in this run. A seat walk at the
landing (BioPawn_10, 3.5 m) moved 0.16 m and stopped: the AI didn't carry it, it kept its place (`it didn't walk`); the
conversation lasted 5 s.

**Eden Prime** (`moment_pro10_00_plc_train`): opening `BIOA_PRO00` starts the prologue on the Normandy, so the train (later in
the level's plot) is not reached this way. Joker's cockpit scene (`nor10_joker`, 20 lines, a cut opening that put the player on
Shepard's mark, behind the pilot's seat) played by itself: **the re-facing rule turned Joker in his seat 134 degrees, four
times** (the player 171-174 degrees off his facing; the judge's `pivot`). The composition is the stage's there: Joker flies and
talks over his shoulder. **Fix** (`cw::MayReface`, unit-tested): no re-facing for the player while the player stands within 1 m of
where the stage composed Shepard (his place at the opening, or after one of his jumps: `composedAt`), and never for someone seated
(the head bone under 0.62 m above the person's own location; Joker and the co-pilot read 0.55 m, standing people 0.70-0.82).
Run again: 0 body turns by the rule (`convo: BioPawn_0's body keeps the scene's facing for the player: seated (its head 0.55 m
above its place)`). The judge still names two turns of 104 and 115 degrees: the scene's own (no turn, held or eased facing of
ours counted).

**Therum** (07:09-07:13, `-Maps BIOA_LAV00`): the troop drop started as the runner streamed its part in: held in first person 31 s,
the script's moves of Shepard (13 m, then a teleport of 8 m) in the same space, the view kept at the player's place (the guide
found no floor way up the 1.75 m rise), **nothing found**. The runner's place for it is the save generator's, 4 m in front of the
trigger volume, over ground that kills Shepard (the save of the third sample died there): when the scene handed Shepard back he
fell, a test artifact (a player walks in along the path). The krogan boss's entrance went to the 3D screen by the rule: no track
in its first 0.25 s (it started 4 s in). Her conversation (`lav70_trig03_krogan_boss`, 7 lines) opened in the world: the screen
stayed 143 ms into it, then one dip to the world (the judge's `screen`: the cutscene's own screen carried over, not a flash).

### Subtitles for flowed lines (2026-11-11 06:10-06:26, fake headset, Noveria's ICE60_06; item 13)

**The profile shows subtitles.** In Han Olar's conversation (not ambient) his line is drawn on the speaker's subtitle quad
(`2026-11-11_05-55-31-205`, `06-10-44-423`). So the profile's option 0 means subtitles on, and a flowed scene, a conversation
with subtitles before our flags, now played its lines with none. (The empty `BioSubtitles` of the section below was read through
an unfiltered instance listing, whose first object is not the level's; read through
`BIOA_ICE00.TheWorld.PersistentLevel.BioWorldInfo_0`, the object holds what is shown.)

**How the game shows a subtitle.** The conversation GUI's `BioSFHandler_Conversation.Update` (script) shows the world's
`BioSubtitles.m_sSubtitle` whenever `HasSubtitle` holds (0xB67F70: a non-empty text and a render mode other than none). It
passes the movie a placement: 0 for the bottom mode (3), 5 for the load screen, 2 in a conversation or for an alert, else 1.
For an ambient line the game sets the mode to ambient (4) at the conversation's start and never fills the text.

**Built** (`game/ConversationFlow`, rule `cw::ShowBarkSubtitle`, unit-tested; `[Cinematics] worldFlowSubtitles`, default 1):
while a flowed line plays (the line is one we flagged) and the conversation's `m_nSubtitlesMode` (the player's option 5, read at
its start) is 0, its `m_sCurrentSubTitle` (the game's text, the speaker's name first) is handed to
`BioSubtitles.DisplaySubtitle` with a 0.6 s life, again every 0.25 s, so it goes with the line. The first try, in the ambient
mode, drew the text in the middle of the screen, where in VR it stayed in the eye images as head-locked text 6 pixels tall
(`06-22-14-907`). The bottom mode (3) is drawn in the GUI's movie and reaches the HUD panel with the rest of the game's UI, above
the health arc (`06-24-19-375`, `06-25-40-580` .. `06-25-46-585`: both lines, in turn). So the mod sets that mode for the scene
(journalled) and puts the game's back when it ends (read after: 4, the text empty). Log `convo flow: <scene>'s line shown as a
subtitle`. Not seen: the size in the headset (the HUD's own text size), and a profile with subtitles off (option other than 0:
nothing shown, by the rule).

### The subtitle of a line played as a bark (2026-11-11 05:49-05:56, fake headset, Noveria's ICE60_06; item 13)

`conversation-launch.ps1 ice60_trig12_olar_execution` (a chain start the flow table flags), then its start action fired again
with snapshots 2, 4 and 6 s into the barks: **no subtitle on the HUD panel** (the game's whole UI on foot), though the game's
conversation panel was up (`mevr panel`: Conversation visible). Read mid-line (`mevr props`):

| | Olar's execution (flowed: ours) | the science joke (ambient by the game's own data) | Han Olar's conversation (not ambient) |
|---|---|---|---|
| the conversation's `m_bAmbient` | true | true | false |
| its `m_sCurrentSubTitle` | "Han Olar: Will I get a ..." | "Scientist: How can you tell jokes?" | "Han Olar: You came to find out about them, didn't you?" |
| its `m_nSubtitlesMode` | 0 | 0 | 0 |
| the world's `BioSubtitles.m_sSubtitle` | empty | empty | empty |
| on the HUD panel | nothing | nothing | (the conversation's own band, on its quad) |

So **a flowed scene's line is shown exactly as the game shows its own barks**: the text with the speaker's name is built
(ambient lines get the name prefixed, `UBioConversation` 0xC84B70), and in this profile nothing draws it. `m_nSubtitlesMode` is
the player's profile option 5 (`OPTION_TYPE_SUBTITLES`), read at the conversation's start (0xC86B00, through the profile
object's slot 0x458; option 1, the dialogue mode, beside it). The per-tick update (0xC87830) hands the line to the speaker with a
"no subtitle" bit set when that option is 0. 0 is also the game's default (`BIOGame.ini m_nDefaultSubTitles=0`). The world's
`BioSubtitles` isn't where conversation subtitles go (empty in the ordinary conversation too). Open: what the option's 0 means for
ambient lines (the options screen's value), so the headset question is the owner's: with the game's subtitles on, do people
talking around you on foot get subtitles, and do the flowed scenes get the same?

### A moment after a conversation (2026-11-11 05:27-05:42, fake headset; item 11)

Saves 94-96 (`validation-saves-moments2.json`): the tower on Feros on foot, Therum's troop drop, Benezia's phase 3.

| Slot | Moment | Result |
|---|---|---|
| 94 | Feros: the tower on foot | its start action is a plot check (`Should_First_Creeper_scene_fire?`) that this career's plot doesn't pass: nothing played |
| 95 | Therum: the troop drop | Shepard dies as the save loads: the game's GameOver screen (the runner now says so at once instead of waiting four minutes) |
| 96 | Noveria: Benezia, phase 3 | the save's spot is inside phase 2's trigger too: her conversation (`ice60_matriarch_dlg`, a cut, 5 lines) and then her intro, a matinee that walks Shepard 3.4 m in the cinematic mode |

**Benezia's intro went to the 3D screen.** The cinematic mode began in the tick her conversation in the world ended. A moment
needs a fresh place on foot (3 s); the conversation had held the view for 28 s and never counts as one. The fix: **a
conversation shown in the world leaves its held place as the place on foot when it ends** (`End`). For the tick or two before
the track's first move opens the moment, **the host is kept armed** (at most 300 ms after a conversation in the world ends into
the cinematic mode; the same handshake that keeps a scene's start on foot from flashing the 3D screen). The intro then played in
first person: Benezia at the top of the stairs, a squadmate in her stasis in front of the player
(`2026-11-11_05-41-46-695`), and the fight began from the player's place (`05-41-52-737`).

**The guide stepped in place for 12 s.** The view reached Shepard's place 2.4 s into the guided walk, but the track still held
him (`noJoin`: its move is applied every tick until the matinee ends), so the guide went on stepping at full speed round its
target: "walked 17.4 m" for a 3.4 m way, a step's jitter every frame. Now the guide's last step ends on the mark and it waits
there while the track holds him: walked 3.2 m, the two one again as the cinematic mode ended.

**The runner** judges a scene that began as its save loaded from the load (its opening came before the runner noticed it: the
judge had found "no scene in the world"). It stops at the GameOver screen.

Open from this sample: the judge's `pivot` on Benezia (108 degrees, the stage's facing for her at the start of a cut opening:
the summary counts no turn of ours); the Feros start needs its plot; Therum's troop drop needs a save that doesn't kill Shepard.

### The handover between two conversations (2026-11-11 04:20-04:33, fake headset, the launcher's comm room; item 9)

`conversation-launch.ps1 nor10_comm_room -World on`, both conversations driven to their ends, judged by `conversation-session.py`.
The comm room (`nor10_comm_room`, a stay, 9 lines) hands straight over to Anderson's (`nor10_captain`, a cut, 25 lines): the
judge found **Shepard's head up to 4.27 m from the player's view** in Anderson's. The log at the handover: `Shepard kept in place:
the game put him 4.18 m away` (the comm room's keep-in-place), then in the same millisecond `conversation ... began: nor10_captain`
and the player's place taken from Shepard's head bone, still at the new mark: the new stage had placed Shepard and the old
conversation's keep-in-place, run once more from the camera update before the mod's tick saw the new conversation, put his pawn
back; the view went to the mark, his body stayed 4 m off. **Fix:** the keep-in-place pass runs only while the game's live
conversation (`BioWorldInfo.m_oCurrentConversation`) is the one it keeps people for (`KeepsTheLiveConversation`). Replayed: no
undo at the handover, the player's place re-placed 0.17 m, no `body-strayed` in Anderson's conversation. The 3D screen for a
moment of the original note was not seen (the level's intro cutscene and Joker's scene start on it, then the world).

### Which scenes have no place for the player (2026-11-11 03:50, from the sweep runs' summaries; item 10)

Every scene summary the sweep runs wrote since 2026-11-09 (`results.jsonl`, 83 runs): 100 conversations were shown in the world;
5 were named by a no-place rule (`noPlace`):

| Scene | Rule | Right for the 3D screen? |
|---|---|---|
| `sta90_council_saren` (the Council's hearing) | far shots: 17 of 30 from over 8 m, the farthest 17 m | No: played in first person since 2026-11-10 (the walk to the podium) |
| `ice60_matriarch` (Benezia's death) | far shots: 4 of 7, 16 m | No: a walk-in, played in the world from its save |
| `jug20_sal_captainkirrahe` | far shots: 22 of 23, 45 m | Already a set piece (`cw::IsSetPiece`) |
| `jug20_wrex` (Wrex on the beach) | far shots: 12 of 12, 32 m | Probably (an authored walk of his); its save doesn't start the scene yet |
| `sp121_trig01_mine` | presence: Shepard 1007 m from the owner and out of its sight | Yes: there is no place for the player |

The far-shots rule is a hint, not a verdict: directors frame from afar scenes that work from Shepard's eyes. So `worldAutoScreen`
is `on` by default now and means the presence rules (Shepard's pawn hidden, or farther than `worldAbsentM` from the owner and
out of its sight) move a scene to the 3D screen, while the far shots are only logged; `all` lets both act, `log` and `off` as
before. Shepard hidden was named by no scene seen so far.

### The composition of groups, first part (2026-11-11 02:40-03:00, fake headset; item 7)

The opening's arrangement (Part B: people out of the player's view seated on an arc in view) seated everyone facing the
player, square to the camera. Now (`cw::GroupMiddle`, `cw::FaceToward`, `cw::SquadSeats`, unit-tested):

- **The seated face the group's middle** (the mean of the player's place, the owner's and the seats': `worldFaceMiddle = 1`),
  as people standing together face a circle's middle. Seen in the medical bay: Dr Chakwas, brought round from behind the
  player, stands turned in toward the group beside Ashley (`2026-11-11_02-44-19-283`).
- **Squadmates who aren't people of the conversation sit beside the player** (`worldSquadBeside = 1`: alternately either
  side, `worldSquadSideM` 1.0 out, `worldSquadBackM` 0.3 back, a second pair further out, facing the middle). No squad in the
  scenes run: unit-tested only.
- **An opening without a cut walks people into their seats** (`PlanSeatWalks`, with `worldWalk = 1`): the conversation's
  people out of view and within `worldSeatWalkMaxM` (8 m) of a seat get it as a mark, walked to at once with the game's AI,
  ending at the seat's facing; nobody jumps. At C-Sec an officer 14 m off was given a seat and started (the cap came after)
  and, like Wrex, wasn't moved by the AI.

Hot reloads during a scene re-detect it and run its opening again: build between runs, not during them.
`worldWalkLog = 1` adds the gait trace, the walker's properties, its controller's state, its animation tree (all to
`%LOCALAPPDATA%\MEVR\walk-probe`) and a 4 s call trace of the move state's calls. The CallFunction hook goes in when AI walks
are wanted (`WantsScriptHook`). `mevr vslot <n> <hex>` prints the exe address of an object's vtable slot (research).

### The audio list: A1, A2, A3 built; A5 read; A4's plan (2026-11-11, an agent in a worktree; offline only, not run in the game)

Addresses are RVAs in `MassEffect1.exe` 2.0.0.48602, read in Ghidra (the shared project through its MCP server).

**The hook takes more than one list** (`engine/VoicePlace`). Three publishers, each with its own list replaced whole by its
`Set`: the first-person conversation (`game/ConversationWorld`, unchanged: the old `Set(voices, count)` is its list), the
people near the player on foot, and the player's gun (both `game/SoundPlace`). A source whose owner two lists name is placed
by the first. A voice can now say **"centred sources only"**: the hook then leaves alone any source of that actor that the
game placed itself. How the hook tells (read again in `FISACTSoundSource::Update`, 0x7BA90): the wave instance's int at
+0x78 is what the update reads to choose between the wave's place in the world (not 0) and the listener's front (0), and the
update clears it itself where it forces a cue to the front (a sound group of its own, checked against the cue's
`SoundGroup` at the cue's +0xC8; a speaker channel at the wave's +0x40). Read after the game's update, 0 is a source the
game left in the centre: the 82% of dialogue cues. The "relative to the listener" flag the update sends is close to it but
not the same (1 also for a wave given an offset from the listener at its +0x44, sent turned into the listener's frame), so
the hook reads the int itself. `Take` keeps its meaning for the conversation's heartbeat line; a second reader
uses `SinceInstall` (never reset). `TakeHeard` says which actors of a list had a source placed since the last call (who
spoke). The hook now goes in when either the conversation's world mode or `game/SoundPlace` wants it (`EnsureVoicePlaceHook`
in `mod/Mod.cpp`): **with the defaults, for every player on foot**, no longer only with the first-person conversations.

**A1, people near the player** (`game/SoundPlace`, the rule `core/vr/SoundPlace`, unit-tested in `tests/SoundPlaceTests.cpp`;
`[Audio] overheard = 1` default, `overheardWithinM` 8, `overheardKeepM` 10, `overheardNearM` 2, `overheardFalloffDb` 3,
`overheardFloorDb` 6, `overheardGain` 1, `overheardMax` 16). Game mode 0 only, and only while the view is the player's
(not `InWorld`, not a moment's view, not the body away from the pawn); the listener is the player camera's POV. Each tick
every `SFXGame.BioPawn` in `WorldInfo.PawnList` within 12.5 m of the listener (the keep radius plus 2.5 m for the head above
the feet), visible, alive and not the player's own pawn, is a candidate at its head bone (as `game/ConversationWorld`'s
`HeadOf`, else its place plus 70 units), judged by the conversation's own `cw::PlaceVoice` (within 8 m, kept to 10, full
within 2 m, 3 dB quieter a doubling, at most 6). At most 16 are named, **those heard in the last 2 s first** (the hook's
`TakeHeard`), then the nearest: so in a crowd the speakers always have a place. They are named "centred sources only", so
their barks with a falloff of their own, their footsteps and their gunfire keep the game's mix. Who speaks is not looked up in
the conversation objects at all: a centred source owned by a pawn near the player is that pawn's voice (a flowed chain, an
ambient conversation, a `BioSeqAct_FaceOnlyVO`, a squadmate's banter); Shepard's own lines stay in the centre (his pawn is
never named). Logs `sound place:` (`... speaks from where it stands (N m from the player ...)`, once in 10 s a person; the
heartbeat's `N people near named now; this period M source updates placed for them (K of theirs the game placed itself, left
alone)`); snapshot `"mod"."soundPlace"`; `mevr voices [status | overheard on|off | gun on|off]`.

**A2, the player's gun: what was found.**
- `SFXGame.BioWeaponRanged.oAudioComponent` (+0x4B0) is an editable component of the weapon class (CPF_Edit, ExportObject,
  Component, EditInline): every weapon actor gets its own instance, so its `ActorComponent.Owner` is the weapon actor, not
  Shepard's pawn. A sound played on it can be told apart by its owner.
- `BioWeaponRanged.PlayWeaponSound(name)` (native, its exec 0xB72C90, the virtual at the weapon vtable's +0x8E0 = 0xC5ED10)
  does **not** play on the weapon: it hands the name to the local player controller's GUI manager
  (`PlayerController.m_oCachedScaleFormManager`, +0x740, its vtable's +0x348): a UI sound (the overheat warnings,
  `PlayOverheatSound`, `PlayShootingWhileOverheatedSound`), with no place to give.
- The shot itself: the native fire tick (`TickFire`, 0xC621E0) plays nothing; the shot's sound comes from the script events
  `BioWeaponRanged.FireOn`/`FireOff` (and `HasLoopingFire`, `m_nFireSound` at +0x4D0, the item's `BioWeaponSoundsSet`
  `m_oSounds`: labelled sets of cues), whose bytecode was not read. `Actor.PlaySound` (0x505490, the body 0x4FC240) makes a
  component owned by whichever actor it is called on. So whether the gunfire's source belongs to the weapon (its
  `oAudioComponent`, or `PlaySound` on the weapon) or to the pawn (an animation's sound notify, `PlaySound` on the
  instigator) is not settled offline.
- **Built:** the gun's list names the held weapon actor (`weapon::HeldObject`, a verified `BioWeaponRanged` in the right
  hand) at its drawn muzzle (`weapon::CurrentAsDrawn`: where the visible gun is, kick included), every source of it (`[Audio]
  gun = 1` default; on foot only). If the weapon owns the shot, it comes from the gun; if the pawn does, nothing changes.
  **`mevr voices census [seconds]`** settles it in one look: for that long the hook records every distinct owner and audio
  component a source belongs to, with its cue (`AudioComponent.SoundCue`), whether the game placed it, its component's
  `CurrentUseSpatialization`, how many updates, and whose list placed it; the log's `voice census:` lines name them and tag
  `[the player's pawn]` and `[the player's weapon]`. If the shot turns out to be the pawn's, the next step is to tell its cue
  apart among the pawn's sources (by the cue's sound group or name, which the census prints), not built.

**A3, the menu's Sound page** (`assets/menu/main.rml`, a new rail section with a loudspeaker icon; schema
`core/vr/MenuSettings`, mapping `plugin/MainMenuSettings`; offline look `mevr-menu-preview`'s `section-sound*.png`):
"Sound for" (Headphones 3D / Speakers / The game's own: `[Audio] spatial = hrtf | soft | game`), Loudness (`gainDb`, 0 to
+9 dB, a slider; off with The game's own), a line saying what plays now (`openal_soft::Describe`) and a note
(`menu_settings::AudioNote`, unit-tested): "In use now", "Applies when the game next starts" (both host keys are read once at
attach; `openal_soft::Running`/`RunningGainDb` say what attach did), or, with the DLL missing, that MEVR Setup's Repair puts
it back. Two live toggles for the mod's lists: People around you (`overheard`), Your gun (`gun`). The host's own reader of
`spatial` and the menu's agree word for word (`AudioSpatialChoice`).

**A5, the AL source behind an ISACT voice (read, nothing built).**
- The sound source (`FISACTSoundSource`) holds only ISACT handles (an entity at +0x44, a stream at +0x4C), never an AL id.
- ISACT's OpenAL layer is linked into the exe (its own messages name `CAudioOALSource` and `CAudioOALListener`). Its init
  (0x7CC80) opens the device and makes the context current with `alcMakeContextCurrent` (process-wide), sets the inverse
  clamped distance model, and asks `alIsExtensionPresent` for "EAX-RAM" and "EAX4.0" or "EAX4.0Emulated" (then `EAXSet`/`EAXGet`
  through `alGetProcAddress`, kept at 0x16B26B8/0x16B26C0). Its voices are a table of up to 256 `CAudioOALSource` pointers at
  0x16B2800 (zeroed by the init; the count the stream code checks at 0x16B3000), each with **`m_uiSource`, the AL source id,
  at +0xB4** (its last position at +0x40, a mode at +0x7C, the state at +0x84). The voice's `SetPosition` is 0x826B0
  (driver, voice index, position: `alSourcefv(m_uiSource, AL_POSITION, ...)` when it changed); `ProcessStreamSource` 0x7EF40.
- The layer has no filter call: its messages cover state, gain, pitch, mode, position, velocity, orientation, distances,
  cones and buffers. Occlusion and obstruction ("ToggleISACTOcclusionMode - Occlusion Filter / Obstruction Filter", 0x74610)
  go through EAX.
- From an FISACTSoundSource to its voice: the entity's own structures (not read) pick the voice, and one entity may drive
  several voices (layers). The practical route needs none of them: while `engine/VoicePlace` re-sends a source's position
  through the ISACT interface, a hook on 0x826B0 sees the voice index (or indices) that very call reaches; the table gives
  `m_uiSource`.
- EFX is reachable: the host loaded `ASI\audio\soft_oal.dll` itself and every OpenAL call of the game's goes to it; OpenAL
  Soft exports `alGenFilters`, `alFilteri`, `alFilterf`, `alDeleteFilters`, and every device of it has `ALC_EXT_EFX`. The game's
  context is current process-wide, so a call on the game thread reaches it. The colour: one filter, `AL_FILTER_TYPE`
  (0x8001) = `AL_FILTER_BANDPASS` (3), `AL_BANDPASS_GAIN` 1, `GAINLF` and `GAINHF` (2, 3) about 0.3 (-10 dB under 250 Hz and
  over 5 kHz: a telephone's band, roughly), set with `alSourcei(id, AL_DIRECT_FILTER` 0x20005`, filter)`.
- **Risks to measure before building it:** OpenAL Soft's EAX emulation (if ISACT takes its "EAX4.0" path under OpenAL Soft,
  [verify]: log `alIsExtensionPresent`'s answers) drives the same per-source direct filter for occlusion, so ours could be
  overwritten by an EAX commit or override its occlusion; ISACT reuses voices, so a filtered voice must be given
  `AL_FILTER_NULL` back the first update it plays someone else (keep the set of voices filtered this frame, clear the rest);
  with Creative's router (`spatial = game`) nothing of this is reachable the same way. New addresses (0x826B0, the table)
  need rows in `engine/Addresses.hpp` with patterns.

**A4, the plan (not run).** In "To check in the game" below, with A1's and A2's.

**Offline evidence:** all three configurations build (`build`, `build-mono`, `build-trilogy`, /W4 /WX), `ctest` passes
(`sound place tests: ok`, the menu's new checks in `TestMenuSettings`), `check-spdx` passes, two new ReflectCheck rows
(`AudioComponent.SoundCue`, `CurrentUseSpatialization`), and the menu preview shows the page in its three states.

### The coverage pass from saves (2026-11-11 12:45-14:28; the owner: "Run the 47 scene coverage pass now. I can't do the audit until much later")

**The pass** (`conversation-from-save.ps1 -All` over the 47 saves written for the next sample, players' build, fake headset,
saves and inis backed up and restored, `-EndAfter 25`, a snapshot 8 s into each scene): 13 played in the world and were judged,
33 didn't start (21 "nothing came of the action", 8 chunks not loaded, 4 saves that never came up on foot), 5 had no save.
Snapshots of the 13: five showed **Shepard's headless body about 2 m in front of the eyes**, seen from behind (Avina,
Samesh, Septimus, the asari scientist, the surveyor). The mod's own lines told why for three: the view on foot each opening
took was the game's third-person camera, 2 m behind his pawn along his facing (the explore mode's offset 200, 0, 40), not the
eye the first-person camera puts 0.5 m ahead of him in the earlier scenes.

**A Halcyon fault: the first-person table fills.** Halcyon's first person writes its offset into each camera-mode object it
"owns" (`head_aim.cpp`, `FpOwnAndWrite`), in a table of 16. Every save load makes new camera-mode objects; the old entries
die with the level, but were dropped only when the same mode was written again (never) or by the restore-all of the pause
menu (game mode 7). Halcyon's log: a new mode owned at almost every load, the table full at 13:10:30, no first-person write
after it until the next pause menu. A player loading 16 saves without opening the pause menu (quickloads after deaths)
would get the third-person camera on foot from then on. **Fix** (a `[MEVR]` seam in `head_aim.cpp`, `FpPruneDeadOwned`): a
full table first drops its dead entries, by the same deep-liveness check the write path uses (no restore for a dead
entry). **Seen**: a second run loaded 17 saves in one game, then the three Citadel scenes: `[FPCAM] owned table full: 16
dead entries of earlier levels dropped`, all three opened at Shepard's eyes (snapshots: Avina and the keeper ahead, Samesh
facing the player, Septimus's back room), the judge found nothing.

**The loop's gaps, closed** (`conversation-from-save.ps1`; `conversation-sweep.py actions` gives each scene's streaming states
and whether it plays as barks):
- **A landing's squad screen**: a save on foot on an uncharted world loads into PartySelect; the loop calls the screen's own
  FinalizeParty (`mevr vehicle party`) and counts the Mako's mode as loaded. The landing then plays its drop cinematic (mode
  6), which didn't end in two minutes for two of them, and the last left the game with no pawn: such saves need writing
  after the landing (item 17).
- **A chunk shown under a plot state**: when the action isn't loaded, the scene's named streaming states are turned on one
  by one (`mevr stream state <name> on`). The Normandy's Feros huddle (`stream_huddle`) and Ashley's wake-up talk
  (`stream_wakeup2`) streamed and played, Feros's colony (`JumpTo20`) streamed; Virmire's `SS_VIRMIRE_PART2`, the Normandy's
  airlock and locker states and Feros's `PostThorian` bunker did not bring their action in.
- **Barks**: an ambient or flowed scene has no conversation mode to wait for; the loop starts a voice census (`mevr voices
  census 14`) as it fires and counts the census lines whose cue is the conversation's: 7 heard playing (Noveria's interview,
  Virmire's two salarian prisoners, Eden Prime's farmer, the Presidium's employees, the asteroid's prisoner, Feros's
  Calantha Blake). The popup texts and the news videos have no voice to hear; three flowed scenes (Ashley on Virmire, the
  Ilos elevator comment, Major Kyle) and two Feros ambients streamed in by `JumpTo20` were not heard (their start's plot
  conditional, [verify]).

**What the judge found in the scenes that played** (16 conversations): the surveyor (item 15), Hackett's transmission
(item 16), two 63-degree body turns (Ashley on Eden Prime, Tevos on the Normandy's call, both by the re-face rule, the second before
the player has walked in: the owner's re-face question, notes/headset-checklist.md), Shepard moving as his own line began in
two short scenes (`reply-move`, no stick involved: the stage's placement, [verify]). Everything else: nothing found.
**Coverage: 131 of 290 reachable scenes played** in some run (108 before, 16 conversations and 7 bark scenes new).

### Hackett's call and the surveyor, run again (2026-11-11 14:35-15:17; items 15 and 16)

**The flash after Hackett's call (item 16).** Halcyon's log at the first pass's flash: the transmission's conversation (5),
the cinematic (6) 0.3 s, then the galaxy map (9), whose first frame latched the map's camera (`BioCameraBehaviorGalaxy`) as a
cutscene and showed the flat panel for 24 ms (`[VRSTATE] gm 9 alive cutscene -> FLAT panel`, `[FLATWHY] sources: galaxy`),
before the mod's hold took the view. The transmission is the galaxy map's sequence (`GalaxyMap.Comm_Room_Dialogs`): using the
console plays a pending call first and opens the map after it. The mod holds the map's opening cinematic as the map's for
4 s after the console's use (`OnArtPlaceableUsed`), and the call (11-13 s) outlasted that, so whether the first map frame was
flat depended on how fast the cinematic went (1.2 s in a rerun: no flash). Ruled out on the way: a reservation of the table
from its on-foot scene (the travel path's) never applies by default, since the hologram on foot is off (`[Galaxy]
onFootHologram`, the owner's QA of 2026-11-07: the game's own projector on approach); that change was reverted. **Fix**
(`vr::galaxy_map::ConsoleUse`, core, unit-tested in `tests/TableReturnTests.cpp`): the use's 4 s window stays open while a
conversation it began plays and runs from its end (at most 3 minutes from the use). `mevr galaxy use` calls the console's own
use, as A at it, for tests. **Seen**: a save standing Shepard 1.5 m from the console (written from the transmission's save),
the console used twice: Joker's "incoming transmission" line, Hackett's call, the map after 13 s; the hold took the view
22 ms into the cinematic, the map's first frame was the world (`[VRSTATE] gm 9 alive -> VR`), the judge found nothing.

**A cut at 1.2 m.** The same two runs opened the call with a cut first: the opening rule said stay (Shepard's mark 1.28 m from
where the player stood, in sight), but putting Shepard back there was refused (`SetLocation`, the console's geometry), and the
fallback was the stage's placement under a dip, a cut within one space. Now a refused put-back is a walk-in by the view
(`cw::WalkInFacts::putRefused`, `DecideWalkIn`; with `worldWalkInWay = body` still the cut): Shepard stands at his mark,
hidden, and the view walks to him. Seen in both rounds: `opening: walk in (1.2 m to the mark, view)`, no dip. The guided walk
didn't reach him there (people within the clearance of the mark, the hologram's actor among them), so the player stays where
he stood for the 8 s call, which nothing in the owner's rules asks to change.

**The surveyor (item 15).** Run with a replayed recording at rest (head straight, both controllers tracked low at the sides,
no inputs; arm IK `driving`, the torso `first person: the whole body under the view`): snapshots at 6, 12 and 17 s show the
storeroom from Shepard's place, no torso in front. Simon is out of sight behind the crates 8 m ahead, as the scene stages him
hiding. The first pass's view inside the torso was the fake headset without controllers (no body follower), the artefact
Chellik's bump showed (item 7's note); the judge's `body-strayed` 2.2 m is the opening's record of where the stage animation
carries his head, not what the eyes see.

### The third sampled pass (2026-11-11 15:20-16:04)

**The sample**: of the 130 scenes never tried (`%LOCALAPPDATA%\MEVR\sweep\untried-2026-11-11.json`), 1-6 per level and kind
(stage, free, ambient, remote, framed), each from a different chunk where there were several, and all five of the
Normandy's transmissions; the ending's two left out (their script saves the character and steps the achievements). 37 saves
written (two scenes have no placed owner), slots 200-238, kept outside the career until the run, the career's Save folder
put back to its morning state after (checked file by file against the 08:02 backup).

**Played**: 17 conversations in the world (Noveria's Mae Komatsu, the security guards, Lorik Qui'in, Mira; Virmire's choice;
the Normandy's five transmissions, the return from Virmire, Chakwas, Garrus in the hold; the Presidium's hanar, the crime
boss, the banker; Feros's asari commando), 6 bark scenes heard (the voice census). The eyes (a snapshot 8 s into each): the
speaker in front, or the CIC's table for the calls, in 15; in Noveria's guards and Virmire's choice the back of Shepard's
armour in front, the fake headset without controllers (the body follower doesn't run; item 15's finding). Not started: 6
ambient or flowed scenes not heard, 4 chunks their named states didn't stream (the Wards' scenic view, `RP109_CarryTheOne`),
5 behind plot conditions, 2 without a save.

**Findings**:
- **The transmissions' end flash came back, from the harness**: two of the five calls ended with a single flat frame (0.01 s,
  two dips). They were fired directly; in play a call at the galaxy map comes from the console's use, which the fix of item
  16 holds through. The loop now starts `GalaxyMap.Comm_Room_Dialogs` scenes through `mevr galaxy use`, and the two calls
  run again that way (16:02-16:04): Joker's line, the call, the map straight into the world, nothing found.
- **Two cuts from a view at Shepard's hips** (item 18).
- `reply-move` once more (Mira on Noveria), a 146-degree turn of an inert object (the game's, the return from Virmire).

**Coverage: 154 of 290 reachable scenes played** in some run (131 before, 23 new).

### The camera that fell to Shepard's knees (2026-11-11 16:06-17:20; item 18)

**What the eyes showed.** The three saves whose openings had started from a view at Shepard's hips (Noveria's guards, Garrus
in the Normandy's hold, the asteroid's surveyor) were loaded and looked at on foot, before any scene: in all three the eyes
stood behind his knees, looking at his legs from behind; the game's camera 0.62 m above the floor under him (1.79 m in the
other openings of the pass). Not the mod's posture (standing, not crouched, by the game too), not the camera mode (Explore,
Halcyon's first-person offset written), not the small drop as a generated save loads (rewritten at the settled height: the
same 0.62 m). Bound to the spot: at the guards' spot 0.62 m, moved 1.5 m along his facing 1.79 m, moved back 0.62 m again.

**The cause.** The mod's camera probe gave it in numbers: the explore mode's own formula put the camera 0.5 m ahead of its
hook (the Camera bone, at the pawn's centre) at head height, and the game's camera stood at the bone, 0.26 m below the
centre, 1.3 m from where the mode wanted it. Every camera mode carries `SFXCameraMode.bCollisionEnabled`; with it set,
`SFXPlayerCamera.UpdateCameraMode` calls the mode's `DoCameraCollision`, which pulls the camera back toward the hook when
anything lies between them. For the game's third-person camera that keeps walls out of the shot; in Halcyon's first person
the camera point is just ahead of the face, so anything there (a wall, a pillar, someone close in front) drops the view
1.2 m to the hook, and Halcyon draws the eye 0.43 m behind that: behind the knees. A player can meet it walking up to a wall
or with someone right in front.

**Fix** (`game/CameraCollision`, mod side, hot-reloaded; `[Game] cameraCollision`, default 0): while the view is
head-tracked on foot, the player camera's current mode (and an interpolation's target) of the classes Halcyon puts in
first person (Explore, ExploreStorm, Combat, CombatStorm, TightAim; never Vehicle, Sniper or HipAim) has its collision
cleared, journalled (`CameraCollision.bCollisionEnabled`), and put back once the view isn't head-tracked or the mod unloads
(a level's modes die with it; a conversation or a menu keeps what was cleared). Reflection row in `engine/ReflectCheck`.
**Seen**: the three saves again: the camera 1.82, 1.83, 1.83 m above the floor, the guards' view a normal first-person one
down the corridor. The other two spots stand Shepard against a pillar and inside crates (the eyes see the pillar, and black):
the generator's place, a few centimetres from the person's own mark through geometry it can't see; their openings still
cut because his mark is out of sight from there, which is the rule. [verify] in the headset: walking face-first into a wall,
a squadmate crossing close in front: the view stays at the eyes.

### The fourth sampled pass (2026-11-11 17:22-17:48)

29 of the 91 scenes still untried (`%LOCALAPPDATA%\MEVR\sweep\untried-after-third.json`), one per chunk: 12 staged scenes of
the Citadel, 6 of Noveria, 4 of Feros, 2 of the Normandy and one of each other kind; 27 saves (two scenes have no placed
owner), slots 300-328, the players' build with the camera-collision fix (item 18). **Played**: 14 conversations in the
world (Noveria's Maeko twice, Mallen Ecalis, the assault's end, Hanolar, Alestia; the Presidium's turian preacher, the
family's argument, the emporium, Garrus at C-Sec, the snap inspection, Girard; Feros's Hana Murakami and the gunman), 4 bark
scenes heard. **The openings**: 16, all from the first-person eye (0.5 m ahead, 0.9 m up of the pawn; none from a view at
his hips, which three of 50 had before the fix); stays at 0.9-1.3 m, walk-ins at 1.8-4.7 m (one by the view, through a
plot state streamed in), one cut at 15.9 m (Hana Murakami's mark, by the rule). **The eyes** (a snapshot 8 s in): all 14 at
eye height, the speaker in view in most; no torso or legs in front. **The judge**: a 133-degree re-face by the rule (the
snap inspection, the player walking in from behind: the owner's re-face question) and a 0.92 m `body-strayed` at a stay
(the fake headset's missing body follower). Not started: 2 barks not heard, 4 starts behind plot checks, 1 chunk no state
streamed (the Wards' market), another scene began as one save loaded, 2 without a save.
**Coverage: 172 of 290 reachable scenes played** in some run (154 before, 18 new).

### A4: combat for two ears, measured (2026-11-11 17:50-18:05)

The arena's row of 15 fighting soldiers 20 m ahead (`tools/power-arena.ps1 -Row`, UNC20, the players' build), a census of
the sources, and 22 s of what the PC plays (`mevr-audio-loopback`) under the head replayed turning a full circle
(`look-circle.mevrrec`); once with `[Audio] spatial = hrtf` (the default: `audio: the game's OpenAL (93 functions of 93) now
goes to ...soft_oal.dll`), once with `spatial = game` after a restart (`audio: the game's own OpenAL`). Per second (a scratch
script over `tools/audio-balance.py`'s own `ear_delay`: the shift of the right channel against the left at which the two
are most alike below 1.5 kHz):

- **OpenAL Soft (HRTF)**: the delay between the ears non-zero in 10 of 21 seconds, from -0.83 to +0.33 ms, changing side as
  the head turns, the channels more alike at that shift than at none (0.51-0.87 against 0.29-0.80): an interaural delay, as
  a head gives. The balance swings gently, -2.5 to +1.6 dB (the far ear hears the sound too, later and duller).
- **The game's own**: 0.00-0.04 ms in every second where both ears hear the same sound (alike 0.47-0.91 at zero): plain
  panning. Its few loud one-sided seconds swing the balance to +16, +15 and -20 dB, the other channel nearly silent there
  (alike 0.09: their "delay" means nothing).

So OpenAL Soft turns the game's placed sounds (the census: the fight's fire, impacts and footsteps are placed by the game)
into sounds for two ears, and the game's own audio only pans them. The saves were put back from the backup (identical to
the morning's, file for file), `[Audio] spatial` back to the default.

### Uncharted worlds from a save (2026-11-11 18:00-18:12; item 17)

Saves written on foot on an uncharted world loaded into the landing (the squad screen, then the rover's drop, which
didn't end), while saves inside Bring Down the Sky's facility loaded normally. Read in the asteroid's scripts (the scan's
`--sequence`, kept out of the repository): the shared `REF_UNCWorldLoad` (in `BIOA_UNC52_00_DSG`) waits for the exterior
layer's `UNC_ExteriorLayerLoaded`, then checks plot bool 5470 "Has the Rover Landed?": true, the player gets input and the
world runs (effects, hazards); false, it stops the loading movie, blacks the screen and shows the squad selection, and the
rover's cutscene follows (transition 3862 "Rover Has Landed" sets it then; leaving the world clears it again, transition
3863). The interiors stream another way, which is why the facility's saves were spared. The generated saves come from a
Citadel save, where 5470 is clear. **Fix** (harness): `conversation-sweep.py saves` sets 5470 on any `BIOA_UNC*` level
(`level_plot`, a test in `tests/ConversationSweepTests.py`). **Run**: Bring Down the Sky's radio and diary saves loaded on
foot, no squad screen, and played as the game plays them (their lines heard by the voice census); UNC17's `sp114_triggers`
save loads into the game-over screen: Shepard dies where the save stands him, beside a hostile squad (the generator stands
the player in front of the scene's originator; here that is a combat squad), the place's fault, not the landing's.

### The hearing that went third person (2026-11-11 22:34-22:36; item 19)

The owner's first run of slot 80 in the headset (Virtual Desktop, the players' build of 528b84be with today's fixes). Anderson's
walk held in first person as designed, and the owner walked with the stick during it (6.46 m in 7.6 s; the fake-headset runs
of this slot had nobody at the stick). At 22:34:56.958 the hearing began and opened as a walk-in by the view (Shepard's mark
17.85 m away, a moment's walk still on its way: `cw::DecideWalkIn`'s rule), Shepard hidden on his mark. Its log then shows:

- the opening read Shepard's head bone 3.9 m from his pawn and 0.09 m above its centre (a hidden mesh is not posed: the bone
  holds whatever pose it had);
- in the same tick (0.0 s) the head-bone follower saw it "jump" 21.51 m, past the 6 m a scene without a cut still follows (a
  scripted entrance), moved the player's place there, and cleared the wanted mark, so the view-walk joined Shepard at once
  ("was put on Shepard, 0.06 m walked");
- 0.1 s later a second "jump" of 17.04 m moved the place to (-8674, -7030, 62369), 4.1 m from his pawn and 2.2 m above it.

From there the held view stayed that far from Shepard's head bone, so arm IK and the body follower took it as third person
("torso: head off the view (third person: the hips turn)" every 2 s to the end of the log) and the owner saw Shepard's body in
front of and below the view for the whole hearing: "I am not even in first person". Halcyon showed the world in VR throughout
(`[VRSTATE] gm 5 alive -> VR`): it was the mod's held place, not the presentation.

**Fix** (`cw::HeadFollowFor`, unit-tested; `game/ConversationWorld`): Shepard's head bone may move the player's place only
when his mesh is posed and his own: never while the view walks to him with his pawn hidden (the anchor takes the bone's place,
one log line `convo: ... Shepard's head bone reads N m from where it was while his pawn is hidden ...: not followed`), and for
1.5 s after the view reaches him and he is shown again (the settle every put-back already had). After an opening without a cut
the 6 m rule for a scripted entrance is unchanged. Not yet run in the game: the owner was playing.

### Kirrahe's camp (2026-11-11 22:43-22:46; items 20-22)

The owner's run of slot 88 in the headset (Virmire, Kirrahe's camp; the scene is a set piece, so on the 3D screen). From the
mod's log and the owner's two snapshots (22:43:25 on the screen, 22:44:41 on foot after it), and the level's script read
offline (`conversation-scan --sequence BIOA_JUG20_08_DSG:jug20_sal_captainkirrahe_dlg`; game data, not kept):

- **First use, 22:43:12 (33.5 s):** the save stood the player 4 m in front of Kirrahe at his idle spot, 14 m from the stage.
  The script took the route for a player who has talked to him and settled Wrex (its plot checks 3953, 3015, both set by the
  generator) and started the conversation, whose starting entries chose the arrival content (entry 0 wants plot 3039,
  "Mission started", clear in the save). That content is staged for the arrival: a move track walked Shepard 6.3 m, the
  party was teleported 16.5 m onto the stage, and Kirrahe stood 11-17 m from where his shots look until a node's script
  (`kirraheTP`) teleported him at 31.1 s. His lines were cutaways onto empty ground (the snapshot: grass and a leg).
  The mod changed nothing in it (`kept 0`, `sourceKept 0`, `trackHeld 0`: a set piece is the game's own).
- **Second use, 22:44:00 (24.1 s):** the planning conversation (Kaidan sent with Kirrahe). Its last node's own script turned the
  black screen on (`jug20_sal_captainkirrahe_N ... BioSeqAct_BlackScreen_8`, fade to black) for the speech, which the level
  script starts only through plot 3039 and the squadmate sent: with 3039 clear, nothing followed. The black screen stayed up
  until the game was closed at 22:46; on foot, the game's UI goes to the HUD panel, so it showed as a black square ahead with
  the panel's crop holes in it, and the use prompt's quad as a black bar (`-hud.png` black, `-label.png`).

So items 20-22: the scene itself is a candidate for the world (the owner's call, made); the black screen's look on foot is
open; the stuck speech and the missing Kirrahe were the save's plot, fixed in the save.

### The owner's session, judged (2026-11-11 22:33-22:59; items 23-27)

The rest of the owner's first headset session after the fixes of the day (Virtual Desktop; the hearing, Anderson after it,
Anoleis, Kirrahe on the screen and, after a restart with `worldSetPieces = world`, Kirrahe's arrival in first person):

- **The wheel's band** (item 23): the snapshot at 22:38:51 shows the wheel quad ending at "The Council won't help" and "us."
  floating to the right of it, on the HUD panel. The band was 330..950 of the 1280 stage; the reply began at 738.
- **Frame timing in conversations** (item 24, Halcyon's `[PRESENTHZ]`/`[GPU]` lines, two-second windows): fps 82.5-90 in the
  hearing, 89-90 on foot on Virmire; windows with a GPU frame over 25 ms: hearing 34 of 85, Anderson 19 of 25, Anoleis 16 of
  25, Kirrahe on the screen 8 of 15, Virmire on foot 6 of 40 (the Citadel on foot, with its loads, 24 of 40). The hearing's
  frames over 40 ms (41-90 ms) at 22:35:01, :21, :23, :27, :41, :45, 22:36:01, 22:37:01, :15 against its cuts at 22:35:00,
  :20, :23, :27, :38, ... 22:37:01, :13: most within two seconds of a cut. One Draw stayed healthy (one scene render a frame).
- **The third-person Anderson conversation after the hearing** (22:38, two snapshots): the view still 3-4 m behind and above
  Shepard: the hearing's bad place handed on to the next conversation; item 19's fix removes it at the source.
- **Kirrahe's arrival in first person** (22:57, `worldSetPieces = world`, the set pieces back on the screen at 22:59 for the
  rest of the session): item 27.

### The conversational circle (2026-11-11 23:05-; item 28)

**What the owner asked for**, after the first full headset session (Kirrahe's planning talk in first person: the player walked
by the game for 7.8 m, frozen while the wheel was up, people seated and teleported around): "I don't understand why we can't just
maintain our current position where we engage the conversation unless the conversation actually requires physical movement. And
if it does, then I should be able to completely move around. I should never be forced to be stationary." Then: "I'm fine with
restricting movement while the dialogue wheel is up, I'd even consider making the movement slower in speed to persuade the user
to not move. ... I don't want static rules. Each scene is different. I understand the squad being stationary if they aren't
talking but if they ever engage then they should slowly walk up to form a conversational circle. This needs to feel organic ...
I know for some scenes the composition requires some NPC's to be in certain locations (speech in virmire by the captain)." And,
approving the design: "if we have to do the black fade in/out I recognize that it's acceptable for now", and "if the game
specifically programs in animations for things like head movements (nods for the council) or arm gestures, those must be retained."

**Why the earlier design wasn't this.** It was built around a scene's first frame: at an opening without a cut, everyone of the
conversation out of view was given a seat on an arc in front of the player and walked or (before the AI walk worked, this
morning) placed there at once (item 7), and a walk-in left the player "wanted" at Shepard's mark and walked there by the game after
5 s (the guided walk). Placing people was the only tool until the game's AI could be made to walk people in a conversation;
once it could, it was bolted onto the opening-time seats instead of a design around who speaks. Both were made defaults on log
measures without the owner's review.

**Built** (`[Cinematics]` in `ASI\LE1MEVR.ini`):
- `worldCircle = 1` (default): `cw::MakeCircle` forms a circle round the player's place, toward the first person engaged, with as
  many places as the conversation has people in the scene (Shepard's the player's), `worldCircleGapM` 1.1 m of rim each (1.5 m
  across at the least). When a person first speaks or is spoken to (the game's `m_pSpeaker`/`m_pListener`; a line with no
  listener is spoken to the player), `cw::JoinCircle` gives it the free place nearest its own bearing (nobody crosses the
  circle); the one spoken to stays where it stands within `worldCircleNearM` 2.5 m of the player, anyone a step (0.6 m) from
  its place joins where it stands, anyone farther than `worldCircleMaxWalkM` 12 m stays out. The walk is the game's own AI walk
  (`WalkToMark`, method 1: `PushMoveToPoint`, the walk animation, the conversation's gesture mode back on arrival, facing the
  circle's middle), walkers set off `worldCircleStaggerS` 0.6 s apart. The stage's own marks are not walked to in a circle scene
  (its marks are the camera's composition). `cw::ShouldRegather`: the player `worldCircleRegatherM` 2.5 m from where it formed and
  still for `worldCircleStillS` 2 s: the circle forms again round the player, nearest people first. Logs `convo: circle:`;
  the summary's `circleJoined`, `circleWalked`, `circleOut`, `circleRegathers`.
- Staged scenes (`cw::IsStagedScene`: Kirrahe's camp; `worldStaged` adds keys): no circle; the opening accepts the stage's
  placements and puts the player at Shepard's mark under a dip; mid-scene marks are walked to as before. Log `convo: a staged scene`.
- The player: `worldWalkInS = -1` (never guided; the owner's setting since 22:59 made the default), `worldMoveSpeedMS` 0.9 (1.4),
  `worldMoveLeashM` 6 (3); the wheel still takes the stick while it is up (the owner: acceptable).
- The game's animation kept: `worldLookReach = 0` (0.4: the pair's stronger look-at chain pulled heads off their authored nods);
  people leave the conversation's gesture mode only while they walk (as before); `worldArrange = 0` (the opening-time seats off).
- Tests: `tools/conversation-from-save.ps1 -WanderAt N` sidesteps the player 3 s with the stick N s into a conversation.

**First run on the fake headset (23:26-23:31, four saves)**: Anoleis (behind his desk) walked 4.3 m round it to the player;
Chorban joined where he stood; Avina, a hologram on a kiosk, was walked 2 m off her pedestal and bobbed up and down, 86
placements in drawn frames (the AI's move against the game's own placement of her); at Fist's (Wrex and the bouncer arguing, the
player watching) nothing moved. Then the owner asked to align before more building.

**The owner's answers (2026-11-11, late)**, to eight questions: (1) the one the player talks to "stays put unless something in the
scene requires dynamic movement"; (2) in a scene the player only watches, "the squad should have a relaxed formation"; (3) "squad
should be gathered", made to look natural; (4) "the conversation should continue as they get close enough to be in range. We
shouldn't accept long pauses"; (5) "we should need a max boundary to make sure we can't just wander off"; (6) distance: open;
(7) "Staged scenes are like the council or the virmire speech", the list to be found in the records; (8) "Right stick should
function"; and: someone "necessary via walk up" should "start walking much before their line, ideally a few seconds gap so it
feels natural". Defaults taken for what was left open: a 4 m boundary, talking range 3.5 m, the squad 1.7 m to the side and 0.5 m
back.

**Built (second design, 2026-11-12)**: the owner stays (`worldCircleOwnerStays`), the circle round the player toward the owner,
formed only once the player speaks or is spoken to now or within the lines read ahead (`cw::PlayerSoon`: people talking among
themselves move nobody); anyone within 3.5 m of the player joins where it stands (`worldCircleInRangeM`); whoever speaks or is
spoken to within the next 4 lines (`worldCircleLookahead`, any branch: `cw::UpcomingPeople` over the conversation's
`m_EntryList`/`ReplyListNew`/`m_ReplyList.EntryList`) sets off now, so it is near by its line; nobody who isn't on the ground
or is seated is walked (`CanWalk`: `Physics` PHYS_Walking, the head bone 0.62 m above its place; Avina); the squad gathers at the
start in a relaxed formation (`cw::SquadFormation`: one each side 1.7 m out and 0.5 m back, varied a little, setting off 0.9 s
apart, facing the owner; `worldSquadGather`, its squadmates tracked for it; they keep it: the stage's marks for them are not
walked); everyone else still walks to a stage mark that is a real move (item 1); no re-gathering (`worldCircleRegather = 0`): the
boundary `worldMoveLeashM` 4 m is around where the conversation began for the player; the right stick turns the held view as
`[Comfort] turn` says (snap or smooth; [verify]: no controllers on the fake headset). Logs `convo: circle:`, `convo: squad:`,
`the right stick turns you`; the summary's `circleEarly`, `circleStill`, `squadWalked`, `turnedDeg`.

**Validated on the fake headset (2026-11-11 23:56 to 2026-11-12 00:15)**, eight scenes at full pace with the stick: forward 3 s
at each start and during Anderson's walk, sideways 3 s ten seconds into each conversation (`-Walk 3 -WanderAt 10`). Against the
owner's rules:
- The one the player talks to stays put: Anoleis (5.4 m away, behind his desk) and Avina (3.5 m, a hologram) stayed; Chorban
  (2.2 m) too. No drawn jumps anywhere (Avina's 86 gone).
- Nothing walks the player: `guidedM` 0 in every scene; the player's own stick walks of 0.8-5.4 m all went through.
- People talking among themselves move nobody: at Fist's (Wrex and the bouncer) the circle formed only when a line reached the
  player, and nobody walked.
- Staged scenes: the hearing and Kirrahe's camp opened with the stage's placements and a dip, no circle. At the hearing the
  re-face rule still turned Udina and Anderson 115-124 degrees four times: staged scenes now keep the scene's facings (no body
  turns for the player there).
- The squad's formation, from the owner's own Citadel save (John00_96: Garrus and Tali; Khalisah's interview): both set off 0.6 s
  and 1.7 s in, but the AI walk took Garrus 2.8 m of 6.9 and stopped, and Tali walked 1.6 m the other way and gave up: Khalisah's
  opening walked the player's view only (his mark inside a trigger), so Shepard's pawn stood at his mark 5.7 m from the player and
  the henchmen's follow pulled toward it. **`worldSquadGather` is off by default** until it arrives reliably: next, the squad's own
  HOLD formation at each place (game/SquadOrders' `GiveAt` way, without its acknowledgements), released at the end.
- Not exercised: early walk-ins (nobody out of range in these scenes), the boundary's edge (the walks stayed within 4 m), the
  right stick (no controllers on the fake headset). The embassy scene (Udina and Anderson, ten people) wasn't loaded from that
  save.

### The squad's formation, held (2026-11-12 09:20-09:43, fake headset; item 28)

The first try (00:12) stalled: Garrus walked 2.8 m of 6.9 and stopped, Tali 1.6 m the wrong way. Read again with the log, both
had been moved by **the conversation's start itself**: between the start action and the first tick of the scene the game put
them onto the stage's marks next to Shepard's mark (Garrus 6.2 m, Tali 7.6 m from where they stood beside the player; the
conversation's own saved places, `m_aFSPrevPositions`, were empty). Khalisah's opening is a view walk-in, so the player stays
5.7 m from that mark and the formation round him sent them all the way back, while their own follow logic (BioAI_Henchman, its
AI switched on for the walk) pulled toward Shepard's pawn, hidden at his mark.

Built:
- **Put back**: the on-foot record keeps the squad's places every camera update (`Foot::squad`); when a conversation in the world
  that isn't staged begins, a squadmate the start moved more than a metre is put back where it stood (`PutBackSquad`, before
  anything is tracked or drawn), if it stood with the player: within 10 m of Shepard, within 2 m of his height, on a floor.
- **Held**: the squad's own HOLD formation for each walker, written into `BioBaseSquad.Members[i]` (`vHoldPosition`, then
  `eFormation` = 2), as `SetMemberHoldPosition` and `SetSquadFollowFormation(HOLD)` leave them but without the calls: the formation
  call runs `BioSquadPlayer.OnSetSquadFollowFormation`, whose chatter makes the squadmate answer the order out loud (research/54's
  trace). Their follow logic then walks to the formation spot with our `PushMoveToPoint`. What the member data had is kept and
  written back when the conversation ends (`ReleaseSquad`; also when the mod unloads). Journalled; ReflectCheck rows for both fields.
- **Under a cut** (the opening dips to Shepard's mark): the squad is put at its spots at once, facing the owner, nobody seen
  walking.
- **Each on its own side** (`cw::AssignSpots`, unit-tested): the assignment with the least walking; by index the two had swapped
  sides, crossing behind the player (seen from above, below).

Runs (the owner's John00_96: Garrus and Tali; Pace full; the stick untouched):
- Khalisah's interview (09:25, before the side rule): put back 6.2 and 7.6 m; walked 2.1 and 2.4 m with the AI and arrived 0.25
  and 0.24 m from their spots 2.5-3 s in; released when the scene ended. Their sides swapped.
- The embassy (Udina's "Krogan on Virmire!") and Anderson's office (09:37): both openings cuts (6.5 and 5.8 m to the mark); the
  squad put at its spots under the dip, 1.2-2.8 m from where the start had them. In this run the squad's place on foot was 125 m
  under the Presidium's floor (the runner's touch of the embassy's streaming volume had dropped them), and the put-back used it:
  the 10 m / 2 m / floor guard since.
- Khalisah's interview again (09:42, the side rule, the formation on by default with nothing in the file): Garrus walked 1.7 m to
  his own side and arrived 0.24 m from the spot; Tali stood 0.9 m from hers and stayed.
- Not judged: **in this save Khalisah, Udina and Anderson are hidden by the plot** (`bHidden`: the save is before those beats), so
  each scene played to nobody; the squad's places are right, the composition with them present needs saves at those beats. The
  session judge's "pivot" counts a squadmate's turn under the dip (170 degrees), which nobody sees.

### Stances from above (2026-11-12 09:10-09:45; item 29)

The owner: "capture the conversational stances from a top/bottom view so that we can more easily judge the conversations. If the
conversation doesn't require any mid scene movement then those can be skipped early after capturing the screenshot."

A real top-down render was weighed and set aside: a camera above the room sees the ceiling (a near plane past it would cut it,
but Umbra culls from the camera, and the roof occludes the room), and an overhead held view on the fake headset covers a couple
of metres. So the mod writes the scene as data and a tool draws it:
- **The record** (`mevr convo stance [tag]`; with `mevr convo stances on` or `[Cinematics] worldStances = 1` by itself 2 s after
  the opening, after each walk ends (at most one a second) and at the conversation's end): the conversation, the line, the
  player's held place and facing, the boundary, everyone tracked (name from the subtitles' "Name: ..." or the squad's, place,
  facing, head, place at the start, speaker or spoken to, owner, squad, hidden, whether it can walk, its walk, its held place,
  whom it looks at), the circle, and two ray scans from the group's middle (knee 0.45 m and chest 1.3 m above its floor, every 2
  degrees, 12 m; level geometry only): about 360 traces in one tick, only when asked. `%LOCALAPPDATA%\MEVR\stances\<stamp>-<number>-
  <conversation>-<tag>.json`.
- **What still has to move** (`mevr convo ahead`, also in each record): people walking now or about to; the squad's formation not
  yet asked for; people of the dialogue reachable from the current line (any branch, `cw::UpcomingPeople` without a depth limit)
  who stand beyond the talking range, can walk, haven't joined and aren't the owner.
- **The drawing** (`tools/conversation-stance.py [--since <time>] [files]`, Pillow): a sheet per conversation, a panel per moment
  turned so the player looks up the page; 40 px a metre, or less to fit everyone within 8 m (further ones pinned to the edge with
  their distance); walls dark, low things tan; the walking boundary dashed; facings, speaker and listener rings, gazes (a look at
  Shepard drawn to the player), walks with their ends, held places, each one's start.
- **The runner** (`tools/conversation-from-save.ps1 -Stances`): turns the stances on, and 4 s into a conversation, once nobody
  walks (or at 40 s), asks `mevr convo ahead`; if nothing moves from there and the scan has no stage walk in the conversation (its
  `actorMove`; `%LOCALAPPDATA%\MEVR\conversation-scan.csv`, made from `scan-all.jsonl` when older), it writes a `settled` stance and
  ends the scene; at the end it draws the sheets. `-Chunk <name>` streams a chunk in by touching the volume whose plain state shows
  it, when the save stands outside it.

In the runs: Anoleis, Chorban and Avina (John02_83, 85, 89) and Khalisah, Udina and Anderson ended 8-13 s in, each with its opening
and settled stances; Khalisah's first sheet showed the squad crossing behind the player (the side rule), Chorban's the stage's
mark Shepard's body was put back from (now left out of the drawing), Anoleis's a guard 12 m behind the player, which had shrunk
the drawing (the 8 m fit).

### Heads that look past the player (2026-11-12 17:20-17:40, fake headset; item 30)

The owner asked why the drawings show both "you" and "Shepard", and whether heads point the wrong way. They are one place in
most scenes; they part in a walk-in by the view (the opening's `view` way: Shepard's mark inside a trigger, or a moment still
carrying him, where putting his body back where the player stood is refused or ended a scene; research/69 "The view walks"):
his body stays on the mark and only the player's view stays where the conversation began, and with the owner's rule (nothing
walks the player) they never join. A head's look-at chain (`BioPawnLookAtController`: `m_pTargetActor`, `m_pTargetMesh`,
`m_nTargetBoneIndex`, aiming its `SkelControlLookAt.TargetLocation` at the target's head bone) then aims every look at
"Shepard" at the mark: the game's own (the conversation's, rank 99999, for most speakers) and ours (the pair's and a bystander's
glance give `BioPawn.SetLookAtTarget(Shepard's pawn)`). The stances now carry each face's direction (the head bone's first axis),
how far it is off the player's eyes and off Shepard's head, the game's target and rank and where its last link aims.

Measured (eleven Citadel scenes from the owner's John00_96 with Garrus and Tali; `%LOCALAPPDATA%\MEVR\squad-saves`, written by
`conversation-sweep.py saves --base`; eight started, three stopped at plot checks): where Shepard's body is with the player (0.5 m
from the view, the eye ahead of the head bone; seven scenes) whoever talks to the player looks at him, 3-19 degrees off his eyes
(Zabaleta, Chorban, Avina, Dr. Michel, the preacher's C-Sec officer 13-35). At Girard's (a walk-in by the view, the body 3.4 m from
the view) Girard's face pointed 7-16 degrees off the empty mark and 58-78 off the player, and Garrus's too, 5-7 off it.

Built (`StepEyes`, `worldLookEyes = 1`): while Shepard's head bone is over a metre (flat) from the player's held eyes, a
`SFXGame.BioLookAtTarget` (the game's own look-at target actor; spawned once on the player controller, its class's
`bStatic`/`bNoDelete` defaults cleared for the call as game/ScopeCapture does; destroyed when the mod unloads) is kept at the
player's eyes each tick; our looks at the player go to it, and each person whose game target is Shepard's pawn gets it through
`SetLookAtTarget` at the rank it had (99999 replaced 99999: not refused once); when his body is with the player again or the
conversation ends, every look at it goes back to Shepard at its rank. `GameLookTarget` reads it as the player, so the look rules
and the re-face rule are unchanged. Logs `convo: eyes:`; the summary's `eyesSwapped`, `eyesRefused`.

In the game (17:36): Girard's look moved to the eyes 0.5 s in (6 moves, 0 refused); his face 12-27 degrees off the player and
50-63 off the mark (one later line's own gesture turned it 54 degrees away); Zabaleta and Chorban unchanged (no move: his body
with the player). `tools/conversation-stance.py --heads` lists whom each face points at, by the game's target where the face points
at its aim (Garrus at Girard, who stands by the mark, was first read as looking at the mark).

### Virmire's start from above (2026-11-12 17:45-18:20, fake headset; item 31)

The scene: Kirrahe's camp on Virmire (`jug20_sal_captainkirrahe`, staged), from slot 88's place with plot 3039 clear (a copy,
`%LOCALAPPDATA%\MEVR\squad-saves\John02_70.pcsav`): one conversation of 214 s and 41 lines, the planning by the beach (Kaidan sent
with Kirrahe's men), then at 135 s the script's black screen (`jug20_sal_captainkirrahe_N ... BioSeqAct_BlackScreen_8`, on, and
`_0`, off, in the same tick) and under it the stage's moves for the speech (Kirrahe 3.7 m, Shepard 0.7 m, Kaidan 9.8 m, Ashley
10.1 m), then Kirrahe's speech to his troops, his own walk out to them a matinee's track.

**The stances follow a whole scene now**: `begin` (as it starts: the stage's first places, the player where he stood), after each
dip (`after-dip`), after each walk, and whenever the scene moved since the last one (someone of it a metre away or turned 40
degrees, at most every 1.5 s, the one who moved most in the tag); everyone else standing within 20 m is in the record
(`others`: the troops, small dots); a sheet over 9 moments has 4 columns, over 40 an even spread with the start, dips and end.

First run (17:49): the staged rule walked the speech's marks, so after the black screen Kaidan and Ashley walked 10 m across the
camp for 18 s during the speech, and the player's view kept the planning's facing: Kirrahe 6.4 m behind, the troops 7.5 m behind.

Built:
- **The script's black screen in a staged scene is the game's own cut to a new composition** (`ScriptBlackScreen`, from
  game/AuthoredMoment's handler, actions 0 and 2): for the next second each stage placement of the others is let through and, on
  the next tick, taken where the game's call put it or put on its mark by us (`relocateMark`: the game's call moved nobody,
  18:06); an actor a matinee's track moves is left to it.
- **Shepard is never moved** (the owner: "instead of forcing Shepard to move ... a simulated analog stick move"): his mark in that
  window is recorded (`sceneWants`) and walked to by a **simulated left stick** (`StartSteer`, in `MovePlayer`: the direction to
  the place in the head's frame, through the player's own walk step: the conversation's pace, the boundary, floors, walls,
  people, triggers), 0.4 s after the black; the player's own stick (or `mevr convo walk`) takes over at once; it ends on the
  place or after two seconds without coming nearer; it never turns the player. `mevr convo goto [<x> <y>]` steers in tests.
  The record's `sceneWants` and `steering`; the sheets draw the place as a blue cross.
- (A forced move of the view onto his mark and facing under a dip was built first and taken out the same hour, at the owner's word.)

Run (18:11): under the black screen Kaidan and Ashley put on their marks (10.1 and 9.9 m), the simulated stick walked the player
0.51 m to Shepard's mark in 0.6 s (0.15 m from it); the speech: Kaidan 0.9 m front-right and Ashley 1.3 m left of the player,
looking at Kirrahe, Kirrahe walking out to 7 m in front of his troops (8 m) - behind the facing the player had in the planning
(the scene turns Shepard 155 degrees there; the player turns himself). Not seen: the arrival content (Kirrahe meeting the party
on the beach, plot 3039's other branch) did not play from this save.

### Heads at the drawn eyes: the audit (2026-11-12 19:00-, fake headset; item 32)

The owner: with the overhead records, "run an audit over the different categories of conversations to see if we have this as a
type of regression where the other scenes would have these types of strange interactions where we weren't facing the actual
player Shepard, we were still facing the old avatar Shepard that is supposed to be hidden".

**What "the player" was.** Every record, the eyes stand-in and the look and re-face rules took the player at the held place,
`gC.hold`. That is the game's camera (the culling camera); the host draws the eyes at it moved by its render-side view offset
(the camera offset, the "view back" framing, the lean; research/69 "The body that stayed culled"). Measured in every record of
this audit: **the drawn eyes are 43 units behind and 18-19 below the held place**, along the held view's axes. The records now
carry the drawn eyes (`player.eyes`, the tick's own frame, the one arm IK and body follow put the body under:
`engine::FromCameraView`'s `headWorld`), how far Shepard's head bone is from them (`bodyFromEyesM`), whether the view walks
apart from the body (`detached`) and the turn so far; each face's angle is taken from the drawn eyes (`toYouDeg`; the held
place's `toHeldDeg` kept) and from his head bone always (`toShepardDeg`). `conversation-stance.py --audit` tabulates per scene
the looks meant for the player (the game's look-at target Shepard or the eyes stand-in): the face at the player, at his body
(his head bone 0.3 m or more from the eyes and the face nearer it), elsewhere. `conversation-from-save.ps1 -Lines N` writes a
stance at each of a conversation's first N lines; `-TurnAt N` turns the player 90, 90 and -180 degrees as the right stick would
(`mevr convo turn <deg>`), a stance after each.

**Run 1 (19:02-19:20, the build as it was, the new measure):** one scene of each kind from the validation saves.

| Scene (kind) | Shepard's head from the drawn eyes | Faces meant for the player |
|---|---|---|
| Opold, Noveria (a plain stage; stay) | 0.07-0.08 m | his face axis 90 degrees off throughout: a hanar's head bone axis is no face (the game's aim points at the player) |
| Benezia's death, Noveria (a trigger's scene; walk-in) | 0.07-0.17 m | her own dying animation turns her head; not at the body |
| Wrex at Fist's (framed, in the world) | 0.09-0.35 m | none meant for the player |
| Avina (the baseline; walk-in) | 0.06-0.07 m | 7 of 7 at the player, median 6 degrees off the eyes |
| Chorban (a wheel conversation) | 0.07-0.09 m | 3 of 3 at the player |
| Dr. Michel, Zabaleta (the squad, Garrus and Tali) | 0.06-0.09 m | 16 of 16 at the player, median 8-9 degrees |
| **Anoleis, turned by the right stick** | 0.07 m, **0.68 m after 90 degrees, 0.92 m after 180** | 3 of 9 at the body (far: 6-7 degrees either way) |
| **Girard (a walk-in by the view: the body on its mark)** | 3.7-4.4 m (the stand-in on) | 13 of 14 at the stand-in, **median 27 degrees off the eyes** |
| **The council's hearing (a cut, staged)** | **0.30-0.97 m; 0.37 m in front of the eyes all hearing** | 13 of 13 nearer the body (councillors 10 m away: 13 degrees) |
| **Becoming a Spectre (a cut, staged)** | **0.33-0.44 m** | 3 of 4 nearer the body |
| **Virmire, Kirrahe's camp (a cut, staged)** | **0.34-0.35 m from the opening on** | Kirrahe 3-13 degrees off the body, 8-18 off the eyes; Ashley 6-10 and 7-11 |
| Wrex on the beach, Virmire | - | did not start (a plot check) |

So in scenes where the player stays or walks in with the body (put back where he stood), **a look at Shepard is a look at the
player**: his head bone is 6-9 cm from the drawn eyes (the eyes ahead of it), body follow keeps it there. Three faults put the
eyes away from his head:

1. **An opening that cuts to Shepard's mark** (every staged scene: the Council, Kirrahe's camp; the opening's `Cut`) placed the
   held place at his head bone plus the eyes' offset from it (`gFoot.eyeFromHead`, arm IK's measure of the drawn eyes) and
   corrected its height for the host's offset but not its 0.43 m back: **the player's eyes stood 0.34-0.37 m behind his head**,
   inside his back, and every look at him passed 8-18 degrees beside the player at Kirrahe's distance. This is the owner's
   "weird behaviours ... like the Virmire intro".
2. **The right stick's turn** turned the held place about itself, so the eyes swung round it on the offset's radius: 0.61 m for
   90 degrees, 0.85 m for 180 (measured), and Shepard's body, which body follow keeps under the eyes within 0.75 m, fell behind.
3. **The eyes stand-in** (item 30) stood at the held place: 0.43 m ahead of and 0.18 m above the eyes; Girard's face 7-28 degrees
   off the eyes. And the body re-face rule, the conversational circle and the squad's formation took the player there too: the
   circle round a point 0.43 m in front of him.

Built (`game/ConversationWorld`):
- **`PlayerEyes()`**: the tick frame's `headWorld` (the held place before the host shows the world); the eyes stand-in stands
  there, and "Shepard's body is away" is his head bone half a metre (flat) from it, with him again under 0.35 m (`StepEyes`; the
  metre from the held place before); the re-face rule's bearing and distance, the circle (`MakeCircle`, `ShouldRegather`), the
  squad's formation and the old arc seats take the player there.
- **`FlatViewOffset()`**: the host's offset along the held view's axes, flat. An opening's place from Shepard's head bone (a cut:
  `FinishOpening`; the start's fallback without a fresh view on foot) is his head bone plus `gFoot.eyeFromHead` minus it, so the
  drawn eyes land where the eyes' offset says.
- **`TurnBy`**: the right stick's turn moves the held place round the eyes by the offset's change (and the jump follower's
  place with it); the eyes and the feet under them stay.
- Run 2 (19:18-19:29, the fixes above) showed one fault left, a body's, not a look's: at Girard's (the walk-in by the view) the
  stage's facing track turns Girard to Shepard's body on its mark (2 m from him, 0-2 degrees off it) with the player 3.9 m away
  and 69 degrees off, inside the re-face rule's 80 degrees, so his head reached 35-58 degrees of it and stayed 11-34 short: his
  body faced the hidden Shepard. **A facing the stage sets at once that points at Shepard's body (within 20 degrees) while the
  eyes stand in for him is turned to the player's eyes instead** (`StageFacingForPlayer`; an eased turn, as the stage's own
  facings are; the summary's `facingsToEyes`; log `the stage turns X to Shepard's body, away from the player: to the player's
  eyes instead`).

**Run 2 (the same scenes, the fixes but the last):**

| Scene | Shepard's head from the drawn eyes | Faces meant for the player |
|---|---|---|
| Anoleis, turned 90, 90, -180 | 0.07-0.08 m through the turns (the eyes at the same point to the unit) | 9 of 9 at the player, median 6 degrees |
| Zabaleta with Garrus and Tali, turned the same | 0.08-0.09 m (the eyes unmoved) | 16 of 16, median 9 |
| Virmire, Kirrahe's camp | **0.09-0.11 m** (0.53 for a moment at the opening's dip) | 16 of 16 at the player; Kirrahe 8-17 degrees off the eyes, as in plain scenes |
| The council's hearing | **0.10-0.14 m**; 0.47-0.60 for half a second at the cut | 9 of 13 at the player, 2 in that half second |
| Girard (before the facing rule) | 3.7 m (the stand-in on) | 12 of 13 at the stand-in at the eyes; his body faced Shepard's mark: 27 degrees median off |

**Run 3 (19:29-19:33, Girard with the facing rule):** the stage turned Girard to Shepard's body 1.4 s in and the rule turned him
to the player instead (`-163 deg, not 266`); from then on his body faced the player (0-2 degrees off it, 20 at the start) and his
face was 7-14 degrees off the player's eyes, median 14 over the scene (27 before); one line's own gesture turns his head 85
degrees away (54 in every earlier run: authored). Garrus "at Shepard's body" for 1.8 s of his formation walk was a misreading,
checked over every record of the day (20:40): his look target in those moments was an unnamed object (`''`, rank 99999), none,
or Girard, who stands 2 m from Shepard's hidden mark, so a face toward Girard reads as toward the mark by angle; the stand-in
moved his look at Shepard the first time he had one (3.8 s). **In every record since the drawn eyes were recorded, no look whose
target was Shepard stayed on his body while it stood half a metre or more from the eyes**, but for the runs before the fixes,
the A/B pass with the stand-in off, and one record in the half second after a cut.

Not covered by this audit: scenes on the 3D screen (no first-person eyes); flowed scenes on foot (Shepard's body is the player's
there); the arrival half of Virmire's start and Wrex on Virmire's beach (did not start from these saves). The face axis is the
head bone's first axis, no face for a hanar (Opold: 90 degrees throughout, the game's aim on the player).

**The moments (20:00-20:15; the owner: "Yes please", extend the records to the scripted walks).** A moment tracks nobody, so its
records take everyone shown within 20 m and Shepard's pawn where the track has him (`moment-begin`, `moment` every 1.5 s,
`moment-end`; named by the matinee, `moment_<sequence>_SeqAct_Interp_N`, from `authored_moment::PlayerTrackMatinee`), and the eyes
stand-in runs in a moment too (`StepEyes` over the people near: `NearPawns`, `EyesPeople`). Run from the John02 saves twice, the
stand-in off (`mevr convo eyes off`, the runner's `-Before`) and on:

| Moment | Shepard's head from the eyes | Who is near, and their looks |
|---|---|---|
| Anderson's walk to the Council (80, `SarenCouncil`) | 1.8 m at the start, 6.9 m at the end (the player stood still) | Anderson: no look-at target at all; his face follows the walk (11 degrees off the hidden pawn at the start, then up the stairs) |
| Becoming a Spectre (81, `PlayerBecomesSpectre`) | 2.0 to 17.5 m | Anderson and Udina: no look-at targets (one with an empty target at rank 99999 at the end); faces up the stairs |
| Chellik bumps into Shepard (90, on foot) | 2.4-3.3 m | nine bystanders, none with a look-at target |
| Liara's intro (92, `CD_LAV70_06_Liara_Intro`) | 3.6-6.5 m | Liara: no look-at target; her face (the matinee's animation) 7-21 degrees off the player, 1-14 off the hidden pawn |
| The geth intro on Feros (93, `GethIntro`) | 0.09-0.16 m (another composition: the view goes with him under a dip) | one, no look-at target |
| Benezia, phase 2 (91) | - | started her conversation, not a moment |

**In a moment nobody looks at Shepard through the look-at system**: the people's `m_oLookAtData` has no target (the conversation
gives them theirs); where a face is turned toward the hidden pawn, it is the matinee's own animation (Liara), and the player and
the pawn are a few degrees apart from her. So there is no look to move; the stand-in in a moment is a guard for one that sets a
look at Shepard (none of these six). What a player standing still sees is the scene walking away from him with Shepard's hidden
pawn (Anderson up the Council's stairs, 7 to 17 m): by the owner's rule nothing walks the player; he walks along with the stick.

The same run found one more cut that put the eyes behind Shepard's head: **Benezia's conversation after phase 2** (a scene that
starts as its save loads), 0.50-0.75 m all along. The opening had placed the eyes with the host's view offset of the frame on
screen, which was still the frame from before the world view: 0.39 m forward (0.44 m back from the next frames on). Now the
offset is the one of the last first-person view (on foot or held: `gViewOffsetSeen`, `NoteViewOffset`), and for 0.3-1.3 s after
an opening at Shepard's mark the drawn eyes are checked against the place meant for them: more than 15 cm off, the held place
follows them, once, under a dip (`gC.eyesMeant`; log `the opening's eyes were drawn N m from Shepard's mark`). Run again
(20:14-20:19, `tools/fake-headset-session.ps1`, Benezia's scene first after the launch): his head 0.06 m from the eyes after the
dip (the cached offset right, no correction needed), then 0.29-0.32 m as his own pose leans forward (Benezia's face 8-16 degrees
off both); the council's hearing 0.10-0.11 m, Virmire 0.09-0.10 m.

### Walking along (2026-11-12 21:00-, item 33)

The owner: "How do we handle the walking types of scenes? ideally we would know that shepherd should be walking alongside some
of the other characters. If we fall too behind the conversation should likely pause"; then, offered a pause at about 3 m behind
or a slow-down first: "Yes, build it" (the pause).

**The scripted walks, from the scan** (`conversation-sweep.py moments` over `sweep\scan.jsonl`: the 21 level-script matinees
that move the player by a track outside animated cutscenes; each matinee's groups with a move track, and its voice tracks):
someone walks beside Shepard in 10 (Anderson, the matinee's "Captain", in both Council walks; the squad, "hench1/2", in eight
others: Virmire's deployment, the Feros transition, the gambler, Noveria's double homicide...); in the rest the others who move
go their own way (Benezia's guards, geth, troopers) or nobody moves. 16 play sound during the walk, 7 of them with FaceFX (people
talking as they walk); the two Council walks are silent.

**How they played until now**: the track walks Shepard's hidden pawn, the view stays the player's; the player walks along with
the stick (the view joins the pawn when it rests) or the scene goes on without him (Anderson 7 m away at the Council walk's end,
17 m at the Spectre's), and the conversation after it cuts the player to Shepard's mark.

Built (`game/ConversationWorld`, rule `cw::StepWalkAlong` in core/vr/ConversationWorld, unit-tested):
- **Who walks beside Shepard** is read live from the matinee walking him (the move track instance's `SeqAct_Interp`,
  `authored_moment::PlayerTrackInstance`): every group actor of it, a person, with a move track, within 5 m of where the track has
  Shepard. No per-scene data: every walk is covered.
- **The pause**: with someone beside him, when the player's eyes are more than `worldWalkAlongPauseM` (3) from where the track has
  Shepard, the matinee's own `bPaused` is set (its tracks, camera and events wait); a line playing (a sound track's audio
  `BioIsPlaying`, or a person's `IsPlayingFaceFXAnim`) is not cut: the pause comes at its end. It goes on when the player is
  back within `worldWalkAlongResumeM` (1.5), when nobody walks beside him any more, or after `worldWalkAlongMaxS` (120 s; then
  not again that walk). The view never joins a paused walk (`gC.noJoin`): the track would carry the player when it went on.
  Released at the moment's end and when the mod unloads. `worldWalkAlong = 0` turns it off. Logs `moment: walk along:`; the
  moment records' `walkAlong` (paused, the gap, who is beside him, a line, the pauses).
- Tests: `mevr convo follow on|off` (the simulated stick follows Shepard's walk in a moment, a player walking along).

**Conversations**: holding the next line while the player falls behind was offered too; every record of the day was checked
for a speaker more than 6 m from the player's eyes: five conversations, all distances the scenes are built with (the councillors
on their platforms 18 m, Kirrahe before his troops, Benezia across the room, Liara in her bubble), none a group walking away. In
the first-person design the group gathers round the player (the circle) or the player stands at Shepard's mark (staged), so
nothing was built for conversations; a scene where people walk off while talking would be the case for it.

**In the game (fake headset, 2026-11-12 22:20-23:00; John02 80, 81, 92, 93, `mevr convo follow on|off`).**
- Standing still: Anderson's walk paused 2.8 s in (the player 3 m behind, Anderson beside Shepard) and stayed paused 75-91 s
  until the next load; the Spectre's the same; the two touches (Liara's intro, the geth on Feros) have nobody beside Shepard
  and never paused. Right.
- Walking along, first try: the follower closed to 1.1 m, fell back to 3.09 m and stuck there. **The walk's boundary**
  (`worldMoveLeashM`, 4 m around where the scene began for the player: the second design's) held the view in a moment too: a
  wall 4 m into every scripted walk, for a real player as much as for the test. Fixed: in a moment the boundary is around where
  the track has Shepard (`gMomentShepardAt`). Then the Spectre's walk: 24 m walked, the ceremony reached in 30 s; Anderson's
  (8 m, 8 s) never paused.
- **The pace**: the track walks Shepard at 1.2-1.34 m/s and jumps on two stretches (3.4 and 2.6 m/s, the stairs and the
  teleport up them), the player's stick at 0.9 (`worldMoveSpeedMS`, the owner's second design). Where the track went 0.9 the
  follower held 2.0 m behind; the opening pace and the two jumps paused the Spectre's walk three times, 11.9 s in all, for a
  player at full stick. **The owner: "for the walking speed we can increase the speed for the dialogue"** (2026-11-13):
  `worldMoveSpeedMS` is 1.4 again (as until 2026-11-11). The Spectre's walk at 1.4: 23.3 s instead of 29.8, the follower within
  0.8 m wherever the track walks (1.2-1.34 m/s), three pauses of 5.4 s in all, every one at the track's 2.8-2.9 m/s stretches.
  The on-foot walk's own ramp, "similar to the third person mode", is research/38 "Linear walking".

**The default mode keeps the game's behaviour** (the owner, 2026-11-12 21:40: "our changes to where Shepard is standing should
only apply when we're doing first person mode. when we're doing the default we need to ensure it maintains existing behavior").
Read in the code: everything that places, hides, shows or turns anyone, or holds a script, is behind the first-person mode
(`conversations = world`, `gC.world`): the stage's placement hook is installed only in it (`SetKeepProvider`) and its provider
checks again; the conversation's tick returns before any of it otherwise (`world` false: `StopWorld`); `Begin` only records
(its squad put-back behind `gC.world`); the opening, the circle, the squad's formation, the eyes stand-in, the facing rule, the
turn and the walk-along all run in the world mode's tick or a moment, and a moment begins only in it (`TickMoment`,
`MomentMayStart`); the matinee's move of Shepard is held only there (`MomentWithholds`); the flowed scenes need it
(`ConversationFlow`'s `Active()`), so do the black screen's dips (`MomentDip`), the staged relocation (`ScriptBlackScreen`) and
the voices (`PublishVoices`). `HidePawn` only ever shows a pawn it hid itself (a pawn the scene hides stays hidden), and with
`worldBody = player` it hides nothing: the body follower draws Shepard where the player stands (`BodyAway`, first person only).
An in-game check on the 3D screen (Chorban, Anoleis, the Council, Girard; no `withheld`, `put back`, `eyes:`, `squad:` lines
expected) waits for the game.

### Auditing scenes by rule (2026-11-12 22:00-, item 34)

The owner, after the audits from above: "I'm just curious if we can have a better/efficient way to audit scenes"; then, offered
rules over the records, numbers in every scene's summary and a regression set with a baseline: "Yes, build them".

- **The rules** (`tools/scene_audit.py`, `conversation-stance.py --findings`; tests/SceneAuditTests.py, ctest
  `mevr-scene-audit`), each with a threshold, from what the audits of the day judged by eye: `look-at-body` (a look target
  Shepard with his head 0.5 m or more from the eyes, once the scene settled: 1.5 s after its start or a dip), `eyes-off-body`
  (his head 0.3 m or more from the eyes in two settled records, his body with the player), `face-off-you` (a face meant for the
  player 35 degrees off his eyes two records running; minor in a staged scene, whose facings are its own; a person whose face
  reads 80-100 degrees in nearly every record has no face axis, a hanar, and is left out), `speaker-behind` (someone speaking to
  the player 120 degrees or more from his body's facing, twice), `too-close` (a person's centre within 0.5 m of the eyes),
  `squad-behind` (a squadmate 150 degrees or more round at the opening or the end), `walk-pauses` (a scripted walk that paused 4
  times or more), `walk-left-behind` (a walk with someone beside Shepard 5 m from the player and not paused). Over the day's 70
  scenes they found every fault judged by hand (the turn's 0.92 m, the cut's 0.35-0.97 m, Kirrahe's looks), and after the fixes
  only what was known: the hearing's first two seconds (0.60 m), Udina and Benezia (authored faces), Benezia's own pose (0.32 m),
  Zabaleta behind the player in the turn test.
- **A baseline** (`--baseline-write <file>`, `--baseline-compare <file>`: per scene the gap once settled, 90th percentile, the looks
  meant for the player and where they land, the faces' median angle off the eyes, the rules found; a regression is a new rule, a
  gap 0.1 m more, a look more on the body, a median 6 degrees more, or no look meant for the player any more; exit code 1).
  Checked by comparing the runs before the fixes with a baseline of the runs after them: the 12 regressions the fixes had removed.
- **The regression set** (`tools/conversation-audit-set.json`: ten scenes, one of each kind, from the validation saves) and its
  runner (`tools/conversation-audit.ps1 [-Long] [-WriteBaseline] [-Only ...] [-Judge <since>] [-Plan]`): through
  `tools/fake-headset-session.ps1`, records at each of the first four lines, the turn and the wander where the set says, then the
  findings and the comparison with `tools/conversation-audit-baseline.json` (written by the first good run).
- **Every play is an audit** (`game/ConversationWorld` `StepAudit`, ten times a second once the scene settled, no ray scans): the
  same rules measured live in every scene shown in the world; each logged once when it trips (`convo: audit: <rule>: ...`, with a
  record from above `finding-<rule>` when the stances are on), the totals in the summary's `audit` (`gapMaxM`, `gapOverS`,
  `lookAtBodyS`, `faceOffMeanDeg`, `faceOffMaxDeg`, `faceOver35`, `faceSamples`, `speakerBehindS`, `closeMinM`, `tripped`);
  `tools/conversation-session.py` lists them per scene (`audit:<rule>`, and `walk-paused` for a walk that waited), so a headset
  session is judged by the same rules as an unattended run.

**In the game (fake headset, 2026-11-12 22:01-23:06, three passes of the set; sessions `audit`, `walkset`, `walkset3`).**
- **The live numbers** came in every summary from the first scene (Chorban: the gap at most 0.08 m, faces 7 degrees off on
  average, 46 samples, nothing tripped). Two false alarms of theirs, fixed: Opold tripped `face-off-you` in 149 of 149 samples
  (a hanar's head axis, which the offline rules already left out: the faces are now tallied per person and a head reading
  80-100 degrees in four fifths of ten or more samples is left out, `faceAxisUnknown`); Anoleis tripped `speaker-behind` while
  the test had turned the player 180 degrees himself (the rule now judges, live and offline, against the facing the scene gave:
  the held yaw without the right stick's turns, `turnedDeg`).
- **What the set's first pass taught the set**: Chorban's first reply is "I'm going" (two lines, so its turn never came: Anoleis
  keeps the turn); the walk along made the Council wait 91 s for the scripted player, who stood still: the set now walks along
  in its two scripted walks (`before`: `mevr convo follow on`, every other scene with it off), and the Council gets 120 s; its
  judge printed nothing into a captured log (`Out-Host` bypasses every stream: `Write-Host` now).
- **Ashley speaking from 140 degrees behind** at Kirrahe's camp (line 63, her stage mark) was in the earlier run too, one
  record only; the live rule's own record made it two. A staged scene's marks are its own, so `speaker-behind` there is minor,
  as `face-off-you` was.
- **Zabaleta's sidestep moved 0.24 m in 3 s**: the squad's formation stands Tali 0.70 m from Shepard, at the walk's clearance,
  and a step nearer someone already that close was refused whole (374 pushes). `cw::KeepClear` now keeps such a person's
  distance and lets the step slide round them (unit-tested); the next pass slid past Tali and stopped at `BioTrigger_1`'s edge
  (the trigger guard, by design: this spot has a trigger beside the player).
- **A crash of ours** (the second pass, 22:36, as Liara's save loaded after the Council's): the conversation's last record from
  above listed its bystanders by the world's pawn list, which a level torn down under it leaves with freed pawns, and read
  0xFFFFFFFFFFFFFFFF through one's `NextPawn` (`WriteStance` from `End`). Every walk of that list in the module goes through
  `LivePawns` now (it stops at the first entry that is not a live object), and a conversation object freed under the record is
  no conversation. The third pass loaded the same scene after the same one without a fault. Not ours: quitting the game from
  inside Kirrahe's scene faults in the game's own `SFXGameModeManager.DisableMode` (a null read at exit, both times).
- **The baseline** (`tools/conversation-audit-baseline.json`, from the third pass on the final build: 12 scenes, the ten and the
  two scripted walks before them) holds what is known: Benezia's face (her dying animation, 68 degrees median), the hearing's
  opening (0.70 m for 1.3 s at 2.5 s, live: the known transient), Ashley's mark, Tali 152 degrees round at Zabaleta's (at the
  rule's edge: 149.5 in the first pass; she stands where she followed the player in). The pass against it: 0 regressions.

### The staged scenes (2026-11-12; for the owner's review)

From the scan's 491 conversations (`conversation-scan.py --csv`), the ones that look like "the council or the virmire speech": a
council, a ceremony, a speech or a briefing to a group, or a scene whose camera work is authored. Staged now (`cw::IsStagedScene`):
Kirrahe's camp (`jug20_sal_captainkirrahe`, off the 3D screen's list since 2026-11-12: the scan's `STAGED_IN_WORLD`), the
Council's hearing (`sta90_council_saren`), the Spectre induction (`sta90_council_spectre_induction`), the grounding of the
Normandy (`sta90_grounded`), the ending's Council and choice (`end90_council_alive`, `end90_council_dead`, `end90_choice`; the first
two on the 3D screen as set pieces). Candidates for the owner to judge (each a group composed for the camera): Anderson's briefing
in the comm room (`nor10_captain`), the approach to Ilos (`nor20_ilos_approach`), the archive door (`los10_trig_door00`), Vigil
(`los40_watcher`), Benezia and the rachni queen (`ice60_matriarch`, `ice60_rachniqueen`), the Thorian (`war30_thorianasari`), the
mission debriefs in the comm room (`nor20_*_done`: the Council's holograms), Udina and Anderson at the embassy
(`sta20_human_ambassador_enter`). `[Cinematics] worldStaged` adds any of them without a build.

### The body that stayed at the mark (2026-11-13; item 35)

**What the owner saw** (headset session 10:33-10:40, save 80, first person, Thespian mode on): "the walk works out, and when it
does the fade to black transition and comes back in, I'm now placed where the camera wants me to be, which is fine. However, my
body is where I was previously walking." And: "this has been a persistent issue that we've had for this scene in particular."

**What the log says** (that session's `LE1MEVR.log`):
- arm IK went `body anchor: third person (tracking origin at Shepard's animated head), view 254 units from Shepard's animated
  head bone` on the hearing's first tick (10:34:45.044) and stayed there through the hearing and the two conversations after it
  (`sta90_captain`, `sta20_captain`) until on foot again at 10:39:25: four and a half minutes in which the body follower made no
  slide (`torso: third person: the hips turn ... body slid 0.00 m`), so Shepard's body stood where the stage animated him.
- The hearing's live audit: `gapMaxM` 5.71, `gapOverS` 224.8 of 226.6 s; the rule's own line said "4.59 m ... for 1.1 s",
  its first second only.
- The opening: `the player's place: Shepard's head bone at (-9477, ...)` with his pawn at (-9076, ...): his head bone 3.5 m from
  his pawn. Then `Shepard's head bone JUMPED 4.84 m`, `the opening's eyes were drawn 5.14 m from Shepard's mark: the held place
  follows`, `JUMPED 0.81 m`. And `the player walks: the stick (-0.08, 1.00)` at 0.0 s: the stick still pushed from the walk,
  4 m in the first four seconds (`leashHits` 11).

**Why.** The scene's opening placed the player by `HeadOf(pawn)`, which reads the DRAWN bones (SpaceBases after our writes).
During the walk with Anderson the body follower had slid Shepard's bones to where the player walked (`BodyAway`, "the body stays
with the player"), so the opening read our own slide as his mark; the follower chased that head as the slide went away; the eyes
check (`gC.eyesMeant`, made from the same reading) moved the held place back by 4-5 m. Arm IK, deciding its frame by the view's
distance to Shepard's ANIMATED head (`game/HandTargets`: first person within 45 units, third person past 75), saw 254 units on the
first tick and fell to its third person; in a conversation nothing brings the view back within 45 units of the stage's head, so it
never came back. The same anchor fall bit on 2026-11-09 ("weapons very low" after a crouch): gotchas/arm-ik-third-person-anchor.

**Why the audits never saw it.** The fake headset has no controllers: without tracked hands arm IK and the body follower never
run, so the unattended runs had neither the slide nor the anchor fall. What they did see (0.70 m for 1.3 s at 2.5 s in the
2026-11-12 passes) went into the baseline as "the hearing's opening, the known transient", and the baseline compares a run only
with the last one. The scripted player also never kept the stick pushed through the cut.

**Built** (03ba0319):
- Every conversation shown in the world counts as the view held away from the pawn (`conversation_world::BodyAway`: `gC.detached
  || (gC.on && gInWorld)`, the rise 0 when not detached): arm IK keeps the view's frame (`hand_targets::Current`'s `inView`) and
  the body follower puts the body where the player stands, without the 0.75 m limit.
- The player's place, the head follower, the opening's facts, the put-back and the eyes stand-in read Shepard's ANIMATED head
  (`MarkHeadOf` over `arm_ik::AnimatedHeadComponent`: `bone_pose::AnimatedHead`, scaled as drawn); `HeadOf` (drawn) stays for
  what the player sees (the audit's gap, the diagnostics).
- A stick still pushed through a cut or a dip walks only once let go (`gStickCutSeen` against the published cut; log `convo: at
  N s the stick is still out (...) through a cut`).
- The live audit: `body-third-person` (arm IK driving but not in the view's frame for a second), the summary's `sampledS` and
  `thirdPersonS`, and `convo: audit totals:` at the end when a body finding lasted 2 s or more.
- `tools/scene_audit.py`: `eyes-off-body` and a 90th-percentile gap of 0.3 m or more are never excused by a baseline
  (`ABSOLUTE`; tests/SceneAuditTests.py).
- The audit set replays tracked hands in every scene (the set's `hands`, `tools/make-walk-recording.py --plan stand:600`); the
  hearing's `forward:14,stand:300` keeps the stick pushed through the walk and its cut.
- `tools/conversation-from-save.ps1 -Probe 'seconds:console line'`: console lines at times after the fire, from the runner.

**In the game (fake headset, tracked hands replayed):**

| Run (session) | Build | The hearing's opening | Shepard's head from the eyes | Arm IK |
|---|---|---|---|---|
| A (`2026-11-13_11-02-49-audit`) | main 7f581977, tracked hands, the stick pushed through the cut | the player at (-9382, -6853), his head bone then `JUMPED 3.63 m`, `eyes were drawn 3.97 m from Shepard's mark` | 7 records at 0.3 m or more, at most 3.85 m (the audit: `eyes-off-body`, 3 regressions against the baseline) | `third person`, 372 then 384 units |
| B (`2026-11-13_11-06-04-audit`) | 03ba0319 | the player at (-9044, -6994), as the runs without hands had it; `the stick is still out (0.90) through a cut` | at most 0.11 m, 0 s over 0.3 m (`thirdPersonS` 0; 0 findings, 0 regressions) | first person throughout |
| C (`2026-11-13_11-09-31-probe`) | 03ba0319, the whole sequence, lines skipped | as B | the hearing at most 0.90 m for 0.2 s (one transient), the two conversations after it 0.10 m | first person from the walk's start to the end |

**The council during the walk (run C's probes, `-Probe`):** five seconds into the walk `BIOA_STA70_02council_DSG loaded=1
visible=0 shouldLoad=1 shouldShow=0`, with all its people loaded (Udina `BioPawn_7` at (-9188, -7087), the councillors
`BioPawn_3/4/5`, Saren's `BioPawn_1`, the gallery's `BioPawn_11-16/77/79/91`); by 9 s, after the black screen, `visible=1`. The
level's `SarenCouncil` sequence begins the hearing from `BioSeqEvt_ForegroundStreamingDone_0` (its volume `streamvol_council`)
through `BioSeqAct_PMCheckState_1` ("Council meeting happened?"); Saren becomes a hologram only then
(`BioSeqAct_AttachCrustEffect_0`, "Saren is a transmission"), and "Activate all the players" comes after it. So the chamber is
empty during the walk because its level is loaded and hidden until the volume's touch; showing it early is the next step
(item 36), with three things to verify: that showing the level does not fire the streaming-done event (the hearing would begin
mid-walk), what Saren's pawn looks like before his crust effect, and that the hearing still begins at the touch.

**The whole set with tracked hands** (sessions `2026-11-13_11-20-59-audit`, `11-28-50-audit`, `11-34-53-audit`, the fixed build):
0 regressions in all 12 entries against the 2026-11-12 baseline; the findings left are the known ones (Benezia's dying face,
`face-off-you`; Ashley's mark at Kirrahe's camp, `speaker-behind`). Shepard's head from the eyes, 90th percentile, 0.09-0.11 m in
every scene that has one; the hearing 0.10 m (0.6 m in the old baseline). The baseline is rewritten from these passes
(`-Judge 2026-11-13T11:20:59 -WriteBaseline`). Two of the passes stopped on a crash that is not this change: with the owner's
`[Thespian] on = 1` (the owner's ini since today), Thespian mode called `BioConversation.GetReplyText` on a conversation the runner
had just ended with `mevr convo end` (Benezia's, 57 ms after; Liara's), an access violation inside the script function
(`crash-breadcrumbs.txt` 11:26 and 11:33). The last pass ran with `[Thespian] on = 0` for the run and the owner's 1 put back; the
crash is a task of its own. A console `mevr thespian off` in a scene's `before` does not help a scene that starts by itself as its
save loads (the runner skips `before` there).

### The council during the walk (2026-11-13; item 36)

**The owner:** "when we're walking up with Anderson, the council haven't spawned in the assets with like Nihilus or Odina, so
that's not desirable either" (Udina and the councillors; Nihlus is dead by then). The owner's go-ahead: "Yes, do it".

**Measured** (fake headset, save 80, `conversation-from-save.ps1 -Probe`, sessions `2026-11-13_12-34-11-show`,
`12-39-09-people`, `12-47-06-early`, `12-51-54-early2`, `12-55-57-early3`, `12-59-29-early4`):
- `BIOA_STA70_02council_DSG` is loaded (`shouldLoad=1`) but not shown (`shouldShow=0`) all through the walk; the chamber's
  streaming volume shows it under the walk's black screen.
- Its 14 pawns are all `bHidden` 1 through the walk (`mevr stream people`): Saren `sta90_saren`, the councillors
  `sta90_asari_councilor`, `sta90_salarian_councilor`, `sta90_turian_councilor`, Udina `sta90_ambassador`, the hearing's own
  Anderson `sta90_captain` and eight `sta90_induction_extra_*`. At the hearing the script shows the first six; the extras stay
  hidden (they are the Spectre induction's).
- Showing the level by hand (`mevr stream show BIOA_STA70_02council_DSG on`, 4.5 s into the walk) did not begin the hearing early:
  it began at the walk's end as before (12:36:44.8, the black screens at 44.70 and 44.79). Its start is the level's
  `BioSeqEvt_ForegroundStreamingDone_0` (volume `streamvol_council`), which the volume's own state change fires, not the level's
  visibility.

**Built** (`cw::ShownEarlyFor`, unit-tested; `game/ConversationWorld` `StepShownEarly` / `EndShownEarly`; `[Cinematics]
worldShowEarly = 1`): a table of scenes the script shows only under its black screen. Its one entry: the moment whose move track
lives in `BIOA_STA70_02captain_DSG`'s `SarenCouncil.SeqAct_Interp_0` shows `BIOA_STA70_02council_DSG` (bShouldBeVisible, journalled)
and unhides Udina and the three councillors (`Actor.SetHidden`). Saren stays hidden: his pawn is a hologram only once the hearing
attaches `Hologram_Avatar_VFX` (`BioSeqAct_AttachCrustEffect_0`). The hearing's Anderson stays hidden too, since the walk's
Anderson (`BIOA_STA70_02captain_DSG` `sta90_captain_cutscene`) still walks beside the player. A walk that ends without its
conversation hides them and the level again. Logs `moment: <level> shown as the walk begins`, `moment: N of M of the scene's
people ... shown`, `moment: the scene shown early hands over to its conversation`. Console `mevr stream show <package> [on|off]`,
`mevr stream people <package>` (tag, hidden, last drawn, place).

**In the game:** 4 of 4 people shown 3-4 s into the walk, `hidden 0`, drawn on their first frame (`drawn 1.21 s ago` 1.2 s
later), then not drawn again from the stairs: the chamber's front edge stands between them and an eye at the foot of the stairs
(an eye at 62007 sees that edge, 2.4 m up at 19 m, above the councillors' heads at 36 m), and the replayed walk (and the owner's
too, the view at (-7328, -7066, 61986) when the hearing began) ends on the stairs. Anderson beside the player was drawn every
frame. The hearing began on time in every run; the black screen's change detector saw 2 new people (Saren and the hearing's
Anderson), no dip; the hearing's body audit stayed clean (Shepard's head from the eyes at most 0.11 m, `thirdPersonS` 0). Seeing
Udina and the Council over the edge as you climb is the headset's check.

### Seeing the body in the audit; broken runs the audit must catch (2026-11-13; item 37)

**The owner:** "how can I get better confidence on your audits?", then "we need to ensure that we can always see our body in
the screenshot to validate placement. So the hands may need to be in front of our player for the sake of visibility". The
audit had missed both of the owner's slot-80 reports (items 35 and 36): the fake headset had no controllers, so arm IK and the
body follower never ran; its scripted player stood still where the owner pushed the stick through the fade; no rule looked at
what is drawn (the empty chamber); a run is compared with the last one, and the hearing's gap was already in the baseline; and
"0 regressions" was read as "fine" where it meant "nothing new on eight rules".

**Built** (commits on main; `tools/conversation-audit.ps1`, `tools/audit-report.py`, the skill `first-person-scene-audit` 0 and
1b):
1. **Where the drawn hands are** (`arm_ik::MeasureHands`): each drawn wrist (the last posed pose, `info->arms[side]->hand`) taken
   into tracking space through the view's frame (`view_anchor::Current`), its distance from that controller's grip, and whether
   it lies within 35 degrees of the head's forward, up, down and sideways. In first person the wrist sits 7 units behind the
   grip (0.07 m measured); a body left at the mark puts it metres away whatever arm IK says of itself. It is in every snapshot
   (`mod.armIk.hands`: `firstPerson`, `left`/`right` `fromGripM`, `inView`) and every stance record (`player.hands`,
   `player.armIk`).
2. **The rule `hands-off-controllers`**: live (`StepAudit`: a drawn wrist 0.3 m or more off for 1 s; the summary's audit has
   `armDrivenS`, `handsSampledS`, `handsInViewS`, `handsOffS`, `handsOffMaxM`, and `convo: audit totals:` names them) and
   offline (`scene_audit.py` over records, two settled records running; in `ABSOLUTE`: never excused by a baseline; covers the
   scripted walks, which the live audit doesn't). Tests in tests/SceneAuditTests.py.
3. **The replayed hands held up in view** (`make-walk-recording.py --hands view`: the grips 0.25 m ahead, 0.08 m under the eyes,
   0.09 m to each side, 0.28 m from the eyes, inside `[Hands] reachNearFaceM`'s 0.30, so the arm-length calibration doesn't pull
   them in). At 0.40 m ahead (the first try) they were reach-scaled 0.16 m low and cut off at the picture's bottom edge, which the
   45-degree "in view" of the first build still counted as seen: hence 35 degrees.
4. **Snapshots and a contact sheet**: an eye snapshot every 12 s, 4 s into each conversation and 3 s into each scripted walk
   (`conversation-from-save.ps1 -SnapshotEvery`, asked without waiting, tagged `a-<scene>-<seconds>`); `audit-report.py` puts
   each scene's in a row of `contact-sheet.png` in the session folder, framed green (both drawn hands in view and within 0.3 m of
   the controllers), red (not), grey (not measured) or blue (no scene held the view: the game's own camera after a conversation
   ended is not judged).
5. **What was measured, and what was not**: per scene (`-Report`, one JSON line each in the session's `audit-report.jsonl`) the
   hands replayed, how long arm IK drove the body, the hands' totals, the snapshots' verdicts, the scene's expectations, the rules
   that tripped, and `NOT MEASURED` lines (arm IK never drove the body, no conversation shown in the world, an expectation never
   reached, snapshots that never came). A body not measured or a failed expectation fails the run whatever the baseline.
6. **A scene's own expectations** (the set's `expect`: `seconds:console lines=>regex`): the council's is `mevr convo` and `mevr
   stream people BIOA_STA70_02council_DSG` 5 s into the walk with Anderson, requiring game mode 6 (still the walk) and the four
   people `hidden 0`. At 10 s (the first try) it ran in the hearing: the walk lasts 7.5 s, and the hearing shows them itself.
7. **Broken runs** (`conversation-audit.ps1 -Canaries [-Canary <names>]`, each in a game of its own; the set's `canaries`):
   a fixed bug brought back by a test switch in the mod (`mevr convo bodyaway off`: item 35's fix undone whole; `mevr convo
   showearly off`: item 36's), or what the audit needs taken away (no hands). Each must be caught by one of its `mustFind`
   rules; a run passed clean is `BLIND` and fails.

**Measured** (fake headset; sessions `2026-11-13_14-47-44-audit`, `15-09-38-audit`, `15-21-42-audit` for the set, `14-54-19-canary-body-left-at-the-mark`, `14-57-33-canary-empty-chamber`, `15-00-31-canary-no-hands`, `15-03-42-canary-body-left-at-the-mark` for the broken runs):

| broken run | result |
|---|---|
| body left at the mark, body rule alone off | **BLIND**: nothing broke (hands 0.07 m, first person throughout): the placement from Shepard's animated head and the stick held through the cut kept the bug away by themselves. The switch now undoes all three |
| body left at the mark, the whole fix off | caught: `hands-off-controllers`, `body-third-person`, `eyes-off-body`; 3.89 m (the owner's run A: 3.85), third person for all 11.9 s; 3 of 3 snapshots red, no hands in them, Shepard's body standing about 4 m off |
| the empty chamber | caught: the expectation failed 5.6 s into the walk |
| no hands | caught: the body not measured (arm IK 0.0 s of 13.7) |

The set with the hands in view, all ten scenes (over three passes: the first stopped at Benezia's crash, below): arm IK drove the body for all of every scene's time in the world (Kirrahe's camp 173.3 of 175.1 s), both drawn hands in view 100% of it, off the controllers 0.0 s (at most 0.07 m; Anoleis 0.35 m for 0.1 s, at a cut), third person 0.0 s; every in-scene snapshot green (the hands in the lower half of the picture, the scene above them). The council's check met 6.9 s into the walk. Against the baseline two new findings, once each: `too-close` in the walk with Anderson (someone 0.41 m from the eyes 4.6 s in) and one look at Shepard's body in Girard's scene; neither comes from this change (it only reads), the baseline was not rewritten to excuse them. A scene still playing when the runner stops (Kirrahe's camp) wrote no summary and read "not measured" until the runner asked for the live totals (`mevr convo audit`).

**The forced end crashed the game now and then (fixed)**: the set stopped after Benezia's scene twice today, both at the
runner's forced end (`mevr convo end`, `-EndAfter`): in the morning in Thespian's `GetReplyText` (fixed, 57e298c3), this
afternoon (1 of 3 plays) 20 ms after it at `MassEffect1.exe+0xC87E81`, reading 0x20880E7FDE4 (the script frames named
`BioWorldInfo.UpdateConversation`; `BioSubtitles.UpdateSubtitles` above it had already returned). That address is inside the
conversation's own update (`0xC87830`, what `BioWorldInfo.UpdateConversation` calls each tick on `m_oCurrentConversation`): with
a current reply (`+0x270` not -1) it reads the reply's number through `m_lstCurrentReplyIndices` (`+0x108`), the entry's reply
list (`m_EntryList`, `+0xB8`, entry `+0x268`) and `m_ReplyList` (`+0xC8`), and it read a garbage number there. `mevr convo end`
called `BioConversation.EndConversation` on the conversation alone. The game's own early end, `BioWorldInfo.EndCurrentConversation`
(`0xB58790`; the conversation handler calls it when a reply can't be queued, an interruption goes through it), flags the
conversation ended (`0x2000` at `+0x2C0`), calls its `EndConversation`, and then lets go of it (`m_oCurrentConversation` =
None, `+0xB1C`). Without that last step the world went on updating an ended conversation whose lists were gone. **Fix:** `mevr convo
end` calls `BioWorldInfo.EndCurrentConversation` on the world info (game thread, console) and logs `convo: mevr convo end:
BioWorldInfo.EndCurrentConversation called 1, the world let go of the conversation`. Every tool's forced end goes through it
(conversation-from-save, -sweep, -launch, -survey). **Checked** (session `2026-11-13_15-42-38-forced-ends`, one game): Benezia's scene ended early 8 times and the council's hearing (and the conversation after it) once each, 10 forced ends; every one logged `the world let go of the conversation`, and the game ran on after the last. Before the fix 1 of 3 forced ends of Benezia's scene ended the game. **Not this:** the crash as the session tool quits the game (`ntdll.dll+0x1A12D` writing 0x24, 23 ms after `game exiting (code 0)`, from the exe's exit path through `ucrtbase`), seen again at 15:53:56 after these 10 clean ends, and a null read at `MassEffect1.exe+0x12DF33` 1.4 s after the quit's `exit` with Kirrahe's conversation still playing (15:28:12): both at the quit, after every scene is over.

### Frame timing at the director's cuts (2026-11-13; item 24)

The owner's session of 2026-11-11 had frames of 40-90 ms near most of the hearing's camera cuts (Halcyon's `[GPU] maxMs` per
two-second window). Measured on the fake headset with the installed build (0.2.0+fcb1c39716), John02 slot 80 (Anderson's walk
and the hearing), the walk followed (`mevr convo follow on`, else the walk along waits 120 s for a player who stands still),
`tools/perf-probe.ps1` in two back-to-back captures of 110 s per run, one fresh game per setting, and each frame lined up with
the mod's `convo: shot` lines (the scripts' logic: per frame, the GPU spans of the two eye renders, `gpu_start_pass0 +
gpu_pass0_present` from `tools/perf-probe.py`'s `frame_metrics`, and the frame interval, against the second after each cut):

| `[Cinematics] worldCamera` | cuts | scene GPU within 1 s after a cut: p50 / p99 / max | elsewhere: p50 / p99 | frames over 60 ms within 2 s of a cut |
|---|---|---|---|---|
| 0 (default) | 27 + 19 | 9.37 / 13.66 / 15.70 ms; 11.13 / 13.52 / 13.73 | 9.54 / 15.33; 12.86 / 15.33 | 0 of 46 cuts |
| 1 | 33 + 20 | 9.33 / 13.77 / 14.37 ms; 11.16 / 13.35 / 26.03 | 9.57 / 15.40; 12.91 / 17.14 | 0 of 53 cuts |

The render thread's work after a cut: p99 5.2-5.6 ms, as elsewhere. The mod's own work per game-thread tick (the probe's host
busy time) after a cut: p99 2.7 ms, at most 6.4 ms. The render levers line: one scene render per frame throughout, no One Draw
fallback, no change of Halcyon's presentation at a cut. So on the fake headset **a cut costs nothing measurable, and holding the
game's camera at the player (`worldCamera = 1`) changes nothing**: the code's own note is right that the render-time swap of the
main view already feeds culling and texture streaming (UE3 takes the streaming view from the view CalcSceneView builds). No
default changes.

What the fake headset cannot show: **its runtime paces every frame at about 31.9 ms** (every capture since 2026-11-09; SteamVR's
null driver; the compositor's every present is "timed out"), and the game thread spins through that wait (its busy time is the
frame's), so a stall under about 20 ms on the game thread hides in the slack. A 40-90 ms stall would still show as a frame over
60 ms, and none came within 2 s of a cut. Halcyon's `[GPU] avgMs`/`maxMs` include the runtime's pacing (33 ms on the fake
headset, the frame interval), so the owner's 40-90 ms were frames 4-8 display periods long, whatever stalled them. Not tried:
the textures were resident (3.2 GB of a 23 GB budget; the scene played several times that day), so a first play's streaming
uploads did not happen here; the head stood still.

**Next: a capture in the headset**, the hearing played with `tools/perf-probe.ps1 -Seconds 110` (or the menu's Advanced >
Diagnostics > Measure frame timing) started as the hearing begins; then the frame interval and the scene GPU spans against the
cuts, and the game thread's ticks. If the stalls come back there: the probe's frames say GPU or render thread; on the game
thread, `tools/sample-render-thread.ps1`'s sampler (`mevr-stack-sampler`) pointed at the game thread.

### Obvious bugs, measured (2026-11-13; item 38)

**The owner:** "for most everything it was a bug like obviously being placed in the wrong location. For those cases, how well
do you think you can measure these types of problems? I want my time spent on fine tuning, not finding obvious bugs." Of the 14
obvious bugs the owner found in the headset (2026-11-06 to 11-13), the audit of item 37 would have caught 2 outright (the
hearing in third person, the body left at the mark) and 3 in part (a blink for nothing, Kirrahe's empty marks, the council
hidden during the walk: only through that scene's own expectation). Placement was measured; what is drawn, what the stick does,
the UI's pictures and frame timing were not. Six steps, in the owner's order ("Yes, please get started"):

1. **Drawn, not only placed** (live in `StepAudit`, offline in `scene_audit.py`; both in `ABSOLUTE` but the person's): the engine's
   `PrimitiveComponent.LastRenderTime` (the world's clock when a view last drew the mesh; frustum, occlusion and hide flags all
   leave it behind) against `WorldInfo.TimeSeconds`.
   - `body-not-drawn`: a drawn wrist inside the head's view (so the body is in the picture) and the player's body mesh not drawn
     for 0.3 s, 1 s in all (the council's lines that hid the body with every bone in place, 2026-11-10).
   - `person-not-drawn`: someone of the scene with the head inside the view (35 degrees each way, `InHeadView`), a clear line from
     the eyes (`FastTrace`), within 12 m, not hidden, its mesh not drawn for 0.3 s, 1 s in all (the comm room's culled people,
     2026-11-06).
   - `near-plane`: the frame drawn with `LocalPlayer.NearClipPlane` over 15 units (read after the world view's write-back, the
     value the frame draws with; the director's are 67-280: the floor and the body cut away, 2026-11-10).
   - Records: `player.bodyDrawnAgoS`, `player.nearPlane`, `people[].drawnAgoS`, `people[].inSight`; the summary's audit
     `bodyCheckedS`, `bodyUndrawnS`, `sightCheckedS`, `personUndrawnS`, `nearOverS`, `nearMax`.
2. **The scene that played** (`conversation-from-save.ps1 -Report`): each conversation that played and the first entry it showed
   (`conversations`), `sceneSeen` (the scene's own conversation played at all) and, with a scene's `startEntry` in the set,
   whether it began there (Kirrahe's arrival talk where the planning was meant, 2026-11-11). `audit-report.py`'s WRONG SCENE.
3. **The owner's own saves in the set**: slots 81 (the Spectre ceremony's walk) and 89 (Avina, the owner's baseline) added;
   slot 88 (Kirrahe's camp) not: its first use plays the arrival talk whatever the plot (item 22), and the set's Kirrahe from
   `John02_70` plays the planning and its new composition.
4. **The stick as a player holds it**: `mevr convo stick left <x> <y> [s] | right <x> [s]` puts a value in the controller's stick
   where the conversation's walk and turn read it (`SticksNow`; through the wheel's latch, which `mevr convo walk` goes round).
   The runner's `-HoldThroughPick` holds the left stick from a conversation's first wheel through the pick for 4 s,
   `-StickTurnAt N` the right stick for 1.5 s, and `-WanderAt` walks away with the stick now. Live rules, run every tick (the
   walk a pick makes comes in the cut after it, which the 1.5 s settle skipped): `moved-by-pick` (the eyes 0.2 m from where they
   were as the wheel closed, the stick never let go, within 3 s: the lurch after every reply, 2026-11-10) and `turn-ignored` (the
   right stick held over 0.7 for 1 s, the view turned under 10 degrees: 2026-11-11). Totals `picksHeld`, `movedByPickM`,
   `turnsTested`, `turnMaxDeg`.
5. **The pictures** (`audit-report.py`, each snapshot in a scene; a snapshot as each conversation's wheel first comes up):
   `eye-flat` (the left eye one colour, its spread under 2 levels, not under the transition fade), `hud-black` (a quarter of the
   HUD panel opaque black: the script's black screen as a square ahead of the player, item 21), `wheel-cut` (more than 8 opaque
   pixels in the wheel band's three outer columns: "us." left floating, item 23). On 19 snapshots of the day's good runs: none.
   And from the log: `black-panel-on-foot` (`game/AuthoredMoment`: the script's black screen up 1 s in game mode 0 outside a held
   moment), which fails every run until item 21 is decided.
6. **Frame timing on the fake headset: not solved.** Halcyon's `[PRESENTHZ]` tops out at 31.3 fps in every scene of the day's runs
   (the splash screen too: research/67's 31.97 ms), so the fake headset hides stalls under about 20 ms. SteamVR's compositor
   ends each session with "N presents. N dropped. 0 reprojected". Ruled out, measured during the runs: **the game's window
   covering the headset window** (the game's borderless window is 3840 x 3840 at 0,0 and lies over SteamVR's 'Headset Window',
   1920 x 1080 at 1600,180; raised to the top with `SetWindowPos(HWND_TOPMOST)`, 30.5 fps, put back), **the display asleep** (the
   owner's 5120 x 1440 monitor reads Availability 3, running, 239 Hz, and holding it on with `SetThreadExecutionState(
   ES_DISPLAY_REQUIRED)` changed nothing), and SteamVR's per-app settings (`steam.app.1328670`: `framesToThrottle` 0,
   `motionSmoothingOverride` 0; `steamvr.motionSmoothing` false). The null driver says 90 Hz (`Display Frequency: 90.000000`,
   render target 2824 x 2880). Leads, not tried: SteamVR's automatic throttling (every present counted late, the app given every
   third vsync), and the two virtual monitors now installed (Virtual Desktop's and Meta's, since the owner's Quest tests; the
   31.9 ms captures began 2026-11-09). Both need a change to the machine's SteamVR or display setup that is the owner's to allow.

A run fails on any of `audit-report.py`'s `FAIL_RULES` (the live rules a player would see at once), a picture check or a wrong
scene, besides what failed it before. **Broken runs** (one game, `"game": "opold"`, each switch put back after its scene):
`mevr convo hide body on` (`body-not-drawn`), `hide person on` (the owner's mesh: `person-not-drawn`), `nearplane off`
(`near-plane`), `sticklatch off` (`moved-by-pick`), `stickturn off` (`turn-ignored`).

Also counted now: the session judge's lines per scene (`tools/conversation-session.py`: flash, dip, screen, body-strayed...) go in
each scene's record (`noticed`); a `flash` fails the run. The offline drawn rules skip the record written as a scene ends (the
view may already be the next screen's: Opold's shop).

**Run on the fake headset, 2026-11-13** (the players' build with these changes, `0.2.0+557628cac6-dirty`):
- **The broken runs** (18:24-18:31, one game, five plays of Opold's scene): **all five caught**, each by its own rule:
  `body-not-drawn` (not drawn 17.1 of 17.4 s with a wrist in view), `person-not-drawn` (Opold in sight 3.4 m away, not drawn
  11.6 s), `near-plane` (337 units: it also culled the whole body, `body-not-drawn` 5.3 s), `moved-by-pick` (0.21 m 0.2 s after
  the wheel closed; the walk went on to 3.1 m), `turn-ignored` (the right stick at 0.90 for 1 s, 0 degrees).
- **The set** (18:32-18:46, 13 scenes): in every scene where a wrist was in view the body was drawn throughout (8.0-32.1 s
  checked, 0 undrawn), everyone in sight drawn (1.7-32.2 s checked, 0 undrawn), the near plane 10 units; a reply picked with
  the stick held walked nobody (0.00 m, Opold and Avina), the right stick turned the view 153 degrees in 1.5 s; the wheel
  snapshots whole, no black HUD panel. Not measurable by design: the body's drawing while the view walks to a hidden Shepard
  (Liara's and Girard's walk-ins by the view: `detached`), people in sight at Wrex's (framed far).
- **Found:**
  1. **A near-black frame in the Spectre ceremony's walk** (`eye-flat`, 6.8 s; the room just visible, as under a fade): 0.3 s
     after the script moved Shepard 2.67 m up the stairs in sight (`moment: the script moved Shepard ... the same space, no cut`).
     The owner's "blink in the middle of my own walk" of 2026-11-10 was the script's `BioSeqAct_BlackScreen`, now taken down in
     a held moment; this one looks like the matinee's own fade [verify: the snapshot `2026-11-13_18-41-30-411`].
  2. **A 0.03 s flash of the 3D screen as Opold's conversation hands over to his shop** (game mode 7), in 3 of the day's 6 plays
     of his scene (the session judge's `flash`; it fails runs from now on).
  3. **Anderson within 0.18-0.28 m of the player's eyes in both Council walks** (`too-close`, 4.6 s into each, the player walking
     along): seen as 0.41 m in a run of item 37 and taken for a one-off; it recurs.
  4. Unchanged: Benezia's face 86 degrees off the eyes at her line 53, Udina 77 at the ceremony's opening (staged).

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

The audio list (A1, A2, A4; host and mod from the same build: the menu's Sound page is the host's, `game/SoundPlace` the mod's;
OpenAL Soft in `ASI\audio` as `tools/deploy-asi.ps1` puts it; one fixed head for every A/B, as in "The voices, placed"):

6. **The hook goes in on foot without the world mode**: with `[Cinematics] conversations` not `world`, the log has `hooked
   FISACTSoundSource::Update at ... (ActorComponent.Owner at +0x..; the census reads AudioComponent.SoundCue at +0x88 and
   CurrentUseSpatialization at +0x24C)`, then `sound place: [Audio] overheard on (...), gun on` and, on foot, `sound place: on
   foot: people near the player heard from where they stand, the gun from its muzzle`; `reflect:` shows the two new rows agree.
7. **A1, the Garrus argument at C-Sec on foot**: `tools/conversation-launch.ps1 sta70_garrus -World on` (the world mode
   makes its argue chain play as barks on foot: `convo flow:`). Expect `voice place: the first source placed for the people
   near the player: ...` and `sound place: BioPawn_N speaks from where it stands (x.x m from the player ...)` for Garrus and
   the Executor, and the heartbeat's `sound place: 2 people near named now; this period M source updates placed for them`.
   During the argument `tools/console.ps1 "mevr voices census 15"`: Garrus' and the Executor's facial components `in the
   centre (component spatialization 0) ... placed for the people near the player`; anything of theirs `placed by the game`
   is `left alone`. **Measured:** `build\Release\mevr-audio-loopback.exe a1-on.wav 15` with the head turned so Garrus is about
   80 degrees to one side, then `python tools/audio-balance.py a1-on.wav --from <s> --to <s>` over his lines: the balance
   several dB toward his side and, with Headphones 3D, the far ear 0.5 to 0.9 ms later; then `tools/console.ps1 "mevr voices
   overheard off"` and the same lines again (the launcher once more) for `a1-off.wav`: 0 dB, 0.00 ms (the centre, as before).
   The head turned round flips the sign. A walk away past 10 m: `sound place:` names nobody, the voice back in the centre.
8. **A2, the player's gun**: on foot with a gun drawn (any career save on foot; the power arena below works too), `tools/
   console.ps1 "mevr voices census 10"` and fire within those 10 s (a replay with `-Inputs` of a recording that pulls the
   right trigger, or `tools/console.ps1 "StartFire" ... "StopFire"` if LE1's controller has that exec [verify]). Read the
   `voice census:` lines for a weapon-fire cue: **tagged `[the player's weapon]` and `placed for the player's gun`**: A2 works,
   and `sound place: a sound of the player's gun was placed at its muzzle: its audio component belongs to the weapon actor`
   is in the log; **tagged `[the player's pawn]`**: the shot is the pawn's, A2 needs the cue told apart (note the cue and
   group the census prints). Measured (only in the first case): `mevr-audio-loopback.exe a2-on.wav 10` while firing, `audio-
   balance.py` over the shots: on the fake headset the gun hangs where the animation holds it, right of the eyes, so the
   shots lean right (a few dB, the left ear later); `mevr voices gun off` and again: 0 dB, 0.00 ms.
9. **A4, combat under OpenAL Soft**: `tools/power-arena.ps1 -Row` (a row of soldiers ahead that fight; it opens UNC20 and
   autosaves into the career: `tools/power-arena.ps1 -Restore` after the game is closed). `tools/console.ps1 "mevr voices
   census 10"` during the fight: the soldiers' weapon, footstep and impact sources `placed by the game` (they are positioned
   at all: the first question of A4). Then a loopback of 20 s under a replayed head turning a full circle
   (`tools/make-look-recording.py`, `tools/replay.ps1 -Loop`): `audio-balance.py --window 0.5` shows the balance swinging
   from one side to the other as the head turns and the far ear's delay changing side with it; the same with `[Audio] spatial
   = game` (the menu's The game's own, then a restart): the balance swings, the delay stays 0.00 ms. Explosions: a grenade
   (`mevr powerwheel` or the arena's rocket soldiers) in the same recording.

**Run on the fake headset, 2026-11-11 02:30-02:42** (the merged build, `72fc6cb`):

- **6, passed** (the world mode was set in that run, so "without it" is only by the code's path: `EnsureVoicePlaceHook` on
  foot): OpenAL Soft opened with HRTF, `hooked FISACTSoundSource::Update ... (the census reads AudioComponent.SoundCue at
  +0x88 and CurrentUseSpatialization at +0x24C)`, `sound place: [Audio] overheard on ...`, and on foot `sound place: on foot:
  people near the player heard from where they stand`. The reflection rows agree (2316 of 2346, the 30 known vehicle ones off).
- **7, not reached:** the launcher's `sta20_garrus` fired Garrus's wheel conversation (`GarrusAfterReceiveNormandy`), not the
  argument (the plot decides which start runs). On foot at C-Sec afterwards, 3 people near were named, and a 15 s census
  listed 11 sources: ambient beds and the music, nobody speaking. A1 needs a scene with barks on foot.
- **8, not reached; what the census says about it:** the launcher's and the arena's Shepard carry no weapon the holsters can
  draw (`mevr weapon assault`: no assault rifle is carried), so the player's gun never fired. But every shot sound in the
  arena's fight (`snd_xmods.c_snd_assault_generic`, `c_generic_impacts`) is a one-shot with **no owner** (`no owner | ?`),
  placed by the game: if the player's own shot is played the same way, A2's Gun list (which names the weapon actor) never
  matches it, and the gun needs its cue picked out by name and place instead. The changelog's gun line is out until then.
- **9, the first part answered:** the fight's weapon fire, impacts and footsteps are all `placed by the game` (component
  spatialization 1), so OpenAL Soft renders them for two ears with no work of ours; the shields' cues and the rover's
  engine are in the centre. A 20 s loopback with the head turning a full circle (`look-circle.mevrrec`): the balance swings
  -1.7 dB at 3 s to +2.7 dB at 16 s as the row of soldiers passes each side, the whole recording +0.3 dB. The per-window
  delay and the same with `spatial = game` are not measured yet. The arena's saves were put back (identical to the backup).

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
- 2026-11-10, 16:10-16:30, **the first shot explained and fixed** ("The first shot: the director's near plane"). A
  conversation's camera sets `LocalPlayer.NearClipPlane` per shot (0.3 to 2.8 m against 0.1 m on foot): from the player's
  eyes that removed the floor underfoot, the player's body and whoever stood near, shot by shot (the owner's "assets near
  me disappear when the council talks"). The on-foot plane is now kept while a world view is held, and the game-camera
  hold (`worldCamera`) is off by default: it was built for this symptom, did not cure it, and with the Umbra eye-matrix
  lever culled the body. Two ordinary conversations now show arms, hands and chest on every line. **Open:** the body in
  the ceremony (the scripted walk and the podium after it). Mod built; the other builds and the tests not run; branch only.
- 2026-11-10, 17:00-18:00, **the body in the ceremony, found and fixed** ("The body that stayed culled"). The engine
  culls with the game's camera and Halcyon draws the eye 0.43 m behind it (the "view back" framing lives in the GPU
  matrices only): Umbra's frustum missed the body's tight box right under the eye at some head angles (facing the Council
  and looking down 50 degrees: never; 30 degrees aside: always). `engine/BodyBounds` pads the player's body boxes by the
  view offset and hands Umbra the padded box. The ceremony from its save, facing forward: arms, hands and chest through
  the scripted walk (the body walks with the player while Shepard's actor goes ahead) and at the podium on every line.
  Open: the legs' walk animation under a standing player, the on-foot look-around, a real headset.
- 2026-11-10, 18:10-18:30, **the legs, the walkway's blink, a stall** ("The legs under the player, the walkway's blink").
  Legs: the writer's own leg IK stands them under a player whose view rests and leaves the script's walk while the view
  moves. The blink: only for a change within 8 m (walking along the walkway: none). The host trusts the mod's state 1.5 s.
  Not judged from outside (the chest hides the legs from the eye); the rest of the list is named there with what it needs.
- 2026-11-10, 18:40-19:25, **who attends to whom** ("Who attends to whom"; the owner: when Udina speaks Anderson need not
  face him). `worldTurn = roles`: the speaker and the one spoken to look at each other, a bystander glances at the speaker
  only within 70 degrees and never turns its body, a body re-faces only for the player within 4 m and past 80 degrees and
  goes back afterwards; the pair's look-at chains reach further (`worldLookReach`). The game has the same rule built in,
  off in staged scenes. Ceremony: 0 body turns, 38 looks, Anderson's head 12 degrees short of the player at 77 (31 to 36
  before), Udina keeps his back to a speaker behind him. Two faults of the eased turn fixed (the pawn's physics turned it
  back; it stopped half way at 90 frames a second). No turn animation exists to borrow.
- 2026-11-10, 20:20-21:05, **the voices, placed** ("The voices, placed"; the owner: the Council like an intercom, other
  scenes spatial). A post-hook on the ISACT source's update sends a speaker's head as the source's place when the speaker
  is within 8 m; further voices stay the game's centred mix. Measured from the PC's output: Anderson 77 degrees to the
  right is 8 to 9 dB louder on the right (0 in the game's mix), on the left by as much with the head turned round, the
  Council stays centred; ISACT pans but does not attenuate, so the falloff (3 dB a doubling past 2 m, at most 6) is
  ours. No intercom colour yet (no filter in the dialogue path). Not heard by anyone.
- 2026-11-10, 21:10-21:30, **OpenAL Soft tried** ("Real 3D audio"; the owner asked for the test). In place of the game's
  Creative `OpenAL32.dll`, with HRTF on: Anderson's voice reaches the far ear 0.9 ms later (nothing in common between the
  ears before), mirrored with the head turned round; the Council unchanged. Merely added beside Creative's it is loaded but
  not used. The game folder is back as it was; whether to ship it is the owner's (licence, headphones only, unheard).
- 2026-11-10, 22:00-23:00, **OpenAL Soft as the mod ships it** (the owner: set it up, and installable for every player).
  Built from its pinned source release, installed in `ASI\audio` by Setup (no game file replaced; licences and the
  source in the release), and the host detours the router's 93 functions to it, all at once, at the start of attach
  (the import slots alone were not the game's route; one by one, the game won the race). The same 0.9 ms between the
  ears as with the DLL replaced. Everything the game positions now gets it; gunfire not measured in combat yet.
- 2026-11-10 23:30 to 2026-11-11 02:20, **people walk to their marks** ("People walking to their marks"; item 1). The
  game's AI walks a person to a mark the stage asks for once four holds are lifted for that walker: its AI enable
  counters, the conversation's ban on AI movement, its paused speed and, the one that mattered, the gesture mode, whose
  animation node plays gestures over the locomotion branch. Measured by the walker's ankles (30-70 units apart in turn at
  130 u/s) and seen in snapshots (Anderson mid-stride, then on his mark); five of six walks in the medical bay arrive within
  0.25 m. C-Sec's Wrex didn't walk: hidden in the test's plot state (the game runs no AI for him). `worldWalk = 1` is the
  world mode's default now.
- 2026-11-11, **the audio list, offline** (an agent in a worktree; no game): `engine/VoicePlace` takes three lists (the
  conversation's unchanged, people near the player, the player's gun) and a "centred sources only" voice; `game/SoundPlace`
  names the people within 8 m on foot (those heard first) and the held weapon at its muzzle; `mevr voices census` tells which
  actor owns a sound; the menu's Sound page sets `[Audio] spatial`, `gainDb`, `overheard`, `gun`. Read: the weapon's own audio
  component is the weapon actor's, `PlayWeaponSound` is a GUI sound, the shot's owner is [verify]; ISACT's AL source ids sit
  in its own OpenAL layer in the exe (a table of 256 voices, `m_uiSource` at +0xB4), reachable for an EFX band-pass. All three
  builds and the tests pass; the in-game checks are items 6 to 9 of "To check in the game".
- 2026-11-11 02:40-03:45, **groups composed, Wrex explained, steps kept** ("The composition of groups", "Why C-Sec's people didn't walk", "The validation saves, run"): seats face the group's middle, the squad beside the player, walk-ins to seats; walkers' AI switched on through the game's AbilityEnable (`engine/NameLookup`); C-Sec's Wrex was hidden in the test's plot state; steps back or aside aren't walked; a failed walk restores the gesture mode.
- 2026-11-11 03:50, **item 10 decided** ("Which scenes have no place for the player"): of 100 scenes seen in the world, the far-shots rule named two that play well in first person; the presence rules now send scenes to the 3D screen by default (`worldAutoScreen = on`), the far shots only log.
- 2026-11-11 04:00, **slot 84 read** (item 12): Wrex's scene starts from its save as his ambient line (its first starting entry needs conditional 3684, which the save doesn't set); the save generator needs the starting entries' conditionals.
- 2026-11-11 04:30, **the starting entries' plot flags** (item 12): an entry's conditional is a plot flag unless `bFireConditional`; the scan records it, the save generator sets the scene's entry flag; slots 84 and 85 got theirs (in-game check pending).
- 2026-11-11 04:20, **the starting flags tried in the game** (item 12): Wrex's flag 3684 made the level start Kirrahe's briefing on load; Chorban's 5316 changed nothing. Saves put back; the generator's starting flags are opt-in.
- 2026-11-11 04:33, **item 9: the handover stray fixed** ("The handover between two conversations"): the ending conversation's keep-in-place undid the next stage's placement of Shepard; it now keeps only for the live conversation.
- 2026-11-11 04:50, **item 11 sampled** ("Moments from saves, first sample"): 4 moments from new saves 90-93: the geth intro played in first person, Chellik's bump led into a conversation with a 1.73 m stray, two saves didn't come up on foot.
- 2026-11-11 05:23, **item 11, a moment on foot** ("A moment on foot"): Chellik's bump is a matinee with its own cameras in game mode 0 (third person, letterboxed in the eyes, and the conversation after it opened from that camera: the 1.6-1.8 m stray). The camera's view target now marks it; such a moment is held in first person from where the player stood, without the letterbox (a host seam). Saves 91/92 start as they load (runner fixed); Liara's 10 lines clean.
- 2026-11-11 05:42, **item 11, a moment after a conversation** ("A moment after a conversation"): Benezia's intro plays in first person after her conversation (the ended conversation leaves its held place as the place on foot; the host stays armed for the track's first move); the guide waits at the mark while a track holds Shepard (it had stepped in place for 12 s); the runner judges self-started scenes from the load and stops at GameOver.
- 2026-11-11 05:56, **item 13 narrowed** ("The subtitle of a line played as a bark"): a flowed scene's lines and the game's own barks are alike (text built, nothing drawn, the profile's subtitle option 0, the game's default); the headset question is what the owner sees with subtitles on.
- 2026-11-11 06:11, **items 1 and 7: people animated by gestures only** ("People animated by gestures only"): two seat walks arrived at Han Olar's in their reference pose (their tree has no walk); such people now keep their place, posed.
- 2026-11-11 06:26, **item 13 built** ("Subtitles for flowed lines"): this profile shows conversation subtitles, so flowed lines lost theirs; the mod hands each to the game's subtitle display in the bottom style, which reaches the HUD panel (seen); the game's mode put back after.
- 2026-11-11 06:58, **item 4 and 11: Virmire and the cockpit** ("Virmire and the cockpit"): Virmire's deployment and its scenes judged clean; the re-facing rule turned Joker in his seat 134 degrees: no re-facing while the player stands at Shepard's mark or for someone seated (`cw::MayReface`).
- 2026-11-11 07:13, **item 11, Therum** ("Virmire and the cockpit"): the troop drop held in first person, clean; the krogan's entrance on the screen by rule, her conversation in the world; 10 of the 21 moments seen.
- 2026-11-11 07:25, **item 1: `worldWalk = 3` measured** ("People walking to their marks", its table): the engine's walking moved nobody in four walks of the wake-up scene; each fell back to the slide. The AI's walk (`1`) is the only one that animates.
- 2026-11-11 08:05, **item 8 decided by the owner** ("Is the player's place ever necessary"): a small floor marker only where the player's place is necessary; nothing in the logs shows it is (37 walk-ins: 29 arrived, 2 blocked, no scene waited), so nothing is built.
- 2026-11-11 08:30, **item 7: seats for the pairs** ("Seats for the pairs"): people brought into view take the seats that let those who talk to each other look at each other; seen in Joker's cockpit scene; a refused seat is swapped.
- 2026-11-11 08:13, **item 11: the moment line names its matinee**: the Normandy's moment is the prologue's opening as `BIOA_PRO00` loads (a placement, on the 3D screen); the other prologue placements are the opening fly-by (`BIOA_GLO00_A_Opening_Flyby_CIN`), not one of the 21.
- 2026-11-11 08:47, **item 11: six more moments run** ("The scripted walks, named"): Noveria's scene after the squad falls (fixed: no flash of the screen while the script's black screen comes first, `cw::StepProvisional`), the double homicide (fixed: a cut faces Shepard's way, the view's offset turned), Feros's transition (screen) and first creeper (clean); the creeper from the Mako and the gambler not reached. 16 of 21 seen.
- 2026-11-11 09:11, **item 11: Eden Prime's train** ("The scripted walks, named"): from the John01 career's Eden Prime save; a ride now carries the view (`cw::IsRideStep`) and a moment's first move on foot is judged like any other. 17 of 21 seen.
- 2026-11-11 09:27, **item 11: the gambler** ("The scripted walks, named"): moments now carry their chunk's named streaming states (the gambler's `stream_schellsToss`); played clean from the Flux door. 18 of 21 seen; left: the ending's two (the owner's call) and the creeper from the Mako.
- 2026-11-11 09:38, **item 11: the creeper from the Mako** ("The scripted walks, named"): the runner puts Shepard in a vehicle first (`enterVehicle`); from the Mako the scene goes to the 3D screen (moments are held from on foot). 19 of 21 seen; left: the ending's two (the owner's call).
- 2026-11-11 09:53, **item 7: no seat walk in two more scenes** (the list's item 7): openings without a cut rarely leave someone out of view within 8 m; the medical bay's mid-scene AI walks arrived again.
- 2026-11-11 10:15, **item 6: released as experimental** (the owner): the menu says First person (experimental); the ending's two scripted walks approved to run; a Codex adversarial review of 7c89c3a..HEAD started.
- 2026-11-11 11:02, **item 11: the ending** ("The scripted walks, named"): its first scripted walk held in first person after the Council's conversation; 20 of 21 seen (the second is the other ending's branch).
- 2026-11-11 11:05, **the Codex review** ("An adversarial review by a second model"): eleven findings, all real on checking; all fixed (walk retarget and gesture mode, the guide's wait, the handover's place on foot, the flowed subtitle mode, a crowd's speakers, three tooling faults, `mevr vslot`, a test bound).
- 2026-11-11 11:15, **the saves and their tests** ("The saves and their tests"): `tools/save-dictionary.py` maps every save to the scene it was written for (or the scenes near it), with the command that plays it.
- 2026-11-11 11:40, **the second Codex pass** ("An adversarial review by a second model"): five more findings on the fixes, all fixed (the handover when Shepard can't be moved, gestures at teardown, a moment judged by its own matinee, a cinematic on load, subtitles switched off).
- 2026-11-11 11:33, **the handover fix seen, item 7's seat walk seen arriving** ("The handover, run again"): Anderson's conversation opened as a 4.2 m walk-in after the comm room, no cut; Nihlus walked 2 m into a seat by the AI and arrived.
- 2026-11-11 11:50, **item 12: coverage counted** (the list's item 12): 108 of 290 reachable scenes played in some run; the next sampled pass (52 scenes, 14 levels) is ready for after the owner's audit.
- 2026-11-11 12:01, **item 11 done: the ending's other branch** ("The scripted walks, named"): the final cutscene's `Interp_2` takes conditional F1289 (plot integer 46 over 47); from a save with 46 = 18 it played held in first person, 16.9 s, one dip at the cut 203 m into the tower; all 21 scripted walks seen. The platform under Shepard is pure black from his eyes (not the near plane, not culling: measured), [verify] in the headset.
- 2026-11-11 14:30, **item 12: the coverage pass from saves** ("The coverage pass from saves"): 16 conversations played in the world and 7 bark scenes heard (131 of 290 now); a Halcyon fault fixed (its first-person table full after 16 loads without the pause menu: the third-person camera), seen fixed after 17 loads; items 15-17 new.
- 2026-11-11 15:20, **items 15 and 16 done** ("Hackett's call and the surveyor, run again"): the flash after Hackett's call fixed (the console's use holds through the call it plays; seen twice through the real path), a refused put-back walks by the view instead of a cut; the surveyor's torso was the fake headset without controllers.
- 2026-11-11 16:10, **item 12: the third sampled pass** ("The third sampled pass"): 17 conversations in the world and 6 bark scenes (154 of 290 now); the transmissions' end flash was the harness firing them directly (through the console's use: none); item 18 new (openings from a view at Shepard's hips, generated saves).
- 2026-11-11 17:20, **item 18 done: the camera that fell to Shepard's knees** ("The camera that fell to Shepard's knees"): the game's camera collision pulled the first-person camera to its hook (the view behind his knees) whenever something stood just ahead of his face; off in first person on foot now (`game/CameraCollision`), seen at the three spots.
- 2026-11-11 17:55, **item 12: the fourth sampled pass** ("The fourth sampled pass"): 14 conversations in the world and 4 bark scenes (172 of 290 now); every opening from the first-person eye after the camera-collision fix; nothing new found.
- 2026-11-11 18:05, **A4 done** ("A4: combat for two ears, measured"): the fight under OpenAL Soft has a real delay between the ears (to 0.83 ms, following the head), the game's own audio pans by level only (0.00 ms); A/B with a restart.
- 2026-11-11 18:12, **item 17 done: uncharted worlds from a save** ("Uncharted worlds from a save"): plot 5470 "Has the Rover Landed?" decides the landing; the generator sets it; both asteroid scenes loaded on foot and played.
- 2026-11-11 18:45, **item 12: the fifth sampled pass** (one untried scene per chunk): 15 conversations in the world and 3 bark scenes (190 of 290 now), every opening a stay or a walk-in from the eye, nothing new found.
- 2026-11-11 23:00, **item 19: the hearing that went third person** (the owner's first slot-80 run in the headset): a hidden Shepard's unposed head bone "jumped" as the hearing opened and the view followed it to 4 m behind him; fixed (`cw::HeadFollowFor`), [verify] in the headset.
- 2026-11-11 23:20, **items 20-22 from the owner's slot-88 run** ("Kirrahe's camp"): Kirrahe's scene wanted in first person; the game's black screen on foot shows as a black panel; slot 88 lacked plot 3039, so Kirrahe's arrival content played and the speech never started after the planning talk (save fixed).
- 2026-11-11 23:40, **items 23-27 from the owner's session** ("The owner's session, judged"): the wheel's band fixed; judder at the director's cuts measured; the right stick in conversations, tests that walk with the stick, and a dip for scenes that relocate their cast to build.
- 2026-11-11 23:50, **item 28: the conversational circle** (the owner's design): built and unit-tested; the guided walk, the opening-time seats and the stronger look-at chain off by default; first run on the fake headset under way.
- 2026-11-12 00:30, **item 28, second design** (the owner's answers): built and validated on the fake headset for eight scenes; the squad's formation off by default (the AI walk stalls or follows Shepard's pawn); the staged list for the owner's review.
- 2026-11-12 09:45, **item 28, the squad's formation held** (put back before the scene is drawn, each on its own side, the squad's own HOLD formation written quietly, placed under a cut's dip): validated on the fake headset, on by default; **item 29, stances from above** built (`mevr convo stance`, `tools/conversation-stance.py`, the runner's `-Stances` that ends a scene once nothing will move).
- 2026-11-12 17:45, **item 30, heads that look past the player** (the owner's question): measured with head directions in the stances (Girard looked at Shepard's body left on his mark), fixed with a look-at target at the player's eyes (`worldLookEyes`); the squad saves from the owner's Citadel save (`%LOCALAPPDATA%\MEVR\squad-saves`).
- 2026-11-12 18:20, **item 31, Virmire's start from above**: the stances follow a whole scene; a staged scene's moves under the script's black screen are taken at once; Shepard is walked to his new mark by a simulated left stick (the owner), never moved.
- 2026-11-12 19:35, **item 32, heads at the drawn eyes** (the owner: an audit of every kind of conversation for heads facing Shepard's hidden body): the player is where the host draws the eyes, 0.43 m behind the held place; staged openings put the eyes 0.34-0.37 m behind his head (Virmire, the Council), the right stick's turn swung them 0.85 m, the eyes stand-in, re-face rule, circle and formation took the held place, and a stage's facing at the hidden body in a walk-in by the view: all fixed and run again on the fake headset (`--audit`).
- 2026-11-12 20:30, **item 32, the moments audited**: stance records and the eyes stand-in in scripted walks (nobody there looks at Shepard through the look-at system: the faces are the matinees'); Benezia's cut after phase 2 placed the eyes with a stale view offset (0.74 m behind his head): the last first-person view's offset and a check after the opening; `tools/fake-headset-session.ps1`, `tools/land-on-main.py` and the skills `fake-headset-run`, `first-person-scene-audit` (the owner: author skills that make this process more efficient).
- 2026-11-12 21:50, **item 33, walking along** built (a scripted walk with someone beside Shepard waits for a player 3 m behind); **item 34, auditing scenes by rule** built (rules over the records, a baseline, a regression set, the rules live in every scene).
- 2026-11-12 23:10, **items 33 and 34 in the game** (three passes on the fake headset): the walks wait for a player who stands still; a 4 m wall in every scripted walk (the conversation's boundary) fixed; the live audit's two false alarms fixed; a sidestep stuck against a squadmate fixed; a crash of ours at a load (the pawn list) fixed; the baseline written (12 scenes). Open: the stick's 0.9 m/s against the walks' 1.2-1.3.
- 2026-11-13 01:00, **item 33, the walk's pace**: the conversation and scripted-walk walk 1.4 m/s again (the owner); the Spectre's walk waits only on its stairs (5.4 s, from 11.9). The on-foot walk linear to the stick: research/38 "Linear walking".
- 2026-11-13 11:20, **item 35, the body that stayed at the mark** (the owner's council hearing: the body up to 5.7 m away for 225
  of 227 s): every conversation in the world keeps the body with the player, the player is placed by Shepard's animated head, a
  stick pushed through a cut waits to be let go; the audits replay tracked hands, and a body away from the eyes is never excused
  by a baseline. Fake headset: 3.85 m before, 0.11 m after. Item 36 opened: the council's level is loaded but hidden during the
  walk with Anderson.
- 2026-11-13 13:10, **item 36, the council during the walk**: the hearing's level and four of its people (Udina, the councillors)
  shown as the walk with Anderson begins (`cw::ShownEarlyFor`, `worldShowEarly`); the hearing still begins at the walk's end.
- 2026-11-13 15:30, **item 37, seeing the body in the audit**: the drawn hands against the controllers (`arm_ik::MeasureHands`,
  `hands-off-controllers`), the audit's hands held up in view, snapshots on a contact sheet, what each scene did not measure,
  per-scene expectations, and broken runs the audit must catch (all three caught once item 35's switch undid its whole fix).
- 2026-11-13 17:35, **item 24, frame timing at the director's cuts**: measured on the fake headset (the hearing, 99 cuts,
  `worldCamera` 0 and 1): no stall at a cut, the same GPU work after a cut as elsewhere, `worldCamera = 1` no different; the
  fake headset's 31.9 ms pacing hides game-thread stalls under about 20 ms; next a capture in the headset.
- 2026-11-13 19:00, **item 38, obvious bugs, measured**: drawn meshes (LastRenderTime), the drawn near plane, the stick through
  the wheel's latch (`mevr convo stick`), snapshot pictures, the black screen on foot, the scene a save played; five broken runs
  caught in one game; the set found a near-black frame in the Spectre walk, a 3D-screen flash into Opold's shop and Anderson
  0.2 m from the eyes; the fake headset's 31.3 fps cap is neither the window's cover nor the display's sleep.
