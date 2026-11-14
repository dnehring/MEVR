// SPDX-License-Identifier: MIT
#include "engine/Le2DiagnosticHost.hpp"

#include "core/Log.hpp"
#include "core/qa/DiagnosticTickGate.hpp"

#include <Windows.h>

namespace mevr::engine::le2_diagnostic {
namespace {
qa::DiagnosticTickGate gGate;
}

bool SetTickCallback(TickCallback callback) {
    bool const ok = gGate.Register(callback);
    log::Info("LE2 diagnostic consumer registration: {}", ok ? "accepted" : "rejected");
    return ok;
}

void Tick(float deltaSeconds) {
    auto const result = gGate.Tick(GetCurrentThreadId(), deltaSeconds);
    if (result == qa::DiagnosticTickGate::Result::WrongThread || result == qa::DiagnosticTickGate::Result::Reentrant) {
        log::Error("LE2 diagnostic tick rejected: {}", result == qa::DiagnosticTickGate::Result::WrongThread ? "wrong thread" : "reentrant");
    }
}

void Shutdown() {
    if (!gGate.Reset()) log::Error("LE2 diagnostic shutdown rejected: callback still running");
}

} // namespace mevr::engine::le2_diagnostic
