// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace mevr::qa::utf16 {
// Pinned Win64 FString/TArray wire shape, not an SDK object or owning pointer.
// Count includes the final UTF-16 NUL when nonzero; capacity is code units.
struct Header { std::uint64_t data{}; std::uint32_t count{}, capacity{}; };
inline constexpr std::size_t kHeaderBytes = 16;
inline constexpr std::uint32_t kHardCodeUnits = 4096;
inline constexpr std::uint32_t kHardCapacity = 65536;
inline constexpr std::uint32_t kHardUtf8Bytes = 16384;
struct Limits {
    std::uint32_t maxCodeUnits = 1024;
    std::uint32_t maxCapacity = 4096;
    std::uint32_t maxUtf8Bytes = 4096;
};
enum class Status { Text, Empty, Unavailable, Malformed };
enum class Reason {
    None, InvalidLimits, MissingReader, NullHeader, AddressOverflow, HeaderUnreadable,
    HeaderChanged, BodyUnreadable, InconsistentHeader, CodeUnitLimit, CapacityLimit,
    MissingTerminator, EmbeddedNul, InvalidSurrogate, Utf8Limit, ConversionSize,
    ConversionWrite, AllocationFailure, InternalFailure
};
struct Result {
    Status status = Status::Unavailable;
    Reason reason = Reason::None;
    Header header{};
    std::string utf8;
    bool Available() const noexcept { return status == Status::Text || status == Status::Empty; }
};

// Copy exactly bytes into caller-owned memory, or return false. The application
// supplies its guarded read boundary. Never throw, retain pointers, or write the
// source. The helper itself never dereferences/frees an engine pointer.
using Reader = bool (*)(std::uintptr_t source, void* destination, std::size_t bytes, void* context) noexcept;

// Reads/copies an unaligned 16-byte header, bounded body, then the header again.
// Rejects embedded NUL and malformed UTF-16 rather than repairing/truncating it.
// No normalization; UTF-8 is deterministic for the copied scalar sequence.
// Header stability is a sample check, NOT lifetime or atomic-body proof.
Result ReadFString(std::uintptr_t headerAddress, Reader reader, void* context, Limits limits = {}) noexcept;
char const* Name(Status) noexcept;
char const* Name(Reason) noexcept;

#if defined(MEVR_BOUNDED_UTF16_TEST)
namespace testing {
enum class ConversionFault { None, SizeQuery, WriteZero, WriteShort };
void SetConversionFault(ConversionFault) noexcept;
}
#endif
} // namespace mevr::qa::utf16
