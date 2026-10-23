// SPDX-License-Identifier: MIT
#include "engine/NativesDump.hpp"

#include "core/Log.hpp"
#include "engine/Objects.hpp"

#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>

#include <Windows.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace mevr::engine::natives_dump {
namespace {

constexpr DWORD kFuncNative = 0x400;     // FUNC_Native
constexpr std::size_t kGNativesCount = 0x1000;  // EX_Max: GNatives[EX_Max]

bool gDone = false;

bool Requested() {
    char* value = nullptr;
    std::size_t length = 0;
    bool on = false;
    if (_dupenv_s(&value, &length, "MEVR_DUMP_NATIVES") == 0 && value != nullptr) {
        on = value[0] == '1';
        std::free(value);
    }
    if (on) {
        return true;
    }
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    std::filesystem::path const ini =
        std::filesystem::path(buffer).parent_path() / L"ASI" / (SDK_TARGET_NAME_W L"MEVR.ini");
    return GetPrivateProfileIntW(L"Debug", L"dumpNatives", 0, ini.c_str()) == 1;
}

std::filesystem::path DumpDirectory() {
    char* appData = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&appData, &length, "LOCALAPPDATA") != 0 || appData == nullptr) {
        return {};
    }
    std::filesystem::path dir = std::filesystem::path(appData) / "MEVR" / "dump";
    std::free(appData);
    return dir;
}

bool IsSubclassOf(UClass* klass, UClass* parent) {
    for (UField* field = klass; field != nullptr; field = field->SuperField) {
        if (field == parent) {
            return true;
        }
    }
    return false;
}

// "ABioWeaponRanged::execGetMuzzleLoc" (or "...::State::execName" for a state's native) and
// "BioWeaponRanged.GetMuzzleLoc (SFXGame)".
std::pair<std::string, std::string> Names(UFunction* function, UClass* classClass, UClass* actorClass) {
    std::vector<std::string> outers;  // innermost first: state, class, package
    UClass* owner = nullptr;
    for (UObject* outer = function->Outer; outer != nullptr; outer = outer->Outer) {
        outers.push_back(ToString(outer->Name));
        if (owner == nullptr && outer->IsA(classClass)) {
            owner = static_cast<UClass*>(outer);
        }
    }
    std::string const leaf = ToString(function->Name);
    std::string script;
    for (auto it = outers.rbegin() + (outers.size() > 1 ? 1 : 0); it != outers.rend(); ++it) {
        script += *it + ".";
    }
    script += leaf;
    std::string const package = outers.empty() ? std::string("?") : outers.back();

    std::string cpp;
    if (owner != nullptr) {
        cpp = (IsSubclassOf(owner, actorClass) ? "A" : "U") + ToString(owner->Name) + "::";
        if (static_cast<UObject*>(owner) != function->Outer && function->Outer != nullptr) {
            cpp += ToString(function->Outer->Name) + "::";  // a state
        }
    }
    cpp += "exec" + leaf;
    return {cpp, std::format("native {} ({})", script, package)};
}

} // namespace

void SaveIfRequested(::LESDK::Initializer const& init) {
    if (gDone) {
        return;
    }
    gDone = true;
    if (!Requested()) {
        return;
    }
    auto const base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    auto const* const dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(base);
    auto const* const nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(base + dos->e_lfanew);
    std::uintptr_t const size = nt->OptionalHeader.SizeOfImage;
    auto const inImage = [base, size](void const* pointer) {
        auto const address = reinterpret_cast<std::uintptr_t>(pointer);
        return address > base && address < base + size;
    };

    std::filesystem::path const dir = DumpDirectory();
    if (dir.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    std::filesystem::path const file =
        dir / std::format("natives-{}-{:08X}.csv", SDK_TARGET_NAME_A, nt->FileHeader.TimeDateStamp);
    std::ofstream out(file, std::ios::binary);
    out << "# Native UFunctions of " SDK_TARGET_NAME_A " (exe timestamp " << std::format("0x{:08X}", nt->FileHeader.TimeDateStamp)
        << "), written by the mod (MEVR_DUMP_NATIVES). Local research data: never commit or share.\n"
           "rva,name,kind,comment\n";

    UClass* const functionClass = UFunction::StaticClass();
    UClass* const classClass = UClass::StaticClass();
    UClass* const actorClass = AActor::StaticClass();
    std::map<std::uintptr_t, int> named;  // Func -> UFunctions pointing at it
    int functions = 0;
    auto* const objects = UObject::GObjObjects;
    for (int i = 0; i < static_cast<int>(objects->Count()); ++i) {
        UObject* const object = objects->GetData()[i];
        if (object == nullptr || !object->IsA(functionClass)) {
            continue;
        }
        auto* const function = static_cast<UFunction*>(object);
        if ((function->FunctionFlags & kFuncNative) == 0 || !inImage(function->Func)) {
            continue;
        }
        auto const rva = reinterpret_cast<std::uintptr_t>(function->Func) - base;
        auto const [cpp, comment] = Names(function, classClass, actorClass);
        out << std::format("0x{:X},{},func,{}; iNative {}; flags 0x{:X}\n", rva, cpp, comment,
            static_cast<int>(function->iNative), static_cast<unsigned>(function->FunctionFlags));
        ++named[rva];
        ++functions;
    }

    // The bytecode handlers (EX_LocalVariable, EX_NativeParm, ...) and other GNatives entries that
    // no UFunction names. The most common pointer is execUndefined.
    int handlers = 0;
    auto* const natives = init.ResolveTyped<void*>(BUILTIN_GNATIVES_RVA);
    if (natives != nullptr) {
        std::map<std::uintptr_t, std::vector<std::size_t>> slots;
        for (std::size_t i = 0; i < kGNativesCount; ++i) {
            if (inImage(natives[i])) {
                slots[reinterpret_cast<std::uintptr_t>(natives[i]) - base].push_back(i);
            }
        }
        std::uintptr_t undefined = 0;
        std::size_t most = 0;
        for (auto const& [rva, indices] : slots) {
            if (indices.size() > most) {
                most = indices.size();
                undefined = rva;
            }
        }
        for (auto const& [rva, indices] : slots) {
            if (named.contains(rva)) {
                continue;
            }
            std::string const name = (rva == undefined && most > 8)
                ? std::string("UObject::execUndefined_guess")
                : std::format("UObject::execGNative_0x{:X}", indices.front());
            out << std::format("0x{:X},{},func,GNatives[0x{:X}] ({} slots)\n", rva, name, indices.front(), indices.size());
            ++handlers;
        }
    }
    out.close();
    if (!out) {
        log::Warn("natives dump: writing {} failed", file.string());
        return;
    }
    log::Info("natives dump: {} native functions ({} distinct addresses) and {} other GNatives handlers -> {} "
              "(local research data)",
        functions, named.size(), handlers, file.string());
}

} // namespace mevr::engine::natives_dump
