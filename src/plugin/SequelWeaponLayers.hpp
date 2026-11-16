// SPDX-License-Identifier: MIT
#pragma once
#include "halcyon/SequelPresentation.h"
struct ID3D11Device;
namespace mevr::host::sequel_weapon_layers {
// Shared LE2/LE3 sight-first/ring-suppression composition. reticleMode is the HandReticle legacy context:
// 0 on foot, 1 a verified turret aim ray, -1 disallowed. Native crosshair ownership stays in each adapter.
unsigned Layers(ME2VR::MevrBridge::OverlayFrame const& frame, ID3D11Device* device, int reticleMode,
    void const** out, unsigned count, unsigned capacity, bool requirePublishedAim = false) noexcept;
}
