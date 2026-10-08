#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>

#include <cstdint>

namespace mevr::engine {
namespace {

std::string Narrow(wchar_t const* chars) {
    int const size = WideCharToMultiByte(CP_UTF8, 0, chars, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string narrow(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, chars, -1, narrow.data(), size, nullptr, nullptr);
    return narrow;
}

} // namespace

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

UFunction* FindFunction(wchar_t const* name, char const* fullName) {
    for (UFunction* function : FindFunctionsNamed(name)) {
        if (FullName(function) == fullName) {
            return function;
        }
    }
    return nullptr;
}

std::string FullName(UObject* object) {
    if (object == nullptr) {
        return "None";
    }
    FString const wide = object->GetFullName();
    return Narrow(wide.Chars());
}

std::string ToString(SFXName const& name) {
    FString const wide = name.Instanced();
    return Narrow(wide.Chars());
}

bool IsAlive(UObject const* object) {
    if (object == nullptr) {
        return false;
    }
    auto* const objects = UObject::GObjObjects;
    int const index = object->ObjectInternalInteger;
    if (index < 0 || index >= static_cast<int>(objects->Count()) || objects->GetData()[index] != object) {
        return false;
    }
    constexpr std::uint64_t kPendingKill = 0x2000000000000000ull;  // EObjectFlags::PendingKill
    return (static_cast<std::uint64_t>(object->ObjectFlags) & kPendingKill) == 0;
}

APlayerController* LocalPlayerController() {
    UEngine* const engine = (GEngine != nullptr) ? *GEngine : nullptr;
    if (engine == nullptr || engine->GamePlayers.Count() == 0) {
        return nullptr;
    }
    ULocalPlayer* const player = engine->GamePlayers.GetData()[0];
    return player != nullptr ? player->Actor : nullptr;
}

} // namespace mevr::engine
