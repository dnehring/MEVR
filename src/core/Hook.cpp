// SPDX-License-Identifier: MIT
#include "core/Hook.hpp"

#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#include <SPI.h>

#include <string>

namespace mevr::hook {
namespace {

std::string FullName(::LESDK::Initializer const& init, char const* name) {
    return std::string(init.OwnerName).append("_").append(name);
}

int Code(SPIReturn rc) {
    return static_cast<int>(rc);
}

} // namespace

bool Install(::LESDK::Initializer& init, char const* name, void* target, void* detour, void** original) {
    *original = nullptr;
    if (target == nullptr || detour == nullptr || init.ProxyInstance == nullptr) {
        log::Error("hook {}: nothing to hook (target {}, detour {})", name, target, detour);
        return false;
    }
    std::string const fullName = FullName(init, name);
    SPIReturn const rc = init.ProxyInstance->InstallHook(fullName.c_str(), target, detour, original);
    if (rc != SPIReturn::Success) {
        *original = nullptr;  // the detour never went live (or LEBinkProxy left it created, disabled)
        log::Error("hook {}: LEBinkProxy refused it (SPI code {})", fullName, Code(rc));
        return false;
    }
    if (*original == nullptr) {  // live with nothing to call: take it out again
        log::Error("hook {}: installed without an original; removing it", fullName);
        Uninstall(init, name);
        return false;
    }
    return true;
}

bool Uninstall(::LESDK::Initializer& init, char const* name) {
    if (init.ProxyInstance == nullptr) {
        return false;
    }
    std::string const fullName = FullName(init, name);
    SPIReturn const rc = init.ProxyInstance->UninstallHook(fullName.c_str());
    if (rc != SPIReturn::Success) {
        log::Error("hook {}: could not be removed (SPI code {}); it stays live", fullName, Code(rc));
        return false;
    }
    return true;
}

} // namespace mevr::hook
