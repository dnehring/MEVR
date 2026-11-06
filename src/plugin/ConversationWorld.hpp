// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/ConversationWorld.hpp"

#include <optional>
#include <string>

// Conversations in the world, first person (research/59): the host's side. The mod's state (HostServices v33,
// core/vr/ConversationWorld's State) says a conversation is shown in the world; Halcyon's [MEVR] seams then present game mode 5
// as on foot (MevrBridge::ConversationInWorld), the main view's CalcSceneView gets the held view (plugin/GamePointer's view swap),
// and HalcyonOverlays puts the conversation movie on the HUD panel with its subtitle at the speaker and its wheel at a hand.
namespace mevr::host::conversation_world {

// The mod's state while fresh and active (any thread).
std::optional<vr::conversation_world::State> Current();
// Logs each start and end of the world view (render thread, once a frame).
void NoteFrame(int gameMode);
std::string StatusJson();

} // namespace mevr::host::conversation_world
