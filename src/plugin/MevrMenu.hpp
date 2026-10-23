#pragma once

#include <openxr/openxr.h>

#include <string>

// Between our additions to Halcyon's menu (MevrMenuPointer.cpp, MevrMenuTab.cpp). Present thread.
namespace MELEVR::MevrBridge {

// The head pose (VIEW space in Halcyon's app space, the controllers' space) from the last frame
// that submitted the menu quad, if that was within `maxAgeMs`.
bool LatestMenuHead(XrPosef& head, unsigned maxAgeMs = 250) noexcept;

// The mod's settings file, <GAME>MEVR.ini next to the host ASI (MevrMenuTab.cpp); empty if unknown.
std::wstring const& SettingsPath() noexcept;

// An arm-length T-pose sample, written as the tab's "Measure arm length" writes it ([Arms] measure* and then
// measureRequest + 1; the mod measures armLengthM from it): grip and head positions in the controllers'
// tracking space (meters). `why` names the caller in the log. The guided calibration uses it too.
void TakeArmSample(XrVector3f const& left, XrVector3f const& right, XrVector3f const* head, char const* why) noexcept;

// Camera alignment armed from the VR hands tab (MevrMenuTab.cpp): each frame, menu open or not.
void TickCameraAlign() noexcept;
// While armed, the left trigger is the alignment's, not the game's.
bool CameraAlignOwnsLeftTrigger() noexcept;

} // namespace MELEVR::MevrBridge
