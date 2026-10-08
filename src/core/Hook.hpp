#pragma once

namespace LESDK {
class Initializer;
}

// Detours through LEBinkProxy's shared hook manager (its SPI), for the host and the mod alike.
// Hook names are built as LESDK::Initializer builds them ("<owner>_<name>").
//
// Install vs LESDK::Initializer::InstallHook: LESDK hands the original back only after the SPI
// call returns, by which time the detour is already live. A detour entered in between (the host
// attaches on LEBinkProxy's own thread while the game runs; the mod hooks functions other threads
// call) would call through a null original. LEBinkProxy's InstallHook writes the trampoline
// through its `original` out-parameter in MinHook's create step, before the enable step, so
// passing the detour's own pointer variable makes it valid before the first call can arrive.
//
// Uninstall vs LESDK::Initializer::UninstallHook: LESDK drops the SPI's result. A hook that
// couldn't be removed is still live, so the code it runs must stay loaded.
namespace mevr::hook {

// `original` is written before the detour is enabled; null again if the install failed.
bool Install(::LESDK::Initializer& init, char const* name, void* target, void* detour, void** original);

template <typename Function>
bool Install(::LESDK::Initializer& init, char const* name, Function* target, Function* detour, Function*& original) {
    return Install(init, name, reinterpret_cast<void*>(target), reinterpret_cast<void*>(detour),
        reinterpret_cast<void**>(&original));
}

// False (logged) if the SPI couldn't remove it: the detour stays live, so neither it nor the
// original pointer it calls may go away.
bool Uninstall(::LESDK::Initializer& init, char const* name);

} // namespace mevr::hook
