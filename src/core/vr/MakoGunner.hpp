// SPDX-License-Identifier: MIT
#pragma once

#include <span>
#include <string>

// The Mako's gunner (research/47 "The gunner", 2026-11-04): riding in the cockpit, a squadmate runs the turret: it picks the
// nearest hostile it can see, aims, fires the machine gun in bursts and the cannon at big targets. The player takes the gun at
// once by deflecting the stick's thumbstick (the turret by the stick, as before) or by aiming with the free right hand (off the
// stick, the index finger on the trigger or pulling it, the ray out through the front); the gunner takes it back after
// `handbackS` without either. The rules, engine-agnostic and unit-tested; game/mako_gunner reads the world and fires.
namespace mevr::vr::mako_gunner {

struct Settings {
    bool on = true;              // [Mako] gunner
    float rangeM = 120.0f;       // gunnerRangeM: targets this close
    float handbackS = 3.0f;      // gunnerHandbackS: the gunner's again this long after the player's last aiming
    bool cannon = true;          // gunnerCannon: the cannon at big targets
    float settleS = 0.35f;       // on a new target this long before firing (the turret swings over)
    float burstS = 1.2f;         // the machine gun's bursts...
    float pauseS = 0.5f;         // ...and the pause between them
    float cannonEveryS = 3.0f;   // the cannon at most this often
    float bigRadiusM = 1.4f;     // a target this wide (its collision radius) takes the cannon too...
    int crowdCount = 3;          // ...and so does one with this many hostiles (itself included) within crowdRadiusM: the blast
    float crowdRadiusM = 6.0f;   //    hits them all (the user, 2026-11-04: "the rocket launcher for heavier/crowded targets")
    float stickDeadZone = 0.25f;  // the thumbstick past this aims by hand
    float stickyFactor = 0.7f;   // another target replaces the current one only if this much nearer
    bool operator==(Settings const&) const = default;
};

enum class Control { Gunner, Stick, Hand };
char const* Name(Control control);

// What the player's hands do this tick.
struct Hands {
    bool stickHeld = false;     // the right hand holds the cockpit's stick
    float thumbX = 0.0f;        // the right thumbstick
    float thumbY = 0.0f;
    bool handFree = false;      // the right controller tracked and not on the stick
    bool handIntent = false;    // its index finger on the trigger (the touch sensor) or pulling it
    bool handPointsOut = false;  // its ray leaves through the front (not at the floor or the seat)
};

struct ControlState {
    Control control = Control::Gunner;
    double lastManual = -1e9;  // when the player last aimed
};

// Who aims this tick (`now` in seconds). Manual input wins at once; the gunner's again after `handbackS` without any.
Control Step(ControlState& state, Hands const& hands, double now, Settings const& settings);
// The hand aims: free, intent, out through the front. The thumbstick aims: held and past the dead zone.
bool HandAims(Hands const& hands);
bool StickAims(Hands const& hands, Settings const& settings);

// A hostile the gunner could shoot, as the world read it this tick.
struct Candidate {
    int id = -1;
    double distanceM = 0.0;
    bool visible = false;   // nothing between the muzzle and it
    bool inCone = true;     // within the turret's reach
    double radiusM = 0.0;   // its collision radius
    int neighbours = 0;     // other hostiles within crowdRadiusM of it
};
// The target: the current one while it stays valid unless another valid one is nearer than stickyFactor of its distance; else the
// nearest valid one; -1 if none. Valid: visible, in the cone, within range.
int Choose(std::span<Candidate const> candidates, int current, Settings const& settings);
bool IsBig(Candidate const& candidate, Settings const& settings);
// Within crowdRadiusM of the others: crowdCount or more hostiles together.
bool IsCrowd(Candidate const& candidate, Settings const& settings);
// The cannon for this target: big, or in a crowd (and [Mako] gunnerCannon on).
bool UseCannon(Candidate const& candidate, Settings const& settings);
// For each of `positions` (metres), how many others are within `radiusM` of it.
void CountNeighbours(std::span<Candidate> candidates, std::span<double const> xyz, double radiusM);

// The firing schedule.
struct FireState {
    int target = -1;
    double since = 0.0;        // when the current target was taken
    bool mg = false;           // the machine gun's trigger held now
    double burstUntil = 0.0;
    double nextBurst = 0.0;
    double nextCannon = 0.0;
};
struct Orders {
    bool mg = false;       // hold the machine gun's trigger this tick
    bool cannon = false;   // fire the cannon this tick (a press)
};
// What to fire at `target` (-1: nothing: the machine gun's trigger let go) this tick; `cannon`: UseCannon's answer for it.
Orders Fire(FireState& state, double now, int target, bool cannon, Settings const& settings);

Settings Sanitized(Settings settings);
std::string Describe(Settings const& settings);

} // namespace mevr::vr::mako_gunner
