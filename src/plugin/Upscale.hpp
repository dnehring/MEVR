// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/Upscale.hpp"

#include <dxgiformat.h>

#include <string>

struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11Texture2D;
namespace mevr::d3d11 {
class Blit;
}

// Spatial upscaling of the eye images (research/32; the math and shader text in core/vr/Upscale): with [Upscale]
// method = fsr in ASI\<GAME>MEVR.ini (the default) and the game's picture smaller than the eye image, each eye's 8-bit
// source is scaled up by FSR 1's EASU into an intermediate of the eye's size, then RCAS into the eye, with the colour
// correction (plugin/Colour) at the end of RCAS. One pass and one set of settings for the three games (research/27
// "One eye-image pipeline"), reached two ways:
//  - LE1: the [MEVR] upscale seam in Halcyon's eye blit (BlitTextureToEyeWithFill in xr_session.cpp, declared in
//    src/halcyon/le1/mevr_bridge.h as EyeUpscale), in Halcyon's pipeline state and with its fill rectangle. It returns
//    false, and Halcyon's own bilinear fill draws, whenever the method is off, the shaders aren't ready, the source
//    isn't a smaller 8-bit image, or the eye takes the linear (float HDR) path. LE1's eye swapchains are the headset's
//    size; the picture is smaller when the render resolution is (tools/vr-game-settings.ps1 -Scale, the menu's Display).
//  - LE2 and LE3: plugin/EyeCopy calls Copy for every copy into an eye image. Their renderers make the eye swapchains
//    at the backbuffer's size, so the seam MevrSeams::EyeImageSize asks EyeImageSize below when they are made: with FSR
//    on and the backbuffer smaller than the size the headset recommends, the eye images are larger than the picture and
//    Copy scales it up; otherwise they are the picture's size and the copy stays raw.
// Re-read once a second; log lines start with "upscale:" (the setting on change, the GPU cost per eye every 10 s).
namespace mevr::host::upscale {

// {"method": "fsr", "sharpness": 0.9, "active": bool, "source": [w, h], "target": [w, h], "gpuMs": x}
std::string StatusJson();

// The size the sequels' eye swapchains should have (core/vr/Upscale EyeImageSize with the current settings), logged.
// The Present thread, when the renderer makes its eye swapchains. A method changed later applies to the next ones (a
// new session): until then a larger image with the method off is filled bilinearly by plugin/EyeCopy.
vr::upscale::Size EyeImageSize(unsigned sourceWidth, unsigned sourceHeight, unsigned recommendedWidth,
    unsigned recommendedHeight);

// The sequels' eye copy with the upscale: `source` (8-bit, encoded) scaled into `target`, a view of the eye image that
// stores what is written unchanged (its UNORM view), of width x height, through `blit`'s pipeline state; the colour
// correction at the end as on LE1. `shiftX`: the convergence shift in the image's pixels. False (nothing drawn): the
// method is off, the source isn't a smaller 8-bit picture, or the pass couldn't be made; the caller copies its own way.
// Present thread.
bool Copy(ID3D11DeviceContext* context, d3d11::Blit& blit, ID3D11Texture2D* source, ID3D11RenderTargetView* target,
    unsigned sourceWidth, unsigned sourceHeight, DXGI_FORMAT sourceFormat, unsigned width, unsigned height, int shiftX,
    int eye);

} // namespace mevr::host::upscale
