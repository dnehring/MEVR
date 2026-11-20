// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// People leaving a first-person conversation (research/conversation-exits.md; the owner, 2026-11-19: "Udina in the post chat
// with Anderson and shepherd. He just disappears"). When the game is about to remove someone of a scene shown in the world
// (deactivate, hide, teleport) while the player can see them, the change is held back: the person walks away with the game's
// own AI toward a place the player can't see, and the game's own change is applied the moment they are out of view. No walk:
// the change waits for the player to look away, a few seconds at most, then comes under a short dip from black. Squadmates
// and anyone the next scene needs at once are left alone; changes that can't wait (a destroyed object, a level streamed out)
// go through at once. The rules here are each game's: pure data, unit-tested (tests/ConversationExitTests.cpp); the facts
// (who, where, what the eyes see) come from each game's adapter (ME1: game/ConversationExit).
namespace mevr::vr::conversation_exit {

// [Cinematics] worldExit: walk (the default with the first-person mode), hold (the look-away alone, no walk), off (the
// game's change at once, as before).
enum class Mode : std::uint8_t { Off = 0, Hold = 1, Walk = 2 };
// "walk", "hold", "off" (also "0"/"1"/"2", "on" = walk); anything else: `fallback`.
Mode ParseMode(char const* text, Mode fallback);
char const* Name(Mode mode);

// What the game asked for, in each game's terms mapped to these.
enum class Kind : std::uint8_t {
    Deactivate = 0,  // ME1: BioActorBehavior.SetActive(false) (a Kismet ModifyPropertyPawn's "Active", Udina's case)
    Hide = 1,        // the actor hidden (SeqAct_ToggleHidden, ...)
    Teleport = 2,    // moved somewhere else at once (SeqAct_Teleport, BioSeqAct_TeleportToObject)
    Destroy = 3,     // the actor destroyed: never held (nothing could give it back later)
    StreamOut = 4,   // a level or streaming state taken out: never held (the next area may wait on it)
};
char const* Name(Kind kind);

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Limits {
    double windowS = 2.0;          // a removal this soon after a scene shown in the world ended is still its exit
    double settleS = 0.25;         // out of the player's sight this long: the change is applied
    double lookAwayS = 3.0;        // no walk (refused, stalled, arrived in view): the player still looking this long: under a dip
    double walkMaxS = 12.0;        // a walk not out of sight by then has failed (then the look-away's wait)
    double holdMaxS = 25.0;        // nothing is held longer (under a dip)
    double minUnits = 300.0;       // a destination at least this far from the person (3 m)
    double maxUnits = 1200.0;      // ... and at most this far (12 m)
    double sameSpaceUnits = 60.0;  // its floor within this of the person's (the same space: no ledge, no other storey)
    double swapUnits = 200.0;      // someone shown within 2 m of a held person ...
    double swapWithinS = 1.0;      // ... within a second of the hold: the scene's swap of one actor for another
};

// The person the game removes, as the adapter sees them this tick.
struct Subject {
    bool player = false;      // the player's own pawn: never
    bool squad = false;       // in the player's squad: never (the next scene needs the squad)
    bool person = false;      // one of the scene's people (speaker, owner, listener, the speaker list) now or within the window
    bool neededNext = false;  // a scene that has begun since needs them (its owner, speaker or speaker list)
    bool seen = false;        // inside the view and in sight of the drawn eyes
};

struct Context {
    Mode mode = Mode::Walk;
    bool worldMode = false;       // [Cinematics] conversations = world
    bool underBlack = false;      // the game's own black screen turned black within the last 1.5 s: it covers the change
    bool sceneShown = false;      // a conversation (or a scripted walk) is shown in the world now
    double sinceSceneEndS = -1.0; // seconds since one was last shown; negative: none in this level
};

struct Verdict {
    bool hold = false;
    char const* why = "";  // a static string: for the log
};

// Whether a removal is an exit to hold back.
Verdict Classify(Kind kind, Subject const& subject, Context const& context, Limits const& limits = {});

// ---- Where to walk -----------------------------------------------------------------------------------------------------------
// Points to try around the person (on the person's floor height; the adapter traces each): 12 bearings, three distances
// between minUnits and maxUnits, the bearings away from the eyes first.
std::vector<Vec3> CandidatePoints(Vec3 person, Vec3 eyes, Limits const& limits = {});

struct Candidate {
    Vec3 at;                 // on the floor (the adapter's trace down)
    bool path = false;       // the way there is clear at knee and chest height, the floor within sameSpaceUnits: the same space
    bool hidden = false;     // the drawn eyes don't see someone standing there (head and chest lines blocked)
    bool scripted = false;   // where the script sends them (a teleport's destination)
};
// The destination: where the script sends them if the way there is clear (whatever the eyes see: the walk is the move the
// script means); else a hidden place with a clear way, at minUnits..maxUnits, not toward the player, the most away from the
// eyes and the nearest among equals; else (an open hall: the council chamber had no hidden place within 12 m) the farthest
// place with a clear way within 60 degrees of straight away from the eyes: the person walks off, and the change waits for them
// to leave the view (Step). Nullopt: nowhere with a way (the look-away's wait instead).
struct Pick {
    std::size_t index = 0;
    bool hidden = false;  // the place is hidden from the eyes (else only away from them)
};
std::optional<Pick> PickDestination(std::span<Candidate const> candidates, Vec3 person, Vec3 eyes, Limits const& limits = {});

// ---- When to apply -----------------------------------------------------------------------------------------------------------
enum class Act : std::uint8_t {
    Keep = 0,           // still held
    Apply = 1,          // the game's change now (out of sight, or the player sees no world)
    ApplyUnderDip = 2,  // the game's change now, under a short dip from black (the player kept looking)
};
char const* Name(Act act);

struct Held {
    double heldS = 0.0;                 // when the change was held (the adapter's clock, seconds)
    std::optional<double> unseenSinceS; // out of the player's sight since
    std::optional<double> waitSinceS;   // the look-away's wait began (no walk under way)
};

struct Facts {
    double nowS = 0.0;
    bool seen = true;        // inside the view and in sight of the drawn eyes
    bool walking = false;    // a walk is under way and making its way
    bool viewAway = false;   // the player sees no world in first person (a screen, a load, the 3D screen, a cutscene)
};

struct StepResult {
    Act act = Act::Keep;
    char const* why = "";
};
StepResult Step(Held& held, Facts const& facts, Limits const& limits = {});
// A walk still under way after walkMaxS has failed: the adapter ends it (and the look-away's wait begins).
bool WalkOverdue(Held const& held, double nowS, Limits const& limits = {});
// Someone the script shows (an activation) within swapUnits of a held person, within swapWithinS of the hold: one actor
// swapped for another (ME1's Benezia: the conversation's pawn out, the combat one in): the held change is applied at once.
bool IsSwap(Vec3 heldAt, Vec3 shownAt, double sinceHoldS, Limits const& limits = {});

// ---- The live audit (vanished-in-view) ---------------------------------------------------------------------------------------
// One of the scene's people seen last tick, gone this tick (hidden, removed), with no dip from black over the change and not
// a change of ours under one: the pop the owner saw. The adapter calls it per person per tick.
bool VanishedInView(bool seenBefore, bool presentBefore, bool presentNow, bool dipped);

} // namespace mevr::vr::conversation_exit
