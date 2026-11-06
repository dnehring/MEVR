// SPDX-License-Identifier: MIT
#pragma once

#include <openxr/openxr.h>

#include <cstdint>
#include <vector>

// Squad placement by an arc, like a teleport (the user, 2026-11-05: "something like the teleport gesture to place our squad
// mates manually"): on foot, hold the right B and an arc leaves the right hand's aim like a thrown ball; a ring marks where it
// lands; let go and the squad moves there (the one dispatcher, game/SquadOrders). The ray on a squadmate when B goes down
// orders that one alone. Landing within followWithinM of Shepard is "on me" (a follow); landing on no floor (a wall, nothing
// within reach) calls it off. Game-independent rules here (unit-tested, tests/SquadPlaceTests.cpp); the mod traces the arc in
// the world (game/SquadPlace) and the host draws it (plugin/SquadPlaceDraw) from the View below.
namespace mevr::vr::squad_place {

// A point or direction in the game's world, metres, Z up.
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct ArcSettings {
    float speedMS = 14.0f;    // the launch speed: about 20 m at 45 degrees from the hand
    float gravityMS2 = 9.8f;  // the drop
    float stepS = 0.08f;      // one traced segment
    float maxS = 3.0f;        // the longest flight looked at
    float rangeM = 30.0f;     // farther than this (flat, from the hand) is out of reach
    float minUpNormal = 0.7f; // a landing surface steeper than about 45 degrees isn't floor
};

// Where the throw is `t` seconds after leaving `origin` along unit `direction`.
Vec3 ArcAt(Vec3 const& origin, Vec3 const& direction, float t, ArcSettings const& settings);
// The segment ends, from the origin, every stepS until maxS or until the flat distance passes rangeM (the last point is the
// first beyond it). The mod traces each segment in turn and stops at the first hit.
std::vector<Vec3> ArcSegments(Vec3 const& origin, Vec3 const& direction, ArcSettings const& settings);
// `count` points along the flight from 0 to `untilS` (for drawing; count >= 2).
std::vector<Vec3> ArcSamples(Vec3 const& origin, Vec3 const& direction, float untilS, int count, ArcSettings const& settings);
// A hit's surface counts as floor.
bool IsFloor(Vec3 const& normal, ArcSettings const& settings);

// ---- The button's hold ----------------------------------------------------------------------------------------------
struct Settings {
    bool enabled = true;
    float minHoldS = 0.15f;      // a shorter press is a blip: nothing (or the first of a double tap)
    float maxHoldS = 15.0f;      // held longer: called off
    float followWithinM = 2.0f;  // a landing this close to Shepard means "on me"
    float doubleTapS = 0.35f;    // a second press this soon after a blip's release: the whole squad back to Shepard
    float highlightM = 0.025f;   // the squad wheel: the hand this far from where B went down lights the tile it moves toward...
    float commitM = 0.07f;       // ...and this far picks it: the wheel closes and the arc comes up
    float keepDeg = 6.0f;        // a lit tile changes only when another is nearer by this
};

enum class Landing : std::uint8_t { None, Floor, Wall };
char const* Name(Landing landing);

// The squad wheel at the right hand (the user, 2026-11-05: "the radial selector ... where we physically move the right hand to
// target one of the players, left or right, once selected we are then free to move our arm to do the drop location"; B and the
// stick share the thumb, so the stick can't pick). With two squadmates B opens it where the hand is, like the power wheel at the
// left palm: [first squadmate] to the left, [Squad] above, [second] to the right (ME1's d-pad order); the hand's travel toward a
// tile lights it and, past commitM, picks it, and the arc takes over. With one squadmate there's nothing to pick: the arc comes
// up at once, for the squad. Who a tile orders: kSquadTile (-1) the squad, else the squadmate's index in the party.
inline constexpr int kSquadTile = -1;
int TileCount(int members);               // 3 with two squadmates; 0 (no wheel) otherwise
int MemberOfTile(int tile, int members);  // kSquadTile or the squadmate's index
std::vector<float> WheelAngles();         // the tiles' directions in the wheel's plane: 180 (left), 90 (up), 0 (right)
// A point in the wheel's plane (x right, y up as the eye sees it), metres.
struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

struct Input {
    float dt = 0.0f;
    bool usable = false;       // on foot in VR, the right controller tracked, a squad present, nothing else holds B
    bool button = false;       // B is down
    int members = 0;           // squadmates present
    bool handValid = false;    // the right hand tracked...
    Vec2 hand;                 // ...its offset from where B went down, in the wheel's plane (metres)
    Landing landing = Landing::None;
    float landingFromPlayerM = 0.0f;  // Floor: how far from Shepard
};

enum class Order : std::uint8_t { None, Move, Follow };
char const* Name(Order order);

struct Step {
    bool started = false;      // B went down: the arc and the selector show
    bool given = false;        // let go on floor: order given (order, member)
    bool cancelled = false;    // let go elsewhere, held too long, or no longer usable
    bool regroup = false;      // B double-tapped: the whole squad back to Shepard
    bool landingChanged = false;  // the landing's kind changed while holding (a haptic tick)
    bool highlightChanged = false;  // the squad wheel's lit tile changed (a tick)
    bool picked = false;            // the hand picked a tile: the wheel closes, the arc comes up (a pulse per squadmate)
    Order order = Order::None;
    int member = kSquadTile;   // kSquadTile: everyone
    char const* why = "";
};

class Machine {
public:
    Step Advance(Input const& in, Settings const& settings);
    bool Holding() const { return holding_; }
    bool Picking() const { return holding_ && picking_; }  // the squad wheel is up (no arc yet)
    int Tile() const { return tile_; }                      // the lit (picking) or picked tile; -1 none yet
    int Member() const { return tile_ < 0 ? kSquadTile : MemberOfTile(tile_, members_); }
    int Members() const { return members_; }
    float HeldS() const { return heldS_; }
    void Reset();

private:
    bool holding_ = false;
    bool wasDown_ = false;
    bool blocked_ = false;  // B was down before it could start (held through a menu): wait for its release
    bool picking_ = false;
    float heldS_ = 0.0f;
    float sinceBlipS_ = 1e9f;  // since the last blip's release (the double tap)
    int tile_ = -1;
    int members_ = 0;
    Landing landing_ = Landing::None;
};

// ---- What the host draws ---------------------------------------------------------------------------------------------
inline constexpr int kMaxPoints = 32;
struct View {
    std::uint32_t size = sizeof(View);  // the mod's sizeof(View): the host draws only a matching one
    std::uint8_t active = 0;            // draw it
    std::uint8_t landing = 0;           // Landing: Floor draws the ring; None/Wall a short end mark
    std::uint8_t follow = 0;            // the landing is "on me" (drawn in the follow colour)
    std::uint8_t count = 0;             // points used
    std::int32_t member = -1;           // -1 everyone, else the squad's index in the party (its colour)
    XrVector3f points[kMaxPoints]{};    // the arc, tracking space (OpenXR axes, metres)
    XrVector3f landingPoint{};          // tracking space
    XrVector3f landingNormal{0.0f, 1.0f, 0.0f};
    float ringRadiusM = 0.45f;
    float heldS = 0.0f;                 // seconds held (the ring pulses in)
    // The squad wheel (picking: no arc, count 0): its tiles' names (left, top, right), the lit one (-1 none), where it stands
    // (tracking space, +Z toward the eyes, as power_wheel::Place), how far the hand is from its centre in its plane, and the
    // seconds since it opened and since the lit tile changed. After the pick, `tile` is who goes (the ring's label).
    std::uint8_t wheel = 0;
    std::uint8_t tiles = 0;
    std::int8_t tile = -1;
    std::uint8_t padding = 0;
    char names[3][24] = {};
    XrPosef wheelPose{{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f}};
    float wheelWidthM = 0.30f;
    Vec2 hand;
    float openFor = 0.0f;
    float litFor = 0.0f;
};
void Publish(View const& view);
View Latest(std::uint64_t maxAgeMs = 250);

}  // namespace mevr::vr::squad_place
