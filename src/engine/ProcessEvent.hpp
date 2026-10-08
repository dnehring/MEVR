#pragma once

class UFunction;
class UObject;

namespace LESDK {
class Initializer;
}

namespace mevr::engine::process_event {

// Runs after the original UObject::ProcessEvent for the UFunction it was registered with.
// Out-parameters in `parms` can be modified here.
using Handler = void (*)(UObject* context, UFunction* function, void* parms, void* result);

// Register handlers before Install; the table is read without locking while hooked.
void AddPostHandler(UFunction* function, Handler handler);
void ClearHandlers();

bool Install(::LESDK::Initializer& init);
void Uninstall(::LESDK::Initializer& init);

} // namespace mevr::engine::process_event
