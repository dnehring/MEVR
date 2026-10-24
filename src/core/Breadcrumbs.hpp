// SPDX-License-Identifier: MIT
#pragma once

// Crash breadcrumbs (research/36): small lock-free rings of the last few hundred things that touched the
// engine, read only when the process crashes (plugin/CrashBreadcrumbs writes them out). Recording is a few
// plain stores, a timestamp and one interlocked add, so it can sit on ProcessEvent's hot path; nothing here
// allocates, locks or follows a pointer. Readers tolerate torn entries: each one carries the sequence number
// of the record it holds, written last, so a reader can tell a slot that was overwritten (or is being written
// right now) from the record it expected.
//
// Header-only and game-independent (the offline tests use it as is).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <intrin.h>

namespace mevr::breadcrumbs {

// The CPU's timestamp counter: invariant on every CPU this game runs on, and cheaper than
// QueryPerformanceCounter. The report converts it with a rate measured between install and the crash.
inline std::uint64_t Stamp() {
    return __rdtsc();
}

// The current thread's id, straight from the TEB (ClientId.UniqueThread, gs:[0x48] on x64): what
// GetCurrentThreadId returns, without the call.
inline std::uint32_t ThreadId() {
    return static_cast<std::uint32_t>(__readgsqword(0x48));
}

// A sequence number: the record's index + 1 in the low 30 bits (0 only for a slot never written, or once
// every 2^30 records), a state in the top two.
inline constexpr std::uint32_t kSeqMask = 0x3FFFFFFFu;
inline constexpr std::uint32_t kStateShift = 30;
inline std::uint32_t SeqOf(std::uint64_t index) {
    return static_cast<std::uint32_t>(index + 1) & kSeqMask;
}

// Where a dispatch is (the state bits of its seq).
enum class State : std::uint32_t {
    Engine = 0,  // inside the engine's call (ProcessEvent's original), not returned yet
    Mod = 2,     // the engine's call returned; the mod's handler for it is running
    Done = 3,    // all of it returned
};

// The counter and the entries on cache lines of their own (padding intended).
#pragma warning(push)
#pragma warning(disable : 4324)
template <class Entry, std::size_t N>
class Ring {
    static_assert(N != 0 && (N & (N - 1)) == 0, "a power of two");

public:
    static constexpr std::size_t kSize = N;

    constexpr Ring() = default;
    Ring(Ring const&) = delete;
    Ring& operator=(Ring const&) = delete;

    // Records claimed so far (the next record's index).
    std::uint64_t Count() const { return next_.load(std::memory_order_relaxed); }
    // The oldest record still in the ring: the readable ones are [First(), Count()).
    std::uint64_t First() const {
        std::uint64_t const n = Count();
        return n > N ? n - N : 0;
    }
    Entry& At(std::uint64_t index) { return entries_[index & (N - 1)]; }
    Entry const& At(std::uint64_t index) const { return entries_[index & (N - 1)]; }

    // Whether the slot still holds record `index`, completely written (any state).
    bool Holds(std::uint64_t index) const { return (LoadSeq(At(index)) & kSeqMask) == SeqOf(index); }
    State StateOf(std::uint64_t index) const { return static_cast<State>(LoadSeq(At(index)) >> kStateShift); }

protected:
    std::uint64_t Claim() { return next_.fetch_add(1, std::memory_order_relaxed); }
    // After the entry's fields: the seq goes last, so a reader never takes a half-written entry for this record.
    void Publish(std::uint64_t index, State state) {
        std::atomic_signal_fence(std::memory_order_release);  // x64 keeps stores in order; the compiler must too
        StoreSeq(At(index), SeqOf(index) | (static_cast<std::uint32_t>(state) << kStateShift));
    }
    // A later state for record `index`, unless its slot was reused meanwhile.
    void Advance(std::uint64_t index, State state) {
        Entry& e = At(index);
        std::uint32_t const seq = SeqOf(index);
        if ((LoadSeq(e) & kSeqMask) == seq) {
            StoreSeq(e, seq | (static_cast<std::uint32_t>(state) << kStateShift));
        }
    }

private:
    static std::uint32_t LoadSeq(Entry const& e) { return static_cast<std::uint32_t const volatile&>(e.seq); }
    static void StoreSeq(Entry& e, std::uint32_t seq) { static_cast<std::uint32_t volatile&>(e.seq) = seq; }

    alignas(64) std::atomic<std::uint64_t> next_{0};
    alignas(64) Entry entries_[N]{};
};
#pragma warning(pop)

// ---- dispatches: ProcessEvent (host) and the mod's CallFunction handlers -------------------------------

struct Dispatch {
    void const* function = nullptr;  // the UFunction
    void const* object = nullptr;    // the object it ran on
    std::uint64_t stamp = 0;         // Stamp() when it began
    std::uint32_t thread = 0;
    std::uint32_t seq = 0;
};
static_assert(sizeof(Dispatch) == 32);

template <std::size_t N>
class DispatchRing : public Ring<Dispatch, N> {
    using Base = Ring<Dispatch, N>;

public:
    // A dispatch begins (in `state`); the token is for Advance. Five stores, a timestamp and an interlocked add.
    std::uint64_t Begin(void const* function, void const* object, State state = State::Engine) {
        std::uint64_t const index = Base::Claim();
        Dispatch& e = Base::At(index);
        e.function = function;
        e.object = object;
        e.stamp = Stamp();
        e.thread = ThreadId();
        Base::Publish(index, state);
        return index;
    }
    void Advance(std::uint64_t token, State state) { Base::Advance(token, state); }
};

// ---- the mod's writes into engine memory ---------------------------------------------------------------

inline constexpr std::size_t kTagChars = 31;

struct Write {
    std::uint64_t first = 0;         // Stamp() of the first write this entry stands for
    std::uint64_t last = 0;          // and of the latest
    void const* address = nullptr;
    std::uint64_t value = 0;         // the first 8 bytes written, the latest write's
    char const* key = nullptr;       // the tag's pointer: compared for coalescing, never read through (it may
                                     // point into a mod DLL that was unloaded since)
    std::uint32_t size = 0;
    std::uint32_t thread = 0;
    std::uint32_t count = 0;         // writes coalesced into this entry
    std::uint32_t seq = 0;
    char tag[kTagChars + 1]{};       // the tag's text, copied
};

// Up to kTagChars characters of `tag`, zero-terminated (the rest of `out` zeroed).
inline void CopyTag(char (&out)[kTagChars + 1], char const* tag) {
    std::size_t i = 0;
    if (tag != nullptr) {
        for (; i < kTagChars && tag[i] != '\0'; ++i) {
            out[i] = tag[i];
        }
    }
    for (; i <= kTagChars; ++i) {
        out[i] = '\0';
    }
}

template <std::size_t N>
class WriteRing : public Ring<Write, N> {
    using Base = Ring<Write, N>;

public:
    // A write the same as one of the last kWindow entries (tag, address, size) is folded into it: per-tick
    // writes (the control rotation each tick) then keep one entry each instead of flushing the ring.
    static constexpr std::uint64_t kWindow = 16;

    void Record(char const* tag, void const* address, std::uint32_t size, std::uint64_t value) {
        std::uint64_t const now = Stamp();
        std::uint32_t const thread = ThreadId();
        std::uint64_t const n = Base::Count();
        for (std::uint64_t back = 1; back <= kWindow && back <= n && back <= N; ++back) {
            std::uint64_t const index = n - back;
            Write& e = Base::At(index);
            if (e.key == tag && e.address == address && e.size == size && Base::Holds(index)) {
                e.value = value;
                e.thread = thread;
                ++e.count;
                e.last = now;
                return;
            }
        }
        std::uint64_t const index = Base::Claim();
        Write& e = Base::At(index);
        e.first = now;
        e.last = now;
        e.address = address;
        e.value = value;
        e.key = tag;
        e.size = size;
        e.thread = thread;
        e.count = 1;
        CopyTag(e.tag, tag);
        Base::Publish(index, State::Done);
    }
};

// The first 8 bytes at `address` (fewer if `size` is smaller), as the journal keeps them.
inline std::uint64_t FirstBytes(void const* address, std::size_t size) {
    std::uint64_t value = 0;
    if (address != nullptr && size > 0) {
        std::memcpy(&value, address, size < sizeof(value) ? size : sizeof(value));
    }
    return value;
}

} // namespace mevr::breadcrumbs
