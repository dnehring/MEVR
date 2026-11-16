// SPDX-License-Identifier: MIT
#pragma once

struct DXGI_OUTPUT_DESC1;

// The game's "is the display HDR?" question, answered the same way on the three games ([Colour] hdrDisplay in
// <GAME>MEVR.ini: <0 answers SDR for HDR10, the default; 0 keeps the real descriptor; >0 answers HDR10 for other colour
// spaces; research/27 7c, research/le2-hdr.md).
namespace mevr::host::hdr_display {
// The policy: rewrites `desc` (what IDXGIOutput6::GetDesc1 returned) as the setting says, and logs the first three
// answers. `moveOldDefault`: LE1 only, a file that says 0 (LE1's default before 2026-11-14) moves to -1 once. Any thread.
void Answer(DXGI_OUTPUT_DESC1* desc, bool moveOldDefault) noexcept;

// The sequels' route to Answer: our own hook on the first output's GetDesc1 (LE1's route is Halcyon LE1's patch of the
// same slot, through MELEVR::MevrBridge::HdrDisplayDesc). Called under the renderer's factory-hook mutex, before
// returning the factory to the game. Process-lifetime hook: the host is not hot-unloaded.
void OnFactory(void* factory) noexcept;
} // namespace mevr::host::hdr_display
