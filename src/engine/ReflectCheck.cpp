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
// LExSDKv2 in it at all, on any game (research/34 stage 6 for LE1; T04, docs/trilogy-plan.md, for LE2 and LE3:
// MEVR_LE2_LESDK / MEVR_LE3_LESDK off): the cross-check is skipped, our layer runs on its own. LE2 since
// its layout (docs/le2-continuation-plan.md M1): the same check, over the classes, fields and functions LE2 reads.
// LE3 has its own foundation table (L3-1), inert until its reflection layout passes the runtime gate.
#if (defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)) && !defined(MEVR_NO_LESDK)
#define MEVR_REFLECT_CHECK 1
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
#include <unordered_set>

// Everything the mod reads from LExSDKv2 today, checked against our reflection layer (research/34
// section 4 is the inventory; section 5 says how to read the result). LExSDKv2's offsets are used
// here only as the reference to compare with: none of them feeds ue3/.

namespace mevr::engine::reflect_check {

#if defined(MEVR_REFLECT_CHECK)
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

#if defined(MEVR_GAME_LE1)

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
    CheckClass<UParticleSystemComponent>("Engine.ParticleSystemComponent");
    CheckClass<UParticleSystem>("Engine.ParticleSystem");
    CheckClass<UParticleEmitter>("Engine.ParticleEmitter");
    CheckClass<UParticleModuleMeshMaterial>("Engine.ParticleModuleMeshMaterial");
    CheckClass<UParticleModuleMaterialByParameter>("Engine.ParticleModuleMaterialByParameter");
    CheckClass<UParticleLODLevel>("Engine.ParticleLODLevel");
    CheckClass<UParticleModule>("Engine.ParticleModule");
    CheckClass<UParticleModuleRequired>("Engine.ParticleModuleRequired");
    CheckClass<UDistributionFloatParameterBase>("Engine.DistributionFloatParameterBase");
    CheckClass<UDistributionVectorParameterBase>("Engine.DistributionVectorParameterBase");
    CheckClass<UDistributionFloatConstant>("Engine.DistributionFloatConstant");
    CheckClass<UDistributionVectorConstant>("Engine.DistributionVectorConstant");
    CheckClass<UParticleModuleTypeDataMesh>("Engine.ParticleModuleTypeDataMesh");
    CheckClass<UDistributionFloatParticleParameter>("Engine.DistributionFloatParticleParameter");
    CheckClass<UDistributionVectorParticleParameter>("Engine.DistributionVectorParticleParameter");
    CheckClass<UAnimNodeBlendBase>("Engine.AnimNodeBlendBase");  // game/combat_stance (research/38)
    CheckClass<UAnimNodeSequence>("Engine.AnimNodeSequence");
    CheckClass<UAnimTree>("Engine.AnimTree");
    CheckClass<UBioAnimNodeBlendByAction>("SFXGame.BioAnimNodeBlendByAction");  // game/ConversationWorld: who can walk (research/69)
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
    CheckClass<UBioSFHandler_Conversation>("SFXGame.BioSFHandler_Conversation");  // game/ConversationWorld: the dialogue wheel
    CheckClass<UBioSFManager>("Engine.BioSFManager");
    CheckClass<ABioPawn>("SFXGame.BioPawn");
    CheckClass<ABioTriggerStream>("SFXGame.BioTriggerStream");  // mevr stream
    CheckClass<UBioSeqAct_SetStreamingState>("SFXGame.BioSeqAct_SetStreamingState");  // mevr stream state (research/59)
    CheckClass<ABioPlayerController>("SFXGame.BioPlayerController");
    CheckClass<ABioAiController>("SFXGame.BioAiController");  // game/ConversationWorld: walking to marks
    CheckClass<UBioSeqData_MoveToPoint>("SFXGame.BioSeqData_MoveToPoint");
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
    CheckClass<UGFxMovieInfo>("GFxUI.GFxMovieInfo");  // game/GameArt (research/57): the movies' bytes
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
    CheckClass<UBioSkillGame_Handler>("SFXGame.BioSkillGame_Handler");  // game/WorldUi: the minigame's placeable    CheckClass<UBioVFXTemplate>("SFXGame.BioVFXTemplate");  // game/OmniTool: the game's omni-tool effect
    CheckClass<ABioVisualEffect>("SFXGame.BioVisualEffect");  // game/OmniTool
    CheckClass<UBioSeqEvt_ArtPlaceableUsed>("SFXGame.BioSeqEvt_ArtPlaceableUsed");
    CheckClass<UBioArtPlaceableBehavior>("SFXGame.BioArtPlaceableBehavior");
    CheckClass<AEmitter>("Engine.Emitter");  // the CIC projector's own hologram, hidden while ours shows
    CheckClass<UBioSeqAct_Delay>("SFXGame.BioSeqAct_Delay");  // the galaxy map's opening wait
    // game/mako (research/44): the Mako's turret aimed by the controller.
    CheckClass<ABioVehicleBase>("SFXGame.BioVehicleBase");
    CheckClass<ABioVehicleWheeled>("SFXGame.BioVehicleWheeled");
    CheckClass<USFXCameraShakeBase>("SFXGame.SFXCameraShakeBase");  // game/CameraShake
    CheckClass<UDamageType>("Engine.DamageType");  // `mevr vehicle damage`
    CheckClass<UBioSFHandler_GameOver>("SFXGame.BioSFHandler_GameOver");  // `mevr vehicle resume`
    CheckClass<UBioVehicleBehaviorBase>("SFXGame.BioVehicleBehaviorBase");
    CheckClass<UBioSkelControl_TurretConstrained>("SFXGame.BioSkelControl_TurretConstrained");
    // game/MakoControls (research/46): which simulation the vehicle runs.
    CheckClass<USVehicleSimCar>("Engine.SVehicleSimCar");
    CheckClass<USVehicleSimTank>("Engine.SVehicleSimTank");
    CheckClass<UBioSVehicleSimTank>("SFXGame.BioSVehicleSimTank");
    CheckClass<UBioSFHandler_PCHUD>("SFXGame.BioSFHandler_PCHUD");
    CheckClass<UBioSFHandler_Reticule>("SFXGame.BioSFHandler_Reticule");
}

// OmniTool's read-only distribution walker uses reflected property chains rather than a list of module layouts.
// Inventory every loaded module class and nested struct/array field against SDK metadata, including RawDistribution.Distribution.
void CheckModuleDistributionFields() {
    Guard("field", "particle module distribution inventory", [] {
        std::unordered_set<UStruct*> visited;
        std::function<void(UStruct*,ue3::Struct*,int)> walk = [&](UStruct* sdk, ue3::Struct* ours, int depth) {
            if (!sdk || !ours || depth>5 || !visited.insert(sdk).second) return;
            for (auto* cls=sdk; cls; cls=static_cast<UStruct*>(cls->SuperField)) {
                int fields=0;
                for (auto* f=cls->Children; f && fields++<1024; f=f->Next) {
                    if (!f->IsA(UProperty::StaticClass())) continue;
                    auto* const prop = static_cast<UProperty*>(f);
                    std::string const name = SdkName(prop->Name);
                    auto const info = R().PropertyOf(ours,name);
                    bool const relevant = prop->IsA(UStructProperty::StaticClass()) || prop->IsA(UArrayProperty::StaticClass()) || name=="Distribution";
                    if (!relevant) continue;
                    Sdk const ref{prop->Offset,0,static_cast<long long>(prop->ElementSize)*prop->ArrayDim};
                    Add("field",SdkFullName(prop),info && Matches(*info,info->offset,ref),Ours(info),SdkText(ref));
                    if (prop->IsA(UStructProperty::StaticClass())) {
                        walk(static_cast<UStructProperty*>(prop)->Struct,info ? R().StructOf(*info) : nullptr,depth+1);
                    } else if (prop->IsA(UArrayProperty::StaticClass())) {
                        auto* const inner = static_cast<UArrayProperty*>(prop)->Inner;
                        auto const innerInfo = info && info->kind==ue3::Kind::Array ? R().Describe(static_cast<ue3::Property*>(info->typed)) : std::nullopt;
                        Add("path",SdkFullName(prop)+"[]",inner && innerInfo && innerInfo->elementSize==inner->ElementSize,
                            innerInfo ? std::format("element size {}",innerInfo->elementSize) : "not found",
                            inner ? std::format("element size {}",inner->ElementSize) : "not found");
                        if (inner && inner->IsA(UStructProperty::StaticClass()))
                            walk(static_cast<UStructProperty*>(inner)->Struct,innerInfo ? R().StructOf(*innerInfo) : nullptr,depth+1);
                    }
                }
            }
        };
        auto* const parent = R().FindClass("Engine.ParticleModule");
        for (int i=0,n=R().ObjectCount(); i<n; ++i) {
            auto* const object = R().ObjectAt(i);
            if (!object || !R().IsAlive(object) || !R().IsA(object,R().ClassClass())) continue;
            auto* const cls = static_cast<ue3::Class*>(object);
            if (!R().IsChildOf(cls,parent)) continue;
            std::string const path = R().PathName(cls);
            auto* const resolved = R().FindClass(path);
            Add("class",path,resolved==cls,R().FullName(resolved),SdkFullName(reinterpret_cast<UObject*>(cls)));
            walk(reinterpret_cast<UStruct*>(cls),cls,0);
        }
    });
}

void CheckFields() {
    CheckModuleDistributionFields();
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
    CheckField("Engine.PlayerController", "bCinematicMode", SDK_BOOL(APlayerController, bCinematicMode));  // game/AuthoredMoment
    CheckField("Engine.Actor", "bCollideActors", SDK_BOOL(AActor, bCollideActors));  // the Mako's gunner skips dormant pawns
    CheckField("Engine.Actor", "LastRenderTime", SDK_FIELD(AActor, LastRenderTime));  // mevr crowd
    CheckField("Engine.PrimitiveComponent", "HiddenGame", SDK_BOOL(UPrimitiveComponent, HiddenGame));
    CheckField("Engine.PrimitiveComponent", "bOnlyOwnerSee", SDK_BOOL(UPrimitiveComponent, bOnlyOwnerSee));  // the seated driver
    // The Mako's thrusters (game/MakoControls): longer, pitched by the stick.
    CheckField("SFXGame.BioPlayerController", "bJumpJetsOn", SDK_FIELD(ABioPlayerController, bJumpJetsOn));
    CheckField("SFXGame.BioSeqAct_BlackScreen", "m_eBlackScreenAction", SDK_FIELD(UBioSeqAct_BlackScreen, m_eBlackScreenAction));  // game/AuthoredMoment
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
    CheckField("Engine.Pawn", "GroundSpeed", SDK_FIELD(APawn, GroundSpeed));  // game/sprint
    CheckField("SFXGame.BioPawn", "StormSpeed", SDK_FIELD(ABioPawn, StormSpeed));
    CheckField("SFXGame.BioPawn", "CombatStormSpeed", SDK_FIELD(ABioPawn, CombatStormSpeed));
    CheckField("SFXGame.BioPawn", "CombatGroundSpeed", SDK_FIELD(ABioPawn, CombatGroundSpeed));  // game/sprint: the drawn run
    CheckField("SFXGame.BioPlayerController", "bWantsToStorm", SDK_FIELD(ABioPlayerController, bWantsToStorm));  // StormOn's intent
    CheckField("Engine.Pawn", "Controller", SDK_FIELD(APawn, Controller));  // a squadmate's AI for a power order (research/54)
    CheckField("Engine.WorldInfo", "bPlayersOnly", SDK_BOOL(AWorldInfo, bPlayersOnly));
    CheckField("Engine.WorldInfo", "bPlayersOnlyPending", SDK_BOOL(AWorldInfo, bPlayersOnlyPending));
    CheckField("SFXGame.BioWorldInfo", "m_bPausedByFocusLoss", SDK_BOOL(ABioWorldInfo, m_bPausedByFocusLoss));  // game/FocusPause
    // The UI world's squad member behind the equipment and squad screens (game/UiWorldFigure, research/57 section 7).
    CheckClass<ACameraActor>("Engine.CameraActor");
    CheckField("SFXGame.BioWorldInfo", "m_UIWorld", SDK_FIELD(ABioWorldInfo, m_UIWorld));
    CheckField("SFXGame.BioUIWorld", "m_pWorld", SDK_FIELD(UBioUIWorld, m_pWorld));
    CheckField("SFXGame.BioUIWorld", "m_pCameraActor", SDK_FIELD(UBioUIWorld, m_pCameraActor));
    CheckField("SFXGame.BioUIWorld", "m_PostProcessChain", SDK_FIELD(UBioUIWorld, m_PostProcessChain));
    CheckField("SFXGame.BioUIWorld", "m_bLoaded", SDK_BOOL(UBioUIWorld, m_bLoaded));
    CheckField("SFXGame.BioUIWorld", "m_bEnabled", SDK_BOOL(UBioUIWorld, m_bEnabled));
    CheckField("Engine.CameraActor", "FOVAngle", SDK_FIELD(ACameraActor, FOVAngle));
    CheckField("Engine.CameraActor", "AspectRatio", SDK_FIELD(ACameraActor, AspectRatio));
    CheckField("Engine.PrimitiveComponent", "bOwnerNoSee", SDK_BOOL(UPrimitiveComponent, bOwnerNoSee));
    // game/ConversationWorld's `convo: body:` (research/69 "The plan for the blocker"): every flag that stops a pawn being drawn.
    CheckField("SFXGame.BioPawn", "m_nHiddenRequestCount", SDK_FIELD(ABioPawn, m_nHiddenRequestCount));
    // game/ConversationWorld's looks by part (research/69 "Who attends to whom"): the pawn's own look-at target.
    CheckField("SFXGame.BioPawn", "m_oLookAtData", SDK_FIELD(ABioPawn, m_oLookAtData));
    CheckField("SFXGame.BioLookAt", "m_oTarget", SDK_FIELD(UBioLookAt, m_oTarget));
    CheckField("SFXGame.BioLookAt", "m_nRank", SDK_FIELD(UBioLookAt, m_nRank));
    CheckField("SFXGame.BioLookAt", "m_Controllers", SDK_FIELD(UBioLookAt, m_Controllers));
    // game/ConversationWorld's walk to a mark by the engine's walking (research/69 "Walking to marks"; Acceleration and
    // Physics have rows above).
    CheckField("Engine.Pawn", "AccelRate", SDK_FIELD(APawn, AccelRate));
    CheckField("Engine.Pawn", "bIsWalking", SDK_BOOL(APawn, bIsWalking));
    // ... and by the AI: the behaviour's AI enable counters the walk raises (BioAiController.CanUseStack reads them).
    CheckField("Engine.Actor", "oBioComponent", SDK_FIELD(AActor, oBioComponent));
    CheckField("SFXGame.BioPawn", "m_bInGestures", SDK_BOOL(ABioPawn, m_bInGestures));  // ... and its legs (out of the gesture mode)
    CheckField("SFXGame.BioEpicPawnBehavior", "m_nALLEnabled", SDK_FIELD(UBioEpicPawnBehavior, m_nALLEnabled));
    CheckField("SFXGame.BioEpicPawnBehavior", "m_nStackEnabled", SDK_FIELD(UBioEpicPawnBehavior, m_nStackEnabled));
    CheckField("SFXGame.BioLookAt", "m_Definition", SDK_FIELD(UBioLookAt, m_Definition));
    CheckField("SFXGame.BioPawnLookAtController", "mc_pDefinition", SDK_FIELD(UBioPawnLookAtController, mc_pDefinition));
    CheckField("SFXGame.BioLookAtDefinition", "BoneDefinitions", SDK_FIELD(UBioLookAtDefinition, BoneDefinitions));
    CheckField("Engine.PrimitiveComponent", "bIgnoreOwnerHidden", SDK_BOOL(UPrimitiveComponent, bIgnoreOwnerHidden));
    CheckField("Engine.PrimitiveComponent", "bIgnoreHiddenActorsMembership", SDK_BOOL(UPrimitiveComponent, bIgnoreHiddenActorsMembership));
    CheckField("Engine.PrimitiveComponent", "LastRenderTime", SDK_FIELD(UPrimitiveComponent, LastRenderTime));
    CheckField("Engine.PlayerController", "HiddenActors", SDK_FIELD(APlayerController, HiddenActors));
    CheckClass<UBioCameraBehaviorConversation>("SFXGame.BioCameraBehaviorConversation");
    CheckField("SFXGame.BioCameraBehaviorConversation", "m_fNearPlane", SDK_FIELD(UBioCameraBehaviorConversation, m_fNearPlane));
    CheckField("SFXGame.BioCameraBehaviorConversation", "CAMERA_FOV", SDK_FIELD(UBioCameraBehaviorConversation, CAMERA_FOV));
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
    CheckField("GFxUI.GFxMovieInfo", "RawData", SDK_FIELD(UGFxMovieInfo, RawData));  // game/GameArt
    CheckField("Engine.BioSFPanel", "bFullScreen", SDK_BOOL(UBioSFPanel, bFullScreen));
    CheckField("Engine.BioSFPanel", "m_DefaultHandler", SDK_FIELD(UBioSFPanel, m_DefaultHandler));
    CheckField("Engine.BioSFPanel", "IsVisible", SDK_BOOL(UBioSFPanel, IsVisible));
    CheckField("Engine.BioSFPanel", "ScriptHandlers", SDK_FIELD(UBioSFPanel, ScriptHandlers));  // game/ScreenReticle
    CheckField("Engine.BioSFPanel", "MovieRef", SDK_FIELD(UBioSFPanel, MovieRef));
    CheckField("SFXGame.BioSFHandler_Reticule", "m_Reticule", SDK_FIELD(UBioSFHandler_Reticule, m_Reticule));
    CheckField("SFXGame.BioSFHandler_Reticule", "m_eLastReticuleMode", SDK_FIELD(UBioSFHandler_Reticule, m_eLastReticuleMode));
    CheckField("SFXGame.BioSFHandler_Reticule.ReticuleInfo", "m_eMode", SDK_FIELD(FReticuleInfo, m_eMode));
    CheckField("SFXGame.BioSFHandler_Loot", "m_oLootInventory", SDK_FIELD(UBioSFHandler_Loot, m_oLootInventory));
    CheckField("SFXGame.BioSkillGame_Handler", "m_oDependent", SDK_FIELD(UBioSkillGame_Handler, m_oDependent));
    CheckField("SFXGame.BioSkillGame_Handler", "m_bGameActive", SDK_BOOL(UBioSkillGame_Handler, m_bGameActive));  // the puzzle played: its d-pad
    CheckField("SFXGame.BioSFHandler_Shop", "m_shop", SDK_FIELD(UBioSFHandler_Shop, m_shop));
    // game/ConversationWorld (research/59): the live conversation, its actors and the dialogue wheel.
    // Walking also uses Engine.Pawn.Controller, Engine.Actor.Velocity, Actor.SetLocation/SetRotation and PushMoveToPoint:
    // their shared field/function/parameter rows are below (and with SquadOrders).
    CheckField("SFXGame.BioSeqData_MoveToPoint", "Terminate", SDK_BOOL(UBioSeqData_MoveToPoint, Terminate));
    CheckField("SFXGame.BioWorldInfo", "m_oCurrentConversation", SDK_FIELD(ABioWorldInfo, m_oCurrentConversation));
    CheckField("SFXGame.BioConversation", "m_pSpeaker", SDK_FIELD(UBioConversation, m_pSpeaker));
    CheckField("SFXGame.BioConversation", "m_pListener", SDK_FIELD(UBioConversation, m_pListener));
    CheckField("SFXGame.BioConversation", "m_pConversationOwner", SDK_FIELD(UBioConversation, m_pConversationOwner));
    CheckField("SFXGame.BioConversation", "m_pConversationPlayer", SDK_FIELD(UBioConversation, m_pConversationPlayer));
    CheckField("SFXGame.BioConversation", "m_stage", SDK_FIELD(UBioConversation, m_stage));
    CheckField("SFXGame.BioConversation", "m_SpeakerList", SDK_FIELD(UBioConversation, m_SpeakerList));
    CheckField("SFXGame.BioConversation", "m_LookAtList", SDK_FIELD(UBioConversation, m_LookAtList));
    CheckField("SFXGame.BioConversation", "m_aFSPrevPositions", SDK_FIELD(UBioConversation, m_aFSPrevPositions));
    CheckField("SFXGame.BioConversation", "m_sCurrentSubTitle", SDK_FIELD(UBioConversation, m_sCurrentSubTitle));
    CheckField("SFXGame.BioConversation", "m_nCurrentEntry", SDK_FIELD(UBioConversation, m_nCurrentEntry));
    CheckField("SFXGame.BioConversation", "m_sCurrentSubTitle", SDK_FIELD(UBioConversation, m_sCurrentSubTitle));  // game/ConversationFlow: a flowed line's subtitle
    CheckField("SFXGame.BioConversation", "m_nSubtitlesMode", SDK_FIELD(UBioConversation, m_nSubtitlesMode));
    CheckField("SFXGame.BioWorldInfo", "m_Subtitles", SDK_FIELD(ABioWorldInfo, m_Subtitles));
    CheckField("SFXGame.BioSubtitles", "m_CurrentRenderMode", SDK_FIELD(UBioSubtitles, m_CurrentRenderMode));
    CheckField("SFXGame.BioConversation", "bUseFixedCameras", SDK_BOOL(UBioConversation, bUseFixedCameras));
    // game/ConversationFlow (research/59 "Flowing conversations"): the chain starts' entries flagged ambient before the level's
    // script starts them, the live conversation's interrupt range.
    CheckField("SFXGame.BioConversation", "m_EntryList", SDK_FIELD(UBioConversation, m_EntryList));
    CheckField("SFXGame.BioConversation", "m_ReplyList", SDK_FIELD(UBioConversation, m_ReplyList));
    CheckField("SFXGame.BioConversation", "m_fInterruptRange", SDK_FIELD(UBioConversation, m_fInterruptRange));
    CheckField("SFXGame.BioConversation", "m_bAmbient", SDK_BOOL(UBioConversation, m_bAmbient));
    CheckClass<UBioSeqAct_StartConversation>("SFXGame.BioSeqAct_StartConversation");
    CheckField("SFXGame.BioSeqAct_StartConversation", "Conv", SDK_FIELD(UBioSeqAct_StartConversation, Conv));
    CheckField("SFXGame.BioSeqAct_StartConversation", "m_fInterruptRange", SDK_FIELD(UBioSeqAct_StartConversation, m_fInterruptRange));
    CheckField("SFXGame.BioWorldInfo", "m_fConversationInterruptDistance", SDK_FIELD(ABioWorldInfo, m_fConversationInterruptDistance));
    // research/69: a flowed chain protected from barks (the newcomer's starts), the walk's triggers.
    CheckField("SFXGame.BioConversation", "m_StartingList", SDK_FIELD(UBioConversation, m_StartingList));
    CheckField("SFXGame.BioConversation", "m_pKismetStart", SDK_FIELD(UBioConversation, m_pKismetStart));
    CheckClass<ATrigger>("Engine.Trigger");
    CheckClass<ATriggerVolume>("Engine.TriggerVolume");
    CheckField("SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel", SDK_BOOL(UBioSFHandler_Conversation, m_bDisplayingWheel));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_bSlotsUsed", SDK_FIELD(UBioSFHandler_Conversation, m_bSlotsUsed));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_aReplyLocations", SDK_FIELD(UBioSFHandler_Conversation, m_aReplyLocations));  // game/Thespian: mevr thespian stick
    // game/Thespian (research/70): the live wheel's replies and the conversation's state around a commit.
    CheckField("SFXGame.BioSFHandler_Conversation", "m_bDisplayInvestigateSubMenu", SDK_BOOL(UBioSFHandler_Conversation, m_bDisplayInvestigateSubMenu));
    CheckField("SFXGame.BioConversation", "m_lstCurrentReplyIndices", SDK_FIELD(UBioConversation, m_lstCurrentReplyIndices));
    CheckField("SFXGame.BioConversation", "m_nSelectedReply", SDK_FIELD(UBioConversation, m_nSelectedReply));
    CheckField("SFXGame.BioConversation", "m_nDelayedReplyChoice", SDK_FIELD(UBioConversation, m_nDelayedReplyChoice));
    CheckField("SFXGame.BioConversation", "m_fRemainingVOLength", SDK_FIELD(UBioConversation, m_fRemainingVOLength));
    CheckField("SFXGame.BioConversation", "m_fShowRepliesOffset", SDK_FIELD(UBioConversation, m_fShowRepliesOffset));
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
    CheckField("Engine.Camera", "bEnableFading", SDK_BOOL(ACamera, bEnableFading));  // a matinee's fade in a held moment (game/ConversationWorld)
    CheckField("Engine.Camera", "FadeAmount", SDK_FIELD(ACamera, FadeAmount));
    CheckField("Engine.WorldInfo", "PawnList", SDK_FIELD(AWorldInfo, PawnList));
    CheckField("Engine.Pawn", "CylinderComponent", SDK_FIELD(APawn, CylinderComponent));  // the Mako's gunner: a target's size
    CheckField("Engine.CylinderComponent", "CollisionRadius", SDK_FIELD(UCylinderComponent, CollisionRadius));
    CheckField("Engine.CylinderComponent", "CollisionHeight", SDK_FIELD(UCylinderComponent, CollisionHeight));
    CheckField("Engine.WorldInfo", "TimeSeconds", SDK_FIELD(AWorldInfo, TimeSeconds));  // mevr crowd
    CheckField("SFXGame.BioEpicPawnBehavior", "m_powers", SDK_FIELD(UBioEpicPawnBehavior, m_powers));  // the squad context (research/54)
    CheckField("SFXGame.BioPower", "m_nmPower", SDK_FIELD(UBioPower, m_nmPower));
    CheckField("SFXGame.BioPower", "m_fCooldownRemaining", SDK_FIELD(UBioPower, m_fCooldownRemaining));
    CheckField("SFXGame.BioPower", "m_fTotalCooldownTime", SDK_FIELD(UBioPower, m_fTotalCooldownTime));  // the power wheel (research/55)
    CheckField("SFXGame.BioPlayerInput", "MappedPower", SDK_FIELD(UBioPlayerInput, MappedPower));
    CheckField("SFXGame.BioPower", "m_nEnabled", SDK_FIELD(UBioPower, m_nEnabled));
    CheckField("SFXGame.BioPower", "m_nSuppressedCount", SDK_FIELD(UBioPower, m_nSuppressedCount));
    CheckField("SFXGame.BioTriggerStream", "StreamingStates", SDK_FIELD(ABioTriggerStream, StreamingStates));  // mevr stream
    // `mevr stream state` and `chunks` (research/59 "The launcher's gaps closed"): the world's named streaming states, the
    // plot's action that sets one, and each streaming level's chunk.
    CheckField("SFXGame.BioWorldInfo", "m_WorldStreamingStates", SDK_FIELD(ABioWorldInfo, m_WorldStreamingStates));
    CheckField("SFXGame.BioSeqAct_SetStreamingState", "StateName", SDK_FIELD(UBioSeqAct_SetStreamingState, StateName));
    CheckField("SFXGame.BioSeqAct_SetStreamingState", "NewValue", SDK_BOOL(UBioSeqAct_SetStreamingState, NewValue));
    CheckField("Engine.LevelStreaming", "VirtualChunkName", SDK_FIELD(ULevelStreaming, VirtualChunkName));
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
    // engine/VoicePlace's census (research/69 A2): each sound source's cue, and whether its component spatializes.
    CheckField("Engine.AudioComponent", "SoundCue", SDK_FIELD(UAudioComponent, SoundCue));
    CheckField("Engine.AudioComponent", "CurrentUseSpatialization", SDK_FIELD(UAudioComponent, CurrentUseSpatialization), true);
    CheckField("Engine.PrimitiveComponent", "LocalToWorld", SDK_FIELD(UPrimitiveComponent, LocalToWorld), true);
    CheckField("Engine.SkeletalMeshComponent", "SkeletalMesh", SDK_FIELD(USkeletalMeshComponent, SkeletalMesh));
    CheckField("Engine.SkeletalMeshComponent", "AnimTreeTemplate", SDK_FIELD(USkeletalMeshComponent, AnimTreeTemplate));
    CheckField("Engine.SkeletalMeshComponent", "Animations", SDK_FIELD(USkeletalMeshComponent, Animations));
    CheckField("Engine.SkeletalMeshComponent", "SpaceBases", SDK_FIELD(USkeletalMeshComponent, SpaceBases), true);
    CheckField("Engine.SkeletalMeshComponent", "ParentAnimComponent", SDK_FIELD(USkeletalMeshComponent, ParentAnimComponent));
    CheckField("Engine.SkeletalMeshComponent", "bConsiderAllBodiesForBounds", SDK_BOOL(USkeletalMeshComponent, bConsiderAllBodiesForBounds));
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
    CheckClass<ACoverLink>("Engine.CoverLink");  // the squad orders' retreat to cover (research/54)
    CheckField("Engine.CoverLink", "Slots", SDK_FIELD(ACoverLink, Slots));
    CheckField("Engine.CoverLink", "bDisabled", SDK_BOOL(ACoverLink, bDisabled));
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
    // Omni-tool discovery and live component parameter route (2026-11-09).
    CheckField("Engine.ParticleSystemComponent", "SMComponents", SDK_FIELD(UParticleSystemComponent, SMComponents));
    CheckField("Engine.ParticleSystemComponent", "SMMaterialInterfaces", SDK_FIELD(UParticleSystemComponent, SMMaterialInterfaces));
    CheckField("Engine.ParticleModuleMeshMaterial", "MeshMaterials", SDK_FIELD(UParticleModuleMeshMaterial, MeshMaterials));
    CheckField("Engine.ParticleModuleMaterialByParameter", "DefaultMaterials", SDK_FIELD(UParticleModuleMaterialByParameter, DefaultMaterials));
    CheckField("Engine.ParticleSystem", "Emitters", SDK_FIELD(UParticleSystem, Emitters));
    CheckField("Engine.ParticleEmitter", "LODLevels", SDK_FIELD(UParticleEmitter, LODLevels));
    CheckField("Engine.ParticleLODLevel", "Modules", SDK_FIELD(UParticleLODLevel, Modules));
    CheckField("Engine.ParticleLODLevel", "RequiredModule", SDK_FIELD(UParticleLODLevel, RequiredModule));
    CheckField("Engine.ParticleLODLevel", "SpawnModule", SDK_FIELD(UParticleLODLevel, SpawnModule));
    CheckField("Engine.ParticleLODLevel", "TypeDataModule", SDK_FIELD(UParticleLODLevel, TypeDataModule));
    CheckField("Engine.ParticleModuleRequired", "Material", SDK_FIELD(UParticleModuleRequired, Material));
    CheckField("Engine.ParticleModuleTypeDataMesh", "Mesh", SDK_FIELD(UParticleModuleTypeDataMesh, Mesh));
    CheckField("Engine.StaticMesh", "LODInfo", SDK_FIELD(UStaticMesh, LODInfo));
    CheckField("Engine.DistributionFloatParameterBase", "ParameterName", SDK_FIELD(UDistributionFloatParameterBase, ParameterName));
    CheckField("Engine.DistributionVectorParameterBase", "ParameterName", SDK_FIELD(UDistributionVectorParameterBase, ParameterName));
    CheckField("Engine.DistributionFloatConstant", "Constant", SDK_FIELD(UDistributionFloatConstant, Constant));
    CheckField("Engine.DistributionVectorConstant", "Constant", SDK_FIELD(UDistributionVectorConstant, Constant));
    CheckField("Engine.DistributionFloatParameterBase", "ParamMode", SDK_FIELD(UDistributionFloatParameterBase, ParamMode));
    CheckField("Engine.DistributionVectorParameterBase", "ParamModes", SDK_FIELD(UDistributionVectorParameterBase, ParamModes));
    CheckField("Engine.ParticleSystemComponent", "InstanceParameters", SDK_FIELD(UParticleSystemComponent, InstanceParameters));
    CheckElement("Engine.StaticMesh", "LODInfo", sizeof(FStaticMeshLODInfo));
    CheckPath("Engine.StaticMesh", {"LODInfo", "Elements"}, SDK_AT(offsetof(FStaticMeshLODInfo, Elements), 16));
    CheckPath("Engine.StaticMesh", {"LODInfo", "Elements", "Material"}, SDK_AT(offsetof(FStaticMeshLODElement, Material), 8));
    CheckElement("Engine.ParticleSystemComponent", "InstanceParameters", sizeof(FParticleSysParam));
    CheckPath("Engine.ParticleSystemComponent", {"InstanceParameters", "Material"}, SDK_AT(offsetof(FParticleSysParam, Material), 8));
    CheckPath("Engine.ParticleSystemComponent", {"InstanceParameters", "Name"}, SDK_AT(offsetof(FParticleSysParam, Name), 8));
    CheckPath("Engine.ParticleSystemComponent", {"InstanceParameters", "ParamType"}, SDK_AT(offsetof(FParticleSysParam, ParamType), 1));
    CheckPath("Engine.ParticleSystemComponent", {"InstanceParameters", "Scalar"}, SDK_AT(offsetof(FParticleSysParam, Scalar), 4));
    CheckPath("Engine.ParticleSystemComponent", {"InstanceParameters", "Vector"}, SDK_AT(offsetof(FParticleSysParam, Vector), 16));
    CheckPath("Engine.ParticleSystemComponent", {"InstanceParameters", "Color"}, SDK_AT(offsetof(FParticleSysParam, Color), 4));
    CheckField("SFXGame.BioVisualEffect", "m_CachedInstanceParameters", SDK_FIELD(ABioVisualEffect, m_CachedInstanceParameters));
    CheckElement("SFXGame.BioVisualEffect", "m_CachedInstanceParameters", sizeof(FCachedInstanceParameter));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "ParameterName"}, SDK_AT(offsetof(FCachedInstanceParameter, ParameterName), 8));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "fCurrentValue"}, SDK_AT(offsetof(FCachedInstanceParameter, fCurrentValue), 4));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "OffsetInfo"}, SDK_AT(offsetof(FCachedInstanceParameter, OffsetInfo), 16));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "bInited"}, SDK_BOOL(FCachedInstanceParameter, bInited));
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
    // The cockpit's hazard lights: the Mako's hull health (2026-11-04; BioVehicleBase.m_oBehavior has its row below).
    CheckField("SFXGame.BioEpicPawnBehavior", "m_PawnAttributes", SDK_FIELD(UBioEpicPawnBehavior, m_PawnAttributes));
    CheckField("SFXGame.BioAttributesEpicPawn", "m_HealthCurrent", SDK_FIELD(UBioAttributesEpicPawn, m_HealthCurrent));
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
    CheckField("SFXGame.BioPawn", "CoverNormal", SDK_FIELD(ABioPawn, CoverNormal));  // VR cover: the facing for the damage cone
    CheckField("Engine.Pawn", "EyeHeight", SDK_FIELD(APawn, EyeHeight));  // VR cover: the shooter's eyes
    // `mevr cover hit`: a weapon's damage object, Shepard's shields.
    CheckField("SFXGame.BioWeapon", "m_pDamage", SDK_FIELD(ABioWeapon, m_pDamage));
    CheckField("SFXGame.BioActorBehavior", "m_oShield", SDK_FIELD(UBioActorBehavior, m_oShield));
    CheckField("SFXGame.BioShield", "m_pAttributes", SDK_FIELD(UBioShield, m_pAttributes));
    CheckField("SFXGame.BioAttributesShield", "m_DamageCapacity", SDK_FIELD(UBioAttributesShield, m_DamageCapacity));
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
    CheckField("SFXGame.SFXCameraMode", "bCollisionEnabled", SDK_BOOL(USFXCameraMode, bCollisionEnabled));  // game/CameraCollision
    // game/CameraShake (research/71): the body's camera shakes per mode, the guns' and the Mako's screen shakes.
    CheckField("SFXGame.SFXCameraMode", "bIsCameraShakeEnabled", SDK_BOOL(USFXCameraMode, bIsCameraShakeEnabled));
    CheckField("SFXGame.SFXWeaponStatsBase", "FiringShake", SDK_FIELD(USFXWeaponStatsBase, FiringShake));
    CheckField("SFXGame.SFXWeaponStatsBase", "TightAimFiringShake", SDK_FIELD(USFXWeaponStatsBase, TightAimFiringShake));
    CheckField("SFXGame.BioVehicleWheeled", "JumpShake", SDK_FIELD(ABioVehicleWheeled, JumpShake));
    CheckField("SFXGame.BioVehicleWheeled", "BoostShake", SDK_FIELD(ABioVehicleWheeled, BoostShake));
    CheckField("SFXGame.SFXCameraShakeBase", "TheShake", SDK_FIELD(USFXCameraShakeBase, TheShake));
    CheckPath("SFXGame.SFXCameraShakeBase", {"TheShake", "RotAmplitude"}, SDK_AT(offsetof(FScreenShakeStruct, RotAmplitude), 12));
    CheckPath("SFXGame.SFXCameraShakeBase", {"TheShake", "LocAmplitude"}, SDK_AT(offsetof(FScreenShakeStruct, LocAmplitude), 12));
    CheckPath("SFXGame.SFXCameraShakeBase", {"TheShake", "FOVAmplitude"}, SDK_AT(offsetof(FScreenShakeStruct, FOVAmplitude), 4));
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
    // game/InventoryProbe (research/57 section 6): the inventory screen's model, for our own touch inventory.
    CheckField("SFXGame.BioSFHandler_Inventory", "m_oInvInterface", SDK_FIELD(UBioSFHandler_Inventory, m_oInvInterface));
    CheckField("SFXGame.BioSFHandler_Inventory", "m_nCurrentSquadMember", SDK_FIELD(UBioSFHandler_Inventory, m_nCurrentSquadMember));
    CheckField("SFXGame.BioSFHandler_Inventory", "m_eCurrentSlot", SDK_FIELD(UBioSFHandler_Inventory, m_eCurrentSlot));
    CheckField("SFXGame.BioInventoryGuiInterface", "m_eCurrentSlot", SDK_FIELD(UBioInventoryGuiInterface, m_eCurrentSlot));
    CheckField("SFXGame.BioInventoryGuiInterface", "lstEquipSlotOrder", SDK_FIELD(UBioInventoryGuiInterface, lstEquipSlotOrder));
    CheckField("SFXGame.BioInventoryGuiInterface", "EquippableItemsList", SDK_FIELD(UBioInventoryGuiInterface, EquippableItemsList));
    CheckElement("SFXGame.BioInventoryGuiInterface", "EquippableItemsList", static_cast<long long>(sizeof(FItemInfo)));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "ItemName"}, SDK_AT(offsetof(FItemInfo, ItemName), 16));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "sophistication"}, SDK_AT(offsetof(FItemInfo, sophistication), 4));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "ManufacturerName"}, SDK_AT(offsetof(FItemInfo, ManufacturerName), 4));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "ItemCategoryName"}, SDK_AT(offsetof(FItemInfo, ItemCategoryName), 4));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "m_price"}, SDK_AT(offsetof(FItemInfo, m_price), 4));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "InvIndex"}, SDK_AT(offsetof(FItemInfo, InvIndex), 4));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "m_rawStats"},
        SDK_AT(offsetof(FItemInfo, m_rawStats), static_cast<long long>(sizeof(FBioInventoryGuiInterfaceItemStats))));
    CheckPath("SFXGame.BioInventoryGuiInterface", {"EquippableItemsList", "m_rawStats", "lstStats"},
        SDK_AT(offsetof(FBioInventoryGuiInterfaceItemStats, lstStats), static_cast<long long>(sizeof(FBioInventoryGuiInterfaceItemStat) * 3)));
    CheckPath("SFXGame.SFXWeaponStatsBase", {"AimModes", "ZoomFOV"}, SDK_AT(offsetof(FWeaponAimMode, ZoomFOV), 4));
    CheckPath("SFXGame.SFXWeaponStatsBase", {"AimModes", "bScoped"}, SDK_BOOL(FWeaponAimMode, bScoped));
    // Vent (research/45)
    CheckField("SFXGame.BioWeaponRanged", "m_fCurrentHeat", SDK_FIELD(ABioWeaponRanged, m_fCurrentHeat));
    CheckField("SFXGame.BioWeaponRanged", "m_bOverheated", SDK_BOOL(ABioWeaponRanged, m_bOverheated));
    // ...the game's cool-down effect, logged once per weapon (research/45 section 6)
    CheckField("SFXGame.BioWeaponRanged", "m_pCachedVFXAppearance", SDK_FIELD(ABioWeaponRanged, m_pCachedVFXAppearance));
    CheckField("SFXGame.BioWeaponRanged", "m_visualEffects", SDK_FIELD(ABioWeaponRanged, m_visualEffects));
    CheckField("SFXGame.BioWeaponVFXAppearance", "nmCoolDown", SDK_FIELD(UBioWeaponVFXAppearance, nmCoolDown));
    CheckField("SFXGame.BioWeaponVFXAppearance", "vfxCoolDown", SDK_FIELD(UBioWeaponVFXAppearance, vfxCoolDown));
    CheckField("SFXGame.BioWeaponVFXAppearance", "fCooldownPerSecond", SDK_FIELD(UBioWeaponVFXAppearance, fCooldownPerSecond));
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
    // The walk's speed from the push and the game's start, stop and skid steps (game/combat_stance, research/38 "Linear walking").
    CheckField("SFXGame.BioPawnBehavior", "m_fDesiredMaxSpeed", SDK_FIELD(UBioPawnBehavior, m_fDesiredMaxSpeed));
    CheckField("SFXGame.BioPawnBehavior", "m_bEnableStartAnim", SDK_BOOL(UBioPawnBehavior, m_bEnableStartAnim));
    CheckField("SFXGame.BioPawnBehavior", "m_bEnableStopAnim", SDK_BOOL(UBioPawnBehavior, m_bEnableStopAnim));
    CheckField("SFXGame.BioPawnBehavior", "m_bEnableSkidAnim", SDK_BOOL(UBioPawnBehavior, m_bEnableSkidAnim));
    CheckField("SFXGame.BioPawnBehavior", "m_eAnimStartState", SDK_FIELD(UBioPawnBehavior, m_eAnimStartState));
    CheckField("SFXGame.BioPawnBehavior", "m_eAnimStopState", SDK_FIELD(UBioPawnBehavior, m_eAnimStopState));
    CheckField("SFXGame.BioPawnBehavior", "m_eAnimSkidState", SDK_FIELD(UBioPawnBehavior, m_eAnimSkidState));
    CheckField("SFXGame.BioPlayerController", "MoveStickIdleThreshold", SDK_FIELD(ABioPlayerController, MoveStickIdleThreshold));
    CheckField("SFXGame.BioPlayerController", "MoveStickWalkThreshold", SDK_FIELD(ABioPlayerController, MoveStickWalkThreshold));
    CheckField("SFXGame.BioPlayerController", "MoveStickRunThreshold", SDK_FIELD(ABioPlayerController, MoveStickRunThreshold));
    CheckField("Engine.PlayerInput", "RawJoyUp", SDK_FIELD(UPlayerInput, RawJoyUp));
    CheckField("Engine.PlayerInput", "RawJoyRight", SDK_FIELD(UPlayerInput, RawJoyRight));
    CheckField("Engine.PlayerController", "bIgnoreMoveInput", SDK_FIELD(APlayerController, bIgnoreMoveInput));
    CheckField("Engine.Pawn", "DesiredSpeed", SDK_FIELD(APawn, DesiredSpeed));
    CheckField("SFXGame.BioEpicPawnBehavior", "Squad", SDK_FIELD(UBioEpicPawnBehavior, Squad));
    CheckField("SFXGame.BioActorBehavior", "ActorGameName", SDK_FIELD(UBioActorBehavior, ActorGameName));
    CheckField("SFXGame.BioActorBehavior", "m_oActorType", SDK_FIELD(UBioActorBehavior, m_oActorType));
    CheckField("SFXGame.BioActorBehavior", "bActive", SDK_BOOL(UBioActorBehavior, bActive));  // the power lock skips dormant pawns
    // The powers' own visual effects (research/55 section 9): the appearance's slots and the templates' prefabs.
    CheckField("SFXGame.BioPower", "m_VFXAppearance", SDK_FIELD(UBioPower, m_VFXAppearance));
    CheckField("SFXGame.BioPowerVFXAppearance", "PlayerCrust", SDK_FIELD(UBioPowerVFXAppearance, PlayerCrust));
    CheckField("SFXGame.BioPowerVFXAppearance", "PlayerMaterial", SDK_FIELD(UBioPowerVFXAppearance, PlayerMaterial));
    CheckField("SFXGame.BioPowerVFXAppearance", "UsingEffect", SDK_FIELD(UBioPowerVFXAppearance, UsingEffect));
    CheckField("SFXGame.BioPowerVFXAppearance", "FramebufferEffect", SDK_FIELD(UBioPowerVFXAppearance, FramebufferEffect));
    CheckField("SFXGame.BioPowerVFXAppearance", "TargetCrust", SDK_FIELD(UBioPowerVFXAppearance, TargetCrust));
    CheckField("SFXGame.BioPowerVFXAppearance", "TargetMaterial", SDK_FIELD(UBioPowerVFXAppearance, TargetMaterial));
    CheckField("SFXGame.BioPowerVFXAppearance", "ImpactVisualEffect", SDK_FIELD(UBioPowerVFXAppearance, ImpactVisualEffect));
    CheckField("SFXGame.BioPowerVFXAppearance", "WorldImpactVisualEffect", SDK_FIELD(UBioPowerVFXAppearance, WorldImpactVisualEffect));
    CheckField("SFXGame.BioPowerVFXAppearance", "CastingBeam", SDK_FIELD(UBioPowerVFXAppearance, CastingBeam));
    CheckField("SFXGame.BioPowerVFXAppearance", "nmProjectileAttachPoint", SDK_FIELD(UBioPowerVFXAppearance, nmProjectileAttachPoint));
    CheckField("SFXGame.BioPowerVFXAppearance", "vfxProjectileEffect", SDK_FIELD(UBioPowerVFXAppearance, vfxProjectileEffect));
    CheckField("SFXGame.BioPowerVFXAppearance", "vfxReleaseEffect", SDK_FIELD(UBioPowerVFXAppearance, vfxReleaseEffect));
    CheckField("SFXGame.BioVFXTemplate", "oPrefab", SDK_FIELD(UBioVFXTemplate, oPrefab));
    CheckField("SFXGame.BioVFXTemplate", "fLifeTime", SDK_FIELD(UBioVFXTemplate, fLifeTime));
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
    CheckElement("SFXGame.BioWorldInfo", "m_WorldStreamingStates", static_cast<long long>(sizeof(FWorldStreamingState)));  // mevr stream state
    CheckPath("SFXGame.BioWorldInfo", {"m_WorldStreamingStates", "Name"}, SDK_AT(offsetof(FWorldStreamingState, Name), 8));
    CheckPath("SFXGame.BioWorldInfo", {"m_WorldStreamingStates", "Enabled"}, SDK_BOOL(FWorldStreamingState, Enabled));
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
    CheckElement("Engine.CoverLink", "Slots", sizeof(FCoverSlot));
    // game/mako (research/44): the turret each vehicle weapon sits on.
    CheckElement("SFXGame.BioVehicleBehaviorBase", "m_aVehicleWeaponInfo", sizeof(FBioVehicleWeaponInfo));
    CheckPath("SFXGame.BioVehicleBehaviorBase", {"m_aVehicleWeaponInfo", "oVehicleWeaponPtr"},
        SDK_AT(offsetof(FBioVehicleWeaponInfo, oVehicleWeaponPtr), 8));
    CheckPath("SFXGame.BioVehicleBehaviorBase", {"m_aVehicleWeaponInfo", "nTurretIndex"}, SDK_AT(offsetof(FBioVehicleWeaponInfo, nTurretIndex), 4));
    CheckPath("Engine.AnimTree", {"SkelControlLists", "BoneName"}, SDK_AT(offsetof(FSkelControlListHead, BoneName), 8));
    CheckPath("Engine.AnimTree", {"SkelControlLists", "ControlHead"}, SDK_AT(offsetof(FSkelControlListHead, ControlHead), 8));
    CheckPath("Engine.CoverLink", {"Slots", "LocationOffset"}, SDK_AT(offsetof(FCoverSlot, LocationOffset), 12));
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
    // game/ConversationWorld: the player's eyes as a look-at target (research/69 "Heads that look past the player").
    CheckClass<ABioLookAtTarget>("SFXGame.BioLookAtTarget");
    // game/ConversationWorld: where a look-at controller aims (the stances from above).
    CheckField("Engine.SkelControlLookAt", "TargetLocation", SDK_FIELD(USkelControlLookAt, TargetLocation));
    // game/ConversationWorld: the squad's formation held quietly (research/69 "The squad's formation, held").
    CheckPath("SFXGame.BioBaseSquad", {"Members", "vHoldPosition"}, SDK_AT(offsetof(FMemberData, vHoldPosition), 12));
    CheckPath("SFXGame.BioBaseSquad", {"Members", "eFormation"}, SDK_AT(offsetof(FMemberData, eFormation), 1));
    // game/ConversationWorld: a scripted walk that waits for the player (research/69 "Walking along").
    CheckField("Engine.SeqAct_Interp", "bPaused", SDK_BOOL(USeqAct_Interp, bPaused));
    CheckField("Engine.SeqAct_Interp", "bIsPlaying", SDK_BOOL(USeqAct_Interp, bIsPlaying));
    CheckField("Engine.SeqAct_Interp", "GroupInst", SDK_FIELD(USeqAct_Interp, GroupInst));
    CheckField("Engine.InterpGroupInst", "GroupActor", SDK_FIELD(UInterpGroupInst, GroupActor));
    CheckField("Engine.InterpGroupInst", "TrackInst", SDK_FIELD(UInterpGroupInst, TrackInst));
    CheckField("Engine.InterpTrackInstSound", "PlayAudioComp", SDK_FIELD(UInterpTrackInstSound, PlayAudioComp));
    CheckClass<UInterpTrackInstMove>("Engine.InterpTrackInstMove");
    // game/ConversationWorld (research/69 item 7, "Seats for the pairs"): who says each line to whom, and who the indices name.
    CheckElement("SFXGame.BioConversation", "m_SpeakerList", sizeof(FBioDialogSpeaker));
    CheckPath("SFXGame.BioConversation", {"m_SpeakerList", "aSpeaker"}, SDK_AT(offsetof(FBioDialogSpeaker, aSpeaker), 8));
    CheckElement("SFXGame.BioConversation", "m_EntryList", sizeof(FBioDialogEntryNode));
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "nSpeakerIndex"}, SDK_AT(offsetof(FBioDialogEntryNode, nSpeakerIndex), 4));
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "nListenerIndex"}, SDK_AT(offsetof(FBioDialogEntryNode, nListenerIndex), 4));
    CheckElement("SFXGame.BioConversation", "m_ReplyList", sizeof(FBioDialogReplyNode));
    CheckPath("SFXGame.BioConversation", {"m_ReplyList", "nListenerIndex"}, SDK_AT(offsetof(FBioDialogReplyNode, nListenerIndex), 4));
    // The dialogue's graph (game/ConversationWorld's look ahead: who speaks within the next lines, research/69 item 28).
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "ReplyListNew"}, SDK_AT(offsetof(FBioDialogEntryNode, ReplyListNew), 16));
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "ReplyListNew", "nIndex"}, SDK_AT(offsetof(FBioDialogReplyListDetails, nIndex), 4));
    CheckPath("SFXGame.BioConversation", {"m_ReplyList", "EntryList"}, SDK_AT(offsetof(FBioDialogReplyNode, EntryList), 16));
    CheckPath("SFXGame.BioConversation", {"m_ReplyList", "bUnskippable"}, SDK_BOOL(FBioDialogReplyNode, bUnskippable));  // game/Thespian
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
        // mevr stream (the measuring loop's streaming volumes; `state`: research/59 "The launcher's gaps closed")
        "SFXGame.BioTriggerStream.Touch",
        "SFXGame.BioTriggerStream.Retouch",
        "SFXGame.BioSeqAct_SetStreamingState.Activated",
        // the omni-tool on Shepard's arm (game/OmniTool)
        "SFXGame.BioVisualEffect.CreateCrustEffect",
        "SFXGame.BioVisualEffect.SetLifeTime",
        "SFXGame.BioVisualEffect.SetScalarParameter",
        "SFXGame.BioVisualEffect.CreateVFXOnPosition",  // the bolt from the hand (research/55 section 9)
        "SFXGame.BioPawn.ReleasePower",                  // ...timed against the game's release
        "SFXGame.BioVisualEffect.SetState",
        "Engine.Actor.DetachComponent",
        "Engine.SkeletalMeshComponent.AttachComponent",
        "Engine.SkeletalMeshComponent.DetachComponent",
        "Engine.ParticleSystemComponent.ActivateSystem",
        "Engine.ParticleSystemComponent.DeactivateSystem",
        "Engine.ParticleSystemComponent.SetFloatParameter",
        "Engine.ParticleSystemComponent.SetVectorParameter",
        "Engine.ParticleSystemComponent.SetColorParameter",

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
        "SFXGame.BioWeaponRanged.GetRecoil",  // game/gun_kick (research/24 section 8)
        "SFXGame.BioWeaponRanged.TickFire",
        "SFXGame.BioWeaponRanged.GetAdjustedAim",
        "Engine.BioSFPanel.SetMovieVisibility",
        "Engine.BioSFPanel.InvokeMethod",
        "Engine.Bio2DA.GetFloatEntryII",
        "SFXGame.BioWorldInfo.CheckConditional",
        "SFXGame.BioArtPlaceableBehavior.OnArtPlaceableUsed",
        "SFXGame.BioSFHandler_GalaxyMap.TravelConfirm",
        "SFXGame.BioSFHandler_Conversation.SelectConversationEntry",  // game/ConversationWorld: mevr convo pick
        "SFXGame.BioSFHandler_Conversation.SkipConversation",
        "SFXGame.BioConversation.GetReplyText",                  // game/Thespian (research/70): the live wheel as script cards
        "SFXGame.BioConversation.GetReplyParaphraseText",
        "SFXGame.BioConversation.GetReplyCategory",
        "SFXGame.BioConversation.GetReplyGUIStyle",
        "SFXGame.BioConversation.QueueReply",                    // ... a reply taken as a wheel pick takes it
        "SFXGame.BioConversation.SkipNode",                      // ... Shepard's recording of it skipped
        "SFXGame.BioWorldInfo.EndCurrentConversation",            // ... and mevr convo end (game/ConversationWorld)
        "SFXGame.BioConversation.GetEntryText",                  // ... the other person's line, for the panel's header
        "SFXGame.BioSFHandler_Conversation.UpdateConversationOptions",  // ... the game's wheel shown again (it was hidden)
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
        "SFXGame.BioPower.IsRequiredWeaponEquipped",
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
        "Engine.Actor.PointCheckComponent",  // game/ConversationWorld: the walk's triggers (research/69 item 5)
        "Engine.SequenceOp.ForceActivateInput",  // game/ConversationFlow: a held start fired again (research/69 item 1)
        "Engine.Controller.OnTeleport",          // game/AuthoredMoment: the script's teleport of the player (research/69)
        "SFXGame.BioPawn.OnTeleport",
        "SFXGame.BioSeqAct_BlackScreen.Activated",
        "SFXGame.BioSeqAct_BioToggleCinematicMode.Activated",
        "SFXGame.MassEffectGuiManager.HideBlackScreen",
        "Engine.Actor.Destroy",
        // Vent (research/45)
        "SFXGame.BioWeaponRanged.DoCoolDownEffect",
        "Engine.SkeletalMeshComponent.ForceSkelUpdate",  // the visible rack, a gun not posed each frame (section 6)
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
        // SquadOrders (research/54)
        "SFXGame.BioPlayerController.QuickCommandMoveTo",
        "SFXGame.BioPlayerController.QuickCommandAttackTarget",
        "SFXGame.BioPlayerController.QuickCommandFollowPlayer",
        "SFXGame.BioBaseSquad.SetSquadFollowFormation",
        "SFXGame.BioBaseSquad.SetMemberHoldPosition",
        "SFXGame.BioBaseSquad.GetMemberFormation",
        "SFXGame.BioPlayerController.SquadOrderUsePower",  // power orders and the acknowledgement (research/54, 2026-11-05)
        "SFXGame.BioAiController.AddUsePower",
        "SFXGame.BioAiController.PushMoveToPoint",  // game/SquadPlace: the squadmate walked to the arc's point
        "SFXGame.BioAiController.DoesGameModeAllowMovement",  // game/ConversationWorld: answered for a walk to a mark
        "SFXGame.BioAI_Henchman.DoesGameModeAllowMovement",
        "Core.Object.GetStateName",  // game/ConversationWorld: worldWalkLog's probe of a walker's controller
        "SFXGame.BioPawn.SetDesiredSpeed",  // ... and a walker's speed under a paused controller
        "SFXGame.BioActorBehavior.AbilityEnable",  // ... and its AI switched on the game's way
        "SFXGame.BioSubtitles.DisplaySubtitle",  // game/ConversationFlow: a flowed line's subtitle
        "SFXGame.BioBaseSquad.SquadHandleChatterEvent",
        "SFXGame.BioPawn.PlaySoundSet",
        "SFXGame.SFXGameModeWeaponWheel.ExitWeaponWheel",
        "SFXGame.SFXGameModeWeaponWheel.Deactivated",  // seated holster fallback: draw on close
        "SFXGame.SFXGameModeDefault.TryHolster",
        "SFXGame.SFXGameModeDefault.StormOn",   // game/sprint: the sprint button as a faster run (2026-11-06)
        "SFXGame.SFXGameModeDefault.StormOff",
        "Engine.Pawn.GetMaxSpeed",              // ... the run in effect (2026-11-16)
        "SFXGame.SFXGameModeDefault.SwitchToWeapon",
        "SFXGame.SFXPawn_Player.DrawPrimaryWeapon",  // holsters' onlyHolstersDraw (2026-10-25)
        "SFXGame.SFXGameModeDefault.UseFirstAid",         // ...whose draws still go through from these (2026-10-26)
        "SFXGame.SFXGameModeDefault.TryUseMappedPower",
        "SFXGame.BioPlayerInput.ActivatePower",  // the power wheel's push (research/55 section 4)
        "SFXGame.SFXGameModeDefault.EnterPowerWheel",
        "SFXGame.SFXGameModePowerWheel.ExitPowerWheel",  // holsters: the right stick click's hold (2026-11-05)
        "SFXGame.SFXGameModeBase.FireWeapon",  // ...and the trigger's, refused without a buzz (2026-11-04)
        "SFXGame.SFXGameModeBase.StopFiringWeapon",  // `mevr vehicle fire mg` (2026-11-04)
        "Engine.PlayerController.GetScaleFormManager",
        // Posture, ViewLevel, Comfort, MenuPause (migrated in stage 1)
        "SFXGame.BioPawn.ShouldCrouch",
        "SFXGame.SFXGameModeDefault.TryExitCover",
        "SFXGame.BioPawn.IsInCover",
        "SFXGame.BioPawn.IsInCoverLeaning",  // VR cover (research/23 section 6): answered in the damage call
        "SFXGame.BioPawnBehavior.ProxyTakeDamage",
        "SFXGame.BioActorBehavior.TakeDamage",  // `mevr cover hit`
        "SFXGame.BioPawn.LeaveCover",
        "SFXGame.SFXInventoryManager.SetWeaponRecoil",
        "SFXGame.SFXModule_AimAssist.ViewFriction",
        "SFXGame.SFXModule_AimAssist.ViewAdhesion",
        "Engine.Actor.SetRotation",
        "Engine.WorldInfo.PauseGame",
        // GameMenuRequest (research/28): Start's Console_Menu on each mode that declares it
        "SFXGame.SFXGameModeBase.ShowMenuGui",
        // GameMenuRequest, the omni-tool's rail (research/57): the screens' own functions and the screens' B
        "SFXGame.SFXGameModeBase.ShowEquipmentGui",
        "SFXGame.SFXGameModeBase.ShowSquadGui",
        "SFXGame.SFXGameModeBase.ShowJournalGui",
        "SFXGame.SFXGameModeBase.ShowCodexGui",
        "SFXGame.SFXGameModeBase.ShowMapGui",
        "Engine.BioSFHandler.HandleInputEvent",
        "SFXGame.SFXGameModeBase.GuiKey",
        // game/InventoryProbe (research/57 section 6)
        "SFXGame.BioInventoryGuiInterface.SelectItemSlot",
        "SFXGame.BioInventoryGuiInterface.BuildEquippableItemList",
        "SFXGame.BioInventoryGuiInterface.Equip",
        "SFXGame.BioInventoryGuiInterface.GetEquippedItem",
        // game/GuiCursor (research/57 section 6)
        "Engine.BioSFPanel.GetVariableFloat",
        "Engine.BioSFPanel.GetVariableBool",
        "Engine.BioSFPanel.SetVariableFloat",
        "SFXGame.SFXGameModeVehicle.ShowMenuGui",
        // CockpitPanel (research/48 section 27): the repair button's and the extraction lever's calls, as Y and X in the Mako
        "SFXGame.SFXGameModeVehicle.RepairVehicle",
        "SFXGame.SFXGameModeVehicle.LeaveWorld",
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
        "SFXGame.BioPawn.RequestTurnInPlace",    // game/ConversationWorld: a body re-faces by the game's own turn (research/69)
        "SFXGame.BioPawn.SetLookAtTarget",       // game/ConversationWorld: a look by the person's part in the line (research/69)
        "SFXGame.BioPawn.SetDesiredRotation",    // game/ConversationWorld: a body re-faces by its own physics (research/69)
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
        "Engine.MaterialInstanceConstant.SetScalarParameterValue",  // the cockpit's hazard lights (2026-11-04)
        "SFXGame.BioEpicPawnBehavior.GetHealthAndShieldsScaled",
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
        "SFXGame.BioPower.IsEnabled",  // the power wheel (research/55)
        "SFXGame.BioPlayerInput.IsCombatEnabled",
        "SFXGame.BioPower.IsRequiredResourceAvailable",  // game/CockpitPanel: the repair's omni-gel
        "Engine.AudioComponent.BioIsPlaying",  // game/ConversationWorld: a walk pauses at a line's end
        "Engine.SkeletalMeshComponent.IsPlayingFaceFXAnim",
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
    // CombatStance's move set (research/38 "Linear walking"): every PlayerMoveExplore and PlayerMoveCombat.
    for (char const* name : {"PlayerMoveExplore", "PlayerMoveCombat"}) {
        Guard("function", std::format("{} (all)", name), [name] {
            std::vector<std::string> ours;
            for (ue3::Function* f : R().FindFunctionsNamed(name)) {
                ours.push_back(R().FullName(f));
            }
            std::vector<std::string> sdk;
            std::wstring const wide(name, name + std::strlen(name));
            for (UFunction* f : SdkFunctionsNamed(wide.c_str())) {
                sdk.push_back(SdkFullName(f));
            }
            std::sort(ours.begin(), ours.end());
            std::sort(sdk.begin(), sdk.end());
            Add("function", std::format("{} (all)", name), !ours.empty() && ours == sdk, std::format("{} functions", ours.size()),
                std::format("{} functions", sdk.size()));
        });
    }
}

void CheckParams() {
    PARAM(UParticleSystemComponent_execSetFloatParameter_Parms, "Engine.ParticleSystemComponent.SetFloatParameter", ParameterName);
    PARAM(UParticleSystemComponent_execSetFloatParameter_Parms, "Engine.ParticleSystemComponent.SetFloatParameter", Param);
    PARMS(UParticleSystemComponent_execSetFloatParameter_Parms, "Engine.ParticleSystemComponent.SetFloatParameter");
    PARAM(UParticleSystemComponent_execSetVectorParameter_Parms, "Engine.ParticleSystemComponent.SetVectorParameter", ParameterName);
    PARAM(UParticleSystemComponent_execSetVectorParameter_Parms, "Engine.ParticleSystemComponent.SetVectorParameter", Param);
    PARMS(UParticleSystemComponent_execSetVectorParameter_Parms, "Engine.ParticleSystemComponent.SetVectorParameter");
    PARAM(UParticleSystemComponent_execSetColorParameter_Parms, "Engine.ParticleSystemComponent.SetColorParameter", ParameterName);
    PARAM(UParticleSystemComponent_execSetColorParameter_Parms, "Engine.ParticleSystemComponent.SetColorParameter", Param);
    PARMS(UParticleSystemComponent_execSetColorParameter_Parms, "Engine.ParticleSystemComponent.SetColorParameter");
    PARAM(UMaterialInstanceConstant_execSetScalarParameterValue_Parms, "Engine.MaterialInstanceConstant.SetScalarParameterValue", ParameterName);
    PARAM(UMaterialInstanceConstant_execSetScalarParameterValue_Parms, "Engine.MaterialInstanceConstant.SetScalarParameterValue", Value);
    PARMS(UMaterialInstanceConstant_execSetScalarParameterValue_Parms, "Engine.MaterialInstanceConstant.SetScalarParameterValue");

    PARAM(ABioVisualEffect_execSetScalarParameter_Parms, "SFXGame.BioVisualEffect.SetScalarParameter", sParameter);
    PARAM(ABioVisualEffect_execSetScalarParameter_Parms, "SFXGame.BioVisualEffect.SetScalarParameter", fValue);
    PARMS(ABioVisualEffect_execSetScalarParameter_Parms, "SFXGame.BioVisualEffect.SetScalarParameter");
    PARAM(ABioWeaponRanged_execGetMuzzleLoc_Parms, "SFXGame.BioWeaponRanged.GetMuzzleLoc", ReturnValue);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", InSocketName);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutLocation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutRotation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", Space);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", ReturnValue);
    PARAM(ABioWeaponRanged_execGetRangedWeaponType_Parms, "SFXGame.BioWeaponRanged.GetRangedWeaponType", ReturnValue);
    PARAM(ABioWeaponRanged_execGetPlayerAimError_Parms, "SFXGame.BioWeaponRanged.GetPlayerAimError", Accuracy);
    PARAM(ABioWeaponRanged_execGetPlayerAimError_Parms, "SFXGame.BioWeaponRanged.GetPlayerAimError", ReturnValue);
    PARAM(ABioWeaponRanged_execGetRecoil_Parms, "SFXGame.BioWeaponRanged.GetRecoil", ReturnValue);
    PARAM(ABioGrenadeProjectile_execSetVelocity_Parms, "SFXGame.BioGrenadeProjectile.SetVelocity", vVelocity);
    PARAM(ABioGrenade_eventLaunchGrenadeAtPoint_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtPoint", vTarget);
    PARAM(ABioGrenade_eventLaunchGrenadeAtPoint_Parms, "SFXGame.BioGrenade.LaunchGrenadeAtPoint", ReturnValue);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", HitLocation);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", HitNormal);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceEnd);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceStart);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", bTraceActors);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", ReturnValue);
    PARAM(USequenceOp_execForceActivateInput_Parms, "Engine.SequenceOp.ForceActivateInput", InputIdx);
    PARAM(AActor_execPointCheckComponent_Parms, "Engine.Actor.PointCheckComponent", InComponent);
    PARAM(AActor_execPointCheckComponent_Parms, "Engine.Actor.PointCheckComponent", PointLocation);
    PARAM(AActor_execPointCheckComponent_Parms, "Engine.Actor.PointCheckComponent", PointExtent);
    PARAM(AActor_execPointCheckComponent_Parms, "Engine.Actor.PointCheckComponent", ReturnValue);
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
    PARAM(UBioPower_execIsEnabled_Parms, "SFXGame.BioPower.IsEnabled", ReturnValue);  // the power wheel (research/55)
    PARAM(UBioPower_execIsRequiredResourceAvailable_Parms, "SFXGame.BioPower.IsRequiredResourceAvailable", ReturnValue);  // game/CockpitPanel
    PARAM(UBioPlayerInput_execIsCombatEnabled_Parms, "SFXGame.BioPlayerInput.IsCombatEnabled", ReturnValue);
    PARAM(UBioPower_execIsRequiredWeaponEquipped_Parms, "SFXGame.BioPower.IsRequiredWeaponEquipped", ReturnValue);
    PARAM(UBioPlayerInput_execActivatePower_Parms, "SFXGame.BioPlayerInput.ActivatePower", nmPower);  // the push
    PARAM(UBioPlayerInput_execActivatePower_Parms, "SFXGame.BioPlayerInput.ActivatePower", oTarget);
    PARAM(UBioPlayerInput_execActivatePower_Parms, "SFXGame.BioPlayerInput.ActivatePower", vTargetLocation);
    PARAM(UBioPlayerInput_execActivatePower_Parms, "SFXGame.BioPlayerInput.ActivatePower", ReturnValue);
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
    PARAM(UBioSFPanel_execGetVariableFloat_Parms, "Engine.BioSFPanel.GetVariableFloat", sPath);
    PARAM(UBioSFPanel_execGetVariableFloat_Parms, "Engine.BioSFPanel.GetVariableFloat", ReturnValue);
    PARAM(UBioSFPanel_execGetVariableBool_Parms, "Engine.BioSFPanel.GetVariableBool", sPath);
    PARAM(UBioSFPanel_execGetVariableBool_Parms, "Engine.BioSFPanel.GetVariableBool", ReturnValue);
    PARAM(UBioSFPanel_execSetVariableFloat_Parms, "Engine.BioSFPanel.SetVariableFloat", sPath);
    PARAM(UBioSFPanel_execSetVariableFloat_Parms, "Engine.BioSFPanel.SetVariableFloat", fVar);
    PARAM(UBioSFPanel_execInvokeMethod_Parms, "Engine.BioSFPanel.InvokeMethod", sMethodName);
    PARAM(UBio2DA_execGetFloatEntryII_Parms, "Engine.Bio2DA.GetFloatEntryII", nRow);
    PARAM(UBio2DA_execGetFloatEntryII_Parms, "Engine.Bio2DA.GetFloatEntryII", nColumn);
    PARAM(UBio2DA_execGetFloatEntryII_Parms, "Engine.Bio2DA.GetFloatEntryII", fEntry);
    PARAM(ABioWorldInfo_execCheckConditional_Parms, "SFXGame.BioWorldInfo.CheckConditional", nConditional);
    PARAM(ABioWorldInfo_execCheckConditional_Parms, "SFXGame.BioWorldInfo.CheckConditional", nParam);
    PARAM(ABioWorldInfo_execCheckConditional_Parms, "SFXGame.BioWorldInfo.CheckConditional", ReturnValue);
    PARAM(UBioArtPlaceableBehavior_eventOnArtPlaceableUsed_Parms, "SFXGame.BioArtPlaceableBehavior.OnArtPlaceableUsed", User);
    PARAM(UBioSFHandler_GalaxyMap_execTravelConfirm_Parms, "SFXGame.BioSFHandler_GalaxyMap.TravelConfirm", nContext);
    PARAM(UBioSFHandler_Conversation_execSelectConversationEntry_Parms, "SFXGame.BioSFHandler_Conversation.SelectConversationEntry", nWheelLocation);
    PARAM(UBioConversation_execGetReplyText_Parms, "SFXGame.BioConversation.GetReplyText", nIndex);
    PARAM(UBioConversation_execGetReplyText_Parms, "SFXGame.BioConversation.GetReplyText", bParse);
    PARAM(UBioConversation_execGetReplyText_Parms, "SFXGame.BioConversation.GetReplyText", ReturnValue);
    PARAM(UBioConversation_execGetReplyParaphraseText_Parms, "SFXGame.BioConversation.GetReplyParaphraseText", nIndex);
    PARAM(UBioConversation_execGetReplyParaphraseText_Parms, "SFXGame.BioConversation.GetReplyParaphraseText", ReturnValue);
    PARAM(UBioConversation_execGetReplyCategory_Parms, "SFXGame.BioConversation.GetReplyCategory", nIndex);
    PARAM(UBioConversation_execGetReplyCategory_Parms, "SFXGame.BioConversation.GetReplyCategory", ReturnValue);
    PARAM(UBioConversation_execGetReplyGUIStyle_Parms, "SFXGame.BioConversation.GetReplyGUIStyle", nIndex);
    PARAM(UBioConversation_execGetReplyGUIStyle_Parms, "SFXGame.BioConversation.GetReplyGUIStyle", ReturnValue);
    PARAM(UBioConversation_execQueueReply_Parms, "SFXGame.BioConversation.QueueReply", nReply);
    PARAM(UBioConversation_execQueueReply_Parms, "SFXGame.BioConversation.QueueReply", ReturnValue);
    PARAM(UBioConversation_execSkipNode_Parms, "SFXGame.BioConversation.SkipNode", ReturnValue);
    PARAM(UBioConversation_execGetEntryText_Parms, "SFXGame.BioConversation.GetEntryText", bParse);
    PARAM(UBioConversation_execGetEntryText_Parms, "SFXGame.BioConversation.GetEntryText", ReturnValue);
    PARAM(UBioSFHandler_Conversation_execUpdateConversationOptions_Parms, "SFXGame.BioSFHandler_Conversation.UpdateConversationOptions", oConversation);
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
    PARAM(ABioPlayerController_execQuickCommandMoveTo_Parms, "SFXGame.BioPlayerController.QuickCommandMoveTo", iSquadMember);
    PARAM(ABioPlayerController_execQuickCommandMoveTo_Parms, "SFXGame.BioPlayerController.QuickCommandMoveTo", ReturnValue);
    PARAM(ABioPlayerController_execQuickCommandAttackTarget_Parms, "SFXGame.BioPlayerController.QuickCommandAttackTarget", iSquadMember);
    PARAM(ABioPlayerController_execQuickCommandAttackTarget_Parms, "SFXGame.BioPlayerController.QuickCommandAttackTarget", pTarget);
    PARAM(ABioPlayerController_execQuickCommandAttackTarget_Parms, "SFXGame.BioPlayerController.QuickCommandAttackTarget", ReturnValue);
    PARAM(ABioPlayerController_execQuickCommandFollowPlayer_Parms, "SFXGame.BioPlayerController.QuickCommandFollowPlayer", iSquadMember);
    PARAM(ABioPlayerController_execQuickCommandFollowPlayer_Parms, "SFXGame.BioPlayerController.QuickCommandFollowPlayer", ReturnValue);
    PARAM(ABioBaseSquad_execSetSquadFollowFormation_Parms, "SFXGame.BioBaseSquad.SetSquadFollowFormation", eFormation);
    PARAM(ABioBaseSquad_execSetSquadFollowFormation_Parms, "SFXGame.BioBaseSquad.SetSquadFollowFormation", bForceFormation);
    PARAM(ABioBaseSquad_execSetSquadFollowFormation_Parms, "SFXGame.BioBaseSquad.SetSquadFollowFormation", iSquadMember);
    PARAM(ABioBaseSquad_execSetMemberHoldPosition_Parms, "SFXGame.BioBaseSquad.SetMemberHoldPosition", nIndex);
    PARAM(ABioBaseSquad_execSetMemberHoldPosition_Parms, "SFXGame.BioBaseSquad.SetMemberHoldPosition", vPosition);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", nmPower);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", pPawn);
    PARAM(ABioAiController_execAddUsePower_Parms, "SFXGame.BioAiController.AddUsePower", nmPower);
    PARAM(ABioAiController_execAddUsePower_Parms, "SFXGame.BioAiController.AddUsePower", oTarget);
    PARAM(ABioAiController_execAddUsePower_Parms, "SFXGame.BioAiController.AddUsePower", bOrderedByPlayer);
    PARAM(ABioAiController_execAddUsePower_Parms, "SFXGame.BioAiController.AddUsePower", fTimeOut);
    PARAM(ABioAiController_execAddUsePower_Parms, "SFXGame.BioAiController.AddUsePower", vTarget);
    PARAM(ABioAiController_execPushMoveToPoint_Parms, "SFXGame.BioAiController.PushMoveToPoint", vLoc);
    PARAM(ABioAiController_execPushMoveToPoint_Parms, "SFXGame.BioAiController.PushMoveToPoint", PointTolerance);
    PARAM(ABioAiController_execPushMoveToPoint_Parms, "SFXGame.BioAiController.PushMoveToPoint", bWalking);
    PARAM(ABioAiController_execPushMoveToPoint_Parms, "SFXGame.BioAiController.PushMoveToPoint", bRequiresPrecisionMovement);
    PARAM(ABioAiController_execPushMoveToPoint_Parms, "SFXGame.BioAiController.PushMoveToPoint", ReturnValue);  // game/ConversationWorld
    PARAM(ABioBaseSquad_eventSquadHandleChatterEvent_Parms, "SFXGame.BioBaseSquad.SquadHandleChatterEvent", eSoundSet);
    PARAM(ABioBaseSquad_eventSquadHandleChatterEvent_Parms, "SFXGame.BioBaseSquad.SquadHandleChatterEvent", nFlags);
    PARAM(ABioPawn_execPlaySoundSet_Parms, "SFXGame.BioPawn.PlaySoundSet", eType);
    PARAM(ABioPawn_execPlaySoundSet_Parms, "SFXGame.BioPawn.PlaySoundSet", ReturnValue);
    PARAM(ABioBaseSquad_execGetMemberFormation_Parms, "SFXGame.BioBaseSquad.GetMemberFormation", nIndex);
    PARAM(ABioBaseSquad_execGetMemberFormation_Parms, "SFXGame.BioBaseSquad.GetMemberFormation", ReturnValue);
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
    PARAM(ABioVehicleWheeled_execGetWeaponStartTraceLocation_Parms, "SFXGame.BioVehicleWheeled.GetWeaponStartTraceLocation", CurrentWeapon);
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
    PARAM(ABioPawn_execRequestTurnInPlace_Parms, "SFXGame.BioPawn.RequestTurnInPlace", rDesDir);
    PARAM(ABioPawn_execRequestTurnInPlace_Parms, "SFXGame.BioPawn.RequestTurnInPlace", ReturnValue);
    PARAM(ABioPawn_execSetLookAtTarget_Parms, "SFXGame.BioPawn.SetLookAtTarget", oTarget);
    PARAM(ABioPawn_execSetLookAtTarget_Parms, "SFXGame.BioPawn.SetLookAtTarget", a_eTransition);
    PARAM(ABioPawn_execSetLookAtTarget_Parms, "SFXGame.BioPawn.SetLookAtTarget", nRank);
    PARAM(ABioPawn_execSetDesiredRotation_Parms, "SFXGame.BioPawn.SetDesiredRotation", rDesiredRotation);
    PARAM(ABioPawn_execSetDesiredRotation_Parms, "SFXGame.BioPawn.SetDesiredRotation", bForce);
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
    PARAM(ABioPawn_execIsInCoverLeaning_Parms, "SFXGame.BioPawn.IsInCoverLeaning", ReturnValue);
    // VR cover reads these in the damage call's frame while it asks IsInCover.
    PARAM(UBioPawnBehavior_execProxyTakeDamage_Parms, "SFXGame.BioPawnBehavior.ProxyTakeDamage", InstigatedBy);
    PARAM(UBioPawnBehavior_execProxyTakeDamage_Parms, "SFXGame.BioPawnBehavior.ProxyTakeDamage", HitLocation);
    PARAM(UBioPawnBehavior_execProxyTakeDamage_Parms, "SFXGame.BioPawnBehavior.ProxyTakeDamage", pDamageCausingActor);
    // game/ConversationWorld's walk to a mark: the walker's AI switched on the game's way, and its speed.
    PARAM(UBioActorBehavior_execAbilityEnable_Parms, "SFXGame.BioActorBehavior.AbilityEnable", bEnable);
    PARAM(UBioActorBehavior_execAbilityEnable_Parms, "SFXGame.BioActorBehavior.AbilityEnable", eEnableType);
    PARAM(UBioActorBehavior_execAbilityEnable_Parms, "SFXGame.BioActorBehavior.AbilityEnable", nmSubType);
    PARAM(ABioPawn_execSetDesiredSpeed_Parms, "SFXGame.BioPawn.SetDesiredSpeed", fDesiredSpeed);
    PARAM(ABioPawn_execSetDesiredSpeed_Parms, "SFXGame.BioPawn.SetDesiredSpeed", bForce);
    PARAM(UBioSubtitles_execDisplaySubtitle_Parms, "SFXGame.BioSubtitles.DisplaySubtitle", sSubtitle);
    PARAM(UBioSubtitles_execDisplaySubtitle_Parms, "SFXGame.BioSubtitles.DisplaySubtitle", fDuration);
    PARAM(UBioActorBehavior_execTakeDamage_Parms, "SFXGame.BioActorBehavior.TakeDamage", Damage);
    PARAM(UBioActorBehavior_execTakeDamage_Parms, "SFXGame.BioActorBehavior.TakeDamage", HitLocation);
    PARAM(UBioActorBehavior_execTakeDamage_Parms, "SFXGame.BioActorBehavior.TakeDamage", DamageType);
    PARAM(UBioActorBehavior_execTakeDamage_Parms, "SFXGame.BioActorBehavior.TakeDamage", pDamageCausingActor);
    PARAM(UBioActorBehavior_execTakeDamage_Parms, "SFXGame.BioActorBehavior.TakeDamage", pDamage);
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
    PARMS(ABioWeaponRanged_execGetRecoil_Parms, "SFXGame.BioWeaponRanged.GetRecoil");
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
    PARMS(ABioAiController_execPushMoveToPoint_Parms, "SFXGame.BioAiController.PushMoveToPoint");  // game/ConversationWorld
    PARMS(AActor_execDestroy_Parms, "Engine.Actor.Destroy");
    PARMS(ABioWeaponRanged_eventDoCoolDownEffect_Parms, "SFXGame.BioWeaponRanged.DoCoolDownEffect");
    PARMS(USkeletalMeshComponent_execForceSkelUpdate_Parms, "Engine.SkeletalMeshComponent.ForceSkelUpdate");
    PARMS(ABioBaseSquad_execGetSquadSize_Parms, "SFXGame.BioBaseSquad.GetSquadSize");
    PARMS(ABioBaseSquad_execGetMember_Parms, "SFXGame.BioBaseSquad.GetMember");
    PARMS(ABioBaseSquad_execGetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.GetMemberSwitchWeapon");
    PARMS(ABioBaseSquad_execSetMemberSwitchWeapon_Parms, "SFXGame.BioBaseSquad.SetMemberSwitchWeapon");
    PARMS(ABioPlayerController_execSquadOrderEquipWeapon_Parms, "SFXGame.BioPlayerController.SquadOrderEquipWeapon");
    PARMS(ABioPlayerController_execApplyTacticalOrders_Parms, "SFXGame.BioPlayerController.ApplyTacticalOrders");
    PARMS(ABioPlayerController_execQuickCommandMoveTo_Parms, "SFXGame.BioPlayerController.QuickCommandMoveTo");
    PARMS(ABioPlayerController_execQuickCommandAttackTarget_Parms, "SFXGame.BioPlayerController.QuickCommandAttackTarget");
    PARMS(ABioPlayerController_execQuickCommandFollowPlayer_Parms, "SFXGame.BioPlayerController.QuickCommandFollowPlayer");
    PARMS(ABioBaseSquad_execSetSquadFollowFormation_Parms, "SFXGame.BioBaseSquad.SetSquadFollowFormation");
    PARMS(ABioBaseSquad_execSetMemberHoldPosition_Parms, "SFXGame.BioBaseSquad.SetMemberHoldPosition");
    PARMS(ABioBaseSquad_execGetMemberFormation_Parms, "SFXGame.BioBaseSquad.GetMemberFormation");
    PARMS(UBioEquipment_execChangeToQuickSlot_Parms, "SFXGame.BioEquipment.ChangeToQuickSlot");
    PARMS(UBioPawnBehavior_execGetClass_Parms, "SFXGame.BioPawnBehavior.GetClass");
    PARMS(AWorldInfo_execPauseGame_Parms, "Engine.WorldInfo.PauseGame");
    PARMS(USFXGameModeDefault_execTryHolster_Parms, "SFXGame.SFXGameModeDefault.TryHolster");
    PARMS(USFXGameModeVehicle_execRepairVehicle_Parms, "SFXGame.SFXGameModeVehicle.RepairVehicle");  // game/CockpitPanel
    PARMS(USFXGameModeVehicle_execLeaveWorld_Parms, "SFXGame.SFXGameModeVehicle.LeaveWorld");
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
    PARMS(ABioPawn_execRequestTurnInPlace_Parms, "SFXGame.BioPawn.RequestTurnInPlace");
    PARMS(ABioPawn_execSetLookAtTarget_Parms, "SFXGame.BioPawn.SetLookAtTarget");
    PARMS(ABioPawn_execSetDesiredRotation_Parms, "SFXGame.BioPawn.SetDesiredRotation");
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
    PARMS(ABioPawn_execIsInCoverLeaning_Parms, "SFXGame.BioPawn.IsInCoverLeaning");
    PARMS(USFXGameModeDefault_execTryExitCover_Parms, "SFXGame.SFXGameModeDefault.TryExitCover");
    // Stage 2: the squad wheel's calls (a 64-byte zeroed block before; the function's own block now).
    PARMS(USFXGameModeDefault_execEnterWeaponWheel_Parms, "SFXGame.SFXGameModeDefault.EnterWeaponWheel");
    PARMS(USFXGameModeWeaponWheel_execExitWeaponWheel_Parms, "SFXGame.SFXGameModeWeaponWheel.ExitWeaponWheel");
}

#elif defined(MEVR_GAME_LE2)  // M1: what LE2 reads through our layer today

// The classes behind the reads engine/Legacy made until M1 (the player controller, its pawn, the game mode, the camera, the
// world), the console, the reflection fix's capture class, and the equipped weapon of the LE2 observers (lane C's first
// reads, research/le2-engine-access-discovery.md "P1a").
void CheckClasses() {
    CheckClass<UObject>("Core.Object");
    CheckClass<UFunction>("Core.Function");
    CheckClass<UClass>("Core.Class");
    CheckClass<UProperty>("Core.Property");
    CheckClass<UBoolProperty>("Core.BoolProperty");
    CheckClass<UStructProperty>("Core.StructProperty");
    CheckClass<UObjectProperty>("Core.ObjectProperty");
    CheckClass<UArrayProperty>("Core.ArrayProperty");
    CheckClass<AActor>("Engine.Actor");
    CheckClass<APawn>("Engine.Pawn");
    CheckClass<AController>("Engine.Controller");
    CheckClass<APlayerController>("Engine.PlayerController");
    CheckClass<ACamera>("Engine.Camera");
    CheckClass<UEngine>("Engine.Engine");
    CheckClass<UGameEngine>("Engine.GameEngine");
    CheckClass<UPlayer>("Engine.Player");
    CheckClass<ULocalPlayer>("Engine.LocalPlayer");
    CheckClass<ASFXWeapon_NativeBase>("SFXGame.SFXWeapon_NativeBase");  // game/Weapon, game/Sights: the zoom flag
    CheckClass<AWorldInfo>("Engine.WorldInfo");
    CheckClass<UWorld>("Engine.World");
    CheckClass<UActorComponent>("Engine.ActorComponent");
    CheckClass<USceneCaptureComponent>("Engine.SceneCaptureComponent");
    CheckClass<USceneCaptureReflectComponent>("Engine.SceneCaptureReflectComponent");
    CheckClass<ABioPlayerController>("SFXGame.BioPlayerController");
    CheckClass<USFXGameModeManager>("SFXGame.SFXGameModeManager");
    CheckClass<USFXGameModeBase>("SFXGame.SFXGameModeBase");
    CheckClass<USFXGameModeDefault>("SFXGame.SFXGameModeDefault");
    CheckClass<USFXGameModeVehicle>("SFXGame.SFXGameModeVehicle");          // game/GameMenuRequest (M4): the modes by class
    CheckClass<USFXGameModePowerWheel>("SFXGame.SFXGameModePowerWheel");
    CheckClass<USFXGameModeWeaponWheel>("SFXGame.SFXGameModeWeaponWheel");
    CheckClass<USFXGameModeCommand>("SFXGame.SFXGameModeCommand");
    CheckClass<ABioPawn>("SFXGame.BioPawn");
    CheckClass<ASFXPawn_Player>("SFXGame.SFXPawn_Player");
    CheckClass<ABioWorldInfo>("SFXGame.BioWorldInfo");
    CheckClass<ASFXWeapon>("SFXGame.SFXWeapon");
    CheckClass<USFXEngine>("SFXGame.SFXEngine");                  // a save loaded by script (mevr load, M1.6)
    CheckClass<USFXSFHandler_Load>("SFXGame.SFXSFHandler_Load");  // ... its descriptor, as the Load screen lists it
    // M3 (research/le2-vrik.md): the skeleton, the camera's hook, arm IK
    CheckClass<USkeletalMeshComponent>("Engine.SkeletalMeshComponent");
    CheckClass<USkeletalMesh>("Engine.SkeletalMesh");
    CheckClass<USkeletalMeshSocket>("Engine.SkeletalMeshSocket");
    CheckClass<UAnimTree>("Engine.AnimTree");
    CheckClass<USkelControlBase>("Engine.SkelControlBase");
    CheckClass<USkelControlLimb>("Engine.SkelControlLimb");
    CheckClass<ASFXCameraNativeBase>("SFXGame.SFXCameraNativeBase");
    CheckClass<USFXCameraMode>("SFXGame.SFXCameraMode");
    CheckClass<USFXCameraMode_Interpolate>("SFXGame.SFXCameraMode_Interpolate");
    CheckClass<ASFXInventoryManager>("SFXGame.SFXInventoryManager");  // game/ViewLevel: the shots' recoil
    CheckClass<USFXModule_AimAssist>("SFXGame.SFXModule_AimAssist");  // ... and aim assist's friction and adhesion
    CheckClass<ASFXPlayerCamera>("SFXGame.SFXPlayerCamera");          // game/CameraShake: the camera's shake modifiers
    CheckClass<UCameraModifier>("Engine.CameraModifier");
    CheckClass<AWeapon>("Engine.Weapon");  // game/Weapon (M3.3): ME2's guns are SFXWeapon on Engine.Weapon
    CheckClass<UCylinderComponent>("Engine.CylinderComponent");  // game/Player's floor-lock sample (M2)
    // game/ScreenReticle (M2): the crosshair's movie hidden while the hand reticle stands in for it
    CheckClass<UBioSFManager>("Engine.BioSFManager");
    CheckClass<UBioSFPanel>("Engine.BioSFPanel");
    CheckClass<UBioSFHandler>("Engine.BioSFHandler");
    CheckClass<USFXSFHandler_Reticle>("SFXGame.SFXSFHandler_Reticle");
    // game/OmniTool (M4.3, research/le2-menus.md "ME2's omni-tool"): the tech powers' omni-tool on Shepard's arm, and its
    // read-only discovery of the effect's brightness parameters (as LE1's rows)
    CheckClass<UBioVFXTemplate>("SFXGame.BioVFXTemplate");
    CheckClass<ABioVisualEffect>("SFXGame.BioVisualEffect");
    CheckClass<AEmitter>("Engine.Emitter");
    CheckClass<UParticleSystemComponent>("Engine.ParticleSystemComponent");
    CheckClass<UParticleSystem>("Engine.ParticleSystem");
    CheckClass<UParticleEmitter>("Engine.ParticleEmitter");
    CheckClass<UParticleLODLevel>("Engine.ParticleLODLevel");
    CheckClass<UParticleModuleRequired>("Engine.ParticleModuleRequired");
    CheckClass<UParticleModuleTypeDataMesh>("Engine.ParticleModuleTypeDataMesh");
    CheckClass<UParticleModuleMeshMaterial>("Engine.ParticleModuleMeshMaterial");
    CheckClass<UParticleModuleMaterialByParameter>("Engine.ParticleModuleMaterialByParameter");
    CheckClass<UDistributionFloatParameterBase>("Engine.DistributionFloatParameterBase");
    CheckClass<UDistributionVectorParameterBase>("Engine.DistributionVectorParameterBase");
    CheckClass<UDistributionFloatParticleParameter>("Engine.DistributionFloatParticleParameter");
    CheckClass<UDistributionVectorParticleParameter>("Engine.DistributionVectorParticleParameter");
    CheckClass<UDistributionFloatConstant>("Engine.DistributionFloatConstant");
    CheckClass<UDistributionVectorConstant>("Engine.DistributionVectorConstant");
    CheckClass<UMeshComponent>("Engine.MeshComponent");
    CheckClass<UStaticMeshComponent>("Engine.StaticMeshComponent");
    CheckClass<UStaticMesh>("Engine.StaticMesh");
    CheckClass<UMaterialInstance>("Engine.MaterialInstance");
    CheckClass<UMaterialInstanceConstant>("Engine.MaterialInstanceConstant");
    // game/Posture and game/VrCover (research/23 section 7): ME1's cover system on ME2
    CheckClass<ACoverLink>("Engine.CoverLink");
    CheckClass<ASFXGame>("SFXGame.SFXGame");
    CheckClass<USFXDamageType>("SFXGame.SFXDamageType");
    CheckClass<USFXModule_Damage>("SFXGame.SFXModule_Damage");
    CheckClass<USFXModule_DamagePlayer>("SFXGame.SFXModule_DamagePlayer");
    CheckClass<ASFXShield_Base>("SFXGame.SFXShield_Base");
    CheckClass<AInventoryManager>("Engine.InventoryManager");
    CheckClass<AInventory>("Engine.Inventory");
    CheckClass<UMassEffectGuiManager>("SFXGame.MassEffectGuiManager");  // the HUD's cover prompt
}

void CheckFields() {
    // game/Sights, game/Weapon (M3.3, research/le2-vrik.md "The sights on the sequels"): the near plane for the scope's pull,
    // the zoom flag set for one aim-error call, the sniper's zoom
    CheckField("Engine.PlayerController", "Player", SDK_FIELD(APlayerController, Player));
    CheckField("Engine.LocalPlayer", "NearClipPlane", SDK_FIELD(ULocalPlayer, NearClipPlane));
    CheckField("SFXGame.SFXWeapon_NativeBase", "bIsZoomed", SDK_BOOL(ASFXWeapon_NativeBase, bIsZoomed));
    // game/Holsters' sequel adapter (research/le2-vrik.md "Holsters and the reload on the sequels"): drawn is the action
    // state, and the draw raises it through the pawn's behavior
    CheckField("SFXGame.BioPawn", "m_oBehavior", SDK_FIELD(ABioPawn, m_oBehavior));
    CheckField("SFXGame.BioPawn", "ActionState", SDK_FIELD(ABioPawn, ActionState));
    CheckField("SFXGame.SFXWeapon", "ZoomFOV", SDK_FIELD(ASFXWeapon, ZoomFOV));
    // ue3::runtime (the local player controller) and engine::World()'s path
    CheckField("Engine.Engine", "GamePlayers", SDK_FIELD(UEngine, GamePlayers));
    CheckField("Engine.Player", "Actor", SDK_FIELD(UPlayer, Actor));
    CheckField("Engine.Actor", "WorldInfo", SDK_FIELD(AActor, WorldInfo));
    // game/Player's reads on every game (head tracking, hand aim's aim query)
    CheckField("Engine.Controller", "Pawn", SDK_FIELD(AController, Pawn));
    CheckField("Engine.Actor", "Location", SDK_FIELD(AActor, Location));
    CheckField("Engine.Actor", "Rotation", SDK_FIELD(AActor, Rotation));
    CheckField("Engine.PlayerController", "PlayerCamera", SDK_FIELD(APlayerController, PlayerCamera));
    CheckField("Engine.Camera", "PCOwner", SDK_FIELD(ACamera, PCOwner));
    CheckField("Engine.Camera", "CameraCache", SDK_FIELD(ACamera, CameraCache));
    CheckField("SFXGame.BioPlayerController", "GameModeManager2", SDK_FIELD(ABioPlayerController, GameModeManager2));
    CheckField("SFXGame.SFXGameModeManager", "CurrentMode", SDK_FIELD(USFXGameModeManager, CurrentMode));
    CheckField("SFXGame.SFXGameModeManager", "GameModes", SDK_FIELD(USFXGameModeManager, GameModes));
    // M4 (research/le2-menus.md): the main menu's pause (game/MenuPause: ME2's weapon wheel pauses with bPlayersOnly) and its
    // Game menu and Holster buttons (game/GameMenuRequest: the mode that has the input)
    CheckField("Engine.WorldInfo", "bPlayersOnly", SDK_BOOL(AWorldInfo, bPlayersOnly));
    CheckField("SFXGame.SFXGameModeBase", "bIsActive", SDK_BOOL(USFXGameModeBase, bIsActive));
    CheckField("SFXGame.SFXGameModeBase", "Priority", SDK_FIELD(USFXGameModeBase, Priority));
    // ... and the omni-tool's entries: the pages of ME2's pause menu (its browser wheel)
    CheckField("Engine.PlayerController", "m_oCachedScaleFormManager", SDK_FIELD(APlayerController, m_oCachedScaleFormManager));
    CheckField("SFXGame.MassEffectGuiManager", "m_oBWHandler", SDK_FIELD(UMassEffectGuiManager, m_oBWHandler));
    CheckField("SFXGame.BioSFHandler_BrowserWheel", "lstPages", SDK_FIELD(UBioSFHandler_BrowserWheel, lstPages));
    CheckPath("SFXGame.BioSFHandler_BrowserWheel", {"lstPages", "Tag"}, SDK_AT(offsetof(FBWPageStruct, Tag), 8));
    CheckPath("SFXGame.BioSFHandler_BrowserWheel", {"lstPages", "Type"}, SDK_AT(offsetof(FBWPageStruct, Type), 1));
    // the reflection fix (game/Reflections, our renderer only)
    CheckField("Engine.SceneCaptureComponent", "FrameRate", SDK_FIELD(USceneCaptureComponent, FrameRate));
    // the equipped weapon and its ammunition (research/le2-engine-access-discovery.md "P1a": lane C's first reads)
    CheckField("Engine.Pawn", "Weapon", SDK_FIELD(APawn, Weapon));
    CheckField("SFXGame.SFXWeapon", "AmmoUsedCount", SDK_FIELD(ASFXWeapon, AmmoUsedCount));
    CheckField("SFXGame.SFXWeapon", "CurrentSpareAmmo", SDK_FIELD(ASFXWeapon, CurrentSpareAmmo));
    CheckField("SFXGame.SFXWeapon", "MagSize", SDK_FIELD(ASFXWeapon, MagSize));
    CheckField("SFXGame.SFXWeapon", "bEarlyReload", SDK_BOOL(ASFXWeapon, bEarlyReload));
    // the reload by hand (game/Reload): a gun that reloads at all, and a finite reserve
    CheckField("SFXGame.SFXWeapon", "bInfiniteAmmo", SDK_BOOL(ASFXWeapon, bInfiniteAmmo));
    CheckField("SFXGame.SFXWeapon", "bWeaponCanBeReloaded", SDK_BOOL(ASFXWeapon, bWeaponCanBeReloaded));
    // M3 (research/le2-vrik.md): the body (game/Player, engine/skeleton, engine/BonePose, the skeleton and camera probes)
    CheckField("Engine.Pawn", "Mesh", SDK_FIELD(APawn, Mesh));
    CheckField("Engine.Actor", "Velocity", SDK_FIELD(AActor, Velocity));
    CheckField("Engine.ActorComponent", "Owner", SDK_FIELD(UActorComponent, Owner));
    CheckField("Engine.PrimitiveComponent", "LocalToWorld", SDK_FIELD(UPrimitiveComponent, LocalToWorld), true);
    CheckField("Engine.SkeletalMeshComponent", "SkeletalMesh", SDK_FIELD(USkeletalMeshComponent, SkeletalMesh));
    CheckField("Engine.SkeletalMeshComponent", "AnimTreeTemplate", SDK_FIELD(USkeletalMeshComponent, AnimTreeTemplate));
    CheckField("Engine.SkeletalMeshComponent", "Animations", SDK_FIELD(USkeletalMeshComponent, Animations));
    CheckField("Engine.SkeletalMeshComponent", "SpaceBases", SDK_FIELD(USkeletalMeshComponent, SpaceBases), true);
    CheckField("Engine.SkeletalMeshComponent", "ParentAnimComponent", SDK_FIELD(USkeletalMeshComponent, ParentAnimComponent));
    CheckField("Engine.SkeletalMeshComponent", "Attachments", SDK_FIELD(USkeletalMeshComponent, Attachments), true);
    CheckField("Engine.SkeletalMesh", "RefSkeleton", SDK_FIELD(USkeletalMesh, RefSkeleton), true);
    CheckField("Engine.SkeletalMesh", "Sockets", SDK_FIELD(USkeletalMesh, Sockets));
    CheckField("Engine.SkeletalMeshSocket", "SocketName", SDK_FIELD(USkeletalMeshSocket, SocketName));
    CheckField("Engine.SkeletalMeshSocket", "BoneName", SDK_FIELD(USkeletalMeshSocket, BoneName));
    CheckField("Engine.SkeletalMeshSocket", "RelativeLocation", SDK_FIELD(USkeletalMeshSocket, RelativeLocation));
    CheckField("Engine.SkeletalMeshSocket", "RelativeRotation", SDK_FIELD(USkeletalMeshSocket, RelativeRotation));
    CheckField("Engine.AnimTree", "SkelControlLists", SDK_FIELD(UAnimTree, SkelControlLists));
    CheckField("Engine.SkelControlBase", "NextControl", SDK_FIELD(USkelControlBase, NextControl));
    CheckField("Engine.SkelControlBase", "ControlName", SDK_FIELD(USkelControlBase, ControlName));
    CheckField("Engine.SkelControlBase", "ControlStrength", SDK_FIELD(USkelControlBase, ControlStrength));
    CheckField("Engine.SkelControlLimb", "EffectorLocationSpace", SDK_FIELD(USkelControlLimb, EffectorLocationSpace));
    CheckField("Engine.SkelControlLimb", "EffectorSpaceBoneName", SDK_FIELD(USkelControlLimb, EffectorSpaceBoneName));
    CheckField("Engine.SkelControlLimb", "JointTargetLocationSpace", SDK_FIELD(USkelControlLimb, JointTargetLocationSpace));
    CheckField("Engine.SkelControlLimb", "JointTargetSpaceBoneName", SDK_FIELD(USkelControlLimb, JointTargetSpaceBoneName));
    CheckField("Engine.SkelControlLimb", "BoneAxis", SDK_FIELD(USkelControlLimb, BoneAxis));
    CheckField("Engine.SkelControlLimb", "JointAxis", SDK_FIELD(USkelControlLimb, JointAxis));
    CheckField("Engine.SkelControlLimb", "bInvertJointAxis", SDK_BOOL(USkelControlLimb, bInvertJointAxis));
    CheckField("SFXGame.SFXCameraNativeBase", "CurrentCameraMode", SDK_FIELD(ASFXCameraNativeBase, CurrentCameraMode));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "To", SDK_FIELD(USFXCameraMode_Interpolate, To));
    CheckField("SFXGame.SFXCameraMode", "m_pov", SDK_FIELD(USFXCameraMode, m_pov));
    CheckField("SFXGame.SFXCameraMode", "Offset", SDK_FIELD(USFXCameraMode, Offset));
    CheckField("SFXGame.SFXCameraMode", "HookOffset", SDK_FIELD(USFXCameraMode, HookOffset));
    CheckField("SFXGame.SFXCameraMode", "HookName", SDK_FIELD(USFXCameraMode, HookName));
    CheckField("SFXGame.SFXCameraMode", "bCollisionEnabled", SDK_BOOL(USFXCameraMode, bCollisionEnabled));  // game/CameraProbe's trace
    // game/ViewLevel (M3.1: no camera recoil on ME2 either, research/le2-vrik-audit.md)
    CheckField("Engine.Actor", "Physics", SDK_FIELD(AActor, Physics));
    CheckField("Engine.Actor", "Modules", SDK_FIELD(AActor, Modules));
    CheckField("Engine.Pawn", "InvManager", SDK_FIELD(APawn, InvManager));
    CheckField("Engine.PlayerController", "PlayerInput", SDK_FIELD(APlayerController, PlayerInput));
    CheckField("Engine.PlayerInput", "RawJoyLookRight", SDK_FIELD(UPlayerInput, RawJoyLookRight));
    CheckField("Engine.PlayerInput", "RawJoyLookUp", SDK_FIELD(UPlayerInput, RawJoyLookUp));
    CheckField("SFXGame.BioPawn", "CoverType", SDK_FIELD(ABioPawn, CoverType));
    CheckField("SFXGame.BioPawn", "CoverAction", SDK_FIELD(ABioPawn, CoverAction));
    CheckField("SFXGame.SFXInventoryManager", "WeaponRecoilOffset", SDK_FIELD(ASFXInventoryManager, WeaponRecoilOffset));
    CheckField("SFXGame.SFXInventoryManager", "TotalRecoil", SDK_FIELD(ASFXInventoryManager, TotalRecoil));
    CheckField("SFXGame.SFXModule_AimAssist", "LastZoomSnapTarget", SDK_FIELD(USFXModule_AimAssist, LastZoomSnapTarget));
    // game/CameraShake (M3.1): the body's camera shakes, the screen shakes' two camera modifiers
    CheckField("SFXGame.SFXCameraMode", "bIsCameraShakeEnabled", SDK_BOOL(USFXCameraMode, bIsCameraShakeEnabled));
    CheckField("SFXGame.SFXPlayerCamera", "ScreenShakeModifier", SDK_FIELD(ASFXPlayerCamera, ScreenShakeModifier));
    CheckField("Engine.Camera", "CameraShakeCamMod", SDK_FIELD(ACamera, CameraShakeCamMod));
    CheckField("Engine.CameraModifier", "bDisabled", SDK_BOOL(UCameraModifier, bDisabled));
    // game/ArmIk (M3.2): the body's bounds, the AnimTree's own off-hand IK
    CheckField("Engine.Actor", "AllComponents", SDK_FIELD(AActor, AllComponents));
    CheckField("Engine.SkeletalMeshComponent", "bConsiderAllBodiesForBounds", SDK_BOOL(USkeletalMeshComponent, bConsiderAllBodiesForBounds));
    CheckField("Engine.SkelControlBase", "StrengthTarget", SDK_FIELD(USkelControlBase, StrengthTarget));
    CheckField("Engine.SkelControlBase", "bSetStrengthFromAnimNode", SDK_BOOL(USkelControlBase, bSetStrengthFromAnimNode));
    // game/Weapon (M3.3): the gun's model and its muzzle socket
    CheckField("Engine.Weapon", "Mesh", SDK_FIELD(AWeapon, Mesh));
    CheckField("SFXGame.SFXWeapon", "MuzzleSocketName", SDK_FIELD(ASFXWeapon, MuzzleSocketName));
    CheckField("Engine.SkeletalMesh", "Bounds", SDK_FIELD(USkeletalMesh, Bounds));
    // game/Player's floor-lock sample (M2): the floor under the pawn
    CheckField("Engine.Pawn", "CylinderComponent", SDK_FIELD(APawn, CylinderComponent));
    CheckField("Engine.CylinderComponent", "CollisionHeight", SDK_FIELD(UCylinderComponent, CollisionHeight));
    // game/ScreenReticle (M2): the GUI manager's panels, each panel's handlers and movie, ME2's crosshair handler
    CheckField("Engine.BioSFManager", "m_lstPanels", SDK_FIELD(UBioSFManager, m_lstPanels));
    CheckField("Engine.BioSFPanel", "ScriptHandlers", SDK_FIELD(UBioSFPanel, ScriptHandlers));
    CheckField("Engine.BioSFPanel", "MovieRef", SDK_FIELD(UBioSFPanel, MovieRef));
    CheckField("Engine.BioSFPanel", "IsVisible", SDK_BOOL(UBioSFPanel, IsVisible));
    CheckField("Engine.BioSFHandler", "oPanel", SDK_FIELD(UBioSFHandler, oPanel));
    CheckField("SFXGame.SFXSFHandler_Reticle", "m_eCurrentCrosshair", SDK_FIELD(USFXSFHandler_Reticle, m_eCurrentCrosshair));
    CheckField("SFXGame.SFXSFHandler_Reticle", "m_bShowSniperDetails", SDK_BOOL(USFXSFHandler_Reticle, m_bShowSniperDetails));
    // game/OmniTool (M4.3): the effect's emitters onto Shepard's mesh, the display's place, the brightness discovery. ME2's
    // StaticMesh has no LODInfo property (tools/le2-name-audit.py): that handle stands down there, so no row. No rows either
    // for ParticleSystemComponent's Template, SMComponents, SMMaterialInterfaces, InstanceParameters (and its members) or
    // ParticleSystem's Emitters: LExSDKv2's LE2 headers put them 8 bytes early (InstanceParameters' element 48 bytes against
    // the engine's 64), and our layer's offsets, the engine's own reflection, are the right ones: they read the omni-tool's
    // particle systems (OmniArm_2sec, OmniHand_2sec) and their emitters' modules in the game (2026-11-14 23:46,
    // research/le2-menus.md "ME2's omni-tool").
    CheckField("Engine.Actor", "Base", SDK_FIELD(AActor, Base));
    CheckField("Engine.Actor", "Owner", SDK_FIELD(AActor, Owner));
    CheckField("Engine.Actor", "BaseBoneName", SDK_FIELD(AActor, BaseBoneName));
    CheckField("Engine.Actor", "RelativeLocation", SDK_FIELD(AActor, RelativeLocation));
    CheckField("Engine.Actor", "RelativeRotation", SDK_FIELD(AActor, RelativeRotation));
    CheckField("Engine.Emitter", "ParticleSystemComponent", SDK_FIELD(AEmitter, ParticleSystemComponent));
    CheckField("Engine.ParticleModuleMeshMaterial", "MeshMaterials", SDK_FIELD(UParticleModuleMeshMaterial, MeshMaterials));
    CheckField("Engine.ParticleModuleMaterialByParameter", "DefaultMaterials", SDK_FIELD(UParticleModuleMaterialByParameter, DefaultMaterials));
    CheckField("Engine.ParticleEmitter", "LODLevels", SDK_FIELD(UParticleEmitter, LODLevels));
    CheckField("Engine.ParticleLODLevel", "Modules", SDK_FIELD(UParticleLODLevel, Modules));
    CheckField("Engine.ParticleLODLevel", "RequiredModule", SDK_FIELD(UParticleLODLevel, RequiredModule));
    CheckField("Engine.ParticleLODLevel", "SpawnModule", SDK_FIELD(UParticleLODLevel, SpawnModule));
    CheckField("Engine.ParticleLODLevel", "TypeDataModule", SDK_FIELD(UParticleLODLevel, TypeDataModule));
    CheckField("Engine.ParticleModuleRequired", "Material", SDK_FIELD(UParticleModuleRequired, Material));
    CheckField("Engine.ParticleModuleTypeDataMesh", "Mesh", SDK_FIELD(UParticleModuleTypeDataMesh, Mesh));
    CheckField("Engine.DistributionFloatParameterBase", "ParameterName", SDK_FIELD(UDistributionFloatParameterBase, ParameterName));
    CheckField("Engine.DistributionVectorParameterBase", "ParameterName", SDK_FIELD(UDistributionVectorParameterBase, ParameterName));
    CheckField("Engine.DistributionFloatConstant", "Constant", SDK_FIELD(UDistributionFloatConstant, Constant));
    CheckField("Engine.DistributionVectorConstant", "Constant", SDK_FIELD(UDistributionVectorConstant, Constant));
    CheckField("Engine.DistributionFloatParameterBase", "ParamMode", SDK_FIELD(UDistributionFloatParameterBase, ParamMode));
    CheckField("Engine.DistributionVectorParameterBase", "ParamModes", SDK_FIELD(UDistributionVectorParameterBase, ParamModes));
    CheckField("Engine.MeshComponent", "Materials", SDK_FIELD(UMeshComponent, Materials));
    CheckField("Engine.StaticMeshComponent", "StaticMesh", SDK_FIELD(UStaticMeshComponent, StaticMesh));
    CheckField("Engine.MaterialInstance", "Parent", SDK_FIELD(UMaterialInstance, Parent));
    CheckField("Engine.MaterialInstanceConstant", "ScalarParameterValues", SDK_FIELD(UMaterialInstanceConstant, ScalarParameterValues));
    CheckField("Engine.MaterialInstanceConstant", "VectorParameterValues", SDK_FIELD(UMaterialInstanceConstant, VectorParameterValues));
    CheckField("SFXGame.BioVisualEffect", "m_CachedInstanceParameters", SDK_FIELD(ABioVisualEffect, m_CachedInstanceParameters));
    // game/GameMenuRequest (M4.3): the rail's back-out of ME2's screens, B through GuiKey; the focused panel for its log
    CheckField("Engine.BioSFManager", "m_FocusPanel", SDK_FIELD(UBioSFManager, m_FocusPanel));
    CheckField("Engine.BioSFPanel", "nmTag", SDK_FIELD(UBioSFPanel, nmTag));
    CheckField("Engine.BioSFPanel", "m_DefaultHandler", SDK_FIELD(UBioSFPanel, m_DefaultHandler));
    // game/WorldUi (M4.3, research/le2-menus.md "ME2's screens on the wrist"): its panels' full-screen flag (the rest: above, and
    // Actor.WorldInfo, the player selection and Camera.UpdateCamera in the checks of traversal and the script packages)
    CheckField("Engine.BioSFPanel", "bFullScreen", SDK_BOOL(UBioSFPanel, bFullScreen));
    // game/Posture (research/23 section 7): the physical crouch, the cover state, the sprint
    CheckField("Engine.Pawn", "bIsCrouched", SDK_BOOL(APawn, bIsCrouched));
    CheckField("Engine.Pawn", "bWantsToCrouch", SDK_BOOL(APawn, bWantsToCrouch));
    CheckField("SFXGame.BioPlayerController", "bStorming", SDK_BOOL(ABioPlayerController, bStorming));
    CheckField("SFXGame.BioPlayerController", "m_bEnableStormToCover", SDK_BOOL(ABioPlayerController, m_bEnableStormToCover));
    CheckField("SFXGame.BioPawn", "CurrentLink", SDK_FIELD(ABioPawn, CurrentLink));
    CheckField("SFXGame.BioPawn", "CurrentSlotIdx", SDK_FIELD(ABioPawn, CurrentSlotIdx));
    // game/VrCover: the shooter, the pawns, the damage type's defaults, Shepard's health and shields (`mevr cover hit`)
    CheckField("Engine.Pawn", "EyeHeight", SDK_FIELD(APawn, EyeHeight));
    CheckField("Engine.Pawn", "NextPawn", SDK_FIELD(APawn, NextPawn));
    CheckField("Engine.Pawn", "Controller", SDK_FIELD(APawn, Controller));
    CheckField("Engine.WorldInfo", "PawnList", SDK_FIELD(AWorldInfo, PawnList));
    CheckField("Engine.Actor", "bHidden", SDK_BOOL(AActor, bHidden));
    CheckField("SFXGame.SFXDamageType", "bIgnoresCoverDirection", SDK_BOOL(USFXDamageType, bIgnoresCoverDirection));
    CheckField("SFXGame.SFXDamageType", "bDamagesFriends", SDK_BOOL(USFXDamageType, bDamagesFriends));
    CheckField("SFXGame.SFXDamageType", "bHealthDamage", SDK_BOOL(USFXDamageType, bHealthDamage));
    CheckField("SFXGame.SFXModule_Damage", "CurrentHealth", SDK_FIELD(USFXModule_Damage, CurrentHealth));
    CheckField("SFXGame.SFXShield_Base", "CurrentShields", SDK_FIELD(ASFXShield_Base, CurrentShields));
    CheckField("Engine.InventoryManager", "InventoryChain", SDK_FIELD(AInventoryManager, InventoryChain));
    CheckField("Engine.Inventory", "Inventory", SDK_FIELD(AInventory, Inventory));
    // game/VrCover: the shields' faster recharge behind cover (research/23 section 7.5)
    CheckField("SFXGame.SFXShield_Base", "ShieldRegenTimer", SDK_FIELD(ASFXShield_Base, ShieldRegenTimer));
    CheckField("SFXGame.SFXShield_Base", "bFastCoverRegen", SDK_BOOL(ASFXShield_Base, bFastCoverRegen));
    // game/Sprint: one sprint for the trilogy (the faster run; StormOn's intent withdrawn)
    CheckField("SFXGame.BioPlayerController", "bWantsToStorm", SDK_FIELD(ABioPlayerController, bWantsToStorm));
    CheckField("Engine.Pawn", "GroundSpeed", SDK_FIELD(APawn, GroundSpeed));
    CheckField("SFXGame.BioPawn", "CombatGroundSpeed", SDK_FIELD(ABioPawn, CombatGroundSpeed));
    CheckField("SFXGame.BioPawn", "StormSpeed", SDK_FIELD(ABioPawn, StormSpeed));
}

void CheckPaths() {
    // game/OmniTool (M4.3): the struct members it reads (InstanceParameters' have no rows: CheckFields above says why)
    CheckElement("SFXGame.BioVisualEffect", "m_CachedInstanceParameters", sizeof(FCachedInstanceParameter));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "ParameterName"}, SDK_AT(offsetof(FCachedInstanceParameter, ParameterName), 8));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "fCurrentValue"}, SDK_AT(offsetof(FCachedInstanceParameter, fCurrentValue), 4));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "OffsetInfo"}, SDK_AT(offsetof(FCachedInstanceParameter, OffsetInfo), 16));
    CheckPath("SFXGame.BioVisualEffect", {"m_CachedInstanceParameters", "bInited"}, SDK_BOOL(FCachedInstanceParameter, bInited));
    CheckPath("Engine.Camera", {"CameraCache", "POV"}, SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV), sizeof(FTPOV)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "Location"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, Location), sizeof(FVector)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "Rotation"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, Rotation), sizeof(FRotator)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "FOV"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, FOV), sizeof(float)));
    CheckPath("Engine.Actor", {"Location", "X"}, SDK_AT(offsetof(AActor, Location) + offsetof(FVector, X), 4));
    CheckPath("Engine.Actor", {"Rotation", "Yaw"}, SDK_AT(offsetof(AActor, Rotation) + offsetof(FRotator, Yaw), 4));
    CheckPath("SFXGame.SFXWeapon", {"MagSize", "Value"}, SDK_AT(offsetof(ASFXWeapon, MagSize) + offsetof(FScaledFloat, Value), 4));
    CheckElement("Engine.Engine", "GamePlayers", static_cast<long long>(sizeof(ULocalPlayer*)));
    // A save's descriptor (mevr load, M1.6), as the Load screen's list holds it.
    CheckElement("SFXGame.SFXSFHandler_Load", "SaveList", static_cast<long long>(sizeof(FSaveGUIRecord)));
    CheckPath("SFXGame.SFXSFHandler_Load", {"SaveList", "SaveDescriptor", "Career"},
        SDK_AT(offsetof(FSaveGUIRecord, SaveDescriptor) + offsetof(FSFXSaveDescriptor, Career), sizeof(FString)));
    CheckPath("SFXGame.SFXSFHandler_Load", {"SaveList", "SaveDescriptor", "SaveName"},
        SDK_AT(offsetof(FSaveGUIRecord, SaveDescriptor) + offsetof(FSFXSaveDescriptor, SaveName), sizeof(FString)));
    CheckPath("SFXGame.SFXSFHandler_Load", {"SaveList", "SaveDescriptor", "Index"},
        SDK_AT(offsetof(FSaveGUIRecord, SaveDescriptor) + offsetof(FSFXSaveDescriptor, Index), 4));
    CheckPath("SFXGame.SFXSFHandler_Load", {"SaveList", "SaveDescriptor", "Type"},
        SDK_AT(offsetof(FSaveGUIRecord, SaveDescriptor) + offsetof(FSFXSaveDescriptor, Type), 1));
    // M3: the camera mode's own view, the skeletal control lists, the attachments (engine/skeleton)
    CheckPath("SFXGame.SFXCameraMode", {"m_pov", "Location"}, SDK_AT(offsetof(USFXCameraMode, m_pov) + offsetof(FTPOV, Location), sizeof(FVector)));
    CheckPath("SFXGame.SFXCameraMode", {"m_pov", "Rotation"}, SDK_AT(offsetof(USFXCameraMode, m_pov) + offsetof(FTPOV, Rotation), sizeof(FRotator)));
    CheckElement("Engine.AnimTree", "SkelControlLists", sizeof(FSkelControlListHead));
    CheckPath("Engine.AnimTree", {"SkelControlLists", "BoneName"}, SDK_AT(offsetof(FSkelControlListHead, BoneName), 8));
    CheckPath("Engine.AnimTree", {"SkelControlLists", "ControlHead"}, SDK_AT(offsetof(FSkelControlListHead, ControlHead), 8));
    CheckElement("Engine.SkeletalMeshComponent", "Attachments", sizeof(FAttachment));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "Component"}, SDK_AT(offsetof(FAttachment, Component), 8));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "BoneName"}, SDK_AT(offsetof(FAttachment, BoneName), 8));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "RelativeLocation"}, SDK_AT(offsetof(FAttachment, RelativeLocation), sizeof(FVector)));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "RelativeRotation"}, SDK_AT(offsetof(FAttachment, RelativeRotation), sizeof(FRotator)));
    CheckPath("Engine.SkeletalMeshComponent", {"Attachments", "RelativeScale"}, SDK_AT(offsetof(FAttachment, RelativeScale), sizeof(FVector)));
    CheckPath("Engine.SkeletalMesh", {"Bounds", "Origin"}, SDK_AT(offsetof(USkeletalMesh, Bounds) + offsetof(FBoxSphereBounds, Origin), sizeof(FVector)));
    CheckPath("Engine.SkeletalMesh", {"Bounds", "BoxExtent"},
        SDK_AT(offsetof(USkeletalMesh, Bounds) + offsetof(FBoxSphereBounds, BoxExtent), sizeof(FVector)));
}

// M3: engine/skeleton's fields as it resolved them, against ME2's natives (research/le2-vrik.md "The skeleton"):
// execGetBoneNames (0x79DD90) and execGetParentBoneIndex (0x79E2A0) read the component's SkeletalMesh at +0x25C and the
// asset's RefSkeleton at +0xB4 (0x50-byte FMeshBones, ParentIndex +0x40); UpdateSkelPose (0x79B8A0) resets SpaceBases at
// +0x2B8 to identity when its size differs from RefSkeleton's (then LocalAtoms at +0x2C8).
void CheckSkeleton() {
    skeleton::TryStartup();
    skeleton::Fields const f = skeleton::ResolvedFields();
    struct Item {
        char const* what;
        long long ours;
        long long native;
    };
    Item const items[] = {
        {"SkeletalMeshComponent.SkeletalMesh", f.skeletalMesh, 0x25C},
        {"SkeletalMesh.RefSkeleton", f.refSkeleton, 0xB4},
        {"SkeletalMeshComponent.SpaceBases", f.spaceBases, 0x2B8},
    };
    for (Item const& item : items) {
        Add("skeleton", item.what, item.ours == item.native, Hex(item.ours), std::format("ME2's natives {}", Hex(item.native)));
    }
}

void CheckFunctions() {
    char const* const functions[] = {
        "SFXGame.SFXWeapon.GetAimError",                  // game/Sights: a scope's aimed fire (ME1's GetPlayerAimError(Accuracy))
        "SFXGame.BioPlayerController.SwitchWeapon",       // game/Holsters' sequel draw (a holster grab, `mevr weapon <family>`)
        "SFXGame.SFXWeapon.CanReload",                    // game/Reload: whether the game would reload (the gesture's eligibility)
        "SFXGame.SFXWeapon.GetAmmoCountInMagazine",       // ... the magazine's count, before and after
        "SFXGame.BioPlayerController.IsReloading",        // ... the game took the request (accepted) and finished it
        "SFXGame.BioPawnBehavior.SetActionState",         // ... the weapon out of its holster (combat), as FireWeapon does first
        "Core.Object.GetStateName",                       // ... the weapon's state: mid-switch (WeaponEquipping, WeaponPuttingDown)
        "SFXGame.BioPlayerInput.IsCombatEnabled",         // ... only holsters draw: answered false inside the buttons' draws
        "SFXGame.SFXGameModeBase.FireWeapon",             //     (the trigger),
        "SFXGame.SFXGameModeDefault.TightAim",            //     (the aim trigger),
        "SFXGame.SFXGameModeDefault.TryReload",
        "SFXGame.SFXGameModeDefault.NextWeapon",
        "SFXGame.SFXGameModeDefault.PrevWeapon",
        "Engine.SkeletalMeshComponent.HideBone",          // game/ScopeParts: the scope's lens part while its picture is up
        "Engine.SkeletalMeshComponent.UnHideBone",
        "Engine.SkeletalMeshComponent.IsBoneHidden",
        "Engine.Camera.UpdateCamera",                     // the script-package gate, head tracking
        "Engine.PlayerController.GetPlayerViewPoint",     // head tracking
        "Engine.PlayerController.GetFOVAngle",            // head tracking
        "SFXGame.BioPlayerController.GetAdjustedAimFor",  // hand aim's aim query
        "SFXGame.SFXWeapon.GetAdjustedAim",               // hand aim: each shot's aim (M3.1)
        "Engine.PlayerController.ConsoleCommand",         // the console request
        "Engine.SceneCaptureComponent.SetFrameRate",      // the reflection fix
        "Engine.Actor.SetRotation",                       // ue3::runtime's own check of the layout
        "SFXGame.SFXWeapon.GetMagazineSize",              // the LE2 observers' first call (P1b)
        "SFXGame.SFXEngine.QueueSaveGameCommand",         // mevr load (M1.6)
        "SFXGame.SFXEngine.GetCurrentSaveDescriptor",     // mevr savedesc
        "Engine.SkeletalMeshComponent.GetBoneNames",      // engine/skeleton (M3)
        "Engine.SkeletalMeshComponent.GetBoneLocation",
        "Engine.SkeletalMeshComponent.MatchRefBone",
        "SFXGame.SFXInventoryManager.SetWeaponRecoil",    // game/ViewLevel (M3.1)
        "SFXGame.SFXModule_AimAssist.ViewFriction",
        "SFXGame.SFXModule_AimAssist.ViewAdhesion",
        "SFXGame.SFXWeapon.GetWeaponRecoil",              // game/GunKick (M3.3)
        "SFXGame.SFXWeapon.GetMuzzleLoc",                 // game/Weapon (M3.3)
        "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation",
        "SFXGame.BioPawn.GetWeaponStartTraceLocation",
        // game/Posture (research/23 section 7): the crouch, leaving a cover, refusing the cover snapping
        "Engine.Pawn.ShouldCrouch",
        "SFXGame.SFXGameModeDefault.TryExitCover",
        "SFXGame.BioPawn.IsInCover",
        "SFXGame.BioPawn.IsInCoverLeaning",
        "SFXGame.BioPawn.LeaveCover",
        "SFXGame.BioPawn.IsInAnimatedTransition",
        "SFXGame.BioPlayerController.FindPlayerCover",
        "SFXGame.BioPlayerController.TryAcquireCover",
        "SFXGame.MassEffectGuiManager.ShowCoverWidget",
        "SFXGame.MassEffectGuiManager.HideCoverWidget",
        // game/VrCover: the damage rule, its cone's slot, the trace, the test tools
        "SFXGame.SFXGame.ModifyDamage",
        "Engine.CoverLink.GetSlotRotation",
        "Engine.Actor.Trace",
        "SFXGame.BioPawn.IsHostile",
        "Engine.Pawn.IsDead",
        "SFXGame.SFXModule_Damage.SFXTakeDamage",
        "SFXGame.SFXModule_DamagePlayer.SFXTakeDamage",
        "Engine.Actor.SetLocation",
        "SFXGame.SFXGameModeBase.ShowMenu",               // game/GameMenuRequest (M4): Start's pause menu on ME2
        "SFXGame.SFXGameModeDefault.TryHolster",          // ... and the holster
        "SFXGame.BioSFHandler_BrowserWheel.ShowSelectedPanel",  // ... and the omni-tool's entries (its pages)
        "SFXGame.MassEffectGuiManager.GetBrowserHandler",
        "Engine.BioSFPanel.GetVariableFloat",             // game/ScreenReticle (M2): the crosshair movie's alpha
        "Engine.BioSFPanel.GetVariableBool",
        "Engine.BioSFPanel.SetVariableFloat",
        "SFXGame.SFXGameModeBase.GuiKey",                 // ... the rail's back-out of ME2's screens (B, 13)
        "Engine.BioSFHandler.HandleInputEvent",
        // game/OmniTool (M4.3): ME2's omni-tool on Shepard's arm
        "SFXGame.BioVisualEffect.CreateCrustEffect",
        "SFXGame.BioVisualEffect.SetLifeTime",
        "SFXGame.BioVisualEffect.SetScalarParameter",
        "SFXGame.BioVisualEffect.SetState",
        "Engine.Actor.DetachComponent",
        "Engine.SkeletalMeshComponent.AttachComponent",
        "Engine.SkeletalMeshComponent.DetachComponent",
        "Engine.ParticleSystemComponent.ActivateSystem",
        "Engine.ParticleSystemComponent.DeactivateSystem",
        "Engine.ParticleSystemComponent.SetFloatParameter",
        "Engine.ParticleSystemComponent.SetVectorParameter",
        "Engine.ParticleSystemComponent.SetColorParameter",
        "Engine.MaterialInstanceConstant.SetScalarParameterValue",
        "SFXGame.SFXShield_Base.Tick",                    // game/VrCover: the shields' faster recharge behind cover
        "SFXGame.SFXGameModeDefault.StormOn",             // game/Sprint: one sprint for the trilogy
        "SFXGame.SFXGameModeDefault.StormOff",
        "Engine.Pawn.GetMaxSpeed",
        // engine/SelectionGaze (research/40 section 6): ME2's no-parameter update and its target search, the search's body
        // hooked as ME3's (0xAC0530, called from the update's body 0xACE8F0 and this function's thunk 0xB4AF70)
        "SFXGame.BioPlayerSelection.UpdateSelection",
        "SFXGame.BioPlayerSelection.FindCurrentSelectionTarget",
    };
    for (char const* function : functions) {
        CheckFunction(function);
    }
}

void CheckParams() {
    // engine/SelectionGaze: both selection functions take nothing (the hook's void(this) signature, as ME3's)
    CheckParmsSize("SFXGame.BioPlayerSelection.UpdateSelection", 0);
    CheckParmsSize("SFXGame.BioPlayerSelection.FindCurrentSelectionTarget", 0);
    // game/Sights and game/ScopeParts (M3.3)
    PARAM(ASFXWeapon_execGetAimError_Parms, "SFXGame.SFXWeapon.GetAimError", Accuracy);
    PARAM(ASFXWeapon_execGetAimError_Parms, "SFXGame.SFXWeapon.GetAimError", ReturnValue);
    PARAM(ABioPlayerController_eventSwitchWeapon_Parms, "SFXGame.BioPlayerController.SwitchWeapon", oWpn);
    // game/Reload
    PARAM(ASFXWeapon_execCanReload_Parms, "SFXGame.SFXWeapon.CanReload", ReturnValue);
    PARMS(ASFXWeapon_execCanReload_Parms, "SFXGame.SFXWeapon.CanReload");
    PARAM(ASFXWeapon_execGetAmmoCountInMagazine_Parms, "SFXGame.SFXWeapon.GetAmmoCountInMagazine", ReturnValue);
    PARMS(ASFXWeapon_execGetAmmoCountInMagazine_Parms, "SFXGame.SFXWeapon.GetAmmoCountInMagazine");
    PARAM(ABioPlayerController_execIsReloading_Parms, "SFXGame.BioPlayerController.IsReloading", ReturnValue);
    PARMS(ABioPlayerController_execIsReloading_Parms, "SFXGame.BioPlayerController.IsReloading");
    PARMS(USFXGameModeDefault_execTryReload_Parms, "SFXGame.SFXGameModeDefault.TryReload");
    PARAM(UBioPawnBehavior_execSetActionState_Parms, "SFXGame.BioPawnBehavior.SetActionState", eState);
    PARAM(UObject_execGetStateName_Parms, "Core.Object.GetStateName", ReturnValue);
    PARAM(USkeletalMeshComponent_execHideBone_Parms, "Engine.SkeletalMeshComponent.HideBone", BoneIndex);
    PARAM(USkeletalMeshComponent_execHideBone_Parms, "Engine.SkeletalMeshComponent.HideBone", PhysBodyOption);
    PARAM(USkeletalMeshComponent_execUnHideBone_Parms, "Engine.SkeletalMeshComponent.UnHideBone", BoneIndex);
    PARAM(USkeletalMeshComponent_execIsBoneHidden_Parms, "Engine.SkeletalMeshComponent.IsBoneHidden", BoneIndex);
    PARAM(USkeletalMeshComponent_execIsBoneHidden_Parms, "Engine.SkeletalMeshComponent.IsBoneHidden", ReturnValue);
    // game/GameMenuRequest (M4.3): the back-out's B
    PARAM(USFXGameModeBase_execGuiKey_Parms, "SFXGame.SFXGameModeBase.GuiKey", Event);
    PARAM(UBioSFHandler_eventHandleInputEvent_Parms, "Engine.BioSFHandler.HandleInputEvent", Event);
    PARAM(UBioSFHandler_eventHandleInputEvent_Parms, "Engine.BioSFHandler.HandleInputEvent", fValue);
    // game/OmniTool (M4.3): the brightness setters, as LE1's rows
    PARAM(UParticleSystemComponent_execSetFloatParameter_Parms, "Engine.ParticleSystemComponent.SetFloatParameter", ParameterName);
    PARAM(UParticleSystemComponent_execSetFloatParameter_Parms, "Engine.ParticleSystemComponent.SetFloatParameter", Param);
    PARMS(UParticleSystemComponent_execSetFloatParameter_Parms, "Engine.ParticleSystemComponent.SetFloatParameter");
    PARAM(UParticleSystemComponent_execSetVectorParameter_Parms, "Engine.ParticleSystemComponent.SetVectorParameter", ParameterName);
    PARAM(UParticleSystemComponent_execSetVectorParameter_Parms, "Engine.ParticleSystemComponent.SetVectorParameter", Param);
    PARAM(UParticleSystemComponent_execSetColorParameter_Parms, "Engine.ParticleSystemComponent.SetColorParameter", ParameterName);
    PARAM(UParticleSystemComponent_execSetColorParameter_Parms, "Engine.ParticleSystemComponent.SetColorParameter", Param);
    PARAM(UMaterialInstanceConstant_execSetScalarParameterValue_Parms, "Engine.MaterialInstanceConstant.SetScalarParameterValue", ParameterName);
    PARAM(UMaterialInstanceConstant_execSetScalarParameterValue_Parms, "Engine.MaterialInstanceConstant.SetScalarParameterValue", Value);
    PARAM(ABioVisualEffect_execSetScalarParameter_Parms, "SFXGame.BioVisualEffect.SetScalarParameter", sParameter);
    PARAM(ABioVisualEffect_execSetScalarParameter_Parms, "SFXGame.BioVisualEffect.SetScalarParameter", fValue);
    PARAM(ASFXWeapon_execGetWeaponRecoil_Parms, "SFXGame.SFXWeapon.GetWeaponRecoil", ReturnValue);
    PARMS(ASFXWeapon_execGetWeaponRecoil_Parms, "SFXGame.SFXWeapon.GetWeaponRecoil");
    PARAM(ASFXWeapon_execGetMuzzleLoc_Parms, "SFXGame.SFXWeapon.GetMuzzleLoc", ReturnValue);
    PARMS(ASFXWeapon_execGetMuzzleLoc_Parms, "SFXGame.SFXWeapon.GetMuzzleLoc");
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", InSocketName);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutLocation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutRotation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", Space);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", ReturnValue);
    PARAM(ABioPawn_execGetWeaponStartTraceLocation_Parms, "SFXGame.BioPawn.GetWeaponStartTraceLocation", ReturnValue);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", Command);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", bWriteToLog);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", ReturnValue);
    PARMS(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand");
    PARAM(APlayerController_eventGetPlayerViewPoint_Parms, "Engine.PlayerController.GetPlayerViewPoint", out_Location);
    PARAM(APlayerController_eventGetPlayerViewPoint_Parms, "Engine.PlayerController.GetPlayerViewPoint", out_Rotation);
    PARMS(APlayerController_eventGetPlayerViewPoint_Parms, "Engine.PlayerController.GetPlayerViewPoint");
    PARAM(APlayerController_eventGetFOVAngle_Parms, "Engine.PlayerController.GetFOVAngle", ReturnValue);
    PARMS(APlayerController_eventGetFOVAngle_Parms, "Engine.PlayerController.GetFOVAngle");
    PARAM(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor", ReturnValue);
    PARMS(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor");
    PARAM(ASFXWeapon_execGetAdjustedAim_Parms, "SFXGame.SFXWeapon.GetAdjustedAim", ReturnValue);
    PARMS(ASFXWeapon_execGetAdjustedAim_Parms, "SFXGame.SFXWeapon.GetAdjustedAim");
    PARAM(USceneCaptureComponent_execSetFrameRate_Parms, "Engine.SceneCaptureComponent.SetFrameRate", NewFrameRate);
    PARMS(USceneCaptureComponent_execSetFrameRate_Parms, "Engine.SceneCaptureComponent.SetFrameRate");
    PARAM(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation", NewRotation);
    PARAM(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation", ReturnValue);
    PARMS(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation");
    PARAM(ASFXWeapon_execGetMagazineSize_Parms, "SFXGame.SFXWeapon.GetMagazineSize", ReturnValue);
    PARMS(ASFXWeapon_execGetMagazineSize_Parms, "SFXGame.SFXWeapon.GetMagazineSize");
    PARAM(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand", Action);
    PARAM(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand", SaveDescriptor);
    PARAM(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand", Callback);
    PARMS(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand");
    PARAM(USFXEngine_execGetCurrentSaveDescriptor_Parms, "SFXGame.SFXEngine.GetCurrentSaveDescriptor", ReturnValue);
    PARMS(USFXEngine_execGetCurrentSaveDescriptor_Parms, "SFXGame.SFXEngine.GetCurrentSaveDescriptor");
    PARAM(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone", BoneName);
    PARAM(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone", ReturnValue);
    PARMS(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone");
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", BoneName);
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", Space);
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", ReturnValue);
    PARMS(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation");
    PARAM(USkeletalMeshComponent_execGetBoneNames_Parms, "Engine.SkeletalMeshComponent.GetBoneNames", BoneNames);
    PARMS(USkeletalMeshComponent_execGetBoneNames_Parms, "Engine.SkeletalMeshComponent.GetBoneNames");
    // game/Posture (research/23 section 7)
    PARAM(APawn_execShouldCrouch_Parms, "Engine.Pawn.ShouldCrouch", bCrouch);
    PARMS(APawn_execShouldCrouch_Parms, "Engine.Pawn.ShouldCrouch");
    PARAM(USFXGameModeDefault_execTryExitCover_Parms, "SFXGame.SFXGameModeDefault.TryExitCover", bForceExit);
    PARAM(USFXGameModeDefault_execTryExitCover_Parms, "SFXGame.SFXGameModeDefault.TryExitCover", ReturnValue);
    PARAM(ABioPawn_execIsInCover_Parms, "SFXGame.BioPawn.IsInCover", ReturnValue);
    PARMS(ABioPawn_execIsInCover_Parms, "SFXGame.BioPawn.IsInCover");
    PARAM(ABioPawn_execIsInCoverLeaning_Parms, "SFXGame.BioPawn.IsInCoverLeaning", ReturnValue);
    PARMS(ABioPawn_execIsInCoverLeaning_Parms, "SFXGame.BioPawn.IsInCoverLeaning");
    PARAM(ABioPawn_execIsInAnimatedTransition_Parms, "SFXGame.BioPawn.IsInAnimatedTransition", ReturnValue);
    PARAM(ABioPlayerController_execFindPlayerCover_Parms, "SFXGame.BioPlayerController.FindPlayerCover", ReturnValue);
    PARMS(ABioPlayerController_execFindPlayerCover_Parms, "SFXGame.BioPlayerController.FindPlayerCover");
    PARAM(ABioPlayerController_execTryAcquireCover_Parms, "SFXGame.BioPlayerController.TryAcquireCover", ReturnValue);
    // game/VrCover: the damage call's arguments as its frame holds them, the cone's slot rotation, the trace
    PARAM(ASFXGame_execModifyDamage_Parms, "SFXGame.SFXGame.ModifyDamage", HitLocation);
    PARAM(ASFXGame_execModifyDamage_Parms, "SFXGame.SFXGame.ModifyDamage", DamageType);
    PARAM(ASFXGame_execModifyDamage_Parms, "SFXGame.SFXGame.ModifyDamage", injured);
    PARAM(ASFXGame_execModifyDamage_Parms, "SFXGame.SFXGame.ModifyDamage", InstigatedBy);
    PARAM(ASFXGame_execModifyDamage_Parms, "SFXGame.SFXGame.ModifyDamage", DamageCauser);
    PARAM(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation", SlotIdx);
    PARAM(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation", ReturnValue);
    PARMS(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation");
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", HitLocation);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", HitNormal);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceEnd);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceStart);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", bTraceActors);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", ReturnValue);
    PARMS(AActor_execTrace_Parms, "Engine.Actor.Trace");
    PARAM(ABioPawn_execIsHostile_Parms, "SFXGame.BioPawn.IsHostile", Other);
    PARAM(ABioPawn_execIsHostile_Parms, "SFXGame.BioPawn.IsHostile", ReturnValue);
    PARAM(APawn_execIsDead_Parms, "Engine.Pawn.IsDead", ReturnValue);
    PARAM(USFXModule_Damage_execSFXTakeDamage_Parms, "SFXGame.SFXModule_Damage.SFXTakeDamage", Damage);
    PARAM(USFXModule_Damage_execSFXTakeDamage_Parms, "SFXGame.SFXModule_Damage.SFXTakeDamage", HitLocation);
    PARAM(USFXModule_Damage_execSFXTakeDamage_Parms, "SFXGame.SFXModule_Damage.SFXTakeDamage", DamageType);
    PARAM(USFXModule_Damage_execSFXTakeDamage_Parms, "SFXGame.SFXModule_Damage.SFXTakeDamage", InstigatedBy);
    PARAM(USFXModule_Damage_execSFXTakeDamage_Parms, "SFXGame.SFXModule_Damage.SFXTakeDamage", DamageCauser);
    PARAM(USFXModule_DamagePlayer_execSFXTakeDamage_Parms, "SFXGame.SFXModule_DamagePlayer.SFXTakeDamage", Damage);
    PARAM(USFXModule_DamagePlayer_execSFXTakeDamage_Parms, "SFXGame.SFXModule_DamagePlayer.SFXTakeDamage", InstigatedBy);
    PARAM(AActor_execSetLocation_Parms, "Engine.Actor.SetLocation", NewLocation);
    PARAM(AActor_execSetLocation_Parms, "Engine.Actor.SetLocation", ReturnValue);
    // game/GameMenuRequest (M4.3): ME2's pause menu pages
    PARAM(UBioSFHandler_BrowserWheel_eventShowSelectedPanel_Parms, "SFXGame.BioSFHandler_BrowserWheel.ShowSelectedPanel", nPanel);
    PARAM(UBioSFHandler_BrowserWheel_eventShowSelectedPanel_Parms, "SFXGame.BioSFHandler_BrowserWheel.ShowSelectedPanel", oManager);
    // game/ScreenReticle (M2)
    PARAM(UBioSFPanel_execGetVariableFloat_Parms, "Engine.BioSFPanel.GetVariableFloat", sPath);
    PARAM(UBioSFPanel_execGetVariableFloat_Parms, "Engine.BioSFPanel.GetVariableFloat", ReturnValue);
    PARAM(UBioSFPanel_execGetVariableBool_Parms, "Engine.BioSFPanel.GetVariableBool", sPath);
    PARAM(UBioSFPanel_execGetVariableBool_Parms, "Engine.BioSFPanel.GetVariableBool", ReturnValue);
    PARAM(UBioSFPanel_execSetVariableFloat_Parms, "Engine.BioSFPanel.SetVariableFloat", sPath);
    PARAM(UBioSFPanel_execSetVariableFloat_Parms, "Engine.BioSFPanel.SetVariableFloat", fVar);
}

// ME2 native dialogue adapter (research/70): reply lists, wheel slots and reflected calls.
void CheckThespian() {
    CheckField("Engine.Actor", "WorldInfo", SDK_FIELD(AActor, WorldInfo));
    CheckField("SFXGame.BioWorldInfo", "m_oCurrentConversation", SDK_FIELD(ABioWorldInfo, m_oCurrentConversation));
    CheckField("SFXGame.BioConversation", "m_pSpeaker", SDK_FIELD(UBioConversation, m_pSpeaker));
    CheckField("SFXGame.BioConversation", "m_pConversationPlayer", SDK_FIELD(UBioConversation, m_pConversationPlayer));
    CheckField("SFXGame.BioConversation", "m_nCurrentEntry", SDK_FIELD(UBioConversation, m_nCurrentEntry));
    CheckField("SFXGame.BioConversation", "m_EntryList", SDK_FIELD(UBioConversation, m_EntryList));
    CheckField("SFXGame.BioConversation", "m_ReplyList", SDK_FIELD(UBioConversation, m_ReplyList));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel", SDK_BOOL(UBioSFHandler_Conversation, m_bDisplayingWheel));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_bDisplayInvestigateSubMenu", SDK_BOOL(UBioSFHandler_Conversation, m_bDisplayInvestigateSubMenu));
    CheckField("SFXGame.BioConversation", "m_lstCurrentReplyIndices", SDK_FIELD(UBioConversation, m_lstCurrentReplyIndices));
    CheckField("SFXGame.BioConversation", "m_nSelectedReply", SDK_FIELD(UBioConversation, m_nSelectedReply));
    CheckField("SFXGame.BioConversation", "m_nDelayedReplyChoice", SDK_FIELD(UBioConversation, m_nDelayedReplyChoice));
    CheckField("SFXGame.BioConversation", "m_fRemainingVOLength", SDK_FIELD(UBioConversation, m_fRemainingVOLength));
    CheckField("SFXGame.BioConversation", "m_fShowRepliesOffset", SDK_FIELD(UBioConversation, m_fShowRepliesOffset));
    CheckField("Engine.PlayerController", "m_oCachedScaleFormManager", SDK_FIELD(APlayerController, m_oCachedScaleFormManager));
    CheckField("Engine.BioSFHandler", "oPanel", SDK_FIELD(UBioSFHandler, oPanel));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_nSlotsUsed", SDK_FIELD(UBioSFHandler_Conversation, m_nSlotsUsed));
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "ReplyListNew"}, SDK_AT(offsetof(FBioDialogEntryNode, ReplyListNew), 16));
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "ReplyListNew", "nIndex"}, SDK_AT(offsetof(FBioDialogReplyListDetails, nIndex), 4));
    CheckPath("SFXGame.BioConversation", {"m_ReplyList", "bUnskippable"}, SDK_BOOL(FBioDialogReplyNode, bUnskippable));  // game/Thespian
    CheckFunction("Engine.BioSFPanel.InvokeMethod");
    CheckFunction("SFXGame.BioConversation.GetEntryText");
    CheckFunction("SFXGame.BioConversation.GetReplyCategory");
    CheckFunction("SFXGame.BioConversation.GetReplyGUIStyle");
    CheckFunction("SFXGame.BioConversation.GetReplyParaphraseText");
    CheckFunction("SFXGame.BioConversation.GetReplyText");
    CheckFunction("SFXGame.BioConversation.QueueReply");
    CheckFunction("SFXGame.BioConversation.SkipNode");
    CheckFunction("SFXGame.BioSFHandler_Conversation.SelectConversationEntry");
    CheckFunction("SFXGame.BioSFHandler_Conversation.UpdateConversationOptions");
    CheckFunction("SFXGame.BioWorldInfo.EndCurrentConversation");
    CheckFunction("SFXGame.MassEffectGuiManager.GetConversationHandler");
    PARAM(UBioSFPanel_execInvokeMethod_Parms, "Engine.BioSFPanel.InvokeMethod", sMethodName);
    PARAM(UBioSFHandler_Conversation_execSelectConversationEntry_Parms, "SFXGame.BioSFHandler_Conversation.SelectConversationEntry", nWheelLocation);
    PARAM(UBioConversation_execGetReplyText_Parms, "SFXGame.BioConversation.GetReplyText", nIndex);
    PARAM(UBioConversation_execGetReplyText_Parms, "SFXGame.BioConversation.GetReplyText", bParse);
    PARAM(UBioConversation_execGetReplyText_Parms, "SFXGame.BioConversation.GetReplyText", ReturnValue);
    PARAM(UBioConversation_execGetReplyParaphraseText_Parms, "SFXGame.BioConversation.GetReplyParaphraseText", nIndex);
    PARAM(UBioConversation_execGetReplyParaphraseText_Parms, "SFXGame.BioConversation.GetReplyParaphraseText", ReturnValue);
    PARAM(UBioConversation_execGetReplyCategory_Parms, "SFXGame.BioConversation.GetReplyCategory", nIndex);
    PARAM(UBioConversation_execGetReplyCategory_Parms, "SFXGame.BioConversation.GetReplyCategory", ReturnValue);
    PARAM(UBioConversation_execGetReplyGUIStyle_Parms, "SFXGame.BioConversation.GetReplyGUIStyle", nIndex);
    PARAM(UBioConversation_execGetReplyGUIStyle_Parms, "SFXGame.BioConversation.GetReplyGUIStyle", ReturnValue);
    PARAM(UBioConversation_execQueueReply_Parms, "SFXGame.BioConversation.QueueReply", nReply);
    PARAM(UBioConversation_execQueueReply_Parms, "SFXGame.BioConversation.QueueReply", ReturnValue);
    PARAM(UBioConversation_execSkipNode_Parms, "SFXGame.BioConversation.SkipNode", ReturnValue);
    PARAM(UBioConversation_execGetEntryText_Parms, "SFXGame.BioConversation.GetEntryText", bParse);
    PARAM(UBioConversation_execGetEntryText_Parms, "SFXGame.BioConversation.GetEntryText", ReturnValue);
    PARAM(UBioSFHandler_Conversation_execUpdateConversationOptions_Parms, "SFXGame.BioSFHandler_Conversation.UpdateConversationOptions", oConversation);
    PARAM(UMassEffectGuiManager_execGetConversationHandler_Parms, "SFXGame.MassEffectGuiManager.GetConversationHandler", ReturnValue);
}

// game/Traversal (research/le2-traversal.md): the cover list and its slots that vault or climb, the game's own admission
// (CanPerformMantle/Climb with m_bCanMantleOutsideOfCover and the remapped stick for one call), its MantleCover/ClimbCover and the
// custom action they start.
// M6 B/C: live inventory and explicit-target native squad order parameter layouts.
void CheckLe2Squad() {
    CheckField("Engine.Pawn", "InvManager", SDK_FIELD(APawn, InvManager));
    CheckField("Engine.InventoryManager", "InventoryChain", SDK_FIELD(AInventoryManager, InventoryChain));
    CheckField("Engine.Inventory", "Inventory", SDK_FIELD(AInventory, Inventory));
    CheckField("Engine.Pawn", "Weapon", SDK_FIELD(APawn, Weapon));
    CheckField("SFXGame.BioPawn", "PowerManager", SDK_FIELD(ABioPawn, PowerManager));
    CheckField("SFXGame.SFXPowerManager", "Powers", SDK_FIELD(USFXPowerManager, Powers));
    CheckField("SFXGame.SFXPower", "PowerName", SDK_FIELD(USFXPower, PowerName));
    CheckField("SFXGame.SFXPower", "Rank", SDK_FIELD(USFXPower, Rank));
    CheckField("SFXGame.SFXPower", "DisplayInHUD", SDK_BOOL(USFXPower, DisplayInHUD));
    CheckFunction("SFXGame.BioPlayerController.OrderWeaponSwitch");
    PARAM(ABioPlayerController_execOrderWeaponSwitch_Parms, "SFXGame.BioPlayerController.OrderWeaponSwitch", oOrderedPawn);
    PARAM(ABioPlayerController_execOrderWeaponSwitch_Parms, "SFXGame.BioPlayerController.OrderWeaponSwitch", Wpn);
    PARMS(ABioPlayerController_execOrderWeaponSwitch_Parms, "SFXGame.BioPlayerController.OrderWeaponSwitch");
    CheckFunction("SFXGame.BioPlayerController.HenchmanOrderAttackTarget");
    PARAM(ABioPlayerController_eventHenchmanOrderAttackTarget_Parms, "SFXGame.BioPlayerController.HenchmanOrderAttackTarget", oHenchman);
    PARAM(ABioPlayerController_eventHenchmanOrderAttackTarget_Parms, "SFXGame.BioPlayerController.HenchmanOrderAttackTarget", oTargetActor);
    PARMS(ABioPlayerController_eventHenchmanOrderAttackTarget_Parms, "SFXGame.BioPlayerController.HenchmanOrderAttackTarget");
    CheckFunction("SFXGame.BioPlayerController.HenchmanOrderFollow");
    PARAM(ABioPlayerController_eventHenchmanOrderFollow_Parms, "SFXGame.BioPlayerController.HenchmanOrderFollow", oHenchman);
    PARMS(ABioPlayerController_eventHenchmanOrderFollow_Parms, "SFXGame.BioPlayerController.HenchmanOrderFollow");
    CheckFunction("SFXGame.BioPlayerController.HenchmanOrderHoldPosition");
    PARAM(ABioPlayerController_eventHenchmanOrderHoldPosition_Parms, "SFXGame.BioPlayerController.HenchmanOrderHoldPosition", oHenchman);
    PARAM(ABioPlayerController_eventHenchmanOrderHoldPosition_Parms, "SFXGame.BioPlayerController.HenchmanOrderHoldPosition", vLocation);
    PARMS(ABioPlayerController_eventHenchmanOrderHoldPosition_Parms, "SFXGame.BioPlayerController.HenchmanOrderHoldPosition");
    CheckFunction("SFXGame.BioPlayerController.SquadOrderUsePower");
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", nmPower);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", pPawn);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", nQueue);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", bShowIndicator);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", pDesiredTarget);
    PARMS(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower");
    CheckFunction("SFXGame.BioPlayerController.ApplyTacticalOrders");
    CheckFunction("SFXGame.SFXPower.IsEnabled");
    CheckFunction("SFXGame.SFXPower.GetCooldownRemaining");
    CheckFunction("SFXGame.SFXPowerManager.HasPowerBeenEvolved");
    // The roster's names (game/Le2Squad.hpp ActorName): the pawn's PrettyName as text through the game info.
    CheckField("Engine.WorldInfo", "Game", SDK_FIELD(AWorldInfo, Game));
    CheckFunction("SFXGame.SFXPawn.GetPrettyName");
    PARAM(ASFXPawn_execGetPrettyName_Parms, "SFXGame.SFXPawn.GetPrettyName", ReturnValue);
    PARMS(ASFXPawn_execGetPrettyName_Parms, "SFXGame.SFXPawn.GetPrettyName");
    CheckFunction("SFXGame.SFXGame.GetSimpleString");
    PARAM(ASFXGame_execGetSimpleString_Parms, "SFXGame.SFXGame.GetSimpleString", StrRef);
    PARAM(ASFXGame_execGetSimpleString_Parms, "SFXGame.SFXGame.GetSimpleString", bParse);
    PARAM(ASFXGame_execGetSimpleString_Parms, "SFXGame.SFXGame.GetSimpleString", ReturnValue);
    PARMS(ASFXGame_execGetSimpleString_Parms, "SFXGame.SFXGame.GetSimpleString");
    CheckFunction("SFXGame.BioActorBehavior.GetActorGameName");
    PARAM(UBioActorBehavior_eventGetActorGameName_Parms, "SFXGame.BioActorBehavior.GetActorGameName", ReturnValue);
    PARMS(UBioActorBehavior_eventGetActorGameName_Parms, "SFXGame.BioActorBehavior.GetActorGameName");
    // The move order's marker, placed before every hold order with a point (game/Le2Squad.hpp PlaceMoveIndicator).
    CheckFunction("SFXGame.BioPlayerController.UpdateMoveToIndicator");
    PARAM(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator", pSquadPawn);
    PARAM(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator", vMoveToPosition);
    PARAM(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator", ReturnValue);
    PARMS(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator");
    // What the henchman's AI made of an order (game/Le2Squad.hpp OrderState: the placement's log and status).
    CheckField("SFXGame.SFXAI_Henchman", "m_Orders", SDK_FIELD(ASFXAI_Henchman, m_Orders));
    CheckField("SFXGame.SFXAI_Henchman", "m_vHoldLocation", SDK_FIELD(ASFXAI_Henchman, m_vHoldLocation));
    CheckField("SFXGame.SFXAI_Henchman", "m_bHoldingPosition", SDK_BOOL(ASFXAI_Henchman, m_bHoldingPosition));
    CheckField("SFXGame.SFXAI_Henchman", "m_bFollowPlayer", SDK_BOOL(ASFXAI_Henchman, m_bFollowPlayer));
    CheckField("SFXGame.SFXAI_Henchman", "m_ActorToDefend", SDK_FIELD(ASFXAI_Henchman, m_ActorToDefend));
    CheckField("SFXGame.SFXAI_Henchman", "TetherDistanceWhileExecutingOrder", SDK_FIELD(ASFXAI_Henchman, TetherDistanceWhileExecutingOrder));
    CheckField("SFXGame.SFXAI_Core", "m_RequestedActorToFollow", SDK_FIELD(ASFXAI_Core, m_RequestedActorToFollow));
}

void CheckTraversal() {
    CheckClass<ACoverLink>("Engine.CoverLink");
    CheckClass<ASFXPlayerController>("SFXGame.SFXPlayerController");
    CheckClass<UBioPlayerSelection>("SFXGame.BioPlayerSelection");
    CheckClass<UBioCustomAction_CoverClimbMantleBase>("SFXGame.BioCustomAction_CoverClimbMantleBase");
    CheckField("Engine.WorldInfo", "CoverList", SDK_FIELD(AWorldInfo, CoverList));
    CheckField("Engine.Actor", "WorldInfo", SDK_FIELD(AActor, WorldInfo));
    CheckField("Engine.Actor", "bBlockActors", SDK_BOOL(AActor, bBlockActors));
    CheckField("Engine.CoverLink", "NextCoverLink", SDK_FIELD(ACoverLink, NextCoverLink));
    CheckField("Engine.CoverLink", "Slots", SDK_FIELD(ACoverLink, Slots));
    CheckElement("Engine.CoverLink", "Slots", static_cast<long long>(sizeof(FCoverSlot)));
    CheckPath("Engine.CoverLink", {"Slots", "bCanMantle"}, SDK_BOOL(FCoverSlot, bCanMantle));
    CheckPath("Engine.CoverLink", {"Slots", "bCanClimbUp"}, SDK_BOOL(FCoverSlot, bCanClimbUp));
    CheckPath("Engine.CoverLink", {"Slots", "bEnabled"}, SDK_BOOL(FCoverSlot, bEnabled));
    CheckPath("Engine.CoverLink", {"Slots", "MantleTarget"}, SDK_FIELD(FCoverSlot, MantleTarget));
    CheckPath("Engine.CoverLink", {"Slots", "MantleTarget", "Actor"},
        SDK_AT(offsetof(FCoverSlot, MantleTarget) + offsetof(FCoverReference, Actor), 8));
    CheckPath("Engine.CoverLink", {"Slots", "MantleTarget", "SlotIdx"},
        SDK_AT(offsetof(FCoverSlot, MantleTarget) + offsetof(FCoverReference, SlotIdx), 4));
    CheckField("SFXGame.BioPlayerController", "m_bCanMantleOutsideOfCover", SDK_BOOL(ABioPlayerController, m_bCanMantleOutsideOfCover));
    CheckField("SFXGame.BioPlayerController", "RemappedJoyUp", SDK_FIELD(ABioPlayerController, RemappedJoyUp));
    CheckField("SFXGame.BioPlayerController", "RemappedJoyRight", SDK_FIELD(ABioPlayerController, RemappedJoyRight));
    CheckField("SFXGame.BioPlayerController", "m_fClimbMantleDistanceThreshold", SDK_FIELD(ABioPlayerController, m_fClimbMantleDistanceThreshold));
    CheckField("SFXGame.BioPlayerController", "m_fClimbMantleFaceAngleThreshold", SDK_FIELD(ABioPlayerController, m_fClimbMantleFaceAngleThreshold));
    CheckField("SFXGame.BioPlayerController", "m_oPlayerSelection", SDK_FIELD(ABioPlayerController, m_oPlayerSelection));
    CheckField("SFXGame.BioPlayerController", "CustomActions", SDK_FIELD(ABioPlayerController, CustomActions));
    CheckField("SFXGame.SFXPlayerController", "CurrentCustomAction", SDK_FIELD(ASFXPlayerController, CurrentCustomAction));
    CheckField("SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget", SDK_FIELD(UBioPlayerSelection, m_oCurrentSelectionTarget));
    CheckField("SFXGame.BioPlayerSelection", "m_bCurrentSelectionIsCombatTarget", SDK_BOOL(UBioPlayerSelection, m_bCurrentSelectionIsCombatTarget));
    CheckField("Engine.PlayerController", "bIgnoreMoveInput", SDK_FIELD(APlayerController, bIgnoreMoveInput));
    CheckField("Engine.PlayerController", "bIgnoreLookInput", SDK_FIELD(APlayerController, bIgnoreLookInput));
    CheckField("SFXGame.BioPawn", "CurrentLink", SDK_FIELD(ABioPawn, CurrentLink));
    CheckField("Engine.Pawn", "Weapon", SDK_FIELD(APawn, Weapon));
    // LExSDKv2 compiles these two 8 bytes early (its FBodyStance packs to 9 bytes, the game's to 12); its header's own comments say 0xFC
    // and 0xF0, which is where our layer finds them (the run of 2026-11-14 read the played stance there).
    CheckField("SFXGame.BioCustomAction_CoverClimbMantleBase", "m_CurrentBodyStance", SDK_AT(0xFC, 1));
    CheckField("SFXGame.BioCustomAction_CoverClimbMantleBase", "m_fCollisionOffPerioud", SDK_AT(0xF0, 4));
    // The CovPosInfo the functions take (the same struct as the custom action's cached one).
    long long const info = static_cast<long long>(offsetof(UBioCustomAction_CoverClimbMantleBase, m_CurrentCoverInfo));
    CheckPath("SFXGame.BioCustomAction_CoverClimbMantleBase", {"m_CurrentCoverInfo", "Location"}, SDK_AT(info + offsetof(FCovPosInfo, Location), 12));
    CheckPath("SFXGame.BioCustomAction_CoverClimbMantleBase", {"m_CurrentCoverInfo", "Normal"}, SDK_AT(info + offsetof(FCovPosInfo, Normal), 12));
    CheckPath("SFXGame.BioCustomAction_CoverClimbMantleBase", {"m_CurrentCoverInfo", "Tangent"}, SDK_AT(info + offsetof(FCovPosInfo, Tangent), 12));
    CheckPath("SFXGame.BioCustomAction_CoverClimbMantleBase", {"m_CurrentCoverInfo", "Link"}, SDK_AT(info + offsetof(FCovPosInfo, Link), 8));
    CheckPath("SFXGame.BioCustomAction_CoverClimbMantleBase", {"m_CurrentCoverInfo", "LtSlotIdx"}, SDK_AT(info + offsetof(FCovPosInfo, LtSlotIdx), 4));
    CheckPath("SFXGame.BioCustomAction_CoverClimbMantleBase", {"m_CurrentCoverInfo", "RtSlotIdx"}, SDK_AT(info + offsetof(FCovPosInfo, RtSlotIdx), 4));
    CheckPath("SFXGame.BioCustomAction_CoverClimbMantleBase", {"m_CurrentCoverInfo", "LtToRtPct"}, SDK_AT(info + offsetof(FCovPosInfo, LtToRtPct), 4));
    char const* const functions[] = {
        "Engine.CoverLink.GetSlotLocation",
        "Engine.CoverLink.GetSlotRotation",
        "Engine.Actor.Trace",
        "Engine.Actor.SetLocation",
        "SFXGame.BioPlayerController.CanPerformMantle",
        "SFXGame.BioPlayerController.CanPerformClimb",
        "SFXGame.SFXPlayerController.MantleCover",
        "SFXGame.SFXPlayerController.ClimbCover",
        "SFXGame.SFXPlayerController.TryClimbOrMantleCover",
        "SFXGame.SFXPlayerController.DoCustomAction",
        "SFXGame.BioCustomAction_CoverClimbMantleBase.StartCustomAction",
        "SFXGame.BioCustomAction_CoverClimbMantleBase.StopCustomAction",
        "SFXGame.BioCustomAction_CoverClimbMantleBase.BodyStanceAnimEndNotification",
    };
    for (char const* function : functions) {
        CheckFunction(function);
    }
    PARAM(ABioPlayerController_execCanPerformMantle_Parms, "SFXGame.BioPlayerController.CanPerformMantle", CoverInfo);
    PARAM(ABioPlayerController_execCanPerformMantle_Parms, "SFXGame.BioPlayerController.CanPerformMantle", ReturnValue);
    PARAM(ABioPlayerController_execCanPerformClimb_Parms, "SFXGame.BioPlayerController.CanPerformClimb", CoverInfo);
    PARAM(ABioPlayerController_execCanPerformClimb_Parms, "SFXGame.BioPlayerController.CanPerformClimb", ReturnValue);
    PARAM(ASFXPlayerController_execMantleCover_Parms, "SFXGame.SFXPlayerController.MantleCover", CovInfo);
    PARAM(ASFXPlayerController_execClimbCover_Parms, "SFXGame.SFXPlayerController.ClimbCover", CovInfo);
    PARAM(ACoverLink_execGetSlotLocation_Parms, "Engine.CoverLink.GetSlotLocation", SlotIdx);
    PARAM(ACoverLink_execGetSlotLocation_Parms, "Engine.CoverLink.GetSlotLocation", ReturnValue);
    PARAM(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation", SlotIdx);
    PARAM(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation", ReturnValue);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceEnd);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceStart);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", HitLocation);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", ReturnValue);
    PARAM(AActor_execSetLocation_Parms, "Engine.Actor.SetLocation", NewLocation);
}

#elif defined(MEVR_GAME_LE3)
// Shared SceneCaptureRig, checked against LE3 metadata before native use.
void CheckLe3CaptureRig() {
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pCenterObject", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pCenterObject));
    CheckClass<UTextureRenderTarget2D>("Engine.TextureRenderTarget2D");
    CheckClass<USceneCapture2DComponent>("Engine.SceneCapture2DComponent");
    CheckClass<ASceneCapture2DActor>("Engine.SceneCapture2DActor");
    CheckField("Engine.SceneCaptureActor", "SceneCapture", SDK_FIELD(ASceneCaptureActor, SceneCapture));
    CheckField("Engine.Actor", "bStatic", SDK_BOOL(AActor, bStatic));
    CheckField("Engine.Actor", "bNoDelete", SDK_BOOL(AActor, bNoDelete));
    CheckField("Engine.SceneCaptureComponent", "bEnabled", SDK_BOOL(USceneCaptureComponent, bEnabled));
    CheckField("Engine.SceneCaptureComponent", "bEnablePostProcess", SDK_BOOL(USceneCaptureComponent, bEnablePostProcess));
    CheckField("Engine.SceneCaptureComponent", "bEnableFog", SDK_BOOL(USceneCaptureComponent, bEnableFog));
    CheckField("Engine.SceneCaptureComponent", "bUseMainScenePostProcessSettings",
        SDK_BOOL(USceneCaptureComponent, bUseMainScenePostProcessSettings));
    CheckField("Engine.SceneCaptureComponent", "bSkipUpdateIfTextureUsersOccluded",
        SDK_BOOL(USceneCaptureComponent, bSkipUpdateIfTextureUsersOccluded));
    CheckField("Engine.SceneCaptureComponent", "bSkipUpdateIfOwnerOccluded", SDK_BOOL(USceneCaptureComponent, bSkipUpdateIfOwnerOccluded));
    CheckField("Engine.SceneCaptureComponent", "MaxUpdateDist", SDK_FIELD(USceneCaptureComponent, MaxUpdateDist));
    CheckField("Engine.SceneCaptureComponent", "PostProcess", SDK_FIELD(USceneCaptureComponent, PostProcess));
    CheckField("Engine.SceneCaptureComponent", "FrameRate", SDK_FIELD(USceneCaptureComponent, FrameRate));
    // The generated LE3 subclass is four bytes late here. Native SetCaptureParameters
    // (60AEF0C4, RVA 0x700500) saves/sets/restores mask 1 at +0x174 around its update.
    // Compare that independent native contract, not the incorrect compiled SDK bitfield.
    Guard("native-layout", "SceneCapture2DComponent.bUpdateMatrices", [&] {
        auto const info = R().PropertyOf(R().FindClass("Engine.SceneCapture2DComponent"), "bUpdateMatrices");
        Add("native-layout", "SceneCapture2DComponent.bUpdateMatrices",
            info && Matches(*info, info->offset, Sdk{0x174, 1, 4}), Ours(info),
            "native SetCaptureParameters RVA 0x700500: +0x174 mask 1");
    });
    CheckField("Engine.Texture", "Resource", SDK_FIELD(UTexture, Resource));
    CheckField("Engine.TextureRenderTarget2D", "Format", SDK_FIELD(UTextureRenderTarget2D, Format));
    CheckField("Engine.PlayerController", "Player", SDK_FIELD(APlayerController, Player));
    CheckField("Engine.LocalPlayer", "PlayerPostProcess", SDK_FIELD(ULocalPlayer, PlayerPostProcess));
    CheckField("Engine.Engine", "DefaultPostProcess", SDK_FIELD(UEngine, DefaultPostProcess));
    CheckFunction("Engine.Actor.Spawn");
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", SpawnClass);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", SpawnOwner);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", SpawnLocation);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", bNoCollisionFail);
    PARAM(AActor_execSpawn_Parms, "Engine.Actor.Spawn", ReturnValue);
    PARMS(AActor_execSpawn_Parms, "Engine.Actor.Spawn");
    CheckFunction("Engine.Actor.Destroy");
    PARAM(AActor_execDestroy_Parms, "Engine.Actor.Destroy", ReturnValue);
    PARMS(AActor_execDestroy_Parms, "Engine.Actor.Destroy");
    CheckFunction("Engine.Actor.ForceUpdateComponents");
    PARAM(AActor_execForceUpdateComponents_Parms, "Engine.Actor.ForceUpdateComponents", bCollisionUpdate);
    PARAM(AActor_execForceUpdateComponents_Parms, "Engine.Actor.ForceUpdateComponents", bTransformOnly);
    PARMS(AActor_execForceUpdateComponents_Parms, "Engine.Actor.ForceUpdateComponents");
    CheckFunction("Engine.TextureRenderTarget2D.Create");
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InSizeX);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InSizeY);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InFormat);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", InClearColor);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", bOnlyRenderOnce);
    PARAM(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create", ReturnValue);
    PARMS(UTextureRenderTarget2D_execCreate_Parms, "Engine.TextureRenderTarget2D.Create");
    CheckFunction("Engine.SceneCaptureComponent.SetEnabled");
    PARAM(USceneCaptureComponent_execSetEnabled_Parms, "Engine.SceneCaptureComponent.SetEnabled", bEnable);
    PARMS(USceneCaptureComponent_execSetEnabled_Parms, "Engine.SceneCaptureComponent.SetEnabled");
    CheckFunction("Engine.SceneCaptureComponent.SetFrameRate");
    PARAM(USceneCaptureComponent_execSetFrameRate_Parms, "Engine.SceneCaptureComponent.SetFrameRate", NewFrameRate);
    PARMS(USceneCaptureComponent_execSetFrameRate_Parms, "Engine.SceneCaptureComponent.SetFrameRate");
    CheckFunction("Engine.SceneCapture2DComponent.SetView");
    PARAM(USceneCapture2DComponent_execSetView_Parms, "Engine.SceneCapture2DComponent.SetView", NewLocation);
    PARAM(USceneCapture2DComponent_execSetView_Parms, "Engine.SceneCapture2DComponent.SetView", NewRotation);
    PARMS(USceneCapture2DComponent_execSetView_Parms, "Engine.SceneCapture2DComponent.SetView");
    CheckFunction("Engine.SceneCapture2DComponent.SetCaptureParameters");
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewTextureTarget);
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewFOV);
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewNearPlane);
    PARAM(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters", NewFarPlane);
    PARMS(USceneCapture2DComponent_execSetCaptureParameters_Parms, "Engine.SceneCapture2DComponent.SetCaptureParameters");
}


// Scoped LE3 real-slot cover candidate: metadata only, no damage or gameplay invocation.
// T04 (docs/trilogy-plan.md): the reads LE3 made through LExSDKv2 (engine/Legacy) until 2026-11-16, on our layer now:
// head tracking's three functions and their blocks, the camera's owner, the reflection fix's class and function.
// (Their other pieces have rows in the tables below: Camera.CameraCache and its POV, Controller.Pawn, the game mode
// manager, SceneCaptureComponent.FrameRate and SetFrameRate's block in CheckLe3CaptureRig, ConsoleCommand's block.)
// The param names are the ones the modules ask for; LExSDKv2's LE3 generator lower-cased out_Rotation.
void CheckLe3EngineAccess() {
    CheckClass<APlayerController>("Engine.PlayerController");
    CheckClass<ACamera>("Engine.Camera");
    CheckClass<USceneCaptureComponent>("Engine.SceneCaptureComponent");
    CheckClass<USceneCaptureReflectComponent>("Engine.SceneCaptureReflectComponent");
    CheckField("Engine.Camera", "PCOwner", SDK_FIELD(ACamera, PCOwner));
    CheckFunction("Engine.Camera.UpdateCamera");
    CheckFunction("Engine.PlayerController.GetPlayerViewPoint");
    CheckFunction("Engine.PlayerController.GetFOVAngle");
    CheckParam("Engine.PlayerController.GetPlayerViewPoint", "out_Location",
        SDK_FIELD(APlayerController_eventGetPlayerViewPoint_Parms, out_Location));
    CheckParam("Engine.PlayerController.GetPlayerViewPoint", "out_Rotation",
        SDK_FIELD(APlayerController_eventGetPlayerViewPoint_Parms, out_rotation));
    PARMS(APlayerController_eventGetPlayerViewPoint_Parms, "Engine.PlayerController.GetPlayerViewPoint");
    PARAM(APlayerController_eventGetFOVAngle_Parms, "Engine.PlayerController.GetFOVAngle", ReturnValue);
    PARMS(APlayerController_eventGetFOVAngle_Parms, "Engine.PlayerController.GetFOVAngle");
}

// game/OmniTool's ME3 adapter: the omni-tool is ME3's client effect, started and stopped through its manager. No row for
// ParticleSystemComponent.Template (LExSDKv2's particle headers were 8 bytes early on LE2; the adapter checks the template's
// name it reads instead).
void CheckLe3OmniTool() {
    CheckClass<URvrClientEffectManager>("SFXGame.RvrClientEffectManager");
    CheckClass<URvrClientEffectMulti>("SFXGame.RvrClientEffectMulti");
    CheckClass<UParticleSystemComponent>("Engine.ParticleSystemComponent");
    CheckField("Engine.ActorComponent", "Owner", SDK_FIELD(UActorComponent, Owner));
    CheckFunction("SFXGame.RvrClientEffectManager.GetClientEffectManager");
    PARAM(URvrClientEffectManager_execGetClientEffectManager_Parms, "SFXGame.RvrClientEffectManager.GetClientEffectManager", ReturnValue);
    CheckFunction("SFXGame.RvrClientEffectManager.Start");
    PARAM(URvrClientEffectManager_execStart_Parms, "SFXGame.RvrClientEffectManager.Start", pEffect);
    PARAM(URvrClientEffectManager_execStart_Parms, "SFXGame.RvrClientEffectManager.Start", pInstigator);
    PARAM(URvrClientEffectManager_execStart_Parms, "SFXGame.RvrClientEffectManager.Start", ReturnValue);
    PARMS(URvrClientEffectManager_execStart_Parms, "SFXGame.RvrClientEffectManager.Start");
    CheckFunction("SFXGame.RvrClientEffectManager.Stop");
    PARAM(URvrClientEffectManager_execStop_Parms, "SFXGame.RvrClientEffectManager.Stop", pEffect);
    PARAM(URvrClientEffectManager_execStop_Parms, "SFXGame.RvrClientEffectManager.Stop", Id);
    PARAM(URvrClientEffectManager_execStop_Parms, "SFXGame.RvrClientEffectManager.Stop", bAllowCooldown);
    PARMS(URvrClientEffectManager_execStop_Parms, "SFXGame.RvrClientEffectManager.Stop");
    // The display: the player's own, live OmniHand_Loop (FindDisplay): its actor the client effect's (RvrClientEffectActor),
    // whose effect component is active, not stopping, and was started for the player's pawn (its target's Instigator). The
    // particle system's own bWasCompleted/bWasDeactivated aren't read: LExSDKv2's particle headers are 8 bytes early here too
    // (as for Template above; their rows disagreed, 0x364 ours against 0x35C, 2026-11-17).
    CheckClass<ARvrClientEffectActor>("SFXGame.RvrClientEffectActor");
    CheckClass<URvrClientEffectComponent>("SFXGame.RvrClientEffectComponent");
    CheckField("SFXGame.RvrClientEffectActor", "m_pCEffectComponent", SDK_FIELD(ARvrClientEffectActor, m_pCEffectComponent));
    CheckField("SFXGame.RvrClientEffectComponent", "m_bActive", SDK_BOOL(URvrClientEffectComponent, m_bActive));
    CheckField("SFXGame.RvrClientEffectComponent", "m_bStopRequested", SDK_BOOL(URvrClientEffectComponent, m_bStopRequested));
    CheckField("SFXGame.RvrClientEffectComponent", "m_oTarget", SDK_FIELD(URvrClientEffectComponent, m_oTarget));
    // A struct's member: the offsets add up (CheckPath), so LExSDKv2's side is the struct's place plus the member's in it.
    CheckPath("SFXGame.RvrClientEffectComponent", {"m_oTarget", "Instigator"},
        SDK_AT(offsetof(URvrClientEffectComponent, m_oTarget) + offsetof(FRvrClientEffectTarget, Instigator), 8));
}

// game/WorldUi's ME3 adapter (research/le3-native-gui.md "The game's screens on ME3"): the focused movie's tag (game/Le3Gui)
// and whether it shows the game's UI world (such a screen keeps the native panel).
void CheckLe3WorldUi() {
    CheckClass<USFXGUIMovie>("SFXGame.SFXGUIMovie");
    CheckField("SFXGame.SFXGUIMovie", "nmTag", SDK_FIELD(USFXGUIMovie, nmTag));
    CheckField("SFXGame.SFXGUIMovie", "m_bRequiresUIWorld", SDK_BOOL(USFXGUIMovie, m_bRequiresUIWorld));
    CheckFunction("SFXGame.SFXGUIMovie.SetRequiresUIWorld");
    PARAM(USFXGUIMovie_execSetRequiresUIWorld_Parms, "SFXGame.SFXGUIMovie.SetRequiresUIWorld", bUIWorldRequired);
    PARMS(USFXGUIMovie_execSetRequiresUIWorld_Parms, "SFXGame.SFXGUIMovie.SetRequiresUIWorld");
    // game/GuiCursor's ME3 route for the pointer's mouse (research/le3-native-gui.md "The movie's mouse") and its diagnostics.
    CheckFunction("SFXGame.SFXGUIMovie.SendMouseEvent");
    PARAM(USFXGUIMovie_execSendMouseEvent_Parms, "SFXGame.SFXGUIMovie.SendMouseEvent", nEventID);
    PARMS(USFXGUIMovie_execSendMouseEvent_Parms, "SFXGame.SFXGUIMovie.SendMouseEvent");
    CheckField("SFXGame.SFXGUIMovie", "m_bMouseVisibleWhenFocused", SDK_BOOL(USFXGUIMovie, m_bMouseVisibleWhenFocused));
    CheckField("SFXGame.SFXGUIMovie", "bUsingGamepad", SDK_BOOL(USFXGUIMovie, bUsingGamepad));
    CheckFunction("GFxUI.GFxMovie.GetVariableNumber");  // `mevr guimouse` on a movie that isn't a legacy adapter (War Assets)
    PARAM(UGFxMovie_execGetVariableNumber_Parms, "GFxUI.GFxMovie.GetVariableNumber", Path);
    PARAM(UGFxMovie_execGetVariableNumber_Parms, "GFxUI.GFxMovie.GetVariableNumber", ReturnValue);
}

// game/ScreenReticle's ME3 adapter (research/le3-hud-panel.md "The crosshair hidden"): the GUI manager's reticle movie, its
// alpha (the legacy adapter's calls, ME1's and ME2's names), what it shows, and the weapon's zoom (the scope view stays the game's).
void CheckLe3ScreenReticle() {
    CheckClass<USFXGUIInteraction>("SFXGame.SFXGUIInteraction");
    CheckField("SFXGame.SFXGUIInteraction", "MovieTag_Reticle", SDK_FIELD(USFXGUIInteraction, MovieTag_Reticle));
    CheckFunction("SFXGame.SFXGUIInteraction.GetMovie");
    PARAM(USFXGUIInteraction_execGetMovie_Parms, "SFXGame.SFXGUIInteraction.GetMovie", i_pOwningPlayer);
    PARAM(USFXGUIInteraction_execGetMovie_Parms, "SFXGame.SFXGUIInteraction.GetMovie", nmMovieTag);
    PARAM(USFXGUIInteraction_execGetMovie_Parms, "SFXGame.SFXGUIInteraction.GetMovie", ReturnValue);
    PARMS(USFXGUIInteraction_execGetMovie_Parms, "SFXGame.SFXGUIInteraction.GetMovie");
    CheckClass<USFXSFHandler_Reticle>("SFXGame.SFXSFHandler_Reticle");
    CheckField("SFXGame.SFXSFHandler_Reticle", "m_oCurrentWeaponReticle", SDK_FIELD(USFXSFHandler_Reticle, m_oCurrentWeaponReticle));
    CheckField("SFXGame.SFXSFHandler_Reticle", "m_bReticleVisible", SDK_BOOL(USFXSFHandler_Reticle, m_bReticleVisible));
    CheckFunction("SFXGame.SFXGUIMovieLegacyAdapter.GetVariableFloat");
    PARAM(USFXGUIMovieLegacyAdapter_execGetVariableFloat_Parms, "SFXGame.SFXGUIMovieLegacyAdapter.GetVariableFloat", sPath);
    PARAM(USFXGUIMovieLegacyAdapter_execGetVariableFloat_Parms, "SFXGame.SFXGUIMovieLegacyAdapter.GetVariableFloat", ReturnValue);
    PARMS(USFXGUIMovieLegacyAdapter_execGetVariableFloat_Parms, "SFXGame.SFXGUIMovieLegacyAdapter.GetVariableFloat");
    CheckFunction("SFXGame.SFXGUIMovieLegacyAdapter.SetVariableFloat");
    PARAM(USFXGUIMovieLegacyAdapter_execSetVariableFloat_Parms, "SFXGame.SFXGUIMovieLegacyAdapter.SetVariableFloat", sPath);
    PARAM(USFXGUIMovieLegacyAdapter_execSetVariableFloat_Parms, "SFXGame.SFXGUIMovieLegacyAdapter.SetVariableFloat", fVar);
    PARMS(USFXGUIMovieLegacyAdapter_execSetVariableFloat_Parms, "SFXGame.SFXGUIMovieLegacyAdapter.SetVariableFloat");
    CheckFunction("SFXGame.SFXGUIMovie.GetVisible");
    PARAM(USFXGUIMovie_execGetVisible_Parms, "SFXGame.SFXGUIMovie.GetVisible", ReturnValue);
    PARMS(USFXGUIMovie_execGetVisible_Parms, "SFXGame.SFXGUIMovie.GetVisible");
    CheckField("Engine.Pawn", "Weapon", SDK_FIELD(APawn, Weapon));
    CheckClass<ASFXWeapon>("SFXGame.SFXWeapon");
    CheckField("SFXGame.SFXWeapon", "GUIReticleClass", SDK_FIELD(ASFXWeapon, GUIReticleClass));
    CheckField("SFXGame.SFXWeapon", "GUIZoomReticleClass", SDK_FIELD(ASFXWeapon, GUIZoomReticleClass));
    CheckField("SFXGame.SFXWeapon_NativeBase", "bIsZoomed", SDK_BOOL(ASFXWeapon_NativeBase, bIsZoomed));
}

void CheckLe3PhysicalCover() {
    CheckClass<ACoverLink>("Engine.CoverLink");
    CheckClass<ASFXPawn_PlayerParty>("SFXGame.SFXPawn_PlayerParty");
    CheckField("SFXGame.BioPawn", "CurrentLink", SDK_FIELD(ABioPawn, CurrentLink));
    CheckField("SFXGame.BioPawn", "CurrentSlotIdx", SDK_FIELD(ABioPawn, CurrentSlotIdx));
    CheckField("Engine.CoverLink", "Slots", SDK_FIELD(ACoverLink, Slots));
    CheckElement("Engine.CoverLink", "Slots", sizeof(FCoverSlot));
    CheckPath("Engine.CoverLink", {"Slots", "bUnsafeCover"}, SDK_BOOL(FCoverSlot, bUnsafeCover));
    CheckPath("Engine.CoverLink", {"Slots", "bEnabled"}, SDK_BOOL(FCoverSlot, bEnabled));
    char const* const functions[] = {"SFXGame.SFXPawn_PlayerParty.ModifyDamage", "SFXGame.SFXGameModeDefault.FindCover",
        "SFXGame.SFXGameModeDefault.TryAcquireCover", "Engine.CoverLink.GetSlotLocation", "Engine.CoverLink.GetSlotRotation",
        "SFXGame.BioPawn.IsInCover", "SFXGame.BioPawn.IsInCoverLeaning"};
    for (auto path : functions) CheckFunction(path);
    PARAM(ASFXPawn_PlayerParty_execModifyDamage_Parms, "SFXGame.SFXPawn_PlayerParty.ModifyDamage", instigatedBy);
    PARAM(ASFXPawn_PlayerParty_execModifyDamage_Parms, "SFXGame.SFXPawn_PlayerParty.ModifyDamage", HitLocation);
    PARAM(ASFXPawn_PlayerParty_execModifyDamage_Parms, "SFXGame.SFXPawn_PlayerParty.ModifyDamage", DamageCauser);
    PARMS(ASFXPawn_PlayerParty_execModifyDamage_Parms, "SFXGame.SFXPawn_PlayerParty.ModifyDamage");
    PARAM(USFXGameModeDefault_execFindCover_Parms, "SFXGame.SFXGameModeDefault.FindCover", FoundCover);
    PARAM(USFXGameModeDefault_execFindCover_Parms, "SFXGame.SFXGameModeDefault.FindCover", bWideCheck);
    PARAM(USFXGameModeDefault_execFindCover_Parms, "SFXGame.SFXGameModeDefault.FindCover", ReturnValue);
    PARMS(USFXGameModeDefault_execFindCover_Parms, "SFXGame.SFXGameModeDefault.FindCover");
    PARAM(USFXGameModeDefault_execTryAcquireCover_Parms, "SFXGame.SFXGameModeDefault.TryAcquireCover", bWideCheck);
    PARAM(USFXGameModeDefault_execTryAcquireCover_Parms, "SFXGame.SFXGameModeDefault.TryAcquireCover", bTestOnly);
    PARAM(USFXGameModeDefault_execTryAcquireCover_Parms, "SFXGame.SFXGameModeDefault.TryAcquireCover", ReturnValue);
    PARMS(USFXGameModeDefault_execTryAcquireCover_Parms, "SFXGame.SFXGameModeDefault.TryAcquireCover");
    PARAM(ACoverLink_execGetSlotLocation_Parms, "Engine.CoverLink.GetSlotLocation", SlotIdx);
    PARAM(ACoverLink_execGetSlotLocation_Parms, "Engine.CoverLink.GetSlotLocation", bForceUseOffset);
    PARAM(ACoverLink_execGetSlotLocation_Parms, "Engine.CoverLink.GetSlotLocation", ReturnValue);
    PARMS(ACoverLink_execGetSlotLocation_Parms, "Engine.CoverLink.GetSlotLocation");
    PARAM(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation", SlotIdx);
    PARAM(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation", bForceUseOffset);
    PARAM(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation", ReturnValue);
    PARMS(ACoverLink_execGetSlotRotation_Parms, "Engine.CoverLink.GetSlotRotation");
    PARAM(ABioPawn_execIsInCover_Parms, "SFXGame.BioPawn.IsInCover", ReturnValue);
    PARMS(ABioPawn_execIsInCover_Parms, "SFXGame.BioPawn.IsInCover");
    PARAM(ABioPawn_execIsInCoverLeaning_Parms, "SFXGame.BioPawn.IsInCoverLeaning", ReturnValue);
    PARMS(ABioPawn_execIsInCoverLeaning_Parms, "SFXGame.BioPawn.IsInCoverLeaning");
    // game/Posture: the HUD's "take cover" prompt hidden again while the snapping is refused (ME2's ShowCoverWidget follow-up).
    CheckClass<USFXGUIInteraction>("SFXGame.SFXGUIInteraction");
    CheckFunction("SFXGame.SFXGUIInteraction.ShowCoverWidget");
    PARAM(USFXGUIInteraction_eventShowCoverWidget_Parms, "SFXGame.SFXGUIInteraction.ShowCoverWidget", oPlayerController);
    PARMS(USFXGUIInteraction_eventShowCoverWidget_Parms, "SFXGame.SFXGUIInteraction.ShowCoverWidget");
    CheckFunction("SFXGame.SFXGUIInteraction.HideCoverWidget");
    PARAM(USFXGUIInteraction_eventHideCoverWidget_Parms, "SFXGame.SFXGUIInteraction.HideCoverWidget", oPlayerController);
    PARMS(USFXGUIInteraction_eventHideCoverWidget_Parms, "SFXGame.SFXGUIInteraction.HideCoverWidget");
    struct Member { char const* name; Sdk layout; ue3::Kind kind; };
    Member const members[] = {
        {"Link", SDK_FIELD(FCovPosInfo, Link), ue3::Kind::Object},
        {"LtSlotIdx", SDK_FIELD(FCovPosInfo, LtSlotIdx), ue3::Kind::Int},
        {"RtSlotIdx", SDK_FIELD(FCovPosInfo, RtSlotIdx), ue3::Kind::Int},
    };
    for (auto const& member : members) {
        auto const what = std::format("SFXGame.SFXGameModeDefault.FindCover(FoundCover.{})", member.name);
        Guard("param-path", what, [&] {
            auto const descriptor = R().PropertyOf(R().FindFunction("SFXGame.SFXGameModeDefault.FindCover"), "FoundCover");
            auto* owner = descriptor && descriptor->kind == ue3::Kind::Struct ? R().StructOf(*descriptor) : nullptr;
            auto field = owner ? R().PropertyOf(owner, member.name) : std::nullopt;
            Add("param-path", what, field && field->kind == member.kind && Matches(*field, field->offset, member.layout),
                Ours(field), SdkText(member.layout));
        });
    }
}

// ME1's level camera, gun kick and camera shakes on ME3 (game/ViewLevel, game/GunKick, game/CameraShake; the trilogy
// parity ledger's G09-G11): ME2's names, which ME3 keeps. The rows the other ME3 tables don't already have.
void CheckLe3ViewLevel() {
    CheckClass<ASFXInventoryManager>("SFXGame.SFXInventoryManager");  // the shots' recoil queue
    CheckClass<USFXModule_AimAssist>("SFXGame.SFXModule_AimAssist");  // aim assist's friction and adhesion
    CheckField("Engine.Actor", "Modules", SDK_FIELD(AActor, Modules));
    CheckField("Engine.PlayerController", "PlayerInput", SDK_FIELD(APlayerController, PlayerInput));
    CheckField("Engine.PlayerInput", "RawJoyLookRight", SDK_FIELD(UPlayerInput, RawJoyLookRight));
    CheckField("Engine.PlayerInput", "RawJoyLookUp", SDK_FIELD(UPlayerInput, RawJoyLookUp));
    CheckField("SFXGame.SFXInventoryManager", "WeaponRecoilOffset", SDK_FIELD(ASFXInventoryManager, WeaponRecoilOffset));
    CheckField("SFXGame.SFXInventoryManager", "TotalRecoil", SDK_FIELD(ASFXInventoryManager, TotalRecoil));
    CheckField("SFXGame.SFXModule_AimAssist", "LastZoomSnapTarget", SDK_FIELD(USFXModule_AimAssist, LastZoomSnapTarget));
    CheckField("SFXGame.BioPawn", "CoverAction", SDK_FIELD(ABioPawn, CoverAction));
    CheckFunction("SFXGame.SFXInventoryManager.SetWeaponRecoil");
    CheckFunction("SFXGame.SFXModule_AimAssist.ViewFriction");
    PARAM(USFXModule_AimAssist_execViewFriction_Parms, "SFXGame.SFXModule_AimAssist.ViewFriction", ReturnValue);
    CheckFunction("SFXGame.SFXModule_AimAssist.ViewAdhesion");
    PARAM(USFXModule_AimAssist_execViewAdhesion_Parms, "SFXGame.SFXModule_AimAssist.ViewAdhesion", ReturnValue);
    // game/GunKick: the weapon's own recoil for a shot.
    CheckFunction("SFXGame.SFXWeapon.GetWeaponRecoil");
    PARAM(ASFXWeapon_execGetWeaponRecoil_Parms, "SFXGame.SFXWeapon.GetWeaponRecoil", ReturnValue);
    PARMS(ASFXWeapon_execGetWeaponRecoil_Parms, "SFXGame.SFXWeapon.GetWeaponRecoil");
    // game/CameraShake: the camera modes' body shakes and the camera's two shake modifiers.
    CheckField("SFXGame.SFXCameraMode", "bIsCameraShakeEnabled", SDK_BOOL(USFXCameraMode, bIsCameraShakeEnabled));
    CheckField("SFXGame.SFXPlayerCamera", "ScreenShakeModifier", SDK_FIELD(ASFXPlayerCamera, ScreenShakeModifier));
    CheckField("Engine.Camera", "CameraShakeCamMod", SDK_FIELD(ACamera, CameraShakeCamMod));
    CheckField("Engine.CameraModifier", "bDisabled", SDK_BOOL(UCameraModifier, bDisabled));
}

// L3-1: metadata comparisons only, not an assertion that any gameplay adapter is ready.
// The common Run gate requires runtime::Up(); the default LE3 layout remains disabled until
// native discovery is complete. These rows use the LE3 SDK as an independent comparison side.
void CheckClasses() {
    CheckClass<USFXGameModeBase>("SFXGame.SFXGameModeBase");
    CheckClass<USFXGameModeGalaxy>("SFXGame.SFXGameModeGalaxy");
    CheckClass<USFXGameModeOrbital>("SFXGame.SFXGameModeOrbital");
    CheckClass<UBioCameraBehaviorGalaxy>("SFXGame.BioCameraBehaviorGalaxy");
    CheckClass<USFXSystem>("SFXGame.SFXSystem");
    CheckClass<UBioPlanet>("SFXGame.BioPlanet");
    CheckClass<UBioSeqAct_OrbitalGame>("SFXGame.BioSeqAct_OrbitalGame");
    CheckClass<UObject>("Core.Object");
    CheckClass<UFunction>("Core.Function");
    CheckClass<UClass>("Core.Class");
    CheckClass<UProperty>("Core.Property");
    CheckClass<UBoolProperty>("Core.BoolProperty");
    CheckClass<UStructProperty>("Core.StructProperty");
    CheckClass<UObjectProperty>("Core.ObjectProperty");
    CheckClass<UArrayProperty>("Core.ArrayProperty");
    CheckClass<AActor>("Engine.Actor");
    CheckClass<APawn>("Engine.Pawn");
    CheckClass<UEngine>("Engine.Engine");
    CheckClass<UWorld>("Engine.World");
    CheckClass<ABioPlayerController>("SFXGame.BioPlayerController");
    CheckClass<USFXGameModeManager>("SFXGame.SFXGameModeManager");
    CheckClass<ASFXWeapon>("SFXGame.SFXWeapon");
    CheckClass<AWeapon>("Engine.Weapon");
    CheckClass<ASFXWeapon_NativeBase>("SFXGame.SFXWeapon_NativeBase");
    CheckClass<ASFXWeapon_Pistol_Base>("SFXGame.SFXWeapon_Pistol_Base");
    CheckClass<ASFXWeapon_SMG_Base>("SFXGame.SFXWeapon_SMG_Base");
    CheckClass<ASFXWeapon_AssaultRifle_Base>("SFXGame.SFXWeapon_AssaultRifle_Base");
    CheckClass<ASFXWeapon_Shotgun_Base>("SFXGame.SFXWeapon_Shotgun_Base");
    CheckClass<ASFXWeapon_SniperRifle_Base>("SFXGame.SFXWeapon_SniperRifle_Base");
    CheckClass<USkeletalMeshComponent>("Engine.SkeletalMeshComponent");
    CheckClass<USkeletalMesh>("Engine.SkeletalMesh");
    CheckClass<USkeletalMeshSocket>("Engine.SkeletalMeshSocket");
    CheckClass<ABioWorldInfo>("SFXGame.BioWorldInfo");
    CheckClass<ABioBaseSquad>("SFXGame.BioBaseSquad");
    CheckClass<ABioPawn>("SFXGame.BioPawn");
    CheckClass<ASFXPawn_Henchman>("SFXGame.SFXPawn_Henchman");
    CheckClass<ASFXAI_Henchman>("SFXGame.SFXAI_Henchman");
    CheckClass<USFXPowerManager>("SFXGame.SFXPowerManager");
    CheckClass<USFXPowerCustomActionBase>("SFXGame.SFXPowerCustomActionBase");
}

// The live controller and static conversation data split in LE3. Source: installed SFXGame.pcc.
// This table supplies native evidence, not permission to inherit LE1's audio/camera paths.
void CheckLe3Dialogue() {
    CheckClass<UBioConversation>("SFXGame.BioConversation");
    CheckClass<UBioConversationController>("SFXGame.BioConversationController");
    CheckClass<UBioConversationManager>("SFXGame.BioConversationManager");
    CheckClass<UBioSFHandler_Conversation>("SFXGame.BioSFHandler_Conversation");
    CheckField("SFXGame.BioWorldInfo", "m_pConversationManager", SDK_FIELD(ABioWorldInfo, m_pConversationManager));
    CheckField("SFXGame.BioConversationController", "m_pConvData", SDK_FIELD(UBioConversationController, m_pConvData));
    CheckField("SFXGame.BioConversationController", "m_aCurrentReplyIndices", SDK_FIELD(UBioConversationController, m_aCurrentReplyIndices));
    CheckField("SFXGame.BioConversationController", "m_pSpeaker", SDK_FIELD(UBioConversationController, m_pSpeaker));
    CheckField("SFXGame.BioConversationController", "m_pPlayer", SDK_FIELD(UBioConversationController, m_pPlayer));
    CheckField("SFXGame.BioConversationController", "m_nCurrentEntry", SDK_FIELD(UBioConversationController, m_nCurrentEntry));
    CheckField("SFXGame.BioConversationController", "m_nSelectedReply", SDK_FIELD(UBioConversationController, m_nSelectedReply));
    CheckField("SFXGame.BioConversationController", "m_nDelayedReplyChoice", SDK_FIELD(UBioConversationController, m_nDelayedReplyChoice));
    CheckField("SFXGame.BioConversationController", "m_fRemainingVOAudioLength", SDK_FIELD(UBioConversationController, m_fRemainingVOAudioLength));
    CheckField("SFXGame.BioConversationController", "m_fShowRepliesOffset", SDK_FIELD(UBioConversationController, m_fShowRepliesOffset));
    CheckField("SFXGame.BioConversationController", "m_sCurrentSubTitle", SDK_FIELD(UBioConversationController, m_sCurrentSubTitle));
    CheckField("SFXGame.BioConversation", "m_EntryList", SDK_FIELD(UBioConversation, m_EntryList));
    CheckField("SFXGame.BioConversation", "m_ReplyList", SDK_FIELD(UBioConversation, m_ReplyList));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_nSlotsUsed", SDK_FIELD(UBioSFHandler_Conversation, m_nSlotsUsed));
    CheckField("SFXGame.BioConversationController", "m_bConversationEnded", SDK_BOOL(UBioConversationController, m_bConversationEnded));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_bDisplayingWheel", SDK_BOOL(UBioSFHandler_Conversation, m_bDisplayingWheel));
    CheckField("SFXGame.BioSFHandler_Conversation", "m_bDisplayInvestigateSubMenu", SDK_BOOL(UBioSFHandler_Conversation, m_bDisplayInvestigateSubMenu));
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "ReplyListNew"}, SDK_AT(offsetof(FBioDialogEntryNode, ReplyListNew), 16));
    CheckPath("SFXGame.BioConversation", {"m_EntryList", "ReplyListNew", "nIndex"}, SDK_AT(offsetof(FBioDialogReplyListDetails, nIndex), 4));
    CheckPath("SFXGame.BioConversation", {"m_ReplyList", "bUnskippable"}, SDK_BOOL(FBioDialogReplyNode, bUnskippable));
    CheckPath("SFXGame.BioConversation", {"m_ReplyList", "sText"}, SDK_AT(offsetof(FBioDialogReplyNode, sText), 16));
    CheckPath("SFXGame.BioConversation", {"m_ReplyList", "srText"}, SDK_AT(offsetof(FBioDialogReplyNode, srText), 4));
    CheckFunction("SFXGame.BioWorldInfo.GetConversationManager");
    PARAM(ABioWorldInfo_execGetConversationManager_Parms, "SFXGame.BioWorldInfo.GetConversationManager", ReturnValue);
    PARMS(ABioWorldInfo_execGetConversationManager_Parms, "SFXGame.BioWorldInfo.GetConversationManager");
    CheckFunction("SFXGame.BioConversationManager.GetFullConversation");
    PARAM(UBioConversationManager_execGetFullConversation_Parms, "SFXGame.BioConversationManager.GetFullConversation", ReturnValue);
    PARMS(UBioConversationManager_execGetFullConversation_Parms, "SFXGame.BioConversationManager.GetFullConversation");
    CheckFunction("SFXGame.BioConversationController.GetReplyParaphraseText");
    PARAM(UBioConversationController_execGetReplyParaphraseText_Parms, "SFXGame.BioConversationController.GetReplyParaphraseText", nIndex);
    PARAM(UBioConversationController_execGetReplyParaphraseText_Parms, "SFXGame.BioConversationController.GetReplyParaphraseText", ReturnValue);
    PARMS(UBioConversationController_execGetReplyParaphraseText_Parms, "SFXGame.BioConversationController.GetReplyParaphraseText");
    CheckFunction("SFXGame.BioConversationController.GetReplyCategory");
    PARAM(UBioConversationController_execGetReplyCategory_Parms, "SFXGame.BioConversationController.GetReplyCategory", nIndex);
    PARAM(UBioConversationController_execGetReplyCategory_Parms, "SFXGame.BioConversationController.GetReplyCategory", ReturnValue);
    PARMS(UBioConversationController_execGetReplyCategory_Parms, "SFXGame.BioConversationController.GetReplyCategory");
    CheckFunction("SFXGame.BioConversationController.GetReplyGUIStyle");
    PARAM(UBioConversationController_execGetReplyGUIStyle_Parms, "SFXGame.BioConversationController.GetReplyGUIStyle", nIndex);
    PARAM(UBioConversationController_execGetReplyGUIStyle_Parms, "SFXGame.BioConversationController.GetReplyGUIStyle", ReturnValue);
    PARMS(UBioConversationController_execGetReplyGUIStyle_Parms, "SFXGame.BioConversationController.GetReplyGUIStyle");
    CheckFunction("SFXGame.BioConversationController.QueueReply");
    PARAM(UBioConversationController_execQueueReply_Parms, "SFXGame.BioConversationController.QueueReply", nReply);
    PARAM(UBioConversationController_execQueueReply_Parms, "SFXGame.BioConversationController.QueueReply", ReturnValue);
    PARMS(UBioConversationController_execQueueReply_Parms, "SFXGame.BioConversationController.QueueReply");
    CheckFunction("SFXGame.SFXGUIInteraction.GetConversationHandler");
    PARAM(USFXGUIInteraction_execGetConversationHandler_Parms, "SFXGame.SFXGUIInteraction.GetConversationHandler", oPlayerController);
    PARAM(USFXGUIInteraction_execGetConversationHandler_Parms, "SFXGame.SFXGUIInteraction.GetConversationHandler", ReturnValue);
    PARMS(USFXGUIInteraction_execGetConversationHandler_Parms, "SFXGame.SFXGUIInteraction.GetConversationHandler");
    CheckFunction("SFXGame.BioSFHandler_Conversation.SelectConversationEntry");
    PARAM(UBioSFHandler_Conversation_execSelectConversationEntry_Parms, "SFXGame.BioSFHandler_Conversation.SelectConversationEntry", nWheelLocation);
    PARAM(UBioSFHandler_Conversation_execSelectConversationEntry_Parms, "SFXGame.BioSFHandler_Conversation.SelectConversationEntry", ReturnValue);
    PARMS(UBioSFHandler_Conversation_execSelectConversationEntry_Parms, "SFXGame.BioSFHandler_Conversation.SelectConversationEntry");
    CheckFunction("SFXGame.BioSFHandler_Conversation.UpdateConversationOptions");
    PARAM(UBioSFHandler_Conversation_eventUpdateConversationOptions_Parms, "SFXGame.BioSFHandler_Conversation.UpdateConversationOptions", oConversation);
    PARMS(UBioSFHandler_Conversation_eventUpdateConversationOptions_Parms, "SFXGame.BioSFHandler_Conversation.UpdateConversationOptions");
    CheckFunction("SFXGame.BioSFHandler_Conversation.SelectInterrupt");
    PARAM(UBioSFHandler_Conversation_eventSelectInterrupt_Parms, "SFXGame.BioSFHandler_Conversation.SelectInterrupt", bParagon);
    PARMS(UBioSFHandler_Conversation_eventSelectInterrupt_Parms, "SFXGame.BioSFHandler_Conversation.SelectInterrupt");
    CheckFunction("SFXGame.SFXPowerCustomActionBase.GetStringFromStringRef");
    PARAM(USFXPowerCustomActionBase_execGetStringFromStringRef_Parms, "SFXGame.SFXPowerCustomActionBase.GetStringFromStringRef", TheStringRef);
    PARAM(USFXPowerCustomActionBase_execGetStringFromStringRef_Parms, "SFXGame.SFXPowerCustomActionBase.GetStringFromStringRef", TheString);
    PARAM(USFXPowerCustomActionBase_execGetStringFromStringRef_Parms, "SFXGame.SFXPowerCustomActionBase.GetStringFromStringRef", bParse);
    PARAM(USFXPowerCustomActionBase_execGetStringFromStringRef_Parms, "SFXGame.SFXPowerCustomActionBase.GetStringFromStringRef", nParseIndex);
    PARMS(USFXPowerCustomActionBase_execGetStringFromStringRef_Parms, "SFXGame.SFXPowerCustomActionBase.GetStringFromStringRef");
    CheckFunction("SFXGame.SFXGUIMovie.GetUIString");
    PARAM(USFXGUIMovie_execGetUIString_Parms, "SFXGame.SFXGUIMovie.GetUIString", sr);
    PARAM(USFXGUIMovie_execGetUIString_Parms, "SFXGame.SFXGUIMovie.GetUIString", bParse);
    PARAM(USFXGUIMovie_execGetUIString_Parms, "SFXGame.SFXGUIMovie.GetUIString", ReturnValue);
    PARMS(USFXGUIMovie_execGetUIString_Parms, "SFXGame.SFXGUIMovie.GetUIString");
    CheckFunction("SFXGame.BioSFHandler_Conversation.AS_HideWheel");
    CheckParmsSize("SFXGame.BioSFHandler_Conversation.AS_HideWheel", 0);
}

void CheckFields() {
    CheckLe3CaptureRig();
    CheckField("Engine.LocalPlayer", "NearClipPlane", SDK_FIELD(ULocalPlayer, NearClipPlane));  // game/Sights: the scope's pull
    CheckField("Engine.SkeletalMeshComponent", "SkeletalMesh", SDK_FIELD(USkeletalMeshComponent, SkeletalMesh));
    // L3 menu pause: native override owns admission; this bit is observed, never forced.
    CheckField("Engine.WorldInfo", "bPlayersOnly", SDK_BOOL(AWorldInfo, bPlayersOnly));
    CheckField("Engine.WorldInfo", "GRI", SDK_FIELD(AWorldInfo, GRI));
    CheckField("SFXGame.SFXGRI", "bPauseForCommand", SDK_BOOL(ASFXGRI, bPauseForCommand));
    CheckField("Engine.SkeletalMeshComponent", "SpaceBases", SDK_FIELD(USkeletalMeshComponent, SpaceBases));
    CheckField("Engine.SkeletalMeshComponent", "LocalAtoms", SDK_FIELD(USkeletalMeshComponent, LocalAtoms));
    CheckField("Engine.SkeletalMeshComponent", "ParentAnimComponent", SDK_FIELD(USkeletalMeshComponent, ParentAnimComponent));
    CheckField("Engine.SkeletalMeshComponent", "Attachments", SDK_FIELD(USkeletalMeshComponent, Attachments));
    CheckField("Engine.SkeletalMeshComponent", "LocalToWorld", SDK_FIELD(USkeletalMeshComponent, LocalToWorld));
    CheckField("Engine.SkeletalMesh", "RefSkeleton", SDK_FIELD(USkeletalMesh, RefSkeleton));
    CheckField("Engine.SkeletalMesh", "Sockets", SDK_FIELD(USkeletalMesh, Sockets));
    CheckField("Engine.SkeletalMeshSocket", "SocketName", SDK_FIELD(USkeletalMeshSocket, SocketName));
    CheckField("Engine.SkeletalMeshSocket", "BoneName", SDK_FIELD(USkeletalMeshSocket, BoneName));
    CheckField("Engine.SkeletalMeshSocket", "RelativeLocation", SDK_FIELD(USkeletalMeshSocket, RelativeLocation));
    CheckField("Engine.SkeletalMeshSocket", "RelativeRotation", SDK_FIELD(USkeletalMeshSocket, RelativeRotation));

    CheckField("SFXGame.SFXGameModeBase", "Bindings", SDK_FIELD(USFXGameModeBase, Bindings));
    CheckField("SFXGame.SFXGameModeBase", "LocalizedBindings", SDK_FIELD(USFXGameModeBase, LocalizedBindings));
    CheckField("SFXGame.SFXGameModeGalaxy", "GalaxyCam", SDK_FIELD(USFXGameModeGalaxy, GalaxyCam));
    // LE3 CIC projector discovery and geometry: independent of the map camera.
    CheckClass<UBioTierManager>("SFXGame.BioTierManager");
    CheckField("SFXGame.BioTierManager", "bReevaluateStreaming", SDK_BOOL(UBioTierManager, bReevaluateStreaming));
    CheckClass<ULevelStreaming>("Engine.LevelStreaming");
    CheckField("Engine.WorldInfo", "StreamingLevels", SDK_FIELD(AWorldInfo, StreamingLevels));
    CheckField("Engine.LevelStreaming", "PackageName", SDK_FIELD(ULevelStreaming, PackageName));
    CheckField("Engine.LevelStreaming", "LoadedLevel", SDK_FIELD(ULevelStreaming, LoadedLevel));
    CheckField("Engine.LevelStreaming", "bShouldBeLoaded", SDK_BOOL(ULevelStreaming, bShouldBeLoaded));
    CheckField("Engine.LevelStreaming", "bShouldBeVisible", SDK_BOOL(ULevelStreaming, bShouldBeVisible));
    CheckField("Engine.LevelStreaming", "bIsVisible", SDK_BOOL(ULevelStreaming, bIsVisible));
    CheckField("Engine.LevelStreaming", "bHasLoadRequestPending", SDK_BOOL(ULevelStreaming, bHasLoadRequestPending));
    CheckField("Engine.LevelStreaming", "bHasUnloadRequestPending", SDK_BOOL(ULevelStreaming, bHasUnloadRequestPending));
    CheckField("SFXGame.BioWorldInfo", "m_WorldStreamingStates", SDK_FIELD(ABioWorldInfo, m_WorldStreamingStates));
    CheckElement("SFXGame.BioWorldInfo", "m_WorldStreamingStates", static_cast<long long>(sizeof(FWorldStreamingState)));
    CheckPath("SFXGame.BioWorldInfo", {"m_WorldStreamingStates", "Name"}, SDK_AT(offsetof(FWorldStreamingState, Name), 8));
    CheckPath("SFXGame.BioWorldInfo", {"m_WorldStreamingStates", "Enabled"}, SDK_BOOL(FWorldStreamingState, Enabled));
    CheckField("Engine.Actor", "Tag", SDK_FIELD(AActor, Tag));
    CheckClass<AInterpActor>("Engine.InterpActor");
    CheckClass<ASFXPointOfInterest>("SFXGame.SFXPointOfInterest");
    CheckClass<UStaticMeshComponent>("Engine.StaticMeshComponent");
    CheckField("Engine.DynamicSMActor", "StaticMeshComponent", SDK_FIELD(ADynamicSMActor, StaticMeshComponent));
    CheckField("Engine.StaticMeshComponent", "StaticMesh", SDK_FIELD(UStaticMeshComponent, StaticMesh));
    CheckField("Engine.PrimitiveComponent", "LocalToWorld", SDK_FIELD(UPrimitiveComponent, LocalToWorld));
    CheckPath("Engine.PrimitiveComponent", {"Bounds", "BoxExtent"},
        SDK_AT(offsetof(UPrimitiveComponent, Bounds) + offsetof(FBoxSphereBounds, BoxExtent), sizeof(FVector)));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_nCurrentState", SDK_FIELD(UBioCameraBehaviorGalaxy, m_nCurrentState));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_nActiveWorld", SDK_FIELD(UBioCameraBehaviorGalaxy, m_nActiveWorld));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pCurrentSystem", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pCurrentSystem));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_pCurrentPlanet", SDK_FIELD(UBioCameraBehaviorGalaxy, m_pCurrentPlanet));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_fSystemScanCooldown", SDK_FIELD(UBioCameraBehaviorGalaxy, m_fSystemScanCooldown));
    CheckField("SFXGame.SFXSystem", "m_fReaperAlertLevel", SDK_FIELD(USFXSystem, m_fReaperAlertLevel));
    CheckField("SFXGame.SFXSystem", "m_fReaperGraceTime", SDK_FIELD(USFXSystem, m_fReaperGraceTime));
    CheckField("SFXGame.SFXSystem", "m_nScannedPlanets", SDK_FIELD(USFXSystem, m_nScannedPlanets));
    CheckField("Engine.Actor", "LatentActions", SDK_FIELD(AActor, LatentActions));
    CheckField("SFXGame.BioSeqAct_OrbitalGame", "CurrentPlanet", SDK_FIELD(UBioSeqAct_OrbitalGame, CurrentPlanet));
    CheckField("SFXGame.BioSeqAct_OrbitalGame", "Planet", SDK_FIELD(UBioSeqAct_OrbitalGame, Planet));
    CheckField("SFXGame.SFXGameModeBase", "bIsActive", SDK_BOOL(USFXGameModeBase, bIsActive));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_bCanDoSystemScan", SDK_BOOL(UBioCameraBehaviorGalaxy, m_bCanDoSystemScan));
    CheckField("SFXGame.BioCameraBehaviorGalaxy", "m_bPaused", SDK_BOOL(UBioCameraBehaviorGalaxy, m_bPaused));
    CheckField("SFXGame.SFXSystem", "m_bReapersChasePlayer", SDK_BOOL(USFXSystem, m_bReapersChasePlayer));
    CheckField("SFXGame.SFXSystem", "m_bSystemReapersActive", SDK_BOOL(USFXSystem, m_bSystemReapersActive));
    CheckField("SFXGame.SFXSystem", "m_bReapersHaveBeenDetected", SDK_BOOL(USFXSystem, m_bReapersHaveBeenDetected));
    CheckField("SFXGame.SFXGameModeOrbital", "Scanning", SDK_BOOL(USFXGameModeOrbital, Scanning));
    CheckField("SFXGame.SFXGameModeOrbital", "ProbeActive", SDK_BOOL(USFXGameModeOrbital, ProbeActive));
    CheckField("SFXGame.SFXGameModeOrbital", "ImpactActive", SDK_BOOL(USFXGameModeOrbital, ImpactActive));
    CheckField("SFXGame.SFXGameModeOrbital", "bRescaling", SDK_BOOL(USFXGameModeOrbital, bRescaling));
    CheckField("SFXGame.SFXGameModeOrbital", "bExiting", SDK_BOOL(USFXGameModeOrbital, bExiting));
    CheckField("Engine.Actor", "Physics", SDK_FIELD(AActor, Physics));
    CheckField("Engine.Actor", "Velocity", SDK_FIELD(AActor, Velocity));
    CheckField("Engine.Pawn", "bWantsToCrouch", SDK_BOOL(APawn, bWantsToCrouch));
    CheckField("Engine.Pawn", "bIsCrouched", SDK_BOOL(APawn, bIsCrouched));
    CheckField("Engine.Pawn", "GroundSpeed", SDK_FIELD(APawn, GroundSpeed));
    CheckField("SFXGame.BioPawn", "CurrentCustomAction", SDK_FIELD(ABioPawn, CurrentCustomAction));
    CheckField("SFXGame.BioPawn", "CoverType", SDK_FIELD(ABioPawn, CoverType));
    CheckField("SFXGame.BioPawn", "StormSpeed", SDK_FIELD(ABioPawn, StormSpeed));
    CheckField("SFXGame.BioPawn", "bStorming", SDK_BOOL(ABioPawn, bStorming));
    CheckField("SFXGame.BioPawn", "CombatGroundSpeed", SDK_FIELD(ABioPawn, CombatGroundSpeed));  // game/Sprint: the drawn run
    CheckField("SFXGame.BioPlayerController", "bWantsToStorm", SDK_FIELD(ABioPlayerController, bWantsToStorm));  // StormOn's intent
    CheckField("SFXGame.BioPlayerController", "m_oPlayerSelection", SDK_FIELD(ABioPlayerController, m_oPlayerSelection));
    CheckField("SFXGame.BioPlayerSelection", "m_oCurrentSelectionTarget", SDK_FIELD(UBioPlayerSelection, m_oCurrentSelectionTarget));
    CheckField("Engine.PlayerController", "PlayerCamera", SDK_FIELD(APlayerController, PlayerCamera));
    CheckField("Engine.Camera", "bLockedFOV", SDK_BOOL(ACamera, bLockedFOV));
    CheckField("Engine.Camera", "LockedFOV", SDK_FIELD(ACamera, LockedFOV));
    CheckField("Engine.Camera", "CameraCache", SDK_FIELD(ACamera, CameraCache));
    CheckField("SFXGame.SFXCameraNativeBase", "CurrentCameraMode", SDK_FIELD(ASFXCameraNativeBase, CurrentCameraMode));
    CheckField("SFXGame.SFXCameraMode_Interpolate", "To", SDK_FIELD(USFXCameraMode_Interpolate, To));
    // game/CameraProbe's trace (the parity ledger's G12 check, lane FP3): the camera mode's collision with the world.
    CheckClass<ASFXCameraNativeBase>("SFXGame.SFXCameraNativeBase");
    CheckClass<USFXCameraMode>("SFXGame.SFXCameraMode");
    CheckClass<USFXCameraMode_Interpolate>("SFXGame.SFXCameraMode_Interpolate");
    CheckField("SFXGame.SFXCameraMode", "bCollisionEnabled", SDK_BOOL(USFXCameraMode, bCollisionEnabled));
    CheckField("SFXGame.SFXCameraMode", "Input", SDK_FIELD(USFXCameraMode, Input));
    CheckField("SFXGame.SFXCameraInput", "MaxCameraRotationSpeed", SDK_FIELD(USFXCameraInput, MaxCameraRotationSpeed));
    CheckField("SFXGame.SFXCameraInput", "CameraSensitivity", SDK_FIELD(USFXCameraInput, CameraSensitivity));
    CheckField("SFXGame.SFXCameraInput", "TimeToReachFullSpeed", SDK_FIELD(USFXCameraInput, TimeToReachFullSpeed));
    CheckField("SFXGame.SFXCameraInput", "StickDeadZone", SDK_FIELD(USFXCameraInput, StickDeadZone));
    CheckLe3Dialogue();
    CheckField("Engine.Engine", "GamePlayers", SDK_FIELD(UEngine, GamePlayers));
    CheckField("Engine.Player", "Actor", SDK_FIELD(UPlayer, Actor));
    CheckField("Engine.Controller", "Pawn", SDK_FIELD(AController, Pawn));
    CheckField("Engine.Actor", "WorldInfo", SDK_FIELD(AActor, WorldInfo));
    CheckField("Engine.Actor", "Location", SDK_FIELD(AActor, location));
    CheckField("Engine.Pawn", "Weapon", SDK_FIELD(APawn, Weapon));
    CheckField("Engine.Weapon", "Mesh", SDK_FIELD(AWeapon, Mesh));
    CheckField("SFXGame.SFXWeapon", "MuzzleSocketName", SDK_FIELD(ASFXWeapon, MuzzleSocketName));
    CheckField("SFXGame.SFXWeapon_NativeBase", "bIsZoomed", SDK_BOOL(ASFXWeapon_NativeBase, bIsZoomed));
    CheckField("SFXGame.BioPlayerController", "GameModeManager2", SDK_FIELD(ABioPlayerController, GameModeManager2));
    CheckField("SFXGame.SFXGameModeManager", "CurrentMode", SDK_FIELD(USFXGameModeManager, CurrentMode));
    CheckField("SFXGame.SFXGameModeManager", "GameModes", SDK_FIELD(USFXGameModeManager, GameModes));
    CheckField("SFXGame.SFXWeapon", "MagSize", SDK_FIELD(ASFXWeapon, MagSize));
    CheckField("SFXGame.SFXWeapon", "AmmoUsedCount", SDK_FIELD(ASFXWeapon, AmmoUsedCount));
    CheckField("SFXGame.SFXWeapon", "CurrentSpareAmmo", SDK_FIELD(ASFXWeapon, CurrentSpareAmmo));
    // game/Reload names them where ME3 declares them
    CheckField("SFXGame.SFXWeapon_NativeBase", "AmmoUsedCount", SDK_FIELD(ASFXWeapon_NativeBase, AmmoUsedCount));
    CheckField("SFXGame.SFXWeapon_NativeBase", "CurrentSpareAmmo", SDK_FIELD(ASFXWeapon_NativeBase, CurrentSpareAmmo));
    CheckField("SFXGame.SFXWeapon", "bWeaponCanBeReloaded", SDK_BOOL(ASFXWeapon, bWeaponCanBeReloaded));
    CheckField("SFXGame.SFXWeapon", "bInfiniteAmmo", SDK_BOOL(ASFXWeapon, bInfiniteAmmo));
    CheckField("Engine.Inventory", "InvManager", SDK_FIELD(AInventory, InvManager));
    CheckField("Engine.InventoryManager", "PendingWeapon", SDK_FIELD(AInventoryManager, PendingWeapon));
    CheckField("Engine.Actor", "Instigator", SDK_FIELD(AActor, Instigator));
    CheckField("Engine.Pawn", "Controller", SDK_FIELD(APawn, Controller));
    CheckField("Engine.Pawn", "InvManager", SDK_FIELD(APawn, InvManager));
    CheckField("Engine.Pawn", "Health", SDK_FIELD(APawn, Health));
    CheckField("Engine.Pawn", "HealthMax", SDK_FIELD(APawn, HealthMax));
    CheckField("Engine.InventoryManager", "InventoryChain", SDK_FIELD(AInventoryManager, InventoryChain));
    CheckField("Engine.Inventory", "Inventory", SDK_FIELD(AInventory, Inventory));
    CheckField("SFXGame.BioWorldInfo", "m_playerSquad", SDK_FIELD(ABioWorldInfo, m_playerSquad));
    CheckField("SFXGame.BioBaseSquad", "Members", SDK_FIELD(ABioBaseSquad, Members));
    CheckField("SFXGame.BioPawn", "PowerManager", SDK_FIELD(ABioPawn, PowerManager));
    CheckField("SFXGame.SFXPowerManager", "Powers", SDK_FIELD(USFXPowerManager, Powers));
    CheckField("SFXGame.SFXPowerCustomActionBase", "PowerName", SDK_FIELD(USFXPowerCustomActionBase, PowerName));
    CheckField("SFXGame.SFXPowerCustomActionBase", "Rank", SDK_FIELD(USFXPowerCustomActionBase, Rank));
    CheckField("SFXGame.SFXPowerCustomActionBase", "CurrentCooldownTime", SDK_FIELD(USFXPowerCustomActionBase, CurrentCooldownTime));
    CheckField("SFXGame.SFXPowerCustomActionBase", "TotalCooldownTime", SDK_FIELD(USFXPowerCustomActionBase, TotalCooldownTime));
    CheckField("SFXGame.SFXPowerCustomActionBase", "PowerType", SDK_FIELD(USFXPowerCustomActionBase, PowerType));
    CheckField("SFXGame.SFXPowerCustomActionBase", "DisplayInHUD", SDK_BOOL(USFXPowerCustomActionBase, DisplayInHUD));
}

void CheckPaths() {
    CheckElement("SFXGame.SFXGameModeBase", "Bindings", sizeof(FKeyBind));
    CheckPath("SFXGame.SFXGameModeBase", {"Bindings", "Command"}, SDK_AT(offsetof(FKeyBind, Command), sizeof(FKeyBind::Command)));
    CheckPath("SFXGame.SFXGameModeBase", {"Bindings", "Name"}, SDK_AT(offsetof(FKeyBind, Name), sizeof(FKeyBind::Name)));
    CheckElement("SFXGame.SFXGameModeBase", "LocalizedBindings", sizeof(FKeyBind));
    CheckPath("SFXGame.SFXGameModeBase", {"LocalizedBindings", "Command"}, SDK_AT(offsetof(FKeyBind, Command), sizeof(FKeyBind::Command)));
    CheckPath("SFXGame.SFXGameModeBase", {"LocalizedBindings", "Name"}, SDK_AT(offsetof(FKeyBind, Name), sizeof(FKeyBind::Name)));
    CheckPath("SFXGame.SFXPowerCustomActionBase", {"MinimumRange", "CurrentValue"},
        SDK_AT(offsetof(USFXPowerCustomActionBase, MinimumRange) + offsetof(FPowerData, CurrentValue), sizeof(FPowerData::CurrentValue)));
    CheckPath("SFXGame.SFXPowerCustomActionBase", {"MaximumRange", "CurrentValue"},
        SDK_AT(offsetof(USFXPowerCustomActionBase, MaximumRange) + offsetof(FPowerData, CurrentValue), sizeof(FPowerData::CurrentValue)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "Location"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, location), sizeof(FTPOV::location)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "Rotation"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, Rotation), sizeof(FTPOV::Rotation)));
    CheckPath("Engine.Camera", {"CameraCache", "POV", "FOV"},
        SDK_AT(offsetof(ACamera, CameraCache) + offsetof(FTCameraCache, POV) + offsetof(FTPOV, FOV), sizeof(FTPOV::FOV)));
    CheckPath("SFXGame.SFXWeapon", {"MagSize", "Value"},
        SDK_AT(offsetof(ASFXWeapon, MagSize) + offsetof(FScaledFloat, Value), sizeof(FScaledFloat::Value)));
}

void CheckSkeleton() {
    skeleton::TryStartup();
    skeleton::Fields const f = skeleton::ResolvedFields();
    struct Item { char const* what; long long ours; long long native; };
    // Original 60AEF0C4 code, not SDK-derived constants: research/le3-body-pose.md.
    Item const items[] = {
        {"SkeletalMeshComponent.SkeletalMesh", f.skeletalMesh, 0x258},
        {"SkeletalMesh.RefSkeleton", f.refSkeleton, 0xB4},
        {"SkeletalMeshComponent.SpaceBases", f.spaceBases, 0x2B4},
    };
    for (Item const& item : items) {
        Add("skeleton", item.what, item.ours == item.native, Hex(item.ours), std::format("ME3's natives {}", Hex(item.native)));
    }
}

// Metadata only: collect private native targets for ownership and squad dispatch audits.
// Allocator cross-free success is not proof that these natives return/copy owned strings.
void CheckNativeTarget(char const* path) {
    CheckFunction(path);
    Guard("native", path, [path] {
        auto* const function = R().FindFunction(path);
        std::string_view const full(path);
        auto const name = Widen(full.substr(full.rfind('.') + 1));
        auto* const sdk = SdkFunction(name.c_str(), std::format("Function {}", path));
        auto const base = exe::Base();
        auto const image = exe::LiveImage();
        auto const describe = [&](void* address) {
            auto const value = reinterpret_cast<std::uintptr_t>(address);
            if (address == nullptr || base == 0 || value < base || value - base < image.textRva ||
                value - base - image.textRva >= image.text.size()) return std::string("outside executable text");
            return std::format("RVA 0x{:X}", value - base);
        };
        void* const ours = function != nullptr ? R().NativeFunc(function) : nullptr;
        void* const theirs = sdk != nullptr ? sdk->Func : nullptr;
        auto const where = describe(ours);
        Add("native", path, ours != nullptr && ours == theirs && where.starts_with("RVA "), where, describe(theirs));
    });
}

void CheckSaveDescriptor(char const* function, char const* parameter) {
    struct Member { char const* name; Sdk layout; ue3::Kind kind; };
    Member const members[] = {
        {"Career", SDK_FIELD(FSFXSaveDescriptor, Career), ue3::Kind::Str},
        {"SaveName", SDK_FIELD(FSFXSaveDescriptor, SaveName), ue3::Kind::Str},
        {"Index", SDK_FIELD(FSFXSaveDescriptor, Index), ue3::Kind::Int},
        {"Type", SDK_FIELD(FSFXSaveDescriptor, Type), ue3::Kind::Byte},
    };
    for (auto const& member : members) {
        auto const what = std::format("{}({}.{})", function, parameter, member.name);
        Guard("param-path", what, [&] {
            auto const descriptor = R().PropertyOf(R().FindFunction(function), parameter);
            auto* const owner = descriptor && descriptor->kind == ue3::Kind::Struct ? R().StructOf(*descriptor) : nullptr;
            auto const field = owner != nullptr ? R().PropertyOf(owner, member.name) : std::nullopt;
            Add("param-path", what, field && field->kind == member.kind && Matches(*field, field->offset, member.layout),
                Ours(field), SdkText(member.layout));
        });
    }
}

void CheckFunctions() {
    // game/Sights and game/ScopeParts (research/le2-vrik.md "The sights on the sequels"): the sight line's trace, a scope's
    // aimed fire, the lens part hidden
    CheckFunction("Engine.Actor.Trace");
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", HitLocation);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceEnd);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", TraceStart);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", bTraceActors);
    PARAM(AActor_execTrace_Parms, "Engine.Actor.Trace", ReturnValue);
    CheckFunction("SFXGame.SFXWeapon.GetPlayerAimError");
    PARAM(ASFXWeapon_execGetPlayerAimError_Parms, "SFXGame.SFXWeapon.GetPlayerAimError", Accuracy);
    PARAM(ASFXWeapon_execGetPlayerAimError_Parms, "SFXGame.SFXWeapon.GetPlayerAimError", ReturnValue);
    CheckFunction("SFXGame.BioPlayerController.SwitchWeapon");  // game/Holsters' sequel draw (a holster grab, `mevr weapon <family>`)
    PARAM(ABioPlayerController_eventSwitchWeapon_Parms, "SFXGame.BioPlayerController.SwitchWeapon", oWpn);
    CheckFunction("SFXGame.SFXGameModeDefault.TryHolster");     // ... resolved, never called on ME3 (it only leaves cover)
    CheckFunction("SFXGame.SFXInventoryManager.SetCurrentWeapon");  // ... ME3's holster: the put-down into no weapon (le3PutAway)
    CheckParam("SFXGame.SFXInventoryManager.SetCurrentWeapon", "DesiredWeapon", SDK_AT(0, 8));
    CheckFunction("Core.Object.GetStateName");                  // ... the weapon's state: mid-switch
    CheckFunction("SFXGame.BioPlayerInput.IsCombatEnabled");    // ... only holsters draw: answered false with the weapon put away
    CheckParam("SFXGame.BioPlayerInput.IsCombatEnabled", "ReturnValue", SDK_AT(0, 4));
    CheckFunction("SFXGame.SFXGameModeBase.FireWeapon");
    CheckFunction("SFXGame.SFXGameModeDefault.TightAim");
    CheckFunction("SFXGame.SFXGameModeDefault.TryReload");
    CheckFunction("SFXGame.SFXGameModeDefault.NextWeapon");
    CheckFunction("SFXGame.SFXGameModeDefault.PrevWeapon");
    PARAM(UObject_execGetStateName_Parms, "Core.Object.GetStateName", ReturnValue);
    CheckFunction("Engine.SkeletalMeshComponent.HideBone");
    PARAM(USkeletalMeshComponent_execHideBone_Parms, "Engine.SkeletalMeshComponent.HideBone", BoneIndex);
    PARAM(USkeletalMeshComponent_execHideBone_Parms, "Engine.SkeletalMeshComponent.HideBone", PhysBodyOption);
    CheckFunction("Engine.SkeletalMeshComponent.UnHideBone");
    PARAM(USkeletalMeshComponent_execUnHideBone_Parms, "Engine.SkeletalMeshComponent.UnHideBone", BoneIndex);
    CheckFunction("Engine.SkeletalMeshComponent.IsBoneHidden");
    PARAM(USkeletalMeshComponent_execIsBoneHidden_Parms, "Engine.SkeletalMeshComponent.IsBoneHidden", BoneIndex);
    PARAM(USkeletalMeshComponent_execIsBoneHidden_Parms, "Engine.SkeletalMeshComponent.IsBoneHidden", ReturnValue);
    CheckFunction("Engine.SkeletalMeshComponent.GetBoneLocation");
    CheckFunction("SFXGame.BioWorldInfo.PauseGame");
    PARAM(ABioWorldInfo_execPauseGame_Parms, "SFXGame.BioWorldInfo.PauseGame", bPause);
    PARMS(ABioWorldInfo_execPauseGame_Parms, "SFXGame.BioWorldInfo.PauseGame");
    CheckFunction("Engine.SkeletalMeshComponent.MatchRefBone");

    // L3-3: exact script/native entry points used by the shared pose/aim adapter.
    CheckFunction("SFXGame.BioPlayerController.GetAdjustedAimFor");
    CheckFunction("SFXGame.SFXWeapon.GetAdjustedAim");
    CheckFunction("SFXGame.BioPawn.GetWeaponStartTraceLocation");
    CheckFunction("SFXGame.SFXWeapon.GetMuzzleLoc");
    CheckFunction("Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation");

    CheckFunction("SFXGame.SFXGameModeGalaxy.ScanSystem");
    CheckFunction("SFXGame.SFXGameModeOrbital.StartScanning");
    CheckFunction("SFXGame.SFXGameModeOrbital.StopScanning");
    CheckFunction("SFXGame.SFXGameModeOrbital.LaunchProbe");
    CheckNativeTarget("SFXGame.BioCameraBehaviorGalaxy.ScanSystem");
    // ME3's reflected UpdateSelection has no parameters. Identify its thunk before
    // adapting LE1's delta-time body hook; a matching name does not prove its ABI.
    CheckNativeTarget("SFXGame.BioPlayerSelection.UpdateSelection");
    CheckParmsSize("SFXGame.BioPlayerSelection.UpdateSelection", 0);
    CheckNativeTarget("SFXGame.BioPlayerSelection.FindCurrentSelectionTarget");
    CheckParmsSize("SFXGame.BioPlayerSelection.FindCurrentSelectionTarget", 0);
    CheckField("Engine.Actor", "Rotation", SDK_FIELD(AActor, Rotation));
    CheckFunction("Engine.Pawn.ShouldCrouch");
    CheckFunction("Engine.Pawn.IsDead");
    CheckFunction("SFXGame.SFXGameModeDefault.StormOn");
    CheckFunction("SFXGame.SFXGameModeDefault.StormOff");
    CheckFunction("SFXGame.BioPlayerController.IsStormDisabled");
    CheckFunction("Engine.Pawn.GetMaxSpeed");  // game/Sprint: the run in effect
    CheckFunction("GFxUI.GFxValue.GetObject");
    CheckNativeTarget("GFxUI.GFxValue.GetDisplayMatrix");
    CheckNativeTarget("SFXGame.BioConversationController.GetReplyParaphraseText");
    CheckNativeTarget("SFXGame.SFXGUIMovie.GetUIString");
    CheckFunction("GFxUI.GFxValue.GetDisplayMatrix");
    CheckFunction("SFXGame.SFXPowerCustomActionBase.IsValidTarget");
    CheckFunction("Engine.Actor.SetRotation");
    CheckFunction("Engine.PlayerController.IsMoveInputIgnored");
    CheckFunction("SFXGame.SFXWeapon.GetMagazineSize");
    CheckFunction("SFXGame.SFXWeapon.GetAmmoCountInMagazine");
    CheckFunction("SFXGame.SFXWeapon.CanReload");
    CheckFunction("SFXGame.BioPlayerController.IsReloading");
    CheckFunction("SFXGame.SFXWeapon.GetCurrentSpareAmmo");
    CheckFunction("Engine.PlayerController.ConsoleCommand");
    CheckNativeTarget("SFXGame.SFXEngine.QueueSaveGameCommand");
    CheckNativeTarget("SFXGame.SFXEngine.GetCurrentSaveDescriptor");
    CheckFunction("SFXGame.BioPlayerController.OrderWeaponSwitch");
    CheckFunction("SFXGame.BioPlayerController.HenchmanOrderFollow");
    CheckFunction("SFXGame.BioPlayerController.HenchmanOrderHoldPosition");
    CheckFunction("SFXGame.BioPlayerController.HenchmanOrderAttackTarget");
    CheckFunction("SFXGame.SFXPowerCustomActionBase.IsEnabled");
    CheckFunction("SFXGame.SFXPowerCustomActionBase.CanUsePower");
    CheckFunction("SFXGame.SFXAI_NativeBase.IsEnabled");
    CheckNativeTarget("SFXGame.BioPlayerController.SquadOrderUsePower");
    CheckNativeTarget("SFXGame.BioPawn.GetActorGameName");
}

void CheckParams() {
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", BoneName);
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", Space);
    PARAM(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation", ReturnValue);
    PARMS(USkeletalMeshComponent_execGetBoneLocation_Parms, "Engine.SkeletalMeshComponent.GetBoneLocation");
    PARAM(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone", BoneName);
    PARAM(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone", ReturnValue);
    PARMS(USkeletalMeshComponent_execMatchRefBone_Parms, "Engine.SkeletalMeshComponent.MatchRefBone");

    PARAM(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor", W);
    PARAM(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor", StartFireLoc);
    PARAM(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor", ReturnValue);
    PARMS(ABioPlayerController_execGetAdjustedAimFor_Parms, "SFXGame.BioPlayerController.GetAdjustedAimFor");
    PARAM(ASFXWeapon_execGetAdjustedAim_Parms, "SFXGame.SFXWeapon.GetAdjustedAim", StartFireLoc);
    PARAM(ASFXWeapon_execGetAdjustedAim_Parms, "SFXGame.SFXWeapon.GetAdjustedAim", ReturnValue);
    PARMS(ASFXWeapon_execGetAdjustedAim_Parms, "SFXGame.SFXWeapon.GetAdjustedAim");
    PARAM(ABioPawn_execGetWeaponStartTraceLocation_Parms, "SFXGame.BioPawn.GetWeaponStartTraceLocation", CurrentWeapon);
    PARAM(ABioPawn_execGetWeaponStartTraceLocation_Parms, "SFXGame.BioPawn.GetWeaponStartTraceLocation", ReturnValue);
    PARMS(ABioPawn_execGetWeaponStartTraceLocation_Parms, "SFXGame.BioPawn.GetWeaponStartTraceLocation");
    PARAM(ASFXWeapon_execGetMuzzleLoc_Parms, "SFXGame.SFXWeapon.GetMuzzleLoc", ReturnValue);
    PARMS(ASFXWeapon_execGetMuzzleLoc_Parms, "SFXGame.SFXWeapon.GetMuzzleLoc");
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", InSocketName);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutLocation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", OutRotation);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", Space);
    PARAM(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation", ReturnValue);
    PARMS(USkeletalMeshComponent_execGetSocketWorldLocationAndRotation_Parms, "Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation");

    CheckParmsSize("SFXGame.SFXGameModeGalaxy.ScanSystem", 0);
    CheckParmsSize("SFXGame.SFXGameModeOrbital.StartScanning", 0);
    CheckParmsSize("SFXGame.SFXGameModeOrbital.StopScanning", 0);
    CheckParmsSize("SFXGame.SFXGameModeOrbital.LaunchProbe", 0);
    CheckParmsSize("SFXGame.BioCameraBehaviorGalaxy.ScanSystem", 0);
    PARAM(APawn_execShouldCrouch_Parms, "Engine.Pawn.ShouldCrouch", bCrouch);
    PARMS(APawn_execShouldCrouch_Parms, "Engine.Pawn.ShouldCrouch");
    PARAM(APawn_execIsDead_Parms, "Engine.Pawn.IsDead", ReturnValue);
    PARMS(APawn_execIsDead_Parms, "Engine.Pawn.IsDead");
    PARAM(ABioPlayerController_execIsStormDisabled_Parms, "SFXGame.BioPlayerController.IsStormDisabled", ReturnValue);
    PARMS(ABioPlayerController_execIsStormDisabled_Parms, "SFXGame.BioPlayerController.IsStormDisabled");
    // Empty C++ parameter structs have sizeof 1; the reflected script ABI has zero parameters.
    CheckParmsSize("SFXGame.SFXGameModeDefault.StormOn", 0);
    CheckParmsSize("SFXGame.SFXGameModeDefault.StormOff", 0);
    PARAM(UGFxValue_execGetObject_Parms, "GFxUI.GFxValue.GetObject", member);
    PARAM(UGFxValue_execGetObject_Parms, "GFxUI.GFxValue.GetObject", Type);
    PARAM(UGFxValue_execGetObject_Parms, "GFxUI.GFxValue.GetObject", ReturnValue);
    PARMS(UGFxValue_execGetObject_Parms, "GFxUI.GFxValue.GetObject");
    PARAM(UGFxValue_execGetDisplayMatrix_Parms, "GFxUI.GFxValue.GetDisplayMatrix", ReturnValue);
    PARMS(UGFxValue_execGetDisplayMatrix_Parms, "GFxUI.GFxValue.GetDisplayMatrix");
    PARAM(USFXPowerCustomActionBase_execIsValidTarget_Parms, "SFXGame.SFXPowerCustomActionBase.IsValidTarget", oTarget);
    PARAM(USFXPowerCustomActionBase_execIsValidTarget_Parms, "SFXGame.SFXPowerCustomActionBase.IsValidTarget", ReturnValue);
    PARMS(USFXPowerCustomActionBase_execIsValidTarget_Parms, "SFXGame.SFXPowerCustomActionBase.IsValidTarget");
    PARAM(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation", NewRotation);
    PARAM(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation", ReturnValue);
    PARMS(AActor_execSetRotation_Parms, "Engine.Actor.SetRotation");
    PARAM(APlayerController_eventIsMoveInputIgnored_Parms, "Engine.PlayerController.IsMoveInputIgnored", ReturnValue);
    PARMS(APlayerController_eventIsMoveInputIgnored_Parms, "Engine.PlayerController.IsMoveInputIgnored");
    PARAM(ABioPlayerController_execOrderWeaponSwitch_Parms, "SFXGame.BioPlayerController.OrderWeaponSwitch", oOrderedPawn);
    PARAM(ABioPlayerController_execOrderWeaponSwitch_Parms, "SFXGame.BioPlayerController.OrderWeaponSwitch", Wpn);
    PARAM(ABioPlayerController_execOrderWeaponSwitch_Parms, "SFXGame.BioPlayerController.OrderWeaponSwitch", eInputMethod);
    PARMS(ABioPlayerController_execOrderWeaponSwitch_Parms, "SFXGame.BioPlayerController.OrderWeaponSwitch");
    PARAM(ABioPlayerController_eventHenchmanOrderFollow_Parms, "SFXGame.BioPlayerController.HenchmanOrderFollow", oHenchman);
    PARAM(ABioPlayerController_eventHenchmanOrderFollow_Parms, "SFXGame.BioPlayerController.HenchmanOrderFollow", eInputMethod);
    PARMS(ABioPlayerController_eventHenchmanOrderFollow_Parms, "SFXGame.BioPlayerController.HenchmanOrderFollow");
    PARAM(ABioPlayerController_eventHenchmanOrderHoldPosition_Parms, "SFXGame.BioPlayerController.HenchmanOrderHoldPosition", oHenchman);
    PARAM(ABioPlayerController_eventHenchmanOrderHoldPosition_Parms, "SFXGame.BioPlayerController.HenchmanOrderHoldPosition", vLocation);
    PARAM(ABioPlayerController_eventHenchmanOrderHoldPosition_Parms, "SFXGame.BioPlayerController.HenchmanOrderHoldPosition", eInputMethod);
    PARMS(ABioPlayerController_eventHenchmanOrderHoldPosition_Parms, "SFXGame.BioPlayerController.HenchmanOrderHoldPosition");
    // The move order's marker before the hold order (game/Le2Squad.hpp PlaceMoveIndicator, found on ME2).
    CheckFunction("SFXGame.BioPlayerController.UpdateMoveToIndicator");
    PARAM(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator", pSquadPawn);
    PARAM(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator", vMoveToPosition);
    PARAM(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator", ReturnValue);
    PARMS(ABioPlayerController_execUpdateMoveToIndicator_Parms, "SFXGame.BioPlayerController.UpdateMoveToIndicator");
    PARAM(ABioPlayerController_eventHenchmanOrderAttackTarget_Parms, "SFXGame.BioPlayerController.HenchmanOrderAttackTarget", oHenchman);
    PARAM(ABioPlayerController_eventHenchmanOrderAttackTarget_Parms, "SFXGame.BioPlayerController.HenchmanOrderAttackTarget", oTargetActor);
    PARAM(ABioPlayerController_eventHenchmanOrderAttackTarget_Parms, "SFXGame.BioPlayerController.HenchmanOrderAttackTarget", eInputMethod);
    PARMS(ABioPlayerController_eventHenchmanOrderAttackTarget_Parms, "SFXGame.BioPlayerController.HenchmanOrderAttackTarget");
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", nmPower);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", pPawn);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", nQueue);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", bShowIndicator);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", pDesiredSelectionTarget);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", eInputMethod);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", pDesiredTarget);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", vDesiredTargetLocation);
    PARAM(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower", ReturnValue);
    PARMS(ABioPlayerController_execSquadOrderUsePower_Parms, "SFXGame.BioPlayerController.SquadOrderUsePower");
    PARAM(ABioPawn_execGetActorGameName_Parms, "SFXGame.BioPawn.GetActorGameName", ReturnValue);
    PARMS(ABioPawn_execGetActorGameName_Parms, "SFXGame.BioPawn.GetActorGameName");
    PARAM(USFXPowerCustomActionBase_execIsEnabled_Parms, "SFXGame.SFXPowerCustomActionBase.IsEnabled", ReturnValue);
    PARMS(USFXPowerCustomActionBase_execIsEnabled_Parms, "SFXGame.SFXPowerCustomActionBase.IsEnabled");
    PARAM(USFXPowerCustomActionBase_eventCanUsePower_Parms, "SFXGame.SFXPowerCustomActionBase.CanUsePower", oTarget);
    PARAM(USFXPowerCustomActionBase_eventCanUsePower_Parms, "SFXGame.SFXPowerCustomActionBase.CanUsePower", ReturnValue);
    PARMS(USFXPowerCustomActionBase_eventCanUsePower_Parms, "SFXGame.SFXPowerCustomActionBase.CanUsePower");
    PARAM(ASFXAI_NativeBase_eventIsEnabled_Parms, "SFXGame.SFXAI_NativeBase.IsEnabled", ReturnValue);
    PARMS(ASFXAI_NativeBase_eventIsEnabled_Parms, "SFXGame.SFXAI_NativeBase.IsEnabled");
    PARAM(ASFXWeapon_execGetMagazineSize_Parms, "SFXGame.SFXWeapon.GetMagazineSize", ReturnValue);
    PARMS(ASFXWeapon_execGetMagazineSize_Parms, "SFXGame.SFXWeapon.GetMagazineSize");
    PARAM(ASFXWeapon_execGetAmmoCountInMagazine_Parms, "SFXGame.SFXWeapon.GetAmmoCountInMagazine", ReturnValue);
    PARMS(ASFXWeapon_execGetAmmoCountInMagazine_Parms, "SFXGame.SFXWeapon.GetAmmoCountInMagazine");
    PARAM(ASFXWeapon_execCanReload_Parms, "SFXGame.SFXWeapon.CanReload", ReturnValue);
    PARMS(ASFXWeapon_execCanReload_Parms, "SFXGame.SFXWeapon.CanReload");
    PARAM(ABioPlayerController_execIsReloading_Parms, "SFXGame.BioPlayerController.IsReloading", ReturnValue);
    PARMS(ABioPlayerController_execIsReloading_Parms, "SFXGame.BioPlayerController.IsReloading");
    PARAM(ASFXWeapon_execGetCurrentSpareAmmo_Parms, "SFXGame.SFXWeapon.GetCurrentSpareAmmo", ReturnValue);
    PARMS(ASFXWeapon_execGetCurrentSpareAmmo_Parms, "SFXGame.SFXWeapon.GetCurrentSpareAmmo");
    PARAM(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand", Action);
    PARAM(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand", SaveDescriptor);
    PARAM(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand", Callback);
    PARMS(USFXEngine_execQueueSaveGameCommand_Parms, "SFXGame.SFXEngine.QueueSaveGameCommand");
    PARAM(USFXEngine_execGetCurrentSaveDescriptor_Parms, "SFXGame.SFXEngine.GetCurrentSaveDescriptor", ReturnValue);
    PARMS(USFXEngine_execGetCurrentSaveDescriptor_Parms, "SFXGame.SFXEngine.GetCurrentSaveDescriptor");
    CheckSaveDescriptor("SFXGame.SFXEngine.QueueSaveGameCommand", "SaveDescriptor");
    CheckSaveDescriptor("SFXGame.SFXEngine.GetCurrentSaveDescriptor", "ReturnValue");
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", Command);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", bWriteToLog);
    PARAM(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand", ReturnValue);
    PARMS(APlayerController_execConsoleCommand_Parms, "Engine.PlayerController.ConsoleCommand");
}

#endif  // per-game metadata tables

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

bool Run(hook::Context& context) {
    // LExSDKv2's resolver for its BUILTIN_*_RVA (the reference side only; FromOffset never calls the interface).
    ::LESDK::Initializer init{reinterpret_cast<ISharedProxyInterface*>(context.Spi()), MEVR_GAME_NAME_A "MEVRreference"};
    // Its module snapshot can fail while another DLL loads and leave the base null (engine/Legacy's Init, 2026-11-14).
    init.ModuleBase = reinterpret_cast<void*>(context.ModuleBase());
    gRows.clear();
    if (!ResolveSdkGlobals(init)) {
        log::Warn("reflect: self-check skipped: LExSDKv2's globals (the reference) didn't resolve");
        Add("global", "LExSDKv2", false, ue3::runtime::Status(), "its globals didn't resolve");
        Write(0, 1, 0);
        return false;
    }
    if (!ue3::runtime::Up()) {
        log::Warn("reflect: self-check skipped: {}", ue3::runtime::Status());
        Add("global", "layer", false, ue3::runtime::Status(), "");
        Write(0, 1, 0);
        return false;
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
#if defined(MEVR_GAME_LE3)
    CheckLe3PhysicalCover();
    CheckLe3EngineAccess();
    CheckLe3ViewLevel();
    CheckLe3OmniTool();
    CheckLe3WorldUi();
    CheckLe3ScreenReticle();
#endif
#if defined(MEVR_GAME_LE2)
    CheckTraversal();
    CheckThespian();
    CheckLe2Squad();
#endif
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
    return agree > 0 && mismatches == 0;
}

#elif defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)

bool Run(hook::Context&) {
    log::Info("reflect: no LExSDKv2 cross-check in this build (a players' build has no LExSDKv2 in it); our reflection layer "
              "requires a supported game layout and each feature stands down with a log line if a lookup fails");
    return false;
}

#else

bool Run(hook::Context&) { return false; }

#endif

} // namespace mevr::engine::reflect_check
