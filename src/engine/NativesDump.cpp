// SPDX-License-Identifier: MIT
#include "engine/NativesDump.hpp"

#include "core/Log.hpp"
#include "engine/GameAdapter.hpp"
#include "ue3/Runtime.hpp"

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

constexpr std::uint32_t kFuncNative = 0x400;     // FUNC_Native
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
        std::filesystem::path(buffer).parent_path() / L"ASI" / (MEVR_GAME_NAME_W L"MEVR.ini");
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

// "ABioWeaponRanged::execGetMuzzleLoc" (or "...::State::execName" for a state's native) and
// "BioWeaponRanged.GetMuzzleLoc (SFXGame)".
std::pair<std::string, std::string> Names(ue3::Reflection& r, ue3::Function* function, ue3::Class* classClass, ue3::Class* actorClass) {
    std::vector<std::string> outers;  // innermost first: state, class, package
    ue3::Class* owner = nullptr;
    ue3::Object* const functionOuter = r.OuterOf(function);
    int depth = 0;
    for (ue3::Object* outer = functionOuter; outer != nullptr && depth < 64; outer = r.OuterOf(outer), ++depth) {
        outers.push_back(r.NameText(outer));
        if (owner == nullptr && r.IsA(outer, classClass)) {
            owner = static_cast<ue3::Class*>(outer);
        }
    }
    std::string const leaf = r.NameText(function);
    std::string script;
    for (auto it = outers.rbegin() + (outers.size() > 1 ? 1 : 0); it != outers.rend(); ++it) {
        script += *it + ".";
    }
    script += leaf;
    std::string const package = outers.empty() ? std::string("?") : outers.back();

    std::string cpp;
    if (owner != nullptr) {
        cpp = (r.IsChildOf(owner, actorClass) ? "A" : "U") + r.NameText(owner) + "::";
        if (static_cast<ue3::Object*>(owner) != functionOuter && functionOuter != nullptr) {
            cpp += r.NameText(functionOuter) + "::";  // a state
        }
    }
    cpp += "exec" + leaf;
    return {cpp, std::format("native {} ({})", script, package)};
}

} // namespace

void SaveIfRequested() {
    if (gDone) {
        return;
    }
    gDone = true;
    if (!Requested()) {
        return;
    }
    if (!ue3::runtime::Ready()) {
        log::Warn("natives dump: our reflection layer is off ({}): nothing written", ue3::runtime::Status());
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
        dir / std::format("natives-{}-{:08X}.csv", MEVR_GAME_NAME_A, nt->FileHeader.TimeDateStamp);
    std::ofstream out(file, std::ios::binary);
    out << "# Native UFunctions of " MEVR_GAME_NAME_A " (exe timestamp " << std::format("0x{:08X}", nt->FileHeader.TimeDateStamp)
        << "), written by the mod (MEVR_DUMP_NATIVES). Local research data: never commit or share.\n"
           "rva,name,kind,comment\n";

    ue3::Reflection& r = ue3::runtime::Get();
    ue3::Class* const functionClass = r.FunctionClass();
    ue3::Class* const classClass = r.ClassClass();
    ue3::Class* const actorClass = r.FindClass("Engine.Actor");
    ue3::runtime::FlushNotes();
    std::map<std::uintptr_t, int> named;  // Func -> UFunctions pointing at it
    int functions = 0;
    std::int32_t const count = r.ObjectCount();
    for (std::int32_t i = 0; i < count; ++i) {
        ue3::Object* const object = r.ObjectAt(i);
        if (object == nullptr || !r.IsA(object, functionClass)) {
            continue;
        }
        auto* const function = static_cast<ue3::Function*>(object);
        void* const func = r.NativeFunc(function);
        if ((r.FunctionFlags(function) & kFuncNative) == 0 || !inImage(func)) {
            continue;
        }
        auto const rva = reinterpret_cast<std::uintptr_t>(func) - base;
        auto const [cpp, comment] = Names(r, function, classClass, actorClass);
        out << std::format("0x{:X},{},func,{}; iNative {}; flags 0x{:X}\n", rva, cpp, comment,
            static_cast<int>(r.NativeIndex(function)), r.FunctionFlags(function));
        ++named[rva];
        ++functions;
    }

    // The bytecode handlers (EX_LocalVariable, EX_NativeParm, ...) and other GNatives entries that
    // no UFunction names. The most common pointer is execUndefined.
    int handlers = 0;
    std::uint32_t const nativesRva = adapter::NativesTableRva();
    auto* const natives = nativesRva != 0 ? reinterpret_cast<void* const*>(base + nativesRva) : nullptr;
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
