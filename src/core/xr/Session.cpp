#include "core/xr/Session.hpp"

#include "core/Log.hpp"

#include <dxgi.h>

#include <cstring>
#include <string>

namespace mevr::xr {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::uint64_t kSystemRetryMs = 2000;
constexpr unsigned kMaxErrorsLogged = 30;

char const* StateName(XrSessionState state) {
    switch (state) {
    case XR_SESSION_STATE_IDLE: return "IDLE";
    case XR_SESSION_STATE_READY: return "READY";
    case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
    case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
    case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
    case XR_SESSION_STATE_STOPPING: return "STOPPING";
    case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
    case XR_SESSION_STATE_EXITING: return "EXITING";
    default: return "UNKNOWN";
    }
}

bool AdapterLuid(ID3D11Device* device, LUID& out) {
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) || FAILED(dxgiDevice->GetAdapter(&adapter)) ||
        FAILED(adapter->GetDesc(&desc))) {
        return false;
    }
    out = desc.AdapterLuid;
    return true;
}

} // namespace

bool Session::Check(XrResult result, char const* what) {
    if (XR_SUCCEEDED(result)) {
        return true;
    }
    if (errorsLogged_ < kMaxErrorsLogged) {
        char name[XR_MAX_RESULT_STRING_SIZE] = "?";
        if (instance_ != XR_NULL_HANDLE) {
            xrResultToString(instance_, result, name);
        }
        log::Error("OpenXR {} failed: {} ({})", what, name, static_cast<int>(result));
        if (++errorsLogged_ == kMaxErrorsLogged) {
            log::Warn("OpenXR: further errors suppressed");
        }
    }
    return false;
}

bool Session::CreateInstance() {
    std::uint32_t count = 0;
    xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr);
    std::vector<XrExtensionProperties> extensions(count, {XR_TYPE_EXTENSION_PROPERTIES});
    if (!Check(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, extensions.data()),
            "xrEnumerateInstanceExtensionProperties")) {
        return false;
    }
    bool hasD3D11 = false;
    for (auto const& ext : extensions) {
        hasD3D11 |= std::strcmp(ext.extensionName, XR_KHR_D3D11_ENABLE_EXTENSION_NAME) == 0;
    }
    if (!hasD3D11) {
        log::Error("OpenXR runtime has no {}", XR_KHR_D3D11_ENABLE_EXTENSION_NAME);
        return false;
    }

    char const* const enabled[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo info{XR_TYPE_INSTANCE_CREATE_INFO};
    std::strcpy(info.applicationInfo.applicationName, "MEVR");
    std::strcpy(info.applicationInfo.engineName, "Unreal Engine 3 (Mass Effect LE)");
    info.applicationInfo.applicationVersion = 1;
    // 1.0 for the widest runtime support; 1.1-only features aren't needed.
    info.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    info.enabledExtensionCount = 1;
    info.enabledExtensionNames = enabled;
    if (!Check(xrCreateInstance(&info, &instance_), "xrCreateInstance")) {
        instance_ = XR_NULL_HANDLE;
        return false;
    }

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    if (XR_SUCCEEDED(xrGetInstanceProperties(instance_, &props))) {
        log::Info("OpenXR runtime: {} {}.{}.{}", props.runtimeName, XR_VERSION_MAJOR(props.runtimeVersion),
            XR_VERSION_MINOR(props.runtimeVersion), XR_VERSION_PATCH(props.runtimeVersion));
    }
    return true;
}

bool Session::CreateSession(ID3D11Device* device) {
    PFN_xrGetD3D11GraphicsRequirementsKHR getRequirements = nullptr;
    if (!Check(xrGetInstanceProcAddr(instance_, "xrGetD3D11GraphicsRequirementsKHR",
                   reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)),
            "xrGetInstanceProcAddr(xrGetD3D11GraphicsRequirementsKHR)")) {
        return false;
    }
    XrGraphicsRequirementsD3D11KHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (!Check(getRequirements(instance_, system_, &requirements), "xrGetD3D11GraphicsRequirementsKHR")) {
        return false;
    }

    // The headset must be driven by the GPU the game renders on.
    LUID luid{};
    if (!AdapterLuid(device, luid) || luid.LowPart != requirements.adapterLuid.LowPart ||
        luid.HighPart != requirements.adapterLuid.HighPart) {
        log::Error("OpenXR: the headset's GPU isn't the one the game renders on; VR disabled");
        return false;
    }
    if (device->GetFeatureLevel() < requirements.minFeatureLevel) {
        log::Error("OpenXR: game device feature level 0x{:x} below required 0x{:x}",
            static_cast<int>(device->GetFeatureLevel()), static_cast<int>(requirements.minFeatureLevel));
        return false;
    }

    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device;
    XrSessionCreateInfo info{XR_TYPE_SESSION_CREATE_INFO};
    info.next = &binding;
    info.systemId = system_;
    if (!Check(xrCreateSession(instance_, &info, &session_), "xrCreateSession")) {
        session_ = XR_NULL_HANDLE;
        return false;
    }

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    spaceInfo.poseInReferenceSpace.orientation.w = 1.0f;
    if (!Check(xrCreateReferenceSpace(session_, &spaceInfo, &localSpace_), "xrCreateReferenceSpace(LOCAL)")) {
        DestroySession();
        return false;
    }
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!Check(xrCreateReferenceSpace(session_, &spaceInfo, &viewSpace_), "xrCreateReferenceSpace(VIEW)")) {
        DestroySession();
        return false;
    }
    ++generation_;
    log::Info("OpenXR session created");
    return true;
}

bool Session::EnsureCreated(ID3D11Device* device) {
    if (session_ != XR_NULL_HANDLE) {
        return true;
    }
    if (instanceFailed_ || userExited_) {
        return false;
    }
    if (instance_ == XR_NULL_HANDLE && !CreateInstance()) {
        instanceFailed_ = true;  // no usable runtime; don't retry every frame
        log::Error("OpenXR unavailable; VR output disabled until the mod reloads");
        return false;
    }

    if (system_ == XR_NULL_SYSTEM_ID) {
        std::uint64_t const now = GetTickCount64();
        if (now < nextSystemRetryMs_) {
            return false;
        }
        XrSystemGetInfo getInfo{XR_TYPE_SYSTEM_GET_INFO};
        getInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
        XrResult const result = xrGetSystem(instance_, &getInfo, &system_);
        if (result == XR_ERROR_FORM_FACTOR_UNAVAILABLE) {
            if (!loggedWaitingForHeadset_) {
                log::Info("OpenXR: no headset yet; retrying every {} s", kSystemRetryMs / 1000);
                loggedWaitingForHeadset_ = true;
            }
            nextSystemRetryMs_ = now + kSystemRetryMs;
            system_ = XR_NULL_SYSTEM_ID;
            return false;
        }
        if (!Check(result, "xrGetSystem")) {
            nextSystemRetryMs_ = now + kSystemRetryMs;
            system_ = XR_NULL_SYSTEM_ID;
            return false;
        }
        XrSystemProperties props{XR_TYPE_SYSTEM_PROPERTIES};
        if (XR_SUCCEEDED(xrGetSystemProperties(instance_, system_, &props))) {
            log::Info("OpenXR system: {} (max layers {})", props.systemName, props.graphicsProperties.maxLayerCount);
        }
    }

    if (!CreateSession(device)) {
        // Probably permanent (wrong GPU etc.); stop retrying until the mod reloads.
        instanceFailed_ = true;
        return false;
    }
    return true;
}

void Session::DestroySession() {
    running_ = false;
    state_ = XR_SESSION_STATE_UNKNOWN;
    for (XrSpace* space : {&viewSpace_, &localSpace_}) {
        if (*space != XR_NULL_HANDLE) {
            xrDestroySpace(*space);
            *space = XR_NULL_HANDLE;
        }
    }
    if (session_ != XR_NULL_HANDLE) {
        xrDestroySession(session_);  // also destroys its swapchains
        session_ = XR_NULL_HANDLE;
        ++generation_;
    }
}

void Session::Shutdown() {
    DestroySession();
    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
        ++instanceGeneration_;
    }
    system_ = XR_NULL_SYSTEM_ID;
    userExited_ = false;
    instanceFailed_ = false;
    loggedWaitingForHeadset_ = false;
    errorsLogged_ = 0;
}

void Session::PollEvents() {
    if (instance_ == XR_NULL_HANDLE) {
        return;
    }
    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (xrPollEvent(instance_, &event) == XR_SUCCESS) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto const& changed = reinterpret_cast<XrEventDataSessionStateChanged const&>(event);
            state_ = changed.state;
            log::Info("OpenXR session state: {}", StateName(state_));
            if (state_ == XR_SESSION_STATE_READY) {
                XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                running_ = Check(xrBeginSession(session_, &begin), "xrBeginSession");
            } else if (state_ == XR_SESSION_STATE_STOPPING) {
                Check(xrEndSession(session_), "xrEndSession");
                running_ = false;
            } else if (state_ == XR_SESSION_STATE_EXITING) {
                // The user chose Quit in the headset menu. Don't come back on our own.
                userExited_ = true;
                DestroySession();
                return;
            } else if (state_ == XR_SESSION_STATE_LOSS_PENDING) {
                // Runtime trouble (e.g. Link dropped): rebuild when it allows.
                DestroySession();
                system_ = XR_NULL_SYSTEM_ID;
                nextSystemRetryMs_ = GetTickCount64() + kSystemRetryMs;
            }
            break;
        }
        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            log::Warn("OpenXR instance loss pending; shutting down XR");
            Shutdown();
            nextSystemRetryMs_ = GetTickCount64() + kSystemRetryMs;
            return;
        default:
            break;
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool Session::BeginFrame(XrFrameState& state) {
    state = {XR_TYPE_FRAME_STATE};
    XrFrameWaitInfo wait{XR_TYPE_FRAME_WAIT_INFO};
    if (!Check(xrWaitFrame(session_, &wait, &state), "xrWaitFrame")) {
        return false;
    }
    XrFrameBeginInfo begin{XR_TYPE_FRAME_BEGIN_INFO};
    return Check(xrBeginFrame(session_, &begin), "xrBeginFrame");
}

void Session::EndFrame(XrTime displayTime, std::span<XrCompositionLayerBaseHeader const* const> layers) {
    XrFrameEndInfo end{XR_TYPE_FRAME_END_INFO};
    end.displayTime = displayTime;
    end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    end.layerCount = static_cast<std::uint32_t>(layers.size());
    end.layers = layers.data();
    Check(xrEndFrame(session_, &end), "xrEndFrame");
}

bool Session::LocateHead(XrTime time, XrPosef& out) {
    XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};
    if (!Check(xrLocateSpace(viewSpace_, localSpace_, time, &location), "xrLocateSpace(VIEW)")) {
        return false;
    }
    if ((location.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) == 0) {
        return false;
    }
    out = location.pose;
    if ((location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) == 0) {
        out.position = {0.0f, 0.0f, 0.0f};
    }
    return true;
}

bool Session::LocateEyes(XrTime time, XrView (&out)[2]) {
    XrViewLocateInfo info{XR_TYPE_VIEW_LOCATE_INFO};
    info.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    info.displayTime = time;
    info.space = localSpace_;
    XrViewState state{XR_TYPE_VIEW_STATE};
    out[0] = {XR_TYPE_VIEW};
    out[1] = {XR_TYPE_VIEW};
    std::uint32_t count = 0;
    if (!Check(xrLocateViews(session_, &info, &state, 2, &count, out), "xrLocateViews") || count != 2) {
        return false;
    }
    return (state.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0;
}

std::int64_t Session::ChooseSwapchainFormat(std::span<std::int64_t const> preferred) const {
    std::uint32_t count = 0;
    xrEnumerateSwapchainFormats(session_, 0, &count, nullptr);
    std::vector<std::int64_t> formats(count);
    xrEnumerateSwapchainFormats(session_, count, &count, formats.data());
    for (std::int64_t want : preferred) {
        for (std::int64_t have : formats) {
            if (want == have) {
                return want;
            }
        }
    }
    return 0;
}

bool ColorSwapchain::Create(Session& session, ID3D11Device* device, std::uint32_t width, std::uint32_t height) {
    // _SRGB: the blit writes linear color and the runtime gets correctly encoded images.
    std::int64_t const preferred[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM_SRGB};
    std::int64_t const format = session.ChooseSwapchainFormat(preferred);
    if (format == 0) {
        log::Error("OpenXR: runtime offers no sRGB 8-bit swapchain format");
        return false;
    }

    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    info.format = format;
    info.sampleCount = 1;
    info.width = width;
    info.height = height;
    info.faceCount = 1;
    info.arraySize = 1;
    info.mipCount = 1;
    if (!session.Check(xrCreateSwapchain(session.Handle(), &info, &swapchain_), "xrCreateSwapchain")) {
        swapchain_ = XR_NULL_HANDLE;
        return false;
    }

    std::uint32_t count = 0;
    xrEnumerateSwapchainImages(swapchain_, 0, &count, nullptr);
    std::vector<XrSwapchainImageD3D11KHR> images(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (!session.Check(xrEnumerateSwapchainImages(swapchain_, count, &count,
                           reinterpret_cast<XrSwapchainImageBaseHeader*>(images.data())),
            "xrEnumerateSwapchainImages")) {
        Destroy();
        return false;
    }
    views_.clear();
    for (auto const& image : images) {
        D3D11_RENDER_TARGET_VIEW_DESC rtv{};
        rtv.Format = static_cast<DXGI_FORMAT>(format);
        rtv.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        ComPtr<ID3D11RenderTargetView> view;
        if (FAILED(device->CreateRenderTargetView(image.texture, &rtv, &view))) {
            log::Error("OpenXR: render target view for swapchain image failed");
            Destroy();
            return false;
        }
        views_.push_back(view);
    }

    session_ = &session;
    width_ = width;
    height_ = height;
    format_ = static_cast<DXGI_FORMAT>(format);
    generation_ = session.Generation();
    log::Info("color swapchain {}x{}, format {}, {} images", width, height, format, count);
    return true;
}

void ColorSwapchain::Destroy() {
    views_.clear();
    if (swapchain_ != XR_NULL_HANDLE) {
        xrDestroySwapchain(swapchain_);
    }
    Forget();
}

void ColorSwapchain::Forget() {
    acquired_.Reset();
    views_.clear();
    swapchain_ = XR_NULL_HANDLE;
    session_ = nullptr;
    width_ = height_ = 0;
    format_ = DXGI_FORMAT_UNKNOWN;
}

bool ColorSwapchain::Matches(std::uint32_t width, std::uint32_t height, std::uint32_t generation) const {
    return swapchain_ != XR_NULL_HANDLE && width == width_ && height == height_ && generation == generation_;
}

ID3D11RenderTargetView* ColorSwapchain::Acquire() {
    std::uint32_t index = 0;
    XrSwapchainImageAcquireInfo acquire{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    if (!session_->Check(xrAcquireSwapchainImage(swapchain_, &acquire, &index), "xrAcquireSwapchainImage")) {
        return nullptr;
    }
    XrSwapchainImageWaitInfo wait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    wait.timeout = XR_INFINITE_DURATION;
    if (!session_->Check(xrWaitSwapchainImage(swapchain_, &wait), "xrWaitSwapchainImage") || index >= views_.size()) {
        Release();
        return nullptr;
    }
    ComPtr<ID3D11Resource> resource;
    views_[index]->GetResource(&resource);
    resource.As(&acquired_);
    return views_[index].Get();
}

void ColorSwapchain::Release() {
    acquired_.Reset();
    XrSwapchainImageReleaseInfo release{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    session_->Check(xrReleaseSwapchainImage(swapchain_, &release), "xrReleaseSwapchainImage");
}

} // namespace mevr::xr
