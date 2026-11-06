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
  pActor) and put back at the end. So **a conversation with a stage can move Shepard and the NPCs onto marks**; a camera cut
  hides the move, first person would show it. The measurements below say which conversations do.
- **The camera** is `BioCameraBehaviorConversation` (fixed or procedural shots: `m_vFixedCamPosition`, `m_vProceduralCamPosition`,
  `CAMERA_FOV`), chosen per line by the stage (`GetBestCameraPosition`, intimacy levels, the line of action `vLineOfAction`).
- **The wheel** is the conversation movie's handler, `BioSFHandler_Conversation` (`m_bDisplayingWheel`, `m_aReplyLocations[6]`,
  `SelectConversationEntry(nWheelLocation)`), on the GUI manager's panels. Its movie (`GUI_SF_Conversation`) is a 1280x720
  stage: `SubtitleConversation` at (639, 605.6), `SubtitleTop` (640, 159), `SubtitleBottom` (639, 703), `ConversationWheel`
  (639.7, 643.3) (Halcyon's `pchud.cpp`, from the movie's SWF).

## The prototype

`[Cinematics] conversations = world` in `ASI\LE1MEVR.ini` (`vr`/`0`, `screen`/`1` as before; `mevr convo world on|off` for a test
without the file). Mod: `game/ConversationWorld`; host: `plugin/ConversationWorld`; shared: `core/vr/ConversationWorld`
(unit-tested: `tests/ConversationWorldTests.cpp`); HostServices v33 `setConversationWorld`.

- **The view.** Engine.Camera.UpdateCamera's post-handler records Shepard's view on foot (the POV in mode 0) and, in mode 5,
  where the POV lives. During the conversation the mod sends that view (moved with Shepard's pawn if the stage moves him,
  `worldFollow = 1`) and the POV's addresses; the host swaps it into the POV around the main view's `CalcSceneView` only
  (`GalaxyViewSwapBegin`, the galaxy map's and the world screens' seam), so the director's camera still serves the game.
- **On foot's presentation.** `MevrBridge::ConversationInWorld()` in three `[MEVR]` seams: `CineFinishDecision` (mode 5 is not a
  cine: no VR cine, no 3D screen, head look on), the second eye's gate (published as gameplay), the conversation movie's
  transform (its authored layout, as on the 3D screen). One in `head_aim.cpp`: Halcyon's first-person hides stay up through the
  conversation camera (Shepard's head hidden from his own eyes).
- **The body.** `view_anchor::Hold` with the on-foot frame (moved like the view): arm IK and body follow run as on foot
  (`arm_ik::OnFoot`).
- **The UI.** In mode 5 with the world view up, the game's UI goes to the HUD panel (HalcyonOverlays' `uiToPanel`), the
  subtitle's band and the wheel's band are cut out of it: the subtitle on a quad under the speaker's face (its head bone,
  `subtitleBelowM` 0.22 under, `subtitleTowardM` 0.15 toward the eyes, `subtitleHeightDeg` 3 tall at any distance; Shepard's
  own lines under the one he talks to), the wheel on a quad above the left hand (`wheelHand`, `wheelWidthM` 0.40,
  `wheelDistanceM` 0.12), placed when it appears, steered by the left stick as before. They reuse the interaction label's and the
  game wheel's swapchains (neither is up in a conversation; the runtime limits swapchains, research/25 section 10). Crops by hand:
  `subtitleCrop`, `wheelCrop` (x y width height of the HUD frame).
- **Measurements, whatever the mode** (`convo:` in the log): each conversation's actors at the start (place, facing, head bone),
  the stage and the saved places, each move or turn of an actor (JUMPED past 30 units in 0.1 s), who speaks to whom with the
  speaker's head and body angles to Shepard's eyes, the lines, who looks at whom, the wheel, and each shot of the director's
  camera classified against Shepard's place (from his eyes, over his shoulder, on an NPC visible or blocked from his eyes, on
  Shepard himself, a cutaway); a summary at the end. `mevr convo` prints the conversation now. Snapshot `"mod"."conversationWorld"`.
