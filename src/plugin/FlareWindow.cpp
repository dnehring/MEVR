// SPDX-License-Identifier: MIT
#include "plugin/FlareWindow.hpp"

#include "core/DevChannels.hpp"
#include "core/Exe.hpp"
#include "core/Hook.hpp"
#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/FlareWindow.hpp"
#include "engine/Addresses.hpp"
#include "plugin/Renderer.hpp"

#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>

namespace mevr::host::flare_window {
namespace {

namespace fw = vr::flare_window;

// FSceneView's matrices (the same offsets in all three games: src/halcyon/MevrMetricView.h's ViewFields).
constexpr std::size_t kProjection = 0xD0;               // the CPU's window (mode 2 never writes it)
constexpr std::size_t kTranslatedView = 0x190;
constexpr std::size_t kInvTranslatedViewProj = 0x210;   // [INVMAT]'s refresh of the GPU's matrix: the eye's in mode 2

// The reflection pass: (dynamic data, scene proxy, draw interface, view, DPG). Five integer/pointer slots, forwarded.
using RenderReflections = void(void* data, void* proxy, void* pdi, void* view, std::uintptr_t dpg);
RenderReflections* gOriginal = nullptr;

std::atomic<bool> gInstalled{false};
std::atomic<bool> gEnabled{false};
std::atomic<std::uint64_t> gCalls{0}, gRewritten{0}, gSame{0}, gFaults{0};

struct ViewMatrices {
    fw::Mat4 tv{}, p{}, inv{};
};

bool ReadView(void const* view, ViewMatrices* out) noexcept {
    __try {
        auto const* const v = static_cast<std::uint8_t const*>(view);
        std::memcpy(out->tv.data(), v + kTranslatedView, sizeof(float) * 16);
        std::memcpy(out->p.data(), v + kProjection, sizeof(float) * 16);
        std::memcpy(out->inv.data(), v + kInvTranslatedViewProj, sizeof(float) * 16);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gFaults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

bool WriteInverse(void* view, float const* m) noexcept {
    __try {
        std::memcpy(static_cast<std::uint8_t*>(view) + kInvTranslatedViewProj, m, sizeof(float) * 16);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        gFaults.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
}

// The engine's own pass with the window's inverse in the view; the engine's inverse back however the call ends.
void CallWithWindow(void* data, void* proxy, void* pdi, void* view, std::uintptr_t dpg, float const* engineInverse) {
    __try {
        gOriginal(data, proxy, pdi, view, dpg);
    } __finally {
        WriteInverse(view, engineInverse);
    }
}

void RenderReflectionsHook(void* data, void* proxy, void* pdi, void* view, std::uintptr_t dpg) {
    if (!gEnabled.load(std::memory_order_relaxed) || view == nullptr) {
        gOriginal(data, proxy, pdi, view, dpg);
        return;
    }
    gCalls.fetch_add(1, std::memory_order_relaxed);
    ViewMatrices m;
    if (!ReadView(view, &m)) {
        gOriginal(data, proxy, pdi, view, dpg);
        return;
    }
    auto const window = fw::WindowInverse(m.tv, m.p);
    if (!window || !fw::Differs(*window, m.inv)) {
        gSame.fetch_add(1, std::memory_order_relaxed);   // mode 1 (or a view we can't invert): the engine's own
        gOriginal(data, proxy, pdi, view, dpg);
        return;
    }
    if (!WriteInverse(view, window->data())) {
        gOriginal(data, proxy, pdi, view, dpg);
        return;
    }
    write_journal::Wrote("FlareWindow.inverse", static_cast<std::uint8_t*>(view) + kInvTranslatedViewProj, sizeof(float) * 16);
    gRewritten.fetch_add(1, std::memory_order_relaxed);
    CallWithWindow(data, proxy, pdi, view, dpg, m.inv.data());
}

// The setting: the ini's key, then (development builds) the measure file's, which wins.
bool ReadEnabled() {
    namespace ini = mevr::ini;
    bool enabled = false;
    std::wstring const& path = mevr::host::SettingsPath();
    if (!path.empty()) {
        ini::File const file = ini::Read(std::filesystem::path(path)).file;
        enabled = ini::GetNumber(file, "Stereo", "flareWindow").value_or(0.0) != 0.0;
    }
    wchar_t appData[MAX_PATH] = {};
    if (dev_channels::kDevelopmentBuild && GetEnvironmentVariableW(L"LOCALAPPDATA", appData, MAX_PATH) > 0) {
        ini::Loaded const measure = ini::Read(std::filesystem::path(appData) / L"MEVR" / L"stereo.measure");
        if (auto const value = ini::GetNumber(measure.file, "Stereo", "flareWindow")) {
            enabled = *value != 0.0;
        }
    }
    return enabled;
}

} // namespace

bool Install(hook::Context& context) {
    if (gInstalled.load(std::memory_order_acquire)) {
        return true;
    }
    std::uint32_t const rva = engine::addresses::kLensFlareReflections;
    if (rva == 0) {
        log::Info("flare window: stands down: this game has no row for its lens flares' reflection pass");
        return false;
    }
    if (exe::Initialized()) {
        exe::Result const* const found = exe::Find(rva);
        if (found == nullptr || !exe::AtItsRva(found->status)) {
            log::Warn("flare window: stands down: the lens flares' reflection pass isn't at its RVA");
            return false;
        }
    }
    auto* const target = context.ResolveTyped<RenderReflections>(rva);
    bool const installed = target != nullptr &&
        hook::Install(context, "FLensFlareDynamicData::RenderReflections", target, &RenderReflectionsHook, gOriginal);
    gInstalled.store(installed, std::memory_order_release);
    gEnabled.store(installed && ReadEnabled(), std::memory_order_relaxed);
    log::Info("flare window: {}; lens flares' reflections in mode 2 {} ([Stereo] flareWindow = {})",
        installed ? "hooked the reflection pass" : "the hook failed",
        gEnabled.load(std::memory_order_relaxed) ? "placed through the CPU's own window"
                                                 : "as the engine places them (through the eye's inverse)",
        gEnabled.load(std::memory_order_relaxed) ? 1 : 0);
    return installed;
}

void Tick() {
    if (!gInstalled.load(std::memory_order_acquire)) {
        return;
    }
    static ULONGLONG nextRead = 0, nextReport = 0;
    static std::uint64_t lastCalls = 0, lastRewritten = 0, lastFaults = 0;
    ULONGLONG const now = GetTickCount64();
    if (now >= nextRead) {
        nextRead = now + 1000;
        bool const enabled = ReadEnabled();
        if (enabled != gEnabled.exchange(enabled, std::memory_order_relaxed)) {
            log::Info("flare window: lens flares' reflections in mode 2 {}", enabled
                ? "placed through the CPU's own window (the view's inverse swapped for the pass)"
                : "as the engine places them (through the eye's inverse)");
        }
    }
    if (now < nextReport) {
        return;
    }
    nextReport = now + 10000;
    std::uint64_t const calls = gCalls.load(std::memory_order_relaxed);
    std::uint64_t const rewritten = gRewritten.load(std::memory_order_relaxed);
    std::uint64_t const faults = gFaults.load(std::memory_order_relaxed);
    if (calls != lastCalls || rewritten != lastRewritten || faults != lastFaults) {
        log::Info("flare window: {} reflection passes, {} through the window's inverse, {} already the window's (mode 1), "
                  "{} faults", calls, rewritten, gSame.load(std::memory_order_relaxed), faults);
        lastCalls = calls;
        lastRewritten = rewritten;
        lastFaults = faults;
    }
}

std::string StatsJson() {
    return std::format("{{\"installed\":{},\"enabled\":{},\"calls\":{},\"rewritten\":{},\"same\":{},\"faults\":{}}}",
        gInstalled.load(std::memory_order_acquire) ? "true" : "false",
        gEnabled.load(std::memory_order_relaxed) ? "true" : "false", gCalls.load(std::memory_order_relaxed),
        gRewritten.load(std::memory_order_relaxed), gSame.load(std::memory_order_relaxed),
        gFaults.load(std::memory_order_relaxed));
}

} // namespace mevr::host::flare_window
