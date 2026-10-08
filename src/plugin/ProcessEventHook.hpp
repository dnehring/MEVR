#pragma once

#include <cstdint>

class UFunction;
class UObject;

namespace LESDK {
class Initializer;
}

// UObject::ProcessEvent, hooked once by the host at attach and never removed. The mod's handlers
// run through a callback the host swaps on hot reload. Halcyon's renderer hooks the same function
// with its own MinHook later on top of ours; removing ours underneath it (the mod used to, on every
// reload and at exit) would undo its hook or crash LEBinkProxy's hook chain walk.
namespace mevr::host::process_event {

using Callback = void (*)(UObject* context, UFunction* function, void* parms, void* result);

// How long clearing the callback waits for calls still inside it, by default.
inline constexpr std::uint32_t kDrainWaitMs = 500;

bool Install(::LESDK::Initializer& init);

// Runs after the original, on whichever thread called ProcessEvent. Clearing waits (up to
// `waitMs`; 0 doesn't wait) until no thread is inside the old callback, so its module can be
// unloaded afterwards. False if one still is: that module must stay loaded. Not to be called from
// inside the callback.
bool SetCallback(Callback callback, std::uint32_t waitMs = kDrainWaitMs);

} // namespace mevr::host::process_event
