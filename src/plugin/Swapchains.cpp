// SPDX-License-Identifier: MIT
// Every swapchain our quads make, through one place (plugin/Swapchains.hpp): room made when the runtime refuses one.

#include "plugin/Swapchains.hpp"

#include "core/Log.hpp"
#include "core/vr/SwapchainBudget.hpp"
#include "plugin/MevrMenu.hpp"
#include "plugin/TestLoop.hpp"

#include <Windows.h>
#include <d3d11.h>

#ifndef XR_USE_PLATFORM_WIN32
#define XR_USE_PLATFORM_WIN32
#endif
#ifndef XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D11
#endif
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <format>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace mevr::host::swapchains {
namespace {

namespace budget = vr::swapchain_budget;

constexpr std::uint64_t kNothingLogMs = 10000;

struct Entry {
    std::string name;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t createdMs = 0;
    std::uint64_t shownMs = 0;  // last in a submitted layer; 0 never
    Need need = Need::Normal;
    std::function<void()> evict;

    std::uint64_t LastUse() const { return std::max(createdMs, shownMs); }
};

struct Wanted {
    std::string name;
    Need need = Need::Normal;
};

std::mutex gMutex;  // StatusJson reads from other threads; never held while an owner's evict runs
XrSession gSession = XR_NULL_HANDLE;
std::unordered_map<XrSwapchain, Entry> gEntries;  // ours
std::unordered_set<XrSwapchain> gForeign;         // seen in layers, not ours: Halcyon's
std::vector<Wanted> gWanted;                      // refused (the runtime's limit) since the last frame's end
std::unordered_map<std::string, std::uint64_t> gRefused;  // name -> its last refusal; erased once made
std::uint64_t gFreedMs = 0;
std::uint64_t gNothingLoggedMs = 0;
unsigned gRefusals = 0;
unsigned gFreedCount = 0;

// "name WxH shown 3.1 s ago, ..." by last use, the most recent first. Under gMutex.
std::string LiveList(std::uint64_t now) {
    std::vector<Entry const*> list;
    list.reserve(gEntries.size());
    for (auto const& [handle, e] : gEntries) {
        list.push_back(&e);
    }
    std::sort(list.begin(), list.end(), [](Entry const* a, Entry const* b) { return a->LastUse() > b->LastUse(); });
    std::string text;
    for (Entry const* e : list) {
        if (!text.empty()) {
            text += ", ";
        }
        text += std::format("{} {}x{}", e->name, e->width, e->height);
        if (e->shownMs == 0) {
            text += std::format(" never shown (made {:.1f} s ago)", static_cast<double>(now - e->createdMs) / 1000.0);
        } else {
            text += std::format(" shown {:.1f} s ago", static_cast<double>(now - e->shownMs) / 1000.0);
        }
    }
    return text.empty() ? std::string("none") : text;
}

void Note(XrSwapchain swapchain, std::uint64_t now) {
    if (swapchain == XR_NULL_HANDLE) {
        return;
    }
    auto it = gEntries.find(swapchain);
    if (it != gEntries.end()) {
        it->second.shownMs = now;
    } else {
        gForeign.insert(swapchain);
    }
}

// [Debug] swapchainCap in ASI\LE1MEVR.ini (0 off, the default): refuse ours past this many as the runtime would
// (XR_ERROR_LIMIT_REACHED), so freeing can be tested without filling the runtime. Read every 2 s.
std::uint32_t TestCap() {
    static std::uint64_t readMs = 0;
    static std::uint32_t cap = 0;
    std::uint64_t const now = GetTickCount64();
    if (readMs != 0 && now - readMs < 2000) {
        return cap;
    }
    readMs = now;
    std::wstring const& path = MELEVR::MevrBridge::SettingsPath();
    std::uint32_t const value = path.empty() ? 0u : GetPrivateProfileIntW(L"Debug", L"swapchainCap", 0, path.c_str());
    if (value != cap) {
        log::Info("swapchains: [Debug] swapchainCap = {}{}", value, value != 0 ? ": ours past it are refused as the runtime would" : " (off)");
        cap = value;
    }
    return cap;
}

struct Victim {
    XrSwapchain handle = XR_NULL_HANDLE;
    std::string name;
    std::uint64_t idleMs = 0;
    std::function<void()> evict;
};

} // namespace

XrResult Create(XrSession session, XrSwapchainCreateInfo const& info, char const* name, std::function<void()> evict, XrSwapchain& handle,
    std::vector<ID3D11Texture2D*>& images, Need need) {
    handle = XR_NULL_HANDLE;
    images.clear();
    std::string const key = name != nullptr ? name : "?";
    if (session == XR_NULL_HANDLE) {
        return XR_ERROR_HANDLE_INVALID;
    }
    if (session != gSession) {
        OnSessionCreated(session);
    }
    std::uint32_t const cap = TestCap();
    bool capped = false;
    {
        std::lock_guard lock(gMutex);
        auto it = gRefused.find(key);
        if (it != gRefused.end() && !budget::RetryDue(it->second, gFreedMs, GetTickCount64())) {
            return XR_ERROR_LIMIT_REACHED;  // refused just now and nothing freed since: don't ask the runtime every frame
        }
        capped = cap != 0 && gEntries.size() >= cap;
    }
    XrResult r = capped ? XR_ERROR_LIMIT_REACHED : xrCreateSwapchain(session, &info, &handle);
    std::uint32_t count = 0;
    if (XR_SUCCEEDED(r)) {
        r = xrEnumerateSwapchainImages(handle, 0, &count, nullptr);
    }
    std::vector<XrSwapchainImageD3D11KHR> list(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
    if (XR_SUCCEEDED(r) && count > 0) {
        r = xrEnumerateSwapchainImages(handle, count, &count, reinterpret_cast<XrSwapchainImageBaseHeader*>(list.data()));
    }
    if (XR_SUCCEEDED(r) && count == 0) {
        r = XR_ERROR_RUNTIME_FAILURE;
    }
    std::uint64_t const now = GetTickCount64();
    if (XR_FAILED(r)) {
        if (handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(handle);
            handle = XR_NULL_HANDLE;
        }
        std::lock_guard lock(gMutex);
        ++gRefusals;
        if (r == XR_ERROR_LIMIT_REACHED) {
            gWanted.push_back({key, need});
        }
        bool const first = gRefused.find(key) == gRefused.end();
        gRefused[key] = now;
        if (first) {
            log::Error("swapchains: {} {}x{} refused (XrResult {}{}); {} of ours live, {} of Halcyon's seen; ours: {}", key, info.width,
                info.height, static_cast<int>(r),
                capped                          ? ", by [Debug] swapchainCap; idle ones are freed at the frame's end"
                : r == XR_ERROR_LIMIT_REACHED   ? ", XR_ERROR_LIMIT_REACHED: the runtime's limit; idle ones are freed at the frame's end"
                                                : "",
                gEntries.size(), gForeign.size(), LiveList(now));
        }
        return r;
    }
    for (auto const& image : list) {
        images.push_back(image.texture);
    }
    std::lock_guard lock(gMutex);
    gEntries[handle] = Entry{key, info.width, info.height, now, 0, need, std::move(evict)};
    auto refused = gRefused.find(key);
    if (refused != gRefused.end()) {
        log::Info("swapchains: {} {}x{} made after its refusal; {} of ours live", key, info.width, info.height, gEntries.size());
        gRefused.erase(refused);
    } else {
        log::Info("swapchains: {} {}x{} made, {} images; {} of ours live", key, info.width, info.height, count, gEntries.size());
    }
    return XR_SUCCESS;
}

void Destroy(XrSwapchain handle) {
    if (handle == XR_NULL_HANDLE) {
        return;
    }
    {
        std::lock_guard lock(gMutex);
        gEntries.erase(handle);
    }
    testloop::ForgetSwapchain(handle);
    xrDestroySwapchain(handle);
}

void Forget(XrSwapchain handle) {
    if (handle == XR_NULL_HANDLE) {
        return;
    }
    {
        std::lock_guard lock(gMutex);
        gEntries.erase(handle);
    }
    testloop::ForgetSwapchain(handle);
}

bool RetryDue(std::uint64_t failedMs) {
    std::lock_guard lock(gMutex);
    return budget::RetryDue(failedMs, gFreedMs, GetTickCount64());
}

void OnSessionCreated(XrSession session) {
    std::lock_guard lock(gMutex);
    if (session == gSession) {
        return;
    }
    gSession = session;
    gEntries.clear();  // they went with the old session
    gForeign.clear();
    gWanted.clear();
    gRefused.clear();
}

void AfterEndFrame(XrFrameEndInfo const& info) {
    std::uint64_t const now = GetTickCount64();
    std::vector<Victim> victims;
    std::string wanted;
    {
        std::lock_guard lock(gMutex);
        for (std::uint32_t i = 0; i < info.layerCount; ++i) {
            XrCompositionLayerBaseHeader const* layer = info.layers != nullptr ? info.layers[i] : nullptr;
            if (layer == nullptr) {
                continue;
            }
            switch (layer->type) {
            case XR_TYPE_COMPOSITION_LAYER_QUAD:
                Note(reinterpret_cast<XrCompositionLayerQuad const*>(layer)->subImage.swapchain, now);
                break;
            case XR_TYPE_COMPOSITION_LAYER_PROJECTION: {
                auto const* projection = reinterpret_cast<XrCompositionLayerProjection const*>(layer);
                for (std::uint32_t v = 0; projection->views != nullptr && v < projection->viewCount; ++v) {
                    Note(projection->views[v].subImage.swapchain, now);
                }
                break;
            }
            case XR_TYPE_COMPOSITION_LAYER_CYLINDER_KHR:
                Note(reinterpret_cast<XrCompositionLayerCylinderKHR const*>(layer)->subImage.swapchain, now);
                break;
            case XR_TYPE_COMPOSITION_LAYER_EQUIRECT2_KHR:
                Note(reinterpret_cast<XrCompositionLayerEquirect2KHR const*>(layer)->subImage.swapchain, now);
                break;
            default:
                break;
            }
        }
        if (gWanted.empty()) {
            return;
        }
        bool essential = false;
        for (Wanted const& w : gWanted) {
            essential = essential || w.need == Need::Essential;
            if (wanted.find(w.name) == std::string::npos) {
                wanted += wanted.empty() ? w.name : ", " + w.name;
            }
        }
        // Ours idle a second, the least recently shown first; for an Essential create, then ours shown 0.1 s ago or more
        // (not Essential ones: the calibration's own). core/vr/SwapchainBudget.
        std::vector<XrSwapchain> handles;
        std::vector<budget::Live> live;
        for (auto const& [handle, e] : gEntries) {
            handles.push_back(handle);
            live.push_back({e.createdMs, e.shownMs, e.need == Need::Essential});
        }
        for (std::size_t i : budget::Choose(live, now, gWanted.size(), essential)) {
            Entry const& e = gEntries[handles[i]];
            victims.push_back({handles[i], e.name, now - e.LastUse(), e.evict});
        }
        gWanted.clear();
        if (victims.empty()) {
            if (now - gNothingLoggedMs >= kNothingLogMs) {
                gNothingLoggedMs = now;
                log::Info("swapchains: nothing of ours idle to free for {}; {} of ours live: {}", wanted, gEntries.size(), LiveList(now));
            }
            return;
        }
    }
    // The owners let go through their own paths (which call Destroy); one that's already gone (a pair freed with its
    // other eye) is skipped.
    std::string freed;
    unsigned count = 0;
    for (Victim& v : victims) {
        bool present = false;
        {
            std::lock_guard lock(gMutex);
            present = gEntries.find(v.handle) != gEntries.end();
        }
        if (!present) {
            continue;
        }
        if (v.evict) {
            v.evict();
        }
        bool still = false;
        {
            std::lock_guard lock(gMutex);
            still = gEntries.find(v.handle) != gEntries.end();
        }
        if (still) {
            Destroy(v.handle);  // the owner forgot its handle without destroying it
        }
        ++count;
        freed += std::format("{}{} (idle {:.1f} s)", freed.empty() ? "" : ", ", v.name, static_cast<double>(v.idleMs) / 1000.0);
    }
    std::lock_guard lock(gMutex);
    gFreedMs = now;
    gFreedCount += count;
    log::Info("swapchains: freed {} for {}; {} of ours live", freed, wanted, gEntries.size());
}

std::string StatusJson() {
    std::lock_guard lock(gMutex);
    std::uint64_t const now = GetTickCount64();
    std::string live;
    for (auto const& [handle, e] : gEntries) {
        live += std::format("{}{{\"name\": \"{}\", \"w\": {}, \"h\": {}, \"idleMs\": {}}}", live.empty() ? "" : ", ", e.name, e.width, e.height,
            now - e.LastUse());
    }
    return std::format("{{\"ours\": {}, \"halcyons\": {}, \"refusals\": {}, \"freed\": {}, \"live\": [{}]}}", gEntries.size(), gForeign.size(),
        gRefusals, gFreedCount, live);
}

} // namespace mevr::host::swapchains
