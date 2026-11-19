// SPDX-License-Identifier: MIT
// The display answer for the three games (research/27 7c, research/le2-hdr.md): one policy (Answer), reached from LE1's
// GetDesc1 patch in Halcyon's d3d_capture.cpp (MELEVR::MevrBridge::HdrDisplayDesc forwards here) and from our own hook
// on the sequels (OnFactory). Windows HDR can select a float game output despite DesiredRange=SDR; the default (-1)
// answers SDR to the game. This changes the application's DXGI answer, never Windows/display settings.
#include "plugin/HdrDisplayPolicy.hpp"
#include "plugin/Renderer.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/vr/SettingsFile.hpp"
#include <dxgi1_6.h>
#include <atomic>
#include <string>

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
    Answer(desc, false);  // the sequels never had 0 as a default: a 0 in their file is the player's own
    return hr;
}

// [Colour] hdrDisplay as -1, 0 or 1; -1 for a missing or unreadable key. `moveOldDefault` (LE1): a file that says 0,
// LE1's default before 2026-11-14, moves to -1 once ([Colour] hdrDisplayMoved = 1 marks it; a 0 set after that stays).
int Setting(bool moveOldDefault) {
    std::wstring const& path = mevr::host::SettingsPath();
    if (path.empty()) {
        return kHdrDisplayDefault;
    }
    wchar_t text[32]{};
    mevr::vr::settings_file::String(L"Colour", L"hdrDisplay", L"", text, 32, path.c_str());
    std::string narrow;
    for (wchar_t const c : text) {
        if (c == L'\0') {
            break;
        }
        narrow.push_back(c < 128 ? static_cast<char>(c) : '?');
    }
    auto const value = mevr::ini::Number(narrow);
    if (!value) {
        return kHdrDisplayDefault;
    }
    int const want = *value > 0.5 ? 1 : (*value < -0.5 ? -1 : 0);
    if (want != 0 || !moveOldDefault || mevr::vr::settings_file::Int(L"Colour", L"hdrDisplayMoved", 0, path.c_str()) != 0) {
        return want;
    }
    static std::atomic<bool> moved{false};
    if (!moved.exchange(true)) {
        mevr::vr::settings_file::Write(L"Colour", L"hdrDisplay", L"-1", path.c_str());
        mevr::vr::settings_file::Write(L"Colour", L"hdrDisplayMoved", L"1", path.c_str());
        mevr::log::Info("hdr display: [Colour] hdrDisplay = 0 (the default before 2026-11-14) moved to -1 once: the game is "
                        "answered SDR on an HDR display; hdrDisplay = 0 again gives the display's own answer");
    }
    return kHdrDisplayDefault;
}
} // namespace

// research/27 7c: the game takes DXGI_OUTPUT_DESC1::ColorSpace == HDR10 as "HDR capable" and, windowed, drops an HDR
// range to SDR otherwise. [Colour] hdrDisplay:
//    1  an SDR answer becomes HDR10 (10 bits, 1000 nits if the display gives no luminance): the game's HDR range then
//       makes its R16G16B16A16_FLOAT scRGB swapchain without Windows HDR (the eyes get our tonemap)
//    0  the display's own answer
//   -1  (the default on every game since 2026-11-14) an HDR answer becomes SDR: the game keeps its 8-bit SDR frame
//       whatever Windows' HDR switch and [HDR] DesiredRange say. That frame is what the VR renderers are built and
//       measured for; the float path under our tonemaps measured worse than it (research/27 7d); players with HDR
//       monitors reported trouble launching ME1 (the owner, 2026-11-14; gotchas/windows-hdr-desktop).
void Answer(DXGI_OUTPUT_DESC1* desc, bool moveOldDefault) noexcept {
    if (desc == nullptr) {
        return;
    }
    int const want = Setting(moveOldDefault);
    int const was = static_cast<int>(desc->ColorSpace);
    unsigned const wasBits = desc->BitsPerColor;
    bool const hdr = desc->ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
    char const* answered = "its own answer";
    if (want > 0 && !hdr) {
        desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
        desc->BitsPerColor = 10;
        if (desc->MaxLuminance <= 0.0f) {
            desc->MinLuminance = 0.005f;
            desc->MaxLuminance = 1000.0f;
            desc->MaxFullFrameLuminance = 1000.0f;
        }
        answered = "HDR10: its HDR range makes the float backbuffer without Windows HDR (research/27 7c)";
    } else if (want < 0 && hdr) {
        desc->ColorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
        desc->BitsPerColor = 8;
        answered = "SDR: the game keeps its 8-bit frame";
    }
    if (gHdrAnswerLogs.fetch_add(1, std::memory_order_relaxed) < 3) {
        mevr::log::Info("hdr display: the display reports colour space {} ({}), {} bits per colour, {:.0f} nits; answered {} "
                        "([Colour] hdrDisplay = {})",
            was, hdr ? "HDR10" : "SDR", wasBits, desc->MaxLuminance, answered, want);
    }
}

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
