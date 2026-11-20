// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// The engine's depth of field switched off for a while and back to what the game's BIOEngine.ini said (ME1's conversation
// world's rule, game/ConversationWorld's DofOff/DofBack, research/59): through the local player's console (`scale set
// DepthOfField`, a boolean through its opposite first, research/35), the ini's value read before the switch (the game writes
// the live value there during play). The caller decides when: never during a load on ME2 (gotchas/me2-scale-set-during-load).
// Every switch also waits for the world to be drawn (game/WorldDrawn: ME3's cab ride loads in game mode 0, and a switch then hung
// it, 2026-11-20): Off and Back say what is wanted, Step (each tick) sends it once the world is drawn; never dropped.
// Game thread.
namespace mevr::game::depth_of_field {

class Hold {
public:
    explicit Hold(char const* owner) : owner_(owner) {}
    // Off (once); `why` for the log. Sent now if the world is drawn, else by a later Off, Back or Step.
    void Off(char const* why);
    // Back to the ini's value; kept owed (tried again by the next call or Step) while the console isn't there or the world
    // isn't drawn.
    void Back(char const* why);
    // Each tick: a switch still owed goes once the world is drawn.
    void Step();
    // Off is wanted (sent or still owed).
    bool IsOff() const { return off_; }

private:
    void Send();
    std::string owner_;
    std::string why_;
    bool off_ = false;      // wanted
    bool sentOff_ = false;  // what the game has from us
    bool was_ = true;
    bool told_ = false;     // "waits for the world" logged for this switch
};

} // namespace mevr::game::depth_of_field
