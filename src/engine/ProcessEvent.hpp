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

namespace mevr::engine::process_event {

// Runs after the original UObject::ProcessEvent for the UFunction it was registered with.
// Out-parameters in `parms` can be modified here.
using Handler = void (*)(UObject* context, UFunction* function, void* parms, void* result);

// The same for modules on our reflection layer (ue3/, research/34).
using ObjectHandler = void (*)(ue3::Object* context, ue3::Function* function, void* parms, void* result);

// Register handlers before Install; the table is read without locking while hooked.
void AddPostHandler(UFunction* function, Handler handler);
void AddPostHandler(ue3::Function* function, ObjectHandler handler);
void ClearHandlers();

// The host's ProcessEvent hook (HostServices::setProcessEventCallback). With it, Install/Uninstall
// just set and clear the host's callback; without, the mod hooks ProcessEvent itself.
using SetHostCallback = void (*)(Handler callback);
void UseHost(SetHostCallback set);

bool Install(::LESDK::Initializer& init);
// False: the mod's own hook couldn't be removed, so this DLL must stay loaded.
bool Uninstall(::LESDK::Initializer& init);

} // namespace mevr::engine::process_event
