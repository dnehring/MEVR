// SPDX-License-Identifier: MIT
#include "plugin/ProcessEventHook.hpp"

#include "core/Hook.hpp"
#include "core/Log.hpp"
#include "engine/Addresses.hpp"
#include "plugin/CrashBreadcrumbs.hpp"
#if defined(MEVR_LE2_DIAGNOSTIC)
#include "ue3/Runtime.hpp"
#include <cstring>
#include <limits>
#endif

#include <Windows.h>

#include <atomic>

namespace mevr::host::process_event {
namespace {

using tProcessEvent = void(ue3::Object* context, ue3::Function* function, void* parms, void* result);
// Written by LEBinkProxy before the hook is enabled (core/Hook): attach runs on LEBinkProxy's
// thread while the game thread is already calling ProcessEvent.
tProcessEvent* gOriginal = nullptr;

#if defined(MEVR_LE2_DIAGNOSTIC)
SRWLOCK gDiagnosticLock = SRWLOCK_INIT;
diagnostic::Snapshot gDiagnostic{};

void PublishDiagnostic(diagnostic::Snapshot const& snapshot) noexcept {
    AcquireSRWLockExclusive(&gDiagnosticLock);
    gDiagnostic = snapshot;
    ReleaseSRWLockExclusive(&gDiagnosticLock);
}

char const* ExeClassification() noexcept {
    auto const* result = exe::Find(static_cast<std::uint32_t>(engine::addresses::kProcessEvent));
    if (!result) return "Unavailable";
    switch (result->status) {
    case exe::Status::AtRva: return "AtRva";
    case exe::Status::AtRvaHooked: return "AtRvaHooked";
    case exe::Status::AtRvaUnchecked: return "AtRvaUnchecked";
    case exe::Status::BytesDiffer: return "BytesDiffer";
    case exe::Status::Moved: return "Moved";
    case exe::Status::Missing: return "Missing";
    }
    return "Unavailable";
}

struct DiagnosticCopy { std::uintptr_t source; void* destination; std::size_t size; };
void CopyDiagnostic(void* raw) {
    auto const& copy = *static_cast<DiagnosticCopy const*>(raw);
    std::memcpy(copy.destination, reinterpret_cast<void const*>(copy.source), copy.size);
}
bool ReadExecutable(std::uintptr_t address, void* destination, std::size_t size, void*) noexcept {
    if (!address || !size || size > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    auto const end = address + size;
    for (auto cursor = address; cursor < end;) {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void const*>(cursor), &memory, sizeof(memory)) != sizeof(memory) ||
            memory.State != MEM_COMMIT || (memory.Protect & PAGE_GUARD)) return false;
        DWORD const protection = memory.Protect & 0xFF;
        if (protection != PAGE_EXECUTE_READ && protection != PAGE_EXECUTE_READWRITE &&
            protection != PAGE_EXECUTE_WRITECOPY) return false;
        auto const base = reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if (memory.RegionSize > std::numeric_limits<std::uintptr_t>::max() - base) return false;
        auto const next = base + memory.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    DiagnosticCopy copy{address, destination, size};
    return ue3::runtime::Guarded(&CopyDiagnostic, &copy);
}
#endif

std::atomic<Callback> gCallback{nullptr};

// ProcessEvent off the game thread (research/36): UnrealScript's VM keeps its state in process-wide globals, so a
// call on another thread races the game thread's script (the 2026-10-23 crash: Halcyon's Present-thread hides).
// Counted per function here (the rare path), named and logged by the game thread (ReportOffThread).
struct OffThread {
    std::atomic<void const*> function{nullptr};
    std::atomic<std::uint32_t> thread{0};  // the latest caller's
    std::atomic<std::uint64_t> count{0};
    std::uint64_t reported = 0;  // game thread only
    ULONGLONG reportedAtMs = 0;
};
constexpr int kOffThreadSlots = 16;
OffThread gOffThread[kOffThreadSlots];
std::atomic<std::uint64_t> gOffThreadTotal{0};
std::atomic<std::uint64_t> gOffThreadUnlisted{0};  // functions past the table's 16
std::uint64_t gOffThreadUnlistedReported = 0;

void NoteOffThread(void const* function, std::uint32_t thread) {
    gOffThreadTotal.fetch_add(1, std::memory_order_relaxed);
    for (OffThread& slot : gOffThread) {
        void const* seen = slot.function.load(std::memory_order_acquire);
        if (seen == nullptr && slot.function.compare_exchange_strong(seen, function)) {
            seen = function;
        }
        if (seen == function) {
            slot.thread.store(thread, std::memory_order_relaxed);
            slot.count.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    gOffThreadUnlisted.fetch_add(1, std::memory_order_relaxed);
}
// Threads inside the callback. A counter rather than a lock: handlers call ProcessEvent
// themselves, and a reader-writer lock taken again while a writer waits would deadlock.
std::atomic<int> gInFlight{0};

// No C++ objects with destructors here, so __finally can keep the count right when an exception
// unwinds through the callback (Halcyon calls ProcessEvent under __except in places).
// The crash breadcrumb (research/36) is taken before the engine's call, so a dispatch that never returns is
// in the ring: a few stores, a timestamp and an interlocked add, then two state updates.
void ProcessEventHook(ue3::Object* context, ue3::Function* function, void* parms, void* result) {
    std::uint64_t const crumb = crash::gProcessEvents.Begin(function, context);
    std::uint32_t const gameThread = crash::gGameThread.load(std::memory_order_relaxed);
    if (gameThread != 0) {
        std::uint32_t const thread = breadcrumbs::ThreadId();
        if (thread != gameThread) {
            NoteOffThread(function, thread);
        }
    }
    gOriginal(context, function, parms, result);
    gInFlight.fetch_add(1);
    __try {
        if (Callback const callback = gCallback.load()) {
            crash::gProcessEvents.Advance(crumb, breadcrumbs::State::Mod);
            callback(context, function, parms, result);
        }
        crash::gProcessEvents.Advance(crumb, breadcrumbs::State::Done);
    } __finally {
        gInFlight.fetch_sub(1);
    }
}

} // namespace

bool Install(hook::Context& init) {
    auto* const target = init.ResolveTyped<tProcessEvent>(engine::addresses::kProcessEvent);
#if defined(MEVR_LE2_DIAGNOSTIC)
    diagnostic::Snapshot snapshot{};
    snapshot.target = reinterpret_cast<std::uintptr_t>(target);
    snapshot.detour = reinterpret_cast<std::uintptr_t>(&ProcessEventHook);
    snapshot.backend = init.Spi() ? "loader-spi" : "local-minhook";
    snapshot.exeCheck = ExeClassification();
#endif
    if (target == nullptr) {
#if defined(MEVR_LE2_DIAGNOSTIC)
        snapshot.installation = diagnostic::InstallState::ResolveFailed;
        PublishDiagnostic(snapshot);
#endif
        log::Error("failed to resolve UObject::ProcessEvent");
        return false;
    }
    if (!hook::Install(init, "UObject::ProcessEvent", target, &ProcessEventHook, gOriginal)) {
#if defined(MEVR_LE2_DIAGNOSTIC)
        snapshot.installation = diagnostic::InstallState::InstallFailed;
        PublishDiagnostic(snapshot);
#endif
        log::Error("failed to hook UObject::ProcessEvent");
        return false;
    }
#if defined(MEVR_LE2_DIAGNOSTIC)
    snapshot.installation = diagnostic::InstallState::Installed;
    snapshot.nextHop = reinterpret_cast<std::uintptr_t>(gOriginal);
    PublishDiagnostic(snapshot);
#endif
    log::Info("hooked UObject::ProcessEvent (host-owned)");
    return true;
}

#if defined(MEVR_LE2_DIAGNOSTIC)
diagnostic::Snapshot ReadDiagnosticSnapshot() noexcept {
    AcquireSRWLockShared(&gDiagnosticLock);
    auto snapshot = gDiagnostic;
    ReleaseSRWLockShared(&gDiagnosticLock);
    if (snapshot.installation == diagnostic::InstallState::Installed) {
        snapshot.chain = diagnostic::CheckChain(snapshot.target, snapshot.detour, &ReadExecutable, nullptr);
    }
    return snapshot;
}
#endif

void ReportOffThread() {
    if (gOffThreadTotal.load(std::memory_order_relaxed) == 0) {
        return;
    }
    constexpr ULONGLONG kEveryMs = 10000;
    ULONGLONG const now = GetTickCount64();
    for (OffThread& slot : gOffThread) {
        void const* const function = slot.function.load(std::memory_order_acquire);
        std::uint64_t const count = slot.count.load(std::memory_order_relaxed);
        if (function == nullptr || count == slot.reported || (slot.reported != 0 && now - slot.reportedAtMs < kEveryMs)) {
            continue;
        }
        char buffer[512];
        text::Appender name(buffer, sizeof(buffer));
        crash::AppendObject(name, function);
        log::Warn("process event: off the game thread: {} on thread {} ({} calls, {} new; research/36)", name.View(),
            slot.thread.load(std::memory_order_relaxed), count, count - slot.reported);
        slot.reported = count;
        slot.reportedAtMs = now;
    }
    std::uint64_t const unlisted = gOffThreadUnlisted.load(std::memory_order_relaxed);
    if (unlisted != gOffThreadUnlistedReported) {
        log::Warn("process event: off the game thread: {} calls of functions past the first {}", unlisted, kOffThreadSlots);
        gOffThreadUnlistedReported = unlisted;
    }
}

std::uint64_t OffThreadCalls() {
    return gOffThreadTotal.load(std::memory_order_relaxed);
}

bool SetCallback(Callback callback, std::uint32_t waitMs) {
    gCallback.store(callback);
    if (callback != nullptr) {
        return true;
    }
    // A thread that read the old callback has already counted itself in.
    ULONGLONG const start = GetTickCount64();
    while (gInFlight.load() != 0) {
        if (GetTickCount64() - start >= waitMs) {
            if (waitMs > 0) {
                log::Error("ProcessEvent: {} call(s) still inside the old callback after {} ms", gInFlight.load(), waitMs);
            }
            return false;
        }
        SwitchToThread();
    }
    return true;
}

} // namespace mevr::host::process_event
