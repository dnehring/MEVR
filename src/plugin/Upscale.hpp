#pragma once

#include <string>

// Spatial upscaling of the eye images in Halcyon's eye blit (research/32; the math and shader text in
// core/vr/Upscale): with [Upscale] method = fsr in ASI\LE1MEVR.ini and a stereo target smaller than the headset's
// eye (tools/vr-game-settings.ps1 -Scale 0.75), each eye's 8-bit source is scaled to the eye swapchain by FSR 1's
// EASU into an intermediate of the eye's size, then RCAS into the eye, with the colour correction (plugin/Colour)
// at the end of RCAS as it is at the end of Halcyon's fill. The seam is [MEVR] upscale in BlitTextureToEyeWithFill
// (xr_session.cpp), declared in mevr_bridge.h (EyeUpscale): it returns false, and Halcyon's own bilinear fill
// draws, whenever the method is off, the shaders aren't ready, the source isn't a smaller 8-bit image, or the eye
// takes the linear (float HDR) path. Re-read once a second; log lines start with "upscale:" (the setting on change,
// the GPU cost per eye every 10 s).
namespace mevr::host::upscale {

// {"method": "fsr", "sharpness": 0.9, "active": bool, "source": [w, h], "target": [w, h], "gpuMs": x}
std::string StatusJson();

} // namespace mevr::host::upscale
