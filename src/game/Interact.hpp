// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/InteractLabel.hpp"

#include <string>

// Using things with A in VR (research/40; LE1, on foot, a head-tracked view; [Interact] in ASI\LE1MEVR.ini):
// - gaze: the game chooses what A would use from where the head looks. Each tick the head's place and direction in
//   the world go to engine/SelectionGaze, which hands them to BioPlayerSelection's native UpdateSelection as the
//   camera's view for that call only.
// - label: where the selected object is (above the middle of its collision bounds) goes to the host in tracking space
//   (HostServices v21, setInteractLabel); the host shows the game's own prompt there, big (plugin/HalcyonOverlays).
// Everything through our reflection layer; a lookup that fails stands it down with an `interact:` line. Game thread.
namespace mevr::game::interact {

struct Link {
    // HostServices v21 setInteractLabel; null: an older host (no label; gaze still works).
    void (*publish)(vr::interact::Label const* label) = nullptr;
};

// Whether the selection hook should go in (gaze wanted; Mod.cpp installs it lazily, like the cover hook).
bool WantsHook();
// After the view anchor's tick (arm IK), once a tick. `hooked`: engine/SelectionGaze is in.
void Tick(float deltaSeconds, bool hooked, Link const& link);
// Snapshot JSON ("interact").
std::string StatusJson();
// Heartbeat line.
void LogStats();
// Mod shutdown: no more swaps; the host's label hidden.
void Shutdown(Link const& link);

} // namespace mevr::game::interact
