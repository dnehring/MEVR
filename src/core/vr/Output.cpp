#include "core/vr/Output.hpp"

#include "core/Log.hpp"
#include "core/d3d11/Blit.hpp"
#include "core/vr/ViewSync.hpp"
#include "core/xr/Session.hpp"

#include <dxgi.h>

#include <array>
#include <atomic>
#include <cmath>
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
bool gXrUp = false;
bool gFailed = false;
XrSessionState gLastState = XR_SESSION_STATE_UNKNOWN;
Mode gLastMode = Mode::HeadTracked;

// The OpenXR frame is begun right after the previous one ends, so the head prediction for the
// frame the game is about to compute is available as early as possible.
bool gFrameBegun = false;
XrFrameState gFrame{XR_TYPE_FRAME_STATE};
std::optional<view_sync::RenderedView> gLastRendered;

// A swapchain from an older session was destroyed along with that session.
void ReleaseImage() {
    if (gImage.Handle() == XR_NULL_HANDLE) {
        return;
    }
    if (gSession.Handle() != XR_NULL_HANDLE && gImage.Generation() == gSession.Generation()) {
        gImage.Destroy();
    } else {
        gImage.Forget();
    }
}

void ResetFrameState() {
    gFrameBegun = false;
    gSessionRunning = false;
    gLastRendered.reset();
    view_sync::Reset();
}

// Closes everything down to the OpenXR instance, so the next start picks up whichever runtime
// is active then (e.g. after switching from the Oculus runtime to SteamVR).
void TearDownXr() {
    if (!gSession.HasInstance()) {
        return;
    }
    ReleaseImage();
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

void BeginNextFrame() {
    gFrameBegun = gSession.BeginFrame(gFrame);
    if (!gFrameBegun) {
        return;
    }
    // The game thread is computing the frame after the one just begun (UE3 renders a frame
    // behind the game thread), so predict one display period further out.
    XrPosef head{};
    if (gSession.LocateHead(gFrame.predictedDisplayTime + gFrame.predictedDisplayPeriod, head)) {
        view_sync::PublishHead(head);
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

// Mono: both eyes get the same image, posed where the game camera was when it rendered it,
// with the game's own field of view. The runtime reprojects it to the eyes' current poses.
void FillProjectionViews(view_sync::RenderedView const& view, std::array<XrCompositionLayerProjectionView, 2>& out) {
    float const halfX = view.fovXDegrees * std::numbers::pi_v<float> / 360.0f;
    float const halfY = std::atan(std::tan(halfX) * static_cast<float>(gImage.Height()) / gImage.Width());
    for (auto& eye : out) {
        eye = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
        eye.pose = view.head;
        eye.fov = {-halfX, halfX, halfY, -halfY};
        eye.subImage.swapchain = gImage.Handle();
        eye.subImage.imageRect = {{0, 0},
            {static_cast<std::int32_t>(gImage.Width()), static_cast<std::int32_t>(gImage.Height())}};
    }
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

void OnPresent(IDXGISwapChain* swapChain) {
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

    if (!gFrameBegun) {
        BeginNextFrame();
        if (!gFrameBegun) {
            return;
        }
    }

    // The pose this image was rendered with (head-tracked mode).
    if (mode == Mode::HeadTracked) {
        if (auto rendered = view_sync::PopRendered()) {
            gLastRendered = rendered;
        }
    }

    XrCompositionLayerQuad quad{};
    XrCompositionLayerProjection projection{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<XrCompositionLayerProjectionView, 2> views{};
    std::array<XrCompositionLayerBaseHeader const*, 1> layers{};
    std::size_t layerCount = 0;
    bool const haveView = mode == Mode::Screen || gLastRendered.has_value();
    if (gFrame.shouldRender && haveView) {
        if (ID3D11RenderTargetView* const target = gImage.Acquire()) {
            ComPtr<ID3D11DeviceContext> immediate;
            gDevice->GetImmediateContext(&immediate);
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
