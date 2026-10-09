#pragma once

#include <openxr/openxr.h>

// Between our additions to Halcyon's menu (MevrMenuPointer.cpp, MevrMenuTab.cpp). Present thread.
namespace MELEVR::MevrBridge {

// The head pose (VIEW space in Halcyon's app space, the controllers' space) from the last frame
// that submitted the menu quad, if that was within `maxAgeMs`.
bool LatestMenuHead(XrPosef& head, unsigned maxAgeMs = 250) noexcept;

// Camera alignment armed from the VR hands tab (MevrMenuTab.cpp): each frame, menu open or not.
void TickCameraAlign() noexcept;
// While armed, the left trigger is the alignment's, not the game's.
bool CameraAlignOwnsLeftTrigger() noexcept;

} // namespace MELEVR::MevrBridge
