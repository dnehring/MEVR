// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Traversal.hpp"

#include <string>

// Vaulting and climbing on ME2 in VR (research/le2-traversal.md; the owner's design research/le2-le3-vr-traversal.md; the rules
// in core/vr/Traversal). The game owns the move; this module observes it, commits it on the player's double A, and presents it.
//
// The route (no cover snap in VR, the owner 2026-11-14): ME2 has an out-of-cover mantle switched off in config
// (BioPlayerController.m_bCanMantleOutsideOfCover, BIOGame.ini "set true to mantle from outside of cover"). The mod finds the cover
// slots that vault (bCanMantle) or climb (bCanClimbUp) near the pawn (WorldInfo.CoverList), asks the game's own
// BioPlayerController.CanPerformMantle / CanPerformClimb for the nearest (its distance, facing, target slot and landing checks),
// with that flag set and the stick's forward push (RemappedJoyUp 1, RemappedJoyRight 0) for that one call only (journalled, put
// back in a __finally); and on the second press of a double A calls SFXPlayerController.MantleCover / ClimbCover with the slot, the
// game's own function: its custom action 10 or 9 plays the authored move from where the pawn stands (its FromExplore or
// FromCombat body stance, root motion, world collision off for a moment). No cover state, no snap. A move the game begins by
// itself (its in-cover A when cover isn't blocked) is observed and judged the same way.
//
// Measured each move (`traversal:` lines, the snapshot's "mod"."traversal"): the eligibility, the presses and the commit's judgement
// (core/vr/Traversal Judge), the target, the custom action's start, its body stance and end, the pawn's path and collision, the
// game camera's pitch, roll and bob against the pawn, the head, elapsed times, when movement and aim came back, the weapon before
// and after, Shepard's hands against the controllers.
//
// Presentation ([Traversal] view, mask; defaults keep today's): the level view writes the camera's POV in the camera update's
// post-handler while the move runs (the pawn's translation, no pitch, roll or bob), then hands back; the mask is the vignette
// (HostServices v17) or a blink (v40). The cue at the interaction point goes to the host (v40 setTraversal). LE2 only; LE1 and LE3:
// TryStartup returns false and nothing runs. Game thread.
namespace mevr::game::traversal {

// What the mod's tick knows from the host.
struct Link {
    bool headTrackedView = false;
    bool controllers = false;     // tracked controllers this tick (the right A read)
    bool rightA = false;          // the right controller's A
    bool menuOpen = false;        // our menu or the renderer's
    void (*setVignette)(float amount) = nullptr;            // HostServices v17
    void (*setCue)(vr::traversal::Cue const* cue) = nullptr;  // HostServices v40
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
};

// Finds the game's functions and fields and registers the handlers (before the hooks go in). False on LE1/LE3 or with the
// reflection layer off.
bool TryStartup();
void Tick(float deltaSeconds, Link const& link);
// A native vault or climb runs (the custom action), or its view is being handed back.
bool Moving();
std::string StatusJson();
void LogStats();
// "mevr traversal ...": status, slots, near, place, commit, try, view, mask, cue, trace.
std::string Command(std::string const& args);
void Shutdown();

} // namespace mevr::game::traversal
