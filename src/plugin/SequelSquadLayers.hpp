// SPDX-License-Identifier: MIT
#pragma once
#include "halcyon/MevrSeams.h"
struct ID3D11Device;
namespace mevr::host::sequel_squad_layers {
// Shared LE2/LE3 consumers; caller supplies semantic on-foot and combined host-menu ownership.
unsigned Place(MevrSeams::OverlayFrame const&, ID3D11Device*, void const**, unsigned, unsigned) noexcept;
unsigned Panel(MevrSeams::OverlayFrame const&, ID3D11Device*, void const**, unsigned, unsigned) noexcept;
void ForgetSession();
void Recentered();
}
