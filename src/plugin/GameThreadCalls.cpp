// SPDX-License-Identifier: MIT
#include "plugin/GameThreadCalls.hpp"

#include "core/CallQueue.hpp"
#include "core/FixedText.hpp"
#include "core/Log.hpp"
#include "engine/GameAdapter.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#include "ue3/Types.hpp"

#include <Windows.h>

#include <cstdint>
#include <optional>

namespace mevr::host::game_thread {
namespace {

constexpr std::size_t kCapacity = 64;
call_queue::Queue<kCapacity> gQueue;

struct Engine {
    bool ok = false;
    ue3::Layout layout;
    std::uintptr_t objects = 0;  // &GObjects
};

Engine const& Get() {
    static Engine const instance = [] {
        Engine e;
        auto const layout = engine::adapter::ReflectionLayout();
        auto const globals = engine::adapter::ReflectionGlobalsForGame();
        if (layout && globals) {
            e.layout = *layout;
            e.objects = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) + globals->objectsRva;
            e.ok = true;
        }
        return e;
    }();
    return instance;
}

// The function's ParmsSize and the object's class; false if either can't be read.
bool Describe(Engine const& e, void* object, void* function, std::uint16_t& parmsSize, void const*& objectClass) {
    __try {
        parmsSize = *reinterpret_cast<std::uint16_t const*>(static_cast<std::uint8_t const*>(function) + e.layout.functionParmsSize);
        objectClass = *reinterpret_cast<void const* const*>(static_cast<std::uint8_t const*>(object) + e.layout.objectClass);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool Listed(Engine const& e, void const* object) {
    auto const* const objects = reinterpret_cast<ue3::TArray<void*> const*>(e.objects);
    std::int32_t const index = *reinterpret_cast<std::int32_t const*>(static_cast<std::uint8_t const*>(object) + e.layout.objectIndex);
    return index >= 0 && index < objects->count && objects->data != nullptr && objects->data[index] == object;
}

// Still the object it was when queued: in GObjects at its index, not pending kill, the same class.
bool StillAlive(Engine const& e, call_queue::Call const& c) {
    __try {
        if (!Listed(e, c.object) || !Listed(e, c.function)) {
            return false;
        }
        auto const* const base = static_cast<std::uint8_t const*>(c.object);
        std::uint64_t const flags = *reinterpret_cast<std::uint64_t const*>(base + e.layout.objectFlags);
        void const* const cls = *reinterpret_cast<void const* const*>(base + e.layout.objectClass);
        return (flags & e.layout.pendingKillFlag) == 0 && cls == c.objectClass;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

using ProcessEventFn = void(__fastcall*)(void*, void*, void*, void*);

// Through the object's vtable, as Halcyon's own call did (so every ProcessEvent hook sees it).
bool Run(Engine const& e, call_queue::Call& c) {
    __try {
        void** const vtable = *reinterpret_cast<void***>(c.object);
        auto const processEvent = reinterpret_cast<ProcessEventFn>(vtable[e.layout.processEventSlot / sizeof(void*)]);
        if (processEvent == nullptr) {
            return false;
        }
        processEvent(c.object, c.function, c.parms, nullptr);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Counts, game thread only.
std::uint64_t gRun = 0;
std::uint64_t gSkipped = 0;
std::uint64_t gFailed = 0;
bool gToldFirst = false;
ULONGLONG gNextStatsMs = 0;
std::uint64_t gStatsPushed = 0;

} // namespace

bool OnGameThread() {
    std::uint32_t const game = crash::gGameThread.load(std::memory_order_relaxed);
    return game != 0 && breadcrumbs::ThreadId() == game;
}

bool Queue(void* object, void* function, void const* parms) {
    Engine const& e = Get();
    std::uint16_t size = 0;
    void const* cls = nullptr;
    if (!e.ok || object == nullptr || function == nullptr || !Describe(e, object, function, size, cls)) {
        return false;
    }
    return gQueue.Push(object, function, cls, parms, size);
}

void Drain() {
    if (gQueue.Pushed() == 0) {
        return;
    }
    Engine const& e = Get();
    static call_queue::Call calls[kCapacity];
    std::size_t const n = gQueue.TakeAll(calls);
    for (std::size_t i = 0; i < n; ++i) {
        call_queue::Call& c = calls[i];
        if (!gToldFirst) {
            gToldFirst = true;
            char buffer[512];
            text::Appender name(buffer, sizeof(buffer));
            crash::AppendObject(name, c.function);
            log::Info("game thread calls: a ProcessEvent from another thread ({}) was queued and runs at the next engine tick "
                      "(script must run on the game thread only; research/36)", name.View());
        }
        if (!StillAlive(e, c)) {
            ++gSkipped;
            continue;
        }
        (Run(e, c) ? gRun : gFailed) += 1;
    }
    ULONGLONG const now = GetTickCount64();
    std::uint64_t const pushed = gQueue.Pushed();
    if (pushed != gStatsPushed && now >= gNextStatsMs) {
        gNextStatsMs = now + 10000;
        gStatsPushed = pushed;
        log::Info("game thread calls: {} queued from other threads, {} run, {} skipped (object gone), {} faulted, {} dropped "
                  "(queue full), {} refused (parameters too big)",
            pushed, gRun, gSkipped, gFailed, gQueue.Dropped(), gQueue.Refused());
    }
}

} // namespace mevr::host::game_thread
