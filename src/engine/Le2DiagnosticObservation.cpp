// SPDX-License-Identifier: MIT
#include "engine/Le2DiagnosticObservation.hpp"

#if defined(MEVR_GAME_LE2)
#include "engine/GameAdapterSdk.hpp"
#include "engine/Addresses.hpp"
#include "core/qa/BoundedUtf16.hpp"
#include "engine/Legacy.hpp"
#include "plugin/ProcessEventDiagnostic.hpp"
#include "ue3/Runtime.hpp"

#include <Windows.h>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <type_traits>

namespace mevr::engine::le2_diagnostic {
namespace {
std::atomic_bool gFovProbeEntered{false};
std::atomic_bool gSplashEntered{false}, gLoadMenuEntered{false};
#if defined(MEVR_LE2_OBSERVATION_TEST)
testing::FovDispatcher gFovDispatcher{};
testing::MenuDispatcher gMenuDispatcher{};
#endif
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
static_assert(sizeof(APlayerController_eventGetFOVAngle_Parms) == 4);
static_assert(offsetof(APlayerController_eventGetFOVAngle_Parms, ReturnValue) == 0);

struct ObjectSample {
    bool available{};
    char const* reason{"not-observed"};
    std::uintptr_t address{};
    std::uintptr_t classAddress{};
    int index{};
    char className[kNameBytes * 2]{};
};
struct ParameterSample {
    ObjectSample property;
    ObjectSample objectClass;
    bool hasObjectClass{}, guiManagerClass{};
    char name[kNameBytes]{};
    int offset{}, elementSize{}, arrayDim{};
    std::uint64_t flags{};
    bool hasBoolMask{};
    std::uint32_t boolMask{};
};
enum class Declaration { Console, Fov, OpenLoad, AdvanceSplash, LoadGame };
char const* DeclarationName(Declaration d) {
    switch (d) {
    case Declaration::Console: return "Engine.PlayerController.ConsoleCommand";
    case Declaration::Fov: return "Engine.PlayerController.GetFOVAngle";
    case Declaration::LoadGame: return "SFXGame.SFXSFHandler_Load.LoadGame";
    case Declaration::OpenLoad: return "SFXGame.BioSFHandler_MainMenu.MoveToLoadGui";
    case Declaration::AdvanceSplash: return "SFXGame.BioSFHandler_PCSplash.AdvanceScreen";
    }
    return "unknown";
}
struct FunctionSample {
    ObjectSample function;
    char const* reason{"player-controller-unavailable"};
    std::uint32_t functionFlags{};
    unsigned nativeIndex{}, numParms{}, parmsSize{};
    int returnOffset{};
    unsigned parameterCount{};
    ParameterSample parameters[kMaxParameters]{};
    bool parametersComplete{}, layoutMatches{};
};
struct SlotSample {
    bool available{};
    char const* reason{"object-not-validated"};
    std::uintptr_t object{}, vtable{}, target{};
};
struct AllocatorSample {
    bool available{};
    char const* reason{"not-observed"};
    std::uintptr_t global{}, object{}, vtable{}, mallocTarget{}, freeTarget{};
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
    FunctionSample console, fov;
    SlotSample processEvent, worldSlot, engineSlot, localPlayerSlot, pawnSlot;
    AllocatorSample allocator;
};
static_assert(std::is_trivially_destructible_v<Sample>);

// Read pointer values only. A readable slot or executable page is not proof of
// function identity, call ABI, allocator ownership, or a permissible call.
void ReadProcessEventSlot(void* context) {
    auto& out = *static_cast<SlotSample*>(context);
    out.reason = "null-object-vtable";
    auto const* vtable = *reinterpret_cast<std::uintptr_t const* const*>(out.object);
    if (vtable == nullptr) return;
    out.vtable = reinterpret_cast<std::uintptr_t>(vtable);
    out.target = vtable[72]; // LE2 SDK UObject declaration: byte offset 0x240.
    out.available = out.target != 0;
    out.reason = out.available ? "slot-read-only-target-unvalidated" : "null-process-event-target";
}
void ReadValidatedSlot(ObjectSample const& object, SlotSample& out) {
    if (!object.available) { out.reason = "object-not-validated"; return; }
    out.object = object.address;
    if (!ue3::runtime::Guarded(ReadProcessEventSlot, &out)) {
        out = {};
        out.reason = "guarded-process-event-slot-read-fault";
    }
}
void ReadAllocator(void* context) {
    auto& out = *static_cast<AllocatorSample*>(context);
    out.global = reinterpret_cast<std::uintptr_t>(GMalloc);
    out.reason = "allocator-global-unavailable";
    if (GMalloc == nullptr) return;
    auto const* object = *GMalloc;
    out.object = reinterpret_cast<std::uintptr_t>(object);
    out.reason = "null-allocator";
    if (object == nullptr) return;
    auto const* vtable = *reinterpret_cast<std::uintptr_t const* const*>(object);
    out.vtable = reinterpret_cast<std::uintptr_t>(vtable);
    out.reason = "null-allocator-vtable";
    if (vtable == nullptr) return;
    out.mallocTarget = vtable[1]; // FMallocLike::Malloc, byte offset 0x08.
    out.freeTarget = vtable[3];   // LE2 FMallocLike::Free, byte offset 0x18.
    out.available = out.mallocTarget != 0 && out.freeTarget != 0;
    out.reason = out.available ? "slots-read-only-targets-unvalidated" : "null-allocator-method-target";
}

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
    out.classAddress = reinterpret_cast<std::uintptr_t>(object->Class);
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
bool HasExpectedParameter(FunctionSample const& sample, char const* name, char const* type,
                          int offset, int size, std::uint64_t flags, std::uint32_t boolMask = 0) {
    for (unsigned i = 0; i < sample.parameterCount; ++i)
        if (ExpectedParameter(sample.parameters[i], name, type, offset, size, flags, boolMask)) return true;
    return false;
}

void ReadFunction(Table const& table, UObject const* controller, FunctionSample& out, Declaration declaration) {
    bool const fov = declaration == Declaration::Fov;
    bool const menu = declaration == Declaration::OpenLoad || declaration == Declaration::AdvanceSplash || declaration == Declaration::LoadGame;
    char const* package = menu ? "SFXGame" : "Engine";
    char const* ownerName = declaration == Declaration::LoadGame ? "SFXSFHandler_Load" : declaration == Declaration::OpenLoad ? "BioSFHandler_MainMenu" :
        declaration == Declaration::AdvanceSplash ? "BioSFHandler_PCSplash" : "PlayerController";
    char const* functionName = declaration == Declaration::LoadGame ? "LoadGame" : declaration == Declaration::OpenLoad ? "MoveToLoadGui" :
        declaration == Declaration::AdvanceSplash ? "AdvanceScreen" : fov ? "GetFOVAngle" : "ConsoleCommand";
    // Match Legacy's exact declaration owner, not a derived class's override.
    auto const* owner = controller->Class;
    out.reason = menu ? "menu-declaration-class-unavailable" : "player-controller-declaration-class-unavailable";
    unsigned depth = 0;
    while (owner != nullptr && depth++ < kMaxClassDepth) {
        if (!ClassMetadata(table, owner)) return;
        if (Qualified(table, owner, package, ownerName)) break;
        if (menu) {
            unsigned nearerFields = 0;
            for (auto const* field = owner->Children; field != nullptr; field = field->Next) {
                out.reason = "invalid-nearer-menu-class-field-or-limit";
                if (++nearerFields > kMaxOwnerFields || !table.Listed(field, true) ||
                    !IsA(table, field, "Core", "Field") || field->Outer != owner) return;
                char nearerName[kNameBytes]{};
                if (!NameText(field->Name, nearerName)) return;
                out.reason = "nearer-menu-function-declaration";
                if (std::strcmp(nearerName, functionName) == 0) return;
            }
        }
        owner = static_cast<UClass const*>(owner->SuperField);
    }
    if (owner == nullptr || !Qualified(table, owner, package, ownerName)) return;
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
        if (std::strcmp(name, functionName) != 0) continue;
        out.reason = menu ? "duplicate-menu-function-declaration" : fov ? "duplicate-get-fov-angle-declaration" : "duplicate-console-command-declaration";
        if (function != nullptr) return;
        out.reason = menu ? "menu-function-is-not-an-exact-Core.Function" : fov ? "get-fov-angle-is-not-an-exact-Core.Function" : "console-command-is-not-an-exact-Core.Function";
        if (!ClassMetadata(table, field->Class) || !Qualified(table, field->Class, "Core", "Function")) return;
        function = static_cast<UFunction const*>(field);
    }
    out.reason = menu ? "menu-function-declaration-missing" : fov ? "get-fov-angle-declaration-missing" : "console-command-declaration-missing";
    if (function == nullptr) return;
    out.reason = menu ? "menu-function-unavailable" : fov ? "get-fov-angle-function-unavailable" : "console-command-function-unavailable";
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
        if (menu && (property->PropertyFlags & 0x400) != 0) { out.reason = "void-menu-function-has-return-property"; return; }
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
        if (declaration == Declaration::LoadGame && Qualified(table, property->Class, "Core", "ObjectProperty")) {
            p.hasObjectClass = true;
            auto const* targetClass = static_cast<UObjectProperty const*>(property)->PropertyClass;
            out.reason = "object-parameter-class-unavailable";
            if (!ClassMetadata(table, targetClass) || !ReadObject(table, targetClass, "Core", "Class", p.objectClass)) return;
            p.guiManagerClass = Qualified(table, targetClass, "SFXGame", "MassEffectGuiManager");
        }
        ++out.parameterCount;
    }
    out.parametersComplete = true;
    // These expected flags and offsets are the pinned SDK declaration, not a
    // capability grant. Scalar DWORD boolean comparison is exact (mask 1).
    out.layoutMatches = fov ? (out.parmsSize == 4 && out.numParms == 1 && out.returnOffset == 0 &&
        out.parameterCount == 1 && HasExpectedParameter(out, "ReturnValue", "Core.FloatProperty", 0, 4, 0x580)) :
        out.parmsSize == 0x24 && out.numParms == 3 && out.returnOffset == 0x14 &&
        out.parameterCount == 3 &&
        HasExpectedParameter(out, "Command", "Core.StrProperty", 0, 16, 0x400080) &&
        HasExpectedParameter(out, "bWriteToLog", "Core.BoolProperty", 0x10, 4, 0x90, 1) &&
        HasExpectedParameter(out, "ReturnValue", "Core.StrProperty", 0x14, 16, 0x400580);
    if (declaration == Declaration::LoadGame) {
        bool managerClass = false;
        for (unsigned i = 0; i < out.parameterCount; ++i)
            if (std::strcmp(out.parameters[i].name, "GuiMan") == 0) managerClass = out.parameters[i].guiManagerClass;
        out.layoutMatches = out.parmsSize == 12 && out.numParms == 2 && out.parameterCount == 2 && managerClass &&
            HasExpectedParameter(out, "SaveIdx", "Core.IntProperty", 0, 4, 0x80) &&
            HasExpectedParameter(out, "GuiMan", "Core.ObjectProperty", 4, 8, 0x80);
    } else if (menu) out.layoutMatches = declaration == Declaration::OpenLoad ?
        out.parmsSize == 1 && out.numParms == 1 && out.parameterCount == 1 &&
        HasExpectedParameter(out, "LoadMode", "Core.ByteProperty", 0, 1, 0x80) :
        out.parmsSize == 0 && out.numParms == 0 && out.parameterCount == 0;
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
        ReadValidatedSlot(out.world, out.worldSlot);
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
    ReadValidatedSlot(out.engine, out.engineSlot);
    out.localPlayer.reason = out.player.reason = out.pawn.reason = "local-player-array-unavailable-or-invalid";
    auto const& players = engine->GamePlayers;
    if (players.Count() == 0 || players.Count() > kMaxPlayers || players.Capacity() < players.Count() ||
        players.Capacity() > kMaxPlayers || players.GetData() == nullptr) return;
    auto const* localPlayer = players.GetData()[0];
    out.player.reason = out.pawn.reason = "local-player-unavailable";
    if (!ReadObject(table, localPlayer, "Engine", "LocalPlayer", out.localPlayer)) return;
    ReadValidatedSlot(out.localPlayer, out.localPlayerSlot);
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
    ReadValidatedSlot(out.player, out.processEvent);
    ReadFunction(table, controller, out.console, Declaration::Console);
    ReadFunction(table, controller, out.fov, Declaration::Fov);
    auto const* pawn = controller->Pawn;
    if (!ReadObject(table, pawn, "Engine", "Pawn", out.pawn)) return;
    ReadValidatedSlot(out.pawn, out.pawnSlot);
    FVector const& position = adapter::Location(pawn);
    out.positionReason = "nonfinite-pawn-location";
    if (std::isfinite(position.X) && std::isfinite(position.Y) && std::isfinite(position.Z)) {
        out.x = position.X; out.y = position.Y; out.z = position.Z;
        out.positionAvailable = true;
        out.positionReason = "finite-sdk-actor-location";
    }
}

constexpr unsigned kMaxMenuPanels = 32, kMaxPanelHandlers = 32, kMaxSaveRows = 12;
constexpr std::size_t kSaveMenuPayloadBytes = 14 * 1024;
static_assert(sizeof(USFXSFHandler_Load_execLoadGame_Parms) == 12);
static_assert(offsetof(USFXSFHandler_Load_execLoadGame_Parms, SaveIdx) == 0 && offsetof(USFXSFHandler_Load_execLoadGame_Parms, GuiMan) == 4);
static_assert(sizeof(FSFXSaveDescriptor) == 0x28 && sizeof(FSaveGUIRecord) == 0x70);
static_assert(offsetof(FSaveGUIRecord, SaveDescriptor) == 0);
static_assert(offsetof(FSFXSaveDescriptor, Career) == 0 && offsetof(FSFXSaveDescriptor, SaveName) == 0x10);
static_assert(offsetof(FSFXSaveDescriptor, Index) == 0x20 && offsetof(FSFXSaveDescriptor, Type) == 0x24);
struct MenuPanel {
    ObjectSample panel, handler, worldInfo;
    bool visible{}, initialized{}, removing{}, inputDisabled{}, eventsDisabled{}, focused{};
    bool pressedStart{}, waitingDlc{}, waitingSaves{}, checkingComplete{}, hasSaves{}, careerHasSaves{}, connecting{};
    bool waitingMessage{}, enumerating{}, fromMainMenu{};
    float legalTimeout{};
    unsigned guiMode{}, loadMode{};
    int careerIndex{};
    FunctionSample function;
};
struct SaveRow {
    std::uintptr_t career{}, name{};
    int index{};
    unsigned type{};
};
struct MenuSample {
    Sample root;
    ObjectSample viewport, manager, worldInfo, controllerWorldInfo;
    MenuPanel splash, main, load;
    bool contextAvailable{}, listComplete{}, worldLinksKnown{};
    char const* reason{"menu-context-unavailable"};
    char const* listReason{"load-panel-unavailable"};
    unsigned panelCount{}, saveCount{}, saveCapacity{};
    std::uintptr_t panelsData{}, savesData{}, focus{};
    SaveRow rows[kMaxSaveRows]{};
};
static_assert(std::is_trivially_destructible_v<MenuSample>);
template<class T> bool BoundedArray(TArray<T> const& array, unsigned limit) {
    return array.Count() <= limit && array.Capacity() >= array.Count() && array.Capacity() <= (limit > 4096 ? limit : 4096) &&
        (array.Count() == 0 || array.GetData() != nullptr);
}
bool PanelContext(Table const& table, UBioSFPanel const* panel, UBioSFManager const* manager,
                  char const* type, MenuPanel& out, bool defaultOnly) {
    if (!ReadObject(table, panel, "Engine", "BioSFPanel", out.panel) || panel->oParentManager != manager) return false;
    out.visible = panel->IsVisible; out.initialized = panel->bInitialized; out.removing = panel->bToBeRemoved;
    out.inputDisabled = panel->bInputDisabled; out.eventsDisabled = panel->bEventsDisabled;
    out.focused = manager->m_FocusPanel == panel;
    if (!BoundedArray(panel->ScriptHandlers, kMaxPanelHandlers)) return false;
    UBioSFHandler const* selected = nullptr;
    auto accept = [&](UBioSFHandler const* candidate, bool required) {
        if (candidate == nullptr) return !required;
        ObjectSample object;
        if (!ReadObject(table, candidate, "Engine", "BioSFHandler", object) || candidate->oPanel != panel) return false;
        if (!IsA(table, candidate, "SFXGame", type)) return !required;
        if (candidate->oWorldInfo != static_cast<UMassEffectGuiManager const*>(manager)->oBioWorldInfo) return false;
        if (candidate->oWorldInfo != nullptr &&
            !ReadObject(table, candidate->oWorldInfo, "Engine", "WorldInfo", out.worldInfo)) return false;
        if (selected != nullptr && selected != candidate) return false;
        selected = candidate; out.handler = object;
        return true;
    };
    if (!accept(panel->m_DefaultHandler, defaultOnly)) return false;
    for (unsigned i = 0; i < panel->ScriptHandlers.Count(); ++i)
        if (!accept(panel->ScriptHandlers.GetData()[i], false)) return false;
    return selected != nullptr;
}
bool PanelInteractive(MenuPanel const& p) {
    return p.panel.available && p.handler.available && p.visible && p.initialized && !p.removing &&
        !p.inputDisabled && !p.eventsDisabled && p.focused;
}
void ReadMenu(void* context) {
    auto& out = *static_cast<MenuSample*>(context);
    Read(&out.root);
    if (!out.root.localPlayer.available || !out.root.player.available) return;
    auto const* objects = UObject::GObjObjects;
    if (objects == nullptr || objects->Count() != out.root.objectCount || !BoundedArray(*objects, kMaxObjects)) return;
    Table const table{objects->GetData(), objects->Count()};
    auto const* local = reinterpret_cast<ULocalPlayer const*>(out.root.localPlayer.address);
    auto const* player = reinterpret_cast<APlayerController const*>(out.root.player.address);
    auto const* viewport = local->ViewportClient;
    out.reason = "viewport-unavailable";
    if (!ReadObject(table, viewport, "Engine", "GameViewportClient", out.viewport)) return;
    auto const* manager = viewport->m_pScaleFormManager;
    out.reason = "scaleform-manager-unavailable";
    if (!ReadObject(table, manager, "SFXGame", "MassEffectGuiManager", out.manager)) return;
    out.reason = "controller-manager-cache-disagrees";
    if (player->m_oCachedScaleFormManager != nullptr && player->m_oCachedScaleFormManager != manager) return;
    auto const* managerWorld = static_cast<UMassEffectGuiManager const*>(manager)->oBioWorldInfo;
    out.reason = "menu-world-info-unavailable";
    if (managerWorld != nullptr && !ReadObject(table, managerWorld, "SFXGame", "BioWorldInfo", out.worldInfo)) return;
    if (player->WorldInfo != nullptr && !ReadObject(table, player->WorldInfo, "Engine", "WorldInfo", out.controllerWorldInfo)) return;
    out.reason = "controller-manager-world-info-disagrees";
    if (player->WorldInfo != managerWorld) return;
    out.worldLinksKnown = managerWorld != nullptr;
    out.reason = "panel-array-invalid-or-limit";
    if (!BoundedArray(manager->m_lstPanels, kMaxMenuPanels)) return;
    out.panelCount = manager->m_lstPanels.Count();
    out.panelsData = reinterpret_cast<std::uintptr_t>(manager->m_lstPanels.GetData());
    out.focus = reinterpret_cast<std::uintptr_t>(manager->m_FocusPanel);
    bool focusListed = manager->m_FocusPanel == nullptr;
    for (unsigned i = 0; i < out.panelCount; ++i) {
        auto const* panel = manager->m_lstPanels.GetData()[i];
        ObjectSample identity;
        out.reason = "invalid-panel-or-manager-reciprocal";
        if (!ReadObject(table, panel, "Engine", "BioSFPanel", identity) || panel->oParentManager != manager) return;
        for (unsigned j = 0; j < i; ++j) {
            out.reason = "duplicate-panel-list-entry";
            if (manager->m_lstPanels.GetData()[j] == panel) return;
        }
        focusListed |= manager->m_FocusPanel == panel;
        char tag[kNameBytes]{};
        out.reason = "panel-tag-unavailable";
        if (!NameText(panel->nmTag, tag)) return;
        MenuPanel* destination = nullptr;
        char const* type = nullptr;
        if (std::strcmp(tag, "Load") == 0) { destination = &out.load; type = "SFXSFHandler_Load"; }
        if (std::strcmp(tag, "MainMenu") == 0) { destination = &out.main; type = "BioSFHandler_MainMenu"; }
        if (std::strcmp(tag, "Splash") == 0) { destination = &out.splash; type = "BioSFHandler_Splash"; }
        if (destination == nullptr) continue;
        out.reason = "ambiguous-menu-panels";
        if (destination->panel.available) return;
        out.reason = "invalid-or-ambiguous-panel-handler";
        if (!PanelContext(table, panel, manager, type, *destination, destination == &out.load)) return;
    }
    out.reason = "focus-panel-not-in-manager-list";
    if (!focusListed) return;
    if (out.splash.handler.available) {
        auto const* h = reinterpret_cast<UBioSFHandler_Splash const*>(out.splash.handler.address);
        out.splash.pressedStart = h->bPressedStart; out.splash.legalTimeout = h->fLegalTimeout;
        ReadFunction(table, h, out.splash.function, Declaration::AdvanceSplash);
    }
    if (out.main.handler.available) {
        auto const* h = reinterpret_cast<UBioSFHandler_MainMenu const*>(out.main.handler.address);
        out.main.waitingDlc = h->bWaitingForDLC; out.main.waitingSaves = h->bWaitingForSaves;
        out.main.connecting = h->m_bIsConnecting;
        out.main.checkingComplete = h->bCheckingDataComplete;
        out.main.hasSaves = h->bHasSaves; out.main.careerHasSaves = h->bCurrentCareerHasSaves;
        ReadFunction(table, h, out.main.function, Declaration::OpenLoad);
    }
    out.contextAvailable = true; out.reason = "reachable-menu-context-only";
    if (!out.load.handler.available) return;
    auto const* h = reinterpret_cast<USFXSFHandler_Load const*>(out.load.handler.address);
    ReadFunction(table, h, out.load.function, Declaration::LoadGame);
    out.load.waitingMessage = h->bWaitingOnMsgBox; out.load.enumerating = h->bEnumeratingSaves;
    out.load.checkingComplete = h->bCheckingDataComplete; out.load.fromMainMenu = h->bFromMainMenu;
    out.load.guiMode = h->GuiMode; out.load.loadMode = h->LoadMode; out.load.careerIndex = h->CurrentCareerIndex;
    out.saveCount = h->SaveList.Count(); out.saveCapacity = h->SaveList.Capacity();
    out.savesData = reinterpret_cast<std::uintptr_t>(h->SaveList.GetData());
    out.listReason = "load-panel-not-interactive-or-enumeration-pending";
    if (!PanelInteractive(out.load) || out.load.waitingMessage || out.load.enumerating || !out.load.checkingComplete) return;
    out.listReason = "save-list-header-invalid-or-row-limit";
    if (!BoundedArray(h->SaveList, kMaxSaveRows)) return;
    for (unsigned i = 0; i < out.saveCount; ++i) {
        auto const& descriptor = h->SaveList.GetData()[i].SaveDescriptor;
        out.rows[i] = {reinterpret_cast<std::uintptr_t>(&descriptor.Career),
            reinterpret_cast<std::uintptr_t>(&descriptor.SaveName), descriptor.Index, descriptor.Type};
    }
    out.listComplete = true; out.listReason = "bounded-descriptor-headers-only";
}
struct CopyRead { std::uintptr_t source; void* destination; std::size_t bytes; };
void CopyBytes(void* context) {
    auto const& c = *static_cast<CopyRead const*>(context);
    std::memcpy(c.destination, reinterpret_cast<void const*>(c.source), c.bytes);
}
bool GuardedCopy(std::uintptr_t source, void* destination, std::size_t bytes, void*) noexcept {
    CopyRead copy{source, destination, bytes};
    return ue3::runtime::Guarded(CopyBytes, &copy);
}
// Injective, versioned descriptor-content comparison; not a hash, authorization,
// persistent object identity, file path, or proof that a save file was loaded.
std::string DescriptorKey(std::string const& career, std::string const& name, int index, unsigned type) {
    std::string bytes;
    auto word = [&](std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<char>((value >> (8 * i)) & 255)); };
    word(static_cast<std::uint32_t>(career.size())); bytes += career;
    word(static_cast<std::uint32_t>(name.size())); bytes += name;
    word(static_cast<std::uint32_t>(index)); bytes.push_back(static_cast<char>(type));
    std::string out = "descriptor-v1:";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char c : bytes) { out.push_back(hex[c >> 4]); out.push_back(hex[c & 15]); }
    return out;
}

json::Value ObjectJson(ObjectSample const& object, char const* failure) {
    auto out = json::Value::Object();
    out["available"] = failure == nullptr && object.available;
    out["reason"] = failure != nullptr ? failure : object.reason;
    if (failure == nullptr && object.available) {
        out["address"] = std::format("0x{:X}", object.address);
        out["index"] = object.index;
        out["class"] = object.className;
        out["classAddress"] = std::format("0x{:X}", object.classAddress);
    }
    return out;
}
json::Value TargetJson(std::uintptr_t target, ObserverServices const* services) {
    auto out = json::Value::Object();
    out["address"] = std::format("0x{:X}", target);
    MEMORY_BASIC_INFORMATION memory{};
    bool const queried = target != 0 && VirtualQuery(reinterpret_cast<void const*>(target), &memory, sizeof(memory)) == sizeof(memory);
    bool const committed = queried && memory.State == MEM_COMMIT;
    DWORD const protection = memory.Protect & 0xFF;
    bool const executable = committed && (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) == 0 &&
        (protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY);
    out["memoryQueryAvailable"] = queried;
    out["committedExecutablePage"] = executable;
    out["identityValidated"] = false;
    if (queried) {
        out["allocationBase"] = std::format("0x{:X}", reinterpret_cast<std::uintptr_t>(memory.AllocationBase));
        out["pageProtection"] = std::format("0x{:X}", memory.Protect);
    }
    auto inspection = json::Value::Object();
    inspection["available"] = false;
    inspection["reason"] = "target-inspector-not-supplied";
    if (target != 0 && services != nullptr && services->inspectTarget != nullptr) {
        auto const detail = services->inspectTarget(target);
        bool const matches = detail.target == target;
        inspection["available"] = matches;
        inspection["reason"] = matches ? (detail.reason ? detail.reason : "unavailable") : "inspector-target-mismatch";
        if (matches) {
            inspection["mainModuleBase"] = std::format("0x{:X}", detail.moduleBase);
            inspection["rva"] = std::format("0x{:X}", detail.rva);
            inspection["inMainText"] = detail.inMainText;
            inspection["boundsKnown"] = detail.boundsKnown;
            inspection["exactFunctionStart"] = detail.exactFunctionStart;
            inspection["boundsUsable"] = detail.boundsUsable;
            inspection["functionBegin"] = std::format("0x{:X}", detail.functionBegin);
            inspection["functionEnd"] = std::format("0x{:X}", detail.functionEnd);
            inspection["baseTarget"] = std::format("0x{:X}", detail.baseTarget);
            inspection["sameAsBase"] = detail.sameAsBase;
            inspection["recognizedJumpToBase"] = detail.jumpToBase;
            inspection["jumpReason"] = detail.jumpReason ? detail.jumpReason : "unavailable";
            inspection["jumpHops"] = static_cast<int>(detail.jumpHops);
        }
    }
    out["inspection"] = std::move(inspection);
    return out;
}
json::Value SlotJson(SlotSample const& sample, char const* failure,
                     host::process_event::diagnostic::Snapshot const* hostSnapshot,
                     ObserverServices const* services, ObjectSample const* object = nullptr) {
    namespace diagnostic = host::process_event::diagnostic;
    auto out = json::Value::Object();
    out["available"] = failure == nullptr && sample.available;
    out["reason"] = failure != nullptr ? failure : sample.reason;
    out["slotIndex"] = 72;
    out["slotByteOffset"] = 0x240;
    out["dispatchCapability"] = false;
    out["targetValidation"] = "pending";
    auto provenance = json::Value::Object();
    provenance["available"] = hostSnapshot != nullptr;
    provenance["reason"] = hostSnapshot != nullptr ? "host-install-and-current-chain-observation" : "host-snapshot-not-supplied";
    bool const slotMatches = failure == nullptr && sample.available && hostSnapshot != nullptr &&
        hostSnapshot->installation == diagnostic::InstallState::Installed && hostSnapshot->target != 0 &&
        sample.target == hostSnapshot->target;
    out["targetMatchesInstalledHook"] = slotMatches;
    out["slotAndOwnedChainMatch"] = slotMatches && hostSnapshot->chain.status == diagnostic::ChainStatus::Matched;
    if (hostSnapshot != nullptr) {
        provenance["installation"] = diagnostic::Name(hostSnapshot->installation);
        provenance["target"] = std::format("0x{:X}", hostSnapshot->target);
        provenance["ownedDetour"] = std::format("0x{:X}", hostSnapshot->detour);
        provenance["installerReturnedNextHop"] = std::format("0x{:X}", hostSnapshot->nextHop);
        provenance["backend"] = hostSnapshot->backend != nullptr ? hostSnapshot->backend : "unavailable";
        provenance["exeCheck"] = hostSnapshot->exeCheck != nullptr ? hostSnapshot->exeCheck : "Unavailable";
        provenance["currentChain"] = diagnostic::Name(hostSnapshot->chain.status);
        provenance["chainHops"] = static_cast<int>(hostSnapshot->chain.hops);
        provenance["pristineEngineCodeValidated"] = false;
    }
    // Full host provenance is emitted once by the compatibility controller
    // field; the comparison set refers to that same supplied snapshot.
    if (object == nullptr) out["hostProvenance"] = std::move(provenance);
    else out["object"] = ObjectJson(*object, failure);
    if (failure == nullptr) {
        out[object == nullptr ? "controllerAddress" : "objectAddress"] = std::format("0x{:X}", sample.object);
        out["vtableAddress"] = std::format("0x{:X}", sample.vtable);
        out["target"] = TargetJson(sample.target, services);
    }
    return out;
}
json::Value AllocatorJson(AllocatorSample const& sample, char const* failure, ObserverServices const* services) {
    auto out = json::Value::Object();
    out["available"] = failure == nullptr && sample.available;
    out["reason"] = failure != nullptr ? failure : sample.reason;
    out["mallocSlotIndex"] = 1;
    out["mallocSlotByteOffset"] = 0x08;
    out["freeSlotIndex"] = 3;
    out["freeSlotByteOffset"] = 0x18;
    out["allocationCapability"] = false;
    out["ownershipValidation"] = "pending";
    if (failure == nullptr) {
        out["globalAddress"] = std::format("0x{:X}", sample.global);
        out["allocatorAddress"] = std::format("0x{:X}", sample.object);
        out["vtableAddress"] = std::format("0x{:X}", sample.vtable);
        out["mallocTarget"] = TargetJson(sample.mallocTarget, services);
        out["freeTarget"] = TargetJson(sample.freeTarget, services);
    }
    return out;
}
json::Value FunctionJson(FunctionSample const& sample, char const* failure, Declaration declaration) {
    bool const fov = declaration == Declaration::Fov;
    char const* const name = DeclarationName(declaration);
    auto out = json::Value::Object();
    bool const available = failure == nullptr && sample.function.available && sample.parametersComplete;
    out["available"] = available;
    out["reason"] = failure != nullptr ? failure : sample.reason;
    out["requestedDeclaration"] = name;
    out["function"] = ObjectJson(sample.function, failure);
    out["parametersComplete"] = failure == nullptr && sample.parametersComplete;
    out["layoutMatchesSdkDeclaration"] = available && sample.layoutMatches;
    out["dispatchCapability"] = false;
    out["nativeIndexValidation"] = "pending";
    out["processEventTargetValidation"] = "pending";
    out["allocatorAndStringLifetimeValidation"] = "pending";
    out["nativeCallValidation"] = "pending";
    if (failure == nullptr && sample.function.available) {
        out["discoveredDeclaration"] = name;
        out["parmsSize"] = static_cast<int>(sample.parmsSize);
        out["numParms"] = static_cast<int>(sample.numParms);
        out["returnValueOffset"] = sample.returnOffset;
        out["functionFlags"] = std::format("0x{:08X}", sample.functionFlags);
        out["functionFlagsMatchSdkDeclaration"] = sample.functionFlags == (declaration == Declaration::LoadGame ? 0x820003u : declaration == Declaration::OpenLoad ? 0x20002u : declaration == Declaration::AdvanceSplash || fov ? 0x20802u : 0x24400u);
        out["nativeIndex"] = static_cast<int>(sample.nativeIndex);
    }
    if (fov) {
        out["expectedNativeIndex"] = 0;
        out["nativeIndexMatchesExpected"] = available && sample.nativeIndex == 0;
        out["declarationMatchesExpected"] = available && sample.layoutMatches && sample.functionFlags == 0x20802 && sample.nativeIndex == 0;
        out["declarationKind"] = "scripted-event-not-native-getter";
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
        if (p.hasObjectClass) { item["objectClass"] = ObjectJson(p.objectClass, nullptr); item["objectClassIsGuiManager"] = p.guiManagerClass; }
        if (p.hasBoolMask) item["boolMask"] = std::format("0x{:08X}", p.boolMask);
        parameters.Push(std::move(item));
    }
    out["parameters"] = std::move(parameters);
    return out;
}
host::process_event::diagnostic::ControllerWatch WatchInput(Sample const& sample, std::uint32_t thread, char const* failure) {
    host::process_event::diagnostic::ControllerWatch identity{};
    if (failure != nullptr || !sample.world.available || !sample.localPlayer.available ||
        !sample.player.available || !sample.processEvent.available) return identity;
    identity.object = sample.player.address;
    identity.index = sample.player.index;
    identity.classAddress = sample.player.classAddress;
    identity.target = sample.processEvent.target;
    identity.world = sample.world.address;
    identity.localPlayer = sample.localPlayer.address;
    identity.gameThread = thread;
    return identity;
}
json::Value WitnessJson(host::process_event::diagnostic::WitnessSnapshot const& sample,
                       host::process_event::diagnostic::ControllerWatch const& identity, bool supplied, char const* reason) {
    namespace diagnostic = host::process_event::diagnostic;
    auto out = json::Value::Object();
    bool const matches = supplied && sample.identity == identity;
    out["available"] = matches && identity.object != 0 &&
        (sample.status == diagnostic::WatchStatus::Watching || sample.status == diagnostic::WatchStatus::Expired);
    out["reason"] = !supplied ? reason : !matches ? "witness-identity-mismatch" : (sample.reason ? sample.reason : "unavailable");
    out["scope"] = "one-controller-observed-slot-target-fixed-window";
    out["durableObjectIdentity"] = false;
    out["dispatchCapability"] = false;
    out["windowMilliseconds"] = static_cast<int>(diagnostic::kWatchDurationMs);
    if (matches) {
        out["status"] = diagnostic::Name(sample.status);
        // Preserve saturated 64-bit counters/epochs exactly across JSON clients.
        out["epoch"] = std::to_string(sample.epoch);
        out["startedMilliseconds"] = std::to_string(sample.startedMs);
        out["expiresMilliseconds"] = std::to_string(sample.expiresMs);
        out["controllerAddress"] = std::format("0x{:X}", identity.object);
        out["controllerIndex"] = identity.index;
        out["classAddress"] = std::format("0x{:X}", identity.classAddress);
        out["target"] = std::format("0x{:X}", identity.target);
        out["worldAddress"] = std::format("0x{:X}", identity.world);
        out["localPlayerAddress"] = std::format("0x{:X}", identity.localPlayer);
        out["gameThread"] = std::to_string(identity.gameThread);
        out["functionBegin"] = std::format("0x{:X}", sample.functionBegin);
        out["functionEnd"] = std::format("0x{:X}", sample.functionEnd);
        out["baseEntries"] = std::to_string(sample.entries);
        out["originalNormalReturns"] = std::to_string(sample.originalReturns);
        out["targetCallerEntries"] = std::to_string(sample.matchingEntries);
        out["targetCallerOriginalNormalReturns"] = std::to_string(sample.matchingOriginalReturns);
        out["lastMatchingCaller"] = std::format("0x{:X}", sample.lastMatchingCaller);
        out["lastMatchingFunction"] = std::format("0x{:X}", sample.lastMatchingFunction);
    }
    return out;
}
bool GetterMatches(Sample const& sample) {
    auto const& fov = sample.fov;
    return fov.function.available && fov.parametersComplete && fov.layoutMatches &&
        fov.functionFlags == 0x20802 && fov.nativeIndex == 0;
}
bool SameProbeSample(Sample const& before, Sample const& after, std::uint32_t thread) {
    return WatchInput(before, thread, nullptr) == WatchInput(after, thread, nullptr) &&
        GetterMatches(after) && before.fov.function.address == after.fov.function.address &&
        before.fov.function.index == after.fov.function.index &&
        before.fov.function.classAddress == after.fov.function.classAddress &&
        before.fov.parameters[0].property.address == after.fov.parameters[0].property.address &&
        before.fov.parameters[0].property.index == after.fov.parameters[0].property.index &&
        before.fov.parameters[0].property.classAddress == after.fov.parameters[0].property.classAddress;
}
bool ProbeTargetValid(host::process_event::diagnostic::TargetInspection const& target,
                      host::process_event::diagnostic::Snapshot const& hook, std::uintptr_t observed) {
    namespace diagnostic = host::process_event::diagnostic;
    bool const checkedExe = hook.exeCheck != nullptr &&
        (std::strcmp(hook.exeCheck, "AtRvaUnchecked") == 0 || std::strcmp(hook.exeCheck, "AtRva") == 0 ||
         std::strcmp(hook.exeCheck, "AtRvaHooked") == 0);
    return hook.installation == diagnostic::InstallState::Installed &&
        hook.chain.status == diagnostic::ChainStatus::Matched && hook.chain.hops > 0 && hook.chain.hops <= diagnostic::kMaxHops &&
        hook.target != 0 && hook.detour != 0 && hook.nextHop != 0 &&
        hook.target != hook.detour && hook.target != hook.nextHop && hook.detour != hook.nextHop && checkedExe &&
        target.target == observed && target.baseTarget == hook.target && target.moduleBase != 0 &&
        hook.target >= target.moduleBase && hook.target - target.moduleBase == addresses::kProcessEvent &&
        target.inMainText && target.boundsKnown && target.exactFunctionStart && target.boundsUsable &&
        target.functionBegin == observed && target.functionEnd > observed &&
        target.functionEnd - observed <= diagnostic::kMaxInspectedFunctionBytes && observed >= target.moduleBase &&
        observed - target.moduleBase <= UINT32_MAX && target.rva == observed - target.moduleBase;
}
struct FovFrame {
    std::uint32_t before{0xC173AB52};
    APlayerController_eventGetFOVAngle_Parms parameters;
    std::uint32_t after{0x5A92E8C1};
};
static_assert(offsetof(FovFrame, parameters) == 4 && offsetof(FovFrame, after) == 8 && sizeof(FovFrame) == 12);
void DispatchFov(Sample const& sample, FovFrame& frame) {
#if defined(MEVR_LE2_OBSERVATION_TEST)
    gFovDispatcher(reinterpret_cast<void*>(sample.player.address), reinterpret_cast<void*>(sample.fov.function.address), &frame.parameters);
#else
    // Same SDK virtual route as Legacy, with no wrapper or FunctionFlags edits.
    reinterpret_cast<APlayerController*>(sample.player.address)->ProcessEvent(
        reinterpret_cast<UFunction*>(sample.fov.function.address), &frame.parameters, nullptr);
#endif
}
} // namespace

namespace {
json::Value MenuPanelJson(MenuPanel const& p, char const* failure) {
    auto out = json::Value::Object();
    out["panel"] = ObjectJson(p.panel, failure); out["handler"] = ObjectJson(p.handler, failure);
    out["worldInfo"] = ObjectJson(p.worldInfo, failure);
    out["structurallyInteractive"] = failure == nullptr && PanelInteractive(p);
    if (failure != nullptr || !p.panel.available) return out;
    out["visible"] = p.visible; out["initialized"] = p.initialized; out["removing"] = p.removing;
    out["inputDisabled"] = p.inputDisabled; out["eventsDisabled"] = p.eventsDisabled; out["focused"] = p.focused;
    return out;
}
json::Value MenuContextJson(MenuSample const& sample, char const* failure) {
    auto out = json::Value::Object();
    out["available"] = failure == nullptr && sample.contextAvailable;
    out["reason"] = failure != nullptr ? failure : sample.reason;
    out["localPlayer"] = ObjectJson(sample.root.localPlayer, failure);
    out["controller"] = ObjectJson(sample.root.player, failure);
    out["viewport"] = ObjectJson(sample.viewport, failure); out["manager"] = ObjectJson(sample.manager, failure);
    out["managerWorldInfo"] = ObjectJson(sample.worldInfo, failure);
    out["controllerWorldInfo"] = ObjectJson(sample.controllerWorldInfo, failure);
    out["worldLinksKnown"] = failure == nullptr && sample.contextAvailable && sample.worldLinksKnown;
    out["worldLinkScope"] = "controller-manager-selected-handlers-point-in-time-only";
    // Partial traversal cannot make an earlier candidate actionable after a later
    // ambiguity, foreign panel or invalid focus reference has been encountered.
    char const* panelFailure = failure != nullptr ? failure : sample.contextAvailable ? nullptr : sample.reason;
    out["splash"] = MenuPanelJson(sample.splash, panelFailure);
    out["mainMenu"] = MenuPanelJson(sample.main, panelFailure);
    out["load"] = MenuPanelJson(sample.load, panelFailure);
    if (panelFailure != nullptr) return out;
    out["panelCount"] = static_cast<int>(sample.panelCount);
    out["focusAddress"] = std::format("0x{:X}", sample.focus);
    if (sample.splash.handler.available) {
        out["splash"]["pressedStart"] = sample.splash.pressedStart;
        out["splash"]["legalTimeoutFinite"] = std::isfinite(sample.splash.legalTimeout);
        if (std::isfinite(sample.splash.legalTimeout)) out["splash"]["legalTimeout"] = sample.splash.legalTimeout;
        out["splash"]["advanceScreen"] = FunctionJson(sample.splash.function, nullptr, Declaration::AdvanceSplash);
        out["splash"]["advanceScreen"]["voidReturnOffsetExpectation"] = "unverified-observed-value-only";
    }
    if (sample.main.handler.available) {
        out["mainMenu"]["waitingForDlc"] = sample.main.waitingDlc;
        out["mainMenu"]["waitingForSaves"] = sample.main.waitingSaves;
        out["mainMenu"]["connecting"] = sample.main.connecting;
        out["mainMenu"]["checkingDataComplete"] = sample.main.checkingComplete;
        out["mainMenu"]["hasSaves"] = sample.main.hasSaves;
        out["mainMenu"]["currentCareerHasSaves"] = sample.main.careerHasSaves;
        out["mainMenu"]["moveToLoadGui"] = FunctionJson(sample.main.function, nullptr, Declaration::OpenLoad);
        out["mainMenu"]["moveToLoadGui"]["voidReturnOffsetExpectation"] = "unverified-observed-value-only";
    }
    if (sample.load.handler.available) {
        out["load"]["loadGame"] = FunctionJson(sample.load.function, nullptr, Declaration::LoadGame);
        out["load"]["loadGame"]["voidReturnOffsetExpectation"] = "unverified-observed-value-only";
        out["load"]["waitingOnMessageBox"] = sample.load.waitingMessage;
        out["load"]["enumeratingSaves"] = sample.load.enumerating;
        out["load"]["checkingDataComplete"] = sample.load.checkingComplete;
        out["load"]["fromMainMenu"] = sample.load.fromMainMenu;
        out["load"]["guiMode"] = static_cast<int>(sample.load.guiMode);
        out["load"]["loadMode"] = static_cast<int>(sample.load.loadMode);
        out["load"]["currentCareerIndex"] = sample.load.careerIndex;
    }
    return out;
}
bool SameMenuHeaders(MenuSample const& a, MenuSample const& b) {
    if (!b.contextAvailable || a.panelCount != b.panelCount || a.panelsData != b.panelsData ||
        a.focus != b.focus || a.saveCount != b.saveCount || a.saveCapacity != b.saveCapacity ||
        a.savesData != b.savesData || a.listComplete != b.listComplete) return false;
    if (json::Write(MenuContextJson(a, nullptr), 0) != json::Write(MenuContextJson(b, nullptr), 0)) return false;
    if (a.listComplete) for (unsigned i = 0; i < a.saveCount; ++i) {
        auto const& x = a.rows[i]; auto const& y = b.rows[i];
        if (x.career != y.career || x.name != y.name || x.index != y.index || x.type != y.type) return false;
    }
    return true;
}
} // namespace

json::Value ObserveSaveMenu(std::uint32_t gameThreadId) {
    MenuSample sample{};
    char const* failure = nullptr;
    if (gameThreadId == 0 || GetCurrentThreadId() != gameThreadId) failure = "not-on-verified-game-thread";
    else if (!ue3::runtime::Guarded(ReadMenu, &sample)) failure = "guarded-menu-read-fault";
    auto out = json::Value::Object();
    out["schemaVersion"] = 1; out["game"] = "LE2";
    out["validationScope"] = "reachable-menu-read-only-sample";
    out["dispatchCapability"] = false; out["fixtureIdentityProven"] = false; out["durableObjectIdentity"] = false;
    out["context"] = MenuContextJson(sample, failure);
    auto records = json::Value::Array();
    bool complete = failure == nullptr && sample.contextAvailable && sample.listComplete;
    std::string reason = failure != nullptr ? failure : !sample.contextAvailable ? sample.reason : sample.listReason;
    constexpr qa::utf16::Limits stringLimits{256, 4096, 1024};
    if (complete) for (unsigned i = 0; i < sample.saveCount; ++i) {
        auto const& row = sample.rows[i];
        auto const career = qa::utf16::ReadFString(row.career, GuardedCopy, nullptr, stringLimits);
        auto const name = qa::utf16::ReadFString(row.name, GuardedCopy, nullptr, stringLimits);
        if (!career.Available() || !name.Available()) {
            reason = std::string("descriptor-string-") + qa::utf16::Name(!career.Available() ? career.reason : name.reason);
            complete = false; break;
        }
        auto item = json::Value::Object();
        item["row"] = static_cast<int>(i); item["career"] = career.utf8; item["saveName"] = name.utf8;
        item["index"] = row.index; item["type"] = static_cast<int>(row.type);
        item["contentKey"] = DescriptorKey(career.utf8, name.utf8, row.index, row.type);
        for (auto const& prior : records.Items()) if (prior.String("contentKey") == item.String("contentKey")) {
            reason = "duplicate-descriptor-content"; complete = false; break;
        }
        if (!complete) break;
        records.Push(std::move(item));
    }
    // Re-sample source identities/flags/array headers before claiming a complete
    // copied list. This is a bounded stability check, not a GC/lifetime guarantee.
    if (failure == nullptr && sample.contextAvailable) {
        MenuSample after{};
        if (!ue3::runtime::Guarded(ReadMenu, &after) || !SameMenuHeaders(sample, after)) {
            failure = "menu-context-changed-or-reread-failed";
            out["context"] = MenuContextJson(sample, failure); reason = failure; complete = false;
        }
    }
    if (complete) for (unsigned i = 0; i < sample.saveCount; ++i) {
        auto const career = qa::utf16::ReadFString(sample.rows[i].career, GuardedCopy, nullptr, stringLimits);
        auto const name = qa::utf16::ReadFString(sample.rows[i].name, GuardedCopy, nullptr, stringLimits);
        if (!career.Available() || !name.Available() || career.utf8 != records.Items()[i].String("career") ||
            name.utf8 != records.Items()[i].String("saveName")) {
            complete = false; reason = "descriptor-string-changed-or-reread-failed"; break;
        }
    }
    if (!complete) records = json::Value::Array();
    out["records"] = std::move(records);
    out["listComplete"] = complete;
    out["reason"] = complete ? "bounded-descriptor-copy-only" : reason;
    if (failure == nullptr && sample.contextAvailable) out["observedRowCount"] = static_cast<std::uint64_t>(sample.saveCount);
    out["rowLimit"] = static_cast<int>(kMaxSaveRows); out["stringCodeUnitLimit"] = 256;
    out["contentKeyFormat"] = "descriptor-v1:hex(le32-utf8-length,career,le32-utf8-length,name,le32-index,u8-type)";
    out["selectionAcceptance"] = "requires-frozen-fixture-mapping-and-fresh-exact-match";
    if (json::Write(out, 0).size() > kSaveMenuPayloadBytes) {
        out["records"] = json::Value::Array(); out["listComplete"] = false;
        out["reason"] = "compact-payload-budget-exceeded";
    }
    // Defense if future context fields grow: never return an oversized payload.
    if (json::Write(out, 0).size() > kSaveMenuPayloadBytes) {
        out = json::Value::Object(); out["schemaVersion"] = 1; out["game"] = "LE2";
        out["reason"] = "compact-context-budget-exceeded"; out["listComplete"] = false;
        out["dispatchCapability"] = false; out["records"] = json::Value::Array();
    }
    return out;
}

#if defined(MEVR_LE2_OBSERVATION_TEST)
void testing::ResetFovProbe(FovDispatcher dispatcher) {
    gFovProbeEntered.store(false);
    gFovDispatcher = dispatcher;
}
void testing::ResetMenuActions(MenuDispatcher dispatcher) {
    gSplashEntered.store(false); gLoadMenuEntered.store(false); gMenuDispatcher = dispatcher;
}
#endif

ProbeResult ProbeFov(std::uint32_t gameThreadId, ObserverServices const& services) {
    namespace diagnostic = host::process_event::diagnostic;
    ProbeResult result{"rejected", json::Value::Object()};
    auto& out = result.payload;
    out["schemaVersion"] = 1;
    out["operation"] = "probe-fov";
    out["declaration"] = "Engine.PlayerController.GetFOVAngle";
    out["entered"] = false;
    out["gameplayValidated"] = false;
    out["durableObjectIdentity"] = false;
    auto reject = [&](char const* reason) { out["reason"] = reason; out["outcome"] = "not-entered"; return result; };
    if (gameThreadId == 0 || GetCurrentThreadId() != gameThreadId) return reject("not-on-verified-game-thread");
    if (gFovProbeEntered.load()) return reject("process-probe-entry-already-spent");
    if (services.inspectTarget == nullptr || services.readHookSnapshot == nullptr) return reject("probe-providers-unavailable");
#if defined(MEVR_LE2_OBSERVATION_TEST)
    if (gFovDispatcher == nullptr) return reject("test-dispatcher-not-supplied");
#endif
    Sample first{};
    if (!ue3::runtime::Guarded(Read, &first)) return reject("guarded-preflight-read-fault");
    auto const identity = WatchInput(first, gameThreadId, nullptr);
    if (identity.object == 0 || !GetterMatches(first)) return reject("controller-context-or-getter-metadata-unavailable");
    auto const hook = services.readHookSnapshot();
    auto const target = services.inspectTarget(identity.target);
    if (!ProbeTargetValid(target, hook, identity.target)) return reject("current-hook-or-target-prerequisites-failed");
    bool const exactBase = identity.target == hook.target;
    diagnostic::WitnessSnapshot witness{};
    if (!exactBase) {
        if (services.observeController == nullptr) return reject("passive-witness-provider-unavailable");
        witness = services.observeController(identity);
        bool const boundedWindow = witness.epoch != 0 && witness.expiresMs > witness.startedMs &&
            witness.expiresMs - witness.startedMs == diagnostic::kWatchDurationMs;
        if ((witness.status != diagnostic::WatchStatus::Watching && witness.status != diagnostic::WatchStatus::Expired) ||
            !(witness.identity == identity) || !boundedWindow || witness.functionBegin != target.functionBegin ||
            witness.functionEnd != target.functionEnd || witness.matchingOriginalReturns == 0 ||
            witness.matchingOriginalReturns > witness.matchingEntries || witness.matchingEntries > witness.entries ||
            witness.matchingOriginalReturns > witness.originalReturns || witness.originalReturns > witness.entries ||
            witness.lastMatchingCaller <= target.functionBegin || witness.lastMatchingCaller >= target.functionEnd)
            return reject("same-controller-passive-return-witness-unavailable");
    }
    out["admissionRoute"] = exactBase ? "exact-installed-base-target" : "same-scope-passive-normal-return";
    out["controller"] = ObjectJson(first.player, nullptr);
    out["world"] = ObjectJson(first.world, nullptr);
    out["localPlayer"] = ObjectJson(first.localPlayer, nullptr);
    out["function"] = ObjectJson(first.fov.function, nullptr);
    if (!exactBase) out["passiveEvidence"] = WitnessJson(witness, identity, true, "unavailable");
    out["targetInspection"] = TargetJson(identity.target, nullptr); // no second provider mutation between final sample and call
    out["targetBoundsBegin"] = std::format("0x{:X}", target.functionBegin);
    out["targetBoundsEnd"] = std::format("0x{:X}", target.functionEnd);
    out["exeCheck"] = hook.exeCheck;
    FovFrame frame;
    constexpr std::uint32_t sentinel = 0x7FC12345;
    frame.parameters.ReturnValue = std::bit_cast<float>(sentinel);
    // No JSON pointer is consumed. Reacquire the complete guarded sample after
    // providers have run, immediately before spending the one process entry.
    Sample immediate{};
    if (!ue3::runtime::Guarded(Read, &immediate) || !SameProbeSample(first, immediate, gameThreadId))
        return reject("identity-or-metadata-changed-before-entry");
    bool expected = false;
    if (!gFovProbeEntered.compare_exchange_strong(expected, true)) return reject("process-probe-entry-already-spent");
    bool returned = false;
    try {
        DispatchFov(immediate, frame); // NOT guarded by read-only SEH; no native-fault recovery claim.
        returned = true;
    } catch (...) {
        // Ordinary C++ unwinding only (/EHsc). Entry is spent even on exception.
    }
    std::uint32_t const bits = std::bit_cast<std::uint32_t>(frame.parameters.ReturnValue);
    bool const finite = std::isfinite(frame.parameters.ReturnValue);
    bool const canaries = frame.before == 0xC173AB52 && frame.after == 0x5A92E8C1;
    Sample after{};
    // Do not traverse engine state after an exception or damaged local frame.
    bool const postRead = returned && canaries && ue3::runtime::Guarded(Read, &after);
    bool const postIdentity = postRead && SameProbeSample(immediate, after, gameThreadId);
    out["entered"] = true;
    out["processEntrySpent"] = true;
    out["returnedNormally"] = returned;
    out["initialBits"] = std::format("0x{:08X}", sentinel);
    out["returnBits"] = std::format("0x{:08X}", bits);
    out["returnChanged"] = bits != sentinel;
    out["returnFinite"] = finite;
    if (finite) out["returnValue"] = frame.parameters.ReturnValue;
    out["canariesIntact"] = canaries;
    out["beforeCanaryBits"] = std::format("0x{:08X}", frame.before);
    out["afterCanaryBits"] = std::format("0x{:08X}", frame.after);
    out["postIdentityReadAvailable"] = postRead;
    out["postIdentityUnchanged"] = postIdentity;
    if (postRead) {
        out["postController"] = ObjectJson(after.player, nullptr);
        out["postFunction"] = ObjectJson(after.fov.function, nullptr);
        out["postSlotTarget"] = std::format("0x{:X}", after.processEvent.target);
    }
    bool const boundedResult = returned && bits != sentinel && finite && canaries && postIdentity;
    result.status = boundedResult ? "dispatched" : "error";
    out["outcome"] = boundedResult ? "bounded-scalar-return-observed" : "uncertain";
    out["reason"] = boundedResult ? "scalar-return-canaries-and-current-identity-observed" : "entered-probe-result-uncertain-no-retry";
    return result;
}

namespace {
struct MenuEntrySample { MenuSample menu; SlotSample slot; bool splash{}; };
void ReadMenuEntry(void* context) {
    auto& out = *static_cast<MenuEntrySample*>(context);
    ReadMenu(&out.menu);
    if (out.menu.contextAvailable) ReadValidatedSlot(out.splash ? out.menu.splash.handler : out.menu.main.handler, out.slot);
}
MenuPanel const& EntryPanel(MenuEntrySample const& sample) { return sample.splash ? sample.menu.splash : sample.menu.main; }
bool MenuEntryReady(MenuEntrySample const& sample) {
    auto const& menu = sample.menu; auto const& panel = EntryPanel(sample); auto const& fn = panel.function;
    if (!menu.contextAvailable || !menu.worldLinksKnown || !PanelInteractive(panel) || !sample.slot.available ||
        !fn.function.available || !fn.parametersComplete || !fn.layoutMatches || fn.nativeIndex != 0 ||
        fn.functionFlags != (sample.splash ? 0x20802u : 0x20002u)) return false;
    if (sample.splash) return !panel.pressedStart && std::isfinite(panel.legalTimeout);
    return !panel.waitingDlc && !panel.waitingSaves && !panel.connecting && panel.checkingComplete && panel.hasSaves;
}
bool SameObjectSample(ObjectSample const& a, ObjectSample const& b) {
    return a.available == b.available && a.address == b.address && a.index == b.index && a.classAddress == b.classAddress;
}
bool SameMenuEntry(MenuEntrySample const& a, MenuEntrySample const& b) {
    if (!MenuEntryReady(b) || a.splash != b.splash || a.slot.object != b.slot.object ||
        a.slot.vtable != b.slot.vtable || a.slot.target != b.slot.target) return false;
    auto const& x = a.menu; auto const& y = b.menu;
    if (x.panelsData != y.panelsData || x.panelCount != y.panelCount || x.focus != y.focus ||
        !SameObjectSample(x.root.engine, y.root.engine) || !SameObjectSample(x.root.world, y.root.world) ||
        !SameObjectSample(x.root.localPlayer, y.root.localPlayer) || !SameObjectSample(x.root.player, y.root.player) ||
        !SameObjectSample(x.viewport, y.viewport) || !SameObjectSample(x.manager, y.manager) ||
        !SameObjectSample(x.worldInfo, y.worldInfo) || !SameObjectSample(x.controllerWorldInfo, y.controllerWorldInfo)) return false;
    auto const& p = EntryPanel(a); auto const& q = EntryPanel(b);
    if (!SameObjectSample(p.panel, q.panel) || !SameObjectSample(p.handler, q.handler) || !SameObjectSample(p.worldInfo, q.worldInfo) ||
        p.legalTimeout != q.legalTimeout || p.pressedStart != q.pressedStart || p.waitingDlc != q.waitingDlc ||
        p.waitingSaves != q.waitingSaves || p.connecting != q.connecting || p.checkingComplete != q.checkingComplete ||
        p.hasSaves != q.hasSaves || p.careerHasSaves != q.careerHasSaves) return false;
    auto const& f = p.function; auto const& g = q.function;
    if (!SameObjectSample(f.function, g.function) || f.functionFlags != g.functionFlags || f.nativeIndex != g.nativeIndex ||
        f.parmsSize != g.parmsSize || f.numParms != g.numParms || f.returnOffset != g.returnOffset || f.parameterCount != g.parameterCount) return false;
    for (unsigned i = 0; i < f.parameterCount; ++i) {
        auto const& u = f.parameters[i]; auto const& v = g.parameters[i];
        if (!SameObjectSample(u.property, v.property) || u.flags != v.flags || u.offset != v.offset ||
            u.arrayDim != v.arrayDim || u.elementSize != v.elementSize || std::strcmp(u.name, v.name) != 0) return false;
    }
    return true;
}
static_assert(sizeof(UBioSFHandler_MainMenu_execMoveToLoadGui_Parms) == 1);
static_assert(offsetof(UBioSFHandler_MainMenu_execMoveToLoadGui_Parms, LoadMode) == 0);
struct MenuFrame {
    unsigned char bytes[9]{};
    MenuFrame() {
        std::uint32_t const before = 0xC173AB52, after = 0x5A92E8C1;
        UBioSFHandler_MainMenu_execMoveToLoadGui_Parms input{}; input.LoadMode = 0;
        std::memcpy(bytes, &before, 4); std::memcpy(bytes + 4, &input, 1); std::memcpy(bytes + 5, &after, 4);
    }
    bool Intact() const {
        std::uint32_t before{}, after{}; std::memcpy(&before, bytes, 4); std::memcpy(&after, bytes + 5, 4);
        return before == 0xC173AB52 && after == 0x5A92E8C1 && bytes[4] == 0;
    }
};
static_assert(sizeof(MenuFrame) == 9);
void DispatchMenu(MenuEntrySample const& sample, MenuFrame& frame) {
    auto const& panel = EntryPanel(sample);
    void* parameters = sample.splash ? nullptr : frame.bytes + 4;
#if defined(MEVR_LE2_OBSERVATION_TEST)
    gMenuDispatcher(reinterpret_cast<void*>(panel.handler.address), reinterpret_cast<void*>(panel.function.function.address), parameters);
#else
    reinterpret_cast<UObject*>(panel.handler.address)->ProcessEvent(
        reinterpret_cast<UFunction*>(panel.function.function.address), parameters, nullptr);
#endif
}
ProbeResult MenuAction(std::uint32_t gameThreadId, ObserverServices const& services, bool splash) {
    ProbeResult result{"rejected", json::Value::Object()}; auto& out = result.payload;
    out["schemaVersion"] = 1; out["game"] = "LE2"; out["action"] = splash ? "advance-splash" : "open-load-menu";
    out["entered"] = false; out["fixtureLoaded"] = false; out["normalUiTransitionProven"] = false;
    auto reject = [&](char const* reason) { out["reason"] = reason; return result; };
    auto& latch = splash ? gSplashEntered : gLoadMenuEntered;
    if (gameThreadId == 0 || GetCurrentThreadId() != gameThreadId) return reject("not-on-verified-game-thread");
    if (latch.load()) return reject("process-menu-action-entry-already-spent");
    if (services.inspectTarget == nullptr || services.readHookSnapshot == nullptr) return reject("host-target-services-unavailable");
    MenuEntrySample first{}; first.splash = splash;
    if (!ue3::runtime::Guarded(ReadMenuEntry, &first)) return reject("guarded-menu-read-fault");
    if (!MenuEntryReady(first)) return reject("active-menu-world-or-exact-metadata-not-ready");
    auto const hook = services.readHookSnapshot();
    auto const target = services.inspectTarget(first.slot.target);
    // Controller forwarding witnesses do not establish this handler's route.
    // Only the exact owned base is admitted for these two menu transitions.
    if (!ProbeTargetValid(target, hook, first.slot.target) || first.slot.target != hook.target || !target.sameAsBase)
        return reject("menu-handler-is-not-validated-owned-base-route");
    auto const currentHook = services.readHookSnapshot();
    if (!ProbeTargetValid(target, currentHook, first.slot.target) || hook.target != currentHook.target ||
        hook.detour != currentHook.detour || hook.nextHop != currentHook.nextHop)
        return reject("owned-hook-chain-changed-before-entry");
    out["handler"] = ObjectJson(EntryPanel(first).handler, nullptr);
    out["function"] = FunctionJson(EntryPanel(first).function, nullptr, splash ? Declaration::AdvanceSplash : Declaration::OpenLoad);
    out["handlerTarget"] = std::format("0x{:X}", first.slot.target);
    out["route"] = "exact-owned-base";
    out["parameterBytes"] = splash ? 0 : 1;
    MenuFrame frame;
    MenuEntrySample immediate{}; immediate.splash = splash;
    // Final guarded identities/function/property/slot resample follows every
    // provider and serialization call. No JSON, file I/O or provider before entry.
    if (!ue3::runtime::Guarded(ReadMenuEntry, &immediate) || !SameMenuEntry(first, immediate))
        return reject("menu-identity-metadata-or-slot-changed-before-entry");
    bool expected = false;
    if (!latch.compare_exchange_strong(expected, true)) return reject("process-menu-action-entry-already-spent");
    bool returned = false;
    try { DispatchMenu(immediate, frame); returned = true; } // No read-only SEH around engine execution.
    catch (...) { /* C++ unwinding only; the entry remains spent. */ }
    bool const intact = frame.Intact();
    out["entered"] = true; out["returnedNormally"] = returned; out["parameterCanariesApplicable"] = !splash;
    if (!splash) out["parameterFrameIntact"] = intact;
    out["entrySpent"] = true; out["retryAllowed"] = false;
    result.status = returned && intact ? "dispatched" : "error";
    out["reason"] = returned && intact ? "normal-return-observed-transition-unverified" : "entered-menu-action-uncertain-no-retry";
    if (returned && intact) {
        // Focus changes, disappearance and an asynchronous new panel are expected
        // possibilities, not identity-corruption failures. Observe separately.
        out["postObservation"] = ObserveSaveMenu(gameThreadId);
        if (json::Write(out, 0).size() > kSaveMenuPayloadBytes) {
            out["postObservation"]["records"] = json::Value::Array();
            out["postObservation"]["listComplete"] = false;
            out["postObservation"]["reason"] = "action-post-observation-budget";
        }
        if (json::Write(out, 0).size() > kSaveMenuPayloadBytes) {
            out["postObservation"] = json::Value::Object();
            out["postObservation"]["reason"] = "action-post-context-budget";
        }
    } else {
        out["postObservation"] = json::Value::Object();
        out["postObservation"]["reason"] = "unavailable-after-uncertain-entry";
    }
    return result;
}
} // namespace
ProbeResult AdvanceSplash(std::uint32_t gameThreadId, ObserverServices const& services) { return MenuAction(gameThreadId, services, true); }
ProbeResult OpenLoadMenu(std::uint32_t gameThreadId, ObserverServices const& services) { return MenuAction(gameThreadId, services, false); }

json::Value Observe(std::uint32_t gameThreadId, host::process_event::diagnostic::Snapshot const* hostSnapshot,
                    ObserverServices const* services) {
    Sample sample{};
    char const* failure = nullptr;
    if (gameThreadId == 0 || GetCurrentThreadId() != gameThreadId) failure = "not-on-verified-game-thread";
    else if (!ue3::runtime::Guarded(Read, &sample)) failure = "guarded-engine-read-fault";
    if (failure == nullptr && !ue3::runtime::Guarded(ReadAllocator, &sample.allocator)) {
        sample.allocator = {};
        sample.allocator.reason = "guarded-allocator-read-fault";
    }
    auto const identity = WatchInput(sample, gameThreadId, failure);
    host::process_event::diagnostic::WitnessSnapshot witness{};
    bool const onThread = gameThreadId != 0 && GetCurrentThreadId() == gameThreadId;
    bool const supplied = onThread && services != nullptr && services->observeController != nullptr;
    // Invalid context on the correct thread disarms the existing scope. An
    // off-thread observation does not invoke any host provider.
    if (supplied) witness = services->observeController(identity);
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
    out["consoleCommandMetadata"] = FunctionJson(sample.console, failure, Declaration::Console);
    out["getFovAngleMetadata"] = FunctionJson(sample.fov, failure, Declaration::Fov);
    out["processEventSlot"] = SlotJson(sample.processEvent, failure, hostSnapshot, services);
    auto comparisons = json::Value::Object();
    comparisons["scope"] = "slot72-on-already-validated-objects-only";
    comparisons["durableObjectIdentity"] = false;
    comparisons["world"] = SlotJson(sample.worldSlot, failure, hostSnapshot, services, &sample.world);
    comparisons["engine"] = SlotJson(sample.engineSlot, failure, hostSnapshot, services, &sample.engine);
    comparisons["localPlayer"] = SlotJson(sample.localPlayerSlot, failure, hostSnapshot, services, &sample.localPlayer);
    comparisons["player"] = SlotJson(sample.processEvent, failure, hostSnapshot, services, &sample.player);
    comparisons["pawn"] = SlotJson(sample.pawnSlot, failure, hostSnapshot, services, &sample.pawn);
    out["processEventSlots"] = std::move(comparisons);
    out["allocatorObservation"] = AllocatorJson(sample.allocator, failure, services);
    out["controllerCallWitness"] = WitnessJson(witness, identity, supplied,
        onThread ? "controller-witness-provider-not-supplied" : "not-on-verified-game-thread");
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
