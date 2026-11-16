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
#include "plugin/GameThreadCalls.hpp"
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

std::wstring gRuntimeManifest;  // what XR_RUNTIME_JSON names ("" when unset), read at attach

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
    wchar_t current[1024] = {};
    if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON", current, 1024) > 0) {
        gRuntimeManifest = current;
    }
}

bool NamesSteamVr(std::wstring path) {
    std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return path.find(L"steamvr") != std::wstring::npos || path.find(L"steamxr") != std::wstring::npos;
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

namespace ME2VR::MevrBridge {

// ProcessEvent on the game thread only (research/36): ME3's renderer's Present-thread calls go to plugin/GameThreadCalls, the
// queue LE1's host runs, drained at the start of the next engine tick (plugin/Entry).
bool OnGameThread() noexcept {
    return mevr::host::game_thread::OnGameThread();
}

bool QueueProcessEvent(void* object, void* function, void const* params, std::uint32_t size) noexcept {
    return mevr::host::game_thread::Queue(object, function, params, size);
}

bool XrRuntimeIsSteamVr() noexcept {
    namespace h = mevr::host::halcyon_le3;
    if (!h::gRuntimeManifest.empty()) {
        return h::NamesSteamVr(h::gRuntimeManifest);
    }
    HKEY key = nullptr;
    bool steam = false;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t path[1024] = {};
        DWORD bytes = sizeof(path) - sizeof(wchar_t);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type, reinterpret_cast<LPBYTE>(path), &bytes) == ERROR_SUCCESS &&
            (type == REG_SZ || type == REG_EXPAND_SZ)) {
            steam = h::NamesSteamVr(path);
        }
        RegCloseKey(key);
    }
    return steam;
}

bool KeyDown(int vk) noexcept {
    return mevr::window::KeyDownInGame(vk);  // the game's window (or its console), not another program's
}

} // namespace ME2VR::MevrBridge
