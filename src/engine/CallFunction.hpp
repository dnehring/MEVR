// SPDX-License-Identifier: MIT
#pragma once

#include "core/CallScope.hpp"

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

// Runs before the original call; true skips it. `caller` is the calling script function (the frame's Node), or null.
using SkipHandler = bool (*)(ue3::Object* context, ue3::Function* function, ue3::Function* caller);

// Runs before the original call of a function without parameters (script or native: a native's exec reads none and
// consumes the same EX_EndFunctionParms token): true skips the call, the handler having written its return value into
// `result` (when not null; a bool is 4 bytes). `callerLocals` is the calling frame's Locals, the running function's
// parameters and locals at their properties' offsets (null if the frame can't be read): how a handler sees the arguments
// of the function that asks (e.g. who shot, inside the damage code that asks IsInCover).
using AnswerHandler = bool (*)(ue3::Object* context, ue3::Function* function, ue3::Function* caller, std::uint8_t* callerLocals,
    void* result);

// Runs after the original call of any function (with or without parameters), with the calling frame's Node (the calling
// script function, or null) and Locals (still live: the caller's own frame): how a handler restricts itself to one caller
// and sees that caller's arguments (LE2's VR cover: a native's answer inside SFXGame.ModifyDamage only). `result` as for
// Handler.
using FramePostHandler = void (*)(ue3::Object* context, ue3::Function* function, ue3::Function* caller, std::uint8_t* callerLocals,
    void* result);

// Per-invocation bracket around the existing hook body, including skip/answer/early return and
// exceptions. Enter returns an opaque identity; Leave runs in reverse registration order in __finally.
// Leave must be noexcept and validate any temporary writes before restoring them. Direct ProcessEvent
// calls are outside this seam. Register before Install; false refuses invalid/over-capacity entries.
using ScopedCall = mevr::CallScope<ue3::Object, ue3::Function>;
bool AddScopedHandler(ue3::Function* function, ScopedCall::Enter enter, ScopedCall::Leave leave);

// Register before Install; the table is read without locking while hooked.
void AddPostHandler(ue3::Function* function, Handler handler);
void AddFramePostHandler(ue3::Function* function, FramePostHandler handler);
// Only for a function without parameters (the caller checks with our reflection layer), as AddSkipHandler.
void AddAnswerHandler(ue3::Function* function, AnswerHandler handler);
// Only for a script function without parameters (not native; the caller checks with our reflection layer): a skip
// consumes the call's EX_EndFunctionParms token, as the engine's own parameter loop would, and runs nothing else. If
// that token isn't the next bytecode byte the call runs as normal (logged once).
void AddSkipHandler(ue3::Function* function, SkipHandler handler);
void ClearHandlers();

// A development trace (game/call_trace): sees every script call before it runs, with the frame's Node (the calling
// function, state or class; unchecked: the observer must check it's a listed object before reading it). Null: off, one
// atomic load per call.
using Observer = void (*)(ue3::Object* context, ue3::Function* function, ue3::Function* caller);
void SetObserver(Observer observer);

// Crash breadcrumbs (HostServices v18, research/36): called around each handler (begin's token goes to end).
// Null (an older host): not recorded.
using BeginCrumb = std::uint64_t (*)(ue3::Function* function, ue3::Object* object);
using EndCrumb = void (*)(std::uint64_t token);
void SetBreadcrumbs(BeginCrumb begin, EndCrumb end);

bool Install(hook::Context& init);
// The hook is in (handlers can run).
bool Installed();
// False: the hook couldn't be removed, so this DLL must stay loaded.
bool Uninstall(hook::Context& init);

} // namespace mevr::engine::call_function
