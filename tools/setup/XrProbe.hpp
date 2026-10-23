#pragma once

#include <string>

// Asks the active OpenXR runtime for the headset's eye size (research/33): an instance, the HMD system, its stereo
// views' recommended image size; no session, nothing drawn. The runtime may start (SteamVR opens), so the setup only
// does this when the player presses Detect, after saying so. [untested: not run while this was written]
namespace mevr::setup {

struct HeadsetProbe {
    bool ok = false;
    int width = 0;
    int height = 0;
    std::string system;   // xrGetSystemProperties' systemName
    std::string runtime;  // xrGetInstanceProperties' runtimeName
    std::string error;
};

HeadsetProbe ProbeHeadset();

} // namespace mevr::setup
