// SPDX-License-Identifier: MIT
#include "ue3/Runtime.hpp"

#include "core/Log.hpp"
#include "engine/GameAdapter.hpp"
#include "ue3/Memory.hpp"
#include "ue3/Pattern.hpp"

#include <Windows.h>

#include <algorithm>
#include <format>
#include <optional>
#include <span>

namespace mevr::ue3::runtime {
namespace {

// GObjects holds hundreds of thousands of objects once the script packages are in; far fewer
// means we're not looking at it (or it's too early).
constexpr std::int32_t kMinObjects = 1000;
constexpr std::int32_t kMaxObjects = 64 * 1024 * 1024;
constexpr std::int32_t kSampleObjects = 512;

bool gTried = false;
bool gReady = false;
bool gTransient = false;     // the last Locate failed only because the game is still starting
int gPendingTries = 0;
bool gToldPending = false;
std::string gStatus = "not started";
Reflection gReflection;
Found gFound;
std::uintptr_t gPoolBases[16]{};
Object* gEngine = nullptr;
std::int32_t gGamePlayersOffset = -1;  // Engine.Engine.GamePlayers
std::int32_t gActorOffset = -1;        // Engine.Player.Actor

struct TextSection {
    std::uintptr_t begin = 0;
    std::size_t size = 0;
};

TextSection FindText(std::uintptr_t module) {
    auto const* dos = reinterpret_cast<IMAGE_DOS_HEADER const*>(module);
    auto const* nt = reinterpret_cast<IMAGE_NT_HEADERS64 const*>(module + dos->e_lfanew);
    IMAGE_SECTION_HEADER const* section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (std::memcmp(section->Name, ".text", 5) == 0) {
            return {module + section->VirtualAddress, section->Misc.VirtualSize};
        }
    }
    return {};
}

bool InText(std::uintptr_t address) {
    TextSection const text = FindText(gFound.module);
    return address >= text.begin && address < text.begin + text.size;
}

// ---- guarded checks (no objects with destructors in the faulting paths) ---------------------

struct ObjectsCheck {
    Layout const* layout;
    std::uintptr_t address;
    bool ok;
    std::int32_t count;
    std::int32_t checked;
    std::int32_t badIndex;  // first sampled slot whose object's index didn't match, or -1
    bool tooFew;            // a well-formed array with fewer than kMinObjects: the game is still starting
};

void CheckObjects(void* context) {
    auto* c = static_cast<ObjectsCheck*>(context);
    auto const* array = reinterpret_cast<TArray<Object*> const*>(c->address);
    c->count = array->count;
    if (array->count >= 0 && array->count < kMinObjects && array->max >= array->count
        && (array->data != nullptr || array->count == 0)) {
        c->tooFew = true;
        return;
    }
    if (array->data == nullptr || array->count < kMinObjects || array->count > kMaxObjects || array->max < array->count) {
        return;
    }
    std::int32_t const n = std::min(array->count, kSampleObjects);
    for (std::int32_t i = 0; i < n; ++i) {
        Object const* const object = array->data[i];
        if (object == nullptr) {
            continue;
        }
        if (Reflection::Read<std::int32_t>(object, c->layout->objectIndex) != i
            || Reflection::Read<Object*>(object, c->layout->objectClass) == nullptr) {
            c->badIndex = i;
            return;
        }
        ++c->checked;
    }
    c->ok = c->checked >= 16;
}

struct NamesCheck {
    Reflection* reflection;
    bool ok;
    std::int32_t checked;
    std::int32_t bad;  // the first object whose name didn't read as printable text, or -1
};

void CheckNames(void* context) {
    auto* c = static_cast<NamesCheck*>(context);
    Reflection& r = *c->reflection;
    std::int32_t const n = std::min(r.ObjectCount(), kSampleObjects);
    for (std::int32_t i = 0; i < n; ++i) {
        Object const* const object = r.ObjectAt(i);
        if (object == nullptr) {
            continue;
        }
        Name const name = r.NameOf(object);
        if (name.index == 0) {
            continue;
        }
        if (names::Entry(r.layout(), r.universe().pools, name.index) == nullptr) {
            c->bad = i;
            return;
        }
        std::string const text = names::BaseText(r.layout(), r.universe().pools, name.index);
        if (text.empty() || text.size() > 512
            || !std::all_of(text.begin(), text.end(), [](char ch) { return ch >= 0x20 && ch < 0x7F; })) {
            c->bad = i;
            return;
        }
        ++c->checked;
    }
    c->ok = c->checked >= 16;
}

struct ReflectionCheck {
    Reflection* reflection;
    char const* failed;  // what didn't check out, or nullptr
    bool transient;      // Core checks out but Engine.Actor isn't loaded yet
};

void CheckReflection(void* context) {
    auto* c = static_cast<ReflectionCheck*>(context);
    Reflection& r = *c->reflection;
    if (r.ClassClass() == nullptr) {
        c->failed = "no Core.Class (the class whose class is itself)";
        return;
    }
    if (r.FunctionClass() == nullptr || r.PropertyClass() == nullptr || r.FindClass("Core.Object") == nullptr) {
        c->failed = "Core.Function, Core.Property or Core.Object missing";
        return;
    }
    // One property and one function deep enough to prove the UField/UStruct/UProperty/UFunction
    // offsets: Actor.Location is a 12-byte struct; Actor.SetRotation(NewRotation) starts at 0.
    Class* const actor = r.FindClass("Engine.Actor");
    if (actor == nullptr) {
        c->failed = "Engine.Actor isn't loaded yet";
        c->transient = true;
        return;
    }
    std::optional<PropertyInfo> const location = r.PropertyOf(actor, "Location");
    if (actor == nullptr || !location || location->kind != Kind::Struct || location->elementSize != 12) {
        c->failed = "Engine.Actor.Location isn't a 12-byte struct property";
        return;
    }
    Function* const setRotation = r.FindFunction(actor, "SetRotation");
    std::optional<PropertyInfo> const newRotation = r.PropertyOf(setRotation, "NewRotation");
    if (setRotation == nullptr || !newRotation || newRotation->offset != 0 || newRotation->elementSize != 12
        || (newRotation->flags & property_flags::kParm) == 0 || r.ParmsSize(setRotation) < 12) {
        c->failed = "Engine.Actor.SetRotation's NewRotation isn't a 12-byte parameter at 0";
        return;
    }
    c->failed = nullptr;
}

struct VtableRead {
    Object const* object;
    std::uint32_t slot;
    std::uintptr_t function;
};

void ReadVtable(void* context) {
    auto* c = static_cast<VtableRead*>(context);
    std::uintptr_t const* const vtable = *reinterpret_cast<std::uintptr_t const* const*>(c->object);
    c->function = vtable[c->slot / sizeof(std::uintptr_t)];
}

// ---- locating ------------------------------------------------------------------------------

// Every match of the pattern in .text, resolved to the RVA its instruction references; the
// pattern counts only if all its matches agree.
std::optional<std::uintptr_t> FromPattern(engine::adapter::GlobalPattern const& p, std::string& note) {
    std::optional<pattern::Compiled> const compiled = pattern::Compile(p.bytes != nullptr ? p.bytes : "");
    TextSection const text = FindText(gFound.module);
    if (!compiled || text.begin == 0) {
        note = "no pattern";
        return std::nullopt;
    }
    std::span<std::uint8_t const> const bytes(reinterpret_cast<std::uint8_t const*>(text.begin), text.size);
    std::vector<std::size_t> const hits = pattern::FindAll(bytes, *compiled, 16);
    std::optional<std::uintptr_t> target;
    for (std::size_t hit : hits) {
        std::optional<std::size_t> const t = pattern::RipTarget(bytes, hit + p.instruction, p.displacement, p.length);
        if (!t) {
            continue;
        }
        std::uintptr_t const address = text.begin + *t;
        if (target && *target != address) {
            note = std::format("{} matches disagree", hits.size());
            return std::nullopt;
        }
        target = address;
    }
    note = std::format("{} match(es)", hits.size());
    return target;
}

bool TryObjects(Layout const& layout, std::uintptr_t address, std::string& why, bool* tooFew = nullptr) {
    ObjectsCheck check{&layout, address, false, 0, 0, -1, false};
    if (!Guarded(&CheckObjects, &check)) {
        why = "reading it faulted";
        return false;
    }
    if (tooFew != nullptr) {
        *tooFew = check.tooFew;
    }
    if (check.tooFew) {
        why = std::format("only {} objects so far", check.count);
    } else if (!check.ok) {
        why = check.badIndex >= 0 ? std::format("object {} has another index", check.badIndex)
                                  : std::format("{} objects, {} checked", check.count, check.checked);
    }
    return check.ok;
}

bool TryNames(std::uintptr_t poolsAddress, std::uint32_t count, Universe& universe, std::string& why) {
    count = std::min<std::uint32_t>(count, static_cast<std::uint32_t>(std::size(gPoolBases)));
    struct Copy {
        std::uintptr_t from;
        std::uint32_t count;
    } copy{poolsAddress, count};
    bool const copied = Guarded(
        [](void* context) {
            auto* c = static_cast<Copy*>(context);
            std::memcpy(gPoolBases, reinterpret_cast<void const*>(c->from), c->count * sizeof(std::uintptr_t));
        },
        &copy);
    if (!copied || gPoolBases[0] == 0) {
        why = copied ? "pool 0 is null" : "reading the pools faulted";
        return false;
    }
    universe.pools = {gPoolBases, count};
    Reflection probe(universe);
    NamesCheck check{&probe, false, 0, -1};
    if (!Guarded(&CheckNames, &check) || !check.ok) {
        why = check.bad >= 0 ? std::format("object {}'s name doesn't read as text", check.bad)
                             : std::format("{} names read", check.checked);
        return false;
    }
    return true;
}

bool Locate() {
    std::optional<Layout> const layout = engine::adapter::ReflectionLayout();
    std::optional<engine::adapter::ReflectionGlobals> const globals = engine::adapter::ReflectionGlobalsForGame();
    if (!layout || !globals) {
        gStatus = "no reflection layout for this game yet (LE1 only, research/34)";
        return false;
    }
    gFound = {};
    gFound.module = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    Universe universe;
    universe.layout = *layout;

    // GObjects: the RVA if it checks out, else the pattern.
    std::string rvaWhy;
    std::string patternWhy;
    std::uintptr_t const objectsAtRva = gFound.module + globals->objectsRva;
    bool tooFew = false;
    if (TryObjects(*layout, objectsAtRva, rvaWhy, &tooFew)) {
        gFound.objects = objectsAtRva;
        gFound.objectsBy = "rva";
    } else if (tooFew) {
        // A well-formed, nearly empty GObjects at its RVA: the game is still starting (no pattern scan).
        gStatus = std::format("GObjects at its RVA 0x{:X} has {}: the game is still starting", globals->objectsRva, rvaWhy);
        gTransient = true;
        return false;
    } else {
        std::string note;
        std::optional<std::uintptr_t> const found = FromPattern(globals->objectsPattern, note);
        if (found && TryObjects(*layout, *found, patternWhy)) {
            gFound.objects = *found;
            gFound.objectsBy = "pattern";
            log::Warn("reflect: GObjects isn't at its RVA 0x{:X} ({}); the pattern found it at RVA 0x{:X} ({})",
                      globals->objectsRva, rvaWhy, *found - gFound.module, note);
        } else {
            gStatus = std::format("GObjects not found (RVA 0x{:X}: {}; pattern: {}{})", globals->objectsRva, rvaWhy, note,
                                  patternWhy.empty() ? "" : ", " + patternWhy);
            return false;
        }
    }
    universe.objects = reinterpret_cast<TArray<Object*> const*>(gFound.objects);

    // The name pools, likewise.
    std::uintptr_t const poolsAtRva = gFound.module + globals->namePoolsRva;
    if (TryNames(poolsAtRva, globals->namePoolCount, universe, rvaWhy)) {
        gFound.namePools = poolsAtRva;
        gFound.namePoolsBy = "rva";
    } else {
        std::string note;
        std::optional<std::uintptr_t> const found = FromPattern(globals->namePoolsPattern, note);
        patternWhy.clear();
        if (found && TryNames(*found, globals->namePoolCount, universe, patternWhy)) {
            gFound.namePools = *found;
            gFound.namePoolsBy = "pattern";
            log::Warn("reflect: the name pools aren't at their RVA 0x{:X} ({}); the pattern found them at RVA 0x{:X} ({})",
                      globals->namePoolsRva, rvaWhy, *found - gFound.module, note);
        } else {
            gStatus = std::format("name pools not found (RVA 0x{:X}: {}; pattern: {}{})", globals->namePoolsRva, rvaWhy, note,
                                  patternWhy.empty() ? "" : ", " + patternWhy);
            return false;
        }
    }

    gReflection = Reflection(universe);
    ReflectionCheck reflectionCheck{&gReflection, "it faulted", false};
    if (!Guarded(&CheckReflection, &reflectionCheck) || reflectionCheck.failed != nullptr) {
        gTransient = reflectionCheck.transient;
        gStatus = reflectionCheck.transient
            ? std::format("{}: the script packages are still loading", reflectionCheck.failed)
            : std::format("the reflection layout doesn't check out: {}", reflectionCheck.failed);
        gReflection = Reflection();
        return false;
    }

    // ProcessEvent: the slot in Core.Class's vtable (every UObject shares the base slots).
    VtableRead vtable{gReflection.ClassClass(), layout->processEventSlot, 0};
    if (!Guarded(&ReadVtable, &vtable) || !InText(vtable.function)) {
        gStatus = std::format("ProcessEvent's vtable slot 0x{:X} doesn't point into .text", layout->processEventSlot);
        gReflection = Reflection();
        return false;
    }
    gFound.processEvent = vtable.function;
    gFound.processEventMatchesRva = vtable.function == gFound.module + globals->processEventRva;
    return true;
}

// ---- the allocator (ue3/Memory) --------------------------------------------------------------

// The function at `rva` if its bytes are the pattern, else the pattern's only match in .text; and the
// GMalloc its load references. Nullopt (with `why`) if neither.
struct FoundFunction {
    std::uintptr_t address = 0;
    std::uintptr_t gmalloc = 0;
    char const* by = "none";
};

std::optional<FoundFunction> FindAllocatorFunction(engine::adapter::AllocatorFunction const& f, std::string& why) {
    std::optional<pattern::Compiled> const compiled = pattern::Compile(f.pattern != nullptr ? f.pattern : "");
    TextSection const text = FindText(gFound.module);
    if (!compiled || text.begin == 0 || compiled->bytes.size() < f.gmallocLoad + 7) {
        why = "no pattern";
        return std::nullopt;
    }
    std::span<std::uint8_t const> const bytes(reinterpret_cast<std::uint8_t const*>(text.begin), text.size);
    auto const resolve = [&](std::size_t offset, char const* by) -> std::optional<FoundFunction> {
        std::optional<std::size_t> const target = pattern::RipTarget(bytes, offset + f.gmallocLoad, 3, 7);
        if (!target) {
            return std::nullopt;
        }
        return FoundFunction{text.begin + offset, text.begin + *target, by};
    };
    std::uintptr_t const atRva = gFound.module + f.rva;
    if (atRva >= text.begin && atRva + compiled->bytes.size() <= text.begin + text.size) {
        std::size_t const offset = atRva - text.begin;
        std::vector<std::size_t> const here =
            pattern::FindAll(bytes.subspan(offset, compiled->bytes.size()), *compiled, 1);
        if (!here.empty()) {
            if (auto found = resolve(offset, "rva")) {
                return found;
            }
        }
    }
    std::vector<std::size_t> const hits = pattern::FindAll(bytes, *compiled, 4);
    if (hits.size() != 1) {
        why = std::format("not at its RVA 0x{:X}, and the pattern matches {} times", f.rva, hits.size());
        return std::nullopt;
    }
    return resolve(hits.front(), "pattern");
}

void InitMemory() {
    memory::Use({});
    std::optional<engine::adapter::AllocatorGlobals> const globals = engine::adapter::AllocatorGlobalsForGame();
    if (!globals) {
        log::Warn("reflect: no allocator for this game yet (LE1 only, research/34): engine strings unavailable");
        return;
    }
    std::string mallocWhy;
    std::string freeWhy;
    std::optional<FoundFunction> const malloc = FindAllocatorFunction(globals->malloc, mallocWhy);
    std::optional<FoundFunction> const free = FindAllocatorFunction(globals->free, freeWhy);
    if (!malloc || !free) {
        log::Warn("reflect: the engine's allocator isn't found (appMalloc: {}; appFree: {}): engine strings unavailable",
                  malloc ? "found" : mallocWhy, free ? "found" : freeWhy);
        return;
    }
    if (malloc->gmalloc != free->gmalloc) {
        log::Warn("reflect: appMalloc and appFree use different GMallocs (RVA 0x{:X} and 0x{:X}): engine strings unavailable",
                  malloc->gmalloc - gFound.module, free->gmalloc - gFound.module);
        return;
    }
    memory::Use({reinterpret_cast<memory::MallocFn>(malloc->address), reinterpret_cast<memory::FreeFn>(free->address)});
    gFound.appMalloc = malloc->address;
    gFound.appFree = free->address;
    gFound.gmalloc = malloc->gmalloc;
    gFound.appMallocBy = malloc->by;
    gFound.appFreeBy = free->by;
    log::Info("reflect: the engine's allocator: appMalloc at RVA 0x{:X} (by {}), appFree at RVA 0x{:X} (by {}), GMalloc at RVA 0x{:X}{}",
              malloc->address - gFound.module, malloc->by, free->address - gFound.module, free->by,
              malloc->gmalloc - gFound.module, malloc->gmalloc == gFound.module + globals->gmallocRva ? "" : " (not the expected RVA)");
}

} // namespace

bool Guarded(void (*fn)(void*), void* context) {
    __try {
        fn(context);
        return true;
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return false;
    }
}

bool Init() {
    if (gTried) {
        return gReady;
    }
    gTransient = false;
    gReady = Locate();
    if (!gReady && gTransient && ++gPendingTries < kMaxPendingTries) {
        if (!gToldPending) {
            gToldPending = true;
            log::Info("reflect: not ready yet: {}; trying again", gStatus);
        }
        return false;  // not an answer: the next call tries again
    }
    if (!gReady && gTransient) {
        gStatus += std::format(" (still after {} tries)", gPendingTries);
        gTransient = false;
    }
    gTried = true;
    if (gReady) {
        gStatus = std::format("ready: {} objects (GObjects by {}, names by {}), ProcessEvent at RVA 0x{:X}{}",
                              gReflection.ObjectCount(), gFound.objectsBy, gFound.namePoolsBy,
                              gFound.processEvent - gFound.module,
                              gFound.processEventMatchesRva ? "" : " (not the expected RVA)");
        log::Info("reflect: {}", gStatus);
        InitMemory();
    } else {
        log::Warn("reflect: our reflection layer is off: {}", gStatus);
    }
    FlushNotes();
    return gReady;
}

bool Ready() {
    return gReady;
}

bool Pending() {
    return !gTried && gTransient;
}

std::string const& Status() {
    return gStatus;
}

void Shutdown() {
    gTried = gReady = gTransient = gToldPending = false;
    gPendingTries = 0;
    memory::Use({});
    gStatus = "not started";
    gReflection = Reflection();
    gFound = {};
    gEngine = nullptr;
    gGamePlayersOffset = gActorOffset = -1;
}

Reflection& Get() {
    return gReflection;
}

Found const& Where() {
    return gFound;
}

void CallProcessEvent(Object* object, Function* function, void* parms) {
    using ProcessEventFn = void (*)(Object*, Function*, void*, void*);
    void* const* const vtable = *reinterpret_cast<void* const* const*>(object);
    auto const call = reinterpret_cast<ProcessEventFn>(vtable[gReflection.layout().processEventSlot / sizeof(void*)]);
    call(object, function, parms, nullptr);
}

bool Call(Object* object, Function* function, Params& params) {
    if (!gReady || object == nullptr || function == nullptr || !params.Valid()) {
        return false;
    }
    CallProcessEvent(object, function, params.Data());
    return true;
}

Object* GameEngine() {
    if (!gReady) {
        return nullptr;
    }
    if (gEngine != nullptr && gReflection.IsAlive(gEngine)) {
        return gEngine;
    }
    gEngine = nullptr;
    Class* const gameEngine = gReflection.FindClass("Engine.GameEngine");
    std::int32_t const count = gReflection.ObjectCount();
    for (std::int32_t i = 0; gameEngine != nullptr && i < count; ++i) {
        Object* const object = gReflection.ObjectAt(i);
        if (object != nullptr && gReflection.IsA(object, gameEngine) && object != static_cast<Object*>(gameEngine)
            && gReflection.NameText(object).rfind("Default__", 0) != 0) {
            gEngine = object;
            break;
        }
    }
    return gEngine;
}

Object* LocalPlayerController() {
    Object* const engine = GameEngine();
    if (engine == nullptr) {
        return nullptr;
    }
    // Per tick from some handlers: the two offsets are looked up once.
    Reflection& r = gReflection;
    if (gGamePlayersOffset < 0) {
        std::optional<PropertyInfo> const players = r.PropertyOf(r.ClassOf(engine), "GamePlayers");
        if (!players || players->kind != Kind::Array) {
            return nullptr;
        }
        gGamePlayersOffset = players->offset;
    }
    TArray<Object*> const array = Reflection::Read<TArray<Object*>>(engine, static_cast<std::uint32_t>(gGamePlayersOffset));
    if (!array.Valid(0) || array.data[0] == nullptr) {
        return nullptr;
    }
    Object* const player = array.data[0];
    if (gActorOffset < 0) {
        std::optional<PropertyInfo> const actor = r.PropertyOf(r.ClassOf(player), "Actor");
        if (!actor || actor->elementSize != static_cast<std::int32_t>(sizeof(void*))) {
            return nullptr;
        }
        gActorOffset = actor->offset;
    }
    return Reflection::Read<Object*>(player, static_cast<std::uint32_t>(gActorOffset));
}

bool IsAlive(Object const* object) {
    return gReady && gReflection.IsAlive(object);
}

std::string FullName(Object const* object) {
    return gReady && object != nullptr ? gReflection.FullName(object) : std::string("None");
}

std::string NameText(Name name) {
    return gReady ? gReflection.ToString(name) : std::string("None");
}

void FlushNotes() {
    for (std::string const& note : gReflection.TakeNotes()) {
        log::Warn("reflect: {}", note);
    }
}

} // namespace mevr::ue3::runtime
