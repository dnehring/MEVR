// SPDX-License-Identifier: MIT
#include "engine/ReflectCheck.hpp"

#include "core/Exe.hpp"
#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/Addresses.hpp"
#include "engine/Engine.hpp"
#include "engine/GameAdapter.hpp"
#include "engine/Skeleton.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Runtime.hpp"

// The reference side (LExSDKv2): development builds only. A players' build (MEVR_NO_LESDK, the release ASI) has no
// LExSDKv2 in it at all (research/34 stage 6 for LE1): the cross-check is skipped, our layer runs on its own.
#if defined(MEVR_GAME_LE1) && !defined(MEVR_NO_LESDK)
#include <LESDK/Headers.hpp>
#include <LESDK/Init.hpp>
#endif

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <algorithm>
#include <functional>
#include <memory>
#include <new>
#include <optional>
#include <string>
#include <vector>

// Everything the mod reads from LExSDKv2 today, checked against our reflection layer (research/34
// section 4 is the inventory; section 5 says how to read the result). LExSDKv2's offsets are used
// here only as the reference to compare with: none of them feeds ue3/.

namespace mevr::engine::reflect_check {

#if defined(MEVR_GAME_LE1) && !defined(MEVR_NO_LESDK)
namespace {

struct Row {
    std::string kind;  // layout, global, type, class, field, path, function, param, parms, sample
    std::string what;
    std::string ours;
    std::string sdk;
    bool agree = false;
    bool notReflected = false;  // a native-only field the engine's reflection may not list
};
std::vector<Row> gRows;

void Add(std::string kind, std::string what, bool agree, std::string ours, std::string sdk, bool notReflected = false) {
    gRows.push_back({std::move(kind), std::move(what), std::move(ours), std::move(sdk), agree, notReflected});
}

ue3::Reflection& R() {
    return ue3::runtime::Get();
}

std::string Hex(long long value) {
    return value < 0 ? std::string("none") : std::format("0x{:X}", value);
}

// ---- LExSDKv2's side (the reference; engine/Objects until stage 3) -------------------------------------

template <typename T>
bool ResolveSdk(::LESDK::Initializer& init, ::LESDK::Address address, T*& out, char const* name) {
    out = init.ResolveTyped<T>(address);
    if (out == nullptr) {
        log::Error("reflect: LExSDKv2's {} didn't resolve", name);
        return false;
    }
    return true;
}

// The LExSDKv2 globals the reference side reads (engine/Engine's ResolveGlobals until stage 3: on LE1 only
// this file reads them now). Each DLL linking LExSDKv2 has its own copies.
bool ResolveSdkGlobals(::LESDK::Initializer& init) {
    bool ok = true;
    ok &= ResolveSdk(init, BUILTIN_GMALLOC_RVA, GMalloc, "GMalloc");
    ok &= ResolveSdk(init, BUILTIN_GOBJECTS_RVA, UObject::GObjObjects, "GObjObjects");
    ok &= ResolveSdk(init, BUILTIN_SFXNAMEPOOLS_RVA, SFXName::GBioNamePools, "SFXName pools");
    ok &= ResolveSdk(init, BUILTIN_SFXNAMEINIT_RVA, SFXName::GInitMethod, "SFXName init");
    ok &= ResolveSdk(init, BUILTIN_GENGINE_RVA, GEngine, "GEngine");
    ok &= ResolveSdk(init, BUILTIN_GWORLD_RVA, GWorld, "GWorld");
    return ok;
}

std::string Narrow(wchar_t const* chars) {
    int const size = WideCharToMultiByte(CP_UTF8, 0, chars, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) {
        return {};
    }
    std::string narrow(static_cast<size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, chars, -1, narrow.data(), size, nullptr, nullptr);
    return narrow;
}

std::string SdkFullName(UObject* object) {
    if (object == nullptr) {
        return "None";
    }
    FString const wide = object->GetFullName();
    return Narrow(wide.Chars());
}

std::string SdkName(SFXName const& name) {
    FString const wide = name.Instanced();
    return Narrow(wide.Chars());
}

std::vector<UFunction*> SdkFunctionsNamed(wchar_t const* name) {
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

UFunction* SdkFunction(wchar_t const* name, std::string const& fullName) {
    for (UFunction* function : SdkFunctionsNamed(name)) {
        if (SdkFullName(function) == fullName) {
            return function;
        }
    }
    return nullptr;
}

bool SdkIsAlive(UObject const* object) {
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

APlayerController* SdkLocalPlayerController() {
    UEngine* const engine = (GEngine != nullptr) ? *GEngine : nullptr;
    if (engine == nullptr || engine->GamePlayers.Count() == 0) {
        return nullptr;
    }
    ULocalPlayer* const player = engine->GamePlayers.GetData()[0];
    return player != nullptr ? player->Actor : nullptr;
}

// Runs one check with access violations caught (a wrong layout reads garbage).
void Guard(char const* kind, std::string const& what, std::function<void()> const& check) {
    auto const thunk = [](void* context) { (*static_cast<std::function<void()> const*>(context))(); };
    if (!ue3::runtime::Guarded(thunk, const_cast<std::function<void()>*>(&check))) {
        Add(kind, what, false, "access violation while checking", "");
    }
}

// ---- what LExSDKv2 compiled in -------------------------------------------------------------------

struct Sdk {
    long long offset = -1;
    std::uint32_t mask = 0;  // bitfield bools
    long long size = -1;     // bytes (elementSize * arrayDim); -1 unknown
};

std::string SdkText(Sdk const& sdk) {
    if (sdk.mask != 0) {
        return std::format("{} mask 0x{:X}", Hex(sdk.offset), sdk.mask);
    }
    return sdk.size >= 0 ? std::format("{} size {}", Hex(sdk.offset), sdk.size) : Hex(sdk.offset);
}

// A bitfield's offset and mask: set it in a zeroed buffer and see which bit moved.
template <class T, class Set>
Sdk BoolOf(Set set) {
    std::size_t const words = (sizeof(T) + 7) / 8 + 1;
    auto const buffer = std::make_unique<std::uint64_t[]>(words);
    set(*reinterpret_cast<T*>(buffer.get()));
    auto const* bytes = reinterpret_cast<std::uint8_t const*>(buffer.get());
    for (std::size_t i = 0; i + 4 <= sizeof(T); i += 4) {
        std::uint32_t word = 0;
        std::memcpy(&word, bytes + i, sizeof(word));
        if (word != 0) {
            return Sdk{static_cast<long long>(i), word, 4};
        }
    }
    return {};
}

#define SDK_FIELD(T, member) (Sdk{static_cast<long long>(offsetof(T, member)), 0u, static_cast<long long>(sizeof(T::member))})
#define SDK_BOOL(T, member) (BoolOf<T>([](T& o) { o.member = 1; }))
#define SDK_AT(offset, size) (Sdk{static_cast<long long>(offset), 0u, static_cast<long long>(size)})

std::string Ours(std::optional<ue3::PropertyInfo> const& info) {
    if (!info) {
        return "not found";
    }
    if (info->kind == ue3::Kind::Bool) {
        return std::format("{} mask 0x{:X} (bool)", Hex(info->offset), info->boolMask);
    }
    return std::format("{} size {} ({})", Hex(info->offset), info->elementSize * info->arrayDim, ue3::KindName(info->kind));
}

bool Matches(ue3::PropertyInfo const& info, long long offset, Sdk const& sdk) {
    if (offset != sdk.offset) {
        return false;
    }
    if (sdk.mask != 0) {
        return info.kind == ue3::Kind::Bool && info.boolMask == sdk.mask;
    }
    return sdk.size < 0 || static_cast<long long>(info.elementSize) * info.arrayDim == sdk.size;
}

// ---- checks ------------------------------------------------------------------------------------

template <class T>
void CheckClass(char const* path) {
    Guard("class", path, [path] {
        UClass* const sdk = T::StaticClass();
        ue3::Class* const ours = R().FindClass(path);
        Add("class", path, ours != nullptr && static_cast<void*>(ours) == static_cast<void*>(sdk),
            ours != nullptr ? R().FullName(ours) : "not found", sdk != nullptr ? SdkFullName(sdk) : "not found");
    });
}

// `notReflected`: a native field the script may not declare; not finding it isn't a mismatch.
void CheckField(char const* cls, char const* field, Sdk sdk, bool maybeNative = false) {
    std::string const what = std::format("{}.{}", cls, field);
    Guard("field", what, [&] {
        std::optional<ue3::PropertyInfo> const info = R().PropertyOf(R().FindClass(cls), field);
        Add("field", what, info && Matches(*info, info->offset, sdk), Ours(info), SdkText(sdk), maybeNative && !info);
    });
}

// A member reached through struct (and array) properties: the offsets add up through structs and
// start again inside an array's element. `sdk.offset` is the same sum on LExSDKv2's side.
void CheckPath(char const* cls, std::vector<char const*> const& path, Sdk sdk) {
    std::string what = cls;
    for (char const* p : path) {
        what += std::format(".{}", p);
    }
    Guard("path", what, [&] {
        ue3::Struct const* owner = R().FindClass(cls);
        long long total = 0;
        std::optional<ue3::PropertyInfo> info;
        for (std::size_t i = 0; i < path.size(); ++i) {
            info = R().PropertyOf(owner, path[i]);
            if (!info) {
                break;
            }
            total += info->offset;
            if (i + 1 == path.size()) {
                break;
            }
            ue3::PropertyInfo element = *info;
            if (info->kind == ue3::Kind::Array) {
                std::optional<ue3::PropertyInfo> const inner = R().Describe(static_cast<ue3::Property*>(info->typed));
                if (!inner) {
                    info.reset();
                    break;
                }
                element = *inner;
                total = 0;  // inside the element
            }
            owner = R().StructOf(element);
            if (owner == nullptr) {
                info.reset();
                break;
            }
        }
        Add("path", what, info && Matches(*info, total, sdk),
            info ? std::format("{} size {} ({})", Hex(total), info->elementSize * info->arrayDim, ue3::KindName(info->kind))
                 : std::string("not found"),
            SdkText(sdk));
    });
}

// An array property's element size (the struct LExSDKv2 declares for it).
void CheckElement(char const* cls, char const* field, long long sdkSize) {
    std::string const what = std::format("{}.{}[]", cls, field);
    Guard("path", what, [&] {
        std::optional<ue3::PropertyInfo> const info = R().PropertyOf(R().FindClass(cls), field);
        std::optional<ue3::PropertyInfo> const inner =
            info && info->kind == ue3::Kind::Array ? R().Describe(static_cast<ue3::Property*>(info->typed)) : std::nullopt;
        Add("path", what, inner && inner->elementSize == sdkSize,
            inner ? std::format("element size {}", inner->elementSize) : std::string("not found"), std::format("element size {}", sdkSize));
    });
}

std::wstring Widen(std::string_view text) {
    return std::wstring(text.begin(), text.end());  // ASCII names
}

void CheckFunction(char const* path) {
    Guard("function", path, [path] {
        ue3::Function* const ours = R().FindFunction(path);
        std::string_view const full(path);
        std::wstring const name = Widen(full.substr(full.rfind('.') + 1));
        UFunction* const sdk = SdkFunction(name.c_str(), std::format("Function {}", path));
        Add("function", path, ours != nullptr && static_cast<void*>(ours) == static_cast<void*>(sdk),
            ours != nullptr ? std::format("found, ParmsSize {}", R().ParmsSize(ours)) : "not found",
            sdk != nullptr ? "found" : "not found");
    });
}

void CheckParam(char const* function, char const* param, Sdk sdk) {
    std::string const what = std::format("{}({})", function, param);
    Guard("param", what, [&] {
        std::optional<ue3::PropertyInfo> const info = R().PropertyOf(R().FindFunction(function), param);
        bool const isParam = info && (info->flags & ue3::property_flags::kParm) != 0;
        Add("param", what, isParam && Matches(*info, info->offset, sdk), Ours(info), SdkText(sdk));
    });
}

// ParmsSize against the size of LExSDKv2's struct (which rounds up to its alignment).
void CheckParmsSize(char const* function, long long sdkSize) {
    Guard("parms", function, [&] {
        ue3::Function* const f = R().FindFunction(function);
        long long const ours = f != nullptr ? R().ParmsSize(f) : -1;
        Add("parms", function, f != nullptr && ours <= sdkSize && sdkSize - ours < 8,
            f != nullptr ? std::format("ParmsSize {}", ours) : "not found", std::format("sizeof {}", sdkSize));
    });
}

#define PARAM(Parms, function, member) CheckParam(function, #member, SDK_FIELD(Parms, member))
#define PARMS(Parms, function) CheckParmsSize(function, static_cast<long long>(sizeof(Parms)))

// ---- the table -----------------------------------------------------------------------------------

void CheckLayout() {
    ue3::Layout const& l = R().layout();
    struct Item {
        char const* what;
        long long ours;
        long long sdk;
    };
    Item const items[] = {
        {"UObject.ObjectInternalInteger", l.objectIndex, static_cast<long long>(offsetof(UObject, ObjectInternalInteger))},
        {"UObject.ObjectFlags", l.objectFlags, static_cast<long long>(offsetof(UObject, ObjectFlags))},
        {"UObject.Outer", l.objectOuter, static_cast<long long>(offsetof(UObject, Outer))},
        {"UObject.Name", l.objectName, static_cast<long long>(offsetof(UObject, Name))},
        {"UObject.Class", l.objectClass, static_cast<long long>(offsetof(UObject, Class))},
        {"UField.SuperField", l.fieldSuper, static_cast<long long>(offsetof(UField, SuperField))},
        {"UField.Next", l.fieldNext, static_cast<long long>(offsetof(UField, Next))},
        {"UStruct.Children", l.structChildren, static_cast<long long>(offsetof(UStruct, Children))},
        {"UStruct.PropertySize", l.structPropertySize, static_cast<long long>(offsetof(UStruct, PropertySize))},
        {"UProperty.ArrayDim", l.propertyArrayDim, static_cast<long long>(offsetof(UProperty, ArrayDim))},
        {"UProperty.ElementSize", l.propertyElementSize, static_cast<long long>(offsetof(UProperty, ElementSize))},
        {"UProperty.PropertyFlags", l.propertyFlags, static_cast<long long>(offsetof(UProperty, PropertyFlags))},
        {"UProperty.Offset", l.propertyOffset, static_cast<long long>(offsetof(UProperty, Offset))},
        {"UBoolProperty.BitMask", l.propertyTyped, static_cast<long long>(offsetof(UBoolProperty, BitMask))},
        {"UStructProperty.Struct", l.propertyTyped, static_cast<long long>(offsetof(UStructProperty, Struct))},
        {"UObjectProperty.PropertyClass", l.propertyTyped, static_cast<long long>(offsetof(UObjectProperty, PropertyClass))},
        {"UArrayProperty.Inner", l.propertyTyped, static_cast<long long>(offsetof(UArrayProperty, Inner))},
        {"UFunction.FunctionFlags", l.functionFlags, static_cast<long long>(offsetof(UFunction, FunctionFlags))},
        {"UFunction.iNative", l.functionNative, static_cast<long long>(offsetof(UFunction, iNative))},
        {"UFunction.ParmsSize", l.functionParmsSize, static_cast<long long>(offsetof(UFunction, ParmsSize))},
        {"UFunction.ReturnValueOffset", l.functionReturnOffset, static_cast<long long>(offsetof(UFunction, ReturnValueOffset))},
        {"UFunction.Func", l.functionFunc, static_cast<long long>(offsetof(UFunction, Func))},
    };
    for (Item const& item : items) {
        Add("layout", item.what, item.ours == item.sdk, Hex(item.ours), Hex(item.sdk));
    }
}

void CheckGlobals(::LESDK::Initializer& init) {
    ue3::runtime::Found const& found = ue3::runtime::Where();
    Guard("global", "GObjects", [&] {
        auto const sdk = reinterpret_cast<std::uintptr_t>(UObject::GObjObjects);
        Add("global", "GObjects", found.objects == sdk,
            std::format("RVA {} (by {}), {} objects", Hex(static_cast<long long>(found.objects - found.module)), found.objectsBy,
                R().ObjectCount()),
            std::format("RVA {}, {} objects", Hex(static_cast<long long>(sdk - found.module)), UObject::GObjObjects->Count()));
    });
    Guard("global", "name pools", [&] {
        auto const sdk = reinterpret_cast<std::uintptr_t>(SFXName::GBioNamePools);
        Add("global", "name pools", found.namePools == sdk,
            std::format("RVA {} (by {})", Hex(static_cast<long long>(found.namePools - found.module)), found.namePoolsBy),
            std::format("RVA {}", Hex(static_cast<long long>(sdk - found.module))));
    });
    Guard("global", "ProcessEvent", [&] {
        auto const sdk = reinterpret_cast<std::uintptr_t>(init.Resolve(BUILTIN_PROCESSEVENT_RVA));
        Add("global", "ProcessEvent", found.processEvent == sdk,
            std::format("vtable +0x{:X}: RVA {}", R().layout().processEventSlot, Hex(static_cast<long long>(found.processEvent - found.module))),
            std::format("RVA {}", Hex(static_cast<long long>(sdk - found.module))));
    });
    // The engine's allocator (ue3/Memory, stage 2): the GMalloc appMalloc and appFree both load, against LExSDKv2's.
    Guard("global", "allocator", [&] {
        auto const sdk = reinterpret_cast<std::uintptr_t>(GMalloc);
        auto const rva = [&](std::uintptr_t a) { return Hex(a != 0 ? static_cast<long long>(a - found.module) : -1); };
        Add("global", "allocator", found.gmalloc != 0 && found.gmalloc == sdk,
            std::format("GMalloc RVA {} (appMalloc RVA {} by {}, appFree RVA {} by {})", rva(found.gmalloc), rva(found.appMalloc),
                found.appMallocBy, rva(found.appFree), found.appFreeBy),
            std::format("GMalloc RVA {}", rva(sdk)));
    });
    Guard("global", "GameEngine", [&] {
        ue3::Object* const ours = ue3::runtime::GameEngine();
        UEngine* const sdk = GEngine != nullptr ? *GEngine : nullptr;
        Add("global", "GameEngine", ours != nullptr && static_cast<void*>(ours) == static_cast<void*>(sdk), R().FullName(ours),
            sdk != nullptr ? SdkFullName(sdk) : "none");
    });
    Guard("global", "LocalPlayerController", [&] {
        ue3::Object* const ours = ue3::runtime::LocalPlayerController();
        APlayerController* const sdk = SdkLocalPlayerController();
        // Both null on the main menu: that agrees too.
        Add("global", "LocalPlayerController", static_cast<void*>(ours) == static_cast<void*>(sdk), R().FullName(ours),
            sdk != nullptr ? SdkFullName(sdk) : "None");
    });
    // Stage 3: engine::World() (the player controller's WorldInfo's outer World) against GWorld. Both null on the
    // main menu's first ticks would agree too; GWorld is never null once a map is up.
    Guard("global", "World", [&] {
        void const* const ours = engine::World();
        UWorld* const sdk = GWorld != nullptr ? *GWorld : nullptr;
        Add("global", "World", ours == static_cast<void const*>(sdk),
            std::format("{} ({})", ours != nullptr ? R().FullName(static_cast<ue3::Object const*>(ours)) : std::string("None"),
                engine::WorldHow()),
            sdk != nullptr ? SdkFullName(sdk) : "None");
    });
    // Stage 3: the GNatives RVA the research diagnostics read (engine/GameAdapter), against LExSDKv2's; its entry
    // 0x14 (execLetBool, research/34 section 2.1) must point into the exe.
    Guard("global", "GNatives", [&] {
        std::uint32_t const rva = adapter::NativesTableRva();
        auto const sdk = reinterpret_cast<std::uintptr_t>(init.Resolve(BUILTIN_GNATIVES_RVA));
        auto const* const table = reinterpret_cast<std::uintptr_t const*>(found.module + rva);
        std::uintptr_t const letBool = rva != 0 ? table[0x14] : 0;
        bool const inExe = letBool > found.module && letBool - found.module < 0x4000000;
        Add("global", "GNatives", rva != 0 && found.module + rva == sdk && inExe,
            std::format("RVA {}, [0x14] RVA {}", Hex(rva), Hex(letBool != 0 ? static_cast<long long>(letBool - found.module) : -1)),
            std::format("RVA {}", Hex(static_cast<long long>(sdk - found.module))));
    });
}

// Stage 4 (research/34): our RVA table (engine/Addresses.hpp, checked by core/Exe in this DLL at mod startup).
// `rva` rows: the seven addresses LExSDKv2 also has a built-in for, our constant and the address core/Exe resolves
// against LExSDKv2's (the same address, and at its RVA in the running exe). `exe` rows: every entry of the table as
// this DLL's check found it (at its RVA, by its bytes, hooked by another module or by structure only; a moved or
// missing one disagrees).
void CheckRvas(::LESDK::Initializer& init) {
    std::uintptr_t const base = exe::Base();
    auto const rvaText = [base](void const* address) {
        auto const a = reinterpret_cast<std::uintptr_t>(address);
        return address != nullptr ? Hex(static_cast<long long>(a - base)) : std::string("none");
    };
    struct Item {
        char const* what;
        std::uint32_t ours;
        ::LESDK::Address sdk;
    };
    Item const items[] = {
        {"UObject::ProcessEvent", addresses::kProcessEvent, BUILTIN_PROCESSEVENT_RVA},
        {"UObject::CallFunction", addresses::kCallFunction, BUILTIN_CALLFUNCTION_RVA},
        {"UGameEngine::Tick", addresses::kGameEngineTick, BUILTIN_GAMEENGINETICK_RVA},
        {"GNatives", addresses::kGNatives, BUILTIN_GNATIVES_RVA},
        {"GObjects", addresses::kGObjects, BUILTIN_GOBJECTS_RVA},
        {"the name pools", addresses::kNamePools, BUILTIN_SFXNAMEPOOLS_RVA},
        {"GMalloc", addresses::kGMalloc, BUILTIN_GMALLOC_RVA},
    };
    for (Item const& item : items) {
        void* const ours = exe::Resolve(item.ours);
        void* const sdk = init.Resolve(item.sdk);
        exe::Result const* const result = exe::Find(item.ours);
        bool const checked = result != nullptr && exe::AtItsRva(result->status);
        Add("rva", item.what, ours != nullptr && ours == sdk && checked,
            std::format("RVA {}, resolved to {} ({})", Hex(item.ours), rvaText(ours),
                result != nullptr ? exe::StatusText(result->status) : "not in our RVA table"),
            std::format("RVA {}", rvaText(sdk)));
    }
    for (exe::Result const& r : exe::Results()) {
        Add("exe", std::format("{} ({})", r.entry->name, exe::KindText(r.entry->kind)),
            exe::AtItsRva(r.status) && r.found == r.entry->rva,
            std::format("RVA {}: {}{}", Hex(r.entry->rva), exe::StatusText(r.status), r.why.empty() ? "" : " (" + r.why + ")"),
            std::format("our table ({})", r.entry->user));
    }
}

void CheckTypes() {
    Guard("type", "our script structs", [] {
        ue3::Class* const actor = R().FindClass("Engine.Actor");
        struct Item {
            char const* what;
            char const* field;
            long long ours;
            long long sdk;
        };
        Item const items[] = {
            {"Rotator", "Rotation", static_cast<long long>(sizeof(ue3::Rotator)), static_cast<long long>(sizeof(FRotator))},
            {"Vector", "Location", static_cast<long long>(sizeof(ue3::Vector)), static_cast<long long>(sizeof(FVector))},
        };
        for (Item const& item : items) {
            std::optional<ue3::PropertyInfo> const info = R().PropertyOf(actor, item.field);
            ue3::Struct* const s = info ? R().StructOf(*info) : nullptr;
            long long const reflected = s != nullptr ? R().PropertySize(s) : -1;
            Add("type", item.what, reflected == item.ours && item.ours == item.sdk,
                std::format("ours {}, the engine's struct {}", item.ours, reflected), std::format("sizeof {}", item.sdk));
        }
        Add("type", "Name", sizeof(ue3::Name) == sizeof(SFXName), std::to_string(sizeof(ue3::Name)), std::to_string(sizeof(SFXName)));
        Add("type", "TArray", sizeof(ue3::TArray<int>) == sizeof(TArray<int>), std::to_string(sizeof(ue3::TArray<int>)),
            std::to_string(sizeof(TArray<int>)));
        Add("type", "FString", sizeof(ue3::FString) == sizeof(FString), std::to_string(sizeof(ue3::FString)), std::to_string(sizeof(FString)));
    });
}

void CheckClasses() {
    CheckClass<UObject>("Core.Object");
    CheckClass<UFunction>("Core.Function");
    CheckClass<UClass>("Core.Class");
    CheckClass<AActor>("Engine.Actor");
    CheckClass<AStaticMeshActorBase>("Engine.StaticMeshActorBase");
    CheckClass<ADynamicSMActor>("Engine.DynamicSMActor");
    CheckClass<ASpeedTreeActor>("Engine.SpeedTreeActor");
    CheckClass<APawn>("Engine.Pawn");
    CheckClass<AController>("Engine.Controller");
    CheckClass<APlayerController>("Engine.PlayerController");
    CheckClass<ACamera>("Engine.Camera");
    CheckClass<AWorldInfo>("Engine.WorldInfo");
    CheckClass<UWorld>("Engine.World");  // stage 3: engine::World()
    CheckClass<AInventoryManager>("Engine.InventoryManager");
    CheckClass<AWeapon>("Engine.Weapon");
    CheckClass<AProjectile>("Engine.Projectile");
    CheckClass<UPlayerInput>("Engine.PlayerInput");
    CheckClass<UActorComponent>("Engine.ActorComponent");
    CheckClass<UPrimitiveComponent>("Engine.PrimitiveComponent");
    CheckClass<UMeshComponent>("Engine.MeshComponent");
    CheckClass<USkeletalMeshComponent>("Engine.SkeletalMeshComponent");
    CheckClass<USkeletalMesh>("Engine.SkeletalMesh");
    CheckClass<USkeletalMeshSocket>("Engine.SkeletalMeshSocket");
    CheckClass<UAnimNode>("Engine.AnimNode");
    CheckClass<UMaterialInstance>("Engine.MaterialInstance");  // game/scope_parts' material log (research/21 section 14)
    CheckClass<UMaterialInstanceConstant>("Engine.MaterialInstanceConstant");
    CheckClass<UAnimNodeBlendBase>("Engine.AnimNodeBlendBase");  // game/combat_stance (research/38)
    CheckClass<UAnimNodeSequence>("Engine.AnimNodeSequence");
    CheckClass<UAnimTree>("Engine.AnimTree");
    CheckClass<USkelControlBase>("Engine.SkelControlBase");
    CheckClass<USkelControlLimb>("Engine.SkelControlLimb");
    CheckClass<USceneCaptureComponent>("Engine.SceneCaptureComponent");
    CheckClass<USceneCaptureReflectComponent>("Engine.SceneCaptureReflectComponent");
    // The scope capture (game/ScopeCapture, research/39).
    CheckClass<USceneCapture2DComponent>("Engine.SceneCapture2DComponent");
    CheckClass<ASceneCaptureActor>("Engine.SceneCaptureActor");
    CheckClass<ASceneCapture2DActor>("Engine.SceneCapture2DActor");
    CheckClass<UTexture>("Engine.Texture");
    CheckClass<UTextureRenderTarget2D>("Engine.TextureRenderTarget2D");
    // The cockpit probe (game/CockpitProbe, research/47).
    CheckClass<AInterpActor>("Engine.InterpActor");
    CheckClass<AKActor>("Engine.KActor");
    CheckClass<AKActorSpawnable>("Engine.KActorSpawnable");
    CheckClass<AStaticMeshActor>("Engine.StaticMeshActor");
    CheckClass<UStaticMesh>("Engine.StaticMesh");
    CheckClass<UStaticMeshComponent>("Engine.StaticMeshComponent");
    CheckClass<UMaterial>("Engine.Material");
    CheckClass<UMaterialExpressionTextureSampleParameter>("Engine.MaterialExpressionTextureSampleParameter");
    CheckClass<UTexture2D>("Engine.Texture2D");
    CheckClass<USFXSaveGame>("SFXGame.SFXSaveGame");
    CheckClass<USeqEvent_RemoteEvent>("Engine.SeqEvent_RemoteEvent");  // `mevr vehicle event`
    CheckClass<UBioSFHandler_PartySelection>("SFXGame.BioSFHandler_PartySelection");  // `mevr vehicle party`
    CheckClass<USFXGameModeVehicle>("SFXGame.SFXGameModeVehicle");  // `mevr vehicle exit`
    CheckClass<UEngine>("Engine.Engine");
    CheckClass<UGameEngine>("Engine.GameEngine");
    CheckClass<UPlayer>("Engine.Player");
    CheckClass<ULocalPlayer>("Engine.LocalPlayer");
    CheckClass<USFXModule>("Engine.SFXModule");
    CheckClass<UBioSFHandler>("Engine.BioSFHandler");
    CheckClass<UBioSFPanel>("Engine.BioSFPanel");
    CheckClass<UBioSFManager>("Engine.BioSFManager");
    CheckClass<ABioPawn>("SFXGame.BioPawn");
    CheckClass<ABioTriggerStream>("SFXGame.BioTriggerStream");  // mevr stream
    CheckClass<ABioPlayerController>("SFXGame.BioPlayerController");
    CheckClass<UBioPlayerInput>("SFXGame.BioPlayerInput");
    CheckClass<ASFXCameraNativeBase>("SFXGame.SFXCameraNativeBase");
    CheckClass<USFXCameraMode>("SFXGame.SFXCameraMode");
    CheckClass<USFXCameraMode_Interpolate>("SFXGame.SFXCameraMode_Interpolate");
    CheckClass<USFXCameraInput>("SFXGame.SFXCameraInput");
    CheckClass<ASFXInventoryManager>("SFXGame.SFXInventoryManager");
    CheckClass<USFXModule_AimAssist>("SFXGame.SFXModule_AimAssist");
    CheckClass<USFXGameModeManager>("SFXGame.SFXGameModeManager");
    CheckClass<USFXGameModeBase>("SFXGame.SFXGameModeBase");
    CheckClass<USFXGameModeDefault>("SFXGame.SFXGameModeDefault");
    CheckClass<USFXGameModeWeaponWheel>("SFXGame.SFXGameModeWeaponWheel");
    // game/GameMenuRequest (research/28): the active mode's ShowMenuGui, by CurrentMode's class.
    CheckClass<USFXGameModeVehicle>("SFXGame.SFXGameModeVehicle");
    CheckClass<USFXGameModePowerWheel>("SFXGame.SFXGameModePowerWheel");
    CheckClass<USFXGameModeCommand>("SFXGame.SFXGameModeCommand");
    CheckClass<USFXGameModePhoto>("SFXGame.SFXGameModePhoto");
    CheckClass<UBioPawnBehavior>("SFXGame.BioPawnBehavior");
    CheckClass<UBioEpicPawnBehavior>("SFXGame.BioEpicPawnBehavior");
    CheckClass<UBioActorBehavior>("SFXGame.BioActorBehavior");
    CheckClass<UBioActorType>("SFXGame.BioActorType");
    CheckClass<ABioBaseSquad>("SFXGame.BioBaseSquad");
    CheckClass<UBioEquipment>("SFXGame.BioEquipment");
    CheckClass<ABioWeapon>("SFXGame.BioWeapon");
    CheckClass<ABioWeaponRanged>("SFXGame.BioWeaponRanged");
    CheckClass<USFXWeaponStatsBase>("SFXGame.SFXWeaponStatsBase");  // the game's magnifications (research/21 section 16)
    CheckClass<ABioGrenade>("SFXGame.BioGrenade");
    CheckClass<ABioGrenadeProjectile>("SFXGame.BioGrenadeProjectile");
    CheckClass<ABioGrenadeProjectileFrisbee>("SFXGame.BioGrenadeProjectileFrisbee");
    CheckClass<ABioGrenadeProjectileBallistic>("SFXGame.BioGrenadeProjectileBallistic");
    CheckClass<UMassEffectGuiManager>("SFXGame.MassEffectGuiManager");
    CheckClass<UBioSFHandler_HUD>("SFXGame.BioSFHandler_HUD");
    CheckClass<UBioCameraBehaviorGalaxy>("SFXGame.BioCameraBehaviorGalaxy");  // the galaxy hologram (research/51 section 8)
    CheckClass<ABioSPGame>("SFXGame.BioSPGame");
    CheckClass<UBio2DA>("Engine.Bio2DA");
    CheckClass<UBioPlayerSelection>("SFXGame.BioPlayerSelection");  // game/interact (research/40)
    CheckClass<UBioSFHandler_GalaxyMap>("SFXGame.BioSFHandler_GalaxyMap");
    CheckClass<UBioSFHandler_Shop>("SFXGame.BioSFHandler_Shop");  // game/WorldUi: the shop's vendor
    CheckClass<UBioSFHandler_Loot>("SFXGame.BioSFHandler_Loot");  // game/WorldUi: the loot's container
    CheckClass<UBioSkillGame_Handler>("SFXGame.BioSkillGame_Handler");  // game/WorldUi: the minigame's placeable
    CheckClass<UBioVFXTemplate>("SFXGame.BioVFXTemplate");  // game/OmniTool: the game's omni-tool effect
    CheckClass<ABioVisualEffect>("SFXGame.BioVisualEffect");  // game/OmniTool
    CheckClass<UBioSeqEvt_ArtPlaceableUsed>("SFXGame.BioSeqEvt_ArtPlaceableUsed");
    CheckClass<UBioArtPlaceableBehavior>("SFXGame.BioArtPlaceableBehavior");
    CheckClass<AEmitter>("Engine.Emitter");  // the CIC projector's own hologram, hidden while ours shows
    CheckClass<UBioSeqAct_Delay>("SFXGame.BioSeqAct_Delay");  // the galaxy map's opening wait
    // game/mako (research/44): the Mako's turret aimed by the controller.
    CheckClass<ABioVehicleBase>("SFXGame.BioVehicleBase");
    CheckClass<ABioVehicleWheeled>("SFXGame.BioVehicleWheeled");
    CheckClass<UDamageType>("Engine.DamageType");  // `mevr vehicle damage`
    CheckClass<UBioSFHandler_GameOver>("SFXGame.BioSFHandler_GameOver");  // `mevr vehicle resume`
    CheckClass<UBioVehicleBehaviorBase>("SFXGame.BioVehicleBehaviorBase");
    CheckClass<UBioSkelControl_TurretConstrained>("SFXGame.BioSkelControl_TurretConstrained");
    // game/MakoControls (research/46): which simulation the vehicle runs.
    CheckClass<USVehicleSimCar>("Engine.SVehicleSimCar");
    CheckClass<USVehicleSimTank>("Engine.SVehicleSimTank");
    CheckClass<UBioSVehicleSimTank>("SFXGame.BioSVehicleSimTank");
    CheckClass<UBioSFHandler_PCHUD>("SFXGame.BioSFHandler_PCHUD");
}

void CheckFields() {
    // Engine
    CheckField("Engine.Actor", "Rotation", SDK_FIELD(AActor, Rotation));
    CheckField("Engine.Actor", "Location", SDK_FIELD(AActor, Location));
    CheckField("Engine.Actor", "Physics", SDK_FIELD(AActor, Physics));
    CheckField("Engine.Actor", "Modules", SDK_FIELD(AActor, Modules));
    CheckField("Engine.Actor", "WorldInfo", SDK_FIELD(AActor, WorldInfo));
    CheckField("Engine.Actor", "Owner", SDK_FIELD(AActor, Owner));
    CheckField("Engine.Actor", "AllComponents", SDK_FIELD(AActor, AllComponents));  // the cockpit probe's `ride`
    CheckField("Engine.Actor", "bDeleteMe", SDK_BOOL(AActor, bDeleteMe));
    CheckField("Engine.Vehicle", "Driver", SDK_FIELD(AVehicle, Driver));  // `mevr vehicle why`
    CheckField("Engine.Actor", "bHidden", SDK_BOOL(AActor, bHidden));
    CheckField("Engine.Actor", "LastRenderTime", SDK_FIELD(AActor, LastRenderTime));  // mevr crowd
    CheckField("Engine.PrimitiveComponent", "HiddenGame", SDK_BOOL(UPrimitiveComponent, HiddenGame));
    CheckField("Engine.PrimitiveComponent", "bOnlyOwnerSee", SDK_BOOL(UPrimitiveComponent, bOnlyOwnerSee));  // the seated driver
    // The Mako's thrusters (game/MakoControls): longer, pitched by the stick.
    CheckField("SFXGame.BioPlayerController", "bJumpJetsOn", SDK_FIELD(ABioPlayerController, bJumpJetsOn));
    CheckField("SFXGame.BioVehicleWheeled", "CurrentJumpFuel", SDK_FIELD(ABioVehicleWheeled, CurrentJumpFuel));
    CheckField("SFXGame.BioVehicleWheeled", "CurrentBoostFuel", SDK_FIELD(ABioVehicleWheeled, CurrentBoostFuel));
    CheckField("SFXGame.BioVehicleWheeled", "MaxJumpFuel", SDK_FIELD(ABioVehicleWheeled, MaxJumpFuel));
    CheckField("SFXGame.BioVehicleWheeled", "MaxBoostFuel", SDK_FIELD(ABioVehicleWheeled, MaxBoostFuel));
    CheckField("SFXGame.BioVehicleWheeled", "bJumping", SDK_BOOL(ABioVehicleWheeled, bJumping));
    CheckField("SFXGame.BioVehicleWheeled", "bBoosting", SDK_BOOL(ABioVehicleWheeled, bBoosting));
    CheckField("SFXGame.BioPawn", "m_oHeadMesh", SDK_FIELD(ABioPawn, m_oHeadMesh));
    CheckField("SFXGame.BioPawn", "m_oHairMesh", SDK_FIELD(ABioPawn, m_oHairMesh));
    CheckField("SFXGame.BioPawn", "m_oHeadGearMesh", SDK_FIELD(ABioPawn, m_oHeadGearMesh));
    CheckField("SFXGame.BioPawn", "m_oVisorMesh", SDK_FIELD(ABioPawn, m_oVisorMesh));
    CheckField("SFXGame.BioPawn", "m_oFacePlateMesh", SDK_FIELD(ABioPawn, m_oFacePlateMesh));
    CheckField("Engine.Pawn", "Health", SDK_FIELD(APawn, Health));
    CheckField("Engine.WorldInfo", "bPlayersOnly", SDK_BOOL(AWorldInfo, bPlayersOnly));
    CheckField("Engine.WorldInfo", "bPlayersOnlyPending", SDK_BOOL(AWorldInfo, bPlayersOnlyPending));
    CheckField("SFXGame.BioWorldInfo", "m_bPausedByFocusLoss", SDK_BOOL(ABioWorldInfo, m_bPausedByFocusLoss));  // game/FocusPause
    CheckField("Engine.PrimitiveComponent", "bOwnerNoSee", SDK_BOOL(UPrimitiveComponent, bOwnerNoSee));
    CheckField("Engine.Actor", "Instigator", SDK_FIELD(AActor, Instigator));
    CheckField("Engine.Actor", "Tag", SDK_FIELD(AActor, Tag));
    // The galaxy hologram (game/GalaxyMap, research/51 section 8).
    CheckField("Engine.WorldInfo", "Game", SDK_FIELD(AWorldInfo, Game));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pDynamicObjects", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pDynamicObjects));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_sSelectableObject", SDK_FIELD(UBioCameraBehaviorGalaxy, m_sSelectableObject));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_nCurrentState", SDK_FIELD(UBioCameraBehaviorGalaxy, m_nCurrentState));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pCenterObject", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pCenterObject));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pCrossHairObject", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pCrossHairObject));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedObject", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pSelectedObject));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedCluster", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pSelectedCluster));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pSelectedSystem", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pSelectedSystem));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pClusterTable", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pClusterTable));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pRelayTable", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pRelayTable));
    CheckField("Engine.Actor", "Acceleration", SDK_FIELD(AActor, Acceleration));
    CheckField("SFXGame.BioSFHandler_GalaxyMap", "m_bWaitingForMsgBox", SDK_BOOL(UBioSFHandler_GalaxyMap, m_bWaitingForMsgBox));
    CheckField("Engine.BioSFManager", "m_lstPanels", SDK_FIELD(UBioSFManager, m_lstPanels));  // game/WorldUi (research/53)
    CheckField("Engine.BioSFManager", "m_FocusPanel", SDK_FIELD(UBioSFManager, m_FocusPanel));
    CheckField("Engine.BioSFPanel", "nmTag", SDK_FIELD(UBioSFPanel, nmTag));
    CheckField("Engine.BioSFPanel", "bFullScreen", SDK_BOOL(UBioSFPanel, bFullScreen));
    CheckField("Engine.BioSFPanel", "m_DefaultHandler", SDK_FIELD(UBioSFPanel, m_DefaultHandler));
    CheckField("Engine.BioSFPanel", "IsVisible", SDK_BOOL(UBioSFPanel, IsVisible));
    CheckField("SFXGame.BioSFHandler_Loot", "m_oLootInventory", SDK_FIELD(UBioSFHandler_Loot, m_oLootInventory));
    CheckField("SFXGame.BioSkillGame_Handler", "m_oDependent", SDK_FIELD(UBioSkillGame_Handler, m_oDependent));
    CheckField("SFXGame.BioSFHandler_Shop", "m_shop", SDK_FIELD(UBioSFHandler_Shop, m_shop));
    CheckField("Engine.SeqAct_Delay", "Duration", SDK_FIELD(USeqAct_Delay, Duration));
    CheckField("Engine.SequenceEvent", "Originator", SDK_FIELD(USequenceEvent, Originator));
    CheckField("Engine.Controller", "Pawn", SDK_FIELD(AController, Pawn));
    CheckField("Engine.Pawn", "Mesh", SDK_FIELD(APawn, Mesh));
    CheckField("Engine.Pawn", "Weapon", SDK_FIELD(APawn, Weapon));
    CheckField("Engine.Pawn", "InvManager", SDK_FIELD(APawn, InvManager));
    CheckField("Engine.Pawn", "NextPawn", SDK_FIELD(APawn, NextPawn));
    CheckField("Engine.Pawn", "bIsCrouched", SDK_BOOL(APawn, bIsCrouched));
    CheckField("Engine.Pawn", "bWantsToCrouch", SDK_BOOL(APawn, bWantsToCrouch));
    CheckField("Engine.PlayerController", "PlayerCamera", SDK_FIELD(APlayerController, PlayerCamera));
    CheckField("Engine.PlayerController", "PlayerInput", SDK_FIELD(APlayerController, PlayerInput));
    CheckField("Engine.PlayerController", "m_oCachedScaleFormManager", SDK_FIELD(APlayerController, m_oCachedScaleFormManager));
    CheckField("Engine.Camera", "CameraCache", SDK_FIELD(ACamera, CameraCache));
    CheckField("Engine.Camera", "PCOwner", SDK_FIELD(ACamera, PCOwner));
    CheckField("Engine.WorldInfo", "PawnList", SDK_FIELD(AWorldInfo, PawnList));
    CheckField("Engine.WorldInfo", "TimeSeconds", SDK_FIELD(AWorldInfo, TimeSeconds));  // mevr crowd
    CheckField("SFXGame.BioTriggerStream", "StreamingStates", SDK_FIELD(ABioTriggerStream, StreamingStates));  // mevr stream
    // `mevr vehicle levels` and `event` (the cockpit probe, research/47)
    CheckField("Engine.WorldInfo", "StreamingLevels", SDK_FIELD(AWorldInfo, StreamingLevels));
    CheckField("Engine.LevelStreaming", "PackageName", SDK_FIELD(ULevelStreaming, PackageName));
    CheckField("Engine.LevelStreaming", "LoadedLevel", SDK_FIELD(ULevelStreaming, LoadedLevel));
    CheckField("Engine.LevelStreaming", "bShouldBeLoaded", SDK_BOOL(ULevelStreaming, bShouldBeLoaded));
    CheckField("Engine.LevelStreaming", "bShouldBeVisible", SDK_BOOL(ULevelStreaming, bShouldBeVisible));
    CheckField("Engine.LevelStreaming", "bIsVisible", SDK_BOOL(ULevelStreaming, bIsVisible));
    CheckField("Engine.LevelStreaming", "bHasLoadRequestPending", SDK_BOOL(ULevelStreaming, bHasLoadRequestPending));
    CheckField("Engine.SeqEvent_RemoteEvent", "EventName", SDK_FIELD(USeqEvent_RemoteEvent, EventName));
    CheckField("Engine.WorldInfo", "WorldGravityZ", SDK_FIELD(AWorldInfo, WorldGravityZ));
    CheckField("Engine.PlayerInput", "RawJoyLookRight", SDK_FIELD(UPlayerInput, RawJoyLookRight));
    CheckField("Engine.PlayerInput", "RawJoyLookUp", SDK_FIELD(UPlayerInput, RawJoyLookUp));
    CheckField("Engine.Weapon", "Mesh", SDK_FIELD(AWeapon, Mesh));
    CheckField("Engine.Projectile", "Speed", SDK_FIELD(AProjectile, Speed));
    CheckField("Engine.ActorComponent", "Owner", SDK_FIELD(UActorComponent, Owner));
    CheckField("Engine.PrimitiveComponent", "LocalToWorld", SDK_FIELD(UPrimitiveComponent, LocalToWorld), true);
    CheckField("Engine.SkeletalMeshComponent", "SkeletalMesh", SDK_FIELD(USkeletalMeshComponent, SkeletalMesh));
    CheckField("Engine.SkeletalMeshComponent", "AnimTreeTemplate", SDK_FIELD(USkeletalMeshComponent, AnimTreeTemplate));
    CheckField("Engine.SkeletalMeshComponent", "Animations", SDK_FIELD(USkeletalMeshComponent, Animations));
    CheckField("Engine.SkeletalMeshComponent", "SpaceBases", SDK_FIELD(USkeletalMeshComponent, SpaceBases), true);
    CheckField("Engine.SkeletalMeshComponent", "ParentAnimComponent", SDK_FIELD(USkeletalMeshComponent, ParentAnimComponent));
    CheckField("Engine.SkeletalMeshComponent", "Attachments", SDK_FIELD(USkeletalMeshComponent, Attachments), true);
    CheckField("Engine.SkeletalMesh", "Bounds", SDK_FIELD(USkeletalMesh, Bounds));
    CheckField("Engine.SkeletalMesh", "RefSkeleton", SDK_FIELD(USkeletalMesh, RefSkeleton), true);
    CheckField("Engine.SkeletalMesh", "Sockets", SDK_FIELD(USkeletalMesh, Sockets));
    // game/sights' near plane and game/scope_parts' material log (research/21 section 14).
    CheckField("Engine.PlayerController", "Player", SDK_FIELD(APlayerController, Player));
    CheckField("Engine.LocalPlayer", "NearClipPlane", SDK_FIELD(ULocalPlayer, NearClipPlane));
    CheckField("Engine.MeshComponent", "Materials", SDK_FIELD(UMeshComponent, Materials));
    CheckField("Engine.SkeletalMesh", "Materials", SDK_FIELD(USkeletalMesh, Materials), true);
    CheckField("Engine.MaterialInstance", "Parent", SDK_FIELD(UMaterialInstance, Parent));
    CheckField("Engine.MaterialInstanceConstant", "ScalarParameterValues", SDK_FIELD(UMaterialInstanceConstant, ScalarParameterValues));
    CheckField("Engine.MaterialInstanceConstant", "VectorParameterValues", SDK_FIELD(UMaterialInstanceConstant, VectorParameterValues));
    CheckField("Engine.MaterialInstanceConstant", "TextureParameterValues", SDK_FIELD(UMaterialInstanceConstant, TextureParameterValues));
    CheckField("Engine.SkeletalMeshSocket", "SocketName", SDK_FIELD(USkeletalMeshSocket, SocketName));
    CheckField("Engine.SkeletalMeshSocket", "BoneName", SDK_FIELD(USkeletalMeshSocket, BoneName));
    CheckField("Engine.SkeletalMeshSocket", "RelativeLocation", SDK_FIELD(USkeletalMeshSocket, RelativeLocation));
    CheckField("Engine.SkeletalMeshSocket", "RelativeRotation", SDK_FIELD(USkeletalMeshSocket, RelativeRotation));
    CheckField("Engine.SkeletalMeshSocket", "RelativeScale", SDK_FIELD(USkeletalMeshSocket, RelativeScale));
    CheckField("Engine.AnimTree", "SkelControlLists", SDK_FIELD(UAnimTree, SkelControlLists));
    CheckField("Engine.SkelControlBase", "NextControl", SDK_FIELD(USkelControlBase, NextControl));
    CheckField("Engine.SkelControlBase", "ControlName", SDK_FIELD(USkelControlBase, ControlName));
    CheckField("Engine.SkelControlBase", "ControlStrength", SDK_FIELD(USkelControlBase, ControlStrength));
    CheckField("Engine.SkelControlBase", "StrengthTarget", SDK_FIELD(USkelControlBase, StrengthTarget));
    CheckField("Engine.SkelControlBase", "bSetStrengthFromAnimNode", SDK_BOOL(USkelControlBase, bSetStrengthFromAnimNode));
    CheckField("Engine.SkelControlLimb", "EffectorLocationSpace", SDK_FIELD(USkelControlLimb, EffectorLocationSpace));
    CheckField("Engine.SkelControlLimb", "EffectorSpaceBoneName", SDK_FIELD(USkelControlLimb, EffectorSpaceBoneName));
    CheckField("Engine.SkelControlLimb", "JointTargetLocationSpace", SDK_FIELD(USkelControlLimb, JointTargetLocationSpace));
    CheckField("Engine.SkelControlLimb", "JointTargetSpaceBoneName", SDK_FIELD(USkelControlLimb, JointTargetSpaceBoneName));
    CheckField("Engine.SkelControlLimb", "BoneAxis", SDK_FIELD(USkelControlLimb, BoneAxis));
    CheckField("Engine.SkelControlLimb", "JointAxis", SDK_FIELD(USkelControlLimb, JointAxis));
    CheckField("Engine.SkelControlLimb", "bInvertJointAxis", SDK_BOOL(USkelControlLimb, bInvertJointAxis));
    CheckField("Engine.SceneCaptureComponent", "FrameRate", SDK_FIELD(USceneCaptureComponent, FrameRate));
    // The scope capture (game/ScopeCapture, research/39).
    CheckField("Engine.SceneCaptureComponent", "MaxUpdateDist", SDK_FIELD(USceneCaptureComponent, MaxUpdateDist));
    CheckField("Engine.SceneCaptureComponent", "PostProcess", SDK_FIELD(USceneCaptureComponent, PostProcess));
    CheckField("Engine.SceneCaptureComponent", "bEnabled", SDK_BOOL(USceneCaptureComponent, bEnabled));
    CheckField("Engine.SceneCaptureComponent", "bEnablePostProcess", SDK_BOOL(USceneCaptureComponent, bEnablePostProcess));
    CheckField("Engine.SceneCaptureComponent", "bEnableFog", SDK_BOOL(USceneCaptureComponent, bEnableFog));
    CheckField("Engine.SceneCaptureComponent", "bEnableParticles", SDK_BOOL(USceneCaptureComponent, bEnableParticles));
    CheckField("Engine.SceneCaptureComponent", "bUseMainScenePostProcessSettings",
        SDK_BOOL(USceneCaptureComponent, bUseMainScenePostProcessSettings));
    CheckField("Engine.SceneCaptureComponent", "bSkipUpdateIfTextureUsersOccluded",
        SDK_BOOL(USceneCaptureComponent, bSkipUpdateIfTextureUsersOccluded));
    CheckField("Engine.SceneCaptureComponent", "bSkipUpdateIfOwnerOccluded", SDK_BOOL(USceneCaptureComponent, bSkipUpdateIfOwnerOccluded));
    CheckField("Engine.SceneCapture2DComponent", "TextureTarget", SDK_FIELD(USceneCapture2DComponent, TextureTarget));
    CheckField("Engine.SceneCapture2DComponent", "FieldOfView", SDK_FIELD(USceneCapture2DComponent, FieldOfView));
    CheckField("Engine.SceneCaptureActor", "SceneCapture", SDK_FIELD(ASceneCaptureActor, SceneCapture));
    CheckField("Engine.Actor", "bStatic", SDK_BOOL(AActor, bStatic));
    CheckField("Engine.Actor", "bNoDelete", SDK_BOOL(AActor, bNoDelete));
    CheckField("Engine.Texture", "Resource", SDK_FIELD(UTexture, Resource));
    // The cockpit probe (game/CockpitProbe, research/47).
    CheckField("Engine.Actor", "bMovable", SDK_BOOL(AActor, bMovable));
    CheckField("Engine.Actor", "Role", SDK_FIELD(AActor, Role));
    CheckField("Engine.Actor", "Base", SDK_FIELD(AActor, Base));
    CheckField("Engine.Emitter", "ParticleSystemComponent", SDK_FIELD(AEmitter, ParticleSystemComponent));  // game/OmniTool
    CheckField("Engine.ParticleSystemComponent", "Template", SDK_FIELD(UParticleSystemComponent, Template));  // game/OmniTool
    CheckField("Engine.Actor", "BaseBoneName", SDK_FIELD(AActor, BaseBoneName));  // game/OmniTool
    CheckField("Engine.Actor", "RelativeLocation", SDK_FIELD(AActor, RelativeLocation));  // game/OmniTool
    CheckField("Engine.Actor", "RelativeRotation", SDK_FIELD(AActor, RelativeRotation));  // game/OmniTool
    CheckField("Engine.DynamicSMActor", "StaticMeshComponent", SDK_FIELD(ADynamicSMActor, StaticMeshComponent));
    CheckField("Engine.DynamicSMActor", "LightEnvironment", SDK_FIELD(ADynamicSMActor, LightEnvironment));
    CheckField("Engine.StaticMeshActor", "StaticMeshComponent", SDK_FIELD(AStaticMeshActor, StaticMeshComponent));
    CheckField("Engine.StaticMeshComponent", "StaticMesh", SDK_FIELD(UStaticMeshComponent, StaticMesh));
    CheckField("Engine.MaterialInstance", "bHasStaticPermutationResource", SDK_BOOL(UMaterialInstance, bHasStaticPermutationResource));
    CheckField("Engine.Material", "Expressions", SDK_FIELD(UMaterial, Expressions));
    CheckField("Engine.Material", "EmissiveColor", SDK_FIELD(UMaterial, EmissiveColor));
    CheckField("Engine.Material", "BlendMode", SDK_FIELD(UMaterial, BlendMode));
    CheckField("Engine.Material", "LightingModel", SDK_FIELD(UMaterial, LightingModel));
    CheckField("Engine.Material", "bUsedWithStaticLighting", SDK_BOOL(UMaterial, bUsedWithStaticLighting));
    CheckField("Engine.Material", "bUsedWithLightEnvironments", SDK_BOOL(UMaterial, bUsedWithLightEnvironments));
    CheckField("Engine.MaterialExpressionTextureSampleParameter", "ParameterName", SDK_FIELD(UMaterialExpressionTextureSampleParameter, ParameterName));
    CheckPath("Engine.Material", {"EmissiveColor", "Expression"}, SDK_AT(offsetof(UMaterial, EmissiveColor) + offsetof(FMaterialInput, Expression), 8));
    CheckField("Engine.TextureRenderTarget2D", "SizeX", SDK_FIELD(UTextureRenderTarget2D, SizeX));
    CheckField("Engine.TextureRenderTarget2D", "SizeY", SDK_FIELD(UTextureRenderTarget2D, SizeY));
    CheckField("Engine.TextureRenderTarget2D", "Format", SDK_FIELD(UTextureRenderTarget2D, Format));
    CheckField("Engine.TextureRenderTarget2D", "bForceLinearGamma", SDK_BOOL(UTextureRenderTarget2D, bForceLinearGamma));
    CheckField("Engine.SceneCapture2DComponent", "bUpdateMatrices", SDK_BOOL(USceneCapture2DComponent, bUpdateMatrices));
    CheckField("Engine.SceneCapture2DComponent", "ViewMatrix", SDK_FIELD(USceneCapture2DComponent, ViewMatrix));
    CheckField("Engine.MeshComponent", "Materials", SDK_FIELD(UMeshComponent, Materials));
    CheckField("Engine.PrimitiveComponent", "LightEnvironment", SDK_FIELD(UPrimitiveComponent, LightEnvironment));
    CheckField("Engine.LightEnvironmentComponent", "bEnabled", SDK_BOOL(ULightEnvironmentComponent, bEnabled));
    CheckField("Engine.Texture", "SRGB", SDK_BOOL(UTexture, SRGB));
    CheckField("Engine.Texture", "NeverStream", SDK_BOOL(UTexture, NeverStream));
    CheckField("Engine.Texture", "LODGroup", SDK_FIELD(UTexture, LODGroup));
    CheckField("Engine.Texture2D", "SizeX", SDK_FIELD(UTexture2D, SizeX));
    CheckField("Engine.Texture2D", "SizeY", SDK_FIELD(UTexture2D, SizeY));
    CheckField("Engine.Texture2D", "Format", SDK_FIELD(UTexture2D, Format));
    CheckField("Engine.Texture2D", "RequestedMips", SDK_FIELD(UTexture2D, RequestedMips));
    CheckField("Engine.Texture2D", "ResidentMips", SDK_FIELD(UTexture2D, ResidentMips));
    CheckField("Engine.Texture2D", "MipTailBaseIdx", SDK_FIELD(UTexture2D, MipTailBaseIdx));
    CheckField("Engine.Texture2D", "Mips", SDK_FIELD(UTexture2D, Mips));
    CheckPath("Engine.MaterialInstanceConstant", {"TextureParameterValues", "ParameterValue"}, SDK_AT(offsetof(FTextureParameterValue, ParameterValue), 8));
    CheckField("Engine.PlayerController", "Player", SDK_FIELD(APlayerController, Player));
    CheckField("Engine.LocalPlayer", "PlayerPostProcess", SDK_FIELD(ULocalPlayer, PlayerPostProcess));
    CheckField("Engine.Engine", "DefaultPostProcess", SDK_FIELD(UEngine, DefaultPostProcess));
    CheckField("Engine.Engine", "GamePlayers", SDK_FIELD(UEngine, GamePlayers));
    CheckField("Engine.Player", "Actor", SDK_FIELD(UPlayer, Actor));
    CheckField("Engine.BioSFHandler", "oPanel", SDK_FIELD(UBioSFHandler, oPanel));
    CheckField("Engine.BioSFPanel", "MovieRef", SDK_FIELD(UBioSFPanel, MovieRef));
    CheckField("Engine.BioSFPanel", "fMovieAspectRatio", SDK_FIELD(UBioSFPanel, fMovieAspectRatio));
    CheckField("Engine.BioSFPanel", "bUsingGamepad", SDK_BOOL(UBioSFPanel, bUsingGamepad));
    // SFXGame
    CheckField("SFXGame.BioPawn", "CoverType", SDK_FIELD(ABioPawn, CoverType));
    CheckField("SFXGame.BioPawn", "CoverAction", SDK_FIELD(ABioPawn, CoverAction));
    CheckField("SFXGame.BioPawn", "CoverTestAngle", SDK_FIELD(ABioPawn, CoverTestAngle));
    CheckField("SFXGame.BioPawn", "m_oBehavior", SDK_FIELD(ABioPawn, m_oBehavior));
    CheckField("SFXGame.BioPlayerController", "GameModeManager", SDK_FIELD(ABioPlayerController, GameModeManager));
    CheckField("SFXGame.BioPlayerController", "bStorming", SDK_BOOL(ABioPlayerController, bStorming));
    CheckField("SFXGame.SFXGameModeManager", "GameModes", SDK_FIELD(USFXGameModeManager, GameModes));
    CheckField("SFXGame.SFXGameModeManager", "CurrentMode", SDK_FIELD(USFXGameModeManager, CurrentMode));
    CheckField("SFXGame.SFXGameModeBase", "bIsActive", SDK_BOOL(USFXGameModeBase, bIsActive));  // game/GameMenuRequest
    CheckField("SFXGame.SFXGameModeBase", "Priority", SDK_FIELD(USFXGameModeBase, Priority));
    CheckField("SFXGame.SFXInventoryManager", "WeaponRecoilOffset", SDK_FIELD(ASFXInventoryManager, WeaponRecoilOffset));
    CheckField("SFXGame.SFXInventoryManager", "TotalRecoil", SDK_FIELD(ASFXInventoryManager, TotalRecoil));
    CheckField("SFXGame.SFXModule_AimAssist", "LastZoomSnapTarget", SDK_FIELD(USFXModule_AimAssist, LastZoomSnapTarget));
    CheckField("SFXGame.SFXCameraNativeBase", "CurrentCameraMode", SDK_FIELD(ASFXCameraNativeBase, CurrentCameraMode));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "To", SDK_FIELD(USFXCameraMode_Interpolate, To));
    // The host's blend fix (plugin/HalcyonBridge, research/38 B) reads these at fixed offsets.
    CheckField("SFXGame.SFXCameraMode_Interpolate", "From", SDK_FIELD(USFXCameraMode_Interpolate, From));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "RotationOffset", SDK_FIELD(USFXCameraMode_Interpolate, RotationOffset));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "LocationOffset", SDK_FIELD(USFXCameraMode_Interpolate, LocationOffset));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "TotalTime", SDK_FIELD(USFXCameraMode_Interpolate, TotalTime));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "CurrentTime", SDK_FIELD(USFXCameraMode_Interpolate, CurrentTime));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "bComplete", SDK_BOOL(USFXCameraMode_Interpolate, bComplete));
    CheckField("SFXGame.SFXCameraMode", "Input", SDK_FIELD(USFXCameraMode, Input));
    CheckField("SFXGame.SFXCameraMode", "m_pov", SDK_FIELD(USFXCameraMode, m_pov));
    CheckField("SFXGame.SFXCameraMode", "Offset", SDK_FIELD(USFXCameraMode, Offset));
    CheckField("SFXGame.SFXCameraMode", "HookOffset", SDK_FIELD(USFXCameraMode, HookOffset));
    CheckField("SFXGame.SFXCameraMode", "LastHookPos", SDK_FIELD(USFXCameraMode, LastHookPos));
    CheckField("SFXGame.SFXCameraMode", "HookName", SDK_FIELD(USFXCameraMode, HookName));
    CheckField("SFXGame.SFXCameraMode", "bFirstPerson", SDK_BOOL(USFXCameraMode, bFirstPerson));
    CheckField("SFXGame.SFXCameraInput", "MaxCameraRotationSpeed", SDK_FIELD(USFXCameraInput, MaxCameraRotationSpeed));
    CheckField("SFXGame.SFXCameraInput", "CameraSensitivity", SDK_FIELD(USFXCameraInput, CameraSensitivity));
    CheckField("SFXGame.SFXCameraInput", "TimeToReachFullSpeed", SDK_FIELD(USFXCameraInput, TimeToReachFullSpeed));
    CheckField("SFXGame.SFXCameraInput", "StickDeadZone", SDK_FIELD(USFXCameraInput, StickDeadZone));
    CheckField("SFXGame.BioWeapon", "SkelMeshComp", SDK_FIELD(ABioWeapon, SkelMeshComp));
    CheckField("SFXGame.BioWeaponRanged", "m_MuzzleSocketName", SDK_FIELD(ABioWeaponRanged, m_MuzzleSocketName));
    CheckField("SFXGame.BioWeaponRanged", "m_fMinVFXDecalIntensity", SDK_FIELD(ABioWeaponRanged, m_fMinVFXDecalIntensity));
    CheckField("SFXGame.BioWeaponRanged", "bIsZoomed", SDK_BOOL(ABioWeaponRanged, bIsZoomed));
    // The game's own magnifications (game/sights, research/21 section 16).
    CheckField("SFXGame.BioWeaponRanged", "Stats", SDK_FIELD(ABioWeaponRanged, Stats));
    CheckField("SFXGame.SFXWeaponStatsBase", "AimModes", SDK_FIELD(USFXWeaponStatsBase, AimModes));
    CheckElement("SFXGame.SFXWeaponStatsBase", "AimModes", static_cast<long long>(sizeof(FWeaponAimMode)));
    CheckPath("SFXGame.SFXWeaponStatsBase", {"AimModes", "ZoomFOV"}, SDK_AT(offsetof(FWeaponAimMode, ZoomFOV), 4));
    CheckPath("SFXGame.SFXWeaponStatsBase", {"AimModes", "bScoped"}, SDK_BOOL(FWeaponAimMode, bScoped));
    // Vent (research/45)
    CheckField("SFXGame.BioWeaponRanged", "m_fCurrentHeat", SDK_FIELD(ABioWeaponRanged, m_fCurrentHeat));
    CheckField("SFXGame.BioWeaponRanged", "m_bOverheated", SDK_BOOL(ABioWeaponRanged, m_bOverheated));
    CheckField("SFXGame.BioPawnBehavior", "m_oEquipment", SDK_FIELD(UBioPawnBehavior, m_oEquipment));
    // game/combat_stance (research/38): the AnimTree walk, the draw rate, the speed ramp, the walking speed.
    CheckField("Engine.AnimNode", "NodeName", SDK_FIELD(UAnimNode, NodeName));
    CheckField("Engine.AnimNode", "NodeTickTag", SDK_FIELD(UAnimNode, NodeTickTag));
    CheckField("Engine.AnimNodeBlendBase", "Children", SDK_FIELD(UAnimNodeBlendBase, Children));
    CheckField("Engine.AnimNodeSequence", "AnimSeqName", SDK_FIELD(UAnimNodeSequence, AnimSeqName));
    CheckField("Engine.AnimNodeSequence", "Rate", SDK_FIELD(UAnimNodeSequence, Rate));
    CheckField("Engine.AnimNodeSequence", "CurrentTime", SDK_FIELD(UAnimNodeSequence, CurrentTime));
    CheckField("Engine.AnimNodeSequence", "bPlaying", SDK_BOOL(UAnimNodeSequence, bPlaying));
    CheckField("Engine.Actor", "Velocity", SDK_FIELD(AActor, Velocity));
    // game/mako (research/44)
    CheckField("SFXGame.BioVehicleBase", "m_oBehavior", SDK_FIELD(ABioVehicleBase, m_oBehavior));
    CheckField("SFXGame.BioVehicleBehaviorBase", "m_aVehicleWeaponInfo", SDK_FIELD(UBioVehicleBehaviorBase, m_aVehicleWeaponInfo));
    CheckField("SFXGame.BioSkelControl_TurretConstrained", "DesiredBoneRotation",
        SDK_FIELD(UBioSkelControl_TurretConstrained, DesiredBoneRotation));
    CheckField("SFXGame.BioSkelControl_TurretConstrained", "bConstrainYaw", SDK_BOOL(UBioSkelControl_TurretConstrained, bConstrainYaw));
    CheckField("SFXGame.BioSkelControl_TurretConstrained", "bConstrainPitch", SDK_BOOL(UBioSkelControl_TurretConstrained, bConstrainPitch));
    // game/MakoControls (research/46)
    CheckField("Engine.Vehicle", "Throttle", SDK_FIELD(AVehicle, Throttle));
    CheckField("Engine.Vehicle", "Steering", SDK_FIELD(AVehicle, Steering));
    CheckField("Engine.SVehicle", "SimObj", SDK_FIELD(ASVehicle, SimObj));
    CheckField("Engine.SVehicle", "MaxSpeed", SDK_FIELD(ASVehicle, MaxSpeed));
    CheckField("Engine.SVehicle", "ForwardVel", SDK_FIELD(ASVehicle, ForwardVel));
    CheckField("Engine.SVehicle", "OutputGas", SDK_FIELD(ASVehicle, OutputGas));
    CheckField("Engine.SVehicle", "OutputSteering", SDK_FIELD(ASVehicle, OutputSteering));
    CheckField("Engine.SVehicle", "OutputBrake", SDK_FIELD(ASVehicle, OutputBrake));
    CheckField("Engine.SVehicleSimCar", "ActualSteering", SDK_FIELD(USVehicleSimCar, ActualSteering));
    CheckField("Engine.SVehicleSimTank", "LeftTrackVel", SDK_FIELD(USVehicleSimTank, LeftTrackVel));
    CheckField("Engine.SVehicleSimTank", "RightTrackVel", SDK_FIELD(USVehicleSimTank, RightTrackVel));
    CheckField("Engine.SVehicleSimTank", "TurnInPlaceThrottle", SDK_FIELD(USVehicleSimTank, TurnInPlaceThrottle));
    CheckField("SFXGame.BioSVehicleSimTank", "m_fCurrentInputYaw", SDK_FIELD(UBioSVehicleSimTank, m_fCurrentInputYaw));
    CheckField("SFXGame.BioVehicleBehaviorBase", "m_bPowertrainEnabled", SDK_BOOL(UBioVehicleBehaviorBase, m_bPowertrainEnabled));
    CheckField("SFXGame.BioVehicleBehaviorBase", "m_bIsPaused", SDK_BOOL(UBioVehicleBehaviorBase, m_bIsPaused));
    CheckField("SFXGame.BioPlayerController", "bUseCameraRelativeVehicleSteering", SDK_BOOL(ABioPlayerController, bUseCameraRelativeVehicleSteering));
    CheckField("SFXGame.BioPawnBehavior", "m_fMaxDesSpdAccelRate", SDK_FIELD(UBioPawnBehavior, m_fMaxDesSpdAccelRate));
    CheckField("SFXGame.BioPawnBehavior", "m_fMaxDesSpdDecelRate", SDK_FIELD(UBioPawnBehavior, m_fMaxDesSpdDecelRate));
    CheckField("SFXGame.BioPawnBehavior", "m_fMaxDesSpdTacAccelRate", SDK_FIELD(UBioPawnBehavior, m_fMaxDesSpdTacAccelRate));
    CheckField("SFXGame.BioPawnBehavior", "m_fMaxDesSpdTacDecelRate", SDK_FIELD(UBioPawnBehavior, m_fMaxDesSpdTacDecelRate));
    CheckField("SFXGame.BioEpicPawnBehavior", "Squad", SDK_FIELD(UBioEpicPawnBehavior, Squad));
    CheckField("SFXGame.BioActorBehavior", "ActorGameName", SDK_FIELD(UBioActorBehavior, ActorGameName));
    CheckField("SFXGame.BioActorBehavior", "m_oActorType", SDK_FIELD(UBioActorBehavior, m_oActorType));
    CheckField("SFXGame.BioActorType", "ActorGameName", SDK_FIELD(UBioActorType, ActorGameName));
    CheckField("SFXGame.BioEquipment", "m_QuickSlotArray", SDK_FIELD(UBioEquipment, m_QuickSlotArray));
    CheckField("SFXGame.BioEquipment", "m_eQuickSlotCurrent", SDK_FIELD(UBioEquipment, m_eQuickSlotCurrent));
    CheckField("SFXGame.BioBaseSquad", "Members", SDK_FIELD(ABioBaseSquad, Members));
    CheckField("SFXGame.BioGrenade", "m_clsProjectile", SDK_FIELD(ABioGrenade, m_clsProjectile));
    CheckField("SFXGame.BioGrenadeProjectileFrisbee", "m_vIntendedTargetLocation",
        SDK_FIELD(ABioGrenadeProjectileFrisbee, m_vIntendedTargetLocation));
    CheckField("SFXGame.BioGrenadeProjectileFrisbee", "m_oIntendedTarget", SDK_FIELD(ABioGrenadeProjectileFrisbee, m_oIntendedTarget));
    CheckField("SFXGame.BioGrenadeProjectileFrisbee", "m_oHeatSeekTarget", SDK_FIELD(ABioGrenadeProjectileFrisbee, m_oHeatSeekTarget));
    CheckField("SFXGame.BioGrenadeProjectileFrisbee", "m_vStartLocation", SDK_FIELD(ABioGrenadeProjectileFrisbee, m_vStartLocation));
    CheckField("SFXGame.MassEffectGuiManager", "m_oSFHudHandler", SDK_FIELD(UMassEffectGuiManager, m_oSFHudHandler));
    CheckField("SFXGame.BioSFHandler_HUD", "vInput", SDK_FIELD(UBioSFHandler_HUD, vInput));
    CheckField("SFXGame.BioSFHandler_HUD", "m_nShepardIndex", SDK_FIELD(UBioSFHandler_HUD, m_nShepardIndex));
    CheckField("SFXGame.BioSFHandler_HUD", "m_nHench1Index", SDK_FIELD(UBioSFHandler_HUD, m_nHench1Index));
    CheckField("SFXGame.BioSFHandler_HUD", "m_nHench2Index", SDK_FIELD(UBioSFHandler_HUD, m_nHench2Index));
    CheckField("SFXGame.BioSFHandler_HUD", "nAngle", SDK_FIELD(UBioSFHandler_HUD, nAngle));
    CheckField("SFXGame.BioSFHandler_HUD", "nLastAngle", SDK_FIELD(UBioSFHandler_HUD, nLastAngle));
    // game/interact (research/40): what A would use, and where it is.
    CheckField("SFXGame.BioPlayerController", "m_oPlayerSelection", SDK_FIELD(ABioPlayerController, m_oPlayerSelection));
    CheckField("SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget", SDK_FIELD(UBioPlayerSelection, m_oCurrentSelectionTarget));
    CheckField("SFXGame.BioPlayerSelection", "m_bCurrentSelectionIsCombatTarget",
        SDK_BOOL(UBioPlayerSelection, m_bCurrentSelectionIsCombatTarget));
    CheckField("Engine.Actor", "CollisionComponent", SDK_FIELD(AActor, CollisionComponent));
    CheckField("Engine.PrimitiveComponent", "Bounds", SDK_FIELD(UPrimitiveComponent, Bounds), true);
    // game/cockpit_probe's `mevr vehicle size` (research/47): a mesh's size in the world.
    CheckField("Engine.Actor", "DrawScale", SDK_FIELD(AActor, DrawScale));
    CheckField("Engine.Actor", "DrawScale3D", SDK_FIELD(AActor, DrawScale3D));
    CheckField("Engine.PrimitiveComponent", "Scale", SDK_FIELD(UPrimitiveComponent, Scale));
    CheckField("Engine.PrimitiveComponent", "Scale3D", SDK_FIELD(UPrimitiveComponent, Scale3D));
}

void CheckPaths() {
    CheckPath("Engine.Camera", {"ViewTarget", "Target"}, SDK_AT(offsetof(ACamera, ViewTarget) + offsetof(FTViewTarget, Target), 8));  // the cockpit probe
    CheckPath("Engine.Actor", {"Rotation", "Pitch"}, SDK_AT(offsetof(AActor, Rotation) + offsetof(FRotator, Pitch), 4));
    CheckElement("SFXGame.BioTriggerStream", "StreamingStates", static_cast<long long>(sizeof(FBioStreamingState)));  // mevr stream
    CheckPath("SFXGame.BioTriggerStream", {"StreamingStates", "StateName"}, SDK_AT(offsetof(FBioStreamingState, StateName), 8));
    CheckPath("SFXGame.BioTriggerStream", {"StreamingStates", "InChunkName"}, SDK_AT(offsetof(FBioStreamingState, InChunkName), 8));
    CheckPath("SFXGame.BioTriggerStream", {"StreamingStates", "VisibleChunkNames"}, SDK_AT(offsetof(FBioStreamingState, VisibleChunkNames), 16));
    CheckPath("SFXGame.BioTriggerStream", {"StreamingStates", "LoadChunkNames"}, SDK_AT(offsetof(FBioStreamingState, LoadChunkNames), 16));
    CheckPath("Engine.Actor", {"Rotation", "Yaw"}, SDK_AT(offsetof(AActor, Rotation) + offsetof(FRotator, Yaw), 4));
    CheckPath("Engine.Actor", {"Rotation", "Roll"}, SDK_AT(offsetof(AActor, Rotation) + offsetof(FRotator, Roll), 4));
    CheckPath("Engine.Actor", {"Location", "X"}, SDK_AT(offsetof(AActor, Location) + offsetof(FVector, X), 4));
    CheckPath("Engine.Actor", {"Location", "Y"}, SDK_AT(offsetof(AActor, Location) + offsetof(FVector, Y), 4));
    CheckPath("Engine.Actor", {"Location", "Z"}, SDK_AT(offsetof(AActor, Location) + offsetof(FVector, Z), 4));
    CheckPath("Engine.PrimitiveComponent", {"Bounds", "Origin"},
        SDK_AT(offsetof(UPrimitiveComponent, Bounds) + offsetof(FBoxSphereBounds, Origin), sizeof(FVector)));
    CheckPath("Engine.PrimitiveComponent", {"Bounds", "BoxExtent"},
        SDK_AT(offsetof(UPrimitiveComponent, Bounds) + offsetof(FBoxSphereBounds, BoxExtent), sizeof(FVector)));
    CheckPath("Engine.Camera", {"CameraCache", "POV"}, SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV), sizeof(FTPOV)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "Location"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, Location), sizeof(FVector)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "Rotation"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, Rotation), sizeof(FRotator)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "FOV"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, FOV), sizeof(float)));
    CheckPath("SFXGame.SFXCameraMode", {"m_pov", "Location"}, SDK_AT(offsetof(USFXCameraMode, m_pov) + offsetof(FTPOV, Location), sizeof(FVector)));
    CheckPath("SFXGame.SFXCameraMode", {"m_pov", "Rotation"}, SDK_AT(offsetof(USFXCameraMode, m_pov) + offsetof(FTPOV, Rotation), sizeof(FRotator)));
    CheckPath("SFXGame.SFXCameraInput", {"CameraSensitivity", "X"},
        SDK_AT(offsetof(USFXCameraInput, CameraSensitivity) + offsetof(FVector2D, X), 4));
    CheckPath("SFXGame.SFXCameraInput", {"CameraSensitivity", "Y"},
        SDK_AT(offsetof(USFXCameraInput, CameraSensitivity) + offsetof(FVector2D, Y), 4));
    CheckPath("Engine.SkeletalMesh", {"Bounds", "Origin"}, SDK_AT(offsetof(USkeletalMesh, Bounds) + offsetof(FBoxSphereBounds, Origin), sizeof(FVector)));
    CheckPath("Engine.SkeletalMesh", {"Bounds", "BoxExtent"},
        SDK_AT(offsetof(USkeletalMesh, Bounds) + offsetof(FBoxSphereBounds, BoxExtent), sizeof(FVector)));
    CheckElement("Engine.AnimTree", "SkelControlLists", sizeof(FSkelControlListHead));
    // game/mako (research/44): the turret each vehicle weapon sits on.
    CheckElement("SFXGame.BioVehicleBehaviorBase", "m_aVehicleWeaponInfo", sizeof(FBioVehicleWeaponInfo));
    CheckPath("SFXGame.BioVehicleBehaviorBase", {"m_aVehicleWeaponInfo", "oVehicleWeaponPtr"},
        SDK_AT(offsetof(FBioVehicleWeaponInfo, oVehicleWeaponPtr), 8));
    CheckPath("SFXGame.BioVehicleBehaviorBase", {"m_aVehicleWeaponInfo", "nTurretIndex"}, SDK_AT(offsetof(FBioVehicleWeaponInfo, nTurretIndex), 4));
    CheckPath("Engine.AnimTree", {"SkelControlLists", "BoneName"}, SDK_AT(offsetof(FSkelControlListHead, BoneName), 8));
    CheckPath("Engine.AnimTree", {"SkelControlLists", "ControlHead"}, SDK_AT(offsetof(FSkelControlListHead, ControlHead), 8));
    CheckElement("Engine.SkeletalMeshComponent", "Attachments", sizeof(FAttachment));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "Component"}, SDK_AT(offsetof(FAttachment, Component), 8));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "BoneName"}, SDK_AT(offsetof(FAttachment, BoneName), 8));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "RelativeLocation"}, SDK_AT(offsetof(FAttachment, RelativeLocation), sizeof(FVector)));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "RelativeRotation"}, SDK_AT(offsetof(FAttachment, RelativeRotation), sizeof(FRotator)));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "RelativeScale"}, SDK_AT(offsetof(FAttachment, RelativeScale), sizeof(FVector)));
    CheckElement("Engine.AnimNodeBlendBase", "Children", sizeof(FAnimBlendChild));  // game/combat_stance (research/38)
    CheckPath("Engine.AnimNodeBlendBase", {"Children", "Name"}, SDK_AT(offsetof(FAnimBlendChild, Name), 8));
    CheckPath("Engine.AnimNodeBlendBase", {"Children", "Anim"}, SDK_AT(offsetof(FAnimBlendChild, Anim), 8));
    CheckPath("Engine.AnimNodeBlendBase", {"Children", "Weight"}, SDK_AT(offsetof(FAnimBlendChild, Weight), 4));
    CheckElement("Engine.MaterialInstanceConstant", "ScalarParameterValues", sizeof(FScalarParameterValue));  // game/scope_parts
    CheckPath("Engine.MaterialInstanceConstant", {"ScalarParameterValues", "ParameterName"}, SDK_AT(offsetof(FScalarParameterValue, ParameterName), 8));
    CheckPath("Engine.MaterialInstanceConstant", {"ScalarParameterValues", "ParameterValue"}, SDK_AT(offsetof(FScalarParameterValue, ParameterValue), 4));
    CheckElement("Engine.MaterialInstanceConstant", "VectorParameterValues", sizeof(FVectorParameterValue));
    CheckPath("Engine.MaterialInstanceConstant", {"VectorParameterValues", "ParameterName"}, SDK_AT(offsetof(FVectorParameterValue, ParameterName), 8));
    CheckPath("Engine.MaterialInstanceConstant", {"VectorParameterValues", "ParameterValue"}, SDK_AT(offsetof(FVectorParameterValue, ParameterValue), 16));
    CheckElement("Engine.MaterialInstanceConstant", "TextureParameterValues", sizeof(FTextureParameterValue));
    CheckPath("Engine.MaterialInstanceConstant", {"TextureParameterValues", "ParameterName"}, SDK_AT(offsetof(FTextureParameterValue, ParameterName), 8));
    CheckPath("Engine.MaterialInstanceConstant", {"TextureParameterValues", "ParameterValue"}, SDK_AT(offsetof(FTextureParameterValue, ParameterValue), 8));
    CheckElement("SFXGame.BioBaseSquad", "Members", sizeof(FMemberData));
    CheckPath("SFXGame.BioBaseSquad", {"Members", "SquadMember"}, SDK_AT(offsetof(FMemberData, SquadMember), 8));
    CheckPath("SFXGame.BioBaseSquad", {"Members", "eSwitchWeapon"}, SDK_AT(offsetof(FMemberData, eSwitchWeapon), 1));
}

// Stage 3: engine/skeleton's fields as it resolved them through our layer (TryStartup), against research/14's
// LE1 offsets, which it read at until stage 3 (its static_asserts against LExSDKv2 before that).
void CheckSkeleton() {
    skeleton::TryStartup();  // resolves them now (once; the skeleton probe starts it again later)
    skeleton::Fields const f = skeleton::ResolvedFields();
    struct Item {
        char const* what;
        long long ours;
        long long research14;
    };
    Item const items[] = {
        {"SkeletalMesh.RefSkeleton", f.refSkeleton, 0xB4},
        {"SkeletalMeshComponent.SpaceBases", f.spaceBases, 0x2B0},
        {"PrimitiveComponent.LocalToWorld", f.localToWorld, 0xC0},
        {"SkeletalMeshComponent.Attachments", f.attachments, 0x3B4},
        {"Attachments[] element size", f.attachmentSize, 0x34},
        {"SkeletalMesh.Sockets", f.sockets, 0x144},
        {"SkeletalMeshSocket.RelativeLocation", f.socketLocation, 0x60},
        {"SkeletalMeshSocket.RelativeRotation", f.socketRotation, 0x6C},
        {"SkeletalMeshSocket.SocketName", f.socketName, 0x84},
        {"SkeletalMeshSocket.BoneName", f.socketBone, 0x8C},
    };
    for (Item const& item : items) {
        Add("skeleton", item.what, item.ours == item.research14, Hex(item.ours), std::format("research/14 {}", Hex(item.research14)));
    }
}

void CheckFunctions() {
    char const* const functions[] = {
        // mevr stream (the measuring loop's streaming volumes)
        "SFXGame.BioTriggerStream.Touch",
        // the omni-tool on Shepard's arm (game/OmniTool)
        "SFXGame.BioVisualEffect.CreateCrustEffect",
        "SFXGame.BioVisualEffect.SetLifeTime",
        "SFXGame.BioVisualEffect.SetState",
        "Engine.Actor.DetachComponent",
        "Engine.SkeletalMeshComponent.AttachComponent",
        "Engine.SkeletalMeshComponent.DetachComponent",
        "Engine.ParticleSystemComponent.ActivateSystem",
        "Engine.ParticleSystemComponent.DeactivateSystem",
        // the galaxy hologram (game/GalaxyMap)
        "SFXGame.BioSPGame.GetSimpleString",
        "Engine.Bio2DA.GetNumRows",
        "Engine.Bio2DA.GetNumColumns",
        "Engine.Bio2DA.GetColumnName",
        "Engine.Bio2DA.GetRowNumber",
        "Engine.Bio2DA.GetIntEntryII",
        // Weapon, HandAim, Sights
        "SFXGame.BioWeaponRanged.GetMuzzleLoc",
        "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation",
        "SFXGame.BioWeaponRanged.GetRangedWeaponType",
        "SFXGame.BioWeaponRanged.GetPlayerAimError",
        "SFXGame.BioWeaponRanged.TickFire",
        "SFXGame.BioWeaponRanged.GetAdjustedAim",
        "Engine.BioSFPanel.SetMovieVisibility",
        "Engine.BioSFPanel.InvokeMethod",
        "Engine.Bio2DA.GetFloatEntryII",
        "SFXGame.BioWorldInfo.CheckConditional",
        "SFXGame.BioArtPlaceableBehavior.OnArtPlaceableUsed",
        "SFXGame.BioSFHandler_GalaxyMap.TravelConfirm",
        "SFXGame.BioPawn.GetWeaponStartTraceLocation",
        "Engine.Pawn.GetWeaponStartTraceLocation",
        "SFXGame.BioPlayerController.GetAdjustedAimFor",
        "Engine.Actor.Trace",
        "Engine.Actor.FastTrace",
        "Engine.SkeletalMeshComponent.HideBone",  // game/scope_parts (research/21 section 14)
        "Engine.SkeletalMeshComponent.UnHideBone",
        "Engine.SkeletalMeshComponent.IsBoneHidden",
        // Throwables
        "SFXGame.SFXGameModeDefault.UseGrenade",
        "SFXGame.SFXGameModeDefault.TryMelee",
        "SFXGame.BioPawn.IsInCombatActionState",
        "SFXGame.BioPawn.IsSwitchingWeapons",
        "SFXGame.BioPawn.GetWalkingSpeedMode",  // game/combat_stance (research/38)
        "SFXGame.BioPawn.IsThrowingGrenade",
        "SFXGame.BioPlayerInput.IsCombatEnabled",
        "SFXGame.BioGrenadeProjectile.SetVelocity",
        "SFXGame.BioGrenade.LaunchGrenadeAtPoint",
        "SFXGame.BioGrenade.LaunchGrenadeAtActor",
        "SFXGame.BioGrenade.LaunchGrenade",
        "SFXGame.BioPawn.IsUsingPower",
        "SFXGame.BioPawnBehavior.GetGrenades",
        "SFXGame.BioPawn.IsHostile",
        "SFXGame.BioPawn.IsDead",
        "SFXGame.BioEpicPawnBehavior.GetPercentHealth",
        "Engine.Actor.SetLocation",
        "Engine.Actor.Destroy",
        // Vent (research/45)
        "SFXGame.BioWeaponRanged.DoCoolDownEffect",
        // SquadWeapons, Holsters
        "SFXGame.BioBaseSquad.GetSquadSize",
        "SFXGame.BioBaseSquad.GetMember",
        "SFXGame.BioBaseSquad.GetMemberSwitchWeapon",
        "SFXGame.BioBaseSquad.SetMemberSwitchWeapon",
        "SFXGame.BioPlayerController.SquadOrderEquipWeapon",
        "SFXGame.BioPlayerController.ApplyTacticalOrders",
        "SFXGame.BioEquipment.ChangeToQuickSlot",
        "SFXGame.BioPawnBehavior.GetClass",
        "SFXGame.SFXGameModeDefault.EnterWeaponWheel",
        "SFXGame.SFXGameModeWeaponWheel.ExitWeaponWheel",
        "SFXGame.SFXGameModeDefault.TryHolster",
        "SFXGame.SFXGameModeDefault.SwitchToWeapon",
        "SFXGame.SFXPawn_Player.DrawPrimaryWeapon",  // holsters' onlyHolstersDraw (2026-10-25)
        "SFXGame.SFXGameModeDefault.UseFirstAid",         // ...whose draws still go through from these (2026-10-26)
        "SFXGame.SFXGameModeDefault.TryUseMappedPower",
        "SFXGame.SFXGameModeDefault.EnterPowerWheel",
        "SFXGame.SFXGameModeBase.FireWeapon",  // ...and the trigger's, refused without a buzz (2026-11-04)
        "Engine.PlayerController.GetScaleFormManager",
        // Posture, ViewLevel, Comfort, MenuPause (migrated in stage 1)
        "SFXGame.BioPawn.ShouldCrouch",
        "SFXGame.SFXGameModeDefault.TryExitCover",
        "SFXGame.BioPawn.IsInCover",
        "SFXGame.BioPawn.LeaveCover",
        "SFXGame.SFXInventoryManager.SetWeaponRecoil",
        "SFXGame.SFXModule_AimAssist.ViewFriction",
        "SFXGame.SFXModule_AimAssist.ViewAdhesion",
        "Engine.Actor.SetRotation",
        "Engine.WorldInfo.PauseGame",
        // GameMenuRequest (research/28): Start's Console_Menu on each mode that declares it
        "SFXGame.SFXGameModeBase.ShowMenuGui",
        "SFXGame.SFXGameModeVehicle.ShowMenuGui",
        // Mako (research/44): the game's own turret aim, the vehicle's Tick and its shot start
        "SFXGame.BioVehicleBehaviorBase.SetDesiredTurretAimPoint",
        "SFXGame.BioVehicleWheeled.Tick",
        "SFXGame.BioVehicleWheeled.GetWeaponStartTraceLocation",
        // MakoControls (research/46): where the sticks' values reach the vehicle
        "SFXGame.BioVehicleBase.SetInputs",
        "SFXGame.SFXGameModePowerWheel.ShowMenuGui",
        "SFXGame.SFXGameModeWeaponWheel.ShowMenuGui",
        "SFXGame.SFXGameModeCommand.ShowMenuGui",
        "SFXGame.SFXGameModePhoto.ShowMenuGui",
        // HeadTracking, Reflections, ConsoleRequest, Skeleton
        "Engine.Camera.UpdateCamera",
        "Engine.PlayerController.GetPlayerViewPoint",
        "Engine.PlayerController.GetFOVAngle",
        "Engine.SceneCaptureComponent.SetFrameRate",
        // ScopeCapture (research/39)
        "Engine.Actor.Spawn",
        "Engine.Actor.ForceUpdateComponents",
        "Engine.TextureRenderTarget2D.Create",
        "Engine.SceneCaptureComponent.SetEnabled",
        "Engine.SceneCapture2DComponent.SetView",
        "Engine.SceneCapture2DComponent.SetCaptureParameters",
        // The cockpit probe (research/47)
        "Core.Object.DynamicLoadObject",
        "Engine.Actor.SetDrawScale",
        "Engine.Actor.SetPhysics",
        "Engine.Actor.SetCollision",
        "Engine.Actor.SetHidden",
        "Engine.PrimitiveComponent.SetHidden",
        "Engine.PrimitiveComponent.SetLightEnvironment",
        "Engine.PrimitiveComponent.SetRBAngularVelocity",
        "Engine.Actor.TakeDamage",
        "SFXGame.BioActorBehavior.ReactToDeath",
        "SFXGame.BioSFHandler_GameOver.HandleEvent",
        "Engine.Actor.SetBase",
        "Engine.Actor.SetHardAttach",
        "Engine.PrimitiveComponent.SetActorCollision",
        "Engine.PrimitiveComponent.SetBlockRigidBody",
        "Engine.PrimitiveComponent.SetTraceBlocking",
        "Engine.StaticMeshComponent.SetStaticMesh",
        "Engine.MeshComponent.GetMaterial",
        "Engine.MeshComponent.SetMaterial",
        "Engine.MeshComponent.GetNumElements",
        "Engine.MeshComponent.GetUnscaledBounds",
        "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant",
        "Engine.MaterialInstanceConstant.SetParent",
        "Engine.MaterialInstanceConstant.SetTextureParameterValue",
        "Engine.MaterialInterface.GetTextureParameterValue",
        "Engine.LightEnvironmentComponent.SetEnabled",
        "Engine.Actor.SetOwner",
        "Engine.PrimitiveComponent.SetOwnerNoSee",
        "Engine.PrimitiveComponent.SetOnlyOwnerSee",
        "SFXGame.BioPlayerController.BioEnterVehicle",
        "SFXGame.SFXSaveGame.FindClosestDrivableVehicle",
        "Engine.SequenceEvent.CheckActivate",
        "SFXGame.BioSFHandler_PartySelection.FinalizeParty",
        "SFXGame.SFXGameModeVehicle.TryExitVehicle",
        "SFXGame.SFXGameModeVehicle.FireCannon",
        "SFXGame.SFXGameModeVehicle.StopFiringCannon",
        "SFXGame.BioPlayerController.BioExitVehicle",
        "SFXGame.BioVehicleBase.PlaceExitingDriver",
        "SFXGame.BioWorldInfo.Tick",
        "Engine.PlayerController.ConsoleCommand",
        "Engine.SkeletalMeshComponent.GetBoneNames",
        "Engine.SkeletalMeshComponent.GetBoneLocation",
        "Engine.SkeletalMeshComponent.MatchRefBone",
    };
    for (char const* function : functions) {
        CheckFunction(function);
    }
    // ViewLevel's PlayerTick set: every override on the player controllers.
    Guard("function", "PlayerTick (all)", [] {
        std::vector<std::string> ours;
        for (ue3::Function* f : R().FindFunctionsNamed("PlayerTick")) {
            ours.push_back(R().FullName(f));
        }
        std::vector<std::string> sdk;
        for (UFunction* f : SdkFunctionsNamed(L"PlayerTick")) {
            sdk.push_back(SdkFullName(f));
        }
        std::sort(ours.begin(), ours.end());
        std::sort(sdk.begin(), sdk.end());
        Add("function", "PlayerTick (all)", !ours.empty() && ours == sdk, std::format("{} functions", ours.size()),
            std::format("{} functions", sdk.size()));
    });
}

void CheckParams() {
    PARAM(ABioWeaponRanged_execGetMuzzleLoc_Parms, "SFXGame.BioWeaponRanged.GetMuzzleLoc", ReturnValue);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", InSocketName);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutLocation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutRotation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", Space);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", ReturnValue);
    PARAM(ABioWeaponRanged_execGetRangedWeaponType_Parms, "SFXGame.BioWeaponRanged.GetRangedWeaponType", ReturnValue);
    PARAM(ABioWeaponRanged_execGetPlayerAimError_Parms, "SFXGame.BioWeaponRanged.GetPlayerAimError", Accuracy);
    PARAM(ABioWeaponRanged_execGetPlayerAimError_Parms, "SFXGame.BioWeaponRanged.GetPlayerAimError", ReturnValue);
    PARAM(ABioGrenadeProjectile_execSetVelocity_Parms, "SFXGame.BioGrenadeProjectile.SetVelocity", vVelocity);
    PARAM(ABioGrenade_eventLaunchGrenadeAtPoint_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtPoint", vTarget);
    PARAM(ABioGrenade_eventLaunchGrenadeAtPoint_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtPoint", ReturnValue);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", HitLocation);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceEnd);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceStart);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", bTraceActors);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", ReturnValue);
    PARAM(AActor_execFastTrace_Parms, "Engine.Actor.FastTrace", TraceEnd);  // `mevr vehicle trace`
    PARAM(AActor_execFastTrace_Parms, "Engine.Actor.FastTrace", TraceStart);
    PARAM(AActor_execFastTrace_Parms, "Engine.Actor.FastTrace", ReturnValue);
    PARAM(USkeletalMeshComponent_execHideBone_Parms, "Engine.SkeletalMeshComponent.HideBone", BoneIndex);
    PARAM(USkeletalMeshComponent_execHideBone_Parms, "Engine.SkeletalMeshComponent.HideBone", PhysBodyOption);
    PARAM(USkeletalMeshComponent_execUnHideBone_Parms, "Engine.SkeletalMeshComponent.UnHideBone", BoneIndex);
    PARAM(USkeletalMeshComponent_execIsBoneHidden_Parms, "Engine.SkeletalMeshComponent.IsBoneHidden", BoneIndex);
    PARAM(USkeletalMeshComponent_execIsBoneHidden_Parms, "Engine.SkeletalMeshComponent.IsBoneHidden", ReturnValue);
    PARAM(ABioGrenade_eventLaunchGrenadeAtActor_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtActor", oTarget);
    PARAM(ABioGrenade_eventLaunchGrenadeAtActor_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtActor", ReturnValue);
    PARAM(ABioGrenade_eventLaunchGrenade_Parms, "SFXGame.BioGrenade.LaunchGrenade", vVelocity);
    PARAM(ABioGrenade_eventLaunchGrenade_Parms, "SFXGame.BioGrenade.LaunchGrenade", ReturnValue);
    PARAM(ABioPawn_execIsUsingPower_Parms, "SFXGame.BioPawn.IsUsingPower", bIgnoreWeaponPowers);
    PARAM(ABioPawn_execIsUsingPower_Parms, "SFXGame.BioPawn.IsUsingPower", ReturnValue);
    PARAM(ABioTriggerStream_eventTouch_Parms, "SFXGame.BioTriggerStream.Touch", Other);  // mevr stream
    PARAM(ABioSPGame_execGetSimpleString_Parms, "SFXGame.BioSPGame.GetSimpleString", StrRef);  // the galaxy hologram
    PARAM(ABioSPGame_execGetSimpleString_Parms, "SFXGame.BioSPGame.GetSimpleString", ReturnValue);
    PARAM(UBio2DA_execGetNumRows_Parms, "Engine.Bio2DA.GetNumRows", ReturnValue);
    PARAM(UBio2DA_execGetNumColumns_Parms, "Engine.Bio2DA.GetNumColumns", ReturnValue);
    PARAM(UBio2DA_execGetColumnName_Parms, "Engine.Bio2DA.GetColumnName", nColumn);
    PARAM(UBio2DA_execGetColumnName_Parms, "Engine.Bio2DA.GetColumnName", ReturnValue);
    PARAM(UBio2DA_execGetRowNumber_Parms, "Engine.Bio2DA.GetRowNumber", nRowIndex);
    PARAM(UBio2DA_execGetRowNumber_Parms, "Engine.Bio2DA.GetRowNumber", ReturnValue);
    PARAM(UBio2DA_execGetIntEntryII_Parms, "Engine.Bio2DA.GetIntEntryII", nRow);
    PARAM(UBio2DA_execGetIntEntryII_Parms, "Engine.Bio2DA.GetIntEntryII", nColumn);
    PARAM(UBio2DA_execGetIntEntryII_Parms, "Engine.Bio2DA.GetIntEntryII", nEntry);
    PARAM(UBio2DA_execGetIntEntryII_Parms, "Engine.Bio2DA.GetIntEntryII", ReturnValue);
    PARAM(ABioTriggerStream_eventTouch_Parms, "SFXGame.BioTriggerStream.Touch", HitLocation);
    PARAM(UBioPawnBehavior_eventGetGrenades_Parms, "SFXGame.BioPawnBehavior.GetGrenades", ReturnValue);
    PARAM(ABioPawn_execIsHostile_Parms, "SFXGame.BioPawn.IsHostile", Other);
    PARAM(ABioPawn_execIsHostile_Parms, "SFXGame.BioPawn.IsHostile", ReturnValue);
    PARAM(UBioEpicPawnBehavior_execGetPercentHealth_Parms, "SFXGame.BioEpicPawnBehavior.GetPercentHealth", ReturnValue);
    PARAM(AActor_execSetLocation_Parms, "Engine.Actor.SetLocation", NewLocation);
    PARAM(AActor_execSetLocation_Parms, "Engine.Actor.SetLocation", ReturnValue);
    PARAM(UBioSFPanel_execSetMovieVisibility_Parms, "Engine.BioSFPanel.SetMovieVisibility", bVisible);
    PARAM(UBioSFPanel_execInvokeMethod_Parms, "Engine.BioSFPanel.InvokeMethod", sMethodName);
    PARAM(UBio2DA_execGetFloatEntryII_Parms, "Engine.Bio2DA.GetFloatEntryII", nRow);
    PARAM(UBio2DA_execGetFloatEntryII_Parms, "Engine.Bio2DA.GetFloatEntryII", nColumn);
    PARAM(UBio2DA_execGetFloatEntryII_Parms, "Engine.Bio2DA.GetFloatEntryII", fEntry);
    PARAM(ABioWorldInfo_execCheckConditional_Parms, "SFXGame.BioWorldInfo.CheckConditional", nConditional);
    PARAM(ABioWorldInfo_execCheckConditional_Parms, "SFXGame.BioWorldInfo.CheckConditional", nParam);
    PARAM(ABioWorldInfo_execCheckConditional_Parms, "SFXGame.BioWorldInfo.CheckConditional", ReturnValue);
    PARAM(UBioArtPlaceableBehavior_eventOnArtPlaceableUsed_Parms, "SFXGame.BioArtPlaceableBehavior.OnArtPlaceableUsed", User);
    PARAM(UBioSFHandler_GalaxyMap_execTravelConfirm_Parms, "SFXGame.BioSFHandler_GalaxyMap.TravelConfirm", nContext);
    PARAM(AActor_execDestroy_Parms, "Engine.Actor.Destroy", ReturnValue);
    PARAM(ABioBaseSquad_execGetSquadSize_Parms, "SFXGame.BioBaseSquad.GetSquadSize", ReturnValue);
    PARAM(ABioBaseSquad_execGetMember_Parms, "SFXGame.BioBaseSquad.GetMember", nIndex);
    PARAM(ABioBaseSquad_execGetMember_Parms, "SFXGame.BioBaseSquad.GetMember", ReturnValue);
    PARAM(ABioBaseSquad_execGetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.GetMemberSwitchWeapon", nIndex);
    PARAM(ABioBaseSquad_execGetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.GetMemberSwitchWeapon", ReturnValue);
    PARAM(ABioBaseSquad_execSetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.SetMemberSwitchWeapon", nIndex);
    PARAM(ABioBaseSquad_execSetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.SetMemberSwitchWeapon", eWeapon);
    PARAM(ABioPlayerController_execSquadOrderEquipWeapon_Parms, "SFXGame.BioPlayerController.SquadOrderEquipWeapon", nWeapon);
    PARAM(ABioPlayerController_execSquadOrderEquipWeapon_Parms, "SFXGame.BioPlayerController.SquadOrderEquipWeapon", pPawn);
    PARAM(UBioEquipment_execChangeToQuickSlot_Parms, "SFXGame.BioEquipment.ChangeToQuickSlot", Slot);
    PARAM(UBioPawnBehavior_execGetClass_Parms, "SFXGame.BioPawnBehavior.GetClass", ReturnValue);
    PARAM(AWorldInfo_execPauseGame_Parms, "Engine.WorldInfo.PauseGame", bPause);
    PARAM(USFXGameModeBase_execShowMenuGui_Parms, "SFXGame.SFXGameModeBase.ShowMenuGui", ReturnValue);
    PARAM(APlayerController_execGetScaleFormManager_Parms, "Engine.PlayerController.GetScaleFormManager", ReturnValue);
    PARAM(USFXGameModeDefault_execSwitchToWeapon_Parms, "SFXGame.SFXGameModeDefault.SwitchToWeapon", Slot);
    PARAM(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor", ReturnValue);
    PARAM(ABioWeaponRanged_execGetAdjustedAim_Parms, "SFXGame.BioWeaponRanged.GetAdjustedAim", StartFireLoc);
    PARAM(APawn_eventGetWeaponStartTraceLocation_Parms, "Engine.Pawn.GetWeaponStartTraceLocation", ReturnValue);
    // Stage 2: hand aim reads BioPawn's override's block through its own parameter (the old code assumed Engine.Pawn's).
    PARAM(APawn_eventGetWeaponStartTraceLocation_Parms, "SFXGame.BioPawn.GetWeaponStartTraceLocation", ReturnValue);
    // Mako (research/44)
    PARAM(APawn_eventGetWeaponStartTraceLocation_Parms, "SFXGame.BioVehicleWheeled.GetWeaponStartTraceLocation", ReturnValue);
    PARAM(UBioVehicleBehaviorBase_execSetDesiredTurretAimPoint_Parms, "SFXGame.BioVehicleBehaviorBase.SetDesiredTurretAimPoint", nTurretIndex);
    PARAM(UBioVehicleBehaviorBase_execSetDesiredTurretAimPoint_Parms, "SFXGame.BioVehicleBehaviorBase.SetDesiredTurretAimPoint",
        vTargetLocation);
    PARAM(UBioVehicleBehaviorBase_execSetDesiredTurretAimPoint_Parms, "SFXGame.BioVehicleBehaviorBase.SetDesiredTurretAimPoint", ReturnValue);
    PARAM(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation", NewRotation);
    PARAM(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation", ReturnValue);
    PARAM(AController_eventGetPlayerViewPoint_Parms, "Engine.PlayerController.GetPlayerViewPoint", out_Location);
    PARAM(AController_eventGetPlayerViewPoint_Parms, "Engine.PlayerController.GetPlayerViewPoint", out_Rotation);
    PARAM(APlayerController_eventGetFOVAngle_Parms, "Engine.PlayerController.GetFOVAngle", ReturnValue);
    PARAM(USceneCaptureComponent_execSetFrameRate_Parms, "Engine.SceneCaptureComponent.SetFrameRate", NewFrameRate);
    // ScopeCapture (research/39).
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", SpawnClass);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", SpawnOwner);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", SpawnLocation);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", bNoCollisionFail);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", ReturnValue);
    PARAM(AActor_execForceUpdateComponents_Parms, "Engine.Actor.ForceUpdateComponents", bCollisionUpdate);
    PARAM(AActor_execForceUpdateComponents_Parms, "Engine.Actor.ForceUpdateComponents", bTransformOnly);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InSizeX);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InSizeY);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InFormat);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InClearColor);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", bOnlyRenderOnce);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", ReturnValue);
    PARAM(USceneCaptureComponent_execSetEnabled_Parms, "Engine.SceneCaptureComponent.SetEnabled", bEnable);
    PARAM(USceneCapture2DComponent_execSetView_Parms, "Engine.SceneCapture2DComponent.SetView", NewLocation);
    PARAM(USceneCapture2DComponent_execSetView_Parms, "Engine.SceneCapture2DComponent.SetView", NewRotation);
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewTextureTarget);
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewFOV);
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewNearPlane);
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewFarPlane);
    // The cockpit probe (research/47).
    PARAM(UObject_execDynamicLoadObject_Parms, "Core.Object.DynamicLoadObject", ObjectName);
    PARAM(UObject_execDynamicLoadObject_Parms, "Core.Object.DynamicLoadObject", ObjectClass);
    PARAM(UObject_execDynamicLoadObject_Parms, "Core.Object.DynamicLoadObject", MayFail);
    PARAM(UObject_execDynamicLoadObject_Parms, "Core.Object.DynamicLoadObject", ReturnValue);
    PARAM(AActor_execSetDrawScale_Parms, "Engine.Actor.SetDrawScale", NewScale);
    PARAM(AActor_execSetPhysics_Parms, "Engine.Actor.SetPhysics", newPhysics);
    PARAM(AActor_execSetCollision_Parms, "Engine.Actor.SetCollision", bNewColActors);
    PARAM(AActor_execSetCollision_Parms, "Engine.Actor.SetCollision", bNewBlockActors);
    PARAM(AActor_execSetCollision_Parms, "Engine.Actor.SetCollision", bNewIgnoreEncroachers);
    PARAM(AActor_execSetHidden_Parms, "Engine.Actor.SetHidden", bNewHidden);
    PARAM(UPrimitiveComponent_execSetHidden_Parms, "Engine.PrimitiveComponent.SetHidden", NewHidden);
    PARAM(UPrimitiveComponent_execSetLightEnvironment_Parms, "Engine.PrimitiveComponent.SetLightEnvironment", NewLightEnvironment);
    PARAM(UPrimitiveComponent_execSetRBAngularVelocity_Parms, "Engine.PrimitiveComponent.SetRBAngularVelocity", NewAngVel);
    PARAM(UPrimitiveComponent_execSetRBAngularVelocity_Parms, "Engine.PrimitiveComponent.SetRBAngularVelocity", bAddToCurrent);
    PARAM(AActor_execSetBase_Parms, "Engine.Actor.SetBase", NewBase);
    PARAM(AActor_execSetBase_Parms, "Engine.Actor.SetBase", NewFloor);
    PARAM(AActor_execSetHardAttach_Parms, "Engine.Actor.SetHardAttach", bNewHardAttach);
    PARAM(AActor_execSetOwner_Parms, "Engine.Actor.SetOwner", NewOwner);
    PARAM(UPrimitiveComponent_execSetOwnerNoSee_Parms, "Engine.PrimitiveComponent.SetOwnerNoSee", bNewOwnerNoSee);
    PARAM(UPrimitiveComponent_execSetOnlyOwnerSee_Parms, "Engine.PrimitiveComponent.SetOnlyOwnerSee", bNewOnlyOwnerSee);
    PARAM(UPrimitiveComponent_execSetActorCollision_Parms, "Engine.PrimitiveComponent.SetActorCollision", NewCollideActors);
    PARAM(UPrimitiveComponent_execSetActorCollision_Parms, "Engine.PrimitiveComponent.SetActorCollision", NewBlockActors);
    PARAM(UPrimitiveComponent_execSetBlockRigidBody_Parms, "Engine.PrimitiveComponent.SetBlockRigidBody", bNewBlockRigidBody);
    PARAM(UPrimitiveComponent_execSetTraceBlocking_Parms, "Engine.PrimitiveComponent.SetTraceBlocking", NewBlockZeroExtent);
    PARAM(UPrimitiveComponent_execSetTraceBlocking_Parms, "Engine.PrimitiveComponent.SetTraceBlocking", NewBlockNonZeroExtent);
    PARAM(UStaticMeshComponent_execSetStaticMesh_Parms, "Engine.StaticMeshComponent.SetStaticMesh", NewMesh);
    PARAM(UStaticMeshComponent_execSetStaticMesh_Parms, "Engine.StaticMeshComponent.SetStaticMesh", bForce);
    PARAM(UStaticMeshComponent_execSetStaticMesh_Parms, "Engine.StaticMeshComponent.SetStaticMesh", ReturnValue);
    PARAM(UMeshComponent_execGetMaterial_Parms, "Engine.MeshComponent.GetMaterial", ElementIndex);
    PARAM(UMeshComponent_execGetMaterial_Parms, "Engine.MeshComponent.GetMaterial", ReturnValue);
    PARAM(UMeshComponent_execSetMaterial_Parms, "Engine.MeshComponent.SetMaterial", ElementIndex);
    PARAM(UMeshComponent_execSetMaterial_Parms, "Engine.MeshComponent.SetMaterial", Material);
    PARAM(UMeshComponent_execGetNumElements_Parms, "Engine.MeshComponent.GetNumElements", ReturnValue);
    PARAM(UMeshComponent_execGetUnscaledBounds_Parms, "Engine.MeshComponent.GetUnscaledBounds", UnscaledBounds);
    PARAM(UMeshComponent_execCreateAndSetMaterialInstanceConstant_Parms, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant", ElementIndex);
    PARAM(UMeshComponent_execCreateAndSetMaterialInstanceConstant_Parms, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant", ReturnValue);
    PARAM(UMaterialInstanceConstant_execSetParent_Parms, "Engine.MaterialInstanceConstant.SetParent", NewParent);
    PARAM(UMaterialInstanceConstant_execSetTextureParameterValue_Parms, "Engine.MaterialInstanceConstant.SetTextureParameterValue", ParameterName);
    PARAM(UMaterialInstanceConstant_execSetTextureParameterValue_Parms, "Engine.MaterialInstanceConstant.SetTextureParameterValue", Value);
    PARAM(UMaterialInterface_execGetTextureParameterValue_Parms, "Engine.MaterialInterface.GetTextureParameterValue", ParameterName);
    PARAM(UMaterialInterface_execGetTextureParameterValue_Parms, "Engine.MaterialInterface.GetTextureParameterValue", OutValue);
    PARAM(UMaterialInterface_execGetTextureParameterValue_Parms, "Engine.MaterialInterface.GetTextureParameterValue", ReturnValue);
    PARAM(ULightEnvironmentComponent_execSetEnabled_Parms, "Engine.LightEnvironmentComponent.SetEnabled", bNewEnabled);
    PARAM(ABioPlayerController_execBioEnterVehicle_Parms, "SFXGame.BioPlayerController.BioEnterVehicle", oVehicle);
    PARAM(ABioPlayerController_execBioEnterVehicle_Parms, "SFXGame.BioPlayerController.BioEnterVehicle", ReturnValue);
    PARAM(USFXSaveGame_execFindClosestDrivableVehicle_Parms, "SFXGame.SFXSaveGame.FindClosestDrivableVehicle", Pawn);
    PARAM(USFXSaveGame_execFindClosestDrivableVehicle_Parms, "SFXGame.SFXSaveGame.FindClosestDrivableVehicle", ReturnValue);
    PARAM(USequenceEvent_execCheckActivate_Parms, "Engine.SequenceEvent.CheckActivate", InOriginator);
    PARAM(USequenceEvent_execCheckActivate_Parms, "Engine.SequenceEvent.CheckActivate", InInstigator);
    PARAM(USequenceEvent_execCheckActivate_Parms, "Engine.SequenceEvent.CheckActivate", ReturnValue);
    PARAM(ABioVehicleBase_execPlaceExitingDriver_Parms, "SFXGame.BioVehicleBase.PlaceExitingDriver", ExitingDriver);
    PARAM(ABioVehicleBase_execPlaceExitingDriver_Parms, "SFXGame.BioVehicleBase.PlaceExitingDriver", ReturnValue);
    PARAM(ABioPlayerController_execBioExitVehicle_Parms, "SFXGame.BioPlayerController.BioExitVehicle", bForceExit);
    PARAM(ABioPlayerController_execBioExitVehicle_Parms, "SFXGame.BioPlayerController.BioExitVehicle", ReturnValue);
    PARAM(UBioSFHandler_PartySelection_execFinalizeParty_Parms, "SFXGame.BioSFHandler_PartySelection.FinalizeParty", bAPressed);
    PARAM(UBioSFHandler_PartySelection_execFinalizeParty_Parms, "SFXGame.BioSFHandler_PartySelection.FinalizeParty", nContext);
    PARAM(UBioSFHandler_PartySelection_execFinalizeParty_Parms, "SFXGame.BioSFHandler_PartySelection.FinalizeParty", bYPressed);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", Command);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", bWriteToLog);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", ReturnValue);
    PARAM(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone", BoneName);
    PARAM(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone", ReturnValue);
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", BoneName);
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", Space);
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", ReturnValue);
    PARAM(USkeletalMeshComponent_execGetBoneNames_Parms, "Engine.SkeletalMeshComponent.GetBoneNames", BoneNames);
    PARAM(ABioPawn_execShouldCrouch_Parms, "SFXGame.BioPawn.ShouldCrouch", bCrouch);
    PARAM(ABioPawn_execIsInCover_Parms, "SFXGame.BioPawn.IsInCover", ReturnValue);
    // The modules' hand-built blocks: a bool return value at 0.
    CheckParam("SFXGame.BioPawn.IsInCombatActionState", "ReturnValue", SDK_AT(0, 4));
    CheckParam("SFXGame.BioPawn.IsSwitchingWeapons", "ReturnValue", SDK_AT(0, 4));
    CheckParam("SFXGame.BioPawn.GetWalkingSpeedMode", "ReturnValue", SDK_AT(0, 1));
    CheckParam("SFXGame.BioPawn.IsThrowingGrenade", "ReturnValue", SDK_AT(0, 4));
    CheckParam("SFXGame.BioPlayerInput.IsCombatEnabled", "ReturnValue", SDK_AT(0, 4));
    CheckParam("SFXGame.BioPawn.IsDead", "ReturnValue", SDK_AT(0, 4));

    PARMS(ABioWeaponRanged_execGetMuzzleLoc_Parms, "SFXGame.BioWeaponRanged.GetMuzzleLoc");
    PARMS(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation");
    PARMS(ABioWeaponRanged_execGetRangedWeaponType_Parms, "SFXGame.BioWeaponRanged.GetRangedWeaponType");
    PARMS(ABioWeaponRanged_execGetPlayerAimError_Parms, "SFXGame.BioWeaponRanged.GetPlayerAimError");
    PARMS(USFXGameModeDefault_execUseGrenade_Parms, "SFXGame.SFXGameModeDefault.UseGrenade");
    PARMS(USFXGameModeDefault_execTryMelee_Parms, "SFXGame.SFXGameModeDefault.TryMelee");
    PARMS(ABioGrenadeProjectile_execSetVelocity_Parms, "SFXGame.BioGrenadeProjectile.SetVelocity");
    PARMS(ABioGrenade_eventLaunchGrenadeAtPoint_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtPoint");
    PARMS(AActor_execTrace_Parms, "Engine.Actor.Trace");
    PARMS(AActor_execFastTrace_Parms, "Engine.Actor.FastTrace");
    PARMS(ABioGrenade_eventLaunchGrenadeAtActor_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtActor");
    PARMS(ABioGrenade_eventLaunchGrenade_Parms, "SFXGame.BioGrenade.LaunchGrenade");
    PARMS(ABioPawn_execIsUsingPower_Parms, "SFXGame.BioPawn.IsUsingPower");
    PARMS(UBioPawnBehavior_eventGetGrenades_Parms, "SFXGame.BioPawnBehavior.GetGrenades");
    PARMS(ABioPawn_execIsHostile_Parms, "SFXGame.BioPawn.IsHostile");
    PARMS(UBioEpicPawnBehavior_execGetPercentHealth_Parms, "SFXGame.BioEpicPawnBehavior.GetPercentHealth");
    PARMS(AActor_execSetLocation_Parms, "Engine.Actor.SetLocation");
    PARMS(AActor_execDestroy_Parms, "Engine.Actor.Destroy");
    PARMS(ABioWeaponRanged_eventDoCoolDownEffect_Parms, "SFXGame.BioWeaponRanged.DoCoolDownEffect");
    PARMS(ABioBaseSquad_execGetSquadSize_Parms, "SFXGame.BioBaseSquad.GetSquadSize");
    PARMS(ABioBaseSquad_execGetMember_Parms, "SFXGame.BioBaseSquad.GetMember");
    PARMS(ABioBaseSquad_execGetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.GetMemberSwitchWeapon");
    PARMS(ABioBaseSquad_execSetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.SetMemberSwitchWeapon");
    PARMS(ABioPlayerController_execSquadOrderEquipWeapon_Parms, "SFXGame.BioPlayerController.SquadOrderEquipWeapon");
    PARMS(ABioPlayerController_execApplyTacticalOrders_Parms, "SFXGame.BioPlayerController.ApplyTacticalOrders");
    PARMS(UBioEquipment_execChangeToQuickSlot_Parms, "SFXGame.BioEquipment.ChangeToQuickSlot");
    PARMS(UBioPawnBehavior_execGetClass_Parms, "SFXGame.BioPawnBehavior.GetClass");
    PARMS(AWorldInfo_execPauseGame_Parms, "Engine.WorldInfo.PauseGame");
    PARMS(USFXGameModeDefault_execTryHolster_Parms, "SFXGame.SFXGameModeDefault.TryHolster");
    PARMS(APlayerController_execGetScaleFormManager_Parms, "Engine.PlayerController.GetScaleFormManager");
    PARMS(USFXGameModeDefault_execSwitchToWeapon_Parms, "SFXGame.SFXGameModeDefault.SwitchToWeapon");
    PARMS(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor");
    PARMS(UBioVehicleBehaviorBase_execSetDesiredTurretAimPoint_Parms, "SFXGame.BioVehicleBehaviorBase.SetDesiredTurretAimPoint");  // Mako
    PARMS(ABioWeaponRanged_execGetAdjustedAim_Parms, "SFXGame.BioWeaponRanged.GetAdjustedAim");
    PARMS(APawn_eventGetWeaponStartTraceLocation_Parms, "Engine.Pawn.GetWeaponStartTraceLocation");
    PARMS(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation");
    PARMS(AController_eventGetPlayerViewPoint_Parms, "Engine.PlayerController.GetPlayerViewPoint");
    PARMS(APlayerController_eventGetFOVAngle_Parms, "Engine.PlayerController.GetFOVAngle");
    PARMS(USceneCaptureComponent_execSetFrameRate_Parms, "Engine.SceneCaptureComponent.SetFrameRate");
    PARMS(AActor_execSpawn_Parms, "Engine.Actor.Spawn");
    PARMS(AActor_execForceUpdateComponents_Parms, "Engine.Actor.ForceUpdateComponents");
    PARMS(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create");
    PARMS(USceneCaptureComponent_execSetEnabled_Parms, "Engine.SceneCaptureComponent.SetEnabled");
    PARMS(USceneCapture2DComponent_execSetView_Parms, "Engine.SceneCapture2DComponent.SetView");
    PARMS(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters");
    PARMS(UObject_execDynamicLoadObject_Parms, "Core.Object.DynamicLoadObject");
    PARMS(AActor_execSetDrawScale_Parms, "Engine.Actor.SetDrawScale");
    PARMS(AActor_execSetPhysics_Parms, "Engine.Actor.SetPhysics");
    PARMS(AActor_execSetCollision_Parms, "Engine.Actor.SetCollision");
    PARMS(AActor_execSetHidden_Parms, "Engine.Actor.SetHidden");
    PARMS(UPrimitiveComponent_execSetHidden_Parms, "Engine.PrimitiveComponent.SetHidden");
    PARMS(UPrimitiveComponent_execSetLightEnvironment_Parms, "Engine.PrimitiveComponent.SetLightEnvironment");
    PARMS(UPrimitiveComponent_execSetRBAngularVelocity_Parms, "Engine.PrimitiveComponent.SetRBAngularVelocity");
    PARMS(AActor_eventTakeDamage_Parms, "Engine.Actor.TakeDamage");
    PARMS(AActor_execSetBase_Parms, "Engine.Actor.SetBase");
    PARMS(AActor_execSetHardAttach_Parms, "Engine.Actor.SetHardAttach");
    PARMS(AActor_execSetOwner_Parms, "Engine.Actor.SetOwner");
    PARMS(UPrimitiveComponent_execSetOwnerNoSee_Parms, "Engine.PrimitiveComponent.SetOwnerNoSee");
    PARMS(UPrimitiveComponent_execSetOnlyOwnerSee_Parms, "Engine.PrimitiveComponent.SetOnlyOwnerSee");
    PARMS(USequenceEvent_execCheckActivate_Parms, "Engine.SequenceEvent.CheckActivate");
    PARMS(UBioSFHandler_PartySelection_execFinalizeParty_Parms, "SFXGame.BioSFHandler_PartySelection.FinalizeParty");
    PARMS(UPrimitiveComponent_execSetActorCollision_Parms, "Engine.PrimitiveComponent.SetActorCollision");
    PARMS(UPrimitiveComponent_execSetBlockRigidBody_Parms, "Engine.PrimitiveComponent.SetBlockRigidBody");
    PARMS(UPrimitiveComponent_execSetTraceBlocking_Parms, "Engine.PrimitiveComponent.SetTraceBlocking");
    PARMS(UStaticMeshComponent_execSetStaticMesh_Parms, "Engine.StaticMeshComponent.SetStaticMesh");
    PARMS(UMeshComponent_execGetMaterial_Parms, "Engine.MeshComponent.GetMaterial");
    PARMS(UMeshComponent_execSetMaterial_Parms, "Engine.MeshComponent.SetMaterial");
    PARMS(UMeshComponent_execGetNumElements_Parms, "Engine.MeshComponent.GetNumElements");
    PARMS(UMeshComponent_execGetUnscaledBounds_Parms, "Engine.MeshComponent.GetUnscaledBounds");
    PARMS(UMeshComponent_execCreateAndSetMaterialInstanceConstant_Parms, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant");
    PARMS(UMaterialInstanceConstant_execSetParent_Parms, "Engine.MaterialInstanceConstant.SetParent");
    PARMS(UMaterialInstanceConstant_execSetTextureParameterValue_Parms, "Engine.MaterialInstanceConstant.SetTextureParameterValue");
    PARMS(UMaterialInterface_execGetTextureParameterValue_Parms, "Engine.MaterialInterface.GetTextureParameterValue");
    PARMS(ULightEnvironmentComponent_execSetEnabled_Parms, "Engine.LightEnvironmentComponent.SetEnabled");
    PARMS(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand");
    PARMS(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone");
    PARMS(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation");
    PARMS(USkeletalMeshComponent_execGetBoneNames_Parms, "Engine.SkeletalMeshComponent.GetBoneNames");
    PARMS(ABioPawn_execShouldCrouch_Parms, "SFXGame.BioPawn.ShouldCrouch");
    PARMS(ABioPawn_execIsInCover_Parms, "SFXGame.BioPawn.IsInCover");
    PARMS(USFXGameModeDefault_execTryExitCover_Parms, "SFXGame.SFXGameModeDefault.TryExitCover");
    // Stage 2: the squad wheel's calls (a 64-byte zeroed block before; the function's own block now).
    PARMS(USFXGameModeDefault_execEnterWeaponWheel_Parms, "SFXGame.SFXGameModeDefault.EnterWeaponWheel");
    PARMS(USFXGameModeWeaponWheel_execExitWeaponWheel_Parms, "SFXGame.SFXGameModeWeaponWheel.ExitWeaponWheel");
}

// Names, full names, IsA and IsAlive over a spread of GObjects, both ways.
void CheckSamples() {
    Guard("sample", "names", [] {
        int const count = R().ObjectCount();
        int const step = std::max(1, count / 20000);
        int checked = 0;
        int differ = 0;
        std::string first;
        for (int i = 0; i < count; i += step) {
            UObject* const object = UObject::GObjObjects->GetData()[i];
            if (object == nullptr) {
                continue;
            }
            ++checked;
            std::string const ours = R().NameText(reinterpret_cast<ue3::Object*>(object));
            std::string const sdk = SdkName(object->Name);
            if (ours != sdk) {
                if (++differ <= 3) {
                    first += std::format("{}[{}] '{}' vs '{}'", first.empty() ? "" : "; ", i, ours, sdk);
                }
            }
        }
        Add("sample", "names", checked > 0 && differ == 0, std::format("{} of {} differ{}", differ, checked, first.empty() ? "" : ": " + first),
            "SFXName::Instanced");
    });
    Guard("sample", "full names", [] {
        int const count = R().ObjectCount();
        int const step = std::max(1, count / 2000);
        int checked = 0;
        int differ = 0;
        std::string first;
        for (int i = 0; i < count; i += step) {
            UObject* const object = UObject::GObjObjects->GetData()[i];
            if (object == nullptr) {
                continue;
            }
            ++checked;
            std::string const ours = R().FullName(reinterpret_cast<ue3::Object*>(object));
            std::string const sdk = SdkFullName(object);
            if (ours != sdk && ++differ <= 3) {
                first += std::format("{}'{}' vs '{}'", first.empty() ? "" : "; ", ours, sdk);
            }
        }
        Add("sample", "full names", checked > 0 && differ == 0,
            std::format("{} of {} differ{}", differ, checked, first.empty() ? "" : ": " + first), "UObject::GetFullName");
    });
    Guard("sample", "IsA and IsAlive", [] {
        struct Probe {
            UClass* sdk;
            ue3::Class* ours;
        };
        Probe const probes[] = {
            {AActor::StaticClass(), R().FindClass("Engine.Actor")},
            {APawn::StaticClass(), R().FindClass("Engine.Pawn")},
            {UFunction::StaticClass(), R().FindClass("Core.Function")},
            {UActorComponent::StaticClass(), R().FindClass("Engine.ActorComponent")},
            {USFXGameModeBase::StaticClass(), R().FindClass("SFXGame.SFXGameModeBase")},
        };
        int const count = R().ObjectCount();
        int const step = std::max(1, count / 5000);
        int checked = 0;
        int differ = 0;
        for (int i = 0; i < count; i += step) {
            UObject* const object = UObject::GObjObjects->GetData()[i];
            if (object == nullptr) {
                continue;
            }
            auto* const ours = reinterpret_cast<ue3::Object*>(object);
            for (Probe const& probe : probes) {
                ++checked;
                if (R().IsA(ours, probe.ours) != (object->IsA(probe.sdk) != 0)) {
                    ++differ;
                }
            }
            ++checked;
            if (R().IsAlive(ours) != SdkIsAlive(object)) {
                ++differ;
            }
        }
        Add("sample", "IsA and IsAlive", checked > 0 && differ == 0, std::format("{} of {} differ", differ, checked), "UObject::IsA, ObjectInternalInteger and ObjectFlags");
    });
}

// Engine strings (ue3/Memory, stage 2): one of ours, built in the engine's memory, and one LExSDKv2 builds (through
// GMalloc), each freed by the other's allocator path, so both are proven the same heap before ConsoleRequest relies on it.
void CheckMemory() {
    if (!ue3::memory::Ready()) {
        Add("sample", "engine strings", false, "the engine's allocator wasn't found", "");
        return;
    }
    if (ue3::runtime::Where().gmalloc != reinterpret_cast<std::uintptr_t>(GMalloc)) {
        Add("sample", "engine strings", false, "not tried: our allocator's GMalloc isn't LExSDKv2's (the allocator row)", "");
        return;
    }
    Guard("sample", "engine strings", [] {
        std::wstring_view const text = L"MEVR reflect check";
        std::string problem;
        {
            // Ours, built and freed through appMalloc/appFree.
            ue3::memory::OwnedString ours(text);
            ue3::FString const& s = ours.Get();
            if (!ours.Valid() || s.data == nullptr || s.count != static_cast<std::int32_t>(text.size() + 1) ||
                std::wstring_view(s.data, text.size()) != text || s.data[text.size()] != L'\0') {
                problem = "our string isn't as built";
            } else if (ue3::memory::Narrow(s) != "MEVR reflect check") {
                problem = "our string doesn't read back";
            }
        }
        if (problem.empty() && GMalloc != nullptr && *GMalloc != nullptr) {
            // A block from appMalloc goes back through GMalloc's own Free, as the engine frees what it is handed.
            void* const block = ue3::memory::Malloc(64);
            if (block == nullptr) {
                problem = "appMalloc returned nothing";
            } else {
                std::memset(block, 0x5A, 64);
                (*GMalloc)->Free(block);
            }
        }
        if (problem.empty()) {
            // LExSDKv2's string (its constructor allocates through GMalloc), freed by our path.
            alignas(FString) unsigned char storage[sizeof(FString)];
            auto* const sdk = new (storage) FString(L"MEVR reflect check");
            ue3::FString theirs{};
            std::memcpy(&theirs, sdk, sizeof theirs);
            if (ue3::memory::Narrow(theirs) != "MEVR reflect check") {
                problem = "LExSDKv2's string doesn't read through ours";
            }
            ue3::memory::FreeString(theirs);  // not the SDK's destructor: the storage is just forgotten
        }
        Add("sample", "engine strings", problem.empty(), problem.empty() ? "built, read, freed both ways" : problem,
            "FString (GMalloc)");
    });
}

std::filesystem::path OutputPath() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR" / L"reflect-check.json";
}

void Write(int agree, int mismatches, int notReflected) {
    namespace json = vr::test_request;
    std::filesystem::path const path = OutputPath();
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::string text = std::format(
        "{{\n  \"status\": {},\n  \"checks\": {},\n  \"agree\": {},\n  \"mismatches\": {},\n  \"notReflected\": {},\n  \"rows\": [\n",
        json::JsonString(ue3::runtime::Status()), gRows.size(), agree, mismatches, notReflected);
    for (std::size_t i = 0; i < gRows.size(); ++i) {
        Row const& row = gRows[i];
        text += std::format("    {{\"kind\": {}, \"what\": {}, \"agree\": {}, \"notReflected\": {}, \"ours\": {}, \"sdk\": {}}}{}\n",
            json::JsonString(row.kind), json::JsonString(row.what), row.agree ? "true" : "false", row.notReflected ? "true" : "false",
            json::JsonString(row.ours), json::JsonString(row.sdk), i + 1 < gRows.size() ? "," : "");
    }
    text += "  ]\n}\n";
    std::filesystem::path const temp = path.wstring() + L".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
    }
    std::filesystem::rename(temp, path, ec);
}

} // namespace

void Run(hook::Context& context) {
    // LExSDKv2's resolver for its BUILTIN_*_RVA (the reference side only; FromOffset never calls the interface).
    ::LESDK::Initializer init{reinterpret_cast<ISharedProxyInterface*>(context.Spi()), MEVR_GAME_NAME_A "MEVRreference"};
    gRows.clear();
    if (!ResolveSdkGlobals(init)) {
        log::Warn("reflect: self-check skipped: LExSDKv2's globals (the reference) didn't resolve");
        Add("global", "LExSDKv2", false, ue3::runtime::Status(), "its globals didn't resolve");
        Write(0, 1, 0);
        return;
    }
    if (!ue3::runtime::Ready()) {
        log::Warn("reflect: self-check skipped: {}", ue3::runtime::Status());
        Add("global", "layer", false, ue3::runtime::Status(), "");
        Write(0, 1, 0);
        return;
    }
    LARGE_INTEGER start{};
    LARGE_INTEGER end{};
    LARGE_INTEGER frequency{};
    QueryPerformanceCounter(&start);
    CheckLayout();
    CheckGlobals(init);
    CheckRvas(init);
    CheckTypes();
    CheckClasses();
    CheckFields();
    CheckPaths();
    CheckSkeleton();
    CheckFunctions();
    CheckParams();
    CheckSamples();
    CheckMemory();
    ue3::runtime::FlushNotes();
    QueryPerformanceCounter(&end);
    QueryPerformanceFrequency(&frequency);

    int agree = 0;
    int mismatches = 0;
    int notReflected = 0;
    std::string list;
    for (Row const& row : gRows) {
        if (row.agree) {
            ++agree;
        } else if (row.notReflected) {
            ++notReflected;
        } else {
            ++mismatches;
            list += std::format("{}{} {}", list.empty() ? "" : ", ", row.kind, row.what);
        }
    }
    double const ms = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
    log::Info("reflect: {} of {} checks agree{}{} ({:.0f} ms; %LOCALAPPDATA%\\MEVR\\reflect-check.json)", agree,
        static_cast<int>(gRows.size()) - notReflected, mismatches > 0 ? "; mismatches: " + list : std::string(),
        notReflected > 0 ? std::format("; {} native fields not in the reflection", notReflected) : std::string(), ms);
    int logged = 0;
    for (Row const& row : gRows) {
        if (!row.agree && logged < 80) {
            ++logged;
            log::Warn("reflect:   {} {}: ours {}, LExSDKv2 {}{}", row.kind, row.what, row.ours, row.sdk,
                row.notReflected ? " (native, not reflected)" : "");
        }
    }
    Write(agree, mismatches, notReflected);
}

#elif defined(MEVR_GAME_LE1)

void Run(hook::Context&) {
    log::Info("reflect: no LExSDKv2 cross-check in this build (a players' build has no LExSDKv2 in it); our reflection layer "
              "runs on its own and each feature stands down with a log line if a lookup fails");
}

#else

void Run(hook::Context&) {}

#endif

} // namespace mevr::engine::reflect_check
