// SPDX-License-Identifier: MIT
#pragma once
// The host's request that the game's own crosshair be hidden (Renderer.hpp SetScreenReticleHidden / ScreenReticleHidden)
// for the renderers whose hiding is the mod's (game/ScreenReticle on the game thread, HostServices hideScreenReticle):
// ME2's and ME3's, one rule. As LE1's (pchud.cpp's [MEVR] SetCrosshairHidden): our layers ask each XR gameplay frame and
// the request lasts 500 ms, so the crosshair comes back by itself when VR frames stop. Any thread.

#include <windows.h>

#include <atomic>
#include <cstdint>

namespace mevr::host::screen_reticle_request {

inline constexpr std::uint64_t kHoldMs = 500;

inline std::atomic<std::uint64_t> gHideUntilMs{0};

inline void Set(bool hidden) noexcept {
    gHideUntilMs.store(hidden ? GetTickCount64() + kHoldMs : 0, std::memory_order_release);
}

inline bool Get() noexcept {
    return GetTickCount64() < gHideUntilMs.load(std::memory_order_acquire);
}

} // namespace mevr::host::screen_reticle_request
