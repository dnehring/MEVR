// SPDX-License-Identifier: MIT
// Conversations in the world, first person: the host's side (plugin/ConversationWorld.hpp, research/59).
#include "plugin/ConversationWorld.hpp"

#include "halcyon/le1/mevr_bridge.h"

#include "core/Log.hpp"

#include <Windows.h>

#include <atomic>
#include <format>
#include <mutex>

namespace mevr::host::conversation_world {
namespace {

namespace cw = mevr::vr::conversation_world;

constexpr std::uint64_t kFreshMs = 300;   // the mod sends every tick
constexpr std::uint64_t kGraceMs = 250;   // the game's mode changes a tick before the mod's state does (as GalaxyHeld)

std::atomic<std::uint64_t> gLastActiveMs{0};
constexpr std::uint64_t kComingMs = 150;  // the mod's "armed" counts this long: the tick or two until its state follows the mode
std::mutex gMutex;
bool gShown = false;
std::uint32_t gConversation = 0;
std::uint64_t gFrames = 0;
std::uint64_t gStartMs = 0;
int gLastMode = -1;

} // namespace

std::optional<cw::State> Current() {
    auto state = cw::Latest(kFreshMs);
    if (state && state->size != sizeof(cw::State)) {
        return std::nullopt;
    }
    if (state) {
        gLastActiveMs.store(GetTickCount64(), std::memory_order_relaxed);
    }
    return state;
}

void NoteFrame(int gameMode) {
    auto const state = Current();
    std::lock_guard lock(gMutex);
    if (gameMode >= 0) {
        gLastMode = gameMode;
    }
    bool const shown = state.has_value();
    if (shown) {
        ++gFrames;
    }
    if (shown && (!gShown || state->conversation != gConversation)) {
        gShown = true;
        gConversation = state->conversation;
        gFrames = 1;
        gStartMs = GetTickCount64();
        mevr::log::Info("conversation world: conversation {} in the world (game mode {}): on foot's presentation, head look on, the view "
                        "held at Shepard's{}; the subtitle at the speaker, the wheel at the {}",
            state->conversation, gameMode, state->holdRender != 0 ? "" : " (not yet: no POV from the mod)",
            state->wheelHand == 0 ? "left hand" : state->wheelHand == 1 ? "right hand" : "front");
    } else if (!shown && gShown) {
        gShown = false;
        mevr::log::Info("conversation world: conversation {} out of the world after {} frames ({:.1f} s)", gConversation, gFrames,
            static_cast<double>(GetTickCount64() - gStartMs) / 1000.0);
    }
}

std::string StatusJson() {
    auto const state = Current();
    std::lock_guard lock(gMutex);
    if (!state) {
        return std::format("{{\"shown\": false, \"lastMode\": {}}}", gLastMode);
    }
    return std::format("{{\"shown\": true, \"conversation\": {}, \"frames\": {}, \"wheel\": {}, \"subtitle\": {}, \"subtitleAt\": [{:.3f}, {:.3f}, "
                       "{:.3f}], \"holdRender\": {}, \"held\": [{:.1f}, {:.1f}, {:.1f}], \"heldYaw\": {}}}",
        state->conversation, gFrames, state->wheel != 0 ? "true" : "false", state->subtitle != 0 ? "true" : "false", state->subtitleAt[0],
        state->subtitleAt[1], state->subtitleAt[2], state->holdRender != 0 ? "true" : "false", state->holdLocation[0], state->holdLocation[1],
        state->holdLocation[2], state->holdRotation[1]);
}

} // namespace mevr::host::conversation_world

namespace MELEVR::MevrBridge {

bool ConversationInWorld() noexcept {
    namespace host = mevr::host::conversation_world;
    if (host::Current()) {
        return true;
    }
    // A short grace: the game leaves (or enters) mode 5 a tick before the mod's state follows.
    std::uint64_t const last = host::gLastActiveMs.load(std::memory_order_relaxed);
    return last != 0 && GetTickCount64() - last < host::kGraceMs;
}

// [MEVR] research/69 "The flash at every scene's start": for the presentation's decision in the conversation and cinematic
// modes only: a scene the mod shows in the world, or one that began this instant with the mod armed for it (the game's mode
// is read here a tick before the mod's state can say so).
bool ConversationWorldOrComing() noexcept {
    if (ConversationInWorld()) {
        return true;
    }
    return mevr::vr::conversation_world::Armed(mevr::host::conversation_world::kComingMs);
}

} // namespace MELEVR::MevrBridge
