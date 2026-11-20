<!-- SPDX-License-Identifier: MIT -->
# People leaving a first-person conversation: the walk away instead of the pop (2026-11-19/20, lane EXIT)

**Why.** The owner, 2026-11-19: "Do we have any logics/rules for people walking away after they would exit a conversation?
Like udina in the post chat with Anderson and shepherd. He just disappears in the current system." We had none: ME1's
first-person rules (research/59, research/69) handle people arriving (walks to marks, shown early, the circle, kept in place),
not people leaving. On the 3D screen or in third person a camera cut hides the script's removal; in first person the player
sees the person pop out. The owner approved this rule (2026-11-19, "Your recommendation sounds like what I was thinking"), for
all three games as one shared system:

1. When the game is about to hide, move away or remove a person of the conversation while the player can see them, the change
   is held back; the person turns and walks away with the game's own walking toward somewhere out of the player's sight.
2. The game's own change is applied the moment they are out of view, so the plot and later scenes are unaffected.
3. No walk: the change happens when the player looks away; after a few seconds at most, under a short dip from black.
4. Squadmates, and anyone the next scene needs at once, are left alone.
5. Only in the first-person mode (`[Cinematics] conversations = world`, experimental, off by default). Within it the walk-away
   is the default: `[Cinematics] worldExit = walk | hold | off`.

The owner's headset notes bound it as every scene rule: never move or freeze the player unasked; no blink with nothing
changing; the authored animation kept; people walk with the game's own AI and animation, never slide.

This note is the implementation guide: what each game does to remove people (sections 1-3, counted), the shared rule
(section 4), ME1's side (section 5), how it was checked (section 6), what the sequels need (section 7) and the owner's
questions (section 8).

## 1. How ME1 removes people at and after a conversation (read from the packages)

**Tool.** A scratch C# scanner over Legendary Explorer's library (kept in `scratch/exit/scan`, gitignored; built like
`tools/conversation-scan`, whose code it borrows, without editing it). For every conversation start (`BioSeqAct_StartConversation`,
ME2's `SFXSeqAct_StartConversation` too) it walks the level script from the start's `Out`, `Failed` and `Finished` outputs as the
script runs (outputs, into sub-sequences through their activation events, out of them at a `SeqAct_FinishSequence`, remote events
to their listeners, delays added up, a `BioSeqAct_BlackScreen` turning black on noted), up to the next conversation start, and
lists every action that can remove or move an actor, with its targets resolved to the actor and its `Tag`. It does the same from
each line's `BioSeqEvt_ConvNode` (the line's own Kismet, in the `_LOC_INT` packages' `<conv>_N.Node_Data_Sequence`). A target is
"one of the conversation's people" when its tag is in the conversation's `m_SpeakerList` or is the start's owner; `hench_*` tags
are squad by tag (the game side uses the real party). Its output is game data and stays in `scratch/`.

**Udina's case, exactly.** `BIOA_STA70_02council_DSG`, sequence `SarenCouncil`: `BioSeqAct_StartConversation_1`
(`sta90_captain_dlg`, comment "Post discussion with captain and ambassador", owner `BioPawn_9` = Anderson `sta90_captain`)
-> `Out` -> **`BioSeqAct_ModifyPropertyPawn_4` "Deactivate the ambassador"** (Target `BioPawn_7`, tag `sta90_ambassador`; its
`Active` row a `SeqVar_Bool` false) -> `SequenceReference_3` (the squad's first two members) -> `BioSeqAct_OverrideStagePlacement_3`
-> `BioSeqAct_StartConversation_2` (`sta20_captain_dlg`, "Final chat with the captain"). No delay, no black screen: Udina is
deactivated in the tick the conversation ends and Anderson's final chat starts at once. After that chat: `BioSeqAct_ModifyPropertyPawn_2`
"Deactivate the captain" (Anderson) and `BioSeqAct_SetStreamingState_5` "Stream out this layer" (`stream_council`).

**What ME1 does, counted** (763 starts of 491 conversations; [verify] the counts are reachability in the script, not every path
a play takes: a plot check on the way decides, as Chorban's showed in the game, section 6):

| After a conversation ends (its outputs) | in the clear | under a black screen turning on in the same tick |
|---|---|---|
| deactivated (`BioSeqAct_ModifyPropertyPawn`, `Active` false): one of the conversation's people | **61 (50 conversations)** | 13 (13) |
| ... someone else (extras, the conversation's double) | 31 (22) | 7 (5) |
| ... a squadmate by tag | 23 (11) | 5 (3) |
| teleported (`SeqAct_Teleport`, `BioSeqAct_TeleportToObject`): one of the people | 8 (8) | 0 |
| ... someone else (mostly the player, and staging) | 232 (61) | 37 (22) |
| hidden (`SeqAct_ToggleHidden`): props, never a person | 7 (7) | 2 (2) |
| destroyed (`SeqAct_Destroy`): one of the people (`jug70_sal_prisoner`) | 1 (1) | 0 |
| a streaming state changed (`BioSeqAct_SetStreamingState`) | 85 (27) | 53 (21) |

| During a line (its `BioSeqEvt_ConvNode`) | | |
|---|---|---|
| a matinee moving an actor (`InterpTrackMove`): authored movement, kept | 500 | 68 |
| deactivated (`ModifyPropertyPawn` `Active` false; targets bound at run time, e.g. Avina "set inactive") | 97 | 29 |
| destroyed | 22 | 7 |
| teleported / hidden | 9 / 5 | 2 / 4 |

So ME1 removes people almost only one way: **the deactivation**, a Kismet `BioSeqAct_ModifyPropertyPawn` whose `Active` row is
false (50 conversations end so in the clear with one of their people in it, 13 more under a black screen turning on in the same
tick; Udina, Chorban, Wrex at Fist's, Anderson after the final chat).
Teleports of a conversation person (8: Morlan "jumped back a bit", the bouncer "returned to first position", Kirrahe) are the
second way. Hides and destroys of people are rare. A streaming change is a level's part going out or in.

**What the deactivation does (Ghidra, LE1 2.0.0.48602; names only, no code).** `BioSeqAct_ModifyPropertyPawn` is a
`BioSeqAct_InteractProperty`: its class defaults map each row name to an object or behaviour property or function
(`aObjectProperties`, `aObjectFunctions`, `aBehaviorProperties`, `aBehaviorFunctions`). Its native `Activated` (`0xA7CED0`, the
class's vtable `0x124ECB0`, slot `0x408`; `USequenceAction::Activated`, `0x5188D0`, in the same slot of `SequenceAction`'s
vtable `0x105DAB0`) walks the rows; a behaviour-function row (type 3, "Active") is `FUN_140A818E0`, which reads the row's
variable and calls the function on the pawn's behaviour through **its vtable slot `0x250`, `UObject::ProcessEvent`
(`0x12DD20`)** with the parameters. The function is `SFXGame.BioActorBehavior.SetActive(bSetActive)`, a native with iNative 0:
`UBioActorBehavior::execSetActive` (`0xBAAB60`, folded with `BioArtPlaceableBehavior`'s) reads the bool and calls the
behaviour's C++ `SetActive` (its vtable slot `0x4D0`). ProcessEvent builds the native's own frame: its `Node` is the function,
its `Locals` the parameters. **So the deactivation can be held at `execSetActive` when the frame's `Node` is `SetActive`, and
made again later by the same ProcessEvent call.** Script callers of `SetActive` (another frame's `Node`) are never touched.

## 2. How ME2 removes people (scanned the same way; nothing run)

1,041 starts of 913 conversations (`BioSeqAct_StartConversation` and ME2's own `SFXSeqAct_StartConversation`):

| After a conversation ends | removals | conversations |
|---|---|---|
| deactivated (`BioSeqAct_ModifyPropertyPawn` `Active` false): one of the people | 74 | 64 |
| ... someone else | 65 | 60 |
| teleported: one of the people | 27 | 22 |
| hidden (`SeqAct_ToggleHidden`): one of the people | 3 | 3 |
| destroyed: one of the people | 3 | 2 |
| a streaming state changed | 569 | 131 |

During lines: 5,328 actor move tracks in 372 conversations (ME2's walking gestures and moves, research/le2-conversations-in-world.md),
231 `SeqAct_ToggleHidden`, 141 `ModifyPropertyPawn`. **ME2 keeps ME1's mechanism** (the same Kismet action and row), so the same
hold applies through ME2's `execSetActive`; ME2 changes streaming states about four times as often around conversations.

## 3. How ME3 removes people (scanned the same way; nothing run)

1,286 starts of 1,304 conversations. **ME3 has no `ModifyPropertyPawn`**: after a conversation ends, people are teleported (18 of
the people's, 21 squad), hidden (`SeqAct_ToggleHidden`: 2 of the people's), destroyed or switched with ME3's own actions
(`BioSeqAct_SetActive`, `SFXSeqAct_ToggleCombatPawn`); streaming states change 422 times in 126 conversations. During lines,
773 line matinees in 147 conversations have an **`InterpTrackVisibility`** (a track that hides or shows an actor at a key), a
mechanism ME1 does not use for people; and 12,284 move tracks in 443 conversations.

(The v2 counts with ME3's `BioSeqAct_SetActive` and the black screens on the same tick are in section 7.)

## 4. The rule (shared core, `src/core/vr/ConversationExit.{hpp,cpp}`, `tests/ConversationExitTests.cpp`)

Pure functions and data, no engine; each game's adapter gives the facts.

- `Mode`, `ParseMode`, `Name`: `[Cinematics] worldExit` = `walk` (default; `on`, `2`), `hold` (`1`: the look-away alone), `off`
  (`0`: the game's change at once).
- `Kind`: `Deactivate`, `Hide`, `Teleport` (held); `Destroy`, `StreamOut` (never held: nothing could give a destroyed actor
  back, and a streaming change may be what the next area waits on).
- `Classify(kind, subject, context)`: held only when the first-person mode is on and worldExit not off, the subject is one of the
  scene's people (speaker, owner, listener, the speaker list: now, or within `windowS` 2 s of a scene shown in the world), not the
  player, not a squadmate, not needed by a scene that began since, and **seen**: inside the view and in sight of the drawn eyes.
  Out of sight the change goes through at once (nobody sees it).
- `CandidatePoints(person, eyes)`: 12 bearings in 30-degree steps starting straight away from the eyes, at 3, 7 and 11 m (12 m
  until run 3: the engine's straight-walk limit is 12 m, section 6).
  `PickDestination(candidates, person, eyes)`: where the script sends them if the way there is clear (a teleport's destination:
  the walk is its move); else a candidate with a clear way in the same space (floor within 0.6 m), hidden from the eyes (head and
  body lines blocked), not toward the player, the most away from the eyes and the nearest among equals; else nowhere.
- `Step(held, facts)`: `Apply` when the player sees no world in first person (a screen, a load, the 3D screen), or the person has
  been out of sight 0.25 s; `ApplyUnderDip` after 25 s held in all, or once no walk is under way (refused, stalled, arrived in view,
  or still in view after `walkMaxS` 12 s) and the player kept looking for `lookAwayS` 3 s; else `Keep`.
- `IsSwap(heldAt, shownAt, sinceHold)`: someone the script shows within 2 m of a held person within 1 s of the hold is one actor
  swapped for another (Benezia's conversation pawn out, her combat pawn in): the held change is applied at once.
- `VanishedInView(seenBefore, presentBefore, presentNow, dipped)`: the live audit's rule.

## 5. ME1's side

| File | What |
|---|---|
| `src/engine/ExitHooks.{hpp,cpp}` | The pre-hook on `UBioActorBehavior::execSetActive` (`engine/Addresses.hpp` `kBehaviorExecSetActive` `0xBAAB60`, its pattern the whole thunk up to its call through slot `0x4D0`: every bool-taking exec thunk begins alike). Asks the provider only for a call whose frame's `Node` is `SetActive` (ProcessEvent's own frame) and passes it the frame's `Locals`; withheld, the native is not run. `Totals()`: calls, through ProcessEvent, withheld. |
| `src/game/ConversationExit.{hpp,cpp}` | The adapter: the scene's people (`LiveConversation()`'s `m_pSpeaker`, `m_pListener`, `m_pConversationOwner`, `m_SpeakerList[].aSpeaker`, each with both behaviour pointers, `Actor.oBioComponent` and `BioPawn.m_oBehavior`), what the drawn eyes see each tick (`view_anchor::Current()`'s `headWorld` and head pose: inside 51 x 48 degrees, and `Actor.FastTrace` to the head or the body), the provider (hold or let through, each decision logged), the walk (`game/AiWalk`), the rule's `Step` per held change, the apply, the live audit. |
| `src/game/AiWalk` (lane FPC's) | The walk: `Start` / `Step` / `End`, every hold given back. `namespace walk = ai_walk;` in ConversationExit.cpp is the one line to change if its interface moves. |
| `src/game/ConversationWorld.{hpp,cpp}` | Two small exports: `DipFromBlack()` (`++gC.cut`, the counter the host dips on) and `CutCount()`; the on-foot "armed" word now carries the same counter (`Arm`: `state.cut`). |
| `src/core/vr/ConversationWorld.{hpp,cpp}` | `FootCut()`: the on-foot word's counter, for the host. |
| `src/plugin/TransitionFade.cpp` | LE1's fade seam dips on the on-foot counter too (a scene's, else `FootCut()`): a removal under a dip on foot. An older mod sends 0 there: no dip, ignored. |
| `src/mod/Mod.cpp` | `ai_walk::TryStartup()` (lane FPC's helper's movement-ban answer, before the CallFunction hook), `conversation_exit::TryStartup()`, `EnsureExitHook()` (the hook once wanted; the CallFunction hook and the name lookup for walks), `Tick` after `conversation_world::Tick`, `Shutdown` before it, the hook's uninstall, the snapshot's `"mod"."conversationExit"`. |
| `src/game/ConsoleRequest.cpp` | `mevr convo exit [on | walk | hold | off | auto | status | shots on|off | trace on|off | test <tag>]` (`shots`: snapshots at each hold and apply; `trace`: the AI's call trace for 4 s as each walk away begins; `test`: the walk away of anyone now, without a hold, 8 s at most). |
| `src/engine/ReflectCheck.cpp` | Rows for `SFXGame.BioActorBehavior.SetActive`, its `bSetActive` and ParmsSize, `Engine.Controller.PointReachable`, its `aPoint`, ReturnValue and ParmsSize, the class `Engine.Level` (the other names had rows). |
| `tools/scene_audit.py`, `tools/audit-report.py`, `tests/SceneAuditTests.py` | The offline rule `vanished-in-view` (someone of the scene in sight in one stance record, gone or hidden in the next with no dip between; never excused by a baseline) and the live one in `FAIL_RULES`. |

**The held change.** In the provider (inside the level script's update, the game thread): a `SetActive(false)` for the behaviour
of one of the scene's people, classified as held, is withheld and kept with the pawn and the behaviour (`ue3::Kept`), where it
stood and its rotation. `SetActive(true)` for a held behaviour (the script shows them again) applies the held one first, then
lets the new one through, in the script's order. `SetActive(true)` for anyone else while something is held is checked for a swap
in the tick. Our own call (`gApplying`) always passes.

**The walk.** In `walk` mode, the first tick with the player seeing the world: `Destination` traces the core's candidate points
(`Actor.Trace` down for the floor; `FastTrace` at knee and chest height for the way; from the eyes to the spot's head and body
for sight; `Engine.Controller.PointReachable` on the person's AI for each clear one: only places the AI walks to, section 6
run 3) and `PickDestination` chooses; `ai_walk::Start` walks them there (its refusal logged: then the look-away). Each tick
`ai_walk::Step`: arrived or stalled ends the walk (the look-away's wait begins; a stall under 0.5 m from where they stood asks one
other place first); a walk still in view after 12 s too.

**The apply** (`Apply`): the walk ended (every hold given back), the dip asked for when the rule says so
(`conversation_world::DipFromBlack()`: in a scene the scene's cut, on foot the "armed" word's), `BioActorBehavior.SetActive(false)`
through ProcessEvent on the kept behaviour (`ue3::runtime::Call`, our flag up), then, if the pawn is hidden as the script left it
and it walked more than 20 units, `Actor.SetLocation` and `SetRotation` back to where it stood (a later scene finds it where the
level put it). A behaviour no longer alive (a load took it) is dropped with a log line.

**Never lost.** Applied at once: the first-person mode or worldExit switched off; the person's own level streamed out (or on its way
out: `LevelGoing`, section 6 run 4); the player seeing no world in first person for
0.5 s (a load's screens, a GUI screen, the 3D screen, a cutscene); a scene that began since needs them; the mod's unload or hot
reload (`Shutdown`, not at process exit). [verify] a quicksave during a hold (it would save the person active; the hold lasts
at most 25 s, and the pause menu is a screen, which applies it first).

**Logs** `convo exit:` (each decision with why: `the script deactivates X in the player's view ...: held back`, `... let through
(why)`, `X walks away N m ...`, `X can't walk away: ...`, `X's deactivation applied after N s: why ...; hidden now yes, put back
where it stood`), the live audit's `convo: audit: vanished-in-view: ...`. **Snapshot** `"mod"."conversationExit"` (mode, held,
totals, the hook's counts). **Console** `mevr convo exit [on|walk|hold|off|auto|status]`.

## 6. Checked in the game (fake headset)

Runs through `tools/fake-headset-session.ps1 -BuildDir build-trilogy` under the run lock, scenes from John02's validation
saves (`tools/conversation-from-save.ps1 -All -Career John02 -List %LOCALAPPDATA%\MEVR\save-dictionary-list-John02.json
-FirstSlot 80 -Only <slot> -Pace full -Stances -Lines 4`, the audit set's hands replayed, body scale 1.185). Session folders
`%LOCALAPPDATA%\MEVR\sessions\2026-11-20_00-35-19-exit1` and `..._00-48-*-exit2`.

**Run 1 (2026-11-20 00:35-00:41), the first build:**

1. The hook went in (`hooked UBioActorBehavior::execSetActive`) and saw every `SetActive` the level scripts made through
   ProcessEvent (38 in the run, the frame's `Node` matched each time).
2. **Chorban (slot 85)**: no deactivation came: his conversation's `Deactivate Chorban` sits behind a plot check the runner's
   reply (I'm going) doesn't pass (he was still the use target 3 s after). Not an exit fixture.
3. **Udina (slot 80, the hearing played to its end)**: as `sta90_captain_dlg` ended, `the script deactivates BioPawn_7
   (sta90_ambassador) in the player's view (2.1 m from the eyes): held back`; then `can't walk away: nowhere out of sight within
   3-12 m with a clear way (22 of 36 points with a way, 0 hidden)` (the council chamber is open); Anderson's final chat
   (`sta20_captain_dlg`) began in the same tick with Udina still standing; 3.0 s later, the player still looking (the fake
   headset's head does not turn), `deactivation applied after 3.0 s: the player kept looking, under a dip from black; hidden now
   yes` (`transition fade: the world -> the world: from black over 0.35 s`). The same for Anderson after his final chat.
4. Found and fixed for run 2: (a) the hearing's end deactivates the player's own pawn twice and the live audit took it for a pop
   (the player is no longer one of "the people"); (b) two ops deactivate the hearing's Anderson: two holds of one person (one
   hold now); (c) the councillors and Saren's hologram were deactivated in the same tick the script's black screen turned on, and
   were held 0.3 s (a black screen now lets them through: `Context::underBlack`, `authored_moment::LastBlackScreenOnMs` within
   1.5 s); (d) with no hidden place the person stood still for the 3 s: now they walk straight away (`PickDestination`'s second
   tier); (e) Saren's hologram went off 12 s before the end by another route (no deactivation: a line's own), and the audit
   failed on it: such vanishes are now noticed and counted (`vanishedOther`), the failing rule kept for deactivations the player
   saw go through.

**Run 2 (2026-11-20 00:48-00:56), with those fixes:**

1. **Wrex at Fist's (slot 87)**: the `Deactivate Wrex` at the end is the hidden gameplay Wrex (the framed scene shows its own
   doubles: the stance records have Wrex and the bouncer `hidden`): `let through (out of the player's sight: at once)`, right. Not
   an exit fixture either. (The runner's `-Before` lines did not run for this save's walk-in start, so its "worldExit off" pass
   was a second plain pass.)
2. **The hearing's end**: the hearing's own Anderson (the walk's, `BIOA_STA70_02captain_DSG` `BioPawn_9`) is deactivated about 8 s
   before the hearing ends, beside the player on the podium: held, no way off the podium within 12 m (1 of 36 points), applied
   under a dip 3.0 s later. The councillors and Saren's hologram, deactivated in the tick the hearing's black screen turned on:
   `let through (the game's own black screen covers it: at once)`. Saren's hologram going off earlier: `noticed ... by another
   route`, no audit failure.
3. **Udina**: at `sta90_captain_dlg`'s end `held back` (2.1 m from the eyes); `walks away 12.0 m straight away from the player
   (nowhere out of sight within reach: it leaves the view) with the game's AI (22 of 36 points with a way, 0 hidden)`; **the
   game's AI did not move him** (`stalled after 0.00 m` at 2 s); the look-away's wait, then `applied after 5.1 s: the player kept
   looking, under a dip from black; hidden now yes`. Anderson after his final chat the same (5.2 s). Snapshots
   `%LOCALAPPDATA%\MEVR\snapshots\2026-11-20_00-55-28-801-left.png` and `..._00-55-30-793-left.png` (Udina and Anderson on the
   tower's plaza during Anderson's final chat, Udina held), `..._00-55-32-855-left.png` (the dip).

**Run 3 (2026-11-20 01:03-), why the AI did not walk Udina** (`mevr convo exit trace on`: the AI's call trace for 4 s at
each walk away; `mevr convo exit test <tag>`: the walk away of anyone now, without a hold):

1. **On foot the walk works**: after Chorban's scene, `mevr convo exit test sta20_chorban` picked a place 12 m straight away
   (30 of 36 points with a way, 2 hidden) and `game/AiWalk` walked him there with the game's own walk: 0.23 m after 1 s, 1.51,
   2.74, ... 7.01 m after 8 s (about 0.9 m/s once under way), the trace `BioAiController.PushMoveToPoint` ->
   `BioSeqAct_MoveToPoint.InitState`, `GetNextPredictedMoveLocation` every tick.
2. **At the council the AI gives the move up in its first tick**: Udina held at `sta90_captain_dlg`'s end, `ai walk: ... walks
   12.00 m to (-9443, -8982, 62126)`, and the trace in the same tick: `PushMoveToPoint`, `BioSeqAct_MoveToPoint.InitState`,
   `CanUseStack`, `GetNextPredictedMoveLocation` x7, **`BioAiController.HandleTeleport`, `PopState`, `Cleanup`, `PoppedState`**;
   `stalled after 0.00 m` 2 s later, applied under the look-away's dip at 5.1 s. Anderson after his final chat the same (a place
   12 m off, 24 of 36 points with a way, 3 hidden; the same trace).
3. **Why** (the state's script, read with `tools/conversation-scan <CookedPCConsole> <out> --script BioAiController.BioSeqAct_MoveToPoint`
   and `--script BioAiController.HandleTeleport`; the game's code stays out of the repository, this is what it does): the
   `BioSeqAct_MoveToPoint` state walks straight to the point when `PointReachable(point + 5 up)` says yes, else along
   `FindPathTo`'s route; with neither (no route, the point not reachable) it asks `HandleTeleport` (which teleports only a pawn
   marked to teleport on failure: none of these), sets the completion reason "failed" and pops itself. So it was never the
   conversation mode holding Udina still: the 12 m point was not reachable for the AI there, and the chamber's plaza has no
   route it found to it. The engine's `PointReachable` also refuses any point flat-farther than its longest straight walk (1200
   units, UE3's `MAXPATHDIST` [verify] that LE1 keeps it), and every 12 m candidate sat exactly on that limit.
4. **The fix** (built after run 3): `Destination` asks the game's own `Engine.Controller.PointReachable` (the person's AI
   controller, `Engine.Pawn.Controller`; a simulated walk from where they stand, the state's own point 5 units up) for every
   candidate with a clear way, and only those it admits are picked (logged: `N clear but not reachable for the AI`); the far ring
   is 11 m (`Limits::maxUnits` 1100, inside the 1200-unit limit); a walk the AI still gives up under 0.5 m from where it stood is
   asked once more to another place (`refused`, 1.5 m around the first kept out), then the look-away. The run-3 guess that the
   conversation mode held him (wait for the player on foot) was dropped unbuilt: the trace refutes it.

**Run 4 (2026-11-20 01:28-01:38), with the reachability check** (session `..._01-28-48-exit4`):

1. **Udina walks off.** At `sta90_captain_dlg`'s end: held (2.1 m from the eyes); `walks away 11.0 m straight away from the player
   ... (22 of 36 points with a way, 1 hidden, 3 clear but not reachable for the AI)`; the game's AI walked him (no
   `HandleTeleport` in the trace), and 2.2 s later, 2.24 m along, he had left the view at its left edge: `applied after 2.2 s: out
   of the player's sight ...; hidden now yes, put back where it stood`. No dip. Snapshots
   `%LOCALAPPDATA%\MEVR\snapshots\2026-11-20_01-35-14-594-left.png` (Anderson's final chat open, Udina mid-stride at the view's
   left edge, walking away) and `..._01-35-15-599-left.png` (Udina gone from the view, Anderson's chat on).
2. **The hearing's Anderson on the podium**: 2 of 36 points with a way, none away from the player: no walk, applied under the
   look-away's dip after 3.0 s, as in run 2 (the podium is a dead end within 11 m).
3. **Anderson after his final chat** showed two things. (a) The walk began in the tick the conversation ended and the game's own
   `BioAiController.ClearSomeActions_State` popped it 9 ms later (on Udina's controller too, the same tick): the
   `SarenCouncil` script's `Deactivate the captain` is followed in the same tick by **`Stream out this layer`
   (`stream_council`), the level both Anderson and Udina stand in.** (b) `convo flow: scan (the streaming levels changed)` at
   that tick, and the next snapshot (`..._01-35-18-628-left.png`) shows the council layer gone: Anderson, the near building and
   the tree vanish together, a streaming pop (owner question 4). The module still saw him "present" (`bHidden` false, his
   behaviour active), held him 5.1 s, and dipped with nothing left to hide: against the owner's "no blink with nothing changing".
   **Fixed after run 4:** `Present` is false when the person's own level is streamed out or on its way out (`LevelGoing`: the
   `Engine.WorldInfo.StreamingLevels` entry whose `LoadedLevel` is the pawn's outer `Engine.Level`, its `bShouldBeVisible` or
   `bIsVisible` off), so such a deactivation is let through (not seen), and a held one is applied at once, no dip
   (`their level <package> is streamed out (the script's streaming change takes them anyway)`).
4. **Benezia (slot 86)**: the validation save fires `Benezia_Dies`' conversation (her death), whose end activates her, it removes
   no one: not an exit fixture (the swap is the intro's, `Benezia_Intro`, 19 s after its conversation: let through by the 2 s
   window in any case).

**ME1's audit set, once, with the hearing to its end** (`tools/conversation-audit.ps1 -Long -IgnoreSlot`, build-mono at
`3341d3a9`, 2026-11-20 01:41-02:01, session `%LOCALAPPDATA%\MEVR\sessions\2026-11-20_01-40-56-audit`, log in
`scratch/exit/audit-set.log`):

1. The exits: the hearing's Anderson held and applied under the look-away's dip at 3.0 s (the podium, as before); the councillors
   and Saren let through under the hearing's black screen; **Udina held, walked off with the game's AI and applied out of sight
   after 1.8 s (2.32 m)**; Anderson after his final chat applied after 0.1 s as `BIOA_STA70_02council_DSG` streamed out; at the
   Spectre ceremony's end two councillors in view (1.6 and 3.1 m) were applied in the same tick as their level streamed out. No
   `vanished-in-view`.
2. The verdict was 6 "regressions", none from this module: (a) `lav70_asariscientist_dlg` faces off the eyes, median 13 -> 22
   degrees, and `sta30_girard_dlg` a new `face-off-you`: no exit decision of any kind was made in either scene (the log's
   `convo exit:` lines); (b) four at the council (`eyes-off-body`, `hands-off-controllers`, Shepard's head 0.1 -> 0.63 m from the
   eyes, looks at his body 0 -> 2), all at 142 s, the hearing's last seconds, which the baseline (written from the short run, 5
   records against the long run's 13) never covered: the stance sheet shows the script hiding Shepard's body at 140.8 s, before any
   hold, and the canary below, with this module off, trips the same two rules at 142.5 s.
3. **The broken run `popped-out`** (`-Canaries -Canary popped-out`, worldExit off, session `..._02-01-58-canary-popped-out`):
   **caught by `vanished-in-view`** (the hearing's Anderson at 2.2 m, Udina at 2.1 m and Anderson after his final chat at 2.1 m).

## 7. What the sequels need

The rule (section 4) is the sequels' as it is; each needs its adapter (the hook, the people, the walk, the dip). Counts below
are the scratch scan's second pass, with a black screen turning on in the same tick counted as covering the change.

**ME2** (LE2, the scan: 1,041 starts, 913 conversations):

1. The mechanism is ME1's Kismet action, with one difference read from the class defaults: ME2's
   `Default__BioSeqAct_ModifyPropertyPawn` maps its `Active` row to an **object function** (`aObjectFunctions`: `SetActive`),
   ME1's to a behaviour function (`aBehaviorFunctions`). So on ME2 it is `Engine.Actor.SetActive` on the pawn, whose native is
   `AActor::execSetActive` at **RVA `0x4BC510`** (the natives dump `natives-LE2-60AEEFA5.csv`). [verify] in the LE2 Ghidra project
   (`tools/ghidra/decompile.ps1 -Game LE2`) that ME2's `BioSeqAct_ModifyProperty` Activated reaches it through ProcessEvent (ME2's
   UObject::ProcessEvent `0xD8D50`, a class's vtable slot `0x240` there) as LE1's does through slot `0x250`; then the hook is
   `engine/ExitHooks` with an LE2 row (`kActorExecSetActive`, a pattern from the LE2 .text dump with `tools/rva-patterns.py`), the
   frame's `Node` compared with `Engine.Actor.SetActive`, the context object the pawn itself (ME1's is the behaviour: the provider
   matches either).
2. Counts after a conversation ends: **58 deactivations of one of the conversation's people in the clear (50 conversations)**,
   16 more under a black screen; 21 teleports of the people (16 conversations), 6 under black; 3 hides, 3 destroys; streaming
   changes 182 in the clear, 387 under black (four times ME1's). During lines: 5,328 actor move tracks (372 conversations),
   231 `SeqAct_ToggleHidden`, 141 `ModifyPropertyPawn`.
3. People: `game/SequelConversationWorld`'s reader (ME1's names on ME2: `m_SpeakerList[].aSpeaker`, `m_pConversationOwner`,
   `m_pSpeaker`, `m_pListener`), the same three readings this module makes; the eyes from `view_anchor::Current()` (the sequels'
   renderer publishes the same frame).
4. The walk: `game/AiWalk`'s ME2 adapter (`BioAiController.PrecisionMoveTo`, [verify] that it moves a person in the conversation
   mode; ME2 has no movement-ban question).
5. The dip: in a held conversation the sequels' cut counter; on foot LE2's fade seam (`me2_xr.cpp`'s last layer through
   `plugin/TransitionFade`) needs the same `FootCut()` read as LE1's seam.

**ME3** (LE3, the scan: 1,286 starts, 1,304 conversations):

1. **No `ModifyPropertyPawn`.** ME3 deactivates with its own `BioSeqAct_SetActive` (its `bSetActive` a property, not a variable):
   **27 of a conversation's people in the clear (26 conversations)**, 10 under a black screen; also `SFXSeqAct_ToggleCombatPawn`.
   Its native: [verify] in the LE3 project whether it calls `Engine.Actor.SetActive` (`AActor::execSetActive`, **RVA `0x4BFAF0`**,
   `natives-LE3-60AEF0C4.csv`) through ProcessEvent (then the same hook as ME2's) or the C++ virtual directly (then the hook goes on
   the op's own `Activated`, its class's vtable slot, found as LE1's was: the class's registration -> its internal constructor ->
   its vtable, section 1).
2. Teleports of the people 18 (10 conversations), 14 of squadmates; hides 2; streaming 165 in the clear, 257 under black.
3. **During lines, 773 `InterpTrackVisibility` tracks in 147 conversations**: a line's matinee hides or shows an actor at a key,
   a route ME1 does not use for people. In first person a hide there is a pop. It is not a deactivation: the hold would be a
   pre-hook on the track's update (`UInterpTrackVisibility`'s native, [verify]) keyed by the group's actor, then the same rule.
   How many hide people (not props or doubles) is the next count.

**Teleports (all three games, not built):** the second mechanism after the deactivation (ME1 8 of the people's, ME2 21, ME3 18 in
the clear: Morlan "jumped back a bit", the Fist's bouncer "returned to first position"). Built the same way: a pre-hook on
`SeqAct_Teleport`'s and `BioSeqAct_TeleportToObject`'s native `Activated` (ME1: `BioSeqAct_TeleportToObject`'s vtable `0x12FEED0`,
slot `0x408` = `0xBF41A0`; `SeqAct_Teleport`'s to find the same way), the op kept, the person walked to the destination (the
core's `Candidate::scripted`: where the script sends them wins), the op's `Activated` called again once out of sight or arrived
(it reads its variables again then: the same targets and destination).

**Streaming out (all three):** never held (the next area may wait on it). A level streamed out with someone the player sees in it
is still a pop; the proposal is a dip from black when a `BioSeqAct_SetStreamingState` (its script `Activated`, a ProcessEvent
post-handler: the change itself lands later) takes out a level holding someone in view. Owner's question 4.

## 8. Questions for the owner

1. **Bystanders.** Built for the conversation's own people (speaker list, owner, speaker, listener). ME1 also deactivates other
   pawns at conversations' ends (31 in the clear: extras, doubles). Cover anyone the player sees within about 10 m too?
   Recommendation: yes, after the headset pass of this one.
2. **Open rooms.** Where nothing within 11 m is out of sight (the council chamber), the person walks straight away from the
   player and goes once out of the view, or under a dip after the walk (12 s) and 3 s more of looking. Keep it, or dip sooner?
   Recommendation: keep (a walk is what the owner asked for).
3. **The look-away's 3 s and the 25 s cap.** Recommendation: keep until the headset says otherwise.
4. **Streaming out with people in view**: a dip (proposal above), or leave as the game does? Seen at the council (run 4): after
   Anderson's final chat the script streams the whole `stream_council` layer out in the same tick as it deactivates him, and he,
   the near building and the tree vanish in one frame. Holding a streaming change is not safe (the next area may wait on it), so the
   choices are (a) a short dip from black over the stream-out when it takes away something in view, (b) leave it. Recommendation:
   (a), only when a person of the scene in view goes with it.
5. **Teleports of a person in view** (Morlan, the bouncer, Kirrahe): walk them to the script's destination as their exit
   (designed above, not built)? Recommendation: yes, next.
6. **Line hides and holograms** (Saren's hologram going off at the hearing's end; ME3's 773 line visibility tracks): left as
   authored (built: noticed and counted, not held)? Recommendation: leave holograms, hold people hidden by a line (ME3 first).

## 9. The sequels, built (lane EXIT2, 2026-11-20)

One module for the three games: `game/ConversationExit` is ME1's adapter plus the sequels' differences in `#if` blocks (ME1's
tokens unchanged; its compiled objects compared with main's build: identical code and data except the four that carry the build's
commit or time). The rule is section 4's, unchanged.

**What reaches the hook, per game (Ghidra, numbers only).**

| | ME1 | ME2 | ME3 |
|---|---|---|---|
| The level script's op | `BioSeqAct_ModifyPropertyPawn`, row "Active" (`aBehaviorFunctions`) | the same op, row "Active" mapped to an object function (`aObjectFunctions`) | `BioSeqAct_SetActive` (no ModifyPropertyPawn in ME3) |
| The function | `SFXGame.BioActorBehavior.SetActive(bSetActive)` on the behaviour | `Engine.Actor.SetActive(bActive)` on the pawn | `Engine.Actor.SetActive(bActive)` on the target actor |
| How it is called | the op's native Activated, its row helper through the behaviour's vtable slot 0x250 (ProcessEvent) | the InteractProperty ops' native Activated (`0xA7A010`, every target in reverse; `0xA79D50`, the first target: four and three
class vtables point at them, [verify] which is ModifyPropertyPawn's in the game with `mevr vslot`), each row of type 4 through a helper (`0xA7FC60`, `0xA7F4C0`) calling the pawn's vtable slot **0x240**, ME2's `UObject::ProcessEvent` (`0xD8D50`) | **UnrealScript**: `BioSeqAct_SetActive.Activated` is a script event whose body is `foreach Targets: Actor(target).SetActive(bSetActive)` (read with `tools/conversation-scan --game LE3 --script BioSeqAct_SetActive.Activated`); the native is called by the script VM on that script's own frame |
| The native hooked | `UBioActorBehavior::execSetActive` `0xBAAB60` | `AActor::execSetActive` **`0x4BC510`** (its C++ `SetActive` at the actor's slot 0x340) | `AActor::execSetActive` **`0x4BFAF0`** (slot 0x330) |
| The frame the hook reads | ProcessEvent's (Node `SetActive`, Locals the parameters) | the same | the calling script's (Node `BioSeqAct_SetActive.Activated`, Object the op): `engine/ExitHooks`' script path |

Each address is pinned in `engine/Addresses.hpp` with a pattern that runs to the thunk's call through the C++ slot (every
bool-taking exec thunk begins alike), unique in each game's `.text` (`tools/rva-patterns.py` found none shorter; the scratch
search over the dumps confirmed one match each).

**ME3's script path (`engine/ExitHooks`, `SetScriptProvider`).** The script calls the native with its own frame, so withholding
it must leave that frame where the call would have: the hook asks the provider with the target and the op (the op's
`bSetActive` is the value: the script passes it unchanged), and a withheld call makes the thunk's own steps without its last
call: `P_GET_UBOOL` (the next token's native through `GNatives`, the value into a local), the `EX_EndFunctionParms` byte, and an
`EX_DebugInfo` (0x41) after it through `GNatives[0x41]`, exactly as `0x4BFAF0` does (Ghidra). Our own apply calls
`Engine.Actor.SetActive` through ProcessEvent, the other path. [verify] in the game.

**The sequels' adapter (`game/ConversationExit.cpp`).**

1. The hook's object is the actor itself; each person's "behaviour" is the pawn (`Person::behaviors[0]`), so ME1's lookups,
   the hold, the swap check and the apply work unchanged; the apply is `Engine.Actor.SetActive(bActive = false)`.
2. The people: `sequel_conversation_world::ScenePeople` (the sequels' reader: ME2's `m_SpeakerList`, owner, speaker and listener;
   ME3's `m_aSpeakerData`), read only in the conversation mode (ME2's `m_oCurrentConversation` can outlive its end; the 2 s
   window covers the end's tick).
3. Present: ME2 the behaviour's `bActive` (through `oBioComponent`) [verify that ME2's actor SetActive changes it], ME3 the pawn's
   own `BioPawn.bActive`; both with `bHidden` and the level's streaming.
4. The game's black screen: a ProcessEvent post-handler on `BioSeqAct_BlackScreen.Activated` (ME1's route; ME3's line track and
   start flags are not read: research/le3-conversations-in-world.md 5.4).
5. The dip: `conversation_world::DipFromBlack` forwards to the sequels' module (`gCutSerial`), which publishes it held or, on
   foot, in the armed word (`PublishArmed` carries `cut` and publishes while it is new); the sequels' fade seam
   (`plugin/HalcyonSequelBridge` `TransitionFadeLayer`) reads `FootCut()` on foot as LE1's does.
6. The world mode: the sequels' module's word (the file's `[Cinematics] conversations`, or `mevr convo world on|off|auto`).
7. The walk: `game/AiWalk`'s adapters (ME2 `BioAiController.PrecisionMoveTo`, ME3 `MoveToSlot`) behind ME1's reachability check.
8. Test aid: `mevr convo exit end` (the sequels): `BioWorldInfo.EndCurrentConversation`, the world's own early end, so a start
   action's outputs run without playing every line. Runner: `tools/le2-halcyon-run/conversation-exit.ps1`.

**ME2's fixture (the scratch scan, 171 deactivations reachable within 6 ops of a start's outputs).** Tali's reintroduction on
Freedom's Progress: `BioD_ProFre_201Tali`, sequence `Tali_Conversation`: `SFXSeqAct_StartConversation_0` (`profre_tali_intro_d_dlg`)
`Out` -> `SeqAct_Gate_2` -> `BioSeqAct_PMExecuteTransition_1` ("Tali_Left -> True") -> **`BioSeqAct_ModifyPropertyPawn_0`, Active
false on Tali (`hench_tali`, the owner), Prazza (`profre_quar_leader`) and two guards**, no delay, no black screen. Reached by
script from the VR career's `Save_0004` (lane FPC2's route: `mevr setloc` into the room, the chain fired from its
`BioSeqAct_CancelHint`). The Normandy's debriefs are the next (Miranda, Jacob and the Illusive Man's hologram after each
`nor*_debrief_d_dlg`), from a Normandy save.

**ME3's line visibility tracks (count only; owner question 6).** A scratch scan of every ME3 package's `InterpTrackVisibility`
inside a conversation's line matinees (base game and DLC, deduplicated by conversation and track): 2,461 tracks in 198
conversations (section 3's 773 in 147 counted fewer packages). 1,187 only show. Of the 1,274 that hide (a Hide or Toggle key), by
the actor their group names or finds (`m_nmFindActor`):

| what is hidden | tracks | conversations | of which hidden after the line's start (a pop mid-line) |
|---|---|---|---|
| one of the conversation's people (a speaker tag, the owner, or a group named for one: a double can't be told apart in the data) | 220 | 37 | 50 tracks in 17 conversations |
| a squadmate (`hench_*`) | 67 | 18 | 11 in 4 |
| someone else (guards, soldiers, extras by name) | 220 | 56 | 99 in 25 |
| the player | 45 | 15 | 5 in 4 |
| a hologram, screen or vision | 70 | 23 | 39 in 16 |
| a camera, light or effect | 77 | 24 | 59 in 20 |
| a prop or unknown (Reapers, doors, glass, ships, backdrops) | 575 | 101 | 220 in 58 |

So **about 287 tracks in about 50 conversations hide people of the scene (220 people, 67 squadmates)**, 61 of them mid-line; a
hide at the line's start (time 0) is the composition of a new shot (who is in it), not a person leaving in view. Classification
by names, an estimate. Not built (owner question 6).

**Checked in the game:** see section 10.

## Log

- 2026-11-19/20: the packages read and counted for the three games; Udina's case found; the natives read in an isolated copy of
  the LE1 Ghidra project (the shared one was locked by another session's MCP server); the rule, ME1's side and the audit built.
- 2026-11-20: runs 1-4 on the fake headset; why the AI never walked Udina (the move state's own reachability test), fixed with
  `PointReachable`; people whose level streams out applied at once; the audit set once (long) and the `popped-out` canary caught.
