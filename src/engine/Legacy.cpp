// SPDX-License-Identifier: MIT
#include "engine/Legacy.hpp"

// Only the LE2 diagnostic host has this file's code (engine/Legacy.hpp): every other build compiles it to nothing, so
// a players' build links no LExSDKv2 through it.
#if defined(MEVR_LE2_DIAGNOSTIC) && defined(MEVR_GAME_LE2)

#if defined(MEVR_NO_LESDK)
#error "The LE2 diagnostic host reads the engine through LExSDKv2: configure it with MEVR_LE2_LESDK=ON (never a players' build)"
#endif

#include "core/Hook.hpp"
#include "core/Log.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <Windows.h>

#include <cstdint>

namespace mevr::engine::legacy {
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

bool Init(hook::Context& context) {
    // LExSDKv2's resolver for its BUILTIN_*_RVA (FromOffset: the main module's base plus the offset; the interface
    // pointer is only kept, never called for those).
    ::LESDK::Initializer init{reinterpret_cast<ISharedProxyInterface*>(context.Spi()), MEVR_GAME_NAME_A "MEVRlegacy"};
    // Its base comes from a module snapshot (CreateToolhelp32Snapshot), which fails now and then while another DLL
    // loads (EA's overlay, 2026-11-14), and its checks compile out in release: every offset then resolved to itself
    // and the first SFXName called 0x78640 (LE2's SFXName init RVA). The exe's base is ours to give.
    void* const exeBase = reinterpret_cast<void*>(context.ModuleBase());
    if (init.ModuleBase != exeBase) {
        log::Warn("LExSDKv2's module snapshot gave the exe's base as {} (the exe is at {}): using ours", init.ModuleBase,
                  exeBase);
        init.ModuleBase = exeBase;
    }
    bool ok = true;
    ok &= Resolve(init, BUILTIN_GMALLOC_RVA, GMalloc, "GMalloc");
    ok &= Resolve(init, BUILTIN_GOBJECTS_RVA, UObject::GObjObjects, "GObjObjects");
    ok &= Resolve(init, BUILTIN_SFXNAMEPOOLS_RVA, SFXName::GBioNamePools, "SFXName pools");
    ok &= Resolve(init, BUILTIN_SFXNAMEINIT_RVA, SFXName::GInitMethod, "SFXName init");
    ok &= Resolve(init, BUILTIN_GENGINE_RVA, GEngine, "GEngine");
    ok &= Resolve(init, BUILTIN_GWORLD_RVA, GWorld, "GWorld");
    return ok;
}

void const* World() {
    return GWorld != nullptr ? *GWorld : nullptr;
}

std::int32_t ObjectCount() {
    return UObject::GObjObjects != nullptr ? static_cast<std::int32_t>(UObject::GObjObjects->Count()) : 0;
}

ue3::Object* LocalPlayerController() {
    UEngine* const engine = (GEngine != nullptr) ? *GEngine : nullptr;
    if (engine == nullptr || engine->GamePlayers.Count() == 0) {
        return nullptr;
    }
    ULocalPlayer* const player = engine->GamePlayers.GetData()[0];
    return player != nullptr ? reinterpret_cast<ue3::Object*>(player->Actor) : nullptr;
}

} // namespace mevr::engine::legacy

#endif  // MEVR_LE2_DIAGNOSTIC
