#include "core/vr/Output.hpp"

#include "core/Log.hpp"
#include "core/d3d11/Blit.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/ViewSync.hpp"
#include "core/xr/Input.hpp"
#include "core/xr/Session.hpp"

#include <dxgi.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>

namespace mevr::vr::output {
namespace {

using Microsoft::WRL::ComPtr;

// Screen mode panel placement in LOCAL space (origin = head position at the last recenter).
constexpr float kScreenDistanceMeters = 2.0f;
constexpr float kScreenWidthMeters = 2.4f;  // about 62 degrees wide at 2 m

std::atomic<bool> gEnabled{true};
std::atomic<Mode> gMode{Mode::HeadTracked};
std::atomic<bool> gSessionRunning{false};

ComPtr<ID3D11Device> gDevice;
HWND gWindow = nullptr;
d3d11::Blit gBlit;
xr::Session gSession;
xr::ColorSwapchain gImage;
xr::Input gInput;
std::uint32_t gInputFailedGeneration = UINT32_MAX;  // don't retry a failed setup every frame
bool gXrUp = false;

// Alternate-eye stereo: each eye has its own swapchain and keeps its latest image (rendered every
// other frame) with the pose it was rendered from, so each frame submits both eyes.
struct EyeImage {
    xr::ColorSwapchain swapchain;
    ComPtr<ID3D11Texture2D> held;
    ComPtr<ID3D11RenderTargetView> heldRtv;
    std::optional<view_sync::RenderedView> view;
};
std::array<EyeImage, 2> gEyes;
bool gFailed = false;
XrSessionState gLastState = XR_SESSION_STATE_UNKNOWN;
Mode gLastMode = Mode::HeadTracked;

// The OpenXR frame is begun right after the previous one ends, so the head prediction for the
// frame the game is about to compute is available as early as possible.
bool gFrameBegun = false;
XrFrameState gFrame{XR_TYPE_FRAME_STATE};
std::optional<view_sync::RenderedView> gLastRendered;
float gPublishedFov = 0.0f;  // last headset FOV logged

// Pacing counters (render thread writes, game thread reads and resets in LogStats).
std::atomic<unsigned> gDepthCounts[4]{};  // queue depth at present: 0, 1, 2, 3+
std::atomic<unsigned> gPresents{0};
std::atomic<std::int64_t> gIntervalMinUs{INT64_MAX};
std::atomic<std::int64_t> gIntervalMaxUs{0};
std::atomic<DWORD> gPresentThread{0};
LARGE_INTEGER gLastPresent{};

void RecordPresentTiming() {
    LARGE_INTEGER now{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&now);
    QueryPerformanceFrequency(&frequency);
    if (gLastPresent.QuadPart != 0) {
        std::int64_t const us = (now.QuadPart - gLastPresent.QuadPart) * 1'000'000 / frequency.QuadPart;
        if (us < gIntervalMinUs) gIntervalMinUs = us;
        if (us > gIntervalMaxUs) gIntervalMaxUs = us;
    }
    gLastPresent = now;
    gPresents.fetch_add(1, std::memory_order_relaxed);
    gPresentThread = GetCurrentThreadId();
}

// Every Present ends a game frame, whatever path OnPresent takes; release the lockstep wait.
struct PresentScope {
    ~PresentScope() { view_sync::NotifyPresented(); }
};

// A swapchain from an older session was destroyed along with that session.
void ReleaseSwapchain(xr::ColorSwapchain& swapchain) {
    if (swapchain.Handle() == XR_NULL_HANDLE) {
        return;
    }
    if (gSession.Handle() != XR_NULL_HANDLE && swapchain.Generation() == gSession.Generation()) {
        swapchain.Destroy();
    } else {
        swapchain.Forget();
    }
}

void ReleaseEyes() {
    for (EyeImage& eye : gEyes) {
        ReleaseSwapchain(eye.swapchain);
        eye.heldRtv.Reset();
        eye.held.Reset();
        eye.view.reset();
    }
}

void ReleaseImage() {
    ReleaseSwapchain(gImage);
    ReleaseEyes();
}

void ReleaseInput() {
    gInput.Release(gSession);
    gInputFailedGeneration = UINT32_MAX;
    controllers::Reset();
}

void ResetFrameState() {
    gFrameBegun = false;
    gSessionRunning = false;
    gLastRendered.reset();
    gPublishedFov = 0.0f;
    for (EyeImage& eye : gEyes) {
        eye.view.reset();
    }
    view_sync::Reset();
}

// Closes everything down to the OpenXR instance, so the next start picks up whichever runtime
// is active then (e.g. after switching from the Oculus runtime to SteamVR).
void TearDownXr() {
    if (!gSession.HasInstance()) {
        return;
    }
    ReleaseImage();
    ReleaseInput();
    gSession.Shutdown();
    ResetFrameState();
    gLastState = XR_SESSION_STATE_UNKNOWN;
    log::Info("vr output: XR {} closed", gXrUp ? "session" : "instance (no session yet)");
    gXrUp = false;
}

// The headset session gaining focus doesn't give the game window Windows focus, and UE3 ignores
// keyboard, mouse and gamepad without it. Windows only lets the foreground thread hand out
// focus, so briefly share its input queue.
void BringToForeground(HWND window) {
    HWND const foreground = GetForegroundWindow();
    if (window == nullptr || foreground == window) {
        return;
    }
    DWORD const foregroundThread = GetWindowThreadProcessId(foreground, nullptr);
    DWORD const thisThread = GetCurrentThreadId();
    bool const attached = foregroundThread != 0 && foregroundThread != thisThread &&
        AttachThreadInput(thisThread, foregroundThread, TRUE);
    BOOL const ok = SetForegroundWindow(window);
    if (attached) {
        AttachThreadInput(thisThread, foregroundThread, FALSE);
    }
    log::Info("vr output: game window to foreground: {}", ok ? "ok" : "refused by Windows");
}

// Horizontal FOV (degrees) a single symmetric image needs so both eyes' views are covered:
// the outermost left and right edges across the two eyes.
float CoveringFovDegrees(XrView const (&eyes)[2]) {
    float const left = -std::min(eyes[0].fov.angleLeft, eyes[1].fov.angleLeft);
    float const right = std::max(eyes[0].fov.angleRight, eyes[1].fov.angleRight);
    float const degrees = 2.0f * std::max(left, right) * 180.0f / std::numbers::pi_v<float>;
    return std::clamp(degrees, 60.0f, 150.0f);  // UE3 projections degrade badly near 180
}

void BeginNextFrame() {
    gFrameBegun = gSession.BeginFrame(gFrame);
    if (!gFrameBegun) {
        return;
    }
    // With the lockstep (game thread waits for each Present), the frame the game computes next
    // is the one shown at this frame's display time.
    XrTime const target = gFrame.predictedDisplayTime;
    XrPosef head{};
    if (gSession.LocateHead(target, head)) {
        view_sync::PublishHead(head);
    }
    XrView eyes[2];
    if (gSession.LocateEyes(target, eyes)) {
        float const fov = CoveringFovDegrees(eyes);
        view_sync::PublishHeadsetFov(fov);
        float const dx = eyes[1].pose.position.x - eyes[0].pose.position.x;
        float const dy = eyes[1].pose.position.y - eyes[0].pose.position.y;
        float const dz = eyes[1].pose.position.z - eyes[0].pose.position.z;
        float const halfIpd = 0.5f * std::sqrt(dx * dx + dy * dy + dz * dz);
        // Runtimes that report no eye separation (or nonsense) get a typical 64 mm IPD.
        view_sync::PublishHalfIpd(halfIpd > 0.02f && halfIpd < 0.045f ? halfIpd : 0.032f);
        if (std::abs(fov - gPublishedFov) > 0.5f) {
            gPublishedFov = fov;
            log::Info("vr output: headset FOV L {:.1f}/{:.1f} R {:.1f}/{:.1f} deg (left/right edges), rendering {:.1f} deg wide",
                eyes[0].fov.angleLeft * 57.2958f, eyes[0].fov.angleRight * 57.2958f,
                eyes[1].fov.angleLeft * 57.2958f, eyes[1].fov.angleRight * 57.2958f, fov);
        }
    }
}

XrCompositionLayerQuad ScreenLayer() {
    XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    layer.space = gSession.LocalSpace();
    layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    layer.subImage.swapchain = gImage.Handle();
    layer.subImage.imageRect = {{0, 0},
        {static_cast<std::int32_t>(gImage.Width()), static_cast<std::int32_t>(gImage.Height())}};
    layer.pose.orientation.w = 1.0f;
    layer.pose.position = {0.0f, 0.0f, -kScreenDistanceMeters};
    layer.size = {kScreenWidthMeters, kScreenWidthMeters * static_cast<float>(gImage.Height()) / gImage.Width()};
    return layer;
}

// One projection view: `image` posed where the game camera was when it rendered it, with the
// game's own (symmetric) field of view. The runtime reprojects it to the eye's current pose.
XrCompositionLayerProjectionView ProjectionView(view_sync::RenderedView const& view, xr::ColorSwapchain const& image) {
    float const halfX = view.fovXDegrees * std::numbers::pi_v<float> / 360.0f;
    float const halfY = std::atan(std::tan(halfX) * static_cast<float>(image.Height()) / image.Width());
    XrCompositionLayerProjectionView out{XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    out.pose = view.pose;
    out.fov = {-halfX, halfX, halfY, -halfY};
    out.subImage.swapchain = image.Handle();
    out.subImage.imageRect = {{0, 0},
        {static_cast<std::int32_t>(image.Width()), static_cast<std::int32_t>(image.Height())}};
    return out;
}

// Mono: both eyes get the same image.
void FillProjectionViews(view_sync::RenderedView const& view, std::array<XrCompositionLayerProjectionView, 2>& out) {
    out[0] = out[1] = ProjectionView(view, gImage);
}

bool EnsureEye(EyeImage& eye, UINT width, UINT height) {
    if (eye.swapchain.Matches(width, height, gSession.Generation()) && eye.held) {
        return true;
    }
    ReleaseSwapchain(eye.swapchain);
    eye.heldRtv.Reset();
    eye.held.Reset();
    eye.view.reset();
    if (!eye.swapchain.Create(gSession, gDevice.Get(), width, height)) {
        return false;
    }
    // Same format as the swapchain, so it can be copied in with CopyResource.
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = eye.swapchain.Format();
    desc.SampleDesc = {1, 0};
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(gDevice->CreateTexture2D(&desc, nullptr, &eye.held)) ||
        FAILED(gDevice->CreateRenderTargetView(eye.held.Get(), nullptr, &eye.heldRtv))) {
        log::Error("vr output: eye image {}x{} failed", width, height);
        eye.held.Reset();
        return false;
    }
    return true;
}

// Alternate-eye stereo: the backbuffer holds the eye `view.eye` only. Keep it with its pose, then
// give each eye's swapchain that eye's latest image (or this one, until the eye has its own).
bool SubmitAlternateEye(ID3D11DeviceContext* context, ID3D11Texture2D* backbuffer, UINT width, UINT height,
    view_sync::RenderedView const& view, std::array<XrCompositionLayerProjectionView, 2>& out) {
    for (EyeImage& eye : gEyes) {
        if (!EnsureEye(eye, width, height)) {
            return false;
        }
    }
    EyeImage& current = gEyes[static_cast<std::size_t>(view.eye)];
    gBlit.Draw(context, backbuffer, current.heldRtv.Get(), width, height);
    current.view = view;
    for (std::size_t e = 0; e < gEyes.size(); ++e) {
        EyeImage& eye = gEyes[e];
        EyeImage const& source = eye.view ? eye : current;
        if (eye.swapchain.Acquire() == nullptr) {
            return false;
        }
        if (ID3D11Texture2D* const texture = eye.swapchain.AcquiredTexture()) {
            context->CopyResource(texture, source.held.Get());
        }
        eye.swapchain.Release();
        out[e] = ProjectionView(*source.view, eye.swapchain);
    }
    return true;
}

} // namespace

void SetEnabled(bool enabled) {
    gEnabled = enabled;
}

bool IsEnabled() {
    return gEnabled;
}

void SetMode(Mode mode) {
    gMode = mode;
}

Mode GetMode() {
    return gMode;
}

bool WantsHeadTracking() {
    return gEnabled && gSessionRunning && gMode == Mode::HeadTracked;
}

void LogStats() {
    unsigned const presents = gPresents.exchange(0);
    unsigned d0 = gDepthCounts[0].exchange(0), d1 = gDepthCounts[1].exchange(0);
    unsigned d2 = gDepthCounts[2].exchange(0), d3 = gDepthCounts[3].exchange(0);
    std::int64_t const minUs = gIntervalMinUs.exchange(INT64_MAX);
    std::int64_t const maxUs = gIntervalMaxUs.exchange(0);
    if (presents == 0) {
        return;
    }
    log::Info("    presents {} (render thread {}), interval {:.2f}-{:.2f} ms, queue depth 0:{} 1:{} 2:{} 3+:{}",
        presents, static_cast<unsigned>(gPresentThread), minUs / 1000.0, maxUs / 1000.0, d0, d1, d2, d3);
}

void OnPresent(IDXGISwapChain* swapChain) {
    PresentScope const presentScope;
    RecordPresentTiming();
    if (!gEnabled) {
        TearDownXr();
        return;
    }
    if (gFailed) {
        return;
    }
    ComPtr<ID3D11Device> device;
    if (FAILED(swapChain->GetDevice(IID_PPV_ARGS(&device)))) {
        return;  // not a D3D11 swap chain
    }
    if (!gDevice) {
        if (!gBlit.Initialize(device.Get())) {
            gFailed = true;
            log::Error("vr output disabled: blit setup failed");
            return;
        }
        gDevice = device;
        DXGI_SWAP_CHAIN_DESC scDesc{};
        if (SUCCEEDED(swapChain->GetDesc(&scDesc))) {
            gWindow = scDesc.OutputWindow;
        }
    } else if (gDevice != device) {
        return;  // a second device's swap chain; only use the game's main one
    }

    if (!gSession.EnsureCreated(gDevice.Get())) {
        return;
    }
    gXrUp = true;
    gSession.PollEvents();
    if (gSession.UserExited()) {
        gEnabled = false;
        TearDownXr();
        log::Info("vr output: quit from the headset menu; press ] in game to turn VR back on");
        return;
    }
    if (gSession.State() != gLastState) {
        gLastState = gSession.State();
        if (gLastState == XR_SESSION_STATE_FOCUSED) {
            BringToForeground(gWindow);
        }
    }
    if (!gSession.IsRunning()) {
        ResetFrameState();  // xrEndSession discards any begun frame
        return;
    }
    gSessionRunning = true;

    ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    backbuffer->GetDesc(&desc);
    if (!gImage.Matches(desc.Width, desc.Height, gSession.Generation())) {
        ReleaseImage();  // resized, or the session was recreated
        if (!gImage.Create(gSession, gDevice.Get(), desc.Width, desc.Height)) {
            gFailed = true;
            log::Error("vr output disabled: swapchain creation failed");
            return;
        }
    }

    Mode const mode = gMode;
    if (mode != gLastMode) {
        gLastMode = mode;
        gLastRendered.reset();
        view_sync::Reset();
        log::Info("vr output: mode {}", mode == Mode::HeadTracked ? "head-tracked" : "screen");
    }

    if (!gInput.Matches(gSession) && gInputFailedGeneration != gSession.Generation()) {
        gInput.Release(gSession);
        if (!gInput.Create(gSession)) {
            gInputFailedGeneration = gSession.Generation();
        }
    }

    if (!gFrameBegun) {
        BeginNextFrame();
        if (!gFrameBegun) {
            return;
        }
    }

    controllers::State input;
    gInput.Sync(gSession, gFrame.predictedDisplayTime, input);
    controllers::Publish(input);

    // The pose this image was rendered with (head-tracked mode).
    if (mode == Mode::HeadTracked) {
        std::size_t depth = 0;
        if (auto rendered = view_sync::TakeForPresent(depth)) {
            gLastRendered = rendered;
        }
        gDepthCounts[std::min<std::size_t>(depth, 3)].fetch_add(1, std::memory_order_relaxed);
    }

    XrCompositionLayerQuad quad{};
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView, 2> views{};
    std::array<XrCompositionLayerBaseHeader const*, 1> layers{};
    std::size_t layerCount = 0;
    bool const haveView = mode == Mode::Screen || gLastRendered.has_value();
    bool const alternateEye = mode == Mode::HeadTracked && gLastRendered && gLastRendered->eye >= 0;
    if (!alternateEye) {
        for (EyeImage& eye : gEyes) {
            eye.view.reset();  // don't resume stereo later with stale images
        }
    }
    ComPtr<ID3D11DeviceContext> immediate;
    gDevice->GetImmediateContext(&immediate);
    if (gFrame.shouldRender && alternateEye) {
        if (SubmitAlternateEye(immediate.Get(), backbuffer.Get(), desc.Width, desc.Height, *gLastRendered, views)) {
            projection.space = gSession.LocalSpace();
            projection.viewCount = static_cast<std::uint32_t>(views.size());
            projection.views = views.data();
            layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader const*>(&projection);
        }
    } else if (gFrame.shouldRender && haveView) {
        if (ID3D11RenderTargetView* const target = gImage.Acquire()) {
            gBlit.Draw(immediate.Get(), backbuffer.Get(), target, gImage.Width(), gImage.Height());
            gImage.Release();

            if (mode == Mode::Screen) {
                quad = ScreenLayer();
                layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader const*>(&quad);
            } else {
                FillProjectionViews(*gLastRendered, views);
                projection.space = gSession.LocalSpace();
                projection.viewCount = static_cast<std::uint32_t>(views.size());
                projection.views = views.data();
                layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader const*>(&projection);
            }
        }
    }
    gSession.EndFrame(gFrame.predictedDisplayTime, {layers.data(), layerCount});
    gFrameBegun = false;

    BeginNextFrame();
}

void Shutdown() {
    ReleaseImage();
    ReleaseInput();
    gSession.Shutdown();
    ResetFrameState();
    gXrUp = false;
    gLastState = XR_SESSION_STATE_UNKNOWN;
    gBlit.Shutdown();
    gDevice.Reset();
    gWindow = nullptr;
    gFailed = false;
}

} // namespace mevr::vr::output
