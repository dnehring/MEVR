// SPDX-License-Identifier: MIT
#pragma once

#include "core/vr/WorldUi.hpp"

#include <openxr/openxr.h>

struct ID3D11Device;
struct ID3D11DeviceContext;

// The faint fan of light from the wrist to a screen projected from it (research/53 section 6; core/vr/WorldUi BeamTo and
// BeamPixels): one quad in the app space whose picture narrows from the screen's width at its bottom edge to a point at the
// wrist, brightest there. Its picture is made once per session (128 x 128, premultiplied); after that only the layer goes out.
// Nothing at all while no screen is on the wrist. Snapshots carry it as the "beam" layer.
namespace mevr::host::wrist_beam {

struct Frame {
    XrSession session = XR_NULL_HANDLE;
    XrSpace appSpace = XR_NULL_HANDLE;
    ID3D11DeviceContext* context = nullptr;
    ID3D11Device* device = nullptr;
};

// Present thread: the fan's quad for `beam`, or nullptr when it can't be shown.
XrCompositionLayerQuad const* Layer(Frame const& frame, vr::world_ui::Beam const& beam);

} // namespace mevr::host::wrist_beam
