// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// People leaving a first-person conversation, ME1's side (core/vr/ConversationExit for the rule; research/conversation-exits.md).
// With [Cinematics] conversations = world and worldExit = walk (the default with it) or hold: the level script's deactivation
// of someone of the scene while the player sees them (BioActorBehavior.SetActive(false), through engine/ExitHooks: Udina's
// "Deactivate the ambassador" after the council hearing's aftermath, Chorban's after his) is held back with its arguments; the
// person walks away with the game's own AI (game/AiWalk) toward a place the player can't see; the same call is made through
// ProcessEvent the moment they are out of sight, and they are put back where they stood (hidden, as the script left them),
// so the plot and later scenes are unaffected. No walk: the call waits for the player to look away, at most a few seconds,
// then comes under a dip from black. Squadmates, anyone the next scene needs, and changes the game makes out of the player's
// sight go through at once. A load, a level change, a screen, the setting off, an unload or a hot reload apply every held
// change at once. The live audit's rule `vanished-in-view` watches the scene's people every tick. Game thread.
namespace mevr::game::conversation_exit {

// The reflection handles and the hook's provider; after ue3::runtime::Ready.
void TryStartup();
// The hook (engine/ExitHooks) is wanted: the first-person mode on and worldExit not off.
bool WantsHook();
// ... and walks: game/AiWalk needs the CallFunction hook (its movement-ban answer) and the name lookup.
bool WantsWalks();
void Tick(double deltaSeconds);
// `mevr convo exit [on | walk | hold | off | auto | status]`.
std::string Command(std::string const& rest);
std::string StatusJson();
void LogStats();
// Every held change applied (unless the process exits: nothing to keep then); the provider taken back.
void Shutdown(bool exiting);

} // namespace mevr::game::conversation_exit
