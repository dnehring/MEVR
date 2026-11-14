// SPDX-License-Identifier: MIT
#include "core/qa/DiagnosticTickGate.hpp"

#include <cstdio>
#include <stdexcept>

using Gate = mevr::qa::DiagnosticTickGate;
namespace {
int failures = 0;
int calls = 0;
float delta = 0;
Gate* current = nullptr;
void Check(bool ok, char const* text) { if (!ok) { ++failures; std::printf("FAIL: %s\n", text); } }
void Count(float value) { ++calls; delta = value; }
void Reenter(float) {
    Check(current->Tick(7, 0) == Gate::Result::Reentrant, "nested tick rejected");
    Check(!current->Reset(), "reset while callback runs rejected");
    Check(!current->Register(&Count), "replacement while callback runs rejected");
}
void Throw(float) { throw std::runtime_error("synthetic consumer failure"); }
}

int main() {
    Gate gate;
    Check(!gate.Register(nullptr), "null registration rejected");
    Check(gate.Tick(0, 0) == Gate::Result::WrongThread, "missing thread rejected");
    Check(gate.Tick(7, 0) == Gate::Result::NoConsumer, "default is no consumer");
    Check(!gate.Register(&Count), "late registration rejected");
    Check(gate.Reset(), "idle reset succeeds");
    Check(gate.Register(&Count), "initial registration succeeds");
    Check(!gate.Register(&Count), "duplicate registration rejected");
    Check(gate.Tick(7, 0.25f) == Gate::Result::Called && calls == 1 && delta == 0.25f, "synchronous callback receives delta");
    Check(gate.Tick(8, 1) == Gate::Result::WrongThread && calls == 1, "other thread cannot dispatch");
    Check(gate.Tick(7, 0.5f) == Gate::Result::Called && calls == 2, "owning thread continues");
    Check(gate.Reset(), "reset clears epoch");
    current = &gate;
    Check(gate.Register(&Reenter), "new epoch callback registered");
    Check(gate.Tick(7, 0) == Gate::Result::Called, "reentrant callback returns without deadlock");
    Check(gate.Reset() && gate.Register(&Throw), "throwing consumer setup");
    try { gate.Tick(9, 0); Check(false, "exception propagates"); } catch (std::runtime_error const&) {}
    Check(gate.Reset(), "exception does not strand running flag");
    Check(gate.Tick(10, 0) == Gate::Result::NoConsumer, "reset leaves no old callback/thread");
    std::printf("LE2 diagnostic dispatch: %d failures\n", failures);
    return failures ? 1 : 0;
}
