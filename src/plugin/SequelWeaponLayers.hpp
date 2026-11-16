// SPDX-License-Identifier: MIT
#pragma once
#include "halcyon/MevrSeams.h"
struct ID3D11Device;
namespace mevr::host::sequel_weapon_layers {
// Shared LE2/LE3 sight-first/ring-suppression composition. reticleMode is the HandReticle legacy context:
// 0 on foot, 1 a verified turret aim ray, -1 disallowed. handAim: the right controller aims weapons (LE1's
// ControllerAimActive: sequel_bridge::ControllerAimActive). Native crosshair ownership stays in each adapter.
unsigned Layers(MevrSeams::OverlayFrame const& frame, ID3D11Device* device, int reticleMode,
    void const** out, unsigned count, unsigned capacity, bool handAim) noexcept;
}
