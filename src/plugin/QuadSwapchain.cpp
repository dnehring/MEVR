// SPDX-License-Identifier: MIT
// Our quads' swapchains (plugin/QuadSwapchain.hpp), moved unchanged out of plugin/HalcyonOverlays.cpp.

#include "plugin/QuadSwapchain.hpp"

#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"

#include <utility>

namespace mevr::host {

bool QuadSwapchain::Refused() const {
    return failed && !swapchains::RetryDue(failedMs);
}

void QuadSwapchain::Refuse() {
    failed = true;
    failedMs = GetTickCount64();
}

void QuadSwapchain::Forget() {
    swapchains::Forget(handle);
    handle = XR_NULL_HANDLE;
    images.clear();
    rtvs.clear();
    width = height = 0;
    failed = false;
}

void QuadSwapchain::Destroy() {
    swapchains::Destroy(handle);
    handle = XR_NULL_HANDLE;
    images.clear();
    rtvs.clear();
    width = height = 0;
}

namespace quad_swapchain {

bool Create(XrSession session, std::uint32_t width, std::uint32_t height, QuadSwapchain& out, char const* what,
    std::function<void()> evict) {
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = kFormat;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!evict) {
        evict = [&out] { out.Destroy(); };
    }
    out.Destroy();
    if (XR_FAILED(swapchains::Create(session, info, what, std::move(evict), out.handle, out.images))) {
        out.Refuse();  // plugin/Swapchains logged it
        return false;
    }
    out.failed = false;
    out.rtvs.resize(out.images.size());
    out.width = width;
    out.height = height;
    return true;
}

bool Ensure(XrSession session, QuadSwapchain& swapchain, std::uint32_t width, std::uint32_t height, char const* what,
    char const* name, std::function<void()> evict) {
    if (swapchain.handle != XR_NULL_HANDLE && (swapchain.width != width || swapchain.height != height)) {
        swapchain.Destroy();
    }
    if (swapchain.handle == XR_NULL_HANDLE) {
        if (swapchain.Refused() || !Create(session, width, height, swapchain, what, std::move(evict))) {
            return false;
        }
        testloop::NameSwapchain(swapchain.handle, name);
    }
    return true;
}

bool Acquire(QuadSwapchain& swapchain, std::uint32_t& index) {
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(swapchain.handle, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (XR_FAILED(xrWaitSwapchainImage(swapchain.handle, &wait)) || index >= swapchain.images.size()) {
        XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        xrReleaseSwapchainImage(swapchain.handle, &release);
        return false;
    }
    return true;
}

void Release(QuadSwapchain& swapchain) {
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(swapchain.handle, &release);
}

} // namespace quad_swapchain
} // namespace mevr::host
