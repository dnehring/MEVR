// SPDX-License-Identifier: MIT
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// A bounded, trivially destructible batch for native __try/__finally wrappers. Registrations are
// immutable while hooked. The wrapper must Leave on every exit; no engine layout or allocation here.
namespace mevr {
template <class Object, class Function>
struct CallScope {
    using Enter = std::uint64_t (*)(Object*, Function*);
    using Leave = void (*)(Object*, Function*, std::uint64_t) noexcept;
    struct Registration { Enter enter = nullptr; Leave leave = nullptr; };
    static constexpr std::size_t kCapacity = 16;
    struct Invocation {
        struct Active { Leave leave; Object* object; Function* function; std::uint64_t token; };
        std::array<Active, kCapacity> active{};
        std::size_t count = 0;
        bool Begin(Registration const& registration, Object* object, Function* function) {
            if (count == kCapacity || !registration.enter || !registration.leave) return false;
            auto const token = registration.enter(object, function);
            active[count++] = {registration.leave, object, function, token};
            return true;
        }
        void End() noexcept {
            while (count) {
                auto const item = active[--count];
                item.leave(item.object, item.function, item.token);
            }
        }
    };
};
} // namespace mevr
