// SPDX-License-Identifier: MIT
#include "engine/Engine.hpp"

#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

namespace mevr::engine {
namespace {

template <typename T>
bool Resolve(::LESDK::Initializer& init, ::LESDK::Address address, T*& out, char const* name) {
    out = init.ResolveTyped<T>(address);
    if (out == nullptr) {
        log::Error("failed to resolve {}", name);
        return false;
    }
    return true;
}

} // namespace

void const* World() {
    return GWorld != nullptr ? *GWorld : nullptr;
}

bool ResolveGlobals(::LESDK::Initializer& init) {
    bool ok = true;
    ok &= Resolve(init, BUILTIN_GMALLOC_RVA, GMalloc, "GMalloc");
    ok &= Resolve(init, BUILTIN_GOBJECTS_RVA, UObject::GObjObjects, "GObjObjects");
    ok &= Resolve(init, BUILTIN_SFXNAMEPOOLS_RVA, SFXName::GBioNamePools, "SFXName pools");
    ok &= Resolve(init, BUILTIN_SFXNAMEINIT_RVA, SFXName::GInitMethod, "SFXName init");
    ok &= Resolve(init, BUILTIN_GENGINE_RVA, GEngine, "GEngine");
    ok &= Resolve(init, BUILTIN_GWORLD_RVA, GWorld, "GWorld");
    return ok;
}

} // namespace mevr::engine
