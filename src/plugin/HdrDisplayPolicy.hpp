// SPDX-License-Identifier: MIT
#pragma once

namespace mevr::host::hdr_display {
// Called under the renderer's factory-hook mutex, before returning the factory to
// the game. Process-lifetime hook: the host is not hot-unloaded. Same policy as LE2:
// [Colour] hdrDisplay <0 answers SDR for HDR10; 0 preserves the real descriptor;
// >0 answers HDR10 for other colour spaces. Missing key defaults to -1.
void OnFactory(void* factory) noexcept;
} // namespace mevr::host::hdr_display
