// SPDX-License-Identifier: MIT
#pragma once
#include "core/Json.hpp"
#include <cstdint>
#include <memory>
namespace mevr::engine::le2_diagnostic {
#if defined(MEVR_GAME_LE2)
// Verified game-thread, direct guarded reads only. Snapshot, not event history,
// legal-input determination, accepted reply proof or durable identity.
json::Value ObserveDialogue(std::uint32_t gameThreadId);
// Host-owned, single verified game-thread recorder. No global run state.
// Start does no engine reads; Tick samples at most once/125ms for <10 seconds.
// Snapshot/Stop are serialized by that same owner. No event completeness claim.
// Completed recordings can be replaced by explicit Start; active restart refuses.
class DialogueHistory {
public:
    DialogueHistory() noexcept;
    ~DialogueHistory();
    DialogueHistory(DialogueHistory const&) = delete;
    DialogueHistory& operator=(DialogueHistory const&) = delete;
    bool Start(std::uint64_t tick,std::uint64_t monoMs,std::uint32_t thread) noexcept;
    void Tick(std::uint64_t tick,std::uint64_t monoMs,std::uint32_t thread) noexcept;
    void Stop() noexcept;
    bool Active() const noexcept;
    json::Value Snapshot() const;
private:
    struct Storage;
    std::unique_ptr<Storage> storage_;
};
#if defined(MEVR_LE2_OBSERVATION_TEST)
namespace testing { void SetDialogueSampleHook(void (*hook)()); }
#endif
#endif
}
