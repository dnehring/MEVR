// SPDX-License-Identifier: MIT
#include "core/CallScope.hpp"
#include <Windows.h>
#include <array>
#include <cstdio>
#include <stdexcept>
#include <type_traits>

namespace {
struct Object {};
struct Function {};
using Scope = mevr::CallScope<Object, Function>;
static_assert(std::is_trivially_destructible_v<Scope::Invocation>);
Object testObject;
Function testFunction;
int failures = 0;
std::array<int, 256> events{};
int eventCount = 0;
std::uint64_t nextToken = 0;
void Check(bool value, int line) { if (!value) { ++failures; std::printf("CallScope failure line %d\n", line); } }
#define CHECK(x) Check((x), __LINE__)
void Event(int value) noexcept { if (eventCount < static_cast<int>(events.size())) events[eventCount++] = value; }
std::uint64_t Enter(Object* o, Function* f) { CHECK(o == &testObject && f == &testFunction); auto n = ++nextToken; Event(static_cast<int>(n)); return n; }
void Leave(Object* o, Function* f, std::uint64_t token) noexcept { CHECK(o == &testObject && f == &testFunction); Event(-static_cast<int>(token)); }
void ThrowCpp() { throw std::runtime_error("synthetic"); }
void Run(int kind) {
    Scope::Invocation invocation;
    __try {
        CHECK(invocation.Begin({Enter, Leave}, &testObject, &testFunction));
        CHECK(invocation.Begin({Enter, Leave}, &testObject, &testFunction));
        Event(101); // existing observer
        Event(102); // existing skip/answer order
        if (kind == 1) return; // original skip/answer early return
        if (kind == 2) ThrowCpp();
        if (kind == 3) RaiseException(0xE0424242, 0, 0, nullptr);
        if (kind == 4) Run(1); // same object + same function, distinct nested invocation
        Event(103); // original call completed
        Event(104); // existing post handlers
        Event(105); // existing frame-post handlers
    } __finally { invocation.End(); }
}
void SehTest() {
    __try { Run(3); }
    __except (GetExceptionCode() == 0xE0424242 ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { Event(201); }
}
void Reset() { eventCount = 0; nextToken = 0; }
void Expect(std::initializer_list<int> expected) {
    CHECK(eventCount == static_cast<int>(expected.size()));
    int index = 0; for (int value : expected) CHECK(events[index++] == value);
}
}
int RunCallScopeTests() {
    failures = 0;
    Reset(); Run(0); Expect({1,2,101,102,103,104,105,-2,-1});
    Reset(); Run(1); Expect({1,2,101,102,-2,-1});
    Reset(); Run(4); Expect({1,2,101,102,3,4,101,102,-4,-3,103,104,105,-2,-1});
    Reset(); try { Run(2); } catch (std::runtime_error const&) { Event(202); }
    Expect({1,2,101,102,-2,-1,202});
    Reset(); SehTest(); Expect({1,2,101,102,-2,-1,201});
    Reset(); Scope::Invocation capacity;
    CHECK(!capacity.Begin({nullptr, Leave}, &testObject, &testFunction));
    CHECK(!capacity.Begin({Enter, nullptr}, &testObject, &testFunction));
    for (std::size_t i = 0; i < Scope::kCapacity; ++i) CHECK(capacity.Begin({Enter, Leave}, &testObject, &testFunction));
    CHECK(!capacity.Begin({Enter, Leave}, &testObject, &testFunction));
    capacity.End(); CHECK(capacity.count == 0 && nextToken == Scope::kCapacity);
    auto const ended = eventCount; capacity.End(); CHECK(eventCount == ended);
    std::printf("CallScope tests: %d failures\n", failures);
    return failures;
}
#ifdef MEVR_CALL_SCOPE_STANDALONE
int main() { return RunCallScopeTests(); }
#endif
