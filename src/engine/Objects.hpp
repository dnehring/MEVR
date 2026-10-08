#pragma once

#include <string>
#include <vector>

class UFunction;
class UObject;

namespace mevr::engine {

// Every UFunction whose object name matches, across all classes (e.g. L"GetPlayerViewPoint"
// finds Engine.Controller's, Engine.PlayerController's and any subclass overrides).
// Walks GObjObjects, so call it at startup, not per frame.
std::vector<UFunction*> FindFunctionsNamed(wchar_t const* name);

// "Function Engine.PlayerController.GetPlayerViewPoint", as UTF-8 for logging.
std::string FullName(UObject* object);

} // namespace mevr::engine
