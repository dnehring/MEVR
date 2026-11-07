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
// A conversation is shown in the world now (arm IK keeps Shepard's arms on the controllers as on foot).
bool InWorld();
// `mevr convo [dump | world on|off|screen|vr | pick <0..5> | skip | end]`.
std::string Command(std::string const& rest);
std::string StatusJson();
void LogStats();
void Shutdown(Link const& link);

} // namespace mevr::game::conversation_world
