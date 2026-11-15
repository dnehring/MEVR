// SPDX-License-Identifier: MIT
#pragma once
#include "core/ik/Melee.hpp"
#include <string>

namespace mevr::game::melee {
struct Link {
    bool enabled = false;          // explicit staged capability; default inert
    bool gestures = false;         // opt in only after native command acceptance
    bool focused = false;
    bool menuOpen = true;
    bool authoredSeated = true;    // authored scene, NOT physically seated play calibration
    // Must re-read the host's CURRENT menu/hand ownership at the instant of dispatch.
    // A missing callback refuses dispatch, including diagnostic requests.
    bool (*allowRequest)(int hand) = nullptr;
    void (*haptic)(int hand, float seconds, float amplitude) = nullptr;
    ik::melee::Input input;
};
// Game thread only. LE3 native adapter; LE1 Throwables remains the production LE1 route.
bool TryStartup();
void Tick(Link const&);
// Assisted/native diagnostic path. Pass a fresh Link; no retained menu snapshot or queued command.
// Reports "dispatched" separately from observing an action; never means impact or damage.
std::string Request(ik::melee::Intent, int hand, Link const&);
// A native request is awaiting admission or an admitted melee still owns the action slot.
bool Busy();
std::string Status();
void Shutdown();
} // namespace mevr::game::melee
