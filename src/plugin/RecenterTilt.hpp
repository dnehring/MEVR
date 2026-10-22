#pragma once

#include <string>

// A recenter that also corrects a badly tilted tracking space (core/vr/Recenter; the seam is
// MevrBridge::RecenterOrientation in Halcyon's DoRecenter). Present thread.
namespace mevr::host::recenter_tilt {

// Every frame (OverlayLayers): the head's orientation in the app space (xyzw). Tracks how long the head
// has looked tilted in the base space (a recenter's decision) and logs a hint once the world has looked
// upside down for a while.
void NoteHead(float const headOrientation[4], bool headValid);
// Halcyon made the new app space (MevrBridge::OnRecentered): the orientation it was given is now the app
// space's.
void Committed();
// For snapshots: recenters so far, how many were corrected, the head's tilt in the base space now.
std::string StatusJson();

} // namespace mevr::host::recenter_tilt
