// SPDX-License-Identifier: MIT
#pragma once

#include <memory>
#include <type_traits>
#include <utility>

namespace mevr::engine::detail {
// Construct one otherwise inactive union member and own its ordinary C++
// lifetime. Keep the enclosing storage alive until this guard is destroyed.
// Failed construction never calls T's destructor; previously created guards
// still unwind normally. This provides no SEH/native-fault ownership recovery.
template<class T>
class ScopedUnionMember final {
    static_assert(std::is_nothrow_destructible_v<T>);
public:
#if defined(_MSC_VER) && defined(_M_X64)
    // SDK parameter blocks use pack(4): FString at offset 0x14 is not
    // naturally aligned. Preserve the Microsoft qualifier through the
    // construct_at/destroy_at template calls, including stored ownership.
    using StoragePointer = T __unaligned*;
#else
    // Standard C++ fallback requires storage aligned for T. It does not
    // promise support for the SDK's Microsoft packed-member ABI.
    using StoragePointer = T*;
#endif
    template<class... Args>
    explicit ScopedUnionMember(StoragePointer storage, Args&&... args)
        : object_(std::construct_at(storage, std::forward<Args>(args)...)) {}
    ~ScopedUnionMember() noexcept { std::destroy_at(object_); }
    ScopedUnionMember(ScopedUnionMember const&) = delete;
    ScopedUnionMember& operator=(ScopedUnionMember const&) = delete;
    ScopedUnionMember(ScopedUnionMember&&) = delete;
    ScopedUnionMember& operator=(ScopedUnionMember&&) = delete;
private:
    StoragePointer object_;
};
} // namespace mevr::engine::detail
