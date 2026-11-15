// SPDX-License-Identifier: MIT
// Shared LE2/LE3 display-answer policy, extracted unchanged from HalcyonLe2Bridge.
// Windows HDR can select a float game output despite DesiredRange=SDR. Until native
// HDR stereo capture is accepted, the sequel default (-1) answers SDR to the game.
// This changes the application's DXGI answer, never Windows/display settings.
#include "plugin/HdrDisplayPolicy.hpp"
#include "plugin/Renderer.hpp"
#include "core/Log.hpp"
#include <dxgi1_6.h>
#include <atomic>

namespace mevr::host::hdr_display {
constexpr int kHdrDisplayDefault = -1;
namespace {
using GetDesc1Fn = HRESULT(STDMETHODCALLTYPE*)(IDXGIOutput6*, DXGI_OUTPUT_DESC1*);
GetDesc1Fn gRealGetDesc1 = nullptr;
std::atomic<int> gHdrAnswerLogs{0};

HRESULT STDMETHODCALLTYPE GetDesc1Hook(IDXGIOutput6* self, DXGI_OUTPUT_DESC1* desc) {
    HRESULT const hr = gRealGetDesc1 != nullptr ? gRealGetDesc1(self, desc) : E_FAIL;
    if (FAILED(hr) || desc == nullptr) {
        return hr;
    }
    std::wstring const& path = mevr::host::SettingsPath();
    int const want = path.empty() ? kHdrDisplayDefault
                                  : static_cast<int>(GetPrivateProfileIntW(L"Colour", L"hdrDisplay", kHdrDisplayDefault,
                                        path.c_str()));
    int const was = static_cast<int>(desc->ColorSpace);
    bool const hdr = desc->ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    char const* answered = "its own";
    if (want > 0 && !hdr) {
        desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        desc->BitsPerColor = 10;
        answered = "HDR10";
    } else if (want < 0 && hdr) {
        desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        desc->BitsPerColor = 8;
        answered = "SDR";
    }
    if (gHdrAnswerLogs.fetch_add(1, std::memory_order_relaxed) < 3) {
        mevr::log::Info("hdr display: the display reports colour space {} ({}), {} bits, {:.0f} nits; answered {} "
                        "([Colour] hdrDisplay = {})",
            was, hdr ? "HDR10" : "SDR", desc->BitsPerColor, desc->MaxLuminance, answered, want);
    }
    return hr;
}
} // namespace

void OnFactory(void* factory) noexcept {
    static bool hooked = false;  // under Halcyon's hook mutex
    if (hooked || factory == nullptr) {
        return;
    }
    IDXGIFactory1* f1 = nullptr;
    if (FAILED(static_cast<IUnknown*>(factory)->QueryInterface(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&f1))) ||
        f1 == nullptr) {
        return;
    }
    IDXGIAdapter1* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    IDXGIOutput6* output6 = nullptr;
    if (SUCCEEDED(f1->EnumAdapters1(0, &adapter)) && adapter != nullptr && SUCCEEDED(adapter->EnumOutputs(0, &output)) &&
        output != nullptr &&
        SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6))) && output6 != nullptr) {
        void** const slot = *reinterpret_cast<void***>(output6) + 27;  // IDXGIOutput6::GetDesc1
        DWORD old = 0;
        if (*slot != reinterpret_cast<void*>(&GetDesc1Hook) &&
            VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
            gRealGetDesc1 = reinterpret_cast<GetDesc1Fn>(*slot);
            *slot = reinterpret_cast<void*>(&GetDesc1Hook);
            VirtualProtect(slot, sizeof(void*), old, &old);
            hooked = true;
            mevr::log::Info("hdr display: IDXGIOutput6::GetDesc1 hooked (the game's HDR question; [Colour] hdrDisplay)");
        }
    }
    if (output6 != nullptr) {
        output6->Release();
    }
    if (output != nullptr) {
        output->Release();
    }
    if (adapter != nullptr) {
        adapter->Release();
    }
    f1->Release();
}
} // namespace mevr::host::hdr_display
