#pragma once

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

#include <cstdint>
#include <span>
#include <vector>

namespace mevr::xr {

// One OpenXR instance and session bound to the game's D3D11 device.
// Everything except Shutdown runs on the game's render thread (inside Present). The host
// guarantees the render thread isn't in here when Shutdown runs.
class Session {
public:
    // Creates the instance, then the session once a headset is available. Cheap to call every
    // frame: system lookup retries on a cooldown while no headset is connected.
    bool EnsureCreated(ID3D11Device* device);
    void Shutdown();

    // Drains runtime events; begins and ends the session when the runtime asks.
    void PollEvents();
    bool IsRunning() const { return running_; }
    XrSessionState State() const { return state_; }
    // The user quit from the headset's menu (session reached EXITING). The caller should shut
    // the session down and not recreate it until the user asks again.
    bool UserExited() const { return userExited_; }

    // xrWaitFrame + xrBeginFrame. When this returns true, EndFrame must follow.
    bool BeginFrame(XrFrameState& state);
    void EndFrame(XrTime displayTime, std::span<XrCompositionLayerBaseHeader const* const> layers);

    bool HasInstance() const { return instance_ != XR_NULL_HANDLE; }
    XrSession Handle() const { return session_; }
    XrSpace LocalSpace() const { return localSpace_; }
    // Changes whenever the session (and every swapchain created from it) is destroyed.
    std::uint32_t Generation() const { return generation_; }

    // First entry of `preferred` the runtime supports, or 0.
    std::int64_t ChooseSwapchainFormat(std::span<std::int64_t const> preferred) const;

    // Logs failures (rate-limited) and returns XR_SUCCEEDED(result).
    bool Check(XrResult result, char const* what);

private:
    bool CreateInstance();
    bool CreateSession(ID3D11Device* device);
    void DestroySession();

    XrInstance instance_ = XR_NULL_HANDLE;
    XrSystemId system_ = XR_NULL_SYSTEM_ID;
    XrSession session_ = XR_NULL_HANDLE;
    XrSpace localSpace_ = XR_NULL_HANDLE;
    XrSessionState state_ = XR_SESSION_STATE_UNKNOWN;
    bool running_ = false;
    bool userExited_ = false;
    bool instanceFailed_ = false;
    bool loggedWaitingForHeadset_ = false;
    std::uint64_t nextSystemRetryMs_ = 0;
    std::uint32_t generation_ = 0;
    unsigned errorsLogged_ = 0;
};

// A single-image-layer swapchain for a quad layer, with one render target view per image.
class QuadSwapchain {
public:
    bool Create(Session& session, ID3D11Device* device, std::uint32_t width, std::uint32_t height);
    void Destroy();
    // Drops handles without calling OpenXR: for when the session already destroyed them.
    void Forget();

    bool Matches(std::uint32_t width, std::uint32_t height, std::uint32_t generation) const;
    // Acquires and waits for the next image; nullptr on failure.
    ID3D11RenderTargetView* Acquire();
    void Release();

    XrSwapchain Handle() const { return swapchain_; }
    // Session generation this swapchain was created under.
    std::uint32_t Generation() const { return generation_; }
    std::uint32_t Width() const { return width_; }
    std::uint32_t Height() const { return height_; }

private:
    Session* session_ = nullptr;
    XrSwapchain swapchain_ = XR_NULL_HANDLE;
    std::vector<Microsoft::WRL::ComPtr<ID3D11RenderTargetView>> views_;
    std::uint32_t width_ = 0;
    std::uint32_t height_ = 0;
    std::uint32_t generation_ = 0;
};

} // namespace mevr::xr
