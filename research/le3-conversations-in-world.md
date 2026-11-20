<!-- SPDX-License-Identifier: MIT -->
# ME3 conversations in the world, first person: the guide to finishing it on the shared module

Lane FPD3, 2026-11-19. Docs only: no source changed. This note is the implementation guide for ME3's half of T49 (first-person
conversations in the world, G36 in the parity ledger). Lane FPC owns the code (`game/SequelConversationWorld`, its ME2 extension
and the shared core `core/vr/ConversationWorld`); this note says what ME3 needs, what the slice on main does against ME1's rules,
and where ME3's data shows an ME1 rule is too narrow.

**Evidence levels.** Everything here is offline: the LE3 Ghidra project (`tools/ghidra/decompile.ps1 -Game LE3`, project
`%LOCALAPPDATA%\MEVR\ghidra\LE3.gpr`, exe timestamp `0x60AEF0C4`), the installed exe's `.rdata` read for vtable slots, LExSDKv2's
LE3 metadata (names and offsets, for ReflectCheck cross-checks only), the conversation slice's source and commit messages, and a
scratch scan of ME3's installed packages with Legendary Explorer's library (3,786 packages, 2,036 distinct conversations; output
kept in the scratchpad, game data, never committed). Nothing was run in the game for this note. Everything a game run must confirm
is marked **[verify]**. Decompiled functions are described, never quoted.

Contents:

1. [The slice on main, audited rule by rule](#1-the-slice-on-main-audited-rule-by-rule)
2. [ME3's conversation runtime against ME1's](#2-me3s-conversation-runtime-against-me1s)
3. [The natives: ME1's three hooks on ME3](#3-the-natives-me1s-three-hooks-on-me3)
4. [ME3's scenes, counted, and ME1's mechanisms for each](#4-me3s-scenes-counted-and-me1s-mechanisms-for-each)
5. [Where ME3's data shows an ME1 rule is too narrow](#5-where-me3s-data-shows-an-me1-rule-is-too-narrow)
6. [The renderer's decision points and the seams](#6-the-renderers-decision-points-and-the-seams)
7. [The work, in order](#7-the-work-in-order)
8. [Fixtures](#8-fixtures)
9. [Questions for the owner](#9-questions-for-the-owner)
10. [How the numbers were made](#10-how-the-numbers-were-made)

---

## 1. The slice on main, audited rule by rule

**What it is.** `src/game/SequelConversationWorld.{hpp,cpp}` (ME3 only, `MEVR_GAME_LE3`), commits `bff358d3` (core slice),
`18225e72` (stereo, Shepard kept, the wheel fixed in the room), `eabfc5f7` (ME1's opening, walking, the wheel on the right stick),
`1e8927f6`, `fef3f831` (no depth-of-field switch around a load), `950abe3f` (a conversation opened around a load stays on the
3D screen), `5d5080e3` (`mevr convo world on` holds it). Host: `plugin/WorldUi` (`ConversationShown`, `HoldConversationBegin/End`,
served through `MevrSeams::WorldViewHoldBegin`, `WorldViewHeld` 1, the new `MevrSeams::ConversationInWorld`), `plugin/ModLoader`
(HostServices v33 `setConversationWorld` on LE3), `plugin/PadMerge` (LE3 only: the right stick drives the game's left stick while
a conversation is held), `src/halcyon/le3/me2_xr.cpp` (`cine = false` while `ConversationInWorld`; `OnViewBuilt` reports a world
view), `src/halcyon/le3/me2_menu.cpp` (upstream's `ConvoFirstPerson` pinned off in `ApplyMevrBaseline`).

**The owner's verdict (2026-11-19):** "wasn't complete. It was enough to view some scenes in VR." So it is audited here as an
unfinished foundation. Verdicts: **right** (ME1's rule, done as ME1 does it or an equivalent), **partial**, **wrong** (does
something ME1's rule says not to, or breaks a kind of scene), **missing**. "Forced" says whether ME3 forces a difference.

| # | ME1 rule (where on ME1) | ME3 slice | Verdict | What ME3 needs | Forced |
|---|---|---|---|---|---|
| 1 | Which scenes: game mode 5 + `[Cinematics] conversations = world` (`game/ConversationWorld`) | mode 7 via `engine::adapter::IsConversationMode`; the live conversation through `BioWorldInfo.m_pConversationManager` + `GetFullConversation`, not ended | **right** | keep | the manager is forced (section 2) |
| 2 | Set pieces to the 3D screen (`cw::IsSetPiece`, 13 names; `worldSetPieces`) | none: every full conversation is held | **missing** | an ME3 table, **not** ME1's scan criterion (section 5.1) | criterion yes |
| 3 | Presence rules: no place for the player goes to the 3D screen (`worldAutoScreen = on`: pawn hidden, far from the owner and out of sight) | only "opened around a load" (not 3 s on foot before it) stays on the screen | **partial** | presence by data: no `Player` group in the conversation's matinees, Shepard hidden, the mark in another space (section 5.2) | no |
| 4 | Opening (`cw::DecideOpening`, `worldOpening = auto`): Stay / WalkIn / Cut from the mark against where the player stood, line of sight, floor, black screen | `DecideOpening` called with `markUnits`, `riseUnits`; `sightToMark = true` hard-coded; no `floorAtMark`, `shepardHidden`, `peopleMovedUnits`, `coveredByBlack` | **partial** | read the start action's `m_bBlackOut` / `m_bFadeIn` (`coveredByBlack`), trace the sight line, the floor; settle on the first drawn world frame, not a 0.35 s timer | no |
| 5 | A Cut opening dips from black (`cw::State::cut`, `Fade::Cut`, plugin/TransitionFade) | `s.cut` is published (`gC.follows`) but nothing on LE3 dips on it | **missing** | the host's TransitionFade on LE3 must read the conversation's `cut` counter as LE1's plugin/ConversationWorld does | no |
| 6 | No flash of the 3D screen at a scene's start: the host counts a scene as coming for 150 ms (`State::holdPad[0]` armed, `MevrBridge::ConversationWorldOrComing`) | publishes only after the 0.35 s settle and placement | **wrong** (probable) | publish "coming" from the first tick of mode 7 (holdPad[0]) and give LE3's renderer `ConversationWorldOrComing` (section 6). [verify] with `[CINEVR]` lines: expect "3D conversation screen" then "none" at every start today | no |
| 7 | The player's place never moved by the game: Shepard's stage placement withheld at the source (`SetPositionRotator` pre-hook, `worldStaySource`), his matinee move withheld (`MoveWithInterpMoveTrack` pre-hook, `trackHeld`) | Shepard put back on the pin with `Actor.SetLocation` after every `Camera.UpdateCamera` that moved him more than 2 units | **wrong** | the source hooks (section 3): no fight with a move track each frame, no `FarMoveActor` touches (ME1 found `SetLocation` fires triggers, research/69 section 5), Shepard left in his seat and on his ride | no |
| 8 | People kept where they stand (`worldStay`): a jump of 30 units to 6 m undone before the draw; at the source since `worldStaySource = 1` | undone after the camera update, 30 units to 6 m | **partial** | the source hook on `SetPositionRotator` (zero place = facing only), as ME1; keep the after-the-fact undo as the fallback that should count zero | no |
| 9 | A placement the game repeats is its own (`cw::Insists`, 2 ticks, 5 units, 1 m) | its own copy: one undo, then yields within 100 units of the refused place | **partial** (a fork of a core rule) | call `cw::Insists` | no |
| 10 | An actor a matinee's move track moves is left to it (an authored walk; the summary's `trackWalks`) | can't tell a track from the stage: a track's cut (a first key 0.3-6 m away) is undone once, then yielded | **partial** | the `MoveWithInterpMoveTrack` hook tells the mod which actors a track moves this tick | no |
| 11 | Shepard's head bone moves the view only for a lasting jump (`FollowHeadSettled`); after a Stay opening never | removed with `shepardStays` | **right** | keep | no |
| 12 | The eyes at the player's height (`StepEyeHeight`, item 44) | `eyeUpUnits` 4, `eyeForwardUnits` 8 fixed | **missing** | port `StepEyeHeight` (floor = pawn `Location.z - CylinderComponent.CollisionHeight` + the live standing eye) | no |
| 13 | Walking during a conversation (`worldMove`): `cw::StepPlayer`, `cw::KeepClear` (speakers, squad, owner), floor (`FloorZ`, 60-unit drop refused), walls (`LineOfSight`), triggers (`worldMoveTriggers`), only while the wheel is down, and the stick latched after a pick (`cw::StickWalks`) | StepPlayer + KeepClear on `m_aSpeakerData` only; always on | **partial** | the floor, walls, triggers, the squad and owner in KeepClear, the wheel gate and `StickWalks`; and section 9 question 1 | no |
| 14 | The right stick turns in a conversation (`[Comfort] turn`, the second design) | the right stick is the dialogue wheel (PadMerge, LE3 only) | **wrong** against ME1; asked for by the owner after the walk took the left stick | section 9 question 1 | no |
| 15 | Looks at the player's eyes, not Shepard's hidden body (`worldLookEyes`, `StepEyes`, a `BioLookAtTarget` at the drawn eyes) and who looks at whom (`worldTurn = roles`) | none | **missing** | ME3's look-at is `SFXModule_LookAt` on the actor's `Modules` (section 2.4) | names yes |
| 16 | The director's near plane kept off the player's view (`worldNearPlane`: `LocalPlayer.NearClipPlane` back to the on-foot plane after each camera update) | none | **missing** | the same write; ME3 also sets it from a matinee track (`SFXInterpTrackSetPlayerNearClipPlane`, 37 full conversations) | the source yes |
| 17 | Depth of field off while held (`scale set DepthOfField`) | `depth_of_field::Hold`, never within 3 s of a load | **right**, and better than ME1 | move the load guard into the shared rule (section 5.6) | no |
| 18 | The 16:9 letterbox cleared (`[NOLETTERBOX]`) | `Camera.bConstrainAspectRatio` cleared after each camera update | **right** | keep | the place yes (LE1 does it in the renderer) |
| 19 | The conversation UI: movie on the HUD panel; the subtitle band under the speaker's face, the wheel at the hand (`DefaultSubtitleCrop`, `DefaultWheelCrop`) | the whole movie on the HUD panel, fixed in the room 0.9 m ahead | **partial** | ME3 crops, measured (research/le3-dialogue-cinema.md: "no inferred per-page crop") | crops yes |
| 20 | Arm IK and the body in the held frame (`view_anchor::Hold`) | yes; `OnViewBuilt` reports a held conversation as a world view | **right** | keep | no |
| 21 | Shepard's head hidden, body drawn (first person's hides) | `WorldViewHeld` 1 | **right** | keep | no |
| 22 | The game's fade to black moves the player (`worldBlackMoves`, item 41), curated walks excepted | none | **missing** | black-screen detection that covers ME3's three routes (section 5.4) | routes yes |
| 23 | What Shepard stands on carries the player (`RideBase`, item 42); steps tick after tick are a ride (`cw::IsRideStep`) | none: the pin holds Shepard at a world place | **wrong** in rides | port `RideBase` and `IsRideStep`; 36 full ME3 conversations move a shuttle, car or lift (section 4) | no |
| 24 | The view sits with him (`StepSeat`, `PlayerSeated`, `cw::IsSeatedScene`, item 43) | none; a Stay opening pins Shepard where the player stood, out of a seat the scene put him in | **wrong** in seated scenes | port `StepSeat`; seated scenes from data (37 conversations seat Shepard, section 5.3) | no |
| 25 | Moments (`worldMoments = view`, walk-along, the trail, `MoveTrackPlace`) | none | **missing** | the move-track hook; ME3 also walks Shepard inside conversations (206 full conversations, section 5.5) | no |
| 26 | People walk to their marks with the game's AI (`worldWalk`), the circle (`worldCircle`), the squad's formation (`worldSquadGather`) | none | **missing** | ME3 AI and squad names differ (section 2.6); for most ME3 scenes the authored composition should stand (section 5.2) | names yes |
| 27 | A GUI screen after a conversation is the room's (`ConversationHandover`, 300 ms) | none | **missing** | a `MevrSeams::ConversationHandover` for ME3's GUI mode 9 (section 6) | no |
| 28 | The host trusts the mod through a stalled game thread (`cw::StillFresh`, 10 s by game ticks) | 1.5 s wall clock (`kConversationFreshMs`) | **partial** | `cw::StillFresh` with the host's tick counter | no |
| 29 | Flowed walk-up scenes (`ConversationFlow`) and held newcomers (`worldFlowNewcomers`) | none | **missing**, low value on ME3 | section 5.7: ME3 already plays its walk-and-talk scenes as ambient; 21 full conversations have no wheel | the need, yes |
| 30 | Audit by rule (`StepAudit`, `scene_audit`, stances) | `mevr convo world` status and one summary line | **missing** | the records and rules, so ME3 scenes can be judged unattended | no |
| 31 | Voices placed (`worldVoices`) | none | n/a here: ME3 is Wwise (T50) | | yes |

**Also found reading the slice:**

- `Speakers()` reads `BioConversationController.m_aSpeakerData` only. The owner (`m_pOwner`), the listener (`m_pListener`), extra
  participants (`m_aExtraParticipants`) and the squad (the stage's nodes `Pup1_1`, `Pup1_2`, filled by `ABioStage` placement,
  section 3.2) may not be in it [verify which list holds whom]. KeepClear and keep-in-place miss whoever isn't.
- The PadMerge remap is `#if defined(MEVR_WITH_HALCYON_LE3)` inside a shared module: a per-game fork of an input rule.
- `SequelConversationWorld` restates core decisions (its own Insists, its own keep thresholds). The ledger's rule (shared core plus
  adapter) says: game-independent decisions go in `core/vr/ConversationWorld` (`cw::`), the module keeps only ME3's reads.

---

## 2. ME3's conversation runtime against ME1's

**The big difference.** ME1 has one live `BioConversation` (`BioWorldInfo.m_oCurrentConversation`). ME2 also keeps its live state on
`BioConversation` (lane FPC's ground). **ME3 splits it:** `SFXGame.BioConversation` is static data (entries, replies, speaker
tags, `MatineeSequence`); the live state is a `SFXGame.BioConversationController`, and a `SFXGame.BioConversationManager` on the world
(`BioWorldInfo.m_pConversationManager`) runs several at once: one full conversation and any number of ambient ones. Offsets below are
LExSDKv2's LE3 metadata (for ReflectCheck rows); the code resolves everything by name (`ue3::FieldRef`, `FunctionRef`).

### 2.1 The conversation

| ME1 (`game/ConversationWorld.cpp`, `ConversationFlow.cpp`) | ME3 | Notes |
|---|---|---|
| `BioWorldInfo.m_oCurrentConversation` | `BioWorldInfo.m_pConversationManager` (+0x948) then `BioConversationManager.GetFullConversation()` (native, exec 0xB51E40, vtable +0x2C0, impl 0xA329D0) | the slice does this. The manager's arrays: `m_aPreloadingConversations` (+0xA8), `m_aReadyConversations` (+0xB8), `m_aActiveConversations` (+0xC8) |
| `BioConversation.m_pSpeaker`, `m_pListener`, `m_pConversationOwner`, `m_pConversationPlayer` | `BioConversationController.m_pSpeaker` (+0x2A4), `m_pPreviousSpeaker` (+0x2AC), `m_pListener` (+0x2B4), `m_pOwner` (+0x294), `m_pPlayer` (+0x29C) | |
| `BioConversation.m_stage` | `BioConversationController.m_pStage` (+0x2C4, `ABioStage`) | |
| `m_SpeakerList[].aSpeaker` | `BioConversationController.m_aSpeakerData[]` (`FBioSpeakerData`: `nmSpeakerTag`, `pSpeakerActor`, 16 bytes), `m_aAllFoundActors[]` (same struct), `m_aExtraParticipants[]` (`AActor*`) | the data object's `m_aSpeakerList` holds tags (`SFXName`) only |
| `m_EntryList`, `m_ReplyList`, `m_StartingList`, `ReplyListNew[].nIndex`, `nSpeakerIndex`, `nListenerIndex`, `ReplyType` | `BioConversationController.m_pConvData` (+0x284) -> `BioConversation.m_EntryList` (`FBioDialogEntryNode`, 0x6C bytes), `m_ReplyList`, `m_StartingList`, the same member names | the node flags (`bAmbient`, `bNonTextLine`...) sit at +0x38 of every node |
| `m_nCurrentEntry`, `m_sCurrentSubTitle` | controller `m_nCurrentEntry` (+0x2EC), `m_nSelectedReply` (+0x2F4), `m_nCurrentReply` (+0x2F8), `m_sCurrentSubTitle` (+0xF8), `m_aCurrentReplyIndices` | Thespian's adapter reads these already (research/le3-dialogue-cinema.md) |
| `m_LookAtList[].pActor`, `pLookAtTarget` | controller `m_aLookAtList[]` (`FBioDialogLookat`: `pActor`, `pLookAtTarget`, `fLookAtDelay`) | |
| `m_aFSPrevPositions[].pActor`, `vPos` | controller `m_aPrevPositions[]` (`FBioSavedActorPos`: `vPos`, `rRot`, `pActor`) | where the game puts people back at the end |
| `m_bAmbient`, `m_fInterruptRange` | controller `m_bCurrentlyAmbient` (+0x358 bit 0x80), `m_bForceAmbientStart` (bit 0x100), `m_fInterruptRange` (+0x30C); `IsCurrentlyAmbient()` native (vtable +0x2C0, impl 0xA25820) | |
| the ended flag (`0x2000` at +0x2C0) | controller `m_bConversationEnded` (bit 0x2), `m_bFailed` (0x1000), `m_bInterrupted` (0x2000), `m_bConversationOver` (0x8000) at +0x358 | Halcyon's ME3 `convo_fp.cpp` tests mask 0xB002 for "over" |
| `m_pKismetStart` (`BioSeqAct_StartConversation`) | controller `m_pKismetStart` (+0x2BC, `SFXSeqAct_StartAmbientConv*`) | the full action `SFXSeqAct_StartConversation` subclasses the ambient one and adds `m_pStage`, `m_bFadeIn`, `m_bBlackOut` |
| the line's matinee (a stage's camera track per line) | controller `m_pCurrentLineMatinee` (+0x2DC, `SeqAct_Interp*`), `m_pEvtSysSeq` (+0x2E4); the data's `MatineeSequence` (+0x148) | **every ME3 line has a matinee** (section 4) |
| `m_nSubtitlesMode`, `BioSubtitles.DisplaySubtitle` | `BioWorldInfo.m_pSubtitles` (`BioSubtitles`); no `DisplaySubtitle` function on ME3 [verify how ME3 shows an ambient line's subtitle] | flow subtitles are ME1-only today |

### 2.2 Modes and input

- Game modes (`engine/GameAdapter.hpp`): ME3 conversation 7, cinematic 8, GUI 9, movie 10, galaxy 11, orbit 12. Shared code says
  "conversation", never 7 (docs/trilogy-plan.md rule 2).
- `SFXGameModeConversation` inherits `bAllowMovement = false`, `bAllowRotationUpdate = false`; its `Activated` ignores move and look
  input (research/le3-dialogue-cinema.md). ME1's walk moves the pawn with `Actor.SetLocation`, not input, so the lock does not
  block it. Its camera mode is `ConversationCam` (`BioCameraBehaviorConversation`).
- The wheel: `BioSFHandler_Conversation` (and `BioSFHandler_PCConversation`), `m_bDisplayingWheel` (+0x3E8 bit 0x2),
  `m_nSlotsUsed[6]` (ME1's name was `m_bSlotsUsed`), `m_bDisplayingParagonInterrupt`/`Renegade` (bits 0x4/0x8). Reached through
  `SFXGUIInteraction.GetConversationHandler(player)` (research/le3-dialogue-cinema.md). `cw::StickWalks` needs `m_bDisplayingWheel`.
- Interrupts stay on the triggers (docs/trilogy-plan.md rule 11); nothing here takes a trigger.

### 2.3 The camera

- `BioCameraBehaviorConversation.m_fNearPlane` (+0x184), `CAMERA_FOV` (+0x188), `ViewSource`, `Target`, `m_tDOFData`, procedural
  and fixed camera places. ME3 also swaps camera classes per shot and hands embedded shots to director cameras: Halcyon's
  `[CONVOLATCH]` and `[CONVOSCOPE 2026-08-18]` comments in `src/halcyon/le3/me2_xr.cpp`, and the scan: 542 of 575 full
  conversations have an `InterpTrackDirector` in at least one line (section 4).
- `Engine.LocalPlayer.NearClipPlane` (+0x5DC) is the plane ME1's `worldNearPlane` writes back.
- Depth of field: `ABioStage.m_bDOFActive` (+0x2EC bit 0x8) and the `BioEvtSysTrackDOF` track; the slice switches the engine's
  `DepthOfField` setting instead (ME1's way), which covers both.

### 2.4 The look-at chain

ME1 uses `BioPawn.m_oLookAtData` (`BioLookAt`), `BioPawn.SetLookAtTarget` at rank 100, `BioPawnLookAtController.mc_pDefinition`,
`BioLookAtDefinition.BoneDefinitions[].m_fConversationStrength`. **ME3 has none of `BioPawn.SetLookAtTarget` or `m_oLookAtData`.**
Its look-at is a module on the actor:

- `Engine.Actor.Modules` (`TArray<SFXModule*>`, +0x70) holds an `SFXGame.SFXModule_LookAt`: `m_Controllers` (`BioActorLookAtController*`),
  `m_pTarget`, `m_Definition` (`BioLookAtDefinition`), `m_nRank`, `bAutoLookAtPlayer`, and the native
  `ChangeTarget(Target, a_eTransition, nRank)`.
- `BioActorLookAtController.mc_pDefinition` (+0x190), `m_pTargetActor`; `BioLookAtDefinition.BoneDefinitions` as ME1.
- Look targets: `SFXGame.BioLookAtTarget` exists on ME3 (ME1's eyes stand-in spawns one); the conversation's
  `BioEvtSysTrackLookAt` tracks and `m_aLookAtList` drive the authored looks.
- So `worldLookEyes` on ME3: spawn a `BioLookAtTarget` at the drawn eyes; for each look the game aims at Shepard's pawn
  (`SFXModule_LookAt.m_pTarget == pawn`), call `ChangeTarget(eyes, transition, rank)` at the module's own rank. [verify] the rank
  semantics (ME1's authored looks were 99999 and stood) and the transition enum.

### 2.5 Seated, rides, black screens

- Seats: gesture tracks (`BioEvtSysTrackGesture`: `m_aGestures[]` with `nmPoseSet`/`nmPoseAnim`, `nmStartingPoseSet`/
  `nmStartingPoseAnim`) carry the sit poses (`HMM_WI_Sitting`, `WI_SittingIdle`, `JokerSit_Idle`, `global_shuttle_pilot`...).
- Rides: `Engine.Actor.Base` (+0x188) exists; matinee groups named for a vehicle (`Shuttle`, `Car`) move it; no conversation uses
  `SFXInterpTrackAttachToActor` (0 of 2,036).
- Black screens, three routes: the level script's `BioSeqAct_BlackScreen` (as ME1: `Activated` is an event,
  `m_eBlackScreenAction`), a conversation line's `SFXInterpTrackBlackScreen` (which holds its own `m_BlackScreenSeq`, a
  `BioSeqAct_BlackScreen`), and the start action's `SFXSeqAct_StartConversation.m_bBlackOut` / `m_bFadeIn`. All of them end in the
  GUI's `BioSFHandler_BlackScreen` (`Show(bWithFade, FadeTime)`, `Hide(...)`, `IsFading()` natives: exec 0xB53300, 0xB53240,
  0xB533C0; `m_CurrentAlpha` +0x344, `m_eDisplayMode` +0x350) [verify that the track and the start flags reach it, and whether
  the track's `m_BlackScreenSeq` goes through `Activated` by ProcessEvent].

### 2.6 People walking, the squad

None of ME1's AI names exist on ME3: no `BioAiController.PushMoveToPoint`, `DoesGameModeAllowMovement`, `BioEpicPawnBehavior`
(`m_nALLEnabled`, `m_nStackEnabled`), `BioBaseSquad.Members[].vHoldPosition`, `BioPawn.m_bInGestures`, `m_nHiddenRequestCount`.
What ME3 has:

- the squad: `BioPlayerController.HenchmanOrderHoldPosition(oHenchman, vLocation, eInputMethod)` and `HenchmanOrderFollow`, as
  ME3's squad placement already uses them (`game/Le2Squad.hpp` `orderHold`, `game/SquadPlace.cpp`; research/le3-squad.md);
  `BioBaseSquad.Members` is `TArray<APawn*>` (+0x270);
- other people: `BioAiController.MoveToSlot(vSlotLocation)`, `SFXAI_NativeBase.MoveTowardLocation(vLocation, fOffset)`
  [verify either moves an NPC in mode 7];
- `BioPawn.SetDesiredSpeed`, `BioPawn.RequestTurnInPlace`, `BioPawn.BioSetDesiredRotation(rot, bForce)`, `BioPawn.OnTeleport`.

---

## 3. The natives: ME1's three hooks on ME3

All four functions below are `.pdata` function starts in the `0x60AEF0C4` exe. Patterns come from
`python -I tools/rva-patterns.py %LOCALAPPDATA%\MEVR\dump\MassEffect-60AEF0C4.text.bin --exe <MassEffect3.exe> name=0x...:function`;
each is unique in ME3's `.text`. They go into `src/engine/Addresses.hpp`'s `MEVR_ADDRESSES_LE3` block, whose
`kConversationSetPositionRotator`, `kInterruptConversation` and `kMoveWithInterpMoveTrack` are 0 today, with `kTable` rows
("mod (conversations)", "mod (authored moments)"). ctest `mevr-rva-table` checks the table.

| ME1 (LE1 RVA) | ME3 function | ME3 RVA | Pattern (offset 0, function) |
|---|---|---|---|
| `UBioConversation::SetPositionRotator` (0xC862D0, BioConversation vtable +0x2C0) | `UBioConversationController::SetPositionRotator` (a direct call, not virtual) | **0xA29DF0** | `48 8B C4 55 57 48 8D 68 A9 48 81 EC B8 00 00 00` |
| `ABioWorldInfo::InterruptConversation` (0xB60460, slot 0x9F0) | `UBioConversationManager::InterruptAmbientConversations` (manager vtable `0x131DA98` +0x2C8) | **0xA38510** | `48 89 54 24 10 48 89 4C 24 08 55 56 57 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 70 48 C7 45 C0` |
| (ME1's `ABioWorldInfo::StartConversation` 0xB65A50 refused newcomers) | `UBioConversationManager::ActivateConversation` | **0xA2EE60** | `48 85 D2 0F 84 ?? ?? ?? ?? 48 8B C4 55 41 56 41 57 48 8B EC 48 83 EC 70` |
| `AActor::MoveWithInterpMoveTrack` (0x4B75F0, actor vtable +0x558) | the same, actor vtable **+0x548** | **0x6BAFF0** | `48 8B C4 48 89 58 18 48 89 70 20 55 57 41 54 41 56 41 57 48 8D A8 68 FE` |

Companions, for reference and for logs (not hooked): `ABioStage::PlaceActors` 0xAC4E10 (`40 55 56 57 41 54 41 55 41 56 41 57 48
8D 6C 24 C0 48 81 EC 40 01 00 00 48 C7 45 E0`), `UBioConversationController::StartConversation` 0xA2A7D0 (`48 8B C4 55 41 56 41
57 48 8B EC 48 83 EC 70 48 C7 45 C0 FE FF FF FF 48 89 58 10 48 89 70 18 48 89 78 20 0F`), `AActor::physInterpolating` 0x6C0120
(actor vtable +0x558; `48 89 5C 24 10 57 48 83 EC 50 48 8B 01 4C 8D 4C`), `UWorld::FarMoveActor` 0x581450 (`40 55 53 57 41 54 41 57
48 8D 6C 24 E1 48 81 EC`), `AActor::SetLocation` (C++) 0x70ABC0, `UWorld::MoveActor` 0x584770. LE2, for FPC: `tools/ghidra/
symbols-le2.csv` already names `AActor::MoveWithInterpMoveTrack_guess` 0x6B9C50 (LE1's first 32 bytes, one match).

### 3.1 How each was found (repeatable)

1. **MoveWithInterpMoveTrack.** LE1's `physInterpolating` (0x4BCC00) prologue matches ME3 0x6C0120 uniquely (16 bytes). Decompiled,
   it asks the actor's vtable +0x440 for its move track, then calls the actor's vtable **+0x548** with (actor, track, track
   instance, time, delta). The exe's `.rdata` at vtable `0x10B3448` + 0x548 (and the same slot in every actor vtable listed by
   Ghidra's "pointers to it") holds 0x6BAFF0. Its body: reads the track's flags, asks the track (its vtable +0x370, new on ME3,
   then **+0x398**: ME1's +0x390) for the keyed place and rotation at the time, a BioWare adjustment (an actor vtable +0x550 call and
   a clamp of a pawn's mesh offset under 200 units), then `UWorld::MoveActor(GWorld, actor, delta, rotation, ...)`, the velocity
   from the step, and the relative place when the actor has a `Base`. Returns whether it moved. LE1's same function calls track +0x390
   and actor +0x6E8 where ME3 calls +0x398 and +0x6D8: the actor's slots sit 0x10 lower on ME3 (this function +0x558 -> +0x548),
   the move track's place function 8 higher.
2. **SetPositionRotator.** The controller's start (0xA2A7D0) calls the participants' setup 0xA24E80, which, for a conversation that is
   not ambient and has a stage (`m_pStage`), calls 0xAC4E10 (ABioStage's placement: it fills `ABioStage.m_mapPlacement` with node
   names to actors, the player, owner, the squad on `Pup1_1` and `Pup1_2`, the speakers; for each it asks the node's place,
   0xAC22E0, and calls 0xA29DF0). 0xA29DF0 is one of the 32 callers of `AActor::SetLocation` (0x70ABC0, which calls
   `UWorld::FarMoveActor`).
3. **InterruptAmbientConversations.** The exec thunk 0xB51E90 (natives dump) calls the manager's vtable +0x2C8; the UTF-16 log
   string "Ambient conversation was interrupted: Ignoring '%s', Only '%s' and reason '%s'" is referenced only inside 0xA38510;
   `.rdata` `0x131DA98` + 0x2C8 holds 0xA38510 (that vtable's +0x2B0..+0x2D0 are the manager's five natives, in the exec thunks'
   slot order: +0x2B0 `GetConversationControllerByResourceID`, +0x2B8 `AnyConversationsActive`, +0x2C0 `GetFullConversation`,
   +0x2C8 `InterruptAmbientConversations`, +0x2D0 `RemoveConversation`).
4. **ActivateConversation.** The strings "ActivateConversation failed - Full conversation currently active", "Conversation '%s' is
   already running...", "Failed to StartConversation with error: %s" are referenced inside 0xA2EE60.

### 3.2 What each does, and how to hook it on ME3

**`SetPositionRotator` 0xA29DF0** — `void(UBioConversationController* controller, AActor* actor, FVector* location, FRotator*
rotation, UBOOL dropToFloor)`. **Five arguments** (ME1's hook takes four; the fifth is on the stack: the detour must take and pass
it). `controller` **may be null**: two callers in the interp-track range (0xC2C2D0, a track's vtable function, and 0xC3E970) pass 0
and `dropToFloor` 1 (the per-key placement of `BioEvtSysTrackSetFacing` and one more track [verify which]); a helper of the
participants' setup, 0xA246F0, passes the actor's own place and rotation. Behaviour, as ME1's: for a pawn it stops the pawn, moves it with `FarMoveActor`
**only when the place is not zero**, and with `dropToFloor` checks the new spot and puts it back or drops it to the floor; then it
sets the pawn's desired rotation and rotation and points its controller's focus 100 units ahead along the facing. A zero place is the
game's own facing-only path. So ME1's hook carries over: in the pre-hook, when the provider keeps the actor, call the original with a
zero location (facing kept). The provider cannot take the conversation from the argument (it can be null): it reads the live
controller itself (`GetFullConversation`), or keys on the actor alone. Callers: 0xA246F0, 0xA29520, 0xA3E2D0, 0xAC4E10, 0xC2C2D0,
0xC3E970.

**`InterruptAmbientConversations` 0xA38510** — `void(UBioConversationManager* manager, FString* reason, UBioConversation* ignore,
UBioConversation* onlyThis)`. Walks `m_aActiveConversations` from the end; each ambient controller (its `IsCurrentlyAmbient`)
that is not `ignore`'s and is `onlyThis`'s (when given) is marked `m_bInterrupted`, logged, and removed through `RemoveConversation`
(+0x2D0). **The controller's start (0xA2A7D0) calls it with (reason, null, null) for every new full conversation.** ME1's hook
refused a newcomer's interruption of a flowed (ambient) scene; on ME3 a pre-hook can keep a protected ambient scene alive simply by
passing its `UBioConversation` as `ignore` (the function's own parameter), without skipping the call.

**`ActivateConversation` 0xA2EE60** — `void(UBioConversationManager* manager, UBioConversationController* controller, UBOOL
allowRepeat)`. Clears the controller's failed bit; refuses (sets `m_bFailed`, then `m_bInterrupted` and `RemoveConversation`) when it
has no data object, when the same resource is already running (unless `allowRepeat`), or when a full conversation is active and this
one isn't ambient; otherwise calls the controller's start 0xA2A7D0 with (data, owner, player, &error). Callers 0xA3CB30 and
0xB1E290 [verify: the manager's tick over its ready list, and the start action]. **This is ME3's one gate for every start, full or
ambient**: ME1's "hold a newcomer" (`worldFlowNewcomers = hold`) becomes a pre-hook that sets `m_bFailed` (bit 0x1000 at +0x358,
journalled) and calls the original, which then fails it through the game's own path (the action's failure output), to be fired again
later (`SequenceOp.ForceActivateInput`, as ME1). Not needed until ME3 has flowed scenes (section 5.7).

**The controller's start 0xA2A7D0** (not hooked; how ME3 chooses ambient): it takes the first starting entry whose conditions hold
(0xA21550) and sets `m_bCurrentlyAmbient` from `m_bForceAmbientStart` or that entry's `bAmbient` flag (+0x38 bit 0x2 of the 0x6C-byte
entry): **ME1's rule exactly** (research/59 "Flowing conversations": the first entry's flag decides). Not ambient: logs "Trying to
start new full conversation" and interrupts every ambient one (above). Ambient: refused while a face-only VO that interrupts ambients
plays. Then it registers with the manager (0xA3B690), sets up participants (0xA1F820, 0xA24E80: the stage placement above) and
displays the first entry.

**`MoveWithInterpMoveTrack` 0x6BAFF0** — `UBOOL(AActor* actor, UInterpTrackMove* track, UInterpTrackInstMove* instance, float
time, float delta)`: ME1's signature. ME1's `engine/ConversationHooks` hook and provider carry over as they are; **`MoveTrackPlace`
must read the track's place function from vtable +0x398 on ME3** (ME1's code hard-codes 0x390: make it a per-game constant in
`engine/GameAdapter.hpp` or next to the hook). Unlike LE1 (no BioWare subclass of the move track), ME3 has `BioInterpTrackMove`
(adds `FacingController`); it still goes through this function (the actor's vtable slot) [verify with the hook's counters].

---

## 4. ME3's scenes, counted, and ME1's mechanisms for each

**How ME3 classes a conversation.** A full (mode-7) conversation is one whose starting entry is not `bAmbient` (section 3.2), started
by `SFXSeqAct_StartConversation`. Counted over the installed packages (base game and DLC; section 10):

| | count |
|---|---|
| distinct conversations | 2,036 |
| start actions: `SFXSeqAct_StartConversation` / `SFXSeqAct_StartAmbientConv` | 1,941 / 1,894 |
| **full** (a non-ambient start entry, a full action) | **575**, and 62 mixed (some starts ambient) |
| ambient (every start entry ambient) / ambient by action only | 1,131 / 55 |
| no start action found (started by code or another package) | 213 |

**Inside the 575 full conversations** (a "node" is one line's matinee; "moves" means a move track takes the actor more than 1.5 m
from its first key in one node; camera groups and the player's group are counted apart):

| kind | full | what it means for the player | ME1's mechanism | fits unchanged? |
|---|---|---|---|---|
| still: nobody moved more than 1.5 m by a node | 251 | ME1's ordinary conversation | opening, keep-in-place, looks, the wheel | yes |
| others walk or are moved | 287 (80 only within 8 m) | the owner's "dynamic scenes with walking movement in the dialogue" | an authored track walk is left to the track (`trackWalks`); keep-in-place only for stage teleports | yes, with the move-track hook (row 10) |
| **Shepard moved** by a node | **206** (121 within 8 m; 77 beyond) | the player would be walked or carried | ME1's moment rules (withhold, the view the player's, walk-along, trail, black moves) — ME1 applies them to level matinees only | **no: extend** (section 5.5) |
| Shepard seated by a node's gesture pose | 37 (21 also moved) | shuttle benches, the cockpit, couches, the Citadel DLC's apartment | `StepSeat`, `PlayerSeated`, `IsSeatedScene` (item 43) | the rule yes; the table from data (section 5.3) |
| someone seated | 155 (41 shuttle or pilot) | Joker in his chair, people at desks | `MayReface` (never turn the seated), staged | yes |
| a shuttle, car or lift group moves | 36 | a ride inside the conversation | `RideBase` (`Actor.Base`), `cw::IsRideStep` | yes [verify that the riders are based on the vehicle or moved by their own tracks] |
| a black-screen track in a node | 118 (64 also move Shepard) | the fade covers a change | `worldBlackMoves` (item 41) | the rule yes; the detection must cover ME3's routes (section 5.4) |
| the start action begins from black | 121 `m_bBlackOut`, 80 `m_bFadeIn` (of 1,941 actions) | the opening is under black | `OpeningFacts::coveredByBlack` -> Cut | yes, read the flags |
| a near-clip track | 37 | the director moves the near plane per line | `worldNearPlane` | yes |
| SetFacing track in a node | 437 | stage-node placements per line | `SetPositionRotator` hook (zero place) | yes |
| a director track in a node | 542 | authored camera work | none (the view is the player's) | yes, but not as ME1's set-piece criterion (section 5.1) |
| a wheel (a standard reply) | 556 | | | |

By name suffix, the full conversations are 298 `_m_` (145 move Shepard), 173 `_d_` (25 move Shepard: the hub talks, closest to ME1),
96 `_v_`, and a few `_a_`, `_b_`, `_i_`, `_c_`. **Ambient conversations essentially never move anyone** (4 of 1,131) and never move
Shepard (0). **Outside conversations**, 910 level matinees bind the player (a `SeqVar_Player` on the matinee's variable links, or a
`Player`/`Shepard` group); 100 move him more than 1.5 m (83 with a director track: cutscenes), 19 seat him: ME1's moments, with
ME3's shuttle and cockpit rides among them (item 43's "not covered").

---

## 5. Where ME3's data shows an ME1 rule is too narrow

The owner (2026-11-19): "It is possible that me1 has bugs, so if we need to change the core module, I'm not opposed." Each item:
the evidence, and the change to the shared rule (FPC may make it for all three games).

### 5.1 The set-piece criterion

ME1's set pieces came from a scan criterion: "a quarter or more of its lines authored camera work (a director track or a moving
camera)" (`cw::IsSetPiece`'s comment; research/59 "The scan"). **On ME3 that criterion takes 506 of 637 full conversations** (79%),
and 117 have a director track in every line. ME3's authors frame almost every conversation; framing is not why a scene can't be first
person. **Change:** the shared rule keeps `cw::IsSetPiece` a per-game table, but the criterion that fills it is presence and
placement (5.2), never camera share. ME1's table stays as it is (it was curated after that criterion).

### 5.2 Presence and staged scenes from the data

ME1's presence rules are run-time (pawn hidden, far from the owner) and its staged scenes a curated list (`cw::IsStagedScene`).
ME3's data gives both directly: **192 full conversations have no `Player` or `Shepard` group in any line's matinee** (an upper bound:
the scan kept 12 group names per conversation), among them the docking officer's (`citprs_citadel_docking_v_dlg`), the Hackett calls
(`n7_*_hackett_v_dlg`), shuttle rides seen from outside (`kro001_shuttle_ride_in_m_dlg`). And a conversation whose lines move or pose
its people by tracks is authored staging (287 move others, 437 set facings). **Change:** generate the tables from each game's scan
(FPC owns `tools/conversation-scan`; LE3 there reads only `--script`/`--class-dump` today): `screen` for no player group (and for
the `_v_` calls that also lack one), `staged` for authored placements, keeping ME1's run-time rules as the fallback. The circle
(`worldCircle`) and walking people to marks then apply only to unstaged scenes (ME3: the 251 still ones and the `_d_` hub talks),
which also sidesteps ME3's missing AI names (2.6) for most scenes.

### 5.3 Seated scenes from the data

ME1's `cw::IsSeatedScene` is a table with one entry (research/69 item 43 said the LE1 scan didn't record gesture names). On ME3 the
gesture names find them: **37 full conversations seat Shepard** (his group's gesture track with a sitting pose: `HMM_WI_Sitting*`,
`WI_SittingIdle`, `JokerSit_*`, `global_shuttle_pilot`, `*_SitFloor*`); examples `kro002_shuttle_ride_in_m_dlg`,
`citprs_ash_hospital_m_dlg`, `citprs_kaid_chr_moment_m_dlg`, `citapt_kaidan_m_dlg` (Citadel DLC). **Change:** (a) record gesture pose
names per group in every game's scan and generate `IsSeatedScene`; (b) a fact for `cw::DecideOpening`: "the scene seats Shepard" ->
his place is his seat (as a Cut, no Stay that pulls him out of it). The run-time `PlayerSeated` and `StepSeat` stay as they are.

### 5.4 "The game's fade to black" has three routes on ME3

ME1 detects the black screen by `BioSeqAct_BlackScreen.Activated` (game/AuthoredMoment's `ScriptBlackScreen`) and a matinee's
`Camera.bEnableFading`/`FadeAmount`. ME3 adds a conversation line's `SFXInterpTrackBlackScreen` (118 full conversations, 64 of
them also moving Shepard) and the start action's `m_bBlackOut`/`m_bFadeIn` (169 actions: 121 black out, 80 fade in, 32 both). **Change:** detect "the game's black
screen" where all routes end, the GUI's `BioSFHandler_BlackScreen` (its `Show`/`Hide` calls, or `m_CurrentAlpha` and `IsFading`),
with the Kismet event kept as a second source. [verify] on ME3 that the track and start flags go through the handler; [verify] on
LE1 whether its own black screen has the same handler (it hides it through `MassEffectGuiManager.HideBlackScreen`), so one rule
serves all three.

### 5.5 Shepard walked inside a conversation is a moment

ME1 withholds the player pawn's track move in conversations (`trackHeld`) and treats walks by level matinees as moments (the view the
player's, `cw::TrailTarget`, `StepWalkAlong`, the patience, carry). **ME3 walks Shepard inside 206 full conversations** (121 within
8 m). **Change:** the moment's treatment applies to the player's move track in any matinee, a conversation's line included: withhold
it (`MoveWithInterpMoveTrack` pre-hook), ask the track's place (`MoveTrackPlace`, vtable +0x398 on ME3), let the player walk to it
or guide/carry after `worldMomentPatienceS`, join at rest; under the game's black screen, move with him (`worldBlackMoves`). One open
point for conversations: ME1's walk-along pauses the level matinee (`SeqAct_Interp.bPaused`); a conversation's line matinee
(`m_pCurrentLineMatinee`) is paced by its VO and node timer (`m_fNodePlayTimer`, `m_fRemainingVOAudioLength`), so pausing it may
desync the line [verify]; guide or carry instead of pausing until that is measured.

### 5.6 Depth of field around a load

The slice found that switching the engine's depth of field (`scale set`) as a conversation hands over to a load left the load spinning
with no scene drawn on ME3 (2026-11-19 20:56), as ME2's `scale set` during a load hung its rendering
(`docs/knowledge/gotchas/me2-scale-set-during-load`). ME1 switches it at every conversation's start and end. **Change:** the shared
`depth_of_field::Hold` (or `cw`) never switches within 3 s of a load or before 3 s on foot, on every game (ME1 included, which has
not shown the hang but runs the same engine path).

### 5.7 Flowed scenes are mostly ME3's own

ME1's `ConversationFlow` flags wheel-less "chain" starts ambient so they play as barks, not conversation mode. ME3's authors did
this themselves: 1,131 ambient conversations; 196 wheel-less conversations whose start entries are all ambient are started by the
full action and so play ambient natively (the controller's start reads the entry's flag, 3.2); only **21 full conversations have no
wheel**. **Change:** none to the rule; don't port
`ConversationFlow` to ME3 until a scene needs it (the hooks to protect one are in 3.2).

---

## 6. The renderer's decision points and the seams

LE1's `[MEVR]` conversation seams (src/halcyon/le1) against ME3's renderer (src/halcyon/le3), today:

| LE1 seam (file) | What it decides | ME3 place | ME3 today |
|---|---|---|---|
| `CineFinishDecision`: `d.inConversation = false` while `ConversationWorldOrComing` (xr_session.cpp) | no cinematic presentation | `me2_xr.cpp` `RunFrame`, after `[PHOTOVR]`: `if (MevrSeams::ConversationInWorld()) cine = false;` | **done**, but only once placed (no "coming") |
| ... mode 7 handover (`ConversationInWorld` in mode 7) and `liveWorldGui` on `ConversationHandover` | a GUI screen right after | `d3d_capture.cpp` `UpdatePresentationState` (`WorldUiOwnsGui(gmNow)`, `demand`) | **missing** |
| second eye's gate (`PublishGameModeForSfr` effective mode 0) | SFR runs as gameplay | follows `cine` (`SetCinematic(cine && ...)`) | done implicitly |
| conversation movie at its authored layout (`Pchud::SetConvoTransform`) | the UI's place | host: HUD panel redirect (`MevrSeams::HudPanelDraw`) and the slice's fixed panel | partial (row 19) |
| cine capture boundary (`SetSfrCineBoundary`) | the pair keeps publishing through the conversation camera's post chain | the slice's letterbox clear removed the `CAPTURE_STALL_MONO` | done (via the mod) [verify in snapshots] |
| `[NOLETTERBOX]` (`SetCineUnconstrain`, also mode 0 for a moment on foot) | full frame | the mod clears `Camera.bConstrainAspectRatio` after each camera update | done for conversations; moments missing |
| first-person hides in an unreadable camera mode (head_aim.cpp) | body drawn, head hidden | `engine_probe.cpp`: `MevrSeams::WorldViewHeld(mode)` 1 | done |
| `GalaxyViewSwapBegin/End` around the main view's CalcSceneView (render_hook.cpp) | the view held at the player's place | `calcview_hook.cpp`: `WorldViewHoldBegin/End` -> `world_ui::HoldConversationBegin` | done |
| upstream first person | | `convo_fp.cpp` `SetEnabled` forced off under `MEVR_ASI`; `ApplyMevrBaseline` pins `ConvoFirstPerson` off; the classic menu's rows compiled out | pinned off, as it should stay |

Where `d3d_capture.cpp` still says `fpConvo` (upstream's `ConvoFp::OwnsCamera()`, always false now) in `outfitCompositeOwner`,
`ordinaryMenuOwner` and `galaxy`, the mod's conversation should take its place: `fpConvo || MevrSeams::ConversationInWorld()`
[verify a GUI-like lock tuple can't arise in a held conversation].

**Seams the shared core needs** (`src/halcyon/MevrSeams.h`; LE1 keeps its own names in `src/halcyon/le1/mevr_bridge.h` and the
host forwards both to one implementation):

```cpp
// Any thread: the mod shows the current conversation (or moment) in the world, or one is beginning this instant (the mod's armed
// bit, State::holdPad[0], for 150 ms; LE1's MevrBridge::ConversationWorldOrComing). ME3's me2_xr.cpp cinematic decision.
bool ConversationWorldOrComing() noexcept;
// Any thread: a conversation in the world ended within the last 300 ms (State::holdPad[1]; LE1's MevrBridge::ConversationHandover):
// a GUI screen that follows is the room's, not the flat panel. ME3's d3d_capture.cpp UpdatePresentationState.
bool ConversationHandover() noexcept;
```

`ConversationInWorld()` (added by the slice) stays. Nothing else is needed in the renderer: the source hooks, seats, rides, looks
and walks are the mod's, the dip on a cut is the host's TransitionFade reading `cw::State::cut`.

**One ME3-specific finding to keep in mind:** holding the main view at Shepard's mark changed what ME3's level streaming saw: the
Citadel's arrival load never finished with the docking officer's conversation held (two runs of two, commit `950abe3f`). ME1 had the
opposite problem (the render-time swap left streaming at the director's camera: `worldCamera`). [verify] which view ME3's streaming
reads (the rendered view or the player camera's POV), before relying on the "3 s on foot" guard as the rule.

---

## 7. The work, in order

Each step names what to check before the next. Build `build-trilogy` and `build-mono`; ME1's compiled code must stay unchanged for
steps that touch shared files (docs/development-approach.md, "How we work").

1. **Addresses.** Fill `MEVR_ADDRESSES_LE3`'s `kConversationSetPositionRotator = 0xA29DF0`, `kInterruptConversation = 0xA38510`,
   `kMoveWithInterpMoveTrack = 0x6BAFF0`, add `kActivateConversation = 0xA2EE60` (0 on LE1/LE2 until found), with `kTable` rows and
   the patterns of section 3. Check: ctest `mevr-rva-table`; the mod's start logs every address found at its pattern.
2. **The hooks on ME3.** `engine/ConversationHooks`: the placement detour takes five arguments on LE3 (a per-game typedef), accepts a
   null controller, and asks the provider with the actor; `MoveTrackPlace` reads track vtable +0x398 on LE3. Check in the game:
   `hooked UBioConversationController::SetPositionRotator`, the summary's counts (`placements`, `kept`, `facings`, `trackMoves`,
   `trackKept`) non-zero in a conversation with SetFacing tracks.
3. **Shepard at the source.** Replace the slice's pin (`SetLocation` every camera update) by the two providers: placements of the
   player's pawn withheld (zero place), his move-track moves withheld. Check: the slice's `Shepard put back N times` counts 0, no
   `SetLocation refused` lines, Shepard's walk animation no longer plays on the spot.
4. **People at the source.** The placement provider withholds stage teleports of kept people (ME1's `worldStaySource`); track moves are
   left to the track and noted (`trackWalks`). Use `cw::Insists`. Check: the after-the-fact undo counts 0; in a walking scene
   (`citapt_liara_m_dlg` or a hub `_m_` talk) people walk as authored.
5. **The opening.** Facts from ME3: `coveredByBlack` from `SFXSeqAct_StartConversation.m_bBlackOut`/`m_bFadeIn` (through
   `m_pKismetStart`, `IsA` the full action), the sight line (`Actor.FastTrace`), the floor, "the scene seats Shepard" (step 9); settle
   on the first drawn world frame; publish the armed bit from the first tick; the two seams of section 6 in `me2_xr.cpp` and
   `d3d_capture.cpp`; TransitionFade on LE3 reads `cut`. Check: one `[CINEVR]` line per conversation start (no "3D conversation
   screen" then "none"), `transition fade:` lines only for cuts.
6. **Presence and staging from data.** Tables generated by the scan (5.1, 5.2) into `cw::IsSetPiece` / `IsStagedScene` per game; no
   circle or arrangement for staged scenes. Check: the docking officer's conversation goes to the 3D screen by the table, not by the
   load guard.
7. **Walking.** ME1's full `worldMove`: floor, walls, triggers (`Actor.PointCheckComponent`), the squad and the owner in KeepClear,
   only while the wheel is down, `cw::StickWalks`; section 9 question 1 for the sticks. Check: `mevr convo walk` equivalents on ME3,
   the walk refused at a trigger's edge.
8. **Looks at the eyes.** `SFXModule_LookAt` adapter (2.4): spawn `BioLookAtTarget`, retarget looks aimed at Shepard's pawn. Check:
   the stance records' "looks at the player" against "looks at his body" (`tools/conversation-stance.py --audit` once ME3 writes
   records).
9. **Seats and rides.** `StepSeat`, `PlayerSeated`, `RideBase`, `IsRideStep` on ME3's names (`Engine.Actor.Base`, the skeleton's head
   bone); seated scenes from the scan (5.3). Check: `kro002_shuttle_ride_in_m_dlg` (Tuchanka's shuttle) and a Normandy cockpit talk.
10. **Black moves and moments.** The black-screen detection of 5.4; `worldBlackMoves`; the moment machinery for the player's move
    tracks in conversation lines and level matinees (5.5). Check: a conversation with a black-screen track that moves Shepard.
11. **The UI pieces.** ME3 crops for the subtitle band and the wheel, measured from snapshots (never LE1's numbers).
12. **Near plane, eye height, freshness.** `worldNearPlane` (`LocalPlayer.NearClipPlane`), `StepEyeHeight`, `cw::StillFresh` in the
    host. Check: the floor visible under the player in every shot; the eyes at the player's standing height in snapshots.
13. **ReflectCheck rows** for every new name (the tables of section 2), `kEveryGame` handles, `Up()` gating; the ledger's G36 row and
    T49 updated in the same landing.

---

## 8. Fixtures

The ME3 career on this machine (the one folder under `Documents\BioWare\Mass Effect Legendary Edition\Save\ME3`, `<career>`
below; read only, level names read from the files): `AutoSave`, `QuickSave`, `LegendSave`, `Save_0031` (2026-10-07: the Normandy, `BioP_Nor`),
`Save_0090` (2026-11-19 18:52: the Citadel hub, `BioP_CitHub` with `BioD_CitHub_Embassy_Cat002/003` and `HanarPlot` loaded),
`ChapterSave` (2026-11-19 20:06: the Citadel arrival, `BioA_CitHub_000Docking`: written by the game during the owner's run). Runner:
`tools/le2-halcyon-run/README.md` "Explicit ME3 renderer preview" (`prep.ps1 -Game LE3 -RendererPreview`, `launch.ps1`, `load.ps1
-Save <career>\<save>`, `restore.ps1`), with `run-lock.ps1` first; `me3-combat.ps1` shows the console routes. Back up the saves first
(`load.ps1` loads them; the game autosaves on its own).

| kind | fixture | why | how to reach |
|---|---|---|---|
| the owner's (no place for the player) | `citprs_citadel_docking_v_dlg` (`BioA_CitHub_000Docking`): the docking officer, Citadel control and EDI's radio; started with `m_bFadeIn`; no `Player` group in any line | the slice's failure case (the load never finished when held); must go to the 3D screen by rule | `ChapterSave` or the Citadel arrival from the galaxy map |
| an ordinary standing talk | `citprs_khalisah2_d_dlg` (Embassy, 30 lines, 22 framed, nobody moves) or `cithub_michel_d_dlg` (Hospital, no player group: a procedural camera); `cat003_wrapup_bailey_d_dlg` (Embassy, Bailey seated at his desk) | ME1's ordinary case: opening, keep-in-place, looks, the wheel | `Save_0090` [verify which is available in that save's plot] |
| people walk, Shepard walks | `citapt_liara_m_dlg` (Shepard 2.2 m in 2 lines, a door, 3 black-screen lines; Citadel DLC) or a base-game hub `_m_` talk: `citprs_miranda_talk1_m_dlg` (Shepard 4.3 m, others 7.7 m), `citprs_cat3_ashjoins_m_dlg` | the dynamic scene the owner named | `Save_0090` for the hub ones [verify plot] |
| someone seated, Shepard walks a little | `cat004_hangar_shuttle_m_dlg` (`BioA_Cat004_000LevelTrans`: Joker seated, Shepard 2.9 m in one line, 21 of 30 lines framed) | the cockpit; reached already by the Cronos Station route in research/le3-dialogue-cinema.md [verify it is the "cabin" wheel seen there] | `AutoSave` -> galaxy map -> Cronos Station (session `20261115-200511-376-LE3-navigation`'s route) |
| Shepard seated, a ride | `kro002_shuttle_ride_in_m_dlg` (Tuchanka: the shuttle ride, Shepard seated and moved) | seats, rides, black moves | a save before Tuchanka's landing (none on this machine): ask the owner, or a level open with the console [verify a safe route] |

---

## 9. Questions for the owner

**Answered by the owner, 2026-11-19: (a) to all three, on all three games**, as the defaults of the first-person mode (which
itself stays experimental and off by default): ME1's sticks; ME1's moment rule with its current defaults (`worldMoments = view`,
walk-along, `worldBlackMoves` with curated walks excepted; never an automatic guided walk to a mark: ME1's `worldWalkInS = -1`);
scenes with no place for the player on the 3D screen from a table generated from the scan (presence-based). Recorded in
docs/trilogy-plan.md T49 and research/le2-conversations-in-world.md.

1. **The sticks in a conversation held in the world.** ME1: the left stick walks only while the wheel is down and steers the wheel
   when it is up (the stick latched after a pick); the right stick turns (`[Comfort] turn`). The ME3 slice (after the owner's run):
   the left stick always walks, the right stick steers the wheel, no turning. Options: (a) ME1's rule on all three games; (b) the
   slice's on all three; (c) the slice's on the sequels only. **Recommendation: (a)**, one rule for the trilogy and the turn kept;
   the slice's complaint ("the wheel could no longer be steered") came from walking while the wheel was up.
2. **Shepard walked by a conversation's line** (206 full ME3 conversations). Options: (a) ME1's moment rule: the walk withheld, the
   player walks there (guided after a pause, carried after patience, moved under the game's black screen); (b) carry the view with
   the track (the player is moved by the game); (c) keep the slice's pin (the player stays where they stood). **Recommendation: (a)**,
   ME1's "the player is never moved by the game" with its existing escape hatches.
3. **Scenes without a place for the player** (up to 192 full conversations, the docking officer's among them): (a) the 3D screen by
   a generated table; (b) in the world anyway, the view at Shepard's mark. **Recommendation: (a)**, as ME1's presence rules decide.

---

## 10. How the numbers were made

- **Ghidra:** `tools\ghidra\decompile.ps1 -Game LE3 <rva> ...` (one process per project; LE1's project was held by another session
  and not used). The outputs are in `%LOCALAPPDATA%\MEVR\ghidra\out\LE3` (local research data).
- **vtable slots:** the installed `MassEffect3.exe`'s `.rdata` read at the vtable address plus the slot (the `.text` dump has no
  `.rdata`), cross-checked by Ghidra's "pointers to it" lines.
- **Patterns:** `tools/rva-patterns.py` over `MassEffect-60AEF0C4.text.bin` with `--exe`, each `[pdata start]` and unique.
- **The scene scan:** a scratch .NET program over Legendary Explorer's library (not committed; it mirrors `tools/conversation-scan`'s
  loader) opened every `BioD_*`, `BioP_*`, `BioA_*`, `BioH_*`, `BioS_*` package and their `_LOC_INT` ones under `ME3\BioGame`
  (3,786 packages, 0 failures, 73 s). For each `BioConversation`: entries, replies (`ReplyType` standard or absent), `bAmbient` on
  entries and starting entries, `MatineeSequence`; for each `InterpData` under that sequence: its groups' names, move tracks' furthest
  key from the first (`PosTrack.Points[].OutVal`), director, black-screen, near-clip, attach, SetFacing and gesture tracks, and
  gesture pose names matching sitting words (excluding `Transition`, `Position`); for each `SFXSeqAct_StartConversation` /
  `SFXSeqAct_StartAmbientConv`: its `Conv`, `m_bFadeIn`, `m_bBlackOut`, `m_fInterruptRange`; for level `SeqAct_Interp`s outside
  conversation sequences: whether a variable link binds the player and how far his group moves. Conversations are counted once by
  name (the copy with the most lines). Groups whose name contains "cam" or with an FOV track are cameras. The group-name list kept
  12 names per conversation, so "no player group" is an upper bound. This belongs in `tools/conversation-scan`'s LE3 mode (FPC's).
