---
type: System
title: ME1 conversations, cutscenes and scripted scenes in VR
description: What ME1 shows during conversations, cutscenes, the front end and scripted walks; the 3D cinema screen (default) and the experimental first-person mode with its staging rules, as numbered contracts.
tags: [me1, conversations, cinema-screen, first-person, scenes, audio]
status: stable
timestamp: 2026-11-18T07:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T07:00:00Z }
game: LE1
---

# What the player gets

- By default a conversation or cutscene plays on a **3D cinema screen** fixed in the room: 2.9 m wide, 3.5 m ahead, in
  stereo, at the headset's rate, with a floor and a horizon around it. The dialogue wheel and subtitles stand on a layer
  0.5 m in front of the screen. A dip from black marks each change between the world and the screen.
- The title and main menu are on the same screen; the boot before the title and movies are flat.
- With Comfort > Conversations > **First person (experimental)** (`[Cinematics] conversations = world`) the player stays
  in the world: stands where they stood, head tracked, both eyes, body and hands drawn. People keep their places, look
  at the player's eyes, and walk into a circle with the player when they first speak or are spoken to.
- The subtitle sits under the speaker's face, the wheel in front of the player. The left stick walks (while the wheel
  is down), the right stick turns. Voices come from the speakers' heads.
- A scene with nowhere for the player to be (13 set pieces, absent Shepard) plays on the 3D screen even in first person.
- Walk-up arguments play as barks around the player with a subtitle ("flowed" scenes).
- Scripted walks ("moments", e.g. the walk with Anderson) are held in first person: the script moves Shepard's hidden
  pawn, the player walks along, and the walk waits when the player falls behind.
- When the game fades to black and moves Shepard, the player is moved with him under a dip.

# Contracts

Defaults are the code's (`game/ConversationWorld.cpp` `Settings`, `core/vr/CineScreen.hpp` `Settings`). "World" means
`conversations = world`. Units: 100 per metre.

| id | behaviour | numbers | gates | code | verified |
|---|---|---|---|---|---|
| CNV-01 | Conversations (game mode 5) and cutscenes play on a stereo screen fixed in the room where the sequence began; it does not follow the head. | `widthM` 2.9, `distanceM` 3.5 (45 degrees); gaps under 3 s keep the place; `aspect` 16:9 band; `fullRate` 1 | `conversations` = screen (default) or world's leftovers; `cutscenes = 1`; prerendered movies stay flat | `core/vr/CineScreen` `PlacePanel`; `plugin/CineScreen`; seams in `xr_session.cpp` | headset 2026-11-06 |
| CNV-02 | Screen stereo: two parallel cameras on the director's camera at the game's own projection; head look and lean parked. | `depth` 0.010 of width (infinity behind the screen; 1.5% was cross-eyed); `convergeM` 0.8; lens planned at most 80 degrees (`kMaxLensHalfH` 0.698 rad); infinity capped at 80% of the eye distance (`kDivergenceShare`) | screen shown | `CineScreen.cpp` plan, `MetricPass::lensTan`, `ScreenLensScale` | unit test; headset 2026-11-06 |
| CNV-03 | The game's UI (wheel, subtitles) is cut from the eye images and shown on its own layer in front of the screen; the game pointer works on that plane. The conversation movie is at its authored size, not Halcyon's half-size VR layout (wheel 1.75x larger). | `uiLayer` 1, `uiForwardM` 0.5 (max 1.5) | screen shown | `plugin/CineScreen`, `plugin/HudPanel`, `CineScreenPointerPlane` | `tools/cine-screen-check.py`, research/52 "Cross-eyed" |
| CNV-04 | A floor and horizon surround the screen (projection layer under it, floor at the player's real floor). | `backdrop` 1, `backdropBrightness` 1.0 (0..3) | screen shown | `plugin/CinemaBackdrop`, `CinemaBackdropDraw` | offline `mevr-cinema-preview` |
| CNV-05 | A dip from black when the view changes between world, screen and a cinematic's flat movie. | `fadeS` 0.35 (0 off) | every game mode change of presentation | `core/vr/TransitionFade`, `plugin/TransitionFade` (last layer, head-locked black quad) | unit test |
| CNV-06 | Start-up and front end: until the first load from the menus or 30 frames of a gameplay mode, every other mode is the menu presentation; the title and main menu are on the 3D screen in stereo; boot before the title flat with head look parked; movies flat; plain VR after another view waits 100 ms. | 30 frames; 100 ms; `frontEnd` 1 | from process start; after the menus a load's -1 and 8 are flat too | `core/vr/PresentationHold`, `plugin/PresentationHold`; `MevrBridge::FrontEndOnScreen` | fake headset (research/56) |
| CNV-07 | First person: in mode 5 the player's view is held at the on-foot view if fresh, else Shepard's head bone plus the first-person eye offset facing his pawn; both eyes, full rate, body and arm IK kept in the view's frame. | host trusts the mod's word 1.5 s, through a game-thread stall up to 10 s (`cw::StillFresh`); "armed" 150 ms; handing over 300 ms | `conversations = world` (off by default); not set pieces (CNV-22) | `game/ConversationWorld`, `view_anchor::Hold`, `plugin/ConversationWorld`, `MevrBridge::ConversationInWorld` (5 seams in `xr_session.cpp`, 2 in `head_aim.cpp`) | fake headset; owner's sessions 2026-11-10/11 |
| CNV-08 | The player's place follows Shepard's head bone only when it jumps and stays jumped. | jump past 80 units (0.8 m) for 3 ticks (`kReanchorUnits`, `kSettleTicks`); not while his hidden head is unposed nor 1.5 s after he is shown again | `worldFollow` 1; not after a no-cut opening | `cw::FollowHeadSettled`, `cw::HeadFollowFor` | unit test; fake headset |
| CNV-09 | Keep-in-place: the stage's placements of people are withheld at the source; a jump that still happens is undone before the draw. | jump over 30 units in a frame and under 600 units (6 m; farther is a scripted entrance); a repeat of at most 1 m at the same place next tick is accepted (`cw::Insists`: 2 ticks, 5 units, 100 units) | `worldStay` 1, `worldStaySource` 1; actors a matinee move track moves are left to it | `engine/ConversationHooks` pre-hook `UBioConversation::SetPositionRotator`; `Camera.UpdateCamera` post-handler `Actor.SetLocation` | fake headset: 28 withheld, 0 undone; 0 of 25 drawn |
| CNV-10 | People asked onto a mark during a line walk there with the game's AI (`PushMoveToPoint`), then turn to the stage's facing. | after `worldWalkSettleS` 2.5 s; min 0.5 m; only marks 1.5 m or more away (`worldWalkLongM`) or within 60 degrees of facing (`worldWalkStepDeg`); arrive within 25 units; stalled 2 s without 8 units: stays | `worldWalk` 1 (0 off, 2 slide, 3 engine physics = falls back to slide); never hidden people, non-walkers, seated, or trees without `Posture` | `game/ConversationWorld` walk state; `cw::WalkWorthIt`, `AiEnableDeficit` | fake headset 2026-11-11 |
| CNV-11 | Who looks at whom by role: speaker and listener look at each other; a bystander glances at the speaker only if near its facing and never turns its body. | `worldGlanceDeg` 70 (keep 85); look rank 100 (game's own 99999 stand); pair delay 0.15 s, bystander 0.45 s + up to 0.6 s; `worldLookReach` 0 (game's strengths) | `worldTurn = roles` (default; look, always, off) | `cw::RoleOf`, `cw::AttendAs`; `BioPawn.SetLookAtTarget` | fake headset (ceremony: 0 body turns) |
| CNV-12 | A body re-faces only for the player, by an eased turn that also sets the desired rotation, and goes home after its last line with him. | within `worldTurnWithinM` 4 m; target past `worldTurnComfortDeg` 80 for `worldTurnDwellS` 1 s; ends at `worldTurnSettleDeg` 40; home after `worldTurnHomeS` 3 s; at most `worldTurnDegPerS` 60 | never while the player is within 1 m of Shepard's composed mark; never someone seated (head under 0.62 m above its place) | `cw::StepAttention`, `cw::MayReface`, `worldTurnWay` slide | unit test; fake headset |
| CNV-13 | Heads look at the player's drawn eyes, not Shepard's hidden body: when his head bone is half a metre (flat) from the eyes a stand-in look target at the eyes replaces him; facings the stage sets toward his body are turned to the eyes. | 0.5 m; the eyes are drawn 0.43 m behind and 0.18 m below the game's camera (Halcyon's view offset) | `worldLookEyes` 1; in moments over everyone shown within 20 m | `StepEyes`, `PlayerEyes()`, `FlatViewOffset()`, `StageFacingForPlayer` | fake headset 2026-11-12 audit |
| CNV-14 | Opening rule: no cut when Shepard's mark is where the player stands or a walk-in in the same space; a cut (dip) only for another composition or an opening under the game's black. | stay within 150 units; walk-in up to 4000 units flat and 600 up/down; people in view moved over 300 units = another composition; sampled pass: 30 stay, 29 walk in, 11 cut of 120 | `worldOpening = auto` (default; accept = dip at every staged opening; keep) | `cw::DecideOpening`, `OpeningLimits`, `InSight` (three lines) | fake headset sampled pass 2026-11-10 |
| CNV-15 | A walk-in is never walked for the player by default; who walks it (body or view only) is decided by rule: view alone when a moment's walk was on its way, the mark is inside a trigger, or putting Shepard back is refused. | `worldWalkInS` -1 (never guided; 0 at once, N seconds); guide gives up after 2 s blocked | `worldWalkInWay = auto` (body, view) | `cw::DecideWalkIn`, `cw::StepGuide` | fake headset (council hearing 4 of 4) |
| CNV-16 | The left stick walks Shepard's pawn during the conversation along the head's facing; the right stick turns round the eyes as `[Comfort] turn` says. | `worldMoveSpeedMS` 1.4; boundary `worldMoveLeashM` 4.0 m around where the conversation began (a walk-in's around its mark; a moment's around Shepard's track place); floor step of 60 units refused; walls by line of sight | `worldMove` 1; only while the wheel is down and after the opening; not seated (CNV-27) | `cw::StepPlayer`, `TurnBy`; `Actor.SetLocation` | fake headset: 5.47 m walked, nobody snapped |
| CNV-17 | The walk does not go through people and never changes which triggers Shepard touches. | `worldMoveClearM` 0.7; someone already nearer keeps their distance | `worldMoveTriggers` 1 | `cw::KeepClear`; `Actor.PointCheckComponent` against `Trigger`/`TriggerVolume` | fake headset (9 triggers, stopped at the first edge) |
| CNV-18 | The left stick is the wheel's until it has been let go after a pick: a stick still out as the wheel closes does not walk. | released below 0.25 magnitude | wheel up or just closed | `cw::StickWalks`, `StickLatch` | unit test; `mevr convo stick`; audit rules `moved-by-pick`, `turn-ignored` |
| CNV-19 | The conversational circle: people stay put until they first speak or are spoken to, then walk with the AI into a circle with the player; the owner stays; whoever takes part within 4 lines sets off early. | share 1.1 m each (`worldCircleGapM`), radius 0.75..2.2 m; join where standing within 3.5 m (`worldCircleInRangeM`); arrive 0.6 m; max walk 12 m; stagger 0.6 s; lookahead 4; re-gather off (2.5 m, 2 s) | `worldCircle` 1; forms only once the player is in the exchange (`cw::PlayerSoon`); not staged scenes; non-walkers never walk | `cw::MakeCircle`, `JoinCircle`, `UpcomingPeople`, `ShouldRegather` | unit test; fake headset 2026-11-12 |
| CNV-20 | The squad's formation: squadmates that stood with the player are put back beside and a little behind the player before anything is drawn, held by the squad's HOLD formation written quietly; the old formation returns at the end. | within 10 m of Shepard at his height; `worldSquadGatherSideM` 1.7, `worldSquadGatherBackM` 0.5 | `worldSquadGather` 1 | `cw::SquadFormation`, `cw::AssignSpots`; `BioBaseSquad.Members` `vHoldPosition`, `eFormation` | fake headset 2026-11-12 |
| CNV-21 | Staged scenes keep their marks and facings; the opening is under a dip; a new composition under the script's own black screen is taken at once. | 7 keys: `jug20_sal_captainkirrahe`, `sta90_council_saren`, `sta90_council_spectre_induction`, `sta90_grounded`, `end90_council_alive`, `end90_council_dead`, `end90_choice`; moves within 1 s after the black screen | `worldStaged` adds keys | `cw::IsStagedScene`, `ScriptBlackScreen` | fake headset (Virmire) |
| CNV-22 | Set pieces go to the 3D screen even in first person; scenes found at run time to have no place for the player too. | 13 keys (`cw::IsSetPiece`, generated by `conversation-scan.py --set-pieces`); absent = Shepard `worldAbsentM` 15 m from the owner and out of sight, or pawn hidden; far shot = most shots from over 8 m (logged only) | `worldSetPieces = screen`; `worldAutoScreen = on` (off, log, all) | `cw::IsSetPiece`; presence rules in `game/ConversationWorld` | scan; presence rules not seen in the game |
| CNV-23 | The director's near plane and depth of field are kept off the player's view; the game's camera is not moved. | on-foot near plane written back each `Camera.UpdateCamera`; director's is 0.3..2.8 m; `scale set DepthOfField False` for the scene, restored to the value read before | `worldNearPlane` 1; `worldCamera` 0 | `game/ConversationWorld` camera post-handler | fake headset 2026-11-10 |
| CNV-24 | Subtitle on a quad under the speaker's face; wheel on a quad in front of the player. | `subtitleHeightDeg` 4.0, `subtitleBelowM` 0.22, `subtitleTowardM` 0.15; `wheelWidthM` 0.40, `wheelDistanceM` 0.12; `wheelHand` front | world view held | `cw::DefaultSubtitleCrop`, `DefaultWheelCrop`, `SubtitlePoint`; `plugin/ConversationWorld` | headset QA 2026-11-06 (wheel moved to front) |
| CNV-25 | Flowed scenes: chain starts that never reach a wheel are flagged ambient so the game plays them as barks on foot; during one the game's barks are refused and another scene's start is held and fired again after. | table `cw::FlowEntries`: 64 conversations, 249 entries; `worldFlowLeashM` 0 = the game's interrupt range (world default 25 m), -1 never | `worldFlow = all` with world mode; `worldFlowProtect` 1; `worldFlowNewcomers = hold` | `game/ConversationFlow`; `cw::DecideNewcomer`; `ABioWorldInfo::InterruptConversation` hook | fake headset 2026-11-09 (Noveria, C-Sec) |
| CNV-26 | A flowed line keeps a subtitle on the HUD panel. | re-sent every 0.25 s with 0.6 s life, render mode 3 (bottom) | `worldFlowSubtitles` 1 and the player's subtitle option on | `cw::ShowBarkSubtitle`; `BioSubtitles.DisplaySubtitle` | [verify] on the HUD |
| CNV-27 | Seats: the view sits with Shepard and stands up with him; the stick does not walk while seated; the dancer's seat is a seated scene, not a set piece. | seated = animated head under 0.62 m above his place for 0.5 s; view follows once his head is 15 cm under standing height, 1 m/s, 4 cm deadband | any conversation in the world | `PlayerSeated`, `StepSeat`, `cw::IsSeatedScene` (`sta60_stripperseat`) | [verify] headset |
| CNV-28 | Moments: a level-script matinee that walks Shepard is held in first person; the track moves his hidden pawn through every trigger, the view stays the player's, walks by the stick and joins the pawn at rest; his body is drawn where the player stands, legs by IK at rest. | wait for a track up to 6 s after the cinematic switch, let go 0.25 s after a scene camera takes the view (`cw::StepProvisional`); a track starting over 3 s into cinematic mode plays on the screen; `worldMomentPatienceS` 12, `worldMomentCarryMS` 3 (walk/carry modes) | `worldMoments = view` (walk, carry, log, off); `worldBody = player`; `worldLegs = stand` | `game/AuthoredMoment`, `TickMoment`; pre-hook `AActor::MoveWithInterpMoveTrack`; `cw::TrailTarget`, `IsRideStep` | fake headset (council sequence) |
| CNV-29 | A scripted walk waits for a player who falls behind: the matinee is paused at a line's end and resumes when the player catches up. | pause over `worldWalkAlongPauseM` 3 m behind, resume within 1.5 m, at most `worldWalkAlongMaxS` 120 s; companion within 5 m of Shepard's track place | `worldWalkAlong` 1 | `cw::StepWalkAlong`; matinee `bPaused` | unit test; fake headset |
| CNV-30 | The game's fade to black always moves the player: Shepard moved under a black screen or matinee fade takes the player with him under a dip. Curated walks keep the walk. | a mark over 1.5 m away is a teleport; nearer is walked by the simulated stick; in a moment any distance; curated: the walk with Anderson to the Council | `worldBlackMoves` 1; `worldCuratedWalks` adds | `TeleportPlayerTo`, `StartSteer`, `UnderGameBlack()`, `cw::IsCuratedWalk` | [verify] headset (owner's rule 2026-11-16) |
| CNV-31 | A script's black screen during a held moment becomes the mod's dip only if it covered a change in the player's view; a matinee's own camera fade is not drawn. | change = someone in view gone, moved 150 units at once or new within 0.3 s; within `worldDipWithinM` 8 m | held moment | `cw::CoveredChanges`, `cw::NearestChange`, `StepMatineeFade` (`Camera.bEnableFading`) | fake headset; owner's ceremony 2026-11-10 |
| CNV-32 | What Shepard stands on carries the player (a lift moves the held view and every kept place). | follows `Actor.Base` motion | conversation in the world | `RideBase` | research/69 item 42 [verify] headset |
| CNV-33 | No flash of the screen at a scene's start or end: on foot the mod says "armed"; a world conversation handing over to a GUI screen or a moment keeps the host for 300 ms. | 150 ms coming, 300 ms handing over | world mode | `MevrBridge::ConversationWorldOrComing`, `ConversationHandover`; `State::holdPad` | session judge rule `flash` |
| CNV-34 | Voices are placed: a speaker near the player is heard from their head; farther speakers keep the game's centred mix. | within 8 m (kept to 10); full level within 2 m, 3 dB per doubling, floor 6 dB, gain 1 | `worldVoices = spatial`; ISACT sources whose owner is a speaker | `cw::PlaceVoice`; `engine/VoicePlace` post-hook `0x7BA90` | measured: 77 degrees right = +8 dB right (`tools/audio-balance.py`) |
| CNV-35 | Two-ear audio: OpenAL Soft with HRTF replaces the game's OpenAL router functions; no game file replaced. | `[Audio] spatial = hrtf`, `gainDb` 3; measured 0.9 ms interaural delay at 77 degrees | read at attach; stands down if Creative's driver is already loaded | `plugin/OpenAlSoft` | measured (research/69) |
| CNV-36 | A scene shown only under its black screen is shown as the walk to it begins (the council during the walk with Anderson). | table `cw::ShownEarlyFor` | `worldShowEarly` 1 | `LevelStreaming.bShouldBeVisible`; `mevr stream show` | fake headset |
| CNV-37 | The player's own body stays drawn in every world conversation: first-person hides kept in far shots, body boxes padded, camera collision off. | view offset 0.43 m; padding offset + 15 units | world view held; first person | `head_aim.cpp` seams; `engine/BodyBounds`; `game/CameraCollision` | audit rule `body-not-drawn`; headset culling pending |

# Arbitration

| input | claimant order | loser sees |
|---|---|---|
| left stick, world conversation | 1 the game's wheel while it is up and until released after a pick (CNV-18); 2 the walk (CNV-16) | no walk while the wheel is up; Thespian's hidden wheel returns on a stick push |
| right stick | `[Comfort] turn` (CNV-16, `worldStickTurns` 1) | the game sees no turn |
| conversation presentation | 1 set piece or presence rule: screen; 2 world mode: world; 3 default: screen; `conversations = vr`: Halcyon's VR cine | one presentation per scene; change under the dip (CNV-05) |
| a new conversation during a flowed scene | the flowed scene; barks refused, scenes held (CNV-25) | fired again when the scene and its follow-up end |
| Shepard's place in a moment | the script's track owns the pawn; the player owns the view (CNV-28) | the stick never moves the pawn through triggers |
| game's black screen vs keep-in-place | the black screen wins: the player goes with Shepard (CNV-30), except curated walks | no walk-to-mark |

# Problems ME1 already solved

| problem | fix | where |
|---|---|---|
| The crop put wheel and subtitles behind faces: cross-eyed | UI on its own layer 0.5 m in front; depth 1.0% | research/52 "Cross-eyed" |
| A scene's first frame used gameplay's lens for the camera spacing | spacing scaled to the lens the frame is drawn with | research/52 |
| Title flashed plain VR and the 3D screen at start-up | presentation hold; 100 ms wait before plain VR | research/56 |
| Flat front end moved with the head | front end on the fixed 3D screen | research/56 "The front end on the 3D screen" |
| Conversations teleport actors onto marks at camera cuts | withhold `SetPositionRotator`, undo jumps before the draw | research/59 "Keeping people in place" |
| The director's near plane removed the floor, the body and near people | on-foot near plane written back each camera update | [gotcha](../gotchas/conversation-camera-near-plane.md) |
| The body culled under the eye (culling camera ahead of the drawn eye) | body boxes padded, Umbra hand-over repeated | [gotcha](../gotchas/culling-camera-ahead-of-the-eye.md) |
| Camera collision dropped the camera to Shepard's knees | `bCollisionEnabled` cleared in first person | [gotcha](../gotchas/camera-collision-in-first-person.md) |
| Arm IK fell to third person and the body stayed at the mark | `BodyAway` in every world conversation; animated head read, not the slid one | [gotcha](../gotchas/arm-ik-third-person-anchor.md) |
| Heads looked 0.34-0.75 m past the player (held place is the camera, not the eyes) | `PlayerEyes()`, view offset subtracted | [gotcha](../gotchas/held-place-is-not-the-eyes.md) |
| Everyone pivoting toward each speaker looked unnatural | roles; bystanders glance only | research/69 "Who attends to whom" |
| People in gesture mode slid instead of walking | `m_bInGestures` cleared for the walk, four holds lifted per walker | research/69 "People walking to their marks" |
| A walk's last step fought keep-in-place 512 times | `cw::Insists` yields to a repeated small placement | research/69 |
| A stick still out after a pick walked Shepard half a metre | `cw::StickWalks` latch | research/69 "The ceremony in the headset" |
| Far-shot camera mode unreadable: Halcyon cleared first-person hides | second seam in `head_aim.cpp` | research/69 "The ceremony in the headset" |
| Putting Shepard back from the podium ended the hearing (9 of 16) | walk-in by the view when a moment was on its way | research/69 "The hearing that ended at once" |
| With the track's move withheld the ceremony never began | `worldMoments = view`: the script's pawn goes through every trigger | research/69 "The script's Shepard, the player's view" |
| A walk to the mark left the player outside an elevator; mission stalled | the game's black always moves the player | research/69 item 41 |
| The view stayed at the bottom of a lift | `RideBase` | research/69 item 42 |
| A stalled game thread showed the 3D screen for a frame | `cw::StillFresh` counts engine ticks, up to 10 s | research/69 item 40 |
| A pick between lines, or `EndConversation` alone, crashed the game | pick only with the wheel up; end through `BioWorldInfo.EndCurrentConversation` | research/69 item 37; [gotcha](../gotchas/conversation-reply-natives-without-a-wheel.md) |
| Freed pawns in the pawn list after a save load ended the game | `LivePawns` stops at the first dead entry | CLAUDE.md gotchas; research/69 |
| 82% of dialogue cues play centred | ISACT source update hooked, head place sent | research/69 "The voices, placed" |
| One-by-one OpenAL hooks lost the race with the game's device open | all detours enabled at once at attach | research/69 "OpenAL Soft as the mod ships it" |
| The conversation camera's depth of field blurred the room; the game writes the live value to the ini | DoF off for the scene, restored to the value read before | research/59 |
| Test runs autosave into the career | save backups, `-RestoreSaves` rules | [gotcha](../gotchas/test-runs-overwrite-saves.md) |

# Code map

| part | files | scope |
|---|---|---|
| Screen rules | `src/core/vr/CineScreen`, `TransitionFade`, `PresentationHold` | game-independent core, unit-tested |
| First-person rules (`cw::`) | `src/core/vr/ConversationWorld` (set-piece, staged, seated, curated, flow tables are ME1 conversation names) | game-independent code, ME1 data |
| Screen host | `src/plugin/CineScreen`, `CinemaBackdrop`, `CinemaBackdropDraw`, `TransitionFade` | host; LE1 seams in `src/halcyon/le1/xr_session.cpp`; the sequels reach the core through `plugin/SequelCinema` (ledger R21) |
| Front end | `src/plugin/PresentationHold` | LE1 host only (ledger R22: missing on sequels) |
| First-person game side | `src/game/ConversationWorld`, `ConversationFlow`, `AuthoredMoment`, `src/engine/ConversationHooks` (three raw addresses) | in the mod's source list for every game, no `MEVR_GAME_LE1` guard; stands down off LE1 (ledger G36: missing, deferred) |
| First-person host | `src/plugin/ConversationWorld`; HostServices v33 `setConversationWorld` | LE1 seams (`xr_session.cpp`, `head_aim.cpp`, `render_hook.cpp`) |
| Voices | `src/engine/VoicePlace` (ISACT `0x7BA90`), `src/game/SoundPlace`, `src/core/vr/SoundPlace` | LE1 only in effect (ledger G38); core game-independent |
| Two-ear audio | `src/plugin/OpenAlSoft` | LE1 only (ledger G37: sequels use Wwise, n/a) |
| Body in scenes | `src/engine/BodyBounds`, `src/game/CameraCollision`, `src/game/BodyFollow`, `src/game/ArmIk` | LE1 addresses (ledger R29) |

# Settings and defaults

All in `[Cinematics]` of `ASI\LE1MEVR.ini` unless noted; the mod re-reads every second.

| key | default | reader | menu |
|---|---|---|---|
| `conversations` | screen (`world`, `vr`) | host and mod | Comfort > Conversations and cutscenes |
| `cutscenes`, `frontEnd` | 1, 1 | host | cutscenes row |
| `widthM`, `distanceM`, `depth` | 2.9, 3.5, 0.010 | host | sliders |
| `convergeM`, `aspect`, `fullRate`, `uiLayer`, `uiForwardM` | 0.8, 16:9, 1, 1, 0.5 | host | no |
| `backdrop`, `backdropBrightness`, `fadeS` | 1, 1.0, 0.35 | host | no |
| `worldOpening`, `worldWalkInWay`, `worldWalkInS` | auto, auto, -1 | mod | no |
| `worldStay`, `worldStaySource`, `worldWalk` | 1, 1, 1 | mod | no |
| `worldTurn`, `worldGlanceDeg`, `worldLookReach`, `worldLookEyes` | roles, 70, 0, 1 | mod | no |
| `worldTurnWithinM`, `worldTurnComfortDeg`, `worldTurnSettleDeg`, `worldTurnHomeS` | 4, 80, 40, 3 | mod | no |
| `worldMove`, `worldMoveLeashM`, `worldMoveSpeedMS`, `worldMoveClearM`, `worldMoveTriggers` | 1, 4.0, 1.4, 0.7, 1 | mod | no |
| `worldCircle`, `worldCircleLookahead`, `worldCircleOwnerStays`, `worldCircleRegather` | 1, 4, 1, 0 | mod | no |
| `worldArrange`, `worldSquadGather` | 0, 1 | mod | no |
| `worldSetPieces`, `worldAutoScreen`, `worldAbsentM`, `worldStaged` | screen, on, 15, empty | mod | no |
| `worldMoments`, `worldBody`, `worldLegs`, `worldWalkAlong`, `worldShowEarly` | view, player, stand, 1, 1 | mod | no |
| `worldBlackMoves`, `worldCuratedWalks`, `worldDipWithinM` | 1, empty, 8 | mod | no |
| `worldNearPlane`, `worldCamera` | 1, 0 | mod | no |
| `worldFlow`, `worldFlowNewcomers`, `worldFlowProtect`, `worldFlowSubtitles`, `worldFlowLeashM` | all, hold, 1, 1, 0 | mod | no |
| `worldVoices`, `worldVoiceWithinM`, `worldVoiceNearM`, `worldVoiceFalloffDb`, `worldVoiceFloorDb` | spatial, 8, 2, 3, 6 | mod | no |
| `wheelHand`, `wheelWidthM`, `subtitleHeightDeg`, `subtitleBelowM` | front, 0.40, 4.0, 0.22 | mod | no |
| `[Audio] spatial`, `gainDb`, `overheard`, `gun` | hrtf, 3, [verify], [verify] | host (first two, at attach), mod | Sound page |
| `[Game] cameraCollision` | off in first person | mod | no |

Diagnostics, off by default: `worldTurnLog`, `worldWalkLog`, `worldBodyLog`, `worldStances`.

# Checks

| kind | what |
|---|---|
| logs | `cine screen:`, `cinema backdrop:`, `transition fade:`, `presentation:`, `convo:` (`summary`, `audit:`, `circle:`, `squad:`, `stance:`, `eyes:`, `steer:`, `voices:`), `convo flow:`, `moment:`, `conversation world:`, `voice place:`, `audio:`, `body bounds:` |
| snapshots | `"cineScreen"`, `"mod"."conversationWorld"`, `"mod"."soundPlace"` |
| console | `mevr convo dump, world on/off, pick, skip, end, stance, stances, ahead, audit, body, eyes, follow, stick, turn, goto, walk, stall, flow list/rescan/restore`; `mevr moment`; `mevr stream chunks, state, show, people`; `mevr voices census` |
| unit tests | `tests/ConversationWorldTests.cpp`, `tests/SoundPlaceTests.cpp`; ctest `mevr-scene-audit`, `mevr-conversation-sweep`, `mevr-le1-save` |
| offline | `mevr-cinema-preview`; `tools/cine-screen-check.py`; `tools/conversation-scan.py` (491 conversations classified, verdicts, set-piece and flow lists) |
| audit rules | `tools/scene_audit.py`: `look-at-body`, `eyes-off-body`, `face-off-you`, `speaker-behind`, `too-close`, `squad-behind`, `walk-pauses`, `walk-left-behind`, `hands-off-controllers`, `body-not-drawn`, `person-not-drawn`, `near-plane`; never excused by a baseline: `eyes-off-body`, `hands-off-controllers`, `body-not-drawn`, `near-plane` |
| runs | `tools/conversation-audit.ps1` (set, baseline, `-Canaries`), `tools/conversation-from-save.ps1 -All`, `tools/conversation-launch.ps1`, `tools/conversation-sweep.ps1`, `tools/conversation-session.py` (after a headset session), `tools/conversation-stance.py`, `tools/audit-report.py`, `tools/audio-balance.py` |

# Citations

- Research: [52 cine 3D screen](../../../research/52-cine-3d-screen.md), [56 start and front end](../../../research/56-start-and-front-end.md),
  [59 conversations in the world](../../../research/59-conversations-in-world.md),
  [69 conversations everywhere](../../../research/69-conversations-everywhere.md) (status table and log; read before touching the feature).
- Gotchas: [near plane](../gotchas/conversation-camera-near-plane.md), [culling camera](../gotchas/culling-camera-ahead-of-the-eye.md),
  [camera collision](../gotchas/camera-collision-in-first-person.md), [held place is not the eyes](../gotchas/held-place-is-not-the-eyes.md),
  [arm IK anchor](../gotchas/arm-ik-third-person-anchor.md), [reply natives](../gotchas/conversation-reply-natives-without-a-wheel.md),
  [saves](../gotchas/test-runs-overwrite-saves.md).
- Core: [CineScreen.hpp](../../../src/core/vr/CineScreen.hpp), [ConversationWorld.hpp](../../../src/core/vr/ConversationWorld.hpp),
  [ConversationWorld.cpp](../../../src/core/vr/ConversationWorld.cpp), [TransitionFade.hpp](../../../src/core/vr/TransitionFade.hpp),
  [PresentationHold.hpp](../../../src/core/vr/PresentationHold.hpp), [SoundPlace.hpp](../../../src/core/vr/SoundPlace.hpp).
- Game: [ConversationWorld.cpp](../../../src/game/ConversationWorld.cpp), [ConversationFlow.cpp](../../../src/game/ConversationFlow.cpp),
  [AuthoredMoment.cpp](../../../src/game/AuthoredMoment.cpp), [SoundPlace.cpp](../../../src/game/SoundPlace.cpp),
  [ConversationHooks.cpp](../../../src/engine/ConversationHooks.cpp), [VoicePlace.cpp](../../../src/engine/VoicePlace.cpp).
- Host: [CineScreen.cpp](../../../src/plugin/CineScreen.cpp), [ConversationWorld.cpp](../../../src/plugin/ConversationWorld.cpp),
  [CinemaBackdrop.cpp](../../../src/plugin/CinemaBackdrop.cpp), [TransitionFade.cpp](../../../src/plugin/TransitionFade.cpp),
  [PresentationHold.cpp](../../../src/plugin/PresentationHold.cpp), [OpenAlSoft.cpp](../../../src/plugin/OpenAlSoft.cpp),
  [xr_session.cpp](../../../src/halcyon/le1/xr_session.cpp), [head_aim.cpp](../../../src/halcyon/le1/head_aim.cpp).
- Tools: [scene_audit.py](../../../tools/scene_audit.py), [conversation-audit.ps1](../../../tools/conversation-audit.ps1),
  [conversation-scan.py](../../../tools/conversation-scan.py), [conversation-session.py](../../../tools/conversation-session.py).
- Ledger: [trilogy parity ledger](../../trilogy-parity-ledger.md) rows R21, R22, R29, G36, G37, G38.
