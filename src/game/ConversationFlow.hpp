// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// LE1 conversations that flow (research/59 "Flowing conversations", 2026-11-09; `[Cinematics] worldFlow`, with the world
// mode): a play of a conversation from a start that can never reach a dialogue wheel (a scene watched on the walk up: Garrus
// arguing with the Executor, Udina's arrival, the trigger lines of a level) is a chain. The game plays a start whose first
// entry is flagged ambient as its own barks (UBioConversation::StartConversation copies the start entry's bAmbient and skips
// the conversation mode, camera and stage), so the mod flags those entries (core/vr/ConversationWorld's FlowEntries, from the
// scan) on every loaded BioSeqAct_StartConversation's conversation before the level's script fires it: the player keeps walking
// while the people talk, within the conversation's interrupt range (`worldFlowLeashM`: the game's, never, or metres). The flags
// are put back when the setting goes off or the mod unloads. Game thread.
namespace mevr::game::conversation_flow {

void Tick();
// `mevr convo flow [list | rescan | restore]`.
std::string Command(std::string const& rest);
std::string StatusJson();
void LogStats();
void Shutdown();

} // namespace mevr::game::conversation_flow
