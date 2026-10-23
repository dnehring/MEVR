#pragma once

class UFunction;
class UObject;

namespace LESDK {
class Initializer;
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
using Handler = void (*)(UObject* context, UFunction* function, void* result);

// The same for modules on our reflection layer (ue3/, research/34).
using ObjectHandler = void (*)(ue3::Object* context, ue3::Function* function, void* result);

// Register before Install; the table is read without locking while hooked.
void AddPostHandler(UFunction* function, Handler handler);
void AddPostHandler(ue3::Function* function, ObjectHandler handler);
void ClearHandlers();

bool Install(::LESDK::Initializer& init);
// False: the hook couldn't be removed, so this DLL must stay loaded.
bool Uninstall(::LESDK::Initializer& init);

} // namespace mevr::engine::call_function
