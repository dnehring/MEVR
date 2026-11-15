// SPDX-License-Identifier: MIT
#pragma once
#include "ue3/Reflect.hpp"

namespace mevr::ue3 {
// ProcessEvent with a class UFunction does not establish which state override UnrealScript
// would select. Callers refuse any same-named state method in the hierarchy.
// This deliberately rejects some safe states instead of interpreting opaque StateFrame memory.
inline Function* UnambiguousMethod(Reflection& r, Object* object, std::string_view name) {
    if (!r.IsAlive(object)) return nullptr;
    auto* stateClass = r.FindClass("Core.State");
    auto* fieldClass = r.FindClass("Core.Field");
    auto* functionClass = r.FunctionClass();
    if (!stateClass || !fieldClass || !functionClass) return nullptr;
    Function* selected = nullptr;
    unsigned budget = 0;
    auto inspectState = [&](Struct* state) {
        unsigned depth = 0;
        for (; state && depth < 64; state = r.SuperOf(state), ++depth) {
            if (!r.IsAlive(state) || !r.IsA(state, stateClass)) return false;
            for (auto* f = r.ChildrenOf(state); f; f = r.NextOf(f)) {
                if (++budget > 8192 || !r.IsAlive(f) || !r.IsA(f, fieldClass) || r.OuterOf(f) != state)
                    return false;
                if (r.NameIs(f, name) || r.IsA(f, stateClass)) return false;
            }
        }
        return state == nullptr;
    };
    auto* cls = static_cast<Struct*>(r.ClassOf(object));
    unsigned depth = 0;
    for (; cls && depth < 64; cls = r.SuperOf(cls), ++depth) {
        if (!r.IsAlive(cls) || !r.IsA(cls, r.ClassClass())) return nullptr;
        Function* local = nullptr;
        for (auto* f = r.ChildrenOf(cls); f; f = r.NextOf(f)) {
            if (++budget > 8192 || !r.IsAlive(f) || !r.IsA(f, fieldClass) || r.OuterOf(f) != cls)
                return nullptr;
            if (r.IsA(f, stateClass) && !inspectState(static_cast<Struct*>(f))) return nullptr;
            if (!r.NameIs(f, name)) continue;
            if (!r.IsA(f, functionClass) || local) return nullptr;
            local = static_cast<Function*>(f);
        }
        if (!selected) selected = local;
    }
    return cls == nullptr ? selected : nullptr;
}
} // namespace mevr::ue3
