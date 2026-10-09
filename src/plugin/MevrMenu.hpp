#pragma once

#include <openxr/openxr.h>

// Between our additions to Halcyon's menu (MevrMenuPointer.cpp, MevrMenuTab.cpp). Present thread.
namespace MELEVR::MevrBridge {

// The head pose (VIEW space in Halcyon's app space, the controllers' space) from the last frame
// that submitted the menu quad, if that was within `maxAgeMs`.
bool LatestMenuHead(XrPosef& head, unsigned maxAgeMs = 250) noexcept;

} // namespace MELEVR::MevrBridge
