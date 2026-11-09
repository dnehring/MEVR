// SPDX-License-Identifier: MIT
#include "plugin/MotionBlurGuard.hpp"

#include "core/Exe.hpp"
#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "engine/Addresses.hpp"
#include "plugin/MevrMenu.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>

namespace mevr::host::motion_blur_guard {
namespace {
namespace address = engine::addresses::le1;

// Native signature: this, scene, DPG/render arguments, view, final argument, output flags.
// All six argument slots are integer/pointer registers or stack slots; forward them unchanged.
using Render = std::uintptr_t(void*, void*, void*, void*, std::uintptr_t, void*);
Render* gOriginal = nullptr;
Render* gSharpRender = nullptr;
struct Pair { void* blur; void* sharp; };
std::array<Pair, 5> gPairs{};  // published before the hook is enabled; immutable thereafter
std::atomic<bool> gInstalled{false};
std::atomic<std::uint64_t> gConverted{0}, gUnknown{0};
std::array<std::atomic<std::uint64_t>, 5> gVariants{};

// The complete ten-slot vtables, normalized to RVAs, are fingerprinted. This rejects another
// plugin's modified shader selectors as well as a mismatched exe. Function prologues alone
// cannot establish that swapping these otherwise identically laid-out specializations is safe.
std::uint64_t Fingerprint(std::uintptr_t base, void const* table) {
    auto const* entries = static_cast<std::uintptr_t const*>(table);
    std::uint64_t hash = 14695981039346656037ull;
    for (std::size_t slot = 0; slot < 10; ++slot) {
        std::uint64_t const rva = entries[slot] - base;
        for (unsigned byte = 0; byte < 8; ++byte) {
            hash ^= (rva >> (byte * 8)) & 0xff;
            hash *= 1099511628211ull;
        }
    }
    return hash;
}

std::uintptr_t RenderHook(void* proxy, void* scene, void* arg3, void* view, std::uintptr_t arg5, void* arg6) {
    void* table = nullptr;
    std::memcpy(&table, proxy, sizeof table);
    for (std::size_t i = 0; i < gPairs.size(); ++i) {
        if (table == gPairs[i].blur) {
            // This proxy is owned by the render thread. Its paired constructors differ only
            // in vtable identity; fields, allocation size and destructor are unchanged.
            // Replace the whole dispatch table before entering the no-blur render body: its
            // virtual shader-selection calls must also use the no-blur specialization.
            // Keep that identity for this proxy's remaining lifetime, including later eyes.
            InterlockedExchangePointer(static_cast<void* volatile*>(proxy), gPairs[i].sharp);
            write_journal::Wrote("MotionBlurGuard.proxy", proxy, sizeof(void*));
            gConverted.fetch_add(1, std::memory_order_relaxed);
            gVariants[i].fetch_add(1, std::memory_order_relaxed);
            return gSharpRender(proxy, scene, arg3, view, arg5, arg6);
        }
    }
    // A changed/foreign class is not safe to reinterpret. The existing settings workaround
    // remains available. Report from the game thread, never flush logs in a render callback.
    gUnknown.fetch_add(1, std::memory_order_relaxed);
    return gOriginal(proxy, scene, arg3, view, arg5, arg6);
}

bool Verified(std::uint32_t rva) {
    exe::Result const* result = exe::Find(rva);
    return result && result->status == exe::Status::AtRva;
}
} // namespace

bool Install(hook::Context& context) {
    if (gInstalled.load(std::memory_order_acquire)) return true;
    if (GetPrivateProfileIntW(L"Performance", L"motionBlurGuard", 1,
                             MELEVR::MevrBridge::SettingsPath().c_str()) == 0) {
        log::Info("motion blur guard: disabled at startup for diagnosis");
        return false;
    }
    bool valid = Verified(address::kMotionBlurProxyRender) && Verified(address::kNoBlurProxyRender);
    std::uintptr_t const base = context.ModuleBase();
    for (std::size_t i = 0; i < gPairs.size(); ++i) {
        auto const& spec = address::kMotionBlurProxyPairs[i];
        valid = valid && Verified(spec.blur) && Verified(spec.sharp);
        if (!valid) break;
        gPairs[i] = {context.Resolve(spec.blur), context.Resolve(spec.sharp)};
        valid = gPairs[i].blur && gPairs[i].sharp &&
                Fingerprint(base, gPairs[i].blur) == spec.blurFingerprint &&
                Fingerprint(base, gPairs[i].sharp) == spec.sharpFingerprint;
        if (!valid) break;
    }
    if (!valid) {
        log::Warn("motion blur guard: native code or proxy vtables differ; guard not installed, settings fallback retained");
        return false;
    }
    auto* target = context.ResolveTyped<Render>(address::kMotionBlurProxyRender);
    gSharpRender = context.ResolveTyped<Render>(address::kNoBlurProxyRender);
    // Both the no-blur target and every mapping exist before the detour can run. core/Hook
    // publishes gOriginal before enabling it. Never remove this host-owned hook at runtime.
    bool const installed = target && gSharpRender &&
        hook::Install(context, "MotionBlurProxy::Render", target, &RenderHook, gOriginal);
    gInstalled.store(installed, std::memory_order_release);
    log::Info("motion blur guard: {} (five paired post-process variants; cached and new proxies; conditional menu-return fallback)",
              installed ? "installed" : "installation failed");
    return installed;
}

bool ProtectsGameplay() {
    return gInstalled.load(std::memory_order_acquire) && gUnknown.load(std::memory_order_relaxed) == 0;
}

void Tick() {
    static std::uint64_t lastConverted = 0, lastUnknown = 0;
    static ULONGLONG nextReport = 0;
    ULONGLONG const now = GetTickCount64();
    if (now < nextReport) return;
    nextReport = now + 10000;
    auto const converted = gConverted.load(std::memory_order_relaxed);
    auto const unknown = gUnknown.load(std::memory_order_relaxed);
    if (converted != lastConverted || unknown != lastUnknown) {
        log::Info("motion blur guard: {} proxies converted to no-blur; {} unknown proxies left to settings fallback",
                  converted, unknown);
        lastConverted = converted;
        lastUnknown = unknown;
    }
}

std::string StatsJson() {
    return std::format("{{\"installed\":{},\"protectsGameplay\":{},\"converted\":{},\"unknown\":{},\"variants\":[{},{},{},{},{}]}}",
        gInstalled.load(std::memory_order_acquire) ? "true" : "false",
        ProtectsGameplay() ? "true" : "false",
        gConverted.load(std::memory_order_relaxed), gUnknown.load(std::memory_order_relaxed),
        gVariants[0].load(std::memory_order_relaxed), gVariants[1].load(std::memory_order_relaxed),
        gVariants[2].load(std::memory_order_relaxed), gVariants[3].load(std::memory_order_relaxed),
        gVariants[4].load(std::memory_order_relaxed));
}
} // namespace mevr::host::motion_blur_guard
