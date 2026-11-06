// SPDX-License-Identifier: MIT
// The cinema screen's floor and horizon in the headset (plugin/CinemaBackdrop.hpp).

#include "plugin/CinemaBackdrop.hpp"

#include "core/Log.hpp"
#include "core/vr/Floor.hpp"
#include "halcyon/le1/mevr_bridge.h"
#include "plugin/CineScreen.hpp"
#include "plugin/CinemaBackdropDraw.hpp"
#include "plugin/Halcyon.hpp"
#include "plugin/Swapchains.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/menu/StateGuard.hpp"

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
#include <cmath>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>
#include <vector>

namespace mevr::host::cinema_backdrop {
namespace {

using Microsoft::WRL::ComPtr;
namespace draw = cinema_backdrop_draw;

constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
constexpr float kScale = 0.5f;            // of the eye's size: a dim, soft background
constexpr std::uint64_t kNewSequenceMs = 500;  // not drawn for longer: the next frame starts a sequence (fade in, floor latched)
constexpr float kFadeS = 0.6f;
constexpr float kFallbackEyeHeightM = 1.6f;

struct Chain {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> images;
    std::vector<ComPtr<ID3D11RenderTargetView>> targets;
    int width = 0;   // both eyes side by side: 2 x the eye's width
    int height = 0;
    bool failed = false;
    std::uint64_t failedMs = 0;
    bool Refused() const { return failed && !swapchains::RetryDue(failedMs); }
    void Destroy() {
        swapchains::Destroy(handle);
        handle = XR_NULL_HANDLE;
        images.clear();
        targets.clear();
        width = height = 0;
    }
    void Forget() {
        swapchains::Forget(handle);
        handle = XR_NULL_HANDLE;
        images.clear();
        targets.clear();
        width = height = 0;
        failed = false;
    }
};

XrSession gSession = XR_NULL_HANDLE;
Chain gChain;
draw::Renderer gRenderer;
bool gRendererTried = false;
XrCompositionLayerProjection gLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
XrCompositionLayerProjectionView gViews[2] = {{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW}};
std::uint64_t gLastDrawMs = 0;
std::uint64_t gSequenceStartMs = 0;
float gLatchedFloorY = 0.0f;
bool gFloorFromStage = false;
unsigned gSequences = 0;
unsigned gFrames = 0;
double gDrawMsSum = 0.0;
std::uint64_t gLastCostLogMs = 0;
std::mutex gStatusMutex;
std::string gStatus = "{\"shown\": false}";

void SetStatus(std::string text) {
    std::lock_guard lock(gStatusMutex);
    gStatus = std::move(text);
}

bool EnsureChain(ID3D11Device* device, XrSession session, int width, int height) {
    if (session != gSession) {
        gChain.Forget();
        gSession = session;
    }
    if (gChain.Refused()) return false;
    if (gChain.handle != XR_NULL_HANDLE && gChain.width == width && gChain.height == height) return true;
    gChain.Destroy();
    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    info.format = kFormat;
    info.sampleCount = 1;
    info.width = static_cast<std::uint32_t>(width);
    info.height = static_cast<std::uint32_t>(height);
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (XR_FAILED(swapchains::Create(session, info, "backdrop", [] { gChain.Destroy(); }, gChain.handle, gChain.images))) {
        gChain.failed = true;
        gChain.failedMs = GetTickCount64();
        return false;
    }
    gChain.failed = false;
    gChain.width = width;
    gChain.height = height;
    gChain.targets.resize(gChain.images.size());
    for (size_t i = 0; i < gChain.images.size(); ++i) {
        D3D11_RENDER_TARGET_VIEW_DESC rd{};
        rd.Format = kFormat;
        rd.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateRenderTargetView(gChain.images[i], &rd, gChain.targets[i].GetAddressOf()))) {
            log::Error("cinema backdrop: no render target on the swapchain's image {}; no floor drawn", i);
            gChain.Destroy();
            gChain.failed = true;
            gChain.failedMs = GetTickCount64();
            return false;
        }
    }
    testloop::NameSwapchain(gChain.handle, "backdrop");
    log::Info("cinema backdrop: the swapchain {}x{} (both eyes side by side), {} images", width, height, gChain.images.size());
    return true;
}

XrQuaternionf Quat(float const q[4]) {
    return {q[0], q[1], q[2], q[3]};
}

float YawOf(XrQuaternionf const& q) {
    // The heading of the quad's back (+Z) on the floor: the screen faces the eyes, so its yaw about +Y.
    float const x = 2.0f * (q.x * q.z + q.w * q.y);
    float const z = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    return std::atan2(x, z);
}

} // namespace

std::string StatusJson() {
    std::lock_guard lock(gStatusMutex);
    return gStatus;
}

} // namespace mevr::host::cinema_backdrop

namespace MELEVR::MevrBridge {

const void* CinemaBackdropLayer(const BackdropFrame& frame) noexcept {
    namespace cb = mevr::host::cinema_backdrop;
    namespace draw = mevr::host::cinema_backdrop_draw;
    mevr::host::cine_screen::Backdrop const wanted = mevr::host::cine_screen::BackdropWanted();
    XrSession const session = static_cast<XrSession>(frame.session);
    XrSpace const appSpace = static_cast<XrSpace>(frame.appSpace);
    if (!wanted.on || session == XR_NULL_HANDLE || appSpace == XR_NULL_HANDLE || frame.context == nullptr || !frame.headValid) {
        return nullptr;
    }
    Microsoft::WRL::ComPtr<ID3D11Device> device;
    frame.context->GetDevice(device.GetAddressOf());
    if (!device) return nullptr;
    if (!cb::gRendererTried) {
        cb::gRendererTried = true;
        std::string error;
        if (!cb::gRenderer.Init(device.Get(), &error)) {
            mevr::log::Error("cinema backdrop: the renderer failed ({}); the screen stays in the dark", error);
        } else {
            mevr::log::Info("cinema backdrop: renderer ready (the procedural floor and sky until the game's textures are found)");
        }
    }
    if (!cb::gRenderer.Ready()) return nullptr;
    int const eyeW = std::clamp(frame.eyeWidth > 0 ? static_cast<int>(static_cast<float>(frame.eyeWidth) * cb::kScale) : 1200, 512, 2048);
    int const eyeH = std::clamp(frame.eyeHeight > 0 ? static_cast<int>(static_cast<float>(frame.eyeHeight) * cb::kScale) : 1200, 512, 2048);
    if (!cb::EnsureChain(device.Get(), session, eyeW * 2, eyeH)) return nullptr;

    // A new sequence: fade in, and the floor where it is now (STAGE's, else 1.6 m under the eyes, kept for the sequence).
    std::uint64_t const now = GetTickCount64();
    bool const fresh = cb::gLastDrawMs == 0 || now - cb::gLastDrawMs > cb::kNewSequenceMs;
    cb::gLastDrawMs = now;
    float const headY = 0.5f * (frame.eyePosition[0][1] + frame.eyePosition[1][1]);
    mevr::vr::floor::State floor;
    mevr::host::halcyon::Floor(&floor);
    bool const stage = floor.valid && floor.eyeHeight > 0.5f && floor.eyeHeight < 2.5f;
    if (fresh) {
        cb::gSequenceStartMs = now;
        cb::gLatchedFloorY = headY - (stage ? floor.eyeHeight : cb::kFallbackEyeHeightM);
        cb::gFloorFromStage = stage;
        ++cb::gSequences;
        mevr::log::Info("cinema backdrop: a sequence on the screen: the floor {:.2f} m under the eyes ({}), brightness {:.2f}",
            headY - cb::gLatchedFloorY, stage ? "the headset's floor" : "assumed: no floor from the runtime", wanted.brightness);
    }
    float const floorY = stage ? headY - floor.eyeHeight : cb::gLatchedFloorY;
    float const sinceS = static_cast<float>(now - cb::gSequenceStartMs) / 1000.0f;

    draw::Scene scene;
    scene.floorY = floorY;
    scene.screenFoot = {frame.screenPosition[0], floorY, frame.screenPosition[2]};
    scene.screenYaw = cb::YawOf(cb::Quat(frame.screenOrientation));
    scene.screenWidthM = frame.screenWidthM > 0.1f ? frame.screenWidthM : 2.9f;
    scene.brightness = wanted.brightness;
    scene.fade = std::clamp(sinceS / cb::kFadeS, 0.0f, 1.0f);
    scene.timeS = sinceS;

    LARGE_INTEGER t0{}, t1{}, freq{};
    QueryPerformanceCounter(&t0);
    std::uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (XR_FAILED(xrAcquireSwapchainImage(cb::gChain.handle, &acquire, &index))) return nullptr;
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    if (XR_FAILED(xrWaitSwapchainImage(cb::gChain.handle, &wait)) || index >= cb::gChain.targets.size()) {
        xrReleaseSwapchainImage(cb::gChain.handle, &release);
        return nullptr;
    }
    {
        mevr::menu::StateGuard guard(frame.context);
        for (int e = 0; e < 2; ++e) {
            draw::Eye eye;
            eye.pose.orientation = cb::Quat(frame.eyeOrientation[e]);
            eye.pose.position = {frame.eyePosition[e][0], frame.eyePosition[e][1], frame.eyePosition[e][2]};
            eye.fov = {frame.eyeFov[e][0], frame.eyeFov[e][1], frame.eyeFov[e][2], frame.eyeFov[e][3]};
            cb::gRenderer.Draw(frame.context, cb::gChain.targets[index].Get(), e * eyeW, 0, eyeW, eyeH, eye, scene);   // its half
            cb::gViews[e].pose = eye.pose;
            cb::gViews[e].fov = eye.fov;
            cb::gViews[e].subImage.swapchain = cb::gChain.handle;
            cb::gViews[e].subImage.imageRect = {{e * eyeW, 0}, {eyeW, eyeH}};
            cb::gViews[e].subImage.imageArrayIndex = 0;
        }
    }
    mevr::host::testloop::OnImageWritten(cb::gChain.handle, cb::gChain.images[index]);
    xrReleaseSwapchainImage(cb::gChain.handle, &release);
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    cb::gDrawMsSum += static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
    ++cb::gFrames;
    if (now - cb::gLastCostLogMs > 10000) {
        if (cb::gLastCostLogMs != 0 && cb::gFrames > 0) {
            mevr::log::Info("cinema backdrop: last 10 s: {} frames drawn, {:.3f} ms a frame on the CPU (both eyes)", cb::gFrames,
                cb::gDrawMsSum / cb::gFrames);
        }
        cb::gLastCostLogMs = now;
        cb::gFrames = 0;
        cb::gDrawMsSum = 0.0;
    }
    cb::SetStatus(std::format("{{\"shown\": true, \"sequences\": {}, \"floorUnderEyesM\": {:.3f}, \"floorFrom\": \"{}\", "
                              "\"brightness\": {:.2f}, \"fade\": {:.2f}, \"target\": [{}, {}]}}",
        cb::gSequences, headY - floorY, stage ? "stage" : "assumed", wanted.brightness, scene.fade, eyeW * 2, eyeH));
    cb::gLayer.layerFlags = 0;   // opaque: the bottom layer
    cb::gLayer.space = appSpace;
    cb::gLayer.viewCount = 2;
    cb::gLayer.views = cb::gViews;
    return &cb::gLayer;
}

} // namespace MELEVR::MevrBridge
