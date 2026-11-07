// SPDX-License-Identifier: MIT
#pragma once

#include "core/Ini.hpp"

namespace mevr::vr::foot_controls {

// LE1 EGameModes: 0 is on foot. Reserve X before the wheel opens, including taps, empty rosters and
// temporary tracking/combat refusals. Flat screens and every other game mode keep their game bindings.
inline bool HandWheelOwnsX(int mode, bool on, bool hostWheel, bool worldView, bool hasPawn) {
    return mode == 0 && on && hostWheel && worldView && hasPawn;
}

struct GrenadeMigration {
    bool button = false;  // grenadeButton = 1 -> 0
    bool hands = false;   // the old default hands = right -> both (the left hand takes grenades while the right holds the gun)
};

// Plan only: the caller writes the marker after every requested write succeeds. Subsequent explicit
// choices (including grenadeButton=1 with the hand wheel off) survive refreshes and launches.
inline GrenadeMigration MigrateGrenades(ini::File const& file) {
    if (ini::Get(file, "Throwables", "powerWheelX")) {
        return {};
    }
    return {ini::GetNumber(file, "Throwables", "grenadeButton") == 1.0,
        ini::Lower(ini::Get(file, "Throwables", "hands").value_or("")) == "right"};
}

} // namespace mevr::vr::foot_controls
