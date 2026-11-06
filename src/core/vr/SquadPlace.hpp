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
    float minHoldS = 0.15f;      // a shorter press is a blip: nothing
    float maxHoldS = 15.0f;      // held longer: called off
    float followWithinM = 2.0f;  // a landing this close to Shepard means "on me"
};

enum class Landing : std::uint8_t { None, Floor, Wall };
char const* Name(Landing landing);

struct Input {
    float dt = 0.0f;
    bool usable = false;       // on foot in VR, the right controller tracked, a squad present, nothing else holds B
    bool button = false;       // B is down
    int pointedMember = -1;    // the squadmate the right hand's straight ray points at now (-1: none)
    Landing landing = Landing::None;
    float landingFromPlayerM = 0.0f;  // Floor: how far from Shepard
};

enum class Order : std::uint8_t { None, Move, Follow };
char const* Name(Order order);

struct Step {
    bool started = false;      // B went down: the arc shows
    bool given = false;        // let go on floor: order given (order, member)
    bool cancelled = false;    // let go elsewhere, held too long, or no longer usable
    bool landingChanged = false;  // the landing's kind changed while holding (a haptic tick)
    Order order = Order::None;
    int member = -1;           // -1: everyone
    char const* why = "";
};

class Machine {
public:
    Step Advance(Input const& in, Settings const& settings);
    bool Holding() const { return holding_; }
    int Member() const { return member_; }
    float HeldS() const { return heldS_; }
    void Reset();

private:
    bool holding_ = false;
    bool wasDown_ = false;
    bool blocked_ = false;  // B was down before it could start (held through a menu): wait for its release
    float heldS_ = 0.0f;
    int member_ = -1;
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
};
void Publish(View const& view);
View Latest(std::uint64_t maxAgeMs = 250);

}  // namespace mevr::vr::squad_place
