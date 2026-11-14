// SPDX-License-Identifier: MIT
// Original synthetic exercise, not game evidence. Uses SDK declarations from
// the submodule, with fake globals/read helpers; never loads a game or its data.
#include <LESDK/Headers.hpp>
#include "engine/Le2DiagnosticObservation.hpp"
#include "engine/Legacy.hpp"
#include "ue3/Runtime.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

TArray<UObject*>* UObject::GObjObjects{};
SFXNameEntry const** SFXName::GBioNamePools{};
UEngine** GEngine{};
UWorld** GWorld{};
namespace {
std::array<SFXNameEntry const*, 64> names{};
unsigned nextName = 1;
std::vector<std::unique_ptr<unsigned char[]>> textStorage;
template<class T> struct Raw {
    alignas(T) unsigned char bytes[sizeof(T)]{};
    T* get() { return reinterpret_cast<T*>(bytes); }
};
void Name(UObject* p, char const* text) {
    auto memory = std::make_unique<unsigned char[]>(sizeof(SFXNameEntry) + 128);
    std::memset(memory.get(), 0, sizeof(SFXNameEntry) + 128);
    auto* entry = reinterpret_cast<SFXNameEntry*>(memory.get());
    std::memcpy(entry->AnsiName, text, std::strlen(text) + 1);
    p->Name.Offset = nextName;
    names[nextName++] = entry;
    textStorage.push_back(std::move(memory));
}
void Check(bool ok, char const* why) { if (!ok) throw std::runtime_error(why); }
bool Available(mevr::json::Value const& v, char const* key) { return v.Get(key).Bool("available"); }
}
SFXNameEntry const* SFXName::GetEntry() const noexcept { return names[Offset]; }
namespace mevr::ue3::runtime {
bool Guarded(void (*fn)(void*), void* ctx) {
    __try { fn(ctx); return true; }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
}
}
namespace mevr::engine::legacy {
void const* World() { return GWorld ? *GWorld : nullptr; }
std::int32_t ObjectCount() { return UObject::GObjObjects ? static_cast<int>(UObject::GObjObjects->Count()) : 0; }
ue3::Object* LocalPlayerController() { return reinterpret_cast<ue3::Object*>((*GEngine)->GamePlayers.GetData()[0]->Actor); }
}
int main() {
    try {
        using mevr::engine::le2_diagnostic::Observe;
        Check(Observe(0).String("reason") == "not-on-verified-game-thread", "zero thread refuses");
        auto const thread = GetCurrentThreadId();
        Check(Observe(thread + 1).String("reason") == "not-on-verified-game-thread", "wrong thread refuses");
        Check(!Available(Observe(thread), "objectCount"), "absent globals unavailable");
        std::array<UObject*, 64> slots{};
        mevr::ue3::TArray<UObject*> table{slots.data(), 0, static_cast<int>(slots.size())};
        UObject::GObjObjects = reinterpret_cast<TArray<UObject*>*>(&table);
        Raw<UObject> corePackage, enginePackage, mapPackage;
        Raw<UClass> meta, packageType, engineType, worldType, localType, controllerType, pawnType, childPawnType;
        Raw<UEngine> engine;
        Raw<UWorld> world;
        Raw<ULocalPlayer> local;
        Raw<APlayerController> controller;
        Raw<APawn> pawn;
        auto add = [&](UObject* object, char const* name, UClass* type, UObject* outer) {
            object->ObjectInternalInteger = table.count;
            slots[table.count++] = object;
            object->Class = type; object->Outer = outer; Name(object, name);
        };
        add(corePackage.get(), "Core", packageType.get(), nullptr);
        add(enginePackage.get(), "Engine", packageType.get(), nullptr);
        add(mapPackage.get(), "SyntheticMap", packageType.get(), nullptr);
        add(meta.get(), "Class", meta.get(), corePackage.get());
        add(packageType.get(), "Package", meta.get(), corePackage.get());
        add(engineType.get(), "Engine", meta.get(), enginePackage.get());
        add(worldType.get(), "World", meta.get(), enginePackage.get());
        add(localType.get(), "LocalPlayer", meta.get(), enginePackage.get());
        add(controllerType.get(), "PlayerController", meta.get(), enginePackage.get());
        add(pawnType.get(), "Pawn", meta.get(), enginePackage.get());
        add(childPawnType.get(), "ChildPawn", meta.get(), enginePackage.get());
        childPawnType.get()->SuperField = pawnType.get();
        add(engine.get(), "engine", engineType.get(), nullptr);
        add(world.get(), "world", worldType.get(), mapPackage.get());
        add(local.get(), "local", localType.get(), nullptr);
        add(controller.get(), "controller", controllerType.get(), nullptr);
        add(pawn.get(), "pawn", childPawnType.get(), nullptr);
        SFXNameEntry const* pools[8]{names[1]};
        SFXName::GBioNamePools = pools;
        UWorld* worldPtr = world.get(); GWorld = &worldPtr;
        UEngine* enginePtr = engine.get(); GEngine = &enginePtr;
        ULocalPlayer* players[1]{local.get()};
        mevr::ue3::TArray<ULocalPlayer*> playerArray{players, 1, 1};
        std::memcpy(&engine.get()->GamePlayers, &playerArray, sizeof(playerArray));
        local.get()->Actor = controller.get(); controller.get()->Pawn = pawn.get();
        pawn.get()->Location = {1, 2, 3};
        auto v = Observe(thread);
        for (auto key : {"objectCount", "world", "engine", "localPlayer", "player", "pawn", "map", "position"})
            Check(Available(v, key), key);
        Check(v.Get("map").String("value") == "SyntheticMap", "map package identity");
        Check(v.Get("position").Number("y") == 2, "finite position");
        Check(!v.Bool("ready") && v.String("nativeValidation") == "pending", "never runtime readiness");
        auto wire = mevr::json::Write(v, 0);
        Check(wire.size() < 16384 && mevr::json::Parse(wire).has_value(), "bounded valid JSON fits control payload");
        Check(!Available(v, "consoleCommandMetadata") &&
              v.Get("consoleCommandMetadata").String("reason") == "console-command-declaration-missing", "missing function explicit");
        // Synthetic reflection hierarchy and the exact packed SDK signature.
        Raw<UClass> fieldType, structType, functionType, propertyType, strType, boolType;
        Raw<UFunction> console, duplicate;
        Raw<UStrProperty> command, result;
        Raw<UBoolProperty> writeLog;
        Raw<UField> unrelated;
        Raw<UProperty> localProperty;
        add(fieldType.get(), "Field", meta.get(), corePackage.get());
        add(structType.get(), "Struct", meta.get(), corePackage.get());
        add(functionType.get(), "Function", meta.get(), corePackage.get());
        add(propertyType.get(), "Property", meta.get(), corePackage.get());
        add(strType.get(), "StrProperty", meta.get(), corePackage.get());
        add(boolType.get(), "BoolProperty", meta.get(), corePackage.get());
        structType.get()->SuperField = fieldType.get();
        functionType.get()->SuperField = structType.get();
        propertyType.get()->SuperField = fieldType.get();
        strType.get()->SuperField = propertyType.get();
        boolType.get()->SuperField = propertyType.get();
        add(console.get(), "ConsoleCommand", functionType.get(), controllerType.get());
        add(duplicate.get(), "ConsoleCommand", functionType.get(), controllerType.get());
        add(command.get(), "Command", strType.get(), console.get());
        add(writeLog.get(), "bWriteToLog", boolType.get(), console.get());
        add(result.get(), "ReturnValue", strType.get(), console.get());
        add(unrelated.get(), "OtherField", fieldType.get(), controllerType.get());
        add(localProperty.get(), "LocalScratch", propertyType.get(), console.get());
        controllerType.get()->Children = unrelated.get(); unrelated.get()->Next = console.get();
        console.get()->Children = command.get(); command.get()->Next = writeLog.get(); writeLog.get()->Next = result.get();
        console.get()->FunctionFlags = 0x24400; console.get()->iNative = 0;
        console.get()->NumParms = 3; console.get()->ParmsSize = 0x24; console.get()->ReturnValueOffset = 0x14;
        auto property = [](UProperty* p, int offset, int size, std::uint64_t flags) {
            p->ArrayDim = 1; p->Offset = offset; p->ElementSize = size; p->PropertyFlags = flags;
        };
        property(command.get(), 0, 16, 0x400080);
        property(writeLog.get(), 0x10, 4, 0x90); writeLog.get()->BitMask = 1;
        property(result.get(), 0x14, 16, 0x400580);
        auto metadata = [&] { return Observe(thread).Get("consoleCommandMetadata"); };
        auto matching = [&] { return metadata().Bool("layoutMatchesSdkDeclaration"); };
        auto m = metadata();
        Check(m.Bool("available") && matching(), "matching packed ConsoleCommand signature");
        Check(m.Number("parmsSize") == 0x24 && m.Number("numParms") == 3 && m.Number("returnValueOffset") == 0x14,
              "actual function header captured");
        Check(m.Get("function").String("class") == "Core.Function" && m.Get("parameters").Items().size() == 3,
              "actual function and property identities captured");
        Check(m.Get("parameters").Items()[1].String("boolMask") == "0x00000001", "exact bool mask observed");
        Check(!m.Bool("dispatchCapability") && m.String("nativeCallValidation") == "pending", "schema never grants dispatch");
        Check(mevr::json::Write(Observe(thread), 0).size() < 16384, "metadata result fits control frame");
        console.get()->ParmsSize = 0x28;
        Check(metadata().Bool("available") && !matching(), "well-formed wrong packed size is visible mismatch");
        console.get()->ParmsSize = 0x24;
        console.get()->NumParms = 2; Check(!matching(), "wrong NumParms"); console.get()->NumParms = 3;
        console.get()->ReturnValueOffset = 0x10; Check(!matching(), "wrong return offset"); console.get()->ReturnValueOffset = 0x14;
        console.get()->FunctionFlags = 0;
        Check(!metadata().Bool("functionFlagsMatchSdkDeclaration"), "function flags compared independently");
        console.get()->FunctionFlags = 0x24400;
        console.get()->iNative = 123;
        Check(metadata().Number("nativeIndex") == 123 && metadata().String("nativeIndexValidation") == "pending", "native index observed, not assumed zero");
        console.get()->iNative = 0;
        writeLog.get()->BitMask = 2;
        Check(metadata().Bool("available") && !matching(), "different valid bool mask is exact comparison mismatch");
        writeLog.get()->BitMask = 3;
        Check(!metadata().Bool("available"), "multiple-bit bool mask malformed"); writeLog.get()->BitMask = 1;
        writeLog.get()->Class = strType.get(); Check(!matching(), "wrong property type"); writeLog.get()->Class = boolType.get();
        command.get()->ArrayDim = -1; Check(!metadata().Bool("available"), "negative ArrayDim"); command.get()->ArrayDim = 1;
        result.get()->Offset = 0x20; Check(!metadata().Bool("available"), "out of call-block span"); result.get()->Offset = 0x14;
        result.get()->Offset = 0x10; Check(!metadata().Bool("available"), "overlapping storage"); result.get()->Offset = 0x14;
        result.get()->PropertyFlags = 0x400080; Check(!matching(), "missing return/out flags"); result.get()->PropertyFlags = 0x400580;
        auto resultName = result.get()->Name; result.get()->Name = command.get()->Name;
        Check(!metadata().Bool("available"), "duplicate parameter names"); result.get()->Name = resultName;
        // Child list order is not the parameter offsets' order and need not be.
        console.get()->Children = result.get(); result.get()->Next = writeLog.get(); writeLog.get()->Next = command.get(); command.get()->Next = nullptr;
        Check(matching(), "valid reordered child list matched by name and offset");
        console.get()->Children = command.get(); command.get()->Next = writeLog.get(); writeLog.get()->Next = result.get(); result.get()->Next = nullptr;
        result.get()->Next = localProperty.get();
        Check(matching(), "non-parameter local excluded from call block");
        localProperty.get()->Next = localProperty.get();
        Check(!metadata().Bool("available") && metadata().String("reason") == "function-child-cycle-or-limit", "non-param cycle bounded");
        localProperty.get()->Next = nullptr; result.get()->Next = nullptr;
        unrelated.get()->Next = unrelated.get();
        Check(!metadata().Bool("available") && metadata().String("reason") == "owner-child-cycle-or-limit", "owner child cycle bounded");
        unrelated.get()->Next = console.get();
        console.get()->Next = duplicate.get();
        Check(!metadata().Bool("available"), "duplicate declaration refused"); console.get()->Next = nullptr;
        command.get()->Outer = duplicate.get();
        Check(!metadata().Bool("available"), "foreign function child refused"); command.get()->Outer = console.get();
        auto propertyIndex = command.get()->ObjectInternalInteger;
        command.get()->ObjectInternalInteger = table.count;
        Check(!metadata().Bool("available"), "stale property slot refused"); command.get()->ObjectInternalInteger = propertyIndex;
        console.get()->ObjectFlags = 0x200;
        Check(!metadata().Bool("available"), "template function not relaxed"); console.get()->ObjectFlags = 0;
        console.get()->Children = reinterpret_cast<UField*>(1);
        Check(Observe(thread).String("reason") == "guarded-engine-read-fault", "metadata access violation guarded");
        console.get()->Children = command.get();
        Check(matching(), "next fresh sample recovers after malformed metadata");
        auto oldIndex = pawn.get()->ObjectInternalInteger;
        pawn.get()->ObjectInternalInteger = table.count;
        Check(!Available(Observe(thread), "pawn"), "out of range slot");
        pawn.get()->ObjectInternalInteger = oldIndex;
        slots[oldIndex] = controller.get();
        Check(!Available(Observe(thread), "position"), "slot pointer mismatch");
        slots[oldIndex] = pawn.get();
        for (auto flag : {0x200ULL, 0x400ULL, 0x2000000000000000ULL}) {
            pawn.get()->ObjectFlags = flag;
            Check(!Available(Observe(thread), "pawn"), "template and pending kill refused");
        }
        pawn.get()->ObjectFlags = 0;
        pawn.get()->Class = worldType.get();
        Check(!Available(Observe(thread), "pawn"), "wrong pawn type");
        pawn.get()->Class = childPawnType.get();
        childPawnType.get()->SuperField = childPawnType.get();
        Check(!Available(Observe(thread), "pawn"), "class cycle bounded");
        childPawnType.get()->SuperField = pawnType.get();
        pawn.get()->Location.X = std::numeric_limits<float>::quiet_NaN();
        Check(Available(Observe(thread), "pawn") && !Available(Observe(thread), "position"), "NaN location unavailable");
        pawn.get()->Location.X = 1;
        controller.get()->Pawn = nullptr;
        Check(Available(Observe(thread), "player") && !Available(Observe(thread), "pawn"), "null pawn preserves controller sample");
        controller.get()->Pawn = pawn.get();
        local.get()->Actor = nullptr;
        Check(Available(Observe(thread), "localPlayer") && !Available(Observe(thread), "player") &&
              !Available(Observe(thread), "position"), "null controller does not imply playable state");
        local.get()->Actor = controller.get();
        playerArray.max = 0;
        std::memcpy(&engine.get()->GamePlayers, &playerArray, sizeof(playerArray));
        Check(!Available(Observe(thread), "localPlayer"), "invalid local-player array header");
        playerArray.max = 1;
        std::memcpy(&engine.get()->GamePlayers, &playerArray, sizeof(playerArray));
        meta.get()->Class = packageType.get();
        Check(!Available(Observe(thread), "world") && !Available(Observe(thread), "player"), "invalid class metatype");
        meta.get()->Class = meta.get();
        auto oldCapacity = table.max; table.max = table.count - 1;
        Check(!Available(Observe(thread), "objectCount"), "invalid table header");
        table.max = oldCapacity;
        mapPackage.get()->Name.Number = 1;
        Check(!Available(Observe(thread), "map"), "instanced package name not silently normalized");
        mapPackage.get()->Name.Number = 0;
        worldPtr = reinterpret_cast<UWorld*>(1);
        v = Observe(thread);
        Check(v.String("reason") == "guarded-engine-read-fault" && !Available(v, "objectCount"), "AV invalidates whole sample");
        std::cout << "Synthetic typed-chain, liveness, metadata, bounds, finite location and SEH checks passed\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
