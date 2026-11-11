// SPDX-License-Identifier: MIT
// LE1 conversations in the world, first person: the game's side (game/ConversationWorld.hpp, research/59).
#include "game/ConversationWorld.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/Rigid.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/HostView.hpp"
#include "engine/CallFunction.hpp"
#include "engine/ProcessEvent.hpp"
#include "engine/ConversationHooks.hpp"
#include "engine/NameLookup.hpp"
#include "engine/VoicePlace.hpp"
#include "game/AuthoredMoment.hpp"
#include "game/CallTrace.hpp"
#include "engine/SettingsFile.hpp"
#include "engine/Skeleton.hpp"
#include "engine/XrToUnreal.hpp"
#include "game/ConsoleRequest.hpp"
#include "game/GalaxyDump.hpp"
#include "game/Player.hpp"
#include "game/SquadWeapons.hpp"
#include "game/ViewAnchor.hpp"
#include "ue3/Handles.hpp"
#include "ue3/Params.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace mevr::game::conversation_world {

namespace {

namespace cw = vr::conversation_world;

constexpr std::uint8_t kModeConversation = 5;
constexpr double kUnitsPerMeter = 100.0;  // research/30: life-size at 100 units per metre
constexpr double kRadToDeg = 57.29577951308232;
constexpr double kRotToDeg = 360.0 / 65536.0;

// The live conversation (SFXGame.BioWorldInfo.m_oCurrentConversation) and what it holds.
ue3::FieldRef gActorWorldInfo{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gCurrentConversation{"SFXGame.BioWorldInfo", "m_oCurrentConversation"};
ue3::FieldRef gSpeaker{"SFXGame.BioConversation", "m_pSpeaker"};
ue3::FieldRef gListener{"SFXGame.BioConversation", "m_pListener"};
ue3::FieldRef gOwner{"SFXGame.BioConversation", "m_pConversationOwner"};
ue3::FieldRef gConvPlayer{"SFXGame.BioConversation", "m_pConversationPlayer"};
ue3::FieldRef gStage{"SFXGame.BioConversation", "m_stage"};
ue3::FieldRef gSpeakerList{"SFXGame.BioConversation", "m_SpeakerList"};
ue3::FieldRef gSpeakerActor{gSpeakerList, "aSpeaker"};
ue3::FieldRef gLookAtList{"SFXGame.BioConversation", "m_LookAtList"};
ue3::FieldRef gLookActor{gLookAtList, "pActor"};
ue3::FieldRef gLookTarget{gLookAtList, "pLookAtTarget"};
ue3::FieldRef gSavedPositions{"SFXGame.BioConversation", "m_aFSPrevPositions"};
ue3::FieldRef gSavedActor{gSavedPositions, "pActor"};
ue3::FieldRef gSavedPos{gSavedPositions, "vPos"};
ue3::FieldRef gSubtitle{"SFXGame.BioConversation", "m_sCurrentSubTitle"};
ue3::FieldRef gCurrentEntry{"SFXGame.BioConversation", "m_nCurrentEntry"};
ue3::FieldRef gUseFixedCameras{"SFXGame.BioConversation", "bUseFixedCameras"};
// The dialogue wheel: the conversation movie's handler on the GUI manager's panels (as game/WorldUi reads them).
ue3::FieldRef gCachedManager{"Engine.PlayerController", "m_oCachedScaleFormManager"};
ue3::FieldRef gPanels{"Engine.BioSFManager", "m_lstPanels"};
ue3::FieldRef gPanelHandler{"Engine.BioSFPanel", "m_DefaultHandler"};
ue3::ClassRef gConvHandlerClass{"SFXGame.BioSFHandler_Conversation"};
ue3::FieldRef gDisplayingWheel{"SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel"};
ue3::FieldRef gSlotsUsed{"SFXGame.BioSFHandler_Conversation", "m_bSlotsUsed"};  // byte[6]: which wheel locations hold a reply
ue3::FunctionRef gUpdateCamera{"Engine.Camera.UpdateCamera"};
ue3::FunctionRef gFastTrace{"Engine.Actor.FastTrace"};
ue3::FunctionRef gSelectEntry{"SFXGame.BioSFHandler_Conversation.SelectConversationEntry"};  // mevr convo pick (tests)
ue3::FunctionRef gSkipConversation{"SFXGame.BioSFHandler_Conversation.SkipConversation"};     // mevr convo skip (tests)
// Keeping people where they stand (research/59 "Keeping people in place"): the game's placements are native (no script call or
// event moves them, call trace 2026-11-06), so a jump is undone after it, in the camera update of the same tick, before the draw.
ue3::FunctionRef gSetLocation{"Engine.Actor.SetLocation"};
// The survey's `mevr convo end` (research/59 "The survey"): the conversation's own end, for one that loops past its time.
ue3::FunctionRef gEndConversation{"SFXGame.BioConversation.EndConversation"};
ue3::FunctionRef gSetRotation{"Engine.Actor.SetRotation"};
ue3::FunctionRef gRequestTurn{"SFXGame.BioPawn.RequestTurnInPlace"};  // the game's own turn on the spot, with its animation
// Whom a pawn looks at, as the game has it (the look-at chain's own target), and the game's function that gives it one
// (research/69 "Who attends to whom").
ue3::FieldRef gPawnLookData{"SFXGame.BioPawn", "m_oLookAtData"};
ue3::FieldRef gLookDataTarget{"SFXGame.BioLookAt", "m_oTarget"};
ue3::FieldRef gLookDataRank{"SFXGame.BioLookAt", "m_nRank"};
ue3::FunctionRef gSetLookAt{"SFXGame.BioPawn.SetLookAtTarget"};
// How far each link of the look-at chain turns: a controller per link, each pointing at its bone's definition in the
// appearance's shared look-at definition (research/69 "How far a head turns").
ue3::FieldRef gLookControllers{"SFXGame.BioLookAt", "m_Controllers"};
ue3::FieldRef gLookDefinition{"SFXGame.BioLookAt", "m_Definition"};
ue3::FieldRef gControllerDefinition{"SFXGame.BioPawnLookAtController", "mc_pDefinition"};
ue3::FieldRef gBoneDefinitions{"SFXGame.BioLookAtDefinition", "BoneDefinitions"};
ue3::FieldRef gLinkStrength{gBoneDefinitions, "m_fConversationStrength"};
// The facing a pawn's own physics turns it to (Actor.DesiredRotation, by BioPawn.SetDesiredRotation): a rotation set without
// it is turned back by the pawn's physics within the tick (the ceremony's Anderson, 2026-11-10: every step of an eased turn).
ue3::FunctionRef gSetDesiredRotation{"SFXGame.BioPawn.SetDesiredRotation"};
ue3::ClassRef gTurnPawnClass{"SFXGame.BioPawn"};
ue3::FunctionRef gTrace{"Engine.Actor.Trace"};  // the floor under an arc seat (Part B)
ue3::FieldRef gWalkController{"Engine.Pawn", "Controller"};
ue3::FieldRef gWalkVelocity{"Engine.Actor", "Velocity"};
// The engine's own walking (worldWalk = 3): the pawn's acceleration toward the mark at walking pace, its physics doing the
// rest (research/69 "Walking to marks": the conversation mode forbids AI movement, bAllowAIMovement).
ue3::FieldRef gWalkAcceleration{"Engine.Actor", "Acceleration"};
ue3::FieldRef gWalkPhysics{"Engine.Actor", "Physics"};
ue3::FieldRef gWalkAccelRate{"Engine.Pawn", "AccelRate"};
ue3::FieldRef gWalkIsWalking{"Engine.Pawn", "bIsWalking"};
ue3::ClassRef gWalkPawnClass{"Engine.Pawn"};
ue3::ClassRef gWalkAiClass{"SFXGame.BioAiController"};
ue3::FunctionRef gPushMove{"SFXGame.BioAiController.PushMoveToPoint"};
// Terminate only the movement action we pushed, never clear the actor's other AI actions.
ue3::ClassRef gMoveDataClass{"SFXGame.BioSeqData_MoveToPoint"};
ue3::FieldRef gMoveTerminate{"SFXGame.BioSeqData_MoveToPoint", "Terminate"};
// The AI's own walk in a conversation (research/69 "Walking to marks", 2026-11-11). The game's move is refused unless the pawn's
// AI is enabled (PushMoveToPoint's native asks BioAiController.CanUseStack: the behaviour's m_nALLEnabled and m_nStackEnabled
// counters above 0; the levels start some people with their AI off, C-Sec's Wrex and officers among them), and a pushed move is
// cancelled every tick while the game mode forbids AI movement (ShouldCancelMove asks DoesGameModeAllowMovement, false in the
// conversation mode; the game's own BioAI_Ambient answers true). For a walker only: its counters raised to 1 for the walk and
// lowered by as much after it, its controller's question answered true while it walks.
ue3::FieldRef gBioComponent{"Engine.Actor", "oBioComponent"};
ue3::ClassRef gEpicBehaviorClass{"SFXGame.BioEpicPawnBehavior"};
ue3::FieldRef gAiAllEnabled{"SFXGame.BioEpicPawnBehavior", "m_nALLEnabled"};
ue3::FieldRef gAiStackEnabled{"SFXGame.BioEpicPawnBehavior", "m_nStackEnabled"};
ue3::FunctionRef gAllowMovement{"SFXGame.BioAiController.DoesGameModeAllowMovement"};
ue3::FunctionRef gAllowMovementHenchman{"SFXGame.BioAI_Henchman.DoesGameModeAllowMovement"};
// ... and the conversation pauses its people's controllers (BioAiController.PauseController: the pawn's desired speed forced to
// 0), while the move state never raises it (normally AdjustGroundSpeed does, once the pawn accelerates): the walker's own
// BioPawn.SetDesiredSpeed(1) while it walks, 0 again after it (as the move state's Cleanup).
ue3::FunctionRef gSetDesiredSpeed{"SFXGame.BioPawn.SetDesiredSpeed"};
// ... and the body's animation: a conversation's gesture track puts each actor it animates in the gesture mode
// (BioPawn.m_bInGestures, set natively when its gesture starts, 0xC904A0), and the animation tree's top action node then plays
// the gestures instead of the posture branch, where locomotion lives (BioAnimNodeBlendByAction's update, 0xBFD3C0: child 10
// "Gestures" over child 0 "Posture"). A walker leaves the gesture mode for its walk; the next gesture the scene gives it
// puts it back (the track's own start, as it does after any gesture mode ended).
ue3::FieldRef gInGestures{"SFXGame.BioPawn", "m_bInGestures"};
bool gAllowAnswerable = false;                  // the answer handler is registered (TryStartup, before the CallFunction hook)
std::vector<ue3::Object*> gWalkingControllers;  // controllers walking one of ours: the game mode's movement ban doesn't cancel it
int gAllowAnswers = 0;                           // times the question was answered for a walker (logged at each walk's end)
int gAiStalled = 0;                              // AI walks the game's AI couldn't make (unavailable, or no move in 2 s): they stayed
constexpr double kAiArriveUnits = 25.0;          // an AI walk arrives within the move's own tolerance (20) and a little
constexpr double kAiStallS = 2.0;                // ... and slides the rest when it hasn't moved 8 units in this long (a path
                                                 // around a bed paused 1 s in the wake-up scene, 2026-11-11)

// Walking and the level's triggers (research/69 item 5): Actor.SetLocation is UWorld::FarMoveActor, which begins and ends touches
// like any move, so a step may not change which triggers Shepard's cylinder overlaps.
ue3::ClassRef gTriggerClass{"Engine.Trigger"};
ue3::ClassRef gTriggerVolumeClass{"Engine.TriggerVolume"};
ue3::FieldRef gCollisionComponent{"Engine.Actor", "CollisionComponent"};
ue3::FunctionRef gPointCheck{"Engine.Actor.PointCheckComponent"};
ue3::FieldRef gPawnCylinder{"Engine.Pawn", "CylinderComponent"};
ue3::FieldRef gCylinderRadius{"Engine.CylinderComponent", "CollisionRadius"};
ue3::FieldRef gCylinderHeight{"Engine.CylinderComponent", "CollisionHeight"};
// Shepard not in the scene (research/69 item 3).
ue3::FieldRef gActorHidden{"Engine.Actor", "bHidden"};
ue3::FieldRef gWorldOfActor{"Engine.Actor", "WorldInfo"};
ue3::FieldRef gWorldPawns{"Engine.WorldInfo", "PawnList"};
ue3::FieldRef gNextPawnOf{"Engine.Pawn", "NextPawn"};
ue3::FunctionRef gSetHidden{"Engine.Actor.SetHidden"};
// The body's visibility, logged (research/69 "The plan for the blocker", phase A): every flag that stops a pawn being drawn.
ue3::FieldRef gHiddenRequests{"SFXGame.BioPawn", "m_nHiddenRequestCount"};
ue3::FieldRef gCompHiddenGame{"Engine.PrimitiveComponent", "HiddenGame"};
ue3::FieldRef gCompOwnerNoSee{"Engine.PrimitiveComponent", "bOwnerNoSee"};
ue3::FieldRef gCompOnlyOwnerSee{"Engine.PrimitiveComponent", "bOnlyOwnerSee"};
ue3::FieldRef gCompIgnoreOwnerHidden{"Engine.PrimitiveComponent", "bIgnoreOwnerHidden"};
ue3::FieldRef gCompIgnoreMembership{"Engine.PrimitiveComponent", "bIgnoreHiddenActorsMembership"};
ue3::FieldRef gCompLastRender{"Engine.PrimitiveComponent", "LastRenderTime"};
ue3::FieldRef gCompBounds{"Engine.PrimitiveComponent", "Bounds"};
ue3::FieldRef gCompBoundsOrigin{gCompBounds, "Origin"};
ue3::FieldRef gCompBoundsExtent{gCompBounds, "BoxExtent"};
ue3::FieldRef gActorLastRender{"Engine.Actor", "LastRenderTime"};
ue3::FieldRef gWorldSeconds{"Engine.WorldInfo", "TimeSeconds"};
ue3::FieldRef gHiddenActors{"Engine.PlayerController", "HiddenActors"};
// The near plane a conversation's camera carries (research/69 "The first shot").
ue3::FieldRef gBodyLocalPlayer{"Engine.PlayerController", "Player"};
ue3::FieldRef gBodyNearClip{"Engine.LocalPlayer", "NearClipPlane"};
ue3::FieldRef gCameraViewTarget{"Engine.Camera", "ViewTarget"};          // what the game's camera looks from (the player's, or a
ue3::FieldRef gCameraViewTargetActor{gCameraViewTarget, "Target"};       // matinee's camera actor in game mode 0: Chellik's bump)
ue3::ClassRef gConvCameraClass{"SFXGame.BioCameraBehaviorConversation"};
ue3::FieldRef gConvCameraNear{"SFXGame.BioCameraBehaviorConversation", "m_fNearPlane"};
ue3::FieldRef gConvCameraFov{"SFXGame.BioCameraBehaviorConversation", "CAMERA_FOV"};

// ---- Settings ([Cinematics] in LE1MEVR.ini, read every second) -----------------------------------------------------------------
struct Settings {
    cw::Mode mode = cw::Mode::Screen;  // the shipped default (research/52); world is opt-in
    bool follow = true;                // the player's place follows Shepard's head bone when it jumps (worldFollow)
    float subtitleHeightDeg = 4.0f;  // two rows of the line: 3 read small at 2 m (2026-11-06)
    float subtitleBelowM = 0.22f;
    float subtitleTowardM = 0.15f;
    int wheelHand = 2;                 // 0 left, 1 right, 2 in front (the default since the owner's QA, 2026-11-06: at the hand it
                                       // was out of sight with the arm down)
    float wheelWidthM = 0.40f;
    float wheelDistanceM = 0.12f;
    bool stay = true;                  // keep people where they stand: undo the conversation's jumps onto its marks (worldStay)
    bool staySource = true;            // ... and withhold the placements in the game's own placement function (worldStaySource)
    float absentM = 15.0f;             // Shepard this far from the conversation's owner and out of its sight: a cutscene (worldAbsentM, 0 off)
    bool moveTriggers = true;          // the walk never changes which triggers Shepard touches (worldMoveTriggers)
    int autoScreen = 2;                // a scene found at run time to be no place for the player (worldAutoScreen): 0 off, 1 logged
                                       // only, 2 (on, the default since 2026-11-11, research/69 item 10) the presence rules move it to
                                       // the 3D screen and the far shots are logged, 3 (all) both move it
                                       // the 3D screen (worldAutoScreen = off | log | on; research/69 item 3)
    int walk = 1;                     // 0 off, 1 the game's AI (default since 2026-11-11), 2 slide, 3 the engine's walking physics
    float walkSettleS = 2.5f;
    float walkMinM = 0.5f;
    float walkSpeedMS = 1.4f;
    float turnDegPerS = 60.0f;         // a body turn of ours: at most this fast (worldTurnDegPerS; 120 until 2026-11-10)
    // Who turns (research/69 "Who turns, and how"; worldTurn = look | always | off): look: the game's look-at chain does the
    // looking and the body re-faces only past what it covers (cw::StepAttention); always: every body toward its look-at
    // target every tick (until 2026-11-10); off: the game's own facings stand.
    cw::TurnRule turnRule = cw::TurnRule::Roles;
    // Who attends to whom by their part in the line (worldTurn = roles; cw::AttendAs): a bystander looks at the speaker only
    // within worldGlanceDeg of its body's facing (kept to worldGlanceKeepDeg); worldLookRank is the rank our look-at targets
    // are given to the game with.
    float glanceDeg = 70.0f;
    float glanceKeepDeg = 85.0f;
    // (Rank: the game's own look-ats, authored tracks among them, are given at 99999 and a lower rank is refused while
    // one holds; its look at the player in passing is rank 1 or 2. Ours sits between.)
    int lookRank = 100;
    // The speaker's and the spoken-to's heads reach further than the game's glance: each link of their look-at chain gets
    // this share of what its strength leaves (worldLookReach; 0: the game's strengths for everyone).
    float lookReach = 0.4f;
    bool turnLog = false;              // worldTurnLog = 1: each looking or turning person's yaw twice a second
    bool walkGestures = true;          // worldWalkGestures = 0: a walker keeps the conversation's gesture mode (no walk cycle)
    bool walkLog = false;              // worldWalkLog = 1: 1 s into an AI walk, the walker's and its controller's properties
                                       // to %LOCALAPPDATA%\MEVR\walk-probe (research/69 "Walking to marks")
    // Where the voices come from (worldVoices = spatial | game; research/69 "The voices, placed"; cw::PlaceVoice): a speaker
    // within worldVoiceWithinM of the player (kept to worldVoiceKeepM) is heard from where their head is; one further away
    // is left to the game's centred mix (the Council across its chamber: an intercom). The audio engine only pans, so the
    // loudness is ours: full within worldVoiceNearM, worldVoiceFalloffDb quieter per doubling of the distance, at most
    // worldVoiceFloorDb; worldVoiceGain: times that.
    bool voices = true;
    float voiceWithinM = 8.0f;
    float voiceKeepM = 10.0f;
    float voiceNearM = 2.0f;
    float voiceFalloffDb = 3.0f;
    float voiceFloorDb = 6.0f;
    float voiceGain = 1.0f;
    // (60 and 25 until 2026-11-10 evening: the chain's own reach is about 84 degrees, the head taking all but the eyes' 25,
    // and people who stand side by side talk with their heads turned.)
    float turnComfortDeg = 80.0f;      // the look-at chain's business up to here (worldTurnComfortDeg)
    float turnSettleDeg = 40.0f;       // a body turn ends with the target this far off (worldTurnSettleDeg)
    // By their part (worldTurn = roles) a body re-faces only for the player (the one person the scene could not compose),
    // while he stands within worldTurnWithinM, and goes back to the facing the scene composed worldTurnHomeS after the
    // last line that engaged it with him (negative: it stays turned).
    float turnWithinM = 4.0f;
    float turnHomeS = 3.0f;
    float turnDwellS = 1.0f;           // ... and begins after the target was outside for this long (worldTurnDwellS)
    // How a body re-faces (worldTurnWay = desired | game | slide): desired: the pawn's own desired rotation, which its physics
    // turns it to at its own rate; game: BioPawn.RequestTurnInPlace; slide: an eased turn of ours. The first two fall back to
    // the eased turn where nothing comes of them.
    int turnWay = 2;                   // 0 desired, 1 game, 2 slide (the default: the other two turned nobody, 2026-11-10)
    bool setPiecesOnScreen = true;     // the scan's set pieces on the 3D screen, not in the world (worldSetPieces = screen | world)
    // The game's own camera at the player's place too, not only the main view (worldCamera). Off since 2026-11-10 (research/69
    // "The first shot"): the main view's build already feeds culling, streaming and the listener; what this was built for
    // (2026-11-06: assets missing in far shots) was the director's near plane, and with it on Umbra's eye-matrix lever culled
    // the player's own body.
    bool camera = false;
    bool openingAccept = true;         // the stage's opening placements are accepted, the player put at Shepard's mark (worldOpening = accept|keep)
    // worldOpening = auto | accept | keep (research/69): a cut only for another composition. The default since the sampled pass
    // of 2026-11-10 (120 scenes: 30 stay, 29 walk in, 11 cut; with accept every staged opening dipped from black).
    cw::OpeningRule opening = cw::OpeningRule::Auto;
    bool arrange = true;               // everyone of the conversation (and the squad) brought into the player's view (worldArrange)
    bool faceMiddle = true;            // ... the seated facing the group's middle, not the player (worldFaceMiddle; item 7)
    bool squadBeside = true;           // ... the squad seated beside the player rather than on the arc (worldSquadBeside)
    double squadSideM = 1.0;           // ... that far to each side (worldSquadSideM)
    double squadBackM = 0.3;           // ... and that far behind the player's place (worldSquadBackM)
    double seatWalkMaxM = 8.0;         // an opening without a cut: people out of view within this walk into seats (worldSeatWalkMaxM)
    double walkLongM = 1.5;            // a mark at least this far is walked whatever its direction (worldWalkLongM)
    double walkStepDeg = 60.0;         // ... a nearer one only within this of the person's facing (worldWalkStepDeg)
    float arcM = 2.5f;                 // the seats' distance from the player (worldArcM)
    float viewDeg = 60.0f;             // half the view's angle a person must be within (worldViewDeg)
    bool move = true;                  // the left stick walks Shepard during the conversation while the wheel is down (worldMove)
    // worldMoments = walk | carry | log: a level-script matinee that walks Shepard is held in first person (research/69
    // "Authored scenes"). walk is the default since 2026-11-10: the track's place for him is known even while its move is
    // withheld, so he always ends where the script has him (walked, or carried after the wait). log only measures.
    bool moments = true;
    bool momentCarry = false;          // ... carry: the track moves him and the view goes with him; walk: its move is withheld,
                                       // he walks it himself
    // view (the default since 2026-11-10, later): the script's Shepard is the script's: the track moves his pawn, hidden,
    // through every trigger and onto every mark as authored, and only the player's view is the player's: it stays where he
    // stands, walks by the stick (guided after worldWalkInS) and joins the pawn where it comes to rest. With the move
    // withheld (walk) the ceremony never began for a player who had not walked far enough: the track carries Shepard
    // through the trigger that streams the council's chunk in, and the event that starts the ceremony is fired once.
    bool momentView = true;
    // While the view is away from Shepard's pawn (a scripted walk, a walk-in by the view) his body is drawn where the player
    // stands, not hidden (worldBody = player | hidden): the body follower slides his bones there (BodyAway). The owner,
    // 2026-11-10: no body for a whole walk "will be very immersion breaking".
    bool bodyWithPlayer = true;
    // The on-foot near plane kept while the world view is held (worldNearPlane = 1; 0: the director's, which clips what is near).
    bool nearPlane = true;
    // The script's black screen during a held moment becomes a dip from black only when what it covered is within this
    // far of the player (worldDipWithinM, metres; negative: any change in view, the rule until 2026-11-10). People shown at
    // the far end of a hall appear without a blink.
    float dipWithinM = 8.0f;
    // While the view is away from the pawn the legs stand under the player by leg IK when the view rests, and keep the
    // script's walk animation while it moves (worldLegs = stand; walk: the animation always). The owner, 2026-11-10: "modify
    // the legs to see if we can just use the VRIK".
    bool legsStand = true;
    // `convo: body:` once a second in a scene, every two on foot: the pawn's and his mesh's hidden flags, when they were last
    // rendered and where they are against the view (worldBodyLog = 1; `mevr convo body log on`). A diagnostic: off by default.
    bool bodyLog = false;
    float momentPatienceS = 12.0f;     // walk: how long the script waits for him where it wants him before he is carried there
    float momentCarryMS = 3.0f;        // the speed he is carried at to where the script wants him (worldMomentCarryMS)
    float moveLeashM = 3.0f;           // how far from the conversation's owner the walk may go (worldMoveLeashM)
    float moveSpeedMS = 1.4f;          // the walk's speed at full deflection (worldMoveSpeedMS)
    float walkInS = 5.0f;              // a walk-in nobody walks: guided to Shepard's mark after this long without the stick
                                       // (worldWalkInS; 0 at once, -1 never; cw::StepGuide)
    cw::WalkInRule walkInWay = cw::WalkInRule::Auto;  // worldWalkInWay = auto | body | view: who walks a walk-in (cw::DecideWalkIn)
    float moveClearM = 0.7f;           // how close the walk may come to a person of the conversation or the squad (worldMoveClearM)
    float subtitleCrop[4] = {};
    float wheelCrop[4] = {};
};
Settings gSettings;
std::uint64_t gSettingsMs = 0;
std::string gSettingsText;
std::optional<cw::Mode> gModeOverride;  // `mevr convo world on|off|screen|vr` (tests); not saved

void ReadCrop(ini::File const& file, char const* key, float (&out)[4]) {
    std::fill(std::begin(out), std::end(out), 0.0f);
    if (auto const text = ini::Get(file, "Cinematics", key)) {
        std::istringstream words(*text);
        std::string word;
        for (int i = 0; i < 4 && words >> word; ++i) {
            if (auto const n = ini::Number(word)) {
                out[i] = static_cast<float>(std::clamp(*n, 0.0, 1.0));
            }
        }
    }
}

void RefreshSettings() {
    std::uint64_t const now = GetTickCount64();
    if (gSettingsMs != 0 && now - gSettingsMs < 1000) {
        return;
    }
    gSettingsMs = now;
    ini::Loaded const loaded = ini::Read(engine::SettingsFile());
    Settings s;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "conversations")) {
        s.mode = cw::ParseMode(text->c_str(), cw::Mode::Screen);
    }
    auto number = [&](char const* key, float fallback, float lo, float hi) {
        auto const v = ini::GetNumber(loaded.file, "Cinematics", key);
        return v ? static_cast<float>(std::clamp(*v, static_cast<double>(lo), static_cast<double>(hi))) : fallback;
    };
    s.follow = number("worldFollow", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.subtitleHeightDeg = number("subtitleHeightDeg", s.subtitleHeightDeg, 0.5f, 20.0f);
    s.subtitleBelowM = number("subtitleBelowM", s.subtitleBelowM, 0.0f, 1.0f);
    s.subtitleTowardM = number("subtitleTowardM", s.subtitleTowardM, 0.0f, 1.0f);
    if (auto const text = ini::Get(loaded.file, "Cinematics", "wheelHand")) {
        std::string const w = ini::Lower(ini::Trim(*text));
        s.wheelHand = w == "right" || w == "1" ? 1 : w == "left" || w == "0" ? 0 : 2;
    }
    s.wheelWidthM = number("wheelWidthM", s.wheelWidthM, 0.15f, 1.5f);
    s.wheelDistanceM = number("wheelDistanceM", s.wheelDistanceM, 0.05f, 0.8f);
    s.stay = number("worldStay", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.walk = static_cast<int>(number("worldWalk", 1.0f, 0.0f, 3.0f));
    s.walkSettleS = number("worldWalkSettleS", s.walkSettleS, 0.0f, 60.0f);
    s.walkMinM = number("worldWalkMinM", s.walkMinM, 0.0f, 6.0f);
    s.walkSpeedMS = number("worldWalkSpeedMS", s.walkSpeedMS, 0.1f, 3.0f);
    s.turnDegPerS = number("worldTurnDegPerS", s.turnDegPerS, 10.0f, 1000.0f);
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldTurn")) {
        s.turnRule = cw::ParseTurnRule(*text, cw::TurnRule::Roles);
    }
    s.turnLog = number("worldTurnLog", 0.0f, 0.0f, 1.0f) != 0.0f;
    s.walkLog = number("worldWalkLog", 0.0f, 0.0f, 1.0f) != 0.0f;
    s.walkGestures = number("worldWalkGestures", 1.0f, 0.0f, 1.0f) != 0.0f;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldVoices")) {
        std::string const v = ini::Lower(ini::Trim(*text));
        s.voices = !(v == "game" || v == "off" || v == "0" || v == "centre" || v == "center");
    }
    s.voiceWithinM = number("worldVoiceWithinM", s.voiceWithinM, 0.0f, 1000.0f);
    s.voiceKeepM = number("worldVoiceKeepM", s.voiceKeepM, 0.0f, 1000.0f);
    s.voiceNearM = number("worldVoiceNearM", s.voiceNearM, 0.1f, 100.0f);
    s.voiceFalloffDb = number("worldVoiceFalloffDb", s.voiceFalloffDb, 0.0f, 12.0f);
    s.voiceFloorDb = number("worldVoiceFloorDb", s.voiceFloorDb, 0.0f, 40.0f);
    s.voiceGain = number("worldVoiceGain", s.voiceGain, 0.0f, 4.0f);
    s.turnWithinM = number("worldTurnWithinM", s.turnWithinM, 0.0f, 100.0f);
    s.turnHomeS = number("worldTurnHomeS", s.turnHomeS, -1.0f, 600.0f);
    s.glanceDeg = number("worldGlanceDeg", s.glanceDeg, 0.0f, 180.0f);
    s.glanceKeepDeg = number("worldGlanceKeepDeg", s.glanceKeepDeg, 0.0f, 180.0f);
    s.lookRank = static_cast<int>(number("worldLookRank", static_cast<float>(s.lookRank), 0.0f, 99999.0f));
    s.lookReach = number("worldLookReach", s.lookReach, 0.0f, 1.0f);
    s.turnComfortDeg = number("worldTurnComfortDeg", s.turnComfortDeg, 5.0f, 180.0f);
    s.turnSettleDeg = number("worldTurnSettleDeg", s.turnSettleDeg, 0.0f, 90.0f);
    s.turnDwellS = number("worldTurnDwellS", s.turnDwellS, 0.0f, 30.0f);
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldTurnWay")) {
        std::string const way = ini::Lower(ini::Trim(*text));
        s.turnWay = way == "desired" ? 0 : way == "game" ? 1 : 2;
    }
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldSetPieces")) {
        s.setPiecesOnScreen = ini::Lower(ini::Trim(*text)) != "world";
    }
    s.camera = number("worldCamera", 0.0f, 0.0f, 1.0f) != 0.0f;
    s.nearPlane = number("worldNearPlane", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.dipWithinM = number("worldDipWithinM", 8.0f, -1.0f, 1000.0f);
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldLegs")) {
        s.legsStand = ini::Lower(ini::Trim(*text)) != "walk";
    }
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldOpening")) {
        s.opening = cw::ParseOpeningRule(*text, cw::OpeningRule::Auto);
        s.openingAccept = s.opening != cw::OpeningRule::Keep;  // accept and auto let the stage's first placements stand, then decide
    }
    s.arrange = number("worldArrange", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.faceMiddle = number("worldFaceMiddle", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.squadBeside = number("worldSquadBeside", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.squadSideM = number("worldSquadSideM", 1.0f, 0.5f, 3.0f);
    s.squadBackM = number("worldSquadBackM", 0.3f, -1.0f, 2.0f);
    s.seatWalkMaxM = number("worldSeatWalkMaxM", 8.0f, 0.0f, 30.0f);
    s.walkLongM = number("worldWalkLongM", 1.5f, 0.0f, 10.0f);
    s.walkStepDeg = number("worldWalkStepDeg", 60.0f, 0.0f, 180.0f);
    s.arcM = number("worldArcM", s.arcM, 1.2f, 6.0f);
    s.viewDeg = number("worldViewDeg", s.viewDeg, 20.0f, 90.0f);
    s.move = number("worldMove", 1.0f, 0.0f, 1.0f) != 0.0f;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldMoments")) {
        std::string const w = ini::Lower(ini::Trim(*text));
        s.moments = w == "view" || w == "walk" || w == "carry";
        s.momentCarry = w == "carry";
        s.momentView = w == "view";
    }
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldBody")) {
        s.bodyWithPlayer = ini::Lower(ini::Trim(*text)) != "hidden";
    }
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldBodyLog")) {
        s.bodyLog = ini::Number(*text).value_or(0.0) != 0.0;
    }
    s.momentPatienceS = number("worldMomentPatienceS", 12.0f, 0.0f, 120.0f);
    s.momentCarryMS = number("worldMomentCarryMS", 3.0f, 0.5f, 10.0f);
    s.moveLeashM = number("worldMoveLeashM", s.moveLeashM, 0.0f, 20.0f);
    s.moveSpeedMS = number("worldMoveSpeedMS", s.moveSpeedMS, 0.2f, 4.0f);
    s.walkInS = number("worldWalkInS", s.walkInS, -1.0f, 120.0f);
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldWalkInWay")) {
        s.walkInWay = cw::ParseWalkInRule(*text, cw::WalkInRule::Auto);
    }
    s.moveClearM = number("worldMoveClearM", s.moveClearM, 0.0f, 3.0f);
    s.staySource = number("worldStaySource", 1.0f, 0.0f, 1.0f) != 0.0f;
    s.absentM = number("worldAbsentM", s.absentM, 0.0f, 200.0f);
    s.moveTriggers = number("worldMoveTriggers", 1.0f, 0.0f, 1.0f) != 0.0f;
    if (auto const text = ini::Get(loaded.file, "Cinematics", "worldAutoScreen")) {
        std::string const v = ini::Lower(ini::Trim(*text));
        s.autoScreen = v == "off" || v == "0" ? 0 : v == "all" || v == "3" ? 3 : v == "log" || v == "1" ? 1 : 2;
    }
    ReadCrop(loaded.file, "subtitleCrop", s.subtitleCrop);
    ReadCrop(loaded.file, "wheelCrop", s.wheelCrop);
    std::string const text = std::format("conversations = {}, worldFollow = {}, subtitles {:.1f} deg tall {:.2f} m under the face, the wheel "
                                         "{} {:.2f} m wide, set pieces {}, the game's camera {}, worldWalk = {} (settle {:.2f} s, "
                                         "min {:.2f} m, speed {:.2f} m/s), the opening {}, arrange {} ({:.1f} m, {:.0f} deg), the player's walk {}, "
                                         "placements {}, Shepard absent beyond {:.0f} m, worldAutoScreen = {}",
        cw::Name(s.mode), s.follow ? 1 : 0,
        s.subtitleHeightDeg, s.subtitleBelowM, s.wheelHand == 0 ? "at the left hand" : s.wheelHand == 1 ? "at the right hand" : "in front",
        s.wheelWidthM, s.setPiecesOnScreen ? "on the 3D screen" : "in the world", s.camera ? "at the player's place" : "the director's",
        s.walk, s.walkSettleS, s.walkMinM, s.walkSpeedMS, s.openingAccept ? "accepted" : "kept", s.arrange ? "on" : "off", s.arcM, s.viewDeg,
        s.move ? std::format("on (leash {:.1f} m, {:.2f} m/s, {:.2f} m clear of people, triggers {})", s.moveLeashM, s.moveSpeedMS, s.moveClearM,
                     s.moveTriggers ? "kept" : "free") : std::string("off"),
        s.staySource ? "withheld at the source" : "undone after the fact", s.absentM,
        s.autoScreen == 0 ? "off" : s.autoScreen == 3 ? "all" : s.autoScreen == 2 ? "on" : "log");
    if (text != gSettingsText) {
        gSettingsText = text;
        log::Info("convo: [Cinematics] {}", text);
    }
    gSettings = s;
}

cw::Mode CurrentMode() {
    return gModeOverride.value_or(gSettings.mode);
}

// ---- Geometry helpers --------------------------------------------------------------------------------------------------------
ik::Vec3 V(ue3::Vector const& v) {
    return {v.x, v.y, v.z};
}
double Dist(ik::Vec3 a, ik::Vec3 b) {
    return ik::Length(a - b);
}
ik::Vec3 Forward(ue3::Rotator const& r) {
    double const pitch = r.pitch * kRotToDeg / kRadToDeg;
    double const yaw = r.yaw * kRotToDeg / kRadToDeg;
    return {std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch)};
}
double YawDeg(ue3::Rotator const& r) {
    double y = std::fmod(r.yaw * kRotToDeg, 360.0);
    return y < 0.0 ? y + 360.0 : y;
}
double AngleDeg(ik::Vec3 a, ik::Vec3 b) {
    double const la = ik::Length(a);
    double const lb = ik::Length(b);
    if (!(la > 1e-6) || !(lb > 1e-6)) {
        return 0.0;
    }
    return std::acos(std::clamp(ik::Dot(a, b) / (la * lb), -1.0, 1.0)) * kRadToDeg;
}
double YawDiffDeg(double a, double b) {
    double d = std::fmod(a - b + 540.0, 360.0) - 180.0;
    return d;
}
std::string F(ik::Vec3 const& v) {
    return std::format("({:.0f}, {:.0f}, {:.0f})", v.x, v.y, v.z);
}

std::string ShortName(ue3::Object const* o) {
    if (o == nullptr) {
        return "none";
    }
    ue3::Reflection& r = ue3::runtime::Get();
    return r.NameText(o);
}

// The head bone of a pawn's mesh: its world place and axes (cached per skeletal mesh asset).
struct Head {
    ik::Vec3 at;
    ik::Vec3 axis[3];
};
std::map<ue3::Object const*, int> gHeadBones;  // skeletal mesh asset -> its head bone (-1: none)

std::optional<Head> HeadOf(ue3::Object* actor) {
    if (actor == nullptr || !player::IsBioPawn(actor) || !ue3::runtime::IsAlive(actor)) {
        return std::nullopt;
    }
    ue3::Object* const mesh = player::Mesh(actor);
    ue3::Object* const asset = player::SkeletalMesh(mesh);
    if (asset == nullptr) {
        return std::nullopt;
    }
    auto it = gHeadBones.find(asset);
    if (it == gHeadBones.end()) {
        int index = -1;
        if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
            for (std::size_t i = 0; i < skeleton->names.size() && index < 0; ++i) {
                if (ini::Lower(skeleton->names[i]) == "head") {
                    index = static_cast<int>(i);
                }
            }
        }
        it = gHeadBones.emplace(asset, index).first;
    }
    int const bone = it->second;
    auto const bases = engine::skeleton::SpaceBases(mesh);
    if (bone < 0 || static_cast<std::size_t>(bone) >= bases.size()) {
        return std::nullopt;
    }
    ik::Affine const toWorld = engine::skeleton::LocalToWorld(mesh);
    ik::Transform const t = engine::skeleton::ToTransform(bases[static_cast<std::size_t>(bone)]);
    Head h;
    h.at = ik::TransformPoint(toWorld, t.translation);
    ik::Vec3 const units[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int a = 0; a < 3; ++a) {
        h.axis[a] = ik::Normalize(ik::TransformDirection(toWorld, ik::Rotate(t.rotation, units[a])), units[a]);
    }
    return h;
}

// An actor's place for the logs: its head if it has one, else its location.
std::optional<ik::Vec3> FacePoint(ue3::Object* actor) {
    if (auto const head = HeadOf(actor)) {
        return head->at;
    }
    if (auto const at = actor != nullptr ? player::Location(actor) : std::nullopt) {
        return V(*at);
    }
    return std::nullopt;
}

bool LineOfSight(ue3::Object* from, ik::Vec3 start, ik::Vec3 end) {
    if (from == nullptr || gFastTrace.Get() == nullptr) {
        return true;
    }
    ue3::Params trace(ue3::runtime::Get(), gFastTrace.Get());
    trace.Set("TraceEnd", ue3::Vector{static_cast<float>(end.x), static_cast<float>(end.y), static_cast<float>(end.z)});
    trace.Set("TraceStart", ue3::Vector{static_cast<float>(start.x), static_cast<float>(start.y), static_cast<float>(start.z)});
    ue3::runtime::Call(from, gFastTrace.Get(), trace);
    return trace.ReturnBool().value_or(true);
}

// "The same space" for the opening and a moment's teleport: the place is in sight if one of three lines is clear (to it, to a
// point 80 units above it, and that one from 60 units above the eyes). One line alone grazed the stairs' edge in the council
// chamber and said "out of sight" in one run of four (2026-11-10: the hearing opened with a cut from the same place it had
// opened as a walk-in three times); a wall between two rooms stops all three.
bool InSight(ue3::Object* from, ik::Vec3 const& eyes, ik::Vec3 const& target) {
    ik::Vec3 const above = target + ik::Vec3{0.0, 0.0, 80.0};
    return LineOfSight(from, eyes, target) || LineOfSight(from, eyes, above) || LineOfSight(from, eyes + ik::Vec3{0.0, 0.0, 60.0}, above);
}

// ---- The on-foot view and the POV (Engine.Camera.UpdateCamera, after the game's) ----------------------------------------------
struct Foot {
    bool have = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    ue3::Vector pawn{};                              // Shepard's pawn at that camera update
    std::optional<engine::TrackingFrame> frame;      // the room as that view maps it
    vr::host_view::State floorView;
    std::uint64_t ms = 0;                            // when
    // The first-person eye from Shepard's head bone, in his yaw frame (forward, right, up; Unreal units): arm IK's log of the first
    // person camera against the head bone with the headset at the recenter point (2026-11-06: forward 8.9, right 1.7, up 29.3). A
    // measure on foot picks up body follow's lean (0.59 m from the head bone, 2026-11-06), so the constant.
    ik::Vec3 eyeFromHead{8.9, 1.7, 29.3};
};
Foot gFoot;

ik::Vec3 YawForward(double yawDeg) {
    double const y = yawDeg / kRadToDeg;
    return {std::cos(y), std::sin(y), 0.0};
}
ik::Vec3 YawRight(double yawDeg) {
    double const y = yawDeg / kRadToDeg;
    return {-std::sin(y), std::cos(y), 0.0};
}
struct PovAt {
    float* location = nullptr;
    ue3::Rotator* rotation = nullptr;
    float* fov = nullptr;
    std::uint64_t ms = 0;
    std::uint64_t pointerMs = 0;  // the addresses above are fresh (taken on foot too: a moment's view begins from there)
    ue3::Vector lastLocation{};  // the director's camera as the game left it (the logs' shots)
    ue3::Rotator lastRotation{};
    float lastFov = 0.0f;
};
PovAt gPov;

// A matinee's camera on foot: the game's camera looking from a camera actor in game mode 0 (set by the camera post-handler).
struct DirectedCamera {
    bool on = false;
    ue3::Object* target = nullptr;  // (compared only, never used)
    std::uint64_t ms = 0;           // the last camera update with it
};
DirectedCamera gDirected;

void KeepInPlace(ue3::Object* pawn);
bool KeepsTheLiveConversation(ue3::Object* controller);
ue3::Object* ConversationNow(ue3::Object* controller);
bool MomentHeld();  // a moment's view is up (provisional too)

// The game's own camera at the player's place (worldCamera; the owner's QA, 2026-11-06): the host's swap held the main view's build
// only, so the engine's culling and texture streaming still followed the director's camera (in the comm room 5-9 m away behind
// Nihlus with a 10-degree lens: assets missing or blurred from where the player stands). Written into the camera's POV right after
// the game computes it each tick (the director's own POV is read first, for the measurements); set by the tick while the world
// view is up.
struct CameraHold {
    bool on = false;
    ue3::Vector location{};
    ue3::Rotator rotation{};
    float fov = 90.0f;
    std::uint64_t writes = 0;
};
CameraHold gCameraHold;
// The near plane (research/69 "The first shot", 2026-11-10): a conversation's camera sets LocalPlayer.NearClipPlane per shot
// (BioCameraBehaviorConversation.m_fNearPlane: 279.5 units in Avina's first shot, 177.5 on Shepard's line, 67.5 after, against
// 10 on foot), which suits a lens metres away and, from the player's own eyes, cuts away everything within reach: the floor
// under the player, the player's own body, "the assets near me" (the owner, 2026-11-10). While the world view is held the
// on-foot plane is written back after the camera's update, before the draw.
bool gNearHold = false;
float gFootNear = 10.0f;      // the plane on foot (read there each camera update)
float gNearReplaced = 0.0f;   // the director's plane last replaced (logged when it changes)
std::uint64_t gNearWrites = 0;
std::uint64_t gArmedSince = 0;  // when the mod last told the host it was armed on foot (set by Arm; 0: not armed)

void OnUpdateCamera(ue3::Object* camera, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    ue3::Object* const controller = player::Controller();
    if (camera == nullptr || controller == nullptr || player::CameraOwner(camera) != controller) {
        return;
    }
    auto const pov = player::CameraPov(camera);
    if (!pov || pov->location == nullptr || pov->rotation == nullptr || pov->fov == nullptr) {
        return;
    }
    auto const mode = player::GameMode(controller);
    if (mode && *mode == 0) {
        // A matinee's own camera on foot (its director track sets the view target to a camera actor without the cinematic mode:
        // Chellik's bump on the Citadel, 2026-11-11): no view of the player's. Taken for one until then, it was what the
        // conversation after it opened from: the player's eyes 1.6 m behind Shepard for the whole scene. A moment holds the view
        // (TickMoment); its POV's addresses serve the render-time hold, the on-foot record is the moment's.
        ue3::Object* const target = gCameraViewTargetActor.Read<ue3::Object*>(gCameraViewTarget.Address(camera)).value_or(nullptr);
        bool const directed = target != nullptr && target != controller && target != player::Pawn(controller);
        if (directed != gDirected.on || (directed && target != gDirected.target)) {
            log::Info("moment: on foot the game's camera {}", directed ? "is a matinee's: " + ue3::runtime::FullName(target) + " (no view of the player's)"
                                                                       : std::string("is the player's again"));
        }
        gDirected.on = directed;
        gDirected.target = directed ? target : nullptr;
        if (directed) {
            gDirected.ms = GetTickCount64();
        }
        if (directed || MomentHeld()) {
            gPov.location = reinterpret_cast<float*>(pov->location);
            gPov.rotation = pov->rotation;
            gPov.fov = pov->fov;
            gPov.pointerMs = GetTickCount64();
            return;
        }
        gFoot.location = *pov->location;
        gFoot.rotation = *pov->rotation;
        // Store the corrected base, not the raw animated camera. Common framing is still applied by the held render.
        auto const view = vr::host_view::Latest();
        auto const basis = engine::ToBasis(engine::UnrealRotation{gFoot.rotation.pitch, gFoot.rotation.yaw, gFoot.rotation.roll});
        gFoot.location.z = static_cast<float>(vr::host_view::HoldBaseZ(view, pov->location->z,
            basis.right.z, basis.up.z, basis.forward.z));
        gFoot.fov = *pov->fov;
        if (auto const p = player::Location(player::Pawn(controller))) {
            gFoot.pawn = *p;
        }
        gFoot.have = true;
        gFoot.ms = GetTickCount64();
        if (ue3::Object* const local = gBodyLocalPlayer.Read<ue3::Object*>(controller).value_or(nullptr)) {
            if (auto const plane = gBodyNearClip.Read<float>(local); plane && std::isfinite(*plane) && *plane > 0.0f && *plane < 50.0f) {
                gFootNear = *plane;
            }
        }
        gPov.location = reinterpret_cast<float*>(pov->location);
        gPov.rotation = pov->rotation;
        gPov.fov = pov->fov;
        gPov.pointerMs = gFoot.ms;
        return;
    }
    // (The cinematic mode too: a moment's first-person view is held there. Until 2026-11-10 the POV was taken in the
    // conversation mode only, so a moment published no view to hold after its first half second and the eyes showed the
    // game's own camera, Shepard from behind: seen in the first snapshots taken of a moment; the logs had said "held".)
    if (mode && (*mode == kModeConversation || *mode == 6)) {
        gPov.location = reinterpret_cast<float*>(pov->location);
        gPov.rotation = pov->rotation;
        gPov.fov = pov->fov;
        gPov.ms = GetTickCount64();
        gPov.pointerMs = gPov.ms;
        gPov.lastLocation = *pov->location;
        gPov.lastRotation = *pov->rotation;
        gPov.lastFov = *pov->fov;
        // Only while the game still plays the conversation we keep people for: when one conversation hands over to the next
        // (the comm room to Anderson's, research/69 item 9), the new stage places Shepard on its mark in the same tick, and
        // the last one's keep-in-place undid it (4.18 m) just before the new opening took the player's place from his head
        // bone, which was still at the mark: the view at one place, his body 4 m off. The mod's tick begins the new one.
        if (*mode == kModeConversation && KeepsTheLiveConversation(controller)) {
            KeepInPlace(player::Pawn(controller));  // after the conversation's placements of this tick, before its draw
        }
        if (!gCameraHold.on && gArmedSince != 0 && gPov.ms - gArmedSince < 150 && gFoot.have) {
            // The mode's first tick, the mod armed on foot a moment ago and its own tick not yet here: the frame drawn now
            // is the player's view on foot still, not the director's first shot (the host keeps the on-foot presentation).
            *pov->location = gFoot.location;
            *pov->rotation = gFoot.rotation;
            *pov->fov = gFoot.fov;
            write_journal::Wrote("ConversationWorld.cameraPovArmed", pov->location, sizeof(ue3::Vector));
        }
        if (gCameraHold.on) {
            *pov->location = gCameraHold.location;
            *pov->rotation = gCameraHold.rotation;
            *pov->fov = gCameraHold.fov;
            write_journal::Wrote("ConversationWorld.cameraPov", pov->location, sizeof(ue3::Vector));
            if (gCameraHold.writes++ == 0) {
                log::Info("convo: the game's camera held at the player's place too (culling, streaming, sound follow it)");
            }
        }
        if (gNearHold) {
            ue3::Object* const local = gBodyLocalPlayer.Read<ue3::Object*>(controller).value_or(nullptr);
            float* const plane = local != nullptr ? static_cast<float*>(gBodyNearClip.Address(local)) : nullptr;
            if (plane != nullptr && std::isfinite(*plane) && *plane != gFootNear) {
                if (*plane != gNearReplaced) {
                    log::Info("convo: the director's near plane is {:.1f} units ({:.2f} m) in this shot: from the player's eyes it would cut "
                              "away everything nearer; the on-foot plane ({:.1f}) is kept instead ({} writes so far)",
                        *plane, *plane / kUnitsPerMeter, gFootNear, gNearWrites);
                    gNearReplaced = *plane;
                }
                *plane = gFootNear;
                write_journal::Wrote("ConversationWorld.nearClipPlane", plane, sizeof(float));
                ++gNearWrites;
            }
        }
    }
}

// ---- One conversation, measured ------------------------------------------------------------------------------------------------
struct Tracked {
    ue3::Object* actor = nullptr;
    std::string name;
    ik::Vec3 start;
    double startYaw = 0.0;
    ik::Vec3 logged;    // the place last logged
    double loggedYaw = 0.0;
    double maxMove = 0.0;
    double maxTurn = 0.0;
    std::optional<ik::Vec3> headStart;  // its head bone at the start
    double headMax = 0.0;               // the head bone's largest move from there
};

struct Convo {
    bool on = false;
    ue3::Object* object = nullptr;
    ue3::Object* playerPawn = nullptr;  // retain Shepard's identity even if possession temporarily changes
    std::string name;
    std::uint64_t startMs = 0;
    std::uint32_t number = 0;
    bool world = false;          // shown in the world
    bool setPiece = false;       // one of the scan's set pieces: on the 3D screen instead (worldSetPieces)
    // The opening (research/59 "Part A"): the stage's first placements are the scene's design and the player hasn't seen the room
    // in this conversation yet, so they are accepted until the first drawn frame of the world view (or openingUntilMs), then the
    // player's place is taken from Shepard's mark and everyone is brought into view (Part B), under a dip from black (`cut`).
    std::uint64_t openingUntilMs = 0;
    bool openingDone = false;
    int openingMoves = 0;        // placements accepted during the opening
    cw::Opening opening = cw::Opening::Cut;  // how the opening ended (the summary's "opening")
    int trackWalks = 0;          // people moved by a matinee's track (authored walks), left to it
    int trackHeld = 0;           // a track's moves of the player's own pawn withheld during this conversation
    int yielded = 0;             // small placements the game repeated tick after tick, accepted (cw::Insists)
    cw::StickLatch wheelStick;   // the left stick is the wheel's until it has been let go after a pick (cw::StickWalks)
    bool wheelStickNoted = false;
    int wheelSticks = 0;         // replies picked with the stick still out as the wheel closed: no walk came of them
    int bodyTurns = 0;           // bodies that re-faced: their look-at target stayed outside what the look-at chain covers
    int reachSamples = 0;        // look-at targets 30 degrees or more off a body's facing, measured after 1.5 s,
    double reachWorstHead = 0.0; // ... the worst angle a head was still off its target then,
    double reachMaxBody = 0.0;   // ... and the widest angle a target stood off a body
    int gameTurns = 0;           // ... of those, turned by the game's own turn in place
    int turnsSlid = 0;           // ... and by an eased turn of ours (the game's refused, or did not move the body)
    int facingsHeld = 0;         // facings the stage set at once mid-conversation, put back (they were for its mark)
    int facingsEased = 0;        // ... and those of people with nobody to look at, taken as a turn instead of at once
    int voicesPlaced = 0;        // people whose voice was heard from where they stand (cw::PlaceVoice) ...
    int voicesFar = 0;           // ... and people too far for it: the game's centred mix (the intercom)
    int reaches = 0;             // look-at chains of the pair given the further reach (worldLookReach)
    int looksRefused = 0;        // looks of ours the game refused (a look-at of its own held the pawn's at a higher rank)
    int turnsHome = 0;           // bodies that went back to the facing the scene composed after their lines with the player
    int looksSet = 0;            // look-at targets given by a person's part in the line (the pair's, and bystanders' glances)
    int glances = 0;             // ... of those, bystanders' glances at the speaker
    int heardOnly = 0;           // bystanders who heard a speaker outside a glance's reach and kept looking where they looked
    int looksGame = 0;           // look-at targets the game itself gave (an authored look-at): left to it
    double markM = 0.0;          // how far Shepard's mark was from where the player stood
    std::optional<ik::Vec3> wantedMark;  // a walk-in: Shepard's mark, where the player is wanted (the walk's leash is around it)
    std::uint64_t settleUntilMs = 0;     // after an opening without a cut: his mesh catches up with the pawn; no jump is read into it
    int arranged = 0;            // people moved to a seat in view
    // An opening without a cut (item 7): people of the conversation out of view walk into seats in view, by the game's AI.
    struct SeatWalk {
        ue3::Object* actor = nullptr;
        ik::Vec3 seat;
        double yawDeg = 0.0;
    };
    std::vector<SeatWalk> seatWalks;  // planned at the opening, handed to each person's walk once its state exists
    int seatWalked = 0;
    std::uint32_t cut = 0;       // the host dips from black when it changes
    // The player's place for this conversation: the view on foot if it was Shepard's just now, else his eyes from his head bone
    // (a conversation right after a level load or a cutscene); it follows his head bone when that jumps (a stage's placement).
    ik::Vec3 hold;
    ue3::Rotator holdRotation{};
    float holdFov = 90.0f;
    char const* holdFrom = "";
    std::optional<ik::Vec3> anchorHead;  // Shepard's head bone where the hold was last placed
    vr::conversation_world::FollowState follow;  // the hold and its anchor, for FollowHeadSettled
    int reanchors = 0;
    int lines = 0;      // lines spoken (the subtitle field changed to a new text)
    std::map<std::string, int> speakers;  // who spoke, how often
    bool stage = false;
    int kept = 0;       // jumps undone (people kept where they stood)
    int walks = 0;      // walks started (including AI calls that fell back to slide)
    int letGo = 0;      // jumps too long to undo (a scripted entrance)
    std::vector<Tracked> actors;
    ue3::Object* speaker = nullptr;
    std::uint32_t speakerSerial = 0;
    std::string subtitle;
    std::string lookAts;
    int entry = -1;
    std::vector<int> entryPath;        // the entries it passed through, in order (the summary's "entries": the sweep's judge)
    // Shots: each cut of the director's camera.
    int shots = 0;
    ue3::Vector shotLocation{};
    ue3::Rotator shotRotation{};
    bool haveShot = false;
    std::map<std::string, int> shotKinds;
    int shotsHidden = 0;   // shots whose subject can't be seen from Shepard's eyes
    int shotsFar = 0;      // shots framed from far off (the camera past kFarCameraM from Shepard's eyes): a set piece's framing
    double cameraFarM = 0; // the farthest any shot's camera stood from Shepard's eyes
    int wheelOpenings = 0;
    bool wheel = false;
    double eyeToViewMax = 0.0;  // Shepard's head bone from the held view's eyes (the player's), largest
    // The player's walk (research/59 "Walking during conversations"): the left stick moves Shepard's pawn and the held view
    // together, inside a leash around the owner, while the wheel is down.
    double movedUnits = 0.0;    // walked, summed
    int moveSteps = 0;          // ticks that moved
    int leashHits = 0;          // ticks the leash held the step back
    int clearHits = 0;          // ticks a person in the way held the step (worldMoveClearM)
    int triggerHits = 0;        // steps refused because they would begin or end a trigger's touch (worldMoveTriggers)
    std::string triggerName;    // the last such trigger
    // Placements withheld in the game's own placement function (engine/ConversationHooks; research/69 item 4).
    int sourceKept = 0;
    double sourceFarUnits = 0.0;       // the farthest mark a person was not put on
    bool sourceLogged = false;
    std::string absentWhy;             // why this scene is no place for the player, found at run time (research/69 item 3)
    bool autoScreened = false;         // ... and it was moved to the 3D screen for it (worldAutoScreen = on)
    int moveRefused = 0;        // steps refused: no floor, a ledge, a wall, SetLocation
    bool moving = false;        // a velocity is set on the pawn
    cw::GuideState guide;       // a walk-in nobody walks: the guided walk to Shepard's mark
    // The view walks (cw::WalkInWay::View): Shepard stays at his mark, hidden, and `detachedAt` is where the player's feet
    // would be (the pawn's place under the held view, had it come along); the walk moves it and the view, never the pawn.
    bool detached = false;
    ik::Vec3 detachedAt{};
    bool hidPawn = false;       // the mod hid Shepard's pawn (shown again on arrival and at the end)
    bool noJoin = false;        // a moment's track is moving the pawn: the view does not join it until it rests
    bool arrived = false;       // the view reached Shepard and the two are one again
    char const* walkInWay = "-";
    bool guideProgressed = false;
    bool guideLogged = false;
    double guidedUnits = 0.0;   // walked by the guide, summed
    bool moveLogged = false;    // the first step logged
};
Convo gC;

// `mevr convo walk <x> <y> [seconds]` (tests without controllers, a flat game included): the stick the walk reads until the time
// is up; the head's facing is the held view's yaw when the host has no VR view.
struct WalkOverride {
    float x = 0.0f;
    float y = 0.0f;
    std::uint64_t untilMs = 0;
};
WalkOverride gWalkOverride;

// Where each person of the conversation is kept, and the yaw shown while they turn.
// A look-at chain given a further reach: each controller's definition pointer put on a copy of ours with a stronger
// strength, and what it pointed at before (put back when the person is no longer one of the pair, and at the end).
struct Reach {
    struct Link {
        ue3::Object* controller = nullptr;
        void* original = nullptr;
        std::unique_ptr<std::uint8_t[]> copy;
    };
    std::vector<Link> links;
    ue3::Object* data = nullptr;  // the pawn's look-at data these controllers belong to
};

struct Stay {
    ue3::Object* actor = nullptr;
    std::string name;
    ik::Vec3 place;
    double shownYaw = 0.0;
    int kept = 0;
    int marksAsked = 0;            // the stage's asks to move it onto another mark (withheld at the source), this conversation
    cw::MarkWalk walk;
    int method = 0;               // the method chosen for this walk (1 AI, 2 slide, 3 the engine's walking)
    std::optional<double> walkFacing;  // the facing the stage gave with the mark (applied at the ask): taken on arrival
    std::optional<bool> wasWalking;    // Pawn.bIsWalking before a walk of method 3, put back after it
    ik::Vec3 progressFrom;             // method 3: where it stood at the last progress check, and when
    double progressS = 0.0;
    int requestedMethod = 0;      // detect a live setting change even after falling back to slide
    ue3::Object* moveAction = nullptr;
    ue3::Object* walkBehavior = nullptr;    // method 1: the behaviour whose AI counters the walk raised, by how much,
    int allRaised = 0;
    int stackRaised = 0;
    ue3::Object* walkController = nullptr;  // ... and the controller whose movement question is answered while it walks
    int answersAtStart = 0;
    std::optional<ik::Vec3> aiRefused;      // a mark the game's AI couldn't walk it to: not tried again this conversation
    std::optional<double> seatYaw;          // a seat's facing (item 7): taken at the walk's end instead of the stage's
    double probeS = 0.0;                    // worldWalkLog: when this walk's properties are written (0: written or none)
    double gaitS = 0.0;                     // ... the next line of its gait trace, and when the walk began
    double gaitStartS = 0.0;
    int gesturesLeft = 0;                   // times this walk took the walker out of the gesture mode
    double deadlineS = 0.0;
    double speedUnitsPerS = 0.0;
    bool tracked = false;  // a matinee's move track moves this actor (an authored walk): never undone
    cw::Insistence insist; // the same small placement asked tick after tick: the game's own, accepted (cw::Insists)
    cw::AttentionState attention;  // whether its body turns toward whom it looks at (cw::StepAttention)
    std::optional<double> facingGoal;  // a facing the stage set at once, taken as a turn (nobody to look at)
    ue3::Object* lookAt = nullptr; // whom it looks at now, since when, and whether the head's reach was logged for it
    cw::RoleState role;            // its part in the line (cw::AttendAs): a bystander's glance,
    ue3::Object* rolePartner = nullptr;  // ... and at whom
    double traceS = 0.0;           // the next line of the turn trace (worldTurnLog)
    double lookRetryS = 0.0;       // a look the game refused is asked again after this
    std::shared_ptr<Reach> reach;  // its look-at chain's links pointed at stronger copies of their definitions (the pair)
    std::optional<double> homeYaw; // the facing the scene composed, while a turn of ours has the body away from it,
    bool away = false;
    double engagedS = 0.0;         // ... and when its body last had someone to turn for
    ue3::Object* lookOurs = nullptr;   // the look-at target we gave it (BioPawn.SetLookAtTarget); nullptr: none of ours
    ue3::Object* lookWanted = nullptr; // ... the one its part wants, and since when (the look begins a moment later)
    double lookWantedS = 0.0;
    ue3::Object* lookGame = nullptr;   // a target the game itself gave it, last seen (logged once per target)
    ue3::Object* heard = nullptr;      // the speaker a bystander did not look at, last logged
    double lookSinceS = 0.0;
    bool lookLogged = false;
    double turnGoal = 0.0;         // a turn asked of the game (BioPawn.RequestTurnInPlace): where to,
    double turnFromYaw = 0.0;      // ... from where,
    double turnAskedS = 0.0;       // ... when,
    double turnUntilS = 0.0;       // ... and until when it is the game's (0: none on its way)
};
std::vector<Stay> gStay;
std::uint64_t gStayMs = 0;
bool gInWorldForStay = false;  // set by Tick: the world view is up

std::string ShortName(ue3::Object const* o);
double Seconds();
cw::Vec3 WalkPoint(ik::Vec3 v);

// The game's own placement function asks (engine/ConversationHooks; research/69 item 4): after the accepted opening a person of
// this conversation is not put on a stage mark; the call's facing still applies. Called on the game thread from inside the
// conversation's code: it reads the mod's own state and nothing of the engine. Each ask for another place is logged and, with
// walking to marks on (worldWalk), becomes the mark the person walks to (research/69 "Walking to marks").
bool KeepAtSource(void const* conversation, void const* actor, float const* location) {
    if (!gC.on || !gC.world || !gInWorldForStay || !gSettings.stay || !gSettings.staySource || !gC.openingDone ||
        conversation != gC.object) {
        return false;
    }
    for (Stay& st : gStay) {
        if (st.actor == actor) {
            ++gC.sourceKept;
            double const away = std::hypot(location[0] - st.place.x, location[1] - st.place.y);
            gC.sourceFarUnits = std::max(gC.sourceFarUnits, away);
            ik::Vec3 const mark{location[0], location[1], location[2]};
            // Asked again for the mark it is already walking to or waiting for: nothing new.
            bool const same = st.walk.Phase() != cw::WalkPhase::Idle &&
                std::hypot(location[0] - st.walk.Wanted().x, location[1] - st.walk.Wanted().y) < 30.0;
            if (away > 30.0 && !same) {
                ++st.marksAsked;
                bool const refused = st.aiRefused && std::hypot(location[0] - st.aiRefused->x, location[1] - st.aiRefused->y) < 30.0;
                // A step back or aside isn't walked (a turn to walk it and back: a pivot); far marks and steps ahead are.
                double const toMark = std::atan2(location[1] - st.place.y, location[0] - st.place.x) * kRadToDeg;
                bool const step = gSettings.walk == 1 &&
                    !cw::WalkWorthIt(away, toMark - st.shownYaw, gSettings.walkLongM * kUnitsPerMeter, gSettings.walkStepDeg);
                log::Info("convo: at {:.1f} s the stage asks {} onto a mark {:.2f} m away ({} ask{} for it so far; {} speaking): withheld{}",
                    Seconds(), st.name, away / kUnitsPerMeter, st.marksAsked, st.marksAsked == 1 ? "" : "s", ShortName(gC.speaker),
                    gSettings.walk == 0 ? std::string()
                    : refused          ? std::string(" (the game's AI couldn't walk it there before)")
                    : step ? std::format(" (a step {:.0f} deg off its facing: it keeps its place)", std::abs(std::remainder(toMark - st.shownYaw, 360.0)))
                           : std::string(", a mark to walk to"));
                if (gSettings.walk != 0 && !refused && !step) {
                    st.seatYaw.reset();  // the stage's own mark: its facing, not a seat's
                    st.walk.Jump(WalkPoint(mark), WalkPoint(st.place), static_cast<double>(GetTickCount64()) / 1000.0,
                        gSettings.walkMinM * kUnitsPerMeter);
                }
            } else if (away <= 30.0 && st.walk.Phase() == cw::WalkPhase::Waiting) {
                // Asked back onto where it stands before its walk began: the earlier mark was the camera's, not a move.
                st.walk.End();
                log::Info("convo: at {:.1f} s the stage asks {} back onto where it stands: its pending walk dropped", Seconds(), st.name);
            }
            return true;
        }
    }
    return false;
}

constexpr double kJumpUnits = 30.0;      // in one frame: a run moves under 3 units a frame at 90 Hz, a placement metres
constexpr double kStayMaxUnits = 600.0;  // farther: a scripted entrance, let through

int ToUnits(double degrees) {
    return static_cast<int>(std::lround(degrees * 65536.0 / 360.0));
}

double Seconds();

cw::Vec3 WalkPoint(ik::Vec3 v) {
    return {v.x, v.y, v.z};
}
ik::Vec3 WalkPoint(cw::Vec3 v) {
    return {v.x, v.y, v.z};
}
ue3::Vector EnginePoint(ik::Vec3 v) {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

// ---- The walk and the level's triggers (research/69 item 5) -------------------------------------------------------------------
// Actor.SetLocation is UWorld::FarMoveActor (0x3FFCE0), which, for an actor that collides, finds the actors it now touches
// and tells both sides, as any move does: a step into a trigger volume would fire its Kismet touch (a scene that then can't
// start during this conversation and may never fire again), a step out of a streaming volume would stream its area out. So
// a step may not change which triggers Shepard's cylinder overlaps: the triggers near the conversation are gathered (every
// Engine.Trigger and TriggerVolume whose collision is within the leash) and each step asks the engine's own point check,
// Actor.PointCheckComponent with the cylinder as a box, at both ends.
struct TriggerNear {
    ue3::Object* actor = nullptr;
    ue3::Object* component = nullptr;
    std::string name;
};
std::vector<TriggerNear> gTriggers;
std::uint64_t gTriggersMs = 0;
std::uint32_t gTriggersFor = 0;  // the conversation (its number) they were gathered for

bool Overlaps(TriggerNear const& t, ik::Vec3 at, ik::Vec3 extent) {
    ue3::Params p(ue3::runtime::Get(), gPointCheck.Get());
    if (!p.Valid() || !p.Set("InComponent", t.component) ||
        !p.Set("PointLocation", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z)}) ||
        !p.Set("PointExtent", ue3::Vector{static_cast<float>(extent.x), static_cast<float>(extent.y), static_cast<float>(extent.z)})) {
        return false;
    }
    ue3::runtime::Call(t.actor, gPointCheck.Get(), p);
    return p.ReturnBool().value_or(false);
}

// The triggers and trigger volumes whose collision is within `reach` of `at` (a box, 400 units up and down).
std::vector<TriggerNear> TriggersNear(ik::Vec3 at, double reach);

void GatherTriggers(ik::Vec3 anchor) {
    gTriggers = TriggersNear(anchor, gSettings.moveLeashM * kUnitsPerMeter + 300.0);
}

std::vector<TriggerNear> TriggersNear(ik::Vec3 anchor, double reach) {
    std::vector<TriggerNear> found;
    if (gPointCheck.Get() == nullptr || gCollisionComponent.Get() == nullptr) {
        return found;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    ik::Vec3 const box{reach, reach, 400.0};
    std::int32_t const n = r.ObjectCount();
    for (std::int32_t i = 0; i < n; ++i) {
        ue3::Object* const o = r.ObjectAt(i);
        if (o == nullptr || !r.IsAlive(o) || !(ue3::IsA(o, gTriggerClass) || ue3::IsA(o, gTriggerVolumeClass)) ||
            r.NameText(o).rfind("Default__", 0) == 0) {
            continue;
        }
        ue3::Object* const component = gCollisionComponent.Read<ue3::Object*>(o).value_or(nullptr);
        if (component == nullptr) {
            continue;
        }
        TriggerNear t{o, component, ShortName(o)};
        if (Overlaps(t, anchor, box)) {  // a component not in the world (its level hidden) answers no
            found.push_back(std::move(t));
        }
    }
    return found;
}

// The pawn's collision cylinder as a point check's extent.
ik::Vec3 PawnExtent(ue3::Object* pawn) {
    ue3::Object* const cylinder = gPawnCylinder.Read<ue3::Object*>(pawn).value_or(nullptr);
    double const radius = cylinder != nullptr ? gCylinderRadius.Read<float>(cylinder).value_or(34.0f) : 34.0f;
    double const half = cylinder != nullptr ? gCylinderHeight.Read<float>(cylinder).value_or(78.0f) : 78.0f;
    return {radius, radius, half};
}

// True if the step from `from` to `to` begins or ends a trigger's touch; `which` and `enters` say which and how.
// (A walk-in's way to Shepard's mark stops at such an edge too, even of a trigger the mark stands in: the council's podium
// is 13 m past one. Letting those through was written and taken out again on 2026-11-10: a second touch of a volume in
// the conversation's first ticks ended the council's hearing, and what a touch sets off is the level's to say.)
bool StepChangesTouch(ue3::Object* pawn, ik::Vec3 anchor, ik::Vec3 from, ik::Vec3 to, std::string& which, bool& enters) {
    std::uint64_t const now = GetTickCount64();
    if (gTriggersFor != gC.number || now - gTriggersMs > 5000) {
        bool const first = gTriggersFor != gC.number;
        GatherTriggers(anchor);
        gTriggersFor = gC.number;
        gTriggersMs = now;
        if (first) {
            std::string names;
            for (TriggerNear const& t : gTriggers) {
                names += (names.empty() ? "" : ", ") + t.name;
            }
            log::Info("convo: the walk's triggers: {} within the leash of the owner{}{}", gTriggers.size(), names.empty() ? "" : ": ", names);
        }
    }
    ik::Vec3 const extent = PawnExtent(pawn);
    for (TriggerNear const& t : gTriggers) {
        if (!ue3::runtime::IsAlive(t.actor)) {
            continue;
        }
        bool const before = Overlaps(t, from, extent);
        bool const after = Overlaps(t, to, extent);
        if (before != after) {
            which = t.name;
            enters = after;
            return true;
        }
    }
    return false;
}

bool PutAt(ue3::Object* actor, ik::Vec3 point) {
    ue3::Params p(ue3::runtime::Get(), gSetLocation.Get());
    return p.Valid() && p.Set("NewLocation", EnginePoint(point)) && ue3::runtime::Call(actor, gSetLocation.Get(), p) &&
           p.ReturnBool().value_or(false);
}

bool WalkVelocity(ue3::Object* actor, ik::Vec3 velocity) {
    ue3::Vector* const out = gWalkVelocity.In<ue3::Vector>(actor);
    if (out == nullptr) {
        return false;  // the handle logs a missing field; the caller stands the walk down
    }
    *out = EnginePoint(velocity);
    write_journal::Wrote("ConversationWorld.walkVelocity", out, sizeof(*out));
    return true;
}

// A counter of the AI's enables raised to 1 for a walk (journalled); returns by how much.
// The game's own switch for a part of a pawn's AI (BioActorBehavior.AbilityEnable(bEnable, DT_AI, subtype), 0x9E6730 and
// AbilityEnable_AI 0x9E6770: a counter that fires the enabled or disabled event on its way through 0, which a counter written
// by hand skips: C-Sec's people entered the move state and its code never ran), `times` times. False: the function or the
// name isn't there, or a call failed (the caller then writes the counter itself).
ue3::FunctionRef gAbilityEnable{"SFXGame.BioActorBehavior.AbilityEnable"};
bool gAbilityEnableNoted = false;
bool AbilityEnable(ue3::Object* behavior, bool enable, std::wstring_view subtype, int times) {
    ue3::Function* const f = gAbilityEnable.Get();
    std::optional<ue3::Name> const name = engine::name_lookup::Find(subtype);
    if (f == nullptr || !name) {
        if (!gAbilityEnableNoted) {
            gAbilityEnableNoted = true;
            log::Warn("convo: walk: the game's AbilityEnable or the name for the AI's subtype isn't there: its counters are written instead");
        }
        return false;
    }
    ue3::Reflection& r = ue3::runtime::Get();
    for (int i = 0; i < times; ++i) {
        ue3::Params p(r, f);
        if (!p.Valid() || !p.SetBool("bEnable", enable) || !p.Set("eEnableType", std::uint8_t{0}) || !p.Set("nmSubType", *name) ||
            !ue3::runtime::Call(behavior, f, p)) {
            return false;
        }
    }
    return true;
}

// A counter of the AI's enables raised to 1 for a walk, through the game's AbilityEnable (else written, journalled); returns
// by how much.
int RaiseAiCounter(ue3::FieldRef& field, ue3::Object* behavior, std::wstring_view subtype) {
    std::int32_t* const value = field.In<std::int32_t>(behavior);
    int const by = value != nullptr ? cw::AiEnableDeficit(*value) : 0;
    if (by == 0) {
        return 0;
    }
    if (AbilityEnable(behavior, true, subtype, by) && field.Read<std::int32_t>(behavior).value_or(0) > 0) {
        return by;
    }
    *value = 1;
    write_journal::Wrote("ConversationWorld.aiEnabled", value, sizeof(*value));
    return by;
}

void LowerAiCounter(ue3::FieldRef& field, ue3::Object* behavior, std::wstring_view subtype, int by) {
    std::int32_t* const value = by > 0 ? field.In<std::int32_t>(behavior) : nullptr;
    if (value == nullptr) {
        return;
    }
    if (AbilityEnable(behavior, false, subtype, by)) {
        return;  // by what the walk added: any change the game made meanwhile stands
    }
    *value -= by;
    write_journal::Wrote("ConversationWorld.aiEnabled", value, sizeof(*value));
}

// BioPawn.SetDesiredSpeed(speed, bForce) on a walker (game thread).
bool SetWalkerSpeed(ue3::Object* pawn, float speed, bool force) {
    ue3::Function* const f = gSetDesiredSpeed.Get();
    if (f == nullptr || pawn == nullptr || !ue3::runtime::IsAlive(pawn) || gTurnPawnClass.Get() == nullptr || !ue3::IsA(pawn, gTurnPawnClass)) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), f);
    return p.Valid() && p.Set("fDesiredSpeed", speed) && p.SetBool("bForce", force) && ue3::runtime::Call(pawn, f, p);
}

// The walk's hold on the AI let go: its counters as the level had them, its controller no longer answered for.
void ReleaseAi(Stay& st) {
    if (st.walkController != nullptr) {
        SetWalkerSpeed(st.actor, 0.0f, false);  // as the move state's own Cleanup
    }
    if (st.walkBehavior != nullptr && ue3::runtime::IsAlive(st.walkBehavior) && ue3::IsA(st.walkBehavior, gEpicBehaviorClass)) {
        LowerAiCounter(gAiAllEnabled, st.walkBehavior, L"ALL", st.allRaised);
        LowerAiCounter(gAiStackEnabled, st.walkBehavior, L"Stack", st.stackRaised);
        if (st.allRaised + st.stackRaised > 0) {
            log::Info("convo: walk: {}'s AI switched off again as the level had it", st.name);
        }
    }
    if (st.walkController != nullptr) {
        std::erase(gWalkingControllers, st.walkController);
        log::Info("convo: walk: the game mode's ban on AI movement answered {} times for {}", gAllowAnswers - st.answersAtStart, st.name);
    }
    st.walkBehavior = nullptr;
    st.walkController = nullptr;
    st.allRaised = st.stackRaised = 0;
}

void EndWalk(Stay& st, char const* event, char const* why) {
    if (st.walk.Phase() == cw::WalkPhase::Idle) {
        return;
    }
    ReleaseAi(st);
    bool const running = st.walk.Phase() == cw::WalkPhase::Walking;
    if (st.moveAction != nullptr && ue3::runtime::IsAlive(st.moveAction) && ue3::IsA(st.moveAction, gMoveDataClass)) {
        if (!gMoveTerminate.WriteBool(st.moveAction, true)) {  // FieldRef journals the bitfield write
            log::Warn("convo: walk: could not terminate {}'s AI move", st.name);
        }
    }
    if (running && ue3::runtime::IsAlive(st.actor)) {
        WalkVelocity(st.actor, {});
        if (st.method == 3) {
            if (ue3::Vector* const accel = gWalkAcceleration.In<ue3::Vector>(st.actor)) {
                *accel = {};
                write_journal::Wrote("ConversationWorld.walkAcceleration", accel, sizeof(*accel));
            }
            if (st.wasWalking) {
                gWalkIsWalking.WriteBool(st.actor, *st.wasWalking);  // journalled by FieldRef
            }
        }
        st.wasWalking.reset();
        if (auto const at = player::Location(st.actor)) {
            st.place = V(*at);
        }
    }
    log::Info("convo: walk: {} {} at {} ({}, {})", event, st.name, F(st.place),
        running ? (st.method == 1 ? "ai" : st.method == 3 ? "the engine's walking" : "slide") : "pending", why);
    st.moveAction = nullptr;
    st.walk.End();
}

void EndWalks(char const* why) {
    for (Stay& st : gStay) {
        EndWalk(st, "cancel", why);
    }
}

std::string StateNameOf(ue3::Object* object);

// Returns the reason to use slide, or nullptr for a successful AI request. Game thread (camera post-handler) only.
char const* StartAiWalk(Stay& st) {
    if (gWalkPawnClass.Get() == nullptr || !ue3::IsA(st.actor, gWalkPawnClass)) return "actor is not an Engine.Pawn";
    if (gWalkController.Get() == nullptr) return "Engine.Pawn.Controller missing";
    ue3::Object* const ai = gWalkController.Read<ue3::Object*>(st.actor).value_or(nullptr);
    if (ai == nullptr || !ue3::runtime::IsAlive(ai)) return "no live controller";
    if (gWalkAiClass.Get() == nullptr || !ue3::IsA(ai, gWalkAiClass)) return "controller is not a BioAiController";
    // The game's AI runs no state code for a hidden pawn (C-Sec's Wrex in a plot state where his scene isn't due: the move state
    // was pushed and never began, 2026-11-11), and nobody sees a hidden person walk.
    if (gActorHidden.ReadBool(st.actor).value_or(false)) return "it is hidden";
    ue3::Function* const fn = gPushMove.Get();
    if (fn == nullptr) return "PushMoveToPoint missing";
    ue3::Reflection& r = ue3::runtime::Get();
    if (r.FindFunction(r.ClassOf(ai), "PushMoveToPoint") != fn) return "controller class has no matching PushMoveToPoint";
    if (gMoveDataClass.Get() == nullptr || gMoveTerminate.Get() == nullptr) return "AI move termination reflection missing";
    ue3::Params p(r, fn);
    if (!p.Valid() || !p.Set("vLoc", EnginePoint(WalkPoint(st.walk.Wanted()))) || !p.Set("PointTolerance", 10.0f) ||
        !p.SetBool("bWalking", true) || !p.SetBool("bRequiresPrecisionMovement", true) || p.ReturnSpec() == nullptr ||
        p.ReturnSpec()->size != static_cast<std::int32_t>(sizeof(ue3::Object*))) return "PushMoveToPoint parameter lookup failed";
    ue3::Object* const behavior = gBioComponent.Read<ue3::Object*>(st.actor).value_or(nullptr);
    if (behavior == nullptr || !ue3::runtime::IsAlive(behavior) || gEpicBehaviorClass.Get() == nullptr ||
        !ue3::IsA(behavior, gEpicBehaviorClass) || gAiAllEnabled.Get() == nullptr || gAiStackEnabled.Get() == nullptr) {
        return "no BioEpicPawnBehavior with the AI's enable counters";
    }
    if (!gAllowAnswerable || !engine::call_function::Installed()) {
        return "the game mode's ban on AI movement can't be answered (no CallFunction hook)";
    }
    // The AI's hold for the walk, before the move is asked (its native checks the counters at once).
    st.walkBehavior = behavior;
    int const allWas = gAiAllEnabled.Read<std::int32_t>(behavior).value_or(0);
    int const stackWas = gAiStackEnabled.Read<std::int32_t>(behavior).value_or(0);
    st.allRaised = RaiseAiCounter(gAiAllEnabled, behavior, L"ALL");
    st.stackRaised = RaiseAiCounter(gAiStackEnabled, behavior, L"Stack");
    st.walkController = ai;
    st.answersAtStart = gAllowAnswers;
    gWalkingControllers.push_back(ai);
    if (st.allRaised + st.stackRaised > 0) {
        log::Info("convo: walk: {}'s AI switched on for the walk (the level had it off: ALL {}, Stack {})", st.name, allWas, stackWas);
    }
    char const* why = nullptr;
    if (gSettings.walkLog) {
        log::Info("convo: walk: {}'s controller is in state {} before its move", st.name, StateNameOf(ai));
    }
    if (!ue3::runtime::Call(ai, fn, p)) {
        why = "PushMoveToPoint call failed";
    } else {
        st.moveAction = p.Return<ue3::Object*>().value_or(nullptr);
        if (st.moveAction == nullptr || !ue3::runtime::IsAlive(st.moveAction)) {
            why = "PushMoveToPoint returned no live move action";
        } else if (!ue3::IsA(st.moveAction, gMoveDataClass)) {
            why = "PushMoveToPoint returned an unexpected action class";
        }
    }
    if (why != nullptr) {
        st.moveAction = nullptr;
        ReleaseAi(st);
    }
    return why;
}

// worldWalkLog: whether a walker's legs walk. Its ankles in its mesh's own space ten times a second for the walk's first
// 3 s: a walk cycle swings them forward and back in turn (tens of units), a glide in a standing pose keeps them still.
std::map<ue3::Object const*, std::pair<int, int>> gAnkleBones;  // skeletal mesh asset -> its left and right ankle (-1: none)
ue3::FunctionRef gForceSkelUpdate{"Engine.SkeletalMeshComponent.ForceSkelUpdate"};  // a mesh out of view isn't posed by itself
void TraceGait(Stay& st, double nowS, double startS) {
    ue3::Object* const mesh = player::Mesh(st.actor);
    ue3::Object* const asset = mesh != nullptr ? player::SkeletalMesh(mesh) : nullptr;
    if (asset == nullptr) {
        return;
    }
    auto it = gAnkleBones.find(asset);
    if (it == gAnkleBones.end()) {
        std::pair<int, int> bones{-1, -1};
        if (auto const skeleton = engine::skeleton::ReadRefSkeleton(mesh)) {
            for (std::size_t i = 0; i < skeleton->names.size(); ++i) {
                std::string const name = ini::Lower(skeleton->names[i]);
                if (name == "leftankle") bones.first = static_cast<int>(i);
                if (name == "rightankle") bones.second = static_cast<int>(i);
            }
        }
        it = gAnkleBones.emplace(asset, bones).first;
    }
    if (ue3::Function* const force = gForceSkelUpdate.Get()) {
        ue3::Params parms(ue3::runtime::Get(), force);
        ue3::runtime::Call(mesh, force, parms);  // the pose as of now, wherever the view points (game thread)
    }
    auto const bases = engine::skeleton::SpaceBases(mesh);
    auto at = [&](int bone) -> std::optional<ik::Vec3> {
        if (bone < 0 || static_cast<std::size_t>(bone) >= bases.size()) return std::nullopt;
        return engine::skeleton::ToTransform(bases[static_cast<std::size_t>(bone)]).translation;
    };
    auto const left = at(it->second.first);
    auto const right = at(it->second.second);
    auto const velocity = gWalkVelocity.Read<ue3::Vector>(st.actor);
    if (!left || !right) {
        return;
    }
    log::Info("convo: walk: gait {} +{:.1f} s: ankles L ({:.0f}, {:.0f}, {:.0f}) R ({:.0f}, {:.0f}, {:.0f}), apart {:.0f}; speed {:.0f} u/s",
        st.name, nowS - startS, left->x, left->y, left->z, right->x, right->y, right->z, ik::Length(*left - *right),
        velocity ? std::hypot(velocity->x, velocity->y) : -1.0);
}

// worldWalkLog: the walker's animation tree as it stands (each blend's children and weights, each sequence's animation, rate
// and time), as text: what the conversation has the body play while it is moved.
ue3::ClassRef gTreeBlendBase{"Engine.AnimNodeBlendBase"};
ue3::ClassRef gTreeSequence{"Engine.AnimNodeSequence"};
ue3::FieldRef gTreeAnimations{"Engine.SkeletalMeshComponent", "Animations"};
ue3::FieldRef gTreeNodeName{"Engine.AnimNode", "NodeName"};
ue3::FieldRef gTreeChildren{"Engine.AnimNodeBlendBase", "Children"};
ue3::FieldRef gTreeChildName{gTreeChildren, "Name"};
ue3::FieldRef gTreeChildAnim{gTreeChildren, "Anim"};
ue3::FieldRef gTreeChildWeight{gTreeChildren, "Weight"};
ue3::FieldRef gTreeSeqName{"Engine.AnimNodeSequence", "AnimSeqName"};
ue3::FieldRef gTreeSeqRate{"Engine.AnimNodeSequence", "Rate"};
ue3::FieldRef gTreeSeqTime{"Engine.AnimNodeSequence", "CurrentTime"};
ue3::FieldRef gTreeSeqPlaying{"Engine.AnimNodeSequence", "bPlaying"};
void DumpTree(ue3::Object* node, int depth, std::vector<ue3::Object const*>& seen, std::string& out) {
    if (node == nullptr || depth > 24 || seen.size() > 400 || std::find(seen.begin(), seen.end(), node) != seen.end()) {
        return;
    }
    seen.push_back(node);
    ue3::Reflection& r = ue3::runtime::Get();
    auto text = [&](std::optional<ue3::Name> n) { return n ? r.ToString(*n) : std::string("?"); };
    std::string const name = text(gTreeNodeName.Read<ue3::Name>(node));
    std::string const pad(static_cast<std::size_t>(depth) * 2, ' ');
    std::string const cls = r.ToString(r.NameOf(r.ClassOf(node)));
    if (ue3::IsA(node, gTreeSequence)) {
        out += std::format("{}{} [{}] seq {} rate {:.2f} t {:.2f} {}\n", pad, name, cls, text(gTreeSeqName.Read<ue3::Name>(node)),
            gTreeSeqRate.Read<float>(node).value_or(-1.0f), gTreeSeqTime.Read<float>(node).value_or(-1.0f),
            gTreeSeqPlaying.ReadBool(node).value_or(false) ? "playing" : "stopped");
        return;
    }
    out += std::format("{}{} [{}]\n", pad, name, cls);
    if (!ue3::IsA(node, gTreeBlendBase)) {
        return;
    }
    if (std::optional<ue3::Elements> const children = gTreeChildren.ElementsOf(node, 64)) {
        for (std::int32_t i = 0; i < children->count; ++i) {
            float const weight = gTreeChildWeight.Read<float>(children->At(i)).value_or(-1.0f);
            std::string const child = text(gTreeChildName.Read<ue3::Name>(children->At(i)));
            out += std::format("{}  - child {} '{}' weight {:.2f}\n", pad, i, child, weight);
            if (weight > 0.001f || i == 0) {  // what shows (an additive node's base input reads weight 0 and always shows)
                DumpTree(gTreeChildAnim.Read<ue3::Object*>(children->At(i)).value_or(nullptr), depth + 2, seen, out);
            }
        }
    }
}

// worldWalkLog: what holds an AI walk still, 1 s in: the controller's state and both objects' properties, to a file each.
ue3::FunctionRef gGetStateName{"Core.Object.GetStateName"};
std::string StateNameOf(ue3::Object* object) {
    ue3::Reflection& r = ue3::runtime::Get();
    if (ue3::Function* const f = gGetStateName.Get(); f != nullptr && object != nullptr && r.IsAlive(object)) {
        ue3::Params p(r, f);
        if (p.Valid() && ue3::runtime::Call(object, f, p)) {
            return r.ToString(p.Return<ue3::Name>().value_or(ue3::Name{}));
        }
    }
    return "?";
}

void ProbeWalk(Stay& st) {
    st.probeS = 0.0;
    ue3::Reflection& r = ue3::runtime::Get();
    std::string const state = StateNameOf(st.walkController);
    wchar_t buffer[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return;
    }
    std::filesystem::path const folder = std::filesystem::path(buffer) / L"MEVR" / L"walk-probe";
    std::error_code ec;
    std::filesystem::create_directories(folder, ec);
    auto write = [&](ue3::Object* o, char const* what) {
        if (o == nullptr || !r.IsAlive(o)) {
            return;
        }
        std::filesystem::path const file = folder / std::format("{}-{}-{}.json", st.name, what, GetTickCount64());
        if (FILE* out = _wfopen(file.c_str(), L"wb")) {
            std::string const text = galaxy_dump::Props(o);
            std::fwrite(text.data(), 1, text.size(), out);
            std::fclose(out);
        }
    };
    write(st.actor, "pawn");
    write(st.walkController, "controller");
    write(st.walkBehavior, "behavior");
    if (ue3::Object* const mesh = player::Mesh(st.actor)) {
        std::string tree;
        std::vector<ue3::Object const*> seen;
        DumpTree(gTreeAnimations.Read<ue3::Object*>(mesh).value_or(nullptr), 0, seen, tree);
        std::filesystem::path const file = folder / std::format("{}-tree-{}.txt", st.name, GetTickCount64());
        if (FILE* out = _wfopen(file.c_str(), L"wb")) {
            std::fwrite(tree.data(), 1, tree.size(), out);
            std::fclose(out);
        }
    }
    log::Info("convo: walk: {} 1 s into its AI walk: the controller's state {}, the properties in {}", st.name, state, ini::Utf8(folder));
}

// BioAiController.DoesGameModeAllowMovement (and the henchman's): true for a controller walking one of ours, so the game's
// ShouldCancelMove keeps its move in the conversation mode. Game thread (the script VM).
bool OnAllowMovement(ue3::Object* context, ue3::Function* /*function*/, ue3::Function* /*caller*/, std::uint8_t* /*locals*/,
    void* result) {
    if (result == nullptr || gWalkingControllers.empty() ||
        std::find(gWalkingControllers.begin(), gWalkingControllers.end(), context) == gWalkingControllers.end()) {
        return false;
    }
    *static_cast<std::uint32_t*>(result) = 1;
    ++gAllowAnswers;
    return true;
}

// Advances a walk and returns whether its travel heading owns the turn this frame.
bool WalkToMark(Stay& st, ik::Vec3 previousKept, double nowS, double dt, double& heading) {
    if (st.walk.Phase() == cw::WalkPhase::Waiting) {
        // Both methods need to zero velocity on exit and face the travel direction. Missing reflection leaves normal stay on.
        if (gWalkVelocity.In<ue3::Vector>(st.actor) == nullptr || gSetRotation.Get() == nullptr) {
            EndWalk(st, "end", "walk reflection missing; keeping in place");
            return false;
        }
        if (st.walk.StartIfSettled(WalkPoint(st.place), nowS, gSettings.walkSettleS, gSettings.walkMinM * kUnitsPerMeter)) {
            previousKept = st.place;  // start from the actor's actual place when the wait finishes
            st.method = st.requestedMethod = gSettings.walk;
            st.speedUnitsPerS = gSettings.walkSpeedMS * kUnitsPerMeter;
            double const distance = Dist(st.place, WalkPoint(st.walk.Wanted()));
            st.deadlineS = nowS + distance / st.speedUnitsPerS + 3.0;
            if (gSettings.walk == 1) {
                st.deadlineS += distance / st.speedUnitsPerS + 2.0;  // the AI's path may go round things, and it starts and stops
            }
            if (st.method == 1) {
                st.progressFrom = st.place;  // a walk the AI doesn't move within a second slides the rest (below)
                st.progressS = nowS;
                if (char const* why = StartAiWalk(st)) {
                    // A glide with the feet planted looks worse than staying: the person keeps its place (worldWalk = 2 slides).
                    log::Info("convo: walk: {} ai unavailable: {}; it stays where it stands", st.name, why);
                    ++gAiStalled;
                    st.aiRefused = WalkPoint(st.walk.Wanted());
                    st.walk.End();
                    return false;
                } else if (gSettings.walkLog) {
                    st.probeS = nowS + 1.0;  // (the slide too, below)
                    log::Info("convo: walk: {}", call_trace::Start(4.0, {"InitState", "PointReachable", "FindPathTo", "GetNextPathNode",
                        "PrecisionMoveTo", "ReachedDestination", "GetMoveTolerance", "NeedPrecisionMovement", "LeaderCanITeleport",
                        "HandleTeleport", "CanUseStack", "PopState", "MoveTo"}));
                }
            }
            if (st.method == 3) {
                // The engine walks it: a pawn on the ground (PHYS_Walking) with an acceleration field. Its facing as the stage
                // left it (the withheld placement applied it) is where it ends up.
                auto const physics = gWalkPhysics.Read<std::uint8_t>(st.actor);
                if (!physics || *physics != 1 || gWalkAcceleration.In<ue3::Vector>(st.actor) == nullptr) {
                    st.method = 2;
                    log::Info("convo: walk: {} not on the ground under the engine's walking (physics {}): sliding instead", st.name,
                        physics ? static_cast<int>(*physics) : -1);
                } else {
                    st.wasWalking = gWalkIsWalking.ReadBool(st.actor);
                    gWalkIsWalking.WriteBool(st.actor, true);  // the walking pace (GroundSpeed x WalkingPct)
                    st.progressFrom = st.place;
                    st.progressS = nowS;
                }
            }
            if (st.seatYaw) {
                st.walkFacing = *st.seatYaw;  // a seat in view: the group's middle
            } else if (auto const rot = player::Rotation(st.actor)) {
                st.walkFacing = YawDeg(*rot);
            }
            ++gC.walks;
            st.gesturesLeft = 0;
            st.gaitStartS = nowS;
            st.gaitS = gSettings.walkLog ? nowS : 0.0;
            if (gSettings.walkLog && st.probeS == 0.0) {
                st.probeS = nowS + 1.0;
            }
            log::Info("convo: walk: start {} from {} to {}, {:.2f} m, method {}", st.name, F(st.place),
                F(WalkPoint(st.walk.Wanted())), distance / kUnitsPerMeter, st.method == 1 ? "ai" : st.method == 3 ? "the engine's walking" : "slide");
        } else if (st.walk.Phase() == cw::WalkPhase::Idle) {
            log::Info("convo: walk: cancel {} pending mark: already within worldWalkMinM", st.name);
        }
    }
    if (st.walk.Phase() != cw::WalkPhase::Walking) return false;
    if (gSettings.walkGestures && gTurnPawnClass.Get() != nullptr && ue3::IsA(st.actor, gTurnPawnClass) &&
        gInGestures.ReadBool(st.actor).value_or(false)) {
        gInGestures.WriteBool(st.actor, false);  // journalled by FieldRef
        if (st.gesturesLeft++ == 0) {
            log::Info("convo: walk: {} leaves the conversation's gesture mode for its walk (its legs walk)", st.name);
        }
    }
    if (gSettings.walkLog && st.gaitS > 0.0 && nowS >= st.gaitS) {
        TraceGait(st, nowS, st.gaitStartS);
        st.gaitS = nowS - st.gaitStartS < 3.0 ? nowS + 0.1 : 0.0;
    }
    if (st.method == 1 && st.walkController != nullptr) {
        SetWalkerSpeed(st.actor, 1.0f, false);  // the paused controller's speed, each tick (the game may zero it again)
    }
    if (st.probeS > 0.0 && nowS >= st.probeS) {
        ProbeWalk(st);
    }
    ik::Vec3 const mark = WalkPoint(st.walk.Wanted());
    if (st.method == 1) {
        // The game's AI walks it (its own locomotion: start, walk cycle, stop). Its place is what the AI made; it arrives within
        // the move's own tolerance (20 units, the state's GetMoveTolerance), measured on the floor (the stage's mark lies lower).
        auto const actual = player::Location(st.actor);
        if (!actual) {
            EndWalk(st, "end", "actor location lookup failed");
            return false;
        }
        st.place = V(*actual);
        double const flat = std::hypot(mark.x - st.place.x, mark.y - st.place.y);
        if (flat <= kAiArriveUnits) {
            EndWalk(st, "arrival", std::format("reached the mark by the game's AI, {:.2f} m from it", flat / kUnitsPerMeter).c_str());
            if (st.walkFacing) {
                st.facingGoal = *st.walkFacing;  // the stage's facing for the mark, as a turn
            }
            return false;
        }
        if (nowS - st.progressS >= kAiStallS) {
            if (std::hypot(st.place.x - st.progressFrom.x, st.place.y - st.progressFrom.y) < 8.0) {
                // The AI holds still (C-Sec's Wrex, 2026-11-11: its move timed out without its loop running): it stays where it
                // got to, as a glide with planted feet would look worse.
                ++gAiStalled;
                st.aiRefused = mark;
                if (st.gesturesLeft > 0 && ue3::runtime::IsAlive(st.actor)) {
                    gInGestures.WriteBool(st.actor, true);  // it never walked: its gestures back (journalled by FieldRef)
                    log::Info("convo: walk: {} back in the conversation's gesture mode (it didn't walk)", st.name);
                }
                EndWalk(st, "end", std::format("the game's AI didn't move it 8 units in {:.1f} s; it stays here", nowS - st.progressS).c_str());
                return false;
            }
            st.progressFrom = st.place;
            st.progressS = nowS;
        }
    }
    ik::Vec3 const travel = mark - st.place;
    if (st.method == 2 && Dist(st.place, mark) <= 0.01) {
        // The AI has a small point tolerance; settle its last centimetres onto the exact mark if collision permits it.
        if (PutAt(st.actor, mark)) {
            auto const actual = player::Location(st.actor);
            if (actual) st.place = V(*actual);
            if (Dist(st.place, mark) <= 0.01) {
                EndWalk(st, "arrival", "reached the mark");
                if (st.method == 1 && st.walkFacing) {
                    st.facingGoal = *st.walkFacing;  // the stage's facing for the mark, as a turn
                }
                return false;
            }
        }
    }
    if (nowS >= st.deadlineS) {
        EndWalk(st, "end", "distance / speed + 3 s elapsed; staying here");
        return false;
    }
    if (std::hypot(travel.x, travel.y) > 0.01) heading = std::atan2(travel.y, travel.x) * kRadToDeg;
    if (st.method == 3) {
        // The engine walks it: acceleration along the floor toward the mark each tick; its place is what the physics made.
        auto const actual = player::Location(st.actor);
        if (!actual) {
            EndWalk(st, "end", "actor location lookup failed");
            return false;
        }
        st.place = V(*actual);
        double const flat = std::hypot(mark.x - st.place.x, mark.y - st.place.y);
        if (flat <= 25.0) {
            EndWalk(st, "arrival", "reached the mark (the engine's walking)");
            if (st.walkFacing) {
                st.facingGoal = *st.walkFacing;  // the stage's facing for the mark, as a turn
            }
            return false;
        }
        if (nowS - st.progressS >= 0.75) {
            if (Dist(st.place, st.progressFrom) < 8.0) {
                // Nothing moves it (blocked, or its own code holds it): the rest by sliding.
                log::Info("convo: walk: {} didn't move under the engine's walking in {:.2f} s ({:.0f} units): sliding the rest", st.name,
                    nowS - st.progressS, Dist(st.place, st.progressFrom));
                if (ue3::Vector* const accel = gWalkAcceleration.In<ue3::Vector>(st.actor)) {
                    *accel = {};
                    write_journal::Wrote("ConversationWorld.walkAcceleration", accel, sizeof(*accel));
                }
                if (st.wasWalking) {
                    gWalkIsWalking.WriteBool(st.actor, *st.wasWalking);
                    st.wasWalking.reset();
                }
                st.method = 2;
                return true;
            }
            st.progressFrom = st.place;
            st.progressS = nowS;
        }
        double const rate = static_cast<double>(gWalkAccelRate.Read<float>(st.actor).value_or(2048.0f));
        if (ue3::Vector* const accel = gWalkAcceleration.In<ue3::Vector>(st.actor)) {
            accel->x = static_cast<float>((mark.x - st.place.x) / flat * rate);
            accel->y = static_cast<float>((mark.y - st.place.y) / flat * rate);
            accel->z = 0.0f;
            write_journal::Wrote("ConversationWorld.walkAcceleration", accel, sizeof(*accel));
        }
        gWalkIsWalking.WriteBool(st.actor, true);
        return true;
    }
    if (st.method == 2 && dt > 0.0) {
        // Velocity can also be integrated by the engine between camera updates. Step from the previous DRAWN/kept place,
        // so that engine motion and our SetLocation together advance one speed * dt, rather than doubling the slide speed.
        ik::Vec3 const before = previousKept;
        cw::WalkStep const step = cw::StepTowardMark(WalkPoint(before), st.walk.Wanted(), st.speedUnitsPerS, dt);
        bool const moved = PutAt(st.actor, WalkPoint(step.place));
        auto const actual = player::Location(st.actor);
        if (!actual) {
            EndWalk(st, "end", "actor location lookup failed");
            return false;
        }
        st.place = V(*actual);  // collision can refuse or shorten SetLocation: never assume we reached the requested point
        if (!WalkVelocity(st.actor, (st.place - before) * (1.0 / dt))) {
            EndWalk(st, "end", "velocity lookup failed");
            return false;
        }
        if (moved && Dist(st.place, mark) <= 0.01) EndWalk(st, "arrival", "reached the mark");
    }
    return true;
}

// After the frame's draw (the mod's tick): anyone still away from where we keep them was moved after this tick's camera update,
// so the drawn frame showed the jump. Undone at the next camera update; counted to measure whether the camera update is early
// enough.
int gDrawnJumps = 0;
void CheckDrawn() {
    for (Stay const& st : gStay) {
        auto const at = st.actor != nullptr && ue3::runtime::IsAlive(st.actor) ? player::Location(st.actor) : std::nullopt;
        if (at && Dist(V(*at), st.place) > kJumpUnits && Dist(V(*at), st.place) < kStayMaxUnits) {
            ++gDrawnJumps;
            log::Info("convo: a jump of {:.2f} m reached a drawn frame (after the camera update)", Dist(V(*at), st.place) / kUnitsPerMeter);
        }
    }
}

// The game's own turn on the spot (BioPawn.RequestTurnInPlace, a native: the pawn turns to the rotation with its turn
// animation). False: not a BioPawn, the function not found, or the game refused.
int gGameTurnFails = 0;  // turns asked of the game that came to nothing, this session: after two, ours
// The pawn's desired rotation (its physics turns it there at its own rate; `force`: at once).
bool SetDesired(ue3::Object* actor, ue3::Rotator const& now, double yawDeg, bool force) {
    ue3::Function* const fn = gSetDesiredRotation.Get();
    if (fn == nullptr || gTurnPawnClass.Get() == nullptr || !ue3::IsA(actor, gTurnPawnClass)) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    ue3::Rotator r = now;
    r.yaw = ToUnits(yawDeg);
    if (!p.Valid() || !p.Set("rDesiredRotation", r) || !p.SetBool("bForce", force)) {
        return false;
    }
    ue3::runtime::Call(actor, fn, p);
    return true;
}

bool RequestTurn(ue3::Object* actor, ue3::Rotator const& now, double yawDeg) {
    if (gSettings.turnWay == 0) {
        return SetDesired(actor, now, yawDeg, false);
    }
    ue3::Function* const fn = gRequestTurn.Get();
    if (fn == nullptr || gTurnPawnClass.Get() == nullptr || !ue3::IsA(actor, gTurnPawnClass)) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    ue3::Rotator r = now;
    r.yaw = ToUnits(yawDeg);
    if (!p.Valid() || !p.Set("rDesDir", r)) {
        return false;
    }
    ue3::runtime::Call(actor, fn, p);
    return p.ReturnBool().value_or(false);
}

// The game's own look-at target on a pawn (BioPawn.m_oLookAtData's target); nullptr: none.
ue3::Object* GameLookTarget(ue3::Object* actor) {
    ue3::Object* const data = gPawnLookData.Read<ue3::Object*>(actor).value_or(nullptr);
    if (data == nullptr || !ue3::runtime::IsAlive(data)) {
        return nullptr;
    }
    return gLookDataTarget.Read<ue3::Object*>(data).value_or(nullptr);
}

// The game's own function gives `actor` a look-at target (nullptr: none): its look-at chain turns the eyes, head, neck and
// chest. On the game thread.
bool SetLook(ue3::Object* actor, ue3::Object* target) {
    ue3::Function* const fn = gSetLookAt.Get();
    if (fn == nullptr || actor == nullptr || !ue3::runtime::IsAlive(actor)) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid() || !p.Set("oTarget", target) || !p.Set("a_eTransition", std::uint8_t{0}) ||
        !p.Set("nRank", static_cast<std::int32_t>(gSettings.lookRank))) {
        return false;
    }
    ue3::runtime::Call(actor, fn, p);
    return true;
}

// The game's strengths back on a look-at chain we gave a further reach (the controllers still alive and still on our copies).
void RestoreReach(Stay& st) {
    if (!st.reach) {
        return;
    }
    for (Reach::Link const& link : st.reach->links) {
        if (link.controller == nullptr || !ue3::runtime::IsAlive(link.controller)) {
            continue;
        }
        void** const slot = static_cast<void**>(gControllerDefinition.Address(link.controller));
        if (slot != nullptr && *slot == link.copy.get()) {
            *slot = link.original;
            write_journal::Wrote("ConversationWorld.lookDefinitionBack", slot, sizeof(void*));
        }
    }
    st.reach.reset();
}

// The pair's heads reach further (worldLookReach): the game turns each link of the chain by its definition's strength
// (the humanoid's: chest 0.125, neck 0.25, head 0.55 of what is left, which leaves the head a third of the way short of
// someone 80 degrees aside); the strength is read through the controller's pointer each frame, so the controllers of one
// of the pair point at copies with `lookReach` of the rest added. Once per look-at data (the game makes the controllers
// anew with each); a bystander keeps the game's glance.
void ApplyReach(Stay& st) {
    if (gSettings.lookReach <= 0.0f) {
        RestoreReach(st);
        return;
    }
    ue3::Object* const data = gPawnLookData.Read<ue3::Object*>(st.actor).value_or(nullptr);
    if (data == nullptr || !ue3::runtime::IsAlive(data)) {
        return;
    }
    if (st.reach && st.reach->data == data) {
        return;
    }
    RestoreReach(st);
    ue3::Object* const definition = gLookDefinition.Read<ue3::Object*>(data).value_or(nullptr);
    if (definition == nullptr || !ue3::runtime::IsAlive(definition)) {
        return;
    }
    auto const defs = gBoneDefinitions.ElementsOf(definition, 32);
    auto const controllers = gLookControllers.ElementsOf(data, 32);
    if (!defs || !controllers || defs->stride < 0x38 || defs->stride > 0x100 || controllers->stride != static_cast<std::int32_t>(sizeof(void*))) {
        return;
    }
    auto reach = std::make_shared<Reach>();
    reach->data = data;
    std::string strengths;
    for (std::int32_t i = 0; i < controllers->count; ++i) {
        ue3::Object* const controller = *static_cast<ue3::Object**>(controllers->At(i));
        if (controller == nullptr || !ue3::runtime::IsAlive(controller)) {
            continue;
        }
        void** const slot = static_cast<void**>(gControllerDefinition.Address(controller));
        if (slot == nullptr) {
            continue;
        }
        // Only a pointer into that definition's own array is taken for a definition.
        std::uint8_t* const original = static_cast<std::uint8_t*>(*slot);
        std::uint8_t* const end = defs->data + static_cast<std::ptrdiff_t>(defs->count) * defs->stride;
        if (original < defs->data || original >= end || (original - defs->data) % defs->stride != 0) {
            continue;
        }
        auto copy = std::make_unique<std::uint8_t[]>(static_cast<std::size_t>(defs->stride));
        std::memcpy(copy.get(), original, static_cast<std::size_t>(defs->stride));
        float* const strength = static_cast<float*>(gLinkStrength.Address(copy.get()));
        if (strength == nullptr || !std::isfinite(*strength) || *strength < 0.0f || *strength >= 0.999f) {
            continue;  // the eyes (1): nothing to add
        }
        float const before = *strength;
        *strength = static_cast<float>(cw::StrongerLink(before, gSettings.lookReach));
        strengths += std::format("{}{:.2f} -> {:.2f}", strengths.empty() ? "" : ", ", before, *strength);
        *slot = copy.get();
        write_journal::Wrote("ConversationWorld.lookDefinition", slot, sizeof(void*));
        reach->links.push_back({controller, original, std::move(copy)});
    }
    if (reach->links.empty()) {
        return;
    }
    st.reach = std::move(reach);
    if (gC.reaches++ == 0) {
        log::Info("convo: at {:.1f} s {}'s look-at chain reaches further (one of the pair; worldLookReach {:.2f}): its links' strengths {}",
            Seconds(), st.name, gSettings.lookReach, strengths);
    }
}

// The looks we gave are taken back (a conversation's end, the setting going off): only where the pawn still looks at ours.
void ReleaseLooks(char const* why) {
    int released = 0;
    for (Stay& st : gStay) {
        RestoreReach(st);
        if (st.lookOurs == nullptr) {
            continue;
        }
        if (st.actor != nullptr && ue3::runtime::IsAlive(st.actor) && GameLookTarget(st.actor) == st.lookOurs && SetLook(st.actor, nullptr)) {
            ++released;
        }
        st.lookOurs = nullptr;
        st.lookWanted = nullptr;
    }
    if (released != 0) {
        log::Info("convo: {} look-at target{} of ours taken back ({})", released, released == 1 ? "" : "s", why);
    }
}

// Whom `st` looks at by its part in the line (worldTurn = roles; research/69 "Who attends to whom"). `listed`: the
// conversation's own look-at list's target for it, if any. Returns whom it looks at now; `mayTurn`: whether its body may
// re-face toward that one (never a bystander's).
ue3::Object* RoleLook(Stay& st, ue3::Object* listed, ue3::Object* speaker, ue3::Object* listener, ue3::Object* player, double bodyYaw,
    double nowS, bool& mayTurn) {
    auto const id = [](ue3::Object const* o) { return static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(o)); };
    cw::Role const role = cw::RoleOf(id(st.actor), id(speaker), id(listener));
    // Its body may re-face only as one of the pair, only toward the player, only while he stands near: everyone else was
    // placed and faced by the scene (the ceremony's first line turned Udina right round toward a player 8 m behind him).
    auto const bodyFor = [&](ue3::Object const* whom) {
        ik::Vec3 const to = gC.hold - st.place;
        return role != cw::Role::Bystander && whom == player && std::hypot(to.x, to.y) <= gSettings.turnWithinM * kUnitsPerMeter;
    };
    mayTurn = false;
    // A target the game itself gives (the conversation's list, an authored look-at track) stands in place of the part's.
    ue3::Object* const game = GameLookTarget(st.actor);
    ue3::Object* const authored = listed != nullptr ? listed : game != nullptr && game != st.lookOurs ? game : nullptr;
    if (authored != nullptr) {
        if (authored != st.lookGame) {
            st.lookGame = authored;
            ++gC.looksGame;
            log::Info("convo: at {:.1f} s {} ({}) looks at {} by the game's own look-at: left to it", Seconds(), st.name, cw::Name(role),
                authored == player ? "the player" : ShortName(authored));
        }
        st.lookOurs = nullptr;
        st.lookWanted = nullptr;
        st.role = {};
        RestoreReach(st);
        mayTurn = bodyFor(authored);
        return authored;
    }
    st.lookGame = nullptr;
    ue3::Object* partner = reinterpret_cast<ue3::Object*>(static_cast<std::uintptr_t>(cw::PartnerOf(id(st.actor), id(speaker), id(listener))));
    if (partner != nullptr && !ue3::runtime::IsAlive(partner)) {
        partner = nullptr;
    }
    std::optional<double> off;
    if (partner != nullptr) {
        auto const theirs = partner == player ? std::optional<ik::Vec3>{gC.hold} : [&]() -> std::optional<ik::Vec3> {
            auto const at = player::Location(partner);
            return at ? std::optional<ik::Vec3>{V(*at)} : std::nullopt;
        }();
        if (theirs) {
            ik::Vec3 const to = *theirs - st.place;
            if (std::hypot(to.x, to.y) > 20.0) {
                off = YawDiffDeg(std::atan2(to.y, to.x) * kRadToDeg, bodyYaw);
            }
        }
    }
    if (partner != st.rolePartner) {
        st.rolePartner = partner;
        st.role = {};  // a glance kept past its reach was for the one before
    }
    cw::RoleLimits limits;
    limits.glanceDeg = gSettings.glanceDeg;
    limits.glanceKeepDeg = gSettings.glanceKeepDeg;
    cw::RoleAttention const attend = cw::AttendAs(st.role, role, id(st.actor), off, bodyFor(partner), limits);
    ue3::Object* const wanted = attend.look ? partner : nullptr;
    if (role == cw::Role::Bystander && partner != nullptr && off && !attend.look && st.heard != partner) {
        st.heard = partner;
        ++gC.heardOnly;
        log::Info("convo: at {:.1f} s {} (bystander) hears {} {:.0f} deg off its facing, outside a glance ({:.0f} deg): no look, its body stays",
            Seconds(), st.name, partner == player ? "the player" : ShortName(partner), std::abs(*off), gSettings.glanceDeg);
    }
    if (wanted != st.lookWanted) {
        st.lookWanted = wanted;
        st.lookWantedS = nowS;
    }
    if (wanted != st.lookOurs && nowS - st.lookWantedS >= attend.delayS && nowS >= st.lookRetryS) {
        if (wanted == nullptr) {
            // Nothing of ours to take back where the game has since cleared or changed it (game is then null or its own).
            if (game == st.lookOurs) {
                SetLook(st.actor, nullptr);
            }
            st.lookOurs = nullptr;
        } else if (SetLook(st.actor, wanted) && GameLookTarget(st.actor) == wanted) {
            st.lookOurs = wanted;
            ++gC.looksSet;
            if (role == cw::Role::Bystander) {
                ++gC.glances;
            }
            st.heard = nullptr;
            log::Info("convo: at {:.1f} s {} ({}) looks at {}: {:.0f} deg off its body's facing{}", Seconds(), st.name, cw::Name(role),
                wanted == player ? "the player" : ShortName(wanted), off ? std::abs(*off) : 0.0,
                role == cw::Role::Bystander ? " (a glance; its body stays)" : "");
        } else {
            // Refused: a look-at of the game's own holds the pawn's at a higher rank (a cleared authored look easing out).
            st.lookOurs = nullptr;
            st.lookRetryS = nowS + 0.5;
            if (gC.looksRefused++ == 0) {
                log::Info("convo: at {:.1f} s {}'s look at {} was refused by the game (rank {} under a look-at of its own): asked again every "
                          "half second", Seconds(), st.name, wanted == player ? "the player" : ShortName(wanted), gSettings.lookRank);
            }
        }
    }
    // The pair's heads reach further than a bystander's glance.
    if (role != cw::Role::Bystander && st.lookOurs != nullptr) {
        ApplyReach(st);
    } else {
        RestoreReach(st);
    }
    mayTurn = bodyFor(st.lookOurs);
    return st.lookOurs;
}

// The speakers' voices from where they stand (engine/VoicePlace; cw::PlaceVoice): everyone of the conversation but the
// player, each tick of a conversation in the world.
struct VoiceOf {
    cw::VoiceState state;
    int logged = -1;  // -1 nothing yet, 0 far, 1 placed
};
std::map<ue3::Object*, VoiceOf> gVoices;
bool gVoicesSet = false;

void ClearVoices() {
    if (gVoicesSet) {
        engine::voice_place::Set(nullptr, 0);
        gVoicesSet = false;
    }
    gVoices.clear();
}

void PublishVoices(ue3::Object* pawn) {
    if (!gC.on || !gC.world || !gInWorldForStay || !gSettings.voices || !engine::voice_place::Installed()) {
        ClearVoices();
        return;
    }
    cw::VoiceLimits limits;
    limits.withinUnits = gSettings.voiceWithinM * kUnitsPerMeter;
    limits.keepUnits = gSettings.voiceKeepM * kUnitsPerMeter;
    limits.nearUnits = gSettings.voiceNearM * kUnitsPerMeter;
    limits.falloffDb = gSettings.voiceFalloffDb;
    limits.floorDb = gSettings.voiceFloorDb;
    cw::Vec3 const ear{gC.hold.x, gC.hold.y, gC.hold.z};
    engine::voice_place::Voice voices[engine::voice_place::kMaxVoices];
    int count = 0;
    for (Tracked const& t : gC.actors) {
        if (t.actor == nullptr || t.actor == pawn || t.actor == gC.playerPawn || !ue3::runtime::IsAlive(t.actor) || !player::IsBioPawn(t.actor)) {
            continue;
        }
        auto const head = HeadOf(t.actor);
        auto const place = player::Location(t.actor);
        if (!head && !place) {
            continue;
        }
        ik::Vec3 const mouth = head ? head->at : V(*place) + ik::Vec3{0.0, 0.0, 70.0};
        VoiceOf& voice = gVoices[t.actor];
        cw::VoiceAt const at = cw::PlaceVoice(voice.state, ear, {mouth.x, mouth.y, mouth.z}, limits);
        if (voice.logged != (at.placed ? 1 : 0)) {
            if (voice.logged == -1) {
                ++(at.placed ? gC.voicesPlaced : gC.voicesFar);
            }
            voice.logged = at.placed ? 1 : 0;
            if (at.placed) {
                log::Info("convo: at {:.1f} s {}'s voice comes from where it stands ({:.1f} m from the player, {:.1f} dB for the distance)",
                    Seconds(), t.name, at.distanceUnits / kUnitsPerMeter, 20.0 * std::log10(std::max(at.gain, 1e-6)));
            } else {
                log::Info("convo: at {:.1f} s {}'s voice is left to the game's mix ({:.1f} m from the player, past the {:.0f} m a voice is "
                          "placed within: the centre, an intercom)", Seconds(), t.name, at.distanceUnits / kUnitsPerMeter, gSettings.voiceWithinM);
            }
        }
        if (at.placed && count < engine::voice_place::kMaxVoices) {
            engine::voice_place::Voice& v = voices[count++];
            v.actor = t.actor;
            v.at[0] = static_cast<float>(at.at.x);
            v.at[1] = static_cast<float>(at.at.y);
            v.at[2] = static_cast<float>(at.at.z);
            v.gain = static_cast<float>(at.gain) * gSettings.voiceGain;
        }
    }
    engine::voice_place::Set(voices, count);
    gVoicesSet = true;
}

// The conversation the game plays now is the one we keep people for (not the next one, begun in this tick).
bool KeepsTheLiveConversation(ue3::Object* controller) {
    return gC.object != nullptr && ConversationNow(controller) == gC.object;
}

void KeepInPlace(ue3::Object* pawn) {
    if (!gC.on || !gInWorldForStay || !gSettings.stay || gSetLocation.Get() == nullptr || gC.object == nullptr ||
        !ue3::runtime::IsAlive(gC.object)) {
        EndWalks("keeping in place is unavailable");
        gStayMs = 0;
        return;
    }
    std::uint64_t const now = GetTickCount64();
    double const elapsed = gStayMs == 0 ? 0.0 : static_cast<double>(now - gStayMs) / 1000.0;
    double const dt = std::clamp(elapsed, 0.0, 0.1); // cap our own slide/turn step, not the engine's elapsed movement
    gStayMs = now;
    double const nowS = static_cast<double>(now) / 1000.0;
    // Whom each one looks at (the game's own look-at list), else the speaker.
    std::map<ue3::Object*, ue3::Object*> lookAt;
    if (auto const list = gLookAtList.ElementsOf(gC.object, 64)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            ue3::Object* const a = gLookActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr);
            ue3::Object* const b = gLookTarget.Read<ue3::Object*>(list->At(i)).value_or(nullptr);
            if (a != nullptr && b != nullptr) {
                lookAt[a] = b;
            }
        }
    }
    ue3::Object* const speaker = gSpeaker.Read<ue3::Object*>(gC.object).value_or(nullptr);
    // Whom the line is spoken to: the game's listener; a line of someone else's with none is spoken to the player.
    ue3::Object* listener = gListener.Read<ue3::Object*>(gC.object).value_or(nullptr);
    ue3::Object* const playerPawn = gC.playerPawn != nullptr ? gC.playerPawn : pawn;
    if (listener == gC.playerPawn || listener == pawn) {
        listener = playerPawn;
    } else if (listener == nullptr && speaker != nullptr && speaker != pawn && speaker != gC.playerPawn) {
        listener = playerPawn;
    }
    ue3::Object* const speakerNow = speaker == pawn || speaker == gC.playerPawn ? playerPawn : speaker;
    for (Tracked const& t : gC.actors) {
        auto it = std::find_if(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == t.actor; });
        if (t.actor == nullptr || !ue3::runtime::IsAlive(t.actor)) {
            if (it != gStay.end()) EndWalk(*it, "end", "actor no longer alive");
            continue;
        }
        auto const at = player::Location(t.actor);
        auto const rot = player::Rotation(t.actor);
        if (!at || !rot) {
            if (it != gStay.end()) EndWalk(*it, "end", "actor transform lookup failed");
            continue;
        }
        if (it == gStay.end()) {
            Stay st;
            st.actor = t.actor;
            st.name = t.name;
            st.place = V(*at);
            st.shownYaw = YawDeg(*rot);
            gStay.push_back(st);
            continue;
        }
        Stay& st = *it;
        if (!gC.seatWalks.empty() && gSettings.walk == 1 && st.walk.Phase() == cw::WalkPhase::Idle) {
            auto const seat = std::find_if(gC.seatWalks.begin(), gC.seatWalks.end(), [&](Convo::SeatWalk const& w) { return w.actor == st.actor; });
            if (seat != gC.seatWalks.end()) {
                // Walk into its seat at once (the settle time is for the stage's marks, which may bounce).
                st.seatYaw = seat->yawDeg;
                st.walk.Jump(WalkPoint(seat->seat), WalkPoint(st.place), nowS - gSettings.walkSettleS - 0.1, gSettings.walkMinM * kUnitsPerMeter);
                ++gC.seatWalked;
                log::Info("convo: {} walks into a seat in view, {:.1f} m away (the opening had no cut)", st.name, Dist(st.place, seat->seat) / kUnitsPerMeter);
                gC.seatWalks.erase(seat);
            }
        }
        ik::Vec3 const previousKept = st.place;
        double const d = Dist(V(*at), st.place);
        double const unexplained = st.walk.Phase() == cw::WalkPhase::Walking
            ? cw::BeyondWalk(WalkPoint(st.place), WalkPoint(V(*at)), st.walk.Wanted(), st.speedUnitsPerS, elapsed) : d;
        bool const isShepard = t.actor == pawn || t.actor == gC.playerPawn;
        if (!gC.openingDone && gSettings.openingAccept) {
            // The opening: the stage's placements stand (Part A); the kept place follows them.
            if (d >= kJumpUnits) {
                ++gC.openingMoves;
                log::Info("convo: at {:.1f} s the opening put {} {:.2f} m away: accepted", Seconds(), isShepard ? "Shepard" : t.name, d / kUnitsPerMeter);
            }
            st.place = V(*at);
            st.shownYaw = YawDeg(*rot);
            continue;
        }
        if (isShepard && gC.settleUntilMs != 0) {
            // Just put back where he stood after an opening without a cut: the engine settles him onto the floor there (the
            // recorded height was 0.52 m low in Benezia's scene, and holding him to it was a fight with the floor every tick:
            // 465 undone steps, each a drawn jump, 2026-11-10). His kept place is where he comes to rest.
            st.place = V(*at);
            st.shownYaw = YawDeg(*rot);
            continue;
        }
        if (!isShepard && authored_moment::TrackMoves(t.actor)) {
            // An authored walk (a matinee's move track holds this actor): his own move, played as the scene has it. Undone, the
            // track put him back the next tick and the mod undid it again: 240 times for Wrex on Virmire (2026-11-10).
            if (!st.tracked) {
                st.tracked = true;
                ++gC.trackWalks;
                log::Info("convo: at {:.1f} s {} is moved by a matinee's track ({:.2f} m from where it stood): an authored walk, left to it",
                    Seconds(), t.name, d / kUnitsPerMeter);
            }
            EndWalk(st, "cancel", "an authored walk took over");
            st.place = V(*at);
            st.shownYaw = YawDeg(*rot);
            continue;
        }
        if (unexplained >= kJumpUnits && d < kStayMaxUnits) {
            ik::Vec3 const wanted = V(*at);  // capture BEFORE undoing the game's native placement
            if (cw::Insists(st.insist, cw::Vec3{wanted.x, wanted.y, wanted.z}, d)) {
                // The game asks for the same small placement tick after tick (a walk's last step onto its goal, a floor): its
                // own move, and undoing it only repeats the fight (512 times for Lieutenant Durand, 2026-11-10).
                cw::Calm(st.insist);
                ++gC.yielded;
                log::Info("convo: at {:.1f} s the game put {} {:.2f} m away again: its own small move, accepted", Seconds(),
                    isShepard ? "Shepard" : t.name, d / kUnitsPerMeter);
                EndWalk(st, "cancel", "the game insisted on its placement");
                st.place = wanted;
                st.shownYaw = YawDeg(*rot);
                continue;
            }
            bool const moved = PutAt(t.actor, st.place);
            EndWalk(st, "cancel", "a newer placement restarts the wait");
            ++st.kept;
            ++gC.kept;
            log::Info("convo: at {:.1f} s {} kept in place: the game put {} {:.2f} m away{}", Seconds(), isShepard ? "Shepard" : t.name,
                isShepard ? "him" : "it", d / kUnitsPerMeter, moved ? "" : " (SetLocation refused: it stays there)");
            if (!moved) {
                st.place = V(*at);
            }
            st.walk.Jump(WalkPoint(wanted), WalkPoint(st.place), nowS, gSettings.walkMinM * kUnitsPerMeter);
            if (!moved) EndWalk(st, "cancel", "SetLocation refused the undo");
            if (isShepard || gSettings.walk == 0) st.walk.End();  // remember the mark/time, but never walk Shepard
        } else {
            if (d >= kStayMaxUnits) {
                EndWalk(st, "cancel", "a scripted entrance was let through");
                ++gC.letGo;
                log::Info("convo: at {:.1f} s {} moved {:.1f} m at once: a scripted entrance, let through", Seconds(), t.name,
                    d / kUnitsPerMeter);
            }
            st.place = V(*at);  // its own walking, or a jump let through
            cw::Calm(st.insist);
        }
        if (isShepard || gSettings.walk == 0 ||
            (st.walk.Phase() == cw::WalkPhase::Walking && st.requestedMethod != gSettings.walk)) {
            EndWalk(st, "cancel", isShepard ? "the player's pawn stays kept" : "worldWalk changed or disabled");
        }
        if (isShepard || gSetRotation.Get() == nullptr) {
            EndWalk(st, "end", "turn reflection unavailable or player's pawn");
            continue;  // Shepard's body turns under the player's view (body follow)
        }
        // The body's facing (research/69 "Who turns, and how"; cw::StepAttention). The game's look-at chain turns the eyes, head,
        // neck and chest toward whom this one looks at (else the speaker), so the body keeps the facing the scene composed: a
        // facing the stage sets at once mid-conversation is put back (it was for its mark), a turn of the game's own (gradual)
        // is followed, and the body re-faces only when the one it looks at stays outside what the chain covers: by the game's
        // own turn in place (its animation), or, where the game refuses or nothing comes of it, by an eased turn of ours.
        double const actualYaw = YawDeg(*rot);
        double target = actualYaw;
        bool const walking = gSettings.walk != 0 && WalkToMark(st, previousKept, nowS, dt, target);
        // Whom it looks at: the game's own look-at list. (Until 2026-11-10 "else the speaker": the councillors, who look at
        // nobody by that list, swung 90 to 170 degrees toward whichever of them spoke, and so did the dais, a BioInert.)
        auto const look = lookAt.find(t.actor);
        bool const always = gSettings.turnRule == cw::TurnRule::Always;
        ue3::Object* other = look != lookAt.end() ? look->second : always && speaker != t.actor ? speaker : nullptr;
        // By their part in the line (worldTurn = roles): the speaker and the one spoken to look at each other and may turn,
        // a bystander glances at the speaker where a glance reaches and its body stays as the scene composed it.
        bool mayTurn = true;
        if (gSettings.turnRule == cw::TurnRule::Roles && player::IsBioPawn(t.actor)) {
            other = RoleLook(st, look != lookAt.end() ? look->second : nullptr, speakerNow, listener, playerPawn, actualYaw, nowS, mayTurn);
        } else if (st.lookOurs != nullptr) {
            ReleaseLooks("worldTurn is no longer roles");
        }
        std::optional<double> lookBearing;
        if (!walking && other != nullptr && ue3::runtime::IsAlive(other)) {
            bool const atPlayer = other == playerPawn;
            if (auto const o = player::Location(other); o || atPlayer) {
                ik::Vec3 const to = (atPlayer ? gC.hold : V(*o)) - st.place;
                if (std::hypot(to.x, to.y) > 20.0) {
                    lookBearing = std::atan2(to.y, to.x) * kRadToDeg;
                }
            }
        }
        // The body's business only for those whose part lets it turn; a bystander's facing is the scene's.
        std::optional<double> const bearing = mayTurn ? lookBearing : std::nullopt;
        // How far the game's look-at chain really brings the head (research/69 "Who turns, and how"): once per look-at
        // target, a second and a half after it was taken: the target's angle off the body, and off the head's facing.
        if (other != st.lookAt) {
            st.lookAt = other;
            st.lookSinceS = nowS;
            st.lookLogged = false;
        }
        if (other != nullptr && lookBearing && !st.lookLogged && nowS - st.lookSinceS >= 1.5) {
            st.lookLogged = true;
            if (auto const head = HeadOf(t.actor)) {
                bool const atPlayer = other == pawn || other == gC.playerPawn;
                auto const theirs = atPlayer ? std::nullopt : HeadOf(other);
                auto const theirPlace = player::Location(other);
                ik::Vec3 const to = (atPlayer ? gC.hold : theirs ? theirs->at : theirPlace ? V(*theirPlace) + ik::Vec3{0.0, 0.0, 70.0} : gC.hold) - head->at;
                double const bodyOff = std::abs(YawDiffDeg(*lookBearing, actualYaw));
                double const headOff = AngleDeg(head->axis[0], to);
                gC.reachMaxBody = std::max(gC.reachMaxBody, bodyOff);
                if (bodyOff >= 30.0) {
                    ++gC.reachSamples;
                    gC.reachWorstHead = std::max(gC.reachWorstHead, headOff);
                }
                log::Info("convo: at {:.1f} s {} looks at {}: {:.0f} deg off its body's facing, its head {:.0f} deg off after 1.5 s", Seconds(),
                    t.name, atPlayer ? "the player" : ShortName(other), bodyOff, headOff);
            }
        }
        bool eased = false;
        if (walking) {
            st.attention = {};
            st.turnUntilS = 0.0;
        } else if (gSettings.turnRule == cw::TurnRule::Off || (!always && !player::IsBioPawn(t.actor))) {
            st.attention = {};
            st.turnUntilS = 0.0;
            st.shownYaw = actualYaw;
            continue;
        } else if (gSettings.turnRule == cw::TurnRule::Always) {
            if (bearing) {
                target = *bearing;
            }
        } else {
            double const moved = std::abs(YawDiffDeg(actualYaw, st.shownYaw));
            bool const gradual = moved <= std::max(2.0, 360.0 * dt);  // the game turning it, not a facing set at once
            if (st.turnUntilS != 0.0) {
                bool const arrived = std::abs(YawDiffDeg(st.turnGoal, actualYaw)) <= 3.0;
                bool const still = nowS - st.turnAskedS > 0.75 && std::abs(YawDiffDeg(actualYaw, st.turnFromYaw)) < 1.0;
                if (!arrived && !still && nowS < st.turnUntilS) {
                    st.shownYaw = actualYaw;  // the game's turn is on its way: the body is its own
                    continue;
                }
                if (!arrived) {
                    ++gC.turnsSlid;
                    --gC.gameTurns;
                    ++gGameTurnFails;
                    log::Info("convo: at {:.1f} s {}'s turn by the game {} ({:.0f} deg of {:.0f} turned in {:.1f} s): the rest by an eased turn",
                        Seconds(), t.name, still ? "did not start" : "did not arrive", std::abs(YawDiffDeg(actualYaw, st.turnFromYaw)),
                        std::abs(YawDiffDeg(st.turnGoal, st.turnFromYaw)), nowS - st.turnAskedS);
                }
                st.turnUntilS = 0.0;
                st.shownYaw = actualYaw;
            } else if (gradual) {
                st.shownYaw = actualYaw;
            } else if (!bearing || std::abs(YawDiffDeg(*bearing, actualYaw)) <= gSettings.turnComfortDeg) {
                // The stage's facing for someone whose body is not ours to turn (nobody to look at, a bystander), or one that
                // leaves the one it looks at within the look-at chain's reach: the scene's own (Udina turning to the council),
                // but set at once for a camera cut: taken as a turn (180 degrees in a frame, seen in first person).
                st.facingGoal = actualYaw;
                st.away = false;  // the scene's own new facing: there is nothing of ours to go back from
                ++gC.facingsEased;
            } else {
                ++gC.facingsHeld;
            }
            if (bearing && st.attention.turning) {
                st.facingGoal.reset();
            }
            cw::AttentionLimits const limits{gSettings.turnComfortDeg, gSettings.turnSettleDeg, gSettings.turnDwellS};
            cw::AttentionStep const a = cw::StepAttention(st.attention, st.shownYaw, bearing, dt, limits);
            if (bearing) {
                st.engagedS = nowS;
            } else if (st.away && st.homeYaw && st.turnUntilS == 0.0 && gSettings.turnHomeS >= 0.0f && nowS - st.engagedS >= gSettings.turnHomeS) {
                // Its lines with the player are over: back to the facing the scene composed.
                st.away = false;
                st.facingGoal = *st.homeYaw;
                ++gC.turnsHome;
                log::Info("convo: at {:.1f} s {} goes back to the facing the scene composed ({:.0f} deg, {:.0f} deg of turn): its last line with "
                          "the player ended {:.1f} s ago", Seconds(), t.name, *st.homeYaw, std::abs(YawDiffDeg(*st.homeYaw, st.shownYaw)),
                    nowS - st.engagedS);
            }
            if (a.began) {
                if (!st.away) {
                    st.homeYaw = st.shownYaw;
                    st.away = true;
                }
                ++gC.bodyTurns;
                bool const asked = gSettings.turnWay != 2 && gGameTurnFails < 2 && RequestTurn(t.actor, *rot, a.goalYawDeg);
                log::Info("convo: at {:.1f} s {} turns its body: {} has been {:.0f} deg off its facing for {:.1f} s, past the {:.0f} its look-at "
                          "covers; to {:.0f} deg ({:.0f} deg of turn), {}", Seconds(), t.name, ShortName(other), std::abs(a.offDeg),
                    gSettings.turnDwellS, gSettings.turnComfortDeg, a.goalYawDeg, std::abs(YawDiffDeg(a.goalYawDeg, st.shownYaw)),
                    asked ? gSettings.turnWay == 0 ? "by its own desired rotation" : "by the game's turn in place"
                          : gSettings.turnWay == 2 ? "by an eased turn (worldTurnWay = slide)"
                          : gGameTurnFails >= 2 ? "by an eased turn (the game's turn came to nothing twice)"
                          : "by an eased turn (the game's turn refused)");
                if (asked) {
                    ++gC.gameTurns;
                    st.turnGoal = a.goalYawDeg;
                    st.turnFromYaw = actualYaw;
                    st.turnAskedS = nowS;
                    st.turnUntilS = nowS + 3.0;
                    continue;
                }
                ++gC.turnsSlid;
            }
            target = a.turning ? a.goalYawDeg : st.facingGoal ? *st.facingGoal : st.shownYaw;
            if (st.facingGoal && std::abs(YawDiffDeg(*st.facingGoal, st.shownYaw)) < 1.0) {
                st.facingGoal.reset();
            }
            eased = true;
        }
        if (gSettings.turnLog && nowS >= st.traceS && (other != nullptr || st.attention.turning || st.facingGoal)) {
            st.traceS = nowS + 0.5;
            log::Info("convo: turn trace: at {:.1f} s {} yaw {:.0f} (kept {:.0f}), looks at {} {:.0f} deg off, {}{}{}", Seconds(), t.name, actualYaw,
                st.shownYaw, other == nullptr ? "nobody" : other == playerPawn ? "the player" : ShortName(other),
                lookBearing ? std::abs(YawDiffDeg(*lookBearing, actualYaw)) : 0.0, mayTurn ? "may turn" : "body stays",
                st.attention.turning ? ", turning" : "", st.facingGoal ? ", easing to the stage's facing" : "");
        }
        double const diff = YawDiffDeg(target, st.shownYaw);
        double const step = gSettings.turnDegPerS * dt;
        st.shownYaw += eased ? cw::TurnStepDeg(diff, dt, gSettings.turnDegPerS) : std::clamp(diff, -step, step);
        // (A threshold of a degree here stopped an eased turn half way at 90 frames a second: its steps near the end are
        // smaller, were not applied, and the kept facing started over from the body's every tick. 2026-11-10.)
        if (std::abs(YawDiffDeg(st.shownYaw, actualYaw)) > 0.05) {
            ue3::Params p(ue3::runtime::Get(), gSetRotation.Get());
            ue3::Rotator r = *rot;
            r.yaw = ToUnits(st.shownYaw);
            if (p.Valid() && p.Set("NewRotation", r)) {
                ue3::runtime::Call(t.actor, gSetRotation.Get(), p);
            }
            SetDesired(t.actor, *rot, st.shownYaw, false);  // else the pawn's physics turns it back within the tick
        }
    }
}
std::uint32_t gConversations = static_cast<std::uint32_t>(GetTickCount64() & 0xFFFF);
bool gHeld = false;          // view_anchor::Hold set by us
bool gPublished = false;
bool gInWorld = false;
std::uint64_t gNextSampleMs = 0;
std::uint64_t gWorldTicks = 0;

std::mutex gStatusMutex;
std::string gStatusJson = "{\"on\": false}";

double Seconds() {
    return gC.startMs == 0 ? 0.0 : static_cast<double>(GetTickCount64() - gC.startMs) / 1000.0;
}

void Track(ue3::Object* actor) {
    if (actor == nullptr || !ue3::runtime::IsAlive(actor)) {
        return;
    }
    for (Tracked const& t : gC.actors) {
        if (t.actor == actor) {
            return;
        }
    }
    auto const at = player::Location(actor);
    auto const rot = player::Rotation(actor);
    if (!at || !rot) {
        return;
    }
    Tracked t;
    t.actor = actor;
    t.name = ShortName(actor);
    t.start = t.logged = V(*at);
    t.startYaw = t.loggedYaw = YawDeg(*rot);
    if (auto const head = HeadOf(actor)) {
        t.headStart = head->at;
    }
    gC.actors.push_back(t);
    log::Info("convo: actor {} at {} facing {:.0f} deg{}", t.name, F(t.start), t.startYaw,
        t.headStart ? std::format(", head bone at {} ({:.2f} m above its location)", F(*t.headStart), (t.headStart->z - t.start.z) / kUnitsPerMeter)
                    : std::string(", no head bone"));
}

ue3::Object* ConversationNow(ue3::Object* controller) {
    ue3::Object* const info = gActorWorldInfo.Read<ue3::Object*>(controller).value_or(nullptr);
    ue3::Object* const conv = info != nullptr ? gCurrentConversation.Read<ue3::Object*>(info).value_or(nullptr) : nullptr;
    return conv != nullptr && ue3::runtime::IsAlive(conv) ? conv : nullptr;
}

std::string Utf8(ue3::FString const* s) {
    std::string out;
    if (s == nullptr) {
        return out;
    }
    for (wchar_t const* p = s->Chars(); *p != L'\0' && out.size() < 120; ++p) {
        out.push_back(*p < 128 && *p >= 32 ? static_cast<char>(*p) : '?');
    }
    return out;
}

// The conversation movie's handler, from the GUI manager's panels.
ue3::Object* WheelHandler(ue3::Object* controller) {
    ue3::Object* const manager = gCachedManager.Read<ue3::Object*>(controller).value_or(nullptr);
    auto const* const panels = manager != nullptr ? gPanels.In<ue3::TArray<ue3::Object*>>(manager) : nullptr;
    if (panels == nullptr || panels->data == nullptr || panels->count <= 0 || panels->count > 64) {
        return nullptr;
    }
    for (std::int32_t i = 0; i < panels->count; ++i) {
        ue3::Object* const panel = panels->data[i];
        if (panel == nullptr || !ue3::runtime::IsAlive(panel)) {
            continue;
        }
        ue3::Object* const handler = gPanelHandler.Read<ue3::Object*>(panel).value_or(nullptr);
        if (handler != nullptr && ue3::IsA(handler, gConvHandlerClass) && ue3::runtime::IsAlive(handler)) {
            return handler;
        }
    }
    return nullptr;
}

// The camera cut to a new shot: what it shows against Shepard's place. Every actor of the conversation whose face is inside the
// frame (the lens's horizontal angle, 16:9) is listed; the nearest of them is the shot's subject.
void LogShot(ue3::Object* pawn, ik::Vec3 eyes) {
    ik::Vec3 const cam = V(gPov.lastLocation);
    ik::Vec3 const fwd = Forward(gPov.lastRotation);
    ik::Vec3 const right = YawRight(YawDeg(gPov.lastRotation));
    ik::Vec3 const up = ik::Normalize(ik::Cross(right, fwd), {0.0, 0.0, 1.0});
    double const halfH = std::clamp(static_cast<double>(gPov.lastFov), 1.0, 170.0) * 0.5 / kRadToDeg;
    double const halfV = std::atan(std::tan(halfH) * 9.0 / 16.0);
    Tracked const* subject = nullptr;
    double nearest = 1e9;
    ik::Vec3 subjectFace{};
    bool shepardInFrame = false;
    double shepardFromCamera = 0.0;
    std::string inFrame;
    for (Tracked const& t : gC.actors) {
        auto const face = FacePoint(t.actor);
        if (!face) {
            continue;
        }
        ik::Vec3 const d = *face - cam;
        double const along = ik::Dot(d, fwd);
        if (along <= 10.0 || std::abs(std::atan2(ik::Dot(d, right), along)) > halfH || std::abs(std::atan2(ik::Dot(d, up), along)) > halfV) {
            continue;
        }
        double const distance = ik::Length(d);
        inFrame += std::format("{}{} {:.1f} m", inFrame.empty() ? "" : ", ", t.actor == pawn ? "Shepard" : t.name, distance / kUnitsPerMeter);
        if (t.actor == pawn) {
            shepardInFrame = true;
            shepardFromCamera = distance;
        }
        if (distance < nearest) {
            nearest = distance;
            subject = &t;
            subjectFace = *face;
        }
    }
    double const camToEyes = Dist(cam, eyes) / kUnitsPerMeter;
    // A director's camera far from Shepard (a boss's entrance, a wide shot of the room) frames what his eyes can't: the survey's
    // "framed" scenes, which first person shows from a place the scene wasn't composed for.
    // (During a walk-in the scene is composed for Shepard's mark, where the player is on his way to: measured from there, or
    // every shot of a scene he has not walked into yet counts as far.)
    constexpr double kFarCameraM = 8.0;
    double const camToPlace = gC.wantedMark ? Dist(cam, *gC.wantedMark + ik::Vec3{0.0, 0.0, 60.0}) / kUnitsPerMeter : camToEyes;
    if (camToPlace > kFarCameraM) {
        ++gC.shotsFar;
    }
    gC.cameraFarM = std::max(gC.cameraFarM, camToPlace);
    bool const subjectIsShepard = subject != nullptr && subject->actor == pawn;
    std::string kind;
    std::string key;
    std::string seen;
    if (subject == nullptr) {
        kind = "a cutaway (no actor of the conversation in the frame)";
        key = "cutaway";
        ++gC.shotsHidden;
    } else if (camToEyes < 0.4) {
        kind = "from Shepard's eyes";
        key = "from Shepard's eyes";
    } else if (subjectIsShepard) {
        kind = "on Shepard (a reverse shot: his own face, not seen from his eyes)";
        key = "on Shepard";
        ++gC.shotsHidden;
    } else {
        bool const visible = LineOfSight(pawn, eyes, subjectFace);
        double const offAxis = AngleDeg(subjectFace - cam, subjectFace - eyes);
        bool const shoulder = shepardInFrame && shepardFromCamera < 150.0;
        kind = shoulder ? "over Shepard's shoulder onto " + subject->name : "on " + subject->name;
        key = shoulder ? "over Shepard's shoulder" : "on an NPC";
        seen = std::format(", {} from Shepard's eyes ({:.1f} m away, the camera {:.0f} deg off his line to it)",
            visible ? "visible" : "BLOCKED", Dist(eyes, subjectFace) / kUnitsPerMeter, offAxis);
        if (!visible) {
            ++gC.shotsHidden;
        }
    }
    ++gC.shotKinds[key];
    log::Info("convo: shot {} at {:.1f} s: {}{}; in frame: {}; the camera at {} ({:.2f} m from Shepard's eyes), {:.0f} deg lens; speaker {}",
        gC.shots, Seconds(), kind, seen, inFrame.empty() ? "nobody" : inFrame, F(cam), camToEyes, gPov.lastFov, ShortName(gC.speaker));
}

// The floor under a point: a trace straight down from 1 m above to 3 m below it (level geometry only). The seat's z is the floor
// plus the actor's own height above its floor.
std::optional<double> FloorZ(ue3::Object* tracer, ik::Vec3 const& at) {
    ue3::Function* const fn = gTrace.Get();
    if (fn == nullptr || tracer == nullptr) {
        return std::nullopt;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    if (!p.Valid()) {
        return std::nullopt;
    }
    p.Set("TraceStart", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z + 100.0)});
    p.Set("TraceEnd", ue3::Vector{static_cast<float>(at.x), static_cast<float>(at.y), static_cast<float>(at.z - 300.0)});
    p.SetBool("bTraceActors", false);
    if (!ue3::runtime::Call(tracer, fn, p) || p.Return<ue3::Object*>().value_or(nullptr) == nullptr) {
        return std::nullopt;
    }
    auto const hit = p.Get<ue3::Vector>("HitLocation");
    return hit ? std::optional<double>(hit->z) : std::nullopt;
}

bool FaceYaw(ue3::Object* actor, double yawDeg) {
    ue3::Function* const fn = gSetRotation.Get();
    if (fn == nullptr) {
        return false;
    }
    ue3::Params p(ue3::runtime::Get(), fn);
    return p.Valid() && p.Set("NewRotation", ue3::Rotator{0, ToUnits(yawDeg), 0}) && ue3::runtime::Call(actor, fn, p);
}

// The end of the opening (research/59 "Part A" and "Part B"): the player's place is taken from Shepard's mark (his head bone, the
// first-person eye offset), facing the owner; then everyone of the conversation and the squad is checked from there (within
// worldViewDeg of the facing, 1.2 m to 6 m away, a clear line of sight) and whoever fails is put on a seat of an arc around the
// player, centred on the owner, facing the player. The host dips from black on `cut`, so both moves read as a cut, not a teleport.
// The opening without a cut (worldOpening = auto, research/69 "Authored scenes"): where the stage put Shepard against where
// the player stood on foot. Near, or within a walk in the same space: Shepard is put back where he stood, the player's view
// stays the one he had, nobody is arranged and nothing dips; his mark is where he is wanted and he walks there himself.
// Returns false for another composition (the cut below).
// Shepard's pawn hidden while the view walks to him (the player would see his own body standing at the mark, and arm IK
// would draw its arms from there to the hands), shown again on arrival and at the conversation's end. Only if the game had
// him shown: a pawn the scene hides stays the scene's.
void HidePawn(ue3::Object* pawn, bool hide) {
    if (gSettings.bodyWithPlayer && !gC.hidPawn) {
        return;  // not hidden: his body goes with the player instead (BodyAway, the body follower)
    }
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn) || gSetHidden.Get() == nullptr) {
        return;
    }
    bool const hidden = gActorHidden.ReadBool(pawn).value_or(false);
    if (hide ? hidden : !gC.hidPawn) {
        return;
    }
    ue3::Params p(ue3::runtime::Get(), gSetHidden.Get());
    if (p.Valid() && p.SetBool("bNewHidden", hide) && ue3::runtime::Call(pawn, gSetHidden.Get(), p)) {
        gC.hidPawn = hide;
    }
}

// When a moment's first-person view last ended, and whether the script still wanted Shepard somewhere he had not reached.
struct MomentEnded {
    std::uint64_t ms = 0;
    bool pending = false;
};
MomentEnded gMomentEnded;

// The places Shepard's pawn passed through while the player's view was elsewhere (a moment's track, the script's moves, the
// stage's mark): the guided walk follows them (cw::TrailTarget). From a moment's start to the view's arrival.
std::vector<cw::Vec3> gTrail;
void TrailAdd(ik::Vec3 const& at) {
    if (gTrail.empty() || std::hypot(gTrail.back().x - at.x, gTrail.back().y - at.y) >= 40.0) {
        gTrail.push_back({at.x, at.y, at.z});
    }
}

bool OpeningWithoutCut(ue3::Object* conv, ue3::Object* pawn) {
    cw::OpeningFacts facts;
    auto const markAt = player::Location(pawn);
    bool const footFresh = gFoot.have && gC.startMs >= gFoot.ms && gC.startMs - gFoot.ms < 5000;
    facts.known = footFresh && markAt.has_value();
    ik::Vec3 const stood = V(gFoot.pawn);
    ik::Vec3 const eyes = V(gFoot.location);
    if (facts.known) {
        ik::Vec3 const mark = V(*markAt);
        facts.markUnits = std::hypot(mark.x - stood.x, mark.y - stood.y);
        facts.riseUnits = mark.z - stood.z;
        auto const head = HeadOf(pawn);
        facts.sightToMark = InSight(pawn, eyes, head ? head->at : mark + ik::Vec3{0.0, 0.0, 60.0});
        facts.floorAtMark = FloorZ(pawn, mark).has_value();
        if (auto const hidden = gActorHidden.ReadBool(pawn)) {
            facts.shepardHidden = *hidden;
        }
        // The people the player was looking at: how far the stage moved them (the conversation saved their places first).
        if (auto const saved = gSavedPositions.ElementsOf(conv, 64)) {
            double const facing = YawDeg(gFoot.rotation);
            for (std::int32_t i = 0; i < saved->count; ++i) {
                ue3::Object* const a = gSavedActor.Read<ue3::Object*>(saved->At(i)).value_or(nullptr);
                auto const was = gSavedPos.Read<ue3::Vector>(saved->At(i));
                auto const now = a != nullptr && a != pawn && ue3::runtime::IsAlive(a) ? player::Location(a) : std::nullopt;
                if (was && now && cw::InView({eyes.x, eyes.y, eyes.z}, facing, {was->x, was->y, was->z}, gSettings.viewDeg, 0.0, 3000.0)) {
                    facts.peopleMovedUnits = std::max(facts.peopleMovedUnits, Dist(V(*now), V(*was)));
                }
            }
        }
    }
    // A conversation that begins while a moment's walk is still on its way to where the script wanted Shepard must not
    // have him put back. The council's hearing begins the moment he crosses into the chamber's streaming volume, 2.3 m
    // into the walk the script had done by a teleport; putting him back from the podium then ended the hearing in its
    // first tick in 9 of 16 runs (whenever the conversation came three ticks or more after that touch: where he was put
    // made no difference), and in none of the 4 that left him where the stage had him (2026-11-10, research/69 "The
    // hearing that ended at once"). So such an opening is walked by the view (below); with worldWalkInWay = body it is
    // the stage's placement (a cut).
    // ... or while a matinee's track still moves him (Chellik's bump on the Citadel, 2026-11-11: the conversation began half
    // a second before the track let him go, the stay opening put the view at his last place on foot, and the track carried
    // his body 1.7 m on).
    bool const trackMoving = authored_moment::TrackMoves(pawn);
    bool const midMoment = (gMomentEnded.pending && GetTickCount64() - gMomentEnded.ms < 1000) || trackMoving;
    if (trackMoving) {
        log::Info("convo: the opening: a matinee's track still moves Shepard: taken as a moment on its way");
    }
    cw::Opening opening = cw::DecideOpening(facts, gSettings.opening);
    if (midMoment && gSettings.walkInWay == cw::WalkInRule::Body && opening != cw::Opening::Cut) {
        opening = cw::Opening::Cut;
        log::Info("convo: the opening: a moment's walk was still on its way to where the script wanted Shepard and the walk-in moves "
                  "his body ([Cinematics] worldWalkInWay = body): the stage's placement stands (a cut), he is not put back");
    }
    gC.opening = opening;
    gC.markM = facts.markUnits / kUnitsPerMeter;
    log::Info("convo: the opening ({}): Shepard's mark is {:.2f} m from where the player stood ({:+.2f} m up), {}, {}, people in view moved "
              "at most {:.2f} m{}: {}", cw::Name(gSettings.opening), facts.markUnits / kUnitsPerMeter, facts.riseUnits / kUnitsPerMeter,
        facts.sightToMark ? "in sight" : "out of sight", facts.floorAtMark ? "a floor there" : "no floor there",
        facts.peopleMovedUnits / kUnitsPerMeter, facts.known ? "" : " (no fresh place on foot)", cw::Name(opening));
    if (opening == cw::Opening::Cut || !facts.known) {
        return false;
    }
    // Shepard back where he stood, facing as the player did; the view the one on foot. (His head bone's place is read
    // before the move: read after it, it already has the move in it, and the follower below took the difference for a jump
    // of his and moved the view to the mark after all, 2026-11-09.)
    ik::Vec3 const mark = V(*markAt);
    auto const headAtMark = HeadOf(pawn);
    // He goes back at the height he stands at on that floor (as high above it as he is above the mark's floor), not at
    // the recorded height: a record taken while he hung in the air or sank (a test's placement) put him half a metre into
    // the floor, and the engine lifting him out was a jump in a drawn frame. The view goes with that correction.
    ik::Vec3 back = stood;
    auto const floorMark = FloorZ(pawn, mark);
    if (auto const floorHere = FloorZ(pawn, stood); floorHere && floorMark) {
        back.z = *floorHere + (mark.z - *floorMark);
    }
    // Who walks: the two together (he is put back under the player), or the view alone (cw::DecideWalkIn).
    cw::WalkInFacts way;
    way.midMoment = midMoment;
    std::string behind;
    if (opening == cw::Opening::WalkIn && !midMoment && gSettings.walkInWay == cw::WalkInRule::Auto) {
        ik::Vec3 const extent = PawnExtent(pawn);
        for (TriggerNear const& t : TriggersNear(mark, 150.0)) {
            if (Overlaps(t, mark, extent) && !Overlaps(t, back, extent)) {
                way.markBehindTrigger = true;
                behind = t.name;
                break;
            }
        }
    }
    if (cw::DecideWalkIn(way, gSettings.walkInWay) == cw::WalkInWay::View) {
        // Nothing of Shepard's is moved: he stands where the stage has him, hidden, and the player's view stays where it
        // was. The walk (MovePlayer) moves the view to him and makes the two one again there.
        for (Stay& st : gStay) {
            if (st.actor == pawn) {
                st.place = mark;
            }
        }
        gC.hold = eyes + (back - stood);
        gC.holdRotation = gFoot.rotation;
        gC.holdFov = gFoot.fov;
        gC.holdFrom = "the view on foot";
        if (headAtMark) {
            gC.anchorHead = headAtMark->at;
        }
        gC.settleUntilMs = 0;
        gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, gC.anchorHead ? cw::Vec3{gC.anchorHead->x, gC.anchorHead->y, gC.anchorHead->z} : cw::Vec3{}, 0};
        gC.detached = true;
        gC.detachedAt = back;
        gC.wantedMark = mark;
        TrailAdd(mark);  // the guided walk follows the way he went, then to the mark
        gC.walkInWay = "view";
        HidePawn(pawn, true);
        log::Info("convo: the opening: no cut and nothing moved; the player stays at {} facing {:.0f} deg and his view walks to Shepard's mark {} "
                  "({:.2f} m); Shepard stands there{} ({})", F(gC.hold), YawDeg(gC.holdRotation), F(mark),
            std::hypot(mark.x - back.x, mark.y - back.y) / kUnitsPerMeter, gC.hidPawn ? ", hidden until the player arrives" : "",
            midMoment ? "a moment's walk was still on its way: putting him back ended the council's hearing"
                      : way.markBehindTrigger ? std::format("his mark stands inside {}, which a walk of his body would stop at", behind)
                                              : std::string("[Cinematics] worldWalkInWay = view"));
        return true;
    }
    gC.walkInWay = opening == cw::Opening::WalkIn ? "body" : "-";
    ik::Vec3 const lift = back - stood;
    bool const put = PutAt(pawn, back);
    FaceYaw(pawn, YawDeg(gFoot.rotation));
    for (Stay& st : gStay) {
        if (st.actor == pawn) {
            st.place = put ? back : mark;
            st.shownYaw = YawDeg(gFoot.rotation);
        }
    }
    if (!put) {
        log::Info("convo: the opening: Shepard could not be put back where he stood (SetLocation refused): the cut after all");
        gC.opening = cw::Opening::Cut;
        return false;
    }
    gC.hold = eyes + lift;
    gC.holdRotation = gFoot.rotation;
    gC.holdFov = gFoot.fov;
    gC.holdFrom = "the view on foot";
    if (headAtMark) {
        gC.anchorHead = headAtMark->at + (back - mark);
    }
    // His head bone is no measure for a moment: the mesh was up to 1.3 m from the pawn when the stage had just placed him
    // (body follow, a pose not yet updated), and the follower read its catching up as a jump of his and moved the view
    // 0.8 m (2026-11-09). For a second and a half the anchor just takes the bone's place.
    gC.settleUntilMs = GetTickCount64() + 1500;
    gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, gC.anchorHead ? cw::Vec3{gC.anchorHead->x, gC.anchorHead->y, gC.anchorHead->z} : cw::Vec3{}, 0};
    if (opening == cw::Opening::WalkIn) {
        gC.wantedMark = mark;
    }
    log::Info("convo: the opening: no cut; the player stays at {} facing {:.0f} deg{}", F(gC.hold), YawDeg(gC.holdRotation),
        opening == cw::Opening::WalkIn ? std::format("; he is wanted at Shepard's mark {} and walks there himself", F(mark)) : std::string());
    return true;
}

// An opening without a cut (item 7): the conversation's people out of the player's view are given seats in view (the arc, facing
// the group's middle), which they walk to with the game's AI when their walk state exists (the tick's loop). Nobody jumps.
void PlanSeatWalks(ue3::Object* conv, ue3::Object* pawn) {
    gC.seatWalks.clear();
    if (!gSettings.arrange || gSettings.walk != 1) {
        return;
    }
    ue3::Object* const owner = gOwner.Read<ue3::Object*>(conv).value_or(nullptr);
    auto const ownerAt = owner != nullptr && owner != pawn ? FacePoint(owner) : std::nullopt;
    double const facing = YawDeg(gC.holdRotation);
    std::vector<std::pair<ue3::Object*, std::string>> out;
    for (Tracked const& t : gC.actors) {
        if (t.actor == nullptr || t.actor == pawn || t.actor == owner || !ue3::runtime::IsAlive(t.actor) ||
            gActorHidden.ReadBool(t.actor).value_or(false)) {
            continue;  // a hidden person isn't in the scene to be seen (nor does the game's AI walk it)
        }
        auto const at = player::Location(t.actor);
        auto const face = FacePoint(t.actor);
        if (!at || !face) {
            continue;
        }
        bool const inView = cw::InView({gC.hold.x, gC.hold.y, gC.hold.z}, facing, {at->x, at->y, at->z}, gSettings.viewDeg, 120.0, 600.0);
        if (!(inView && LineOfSight(pawn, gC.hold, *face))) {
            out.emplace_back(t.actor, t.name);
        }
    }
    if (out.empty()) {
        return;
    }
    ik::Vec3 const centre = ownerAt ? *ownerAt : gC.hold + YawForward(facing) * 250.0;
    cw::Vec3 const player{gC.hold.x, gC.hold.y, gC.hold.z};
    std::vector<cw::ArcSeat> seats = cw::ArcSeats(player, {centre.x, centre.y, centre.z}, static_cast<int>(out.size()),
        gSettings.arcM * kUnitsPerMeter, 25.0, gSettings.viewDeg);
    cw::Vec3 const middle = cw::GroupMiddle(player, {centre.x, centre.y, centre.z}, seats);
    if (gSettings.faceMiddle) {
        cw::FaceToward(seats, middle);
    }
    for (std::size_t i = 0; i < out.size() && i < seats.size(); ++i) {
        auto const at = player::Location(out[i].first);
        ik::Vec3 seat{seats[i].at.x, seats[i].at.y, at ? at->z : gC.hold.z};
        auto const floorHere = at ? FloorZ(pawn, V(*at)) : std::nullopt;
        auto const floorThere = FloorZ(pawn, seat);
        if (!floorThere) {
            log::Info("convo: no floor under a seat for {}: it stays out of view", out[i].second);
            continue;
        }
        seat.z = *floorThere + (floorHere && at ? at->z - *floorHere : 0.0);
        if (at && Dist(V(*at), seat) > gSettings.seatWalkMaxM * kUnitsPerMeter) {
            log::Info("convo: {} is out of view, {:.1f} m from a seat: too far to walk in (worldSeatWalkMaxM {:.0f}); it stays", out[i].second,
                Dist(V(*at), seat) / kUnitsPerMeter, gSettings.seatWalkMaxM);
            continue;
        }
        gC.seatWalks.push_back({out[i].first, seat, seats[i].yawDeg});
        log::Info("convo: {} is out of view; seat {} at {} ({:.1f} m from you) to walk to", out[i].second, i + 1, F(seat),
            Dist(seat, gC.hold) / kUnitsPerMeter);
    }
}

void FinishOpening(ue3::Object* conv, ue3::Object* pawn) {
    gC.openingDone = true;
    if (gSettings.opening == cw::OpeningRule::Auto && OpeningWithoutCut(conv, pawn)) {
        PlanSeatWalks(conv, pawn);
        return;
    }
    ue3::Object* const owner = gOwner.Read<ue3::Object*>(conv).value_or(nullptr);
    auto const ownerAt = owner != nullptr && owner != pawn ? FacePoint(owner) : std::nullopt;
    // Part A: the player at Shepard's mark.
    auto const head = HeadOf(pawn);
    auto const rot = player::Rotation(pawn);
    ik::Vec3 const before = gC.hold;
    if (head) {
        double const yaw = ownerAt ? std::atan2(ownerAt->y - head->at.y, ownerAt->x - head->at.x) * kRadToDeg : rot ? YawDeg(*rot) : 0.0;
        ik::Vec3 const& e = gFoot.eyeFromHead;
        gC.hold = head->at + YawForward(yaw) * e.x + YawRight(yaw) * e.y + ik::Vec3{0.0, 0.0, e.z};
        gC.holdRotation = {0, ToUnits(yaw), 0};
        gC.holdFrom = "Shepard's mark (the opening accepted)";
        gC.anchorHead = head->at;
        gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, {head->at.x, head->at.y, head->at.z}, 0};
        auto const view = vr::host_view::Latest();
        if (view.valid && view.floorLocked) {
            auto const basis = engine::ToBasis(engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll});
            gC.hold.z = view.centreZ - (basis.forward.z * view.offsetForward + basis.right.z * view.offsetRight + basis.up.z * view.offsetUp);
        }
    }
    double const moved = Dist(before, gC.hold) / kUnitsPerMeter;
    log::Info("convo: the opening ended after {:.1f} s: {} placements accepted; the player's place {} at {} facing {:.0f} deg{} ({:.2f} m from where it was)",
        Seconds(), gC.openingMoves, head ? "re-placed" : "kept (no head bone)", F(gC.hold), YawDeg(gC.holdRotation),
        ownerAt ? std::format(", toward {}", ShortName(owner)) : std::string(), moved);
    // Part B: everyone in view.
    if (!gSettings.arrange) {
        if (moved > 0.05) ++gC.cut;
        return;
    }
    double const facing = YawDeg(gC.holdRotation);
    double const halfAngle = gSettings.viewDeg;
    double const minUnits = 120.0, maxUnits = 600.0;
    std::vector<std::pair<ue3::Object*, std::string>> people;
    for (Tracked const& t : gC.actors) {
        if (t.actor != nullptr && t.actor != pawn && ue3::runtime::IsAlive(t.actor)) {
            people.emplace_back(t.actor, t.name);
        }
    }
    std::vector<ue3::Object*> squadOnly;  // squadmates who aren't people of the conversation: beside the player (item 7)
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) {
        if (m.pawn != nullptr && ue3::runtime::IsAlive(m.pawn) &&
            std::none_of(people.begin(), people.end(), [&](auto const& pr) { return pr.first == m.pawn; })) {
            people.emplace_back(m.pawn, m.name.empty() ? ShortName(m.pawn) : m.name);
            squadOnly.push_back(m.pawn);
        }
    }
    std::vector<std::pair<ue3::Object*, std::string>> out;
    std::vector<std::pair<ue3::Object*, std::string>> outSquad;
    for (auto const& [actor, name] : people) {
        if (actor == owner) {
            continue;  // the owner keeps its mark: the arc is centred on it
        }
        auto const at = player::Location(actor);
        auto const face = FacePoint(actor);
        if (!at || !face) {
            continue;
        }
        cw::Vec3 const p{at->x, at->y, at->z};
        bool const inView = cw::InView({gC.hold.x, gC.hold.y, gC.hold.z}, facing, p, halfAngle, minUnits, maxUnits);
        bool const seen = inView && LineOfSight(pawn, gC.hold, *face);
        if (!seen) {
            double const bearing = std::atan2(at->y - gC.hold.y, at->x - gC.hold.x) * kRadToDeg;
            double off = bearing - facing;
            while (off > 180.0) off -= 360.0;
            while (off < -180.0) off += 360.0;
            log::Info("convo: {} is {} ({:.1f} m away, {:.0f} deg off your facing)", name, !inView ? (std::abs(off) > halfAngle ? "out of view" : "too near or too far") : "blocked",
                Dist(V(*at), gC.hold) / kUnitsPerMeter, off);
            bool const squad = gSettings.squadBeside && std::find(squadOnly.begin(), squadOnly.end(), actor) != squadOnly.end();
            (squad ? outSquad : out).emplace_back(actor, name);
        }
    }
    if (!out.empty() || !outSquad.empty()) {
        ik::Vec3 const centre = ownerAt ? *ownerAt : gC.hold + YawForward(facing) * 250.0;
        cw::Vec3 const player{gC.hold.x, gC.hold.y, gC.hold.z};
        std::vector<cw::ArcSeat> seats = cw::ArcSeats(player, {centre.x, centre.y, centre.z}, static_cast<int>(out.size()),
            gSettings.arcM * kUnitsPerMeter, 25.0, halfAngle);
        // The group's middle (the player, the owner and the arc's seats): the seated face it, as people standing together do.
        cw::Vec3 const middle = cw::GroupMiddle(player, {centre.x, centre.y, centre.z}, seats);
        if (gSettings.faceMiddle) {
            cw::FaceToward(seats, middle);
        }
        std::vector<cw::ArcSeat> const beside = cw::SquadSeats(player, facing, middle, static_cast<int>(outSquad.size()),
            gSettings.squadSideM * kUnitsPerMeter, gSettings.squadBackM * kUnitsPerMeter);
        std::vector<std::string> kinds(out.size(), "brought into view");
        for (std::size_t i = 0; i < outSquad.size() && i < beside.size(); ++i) {
            out.push_back(outSquad[i]);
            seats.push_back(beside[i]);
            kinds.push_back("beside you");
        }
        for (std::size_t i = 0; i < out.size() && i < seats.size(); ++i) {
            auto const [actor, name] = out[i];
            auto const at = player::Location(actor);
            if (!at) {
                continue;
            }
            ik::Vec3 seat{seats[i].at.x, seats[i].at.y, at->z};
            auto const floorHere = FloorZ(pawn, V(*at));
            auto const floorThere = FloorZ(pawn, seat);
            if (!floorThere) {
                log::Info("convo: no floor under the seat for {} at {}: left where it is", name, F(seat));
                continue;
            }
            seat.z = *floorThere + (floorHere ? at->z - *floorHere : 0.0);
            bool const put = PutAt(actor, seat);
            FaceYaw(actor, seats[i].yawDeg);
            auto it = std::find_if(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == actor; });
            if (it != gStay.end()) {
                if (auto const now = player::Location(actor)) {
                    it->place = V(*now);
                }
                it->shownYaw = seats[i].yawDeg;
            }
            if (put) ++gC.arranged;
            log::Info("convo: {} {}: seat {} at {} ({:.1f} m from you), facing {:.0f} deg{}{}", name, kinds[i], i + 1, F(seat),
                Dist(seat, gC.hold) / kUnitsPerMeter, seats[i].yawDeg, gSettings.faceMiddle ? " (the group's middle)" : " (you)",
                put ? "" : " (SetLocation refused)");
        }
    }
    if (moved > 0.05 || gC.arranged > 0) {
        ++gC.cut;
    }
}

// The player's walk during the conversation (research/59 "Walking during conversations"): the left stick moves Shepard's pawn
// along the head's facing at worldMoveSpeedMS, inside worldMoveLeashM of the conversation's owner (cw::StepPlayer), never
// within worldMoveClearM of a person of the conversation or the squad (cw::KeepClear: pushed out, so he walks around them), on the
// floor (a trace; a drop or rise past 60 units is a ledge or a step: refused), not through walls (the shots' line of sight at
// waist height), while the wheel is down (its stick is the wheel's) and after the opening. The held view, its anchors and the
// kept place move with the pawn, so keep-in-place never undoes the walk and the head bone's jump detector never fires on it.
// The dips asked for before the current conversation or moment: the host dips whenever the published count changes, so
// the count must not fall back to zero when the next one begins (a moment's dip, then the hearing: a second dip).
std::uint32_t gCutBase = 0;
bool gMomentWalk = false;  // a moment's view is up (set by TickMoment): the walk's trigger guard stands down

void MovePlayer(ue3::Object* pawn, engine::TrackingFrame& frame, vr::host_view::State const& view, double dt) {
    auto stop = [&] {
        gC.guideProgressed = false;
        if (gC.moving) {
            WalkVelocity(pawn, {});
            gC.moving = false;
        }
    };
    if (!gSettings.move || !gC.openingDone || pawn == nullptr || !ue3::runtime::IsAlive(pawn) || gSetLocation.Get() == nullptr) {
        stop();
        return;
    }
    // The stick: `mevr convo walk` while it lasts, else the left controller's; the wheel's while the wheel is up.
    float stickX = 0.0f, stickY = 0.0f;
    vr::controllers::State const controllers = vr::controllers::Latest();
    double const pushed = controllers.active ? std::hypot(controllers.left.stickX, controllers.left.stickY) : 0.0;
    bool const stickWalks = cw::StickWalks(gC.wheelStick, gC.wheel, pushed);  // the wheel's until it has been let go
    if (gC.wheel) {
        // (the stick steers the wheel)
    } else if (gWalkOverride.untilMs > GetTickCount64()) {
        stickX = gWalkOverride.x;
        stickY = gWalkOverride.y;
    } else if (controllers.active && stickWalks) {
        stickX = controllers.left.stickX;
        stickY = controllers.left.stickY;
        gC.wheelStickNoted = false;
    } else if (controllers.active && pushed > 0.3 && !gC.wheelStickNoted) {
        gC.wheelStickNoted = true;
        ++gC.wheelSticks;
        log::Info("convo: at {:.1f} s the stick that picked the reply is still out ({:.2f}): not a walk until it is let go", Seconds(), pushed);
    }
    auto at = player::Location(pawn);
    if (!at) {
        stop();
        return;
    }
    // The view walks (cw::WalkInWay::View): `at` is where the player's feet would be, not the pawn's place; within a metre
    // of Shepard the rest is walked for him, and there the view takes its place over the pawn and the two are one again.
    bool glide = false;
    if (gC.detached) {
        if (gC.hidPawn && !gActorHidden.ReadBool(pawn).value_or(true)) {
            gC.hidPawn = false;
            HidePawn(pawn, true);  // the scene showed him again: hidden once more while the view is elsewhere
        }
        double const left = gC.wantedMark ? std::hypot(gC.detachedAt.x - at->x, gC.detachedAt.y - at->y) : 0.0;
        if (!gC.noJoin && (!gC.wantedMark || left <= 15.0)) {
            ik::Vec3 const delta = V(*at) - gC.detachedAt;
            gC.hold = gC.hold + delta;
            frame.headWorld.x += delta.x;
            frame.headWorld.y += delta.y;
            frame.headWorld.z += delta.z;
            gC.follow.place = {gC.hold.x, gC.hold.y, gC.hold.z};
            gC.detached = false;
            gC.arrived = gC.wantedMark.has_value();
            gC.wantedMark.reset();
            gTrail.clear();
            HidePawn(pawn, false);
            log::Info("convo: at {:.1f} s the player's view {} Shepard ({:.2f} m walked, {:.2f} m of it guided): the two are one again, his body "
                      "under the player", Seconds(), gC.arrived ? "reached" : "was put on", gC.movedUnits / kUnitsPerMeter,
                gC.guidedUnits / kUnitsPerMeter);
            stop();
            return;
        }
        glide = left <= 100.0 && !gC.noJoin;
        at = EnginePoint(gC.detachedAt);
    }
    // A walk-in nobody walks: after worldWalkInS without the stick the player is walked to Shepard's mark by this same step
    // (cw::StepGuide), wheel up or down. Not in a moment: its own patience and carry are TickMoment's.
    bool const stickUsed = std::hypot(stickX, stickY) > 0.3;
    cw::Guide const guide = gMomentWalk && !gC.detached ? cw::Guide::Wait
        : cw::StepGuide(gC.guide, gC.wantedMark.has_value(), stickUsed, gC.guideProgressed, std::clamp(dt, 0.0, 0.1), gSettings.walkInS);
    bool const guided = (guide == cw::Guide::Walk || glide) && gC.wantedMark.has_value();
    if (guide == cw::Guide::GaveUp && gC.guideLogged) {
        gC.guideLogged = false;
        log::Info("convo: at {:.1f} s the guided walk stops {:.2f} m from Shepard's mark: the way is blocked (a wall, a ledge, a person or a "
                  "trigger's edge); he hears the scene from here, the stick still walks",
            Seconds(), gC.wantedMark ? std::hypot(at->x - gC.wantedMark->x, at->y - gC.wantedMark->y) / kUnitsPerMeter : 0.0);
    }
    if (guided) {
        stickX = 0.0f;
        stickY = 1.0f;
        if (!gC.guideLogged) {
            gC.guideLogged = true;
            log::Info("convo: at {:.1f} s nobody walks: the player is guided to Shepard's mark, {:.2f} m, at {:.1f} m/s ([Cinematics] worldWalkInS = {:.0f})",
                Seconds(), std::hypot(at->x - gC.wantedMark->x, at->y - gC.wantedMark->y) / kUnitsPerMeter, gSettings.moveSpeedMS, gSettings.walkInS);
        }
    }
    ue3::Object* const owner = gOwner.Read<ue3::Object*>(gC.object).value_or(nullptr);
    auto const ownerAt = owner != nullptr && ue3::runtime::IsAlive(owner) ? player::Location(owner) : std::nullopt;
    // The leash is around the owner; after a walk-in opening, around Shepard's mark (where the stage wanted him) until he is there.
    if (!gC.detached && gC.wantedMark &&
        std::hypot(at->x - gC.wantedMark->x, at->y - gC.wantedMark->y) <= gSettings.moveLeashM * kUnitsPerMeter * 0.5) {
        log::Info("convo: at {:.1f} s the player reached Shepard's mark ({:.2f} m walked so far)", Seconds(), gC.movedUnits / kUnitsPerMeter);
        gC.wantedMark.reset();
    }
    // A guided walk by the view follows the way Shepard went (stairs, a walkway between railings), where there is one. The
    // leash is then around the next place on that way: around the mark itself it lets a step through only if it brings the
    // player nearer, and the way there often starts off sideways (the guide stood still for its two seconds and gave up).
    std::optional<cw::Vec3> const along = guided && gC.detached ? cw::TrailTarget(gTrail, cw::Vec3{at->x, at->y, at->z}) : std::nullopt;
    ik::Vec3 const anchor = along ? ik::Vec3{along->x, along->y, along->z} : gC.wantedMark ? *gC.wantedMark : ownerAt ? V(*ownerAt) : gC.hold;
    // Where the player faces: the head in the world through the held frame; without a VR view (a flat game), the held view's yaw.
    double headYaw = YawDeg(gC.holdRotation);
    if (guided) {
        ik::Vec3 const to = along ? ik::Vec3{along->x, along->y, along->z} : *gC.wantedMark;
        headYaw = std::atan2(to.y - at->y, to.x - at->x) * kRadToDeg;
    } else if (view.valid) {
        engine::Basis const head = engine::OrientationToWorld(frame, view.head.orientation);
        headYaw = std::atan2(head.forward.y, head.forward.x) * kRadToDeg;
    }
    cw::PlayerStep const step = cw::StepPlayer(WalkPoint(V(*at)), WalkPoint(anchor), gSettings.moveLeashM * kUnitsPerMeter,
        stickX, stickY, headYaw, gSettings.moveSpeedMS * kUnitsPerMeter, std::clamp(dt, 0.0, 0.1));
    if (step.atLeash) {
        ++gC.leashHits;
    }
    if (!step.moved) {
        stop();
        return;
    }
    // Not through people: the conversation's actors, the squad and the owner, at their live places.
    std::vector<cw::Vec3> people;
    auto addPerson = [&](ue3::Object* actor) {
        if (actor == nullptr || actor == pawn || !ue3::runtime::IsAlive(actor)) return;
        if (auto const where = player::Location(actor)) {
            people.push_back(WalkPoint(V(*where)));
        }
    };
    for (Stay const& st : gStay) addPerson(st.actor);
    for (Tracked const& t : gC.actors) {
        if (std::none_of(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == t.actor; })) addPerson(t.actor);
    }
    for (squad_weapons::MemberView const& m : squad_weapons::Members()) addPerson(m.pawn);
    if (ownerAt && std::none_of(gStay.begin(), gStay.end(), [&](Stay const& st) { return st.actor == owner; })) addPerson(owner);
    cw::ClearStep const clear = cw::KeepClear(WalkPoint(V(*at)), step.place, people, gSettings.moveClearM * kUnitsPerMeter);
    if (clear.held) {
        ++gC.clearHits;
    }
    if (!clear.moved) {
        stop();
        return;
    }
    ik::Vec3 target = WalkPoint(clear.place);
    // The floor under the target, the pawn's height above its own floor kept; a ledge or a step refused.
    auto const floorHere = FloorZ(pawn, V(*at));
    auto const floorThere = FloorZ(pawn, target);
    if (!floorHere || !floorThere || std::abs(*floorThere - *floorHere) > 60.0) {
        ++gC.moveRefused;
        stop();
        return;
    }
    target.z = at->z + (*floorThere - *floorHere);
    if (!LineOfSight(pawn, V(*at), target)) {
        ++gC.moveRefused;
        stop();
        return;
    }
    // (Not during a moment: the script wants him through the triggers on the way, the council's streaming volume among them.)
    // (Nor while the view walks alone: the pawn does not move, so nothing is touched.)
    if (gSettings.moveTriggers && !gMomentWalk && !gC.detached) {
        std::string which;
        bool enters = false;
        if (StepChangesTouch(pawn, anchor, V(*at), target, which, enters)) {
            ++gC.triggerHits;
            if (gC.triggerName != which) {
                gC.triggerName = which;
                log::Info("convo: at {:.1f} s the walk stops at {}: the step would {} it, and a trigger's touch belongs to the level's script",
                    Seconds(), which, enters ? "enter" : "leave");
            }
            stop();
            return;
        }
    }
    if (!gC.detached && !PutAt(pawn, target)) {
        ++gC.moveRefused;
        stop();
        return;
    }
    auto const actual = gC.detached ? std::optional<ue3::Vector>(EnginePoint(target)) : player::Location(pawn);
    ik::Vec3 const delta = (actual ? V(*actual) : target) - V(*at);
    if (gC.detached) {
        gC.detachedAt = target;
    }
    gC.hold = gC.hold + delta;
    frame.headWorld.x += delta.x;
    frame.headWorld.y += delta.y;
    frame.headWorld.z += delta.z;
    if (gC.anchorHead && !gC.detached) {
        *gC.anchorHead = *gC.anchorHead + delta;  // (while the view walks alone his head bone stays where it is)
    }
    gC.follow.place = {gC.follow.place.x + delta.x, gC.follow.place.y + delta.y, gC.follow.place.z + delta.z};
    if (!gC.detached) {
        gC.follow.anchor = {gC.follow.anchor.x + delta.x, gC.follow.anchor.y + delta.y, gC.follow.anchor.z + delta.z};
    }
    for (Stay& st : gStay) {
        if (st.actor == pawn && !gC.detached) {
            st.place = actual ? V(*actual) : target;  // the kept place walks along: keep-in-place puts him back here, not at the start
        }
    }
    double const stepLen = std::hypot(delta.x, delta.y);
    gC.movedUnits += stepLen;
    ++gC.moveSteps;
    if (guided) {
        gC.guidedUnits += stepLen;
        gC.guideProgressed = stepLen > 0.01;
    }
    if (dt > 0.0 && !gC.detached) {
        ik::Vec3 const velocity{delta.x / dt, delta.y / dt, 0.0};
        gC.moving = WalkVelocity(pawn, velocity);  // the legs' animation reads the pawn's velocity (body follow's legs)
    }
    if (!gC.moveLogged) {
        gC.moveLogged = true;
        log::Info("convo: at {:.1f} s the player walks: the stick ({:.2f}, {:.2f}){} along the facing {:.0f} deg, {:.2f} m from {} (leash {:.1f} m, "
                  "{:.2f} m clear of {} people)",
            Seconds(), stickX, stickY, gWalkOverride.untilMs > GetTickCount64() ? " (mevr convo walk)" : "", headYaw,
            Dist(target, anchor) / kUnitsPerMeter, ShortName(owner), gSettings.moveLeashM, gSettings.moveClearM, people.size());
    }
}

void Begin(ue3::Object* conv, ue3::Object* controller) {
    gCutBase += gC.cut;
    gC = {};
    gStay.clear();
    gStayMs = 0;
    gDrawnJumps = 0;
    gC.on = true;
    gC.object = conv;
    gC.name = ue3::runtime::FullName(conv);
    gC.startMs = GetTickCount64();
    gC.number = ++gConversations;
    gC.setPiece = CurrentMode() == cw::Mode::World && gSettings.setPiecesOnScreen && cw::IsSetPiece(gC.name);
    gC.world = CurrentMode() == cw::Mode::World && !gC.setPiece;
    ue3::Object* const pawn = player::Pawn(controller);
    gC.playerPawn = pawn;
    // Shepard not in the scene (research/69 item 3; the owner: "some which don't have Shepard at all should be the typical cut
    // scene"), read from the live scene whatever the name list says: his pawn hidden, or farther than worldAbsentM from the
    // conversation's owner and out of its sight (the stage has placed everyone by now). Logged; shown on the screen only with
    // worldAutoScreen = on, until the sweep has shown which scenes the rules name.
    if (gC.world && gSettings.autoScreen != 0 && pawn != nullptr) {
        ue3::Object* const owner = gOwner.Read<ue3::Object*>(conv).value_or(nullptr);
        auto const pawnAt = player::Location(pawn);
        auto const ownerAt = owner != nullptr && owner != pawn ? player::Location(owner) : std::nullopt;
        if (gActorHidden.ReadBool(pawn).value_or(false) && !gC.hidPawn) {
            gC.absentWhy = "Shepard's pawn is hidden";
        } else if (gSettings.absentM > 0.0f && pawnAt && ownerAt && Dist(V(*pawnAt), V(*ownerAt)) > gSettings.absentM * kUnitsPerMeter &&
                   !LineOfSight(pawn, V(*pawnAt) + ik::Vec3{0.0, 0.0, 60.0}, V(*ownerAt) + ik::Vec3{0.0, 0.0, 60.0})) {
            gC.absentWhy = std::format("Shepard is {:.0f} m from {} and out of its sight", Dist(V(*pawnAt), V(*ownerAt)) / kUnitsPerMeter,
                ShortName(owner));
        }
        if (!gC.absentWhy.empty()) {
            gC.autoScreened = gSettings.autoScreen >= 2;
            log::Info("convo: {}: no place for the player in this scene; {}", gC.absentWhy,
                gC.autoScreened ? "shown on the 3D screen (worldAutoScreen = on)" : "logged only (worldAutoScreen = log), shown in the world");
            if (gC.autoScreened) {
                gC.setPiece = true;
                gC.world = false;
            }
        }
    }
    ue3::Object* const stage = gStage.Read<ue3::Object*>(conv).value_or(nullptr);
    gC.stage = stage != nullptr;
    log::Info("convo: conversation {} began: {} (owner {}, player {}, stage {}{}{}); shown {}", gC.number, gC.name,
        ShortName(gOwner.Read<ue3::Object*>(conv).value_or(nullptr)), ShortName(gConvPlayer.Read<ue3::Object*>(conv).value_or(nullptr)),
        ShortName(stage), stage != nullptr && player::Location(stage) ? " at " + F(V(*player::Location(stage))) : std::string(),
        gUseFixedCameras.ReadBool(conv).value_or(false) ? ", fixed cameras" : "", gC.world ? "in the world (first person)" : gC.setPiece ? "on the 3D screen (a set piece: research/59 \"The scan\")" : cw::Name(CurrentMode()));
    if (gFoot.have) {
        auto const nowPawn = player::Location(pawn);
        log::Info("convo: Shepard's last view on foot at {} yaw {:.0f}, his pawn then at {}{}", F(V(gFoot.location)), YawDeg(gFoot.rotation),
            F(V(gFoot.pawn)), nowPawn ? std::format(", now at {} ({:.2f} m from there)", F(V(*nowPawn)), Dist(V(*nowPawn), V(gFoot.pawn)) / kUnitsPerMeter)
                                      : std::string());
    }
    // The player's place: the view on foot if it was Shepard's a moment ago and his pawn is still there, else his eyes from his head
    // bone facing where his pawn faces (a conversation straight after a level load or a cutscene: the view on foot is elsewhere).
    {
        auto const nowPawn = player::Location(pawn);
        auto const head = HeadOf(pawn);
        bool const footFresh = gFoot.have && GetTickCount64() - gFoot.ms < 5000 && nowPawn && Dist(V(*nowPawn), V(gFoot.pawn)) < 300.0;
        if (footFresh) {
            gC.hold = V(gFoot.location) + (V(*nowPawn) - V(gFoot.pawn));
            gC.holdRotation = gFoot.rotation;
            gC.holdFov = gFoot.fov;
            gC.holdFrom = "the view on foot";
        } else if (head) {
            auto const rot = player::Rotation(pawn);
            double const yaw = rot ? YawDeg(*rot) : 0.0;
            ik::Vec3 const& e = gFoot.eyeFromHead;
            gC.hold = head->at + YawForward(yaw) * e.x + YawRight(yaw) * e.y + ik::Vec3{0.0, 0.0, e.z};
            gC.holdRotation = {0, rot ? rot->yaw : 0, 0};
            gC.holdFov = gFoot.have ? gFoot.fov : 90.0f;
            gC.holdFrom = "Shepard's head bone";
            // No recent on-foot POV: use the same cylinder-floor eye reference if it is available, retaining XY staging.
            auto const view = vr::host_view::Latest();
            if (view.valid && view.floorLocked) {
                auto const basis = engine::ToBasis(engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll});
                double const commonZ = basis.forward.z * view.offsetForward + basis.right.z * view.offsetRight + basis.up.z * view.offsetUp;
                gC.hold.z = view.centreZ - commonZ;
            }
        } else {
            gC.hold = V(gFoot.location);
            gC.holdRotation = gFoot.rotation;
            gC.holdFov = gFoot.fov;
            gC.holdFrom = "the last view on foot (no head bone)";
        }
        if (head) {
            gC.anchorHead = head->at;
        }
        gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, gC.anchorHead ? cw::Vec3{gC.anchorHead->x, gC.anchorHead->y, gC.anchorHead->z} : cw::Vec3{}, 0};
        gC.openingUntilMs = gC.startMs + 500;
        gC.openingDone = !(gC.world && gSettings.openingAccept);
        log::Info("convo: the player's place: {} at {} facing {:.0f} deg{}", gC.holdFrom, F(gC.hold), YawDeg(gC.holdRotation),
            head ? std::format(" ({:.2f} m from Shepard's head bone)", Dist(gC.hold, head->at) / kUnitsPerMeter) : std::string());
    }
    Track(pawn);
    Track(gOwner.Read<ue3::Object*>(conv).value_or(nullptr));
    if (auto const list = gSpeakerList.ElementsOf(conv, 64)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            Track(gSpeakerActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr));
        }
    }
    // Where the stage put actors from (the conversation saved their places before it moved them).
    if (auto const saved = gSavedPositions.ElementsOf(conv, 64)) {
        for (std::int32_t i = 0; i < saved->count; ++i) {
            ue3::Object* const a = gSavedActor.Read<ue3::Object*>(saved->At(i)).value_or(nullptr);
            auto const p = gSavedPos.Read<ue3::Vector>(saved->At(i));
            auto const now = a != nullptr ? player::Location(a) : std::nullopt;
            if (p) {
                log::Info("convo: saved place of {}: {}{}", ShortName(a), F(V(*p)),
                    now ? std::format(" (now {:.2f} m from it)", Dist(V(*now), V(*p)) / kUnitsPerMeter) : std::string());
            }
        }
    }
}

void End(char const* why) {
    if (!gC.on) {
        return;
    }
    ReleaseLooks(why);
    ClearVoices();
    gTrail.clear();
    EndWalks(why);
    if (gC.detached && gC.playerPawn != nullptr && ue3::runtime::IsAlive(gC.playerPawn)) {
        // The view never reached him and the game now takes its camera back to his pawn: rather than the player's view
        // jumping to the mark, Shepard comes to where the player is (the conversation is over: an ordinary move of his).
        bool const put = PutAt(gC.playerPawn, gC.detachedAt);
        log::Info("convo: the conversation ended with the player's view {:.2f} m from Shepard: {}", [&] {
                auto const at = player::Location(gC.playerPawn);
                return at ? Dist(V(*at), gC.detachedAt) / kUnitsPerMeter : 0.0;
            }(), put ? "he is brought to where the player stands, the view stays" : "he could not be moved there: the view goes to him");
        gC.detached = false;
    }
    if (gC.hidPawn) {
        HidePawn(gC.playerPawn, false);
    }
    std::string moves;
    for (Tracked const& t : gC.actors) {
        auto const at = ue3::runtime::IsAlive(t.actor) ? player::Location(t.actor) : std::nullopt;
        auto const rot = ue3::runtime::IsAlive(t.actor) ? player::Rotation(t.actor) : std::nullopt;
        moves += std::format("{}{}: moved at most {:.2f} m, turned at most {:.0f} deg{}{}", moves.empty() ? "" : "; ", t.name,
            t.maxMove / kUnitsPerMeter, t.maxTurn,
            at ? std::format(", ends {:.2f} m from its start", Dist(V(*at), t.start) / kUnitsPerMeter) : std::string(),
            t.headStart ? std::format(", head bone moved at most {:.2f} m", t.headMax / kUnitsPerMeter) : std::string());
        (void)rot;
    }
    std::string kinds;
    for (auto const& [k, n] : gC.shotKinds) {
        kinds += std::format("{}{} {}", kinds.empty() ? "" : ", ", n, k);
    }
    log::Info("convo: conversation {} ended ({}) after {:.1f} s: {} shots ({}), {} not seeable from Shepard's eyes; the wheel opened {} times; {}",
        gC.number, why, Seconds(), gC.shots, kinds.empty() ? "none" : kinds, gC.shotsHidden, gC.wheelOpenings, moves);
    if (gC.world) {
        log::Info("convo: in the world: Shepard's head bone was at most {:.2f} m from the player's held eyes; {} jumps onto marks undone "
                  "(people kept in place, {} of them seen in a drawn frame), {} long moves let through", gC.eyeToViewMax / kUnitsPerMeter, gC.kept,
            gDrawnJumps, gC.letGo);
        if (gC.sourceKept > 0) {
            log::Info("convo: {} placements onto marks withheld at their source (the farthest mark {:.2f} m away)", gC.sourceKept,
                gC.sourceFarUnits / kUnitsPerMeter);
        }
        if (gC.moveSteps > 0 || gC.moveRefused > 0 || gC.leashHits > 0 || gC.clearHits > 0 || gC.triggerHits > 0) {
            log::Info("convo: the player walked {:.2f} m in {} steps; the leash held {} steps back, people in the way {}, a trigger's edge {}{}, "
                      "{} refused (no floor, a ledge, a wall, SetLocation)",
                gC.movedUnits / kUnitsPerMeter, gC.moveSteps, gC.leashHits, gC.clearHits, gC.triggerHits,
                gC.triggerName.empty() ? std::string() : " (" + gC.triggerName + ")", gC.moveRefused);
        }
    }
    if (gC.moving && ue3::runtime::IsAlive(gC.playerPawn)) {
        WalkVelocity(gC.playerPawn, {});
    }
    // One line for the survey (tools/conversation-survey.ps1) and the sweep's judge: everything above as numbers.
    std::string entryPath;
    for (int const e : gC.entryPath) {
        entryPath += std::format("{}{}", entryPath.empty() ? "" : ", ", e);
    }
    double maxMove = 0.0;
    for (Tracked const& t : gC.actors) {
        maxMove = std::max(maxMove, t.maxMove);
    }
    log::Info("convo: summary {{\"name\": \"{}\", \"why\": \"{}\", \"seconds\": {:.1f}, \"stage\": {}, \"actors\": {}, \"lines\": {}, "
              "\"speakers\": {}, \"wheel\": {}, \"shots\": {}, \"onShepard\": {}, \"onNpc\": {}, \"shoulder\": {}, \"cutaway\": {}, "
              "\"notSeeable\": {}, \"world\": {}, \"kept\": {}, \"drawn\": {}, \"letGo\": {}, \"reanchors\": {}, \"maxMoveM\": {:.2f}, "
              "\"eyeToViewM\": {:.2f}, \"farShots\": {}, \"cameraFarM\": {:.1f}, \"walks\": {}, \"openingMoves\": {}, \"arranged\": {}, "
              "\"playerWalkedM\": {:.2f}, \"leashHits\": {}, \"clearHits\": {}, \"moveRefused\": {}, \"triggerHits\": {}, "
              "\"sourceKept\": {}, \"noPlace\": \"{}\", \"autoScreened\": {}, \"opening\": \"{}\", \"markM\": {:.2f}, \"cuts\": {}, "
              "\"trackWalks\": {}, \"trackHeld\": {}, \"yielded\": {}, \"guidedM\": {:.2f}, \"walkInWay\": \"{}\", \"arrived\": {}, "
              "\"bodyTurns\": {}, \"gameTurns\": {}, \"turnsSlid\": {}, \"facingsHeld\": {}, \"wheelSticks\": {}, \"lookWide\": {}, "
              "\"lookMaxDeg\": {:.0f}, \"headShortDeg\": {:.0f}, \"facingsEased\": {}, \"looksSet\": {}, \"glances\": {}, \"heardOnly\": {}, "
              "\"looksGame\": {}, \"turnsHome\": {}, \"reaches\": {}, \"looksRefused\": {}, \"voicesPlaced\": {}, \"voicesFar\": {}, "
              "\"entries\": [{}]}}",
        gC.name, why, Seconds(), gC.stage ? "true" : "false", gC.actors.size(), gC.lines, gC.speakers.size(), gC.wheelOpenings, gC.shots,
        gC.shotKinds.count("on Shepard") != 0 ? gC.shotKinds.at("on Shepard") : 0, gC.shotKinds.count("on an NPC") != 0 ? gC.shotKinds.at("on an NPC") : 0,
        gC.shotKinds.count("over Shepard's shoulder") != 0 ? gC.shotKinds.at("over Shepard's shoulder") : 0,
        gC.shotKinds.count("cutaway") != 0 ? gC.shotKinds.at("cutaway") : 0, gC.shotsHidden, gC.world ? "true" : "false", gC.kept, gDrawnJumps,
        gC.letGo, gC.reanchors, maxMove / kUnitsPerMeter, gC.eyeToViewMax / kUnitsPerMeter, gC.shotsFar, gC.cameraFarM, gC.walks, gC.openingMoves, gC.arranged,
        gC.movedUnits / kUnitsPerMeter, gC.leashHits, gC.clearHits, gC.moveRefused, gC.triggerHits, gC.sourceKept, gC.absentWhy,
        gC.autoScreened ? "true" : "false", gC.world ? cw::Name(gC.opening) : "-", gC.markM, gC.cut, gC.trackWalks, gC.trackHeld, gC.yielded, gC.guidedUnits / kUnitsPerMeter, gC.walkInWay, gC.arrived ? "true" : "false",
        gC.bodyTurns, gC.gameTurns, gC.turnsSlid, gC.facingsHeld, gC.wheelSticks, gC.reachSamples, gC.reachMaxBody, gC.reachWorstHead, gC.facingsEased, gC.looksSet, gC.glances, gC.heardOnly, gC.looksGame, gC.turnsHome, gC.reaches, gC.looksRefused, gC.voicesPlaced, gC.voicesFar, entryPath);
    gCutBase += gC.cut;
    gC = {};
}

void Sample(ue3::Object* conv, ue3::Object* controller, ue3::Object* pawn, ik::Vec3 eyes) {
    // Who speaks, the line, who looks at whom.
    ue3::Object* const speaker = gSpeaker.Read<ue3::Object*>(conv).value_or(nullptr);
    if (speaker != gC.speaker) {
        gC.speaker = speaker;
        ++gC.speakerSerial;
        Track(speaker);
        ue3::Object* const listener = gListener.Read<ue3::Object*>(conv).value_or(nullptr);
        Track(listener);
        std::string facing;
        if (auto const head = HeadOf(speaker); head && speaker != pawn) {
            ik::Vec3 const toEyes = eyes - head->at;
            facing = std::format("; its head bone's axes are {:.0f}/{:.0f}/{:.0f} deg off its line to Shepard's eyes",
                AngleDeg(head->axis[0], toEyes), AngleDeg(head->axis[1], toEyes), AngleDeg(head->axis[2], toEyes));
            if (auto const rot = player::Rotation(speaker)) {
                facing += std::format(", its body {:.0f} deg", std::abs(YawDiffDeg(YawDeg(*rot), std::atan2(toEyes.y, toEyes.x) * kRadToDeg)));
            }
        }
        log::Info("convo: at {:.1f} s {} speaks to {}{}", Seconds(), ShortName(speaker), ShortName(listener), facing);
    }
    std::string const subtitle = Utf8(gSubtitle.In<ue3::FString>(conv));
    if (subtitle != gC.subtitle) {
        gC.subtitle = subtitle;
        if (!subtitle.empty()) {
            ++gC.lines;
            ++gC.speakers[ShortName(speaker)];
            log::Info("convo: at {:.1f} s the line: \"{}\"", Seconds(), subtitle);
        }
    }
    std::string looks;
    if (auto const list = gLookAtList.ElementsOf(conv, 64)) {
        for (std::int32_t i = 0; i < list->count; ++i) {
            looks += std::format("{}{} -> {}", looks.empty() ? "" : ", ", ShortName(gLookActor.Read<ue3::Object*>(list->At(i)).value_or(nullptr)),
                ShortName(gLookTarget.Read<ue3::Object*>(list->At(i)).value_or(nullptr)));
        }
    }
    if (looks != gC.lookAts) {
        gC.lookAts = looks;
        log::Info("convo: at {:.1f} s who looks at whom: {}", Seconds(), looks.empty() ? "nobody listed" : looks);
    }
    int const entry = gCurrentEntry.Read<int>(conv).value_or(-1);
    if (entry != gC.entry) {
        gC.entry = entry;
        if (entry >= 0 && gC.entryPath.size() < 400) {
            gC.entryPath.push_back(entry);
        }
    }
    // The wheel.
    ue3::Object* const handler = WheelHandler(controller);
    bool const wheel = handler != nullptr && gDisplayingWheel.ReadBool(handler).value_or(false);
    if (wheel != gC.wheel) {
        gC.wheel = wheel;
        if (wheel) {
            ++gC.wheelOpenings;
        }
        log::Info("convo: at {:.1f} s the dialogue wheel {}", Seconds(), wheel ? "is up" : "is gone");
    }
    // The actors' moves and turns.
    for (Tracked& t : gC.actors) {
        if (!ue3::runtime::IsAlive(t.actor)) {
            continue;
        }
        auto const at = player::Location(t.actor);
        auto const rot = player::Rotation(t.actor);
        if (!at || !rot) {
            continue;
        }
        ik::Vec3 const p = V(*at);
        double const yaw = YawDeg(*rot);
        t.maxMove = std::max(t.maxMove, Dist(p, t.start));
        t.maxTurn = std::max(t.maxTurn, std::abs(YawDiffDeg(yaw, t.startYaw)));
        if (t.headStart) {
            if (auto const head = HeadOf(t.actor)) {
                t.headMax = std::max(t.headMax, Dist(head->at, *t.headStart));
            }
        }
        double const moved = Dist(p, t.logged);
        double const turned = std::abs(YawDiffDeg(yaw, t.loggedYaw));
        if (moved > 5.0 || turned > 5.0) {
            log::Info("convo: at {:.1f} s {} {} to {} facing {:.0f} deg ({:.2f} m and {:.0f} deg from its start)", Seconds(), t.name,
                moved > 30.0 ? "JUMPED" : "moved", F(p), yaw, Dist(p, t.start) / kUnitsPerMeter, YawDiffDeg(yaw, t.startYaw));
            t.logged = p;
            t.loggedYaw = yaw;
        }
    }
    // A cut: the director's camera jumped.
    if (GetTickCount64() - gPov.ms < 500) {
        bool const cut = !gC.haveShot || Dist(V(gPov.lastLocation), V(gC.shotLocation)) > 30.0 ||
                         AngleDeg(Forward(gPov.lastRotation), Forward(gC.shotRotation)) > 12.0;
        if (cut) {
            ++gC.shots;
            gC.haveShot = true;
            LogShot(pawn, eyes);
        }
        gC.shotLocation = gPov.lastLocation;
        gC.shotRotation = gPov.lastRotation;
    }
}

void SetStatus(std::string json) {
    std::lock_guard lock(gStatusMutex);
    gStatusJson = std::move(json);
}

// The depth of field: the conversation camera's is focused for the director's shot (the door behind Chakwas blurred, then the
// whole room when the focus moved on, 2026-11-06), whatever its behaviour's m_bDOFSettingDisabled says (tried: no effect). The
// engine's own switch through the local player's console while the world view lasts (`scale set`, a boolean through its opposite
// first, research/35), and back to what the game's BIOEngine.ini said before (read first: the game writes the live value there
// during play, 2026-11-06, so a read afterwards finds our False).
bool gDofOff = false;
bool gDofWas = true;  // BIOEngine.ini's value read before we switched it (the game writes its live value there during play)

bool DofInIni() {
    // <exe dir>\..\..\BioGame\Config\BIOEngine.ini's [SystemSettings] (it wins over GamerSettings.ini, research/35).
    std::filesystem::path const ini = engine::SettingsFile().parent_path().parent_path().parent_path().parent_path() / L"BioGame" /
        L"Config" / L"BIOEngine.ini";
    ini::Loaded const loaded = ini::Read(ini);
    auto const v = ini::Get(loaded.file, "SystemSettings", "DepthOfField");
    return !v || ini::Lower(ini::Trim(*v)) != "false";
}

void DofOff() {
    if (gDofOff) {
        return;
    }
    gDofOff = true;
    gDofWas = DofInIni();
    std::string ignored;
    bool const ok = console_request::RunCommand("scale set DepthOfField True", &ignored) &&
                    console_request::RunCommand("scale set DepthOfField False", &ignored);
    log::Info("convo: depth of field off for the world view{}", ok ? "" : " (the console wasn't there: it stays as it is)");
}

void DofBack() {
    if (!gDofOff) {
        return;
    }
    if (gDofWas) {
        // Kept owed until both commands ran: StopWorld comes every tick outside a conversation, so a restore that
        // found no console (no local controller in a transition) is tried again, instead of DOF staying off for good
        // (the game writes the live value back into BIOEngine.ini; Codex review 2026-11-06, gameA#7).
        std::string ignored;
        bool const ok = console_request::RunCommand("scale set DepthOfField False", &ignored) &&
                        console_request::RunCommand("scale set DepthOfField True", &ignored);
        if (!ok) {
            return;
        }
        log::Info("convo: depth of field on again (BIOEngine.ini's [SystemSettings])");
    }
    gDofOff = false;
}

void StopWorld(Link const& link, char const* why) {
    EndWalks(why);
    gStayMs = 0;
    gCameraHold.on = false;
    gNearHold = false;
    DofBack();
    if (gPublished && link.publish != nullptr) {
        link.publish(nullptr);
    }
    if (gHeld) {
        view_anchor::Hold(std::nullopt);
        gHeld = false;
    }
    if (gInWorld) {
        log::Info("convo: the world view ends ({})", why);
    }
    gPublished = false;
    gInWorld = false;
    gInWorldForStay = false;
}

// ---- A moment in the world (research/69 "Authored scenes: a treatment per kind of track") --------------------------------------
// A level-script matinee that walks Shepard (the walk to the council with Anderson) runs in the cinematic game mode, with
// the director's camera. With worldMoments = walk the mod holds the view where the player stood on foot, as in a
// conversation; the track's move of his pawn is withheld (engine/ConversationHooks, asked through game/AuthoredMoment) and
// the stick walks him. What the script then does to his pawn (its teleport, the matinee putting him back at its end) is
// judged by the opening's rule: in the same space he is put back where his own walk has him, nothing is cut; to another
// composition the view goes with him under a dip. The on-foot record is kept fresh, so the conversation that follows
// opens from where he stands.
struct MomentView {
    bool on = false;
    std::uint64_t startMs = 0;
    std::uint64_t cinematicMs = 0;  // the last tick in the cinematic mode
    ik::Vec3 pawnAt{};              // where the player's own walk has Shepard
    std::optional<ik::Vec3> wanted; // where the script wants him (its teleport's destination in the same space): he walks there
    int dips = 0;                    // dips from black in place of the script's black screens
    bool provisional = false;        // begun at the cinematic switch (MomentMayStart): no track has moved Shepard yet
    bool onFoot = false;             // begun in game mode 0 under a matinee's own camera (gDirected), not the cinematic mode
    bool viewWalk = false;           // the script moved Shepard within the same space: he stays there, the view walks to him
    std::optional<ik::Vec3> trackAt; // where the withheld track has him
    bool trackEnded = false;
    std::uint64_t wantedMs = 0;     // ... since when
    int arrivals = 0;               // times he got there (walked, or carried after the wait)
    double carriedUnits = 0.0;
    int putBack = 0;                // the script's moves of him undone (the same space)
    int cuts = 0;                   // ... accepted under a dip (another composition)
    std::uint32_t number = 0;
};
MomentView gMoment;
std::uint32_t gMoments = 0;

bool MomentHeld() {
    return gMoment.on;
}

bool FootFresh(std::uint64_t now) {
    return gFoot.have && now >= gFoot.ms && now - gFoot.ms < 3000;
}

void EndMomentView(Link const& link, char const* why, bool stopWorld) {
    if (gMoment.provisional) {
        // Never a moment: the cinematic mode began and no track moved Shepard. No summary (nothing was shown as one).
        log::Info("moment: the cinematic mode began on foot but no track moved Shepard ({}): not a moment; the view was held {:.2f} s",
            why, static_cast<double>(GetTickCount64() - gMoment.startMs) / 1000.0);
        gMoment = {};
        gMomentWalk = false;
        gCutBase += gC.cut;
        gC = {};
        if (stopWorld) {
            StopWorld(link, why);
        }
        return;
    }
    // Whether the script still had Shepard somewhere the player is not: a place he was wanted at and had not reached, or a
    // move of this very tick (the ceremony's teleport and its conversation's start come together): the conversation that
    // begins then walks in by the view, never by putting Shepard back (cw::DecideWalkIn).
    bool pending = gMoment.wanted.has_value() || gC.detached;
    if (ue3::Object* const controller = player::Controller()) {
        if (ue3::Object* const pawn = player::Pawn(controller); pawn != nullptr && ue3::runtime::IsAlive(pawn)) {
            if (auto const at = player::Location(pawn)) {
                pending = pending || Dist(V(*at), gMoment.pawnAt) > kJumpUnits;
            }
        }
    }
    gMomentEnded = {GetTickCount64(), pending};
    log::Info("moment: the first-person view of the moment ends ({}) after {:.1f} s: the player walked {:.2f} m and was carried {:.2f} m; "
              "the script's moves of Shepard: {} within the same space (he got there {} times), {} accepted under a dip",
        why, static_cast<double>(GetTickCount64() - gMoment.startMs) / 1000.0, gC.movedUnits / kUnitsPerMeter,
        gMoment.carriedUnits / kUnitsPerMeter, gMoment.putBack, gMoment.arrivals, gMoment.cuts);
    log::Info("moment: view summary {{\"seconds\": {:.1f}, \"walkedM\": {:.2f}, \"carriedM\": {:.2f}, \"sameSpace\": {}, \"arrivals\": {}, \"dips\": {}, "
              "\"why\": \"{}\"}}", static_cast<double>(GetTickCount64() - gMoment.startMs) / 1000.0, gC.movedUnits / kUnitsPerMeter,
        gMoment.carriedUnits / kUnitsPerMeter, gMoment.putBack, gMoment.arrivals, gMoment.cuts, why);
    if (ue3::Object* const controller = player::Controller()) {
        if (ue3::Object* const pawn = player::Pawn(controller); pawn != nullptr && gC.moving && ue3::runtime::IsAlive(pawn)) {
            WalkVelocity(pawn, {});
        }
        // The view was still on its way to Shepard: shown again; a conversation that begins takes it from here (its own
        // walk-in by the view hides him again), and where the cinematic mode just ends he is brought to where the player
        // stands (on foot the game's camera is his: the view would jump to him).
        if (ue3::Object* const pawn = player::Pawn(controller); pawn != nullptr && gC.detached && ue3::runtime::IsAlive(pawn)) {
            HidePawn(pawn, false);
            if (stopWorld) {
                bool const moved = PutAt(pawn, gC.detachedAt);
                log::Info("moment: the cinematic mode ended with the player's view {:.2f} m from Shepard: he is brought to where the player "
                          "stands{}", Dist(gC.detachedAt, gMoment.pawnAt) / kUnitsPerMeter, moved ? "" : " (refused: the view goes to him)");
            }
        }
    }
    gMoment = {};
    gMomentWalk = false;
    gCutBase += gC.cut;
    gC = {};
    if (stopWorld) {
        StopWorld(link, why);
    }
}

// A moment's view begins: the place on foot, carried to where the pawn is now.
void BeginMomentView(ik::Vec3 const& pawnAt, std::uint64_t now, bool provisional) {
    gCutBase += gC.cut;
    gC = {};
    gMoment = {};
    gMoment.on = true;
    gMoment.provisional = provisional;
    gMomentWalk = true;
    gMoment.startMs = now;
    gMoment.cinematicMs = now;
    gMoment.number = 900000 + ++gMoments;
    gMoment.pawnAt = pawnAt;
    gC.world = true;
    gC.openingDone = true;
    gC.startMs = now;
    gC.number = gMoment.number;
    gC.hold = V(gFoot.location) + (pawnAt - V(gFoot.pawn));
    gC.holdRotation = gFoot.rotation;
    gC.holdFov = gFoot.fov;
    gC.holdFrom = "the view on foot";
    gTrail.clear();
    if (gSettings.momentView && !gSettings.momentCarry) {
        // The pawn is the script's from the first tick; the view stays the player's, where the player stood (a moment seen on
        // foot begins a tick after its track's first move, which had already taken Shepard 2 m on at Chellik's bump: the view
        // went with him, 2026-11-11).
        gC.hold = V(gFoot.location);
        gC.detached = true;
        gC.detachedAt = V(gFoot.pawn);
        gC.noJoin = true;
        TrailAdd(V(gFoot.pawn));
        TrailAdd(pawnAt);
    }
}

// What the host is told of a moment's view: held at gC.hold, the game's camera too.
cw::State MomentState(std::uint64_t now) {
    cw::State state;
    state.active = 1;
    state.conversation = gMoment.number;
    state.cut = gCutBase + gC.cut;
    if (gPov.location != nullptr && now - gPov.pointerMs < 500) {
        state.holdRender = 1;
        state.holdLocation[0] = static_cast<float>(gC.hold.x);
        state.holdLocation[1] = static_cast<float>(gC.hold.y);
        state.holdLocation[2] = static_cast<float>(gC.hold.z);
        state.holdRotation[0] = gC.holdRotation.pitch;
        state.holdRotation[1] = gC.holdRotation.yaw;
        state.holdRotation[2] = gC.holdRotation.roll;
        state.holdFov = gC.holdFov;
        state.povLocationAddress = reinterpret_cast<std::uint64_t>(gPov.location);
        state.povRotationAddress = reinterpret_cast<std::uint64_t>(gPov.rotation);
        state.povFovAddress = reinterpret_cast<std::uint64_t>(gPov.fov);
    }
    gCameraHold.on = gSettings.camera && state.holdRender != 0;
    gNearHold = gSettings.nearPlane && state.holdRender != 0;
    gCameraHold.location = {static_cast<float>(gC.hold.x), static_cast<float>(gC.hold.y), static_cast<float>(gC.hold.z)};
    gCameraHold.rotation = gC.holdRotation;
    gCameraHold.fov = gC.holdFov;
    return state;
}

Link gLink;  // the host's publish function, as the last tick had it (MomentMayStart publishes from a handler)

// Armed (research/69 "The flash at every scene's start"): on foot with the first-person mode on, the host is told every tick
// that a scene beginning now would be shown in the world, so it keeps the on-foot presentation over the tick in which the
// game's mode has changed and the mod's state has not; said to be off the moment a mode begins that the mod does not take.
std::uint64_t gArmedMs = 0;
void Arm(Link const& link, bool armed) {
    if (link.publish == nullptr || (!armed && gArmedMs == 0)) {
        return;
    }
    cw::State state;
    state.active = 0;
    state.holdPad[0] = armed ? 1 : 0;
    link.publish(&state);
    gArmedMs = armed ? GetTickCount64() : 0;
    gArmedSince = gArmedMs;
}

// True while the moment's view is up this tick (the caller returns).
bool TickMoment(Link const& link, ue3::Object* pawn, std::optional<std::uint8_t> mode, double deltaSeconds) {
    std::uint64_t const now = GetTickCount64();
    bool const cinematic = mode && *mode == 6;
    // A matinee's own camera on foot is the cinematic mode's equal: its track walks Shepard while the game's camera shows him
    // from behind, letterboxed, in the player's eyes (Chellik's bump, 2026-11-11; research/69 "A moment on foot").
    bool const directed = mode && *mode == 0 && gDirected.on && now - gDirected.ms < 500;
    if (!gSettings.moments || CurrentMode() != cw::Mode::World || link.publish == nullptr || pawn == nullptr || !ue3::runtime::IsAlive(pawn)) {
        return false;
    }
    if (!gMoment.on) {
        std::uint64_t const track = authored_moment::LastPlayerTrackMs();
        auto const at = player::Location(pawn);
        if (!(cinematic || directed) || track == 0 || now - track > 500 || !FootFresh(now) || !at) {
            return false;
        }
        BeginMomentView(V(*at), now, false);
        gMoment.onFoot = directed;
        log::Info("moment: a matinee walks Shepard{}: held in first person at {} facing {:.0f} deg; {}",
            directed ? " with its own camera on foot (no cinematic mode)" : "", F(gC.hold), YawDeg(gC.holdRotation),
            gSettings.momentCarry ? "the track carries him, the view goes with him"
            : gSettings.momentView ? "the track moves his pawn, hidden; the view is the player's and walks to him"
            : "its move of him is withheld, the stick walks him");
    } else if (gMoment.provisional) {
        // Begun at the cinematic switch: a moment once a track moves Shepard, else over after a quarter of a second.
        std::uint64_t const track = authored_moment::LastPlayerTrackMs();
        if (track != 0 && track + 100 >= gMoment.startMs && now - track <= 500) {
            gMoment.provisional = false;
            log::Info("moment: a matinee walks Shepard: held in first person at {} facing {:.0f} deg since the cinematic mode began {:.0f} ms "
                      "ago (no flash of the screen); {}", F(gC.hold), YawDeg(gC.holdRotation), static_cast<double>(now - gMoment.startMs),
                gSettings.momentCarry ? "the track carries him, the view goes with him"
                : gSettings.momentView ? "the track moves his pawn, hidden; the view is the player's and walks to him"
                : "its move of him is withheld, the stick walks him");
        } else if (now - gMoment.startMs > 250) {
            EndMomentView(link, "0.25 s without one", true);
            return false;
        }
    }
    if (cinematic || directed) {
        gMoment.cinematicMs = now;
    } else if (!(mode && *mode == 0 && now - gMoment.cinematicMs < 1000)) {
        return false;  // the cinematic mode is over (the caller ends the view)
    }
    // The script moved Shepard: the track carrying him (small steps: the view goes with him), a teleport, or the matinee
    // putting him back at its end (a jump).
    if (auto const at = player::Location(pawn)) {
        ik::Vec3 const here = V(*at);
        if (double const d = Dist(here, gMoment.pawnAt); d > 0.01 && d <= kJumpUnits) {
            if (!gC.detached) {  // (detached: the view is the player's own, wherever the pawn is moved)
                gC.hold = gC.hold + (here - gMoment.pawnAt);
                gMoment.carriedUnits += d;
            }
            gMoment.pawnAt = here;
        }
        if (Dist(here, gMoment.pawnAt) > kJumpUnits) {
            cw::OpeningFacts facts;
            facts.known = true;
            facts.markUnits = std::hypot(here.x - gMoment.pawnAt.x, here.y - gMoment.pawnAt.y);
            facts.riseUnits = here.z - gMoment.pawnAt.z;
            facts.sightToMark = InSight(pawn, gC.hold, here + ik::Vec3{0.0, 0.0, 60.0});
            facts.floorAtMark = FloorZ(pawn, here).has_value();
            cw::Opening const rule = cw::DecideOpening(facts, cw::OpeningRule::Auto);
            if (rule != cw::Opening::Cut) {
                // The same space: nothing is cut, and Shepard stays where the script put him. What follows may count on it in
                // this very tick: at the council the place streams the chamber's chunk in and the event that starts the
                // ceremony is fired once (put back to walk there himself, a player who had not walked far enough during the
                // walk got no ceremony at all: found on the fake headset, 2026-11-10; the owner had walked the whole way). His
                // pawn is hidden there and the player's view stays where it is and walks to him: the stick, or guided after
                // worldWalkInS, as in a walk-in by the view (cw::WalkInWay::View); on arrival the two are one again.
                ++gMoment.putBack;
                gMoment.wanted = here;
                gMoment.wantedMs = now;
                gMoment.viewWalk = true;
                gC.wantedMark = here;
                if (!gC.detached) {
                    gC.detached = true;
                    gC.detachedAt = gMoment.pawnAt;
                    HidePawn(pawn, true);
                }
                gMoment.pawnAt = here;
                log::Info("moment: the script moved Shepard {:.2f} m ({:+.2f} m up, {}): the same space, no cut; he stays there, hidden, and "
                          "the player's view walks to him from {} (the stick, or guided after {:.0f} s)",
                    facts.markUnits / kUnitsPerMeter, facts.riseUnits / kUnitsPerMeter, facts.sightToMark ? "in sight" : "out of sight",
                    F(gC.detachedAt), gSettings.walkInS);
            } else {
                gC.hold = gC.hold + (here - (gC.detached ? gC.detachedAt : gMoment.pawnAt));
                if (gC.detached) {
                    gC.detached = false;
                    gC.wantedMark.reset();
                    gMoment.wanted.reset();
                    gMoment.viewWalk = false;
                    HidePawn(pawn, false);
                }
                gMoment.pawnAt = here;
                ++gMoment.cuts;
                ++gC.cut;
                log::Info("moment: the script moved Shepard {:.2f} m ({:+.2f} m up, {}): another composition, the view goes with him under a dip",
                    facts.markUnits / kUnitsPerMeter, facts.riseUnits / kUnitsPerMeter, facts.sightToMark ? "in sight" : "out of sight");
            }
        }
    }
    // A withheld track: where it has him is where the script has him. While it runs he walks as he likes; when it lets him
    // go (the matinee is over) and the script has not said otherwise, that place is where he is wanted, as after a teleport:
    // what follows may count on him being there (a door closed behind him, a trigger he was carried through).
    if (gC.detached && gSettings.momentView && !gMoment.provisional) {
        // The view's own walk: Shepard is where the script has him, hidden; where he is is where the player is wanted, and
        // the view joins him only once the track has let him rest.
        HidePawn(pawn, true);
        gC.wantedMark = gMoment.pawnAt;
        TrailAdd(gMoment.pawnAt);
        std::uint64_t const track = authored_moment::LastPlayerTrackMs();
        gC.noJoin = track != 0 && now >= track && now - track <= 500;
        if (!gMoment.viewWalk) {
            gMoment.viewWalk = true;
            gMoment.wanted = gMoment.pawnAt;
            gMoment.wantedMs = now;
        } else if (gMoment.wanted) {
            gMoment.wanted = gMoment.pawnAt;
        }
    }
    if (!gSettings.momentCarry && !gSettings.momentView) {
        float place[3];
        std::uint64_t ms = 0;
        if (authored_moment::PlayerTrackPlace(place, &ms) && ms >= gMoment.startMs) {
            if (now - ms <= 500) {
                gMoment.trackAt = ik::Vec3{place[0], place[1], place[2]};
                gMoment.trackEnded = false;
            } else if (gMoment.trackAt && !gMoment.trackEnded) {
                gMoment.trackEnded = true;
                if (!gMoment.wanted && Dist(*gMoment.trackAt, gMoment.pawnAt) > 150.0) {
                    gMoment.wanted = *gMoment.trackAt;
                    gMoment.wantedMs = now;
                    gC.wantedMark = *gMoment.trackAt;
                    log::Info("moment: the track let Shepard go {:.2f} m from where it had him ({}): he is wanted there and walks (carried after {:.0f} s)",
                        Dist(*gMoment.trackAt, gMoment.pawnAt) / kUnitsPerMeter, F(*gMoment.trackAt), gSettings.momentPatienceS);
                }
            }
        }
    }
    // Where the script wants him: there when he has walked within a step of it, or carried once the wait is over (or at
    // once with worldMoments = carry), in a straight line at worldMomentCarryMS; then he stands exactly there, as its
    // teleport would have put him.
    if (gMoment.wanted && gMoment.viewWalk) {
        if (!gC.detached) {  // MovePlayer brought the view to him
            ++gMoment.arrivals;
            log::Info("moment: the player's view reached Shepard where the script has him ({})", F(*gMoment.wanted));
            gMoment.wanted.reset();
            gMoment.viewWalk = false;
        }
    } else if (gMoment.wanted) {
        ik::Vec3 const to = *gMoment.wanted - gMoment.pawnAt;
        double const left = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
        double const flat = std::hypot(to.x, to.y);
        bool const close = flat <= 150.0;
        bool const waited = static_cast<double>(now - gMoment.wantedMs) / 1000.0 >= gSettings.momentPatienceS;
        if (close || waited || gSettings.momentCarry) {
            double const step = gSettings.momentCarryMS * kUnitsPerMeter * std::clamp(deltaSeconds, 0.0, 0.1);
            ik::Vec3 const next = left <= step ? *gMoment.wanted : gMoment.pawnAt + to * (step / left);
            if (PutAt(pawn, next)) {
                gC.hold = gC.hold + (next - gMoment.pawnAt);
                gMoment.carriedUnits += Dist(next, gMoment.pawnAt);
                gMoment.pawnAt = next;
            } else if (PutAt(pawn, *gMoment.wanted)) {
                // Something in the straight line: the last resort, there at once under a dip.
                gC.hold = gC.hold + (*gMoment.wanted - gMoment.pawnAt);
                gMoment.pawnAt = *gMoment.wanted;
                ++gMoment.cuts;
                ++gC.cut;
                log::Info("moment: the way to where the script wants Shepard is blocked: put there under a dip");
            }
            if (Dist(gMoment.pawnAt, *gMoment.wanted) < 1.0) {
                ++gMoment.arrivals;
                log::Info("moment: Shepard is where the script wants him ({}): {}", F(*gMoment.wanted),
                    close && !waited && !gSettings.momentCarry ? "he walked there" : "carried there");
                gMoment.wanted.reset();
                gC.wantedMark.reset();
            }
        }
    }
    std::optional<engine::TrackingFrame> frame;
    vr::host_view::State const view = vr::host_view::Latest();
    if (view.valid && gInWorld) {
        frame = engine::FromCameraView(engine::UnrealVector{gC.hold.x, gC.hold.y, gC.hold.z},
            engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll}, {view.lookPitch, view.lookYaw, 0},
            {view.offsetForward, view.offsetRight, view.offsetUp}, view.head, view.unitsPerMeter);
        if (view.centreValid) frame->headWorld.z = view.centreZ;
        MovePlayer(pawn, *frame, view, deltaSeconds);
    } else if (gFoot.frame) {
        frame = gFoot.frame;
        frame->headWorld.x += gC.hold.x - gFoot.location.x;
        frame->headWorld.y += gC.hold.y - gFoot.location.y;
        frame->headWorld.z += gC.hold.z - gFoot.location.z;
        MovePlayer(pawn, *frame, view, deltaSeconds);
    }
    if (auto const at = player::Location(pawn)) {
        gMoment.pawnAt = V(*at);  // after his own step
    }
    if (frame) {
        view_anchor::Hold(frame, gFoot.floorView);
        gHeld = true;
    }
    if (!gMoment.provisional) {
        DofOff();  // the matinee's camera focuses for its own shot: from the player's place it blurred people at random
    }
    cw::State const state = MomentState(now);
    link.publish(&state);
    gPublished = true;
    ++gWorldTicks;
    if (!gInWorld) {
        gInWorld = true;
        log::Info("moment: in the world: the view held at {} (yaw {:.0f}); arm IK and body follow {}", F(gC.hold), YawDeg(gC.holdRotation),
            frame ? "on" : "waiting for the host's world view");
    }
    // The on-foot record stays fresh: the conversation that follows opens from where the player stands.
    gFoot.location = EnginePoint(gC.hold);
    gFoot.rotation = gC.holdRotation;
    gFoot.pawn = EnginePoint(gC.detached ? gC.detachedAt : gMoment.pawnAt);  // where the player's feet are
    gFoot.have = true;
    gFoot.ms = now;
    SetStatus(std::format("{{\"on\": true, \"world\": true, \"moment\": {}, \"held\": [{:.1f}, {:.1f}, {:.1f}], \"putBack\": {}, \"cuts\": {}}}",
        gMoment.number, gC.hold.x, gC.hold.y, gC.hold.z, gMoment.putBack, gMoment.cuts));
    return true;
}

} // namespace

// The move track's question, from game/AuthoredMoment's provider (inside the engine's physics step: reads only): the track's
// move of the player's pawn is withheld while a moment's view is up, and from its first call when one can start (the
// cinematic game mode, a fresh place on foot).
void MomentMayStart() {
    std::uint64_t const now = GetTickCount64();
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    if (!gSettings.moments || CurrentMode() != cw::Mode::World || gLink.publish == nullptr || gMoment.on || gC.on || !(mode && *mode == 6) ||
        pawn == nullptr || !ue3::runtime::IsAlive(pawn) || !FootFresh(now)) {
        return;
    }
    auto const at = player::Location(pawn);
    if (!at) {
        return;
    }
    BeginMomentView(V(*at), now, true);
    cw::State const state = MomentState(now);
    gLink.publish(&state);
    gPublished = true;
    log::Info("moment: the cinematic mode began on foot: the view is held at {} until a track moves Shepard or 0.25 s pass{}", F(gC.hold),
        state.holdRender != 0 ? "" : " (no camera address yet: the hold begins with the next camera update)");
}

bool MomentViewOn() {
    return gMoment.on && !gMoment.provisional;
}

ik::Vec3 gViewAt;            // the held view's place last tick
bool gViewAtValid = false;
double gViewSpeed = 0.0;     // the held view's speed over the floor (units a second)
double gLegsStand = 1.0;     // 1: the legs stand by leg IK; 0: they play the script's walk

BodyAwayState BodyAway() {
    BodyAwayState away;
    if (!gSettings.bodyWithPlayer || !gC.world || !gC.detached || (gMoment.on && gMoment.provisional)) {
        return away;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    auto const at = pawn != nullptr && ue3::runtime::IsAlive(pawn) ? player::Location(pawn) : std::nullopt;
    if (!at) {
        return away;
    }
    away.on = true;
    away.riseUnits = gC.detachedAt.z - at->z;
    away.legsStand = gSettings.legsStand ? gLegsStand : 0.0;
    return away;
}

// The view's speed, and from it how far the legs stand (BodyAwayState::legsStand): every tick of a scene in the world.
void TrackViewSpeed(double deltaSeconds) {
    bool const scene = (gC.on && gC.world) || gMoment.on;
    if (!scene || deltaSeconds <= 0.0) {
        gViewAtValid = false;
        gLegsStand = 1.0;
        return;
    }
    double speed = 0.0;
    if (gViewAtValid) {
        speed = std::hypot(gC.hold.x - gViewAt.x, gC.hold.y - gViewAt.y) / deltaSeconds;
    }
    gViewAt = gC.hold;
    gViewAtValid = true;
    gViewSpeed += (speed - gViewSpeed) * (std::min)(1.0, deltaSeconds * 10.0);  // units a second, smoothed over a tenth
    double const wanted = gViewSpeed < 15.0 ? 1.0 : gViewSpeed > 60.0 ? 0.0 : 1.0 - (gViewSpeed - 15.0) / 45.0;
    double const step = deltaSeconds * 4.0;  // a quarter of a second from one to the other
    gLegsStand += std::clamp(wanted - gLegsStand, -step, step);
}

// The people shown in the player's view (anywhere but behind him, within 30 m), for what a script's black screen covers.
std::vector<cw::Seen> PeopleInView(ue3::Object* pawn, ik::Vec3 const& eye, double facing) {
    std::vector<cw::Seen> seen;
    ue3::Object* const world = pawn != nullptr ? gWorldOfActor.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    if (world == nullptr) {
        return seen;
    }
    int guard = 0;
    for (ue3::Object* p = gWorldPawns.Read<ue3::Object*>(world).value_or(nullptr); p != nullptr && guard < 512;
         p = gNextPawnOf.Read<ue3::Object*>(p).value_or(nullptr), ++guard) {
        if (p == pawn || !player::IsBioPawn(p) || gActorHidden.ReadBool(p).value_or(true)) {
            continue;
        }
        if (auto const at = player::Location(p)) {
            if (cw::InView({eye.x, eye.y, eye.z}, facing, {at->x, at->y, at->z}, 110.0, 0.0, 3000.0)) {
                seen.push_back({reinterpret_cast<std::uint64_t>(p), {at->x, at->y, at->z}});
            }
        }
    }
    return seen;
}

// A script's black screen during a held moment, taken down by game/AuthoredMoment: whether the mod dips in its place is
// decided over the next ticks, by what the black screen covered in the player's view (cw::CoveredChanges).
struct BlackPending {
    bool on = false;
    std::uint64_t ms = 0;
    ik::Vec3 eye;        // the view it was seen from
    double facing = 0.0;
    std::vector<cw::Seen> before;
};
BlackPending gBlack;

bool MomentDip() {
    if (!gMoment.on) {
        return false;
    }
    ue3::Object* const controller = player::Controller();
    gBlack.on = true;
    gBlack.ms = GetTickCount64();
    gBlack.eye = gC.hold;
    gBlack.facing = YawDeg(gC.holdRotation);
    gBlack.before = PeopleInView(controller != nullptr ? player::Pawn(controller) : nullptr, gBlack.eye, gBlack.facing);
    return true;
}

void ResolveBlack(ue3::Object* pawn) {
    if (!gBlack.on) {
        return;
    }
    std::uint64_t const now = GetTickCount64();
    std::vector<cw::Seen> const after = PeopleInView(pawn, gBlack.eye, gBlack.facing);
    cw::Covered const covered = cw::CoveredChanges(gBlack.before, after);
    double const nearestM = cw::NearestChange(gBlack.before, after, {gBlack.eye.x, gBlack.eye.y, gBlack.eye.z}) / kUnitsPerMeter;
    if (covered.Total() > 0 && gSettings.dipWithinM >= 0.0f && nearestM > gSettings.dipWithinM) {
        if (now - gBlack.ms > 300) {
            log::Info("moment: the script's black screen covered a change in view, but none of it near the player ({} gone, {} "
                      "moved at once, {} new; the nearest {:.1f} m away, worldDipWithinM {:.0f}): no dip",
                covered.gone, covered.moved, covered.appeared, nearestM, gSettings.dipWithinM);
            gBlack = {};
        }
    } else if (covered.Total() > 0) {
        ++gC.cut;  // published with the view's state: the host dips from black on a change
        if (gMoment.on) {
            ++gMoment.dips;
        }
        log::Info("moment: the script's black screen covered a change in view ({} of {} people gone, {} moved at once, {} new; the "
                  "nearest {:.1f} m from the player): a dip from black in its place", covered.gone, gBlack.before.size(), covered.moved,
            covered.appeared, nearestM);
        gBlack = {};
    } else if (now - gBlack.ms > 300) {
        log::Info("moment: the script's black screen covered nothing in view ({} people, none gone, moved or new in 0.3 s): no dip",
            gBlack.before.size());
        gBlack = {};
    }
}

// Every flag that stops Shepard being drawn, when he and his mesh were last rendered, and where they are against the view
// (research/69 "The plan for the blocker", phase A). Reads only.
std::optional<bool> gBodyLogOverride;  // `mevr convo body log on|off`
std::uint64_t gBodyLogMs = 0;

// The renderer's own copy of a mesh's box (research/69 "The culling camera is ahead of the eye"): the scene info behind the
// component (the pointer right after Bounds) holds the bounds it was last sent. Its layout isn't reflected, so the copy is
// looked for: seven floats that read as a box within 60 m of the component's (an origin, positive extents, a radius that
// holds them). A guarded read; a diagnostic only.
bool CopyGuarded(void const* from, void* to, std::size_t bytes) {
    __try {
        std::memcpy(to, from, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

std::string RendererBox(void const* componentBounds, ik::Vec3 const& origin) {
    void const* info = nullptr;
    if (!CopyGuarded(static_cast<std::uint8_t const*>(componentBounds) + 0x1C, &info, sizeof(info)) || info == nullptr) {
        return "no scene info";
    }
    float words[0x180 / 4] = {};
    if (!CopyGuarded(info, words, sizeof(words))) {
        return "scene info unreadable";
    }
    std::string found;
    int count = 0;
    for (std::size_t i = 0; i + 7 <= std::size(words) && count < 3; ++i) {
        float const* const f = words + i;
        bool finite = true;
        for (int k = 0; k < 7; ++k) {
            finite = finite && std::isfinite(f[k]);
        }
        if (!finite || f[3] < 5.0f || f[4] < 5.0f || f[5] < 5.0f || f[3] > 2000.0f || f[4] > 2000.0f || f[5] > 2000.0f) {
            continue;
        }
        float const corner = std::sqrt(f[3] * f[3] + f[4] * f[4] + f[5] * f[5]);
        if (f[6] < corner * 0.5f || f[6] > corner * 2.5f) {
            continue;
        }
        ik::Vec3 const at{f[0], f[1], f[2]};
        if (Dist(at, origin) > 6000.0) {
            continue;
        }
        found += std::format("{}+0x{:X}: at {} extent ({:.0f}, {:.0f}, {:.0f}), {:.2f} m from the component's", count ? "; " : "",
            i * 4, F(at), f[3], f[4], f[5], Dist(at, origin) / kUnitsPerMeter);
        ++count;
    }
    return count ? found : std::string("none found");
}

std::string BodyLine(ue3::Object* controller, ue3::Object* pawn, std::optional<std::uint8_t> mode) {
    if (pawn == nullptr || !ue3::runtime::IsAlive(pawn)) {
        return "convo: body: no pawn";
    }
    auto const flag = [](ue3::FieldRef& field, void const* object) -> char {
        auto const v = object != nullptr ? field.ReadBool(object) : std::nullopt;
        return v ? (*v ? '1' : '0') : '?';
    };
    ue3::Object* const world = gWorldOfActor.Read<ue3::Object*>(pawn).value_or(nullptr);
    double const now = world != nullptr ? gWorldSeconds.Read<float>(world).value_or(0.0f) : 0.0;
    bool const scene = (gC.on && gC.world) || gMoment.on;
    ik::Vec3 const director = V(gPov.lastLocation);
    ik::Vec3 const view = scene ? gC.hold : director;
    ue3::Object* const mesh = player::Mesh(pawn);
    bool const meshOk = mesh != nullptr && ue3::runtime::IsAlive(mesh);
    std::string meshText = "none";
    if (meshOk) {
        void const* const bounds = gCompBounds.Address(mesh);
        auto const origin = bounds != nullptr ? gCompBoundsOrigin.Read<ue3::Vector>(bounds) : std::nullopt;
        auto const extent = bounds != nullptr ? gCompBoundsExtent.Read<ue3::Vector>(bounds) : std::nullopt;
        meshText = std::format("hiddenGame {} ownerNoSee {} onlyOwnerSee {} ignoreOwnerHidden {} ignoreMembership {}, rendered {:.2f} s ago, "
                               "bounds at {} extent {} ({:.2f} m from the view)",
            flag(gCompHiddenGame, mesh), flag(gCompOwnerNoSee, mesh), flag(gCompOnlyOwnerSee, mesh), flag(gCompIgnoreOwnerHidden, mesh),
            flag(gCompIgnoreMembership, mesh), now - gCompLastRender.Read<float>(mesh).value_or(0.0f),
            origin ? F(V(*origin)) : std::string("?"), extent ? F(V(*extent)) : std::string("?"),
            origin ? Dist(V(*origin), view) / kUnitsPerMeter : -1.0);
        if (bounds != nullptr && origin) {
            meshText += "; the renderer's box " + RendererBox(bounds, V(*origin));
        }
    }
    int listed = 0;
    bool inList = false;
    if (controller != nullptr) {
        if (auto const list = gHiddenActors.ElementsOf(controller, 4096)) {
            listed = list->count;
            for (std::int32_t i = 0; i < list->count && list->stride == static_cast<std::int32_t>(sizeof(void*)); ++i) {
                if (*static_cast<ue3::Object**>(list->At(i)) == pawn) {
                    inList = true;
                }
            }
        }
    }
    ue3::Object* const localPlayer = controller != nullptr ? gBodyLocalPlayer.Read<ue3::Object*>(controller).value_or(nullptr) : nullptr;
    float const nearClip = localPlayer != nullptr ? gBodyNearClip.Read<float>(localPlayer).value_or(-1.0f) : -1.0f;
    std::string cameras;
    {
        ue3::Reflection& r = ue3::runtime::Get();
        std::int32_t const n = r.ObjectCount();
        int found = 0;
        for (std::int32_t i = 0; i < n && found < 3; ++i) {
            ue3::Object* const o = r.ObjectAt(i);
            if (o == nullptr || !r.IsAlive(o) || !ue3::IsA(o, gConvCameraClass) || r.NameText(o).rfind("Default__", 0) == 0) {
                continue;
            }
            ++found;
            cameras += std::format(" [near {:.1f} fov {:.1f}]", gConvCameraNear.Read<float>(o).value_or(-1.0f),
                gConvCameraFov.Read<float>(o).value_or(-1.0f));
        }
    }
    auto const head = HeadOf(pawn);
    auto const at = player::Location(pawn);
    ue3::Object* const speaker = gC.on && gC.object != nullptr && ue3::runtime::IsAlive(gC.object)
        ? gSpeaker.Read<ue3::Object*>(gC.object).value_or(nullptr) : nullptr;
    return std::format(
        "convo: body: game mode {}, {}{} | pawn hidden {} hide requests {} rendered {:.2f} s ago, at {} | mesh {} | in HiddenActors {} "
        "({} listed) | head bone {} ({:.2f} m from the view) | view {} | director's camera {} ({:.2f} m from the view) | speaker {} "
        "| near plane {:.1f} (LocalPlayer), fov {:.1f} (the director's), conversation camera{}",
        mode ? static_cast<int>(*mode) : -1,
        gMoment.on ? (gMoment.provisional ? "a moment (provisional)" : "a moment") : gC.on ? (gC.world ? "a conversation in the world" : "a conversation") : "no scene",
        gC.detached ? ", the view away from the pawn" : "",
        flag(gActorHidden, pawn), gHiddenRequests.Read<std::int32_t>(pawn).value_or(-1),
        now - gActorLastRender.Read<float>(pawn).value_or(0.0f), at ? F(V(*at)) : std::string("?"), meshText, inList ? 1 : 0, listed,
        head ? F(head->at) : std::string("?"), head ? Dist(head->at, view) / kUnitsPerMeter : -1.0, F(view), F(director),
        Dist(director, view) / kUnitsPerMeter, speaker != nullptr ? ShortName(speaker) : std::string("-"), nearClip, gPov.lastFov,
        cameras.empty() ? std::string(" none") : cameras);
}

void BodyReport(ue3::Object* controller, ue3::Object* pawn, std::optional<std::uint8_t> mode) {
    if (!gBodyLogOverride.value_or(gSettings.bodyLog)) {
        return;
    }
    std::uint64_t const now = GetTickCount64();
    bool const scene = gC.on || gMoment.on || (mode && (*mode == kModeConversation || *mode == 6));
    if (now - gBodyLogMs < (scene ? 1000u : 2000u)) {
        return;
    }
    gBodyLogMs = now;
    log::Info("{}", BodyLine(controller, pawn, mode));
}

bool MomentWithholds() {
    // In a conversation shown in the world a matinee's move track never moves the player's pawn: his place is his own (the
    // walk, the opening's rule). Left to the track, keep-in-place put him back every tick and the track moved him again:
    // 464 times in Benezia's scene, each one a drawn jump of 0.5 m (2026-11-10).
    if (gC.on && gC.world && gC.openingDone && gInWorldForStay) {
        ++gC.trackHeld;
        return true;
    }
    if (!gSettings.moments || CurrentMode() != cw::Mode::World) {
        return false;
    }
    if (gSettings.momentCarry || gSettings.momentView) {
        return false;  // the track moves him: the view goes with him (carry), or is the player's own (view)
    }
    if (gMoment.on) {
        return true;
    }
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    return mode && *mode == 6 && FootFresh(GetTickCount64()) && !gC.on;
}

void TryStartup() {
    ue3::Function* const update = ue3::runtime::Ready() ? gUpdateCamera.Get() : nullptr;
    if (update == nullptr) {
        log::Warn("convo: Engine.Camera.UpdateCamera not found: conversations can't be shown in the world (measurements only)");
        return;
    }
    engine::process_event::AddPostHandler(update, &OnUpdateCamera);
    // The AI's walk to a mark: the game mode's movement ban answered for walkers (before the CallFunction hook goes in).
    ue3::Reflection& reflection = ue3::runtime::Get();
    for (ue3::Function* const f : {gAllowMovement.Get(), gAllowMovementHenchman.Get()}) {
        if (f != nullptr && reflection.Parameters(f).size() == 1 && reflection.ParmsSize(f) == 4) {  // only the bool return value
            engine::call_function::AddAnswerHandler(f, &OnAllowMovement);
            gAllowAnswerable = true;
        }
    }
    if (!gAllowAnswerable) {
        log::Warn("convo: BioAiController.DoesGameModeAllowMovement not found (or has parameters): walks to marks slide");
    }
}

// A scene whose shots are framed from far off is composed for a camera somewhere else (research/59's survey: the framed set
// pieces; research/69 item 3): at least three shots and two thirds of them from more than 8 m away. Once per conversation.
void CheckFarShots() {
    if (!gC.world || gSettings.autoScreen == 0 || !gC.absentWhy.empty() || gC.shots < 3 || gC.shotsFar * 3 < gC.shots * 2) {
        return;
    }
    gC.absentWhy = std::format("{} of the first {} shots are framed from more than 8 m away (the farthest {:.0f} m)", gC.shotsFar, gC.shots,
        gC.cameraFarM);
    // The far shots alone don't move a scene (research/69 item 10: of 100 scenes seen in the world they named the Council's hearing
    // and Benezia's death, both good in first person, and Kirrahe, a set piece already): only with worldAutoScreen = all.
    gC.autoScreened = gSettings.autoScreen == 3;
    log::Info("convo: at {:.1f} s {}: a scene composed for a camera elsewhere; {}", Seconds(), gC.absentWhy,
        gC.autoScreened ? "moved to the 3D screen (worldAutoScreen = all)" : "logged only, still in the world (worldAutoScreen = all would move it)");
    if (gC.autoScreened) {
        gC.setPiece = true;
        gC.world = false;  // people are no longer kept: the lines' facing tracks put them back on their marks
    }
}

bool WantsHooks() {
    return CurrentMode() == cw::Mode::World;
}

bool WantsScriptHook() {
    return gAllowAnswerable && gSettings.stay && gSettings.walk == 1 && CurrentMode() == cw::Mode::World;
}

void Tick(double deltaSeconds, Link const& link) {
    gLink = link;
    RefreshSettings();
    engine::conversation_hooks::SetKeepProvider(CurrentMode() == cw::Mode::World ? &KeepAtSource : nullptr);
    if (!gSettings.stay || gSettings.walk == 0) {
        EndWalks("worldStay or worldWalk disabled");
    }
    ue3::Object* const controller = player::Controller();
    auto const mode = controller != nullptr ? player::GameMode(controller) : std::nullopt;
    ue3::Object* const pawn = controller != nullptr ? player::Pawn(controller) : nullptr;
    if (mode && *mode == 0) {
        if (auto const frame = view_anchor::Current()) {
            gFoot.frame = frame;
            gFoot.floorView = vr::host_view::Latest();
        }
    }
    ResolveBlack(pawn);
    PublishVoices(pawn);
    TrackViewSpeed(deltaSeconds);
    BodyReport(controller, pawn, mode);
    ue3::Object* const conv = controller != nullptr && mode && *mode == kModeConversation ? ConversationNow(controller) : nullptr;
    // A moment in the world (a matinee that walks Shepard): its own view, until the cinematic mode ends or a conversation begins.
    if (conv == nullptr && TickMoment(link, pawn, mode, deltaSeconds)) {
        return;
    }
    if (gMoment.on) {
        EndMomentView(link, conv != nullptr ? "a conversation began" : gMoment.onFoot ? "the matinee's camera let go" : "the cinematic mode ended",
            conv == nullptr);
    }
    if (conv == nullptr) {
        End(mode && *mode == kModeConversation ? "no conversation object" : "the game left its conversation mode");
        StopWorld(link, "the conversation ended");
        Arm(link, mode && *mode == 0 && CurrentMode() == cw::Mode::World && FootFresh(GetTickCount64()));
        SetStatus(std::format("{{\"on\": false, \"mode\": \"{}\"}}", cw::Name(CurrentMode())));
        return;
    }
    if (!gC.on || gC.object != conv) {
        End("another conversation began");
        Begin(conv, controller);
    }
    // The player's place follows Shepard's head bone when it jumps (a stage's placement, a cut to a new mark): re-anchored past
    // kReanchorUnits, never for the head's own small moves (gestures, breathing), which the player's real head replaces.
    constexpr double kReanchorUnits = 80.0;  // 0.4 m: his own steps in the comm room moved the player 4 times (2026-11-06)
    auto const shepardHead = HeadOf(pawn);
    // Settled over 3 ticks: a pawn put back (people kept in place) drags his mesh there and back within a tick.
    constexpr int kSettleTicks = 3;
    if (shepardHead && gC.settleUntilMs != 0) {
        if (GetTickCount64() < gC.settleUntilMs) {
            gC.anchorHead = shepardHead->at;
            gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, {shepardHead->at.x, shepardHead->at.y, shepardHead->at.z}, 0};
        } else {
            gC.settleUntilMs = 0;
            gC.eyeToViewMax = 0.0;  // measured from here on: before, the stage had him at the mark and his mesh elsewhere
        }
    } else if (shepardHead && gC.anchorHead && gSettings.follow && gC.opening != cw::Opening::Cut &&
               Dist(shepardHead->at, *gC.anchorHead) < kStayMaxUnits) {
        // After an opening without a cut the player's place is his own: Shepard's body is where the player walked him, and what
        // his head bone does there (the hearing's animation moved it 0.88 m twice and the view went with it, 2026-11-10) is not
        // the player's move. Only a move past 6 m (the script put him elsewhere: a scripted entrance) is followed, below.
        gC.anchorHead = shepardHead->at;
        gC.follow = {{gC.hold.x, gC.hold.y, gC.hold.z}, {shepardHead->at.x, shepardHead->at.y, shepardHead->at.z}, 0};
    } else if (shepardHead && gC.anchorHead && gSettings.follow) {
        ik::Vec3 const a = *gC.anchorHead;
        ik::Vec3 const& h = shepardHead->at;
        if (cw::FollowHeadSettled(gC.follow, {h.x, h.y, h.z}, kReanchorUnits, kSettleTicks)) {
            log::Info("convo: at {:.1f} s Shepard's head bone JUMPED {:.2f} m: the player's place moves with it to ({:.0f}, {:.0f}, {:.0f})",
                Seconds(), Dist(h, a) / kUnitsPerMeter, gC.follow.place.x, gC.follow.place.y, gC.follow.place.z);
            gC.hold = {gC.follow.place.x, gC.follow.place.y, gC.follow.place.z};
            gC.anchorHead = h;
            ++gC.reanchors;
            if (gC.opening != cw::Opening::Cut) {
                ++gC.cut;  // the script moved him to another place in a scene that had no cut so far: under a dip
                gC.wantedMark.reset();
            }
        }
    }
    ik::Vec3 const eyes = gC.hold;
    std::uint64_t const now = GetTickCount64();
    if (now >= gNextSampleMs) {
        gNextSampleMs = now + 100;
        Sample(conv, controller, pawn, eyes);
        CheckFarShots();
    }
    if (gC.sourceKept > 0 && !gC.sourceLogged) {
        gC.sourceLogged = true;
        log::Info("convo: at {:.1f} s the game's placement of a person onto a mark {:.2f} m away was withheld at its source "
                  "(UBioConversation::SetPositionRotator; the facing applied): nobody jumps, nothing to undo", Seconds(),
            gC.sourceFarUnits / kUnitsPerMeter);
    }
    // ---- The world mode ----
    bool const world = CurrentMode() == cw::Mode::World && !gC.setPiece && link.publish != nullptr;  // live, as the 3D screen's setting
    if (!world) {
        Arm(link, false);  // not ours: the host shows it its own way from this tick (armed, it would wait for us)
        StopWorld(link, gC.setPiece ? "a set piece: on the 3D screen" : CurrentMode() == cw::Mode::World ? "an older host" : "not in the world mode");
        SetStatus(std::format("{{\"on\": true, \"world\": false, \"conversation\": {}, \"shots\": {}, \"speaker\": \"{}\"}}", gC.number,
            gC.shots, ShortName(gC.speaker)));
        return;
    }
    if (!gC.openingDone && (gInWorld || now >= gC.openingUntilMs)) {
        FinishOpening(conv, pawn);
    }
    // The room as the held view maps it: the host's view (its head pose and head look) placed on the held view, as view_anchor does
    // on the game camera; before the host shows the world (its first frames), the frame on foot moved to the held place.
    std::optional<engine::TrackingFrame> frame;
    vr::host_view::State const view = vr::host_view::Latest();
    if (view.valid && gInWorld) {
        frame = engine::FromCameraView(engine::UnrealVector{gC.hold.x, gC.hold.y, gC.hold.z},
            engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll}, {view.lookPitch, view.lookYaw, 0},
            {view.offsetForward, view.offsetRight, view.offsetUp}, view.head, view.unitsPerMeter);
        if (view.centreValid) frame->headWorld.z = view.centreZ;
        MovePlayer(pawn, *frame, view, deltaSeconds);  // the player's walk: the pawn, the hold and this frame move together
    } else if (gFoot.frame && gC.holdFrom == std::string_view("the view on foot")) {
        frame = gFoot.frame;
        frame->headWorld.x += gC.hold.x - gFoot.location.x;
        frame->headWorld.y += gC.hold.y - gFoot.location.y;
        frame->headWorld.z += gC.hold.z - gFoot.location.z;
        MovePlayer(pawn, *frame, view, deltaSeconds);  // without a VR view (a flat game): the held view's yaw, `mevr convo walk`
    }
    if (frame) {
        view_anchor::Hold(frame, gFoot.floorView);
        gHeld = true;
    }
    if (shepardHead && !gC.detached) {  // (while the view walks to him the distance is the walk's, not a fault)
        gC.eyeToViewMax = std::max(gC.eyeToViewMax, Dist(shepardHead->at, eyes));
    }
    cw::State state;
    state.active = 1;
    state.conversation = gC.number;
    state.speakerSerial = gC.speakerSerial;
    state.wheel = gC.wheel ? 1 : 0;
    state.wheelHand = static_cast<std::uint8_t>(gSettings.wheelHand);
    state.wheelWidthM = gSettings.wheelWidthM;
    state.wheelDistanceM = gSettings.wheelDistanceM;
    state.subtitleHeightDeg = gSettings.subtitleHeightDeg;
    std::copy(std::begin(gSettings.subtitleCrop), std::end(gSettings.subtitleCrop), std::begin(state.subtitleCrop));
    std::copy(std::begin(gSettings.wheelCrop), std::end(gSettings.wheelCrop), std::begin(state.wheelCrop));
    // The subtitle under the speaker's face; Shepard's own lines under the one he talks to.
    ue3::Object* face = gC.speaker;
    if (face == pawn || face == nullptr) {
        face = gListener.Read<ue3::Object*>(conv).value_or(nullptr);
    }
    if (face != nullptr && face != pawn && frame) {
        if (auto const point = FacePoint(face)) {
            XrVector3f const faceT = engine::ToTracking(*frame, engine::UnrealVector{point->x, point->y, point->z});
            XrVector3f out{};
            if (cw::SubtitlePoint(faceT, frame->head.position, gSettings.subtitleBelowM, gSettings.subtitleTowardM, out)) {
                state.subtitle = 1;
                state.subtitleAt[0] = out.x;
                state.subtitleAt[1] = out.y;
                state.subtitleAt[2] = out.z;
            }
        }
    }
    state.cut = gCutBase + gC.cut;
    if (gPov.location != nullptr && now - gPov.ms < 500) {
        state.holdRender = 1;
        state.holdLocation[0] = static_cast<float>(gC.hold.x);
        state.holdLocation[1] = static_cast<float>(gC.hold.y);
        state.holdLocation[2] = static_cast<float>(gC.hold.z);
        state.holdRotation[0] = gC.holdRotation.pitch;
        state.holdRotation[1] = gC.holdRotation.yaw;
        state.holdRotation[2] = gC.holdRotation.roll;
        state.holdFov = gC.holdFov;
        state.povLocationAddress = reinterpret_cast<std::uint64_t>(gPov.location);
        state.povRotationAddress = reinterpret_cast<std::uint64_t>(gPov.rotation);
        state.povFovAddress = reinterpret_cast<std::uint64_t>(gPov.fov);
    }
    gCameraHold.on = gSettings.camera && state.holdRender != 0;
    gNearHold = gSettings.nearPlane && state.holdRender != 0;
    gCameraHold.location = {static_cast<float>(gC.hold.x), static_cast<float>(gC.hold.y), static_cast<float>(gC.hold.z)};
    gCameraHold.rotation = gC.holdRotation;
    gCameraHold.fov = gC.holdFov;
    DofOff();
    link.publish(&state);
    gPublished = true;
    ++gWorldTicks;
    if (gInWorldForStay && gSettings.stay) {
        CheckDrawn();
    }
    gInWorldForStay = true;
    if (!gInWorld) {
        gInWorld = true;
        auto const& v = gFoot.floorView;
        auto const basis = engine::ToBasis(engine::UnrealRotation{gC.holdRotation.pitch, gC.holdRotation.yaw, gC.holdRotation.roll});
        double const commonZ = basis.right.z * v.offsetRight + basis.up.z * v.offsetUp + basis.forward.z * v.offsetForward;
        log::Info("floor lock: conversation hold start: locked {}, held base Z {:.3f}, common Z {:+.3f}, "
                  "render centre Z {:.3f}, IK head Z {:.3f}, floor Z {:.3f}",
            v.floorLocked, gC.hold.z, commonZ, gC.hold.z + commonZ, frame ? frame->headWorld.z : 0.0, v.pawnFloorZ);
        log::Info("convo: conversation {} in the world: the view held at {} ({}, yaw {:.0f}){}; arm IK and body follow {}", gC.number,
            F(gC.hold), gC.holdFrom, YawDeg(gC.holdRotation), gSettings.follow ? ", following his head bone's jumps" : "",
            frame ? "on" : "waiting for the host's world view");
    }
    SetStatus(std::format("{{\"on\": true, \"world\": true, \"conversation\": {}, \"shots\": {}, \"speaker\": \"{}\", \"wheel\": {}, "
                          "\"subtitle\": {}, \"subtitleAt\": [{:.3f}, {:.3f}, {:.3f}], \"held\": [{:.1f}, {:.1f}, {:.1f}], \"heldRender\": {}, "
                          "\"reanchors\": {}, \"frame\": {}, \"from\": \"{}\", \"kept\": {}, \"letGo\": {}, \"walks\": {}}}",
        gC.number, gC.shots, ShortName(gC.speaker), state.wheel != 0 ? "true" : "false", state.subtitle != 0 ? "true" : "false",
        state.subtitleAt[0], state.subtitleAt[1], state.subtitleAt[2], gC.hold.x, gC.hold.y, gC.hold.z, state.holdRender != 0 ? "true" : "false",
        gC.reanchors, frame ? "true" : "false", gC.holdFrom, gC.kept, gC.letGo, gC.walks));
}

bool InWorld() {
    return gInWorld;
}

std::string Command(std::string const& rest) {
    std::istringstream words(rest);
    std::string verb;
    words >> verb;
    if (verb == "world") {
        std::string what;
        words >> what;
        if (what == "off" || what == "file") {
            gModeOverride.reset();
            return std::format("convo: the mode from the file again: {}", cw::Name(gSettings.mode));
        }
        gModeOverride = what == "on" || what.empty() ? cw::Mode::World : cw::ParseMode(what.c_str(), cw::Mode::World);
        return std::format("convo: conversations {} until `mevr convo world off` (now)", cw::Name(*gModeOverride));
    }
    if (verb == "body") {
        // `mevr convo body [log on|off]`: every flag that stops Shepard being drawn, now; `log`: the same line once a second.
        std::string what, value;
        words >> what >> value;
        if (what == "log") {
            gBodyLogOverride = value != "off";
            return std::format("convo: the body's visibility is {} (`convo: body:`)", *gBodyLogOverride ? "logged once a second in scenes" : "no longer logged");
        }
        ue3::Object* const controller = player::Controller();
        return BodyLine(controller, controller != nullptr ? player::Pawn(controller) : nullptr,
            controller != nullptr ? player::GameMode(controller) : std::nullopt);
    }
    if (verb == "walk") {
        // Tests without controllers: `mevr convo walk <x> <y> [seconds]` is the left stick for that long (default 3 s); `walk off`.
        std::string x;
        words >> x;
        if (x == "off" || x.empty()) {
            gWalkOverride = {};
            return x.empty() ? std::string("usage: mevr convo walk <x> <y> [seconds] | off") : std::string("convo: the walk override is off");
        }
        double sx = 0.0, sy = 0.0, seconds = 3.0;
        std::string y, s;
        words >> y >> s;
        auto const px = ini::Number(x), py = ini::Number(y);
        if (!px || !py) {
            return "usage: mevr convo walk <x> <y> [seconds] | off";
        }
        sx = std::clamp(*px, -1.0, 1.0);
        sy = std::clamp(*py, -1.0, 1.0);
        if (auto const ps = s.empty() ? std::nullopt : ini::Number(s)) {
            seconds = std::clamp(*ps, 0.1, 60.0);
        }
        gWalkOverride = {static_cast<float>(sx), static_cast<float>(sy), GetTickCount64() + static_cast<std::uint64_t>(seconds * 1000.0)};
        return std::format("convo: the stick is ({:.2f}, {:.2f}) for {:.1f} s (worldMove {}, world mode {}, conversation {})", sx, sy, seconds,
            gSettings.move ? "on" : "off", gInWorld ? "up" : "not up", gC.on ? "running" : "none");
    }
    if (verb == "pick" || verb == "skip") {
        // Tests without controllers: the wheel's own reply pick (a wheel location 0..5, as the stick and A choose) or its skip
        // of the current line (what A / X do while a line plays).
        ue3::Object* const handler = WheelHandler(player::Controller());
        ue3::Function* const f = verb == "pick" ? gSelectEntry.Get() : gSkipConversation.Get();
        if (handler == nullptr || f == nullptr) {
            return std::format("convo: no conversation wheel handler{}", f == nullptr ? " (or its function wasn't found)" : "");
        }
        // Only on the game's own terms: a pick while the wheel is up and on a slot it uses, a skip while a line plays. A pick
        // between lines crashed the game inside the script (2026-11-06 12:23, research/59: it reads an empty reply list).
        bool const wheelUp = gDisplayingWheel.ReadBool(handler).value_or(false);
        int location = 0;
        if (verb == "pick") {
            words >> location;
            location = std::clamp(location, 0, 5);
            std::uint8_t const* const used = gSlotsUsed.At<std::uint8_t>(handler, location);
            if (!wheelUp || used == nullptr || *used == 0) {
                return std::format("convo: no pick: {}", !wheelUp ? "the wheel isn't up" : std::format("slot {} isn't used", location));
            }
        } else if (wheelUp) {
            return "convo: no skip: the wheel is up (pick a reply)";
        }
        ue3::Params parms(ue3::runtime::Get(), f);
        if (verb == "pick") {
            parms.Set("nWheelLocation", static_cast<std::uint8_t>(location));
        }
        bool const called = ue3::runtime::Call(handler, f, parms);
        return std::format("convo: {} called {}{}", verb == "pick" ? std::format("SelectConversationEntry({})", location) : "SkipConversation",
            called, parms.ReturnBool() ? std::format(", returned {}", *parms.ReturnBool()) : std::string());
    }
    if (verb == "end") {
        // The conversation's own end (tests: a survey's conversation that loops past its time).
        if (!gC.on || gC.object == nullptr || !ue3::runtime::IsAlive(gC.object) || gEndConversation.Get() == nullptr) {
            return "convo: no conversation to end";
        }
        ue3::Params p(ue3::runtime::Get(), gEndConversation.Get());
        bool const called = ue3::runtime::Call(gC.object, gEndConversation.Get(), p);
        return std::format("convo: EndConversation called {}", called);
    }
    if (verb == "dump" || verb.empty()) {
        auto const gameMode = player::GameMode(player::Controller());
        std::string out = std::format("convo: mode {}; game mode {}; ", cw::Name(CurrentMode()), gameMode ? static_cast<int>(*gameMode) : -1);
        if (!gC.on) {
            return out + "no conversation";
        }
        out += std::format("conversation {} ({}), {:.1f} s, {} shots, speaker {}, entry {}, wheel {}; actors:", gC.number, gC.name,
            Seconds(), gC.shots, ShortName(gC.speaker), gC.entry, gC.wheel ? "up" : "down");
        for (Tracked const& t : gC.actors) {
            auto const at = ue3::runtime::IsAlive(t.actor) ? player::Location(t.actor) : std::nullopt;
            out += std::format(" {} {}", t.name, at ? F(V(*at)) : std::string("gone"));
        }
        return out;
    }
    return "usage: mevr convo [dump | world on|off|screen|vr | pick <0..5> | skip | end]";
}

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    return gStatusJson;
}

void LogStats() {
    if (gInWorld) {
        log::Info("convo: in the world for {} ticks (conversation {}, {} shots so far)", gWorldTicks, gC.number, gC.shots);
    }
    if (gAiStalled != 0) {
        log::Info("convo: walk: {} walks to a mark the game's AI couldn't make since the last line like this (they stayed)", gAiStalled);
        gAiStalled = 0;
    }
    if (engine::voice_place::Installed()) {
        engine::voice_place::Stats const v = engine::voice_place::Take();
        if (v.voices != 0 || v.placed != 0 || v.offThread != 0 || v.faults != 0) {
            log::Info("convo: voices: {} people's voices placed now; {} of {} sound source updates placed this period{}{}", v.voices, v.placed,
                v.updates, v.offThread != 0 ? std::format("; {} seen on another thread and left alone", v.offThread) : "",
                v.faults != 0 ? std::format("; {} faults in the hook (after three it places nothing)", v.faults) : "");
        }
    }
}

void Shutdown(Link const& link) {
    ClearVoices();
    engine::conversation_hooks::SetKeepProvider(nullptr);
    End("the mod unloads");
    StopWorld(link, "the mod unloads");
}

} // namespace mevr::game::conversation_world
