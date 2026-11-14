// SPDX-License-Identifier: MIT
// Original synthetic exercise, not game evidence. Uses SDK declarations from
// the submodule, with fake globals/read helpers; never loads a game or its data.
#include <LESDK/Headers.hpp>
#include "engine/Le2DiagnosticObservation.hpp"
#include "engine/Legacy.hpp"
#include "engine/Addresses.hpp"
#include "plugin/ProcessEventDiagnostic.hpp"
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
FMallocLike** GMalloc{};
namespace {
std::array<SFXNameEntry const*, 256> names{};
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
void MustNeverCall() { std::abort(); }
namespace diagnostic = mevr::host::process_event::diagnostic;
unsigned inspectCalls{}, watchCalls{};
bool wrongInspectionTarget{}, wrongWitnessIdentity{}, expiredWitness{};
diagnostic::ControllerWatch lastWatch{};
std::uint64_t watchEpoch{};
diagnostic::TargetInspection FakeInspect(std::uintptr_t target) noexcept {
    ++inspectCalls;
    diagnostic::TargetInspection out;
    out.target = target + (wrongInspectionTarget ? 1 : 0);
    out.moduleBase = target - 0x1000; out.rva = 0x1000;
    out.baseTarget = target - 0x100;
    out.inMainText = out.boundsKnown = out.exactFunctionStart = out.boundsUsable = true;
    out.functionBegin = target; out.functionEnd = target + 64;
    out.reason = "synthetic-bounds";
    return out;
}
diagnostic::WitnessSnapshot FakeWitness(diagnostic::ControllerWatch const& identity) noexcept {
    ++watchCalls;
    if (!(lastWatch == identity)) ++watchEpoch;
    lastWatch = identity;
    diagnostic::WitnessSnapshot out;
    out.identity = identity; out.epoch = watchEpoch;
    if (wrongWitnessIdentity) ++out.identity.object;
    out.status = !identity.object ? diagnostic::WatchStatus::Unavailable :
        expiredWitness ? diagnostic::WatchStatus::Expired : diagnostic::WatchStatus::Watching;
    out.reason = "synthetic-passive-witness";
    out.startedMs = 100; out.expiresMs = 10100;
    out.functionBegin = identity.target; out.functionEnd = identity.target + 64;
    out.entries = std::numeric_limits<std::uint64_t>::max(); out.originalReturns = 7;
    out.matchingEntries = 3; out.matchingOriginalReturns = 2;
    out.lastMatchingCaller = identity.target + 8;
    return out;
}
std::uintptr_t probeTarget{};
bool probeExactBase{}, probeBadHook{}, probeBadBounds{}, probeNoReturns{}, probeMutateBeforeEntry{}, probeWrongBase{}, probeBadExeCheck{};
UFunction* probeGetter{};
UProperty* replacementFovProperty{};
bool replaceBeforeEntry{};
unsigned dispatchCalls{};
unsigned engineReadCalls{}, readsAtDispatch{};
USFXSFHandler_Load* mutatingMenu{};
unsigned menuWorldReads{};
int dispatchMode{};
diagnostic::TargetInspection ProbeInspect(std::uintptr_t target) noexcept {
    auto out = FakeInspect(target);
    out.rva = probeExactBase ? mevr::engine::addresses::kProcessEvent : 0xE0000;
    out.moduleBase = target - out.rva;
    out.baseTarget = out.moduleBase + mevr::engine::addresses::kProcessEvent;
    out.sameAsBase = probeExactBase;
    if (probeBadBounds) out.boundsUsable = false;
    return out;
}
diagnostic::Snapshot ProbeHook() noexcept {
    diagnostic::Snapshot out;
    auto const detail = ProbeInspect(probeTarget);
    out.target = detail.baseTarget; out.detour = out.target + 1; out.nextHop = out.target + 2;
    out.installation = diagnostic::InstallState::Installed;
    out.chain = {probeBadHook ? diagnostic::ChainStatus::Changed : diagnostic::ChainStatus::Matched, 2};
    out.exeCheck = "AtRvaUnchecked"; out.backend = "synthetic";
    if (probeWrongBase) ++out.target;
    if (probeBadExeCheck) out.exeCheck = "BytesDiffer";
    return out;
}
diagnostic::WitnessSnapshot ProbeWitness(diagnostic::ControllerWatch const& identity) noexcept {
    auto out = FakeWitness(identity);
    if (probeNoReturns) out.matchingOriginalReturns = 0;
    if (probeMutateBeforeEntry) probeGetter->ParmsSize = 8;
    if (replaceBeforeEntry) probeGetter->Children = replacementFovProperty;
    return out;
}
unsigned menuDispatchCalls{}, menuReadsAtDispatch{};
int menuDispatchMode{};

UMassEffectGuiManager* flowManager{}; USFXGameModeManager* flowModes{}; ABioWorldInfo* flowWorld{};
UBioSFPanel* flowBrowserPanel{}; UBioSFPanel* flowSavePanel{}; UBioSFHandler_PCPauseMenu* flowBrowser{};
USFXGameModeDefault* flowDefault{}; UFunction* flowPauseFunction{}; UFunction* flowSaveFunction{};
UBioSFPanel* flowPanels[2]{}; FBWPageStruct* flowPages{}; FSubPageState* flowOverrides{};
UObjectProperty* flowManagerProperty{}; UClass* flowWrongClass{};
unsigned flowDispatchCalls{}, flowReadsAtDispatch{}; int flowBehavior{}, flowMutation{};
void MutateFlowSample() { flowWorld->bPlayersOnly = !flowWorld->bPlayersOnly; }
diagnostic::TargetInspection FlowInspect(std::uintptr_t target) noexcept {
    auto result = ProbeInspect(target);
    if (flowMutation == 1) flowPages[1].Type = 3;
    if (flowMutation == 2) flowOverrides[0].State = 2;
    if (flowMutation == 3) flowManagerProperty->PropertyClass = flowWrongClass;
    if (flowMutation == 4) flowModes->CurrentMode = 0;
    if (flowMutation == 5) flowSaveFunction->FunctionFlags = 0;
    return result;
}
void FakeSaveFlowDispatch(void* object, void* function, void* parameters) {
    ++flowDispatchCalls; flowReadsAtDispatch = engineReadCalls;
    bool const pause = function == flowPauseFunction;
    Check(object == (pause ? static_cast<void*>(flowDefault) : static_cast<void*>(flowBrowser)), "flow current exact receiver");
    Check(pause || function == flowSaveFunction, "flow exact selected declaration");
    if (flowBehavior == 1) throw std::runtime_error("synthetic uncertain flow dispatch");
    if (pause) {
        Check(parameters == nullptr, "ShowMenu uses null no-param frame");
        flowModes->CurrentMode = 7; flowWorld->bPlayersOnly = 1;
        flowPanels[0] = flowBrowserPanel; flowManager->m_FocusPanel = flowBrowserPanel;
    } else {
        UBioSFHandler_BrowserWheel_eventShowSelectedPanel_Parms p{}; std::memcpy(&p, parameters, sizeof(p));
        Check(p.nPanel == 1 && p.oManager == nullptr, "Save page derived row and null optional own manager");
        auto* bytes = static_cast<unsigned char*>(parameters);
        if (flowBehavior == 2) bytes[-1] ^= 1;
        if (flowBehavior == 3) bytes[12] ^= 1;
        if (flowBehavior == 4) bytes[4] = 1;
        if (flowBehavior >= 2) return;
        flowPanels[0] = flowSavePanel; flowManager->m_FocusPanel = flowSavePanel;
    }
    mevr::ue3::TArray<UBioSFPanel*> h{flowPanels,1,2};
    std::memcpy(&flowManager->m_lstPanels,&h,sizeof(h));
}
void* expectedMenuHandler{}; void* expectedMenuFunction{};
bool expectedNoParameters{}, mutateMenuOnInspection{}, replaceMenuProperty{};
UProperty* replacementMenuProperty{};
UBioSFPanel* actionPanel{}; UMassEffectGuiManager* actionManager{};
UFunction* actionFunction{};
diagnostic::TargetInspection MenuInspect(std::uintptr_t target) noexcept {
    auto out = ProbeInspect(target);
    if (mutateMenuOnInspection) actionFunction->FunctionFlags = 0;
    if (replaceMenuProperty) actionFunction->Children = replacementMenuProperty;
    return out;
}
void FakeMenuDispatch(void* handler, void* function, void* parameters) {
    ++menuDispatchCalls; menuReadsAtDispatch = engineReadCalls;
    Check(handler == expectedMenuHandler && function == expectedMenuFunction, "hardcoded menu receiver/function");
    Check((parameters == nullptr) == expectedNoParameters, "empty splash params versus exactly one opener byte");
    if (parameters != nullptr) Check(*static_cast<unsigned char*>(parameters) == 0, "fixed normal LoadMode zero");
    if (menuDispatchMode == 2) throw std::runtime_error("synthetic menu exception");
    if (menuDispatchMode == 3 || menuDispatchMode == 4) {
        auto* bytes = static_cast<unsigned char*>(parameters);
        bytes[menuDispatchMode == 3 ? 1 : -1] ^= 0xff;
    }
    if (menuDispatchMode == 5) *static_cast<unsigned char*>(parameters) = 1;
    if (menuDispatchMode == 0) { actionPanel->IsVisible = 0; actionManager->m_FocusPanel = nullptr; }
}
unsigned fixtureDispatchCalls{}, fixtureReadsAtDispatch{}, expectedFixtureRow{};
int fixtureDispatchMode{}, fixtureMutation{};
UObject* fixtureHandler{}; UFunction* fixtureFunction{}; UMassEffectGuiManager* fixtureManager{};
UBioSFPanel* fixturePanel{}; FSaveGUIRecord* fixtureRecords{};
UObjectProperty* fixtureManagerProperty{}; UClass* alternateManagerClass{};
std::wstring* fixtureMutableText{}; std::wstring* fixtureMutableOther{};
std::uintptr_t* fixtureVtable{};
diagnostic::TargetInspection FixtureInspect(std::uintptr_t target) noexcept {
    auto out = ProbeInspect(target);
    if (fixtureMutation == 1) (*fixtureMutableText)[0] ^= 1;
    if (fixtureMutation == 2) {
        std::memcpy(&fixtureRecords[1].SaveDescriptor, &fixtureRecords[expectedFixtureRow].SaveDescriptor, sizeof(FSFXSaveDescriptor));
    }
    if (fixtureMutation == 3) fixtureManagerProperty->PropertyClass = alternateManagerClass;
    if (fixtureMutation == 4) ++fixtureVtable[72];
    if (fixtureMutation == 5) (*fixtureMutableOther)[0] ^= 1;
    return out;
}
void FakeFixtureDispatch(void* handler, void* function, void* parameters) {
    ++fixtureDispatchCalls; fixtureReadsAtDispatch = engineReadCalls;
    Check(handler == fixtureHandler && function == fixtureFunction && parameters != nullptr, "only frozen fixture normal LoadGame route");
    Check(reinterpret_cast<std::uintptr_t>(parameters) % alignof(USFXSFHandler_Load_execLoadGame_Parms) == 0, "actual SDK parameter alignment");
    USFXSFHandler_Load_execLoadGame_Parms input{};
    std::memcpy(&input, parameters, sizeof(input));
    Check(input.SaveIdx == static_cast<int>(expectedFixtureRow) && input.GuiMan == fixtureManager, "packed actual current row and typed same manager");
    if (fixtureDispatchMode == 2) throw std::runtime_error("synthetic load exception");
    auto* bytes = static_cast<unsigned char*>(parameters);
    if (fixtureDispatchMode == 3) bytes[12] ^= 1;
    if (fixtureDispatchMode == 4) bytes[-1] ^= 1;
    if (fixtureDispatchMode == 5) bytes[0] ^= 1;
    if (fixtureDispatchMode == 6) bytes[4] ^= 1;
    if (fixtureDispatchMode == 0) { fixtureManager->m_FocusPanel = nullptr; fixturePanel->IsVisible = 0; }
}
ASFXInventoryManager* resourceInventory{};
APawn* resourcePawnForMutation{};
int resourceMutation{};
void MutateResourceSample() {
    if (resourceMutation == 1) ++resourceInventory->Probes;
    if (resourceMutation == 2) resourceInventory->Owner = nullptr;
    if (resourceMutation == 3) resourcePawnForMutation->Location.X += 1;
    if (resourceMutation == 4) resourceInventory->CurrentFuel = std::numeric_limits<float>::quiet_NaN();
}
void FakeFovDispatch(void* controller, void* function, void* parameters) {
    ++dispatchCalls;
    readsAtDispatch = engineReadCalls;
    Check(function == probeGetter, "hardcoded getter function passed to test dispatcher");
    auto* value = static_cast<float*>(parameters);
    if (dispatchMode == 1) return;
    if (dispatchMode == 2) throw std::runtime_error("synthetic dispatch exception");
    *value = dispatchMode == 6 ? std::numeric_limits<float>::infinity() : 75.0f;
    if (dispatchMode == 3 || dispatchMode == 4) {
        std::uint32_t corrupt = 0;
        std::memcpy(static_cast<unsigned char*>(parameters) + (dispatchMode == 3 ? 4 : -4), &corrupt, sizeof(corrupt));
    }
    if (dispatchMode == 5) ++static_cast<UObject*>(controller)->ObjectInternalInteger;
    if (dispatchMode == 7) probeGetter->iNative = 1;
    if (dispatchMode == 8) probeGetter->Children = replacementFovProperty;
}
}
SFXNameEntry const* SFXName::GetEntry() const noexcept { return names[Offset]; }
namespace mevr::ue3::runtime {
bool Guarded(void (*fn)(void*), void* ctx) {
    __try { fn(ctx); return true; }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return false; }
}
}
namespace mevr::engine::legacy {
void const* World() { ++engineReadCalls; if (mutatingMenu != nullptr && ++menuWorldReads == 2) mutatingMenu->bEnumeratingSaves = 1; return GWorld ? *GWorld : nullptr; }
std::int32_t ObjectCount() { ++engineReadCalls; return UObject::GObjObjects ? static_cast<int>(UObject::GObjObjects->Count()) : 0; }
ue3::Object* LocalPlayerController() { return reinterpret_cast<ue3::Object*>((*GEngine)->GamePlayers.GetData()[0]->Actor); }
}
int main() {
    try {
        using mevr::engine::le2_diagnostic::Observe;
        using mevr::engine::le2_diagnostic::ObserveSaveMenu;
        Check(Observe(0).String("reason") == "not-on-verified-game-thread", "zero thread refuses");
        auto const thread = GetCurrentThreadId();
        Check(Observe(thread + 1).String("reason") == "not-on-verified-game-thread", "wrong thread refuses");
        Check(!Available(Observe(thread), "objectCount"), "absent globals unavailable");
        std::array<UObject*, 256> slots{};
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
        Check(!Available(v, "getFovAngleMetadata") &&
              v.Get("getFovAngleMetadata").String("reason") == "get-fov-angle-declaration-missing", "missing getter explicit");
        Check(!Available(v, "processEventSlot") && !Available(v, "allocatorObservation"), "missing vtable and allocator explicit");
        // Real SDK object shape; invented vtable slots. Neither method may run.
        std::array<std::uintptr_t, 73> controllerVtable{};
        auto* controllerVtablePtr = controllerVtable.data();
        std::memcpy(controller.get(), &controllerVtablePtr, sizeof(controllerVtablePtr));
        controllerVtable[71] = reinterpret_cast<std::uintptr_t>(&MustNeverCall);
        Check(!Available(Observe(thread), "processEventSlot"), "adjacent executable slot is not slot72");
        controllerVtable[72] = reinterpret_cast<std::uintptr_t>(&MustNeverCall);
        auto slot = Observe(thread).Get("processEventSlot");
        Check(slot.Bool("available") && slot.Number("slotByteOffset") == 0x240 &&
              slot.Get("target").Bool("committedExecutablePage") && !slot.Get("target").Bool("identityValidated") &&
              !slot.Bool("dispatchCapability"), "slot72 observed without target identity or dispatch");
        Check(!slot.Get("hostProvenance").Bool("available") && !slot.Bool("targetMatchesInstalledHook"), "omitted host provenance is not fabricated");
        namespace diagnostic = mevr::host::process_event::diagnostic;
        diagnostic::Snapshot host;
        host.installation = diagnostic::InstallState::Installed;
        host.target = controllerVtable[72]; host.detour = host.target + 1; host.nextHop = host.target + 2;
        host.backend = "synthetic"; host.exeCheck = "AtRvaUnchecked";
        host.chain = {diagnostic::ChainStatus::Matched, 2};
        auto hostSlot = [&] { return Observe(thread, &host).Get("processEventSlot"); };
        Check(hostSlot().Bool("targetMatchesInstalledHook") && hostSlot().Bool("slotAndOwnedChainMatch") &&
              !hostSlot().Bool("dispatchCapability"), "exact slot plus recorded host chain remains observation only");
        Check(hostSlot().Get("hostProvenance").String("exeCheck") == "AtRvaUnchecked" &&
              !hostSlot().Get("hostProvenance").Bool("pristineEngineCodeValidated"), "preserves structural exe check and next-hop limitation");
        ++host.target; Check(!hostSlot().Bool("targetMatchesInstalledHook"), "different executable address is not the installed target"); --host.target;
        host.chain.status = diagnostic::ChainStatus::Changed;
        Check(hostSlot().Bool("targetMatchesInstalledHook") && !hostSlot().Bool("slotAndOwnedChainMatch"), "slot identity and changing chain are separate");
        host.chain.status = diagnostic::ChainStatus::Matched;
        host.installation = diagnostic::InstallState::InstallFailed;
        Check(!hostSlot().Bool("targetMatchesInstalledHook"), "failed install cannot validate a matching address");
        host.installation = diagnostic::InstallState::Installed;
        Check(!Observe(0, &host).Get("processEventSlot").Bool("slotAndOwnedChainMatch"), "wrong thread cannot match provenance");
        std::array<std::array<std::uintptr_t, 73>, 4> comparisonVtables{};
        std::array<UObject*, 4> comparisonObjects{world.get(), engine.get(), local.get(), pawn.get()};
        for (std::size_t i = 0; i < comparisonObjects.size(); ++i) {
            comparisonVtables[i][72] = controllerVtable[72];
            auto* vtable = comparisonVtables[i].data();
            std::memcpy(comparisonObjects[i], &vtable, sizeof(vtable));
        }
        auto comparisons = [&] { return Observe(thread, &host).Get("processEventSlots"); };
        for (auto key : {"world", "engine", "localPlayer", "player", "pawn"}) {
            auto observed = comparisons().Get(key);
            Check(observed.Bool("available") && observed.Get("object").Bool("available") &&
                  observed.Bool("targetMatchesInstalledHook") && !observed.Bool("dispatchCapability"), "validated object slot72 comparison");
        }
        comparisonVtables[0][71] = controllerVtable[72]; comparisonVtables[0][72] = 0;
        Check(!comparisons().Get("world").Bool("available") && comparisons().Get("player").Bool("available"), "world adjacent-slot trap does not affect controller");
        comparisonVtables[0][72] = controllerVtable[72] + 1;
        Check(comparisons().Get("world").Bool("available") && !comparisons().Get("world").Bool("targetMatchesInstalledHook"), "different world target remains discrepancy");
        comparisonVtables[0][72] = controllerVtable[72];
        auto* invalidComparisonVtable = reinterpret_cast<std::uintptr_t*>(1);
        std::memcpy(world.get(), &invalidComparisonVtable, sizeof(invalidComparisonVtable));
        Check(!comparisons().Get("world").Bool("available") && comparisons().Get("player").Bool("available"), "world vtable fault contained");
        world.get()->ObjectFlags = 0x200;
        Check(comparisons().Get("world").String("reason") == "object-not-validated", "unvalidated world never dereferences bad vtable");
        world.get()->ObjectFlags = 0;
        auto* worldVtable = comparisonVtables[0].data(); std::memcpy(world.get(), &worldVtable, sizeof(worldVtable));
        controllerVtable[72] = reinterpret_cast<std::uintptr_t>(&table);
        Check(Available(Observe(thread), "processEventSlot") &&
              !Observe(thread).Get("processEventSlot").Get("target").Bool("committedExecutablePage"), "readable data target is not executable");
        controllerVtable[72] = reinterpret_cast<std::uintptr_t>(&MustNeverCall);
        auto* badVtable = reinterpret_cast<std::uintptr_t*>(1);
        std::memcpy(controller.get(), &badVtable, sizeof(badVtable));
        v = Observe(thread);
        Check(Available(v, "player") && !Available(v, "processEventSlot") &&
              v.Get("processEventSlot").String("reason") == "guarded-process-event-slot-read-fault", "bad vtable isolated and guarded");
        std::memcpy(controller.get(), &controllerVtablePtr, sizeof(controllerVtablePtr));
        std::array<std::uintptr_t, 4> allocatorVtable{};
        auto* allocatorVtablePtr = allocatorVtable.data();
        auto* allocator = reinterpret_cast<FMallocLike*>(&allocatorVtablePtr);
        GMalloc = &allocator;
        allocatorVtable[1] = allocatorVtable[3] = reinterpret_cast<std::uintptr_t>(&MustNeverCall);
        auto allocation = Observe(thread).Get("allocatorObservation");
        Check(allocation.Bool("available") && allocation.Number("mallocSlotByteOffset") == 8 &&
              allocation.Number("freeSlotByteOffset") == 24 && !allocation.Bool("allocationCapability"), "allocator exact slots without calls");
        allocatorVtable[3] = 0; allocatorVtable[2] = reinterpret_cast<std::uintptr_t>(&MustNeverCall);
        Check(!Available(Observe(thread), "allocatorObservation"), "Realloc slot cannot stand in for Free");
        allocatorVtable[3] = reinterpret_cast<std::uintptr_t>(&table);
        Check(!Observe(thread).Get("allocatorObservation").Get("freeTarget").Bool("committedExecutablePage"), "allocator data target remains unvalidated");
        allocator = nullptr; Check(!Available(Observe(thread), "allocatorObservation"), "null allocator instance");
        allocator = reinterpret_cast<FMallocLike*>(1);
        v = Observe(thread);
        Check(Available(v, "player") && !Available(v, "allocatorObservation") &&
              v.Get("allocatorObservation").String("reason") == "guarded-allocator-read-fault", "allocator instance access fault contained");
        GMalloc = reinterpret_cast<FMallocLike**>(1);
        Check(!Available(Observe(thread), "allocatorObservation"), "allocator global access fault contained");
        Check(Observe(0).String("reason") == "not-on-verified-game-thread", "wrong thread does not read invalid allocator");
        GMalloc = &allocator; allocator = reinterpret_cast<FMallocLike*>(&allocatorVtablePtr);
        allocatorVtablePtr = nullptr; Check(!Available(Observe(thread), "allocatorObservation"), "null allocator vtable");
        allocatorVtablePtr = reinterpret_cast<std::uintptr_t*>(1);
        Check(!Available(Observe(thread), "allocatorObservation"), "allocator vtable access fault contained");
        allocatorVtablePtr = allocatorVtable.data(); allocatorVtable[3] = allocatorVtable[1];
        Check(Available(Observe(thread), "allocatorObservation"), "fresh allocator sample recovers");
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
        Raw<UClass> floatType;
        Raw<UFunction> fov, duplicateFov;
        Raw<UFloatProperty> fovReturn;
        add(floatType.get(), "FloatProperty", meta.get(), corePackage.get());
        floatType.get()->SuperField = propertyType.get();
        add(fov.get(), "GetFOVAngle", functionType.get(), controllerType.get());
        add(duplicateFov.get(), "GetFOVAngle", functionType.get(), controllerType.get());
        add(fovReturn.get(), "ReturnValue", floatType.get(), fov.get());
        console.get()->Next = fov.get(); fov.get()->Children = fovReturn.get();
        fov.get()->FunctionFlags = 0x20802; fov.get()->iNative = 0;
        fov.get()->NumParms = 1; fov.get()->ParmsSize = 4; fov.get()->ReturnValueOffset = 0;
        property(fovReturn.get(), 0, 4, 0x580);
        auto getter = [&] { return Observe(thread).Get("getFovAngleMetadata"); };
        auto getterMatches = [&] { return getter().Bool("declarationMatchesExpected"); };
        Check(getterMatches() && getter().Bool("nativeIndexMatchesExpected") && !getter().Bool("dispatchCapability"), "exact scalar getter metadata only");
        Check(matching(), "getter metadata coexists with console signature");
        fov.get()->iNative = 1; Check(!getterMatches(), "getter nonzero native index refuses match"); fov.get()->iNative = 0;
        fov.get()->FunctionFlags = 0x24400; Check(!getterMatches(), "native Console flags are not getter flags"); fov.get()->FunctionFlags = 0x20802;
        fov.get()->ParmsSize = 8; Check(!getterMatches(), "wrong getter size"); fov.get()->ParmsSize = 4;
        fov.get()->NumParms = 0; Check(!getterMatches(), "wrong getter count"); fov.get()->NumParms = 1;
        fov.get()->ReturnValueOffset = 4; Check(!getterMatches(), "wrong getter return offset"); fov.get()->ReturnValueOffset = 0;
        fovReturn.get()->PropertyFlags = 0x80; Check(!getterMatches(), "getter missing out/return flags"); fovReturn.get()->PropertyFlags = 0x580;
        fovReturn.get()->Class = strType.get(); Check(!getterMatches(), "getter wrong property type"); fovReturn.get()->Class = floatType.get();
        fov.get()->Class = structType.get(); Check(!getter().Bool("available"), "getter must be exact Core.Function"); fov.get()->Class = functionType.get();
        fovReturn.get()->Offset = 1; Check(!getter().Bool("available"), "getter return exceeds block"); fovReturn.get()->Offset = 0;
        fovReturn.get()->ArrayDim = 2; Check(!getter().Bool("available"), "getter is not an array"); fovReturn.get()->ArrayDim = 1;
        fovReturn.get()->Outer = console.get(); Check(!getter().Bool("available"), "getter foreign parameter"); fovReturn.get()->Outer = fov.get();
        fovReturn.get()->Next = fovReturn.get(); Check(!getter().Bool("available"), "getter parameter cycle"); fovReturn.get()->Next = nullptr;
        fov.get()->Next = duplicateFov.get(); Check(!getter().Bool("available"), "duplicate getter declaration"); fov.get()->Next = nullptr;
        fov.get()->ObjectFlags = 0x200; Check(!getter().Bool("available"), "template getter refused"); fov.get()->ObjectFlags = 0;
        Check(getterMatches() && mevr::json::Write(Observe(thread), 0).size() < 16384, "expanded observation recovers and fits control payload");
        Check(fov.get()->iNative == 0 && fov.get()->FunctionFlags == 0x20802 && console.get()->FunctionFlags == 0x24400,
              "observation never rewrites function flags or native index");
        mevr::engine::le2_diagnostic::ObserverServices services{FakeInspect, FakeWitness};
        auto provided = [&] { return Observe(thread, &host, &services); };
        v = provided();
        auto evidence = v.Get("controllerCallWitness");
        Check(evidence.Bool("available") && evidence.String("baseEntries") == "18446744073709551615" &&
              evidence.String("originalNormalReturns") == "7" && evidence.String("targetCallerEntries") == "3" &&
              evidence.String("targetCallerOriginalNormalReturns") == "2" && !evidence.Bool("dispatchCapability") &&
              !evidence.Bool("durableObjectIdentity"), "separate passive counts, exact integers, no capability");
        Check(lastWatch.object == reinterpret_cast<std::uintptr_t>(controller.get()) && lastWatch.index == controller.get()->ObjectInternalInteger &&
              lastWatch.classAddress == reinterpret_cast<std::uintptr_t>(controllerType.get()) && lastWatch.target == controllerVtable[72] &&
              lastWatch.world == reinterpret_cast<std::uintptr_t>(world.get()) && lastWatch.localPlayer == reinterpret_cast<std::uintptr_t>(local.get()) &&
              lastWatch.gameThread == thread, "fresh guarded controller/context feeds watcher");
        for (auto key : {"world", "engine", "localPlayer", "player", "pawn"}) {
            auto inspection = v.Get("processEventSlots").Get(key).Get("target").Get("inspection");
            Check(inspection.Bool("available") && inspection.String("rva") == "0x1000" && inspection.Bool("boundsUsable"), "injected target facts on five validated objects");
        }
        auto initialEpoch = evidence.String("epoch");
        Check(provided().Get("controllerCallWitness").String("epoch") == initialEpoch, "same scope does not force epoch renewal");
        expiredWitness = true;
        Check(provided().Get("controllerCallWitness").String("status") == "expired", "expired evidence reported without rearming");
        expiredWitness = false;
        wrongInspectionTarget = true;
        Check(!provided().Get("processEventSlot").Get("target").Get("inspection").Bool("available"), "mismatched inspector output not attributed to target");
        wrongInspectionTarget = false;
        wrongWitnessIdentity = true;
        Check(!provided().Get("controllerCallWitness").Bool("available") &&
              provided().Get("controllerCallWitness").String("reason") == "witness-identity-mismatch", "stale witness identity rejected");
        wrongWitnessIdentity = false;
        auto priorInspects = inspectCalls, priorWatches = watchCalls;
        v = Observe(0, &host, &services);
        Check(inspectCalls == priorInspects && watchCalls == priorWatches && !v.Get("controllerCallWitness").Bool("available"), "wrong thread invokes no provider");
        console.get()->Children = reinterpret_cast<UField*>(1);
        v = provided();
        Check(inspectCalls == priorInspects && watchCalls == priorWatches + 1 && lastWatch.object == 0 &&
              !v.Get("controllerCallWitness").Bool("available"), "metadata fault disarms without inspecting partial samples");
        console.get()->Children = command.get();
        Check(provided().Get("controllerCallWitness").Bool("available"), "fresh scope after read failure");
        world.get()->ObjectFlags = 0x200;
        Check(!provided().Get("controllerCallWitness").Bool("available") && lastWatch.object == 0, "invalid world disarms despite readable controller");
        world.get()->ObjectFlags = 0;
        auto representative = provided();
        auto const observationBytes = mevr::json::Write(representative, 0).size();
        auto envelope = mevr::json::Value::Object();
        envelope["observation"] = std::move(representative);
        envelope["reservedHostEnvelope"] = std::string(1024, 'x');
        auto const envelopeBytes = mevr::json::Write(envelope, 0).size();
        Check(envelopeBytes <= 16384, "full five-object observation leaves 1024 bytes for host envelope");
        std::cout << "Representative full Observe JSON: " << observationBytes << " bytes; with reserved envelope: " << envelopeBytes << " bytes\n";
        using mevr::engine::le2_diagnostic::ProbeFov;
        using mevr::engine::le2_diagnostic::testing::ResetFovProbe;
        probeTarget = controllerVtable[72]; probeGetter = fov.get();
        Raw<UFloatProperty> replacementFovReturn;
        add(replacementFovReturn.get(), "ReturnValue", floatType.get(), fov.get());
        property(replacementFovReturn.get(), 0, 4, 0x580);
        replacementFovProperty = replacementFovReturn.get();
        mevr::engine::le2_diagnostic::ObserverServices probeServices{ProbeInspect, ProbeWitness, ProbeHook};
        auto resetProbe = [&] { ResetFovProbe(FakeFovDispatch); dispatchCalls = 0; dispatchMode = 0; };
        resetProbe();
        Check(ProbeFov(0, probeServices).status == "rejected" && dispatchCalls == 0, "probe wrong thread rejects before entry");
        Check(ProbeFov(thread, {}).status == "rejected" && dispatchCalls == 0, "probe providers required");
        fov.get()->Children = reinterpret_cast<UField*>(1);
        Check(ProbeFov(thread, probeServices).status == "rejected" && dispatchCalls == 0, "probe metadata access fault rejects before entry");
        fov.get()->Children = fovReturn.get();
        fov.get()->iNative = 1;
        Check(ProbeFov(thread, probeServices).status == "rejected" && dispatchCalls == 0, "probe does not clear nonzero native index");
        fov.get()->iNative = 0;
        probeBadHook = true; Check(ProbeFov(thread, probeServices).status == "rejected", "changed hook chain refuses probe"); probeBadHook = false;
        probeWrongBase = true; Check(ProbeFov(thread, probeServices).status == "rejected", "wrong installed base refuses probe"); probeWrongBase = false;
        probeBadExeCheck = true; Check(ProbeFov(thread, probeServices).status == "rejected", "unexpected executable check refuses probe"); probeBadExeCheck = false;
        probeBadBounds = true; Check(ProbeFov(thread, probeServices).status == "rejected", "unusable bounds refuse probe"); probeBadBounds = false;
        probeNoReturns = true; Check(ProbeFov(thread, probeServices).status == "rejected", "base entry alone is insufficient"); probeNoReturns = false;
        wrongWitnessIdentity = true; Check(ProbeFov(thread, probeServices).status == "rejected", "stale witness refuses probe"); wrongWitnessIdentity = false;
        probeMutateBeforeEntry = true;
        Check(ProbeFov(thread, probeServices).payload.String("reason") == "identity-or-metadata-changed-before-entry" && dispatchCalls == 0,
              "immediate resample catches provider-time metadata change");
        probeMutateBeforeEntry = false; fov.get()->ParmsSize = 4;
        replaceBeforeEntry = true;
        Check(ProbeFov(thread, probeServices).payload.String("reason") == "identity-or-metadata-changed-before-entry" && dispatchCalls == 0,
              "same-layout replacement return property rejects before entry");
        replaceBeforeEntry = false; fov.get()->Children = fovReturn.get();
        expiredWitness = true;
        auto probe = ProbeFov(thread, probeServices);
        Check(probe.status == "dispatched" && probe.payload.Number("returnValue") == 75 && probe.payload.Bool("canariesIntact") &&
              probe.payload.Bool("postIdentityUnchanged") && dispatchCalls == 1 &&
              probe.payload.Get("passiveEvidence").String("status") == "expired", "expired same-scope bounded witness admits one scalar proof");
        Check(ProbeFov(thread, probeServices).status == "rejected" && dispatchCalls == 1, "successful process entry never repeats");
        expiredWitness = false;
        resetProbe(); probeExactBase = true;
        auto exactServices = probeServices; exactServices.observeController = nullptr;
        Check(ProbeFov(thread, exactServices).status == "dispatched" && dispatchCalls == 1, "exact installed base needs no override witness");
        probeExactBase = false;
        auto const controllerIndex = controller.get()->ObjectInternalInteger;
        for (int mode : {1, 2, 3, 4, 5, 6, 7, 8}) {
            resetProbe(); dispatchMode = mode;
            probe = ProbeFov(thread, probeServices);
            Check(probe.status == "error" && probe.payload.Bool("entered") && probe.payload.String("outcome") == "uncertain" && dispatchCalls == 1,
                  "post-entry exception/return/canary/identity issue remains uncertain");
            Check(ProbeFov(thread, probeServices).status == "rejected" && dispatchCalls == 1, "uncertain entered call cannot retry");
            if (mode == 2 || mode == 3 || mode == 4)
                Check(!probe.payload.Bool("postIdentityReadAvailable") && engineReadCalls == readsAtDispatch,
                      "no engine traversal after exception or damaged canary");
            controller.get()->ObjectInternalInteger = controllerIndex; fov.get()->iNative = 0; fov.get()->Children = fovReturn.get();
        }
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
        Check(!comparisons().Get("pawn").Bool("available") && comparisons().Get("player").Bool("available"), "optional pawn absent without invalidating other slots");
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
        // Read-only menu fixtures: all objects/classes are synthetic SDK storage.
        Raw<UObject> sfxPackage;
        Raw<UClass> viewportType, managerType, panelType, handlerType, loadType, mainType, splashType, pcSplashType, byteType;
        Raw<UGameViewportClient> viewport;
        Raw<UMassEffectGuiManager> manager;
        Raw<UBioSFPanel> loadPanel, mainPanel, splashPanel, otherPanel;
        Raw<USFXSFHandler_Load> loadHandler, competingLoad;
        Raw<UBioSFHandler_MainMenu> mainHandler;
        Raw<UBioSFHandler_PCSplash> splashHandler;
        Raw<UFunction> openLoad, advanceSplash, normalLoad;
        Raw<UClass> intType, objectPropertyType;
        Raw<UIntProperty> saveIdxProperty;
        Raw<UObjectProperty> guiManagerProperty;
        Raw<UByteProperty> loadModeProperty;
        add(sfxPackage.get(), "SFXGame", packageType.get(), nullptr);
        add(viewportType.get(), "GameViewportClient", meta.get(), enginePackage.get());
        add(managerType.get(), "MassEffectGuiManager", meta.get(), sfxPackage.get());
        add(panelType.get(), "BioSFPanel", meta.get(), enginePackage.get());
        add(handlerType.get(), "BioSFHandler", meta.get(), enginePackage.get());
        add(loadType.get(), "SFXSFHandler_Load", meta.get(), sfxPackage.get());
        add(mainType.get(), "BioSFHandler_MainMenu", meta.get(), sfxPackage.get());
        add(splashType.get(), "BioSFHandler_Splash", meta.get(), sfxPackage.get());
        add(pcSplashType.get(), "BioSFHandler_PCSplash", meta.get(), sfxPackage.get());
        add(byteType.get(), "ByteProperty", meta.get(), corePackage.get());
        byteType.get()->SuperField = propertyType.get();
        loadType.get()->SuperField = mainType.get()->SuperField = splashType.get()->SuperField = handlerType.get();
        pcSplashType.get()->SuperField = splashType.get();
        add(viewport.get(), "MenuViewport", viewportType.get(), nullptr);
        add(manager.get(), "MenuManager", managerType.get(), nullptr);
        add(loadPanel.get(), "LoadPanel", panelType.get(), nullptr);
        add(mainPanel.get(), "MainPanel", panelType.get(), nullptr);
        add(splashPanel.get(), "SplashPanel", panelType.get(), nullptr);
        add(otherPanel.get(), "OtherPanel", panelType.get(), nullptr);
        add(loadHandler.get(), "LoadHandler", loadType.get(), nullptr);
        add(competingLoad.get(), "CompetingLoad", loadType.get(), nullptr);
        add(mainHandler.get(), "MainHandler", mainType.get(), nullptr);
        add(splashHandler.get(), "SplashHandler", pcSplashType.get(), nullptr);
        add(openLoad.get(), "MoveToLoadGui", functionType.get(), mainType.get());
        add(advanceSplash.get(), "AdvanceScreen", functionType.get(), pcSplashType.get());
        add(loadModeProperty.get(), "LoadMode", byteType.get(), openLoad.get());
        add(intType.get(), "IntProperty", meta.get(), corePackage.get()); intType.get()->SuperField = propertyType.get();
        add(objectPropertyType.get(), "ObjectProperty", meta.get(), corePackage.get()); objectPropertyType.get()->SuperField = propertyType.get();
        add(normalLoad.get(), "LoadGame", functionType.get(), loadType.get()); loadType.get()->Children = normalLoad.get();
        add(saveIdxProperty.get(), "SaveIdx", intType.get(), normalLoad.get());
        add(guiManagerProperty.get(), "GuiMan", objectPropertyType.get(), normalLoad.get());
        normalLoad.get()->Children = saveIdxProperty.get(); saveIdxProperty.get()->Next = guiManagerProperty.get();
        normalLoad.get()->NumParms = 2; normalLoad.get()->ParmsSize = 12; normalLoad.get()->FunctionFlags = 0x820003;
        normalLoad.get()->ReturnValueOffset = -1;
        property(saveIdxProperty.get(), 0, 4, 0x80); property(guiManagerProperty.get(), 4, 8, 0x80);
        guiManagerProperty.get()->PropertyClass = managerType.get();
        mainType.get()->Children = openLoad.get(); pcSplashType.get()->Children = advanceSplash.get();
        openLoad.get()->FunctionFlags = 0x20002; openLoad.get()->NumParms = 1; openLoad.get()->ParmsSize = 1;
        openLoad.get()->ReturnValueOffset = -1; openLoad.get()->Children = loadModeProperty.get();
        property(loadModeProperty.get(), 0, 1, 0x80);
        advanceSplash.get()->FunctionFlags = 0x20802; advanceSplash.get()->ReturnValueOffset = -1;
        auto tag = [&](UBioSFPanel* panel, char const* value) {
            Raw<UObject> named; Name(named.get(), value); panel->nmTag = named.get()->Name;
        };
        tag(loadPanel.get(), "Load"); tag(mainPanel.get(), "MainMenu"); tag(splashPanel.get(), "Splash"); tag(otherPanel.get(), "Other");
        std::array<UBioSFPanel*, 4> menuPanels{loadPanel.get(), mainPanel.get(), splashPanel.get(), otherPanel.get()};
        for (auto* panel : menuPanels) {
            panel->oParentManager = manager.get(); panel->IsVisible = panel->bInitialized = 1;
        }
        loadPanel.get()->m_DefaultHandler = loadHandler.get(); loadHandler.get()->oPanel = loadPanel.get();
        mainPanel.get()->m_DefaultHandler = mainHandler.get(); mainHandler.get()->oPanel = mainPanel.get();
        splashPanel.get()->m_DefaultHandler = splashHandler.get(); splashHandler.get()->oPanel = splashPanel.get();
        competingLoad.get()->oPanel = loadPanel.get();
        local.get()->ViewportClient = viewport.get(); viewport.get()->m_pScaleFormManager = manager.get();
        controller.get()->m_oCachedScaleFormManager = manager.get();
        manager.get()->m_FocusPanel = loadPanel.get();
        mevr::ue3::TArray<UBioSFPanel*> panelsHeader{menuPanels.data(), 3, 4};
        std::memcpy(&manager.get()->m_lstPanels, &panelsHeader, sizeof(panelsHeader));
        loadHandler.get()->GuiMode = 1; loadHandler.get()->LoadMode = 0; loadHandler.get()->CurrentCareerIndex = -1;
        loadHandler.get()->bCheckingDataComplete = loadHandler.get()->bFromMainMenu = 1;
        mainHandler.get()->bCheckingDataComplete = mainHandler.get()->bHasSaves = 1;
        auto menu = [&] { return ObserveSaveMenu(thread); };
        auto menuMeta = [&] { return menu().Get("context").Get("mainMenu").Get("moveToLoadGui"); };
        Check(menu().Get("context").Bool("available") && menu().Bool("listComplete"), "valid reachable empty load list");
        auto loadMetadata = [&] { return menu().Get("context").Get("load").Get("loadGame"); };
        Check(loadMetadata().Bool("layoutMatchesSdkDeclaration") && loadMetadata().Bool("functionFlagsMatchSdkDeclaration"), "normal LoadGame packed12byte declaration observed only");
        guiManagerProperty.get()->PropertyClass = viewportType.get();
        Check(!loadMetadata().Bool("layoutMatchesSdkDeclaration"), "same-size foreign GuiMan target class rejected");
        guiManagerProperty.get()->PropertyClass = reinterpret_cast<UClass*>(1);
        Check(!menu().Get("context").Bool("available"), "invalid ObjectProperty target class guarded");
        guiManagerProperty.get()->PropertyClass = managerType.get();
        saveIdxProperty.get()->Offset = 4;
        Check(!loadMetadata().Bool("available"), "overlapping native LoadGame parameters rejected"); saveIdxProperty.get()->Offset = 0;
        Check(menu().Get("context").Get("load").Number("currentCareerIndex") == -1, "legal default career index preserved");
        Check(menuMeta().Bool("layoutMatchesSdkDeclaration") && menuMeta().Bool("functionFlagsMatchSdkDeclaration"), "one-byte normal menu opener metadata");
        Check(menu().Get("context").Get("splash").Get("advanceScreen").Bool("layoutMatchesSdkDeclaration"), "effective PC splash override metadata");
        openLoad.get()->ReturnValueOffset = 0;
        Check(menuMeta().Bool("layoutMatchesSdkDeclaration") && menuMeta().Number("returnValueOffset") == 0, "void offset observed without inventing expected constant");
        loadModeProperty.get()->PropertyFlags = 0x580;
        Check(!menuMeta().Bool("layoutMatchesSdkDeclaration"), "return property cannot masquerade as byte input");
        loadModeProperty.get()->PropertyFlags = 0x80;
        loadModeProperty.get()->PropertyFlags = 0x400;
        Check(!menuMeta().Bool("available"), "stray return flag without Parm rejected for void declaration");
        loadModeProperty.get()->PropertyFlags = 0x80;
        openLoad.get()->ParmsSize = 2; Check(!menuMeta().Bool("layoutMatchesSdkDeclaration"), "incorrect opener block size"); openLoad.get()->ParmsSize = 1;
        pcSplashType.get()->Children = nullptr;
        Check(!menu().Get("context").Get("splash").Get("advanceScreen").Bool("available"), "no fallback from missing PC override to guessed base");
        pcSplashType.get()->Children = advanceSplash.get();
        std::array<Raw<FSaveGUIRecord>, 13> saveRecords;
        static_assert(sizeof(Raw<FSaveGUIRecord>) == sizeof(FSaveGUIRecord));
        std::wstring career = L"SyntheticCareer", filename = L"SyntheticSave", unicodeName = L"Save\u00e9\U0001f680";
        auto setText = [](FString& field, std::wstring& value) {
            mevr::ue3::TArray<wchar_t> header{value.data(), static_cast<int>(value.size() + 1), static_cast<int>(value.size() + 1)};
            std::memcpy(&field, &header, sizeof(header));
        };
        for (auto& raw : saveRecords) {
            setText(raw.get()->SaveDescriptor.Career, career); setText(raw.get()->SaveDescriptor.SaveName, filename);
            raw.get()->SaveDescriptor.Index = static_cast<int>(&raw - saveRecords.data()) + 42;
        }
        mevr::ue3::TArray<FSaveGUIRecord> saveHeader{saveRecords[0].get(), 1, 13};
        auto setSaves = [&] { std::memcpy(&loadHandler.get()->SaveList, &saveHeader, sizeof(saveHeader)); };
        setSaves();
        mutatingMenu = loadHandler.get(); menuWorldReads = 0;
        auto changedMenu = menu();
        Check(changedMenu.String("reason") == "menu-context-changed-or-reread-failed" &&
              !changedMenu.Get("context").Bool("available") && changedMenu.Get("records").Items().empty(), "second-sample enumeration mutation invalidates copied records and readiness");
        mutatingMenu = nullptr; loadHandler.get()->bEnumeratingSaves = 0;
        Raw<UClass> worldInfoType, bioWorldInfoType, derivedMainType;
        Raw<ABioWorldInfo> menuWorldInfo;
        Raw<UFunction> nearerOpen;
        add(worldInfoType.get(), "WorldInfo", meta.get(), enginePackage.get());
        add(bioWorldInfoType.get(), "BioWorldInfo", meta.get(), sfxPackage.get());
        bioWorldInfoType.get()->SuperField = worldInfoType.get();
        add(menuWorldInfo.get(), "MenuWorldInfo", bioWorldInfoType.get(), nullptr);
        Check(!menu().Get("context").Bool("worldLinksKnown"), "null/null menu world links remain unknown");
        manager.get()->oBioWorldInfo = menuWorldInfo.get();
        Check(menu().Get("context").String("reason") == "controller-manager-world-info-disagrees", "controller-manager world disagreement");
        controller.get()->WorldInfo = menuWorldInfo.get();
        Check(!menu().Get("context").Bool("available"), "selected handler world disagreement");
        loadHandler.get()->oWorldInfo = mainHandler.get()->oWorldInfo = splashHandler.get()->oWorldInfo = competingLoad.get()->oWorldInfo = menuWorldInfo.get();
        Check(menu().Get("context").Bool("worldLinksKnown"), "typed controller-manager-handler world consistency");
        menuWorldInfo.get()->ObjectFlags = 0x200;
        Check(!menu().Get("context").Bool("available"), "template menu world reference rejected"); menuWorldInfo.get()->ObjectFlags = 0;
        add(derivedMainType.get(), "DerivedMainMenu", meta.get(), sfxPackage.get()); derivedMainType.get()->SuperField = mainType.get();
        mainHandler.get()->Class = derivedMainType.get();
        Check(menuMeta().Bool("layoutMatchesSdkDeclaration"), "inheritance without nearer declaration allowed");
        add(nearerOpen.get(), "MoveToLoadGui", functionType.get(), derivedMainType.get()); derivedMainType.get()->Children = nearerOpen.get();
        Check(menuMeta().String("reason") == "nearer-menu-function-declaration" && !menuMeta().Bool("available"), "nearer override not silently bypassed");
        derivedMainType.get()->Children = nullptr; mainHandler.get()->Class = mainType.get();
        auto row = menu().Get("records").Items().at(0);
        Check(menu().Bool("listComplete") && row.Number("row") == 0 && row.Number("index") == 42 && row.String("career") == "SyntheticCareer", "UI position and native descriptor index kept distinct");
        auto key = row.String("contentKey");
        saveRecords[0].get()->SaveDescriptor.Type = 2;
        Check(menu().Get("records").Items().at(0).String("contentKey") != key, "type participates in descriptor content key");
        saveRecords[0].get()->SaveDescriptor.Type = 0;
        setText(saveRecords[0].get()->SaveDescriptor.SaveName, unicodeName);
        Check(menu().Get("records").Items().at(0).String("saveName") == "Save\xC3\xA9\xF0\x9F\x9A\x80", "strict Unicode descriptor copied unchanged");
        setText(saveRecords[0].get()->SaveDescriptor.SaveName, filename);
        saveHeader.count = 2; setSaves(); saveRecords[1].get()->SaveDescriptor.Index = 42;
        Check(menu().String("reason") == "duplicate-descriptor-content" && menu().Get("records").Items().empty(), "duplicate native descriptor refuses list selection");
        saveRecords[1].get()->SaveDescriptor.Index = 43;
        saveHeader.count = 13; setSaves();
        Check(!menu().Bool("listComplete") && menu().Get("context").Bool("available") && menu().Get("records").Items().empty(), "over-limit list preserves menu context but exposes no partial selection");
        saveHeader.count = 1; setSaves();
        loadHandler.get()->bEnumeratingSaves = 1; Check(!menu().Bool("listComplete"), "enumerating list not selectable"); loadHandler.get()->bEnumeratingSaves = 0;
        loadHandler.get()->bCheckingDataComplete = 0; Check(!menu().Bool("listComplete"), "async checking incomplete"); loadHandler.get()->bCheckingDataComplete = 1;
        loadHandler.get()->bWaitingOnMsgBox = 1; Check(!menu().Bool("listComplete"), "modal wait not selectable"); loadHandler.get()->bWaitingOnMsgBox = 0;
        loadPanel.get()->bInputDisabled = 1; Check(!menu().Bool("listComplete"), "disabled input not selectable"); loadPanel.get()->bInputDisabled = 0;
        manager.get()->m_FocusPanel = mainPanel.get(); Check(!menu().Bool("listComplete"), "background load panel not active"); manager.get()->m_FocusPanel = loadPanel.get();
        menuPanels[3] = loadPanel.get(); panelsHeader.count = 4;
        std::memcpy(&manager.get()->m_lstPanels, &panelsHeader, sizeof(panelsHeader));
        Check(!menu().Get("context").Bool("available") && !menu().Get("context").Get("load").Bool("structurallyInteractive"), "duplicate panels invalidate all partial readiness");
        menuPanels[3] = otherPanel.get(); tag(otherPanel.get(), "Load");
        Check(menu().Get("context").String("reason") == "ambiguous-menu-panels", "two reachable load tags rejected");
        panelsHeader.count = 3; std::memcpy(&manager.get()->m_lstPanels, &panelsHeader, sizeof(panelsHeader));
        std::array<UBioSFHandler*, 2> handlers{loadHandler.get(), competingLoad.get()};
        mevr::ue3::TArray<UBioSFHandler*> handlersHeader{handlers.data(), 1, 2};
        std::memcpy(&loadPanel.get()->ScriptHandlers, &handlersHeader, sizeof(handlersHeader));
        Check(menu().Bool("listComplete"), "same handler in default and list deduplicated");
        handlersHeader.count = 2; std::memcpy(&loadPanel.get()->ScriptHandlers, &handlersHeader, sizeof(handlersHeader));
        Check(!menu().Get("context").Bool("available"), "competing typed handlers rejected");
        handlersHeader.count = 1; std::memcpy(&loadPanel.get()->ScriptHandlers, &handlersHeader, sizeof(handlersHeader));
        loadHandler.get()->oPanel = mainPanel.get(); Check(!menu().Get("context").Bool("available"), "handler reciprocal mismatch"); loadHandler.get()->oPanel = loadPanel.get();
        loadHandler.get()->ObjectFlags = 0x200; Check(!menu().Get("context").Bool("available"), "template handler rejected"); loadHandler.get()->ObjectFlags = 0;
        controller.get()->m_oCachedScaleFormManager = reinterpret_cast<UBioSFManager*>(1);
        Check(menu().Get("context").String("reason") == "controller-manager-cache-disagrees", "foreign controller cache never dereferenced"); controller.get()->m_oCachedScaleFormManager = manager.get();
        manager.get()->m_FocusPanel = otherPanel.get(); Check(!menu().Get("context").Bool("available"), "foreign focus unavailable"); manager.get()->m_FocusPanel = loadPanel.get();
        std::wstring malformed(1, static_cast<wchar_t>(0xd800));
        setText(saveRecords[0].get()->SaveDescriptor.Career, malformed);
        Check(!menu().Bool("listComplete") && menu().Get("records").Items().empty(), "malformed UTF16 cannot become a content key");
        setText(saveRecords[0].get()->SaveDescriptor.Career, career);
        mevr::ue3::TArray<wchar_t> badText{reinterpret_cast<wchar_t*>(1), 2, 2};
        std::memcpy(&saveRecords[0].get()->SaveDescriptor.Career, &badText, sizeof(badText));
        Check(!menu().Bool("listComplete"), "descriptor body fault contained");
        setText(saveRecords[0].get()->SaveDescriptor.Career, career);
        saveHeader.data = reinterpret_cast<FSaveGUIRecord*>(1); setSaves();
        Check(menu().String("reason") == "guarded-menu-read-fault", "bad descriptor record pointer invalidates sample");
        Check(ObserveSaveMenu(0).String("reason") == "not-on-verified-game-thread", "wrong thread does not read corrupt descriptor");
        saveHeader.data = saveRecords[0].get(); saveHeader.count = 12; setSaves();
        std::wstring large(255, L'\x01');
        for (auto& raw : saveRecords) { setText(raw.get()->SaveDescriptor.Career, large); setText(raw.get()->SaveDescriptor.SaveName, large); }
        auto bounded = menu();
        Check(bounded.String("reason") == "compact-payload-budget-exceeded" && bounded.Get("context").Bool("available") && bounded.Get("records").Items().empty(), "worst escaping clears rows but preserves menu context");
        Check(mevr::json::Write(bounded, 0).size() <= 14 * 1024, "hard compact output budget reserves host envelope");
        for (auto& raw : saveRecords) { setText(raw.get()->SaveDescriptor.Career, career); setText(raw.get()->SaveDescriptor.SaveName, filename); }
        auto menuBytes = mevr::json::Write(menu(), 0).size();
        Check(menu().Bool("listComplete") && menuBytes <= 14 * 1024, "all12 rows and metadata fit compact budget");
        std::cout << "Representative save-menu payload bytes: " << menuBytes << "; budget-fallback bytes: " << mevr::json::Write(bounded, 0).size() << '\n';
        // Hardcoded menu actions never execute SDK stubs in this synthetic suite.
        using mevr::engine::le2_diagnostic::AdvanceSplash;
        using mevr::engine::le2_diagnostic::OpenLoadMenu;
        using mevr::engine::le2_diagnostic::testing::ResetMenuActions;
        std::memcpy(mainHandler.get(), &controllerVtablePtr, sizeof(controllerVtablePtr));
        std::memcpy(splashHandler.get(), &controllerVtablePtr, sizeof(controllerVtablePtr));
        probeTarget = controllerVtable[72]; probeExactBase = true; probeBadHook = probeBadBounds = probeWrongBase = probeBadExeCheck = false;
        mevr::engine::le2_diagnostic::ObserverServices menuServices{MenuInspect, nullptr, ProbeHook};
        expectedMenuHandler = mainHandler.get(); expectedMenuFunction = openLoad.get(); expectedNoParameters = false;
        actionPanel = mainPanel.get(); actionManager = manager.get(); actionFunction = openLoad.get();
        manager.get()->m_FocusPanel = mainPanel.get(); menuDispatchMode = 1;
        ResetMenuActions(FakeMenuDispatch);
        auto menuActionsBefore = menuDispatchCalls;
        Check(OpenLoadMenu(0, menuServices).status == "rejected" && menuDispatchCalls == menuActionsBefore, "wrong-thread menu action never enters");
        mainHandler.get()->bWaitingForDLC = 1;
        Check(OpenLoadMenu(thread, menuServices).status == "rejected", "waiting DLC blocks normal menu action"); mainHandler.get()->bWaitingForDLC = 0;
        mainHandler.get()->bHasSaves = 0;
        Check(OpenLoadMenu(thread, menuServices).status == "rejected", "disabled load availability blocks action"); mainHandler.get()->bHasSaves = 1;
        manager.get()->m_FocusPanel = loadPanel.get();
        Check(OpenLoadMenu(thread, menuServices).status == "rejected", "background main menu cannot open another panel"); manager.get()->m_FocusPanel = mainPanel.get();
        probeExactBase = false;
        Check(OpenLoadMenu(thread, menuServices).status == "rejected", "controller-like forwarding route does not authorize menu receiver"); probeExactBase = true;
        probeBadHook = true; Check(OpenLoadMenu(thread, menuServices).status == "rejected", "changed owned chain rejects menu"); probeBadHook = false;
        probeBadBounds = true; Check(OpenLoadMenu(thread, menuServices).status == "rejected", "unknown function bounds rejects menu"); probeBadBounds = false;
        mutateMenuOnInspection = true;
        Check(OpenLoadMenu(thread, menuServices).payload.String("reason") == "menu-identity-metadata-or-slot-changed-before-entry", "metadata mutation during provider caught by final guarded sample");
        mutateMenuOnInspection = false; openLoad.get()->FunctionFlags = 0x20002;
        Raw<UByteProperty> replacementLoadMode;
        add(replacementLoadMode.get(), "LoadMode", byteType.get(), openLoad.get()); property(replacementLoadMode.get(), 0, 1, 0x80);
        replacementMenuProperty = replacementLoadMode.get(); replaceMenuProperty = true;
        Check(OpenLoadMenu(thread, menuServices).payload.String("reason") == "menu-identity-metadata-or-slot-changed-before-entry", "same-layout replacement parameter identity caught");
        replaceMenuProperty = false; openLoad.get()->Children = loadModeProperty.get();
        Check(menuDispatchCalls == menuActionsBefore, "all menu pre-entry rejections preserve entry budget");
        menuDispatchMode = 0;
        auto opened = OpenLoadMenu(thread, menuServices);
        Check(opened.status == "dispatched" && opened.payload.Bool("entered") && opened.payload.Bool("parameterFrameIntact") &&
              !opened.payload.Bool("normalUiTransitionProven"), "normal return with expected focus/visibility change is dispatched only");
        Check(!opened.payload.Get("postObservation").Get("context").Get("mainMenu").Bool("structurallyInteractive"), "post transition observed separately");
        Check(mevr::json::Write(opened.payload, 0).size() <= 14 * 1024, "action with post context within wire budget");
        Check(OpenLoadMenu(thread, menuServices).status == "rejected" && menuDispatchCalls == menuActionsBefore + 1, "open entry spent even while transition pending");
        mainPanel.get()->IsVisible = 1;
        // Splash has an independent budget and exactly null parameters.
        expectedMenuHandler = splashHandler.get(); expectedMenuFunction = advanceSplash.get(); expectedNoParameters = true;
        actionPanel = splashPanel.get(); actionFunction = advanceSplash.get(); manager.get()->m_FocusPanel = splashPanel.get();
        splashHandler.get()->bPressedStart = 1;
        Check(AdvanceSplash(thread, menuServices).status == "rejected", "already pressed splash cannot advance again"); splashHandler.get()->bPressedStart = 0;
        splashHandler.get()->fLegalTimeout = 5.0f; // Source has no timer comparison in the PC input advance route.
        auto advanced = AdvanceSplash(thread, menuServices);
        Check(advanced.status == "dispatched" && advanced.payload.Number("parameterBytes") == 0, "independent splash entry with nullptr params");
        Check(AdvanceSplash(thread, menuServices).status == "rejected", "splash entry never repeated"); splashPanel.get()->IsVisible = 1;
        expectedMenuHandler = mainHandler.get(); expectedMenuFunction = openLoad.get(); expectedNoParameters = false;
        actionPanel = mainPanel.get(); actionFunction = openLoad.get(); manager.get()->m_FocusPanel = mainPanel.get();
        for (int mode : {2, 3, 4, 5}) {
            ResetMenuActions(FakeMenuDispatch); menuDispatchMode = mode;
            auto uncertain = OpenLoadMenu(thread, menuServices);
            Check(uncertain.status == "error" && uncertain.payload.Bool("entrySpent") &&
                  uncertain.payload.Get("postObservation").String("reason") == "unavailable-after-uncertain-entry", "exception/frame corruption leaves uncertain spent entry");
            Check(engineReadCalls == menuReadsAtDispatch, "no engine traversal after exception or damaged byte frame");
            auto calls = menuDispatchCalls;
            Check(OpenLoadMenu(thread, menuServices).status == "rejected" && menuDispatchCalls == calls, "uncertain menu entry cannot retry");
        }
        using mevr::engine::le2_diagnostic::FrozenDescriptor;
        using mevr::engine::le2_diagnostic::ObserveFixtureCandidate;
        using mevr::engine::le2_diagnostic::LoadFixture;
        using mevr::engine::le2_diagnostic::testing::ResetFixtureLoad;
        Raw<UClass> pcLoadType, otherManagerType;
        add(pcLoadType.get(), "SFXSFHandler_PCLoad", meta.get(), sfxPackage.get()); pcLoadType.get()->SuperField = loadType.get();
        add(otherManagerType.get(), "MassEffectGuiManager", meta.get(), sfxPackage.get());
        loadHandler.get()->Class = pcLoadType.get();
        std::memcpy(loadHandler.get(), &controllerVtablePtr, sizeof(controllerVtablePtr));
        manager.get()->m_FocusPanel = loadPanel.get(); loadPanel.get()->IsVisible = 1;
        auto records512 = std::make_unique<Raw<FSaveGUIRecord>[]>(512);
        std::wstring otherSave = L"OtherSave";
        for (unsigned i = 0; i < 512; ++i) {
            setText(records512[i].get()->SaveDescriptor.Career, career);
            setText(records512[i].get()->SaveDescriptor.SaveName, otherSave);
            records512[i].get()->SaveDescriptor.Index = 1000 + static_cast<int>(i);
        }
        expectedFixtureRow = 300;
        auto configureTarget = [&] {
            setText(records512[expectedFixtureRow].get()->SaveDescriptor.SaveName, filename);
            records512[expectedFixtureRow].get()->SaveDescriptor.Index = 99;
        };
        configureTarget();
        saveHeader = {records512[0].get(), 512, 512}; setSaves();
        fixtureHandler = loadHandler.get(); fixtureFunction = normalLoad.get(); fixtureManager = manager.get(); fixturePanel = loadPanel.get();
        fixtureRecords = records512[0].get(); fixtureManagerProperty = guiManagerProperty.get(); alternateManagerClass = otherManagerType.get();
        fixtureMutableText = &filename; fixtureMutableOther = &otherSave; fixtureVtable = controllerVtable.data();
        FrozenDescriptor frozen{"SyntheticCareer", "SyntheticSave", 99, 0};
        mevr::engine::le2_diagnostic::ObserverServices fixtureServices{FixtureInspect, nullptr, ProbeHook};
        auto candidate = [&] { return ObserveFixtureCandidate(thread, fixtureServices, frozen); };
        Check(!menu().Bool("listComplete") && candidate().Bool("scanComplete") && candidate().Bool("candidateAvailable"), "complete512-row filtered scan bypasses12-row display cap");
        Check(candidate().Get("candidate").Number("row") == 300 && candidate().Get("candidate").Number("index") == 99 &&
              candidate().Bool("ownedBaseRouteObserved") && !candidate().Bool("dispatchCapability"), "unique current UI row kept separate from descriptor index");
        Check(mevr::json::Write(candidate(), 0).size() <= 14 * 1024, "filtered512 scan has bounded result");
        ResetFixtureLoad(FakeFixtureDispatch); fixtureDispatchMode = 1;
        auto originalReads = engineReadCalls;
        FrozenDescriptor invalid = frozen; invalid.career.clear();
        Check(LoadFixture(thread, fixtureServices, invalid).status == "rejected" && engineReadCalls == originalReads, "blank frozen identity refuses before engine reads");
        invalid = frozen; invalid.saveName = "../SyntheticSave";
        Check(!ObserveFixtureCandidate(thread, fixtureServices, invalid).Bool("scanComplete"), "paths not accepted as descriptor names");
        invalid = frozen; invalid.type = 2;
        Check(LoadFixture(thread, fixtureServices, invalid).status == "rejected", "auto/quick/import types not enabled");
        invalid = frozen; invalid.index = -1;
        Check(LoadFixture(thread, fixtureServices, invalid).status == "rejected", "manual negative index refused");
        Check(LoadFixture(0, fixtureServices, frozen).status == "rejected", "wrong-thread load refused");
        saveHeader.count = saveHeader.max = 513; setSaves();
        Check(!candidate().Bool("scanComplete") && LoadFixture(thread, fixtureServices, frozen).status == "rejected", "513 rows cannot silently become complete");
        saveHeader.count = saveHeader.max = 512; setSaves();
        invalid = frozen; invalid.index = 98;
        Check(ObserveFixtureCandidate(thread, fixtureServices, invalid).Number("matches") == 0 &&
              LoadFixture(thread, fixtureServices, invalid).status == "rejected", "all four descriptor fields required");
        std::memcpy(&records512[1].get()->SaveDescriptor, &records512[300].get()->SaveDescriptor, sizeof(FSFXSaveDescriptor));
        Check(candidate().Bool("scanComplete") && candidate().Number("matches") == 2 && !candidate().Bool("candidateAvailable") &&
              LoadFixture(thread, fixtureServices, frozen).status == "rejected", "scan continues beyond first full match to reject duplicates");
        records512[1].get()->SaveDescriptor.Index = 1001; setText(records512[1].get()->SaveDescriptor.SaveName, otherSave);
        setText(records512[511].get()->SaveDescriptor.SaveName, malformed);
        Check(!candidate().Bool("scanComplete") && LoadFixture(thread, fixtureServices, frozen).status == "rejected", "malformed nonmatching tail refuses complete uniqueness proof");
        setText(records512[511].get()->SaveDescriptor.SaveName, otherSave);
        loadHandler.get()->LoadMode = 1; Check(LoadFixture(thread, fixtureServices, frozen).status == "rejected", "NG+ route refused"); loadHandler.get()->LoadMode = 0;
        loadHandler.get()->GuiMode = 0; Check(LoadFixture(thread, fixtureServices, frozen).status == "rejected", "in-game load prompt route refused"); loadHandler.get()->GuiMode = 1;
        loadHandler.get()->bFromMainMenu = 0; Check(LoadFixture(thread, fixtureServices, frozen).status == "rejected", "normal main-menu provenance required"); loadHandler.get()->bFromMainMenu = 1;
        loadHandler.get()->bEnumeratingSaves = 1; Check(LoadFixture(thread, fixtureServices, frozen).status == "rejected", "enumerating load list refused"); loadHandler.get()->bEnumeratingSaves = 0;
        probeExactBase = false; Check(LoadFixture(thread, fixtureServices, frozen).status == "rejected", "forwarded receiver route not admitted for load"); probeExactBase = true;
        for (int mutation : {1, 2, 3, 4, 5}) {
            fixtureMutation = mutation;
            auto refused = LoadFixture(thread, fixtureServices, frozen);
            Check(refused.status == "rejected" && refused.payload.String("reason") == "fixture-context-or-list-changed-before-entry", "final guarded whole-list/metadata/slot comparison catches provider mutation");
            fixtureMutation = 0;
            if (mutation == 1) filename[0] ^= 1;
            if (mutation == 2) { records512[1].get()->SaveDescriptor.Index = 1001; setText(records512[1].get()->SaveDescriptor.SaveName, otherSave); }
            if (mutation == 3) guiManagerProperty.get()->PropertyClass = managerType.get();
            if (mutation == 4) --controllerVtable[72];
            if (mutation == 5) otherSave[0] ^= 1;
        }
        Check(fixtureDispatchCalls == 0, "all pre-entry refusals preserve load budget");
        fixtureDispatchMode = 0;
        auto loaded = LoadFixture(thread, fixtureServices, frozen);
        Check(loaded.status == "dispatched" && loaded.payload.Bool("parameterFrameIntact") && loaded.payload.Bool("entrySpent") &&
              !loaded.payload.Bool("fixtureLoaded"), "normal packed LoadGame return is dispatched only, not loaded acceptance");
        Check(mevr::json::Write(loaded.payload, 0).size() <= 14 * 1024, "load result plus post observation within budget");
        Check(LoadFixture(thread, fixtureServices, frozen).status == "rejected" && fixtureDispatchCalls == 1, "no repeated load after normal return");
        manager.get()->m_FocusPanel = loadPanel.get(); loadPanel.get()->IsVisible = 1;
        for (int mode : {2, 3, 4, 5, 6}) {
            ResetFixtureLoad(FakeFixtureDispatch); fixtureDispatchMode = mode;
            auto uncertain = LoadFixture(thread, fixtureServices, frozen);
            Check(uncertain.status == "error" && uncertain.payload.Bool("entrySpent") &&
                  uncertain.payload.Get("postObservation").String("reason") == "unavailable-after-uncertain-entry", "throw/canaries/input changes leave spent uncertain load");
            Check(engineReadCalls == fixtureReadsAtDispatch, "no engine reread after uncertain load");
            auto calls = fixtureDispatchCalls;
            Check(LoadFixture(thread, fixtureServices, frozen).status == "rejected" && fixtureDispatchCalls == calls, "uncertain load cannot retry");
        }
        // Direct live inventory observation: every resource is synthetic POD.
        using mevr::engine::le2_diagnostic::ObserveResources;
        using mevr::engine::le2_diagnostic::testing::SetResourceSampleHook;
        Raw<UClass> playerPawnResourceType, soldierResourceType, resourceInventoryType, resourceSquadType;
        Raw<ASFXPawn_PlayerSoldier> resourcePawn;
        Raw<ASFXInventoryManager> inventory;
        Raw<ABioPlayerSquad> resourceSquad;
        add(playerPawnResourceType.get(), "SFXPawn_Player", meta.get(), sfxPackage.get()); playerPawnResourceType.get()->SuperField = pawnType.get();
        add(soldierResourceType.get(), "SFXPawn_PlayerSoldier", meta.get(), sfxPackage.get()); soldierResourceType.get()->SuperField = playerPawnResourceType.get();
        add(resourceInventoryType.get(), "SFXInventoryManager", meta.get(), sfxPackage.get());
        add(resourceSquadType.get(), "BioPlayerSquad", meta.get(), sfxPackage.get());
        add(resourcePawn.get(), "ResourcePawn", soldierResourceType.get(), nullptr);
        add(inventory.get(), "ResourceInventory", resourceInventoryType.get(), nullptr);
        add(resourceSquad.get(), "ResourceSquad", resourceSquadType.get(), nullptr);
        controller.get()->Pawn = resourcePawn.get(); controller.get()->Player = local.get();
        resourcePawn.get()->Controller = controller.get(); resourcePawn.get()->WorldInfo = menuWorldInfo.get();
        resourcePawn.get()->InvManager = inventory.get(); resourcePawn.get()->Location = {11, 22, 33};
        inventory.get()->Owner = inventory.get()->Instigator = resourcePawn.get(); inventory.get()->WorldInfo = menuWorldInfo.get();
        menuWorldInfo.get()->LocalPlayerController = reinterpret_cast<ABioPlayerController*>(controller.get());
        menuWorldInfo.get()->m_playerSquad = resourceSquad.get();
        resourceSquad.get()->m_playerPawn = resourcePawn.get(); resourceSquad.get()->WorldInfo = menuWorldInfo.get();
        inventory.get()->Credits = 123456; inventory.get()->Medigel = 4; inventory.get()->Eezo = 50;
        inventory.get()->Iridium = 60; inventory.get()->Palladium = 70; inventory.get()->Platinum = 80;
        inventory.get()->Probes = 0; inventory.get()->CurrentFuel = 1500.25f;
        auto resources = [&] { return ObserveResources(thread); };
        auto fields = [&] { return resources().Get("resources"); };
        auto initialResources = resources();
        Check(initialResources.Bool("allResourcesAvailable") && initialResources.Get("context").Bool("available") &&
              initialResources.Get("context").Bool("squadCrossCheckAvailable"), "validated current-player inventory and independent squad route");
        Check(initialResources.Get("context").Get("map").Bool("available") && initialResources.Get("context").Get("position").Number("x") == 11,
              "resource context map and position come from same fresh root sample");
        Check(fields().Get("credits").Number("value") == 123456 && fields().Get("medigel").Number("value") == 4 &&
              fields().Get("eezo").Number("value") == 50 && fields().Get("iridium").Number("value") == 60 &&
              fields().Get("palladium").Number("value") == 70 && fields().Get("platinum").Number("value") == 80 &&
              fields().Get("probes").Number("value", -1) == 0 && fields().Get("fuel").Number("value") == 1500.25,
              "all8 source fields mapped without rounding fuel or dropping legitimate zero");
        Check(!initialResources.Bool("saveCacheUsed") && !initialResources.Bool("persistenceProven") &&
              !initialResources.Bool("dispatchCapability"), "no save cache or call/persistence claim");
        Check(fields().Get("fuel").String("sourceField") == "SFXGame.SFXInventoryManager.CurrentFuel", "normalized fuel names exact SDK source");
        inventory.get()->Credits = std::numeric_limits<int>::max(); inventory.get()->MaxCredits = 0;
        Check(fields().Get("credits").Bool("available") && fields().Get("credits").Number("value") == std::numeric_limits<int>::max(),
              "no invented MaxCredits clamp or overflow on valid int32");
        inventory.get()->Credits = -1;
        Check(!fields().Get("credits").Bool("available") && !fields().Get("credits").Has("value") && fields().Get("medigel").Bool("available"),
              "negative resource unavailable independently without fabricated zero"); inventory.get()->Credits = 123456;
        for (float invalidFuel : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -1.0f}) {
            inventory.get()->CurrentFuel = invalidFuel;
            Check(!fields().Get("fuel").Bool("available") && !fields().Get("fuel").Has("value") && fields().Get("probes").Bool("available"),
                  "nonfinite/negative fuel cannot hide other valid fields or serialize invalid number");
        }
        inventory.get()->CurrentFuel = 1500.25f;
        inventory.get()->ObjectFlags = 0x200;
        Check(!resources().Get("context").Bool("available") && !fields().Get("credits").Has("value"), "template inventory refused"); inventory.get()->ObjectFlags = 0;
        inventory.get()->ObjectFlags = 0x2000000000000000ull;
        Check(!resources().Get("context").Bool("available"), "pending kill inventory refused"); inventory.get()->ObjectFlags = 0;
        inventory.get()->Owner = pawn.get(); Check(resources().String("reason") == "inventory-owner-instigator-or-world-disagrees", "foreign inventory owner refused"); inventory.get()->Owner = resourcePawn.get();
        inventory.get()->Instigator = pawn.get(); Check(!resources().Get("context").Bool("available"), "foreign inventory instigator refused"); inventory.get()->Instigator = resourcePawn.get();
        inventory.get()->WorldInfo = nullptr; Check(!resources().Get("context").Bool("available"), "foreign inventory world refused"); inventory.get()->WorldInfo = menuWorldInfo.get();
        resourcePawn.get()->Controller = nullptr; Check(!resources().Get("context").Bool("available"), "pawn-controller reciprocal required"); resourcePawn.get()->Controller = controller.get();
        controller.get()->Player = nullptr; Check(!resources().Get("context").Bool("available"), "controller-localplayer reciprocal required"); controller.get()->Player = local.get();
        resourceSquad.get()->m_playerPawn = reinterpret_cast<ABioPawn*>(pawn.get());
        Check(resources().String("reason") == "squad-player-or-world-disagrees", "squad and local player resource routes cannot disagree"); resourceSquad.get()->m_playerPawn = resourcePawn.get();
        menuWorldInfo.get()->m_playerSquad = nullptr;
        Check(resources().Bool("allResourcesAvailable") && !resources().Get("context").Bool("squadCrossCheckAvailable"), "missing auxiliary squad never fabricates a cross-check or uses alternate resource cache");
        menuWorldInfo.get()->m_playerSquad = resourceSquad.get();
        menuWorldInfo.get()->LocalPlayerController = reinterpret_cast<ABioPlayerController*>(1);
        Check(resources().String("reason") == "world-controller-cache-disagrees", "foreign world controller cache refused without dereference");
        menuWorldInfo.get()->LocalPlayerController = reinterpret_cast<ABioPlayerController*>(controller.get());
        resourcePawn.get()->Class = pawnType.get(); Check(resources().String("reason") == "not-current-SFXPawn_Player", "vehicle/generic pawn not treated as player resource authority"); resourcePawn.get()->Class = soldierResourceType.get();
        resourceInventory = inventory.get(); resourcePawnForMutation = resourcePawn.get(); SetResourceSampleHook(MutateResourceSample);
        resourceMutation = 1;
        auto changingProbes = resources();
        Check(changingProbes.Get("context").Bool("available") && !changingProbes.Get("resources").Get("probes").Bool("available") &&
              changingProbes.Get("resources").Get("credits").Bool("available"), "single changing value unavailable independently");
        resourceMutation = 2;
        auto changingOwner = resources();
        Check(!changingOwner.Get("context").Bool("available") && !changingOwner.Get("resources").Get("credits").Has("value"), "changed root ownership invalidates all values"); inventory.get()->Owner = resourcePawn.get();
        resourceMutation = 3;
        auto changingPosition = resources();
        Check(changingPosition.Bool("allResourcesAvailable") && !changingPosition.Get("context").Get("position").Bool("available"), "moving position unavailable without fabricating same-snapshot coordinates");
        resourceMutation = 4;
        auto changingFuel = resources();
        Check(!changingFuel.Get("resources").Get("fuel").Bool("available") && changingFuel.Get("resources").Get("credits").Bool("available"), "fuel mutation isolated from other values");
        SetResourceSampleHook(nullptr); inventory.get()->CurrentFuel = 1500.25f;
        // Place the seven trailing fields on an unreadable page while all root
        // metadata and Credits remain readable: one field fault cannot zero all.
        SYSTEM_INFO systemInfo{}; GetSystemInfo(&systemInfo);
        auto* pages = static_cast<unsigned char*>(VirtualAlloc(nullptr, systemInfo.dwPageSize * 2, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        Check(pages != nullptr, "allocate synthetic guard-page inventory");
        auto* partialInventory = reinterpret_cast<ASFXInventoryManager*>(pages + systemInfo.dwPageSize - 0x308);
        std::memcpy(partialInventory, inventory.get(), sizeof(ASFXInventoryManager));
        auto inventoryIndex = inventory.get()->ObjectInternalInteger;
        slots[inventoryIndex] = partialInventory; resourcePawn.get()->InvManager = partialInventory;
        DWORD previousProtection{};
        Check(VirtualProtect(pages + systemInfo.dwPageSize, systemInfo.dwPageSize, PAGE_NOACCESS, &previousProtection) != 0, "protect trailing resource page");
        auto partiallyReadable = resources();
        Check(partiallyReadable.Get("context").Bool("available") && partiallyReadable.Get("resources").Get("credits").Bool("available") &&
              !partiallyReadable.Get("resources").Get("medigel").Bool("available") &&
              partiallyReadable.Get("resources").Get("fuel").String("reason") == "guarded-resource-field-read-fault", "per-field guarded read fault preserves readable Credits");
        resourcePawn.get()->InvManager = inventory.get(); slots[inventoryIndex] = inventory.get();
        Check(VirtualFree(pages, 0, MEM_RELEASE) != 0, "release synthetic pages");
        resourcePawn.get()->InvManager = reinterpret_cast<AInventoryManager*>(1);
        Check(!resources().Get("context").Bool("available") && !fields().Get("credits").Has("value"), "bad inventory pointer invalidates context safely");
        auto priorResourceReads = engineReadCalls;
        Check(ObserveResources(0).String("reason") == "not-on-verified-game-thread" && engineReadCalls == priorResourceReads,
              "wrong thread performs no resource/context reads");
        resourcePawn.get()->InvManager = inventory.get();
        auto finalResources = resources();
        Check(finalResources.Bool("allResourcesAvailable") && mevr::json::Write(finalResources, 0).size() <= 14 * 1024,
              "fresh valid resource observation recovers and fits bounded control payload");
        std::cout << "Representative resource observation bytes: " << mevr::json::Write(finalResources, 0).size() << '\n';
        // Same-flow normal pause/save-menu opening. No native save is submitted.
        using mevr::engine::le2_diagnostic::ObserveSaveFlow;
        using mevr::engine::le2_diagnostic::OpenPauseMenu;
        using mevr::engine::le2_diagnostic::OpenSaveMenu;
        using mevr::engine::le2_diagnostic::testing::ResetSaveFlow;
        using mevr::engine::le2_diagnostic::testing::SetSaveFlowSampleHook;
        Raw<UClass> bioPcType, flowManagerType, modeBaseType, defaultModeType, guiModeType, browserBaseType, pcBrowserType, saveBaseType, pcSaveType, sfManagerType;
        Raw<ABioPlayerController> flowPc;
        Raw<USFXGameModeManager> modes;
        Raw<USFXGameModeDefault> defaultMode;
        Raw<USFXGameModeBase> guiMode;
        Raw<UBioSFHandler_PCPauseMenu> browserHandler;
        Raw<USFXSFHandler_PCSave> saveHandler;
        Raw<UBioSFPanel> browserPanel, normalSavePanel;
        Raw<UFunction> showPause, showSave, closerShowPause;
        Raw<UIntProperty> pageProperty; Raw<UObjectProperty> sfManagerProperty;
        add(bioPcType.get(), "BioPlayerController", meta.get(), sfxPackage.get()); bioPcType.get()->SuperField=controllerType.get();
        add(flowManagerType.get(), "SFXGameModeManager", meta.get(), sfxPackage.get());
        add(modeBaseType.get(), "SFXGameModeBase", meta.get(), sfxPackage.get());
        add(defaultModeType.get(), "SFXGameModeDefault", meta.get(), sfxPackage.get()); defaultModeType.get()->SuperField=modeBaseType.get();
        add(guiModeType.get(), "SFXGameModeGUI", meta.get(), sfxPackage.get()); guiModeType.get()->SuperField=modeBaseType.get();
        add(browserBaseType.get(), "BioSFHandler_BrowserWheel", meta.get(), sfxPackage.get()); browserBaseType.get()->SuperField=handlerType.get();
        add(pcBrowserType.get(), "BioSFHandler_PCPauseMenu", meta.get(), sfxPackage.get()); pcBrowserType.get()->SuperField=browserBaseType.get();
        add(saveBaseType.get(), "SFXSFHandler_Save", meta.get(), sfxPackage.get()); saveBaseType.get()->SuperField=handlerType.get();
        add(pcSaveType.get(), "SFXSFHandler_PCSave", meta.get(), sfxPackage.get()); pcSaveType.get()->SuperField=saveBaseType.get();
        add(sfManagerType.get(), "BioSFManager", meta.get(), enginePackage.get());
        add(flowPc.get(), "FlowController", bioPcType.get(), nullptr);
        add(modes.get(), "GameModeManager", flowManagerType.get(), flowPc.get());
        add(defaultMode.get(), "DefaultMode", defaultModeType.get(), flowPc.get());
        add(guiMode.get(), "GuiMode", guiModeType.get(), flowPc.get());
        add(browserHandler.get(), "PauseHandler", pcBrowserType.get(), nullptr);
        add(saveHandler.get(), "SaveHandler", pcSaveType.get(), nullptr);
        add(browserPanel.get(), "BrowserPanel", panelType.get(), nullptr);
        add(normalSavePanel.get(), "NormalSavePanel", panelType.get(), nullptr);
        add(showPause.get(), "ShowMenu", functionType.get(), modeBaseType.get()); modeBaseType.get()->Children=showPause.get();
        showPause.get()->FunctionFlags=0x20202; showPause.get()->ReturnValueOffset=-1;
        add(closerShowPause.get(), "ShowMenu", functionType.get(), defaultModeType.get());
        add(showSave.get(), "ShowSelectedPanel", functionType.get(), browserBaseType.get()); browserBaseType.get()->Children=showSave.get();
        showSave.get()->FunctionFlags=0x824803; showSave.get()->ParmsSize=12; showSave.get()->NumParms=2; showSave.get()->ReturnValueOffset=-1;
        add(pageProperty.get(), "nPanel", intType.get(), showSave.get()); property(pageProperty.get(),0,4,0x80);
        add(sfManagerProperty.get(), "oManager", objectPropertyType.get(), showSave.get()); property(sfManagerProperty.get(),4,8,0x90);
        sfManagerProperty.get()->PropertyClass=sfManagerType.get(); showSave.get()->Children=pageProperty.get(); pageProperty.get()->Next=sfManagerProperty.get();
        tag(browserPanel.get(), "MenuBrowser"); tag(normalSavePanel.get(), "ConfiguredSave");
        for (auto* panel : {browserPanel.get(),normalSavePanel.get()}) { panel->oParentManager=manager.get(); panel->IsVisible=panel->bInitialized=1; }
        browserPanel.get()->m_DefaultHandler=browserHandler.get(); browserHandler.get()->oPanel=browserPanel.get(); browserHandler.get()->oWorldInfo=menuWorldInfo.get();
        normalSavePanel.get()->m_DefaultHandler=saveHandler.get(); saveHandler.get()->oPanel=normalSavePanel.get(); saveHandler.get()->oWorldInfo=menuWorldInfo.get();
        saveHandler.get()->bCheckingDataComplete=1; saveHandler.get()->PendingOverwriteIndex=-1; saveHandler.get()->MaxSaves=1000;
        flowPc.get()->Pawn=resourcePawn.get(); flowPc.get()->Player=local.get(); flowPc.get()->WorldInfo=menuWorldInfo.get(); flowPc.get()->GameModeManager2=modes.get();
        flowPc.get()->m_oCachedScaleFormManager=manager.get(); local.get()->Actor=flowPc.get();
        resourcePawn.get()->Controller=flowPc.get(); manager.get()->Player=flowPc.get(); menuWorldInfo.get()->LocalPlayerController=flowPc.get();
        std::array<USFXGameModeBase*,8> modeObjects{}; modeObjects[0]=defaultMode.get(); modeObjects[7]=guiMode.get();
        mevr::ue3::TArray<USFXGameModeBase*> modesHeader{modeObjects.data(),8,8}; std::memcpy(&modes.get()->GameModes,&modesHeader,sizeof(modesHeader));
        defaultMode.get()->bIsActive=defaultMode.get()->bAllowPauseMenu=1; guiMode.get()->bIsActive=1;
        std::array<FBWPageStruct,2> browserPages{}; browserPages[0].Type=0; browserPages[1].Type=1;
        browserPages[0].Tag=otherPanel.get()->nmTag; browserPages[1].Tag=normalSavePanel.get()->nmTag;
        mevr::ue3::TArray<FBWPageStruct> pageHeader{browserPages.data(),2,2}; std::memcpy(&browserHandler.get()->lstPages,&pageHeader,sizeof(pageHeader));
        std::array<FSubPageState,1> pageOverrides{}; pageOverrides[0].Page=1; pageOverrides[0].State=0;
        mevr::ue3::TArray<FSubPageState> overrideHeader{pageOverrides.data(),1,1}; std::memcpy(&menuWorldInfo.get()->SubPageStateOverrides,&overrideHeader,sizeof(overrideHeader));
        std::array<std::uintptr_t,73> flowVtable{}; flowVtable[72]=0x123400;
        auto* vt=flowVtable.data(); std::memcpy(defaultMode.get(),&vt,sizeof(vt)); std::memcpy(browserHandler.get(),&vt,sizeof(vt));
        flowManager=manager.get(); flowModes=modes.get(); flowWorld=menuWorldInfo.get(); flowDefault=defaultMode.get(); flowBrowser=browserHandler.get();
        flowBrowserPanel=browserPanel.get(); flowSavePanel=normalSavePanel.get(); flowPauseFunction=showPause.get(); flowSaveFunction=showSave.get();
        flowPages=browserPages.data(); flowOverrides=pageOverrides.data(); flowManagerProperty=sfManagerProperty.get(); flowWrongClass=managerType.get();
        mevr::engine::le2_diagnostic::ObserverServices flowServices{FlowInspect,nullptr,ProbeHook};
        auto beginFlow=[&] {
            ResetSaveFlow(FakeSaveFlowDispatch); flowBehavior=flowMutation=0; flowDispatchCalls=0;
            probeTarget=0x123400; probeExactBase=true; probeBadBounds=probeBadHook=probeWrongBase=probeBadExeCheck=false;
            modes.get()->CurrentMode=0; menuWorldInfo.get()->bPlayersOnly=0; menuWorldInfo.get()->m_bAllowBrowserWheel=1;
            menuWorldInfo.get()->m_lstBrowserAlerts[1]=0; pageOverrides[0].State=0; browserHandler.get()->bWaitForNextFrame=0;
            manager.get()->m_FocusPanel=nullptr; manager.get()->m_oBWHandler=nullptr;
            mevr::ue3::TArray<UBioSFPanel*> h{flowPanels,0,2}; std::memcpy(&manager.get()->m_lstPanels,&h,sizeof(h));
        };
        beginFlow();
        auto beforeFlowReads=engineReadCalls;
        Check(!ObserveSaveFlow(0).Bool("available") && OpenPauseMenu(0,flowServices).status=="rejected" && engineReadCalls==beforeFlowReads,"save flow wrong thread reads nothing");
        auto fs=ObserveSaveFlow(thread);
        Check(fs.Bool("available") && fs.Bool("pauseStructurallySupported") && !fs.Bool("saveNewCapability"),"source-indexed current Default metadata observed");
        Check(fs.String("uiInitializationProof")=="external-review-required-not-inferred-from-alert-zero","readonly sample never asserts UI initialization from alert0");
        modes.get()->CurrentMode=8; Check(!ObserveSaveFlow(thread).Bool("available"),"mode array upper bound"); modes.get()->CurrentMode=0;
        defaultMode.get()->Outer=nullptr; Check(OpenPauseMenu(thread,flowServices).status=="rejected","foreign mode owner rejected"); defaultMode.get()->Outer=flowPc.get();
        defaultModeType.get()->Children=closerShowPause.get(); Check(OpenPauseMenu(thread,flowServices).status=="rejected","closer ShowMenu override cannot be bypassed"); defaultModeType.get()->Children=nullptr;
        defaultMode.get()->bAllowPauseMenu=0; Check(OpenPauseMenu(thread,flowServices).status=="rejected","normal pause permission required"); defaultMode.get()->bAllowPauseMenu=1;
        defaultMode.get()->bIsActive=0; Check(OpenPauseMenu(thread,flowServices).status=="rejected","inactive Default rejected"); defaultMode.get()->bIsActive=1;
        flowVtable[72]=0x123401; Check(OpenPauseMenu(thread,flowServices).status=="rejected","non-owned mode route refused"); flowVtable[72]=0x123400;
        auto flowOpened=OpenPauseMenu(thread,flowServices);
        Check(flowOpened.status=="dispatched" && flowDispatchCalls==1 && flowOpened.payload.Get("postObservation").Get("browser").Bool("structuralSavePageSupported"),"normal pause enters once and observes asynchronous browser without claiming UI proof");
        Check(OpenPauseMenu(thread,flowServices).status=="rejected","pause budget never resets");
        Check(ObserveSaveFlow(thread).Get("context").String("focusedPanelTag")=="MenuBrowser","actual focused panel tag diagnoses conservative admission");
        browserHandler.get()->bWaitForNextFrame=1; Check(OpenSaveMenu(thread,flowServices).status=="rejected","browser initial frame excluded"); browserHandler.get()->bWaitForNextFrame=0;
        browserPages[0].Type=1; Check(OpenSaveMenu(thread,flowServices).status=="rejected","duplicate configured Save type refused"); browserPages[0].Type=0;
        menuWorldInfo.get()->m_lstBrowserAlerts[1]=2; Check(OpenSaveMenu(thread,flowServices).status=="rejected","disabled native CanSave UI sample not overridden to enabled"); menuWorldInfo.get()->m_lstBrowserAlerts[1]=0;
        pageOverrides[0].State=2; Check(OpenSaveMenu(thread,flowServices).status=="rejected","effective override disables otherwise-normal Save"); pageOverrides[0].State=0;
        sfManagerProperty.get()->PropertyClass=managerType.get(); Check(OpenSaveMenu(thread,flowServices).status=="rejected","optional parameter exact Engine.BioSFManager declared class required"); sfManagerProperty.get()->PropertyClass=sfManagerType.get();
        ObserveSaveFlow(thread);
        for (int mutation : {1,2,3,4,5}) {
            flowMutation=mutation;
            auto refused=OpenSaveMenu(thread,flowServices);
            Check(refused.status=="rejected" && refused.payload.String("reason")=="save-flow-changed-before-entry","final provider mutation catches page/override/mode/metadata changes");
            flowMutation=0; browserPages[1].Type=1; pageOverrides[0].State=0; sfManagerProperty.get()->PropertyClass=sfManagerType.get();
            modes.get()->CurrentMode=7; showSave.get()->FunctionFlags=0x824803;
        }
        auto saveOpened=OpenSaveMenu(thread,flowServices);
        Check(saveOpened.status=="dispatched" && saveOpened.payload.Bool("parameterFrameIntact") && !saveOpened.payload.Bool("saveCompleted"),"bounded packed12 normal save opener only");
        Check(saveOpened.payload.Get("postObservation").Get("save").Get("handler").String("class")=="SFXGame.SFXSFHandler_PCSave","configured save tag found from typed reachable handler");
        Check(OpenSaveMenu(thread,flowServices).status=="rejected" && flowDispatchCalls==2,"save opener budget spent independently");
        Check(mevr::json::Write(saveOpened.payload,0).size()<=14*1024,"action and post flow fit wire budget");
        beginFlow(); flowBehavior=1;
        auto pauseError=OpenPauseMenu(thread,flowServices);
        Check(pauseError.status=="error" && engineReadCalls==flowReadsAtDispatch && OpenPauseMenu(thread,flowServices).status=="rejected","uncertain pause does not reread or retry");
        for (int mode : {1,2,3,4}) {
            beginFlow(); Check(OpenPauseMenu(thread,flowServices).status=="dispatched","fresh synthetic flow"); flowBehavior=mode;
            auto bad=OpenSaveMenu(thread,flowServices);
            Check(bad.status=="error" && engineReadCalls==flowReadsAtDispatch && OpenSaveMenu(thread,flowServices).status=="rejected","throw/adjacent canaries/optional-input mutation leaves save opener uncertain without reads or retry");
        }
        beginFlow(); OpenPauseMenu(thread,flowServices);
        SetSaveFlowSampleHook(MutateFlowSample);
        Check(!ObserveSaveFlow(thread).Bool("available"),"second-sample paused context change invalidates flow");
        SetSaveFlowSampleHook(nullptr); menuWorldInfo.get()->bPlayersOnly=1;
        Check(OpenSaveMenu(thread,flowServices).status=="rejected","invalidated comparison receipt cannot reenter after values restored");
        beginFlow(); OpenPauseMenu(thread,flowServices);
        auto originalSaveTag=browserPages[1].Tag; browserPages[1].Tag=otherPanel.get()->nmTag;
        auto changedReadyBrowser=ObserveSaveFlow(thread);
        Check(changedReadyBrowser.Get("browser").Bool("structuralSavePageSupported") &&
              !changedReadyBrowser.Get("context").Bool("sameOpenedFlow"),"otherwise-valid changed Save tag invalidates existing browser receipt");
        browserPages[1].Tag=originalSaveTag; ObserveSaveFlow(thread);
        Check(OpenSaveMenu(thread,flowServices).status=="rejected","restoring original ready browser values cannot rearm invalidated flow");
        beginFlow(); OpenPauseMenu(thread,flowServices); flowPc.get()->Pawn=pawn.get();
        ObserveSaveFlow(thread); flowPc.get()->Pawn=resourcePawn.get();
        Check(OpenSaveMenu(thread,flowServices).status=="rejected","observed pawn switch invalidates same-flow scope");
        beginFlow(); modes.get()->GameModes.GetData()[0]=reinterpret_cast<USFXGameModeBase*>(1);
        Check(!ObserveSaveFlow(thread).Bool("available"),"malformed mode pointer guarded"); modeObjects[0]=defaultMode.get();
        beginFlow(); Check(OpenPauseMenu(thread,flowServices).status=="dispatched","flow recovers only in new synthetic epoch");
        std::cout << "Representative save flow bytes: " << mevr::json::Write(ObserveSaveFlow(thread),0).size() << '\n';
        worldPtr = reinterpret_cast<UWorld*>(1);
        v = Observe(thread);
        Check(v.String("reason") == "guarded-engine-read-fault" && !Available(v, "objectCount"), "AV invalidates whole sample");
        std::cout << "Synthetic typed-chain, liveness, metadata, bounds, finite location and SEH checks passed\n";
        return 0;
    } catch (std::exception const& e) { std::cerr << e.what() << '\n'; return 1; }
}
