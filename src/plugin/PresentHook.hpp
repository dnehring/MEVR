// SPDX-License-Identifier: MIT
#pragma once

struct IDXGISwapChain;

namespace LESDK {
class Initializer;
}

namespace mevr::host::present {

// Runs on the game's render thread, just before each IDXGISwapChain::Present.
using Callback = void (*)(IDXGISwapChain* swapChain);

// Finds IDXGISwapChain::Present through a throwaway device and hooks it via the loader's SPI.
bool Install(::LESDK::Initializer& init);

// Sets or clears the callback. Clearing blocks until any in-flight callback returns, so after
// SetCallback(nullptr) it's safe to unload the module that owned the old callback.
void SetCallback(Callback callback);

} // namespace mevr::host::present
