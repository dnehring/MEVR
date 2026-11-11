// SPDX-License-Identifier: MIT
#include "engine/NameLookup.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"

#include <Windows.h>

#include <string>

namespace mevr::engine::name_lookup {
namespace {

// FName::FName(this, text, find): writes the name's index and number into `out` (8 bytes, ue3::Name's layout).
using tNameFromText = void*(void* out, wchar_t const* text, int find);
tNameFromText* gNameFromText = nullptr;
constexpr int kFindOnly = 0;  // EFindName::FNAME_Find

bool IsFunctionStart(void* address) {
    DWORD64 imageBase = 0;
    auto const* const entry = RtlLookupFunctionEntry(reinterpret_cast<DWORD64>(address), &imageBase, nullptr);
    return entry != nullptr && imageBase + entry->BeginAddress == reinterpret_cast<DWORD64>(address);
}

bool CallGuarded(ue3::Name* out, wchar_t const* text) {
    __try {
        gNameFromText(out, text, kFindOnly);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace

bool Resolve(hook::Context& init) {
    if (gNameFromText != nullptr) {
        return true;
    }
    if (addresses::kNameFromText == 0) {
        return false;  // another game: no address
    }
    void* const target = init.Resolve(addresses::kNameFromText);
    if (target == nullptr || !IsFunctionStart(target)) {
        log::Warn("names: the engine's name lookup at {} is not a function start: names from text are unavailable", target);
        return false;
    }
    gNameFromText = reinterpret_cast<tNameFromText*>(target);
    return true;
}

std::optional<ue3::Name> Find(std::wstring_view text) {
    if (gNameFromText == nullptr || text.empty() || text.size() > 512) {
        return std::nullopt;
    }
    std::wstring const zeroEnded(text);
    ue3::Name name{};
    if (!CallGuarded(&name, zeroEnded.c_str()) || name.index == 0) {
        return std::nullopt;
    }
    return name;
}

} // namespace mevr::engine::name_lookup
