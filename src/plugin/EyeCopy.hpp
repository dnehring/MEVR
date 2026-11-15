// SPDX-License-Identifier: MIT
#pragma once

#include <d3d11.h>

// Copies the game's picture into an OpenXR swapchain image whatever the two formats are, for Halcyon's ME2 renderer
// (me2_xr.cpp copies its eyes and its flat panel with CopyResource, which D3D11 drops without a word when the formats
// aren't of one family). ME2's swap chain is R16G16B16A16_FLOAT in gameplay on this machine (research/
// le2-renderer-integration.md "The black eyes"): such a source is drawn through an sRGB view of the image
// (core/d3d11/Blit) instead. Present thread only.
// ME1's colour correction (plugin/Colour, research/27; research/le2-picture.md) is drawn here too: with a profile that isn't
// neutral every copy into an eye or the flat panel is a corrected pass of core/vr/Colour's prelude (an 8-bit source through
// the image's UNORM view, its encoded values as the raw copy leaves them; a float one through its sRGB view). Neutral leaves
// the copies as they were.
namespace mevr::host::eye_copy {

// Copies `source` into `image` when CopyResource can't, or with the colour correction: true if it did (drawn), false when
// the caller's own copy is right (the same format family and nothing to correct, or an image this can't draw into; logged
// once). `shiftX`: Halcyon's convergence shift in pixels (0 none). `eye`: 0 left, 1 right, -1 the flat panel (the
// correction's dither pattern).
bool Copy(ID3D11DeviceContext* context, ID3D11Texture2D* image, ID3D11Texture2D* source, int shiftX = 0, int eye = -1);

// The session's swapchains are gone (or new): the image views are released.
void Reset();

// True when D3D11's CopyResource can copy between the two formats (one typeless family). Exposed for the tests.
bool CopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b);

} // namespace mevr::host::eye_copy
