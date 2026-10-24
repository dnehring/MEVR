// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>

namespace mevr::hook {
class Context;
}

namespace mevr::ue3 {
struct Object;
struct Function;
}

// UObject::CallFunction: how UnrealScript calls another function. Script-to-script calls (most
// gameplay logic, e.g. a weapon asking the controller for its aim) never pass through
// ProcessEvent, which only sees calls made from native code. Post-handlers here can rewrite a
// function's return value for those calls.
namespace mevr::engine::call_function {

// Runs after the original call. `result` points at the return value (or is null).
using Handler = void (*)(ue3::Object* context, ue3::Function* function, void* result);

// Register before Install; the table is read without locking while hooked.
void AddPostHandler(ue3::Function* function, Handler handler);
void ClearHandlers();

// Crash breadcrumbs (HostServices v18, research/36): called around each handler (begin's token goes to end).
// Null (an older host): not recorded.
using BeginCrumb = std::uint64_t (*)(ue3::Function* function, ue3::Object* object);
using EndCrumb = void (*)(std::uint64_t token);
void SetBreadcrumbs(BeginCrumb begin, EndCrumb end);

bool Install(hook::Context& init);
// False: the hook couldn't be removed, so this DLL must stay loaded.
bool Uninstall(hook::Context& init);

} // namespace mevr::engine::call_function
