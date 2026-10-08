#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

namespace mevr::engine {

std::vector<UFunction*> FindFunctionsNamed(wchar_t const* name) {
    std::vector<UFunction*> found;
    SFXName const target(name, 0);
    UClass* const functionClass = UFunction::StaticClass();
    auto* const objects = UObject::GObjObjects;
    for (int i = 0; i < static_cast<int>(objects->Count()); ++i) {
        UObject* const object = objects->GetData()[i];
        if (object != nullptr && object->Name == target && object->IsA(functionClass)) {
            found.push_back(static_cast<UFunction*>(object));
        }
    }
    return found;
}

std::string FullName(UObject* object) {
    if (object == nullptr) {
        return "None";
    }
    FString const wide = object->GetFullName();
    wchar_t const* const chars = wide.Chars();
    int const size = WideCharToMultiByte(CP_UTF8, 0, chars, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string narrow(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, chars, -1, narrow.data(), size, nullptr, nullptr);
    return narrow;
}

} // namespace mevr::engine
