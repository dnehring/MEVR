// SPDX-License-Identifier: MIT
#include "core/Hook.hpp"

#include "core/Log.hpp"

#include <MinHook.h>

#include <mutex>
#include <string>
#include <vector>

namespace mevr::hook {
namespace {

int Code(spi::Return rc) {
    return static_cast<int>(rc);
}

// Our MinHook's hooks by full name (MinHook keys them by target). One MinHook per DLL: in the LE1 host it is the
// one Halcyon's renderer uses too (one static library), so a target it hooked can't be hooked here as well.
struct Owned {
    std::string name;
    void* target = nullptr;
};
std::mutex gLock;
std::vector<Owned> gOwned;
bool gMinHookReady = false;

bool InstallMinHook(std::string const& fullName, void* target, void* detour, void** original) {
    std::lock_guard const lock(gLock);
    for (Owned const& owned : gOwned) {
        if (owned.name == fullName) {
            log::Error("hook {}: already installed", fullName);
            return false;
        }
    }
    if (!gMinHookReady) {
        MH_STATUS const init = MH_Initialize();
        if (init != MH_OK && init != MH_ERROR_ALREADY_INITIALIZED) {
            log::Error("hook {}: MinHook didn't start ({})", fullName, MH_StatusToString(init));
            return false;
        }
        gMinHookReady = true;
    }
    MH_STATUS status = MH_CreateHook(target, detour, original);  // writes the trampoline before anything is live
    if (status != MH_OK) {
        log::Error("hook {}: MinHook refused it ({})", fullName, MH_StatusToString(status));
        return false;
    }
    status = MH_EnableHook(target);
    if (status != MH_OK) {
        log::Error("hook {}: MinHook couldn't enable it ({})", fullName, MH_StatusToString(status));
        MH_RemoveHook(target);
        return false;
    }
    gOwned.push_back({fullName, target});
    return true;
}

bool UninstallMinHook(std::string const& fullName) {
    std::lock_guard const lock(gLock);
    for (auto it = gOwned.begin(); it != gOwned.end(); ++it) {
        if (it->name != fullName) {
            continue;
        }
        MH_STATUS status = MH_DisableHook(it->target);
        if (status == MH_OK) {
            status = MH_RemoveHook(it->target);
        }
        if (status != MH_OK) {
            log::Error("hook {}: could not be removed ({}); it stays live", fullName, MH_StatusToString(status));
            return false;
        }
        gOwned.erase(it);
        return true;
    }
    log::Error("hook {}: not installed", fullName);
    return false;
}

} // namespace

bool Install(Context& context, char const* name, void* target, void* detour, void** original) {
    *original = nullptr;
    if (target == nullptr || detour == nullptr) {
        log::Error("hook {}: nothing to hook (target {}, detour {})", name, target, detour);
        return false;
    }
    std::string const fullName = context.FullName(name);
    if (context.Spi() == nullptr) {
        if (!InstallMinHook(fullName, target, detour, original)) {
            *original = nullptr;
            return false;
        }
    } else {
        spi::Return const rc = context.Spi()->InstallHook(fullName.c_str(), target, detour, original);
        if (rc != spi::Return::Success) {
            *original = nullptr;  // the detour never went live (or the loader left it created, disabled)
            log::Error("hook {}: the loader refused it (SPI code {})", fullName, Code(rc));
            return false;
        }
    }
    if (*original == nullptr) {  // live with nothing to call: take it out again
        log::Error("hook {}: installed without an original; removing it", fullName);
        Uninstall(context, name);
        return false;
    }
    return true;
}

bool Uninstall(Context& context, char const* name) {
    std::string const fullName = context.FullName(name);
    if (context.Spi() == nullptr) {
        return UninstallMinHook(fullName);
    }
    spi::Return const rc = context.Spi()->UninstallHook(fullName.c_str());
    if (rc != spi::Return::Success) {
        log::Error("hook {}: could not be removed (SPI code {}); it stays live", fullName, Code(rc));
        return false;
    }
    return true;
}

} // namespace mevr::hook
