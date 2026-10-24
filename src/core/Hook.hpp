// SPDX-License-Identifier: MIT
#pragma once

// Detours for the host and the mod alike (research/34 stage 4), through one of two backends:
//   - the SPI interface (loader/SpiAbi.hpp) of whoever loaded the ASI: our own loader (src/loader) or ME3Tweaks'
//     LEBinkProxy. Their shared MinHook keeps one hook per name, so several plugins stack on one target; names are
//     global, so ours carry the owner's prefix: "<owner>_<name>" ("LE1MEVR_UObject::ProcessEvent",
//     "LE1MEVRmod_UObject::CallFunction"), exactly as before stage 4 (LExSDKv2's Initializer built them the same way).
//   - our own MinHook (third_party/minhook) when there is no SPI interface (a context made with a null one). It
//     stacks with other hookers in install order, like Halcyon's private MinHook already does on ProcessEvent;
//     removing a hook another hooker sits on top of breaks their chain, so only remove hooks nobody else can have
//     hooked after us (CLAUDE.md: the host never removes its hooks).
//
// Install writes `original` before the detour goes live. A detour entered at once (the host attaches on the
// loader's thread while the game runs; the mod hooks functions other threads call) never calls through null: both
// backends write the trampoline through `original` in MinHook's create step, before the enable step. (LExSDKv2's
// Initializer::InstallHook handed it back only after the call returned: core/Hook never did that.)
//
// Uninstall reports failure (logged): a hook that couldn't be removed is still live, so the code it runs and the
// original it calls must stay loaded.

#include "core/Exe.hpp"
#include "loader/SpiAbi.hpp"

#include <cstdint>
#include <string>

namespace mevr::hook {

namespace spi = loader::spi;

class Context {
public:
    // `owner` prefixes every hook name: MEVR_GAME_NAME_A "MEVR" for the host, ... "MEVRmod" for the mod.
    Context(spi::Interface* spi, std::string owner) : spi_(spi), owner_(std::move(owner)) {}

    // The loader's interface, or null (our MinHook).
    spi::Interface* Spi() const {
        return spi_;
    }
    std::string const& Owner() const {
        return owner_;
    }
    char const* Backend() const {
        return spi_ != nullptr ? "the loader's SPI" : "our MinHook";
    }

    // The exe's base and its RVAs, checked against our RVA table (core/Exe).
    std::uintptr_t ModuleBase() const {
        return exe::Base();
    }
    void* Resolve(std::uintptr_t rva) const {
        return exe::Resolve(rva);
    }
    template <typename T>
    T* ResolveTyped(std::uintptr_t rva) const {
        return exe::ResolveTyped<T>(rva);
    }

    // "<owner>_<name>".
    std::string FullName(char const* name) const {
        return std::string(owner_).append("_").append(name);
    }

private:
    spi::Interface* spi_ = nullptr;
    std::string owner_;
};

// `original` is written before the detour is enabled; null again if the install failed.
bool Install(Context& context, char const* name, void* target, void* detour, void** original);

template <typename Function>
bool Install(Context& context, char const* name, Function* target, Function* detour, Function*& original) {
    return Install(context, name, reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
        reinterpret_cast<void**>(&original));
}

// False (logged) if it couldn't be removed: the detour stays live, so neither it nor the original pointer it calls
// may go away.
bool Uninstall(Context& context, char const* name);

} // namespace mevr::hook
