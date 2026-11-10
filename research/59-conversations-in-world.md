<!-- SPDX-License-Identifier: MIT -->
# 59: Conversations in the world, first person (2026-11-06)

The owner (2026-11-06, in the PS VR2, after the Normandy intro with Nihlus): "is it possible that ... dialogue in general could
actually just be represented as legitimate in-game experiences instead of having to do the full screen". The 3D cinema screen
(research/52) gave depth but not presence: characters "look physically large, but they don't look human or lifelike". A screen
can't fix scale for a director's close-up; standing in the scene can. This note is what LE1's conversation system does to the
actors and the camera, measured, and a first-person prototype behind `[Cinematics] conversations = world` (off by default).

## How LE1 runs a conversation (the classes, read from the SDK headers)

- **The live conversation** is `SFXGame.BioWorldInfo.m_oCurrentConversation`, a `BioConversation`: `m_pSpeaker` and
  `m_pListener` (the line's two actors), `m_pConversationOwner` (the NPC it belongs to), `m_pConversationPlayer` (Shepard),
  `m_SpeakerList` (every actor a line can name), `m_LookAtList` (`pActor` looks at `pLookAtTarget`, the game's own gaze
  direction), `m_sCurrentSubTitle`, `m_nCurrentEntry`, `bUseFixedCameras`.
- **A stage** (`m_stage`, a `BioStage` actor with a skeletal mesh whose sockets are the camera marks) places the actors:
  `BioStage.PlaceActors(Conv)`, `GetActorStageNodePosition`, `BioConversation.SetPositionRotator(Actor, Position, facing)`,
  with the actors' places before the move saved in `BioConversation.m_aFSPrevPositions` (`FBioSavedActorPos`: vPos, rRot,
  pActor) and put back at the end. `UpdateFacing` turns actors toward the speaker.
- **The camera** is `BioCameraBehaviorConversation` (fixed or procedural shots, its own depth of field `m_tDOFData`), chosen
  per line by the stage (`GetBestCameraPosition`, intimacy levels, the line of action `vLineOfAction`); the player camera
  constrains it to 16:9 (`bConstrainAspectRatio`, which Halcyon's `[NOLETTERBOX]` clears for a VR cine).
- **The wheel** is the conversation movie's handler, `BioSFHandler_Conversation` (`m_bDisplayingWheel`, `m_bSlotsUsed[6]`,
  `SelectConversationEntry(nWheelLocation)`, `SkipConversation`), on the GUI manager's panels.

## Measured (2026-11-06, fake headset)

My branch (main b01916e plus this work), `open BIOA_STA00` from the front end: the arrival cutscene, then the conversation
`nor10_oculon_approach_D.nor10_oculon_approach_dlg`, the medical bay after Eden Prime: Chakwas, Ashley and Anderson (its owner)
around Shepard, Kaidan 12 m away, a `BioStage`. Played twice: on the 3D screen (12:11-12:16, part of it) and in the world mode
(12:31-12:39, 298 s from the first line to gameplay: 65 shots, the wheel 24 times), driven by `mevr convo pick`/`skip`. Logs
`convo:` in `LE1MEVR.log`, kept in the session's scratchpad (`convo-log-run3.txt`).

**1. The conversation moves the actors onto marks, at camera cuts, and turns Shepard.**

| Actor | What it did |
|---|---|
| Shepard | His mesh stood 2.25 m from his pawn at the start (the wake-up cutscene's placement, left over). At 63.2 s the conversation put the pawn on a mark, 1.96 m away and turned 90°, and the mesh back on it: his head bone jumped 1.17 m. His pawn then turned four more times to face the speaker (180° at most from its start). |
| Ashley | Turned 20°, then **jumped 2.91 m** and -111° at 73.5 s, from behind Shepard to beside Chakwas in front of him, at the same instant as a cut (shot 9). |
| Anderson | **Jumped 1.50 m** (226.3 s), **2.49 m** (250.4 s) and back to his start (267.5 s), each at a cut. |
| Chakwas | Never moved; turned 30°. |

The cuts hide these teleports on a screen; in first person they are pops (Ashley appears in front of you). Head bones otherwise
move 0.02-0.04 m (gestures; the first run's summary).

**2. Where people look: at Shepard's eyes, so at the player.** The game's look-at list has the speaker looking at Shepard and the
others at the speaker (or at Shepard). The speaker's head bone X axis (the face) against the line to the player's held eyes:
4-37°, median 12° over Anderson's 22 lines; Chakwas 18-22°, Ashley 19°. The player's place stayed within 0.42 m of Shepard's
head bone. Shepard himself is animated by the conversation (placed and turned, above).

**3. Half the shots are Shepard's own face.** 65 shots: 32 reverse shots on Shepard (his face, which no first-person view
shows: you see the one he faces instead), 33 on an NPC, no cutaways in this conversation. All 33 NPC subjects were visible from
the player's place (`Actor.FastTrace` from the eyes to the face), the director's camera 12-154° (median 20°) off the player's
line to the subject. The lenses: 15° (35 shots), 24° (13), 40° (10), 25-35° (7): tight close-ups 0.7-1.3 m from a face (the
0.84 m of research/52). In the world the same people stand 1.6-2.1 m away at life size.

**4. The conversation movie on the frame.** Its 1280x720 stage spans the frame's width and is **centred top to bottom** (537 px
above and below on the 2344x2392 eye frame), unlike the HUD movie, whose bottom widgets sit on the frame's bottom (research/41).
Measured on the HUD panel: the wheel's rows at stage y 590-680 (frame 1620-1780) around the authored (639.7, 643.3); the line
one row at stage 564-575. Halcyon's VR layout (0.50 x 0.65, centred) shrinks the movie for the eye images; the 3D screen and the
world mode show it at its authored size. During spoken lines nothing was drawn in this profile: the only line text was the
previous line the wheel shows above itself [verify: the game's subtitle option].

## The prototype

`[Cinematics] conversations = world` in `ASI\LE1MEVR.ini` (`vr`/`0`, `screen`/`1` as before; live, like the screen's setting;
`mevr convo world on|off|screen|vr` for a test without the file). Mod: `game/ConversationWorld`; host: `plugin/ConversationWorld`;
shared: `core/vr/ConversationWorld` (unit-tested: `tests/ConversationWorldTests.cpp`); HostServices v33 `setConversationWorld`.

- **The player's place.** The view on foot if it was Shepard's in the last 5 s and his pawn is within 3 m of it; else his eyes
  from his head bone (+8.9 forward, +1.7 right, +29.3 up units, arm IK's measure with the headset at the recenter point),
  facing his pawn's yaw (a conversation straight after a load or a cutscene: the medical bay's). It follows his head bone's jumps
  past 0.80 m that last three ticks (`FollowHeadSettled`, `worldFollow = 1`; 0.40 m until the survey, below, found a 0.4 m
  animation step re-anchoring the player), never its small moves and never the pawn's turns: the player's real head replaces
  them, body follow turns the body under the view.
- **The setting.** Our menu's Comfort > Conversations: **First person** (`conversations = world`), In VR, 3D screen, Flat screen;
  the conventional ways stay the default (the 3D screen). Cutscenes keep their three.
- **The view.** The mod sends the place and the POV's addresses (recorded by an `Engine.Camera.UpdateCamera` post-handler in
  mode 5); the host swaps the place into the POV around the main view's `CalcSceneView` only (`GalaxyViewSwapBegin`, the galaxy
  map's and the world screens' seam): the director's camera still serves the game. The tracking frame for arm IK and the
  subtitle is built from the host's view at the held place (`engine::FromCameraView`, as `view_anchor` does on the game camera).
- **On foot's presentation.** `MevrBridge::ConversationInWorld()` in five `[MEVR]` seams in `xr_session.cpp`: `CineFinishDecision`
  (mode 5 not a cine: no VR cine, no 3D screen, head look on), the second eye's gate (published as gameplay), the conversation
  movie's authored layout, **the cine capture boundary** (the conversation still renders through its camera's post chain: without
  it the UI stayed in the eye images) and **`[NOLETTERBOX]`** (without it the world was a 16:9 band); one in `head_aim.cpp`:
  Halcyon's first-person hides stay up through the conversation camera.
- **Depth of field off.** The conversation camera's depth of field is focused for the director's shot: the room behind Chakwas
  blurred, then all of it when the focus moved on. Its behaviour's `m_bDOFSettingDisabled` changed nothing; the engine's switch
  does: `scale set DepthOfField False` (through True first, research/35) while the world view lasts, then back to what
  `BIOEngine.ini` said before. **The game writes the live value into `BIOEngine.ini`** during play, so a read afterwards finds
  our False (the first build left it off).
- **The body.** `view_anchor::Hold` with that frame: arm IK and body follow run as on foot (`arm_ik::OnFoot`).
- **The UI.** In mode 5 with the world view up the game's UI goes to the HUD panel; the subtitle's band (stage x 80-1200, y
  480-586) and the wheel's (x 330-950, y 586-712) are cut out of it (`DefaultSubtitleCrop`, `DefaultWheelCrop`; `subtitleCrop`,
  `wheelCrop` by hand): the line on a quad under the speaker's face (the head bone, `subtitleBelowM` 0.22 under, `subtitleTowardM`
  0.15 toward the eyes, `subtitleHeightDeg` 4 at any distance; Shepard's own lines under the one he talks to), the game's own
  wheel on a quad above the left hand (`wheelHand`, `wheelWidthM` 0.40, `wheelDistanceM` 0.12), placed when it appears, steered by
  the left stick as before. They use the interaction label's and the game wheel's swapchains (neither is up then; the runtime
  limits swapchains).
- **Measurements, whatever the mode** (`convo:`): the actors at the start (place, facing, head bone), the stage and the saved
  places, each move or turn (JUMPED past 30 units in 0.1 s), who speaks to whom with the speaker's head and body against the
  player's eyes, the lines, who looks at whom, the wheel, each shot (everyone of the conversation inside the 16:9 frame, the
  nearest the subject: from Shepard's eyes, over his shoulder, on an NPC visible or blocked from his eyes, on Shepard, a
  cutaway); a summary at the end. `mevr convo` prints it; `mevr convo pick <slot>` / `skip` drive a conversation without
  controllers (only while the wheel is up and on a used slot / while a line plays: **a pick between lines crashed the game**
  inside `SelectConversationEntry`'s script, 12:23). Snapshot `"mod"."conversationWorld"`.

### Checked in the game (snapshots `%LOCALAPPDATA%\MEVR\snapshots\2026-11-06_12-3*`)

- The medical bay in first person: full frame, sharp, Chakwas facing the player at life size, Ashley beside her after her
  jump, the room around them; `[VRSTATE] gm 5 alive -> VR` for the whole conversation, `gm 0 -> VR` after it, gameplay on foot
  normal afterwards (12-41-23-579). The game's wheel exactly on its quad (12-31-28-125-wheel), Ashley's last line on the
  subtitle quad under her (12-33-11-627-label, 12-34-30-375). The quad projects where it is meant (x 0.85, y 0.66 of the left eye
  for the quad, her face at y 0.58: the line 0.26 m lower, in front of her shoulders).
- **The first frame:** 118 ms of Halcyon's VR cine before the world (the mod's state comes a tick after the mode, and
  research/56's 100 ms hold of plain VR).
- **Cost** (`perf\2026-11-06_12-35-36-convo-end-probe.json`, 60 s inside the world conversation): game thread 6.99 ms median
  (the engine 6.88, ours 0.09) = 63% of the 11.11 ms period, render thread 3.19 ms, GPU 1.98 ms, frame interval 7.2 ms (139 fps
  on the null driver): **the full rate**. On the 3D screen a conversation runs at 45 fps (Halcyon's `cineFpsCap`, measured by the
  release-blockers session); the world mode isn't a cine to Halcyon.

## Keeping people in place (2026-11-06, evening)

The owner, on the pops: "Is there a way we can fix the transporting effect? Since we know the direction I'm curious if we could
have them walk to the target locations." The cheapest of the three answers (keep them where they stand; walk them there; hide the
jump with a dip to dark) first, since the marks only frame the director's shots, which the world mode doesn't show.

- **How the game moves them: natively.** A call trace across the medical bay's jumps (`mevr calltrace 150 position placeactor
  setlocation setrotation teleport facing moveto -canfire -arrowposition`) saw no script call and no ProcessEvent: the stage's
  placement (or a matinee track) moves them in native code. So the move can't be refused, only undone.
- **Undone before the frame is drawn.** The conversation camera cuts in the same tick as the jump (the director's view never shows
  a pop), so the camera update (our `Engine.Camera.UpdateCamera` post-handler, game thread, before the draw) sees the actor already
  on the new mark. There, a jump of 30 units or more since the last frame (a run moves under 3 a frame at 90 Hz) and under 6 m (a
  scripted entrance is let through) is put back with `Actor.SetLocation`; continuous moves update the kept place. A check in the
  mod's tick, after the draw, counts any jump still standing (one that came after the camera update and was drawn).
- **Turning.** Everyone but Shepard turns toward whom the game's look-at list says they look at (else the speaker), from where they
  stand, at most `worldTurnDegPerS` 120 degrees a second; the stage's facing was for its mark. Shepard's body turns under the
  player's view (body follow).
- **Shepard is kept too**, so the player doesn't move with his placements. Putting his pawn back drags his mesh there and back
  within a tick (his head bone read 2 m out and back), which moved the player's place twice in 0.1 s on the first try; the place now
  follows his head bone only when a jump lasts three ticks (`FollowHeadSettled`, unit-tested).
- `[Cinematics] worldStay = 1` (0: the jumps as the game makes them), `worldTurnDegPerS`. Logs `convo: ... kept in place: the game put
  it N m away`, the end summary's `N jumps onto marks undone (people kept in place, M of them seen in a drawn frame)`; snapshot
  `"mod"."conversationWorld"."kept"`.

**Measured** (the whole medical bay conversation again, 229.6 s, the final code): **25 jumps onto marks undone, 0 seen in a drawn
frame**: Ashley's 2.91 m four times, Anderson's 1.50 and 2.50 m thirteen times (the game puts him back on his mark at nearly every
line), Shepard's 1.96 m seven times; every actor ended where it began (moved at most 0.00 m in the 0.1 s samples). The director's
shots now frame empty marks (7 classed as cutaways): not shown in the world mode, so it doesn't matter.

**One jump left in this scene: Shepard's own body.** At 31.5 s his head bone moved 2.26 m and stayed: his mesh, which had stood
2.25 m off his pawn since the wake-up cutscene (an animation's root offset, measured at the start), snapped back onto the pawn. The
player's place follows it (snapshot 2026-11-06_17-33-53-015: behind a medical bed, Chakwas 3.8 m away instead of 1.6). A
conversation started on foot has his mesh on his pawn, so this is the "conversation straight after a cutscene" case. Candidates:
the transition fade (`plugin/TransitionFade`, main since a5c9e8e) over the player's own moves; or not following it while body
follow can't reach (the arms would stretch).

The undo also knows where the game wanted them: the jump's far end. Most of these people have no AI to send and their gestures
are authored standing, so standing still remains the default. The optional prototype below waits for a lasting mark before walking.

## The survey and the scan: every conversation (2026-11-06, night)

The owner: "I imagine there are many scenes that we need to address, it sounds like you only did the early levels", then "we would
also need to categorize some of the scenes which would be bad in the first person mode. I'm thinking of things like the boss fights
where the camera moves to different locations to frame things differently", and "Is there a reason you can't use the decompiling
approach to get these scenes programmatically?". Two tools came of it: a survey that plays conversations in the game, and a scan of
the game's packages that classifies all of them without it. The scan answers "which scenes"; the survey answers "does our fix hold".

### The survey (in the game)

`tools/conversation-survey.ps1 -Maps <persistent level> -Lists <name filters> [-Streams] [-Filter text]`: opens the map (a fresh
Shepard, `disconnect` first), plays any conversation that starts by itself, then fires each `BioSeqAct_StartConversation` of the
level's script (`mevr instances`, `mevr activate n 0`, as its event would; one per conversation, by the action's `Conv`), picks a
random used wheel slot while the wheel is up, skips lines otherwise (`mevr convo pick|skip`), ends a conversation that loops past
`-ConversationSeconds` (`mevr convo end`: `BioConversation.EndConversation`), and keeps the mod's end-of-conversation line
(`convo: summary {...}`: shots by kind, not seeable from Shepard's eyes, far shots, jumps undone and drawn, the player's place
re-anchored). `-Streams` touches each of the map's `BioTriggerStream` volumes in turn (Shepard moved onto it) and surveys what that
area loaded, skipping volumes that show no chunk not seen yet (Noveria: 62 of 209). `tools/conversation-survey.py --all` tabulates.
`UsedBy` and `SFXGameModeDefault.Used` started nothing; the Kismet actions do, out of plot order.

**28 conversations played** (the Normandy, the Citadel's arrival, Noveria's Port Hanshan and Peak 15 through its volumes), **0
jumps onto marks seen in a drawn frame**: the transmissions (12: one shot on Shepard throughout, nobody in the room), the staged
ones (Pressly, Joker seated, Liara, Mira, Anoleis, Gianna Parasini, Lorik Qui'in, Mallene Calis), the Oculon approach (41 shots, 26
jumps undone), and the Ilos approach and opening, where the player's place followed Shepard's steps: at 0.40 m a 0.4 m step of
his animation moved the player four times in a 13 s scene; at 0.80 m once in the 45 s approach (Maeko's greeting: twice).
**Limits:** a fresh map is the story's start: Feros, Virmire and Therum never reached gameplay from `open` (their persistent levels
wait for the landing), and plot conditions are skipped (a conversation can play out of its story). Slow: about 1.8 minutes per
volume before the chunk filter, 1-3 per conversation. The mod now also counts **far shots** (the camera more than 8 m from
Shepard's eyes) and the farthest camera (`farShots`, `cameraFarM` in the summary).

### The scan (the packages, no game)

`tools/conversation-scan` (C#, GPL-3.0 like the cockpit packager, over Legendary Explorer's `LegendaryExplorerCore` by path, never
copied) reads every LE1 package that can hold a conversation (the English localised ones and the levels' own; 2,058 of 4,947, 33 s)
and writes one JSON line per `BioConversation` (lines, speakers, camera intimacy, GUI styles, ambient lines), per
`BioSeqAct_StartConversation` (its conversation and its stage: the `BioStage` actor and its `BioStageType`, whose marks the game
puts the actors on and whose cameras it cuts between) and per line's matinee (the node sequence's `BioSeqEvt_ConvNode` and the
`SeqAct_Interp` it fires: every group and track). `python tools/conversation-scan.py scan.jsonl [--list <kind>] [--csv out]`
joins them per conversation. Its output is the game's data: kept out of the repository (only these counts are ours).

What a line's matinee holds, over the whole game (34,858 lines): voice and gesture tracks on all; `BioEvtSysTrackSwitchCamera`
(the stage's cameras) on 8,971; **`InterpGroupDirector` (authored camera cuts, a cutscene's own) on 1,187**; `InterpTrackMove` on
742 (groups named for a camera, `Cam`, `matcam`, `CamNav`, `Flyer`, or for a person, `kaidan`, `Wrex`, `Saren`, `shep`); look-at
3,213, set-facing 2,397, fades 101.

**The kinds** (each conversation the first that applies):

| Kind | Rule | Count | What first person does with it |
|---|---|---|---|
| ambient | every line ambient (barks, no conversation mode) | 57 | nothing: not a conversation in mode 5 |
| framed | authored camera (a director track or a moving camera) on a quarter or more of its lines | 18 | loses the composition: the set pieces |
| remote | started on the game's `transmission` stage, or a transmission or distress call by name | 19 | fine: a voice or a hologram, nobody to see |
| walks | someone walks an authored path during a line (a move track on a person) | 23 | the walk is kept unless past 6 m [verify each] |
| stage | played on a stage: the game puts people on its marks | 172 | the marks' jumps are undone (above) |
| free | no stage, no authored camera or walk: people stay where they stand | 202 | as on foot |

| Area | free | stage | remote | walks | framed | ambient |
|---|---|---|---|---|---|---|
| CHA (squadmates) | 2 | 5 | 0 | 3 | 0 | 0 |
| END (the Citadel's end) | 3 | 2 | 0 | 3 | 1 | 0 |
| ICE (Noveria) | 39 | 32 | 0 | 1 | 0 | 8 |
| JUG (Virmire) | 19 | 10 | 0 | 3 | 1 | 2 |
| LAV (Therum) | 3 | 2 | 0 | 0 | 0 | 4 |
| LOS (Ilos) | 13 | 3 | 0 | 0 | 1 | 1 |
| NOR (the Normandy) | 9 | 18 | 4 | 2 | 8 | 2 |
| PRC (Bring Down the Sky) | 5 | 2 | 0 | 4 | 2 | 2 |
| PRO (Eden Prime) | 17 | 7 | 0 | 0 | 0 | 5 |
| SP (assignments) | 18 | 13 | 15 | 1 | 0 | 7 |
| STA (the Citadel) | 43 | 58 | 0 | 5 | 4 | 22 |
| WAR (Feros) | 28 | 20 | 0 | 1 | 1 | 4 |
| others (combat, default, UNC) | 3 | 0 | 0 | 0 | 0 | 0 |
| **all 491** | 202 | 172 | 19 | 23 | 18 | 57 |

**The framed set pieces** (authored camera on lines/of lines): Saren at the end (`end90_saren`, 28/45), Sovereign on Virmire
(`jug70_sovereign`, 53/78), the door on Ilos (`los10_trig_door00`, 10/20), Anderson's briefing and the comm room on the Normandy
(`nor10_captain` 71/72, `nor10_comm_room` 17/17), the Ilos approach and opening (`nor20_ilos_approach` 22/27, `nor20_ilos_open`
2/7), leaving the Oculon (`nor20_leaving_oculon` 25/49, with 9 walks), the empty locker (`nor20_empty_locker`, 5 fades), Kaidan's
and Liara's starts, Bring Down the Sky's discovery and console, the Citadel's scenic view, the stripper's seat, Wrex at Fist's,
Saren before the Council (`sta90_council_saren`, 14/31), Feros' entry (`war30_trig_01_entry`, 15/43). These are the owner's "boss
fights". The survey's runtime "framed" (a third of the shots far, or half not seeable) agreed where both saw a scene (the Ilos
approach and opening), and found one the data can't: the Council's hologram call before Virmire (`nor20_virmire_open`, 8 of 12
shots not seeable), which the scan calls remote.

**Checked against the survey** where both saw a conversation: the 12 runtime transmissions are all remote, the Oculon approach
(26 jumps undone) and Noveria's staged ones are stage, the Ilos scenes framed.

**What it means for First person:** 395 conversations (free, stage, remote) are the cases the world mode was built and measured
on; 23 walks need a look each; **18 framed set pieces are composed for a camera elsewhere**: the next step is to show those on the
3D screen even in First person (by name from the scan's list, or by the runtime rule: far shots), the rest in the world.


## The owner's first try (QA build 0.1.0+242d28a, 2026-11-06, 22:08)

The owner, in Nihlus's comm-room conversation on the Normandy (`nor10_comm_room`): "a weird background ... it looks like it's lost a
lot of the texture, or the asset's not being drawn correctly. And also the selection for having the conversation is no longer in
front of me." From the log (`LE1MEVR.log` 22:08-22:10):

- **Culling and streaming followed the director's camera.** The held view reached the main view's `CalcSceneView` (the swap's line),
  but the render levers' one-Draw line gave the view origin at the director's camera, (-5495, 16380) against the player's place
  (-5091, 17165): 5-9 m away behind Nihlus, a 10-13 degree lens. What the engine draws and streams sharp was decided from there; the
  medical bay's director stood near Shepard, so the first test didn't show it. **Fix:** the mod writes the held place into the
  player camera's POV itself, in the `Camera.UpdateCamera` post-handler after reading the director's (`worldCamera = 1`, journalled
  `ConversationWorld.cameraPov`; log `convo: the game's camera held at the player's place too`): culling, streaming, the one-Draw
  record and the sound follow the player's eyes; the measurements keep the director's POV read first.
- **The wheel at the left hand** was out of sight with the arm down (placed at (-0.25, -0.84, -0.05) m from the head). **Fix:**
  `wheelHand = front` is the default (0.45 m ahead, 0.35 m below the eyes, placed when it appears); `left`/`right` stay.
- **The comm room is a set piece** (17 of 17 lines authored camera). **Fix:** in First person the scan's 18 set pieces go to the
  3D screen (`cw::IsSetPiece`, unit-tested; `worldSetPieces = screen`, `world` keeps them in the world; the host's screen takes
  conversations in the world mode for what the mod leaves out). Log `convo: ... shown on the 3D screen (a set piece ...)`.

## Walking to marks (prototype)

**Built offline, NOT yet checked in the game.** With `conversations = world` and `worldStay = 1`, each tracked actor remembers
the destination and monotonic time of the latest jump that KeepInPlace undoes. NPCs walk only when that mark is **more than
`worldWalkMinM`** from their current place and **`worldWalkSettleS`** has passed without a newer jump. Every newer jump (even to
the same mark) restarts the wait and cancels a running walk; an observed return within the minimum distance cancels the request.
Shepard stays kept and never walks. The pure `core/vr/ConversationWorld::MarkWalk` rule and slide steps have offline unit tests.

`[Cinematics]` settings (read live, next to `worldStay`):

| Key | Default | Meaning |
|---|---|---|
| `worldWalk` | `0` | `0` off; `1` AI; `2` slide |
| `worldWalkSettleS` | `2.5` | Seconds without a newer observed placement before starting |
| `worldWalkMinM` | `0.5` | Minimum distance from the actor when deciding to walk |
| `worldWalkSpeedMS` | `1.4` | Slide speed in metres/second; both methods' timeout uses distance / this speed + 3 s |

**AI:** `Engine.Pawn.Controller`, checked for BioAiController and the matching `PushMoveToPoint` function, receives the point
with walking and precision movement requested. A missing controller/function/parameter or failed call (including a null move
action) logs the reason and uses slide for that walk. Its returned `BioSeqData_MoveToPoint.Terminate` flag ends only our move
on cancellation/arrival/timeout (journalled by FieldRef). **Slide:** `Actor.SetLocation` steps toward the mark at speed, faces
the travel direction with the existing smoothed turn, and writes journalled `Actor.Velocity` from the actual step for locomotion;
arrival/exit zeroes it. The kept place follows progress, becomes the mark on arrival, and stays at the reached place on timeout.
Collision refusals remain bounded by the timeout. Disabling stay/walking, changing methods, leaving world mode and conversation
end cancel walks; speed is captured at each walk's start so its deadline stays consistent.
Logs start `convo: walk:` (start/from/to/distance/method, fallback, arrival/end/cancel); summary JSON and the survey's table
include `walks` (starts, including fallback; older survey records display zero).

Offline validation: Release `MEVR-LE1-mod` and `mevr-tests` built with `/W4 /WX`; the test executable printed `all tests passed`
and `conversation world tests: ok`; SPDX passed. The survey table was checked with synthetic new and old records. No game run
or deployment. Two existing installer recovery cases were skipped because the loader DLL was not built.

- [verify] Medical bay: Anderson's bouncing marks keep restarting/cancelling; a lasting mark starts after 2.5 s; Shepard never walks.
  The observer still uses the existing 30-unit jump threshold: a native placement exactly at the already-kept place is
  indistinguishable from no placement, so such returns cannot cancel a pending walk from location samples alone.
- [verify] AI can move during a conversation, stops when its own action's `Terminate` is set, respects collisions and leaves other
  actions intact; missing AI falls back to slide. AI pace is the game's walking pace, not a speed override.
- [verify] Slide looks like walking with the scene's animation tree (standing gestures may override locomotion), speed/velocity
  integration is correct, turns are natural, and blocked moves end where they stand after the timeout.
- [verify] New jumps during a walk, return-to-current-place cancellation, live disable, conversation end/unload and the next
  conversation leave no stale movement/velocity. Recheck jumps reaching a drawn frame with each method.

**QA-approved by the owner (2026-11-07)** in the QA build 0.1.0+7ffe648 with these three fixes. Still open: walking to marks (the
prototype below, off), the one jump of the player's own place after a cutscene.

## The launcher: any conversation in two minutes, no save (2026-11-09)

The owner: "a system where you can launch any of the conversation experiences so we can quickly validate the look and feel without
needing explicit saves." Save editors were weighed first: Legendary Explorer's library has no LE1 save support and Trilogy Save
Editor is a GUI without a command line, so saves would mean our own LE1 save writer. The game itself has every piece, and the scan
has the knowledge:

- **The catalogue** (`python tools/conversation-scan.py scan.jsonl --catalogue %LOCALAPPDATA%/MEVR/conversation-catalogue.json`):
  the scan now records each `BioSeqAct_StartConversation`'s owner (its Owner link's `SeqVar_Object`: the actor's class, `Tag`,
  `Location` and `Rotation` when it is placed in the level) and its stage's place; the catalogue adds the persistent level
  (`BIOA_ICE50_06_DSG` -> `BIOA_ICE00`; `UNC`/`FRE` keep their number) and the chunk (`ICE50_06`). 713 starts of 404 conversations,
  421 with a placed owner; the rest are spawned by the plot or passed in from a parent sequence (no place to stand).
- **`tools/conversation-launch.ps1 <name> [-World on|screen] [-Party] [-NoOpen] [-Snapshot]`** (`-List <text>` searches): `disconnect`,
  `open <persistent level>`, wait for gameplay (a map that sits in the party screen for 20 s gets `mevr vehicle party`), find a
  `BioTriggerStream` volume whose states show the chunk and `mevr stream touch` it, `mevr setloc` Shepard 1.5 m in front of the
  owner and turn him to face it (the console's `set SFXPawn_Player Rotation` and `set BioPlayerController Rotation`: the first run
  held the view 107 degrees away from Mira, since the on-foot view keeps the heading from before the teleport), find the start
  action by its path's tail and `mevr activate n 0`. Plot conditions are skipped: lines can play out of their story.

**First run, five conversations on the fake headset:** Mira on Noveria (`ICE50_08`, streamed), Harkin in Chora's Den (`STA60_05A`)
and Chellick at C-Sec (`STA30_04A`) started in first person about two minutes after the command, Shepard 2 m from the owner,
facing it after the fix (Chellick: the view at 180 degrees, Chellick at a bearing of 162; his office was drawn, the wheel in front,
but nobody stood behind the desk in the snapshot: a placed pawn the plot hasn't shown yet [verify `bHidden`], the fresh plot's
limit). Two didn't: **Virmire** (`JUG20_08`): the level's own trigger
conversation (`jug20_trigger`) started after the stream (the launcher now ends one that starts by itself before firing), and the
teleport to the camp killed Shepard within 3 s, god mode or not (the GameOver screen is mode 7): a teleport into a chunk whose
floor isn't loaded yet drops him through the world to the kill plane. Next: wait until `mevr vehicle levels` lists the chunk as
loaded, then teleport a little above the owner's place; **the medical bay** (`NOR10_07wake`): the chunk is loaded by the plot (the wake-up after Eden Prime), no
streaming volume shows it, and **the engine's `streamlevelin BIOA_NOR10_07wake` froze the game** (no log line for 90 s, the
heartbeat included; killed and relaunched): plot-loaded chunks need the plot's own Kismet (its level-streaming action fired with
`mevr activate`), the next step for those.

### Marking the rest: Shepard's walks and wide stages (2026-11-09)

The owner: "Some scenes span wide areas, or has Shepard slightly walking, I imagine we'll need to mark those you determine if those
need to be cinematic or not." Two more signs are in the data, and the scan gives every conversation a **verdict** for First person
(`tools/conversation-scan.py --verdicts`; `--set-pieces` prints the C++ list `cw::IsSetPiece` is built from, so the mod's list is
generated, not typed):

- **Shepard walked by the game** (a move track on his own matinee group, `shep`/`player`): 4 conversations (both Council endings,
  4 lines each; Liara's first talk; Kirrahe's briefing, 1 line). The player's place would move under the headset: **screen**.
- **Wide stages** (the stage type names its size: `ST_2P_600_combat`, `ST_2P_1000_observe`, people 6-10 m apart): 6 conversations
  (Kaira Stirling, the lab ambush and Olar's execution on Noveria; the researcher, the biotic and Darius of the assignments), all
  stand-offs. People stay where they stand in the world mode, so these may well work; **check** each with the launcher.
- **An NPC's authored walk**: 23 (the squadmates' talks, Wrex and Kirrahe's camp on Virmire, Bring Down the Sky...): **check**.

Verdicts over all 491: **screen 21** (18 framed + 3 more for Shepard's walks; Liara's was both), **check 26**, **world 387**, the 57
ambient without one. The 21 are in `IsSetPiece` and go to the 3D screen in First person; the 26 are the launcher's next list.

## Part A and Part B: the opening accepted, everyone in sight (2026-11-09)

The owner, after the Citadel arrival: "When I loaded into the citadel for the initial conversation I found that I was far away
(close to my initial spawn point) along with my squad." Why: a conversation right after a cutscene or a load starts with Shepard at
the spawn point; the stage teleports him onto his mark beside the owner in the first tick, **keep-in-place undid it** (Shepard was
kept like everyone else), and the player's place was taken from his head bone there, at the spawn. The squad isn't in the speaker
list, so the stage never moves them and neither did we: they stood at the spawn with the player.

- **Part A, the opening** (`[Cinematics] worldOpening = accept`, the default; `keep` is the old way): the stage's first placements
  are the scene's design and the player hasn't seen the room in this conversation yet, so they stand. Until the first drawn frame
  of the world view (or 500 ms) every jump is accepted (`convo: ... the opening put X N m away: accepted`); then the player's place
  is taken again from Shepard's head bone plus the eye offset, **facing the owner** (`FinishOpening`: `convo: the opening ended after
  N s: ...`), and the host dips from black (`cw::State::cut`, `transition_fade::Fade::Cut`, unit-tested): the move reads as a cut,
  not a teleport. Keep-in-place and the walking prototype run from then on.
- **Part B, everyone in sight** (`worldArrange = 1`, `worldArcM` 2.5, `worldViewDeg` 60): at the end of the opening everyone of
  the conversation (owner, speaker list) and the squad (`squad_weapons::Members()`) is checked from the player's eyes:
  within `worldViewDeg` of the facing, 1.2 m to 6 m away, a clear line of sight (`cw::InView`, the shots' `LineOfSight`). The owner
  keeps its mark; whoever else fails is put on a seat of an arc `worldArcM` from the player centred on the owner's bearing, right
  then left, 25 degrees apart, never past the view's edge (`cw::ArcSeats`, unit-tested), on the floor there (`Actor.Trace` down,
  the actor's own height above its floor kept), facing the player (`Actor.SetRotation`); their kept place is the seat. Logs
  `convo: X is out of view / too near or too far / blocked (...)`, `convo: X brought into view: seat n at ...`; the summary's
  `openingMoves` and `arranged`.

[verify] in the game: the Citadel arrival from a save or the launcher (the player beside Udina and Anderson, the squad in view), the
medical bay (unchanged: Shepard already stood on his mark), a conversation started on foot (no opening moves, no cut).
### The launcher's gaps closed (2026-11-09, later)

Run in the game 2026-11-09 (fake headset): the chunk wait works (Virmire's `JUG20_08 is loaded and visible` before the
teleport; the Citadel's and Noveria's chunks reported not visible within 60 s although their actions were there and fired
fine, so the wait's visibility test is stricter than it needs to be [verify]), but **Virmire still kills Shepard**: the teleport
to 50 units over the owner landed him on the GameOver screen again, and the party screen came back twice (FinalizeParty sent
each time). **The medical bay's named state set but its chunk never came:** `mevr stream state stream_wakeup on` ran through
the loaded `BioSeqAct_SetStreamingState_4` (`Activated` called true), the volume was touched after it, and `NOR10_07wake`
stayed unloaded for 60 s, so the start action wasn't there to fire [verify: whether the state needs the volume's `Retouch`
with the state already on, route B, or the plot's own sequence does more than the state]. **The launcher waits for the chunk:** `mevr stream chunks` prints every
streaming level as `<package> loaded=<0|1> visible=<0|1> shouldLoad=.. shouldShow=.. chunk=<VirtualChunkName>`
(`WorldInfo.StreamingLevels`); after the stream the launcher polls it every 2 s for up to `-StreamSeconds` (60) until the start's own
package (`BIOA_JUG20_08_DSG`) is loaded and visible, then teleports to the owner's place 50 units up, in god mode (the game drops
Shepard onto the floor). **Plot-loaded chunks are shown by a named streaming state.** A `BioTriggerStream`'s states carry a
`StateName`: `None` or `Default` (486 and 475 of the 1,482 states in the game) is the plain state a touch applies; a named one
(`stream_wakeup`, `stream_huddle`, `PostThorian`, `JumpTo20`...) applies only while the world's `BioWorldInfo.m_WorldStreamingStates`
entry of that name is on, which the plot's Kismet sets through `BioSeqAct_SetStreamingState` (`StateName`, `NewValue`; its
`Activated` is script, not native: the natives dump lists none for it, while `BioTriggerStream.Retouch` is native). The medical bay's
`NOR10_07wake` is in `stream_wakeup`'s visible list on `BIOA_NOR00`'s `BioTriggerStream_147` and `_152` (in `BIOA_NOR10_07`), under
no plain state. `mevr stream state <name> on|off` switches a state the game's own way: a loaded `BioSeqAct_SetStreamingState`'s
`Activated` through ProcessEvent, one already holding the name and value if there is one, else the first with ours put on it for
the call and its own put back (journalled); with none loaded, the world's entry written and every volume's `Retouch` called
[verify]. The name is taken from the loaded actions, volumes and world states (no name-table lookup: a name nothing loaded knows
can't switch anything). `mevr stream states` lists the world's entries. The scan records every volume's states (`kind: volume`, 998
volumes) and the catalogue gives each start `touchShows`, `volumeShows` and `streamStates` (a `_DS1` sub-package counts as its
parent chunk, the persistent package as always there): **104 of the 713 starts sit in a chunk no plain touch shows, 88 of them
with a named state** (NOR00 39, WAR00 37: `PostThorian`, STA00 21, END00 4, UNC52 3); 16 are listed by no volume state at all
(`UNC52_01`, the squadmates' `NOR10_09*_DS2` rooms, the Citadel's `*news` chunks: another route, probably the plot's own level
streaming actions). The launcher reads the live `mevr stream list` first (a plain state listing the chunk: touch; a named one: the
state on, then that volume touched), the catalogue's `streamStates` when no loaded volume lists the chunk. To run:
`tools/conversation-launch.ps1 jug20_sal_captainkirrahe` (the wait and the raised teleport) and `tools/conversation-launch.ps1
nor10_oculon_approach` (`stream_wakeup` on, then `BioTriggerStream_147`).

### A save instead of open (2026-11-09, later)

The owner: `open` lands in the wrong plot state (the Citadel arrival didn't trigger; a scene's start conditional refused a replay),
"a better system for launching into the citadel with a custom generated save". Trilogy Save Editor (KarlitosVII, CeCILL-2.1) is the
only LE1 save editor and has no command line, so we read its structs (`src/save_data/mass_effect_1_le`, `src/unreal`; fetched into
`reference/` by `tools/fetch-references.ps1`) and wrote our own **`tools/le1-save.py`** (MIT; NOTICE.md lists the source):

- **The format.** A container (magic `C1 83 2A 9E`, block size 0x40000, a total (compressed, uncompressed) pair, one pair per zlib
  chunk of at most a block, the chunks, then CRC-32/BZIP2 of everything before it, the compression flag 1 and the payload size
  again) around Unreal's flat serialization (bool = u32, enum = u8, string = i32 length with its 0, negative for UTF-16, arrays and
  maps a u32 count, option = bool then value): version 50, character id, dates, the plot (booleans as u32 bit words, integers,
  floats), journal, codex, the player and squad, `base_level_name` (the persistent level, `BIOA_STA00`), `map_name` (the **area**,
  `MapNOR01`, `MapSTA30_02`: the Load screen's place name and picture), `parent_map_name` (empty in every save), the place
  (`location` in units, `rotation` in 65536ths), the display text and the file name, then the world state the game keeps per map
  it has visited (`legacy_maps`: every level's saved actors, the `BioWorldInfoSaveObject` with the world's streaming states;
  absent in a character export). The tool reads all of it into Python values and writes it back: **all 12 of the owner's saves
  round-trip byte for byte** (payload and file; the game's deflate isn't zlib's, so an unchanged payload keeps the game's own
  chunks and a changed one is recompressed, which the game loads). `info`, `roundtrip`, `plot --bool N`, `write <in> <out>
  --level --area --location --yaw --bool N=1 --int N=V --float N=V --display-name --drop-map`; ctest `mevr-le1-save`
  (`tests/Le1SaveTests.py`: a synthetic save with every struct and one legacy object of each class, multi-chunk, wide strings,
  the edits, refused corruption; `MEVR_LE1_SAVES=<folder>` adds a local folder's saves; no player's save is committed).
- **The display text is the Load screen's record.** `BioSaveGame.SaveGameRecord` (`m_SaveGameNumber`, the date, the area id, class,
  level, time played) is read from the fixed-width display text (` 21        John 2026/11/09 10:48:37 ... John02   0  NA 00 007
  1h34m`), the place name and picture from `map_name`; `m_filename` is the file's name. A write stamps the time now (in the text
  too) and the file name, and a numbered file (`<career>_<n>`) gets its slot in the text.

**Measured in the game** (fake headset, John02, the deployed hot-reload build, 2026-11-09 14:16-14:50; saves backed up and hash-checked
after, nothing written by the game):

- **The title's Enter resumes the profile's last save**, never a newer one: `PROFILE\GamerProfile.pcsav` ends with the career and
  file name (`John02`, `John02_AutoSave`); a save with a newer time in its text and a newer file time (slot 98, `_MEVR`) was passed
  over three times, before and after a restart. The cheat `BioLoadGame <slot>` (`BioCheatManager`) does nothing in this build.
- **The Load screen lists a career's saves once, at the game's start** (`BioSFHandler_LoadGame.lstRecords`): a file written
  later isn't listed (slot 22), a file deleted later still is (slot 98), `RebuildLoadGameList` doesn't re-read the folder. Its
  content is read only when it's loaded. Non-numbered names are listed (`John02_MEVR`), so the launcher's fixed
  `<career>_MEVR.pcsav` works: once it exists when the game starts, later runs just rewrite it.
- **The Load screen can be driven**: `mevr gui Menu` (the pause menu), down + A (its second entry is Load, on foot and in the Mako),
  then down + A on the list (no record has the focus before a pad direction), and the confirm box's handler says which record it
  is for (`BioSFHandler_MessageBox.nInputCallbackContext` = the record's index): B in a request of its own, the focus moved by the
  difference, A again, then A to confirm. The launcher does this (`Load-ThroughMenu`).
- **A loaded save is placed exactly**: Avina's save (`sta20_avina_keeper`, Presidium `STA20_02A`) loaded on foot within 9 s at the
  written x, y, z 20 units above Avina's own height, dropped onto the floor (z 317 to 286), facing 180 degrees, Avina at her mark;
  the docking bay save (`sta30_normandy_mechanic`, C-Sec's bay `STA30_08`) the same, facing 298. **The chunks stream by themselves**
  from where the player is (no volume touched, no fall), unlike a teleport after `open`.
- **The plot is the career's**: Avina's start (`AvinaWithKeeper`) played its conversation (game mode 5); the mechanic's start
  (`MechanicKeepsPlayerFromEntering`) fired and played nothing, because John02 has been past the Normandy's bay since its first
  visit (its starting entry's conditional). From `open` both would have run in the story's opening plot.
- **The Citadel arrival can't come from John02**: its plot is past the Citadel (Garrus and Wrex in the squad, the Normandy docked).
  The arrival is the Presidium embassy's (`BioTriggerStream_97`'s state `stream_normandyToEmbassy` shows `BIOA_STA20_A_Arrival_CIN`
  and `STA20_10C`), so it needs a save between Eden Prime's end and the Citadel (the Normandy after the debrief): none of the owner's
  careers has one (John01 and VR00 are on Eden Prime). Next: play John01 to the Normandy and save there, then `-Save` from it;
  setting the arrival's plot booleans on a later save is guesswork until the plot database (Trilogy Save Editor's
  `databases/me1_raw_plot_db.ron`, its names) is read for them.

**The launcher's `-Save <career save>`** (as first built, the Load screen; superseded the same evening by `QuickLoad`, below,
kept as `-Load menu`): writes `<career>_MEVR.pcsav` beside it (the catalogue's `map` as the level, Shepard 1.5 m in front of the
owner facing it), after a backup of the Save folder into `%LOCALAPPDATA%\MEVR\save-backups\<time>`; with the game running it loads
it through the Load screen, waits for Shepard on foot near the place, then streams the chunk and fires the start as `-NoOpen` does.
The first menu run of a career with the game running fails with "the Load screen doesn't list ...: restart it". `-Area
MapSTA30_02` sets the area (the Load screen's label; kept from the career save otherwise).

### Replaying a beat (2026-11-09, evening)

The owner: "vital for our testing to be able to replay some of the approach conversations". A save built from scratch isn't
needed: the plot is small and a beat changes a handful of values (28 minutes of the Citadel in John00 set 21 booleans and 8
integers, nothing cleared), so a beat is rewound by putting back only what it set.

- **`tools/le1-save.py diff <before> <after> --rewind-out beat.json`** lists every plot boolean, integer and float that differs
  (with the plot's names from Trilogy Save Editor's `databases/me1_raw_plot_db.ron` in `reference/`), and each visited map whose
  kept world state differs, by level; the rewind file holds the values before (and each changed map's state before, as its
  serialized bytes). **`write <later> <out> --rewind beat.json`** puts the plot values back; `--with-maps` also the maps (which
  undoes everything else done in them since). The launcher's `-Rewind beat.json` (`-RewindMaps`) does it on its save. Unit tests in
  `tests/Le1SaveTests.py`.
- **The after-save comes from the console**: `QuickSave` (`BioBasePlayerController.QuickSave`, an exec) writes
  `<career>_QuickSave.pcsav` and the profile; the cheats `BioSaveGame`/`BioLoadGame <slot>` do nothing, and the pause menu is a wheel
  (`BioSFHandler_BrowserWheel`: down Load, up Codex, left the character record, right Options; Save is no pad direction).
- **`QuickLoad` loads the career's quicksave from the console, whatever the game listed at its start**: a quicksave deleted and
  written fresh by the tool (a file the game never saw) loaded 0.8 s after the command (`world ui: ... player/world changed`, a new
  player pawn), at the file's place, after Shepard had been moved 4 m away. It loads the career that is loaded
  (`BioSaveGame.m_sCharacterID` of `Transient.CurrentGame`). So the launcher's default is now `-Load quick`: the save is written as
  `<career>_QuickSave.pcsav` (never over a quicksave the player made, unless `-ReplaceQuickSave`) and `QuickLoad`ed; no menu
  driving, no restart. The files it creates are listed in `save-backups\generated.txt`; `-RestoreSaves` puts back the oldest backup
  since the last restore and deletes exactly those.
- **Only once the game knows the career has a quicksave** (found by the conversations session the same evening, for the medical
  bay's `nor10_oculon_approach` from John02_21): with no quicksave of the game's in the career, `QuickLoad` of the tool's file
  shows the game's box "The Save currently selected cannot be accessed" (mode 7) and loads nothing, in a session with levels
  opened from the console and after a clean restart alike; after the console's own `QuickSave`, the tool's file written over it
  loaded at once. (The fresh file above loaded because the game had made a quicksave earlier in that session.) So the launcher
  runs `QuickSave` itself before writing (the file then counts as one it created when the career had none) and names the box if
  it still comes.
- **A visited map's streaming states** (`BioWorldInfoSaveObject.streaming_states`, `info` lists those on): `write --stream-state
  BIOA_NOR00:stream_wakeup=1` sets one or adds it. Whether the game streams a level in from a state set in the save is [verify]
  (the medical bay's wake-up level stayed out with the live `stream_wakeup` state on).

**Measured** (fake headset, John02, 15:32-15:49; saves, profiles and inis hash-identical to the run's backup afterwards):

| play | how it was loaded | flag 4019 before | lines | wheels | seconds |
|---|---|---|---|---|---|
| first meeting | the Load screen, the career's own plot | 0 | 5 | 2 (the first at once) | 19.8 |
| repeat, no rewind (control) | already in the level | 1 | 6 | 1 (after 5 lines) | 23.7 |
| rewound | the Load screen | 0 | 5 | 2 (at once) | 19.8 |
| rewound | `QuickLoad` of the game's quicksave, rewritten | 0 | 5 | 2 | 19.3 |
| rewound | `QuickLoad` of a quicksave the tool wrote fresh | 0 | 5 | 2 | 19.3 |
| rewound, launcher run 1 to 3 | `-Save John02_QuickSave -Rewind avina.json` | 0 | 5, 5, 5 | 2, 2, 2 | 19.3, 19.8, 19.3 |

The beat (Avina, the Presidium's VI) set `FIRST_TIME_TALKED_TO.Avina` (4019), two codex entries (5814, 5815), the dialog's
`UseAtEnd.Gen0` (3365) and cleared `NoncombatZoneActive` (5534), banter zone integer 69 to 0; the rewound play set exactly the same
values again. The first meeting replayed 6 of 6 times, the repeat branch played when not rewound. Each launcher run took about 45 s
from the command to the conversation's end (the replies picked by `mevr convo pick`/`skip`).

Not yet tested: a beat whose rewind needs the map state (someone killed or moved by it: `--with-maps`), a beat that changes levels
or the squad, other careers and levels than John02 in the Citadel, and the Citadel arrival (needs a save from before it).


## Flowing conversations: the walk-up scenes as the game's own barks (2026-11-09, evening)

The owner, on the first Citadel visit: "I'll walk up to NPCs and it will start an encounter, like meeting Wrex or saving Tali.
I want these to allow free character movement (within bounds). A lot of the times they will chain where we don't have any vocal
interaction and then we have a small conversation option available." Of the two ways (the mod moving Shepard's pawn inside the
conversation mode, or the game's own ambient path) the owner chose the cheaper, riskier one: "some of the encounters would be
interesting to see if you could do the cheaper but riskier. The ideal case is that the conversations flow really easily."

**How the game decides a conversation is ambient** (Ghidra, UBioConversation's vtable at RVA 0x131EF20; the natives behind the
exec thunks `execUpdateConversation` 0xB909B0 and `execswitchToAmbient` 0x371570):
- `StartConversation` (slot 0x2D0, 0xC86B00) picks the start entry from `m_StartingList` (the first whose conditional holds),
  then copies that entry's `bAmbient` into the conversation's `m_bAmbient` (an XOR bit copy on +0x2C0, bit 0x200). Not ambient:
  it tells the player controller's HUD object to enter the conversation mode (a ProcessEvent with a byte 5: game mode 5) and
  sets the camera up. Ambient: no mode, no camera; it calls `switchToAmbient` (slot 0x318, 0xC892B0: hides the conversation GUI's
  parts and shows it as ambient) and sets the GUI's subtitle style to 4 (`SUBTITLE_RENDER_AMBIENT`).
- The entry start (0xC7DCD0, the function that plays the next entry) copies the new entry's `bAmbient` only while the conversation
  is NOT ambient, and only honours a switch to ambient for an entry with no replies (the last line) whose speaker isn't in the
  party: that is the game's own "last line plays as you walk away" (the 204 conversations that mix ambient and ordinary lines end
  on ambient ones; none begins ambient and turns into a conversation). An ambient conversation stays ambient to its end.
- `UpdateConversation` (slot 0x2E0, 0xC87830): an ambient conversation is interrupted (the controller's slot 0x9F0) when the
  conversation's player is farther than `m_fInterruptRange` from the speaker; the range is the start action's
  `BioSeqAct_StartConversation.m_fInterruptRange`, else (0) `BioWorldInfo.m_fConversationInterruptDistance` (BIOGame.ini);
  -1 never. Skipping and the reply display run only for non-ambient conversations.

So a start whose first entry is flagged ambient plays as the game's own barks, in the ordinary game mode, the player walking;
the whole conversation then plays that way, so only starts that never need the wheel qualify. **A wheel shows at an entry with a
`REPLY_STANDARD` reply**; auto-continue and dialog-end replies go on by themselves (the ambient barks of the game end in
`REPLY_DIALOGEND` replies: Noveria's salarian suicide trigger).

**The scan reads the graph** (`conversation-scan` records every entry's `bAmbient`, speaker and `ReplyListNew`, every reply's
`ReplyType` and `EntryList`, and `m_StartingList`; `conversation-scan.py --flow`): per start, the entries a play can reach; a
start that can't reach a standard reply is a **chain**. The owner's examples are chains inside wheel conversations, as separate
starts: `sta70_garrus` start 0 ("Garrus and Executor argue": Garrus's line, the Executor's, no replies) against start 2 ("Player
speaks to Garrus", the wheel at once); `sta30_chellik` 0-1; `sta30_krogan_cutscene` 23-29 (Wrex at C-Sec); Udina's arrival
`sta20_human_ambassador_enter` 35-37 and 89-94; `nor10_joker` 0 and 25-31. Of 404 conversations a level's script starts, 307
have a chain start; 102 of them have chain lines the game left ordinary (526 entries), the rest are ambient already. With this
the scan's `ambient` kind is every line ambient whatever the replies (182, not 57: the barks with auto-continue chains counted
as `free` before); the kinds are now free 78, stage 171, remote 19, walks 23, framed 18, ambient 182.

**`--flow-list` generates `cw::FlowEntries`**: per conversation the chain starts' entries that no wheel start reaches (a shared
tail would end a wheel conversation ambient), that aren't ambient and whose start the game can take (no unconditional start
before it, below); set pieces (the 3D screen), transmissions (their hologram
needs the stage an ambient play skips) and the narration under the game's own screens (`nor10_backgrounds`, `ice00_popup_text`,
`sp_news_vids`) are left out: 68 conversations, 284 entries (102 and 526 before the start-order rule; **64 and 249 since research/69's item 1**,
which also leaves out the chains that apply plot effects after their first line). The mod (`game/ConversationFlow`, `[Cinematics] worldFlow = all | off`, default all, only with
`conversations = world`) scans the loaded `BioSeqAct_StartConversation` actions (at once, 2 s after the object table grows by a
thousand, every 10 s) and flags those entries' `bAmbient` on their conversations before the level's script fires them
(journalled `ConversationFlow.entryAmbient`; an entry that offers a standard reply in the live data is refused and logged: the
table is from the shipped packages). The flags are put back when the setting goes off or the mod unloads. The live conversation
is watched: a flagged one playing ambient logs `convo flow: <name> plays as barks (ambient): the player stays on foot;
interrupted beyond N m of the speaker`; `worldFlowLeashM` sets the live conversation's `m_fInterruptRange` (-1 never, else
metres; 0 the game's). `mevr convo flow [list | rescan | restore]`.

What this gives: the walk-up scene plays while the player walks and looks (bounds: the interrupt range; the game's default
`m_fConversationInterruptDistance` is 2500 units, 25 m, in BIOGame.ini, where a start sets none), the subtitle in the ambient
style on the HUD panel, the wheel conversation that follows as First
person has it. What it costs: the stage's placement of a chain (Udina and Anderson put on their marks, the squad with them) is
skipped, so the people stay where the level left them; a chain's authored matinee per line fires through the conversation's
script list whatever the mode [verify]; a chain interrupted by distance ends early (the game's own rule for barks). Walking
during a conversation that reaches a wheel is NOT this (the entry start never turns an ambient conversation into a conversation):
that needs the mod moving the pawn inside the conversation mode, the other way.

### In the game (2026-11-09, fake headset, the owner's John02 career)

- **The race.** `tools/conversation-launch.ps1 sta70_garrus` (its first placed start is "Garrus and Executor argue") opened the
  Citadel, streamed C-Sec Academy and fired the scene: it began in the conversation mode, and the flags landed 3 s later. Two
  faults: the rescan waited 2 s after the object table grew, and **the object table's count never moved for the chunk** (176834
  before and after: its objects took freed slots), so only the 10 s periodic scan ran. Now the scan runs when the streaming
  levels' loaded and visible set changes (`LevelsSignature`, at most twice a second), and the flags land within a second of
  the chunk (Noveria: flagged 13:48:45.9, the chunk's volume touched seconds before).
- **Which start the game takes.** `UBioConversation::StartConversation` takes the first entry of `m_StartingList` whose
  conditional holds, so a chain start after an unconditional start never plays (`sta30_krogan_cutscene`: starts 0 (plot 418),
  9 (none), 23 (ours, none): 23 is never reached), and a chain start with a plot conditional plays only in the right plot state
  (`sta70_garrus` start 0, conditional 4401, refused the re-fire after the scene had played: `m_bFailed`; `sta30_chellik` took
  start 22 in this career). The scan records each entry's `nConditionalFunc` now; 69 of the 102 first-pass conversations had their chain behind an unconditional start and are out of the table now (68
  conversations, 284 entries); 18 have the chain as their first start, 6 of them unconditional (the Juleth ambush and the Olar execution on Noveria, Ilos's security system, the
  Bring Down the Sky batarian triggers, two Shepard-plot triggers).
- **Two chain plays as barks.** `ice50_trig22_juleth_ambush` and `ice60_trig12_olar_execution` (a staged two-line scene), fired
  by the launcher with the flags in place: `convo flow: ... plays as barks (ambient): the player stays on foot; interrupted
  beyond the world's default 25.0 m of the speaker` 12 ms after the fire, the game mode 0 throughout, no conversation object
  for `game/ConversationWorld`, the frames plain first-person gameplay with Olar standing in front of the player (snapshots
  `13-49-5*`); afterwards the conversation reads `m_bConversationEnded` and `m_bInterrupted` (the ambient end goes through the
  interrupt path). **No subtitle showed on the HUD** in the frames one and two seconds in [verify]: the ambient subtitle style
  (`SUBTITLE_RENDER_AMBIENT`) against this profile's subtitle setting, or the lines' length.
- **Parts A and B on a staged conversation** (the Garrus conversations in C-Sec Academy): `the opening ended after 0.0 s: 0
  placements accepted; the player's place re-placed ... toward BioPawn_6 (0.52 m from where it was)`, arm IK and body follow
  on, the wheel up at 4.7 s; nobody needed a seat. The Citadel arrival itself didn't trigger from this career's `open BIOA_STA00`
  (its plot is past it): the arrival's Parts A/B and its chain starts (35-37) remain [verify] from an arrival save.
- **The fake headset needs SteamVR's manifest pinned** (`[Debug] openxrRuntime`, `tools/fake-headset.ps1 -On`): the machine's
  active OpenXR runtime was Virtual Desktop's and `xrGetSystem` answered -35 twice.

A generated save would make these checks repeatable from the right plot state (the owner, 2026-11-09: "a better system for
launching into the citadel with a custom generated save"): Trilogy Save Editor (KarlitosVII, CeCILL-2.1) edits an LE1 save's
map name, location and rotation but only through its GUI, so a save writer of our own from its `src/save_data/mass_effect_1_le`
format is the way; a separate job.

## Walking during conversations (2026-11-09, evening)

The owner, choosing the ambitious route: a conversation should be "actually within the game itself and not a dedicated
experience or the camera back and forth with different placements"; the walk-up scenes play in the game (the flow, above),
and "when we're talking that I can somewhat move around, but we may have some sort of barrier to ensure that the conversation
just doesn't get ejected".

**What ejects a conversation.** Nothing by distance: `UpdateConversation`'s interrupt range applies to ambient conversations only
(above). The stage re-places Shepard at camera cuts, which keep-in-place already undoes; the wheel takes the left stick while it
is up. So the barrier is ours, for sense and safety: a leash around the conversation's owner, the floor and the walls.

**Built** (`[Cinematics] worldMove = 1`, default on with the world mode; `worldMoveLeashM` 3.0; `worldMoveSpeedMS` 1.4):
in the world mode, after the opening and while the wheel is down, the left stick moves Shepard's pawn along the head's world
facing (`engine::OrientationToWorld` of the host view's head pose in the held frame) at the walk speed (a gentle push walks
slowly: the deflection past a 0.15 dead zone), inside the leash: a step that would leave the circle slides along it, a place
already outside (the owner moved) may only come nearer (`cw::StepPlayer`, unit-tested). Before the step: the floor under the
target by `Actor.Trace` (Part B's `FloorZ`), the pawn's height above its floor kept, a drop or rise past 60 units refused (a
ledge, a step); the shots' `LineOfSight` from the pawn's place to the target refused by a wall; `Actor.SetLocation` (what
keep-in-place uses), its own refusal respected. After the step everything that defines the player's place moves by the same
vector: the held view (`gC.hold`, so the host's render, the game's camera written into the POV and the people's turning
follow), the frame handed to `view_anchor::Hold` (arm IK, body follow), Shepard's head-bone anchors (`FollowHeadSettled` never
reads the walk as a jump) and his kept place in `gStay` (keep-in-place puts him back where he walked, not where he started).
The pawn's `Velocity` is set to the step's (body follow's legs animate above 60 u/s; whether the conversation's animation tree
lets the legs run is [verify]), cleared when the stick rests, the wheel opens or the conversation ends. Logs `convo: at N s the
player walks: ...` once, and at the end `convo: the player walked N m in K steps; the leash held L steps back, R refused`; the
summary's `playerWalkedM`, `leashHits`, `moveRefused`.

**Not done here:** the squad doesn't follow a walking Shepard during the conversation (they stand where Part B seated them);
the NPCs turn toward him (keep-in-place's turn, `worldTurnDegPerS`) but don't step; no visible barrier at the leash (a haptic
tick or the vignette could mark it); the stick's dead zone and the speed are untried in a headset. [verify] in the game: walk
during the Garrus conversation at C-Sec (the launcher: `sta70_garrus`, its wheel start) with `mevr` replay inputs or a real
stick; the kept place and the hold staying together across a camera cut; the subtitle and the wheel following the walk.

**In the game (2026-11-09, the fake headset).** `mevr convo walk <x> <y> [seconds]` stands in for the left stick (tests without
controllers; the replay's controllers read inactive to `vr::controllers::Latest()`, so the replayed stick never reached the walk),
and without a VR view the facing is the held view's yaw (the on-foot frame fallback runs `MovePlayer` too). The Garrus wheel
conversation at C-Sec Academy (`tools/conversation-launch.ps1 sta70_garrus`: the argue chain played as barks, the game started
the wheel conversation 58533 itself; a reply picked with `mevr convo pick 1`, then `walk 0 1 6` and `walk -1 0 4`):

- `convo: at 91.5 s the player walks: the stick (0.00, 1.00) (mevr convo walk) along the facing 160 deg, 3.67 m from BioPawn_6
  (leash 3.0 m)`: the opening had placed the player 3.67 m from Garrus, outside the leash, so the first steps were the "only
  nearer" rule; the pawn went from (3768, -7073) to (3254, -6888) while the conversation ran on (4 shots, the speaker
  BioPawn_6), Garrus and the Executor (BioPawn_13) standing where they were, and the two drawn frames after the walk show the
  C-Sec hall from the new place at eye height, nobody snapped: `0 jumps onto marks undone`, no `kept in place: the game put
  Shepard`, no `JUMPED`.
- The end summary: `the player walked 5.47 m in 121 steps; the leash held 0 steps back, 0 refused (no floor, a ledge, a wall,
  SetLocation)`; `SFXPawn_Player_3: moved at most 5.47 m, turned at most 0`; the conversation ended by the game's own leaving of
  its mode after 103 s, not by distance.
- Seen and to fix: the walk went straight through Garrus (3.67 m on one side to 1.8 m past him: the leash has no inner radius,
  and `SetLocation` doesn't collide), so a person in the way needs a minimum distance; the leash was entered from outside, so the
  slide along the circle is still unobserved in the game; the squad didn't follow; the legs' animation during the walk is unseen
  (no third-person frame). The 60-unit floor rule and `LineOfSight` refused nothing on the flat hall.

**Not through people** (the same evening, the owner's yes; `worldMoveClearM` 0.7, 0 off): each step's end is checked against the
live places of the conversation's actors, the squad and the owner (`cw::KeepClear`, unit-tested): within the clearance of one of
them it is pushed out to the clearance along the line from that person, the leash's slide turned inside out, so the stick walks
the player around them instead of through; a player already that close (the person walked up to him) may only step away. The
end summary counts the steps a person held (`people in the way N`; the summary's `clearHits`). [verify] in the game: walk at
Garrus and watch the pawn go around him at 0.7 m; a squadmate seated by Part B in the way of the walk.

## Limits and next

**The work toward making this the default for every conversation has its own note:
[research/69](69-conversations-everywhere.md)** (story safety of flowed scenes, the cutscene-or-world decision at run time,
placements stopped at the source, walking and triggers; a status table and a log).

- **28 conversations played, 491 scanned** (the survey and the scan, above). The scan's kinds are read from the data; the
  runtime numbers (jumps undone, drawn) are from 28. Next in the game: the 18 framed set pieces and the 23 walks, from the
  scan's lists, started by `tools/conversation-survey.ps1 -Filter <name>` on their map.
- **NPC teleports at cuts**: undone since the evening (above); Shepard's own body snapping back onto his pawn after a cutscene
  still moves the player once.
- **Reverse shots are lost** (half the shots): the player sees whom Shepard faces, not Shepard. Intended.
- **The depth of field is a global switch**: if the game dies mid-conversation, `BIOEngine.ini` keeps the False the game wrote.
  A post-handler on the conversation camera's `ModifyPostProcessSettings` (its `bEnableDOF`) would be scoped to it [verify].
- **Subtitles during spoken lines** [verify the option]; the wheel at the left hand with tracked controllers; the first frame.
- [verify] In the headset: scale and presence (the owner's question), people staying where they stand (someone talking from
  behind you), the smoothed turns, the one remaining jump of your own place, the subtitle's size and place.
