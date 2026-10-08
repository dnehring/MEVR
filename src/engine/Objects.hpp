#pragma once

#include <string>
#include <vector>

class APlayerController;
class UFunction;
class UObject;
struct SFXName;

namespace mevr::engine {

// Every UFunction whose object name matches, across all classes (e.g. L"GetPlayerViewPoint"
// finds Engine.Controller's, Engine.PlayerController's and any subclass overrides).
// Walks GObjObjects, so call it at startup, not per frame.
std::vector<UFunction*> FindFunctionsNamed(wchar_t const* name);

// The function with this object name and full name ("Function Engine.Camera.UpdateCamera"), or
// nullptr (e.g. its script package isn't loaded yet). Walks GObjObjects.
UFunction* FindFunction(wchar_t const* name, char const* fullName);

// "Function Engine.PlayerController.GetPlayerViewPoint", as UTF-8 for logging.
std::string FullName(UObject* object);
// A name as UTF-8 ("Hand_R", or "Foo_2" for numbered names).
std::string ToString(SFXName const& name);

// The first local player's controller (GEngine->GamePlayers[0]->Actor), or nullptr.
APlayerController* LocalPlayerController();

// True if `object` is a live entry of GObjObjects (its index maps back to it and it isn't
// pending destruction). It reads the object, so it's for pointers just read from live objects
// (e.g. a handler's panel), never for pointers kept across frames: after a level load such a
// pointer may be freed, or reused by an unrelated object that passes this check.
bool IsAlive(UObject const* object);

} // namespace mevr::engine
