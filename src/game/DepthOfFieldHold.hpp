// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The engine's depth of field switched off for a while and back to what the game's BIOEngine.ini said (ME1's conversation
// world's rule, game/ConversationWorld's DofOff/DofBack, research/59): through the local player's console (`scale set
// DepthOfField`, a boolean through its opposite first, research/35), the ini's value read before the switch (the game writes
// the live value there during play). The caller decides when: never during a load on ME2 (gotchas/me2-scale-set-during-load).
// Game thread.
namespace mevr::game::depth_of_field {

class Hold {
public:
    explicit Hold(char const* owner) : owner_(owner) {}
    // Off now (once); `why` for the log.
    void Off(char const* why);
    // Back to the ini's value; kept owed (tried again on the next call) while the console isn't there.
    void Back(char const* why);
    bool IsOff() const { return off_; }

private:
    std::string owner_;
    bool off_ = false;
    bool was_ = true;
};

} // namespace mevr::game::depth_of_field
