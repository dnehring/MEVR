// SPDX-License-Identifier: MIT
#include "plugin/HalcyonLe3.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/Window.hpp"
#include "engine/SettingsFile.hpp"
#include "halcyon/le3/d3d_capture.h"
#include "halcyon/le3/dxgi_proxy.h"
#include "halcyon/le3/logger.h"
#include "halcyon/le3/mevr_bridge.h"
#include "plugin/CrashBreadcrumbs.hpp"
#include "plugin/FloorLock.hpp"
#include "plugin/TestLoop.hpp"
#include "plugin/Voice.hpp"
#include "plugin/VoiceModel.hpp"

#include <MinHook.h>

#include <Windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>

namespace mevr::host::halcyon_le3 {
namespace {

// ---- [Debug] openxrRuntime ------------------------------------------------------------------------------------------


// [Debug] openxrRuntime = <runtime manifest .json>: the statically linked loader takes XR_RUNTIME_JSON over the machine's
// active runtime (HKLM, admin to change), as the LE1 host does (plugin/HalcyonBridge). The fake headset's SteamVR null
// driver needs SteamVR's manifest while the active runtime may be another's; tools/fake-headset.ps1 sets the key.
void PinRuntime() {
    std::filesystem::path const settings = engine::SettingsFile();
    wchar_t manifest[1024] = {};
    if (!settings.empty() &&
        GetPrivateProfileStringW(L"Debug", L"openxrRuntime", L"", manifest, 1024, settings.c_str()) > 0 && manifest[0] != 0) {
        if (GetFileAttributesW(manifest) != INVALID_FILE_ATTRIBUTES) {
            SetEnvironmentVariableW(L"XR_RUNTIME_JSON", manifest);
            log::Info("openxr runtime: this process uses the manifest [Debug] openxrRuntime names: {}",
                ini::Utf8(std::filesystem::path(manifest)));
        } else {
            log::Warn("openxr runtime: [Debug] openxrRuntime names a file that isn't there, the active runtime stays: {}",
                ini::Utf8(std::filesystem::path(manifest)));
        }
    }
}

} // namespace

void Attach() {
    HMODULE self = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Attach), &self);
    PinRuntime();  // before its device hook asks which runtime this is (the SteamVR device-flag fix)

    ME2VR::Log::Line("=====================================================");
    ME2VR::Log::Line("ME3VR (Halcyon) inside ME VR Attempt's LE3MEVR.asi");
    ME2VR::Log::Line(std::string("[BUILDSTAMP] compiled ") + __DATE__ + " " + __TIME__);
    ME2VR::Log::Line("=====================================================");

    MH_STATUS const status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
        log::Error("halcyon le3: MinHook failed to initialise ({}); VR renderer off", static_cast<int>(status));
        return;
    }
    // Upstream runs this in DllMain, before the engine reads GamerSettings.ini; our attach is later, so its render
    // resolution and depth of field may only apply from the next launch (as for LE1: plugin/Halcyon).
    ME2VR::D3DCapture::ArmResolutionSpoof();
    ME2VR::D3DCapture::InstallDisplayQueryHooks();
    // Not upstream's InstallBreakpointProbes (diagnostic, disabled at this pin anyway) nor its finders' probes.
    if (!ME2VR::Dxgi::EnsureLoaded()) {
        log::Error("halcyon le3: the system dxgi.dll didn't load; VR renderer off");
        return;
    }
#if defined(MEVR_WITH_MAIN_MENU)
    voice::Init();
    voice_model::Init();
#endif
    ME2VR::Dxgi::InstallRealFactoryHooks();
    log::Info("halcyon le3: renderer attached (Halcyon log in LOCALAPPDATA/MELEVR; renderer settings next to MassEffect3.exe)");
}

void AfterTick(float deltaSeconds) {
    testloop::Tick(deltaSeconds);  // snapshot / recording keys and request files, as LE1's halcyon::Tick
    // [Body] floorLock, once a second (as LE1's halcyon::RefreshCameraSettings).
    static ULONGLONG nextReadMs = 0;
    if (ULONGLONG const now = GetTickCount64(); now >= nextReadMs) {
        nextReadMs = now + 1000;
        floor_lock::RefreshSettings();
    }
}

} // namespace mevr::host::halcyon_le3
