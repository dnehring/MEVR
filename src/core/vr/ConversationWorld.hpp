// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/InteractLabel.hpp"

#include <openxr/openxr.h>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// LE1 conversations played in the world, in first person (research/59, `[Cinematics] conversations = world`): during a
// conversation (game mode 5) the player keeps their own head-tracked view at Shepard's place instead of the director's camera.
// The game runs the conversation unchanged (its camera included); the mod holds the main view at Shepard's eyes at render time
// (as the galaxy map and the world screens do), and the host keeps the world in VR, puts the conversation movie's UI on the HUD
// panel and cuts its two parts out of it: the subtitle under the speaker's face, the dialogue wheel at the left hand (its stick
// steers it). Shared by the host (plugin/ConversationWorld, plugin/HalcyonOverlays) and the mod (game/ConversationWorld); the
// pure parts are unit-tested.
namespace mevr::vr::conversation_world {

// ---- What the mod publishes (HostServices v33 setConversationWorld) -----------------------------------------------------------
struct State {
    std::uint32_t size = sizeof(State);  // the publisher's struct size: host and mod must match
    std::uint8_t active = 0;      // a conversation the mod plays in the world: the host shows the world as on foot
    std::uint8_t wheel = 0;       // the dialogue wheel is up (BioSFHandler_Conversation.m_bDisplayingWheel)
    std::uint8_t subtitle = 0;    // subtitleAt holds where the speaker's line goes
    std::uint8_t wheelHand = 0;   // 0 the left hand, 1 the right, 2 in front of the head
    float subtitleAt[3] = {};     // tracking space, metres: the subtitle's centre (under the speaker's face)
    float subtitleHeightDeg = 0.0f;  // the subtitle band's angular height ([Cinematics] subtitleHeightDeg)
    float subtitleCrop[4] = {};   // x, y, width, height of the HUD frame; width 0: DefaultSubtitleCrop
    float wheelCrop[4] = {};      // the same for the wheel; width 0: DefaultWheelCrop
    float wheelWidthM = 0.0f;     // the wheel's quad width
    float wheelDistanceM = 0.0f;  // above the hand (the weapon wheel's placement)
    std::uint32_t speakerSerial = 0;  // changes when the speaker does (logs)
    std::uint32_t conversation = 0;   // counts the conversations: a new one is placed again
    // The view held at Shepard's at render time (the director's camera keeps serving the game): the host swaps these into the
    // player camera's POV around the main view's CalcSceneView (Halcyon's [MEVR] seam, as the galaxy map's hold does).
    std::uint8_t holdRender = 0;
    // holdPad[0]: "armed" (with active 0): the player is on foot and the mod would show a conversation or a moment that begins
    // now in the world. The game's mode changes a tick before the mod's state can follow: armed, the host keeps the on-foot
    // presentation over that tick instead of showing the 3D screen for it (a flash and two dips at every scene's start).
    std::uint8_t holdPad[3] = {};
    std::uint32_t cut = 0;  // counts the player's re-placements inside the conversation (the opening): the host dips from black on a change
    float holdLocation[3] = {};
    std::int32_t holdRotation[3] = {};  // pitch, yaw, roll (Unreal units)
    float holdFov = 0.0f;
    std::uint64_t povLocationAddress = 0;  // FVector
    std::uint64_t povRotationAddress = 0;  // FRotator
    std::uint64_t povFovAddress = 0;       // float
    std::uint64_t sequence = 0;   // set by Publish
    std::uint64_t timeMs = 0;     // GetTickCount64 when published (set by Publish)
};

void Publish(State const& state);
// The latest state if active and published within `maxAgeMs`.
std::optional<State> Latest(std::uint64_t maxAgeMs = 300);
// The mod's last word was "armed" (State::holdPad[0] with active 0) and no older than maxAgeMs: on foot, and a scene that
// begins now would be shown in the world.
bool Armed(std::uint64_t maxAgeMs = 150);
void Reset();

// ---- [Cinematics] conversations ----------------------------------------------------------------------------------------------
enum class Mode : std::uint8_t {
    Vr = 0,      // Halcyon's VR cine (the director's camera filling the view)
    Screen = 1,  // the 3D cinema screen (research/52)
    World = 2,   // in the world, first person (research/59)
};
// "world", "screen", "vr" or a number (0 = VR cine, 1 = the 3D screen, 2 = world); anything else: `fallback`.
Mode ParseMode(char const* text, Mode fallback);
char const* Name(Mode mode);

// ---- The conversation movie's parts on the HUD frame -------------------------------------------------------------------------
// The conversation movie (GUI_SF_Conversation) shows its 1280x720 stage across the HUD frame's width, centred top to bottom
// (measured 2026-11-06, research/59; unlike the HUD movie, whose bottom widgets sit on the frame's bottom, research/41). Authored
// places (pchud.cpp, from the movie's SWF): SubtitleConversation at (639, 605.6), ConversationWheel at (639.7, 643.3); measured:
// the wheel's rows at stage y 590..680, a one-row line at 564..575.
// The subtitle's band: the line (up to three rows) above the wheel.
interact::Rect DefaultSubtitleCrop(std::uint32_t width, std::uint32_t height);
// The wheel's band around its centre (the six replies' text reaches about 260 stage px to each side of the ring).
interact::Rect DefaultWheelCrop(std::uint32_t width, std::uint32_t height);

// ---- Placement -----------------------------------------------------------------------------------------------------------------
// Where the subtitle goes for a speaker: `belowM` under the face (the head bone), moved `towardM` toward the eyes so it isn't
// inside the speaker's body, in tracking space. False if the speaker is at the eyes.
bool SubtitlePoint(XrVector3f const& face, XrVector3f const& eyes, float belowM, float towardM, XrVector3f& out);

// The player's place follows Shepard's head bone when it jumps (a stage placing him, a cut to a new mark), never for the head's
// own small moves (gestures, breathing: the player's real head replaces them): past `threshold` (Unreal units) from where it was
// when the place was last set, the place moves by the same jump and the head is the new anchor. Unreal units.
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};
struct Follow {
    Vec3 place;
    Vec3 anchor;
    bool jumped = false;
};
Follow FollowHead(Vec3 const& place, Vec3 const& anchor, Vec3 const& headNow, double threshold);
// The same, taken only once the head has stayed past `threshold` from the anchor for `settle` calls in a row (once a tick): when
// the mod puts Shepard's pawn back after the game's placement (people kept in place), his mesh catches up a tick later, so his
// head bone reads a jump there and back within a tick (2 m and back, measured 2026-11-06); a lasting jump (his mesh put back on
// his pawn) is followed. True when it moved the place.
struct FollowState {
    Vec3 place;
    Vec3 anchor;
    int away = 0;  // calls in a row past the threshold
};
bool FollowHeadSettled(FollowState& state, Vec3 const& headNow, double threshold, int settle);

// ---- Walking to lasting stage marks (prototype) ------------------------------------------------------------------------------
// Positions/distances are in Unreal units; times are monotonic seconds. Each observed native placement is a NEW request,
// even for the same mark. End consumes it (arrival, timeout or cancellation): no retry until another placement.
enum class WalkPhase { Idle, Waiting, Walking };
class MarkWalk {
public:
    void Jump(Vec3 wanted, Vec3 standing, double nowS, double minUnits);
    bool StartIfSettled(Vec3 standing, double nowS, double settleS, double minUnits);
    void End() { phase_ = WalkPhase::Idle; }
    WalkPhase Phase() const { return phase_; }
    Vec3 Wanted() const { return wanted_; }
    double WantedAtS() const { return wantedAtS_; }

private:
    Vec3 wanted_;
    double wantedAtS_ = 0.0;
    WalkPhase phase_ = WalkPhase::Idle;
};

struct WalkStep {
    Vec3 place;
    Vec3 velocity;  // units per second, from the actual step (including the shorter final step)
    bool arrived = false;
};
WalkStep StepTowardMark(Vec3 standing, Vec3 mark, double speedUnitsPerS, double dtS);
// Distance outside the segment a walk could have covered since the last observation; tolerates collision stopping short.
double BeyondWalk(Vec3 standing, Vec3 actual, Vec3 mark, double speedUnitsPerS, double elapsedS);

// ---- Everyone in sight (research/59 "Part B") ----------------------------------------------------------------------------------
// From the player's eyes at `player` facing `facingYawDeg` (degrees, Unreal's yaw about Z, 0 along +X): is `person` within
// `halfAngleDeg` of the facing and between `minUnits` and `maxUnits` away (horizontally)? Line of sight is the game's.
bool InView(Vec3 const& player, double facingYawDeg, Vec3 const& person, double halfAngleDeg, double minUnits, double maxUnits);
// Seats for `count` people who must come into view: on an arc of `radiusUnits` around the player, centred on the owner's
// bearing (the owner keeps its place in the middle), alternately right and left of it, `spacingDeg` apart, never beyond
// `halfAngleDeg`; each seat faces the player. The arc lies in the player's horizontal plane (the caller finds the floor).
struct ArcSeat {
    Vec3 at;
    double yawDeg = 0.0;  // facing the player
};
std::vector<ArcSeat> ArcSeats(Vec3 const& player, Vec3 const& owner, int count, double radiusUnits, double spacingDeg, double halfAngleDeg);

// The set pieces (research/59 "The scan", "Marking the rest"): the 21 conversations whose lines are a quarter or more authored
// camera work (a director track or a moving camera: Saren, Sovereign, the Council, Anderson's briefing...) or where the game walks
// Shepard himself (the Council endings, Kirrahe): composed for a camera elsewhere, or moving the player's place.
// In the world mode they go to the 3D screen (`[Cinematics] worldSetPieces = screen`). `name` is the conversation's object
// name in any form ("BioConversation nor10_comm_room_d.nor10_comm_room_dlg", "nor10_comm_room_dlg", "nor10_comm_room"), any case.
bool IsSetPiece(std::string_view name);
// The conversation's key in the tables above: the object's own name, lower case, without its "_dlg"/"_d" suffix
// ("BioConversation sta70_garrus_D.sta70_garrus_dlg" -> "sta70_garrus").
std::string ConversationKey(std::string_view name);

// ---- Flowing conversations (research/59 "Flowing conversations", 2026-11-09) ---------------------------------------------------
// A conversation's play from a start entry that can never reach a dialogue wheel (a scene watched: Garrus arguing with the
// Executor, Udina's arrival, the trigger lines on a walk up) is a chain. The game plays a start whose first entry is flagged
// ambient as its own barks (UBioConversation::StartConversation copies the start entry's bAmbient and skips the conversation
// mode, the camera and the stage; the first-person player keeps walking, within the interrupt range), so the mod flags the
// chain starts' entries before the level's script starts them. The entries per conversation come from the scan's
// `--flow-list` (chain starts' entries no conversation start reaches, left ordinary by the game; set pieces, transmissions and
// the game's own narration left out): empty for a conversation not in the table. `name` as IsSetPiece takes it.
std::span<int const> FlowEntries(std::string_view name);

// ---- Walking during a conversation (research/59 "Walking during conversations", 2026-11-09) ---------------------------------
// The owner: a conversation should feel like the game itself, where "I can somewhat move around", with "some sort of barrier
// to ensure that the conversation just doesn't get ejected". In the world mode the mod moves Shepard's pawn and the held view
// by the left stick while the wheel is down: this is the step. The stick (x right, y forward, -1..1; a dead zone of 0.15) moves
// `place` along the head's world yaw (`headYawDeg`, Unreal's: 0 along +X, 90 along +Y) at `speedUnitsPerS` for `dtS`, kept
// within `leashUnits` of `anchor` horizontally: a step that would leave the circle slides along it (`atLeash`), and a place
// already outside (the anchor moved) may only come nearer. The z is the caller's (the floor). `moved` is false for a stick in
// the dead zone or no time.
struct PlayerStep {
    Vec3 place;
    bool moved = false;
    bool atLeash = false;
};
PlayerStep StepPlayer(Vec3 const& place, Vec3 const& anchor, double leashUnits, double stickX, double stickY, double headYawDeg,
    double speedUnitsPerS, double dtS);

// A walk-in nobody walks (research/69 "The guided walk-in", 2026-11-10): the opening left the player where he stood and
// wants him at Shepard's mark; a player who does not use the stick for `patienceS` is walked there at the walk's own pace,
// by the same step as the stick's (floor, walls, people, triggers). The stick takes the walk back at once and starts the
// wait again. Blocked for `blockedMaxS` in a row (a wall, a ledge, a trigger's edge), the guide gives up for this
// conversation: he hears it from where he is. patienceS < 0: never guided.
struct GuideState {
    double idleS = 0.0;
    double blockedS = 0.0;
    bool guiding = false;
    bool gaveUp = false;
};
enum class Guide : std::uint8_t { Wait, Walk, GaveUp };
// `wanted`: a mark is wanted and he is not there. `stickUsed`: the player's own stick is out of its dead zone.
// `progressed`: the last guided step moved him (ignored unless the last answer was Walk).
Guide StepGuide(GuideState& state, bool wanted, bool stickUsed, bool progressed, double dtS, double patienceS, double blockedMaxS = 2.0);

// Who turns, and how (research/69 "Who turns, and how"; the owner's headset notes, 2026-11-10: everyone pivoting on the spot
// toward each speaker looked unnatural, a twist should look like the body's own, and the composition should carry the scene).
// The game's own look-at chain turns a person's eyes, head, neck and chest toward whom they look at (HMM_PLR_lookAt_Def: 25,
// 50, 50 and 50 degrees, at 1, 0.55, 0.25 and 0.125 strength in a conversation), so the body keeps the facing the scene
// composed and re-faces only when its look-at target has been more than `comfortDeg` off its facing for `dwellS`; then it
// turns only until the target is `settleDeg` off: the body ends between the people it attends to, and the next change of
// speaker needs no turn.
struct AttentionLimits {
    double comfortDeg = 60.0;
    double settleDeg = 25.0;
    double dwellS = 1.0;
};
struct AttentionState {
    double outsideS = 0.0;
    bool turning = false;
};
struct AttentionStep {
    bool began = false;      // this step started a body turn
    bool turning = false;    // the body is turning, toward goalYawDeg
    double goalYawDeg = 0.0;
    double offDeg = 0.0;     // the target's bearing off the body's facing, signed
};
// `bearingDeg`: the yaw from the person to whom they look at; nullopt: nobody (the body stays, the state starts over).
AttentionStep StepAttention(AttentionState& state, double bodyYawDeg, std::optional<double> bearingDeg, double dtS, AttentionLimits const& limits = {});
// One tick of a body turn nothing animates (the fallback when the game's own turn is refused): at most rateDegPerS, slower
// near the goal, never under a fifth of the rate (so it ends). Returns the signed step toward diffDeg.
double TurnStepDeg(double diffDeg, double dtS, double rateDegPerS);
enum class TurnRule : std::uint8_t { Roles, Look, Always, Off };
// "roles" (the default since 2026-11-10 evening): the rule above, by each person's part in the line (below). "look": the
// rule above for whoever the game gives a look-at target, whatever their part. "always": the body turns toward the look-at
// target every tick (until 2026-11-10). "off": the game's own facings stand.
TurnRule ParseTurnRule(std::string_view text, TurnRule fallback);

// Who attends to whom, by their part in the line (research/69 "Who attends to whom"; the owner, 2026-11-10: "when Udina
// speaks there's no reason that Anderson would need to face him as much"). The game names each line's speaker and whom
// they speak to (BioConversation.m_pSpeaker, m_pListener); everyone else in the scene is a bystander. One rule for every
// scene, no data per scene:
//  - the speaker looks at whom they speak to, and the one spoken to at the speaker: an engaged pair, whose bodies may
//    re-face (StepAttention) when the other stays outside what the look-at chain covers;
//  - a bystander looks at the speaker only where a glance reaches (the speaker within `glanceDeg` of the body's facing, kept
//    until past `glanceKeepDeg`), a moment later than the pair, and its body never turns: it keeps the facing the scene
//    composed. Someone speaking behind or beside a bystander is heard, not looked at.
// A look-at target the game itself gives someone (an authored look-at track) stands in place of the part's.
enum class Role : std::uint8_t { Speaker, Addressed, Bystander };
char const* Name(Role role);
// Ids are opaque (pointers); 0 is nobody. Someone who is both (a line spoken to oneself) is the speaker.
Role RoleOf(std::uint64_t who, std::uint64_t speaker, std::uint64_t listener);
// Whom that part attends to: the speaker to the listener, everyone else to the speaker; 0: nobody (no speaker, or oneself).
std::uint64_t PartnerOf(std::uint64_t who, std::uint64_t speaker, std::uint64_t listener);
struct RoleLimits {
    double glanceDeg = 70.0;
    double glanceKeepDeg = 85.0;
    double pairDelayS = 0.15;       // the engaged pair turn their heads this long after the line began ...
    double bystanderDelayS = 0.45;  // ... a bystander this long, plus up to bystanderSpreadS by who they are (not in unison)
    double bystanderSpreadS = 0.6;
    // One of the pair whose body cannot turn for it looks at the other only within this of its body's facing: past it the
    // look-at chain cranks the head over the shoulder as far as it goes (the ceremony's first line: Udina toward a player
    // 8 m behind him).
    double pairReachDeg = 120.0;
};
struct RoleState {
    bool glancing = false;  // a bystander is looking at the speaker
};
struct RoleAttention {
    bool look = false;         // looks at its partner (the engaged pair: always, when there is one)
    bool bodyMayTurn = false;  // its body may re-face toward whom it looks at (never a bystander's)
    double delayS = 0.0;       // how long after the partner changed the look begins
};
// `partnerOffDeg`: the partner's bearing off the body's facing (signed or not); nullopt: no partner. `bodyCanTurn`: whether
// the body may turn for this partner at all (the game layer's: only for the player, only near); returned as bodyMayTurn
// for one of the pair.
RoleAttention AttendAs(RoleState& state, Role role, std::uint64_t who, std::optional<double> partnerOffDeg, bool bodyCanTurn = true,
    RoleLimits const& limits = {});
// How far the game's look-at chain leaves the head short of a target `offDeg` off the body's facing (read from the game's
// code, research/69 "How far a head turns"): each link, from the chest to the head, turns by its strength times what is
// left, what is left taken as at most the link's limit (`links`: strength and limit per link, chest first; the humanoid
// definition: 0.125/50, 0.25/50, 0.55/50), the eyes (25 degrees) taking the rest. Measured in the game at 77 to 79 degrees
// off the body: 21 to 36 short (the animation's own twist adds to it). Returns what is left for the eyes, in degrees.
// StrongerLink: a link's strength with `gain` of the rest added (the pair's heads reach further: 0.55 with 0.4 is 0.73).
struct LookLink {
    double strength = 0.0;
    double limitDeg = 0.0;
};
double HeadShortDeg(double offDeg, std::span<LookLink const> links);
double StrongerLink(double strength, double gain);

// Where a speaker's voice comes from (research/69 "The voices, placed"; the owner, 2026-11-10: "for the council we would
// expect it to sound a little like an intercom. But for other scenes it should be spatial"). The game plays most dialogue
// in the centre, wherever the speaker stands. Someone who speaks within `withinUnits` of the listener (kept until past
// `keepUnits`, so a step back mid-line changes nothing) is heard from where their head is; someone further away is left
// as the game mixes it: in the centre and at full level, a voice carried across a hall. The audio engine only pans a
// placed voice (measured: one 8.6 m away is as loud as the centred mix), so the loudness is ours: full within `nearUnits`,
// then `falloffDb` quieter for each doubling of the distance, at most `floorDb` quieter (half the law of the open air:
// a conversation stays a conversation).
struct VoiceLimits {
    double withinUnits = 800.0;
    double keepUnits = 1000.0;
    double nearUnits = 200.0;
    double falloffDb = 3.0;
    double floorDb = 6.0;
};
struct VoiceState {
    bool placed = false;
};
struct VoiceAt {
    bool placed = false;        // false: the game's own mix (the centre)
    Vec3 at;                    // where the voice is put: the speaker's head (placed only)
    double gain = 1.0;          // times the game's volume (placed only)
    double distanceUnits = 0.0; // the speaker's head from the listener
};
VoiceAt PlaceVoice(VoiceState& state, Vec3 const& listener, Vec3 const& head, VoiceLimits const& limits = {});

// The left stick steers the conversation wheel while it is up, and is still where it picked the reply as the wheel closes:
// it walks again only once it has been let go (the owner's headset session, 2026-11-10: Shepard lurched half a metre in the
// reply's direction after every pick). True: the stick may walk the player this tick.
struct StickLatch {
    bool held = false;
};
bool StickWalks(StickLatch& latch, bool wheelUp, double magnitude, double releasedBelow = 0.25);

// What a script's black screen covered (research/69 "The second blink"): the people shown in the player's view before it
// and after it. Someone gone, moved at once or new in view wants the mod's dip in the black screen's place; nothing changed
// wants nothing (the Spectre ceremony's black screen covered a teleport the first-person view never makes: a blink in the
// middle of the player's own walk, for nothing).
struct Seen {
    std::uint64_t id = 0;
    Vec3 at;
};
struct Covered {
    int gone = 0;
    int moved = 0;
    int appeared = 0;
    int Total() const { return gone + moved + appeared; }
};
Covered CoveredChanges(std::span<Seen const> before, std::span<Seen const> after, double movedUnits = 150.0);
// How near the nearest of those changes is to `eye` (units; infinity: no change): someone gone or moved counts from where
// they stood, someone new from where they stand. A blink is worth it for a change near the player; people appearing at the
// far end of a hall are not (the owner, 2026-11-10: the blink on the Council's walkway, in the middle of his own walk,
// "wasn't that comfortable"; what it covered was the Council appearing 20 m ahead).
double NearestChange(std::span<Seen const> before, std::span<Seen const> after, Vec3 const& eye, double movedUnits = 150.0);

// The way Shepard went (research/69 "The script's Shepard, the player's view"): while the player's view walks to a pawn the
// script moved, the guided walk follows the places the pawn passed through (stairs, a walkway between railings) instead of
// the straight line to him, which a ledge or a railing blocks. Where to head for from `at`: the trail's point `aheadUnits`
// along it past the one nearest to `at`; nullopt once the trail's end is that near (then: the pawn himself).
std::optional<Vec3> TrailTarget(std::span<Vec3 const> trail, Vec3 const& at, double aheadUnits = 150.0);

// How a walk-in is walked (research/69 "The view walks", 2026-11-10). Body: Shepard is put back under the player and the
// two walk to the mark together (his body under the player; every step moves the pawn, so it ends and begins touches,
// which is why it stops at trigger edges, and why putting him back ended the council's hearing). View: Shepard stays
// where the stage has him, hidden, and only the player's view walks to him (no move of the pawn, so nothing of the
// level's is touched; no body under the player until he arrives). Auto: the view when a moment's walk was still on its
// way as the conversation began, or when the mark stands inside a trigger the player is outside of; the body otherwise.
enum class WalkInRule : std::uint8_t { Auto, Body, View };
enum class WalkInWay : std::uint8_t { Body, View };
struct WalkInFacts {
    bool midMoment = false;          // the conversation began while a moment's walk was on its way
    bool markBehindTrigger = false;  // the mark is inside a trigger that where the player stands is outside of
};
WalkInWay DecideWalkIn(WalkInFacts const& facts, WalkInRule rule);
WalkInRule ParseWalkInRule(std::string_view text, WalkInRule fallback);
char const* Name(WalkInWay way);

// People in the way (2026-11-09, after the first walk in the game went straight through Garrus: the pawn is moved with
// SetLocation, which collides with nothing). A step from `place` to `next` that would end within `clearUnits` of one of
// `people` (horizontally) is pushed out to that distance along the line from the person: the leash's slide turned inside out,
// so the player walks around them. A place already that close (someone walked up) may only step away. `held` says a person
// held the step; `moved` is false when nothing of the step is left. Several people: passes on the nearest, at most four; two
// people closer together than two clearances leave no way between them, and the step is held where it started.
struct ClearStep {
    Vec3 place;
    bool moved = false;
    bool held = false;
};
ClearStep KeepClear(Vec3 const& place, Vec3 const& next, std::span<Vec3 const> people, double clearUnits);

// ---- Scene rules: another conversation during a flowed scene (research/69 item 1, 2026-11-09) -----------------------------------
// The first rules of a scene's description that are data, not code in the game module (the owner: "we likely would codify this
// as a part of our new scene builder so we can try to avoid these one-off fixes"): the game module gathers the facts, this
// decides, and a per-scene table can later hand in a scene's own rule in place of the setting's.
//
// The game ends an ambient conversation for any conversation that wants to start, and refuses every newcomer while a
// conversation that is not ambient runs. A flowed scene was such a conversation, so:
//   Through  the newcomer ends the scene (the game's behaviour for barks; the scene's remaining lines are lost)
//   Refuse   the newcomer is refused, as the game refuses it during the conversation the scene was authored as (a bystander's
//            bark: nothing of value is lost)
//   Hold     refused now, and its start action fired again once the scene and what follows it are over: nothing is lost on
//            either side (in the game on 2026-11-09 a level banter let through cut the Garrus argument off after 0.5 s)
enum class NewcomerRule : std::uint8_t { Through, Refuse, Hold };
NewcomerRule ParseNewcomerRule(std::string_view text, NewcomerRule fallback);
char const* Name(NewcomerRule rule);
struct NewcomerFacts {
    bool sceneOurs = false;       // the current conversation is ambient because of our flags (the line it is on is one we flagged)
    bool same = false;            // the newcomer is the scene itself
    bool gameBark = false;        // the newcomer's every start is ambient by the game's own data
    bool hasStartAction = false;  // a level's start action started it: it can be fired again
};
// `scenes`: the rule for a newcomer that is another scene of the level (the setting's, or the scene's own). A bark is refused
// under Hold and Refuse alike; a newcomer no action started (code started it) can't be fired again, so Hold lets it through.
NewcomerRule DecideNewcomer(NewcomerFacts const& facts, NewcomerRule scenes);

// The opening of a conversation on a stage (research/69 "Authored scenes"; the owner, 2026-11-09: no cut unless the scene's
// composition is completely different). The game puts everyone on the stage's marks when the conversation starts. In first
// person that is a cut only when it has to be:
//   Stay    Shepard's mark is about where the player stands: nobody's view moves, nothing is cut
//   WalkIn  the mark is farther but in the same space (in sight, on a floor, within a walk): the player keeps his place
//           and walks there himself; the conversation goes on meanwhile and its wheel waits for him
//   Cut     another composition (out of sight or reach, no floor, no body, or the people he was looking at put elsewhere):
//           the player is put at the mark under a dip from black, as before
// `worldOpening = accept` (always Cut), `keep` (always Stay) or `auto` (this rule).
enum class OpeningRule : std::uint8_t { Accept, Keep, Auto };
enum class Opening : std::uint8_t { Stay, WalkIn, Cut };
OpeningRule ParseOpeningRule(std::string_view text, OpeningRule fallback);
char const* Name(OpeningRule rule);
char const* Name(Opening opening);
struct OpeningFacts {
    bool known = false;              // where the player stood before the stage moved Shepard is known
    double markUnits = 0.0;          // from there to Shepard's mark, on the ground
    double riseUnits = 0.0;          // and up or down
    bool sightToMark = false;        // a clear line from the player's eyes to Shepard's head at the mark
    bool floorAtMark = true;         // a floor under the mark
    bool shepardHidden = false;      // the pawn is hidden: no body in this scene
    double peopleMovedUnits = 0.0;   // the farthest the stage moved someone who was in the player's view
};
struct OpeningLimits {
    double stayUnits = 150.0;        // a mark this near is "where he stands"
    double walkMaxUnits = 4000.0;    // a walk-in at most this far
    double riseMaxUnits = 600.0;     // and this much up or down (a stair, not another deck)
    double peopleMaxUnits = 300.0;   // people in view put farther than this: another composition
};
Opening DecideOpening(OpeningFacts const& facts, OpeningRule rule, OpeningLimits const& limits = {});

// A placement the game repeats: undone, it is back at the same place the next tick (a walk's last step snapping onto its
// goal: 0.33 m for Lieutenant Durand, undone 512 times in 22 s, 2026-11-10; a floor the engine settles a pawn onto). Asked
// for `ticks` ticks in a row at the same place (within sameUnits), no farther than maxUnits (small enough not to read as a
// teleport), it is the game's own move: Insists says true and the caller accepts it. Farther placements are never accepted
// this way (a stage mark is what keeping people in place is for). A tick without a placement starts the count again (Calm).
// Two ticks: a placement made once is undone once and stays undone; one that comes back is accepted at its second asking
// (with three, the two undone ones each reached a drawn frame: she flickered 0.35 m twice).
struct Insistence {
    Vec3 at;
    int count = 0;
};
struct InsistLimits {
    int ticks = 2;
    double sameUnits = 5.0;
    double maxUnits = 100.0;
};
bool Insists(Insistence& state, Vec3 const& wanted, double movedUnits, InsistLimits const& limits = {});
inline void Calm(Insistence& state) { state.count = 0; }

// The starts held for later (an opaque id each: the start action). HoldStart: false if it was held already (its count grows)
// or eight are held. ReleaseStart: the oldest one to fire now, when nothing that would refuse it is playing (`free`); those
// held longer than maxAgeMs are dropped first (into `expired`).
struct HeldStart {
    std::uint64_t id = 0;
    std::uint64_t heldMs = 0;
    int times = 1;
};
bool HoldStart(std::vector<HeldStart>& held, std::uint64_t id, std::uint64_t nowMs);
std::optional<std::uint64_t> ReleaseStart(std::vector<HeldStart>& held, bool free, std::uint64_t nowMs, std::uint64_t maxAgeMs,
    std::vector<std::uint64_t>* expired = nullptr);

} // namespace mevr::vr::conversation_world
