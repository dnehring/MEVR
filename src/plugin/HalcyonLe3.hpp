// SPDX-License-Identifier: MIT
#pragma once
#include "core/vr/Traversal.hpp"
// LE3 uses the pinned Halcyon renderer as the sole Present/OpenXR owner.
namespace mevr::host::halcyon_le3 {
void Attach();
void AfterTick(float deltaSeconds);
bool SetTraversalPresentation(vr::traversal::Presentation const* value) noexcept;
// HostServices v42 optional capability. Any thread; no OpenXR/native calls.
bool ThespianInputBlocked() noexcept;
}
