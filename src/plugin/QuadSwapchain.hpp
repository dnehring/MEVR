// SPDX-License-Identifier: MIT
#pragma once

// A swapchain for one of our quads (the HUD panel, the reticle, the wheel's panel...), made through plugin/Swapchains
// (the runtime's limit, research/21 section 18): its images, a render target view per image, and the "refused" state that
// plugin/Swapchains says when to try again. Game-independent: both Halcyon hosts (LE1's HalcyonOverlays, LE2's bridge).
// Present thread only.

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <cstdint>
#include <functional>
#include <vector>

namespace mevr::host {

struct QuadSwapchain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;  // owned by the runtime
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> rtvs;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    bool failed = false;          // refused (or its texture failed) at failedMs
    std::uint64_t failedMs = 0;

    // Refused just now: not tried again until plugin/Swapchains says (2 s, or once room was freed). Never for the session.
    bool Refused() const;
    void Refuse();
    void Forget();   // the session that owned it is gone: its swapchains went with it
    void Destroy();
};

namespace quad_swapchain {

constexpr std::int64_t kFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // the menu's family: every runtime has it

// Makes `out` (colour attachment and sampled, kFormat). `evict`: how plugin/Swapchains lets go of it for room (default:
// Destroy; it's made again when next wanted). False (and refused) if the runtime said no.
bool Create(XrSession session, std::uint32_t width, std::uint32_t height, QuadSwapchain& out, char const* what,
    std::function<void()> evict = {});

// (Re)makes `swapchain` at width x height and names it for the snapshots (plugin/TestLoop); false if it can't.
bool Ensure(XrSession session, QuadSwapchain& swapchain, std::uint32_t width, std::uint32_t height, char const* what,
    char const* name, std::function<void()> evict = {});

// Acquire + wait; false (and nothing held) if either fails.
bool Acquire(QuadSwapchain& swapchain, std::uint32_t& index);
void Release(QuadSwapchain& swapchain);

} // namespace quad_swapchain
} // namespace mevr::host
