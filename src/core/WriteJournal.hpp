// SPDX-License-Identifier: MIT
#pragma once

// The mod's write journal (research/36): each place the mod writes engine memory says so right after the
// write, with a tag naming the site. The host keeps the last 128 (HostServices v18, plugin/CrashBreadcrumbs)
// and writes them out if the process crashes. Not connected (an older host, the tests): nothing happens.
//
//   *result = rotation;
//   write_journal::Wrote("HandAim.aim(CF)", result);
//
// Tags must be string literals (static storage): the host copies their text and compares their pointers.

#include "core/Breadcrumbs.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mevr::write_journal {

using RecordFn = void (*)(char const* tag, void const* address, std::uint32_t size, std::uint64_t value);

inline std::atomic<RecordFn> gRecord{nullptr};

inline void Connect(RecordFn record) {
    gRecord.store(record, std::memory_order_relaxed);
}

// After writing `size` bytes at `address` (readable: it was just written).
inline void Wrote(char const* tag, void const* address, std::size_t size) {
    if (RecordFn const record = gRecord.load(std::memory_order_relaxed)) {
        record(tag, address, static_cast<std::uint32_t>(size), breadcrumbs::FirstBytes(address, size));
    }
}

template <class T>
inline void Wrote(char const* tag, T const* written) {
    Wrote(tag, written, sizeof(T));
}

} // namespace mevr::write_journal
