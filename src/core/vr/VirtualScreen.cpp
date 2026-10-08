#include "core/vr/VirtualScreen.hpp"

#include "core/Log.hpp"
#include "core/d3d11/Blit.hpp"
#include "core/xr/Session.hpp"

#include <dxgi.h>

#include <array>
#include <atomic>

namespace mevr::vr::virtual_screen {
namespace {

using Microsoft::WRL::ComPtr;

// Panel placement in LOCAL space (origin = head position at the last recenter).
constexpr float kDistanceMeters = 2.0f;
constexpr float kWidthMeters = 2.4f;  // about 62 degrees wide at 2 m

std::atomic<bool> gEnabled{true};
ComPtr<ID3D11Device> gDevice;
HWND gWindow = nullptr;
d3d11::Blit gBlit;
xr::Session gSession;
xr::QuadSwapchain gQuad;
bool gXrUp = false;
XrSessionState gLastState = XR_SESSION_STATE_UNKNOWN;
bool gFailed = false;

// A swapchain from an older session was destroyed along with that session.
void ReleaseQuad() {
    if (gQuad.Handle() == XR_NULL_HANDLE) {
        return;
    }
    if (gSession.Handle() != XR_NULL_HANDLE && gQuad.Generation() == gSession.Generation()) {
        gQuad.Destroy();
    } else {
        gQuad.Forget();
    }
}

// Closes everything down to the OpenXR instance, so the next start picks up whichever runtime
// is active then (e.g. after switching from the Oculus runtime to SteamVR).
void TearDownXr() {
    if (!gSession.HasInstance()) {
        return;
    }
    ReleaseQuad();
    gSession.Shutdown();
    gLastState = XR_SESSION_STATE_UNKNOWN;
    log::Info("virtual screen: XR {} closed", gXrUp ? "session" : "instance (no session yet)");
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
    log::Info("virtual screen: game window to foreground: {}", ok ? "ok" : "refused by Windows");
}

} // namespace

void SetEnabled(bool enabled) {
    gEnabled = enabled;
}

bool IsEnabled() {
    return gEnabled;
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
            log::Error("virtual screen disabled: blit setup failed");
            return;
        }
        gDevice = device;
        DXGI_SWAP_CHAIN_DESC scDesc{};
        if (SUCCEEDED(swapChain->GetDesc(&scDesc))) {
            gWindow = scDesc.OutputWindow;
        }
    } else if (gDevice != device) {
        return;  // a second device's swap chain; only mirror the game's main one
    }

    if (!gSession.EnsureCreated(gDevice.Get())) {
        return;
    }
    gXrUp = true;
    gSession.PollEvents();
    if (gSession.UserExited()) {
        gEnabled = false;
        TearDownXr();
        log::Info("virtual screen: quit from the headset menu; press ] in game to turn VR back on");
        return;
    }
    if (gSession.State() != gLastState) {
        gLastState = gSession.State();
        if (gLastState == XR_SESSION_STATE_FOCUSED) {
            BringToForeground(gWindow);
        }
    }
    if (!gSession.IsRunning()) {
        return;
    }

    ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backbuffer)))) {
        return;
    }
    D3D11_TEXTURE2D_DESC desc{};
    backbuffer->GetDesc(&desc);

    if (!gQuad.Matches(desc.Width, desc.Height, gSession.Generation())) {
        ReleaseQuad();  // resized, or the session was recreated
        if (!gQuad.Create(gSession, gDevice.Get(), desc.Width, desc.Height)) {
            gFailed = true;
            log::Error("virtual screen disabled: swapchain creation failed");
            return;
        }
    }

    XrFrameState frame{};
    if (!gSession.BeginFrame(frame)) {
        return;
    }

    XrCompositionLayerQuad layer{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerBaseHeader const*, 1> layers{};
    std::size_t layerCount = 0;
    if (frame.shouldRender) {
        if (ID3D11RenderTargetView* const target = gQuad.Acquire()) {
            ComPtr<ID3D11DeviceContext> immediate;
            gDevice->GetImmediateContext(&immediate);
            gBlit.Draw(immediate.Get(), backbuffer.Get(), target, gQuad.Width(), gQuad.Height());
            gQuad.Release();

            layer.space = gSession.LocalSpace();
            layer.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
            layer.subImage.swapchain = gQuad.Handle();
            layer.subImage.imageRect = {{0, 0},
                {static_cast<std::int32_t>(gQuad.Width()), static_cast<std::int32_t>(gQuad.Height())}};
            layer.pose.orientation.w = 1.0f;
            layer.pose.position = {0.0f, 0.0f, -kDistanceMeters};
            layer.size = {kWidthMeters, kWidthMeters * static_cast<float>(gQuad.Height()) / gQuad.Width()};
            layers[layerCount++] = reinterpret_cast<XrCompositionLayerBaseHeader const*>(&layer);
        }
    }
    gSession.EndFrame(frame.predictedDisplayTime, {layers.data(), layerCount});
}

void Shutdown() {
    ReleaseQuad();
    gSession.Shutdown();
    gXrUp = false;
    gLastState = XR_SESSION_STATE_UNKNOWN;
    gBlit.Shutdown();
    gDevice.Reset();
    gWindow = nullptr;
    gFailed = false;
}

} // namespace mevr::vr::virtual_screen
