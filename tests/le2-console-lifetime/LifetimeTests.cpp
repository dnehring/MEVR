// SPDX-License-Identifier: MIT
// Offline lifetime checks: fake strings/dispatch only, no SDK or native engine.
#include "engine/ScopedUnionMember.hpp"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace {
struct Failure {};
struct Counts {
    int attempted{}, constructed{}, alive{}, resources{}, dispatched{}, malformedDestruction{};
    int throwInConstructor{};
    std::array<int, 2> destructionOrder{};
    int destructions{};
} counts;
void Check(bool condition, char const* reason) { if (!condition) throw std::runtime_error(reason); }

struct Resource {
    explicit Resource(char const* text) : value(text) { ++counts.resources; }
    ~Resource() { --counts.resources; }
    std::string value;
};
struct FakeString {
    explicit FakeString(char const* text = "") : id(++counts.attempted), resource(std::make_unique<Resource>(text)) {
        // Deliberately fail after a member has allocated: the C++ constructor
        // must unwind that member without destroying the unconstructed object.
        if (counts.throwInConstructor == id) throw Failure{};
        ++counts.constructed;
        ++counts.alive;
    }
    ~FakeString() noexcept {
        if (!resource || counts.alive <= 0 || counts.destructions >= 2) ++counts.malformedDestruction;
        else counts.destructionOrder[counts.destructions++] = id;
        --counts.alive;
    }
    FakeString(FakeString const&) = delete;
    FakeString& operator=(FakeString const&) = delete;
    int id;
    std::unique_ptr<Resource> resource;
};
#if defined(_MSC_VER) && defined(_M_X64)
#pragma pack(push, 4)
#endif
struct FakeParms {
    union { FakeString Command; };
    unsigned bWriteToLog{};
    union { FakeString ReturnValue; };
    FakeParms() {} // As in the SDK, neither union's nontrivial member is constructed.
    ~FakeParms() {} // The enclosing block deliberately performs no union cleanup.
};
#if defined(_MSC_VER) && defined(_M_X64)
#pragma pack(pop)
static_assert(sizeof(FakeString) == 16 && offsetof(FakeParms, ReturnValue) == 0x14 && sizeof(FakeParms) == 0x24);
#endif
using Guard = mevr::engine::detail::ScopedUnionMember<FakeString>;
static_assert(!std::is_copy_constructible_v<Guard> && !std::is_move_constructible_v<Guard>);
static_assert(std::is_nothrow_destructible_v<Guard>);

enum class Path { Normal, DispatchThrows, OutputThrows };
std::string Run(Path path) {
    FakeParms parms;
    Guard command(&parms.Command, std::string("copied command").c_str());
    Guard returned(&parms.ReturnValue);
    parms.bWriteToLog = 0;
    Check(counts.alive == 2, "both union members constructed before dispatch");
    Check(parms.Command.resource->value == "copied command", "temporary input copied before destruction");
    Check(parms.ReturnValue.resource->value.empty(), "return member explicitly starts empty");
    ++counts.dispatched;
    if (path == Path::DispatchThrows) throw Failure{};
    parms.ReturnValue.resource->value = "observed output";
    if (path == Path::OutputThrows) throw Failure{};
    return parms.ReturnValue.resource->value;
}
void Clean() {
    Check(counts.alive == 0 && counts.resources == 0, "no leaked object or resource");
    Check(counts.malformedDestruction == 0, "no unconstructed or double destruction");
}
void Exercise() {
    counts = {};
    Check(Run(Path::Normal) == "observed output", "output survives parameter cleanup");
    Check(counts.constructed == 2 && counts.destructions == 2 && counts.dispatched == 1, "normal exact counts");
    Check(counts.destructionOrder == std::array<int, 2>{2, 1}, "normal cleanup reverse construction order");
    Clean();
    for (auto path : {Path::DispatchThrows, Path::OutputThrows}) {
        counts = {};
        bool threw = false;
        try { (void)Run(path); } catch (Failure const&) { threw = true; }
        Check(threw, "original C++ exception propagates");
        Check(counts.constructed == 2 && counts.destructions == 2 && counts.dispatched == 1, "exception exact counts");
        Check(counts.destructionOrder == std::array<int, 2>{2, 1}, "exception cleanup reverse order");
        Clean();
    }
    for (int failsAt : {1, 2}) {
        counts = {};
        counts.throwInConstructor = failsAt;
        bool threw = false;
        try { (void)Run(Path::Normal); } catch (Failure const&) { threw = true; }
        Check(threw && counts.dispatched == 0, "partial construction cannot dispatch");
        Check(counts.attempted == failsAt && counts.constructed == failsAt - 1 && counts.destructions == failsAt - 1,
              "only successfully constructed members destroyed");
        if (failsAt == 2) Check(counts.destructionOrder[0] == 1, "first member cleaned after second construction fails");
        Clean();
    }
}
} // namespace
int main() {
    try {
        Exercise();
        std::cout << "Normal, dispatch/output exception, and partial union construction checks passed\n";
        return 0;
    } catch (std::exception const& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
