// SPDX-License-Identifier: MIT
#pragma once

#include "install/Ini.hpp"

#include <string>
#include <string_view>
#include <vector>

// The VR game settings tools/vr-game-settings.ps1 applies, as text transforms for the setup app (research/33):
// GamerSettings.ini's [SystemSettings] and [HDR], BIOEngine.ini's frame smoothing, Halcyon's copy of the stereo
// target. Every change is recorded (ini::Edit) so Restore puts back exactly the player's values.
namespace mevr::install::settings {

// The stereo target. Width 0: keep whatever the file has.
struct Target {
    int width = 0;
    int height = 0;
};

// The target for a choice: "headset" (the runtime's recommended eye size, when known), "low", "performance",
// "balanced", "sharp", "max" (Halcyon's ladder: width, width / 0.964 even), or "keep". {0, 0} for "keep", an unknown
// key, or "headset" without a known size.
Target TargetFor(std::string_view choice, int headsetWidth, int headsetHeight);

struct Choices {
    Target target;             // {0,0}: leave ResX/ResY alone
    bool vsyncOff = true;      // UseVSync=False
    bool motionBlurOff = true; // MotionBlur=False
    bool filmGrainOff = true;  // FilmGrain=False
    bool hdrOff = true;        // [HDR] DesiredRange=DynamicRange_SDR
    bool anisotropy16 = true;  // MaxAnisotropy=16
    // LE1 always: BorderlessWindow=True, Fullscreen=False (Halcyon's stereo needs the window as big as the target),
    // DynamicShadows=False (Halcyon's default; the scene renders twice a frame).
};

// GamerSettings.ini with the choices applied.
std::string ApplyGamerSettings(std::string_view text, Choices const& choices, std::vector<ini::Edit>* edits);
// BIOEngine.ini: SmoothFrameRate=FALSE wherever a section sets it TRUE (the game's 60 fps cap).
std::string ApplyEngineSettings(std::string_view text, std::vector<ini::Edit>* edits);

// What a GamerSettings.ini holds now, for the Settings page: "2824 × 2880", "On"/"Off"...
struct Current {
    int resX = 0;
    int resY = 0;
    bool borderless = false;
    bool vsync = true;
    bool motionBlur = true;
    bool filmGrain = true;
    bool hdr = false;
};
Current Read(std::string_view gamerSettings);

} // namespace mevr::install::settings
