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
// ME1's eye upscale too (plugin/Upscale, research/32; research/27 section 9): an 8-bit picture smaller than the image both
// ways (the sequels' eye images are larger than the backbuffer when MevrSeams::EyeImageSize says so) is scaled up by FSR 1
// with the correction at its end, before any of the paths above. A float picture gets LE1's ToEye step in the corrected
// pass (MEVR_TONE: the hue-preserving clip, or [Colour] tonemap's curve).
// This is an opaque eye/flat-frame operation (shader alpha = 1), not an alpha-preserving UI converter.
namespace mevr::host::eye_copy {

// Owns the complete operation: true if a validated raw copy/shift or shader draw was issued,
// false on failure. Callers MUST NOT fall back to another raw copy after false, or mark the
// image ready. This is GPU command admission, not a fence/presentation proof.
// `shiftX`: Halcyon's convergence shift in pixels (0 none). `eye`: 0 left, 1 right, -1 the flat panel (the
// correction's dither pattern).
bool Copy(ID3D11DeviceContext* context, ID3D11Texture2D* image, ID3D11Texture2D* source, int shiftX = 0, int eye = -1);

// The session's swapchains are gone (or new): the image views are released.
void Reset();

// True when D3D11's CopyResource can copy between the two formats (one typeless family). Exposed for the tests.
bool CopyCompatible(DXGI_FORMAT a, DXGI_FORMAT b);

} // namespace mevr::host::eye_copy
