// SPDX-License-Identifier: MIT
// Offline tests for the crash breadcrumbs (core/Breadcrumbs, core/FixedText, core/WriteJournal; research/36): the
// rings' order, wrap-around and states, the write journal's folding, the fixed-buffer text, and what a record costs.

#include "core/Breadcrumbs.hpp"
#include "core/CallQueue.hpp"
#include "core/FixedText.hpp"
#include "core/WriteJournal.hpp"

#include <Windows.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, char const* what, int line) {
    if (!ok) {
        std::printf("FAIL tests/BreadcrumbTests.cpp line %d: %s\n", line, what);
        ++gFailures;
    }
}
#define CHECK(expr) Check((expr), #expr, __LINE__)

using namespace mevr;
using breadcrumbs::State;

void const* P(std::uintptr_t v) {
    return reinterpret_cast<void const*>(v);
}

void TestDispatchRing() {
    static breadcrumbs::DispatchRing<8> ring;
    CHECK(ring.Count() == 0 && ring.First() == 0);
    CHECK(!ring.Holds(0));  // never written
    std::uint64_t const a = ring.Begin(P(0x100), P(0x200));
    std::uint64_t const b = ring.Begin(P(0x101), P(0x201), State::Mod);
    CHECK(a == 0 && b == 1);
    CHECK(ring.Holds(a) && ring.Holds(b));
    CHECK(ring.StateOf(a) == State::Engine);
    CHECK(ring.StateOf(b) == State::Mod);
    CHECK(ring.At(a).function == P(0x100) && ring.At(a).object == P(0x200));
    CHECK(ring.At(a).thread == GetCurrentThreadId());
    CHECK(ring.At(b).stamp >= ring.At(a).stamp);
    ring.Advance(a, State::Mod);
    CHECK(ring.StateOf(a) == State::Mod && ring.Holds(a));
    ring.Advance(a, State::Done);
    CHECK(ring.StateOf(a) == State::Done && ring.Holds(a));

    // Wrap: 8 slots, 13 records. The oldest five are gone; advancing one of them must not touch its slot's new record.
    for (std::uintptr_t i = 2; i < 13; ++i) {
        ring.Begin(P(0x100 + i), P(0x200 + i));
    }
    CHECK(ring.Count() == 13);
    CHECK(ring.First() == 5);
    CHECK(!ring.Holds(0) && !ring.Holds(4));
    CHECK(ring.Holds(5) && ring.Holds(12));
    CHECK(ring.At(12).function == P(0x10C));
    CHECK(ring.At(4).function == P(0x10C));  // the same slot as 12
    ring.Advance(4, State::Done);            // record 4 is gone: slot keeps record 12's state
    CHECK(ring.StateOf(12) == State::Engine);
    // Oldest first, in order.
    bool ordered = true;
    for (std::uint64_t i = ring.First(); i < ring.Count(); ++i) {
        ordered = ordered && ring.At(i).function == P(0x100 + static_cast<std::uintptr_t>(i));
    }
    CHECK(ordered);
}

void TestWriteRing() {
    static breadcrumbs::WriteRing<32> ring;
    static char const kAim[] = "HandAim.aim";
    static char const kLevel[] = "ViewLevel.ControlRotation";
    std::uint32_t value = 7;
    ring.Record(kAim, &value, 4, 7);
    ring.Record(kAim, &value, 4, 8);  // folds into the first
    CHECK(ring.Count() == 1);
    CHECK(ring.At(0).count == 2 && ring.At(0).value == 8);
    CHECK(ring.At(0).last >= ring.At(0).first);
    CHECK(std::string(ring.At(0).tag) == kAim);
    CHECK(ring.Holds(0) && ring.StateOf(0) == State::Done);
    std::uint32_t other = 0;
    ring.Record(kAim, &other, 4, 1);  // another address: its own entry
    ring.Record(kLevel, &value, 4, 2);  // another tag: its own entry
    ring.Record(kAim, &value, 2, 3);  // another size: its own entry
    CHECK(ring.Count() == 4);
    ring.Record(kAim, &value, 4, 9);  // within the window: folds into entry 0
    CHECK(ring.Count() == 4 && ring.At(0).count == 3 && ring.At(0).value == 9);
    // Past the window (16 other entries since): a new entry.
    for (std::uintptr_t i = 0; i < 16; ++i) {
        ring.Record(kLevel, P(0x1000 + i * 16), 12, i);
    }
    CHECK(ring.Count() == 20);
    ring.Record(kAim, &value, 4, 10);
    CHECK(ring.Count() == 21 && ring.At(20).count == 1 && ring.At(20).value == 10);
    // Long tags are cut, never overrun.
    static char const kLong[] = "A.tag.longer.than.thirty.one.characters";
    ring.Record(kLong, &value, 4, 0);
    CHECK(std::string(ring.At(ring.Count() - 1).tag) == std::string(kLong).substr(0, breadcrumbs::kTagChars));
    char tag[breadcrumbs::kTagChars + 1];
    std::memset(tag, 'x', sizeof(tag));
    breadcrumbs::CopyTag(tag, nullptr);
    CHECK(tag[0] == '\0' && tag[breadcrumbs::kTagChars] == '\0');
    CHECK(breadcrumbs::FirstBytes(&value, 2) == (value & 0xFFFF));
    std::uint64_t big[2] = {0x1122334455667788ull, 0x99};
    CHECK(breadcrumbs::FirstBytes(big, sizeof(big)) == 0x1122334455667788ull);
    CHECK(breadcrumbs::FirstBytes(nullptr, 8) == 0);
}

// The journal: nothing until connected, then every Wrote reaches the recorder with the written bytes.
std::vector<std::pair<std::string, std::uint64_t>> gJournal;
void Recorder(char const* tag, void const* /*address*/, std::uint32_t size, std::uint64_t value) {
    gJournal.emplace_back(std::string(tag) + "/" + std::to_string(size), value);
}

void TestWriteJournal() {
    float f = 1.5f;
    write_journal::Wrote("Test.unconnected", &f);
    CHECK(gJournal.empty());
    write_journal::Connect(&Recorder);
    write_journal::Wrote("Test.float", &f);
    std::uint8_t bytes[3] = {1, 2, 3};
    write_journal::Wrote("Test.bytes", bytes, sizeof(bytes));
    write_journal::Connect(nullptr);
    write_journal::Wrote("Test.after", &f);
    CHECK(gJournal.size() == 2);
    if (gJournal.size() == 2) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &f, 4);
        CHECK(gJournal[0].first == "Test.float/4" && gJournal[0].second == bits);
        CHECK(gJournal[1].first == "Test.bytes/3" && gJournal[1].second == 0x030201u);
    }
}

// Four threads hammering one ring: every slot that says it holds a record holds all of that record.
void TestConcurrent() {
    static breadcrumbs::DispatchRing<128> ring;
    constexpr int kThreads = 4;
    constexpr std::uintptr_t kPerThread = 200000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([t] {
            for (std::uintptr_t i = 0; i < kPerThread; ++i) {
                std::uintptr_t const key = (static_cast<std::uintptr_t>(t + 1) << 32) | i;
                std::uint64_t const token = ring.Begin(P(key), P(key ^ 0x5A5A5A5Aull));
                ring.Advance(token, State::Done);
            }
        });
    }
    for (std::thread& t : threads) {
        t.join();
    }
    CHECK(ring.Count() == kThreads * kPerThread);
    int held = 0;
    int consistent = 0;
    for (std::uint64_t i = ring.First(); i < ring.Count(); ++i) {
        if (!ring.Holds(i)) {
            continue;
        }
        ++held;
        breadcrumbs::Dispatch const e = ring.At(i);
        auto const key = reinterpret_cast<std::uintptr_t>(e.function);
        consistent += reinterpret_cast<std::uintptr_t>(e.object) == (key ^ 0x5A5A5A5Aull) && (key >> 32) >= 1 &&
            (key >> 32) <= kThreads;
    }
    // A slot can only mix two records when one writer lags a whole ring behind another: rare, and then flagged
    // by its seq, so nearly every slot holds its record intact.
    CHECK(held >= 120);
    CHECK(consistent == held);
}

void TestAppender() {
    char buffer[64];
    text::Appender a(buffer, sizeof(buffer));
    a.Str("x=").Dec(-42).Char(' ').UDec(0).Char(' ').Hex(0xABC).Char(' ').Hex(5, 4).Char(' ').Milli(-12345).Char(' ').Milli(7);
    CHECK(std::string(a.View()) == "x=-42 0 0xABC 0x0005 -12.345 0.007");
    CHECK(std::strlen(buffer) == a.Size() && !a.Truncated());
    a.Clear();
    a.Dec(INT64_MIN);
    CHECK(std::string(a.View()) == "-9223372036854775808");
    a.Clear();
    std::size_t const start = a.Size();
    a.Str("ab").PadTo(start, 5).Char('|');
    CHECK(std::string(a.View()) == "ab   |");
    a.Clear();
    a.Wide(L"Abé");
    CHECK(std::string(a.View()) == "Ab?");
    char tiny[6];
    text::Appender t(tiny, sizeof(tiny));
    t.Str("abcdefgh");
    CHECK(t.Truncated() && t.Size() == 5 && std::string(tiny) == "abcde");
    t.Rewind(2);
    CHECK(std::string(tiny) == "ab");
}

// The game-thread call queue (core/CallQueue): oldest first, bounded with the oldest dropped and counted, the
// parameter block copied (the caller's goes away), a block too big refused.
void TestCallQueue() {
    static call_queue::Queue<4> queue;
    static call_queue::Call out[4];
    CHECK(queue.TakeAll(out) == 0);
    for (std::uint32_t i = 0; i < 6; ++i) {
        std::uint32_t parms[2] = {i, i * 10};
        CHECK(queue.Push(reinterpret_cast<void*>(std::uintptr_t{0x100} + i), reinterpret_cast<void*>(std::uintptr_t{0x200}),
            P(0x300), parms, sizeof(parms)));
        parms[0] = 0xDEAD;  // the queue kept its own copy
    }
    CHECK(queue.Size() == 4 && queue.Pushed() == 6 && queue.Dropped() == 2);
    std::size_t const n = queue.TakeAll(out);
    CHECK(n == 4 && queue.Size() == 0);
    bool ok = n == 4;
    for (std::size_t i = 0; i < n && ok; ++i) {
        std::uint32_t parms[2] = {};
        std::memcpy(parms, out[i].parms, sizeof(parms));
        std::uint32_t const want = static_cast<std::uint32_t>(i) + 2;  // calls 0 and 1 were dropped
        ok = out[i].object == reinterpret_cast<void*>(std::uintptr_t{0x100} + want) && out[i].size == 8 && parms[0] == want &&
            parms[1] == want * 10 && out[i].parms[8] == 0 && out[i].objectClass == P(0x300);
    }
    CHECK(ok);
    std::uint8_t big[call_queue::kMaxParms + 1] = {};
    CHECK(!queue.Push(nullptr, nullptr, nullptr, big, sizeof(big)));
    CHECK(queue.Refused() == 1 && queue.Size() == 0);
    CHECK(queue.Push(reinterpret_cast<void*>(std::uintptr_t{1}), nullptr, nullptr, nullptr, 0));  // no parameters
    CHECK(queue.TakeAll(out) == 1 && out[0].size == 0);
    // Pushes from several threads: every call either taken or counted as dropped.
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([] {
            std::uint64_t value = 7;
            for (int i = 0; i < 1000; ++i) {
                queue.Push(reinterpret_cast<void*>(std::uintptr_t{1}), nullptr, nullptr, &value, sizeof(value));
            }
        });
    }
    std::size_t taken = 0;
    for (int i = 0; i < 2000; ++i) {
        taken += queue.TakeAll(out);
    }
    for (std::thread& t : threads) {
        t.join();
    }
    taken += queue.TakeAll(out);
    CHECK(queue.Pushed() == 6 + 1 + 4000);
    CHECK(taken + (queue.Dropped() - 2) == 4000);
}

// What a breadcrumb costs on the hot path: ProcessEvent's Begin + two Advances, and a journal write.
void MeasureCost() {
    static breadcrumbs::DispatchRing<128> ring;
    static breadcrumbs::WriteRing<128> writes;
    constexpr int kCount = 5000000;
    auto const t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        std::uint64_t const token = ring.Begin(P(static_cast<std::uintptr_t>(i)), P(0x1000));
        ring.Advance(token, State::Mod);
        ring.Advance(token, State::Done);
    }
    auto const t1 = std::chrono::steady_clock::now();
    static char const kTag[] = "Cost.write";
    float value = 0.0f;
    for (int i = 0; i < kCount; ++i) {
        value = static_cast<float>(i);
        writes.Record(kTag, &value, 4, breadcrumbs::FirstBytes(&value, 4));  // folds: the per-tick case
    }
    auto const t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCount; ++i) {
        writes.Record(kTag, P(static_cast<std::uintptr_t>(i) * 16), 4, 0);  // a new entry each time: the worst case
    }
    auto const t3 = std::chrono::steady_clock::now();
    auto const ns = [](auto a, auto b) {
        return std::chrono::duration<double, std::nano>(b - a).count() / kCount;
    };
    std::printf("breadcrumbs: ProcessEvent record %.1f ns, journal write %.1f ns folded / %.1f ns new entry\n", ns(t0, t1),
        ns(t1, t2), ns(t2, t3));
    CHECK(ring.Count() == static_cast<std::uint64_t>(kCount));
}

} // namespace

int RunBreadcrumbTests() {
    TestDispatchRing();
    TestWriteRing();
    TestWriteJournal();
    TestConcurrent();
    TestAppender();
    TestCallQueue();
    MeasureCost();
    return gFailures;
}
