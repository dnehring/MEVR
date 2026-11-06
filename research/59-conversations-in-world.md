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
  past 0.40 m (`FollowHead`, `worldFollow = 1`), never its small moves and never the pawn's turns: the player's real
  head replaces them, body follow turns the body under the view.
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

## Limits and next

- **One conversation measured.** Eden Prime's Cole (`UsedBy` on the pawn starts nothing; Shepard was then killed by the geth)
  and the Normandy's other people (`SFXGameModeDefault.Used` needs the gaze selection on the NPC) weren't reached. Next: Pressly,
  Joker, Nihlus on the Normandy, a Citadel conversation with cutaways; `mevr convo` logs each.
- **NPC teleports at cuts are visible** (Ashley's 2.9 m, Anderson's three). A fade over a jump past `kReanchorUnits`, or
  hiding the pawn for the cut, is the next design question; Shepard's own jumps move the player with him (a pop too).
- **Reverse shots are lost** (half the shots): the player sees whom Shepard faces, not Shepard. Intended.
- **The depth of field is a global switch**: if the game dies mid-conversation, `BIOEngine.ini` keeps the False the game wrote.
  A post-handler on the conversation camera's `ModifyPostProcessSettings` (its `bEnableDOF`) would be scoped to it [verify].
- **Subtitles during spoken lines** [verify the option]; the wheel at the left hand with tracked controllers; the first frame.
- [verify] In the headset: scale and presence (the owner's question), comfort at the pops, the subtitle's size and place.
