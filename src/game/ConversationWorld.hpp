// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/ConversationWorld.hpp"

#include <string>

// LE1 conversations in the world, first person (research/59, the game's side; core/vr/ConversationWorld.hpp). Through our
// reflection layer: the live conversation (BioWorldInfo.m_oCurrentConversation: its speaker, listener, stage, speaker list,
// look-at list, the actors' saved places) and the dialogue wheel's handler.
//   - Measures, whatever the setting (logs `convo:`): at each conversation's start and end every actor of it (place, facing,
//     head bone), each shot (the director's camera against Shepard's eyes: an over-the-shoulder, a reverse, a cutaway, whether
//     the shot's subject can be seen from Shepard's place), each move or turn of an actor, who looks at whom.
//   - With `[Cinematics] conversations = world`: holds the main view at Shepard's on-foot view (moved with his pawn) for the
//     host's render (HostServices v33), keeps arm IK and body follow running in the held frame (view_anchor::Hold), and says
//     where the speaker's subtitle goes and whether the wheel is up.
// Game thread.
namespace mevr::game::conversation_world {

struct Link {
    // HostServices v33 setConversationWorld; null: an older host (the measurements still run, no world mode).
    void (*publish)(vr::conversation_world::State const* state) = nullptr;
};

// The camera-update handler (the on-foot view and the POV's address); after ue3::runtime::Ready.
void TryStartup();
void Tick(double deltaSeconds, Link const& link);
// The world mode is asked for: the conversation hooks (engine/ConversationHooks; research/69) should go in.
bool WantsHooks();
// ... and people walk to their marks by the game's AI (worldWalk = 1): the CallFunction hook should go in too (its answer to
// BioAiController.DoesGameModeAllowMovement; research/69 "Walking to marks").
bool WantsScriptHook();
// A conversation is shown in the world now (arm IK keeps Shepard's arms on the controllers as on foot).
bool InWorld();
// A matinee's move of the player's pawn is to be withheld (worldMoments = walk; research/69 "Authored scenes"): asked from
// inside the engine's physics step through game/AuthoredMoment; reads the mod's state and the game mode only.
bool MomentWithholds();
// A moment's first-person view is up (game/AuthoredMoment keeps the script's black screen off it).
bool MomentViewOn();
// The player's view is away from Shepard's pawn (the script moves the actor, the view is the player's) and his body goes
// with the player ([Cinematics] worldBody = player): the body follower then slides the whole body to where the player
// stands, without its limit, and lifts it by `riseUnits` (the player's floor above the pawn's, world units).
struct BodyAwayState {
    bool on = false;
    double riseUnits = 0.0;
    // How far the legs stand by leg IK (1) instead of playing the script's walk animation in place (0): 1 while the view
    // rests, 0 while it moves (the stick, the guided walk), eased; 0 with [Cinematics] worldLegs = walk.
    double legsStand = 0.0;
};
BodyAwayState BodyAway();
// The script switched the cinematic mode on while the player stood on foot (game/AuthoredMoment's handler, the game thread):
// a moment may be starting. The view is held where the player stands from this instant, and published at once, until a
// track moves Shepard (a moment) or a quarter of a second passes (a cutscene: it goes to the screen as before). Without it
// the host showed the 3D screen for the few frames before the track's first move: a flash and two dips as every walk began.
void MomentMayStart();
// A short dip from black over the moment's held view (the host's transition fade), the view where it is: for what the
// script covered with its own black screen. False without a moment's view.
bool MomentDip();
// The script turned its black screen on (game/AuthoredMoment's handler, the game thread; `action` the BioSeqAct_BlackScreen's).
// In a staged scene this is the game's own cut to a new composition (Kirrahe's speech on Virmire: everyone onto new marks
// under it): for the next second the stage's placements of the others are taken at once instead of walked; Shepard is never
// moved: the mark the scene wants him on is walked to by a simulated left stick, which the player's own stick takes over
// (the owner, 2026-11-12: "instead of forcing Shepard to move ... a simulated analog stick move"; research/69 "Virmire's start
// from above").
void ScriptBlackScreen(int action);
// `mevr convo [dump | world on|off|screen|vr | pick <0..5> | skip | end | walk <x> <y> [seconds] | off]`.
std::string Command(std::string const& rest);
std::string StatusJson();
void LogStats();
void Shutdown(Link const& link);

} // namespace mevr::game::conversation_world
