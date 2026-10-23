// SPDX-License-Identifier: MIT
// The comfort vignette's quad (plugin/Vignette.hpp, research/31).

#include "plugin/Vignette.hpp"

#include "core/Log.hpp"
#include "core/vr/Comfort.hpp"
#include "plugin/TestLoop.hpp"

#include <Windows.h>
#include <d3d11.h>
#include <wrl/client.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <vector>

namespace mevr::host::vignette {
namespace {

using Microsoft::WRL::ComPtr;
namespace cf = vr::comfort;

constexpr std::int64_t kSwapchainFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;  // as our other quads
constexpr int kSize = 128;            // a soft gradient: bilinear filtering does the rest
constexpr int kLevels = 64;           // the texture is refilled when the faded strength crosses a level
constexpr std::uint64_t kStaleMs = 250;
constexpr double kStatsEverySeconds = 10.0;
constexpr float kDistance = 1.0f;     // meters ahead of the eyes (the mask assumes 1 m)

std::atomic<float> gTarget{0.0f};
std::atomic<std::uint64_t> gTargetMs{0};

// Present thread.
XrSession gSession = XR_NULL_HANDLE;
XrSpace gViewSpace = XR_NULL_HANDLE;
bool gViewSpaceFailed = false;
XrSwapchain gSwapchain = XR_NULL_HANDLE;
std::vector<ID3D11Texture2D*> gImages;  // owned by the runtime
bool gSwapchainFailed = false;
ComPtr<ID3D11Texture2D> gTexture;       // our copy, refilled from the CPU
cf::HalfExtents gExtents;
std::vector<std::uint8_t> gMask;
std::vector<std::uint32_t> gPixels;
int gLevel = -1;                        // what the swapchain holds (-1: nothing yet)
cf::VignetteFade gFade;
LARGE_INTEGER gLastCounter{};
XrCompositionLayerQuad gQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
bool gLoggedFirst = false;

// Stats since the last line (Present thread).
struct Stats {
    double since = 0.0;   // seconds (QPC) of the window's start; 0: nothing shown yet
    unsigned frames = 0;
    unsigned updates = 0;
    double updateMs = 0.0;
    float peak = 0.0f;
} gStats;

double Seconds(LARGE_INTEGER const& counter) {
    static LARGE_INTEGER const frequency = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    return static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
}

void ForgetSession() {
    // The session's swapchains and spaces went with it.
    gViewSpace = XR_NULL_HANDLE;
    gViewSpaceFailed = false;
    if (gSwapchain != XR_NULL_HANDLE) {
        testloop::ForgetSwapchain(gSwapchain);
    }
    gSwapchain = XR_NULL_HANDLE;
    gImages.clear();
    gSwapchainFailed = false;
    gLevel = -1;
}

bool EnsureViewSpace() {
    if (gViewSpace != XR_NULL_HANDLE) {
        return true;
    }
    if (gViewSpaceFailed) {
        return false;
    }
    XrReferenceSpaceCreateInfo info{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    info.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    info.poseInReferenceSpace.orientation.w = 1.0f;
    XrResult const r = xrCreateReferenceSpace(gSession, &info, &gViewSpace);
    if (XR_FAILED(r)) {
        gViewSpace = XR_NULL_HANDLE;
        gViewSpaceFailed = true;
        log::Error("vignette: the VIEW space couldn't be made (XrResult {}): no comfort vignette this session", static_cast<int>(r));
        return false;
    }
    return true;
}

bool EnsureSwapchain(ID3D11Device* device) {
    if (gSwapchain != XR_NULL_HANDLE && gTexture != nullptr) {
        return true;
    }
    if (gSwapchainFailed || device == nullptr) {
        return false;
    }
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;  // as the reticle's (CopyResource)
    info.format = kSwapchainFormat;
    info.sampleCount = 1;
    info.width = kSize;
    info.height = kSize;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    XrResult r = xrCreateSwapchain(gSession, &info, &gSwapchain);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) {
        r = xrEnumerateSwapchainImages(gSwapchain, 0, &count, nullptr);
    }
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(gSwapchain, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data()));
    }
    D3D11_TEXTURE2D_DESC td{};
    td.Width = kSize;
    td.Height = kSize;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (XR_FAILED(r) || count == 0 || FAILED(device->CreateTexture2D(&td, nullptr, gTexture.ReleaseAndGetAddressOf()))) {
        log::Error("vignette: its swapchain or texture ({}x{}) couldn't be made (XrResult {}): no comfort vignette this session", kSize,
            kSize, static_cast<int>(r));
        if (gSwapchain != XR_NULL_HANDLE) {
            xrDestroySwapchain(gSwapchain);
        }
        gSwapchain = XR_NULL_HANDLE;
        gTexture.Reset();
        gSwapchainFailed = true;
        return false;
    }
    gImages.clear();
    for (auto const& image : images) {
        gImages.push_back(image.texture);
    }
    testloop::NameSwapchain(gSwapchain, "vignette");
    gLevel = -1;
    log::Info("vignette: swapchain {}x{}, {} images", kSize, kSize, count);
    return true;
}

// The swapchain's picture at `level` of kLevels.
bool Fill(ID3D11DeviceContext* context, int level) {
    cf::VignettePixels(gMask, static_cast<float>(level) / kLevels, gPixels);
    context->UpdateSubresource(gTexture.Get(), 0, nullptr, gPixels.data(), kSize * 4, 0);
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    std::uint32_t index = 0;
    if (XR_FAILED(xrAcquireSwapchainImage(gSwapchain, &acquire, &index))) {
        return false;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    bool const ok = XR_SUCCEEDED(xrWaitSwapchainImage(gSwapchain, &wait)) && index < gImages.size();
    if (ok) {
        context->CopyResource(gImages[index], gTexture.Get());
    }
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage(gSwapchain, &release);
    if (ok) {
        // Snapshots draw it from these pixels (the runtime keeps showing the last image released).
        testloop::SetStaticImage(gSwapchain, kSize, kSize, gPixels);
    }
    return ok;
}

void LogStats(double now, bool force) {
    if (gStats.since == 0.0 || (!force && now - gStats.since < kStatsEverySeconds)) {
        return;
    }
    if (gStats.frames > 0) {
        log::Info("vignette: shown in {} frames of the last {:.1f} s, peak {:.0f}%, {} texture refills ({:.3f} ms CPU each on the render "
                  "thread), quad {:.2f} x {:.2f} m at {:.1f} m",
            gStats.frames, now - gStats.since, gStats.peak * 100.0f, gStats.updates,
            gStats.updates > 0 ? gStats.updateMs / gStats.updates : 0.0, gQuad.size.width, gQuad.size.height, kDistance);
    }
    gStats = Stats{};
    gStats.since = force ? 0.0 : now;
}

} // namespace

void SetTarget(float amount) {
    gTarget.store(std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f, std::memory_order_relaxed);
    gTargetMs.store(GetTickCount64(), std::memory_order_relaxed);
}

void Forget() {
    gTarget.store(0.0f, std::memory_order_relaxed);
    gTargetMs.store(0, std::memory_order_relaxed);
}

XrCompositionLayerQuad const* Layer(Frame const& frame) {
    // The usual case costs this much: nothing wanted, nothing showing.
    std::uint64_t const at = gTargetMs.load(std::memory_order_relaxed);
    float target = 0.0f;
    if (at != 0) {
        std::uint64_t const now = GetTickCount64();
        target = now - at <= kStaleMs ? gTarget.load(std::memory_order_relaxed) : 0.0f;
    }
    bool const drawable = frame.worldFrame && !frame.menuOpen && frame.fovValid && frame.session != XR_NULL_HANDLE &&
        frame.context != nullptr;
    if (!drawable) {
        target = 0.0f;
        gFade.value = 0.0f;  // a menu or a flat screen: gone at once, and back from nothing afterwards
    }
    if (target <= 0.0f && gFade.value <= 0.0f) {
        gLastCounter.QuadPart = 0;
        return nullptr;
    }

    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    double const now = Seconds(counter);
    float const dt = gLastCounter.QuadPart != 0 ? static_cast<float>(now - Seconds(gLastCounter)) : 0.0f;
    gLastCounter = counter;
    float const value = gFade.Step(target, dt);
    if (gStats.since == 0.0) {
        gStats.since = now;
    }
    LogStats(now, false);
    if (value <= 0.0f) {
        return nullptr;
    }

    if (frame.session != gSession) {
        ForgetSession();
        gSession = frame.session;
    }
    if (!EnsureViewSpace() || !EnsureSwapchain(frame.device)) {
        return nullptr;
    }
    cf::HalfExtents const extents = cf::CoverFov(frame.fov);
    bool const reshaped = gMask.empty() || std::abs(extents.width - gExtents.width) > 0.01f * gExtents.width ||
        std::abs(extents.height - gExtents.height) > 0.01f * gExtents.height;
    if (reshaped) {
        gExtents = extents;
        gMask = cf::VignetteMask(kSize, kSize, gExtents.width, gExtents.height);
        gLevel = -1;
    }
    int const level = std::clamp(static_cast<int>(std::lround(value * kLevels)), 1, kLevels);
    if (level != gLevel) {
        LARGE_INTEGER before{};
        QueryPerformanceCounter(&before);
        if (!Fill(frame.context, level)) {
            return gLevel > 0 ? &gQuad : nullptr;  // keep showing what the swapchain holds
        }
        LARGE_INTEGER after{};
        QueryPerformanceCounter(&after);
        gLevel = level;
        ++gStats.updates;
        gStats.updateMs += (Seconds(after) - Seconds(before)) * 1000.0;
    }
    gQuad = {XR_TYPE_COMPOSITION_LAYER_QUAD};
    gQuad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;  // premultiplied black
    gQuad.space = gViewSpace;
    gQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    gQuad.subImage.swapchain = gSwapchain;
    gQuad.subImage.imageRect = {{0, 0}, {kSize, kSize}};
    gQuad.pose.orientation.w = 1.0f;
    gQuad.pose.position = {0.0f, 0.0f, -kDistance};
    gQuad.size = {2.0f * gExtents.width * kDistance, 2.0f * gExtents.height * kDistance};
    ++gStats.frames;
    gStats.peak = std::max(gStats.peak, value);
    if (!gLoggedFirst) {
        gLoggedFirst = true;
        log::Info("vignette: first shown ({:.0f}% wanted): a head-locked quad {:.2f} x {:.2f} m at {:.1f} m (the eyes' fields of view "
                  "{:.0f}/{:.0f} and {:.0f}/{:.0f} deg left/right), clear inside {:.0f} deg, dark past {:.0f} deg",
            target * 100.0f, gQuad.size.width, gQuad.size.height, kDistance, frame.fov[0][0] * 57.2958f, frame.fov[0][1] * 57.2958f,
            frame.fov[1][0] * 57.2958f, frame.fov[1][1] * 57.2958f, cf::kVignetteInnerDegrees, cf::kVignetteOuterDegrees);
    }
    return &gQuad;
}

} // namespace mevr::host::vignette
