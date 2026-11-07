// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

// The power wheel at the hand (research/55, the user's concept of 2026-11-04, concept/power-wheel.png): tiles in a ring
// around the palm, the selected one pulled out, a gap where the forearm comes in. This is the game-independent part:
// where the tiles go, their outline, and which one a hand's offset from the centre picks. The look is
// plugin/PowerWheelDraw's. Unit-tested in tests/Main.cpp (TestPowerWheel).
//
// Conventions: the wheel's plane with x right and y up, angles in degrees counter-clockwise from +x (90 is up), lengths
// in whatever unit the caller uses (canvas pixels for the drawing, metres for the hand).
namespace mevr::vr::power_wheel {

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// The concept's six tiles sit at 184, 139, 90, 37, -7 and -69 degrees around a left palm: an arc of 253 degrees,
// evenly spaced to within its perspective, with the empty part toward the bottom left, where the forearm comes in.
struct LayoutSettings {
    float gapCentreDeg = -122.5f;  // the middle of the empty part (toward the forearm)
    float gapDeg = 107.0f;         // from the last tile's centre to the first's, across the gap
    float minStepDeg = 34.0f;      // more entries close the gap before the tiles come closer than this
    float maxStepDeg = 52.0f;      // fewer entries keep this spacing, centred opposite the gap
};
// The spacing between neighbouring tiles for `count` entries.
float StepDeg(int count, LayoutSettings const& settings = {});
// Each entry's centre angle, in (-180, 180], entry 0 at the arc's counter-clockwise end, then clockwise.
std::vector<float> Angles(int count, LayoutSettings const& settings = {});

// A tile: its outer edge an arc, its sides along radii, its inner edge straight (the concept's: the spoke meets it in its
// middle) or an arc, its corners cut.
struct TileSettings {
    float innerRadius = 222.0f;  // the inner edge's distance from the centre (at its middle)
    float outerRadius = 410.0f;
    float halfWidthDeg = 22.0f;
    float outerChamfer = 30.0f;  // the outer corners' cut, along each edge
    float innerChamfer = 12.0f;  // the inner corners'
    bool straightInner = true;
    int arcSegments = 24;        // points along each edge (the cuts' corners are added)
};
// The tile's half-width for `count` entries: the settings' own, narrowed so neighbours keep a gap of `spacingDeg`.
float HalfWidthDeg(int count, TileSettings const& tile, LayoutSettings const& layout = {}, float spacingDeg = 5.0f);

// The tile's boundary as matching points along its outer and inner edge, from its clockwise side to its
// counter-clockwise side: a strip whose quads fill it (each inner point under its outer point), and whose outline is
// the outer points, then the inner points backwards.
struct Strip {
    std::vector<Vec2> outer;
    std::vector<Vec2> inner;
};
Strip TileStrip(float angleDeg, TileSettings const& tile);
std::vector<Vec2> Outline(Strip const& strip);
// The same tile scaled about its own middle (the selected tile grows) and moved out along its angle.
Strip Transformed(Strip const& strip, float angleDeg, float middleRadius, float scale, float pushOut);

// Which entry a hand picks from its offset from the wheel's centre (in the wheel's plane): the one whose angle is
// nearest the offset's direction, once the offset leaves the dead zone. Inside the dead zone the current pick stays
// (it's the way back to the centre, not a choice). A pick changes only when another entry is nearer by `keepDeg`, so
// a hand on the border between two doesn't flicker. Directions in the gap go to the nearer end. -1: nothing picked.
struct PickSettings {
    float deadZone = 0.025f;  // the offset's length below which nothing changes (the caller's unit: metres)
    float keepDeg = 6.0f;
};
int Pick(Vec2 offset, std::vector<float> const& angles, int current, PickSettings const& settings = {});

// The signed difference a - b in degrees, in (-180, 180].
float AngleDiff(float a, float b);

// ---- In the game (game/PowerWheel in the mod, plugin/PowerWheel in the host) ----------------------------------------

// [PowerWheel] in LE1MEVR.ini.
struct Settings {
    bool on = true;            // X opens the wheel on foot (and never throws a grenade then)
    float widthM = 0.42f;      // the wheel's width in the room
    float towardM = 0.04f;     // its centre this far from the palm toward the eyes
    float deadZoneM = 0.025f;  // the hand's travel from where it opened before it picks
    float keepDeg = 6.0f;      // a pick changes only when another tile is nearer by this
    float stickPick = 0.5f;    // the left stick pushed past this picks by its direction
    float tapS = 0.30f;        // X let go sooner than this keeps the wheel open (a tap; a pinch or X again then equips)
    bool emblem = true;        // our N7 mark on the palm (0: none)
    bool upperCase = false;    // the names in capitals (the concept's); 0: title case (the game's)
    int hintOpenings = 3;      // the hint card shows beside the wheel the first this many openings each session
    float haptic = 0.35f;      // 0..1: a tick on the left hand when the pick changes, a pulse when a power is equipped
    bool omniTool = true;      // Shepard's forearm wears the game's omni-tool gauntlet while it's open (the user, 2026-11-04)
};

// The player's mappable powers by their 2DA label (BIOG_2DA_Talents_X.Powers_Powers, research/55 section 3): the name
// the wheel shows, the discipline (0 biotic, 1 tech, 2 combat) and the icon (plugin/PowerIcons' Icon, the same order).
// How the power aims (its 2DA AimType): none (it acts on the caster or the squad), an actor (selected target), or a
// place (camera trace: where it lands, an actor there or not).
enum class Aim : std::uint8_t { None, Target, Place };
struct PowerInfo {
    char const* label;
    char const* name;
    std::uint8_t discipline;
    std::int32_t icon;
    Aim aim;
};
// Null: not one of the player's mappable powers (melee, healing, NPC powers...).
PowerInfo const* FindPower(char const* label);

// What the host draws, as the mod sends it each tick while the wheel is open (HostServices v28, setPowerWheel).
inline constexpr int kMaxEntries = 10;
inline constexpr int kLabelBytes = 32;
struct ViewEntry {
    char label[kLabelBytes] = {};  // UTF-8, as shown
    std::int32_t icon = 0;         // plugin/PowerIcons' Icon
    std::uint8_t discipline = 0;   // 0 biotic, 1 tech, 2 combat
    std::uint8_t enabled = 1;      // 0: the game won't cast it now (drawn dim)
    std::uint8_t padding[2] = {};
    float cooldown = 0.0f;         // the part of its cooldown still to run, 0..1
};
struct View {
    std::uint32_t size = sizeof(View);  // the mod's sizeof(View): the host draws only a matching one
    std::uint32_t serial = 0;           // + 1 at each opening
    std::uint8_t open = 0;
    std::uint8_t emblem = 1;
    std::uint8_t upperCase = 0;
    std::uint8_t hint = 0;              // show the hint card beside it
    std::int32_t count = 0;
    ViewEntry entries[kMaxEntries] = {};
    std::int32_t selected = -1;
    float selectedFor = 1.0f;           // seconds since the pick changed
    float openFor = 1.0f;               // seconds since it opened (its unfolding)
    XrPosef pose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};  // the wheel's centre in tracking space, +Z toward the eyes
    float widthM = 0.42f;
    Vec2 hand;                          // the palm's offset from the centre in the wheel's plane, metres, y up
    // Where the light comes from (the user, 2026-11-06: "actually connect to the omni-tool"): the omni-tool's display as
    // Shepard's hand shows it (game/OmniTool DisplayWorld), in the left grip's frame (metres), so the host puts it on the
    // grip it has that frame (ToFrame/FromFrame). 0: no display on the arm; the host uses the left grip itself.
    XrVector3f source{};
    std::uint8_t sourceValid = 0;
    std::uint8_t sourcePad[3] = {};
};
// Any thread: the host keeps the latest; Latest says closed when it's older than `maxAgeMs` (the mod stopped sending).
void Publish(View const& view);
View Latest(std::uint64_t maxAgeMs = 250);

// The wheel's pose when it opens: its centre `towardM` from the palm toward the eye, facing the eye, upright (no roll).
XrPosef Place(XrVector3f palm, XrVector3f eye, float towardM);
// A point's offset from the wheel's centre in its plane (x right, y up as the eye sees it), in the pose's units.
Vec2 InPlane(XrPosef const& wheel, XrVector3f point);
// A point in a pose's frame, and back (the omni-tool's display rides on the left grip).
XrVector3f ToFrame(XrPosef const& frame, XrVector3f point);
XrVector3f FromFrame(XrPosef const& frame, XrVector3f local);
// Where the eye sees `source` on the wheel: the line from `eye` through it meets the wheel's plane there (an offset from the
// centre in the plane, as InPlane). Straight along the wheel's normal when that line runs within 5 degrees of the plane or
// meets it behind the eye.
Vec2 SeenInPlane(XrPosef const& wheel, XrVector3f eye, XrVector3f source);

// The left hand's controls through one opening (pure: tested offline). X held opens the wheel; the left hand's travel
// toward a tile, or the left stick's direction, picks it; letting go of X equips the pick. X tapped (let go within tapS)
// keeps it open: then a pinch of the left trigger or a squeeze of the left grip equips the pick, and X again equips it
// (or, with nothing picked, closes). Leaving the allowed state (a menu, the Mako, the hand lost) closes it unequipped.
struct MachineInput {
    double time = 0.0;        // seconds, any steady clock
    bool allowed = false;     // it may open or stay open now
    bool button = false;      // X held
    float stickX = 0.0f;      // the left stick (+ right, + forward)
    float stickY = 0.0f;
    float trigger = 0.0f;     // the left trigger, 0..1
    float grip = 0.0f;        // the left grip, 0..1
    bool handValid = false;
    Vec2 hand;                // the palm's offset from the wheel's centre in its plane (metres)
};
enum class Event : std::uint8_t { None, Opened, Equipped, Cancelled };
struct Machine {
    bool open = false;
    bool toggled = false;     // opened by a tap: stays open until a pinch, a squeeze or X again
    double openedAt = 0.0;
    int selected = -1;
    double selectedAt = 0.0;
    bool stickPicked = false; // the current pick came from the stick...
    Vec2 stickHand;           // ...with the hand here: it picks again only once it moves on from there
    bool buttonWas = false;
    bool triggerDown = false; // latched with hysteresis
    bool gripDown = false;
    bool triggerArmed = false;  // let go since it opened: the next press is a confirm
    bool gripArmed = false;
    std::uint32_t serial = 0;
    int equipped = -1;        // the entry the last Equipped event chose
};
Event Step(Machine& machine, MachineInput const& input, std::vector<float> const& angles, Settings const& settings);
char const* Name(Event event);

// ---- The lock and the push (research/55 section 4) -------------------------------------------------------------------

// [PowerWheel] lock* and push* keys: the equipped power aimed with the left hand, cast with a push of it.
struct LockSettings {
    bool on = true;             // the lock and the push (0: the right stick click's tap only, the game's own aim)
    float acquireDeg = 9.0f;    // a hostile within this of the left hand's ray is locked...
    float keepDeg = 16.0f;      // ...and kept until it leaves this
    float rangeM = 35.0f;
    float graceS = 0.35f;       // the line of sight may be blocked this long before the lock lets go
    float reachM = 0.20f;       // the hand this far ahead of the eyes (the arm raised) before anything is locked
    float pushSpeedMS = 1.6f;   // the left hand's speed along its ray that casts...
    float pushTravelM = 0.10f;  // ...over at least this travel
    float cooldownS = 0.6f;     // between pushes
    // The automatic resolver: nothing within acquireDeg of the hand, the best visible hostile within autoHandDeg of the
    // hand's ray or autoGazeDeg of the eyes' forward is locked (hand and gaze agreeing win); a hostile then pointed at
    // takes over. Kept while within those plus kAutoKeepMarginDeg.
    bool autoLock = true;
    float autoHandDeg = 30.0f;
    float autoGazeDeg = 20.0f;
};
inline constexpr float kAutoKeepMarginDeg = 6.0f;

// A hostile as the lock sees it this tick.
struct LockCandidate {
    std::uint64_t id = 0;
    float angleDeg = 180.0f;  // from the hand's ray
    float gazeDeg = 180.0f;   // from the eyes' forward, as seen from the eyes
    float distanceM = 0.0f;
    bool visible = false;     // nothing between the hand and it
};
struct LockState {
    std::uint64_t target = 0;  // 0: none
    double since = 0.0;        // when it locked
    double blockedSince = -1.0;  // the line of sight blocked since (negative: clear)
    bool automatic = false;    // resolved automatically (nothing was pointed at)
};
// One step. The current target is kept while it stays in its cone (pointed: keepDeg of the hand; automatic: autoHandDeg of
// the hand or autoGazeDeg of the gaze, plus kAutoKeepMarginDeg) and range, and its line of sight hasn't been blocked for
// graceS; an automatic one gives way to any other hostile pointed at (within acquireDeg). Else the best visible hostile
// within acquireDeg of the hand is taken (the smallest angle, a nearer one winning a near tie: 0.05 degree per metre);
// else, with autoLock, the best visible one within autoHandDeg of the hand or autoGazeDeg of the gaze (the least hand +
// gaze angle, 0.1 degree per metre). The same hostile taken again keeps its `since`. Returns the target (0: none).
std::uint64_t UpdateLock(LockState& state, std::vector<LockCandidate> const& candidates, double time, LockSettings const& settings);

// The push that casts: the left hand moved along its own ray at pushSpeedMS or more over the last 0.12 s, at least
// pushTravelM of it. Once fired, it re-arms when the hand slows below 40% of that speed, and fires no sooner than
// cooldownS after the last.
struct Push {
    double times[16] = {};
    XrVector3f hands[16] = {};
    int count = 0;
    bool armed = true;
    double lastFire = -1e9;
};
bool StepPush(Push& push, double time, XrVector3f hand, XrVector3f forward, LockSettings const& settings);

// The lock's marker, as the mod sends it each tick (HostServices v29, setPowerLock); the host draws it at the target.
struct LockView {
    std::uint32_t size = sizeof(LockView);  // the mod's sizeof(LockView): the host draws only a matching one
    std::uint8_t active = 0;      // draw it
    std::uint8_t ready = 0;       // the power can be cast now (else cooling down or blocked: drawn dim)
    std::uint8_t discipline = 0;
    std::uint8_t casts = 0;       // + 1 at each cast (a flash)
    std::uint8_t onActor = 1;     // on a locked hostile (else where a place power would land)
    std::uint8_t padding[3] = {};
    std::int32_t icon = 0;
    XrVector3f target{};          // the locked hostile's chest, tracking space
    float sizeDeg = 6.0f;         // its angular size
    float lockedFor = 0.0f;       // seconds since it locked (the brackets close in)
    float cooldown = 0.0f;        // the power's cooldown still to run, 0..1
};
void PublishLock(LockView const& view);
LockView LatestLock(std::uint64_t maxAgeMs = 250);

// ---- Self-cast gestures (research/55 section 10, 2026-11-05) ---------------------------------------------------------
// The powers that aim at nothing are cast by a gesture of their own, not the push: the open left hand alone (the right may
// hold the gun; the left trigger and grip stay the squad's), no buttons:
//   Chest  the palm on the chest, held (Barrier, Shield Boost, Immunity, Adrenaline Burst: a shield or a buff on yourself);
//   Gun    the palm slapped onto the top of the gun in the right hand (Marksman, Overkill, Carnage, Assassination: the gun's modes);
//   Raise  the hand raised above the head, held (Unity: a rally to the squad).
enum class SelfGesture : std::uint8_t { None, Chest, Gun, Raise };
SelfGesture SelfGestureFor(char const* label);  // by the 2DA label; None for a power that aims
char const* Name(SelfGesture gesture);

struct SelfCastSettings {
    float chestBelowM = 0.28f;   // the chest point: below the eyes
    float chestAheadM = 0.14f;   // ...and ahead of them, along the head's flat forward
    float chestRadiusM = 0.13f;
    float raiseAboveM = 0.08f;   // the hand this far above the eyes
    float dwellS = 0.35f;        // held this long (chest, raise; the gun's slow touch)
    float gunUpM = 0.06f;        // the gun's top: this far up and ahead of the right hand's aim pose
    float gunAheadM = 0.05f;
    float gunRadiusM = 0.10f;
    float slapSpeedMS = 0.6f;    // a slap: entering the gun's zone this fast casts at once
};
struct SelfCastInput {
    double time = 0.0;
    bool tracked = false;   // the left hand
    bool open = false;      // no grip, no trigger
    XrVector3f hand{};      // the left hand (tracking space)
    XrPosef head{};
    bool gunValid = false;  // the right hand holds a drawn gun
    XrPosef gun{};          // the right hand's aim pose
};
struct SelfCastState {
    bool inZone = false;
    double since = 0.0;     // in the zone since
    bool spent = false;     // cast in this stay: leave the zone to re-arm
    bool hasLast = false;
    XrVector3f last{};
    double lastTime = 0.0;
};
struct SelfCastStep {
    bool entered = false;   // came into the zone (a tick)
    bool cast = false;
    float progress = 0.0f;  // 0..1 of the dwell
};
SelfCastStep StepSelfCast(SelfCastState& state, SelfGesture gesture, SelfCastInput const& input, SelfCastSettings const& settings);

}  // namespace mevr::vr::power_wheel
