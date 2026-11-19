// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Vaulting and climbing in VR (research/le2-le3-vr-traversal.md, the owner's design; research/le2-traversal.md, ME2's first
// gate). The game owns the move: its admission, the pawn, collision, root motion, timing and events. What VR adds, all here and
// game-independent (pure, unit-tested in tests/TraversalTests.cpp):
//   - the press rule: a fresh press of the action button commits one native action; proximity, walking, looking, a held
//     button, the press that only took cover and a press begun in a menu never do (PressTracker, Judge);
//   - the record of one move: when it was asked for, began, ended, and when movement and aim came back; how far and how high
//     the pawn went; what the game's camera did meanwhile (pitch, roll, the bob against the pawn's path) (MoveRecord);
//   - the presentation: a level view that follows the move's translation without the animation's pitch, roll or bob
//     (LevelView), and the comfort mask over it (MaskAmount: none, the vignette, a brief blink);
//   - the world-space cue at the interaction point ("Vault", "Climb up"): its place and the struct the mod sends the host.
// The game adapters (game/Traversal for ME2) read the game and call these; angles are degrees, places Unreal units (Z up)
// unless a field says tracking space (OpenXR metres, Y up).
namespace mevr::vr::traversal {

// What the native action is. ME2: a mantle over low cover (CustomAction 10, "vault") or a climb onto higher cover (9).
enum class Kind : std::uint8_t {
    None = 0,
    Vault = 1,
    ClimbUp = 2,
    LadderUp = 3,
    LadderDown = 4,
    JumpDown = 5,
    Evade = 6,
    Ladder = 7, // approach cue; the native action may select up, down or jump-down
};
char const* Label(Kind kind);  // the cue's word: "Vault", "Climb up" ("" for None)
char const* Name(Kind kind);   // the logs': "vault", "climb up", "none"

// ---- Settings: [Traversal] in ASI\<GAME>MEVR.ini --------------------------------------------------------------------------------
// The defaults keep today's behaviour (no cue, the game's own camera, no mask): new player-facing defaults wait for the owner.
enum class View : std::uint8_t {
    Game = 0,   // the game's camera as it animates through the move (today's)
    Level = 1,  // the move's translation only: level, no bob (LevelView)
};
enum class Mask : std::uint8_t {
    None = 0,
    Vignette = 1,  // the comfort vignette's edges while the move runs (HostServices v17 setVignette)
    Blink = 2,     // a brief fade to black over the fastest part of the move (the cue's host draws it)
};
char const* Name(View view);
char const* Name(Mask mask);
std::optional<View> ParseView(std::string_view text);
std::optional<Mask> ParseMask(std::string_view text);

struct Settings {
    bool enabled = true;            // the module (measurements and logs); 0 stands it down entirely
    bool cue = false;               // the world-space cue at the interaction point
    View view = View::Game;
    Mask mask = Mask::None;
    float vignetteStrength = 0.85f; // the vignette's amount while the move runs (0..1)
    float blinkInS = 0.06f;         // the blink's fade to black after the move begins
    float blinkHoldS = 0.35f;       // ... black for this long (at most until the move ends)
    float blinkOutS = 0.15f;        // ... and back
    float settleS = 0.25f;          // the level view's hand-back to the game's camera after the move
    float levelTauS = 0.0f;         // the level view's height filter (0: none; LevelView)
    float cueHeightDeg = 2.6f;      // the cue's angular height where it stands
    float cueRangeM = 3.0f;         // the cue shows only within this distance of the head
    float doubleTapS = 0.45f;       // the double A: the second press within this of the first
    float reachM = 1.5f;            // slots within this of the pawn are candidates (the game's own distance rule decides)
    bool commit = true;             // the double tap commits the game's vault (0: measure and cue only)
    bool trace = false;             // a line per camera update while a move runs (traversal: trace)
    // ME2's admission as the player stands (research/le2-traversal.md "The vault Freedom's Progress requires", 2026-11-18): the
    // game asked with the head's facing (the pawn turned to the slot at the commit), not the pawn's (`facing = head | pawn`),
    // and with the weapon away as if drawn (CanPerformMantle needs the combat action state; nothing is drawn).
    bool headFacing = true;
    bool holstered = true;
};

// ---- The press rule ------------------------------------------------------------------------------------------------------------
// The binding is ME2's conventional double A (the owner, 2026-11-14: "The conventional button is double a"). In VR the player never
// snaps into cover (the owner: "We won't snap to cover for VR, it's not a realistic experience"), so the double A is a double tap
// standing next to the obstacle, facing it: the second press commits one native vault; a single A does nothing of ours. What the
// mod knows of the right controller's A and the game each tick.
struct PressInput {
    double nowS = 0.0;
    bool down = false;        // the action button (the right controller's A)
    bool focused = true;      // the session has input focus (the game hears the pad)
    bool menuOpen = false;    // a menu over the game (ours, the renderer's, or the game's GUI modes)
    bool onFoot = true;       // the game's on-foot mode
    bool eligible = false;    // the game admits a vault or climb from where the pawn stands (its own CanPerformMantle/Climb)
    bool useFirst = false;    // something A would use is selected (the game's Use runs first)
    bool traversing = false;  // a native traversal runs
    bool authoredSeated = false; // authored shuttle/dialogue restriction, not physically seated play
};

// Why a press is (or isn't) one that may commit a traversal.
enum class PressWhy : std::uint8_t {
    Fresh,            // went down focused, no menu, on foot, next to an eligible obstacle: may be the first or second of a double tap
    NotEligible,      // nothing to vault or climb from here
    UseFirst,         // A uses the selected object first
    FromMenu,         // went down while a menu was open or the session unfocused
    DuringTraversal,  // went down while a move ran
    NotOnFoot,
    AuthoredSeated,
};
char const* Name(PressWhy why);

// What a press is in the double tap.
enum class Tap : std::uint8_t {
    None,    // no press went down this tick
    First,   // a press that may begin a double tap (or a lone press: nothing of ours)
    Double,  // the second fresh press within doubleTapS of a clean first one: commit one vault
    Other,   // a press that is neither (not fresh: Last().why says why)
};
char const* Name(Tap tap);

struct Press {
    bool valid = false;        // a press has been seen
    std::uint32_t serial = 0;  // counts the presses (1, 2, ...)
    double atS = 0.0;          // when it went down
    double upS = -1.0;         // when it was let go (-1 while held)
    PressWhy why = PressWhy::NotOnFoot;
    Tap tap = Tap::None;
    bool clean = false;        // not from a menu, on foot, no move running (a first tap may be one away from the obstacle)
    std::uint32_t commits = 0; // native traversals begun on this press
};

class PressTracker {
public:
    // One tick: the tap that went down this tick (Tap::None if none; then Last() is the press). The verdict is taken from the state
    // before this tick (the game may already have acted on the press this tick): `in` is this tick's state, the previous Step's is
    // "before". A second press counts as a double tap within `doubleTapS` of the first going down; a third starts again.
    Tap Step(PressInput const& in, double doubleTapS = 0.45);
    Press const& Last() const { return last_; }
    Press const& Previous() const { return previous_; }
    bool Down() const { return down_; }
    // A move began on the last press (Judge counts them).
    void NoteCommit();
    void Reset();

private:
    Press last_{};
    Press previous_{};
    PressInput before_{};
    bool haveBefore_ = false;
    bool down_ = false;
};

// A native traversal began at `nowS`: whether the rules would have committed it, and on which press. Allowed on the second press of a
// double tap, its first commit, within `windowS` of it going down (the game reads the pad within a tick or two of the mod seeing it).
// The mod's own commits are judged too, so a move the game began by itself (its own in-cover A when cover isn't blocked, a script)
// shows up as what it is.
enum class CommitWhy : std::uint8_t {
    Allowed,       // the second press of a double tap, its first commit
    NoPress,       // no press within the window: the game began it on its own (a script, a held button, a repeat)
    NotFresh,      // a press, but not one that may commit (Last().why says which)
    SingleTap,     // a fresh press but not a double tap (the game's own in-cover A)
    Repeated,      // the press already committed one: one action per press
};
char const* Name(CommitWhy why);
struct Commit {
    CommitWhy why = CommitWhy::NoPress;
    std::uint32_t press = 0;  // the press's serial (0: none)
    double sincePressS = 0.0;
};
Commit Judge(PressTracker& presses, double nowS, double windowS = 0.25);

// Native observation, separate from the press policy and native request return value.
enum class ActionPhase { Idle, Requested, Started, Completed, Interrupted, EndedUnknown, NoStartObserved, Cancelled };
char const* Name(ActionPhase phase);
class ActionLifecycle {
public:
    bool Request(std::uint64_t generation, double now);
    void Observe(std::uint64_t generation, std::uintptr_t action, Kind kind, double now);
    // End/interrupt callbacks may be nested: an interrupt arriving after the common end overrides completion.
    void End(std::uint64_t generation, std::uintptr_t action, bool interrupted, double now);
    void Cancel();
    bool Busy() const { return phase == ActionPhase::Requested || phase == ActionPhase::Started; }
    ActionPhase phase = ActionPhase::Idle;
    std::uint64_t serial = 0, generation = 0;
    std::uintptr_t action = 0;
    Kind kind = Kind::None;
    double requestedAt = 0, startedAt = 0, endedAt = 0;
};

// ---- The record of one move ------------------------------------------------------------------------------------------------------
struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};
Vec3 operator+(Vec3 a, Vec3 b);
Vec3 operator-(Vec3 a, Vec3 b);
Vec3 operator*(Vec3 a, double s);
double Length(Vec3 v);
double LengthXY(Vec3 v);

// One camera update while the move runs (and after it, until the view settles).
struct Sample {
    double t = 0.0;            // seconds since the move began
    Vec3 pawn{};               // the pawn's Location (ME2 moves it in steps: the root motion extracted in chunks)
    double pawnYaw = 0.0;      // degrees
    Vec3 camera{};             // the game's camera (the POV) as the game left it
    double pitch = 0.0;        // ... its rotation, degrees, signed
    double yaw = 0.0;
    double roll = 0.0;
    bool collision = true;     // the pawn blocks actors (Actor.bBlockActors: the mantle's SetCollision turns it off)
    std::uint8_t physics = 0;  // the pawn's EPhysics
    bool running = true;       // the custom action still runs (false: its blend-out)
};

struct Summary {
    Kind kind = Kind::None;
    unsigned samples = 0;
    double seconds = 0.0;             // began -> the custom action ended
    double settledS = -1.0;           // the custom action's end -> the camera came to rest (-1: not seen)
    double horizontalM = 0.0;         // the pawn's travel, began -> the last sample, flat
    double riseM = 0.0;               // the pawn's highest point above where it began
    double dropM = 0.0;               // where it ended against where it began (+ up)
    double yawTurnDeg = 0.0;          // the pawn's facing, began -> the last sample
    double pawnStepM = 0.0;           // the pawn's largest move in one update (its root motion's steps)
    double pitchMinDeg = 0.0, pitchMaxDeg = 0.0;  // the game's camera
    double rollMinDeg = 0.0, rollMaxDeg = 0.0;
    double pitchStartDeg = 0.0;
    double cameraTravelM = 0.0;       // the camera, began -> the last sample, flat
    double cameraRiseM = 0.0;         // its highest point above where it began
    double cameraDropM = 0.0;         // its last height against where it began
    double cameraPeakSpeedMS = 0.0;   // its fastest (over 2 updates), m/s
    double cameraPeakVerticalMS = 0.0;
    double cameraBobM = 0.0;          // its height against its own 0.3 s average, peak to peak (what an animated bob would add)
    double maxStepM = 0.0;            // its largest move in one update (a jump: a teleport, a cut)
    double collisionOffS = 0.0;       // how long the pawn didn't block actors
};

class MoveRecord {
public:
    void Begin(Kind kind);
    void Add(Sample const& sample);
    // The custom action ended at `t` (seconds since the move began); samples after it are its blend-out.
    void End(double t);
    bool Active() const { return active_; }
    // The camera has moved less than `speedMS` for the last `forS` (its blend-out over).
    bool Settled(double speedMS = 0.05, double forS = 0.2) const;
    Summary Finish();
    Summary Current() const;
    double unitsPerMeter = 100.0;

private:
    bool active_ = false;
    Kind kind_ = Kind::None;
    double endT_ = -1.0;
    std::vector<Sample> samples_;
};

// ---- The level view ----------------------------------------------------------------------------------------------------------------
// The game's camera through the move, held level ([Traversal] view = level): its place and yaw each update, the pitch it had when the
// move began (0 on foot without the stick's pitch) and no roll; with tauS > 0 ([Traversal] levelTauS) its height goes through a
// critically damped filter of that time constant (an animation's bob, a few hertz, damped; a filter also lags: 2 tauS times the
// vertical speed, 0.06 s x 1.2 m/s = 7 cm at tauS 0.03). ME2's own camera was measured level, smooth and without bob through a vault
// (research/le2-traversal.md), so the hold changes nothing there and the filter is off by default; it is the guard for a move or
// stance whose camera pitches or bobs. When the move ends it hands back to the game's camera over `settleS` (smoothstep).
struct Pose {
    Vec3 location{};
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
};
class LevelView {
public:
    void Begin(Pose const& camera);
    // During the move: the view for this camera update, `dt` after the last (tauS 0: no height filter).
    Pose During(Pose const& game, double dt, double tauS = 0.0);
    void End();
    // After the move ended `sinceEndS` ago: between the last During pose and the game's; nullopt once handed back.
    std::optional<Pose> After(Pose const& game, double sinceEndS, double settleS);
    bool Started() const { return started_; }
    void Reset() { *this = LevelView{}; }

private:
    bool started_ = false;
    Pose start_{};
    double z_ = 0.0;       // the filtered height
    double vz_ = 0.0;      // ... and its rate
    Pose last_{};          // the last During
    Pose ended_{};         // the view at the end
    bool haveEnded_ = false;
};
// Degrees, signed, in (-180, 180].
double SignedDegrees(double degrees);

// LE3 authored yaw never becomes a permanent forced turn. Hold the pre-action heading
// while native traversal owns the camera, then keep its accumulated correction as a view
// basis offset. Ordinary player turns after recovery pass through one-for-one. This does
// not rotate/move the pawn or the tracked head; the host must compensate movement too.
class AuthoredYaw {
public:
    void Begin(double nativeYaw);
    double Update(double nativeYaw, bool actionOwned);
    double Offset() const { return offset_; }
    bool Holding() const { return holding_; }
    void Reset() { *this = AuthoredYaw{}; }
private:
    double offset_ = 0.0;
    double held_ = 0.0;
    bool holding_ = false;
};

// HostServices v41, separate from the cue. Fixed-width POD; accepted publication is required
// before applying the POV yaw correction. Positive yawOffsetUU is UE/right-positive;
// the Halcyon movement correction therefore uses -yawOffsetUU. No pointers cross this ABI.
inline constexpr std::uint32_t kPresentationVersion = 1;
inline constexpr std::uint32_t kPresentationActive = 1u;
inline constexpr std::uint32_t kPresentationActionOwned = 2u;
struct Presentation {
    std::uint32_t size = sizeof(Presentation);
    std::uint32_t version = kPresentationVersion;
    std::uint32_t flags = 0;
    std::int32_t yawOffsetUU = 0;
    std::uint32_t recenters = 0;
    std::uint32_t sessions = 0;
    std::uint64_t sceneGeneration = 0;
    std::uint64_t timeMs = 0;
};
static_assert(sizeof(Presentation) == 40);
static_assert(offsetof(Presentation, yawOffsetUU) == 12);
static_assert(offsetof(Presentation, sceneGeneration) == 24);
static_assert(offsetof(Presentation, timeMs) == 32);
// Invalid/old/future publications are not a zero-angle replacement for an active offset:
// the host must refuse walking until a valid current sample, or an explicit clear, arrives.
bool PresentationFresh(Presentation const&, std::uint64_t nowMs, std::uint32_t recenters,
    std::uint32_t sessions);

// ---- The comfort mask ----------------------------------------------------------------------------------------------------------------
// How dark (0..1) for the mask at `sinceStartS` into a move still running (`running`) or ended `sinceEndS` ago. Vignette: the strength
// while it runs (the host fades it); blink: black after blinkInS, for blinkHoldS at most, back over blinkOutS (a blink masks a move
// already in progress: the native move, its collision and triggers, run unchanged under it).
float MaskAmount(Mask mask, Settings const& s, bool running, double sinceStartS, double sinceEndS);

// ---- The cue -------------------------------------------------------------------------------------------------------------------------
// The world-space cue at the interaction point: the top of the cover between the slot the player holds and its target on the far
// side. The mod sends it each tick (HostServices v40 setTraversal); the host draws a small label there facing the eyes, and the blink.
struct Cue {
    std::uint32_t size = sizeof(Cue);  // the publisher's struct size: host and mod must match
    bool visible = false;      // show the label
    Kind kind = Kind::None;
    XrVector3f position{};     // tracking space (metres): the label's centre
    bool exitValid = false;
    XrVector3f exit{};         // tracking space: where the move lands (the target slot), for the label's arrow
    float heightDeg = 2.6f;    // the label's angular height ([Traversal] cueHeightDeg)
    float blink = 0.0f;        // 0..1: black over both eyes (the blink mask)
    std::uint32_t serial = 0;  // changes with the target (another slot or link)
    std::uint64_t sequence = 0;  // set by Publish
    std::uint64_t timeMs = 0;    // GetTickCount64 when published
};
void Publish(Cue const& cue);
// The latest cue; size 0 and nothing visible if none was published within `maxAgeMs` (the mod stopped).
Cue Latest(std::uint64_t maxAgeMs = 300);
void Reset();

// The interaction point (Unreal units): halfway between the held slot and its target, on the cover's top `coverTopZ` (a trace's hit;
// without one, `fallbackRise` above the slot), raised `raise` over it.
Vec3 CuePoint(Vec3 slot, std::optional<Vec3> target, std::optional<double> coverTopZ, double raise = 18.0, double fallbackRise = 60.0);

// The label's quad in tracking space: at `position`, upright, facing `head` (yaw only), `heightDeg` tall from there (between 4 and
// 20 cm), `aspect` wide. False if degenerate (the label at the head).
struct Quad {
    XrPosef pose{};
    float width = 0.0f;
    float height = 0.0f;
};
bool PlaceCue(XrVector3f const& position, XrVector3f const& head, float heightDeg, float aspect, Quad& out);

// When the cue shows: the game admits a vault or climb from where the pawn stands, on foot, the session focused, no menu, A not taken
// by Use first, no move running, the point within range of the head.
struct CueInput {
    bool enabled = false;      // [Traversal] cue
    bool onFoot = false;
    bool focused = true;
    bool menuOpen = false;
    bool eligible = false;
    bool useFirst = false;
    bool traversing = false;
    double distanceM = 0.0;    // from the head
    double rangeM = 3.0;
};
bool CueShows(CueInput const& in);

} // namespace mevr::vr::traversal
