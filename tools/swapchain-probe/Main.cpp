// SPDX-License-Identifier: MIT
// How many swapchains the active OpenXR runtime lets one session hold (XR_ERROR_LIMIT_REACHED: SteamVR refused the sights',
// the calibration's and a dozen other overlays' swapchains in the user's session, 2026-11-06; research/21 section 18). A
// session of its own on a D3D11 device, then swapchains of each size until the runtime refuses one (or `cap`), then all
// destroyed: whether the limit is a count (the same for every size) or memory, and whether a destroyed one is given back.
//
//   mevr-swapchain-probe [cap]   (default 200)
//
// It starts a session like any VR app: run it with the game closed (on the fake headset, tools/fake-headset.ps1 -On).

#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_PLATFORM_WIN32
#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

using Microsoft::WRL::ComPtr;

char const* ResultName(XrInstance instance, XrResult r) {
    static char text[XR_MAX_RESULT_STRING_SIZE];
    if (instance == XR_NULL_HANDLE || XR_FAILED(xrResultToString(instance, r, text))) {
        std::snprintf(text, sizeof(text), "%d", static_cast<int>(r));
    }
    return text;
}

// Swapchains of `size` until the runtime refuses one or `cap` exist; returns how many it made (and destroys them).
int Fill(XrInstance instance, XrSession session, std::int64_t format, std::uint32_t size, int cap, XrResult& refusal) {
    std::vector<XrSwapchain> made;
    refusal = XR_SUCCESS;
    for (int i = 0; i < cap; ++i) {
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format;
        info.sampleCount = 1;
        info.width = size;
        info.height = size;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;
        XrSwapchain chain = XR_NULL_HANDLE;
        XrResult const r = xrCreateSwapchain(session, &info, &chain);
        if (XR_FAILED(r)) {
            refusal = r;
            break;
        }
        made.push_back(chain);
    }
    int const count = static_cast<int>(made.size());
    // A destroyed one given back: free one, make one.
    if (refusal != XR_SUCCESS && !made.empty()) {
        xrDestroySwapchain(made.back());
        made.pop_back();
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        info.format = format;
        info.sampleCount = 1;
        info.width = info.height = size;
        info.faceCount = info.arraySize = info.mipCount = 1;
        XrSwapchain again = XR_NULL_HANDLE;
        XrResult const r = xrCreateSwapchain(session, &info, &again);
        std::printf("    after destroying one, another %s (%s)\n", XR_SUCCEEDED(r) ? "was made" : "was refused", ResultName(instance, r));
        if (XR_SUCCEEDED(r)) {
            made.push_back(again);
        }
    }
    for (XrSwapchain chain : made) {
        xrDestroySwapchain(chain);
    }
    return count;
}

} // namespace

int main(int argc, char** argv) {
    int const cap = argc > 1 ? std::max(1, std::atoi(argv[1])) : 200;
    char const* extensions[] = {XR_KHR_D3D11_ENABLE_EXTENSION_NAME};
    XrInstanceCreateInfo ici{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ici.applicationInfo.applicationName, "MEVR swapchain probe");
    ici.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    ici.enabledExtensionCount = 1;
    ici.enabledExtensionNames = extensions;
    XrInstance instance = XR_NULL_HANDLE;
    XrResult r = xrCreateInstance(&ici, &instance);
    if (XR_FAILED(r)) {
        std::printf("xrCreateInstance failed (%d)\n", static_cast<int>(r));
        return 1;
    }
    XrInstanceProperties ip{XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties(instance, &ip);
    XrSystemGetInfo sgi{XR_TYPE_SYSTEM_GET_INFO};
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    XrSystemId system = XR_NULL_SYSTEM_ID;
    r = xrGetSystem(instance, &sgi, &system);
    if (XR_FAILED(r)) {
        std::printf("xrGetSystem failed (%s)\n", ResultName(instance, r));
        xrDestroyInstance(instance);
        return 1;
    }
    XrSystemProperties sp{XR_TYPE_SYSTEM_PROPERTIES};
    xrGetSystemProperties(instance, system, &sp);
    std::printf("runtime %s %u.%u.%u, system '%s', max layers %u\n", ip.runtimeName, XR_VERSION_MAJOR(ip.runtimeVersion),
        XR_VERSION_MINOR(ip.runtimeVersion), XR_VERSION_PATCH(ip.runtimeVersion), sp.systemName, sp.graphicsProperties.maxLayerCount);

    PFN_xrGetD3D11GraphicsRequirementsKHR requirements = nullptr;
    xrGetInstanceProcAddr(instance, "xrGetD3D11GraphicsRequirementsKHR", reinterpret_cast<PFN_xrVoidFunction*>(&requirements));
    XrGraphicsRequirementsD3D11KHR req{XR_TYPE_GRAPHICS_REQUIREMENTS_D3D11_KHR};
    if (requirements == nullptr || XR_FAILED(requirements(instance, system, &req))) {
        std::printf("no D3D11 requirements\n");
        xrDestroyInstance(instance);
        return 1;
    }
    ComPtr<IDXGIFactory1> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf()));
    ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; factory && factory->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) == S_OK; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (std::memcmp(&desc.AdapterLuid, &req.adapterLuid, sizeof(LUID)) == 0) {
            break;
        }
    }
    ComPtr<ID3D11Device> device;
    D3D_FEATURE_LEVEL const levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    if (FAILED(D3D11CreateDevice(adapter.Get(), adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
            D3D11_SDK_VERSION, device.GetAddressOf(), nullptr, nullptr))) {
        std::printf("no D3D11 device\n");
        xrDestroyInstance(instance);
        return 1;
    }
    XrGraphicsBindingD3D11KHR binding{XR_TYPE_GRAPHICS_BINDING_D3D11_KHR};
    binding.device = device.Get();
    XrSessionCreateInfo sci{XR_TYPE_SESSION_CREATE_INFO};
    sci.next = &binding;
    sci.systemId = system;
    XrSession session = XR_NULL_HANDLE;
    r = xrCreateSession(instance, &sci, &session);
    if (XR_FAILED(r)) {
        std::printf("xrCreateSession failed (%s)\n", ResultName(instance, r));
        xrDestroyInstance(instance);
        return 1;
    }
    std::uint32_t formatCount = 0;
    xrEnumerateSwapchainFormats(session, 0, &formatCount, nullptr);
    std::vector<std::int64_t> formats(formatCount);
    xrEnumerateSwapchainFormats(session, formatCount, &formatCount, formats.data());
    std::int64_t format = formats.empty() ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : formats[0];
    for (std::int64_t f : formats) {
        if (f == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) {
            format = f;
        }
    }
    std::printf("format %lld; at most %d swapchains per size\n", static_cast<long long>(format), cap);
    for (std::uint32_t size : {64u, 256u, 1024u, 2048u}) {
        XrResult refusal = XR_SUCCESS;
        int const count = Fill(instance, session, format, size, cap, refusal);
        std::printf("  %ux%u: %d swapchains%s%s\n", size, size, count, refusal != XR_SUCCESS ? ", then " : " (the cap)",
            refusal != XR_SUCCESS ? ResultName(instance, refusal) : "");
    }
    xrDestroySession(session);
    xrDestroyInstance(instance);
    return 0;
}
