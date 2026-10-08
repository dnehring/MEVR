#include "plugin/PresentHook.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"

#include <LESDK/Init.hpp>

#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace mevr::host::present {
namespace {

using Microsoft::WRL::ComPtr;

using tPresent = HRESULT(STDMETHODCALLTYPE)(IDXGISwapChain* self, UINT syncInterval, UINT flags);
tPresent* gPresentOrig = nullptr;  // written before the hook goes live (core/Hook)

SRWLOCK gLock = SRWLOCK_INIT;
Callback gCallback = nullptr;

HRESULT STDMETHODCALLTYPE PresentHook(IDXGISwapChain* self, UINT syncInterval, UINT flags) {
    if ((flags & DXGI_PRESENT_TEST) == 0) {
        AcquireSRWLockShared(&gLock);
        if (gCallback != nullptr) {
            gCallback(self);
        }
        ReleaseSRWLockShared(&gLock);
    }
    return gPresentOrig(self, syncInterval, flags);
}

// IDXGISwapChain::Present is vtable slot 8. Every DXGI swap chain in the process shares the
// implementation, so hooking the function a throwaway swap chain points at covers the game's.
void* FindPresent() {
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"MEVR_DummyWindow";
    RegisterClassExW(&wc);
    HWND const window = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64,
        nullptr, nullptr, wc.hInstance, nullptr);
    if (window == nullptr) {
        log::Error("present hook: dummy window failed ({})", GetLastError());
        return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.Width = 64;
    desc.BufferDesc.Height = 64;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    void* present = nullptr;
    for (D3D_DRIVER_TYPE const driver : {D3D_DRIVER_TYPE_NULL, D3D_DRIVER_TYPE_HARDWARE}) {
        ComPtr<IDXGISwapChain> swapChain;
        ComPtr<ID3D11Device> device;
        HRESULT const hr = D3D11CreateDeviceAndSwapChain(nullptr, driver, nullptr, 0, nullptr, 0,
            D3D11_SDK_VERSION, &desc, &swapChain, &device, nullptr, nullptr);
        if (SUCCEEDED(hr)) {
            present = (*reinterpret_cast<void***>(swapChain.Get()))[8];
            break;
        }
        log::Warn("present hook: dummy device (driver type {}) failed: 0x{:08x}",
            static_cast<int>(driver), static_cast<unsigned>(hr));
    }
    DestroyWindow(window);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return present;
}

} // namespace

bool Install(::LESDK::Initializer& init) {
    void* const target = FindPresent();
    if (target == nullptr) {
        log::Error("present hook: couldn't locate IDXGISwapChain::Present");
        return false;
    }
    if (!hook::Install(init, "IDXGISwapChain::Present", reinterpret_cast<tPresent*>(target), &PresentHook, gPresentOrig)) {
        log::Error("present hook: install failed");
        return false;
    }
    log::Info("hooked IDXGISwapChain::Present at {}", target);
    return true;
}

void SetCallback(Callback callback) {
    AcquireSRWLockExclusive(&gLock);
    gCallback = callback;
    ReleaseSRWLockExclusive(&gLock);
}

} // namespace mevr::host::present
