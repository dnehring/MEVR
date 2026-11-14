// SPDX-License-Identifier: MIT
#include "engine/Le2DiagnosticObservation.hpp"

#if defined(MEVR_GAME_LE2)
#include "engine/GameAdapterSdk.hpp"
#include "engine/Legacy.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <type_traits>

namespace mevr::engine::le2_diagnostic {
namespace {
// Same pending-kill/template rules as Legacy; these are not incarnation IDs.
constexpr std::uint64_t kPendingKill = 0x2000000000000000ull;
constexpr std::uint64_t kTemplateFlags = 0x200 | 0x400;
constexpr unsigned kMaxObjects = 64 * 1024 * 1024;
constexpr unsigned kMaxPlayers = 64;
constexpr unsigned kMaxClassDepth = 64;
constexpr unsigned kNameBytes = 128;
constexpr unsigned kMaxOwnerFields = 4096;
constexpr unsigned kMaxFunctionFields = 256;
constexpr unsigned kMaxParameters = 16;
constexpr std::uint64_t kParm = 0x80;
// Declaration comparison only: no SDK parameter instance is constructed.
static_assert(sizeof(APlayerController_execConsoleCommand_Parms) == 0x24);
static_assert(offsetof(APlayerController_execConsoleCommand_Parms, Command) == 0);
static_assert(offsetof(APlayerController_execConsoleCommand_Parms, bWriteToLog) == 0x10);
static_assert(offsetof(APlayerController_execConsoleCommand_Parms, ReturnValue) == 0x14);

struct ObjectSample {
    bool available{};
    char const* reason{"not-observed"};
    std::uintptr_t address{};
    int index{};
    char className[kNameBytes * 2]{};
};
struct ParameterSample {
    ObjectSample property;
    char name[kNameBytes]{};
    int offset{}, elementSize{}, arrayDim{};
    std::uint64_t flags{};
    bool hasBoolMask{};
    std::uint32_t boolMask{};
};
struct ConsoleSample {
    ObjectSample function;
    char const* reason{"player-controller-unavailable"};
    std::uint32_t functionFlags{};
    unsigned nativeIndex{}, numParms{}, parmsSize{};
    int returnOffset{};
    unsigned parameterCount{};
    ParameterSample parameters[kMaxParameters]{};
    bool parametersComplete{}, layoutMatches{};
};
struct Sample {
    bool tableAvailable{};
    char const* tableReason{"not-observed"};
    unsigned objectCount{};
    ObjectSample world, engine, localPlayer, player, pawn;
    bool mapAvailable{};
    char const* mapReason{"world-unavailable"};
    char map[kNameBytes]{};
    bool positionAvailable{};
    char const* positionReason{"pawn-unavailable"};
    float x{}, y{}, z{};
    ConsoleSample console;
};
static_assert(std::is_trivially_destructible_v<Sample>);

struct Table {
    UObject* const* data{};
    unsigned count{};
    bool Listed(UObject const* object, bool instance = false) const {
        if (object == nullptr) return false;
        int const index = object->ObjectInternalInteger;
        if (index < 0 || static_cast<unsigned>(index) >= count || data[index] != object) return false;
        auto const flags = static_cast<std::uint64_t>(object->ObjectFlags);
        return (flags & kPendingKill) == 0 && (!instance || (flags & kTemplateFlags) == 0);
    }
};

// Read existing names only, using the SDK accessor (no SFXName construction,
// lookup/create, FString, StaticClass, generated wrapper or engine allocation).
// Class/package names used by this slice are plain ASCII, non-instanced names.
// Unknown encoding/number or missing termination makes that label unavailable.
bool NameText(SFXName const& name, char (&out)[kNameBytes]) {
    if (name.Number != 0 || SFXName::GBioNamePools == nullptr ||
        SFXName::GBioNamePools[name.Chunk] == nullptr) return false;
    auto const* entry = name.GetEntry();
    if (entry == nullptr || entry->IsSuppressed() || entry->IsUnicode()) return false;
    for (unsigned i = 0; i < kNameBytes; ++i) {
        auto const c = static_cast<unsigned char>(entry->AnsiName[i]);
        if (c == 0) { out[i] = 0; return i != 0; }
        if (c < 32 || c > 126 || i + 1 == kNameBytes) return false;
        out[i] = static_cast<char>(c);
    }
    return false;
}
bool NameIs(UObject const* object, char const* expected) {
    char name[kNameBytes]{};
    return NameText(object->Name, name) && std::strcmp(name, expected) == 0;
}
bool Qualified(Table const& table, UObject const* object, char const* package, char const* name) {
    if (!table.Listed(object) || !table.Listed(object->Outer) || object->Outer->Outer != nullptr) return false;
    return NameIs(object, name) && NameIs(object->Outer, package);
}
bool ClassMetadata(Table const& table, UClass const* type) {
    if (!table.Listed(type) || !table.Listed(type->Class)) return false;
    auto const* meta = type->Class;
    return meta->Class == meta && Qualified(table, meta, "Core", "Class");
}
bool IsA(Table const& table, UObject const* object, char const* package, char const* name) {
    // No unbounded UObject::IsA or SDK class finder. Every superclass is listed
    // and has the self-typed Core.Class metaclass before its SuperField is read.
    UClass const* type = object->Class;
    for (unsigned depth = 0; type != nullptr && depth < kMaxClassDepth; ++depth) {
        if (!ClassMetadata(table, type)) return false;
        if (Qualified(table, type, package, name)) return true;
        type = static_cast<UClass const*>(type->SuperField);
    }
    return false;
}
bool ReadObject(Table const& table, UObject const* object, char const* package, char const* type,
                ObjectSample& out) {
    if (object == nullptr) { out.reason = "null-reference"; return false; }
    if (!table.Listed(object, true)) { out.reason = "not-a-live-nontemplate-object-slot"; return false; }
    if (!IsA(table, object, package, type)) { out.reason = "unexpected-or-invalid-class-chain"; return false; }
    char className[kNameBytes]{}, packageName[kNameBytes]{};
    auto const* cls = object->Class;
    if (!table.Listed(cls->Outer) || cls->Outer->Outer != nullptr ||
        !NameText(cls->Name, className) || !NameText(cls->Outer->Name, packageName)) {
        out.reason = "class-label-unavailable";
        return false;
    }
    auto const packageLength = std::strlen(packageName);
    std::memcpy(out.className, packageName, packageLength);
    out.className[packageLength] = '.';
    std::memcpy(out.className + packageLength + 1, className, std::strlen(className) + 1);
    out.address = reinterpret_cast<std::uintptr_t>(object);
    out.index = object->ObjectInternalInteger;
    out.available = true;
    out.reason = "sdk-structural-checks-passed";
    return true;
}

bool ExpectedParameter(ParameterSample const& p, char const* name, char const* type,
                       int offset, int size, std::uint64_t flags, std::uint32_t boolMask = 0) {
    return std::strcmp(p.name, name) == 0 && std::strcmp(p.property.className, type) == 0 &&
        p.offset == offset && p.elementSize == size && p.arrayDim == 1 && p.flags == flags &&
        (boolMask == 0 ? !p.hasBoolMask : p.hasBoolMask && p.boolMask == boolMask);
}
bool HasExpectedParameter(ConsoleSample const& sample, char const* name, char const* type,
                          int offset, int size, std::uint64_t flags, std::uint32_t boolMask = 0) {
    for (unsigned i = 0; i < sample.parameterCount; ++i)
        if (ExpectedParameter(sample.parameters[i], name, type, offset, size, flags, boolMask)) return true;
    return false;
}

void ReadConsole(Table const& table, APlayerController const* controller, ConsoleSample& out) {
    // Match Legacy's exact declaration owner, not a derived class's override.
    auto const* owner = controller->Class;
    out.reason = "player-controller-declaration-class-unavailable";
    unsigned depth = 0;
    while (owner != nullptr && depth++ < kMaxClassDepth) {
        if (!ClassMetadata(table, owner)) return;
        if (Qualified(table, owner, "Engine", "PlayerController")) break;
        owner = static_cast<UClass const*>(owner->SuperField);
    }
    if (owner == nullptr || !Qualified(table, owner, "Engine", "PlayerController")) return;
    UFunction const* function = nullptr;
    unsigned fields = 0;
    for (auto const* field = owner->Children; field != nullptr; field = field->Next) {
        out.reason = "owner-child-cycle-or-limit";
        if (++fields > kMaxOwnerFields) return;
        out.reason = "invalid-or-foreign-owner-child";
        if (!table.Listed(field, true) || !IsA(table, field, "Core", "Field") || field->Outer != owner) return;
        char name[kNameBytes]{};
        out.reason = "owner-child-name-unavailable";
        if (!NameText(field->Name, name)) return;
        if (std::strcmp(name, "ConsoleCommand") != 0) continue;
        out.reason = "duplicate-console-command-declaration";
        if (function != nullptr) return;
        out.reason = "console-command-is-not-an-exact-Core.Function";
        if (!ClassMetadata(table, field->Class) || !Qualified(table, field->Class, "Core", "Function")) return;
        function = static_cast<UFunction const*>(field);
    }
    out.reason = "console-command-declaration-missing";
    if (function == nullptr) return;
    out.reason = "console-command-function-unavailable";
    if (!ReadObject(table, function, "Core", "Function", out.function)) return;
    out.functionFlags = function->FunctionFlags;
    out.nativeIndex = static_cast<unsigned short>(function->iNative);
    out.numParms = function->NumParms;
    out.parmsSize = function->ParmsSize;
    out.returnOffset = function->ReturnValueOffset;

    fields = 0;
    for (auto const* field = function->Children; field != nullptr; field = field->Next) {
        out.reason = "function-child-cycle-or-limit";
        if (++fields > kMaxFunctionFields) return;
        out.reason = "invalid-or-foreign-function-child";
        if (!table.Listed(field, true) || !IsA(table, field, "Core", "Property") || field->Outer != function) return;
        auto const* property = static_cast<UProperty const*>(field);
        if ((property->PropertyFlags & kParm) == 0) continue; // locals do not belong to the call block
        out.reason = "parameter-count-limit";
        if (out.parameterCount == kMaxParameters) return;
        auto& p = out.parameters[out.parameterCount];
        out.reason = "parameter-identity-or-name-unavailable";
        if (!ReadObject(table, property, "Core", "Property", p.property) || !NameText(property->Name, p.name)) return;
        // Check counts/span without multiplication overflow or trusting signed offsets.
        p.arrayDim = property->ArrayDim;
        p.elementSize = property->ElementSize;
        p.offset = property->Offset;
        p.flags = property->PropertyFlags;
        out.reason = "invalid-parameter-storage-span";
        if (p.arrayDim <= 0 || p.arrayDim > 1024 || p.elementSize <= 0 || p.elementSize > 65535 ||
            p.offset < 0 || static_cast<unsigned>(p.offset) > out.parmsSize ||
            static_cast<std::uint64_t>(p.arrayDim) * p.elementSize > out.parmsSize - static_cast<unsigned>(p.offset)) return;
        out.reason = "duplicate-or-overlapping-parameter";
        for (unsigned i = 0; i < out.parameterCount; ++i) {
            auto const& prior = out.parameters[i];
            if (std::strcmp(prior.name, p.name) == 0 ||
                (p.offset < prior.offset + prior.arrayDim * prior.elementSize &&
                 prior.offset < p.offset + p.arrayDim * p.elementSize)) return;
        }
        if (Qualified(table, property->Class, "Core", "BoolProperty")) {
            p.hasBoolMask = true;
            p.boolMask = static_cast<UBoolProperty const*>(property)->BitMask;
            out.reason = "invalid-bool-storage-or-mask";
            if (p.elementSize != 4 || p.boolMask == 0 || (p.boolMask & (p.boolMask - 1)) != 0) return;
        }
        ++out.parameterCount;
    }
    out.parametersComplete = true;
    // These expected flags and offsets are the pinned SDK declaration, not a
    // capability grant. Scalar DWORD boolean comparison is exact (mask 1).
    out.layoutMatches = out.parmsSize == 0x24 && out.numParms == 3 && out.returnOffset == 0x14 &&
        out.parameterCount == 3 &&
        HasExpectedParameter(out, "Command", "Core.StrProperty", 0, 16, 0x400080) &&
        HasExpectedParameter(out, "bWriteToLog", "Core.BoolProperty", 0x10, 4, 0x90, 1) &&
        HasExpectedParameter(out, "ReturnValue", "Core.StrProperty", 0x14, 16, 0x400580);
    out.reason = out.layoutMatches ? "sdk-parameter-layout-matches-only" : "sdk-parameter-layout-mismatch";
}

// Entire engine-memory traversal runs under the existing SEH read boundary.
// Only POD/fixed buffers here: an access violation must not bypass destructors.
void Read(void* context) {
    auto& out = *static_cast<Sample*>(context);
    auto const* objects = UObject::GObjObjects;
    out.tableReason = "object-table-unavailable-or-invalid";
    if (objects == nullptr) return;
    unsigned const count = objects->Count(), capacity = objects->Capacity();
    if (count == 0 || count > kMaxObjects || capacity < count || capacity > kMaxObjects || objects->GetData() == nullptr) return;
    if (legacy::ObjectCount() != static_cast<std::int32_t>(count)) return;
    Table const table{objects->GetData(), count};
    out.tableAvailable = true;
    out.tableReason = "bounded-array-header-only";
    out.objectCount = count;

    auto const* world = static_cast<UWorld const*>(legacy::World());
    if (ReadObject(table, world, "Engine", "World", out.world)) {
        out.mapReason = "world-outer-not-a-validated-package";
        UObject const* package = world->Outer;
        if (table.Listed(package, true) && IsA(table, package, "Core", "Package") && package->Outer == nullptr) {
            out.mapAvailable = NameText(package->Name, out.map);
            out.mapReason = out.mapAvailable ? "world-outer-package-name" : "package-name-unavailable";
        }
    }

    auto const* engine = GEngine != nullptr ? *GEngine : nullptr;
    out.localPlayer.reason = out.player.reason = out.pawn.reason = "engine-unavailable";
    if (!ReadObject(table, engine, "Engine", "Engine", out.engine)) return;
    out.localPlayer.reason = out.player.reason = out.pawn.reason = "local-player-array-unavailable-or-invalid";
    auto const& players = engine->GamePlayers;
    if (players.Count() == 0 || players.Count() > kMaxPlayers || players.Capacity() < players.Count() ||
        players.Capacity() > kMaxPlayers || players.GetData() == nullptr) return;
    auto const* localPlayer = players.GetData()[0];
    out.player.reason = out.pawn.reason = "local-player-unavailable";
    if (!ReadObject(table, localPlayer, "Engine", "LocalPlayer", out.localPlayer)) return;
    auto const* controller = localPlayer->Actor;
    out.pawn.reason = "player-controller-unavailable";
    if (!ReadObject(table, controller, "Engine", "PlayerController", out.player)) return;
    // Legacy is reused as a comparison only after all of its unchecked links
    // above are validated. No cached controller survives the current sample.
    if (legacy::LocalPlayerController() != reinterpret_cast<ue3::Object const*>(controller)) {
        out.player = {};
        out.player.reason = "legacy-controller-disagrees";
        return;
    }
    ReadConsole(table, controller, out.console);
    auto const* pawn = controller->Pawn;
    if (!ReadObject(table, pawn, "Engine", "Pawn", out.pawn)) return;
    FVector const& position = adapter::Location(pawn);
    out.positionReason = "nonfinite-pawn-location";
    if (std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z)) {
        out.x = position.X; out.y = position.Y; out.z = position.Z;
        out.positionAvailable = true;
        out.positionReason = "finite-sdk-actor-location";
    }
}

json::Value ObjectJson(ObjectSample const& object, char const* failure) {
    auto out = json::Value::Object();
    out["available"] = failure == nullptr && object.available;
    out["reason"] = failure != nullptr ? failure : object.reason;
    if (failure == nullptr && object.available) {
        out["address"] = std::format("0x{:X}", object.address);
        out["index"] = object.index;
        out["class"] = object.className;
    }
    return out;
}
json::Value ConsoleJson(ConsoleSample const& sample, char const* failure) {
    auto out = json::Value::Object();
    bool const available = failure == nullptr && sample.function.available && sample.parametersComplete;
    out["available"] = available;
    out["reason"] = failure != nullptr ? failure : sample.reason;
    out["requestedDeclaration"] = "Engine.PlayerController.ConsoleCommand";
    out["function"] = ObjectJson(sample.function, failure);
    out["parametersComplete"] = failure == nullptr && sample.parametersComplete;
    out["layoutMatchesSdkDeclaration"] = available && sample.layoutMatches;
    out["dispatchCapability"] = false;
    out["nativeIndexValidation"] = "pending";
    out["processEventTargetValidation"] = "pending";
    out["allocatorAndStringLifetimeValidation"] = "pending";
    out["nativeCallValidation"] = "pending";
    if (failure == nullptr && sample.function.available) {
        out["discoveredDeclaration"] = "Engine.PlayerController.ConsoleCommand";
        out["parmsSize"] = static_cast<int>(sample.parmsSize);
        out["numParms"] = static_cast<int>(sample.numParms);
        out["returnValueOffset"] = sample.returnOffset;
        out["functionFlags"] = std::format("0x{:08X}", sample.functionFlags);
        out["functionFlagsMatchSdkDeclaration"] = sample.functionFlags == 0x24400;
        out["nativeIndex"] = static_cast<int>(sample.nativeIndex);
    }
    auto parameters = json::Value::Array();
    if (failure == nullptr) for (unsigned i = 0; i < sample.parameterCount; ++i) {
        auto const& p = sample.parameters[i];
        auto item = ObjectJson(p.property, nullptr);
        item["name"] = p.name;
        item["offset"] = p.offset;
        item["elementSize"] = p.elementSize;
        item["arrayDim"] = p.arrayDim;
        item["propertyFlags"] = std::format("0x{:016X}", p.flags);
        if (p.hasBoolMask) item["boolMask"] = std::format("0x{:08X}", p.boolMask);
        parameters.Push(std::move(item));
    }
    out["parameters"] = std::move(parameters);
    return out;
}
} // namespace

json::Value Observe(std::uint32_t gameThreadId) {
    Sample sample{};
    char const* failure = nullptr;
    if (gameThreadId == 0 || GetCurrentThreadId() != gameThreadId) failure = "not-on-verified-game-thread";
    else if (!ue3::runtime::Guarded(Read, &sample)) failure = "guarded-engine-read-fault";
    auto out = json::Value::Object();
    out["schemaVersion"] = 1;
    out["game"] = "LE2";
    out["validationScope"] = "sdk-structural-diagnostic";
    out["nativeValidation"] = "pending";
    out["ready"] = false;
    out["durableObjectIdentity"] = false;
    out["reason"] = failure != nullptr ? failure : "structural-sample-only";
    auto table = json::Value::Object();
    table["available"] = failure == nullptr && sample.tableAvailable;
    table["reason"] = failure != nullptr ? failure : sample.tableReason;
    if (failure == nullptr && sample.tableAvailable) table["value"] = static_cast<int>(sample.objectCount);
    out["objectCount"] = std::move(table);
    out["world"] = ObjectJson(sample.world, failure);
    out["engine"] = ObjectJson(sample.engine, failure);
    out["localPlayer"] = ObjectJson(sample.localPlayer, failure);
    out["player"] = ObjectJson(sample.player, failure);
    out["pawn"] = ObjectJson(sample.pawn, failure);
    out["consoleCommandMetadata"] = ConsoleJson(sample.console, failure);
    auto map = json::Value::Object();
    map["available"] = failure == nullptr && sample.mapAvailable;
    map["reason"] = failure != nullptr ? failure : sample.mapReason;
    map["source"] = "GWorld.Outer-package-name";
    map["loadedSaveOrGameplayProof"] = false;
    if (failure == nullptr && sample.mapAvailable) map["value"] = sample.map;
    out["map"] = std::move(map);
    auto position = json::Value::Object();
    position["available"] = failure == nullptr && sample.positionAvailable;
    position["reason"] = failure != nullptr ? failure : sample.positionReason;
    position["source"] = "validated-player-pawn.Actor.Location";
    position["units"] = "engine-units-unverified";
    if (failure == nullptr && sample.positionAvailable) {
        position["x"] = sample.x; position["y"] = sample.y; position["z"] = sample.z;
    }
    out["position"] = std::move(position);
    return out;
}
} // namespace mevr::engine::le2_diagnostic
#endif
