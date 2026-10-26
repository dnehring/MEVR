// SPDX-License-Identifier: MIT
#include "plugin/Halcyon.hpp"

#include "core/Log.hpp"
#include "plugin/PerfProbe.hpp"
#include "plugin/TestLoop.hpp"

#include "halcyon/le1/d3d_capture.h"
#include "halcyon/le1/dxgi_proxy.h"
#include "halcyon/le1/logger.h"
#include "halcyon/le1/vr_menu.h"
#include "halcyon/le1/xr_session.h"

#include <MinHook.h>

#include <Windows.h>
#include <dxgi.h>

#include <string>

namespace mevr::host::halcyon {

void Attach() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Attach), &self);

    // Upstream runs this in DllMain so it lands before the engine reads GamerSettings.ini; our
    // attach is later, so an auto-resolution change may only apply from the next launch.
    MELEVR::D3DCapture::ApplyAutoResEarly();

    MELEVR::Logger::LogLine("=====================================================");
    MELEVR::Logger::LogLine("MELEVR1 (Halcyon) inside ME VR Attempt's LE1MEVR.asi");
    MELEVR::Logger::LogLine(std::string("[BUILDSTAMP] compiled ") + __DATE__ + " " + __TIME__);
    MELEVR::Logger::LogLine("=====================================================");
    MELEVR::Logger::LogStartup(self);

    MH_STATUS const status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        log::Error("halcyon: MinHook failed to initialise ({}); VR renderer off", static_cast<int>(status));
        return;
    }
    MELEVR::D3DCapture::EnsureBioEngineSmoothing();
    MELEVR::Menu::InstallInputBlock();
    MELEVR::D3DCapture::InstallDisplayQueryHooks();
    if (!MELEVR::DxgiProxy::EnsureRealDxgiLoaded()) {
        log::Error("halcyon: system dxgi.dll didn't load; VR renderer off");
        return;
    }
    MELEVR::DxgiProxy::InstallRealFactoryHooks();
    // Upstream, as the game's dxgi.dll, saw every factory. We hook the system one's exports, so a
    // factory the game made before our attach would be missed; making one now patches the factory
    // vtable all instances share (the CreateSwapChain capture).
    IDXGIFactory1* factory = nullptr;
    if (SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory))) && factory != nullptr) {
        factory->Release();
    }
    log::Info("halcyon: renderer attached (its log: %LOCALAPPDATA%\\MELEVR\\MELEVR_Log.txt)");
}

void Tick(float deltaSeconds) {
    testloop::Tick(deltaSeconds);  // snapshot / recording keys and request files
    perf::Tick();                  // the frame-breakdown probe: arm a request, write a finished capture
    RefreshCameraSettings();       // the constant eye and blend fix settings, read here (not on the Present thread)
    // Upstream hooks XInput at attach (only DLLs already loaded) and once more at its menu's first
    // frame. Keep trying for the first minutes in case the game loads XInput later; it's
    // idempotent and cheap.
    static double elapsed = 0.0;
    static double sinceTry = 1.0;
    if (elapsed > 300.0) {
        return;
    }
    elapsed += deltaSeconds;
    sinceTry += deltaSeconds;
    if (sinceTry >= 1.0) {
        sinceTry = 0.0;
        MELEVR::Menu::InstallInputBlock();
    }
}

void Shutdown() {
    MELEVR::XrSession::RequestShutdown();
}

} // namespace mevr::host::halcyon
