// SPDX-License-Identifier: MIT
#include "core/qa/BoundedUtf16.hpp"

#include <Windows.h>
#include <array>
#include <bit>
#include <cstring>
#include <limits>
#include <new>

namespace mevr::qa::utf16 {
namespace {
static_assert(sizeof(void*) == 8 && sizeof(wchar_t) == 2 && std::endian::native == std::endian::little);
static_assert(sizeof(Header) == kHeaderBytes && offsetof(Header, count) == 8 && offsetof(Header, capacity) == 12);
#if defined(MEVR_BOUNDED_UTF16_TEST)
testing::ConversionFault gFault{};
#endif
int Convert(wchar_t const* source, int units, char* target, int bytes) noexcept {
#if defined(MEVR_BOUNDED_UTF16_TEST)
    if (!target && gFault == testing::ConversionFault::SizeQuery) return 0;
    if (target && gFault == testing::ConversionFault::WriteZero) return 0;
    if (target && gFault == testing::ConversionFault::WriteShort) return bytes - 1;
#endif
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, source, units, target, bytes, nullptr, nullptr);
}
bool Fits(std::uintptr_t address, std::size_t bytes) noexcept {
    return address != 0 && bytes <= std::numeric_limits<std::uintptr_t>::max() - address;
}
}

Result ReadFString(std::uintptr_t address, Reader read, void* context, Limits limits) noexcept {
    Result out;
    auto fail = [&](Status status, Reason reason) noexcept {
        out.status = status; out.reason = reason; out.utf8.clear();
    };
    try {
        if (!limits.maxCodeUnits || limits.maxCodeUnits > kHardCodeUnits || !limits.maxCapacity ||
            limits.maxCapacity > kHardCapacity || !limits.maxUtf8Bytes || limits.maxUtf8Bytes > kHardUtf8Bytes) {
            fail(Status::Unavailable, Reason::InvalidLimits); return out;
        }
        if (!read) { fail(Status::Unavailable, Reason::MissingReader); return out; }
        if (!address) { fail(Status::Unavailable, Reason::NullHeader); return out; }
        if (!Fits(address, kHeaderBytes)) { fail(Status::Unavailable, Reason::AddressOverflow); return out; }
        std::array<std::byte, kHeaderBytes> original{}, again{};
        if (!read(address, original.data(), original.size(), context)) {
            fail(Status::Unavailable, Reason::HeaderUnreadable); return out;
        }
        // No typed load/reference at the possibly four-byte-aligned SDK member.
        std::memcpy(&out.header.data, original.data(), 8);
        std::memcpy(&out.header.count, original.data() + 8, 4);
        std::memcpy(&out.header.capacity, original.data() + 12, 4);
        auto const& h = out.header;
        if (h.count > h.capacity || (!h.data && (h.count || h.capacity)) || (h.data && !h.capacity)) {
            fail(Status::Malformed, Reason::InconsistentHeader); return out;
        }
        if (h.count > limits.maxCodeUnits) { fail(Status::Unavailable, Reason::CodeUnitLimit); return out; }
        if (h.capacity > limits.maxCapacity) { fail(Status::Unavailable, Reason::CapacityLimit); return out; }
        std::array<wchar_t, kHardCodeUnits> body{};
        if (h.count) {
            auto const bytes = static_cast<std::size_t>(h.count) * sizeof(wchar_t);
            if (!Fits(static_cast<std::uintptr_t>(h.data), bytes)) { fail(Status::Unavailable, Reason::AddressOverflow); return out; }
            if (!read(static_cast<std::uintptr_t>(h.data), body.data(), bytes, context)) {
                fail(Status::Unavailable, Reason::BodyUnreadable); return out;
            }
        }
        if (!read(address, again.data(), again.size(), context)) {
            fail(Status::Unavailable, Reason::HeaderUnreadable); return out;
        }
        if (original != again) { fail(Status::Unavailable, Reason::HeaderChanged); return out; }
        // SDK Clear/Reserve can leave allocated storage with count zero. No
        // declared body exists in that case; do not read capacity or guess NUL.
        if (!h.count) { out.status = Status::Empty; return out; }
        if (body[h.count - 1] != 0) { fail(Status::Malformed, Reason::MissingTerminator); return out; }
        auto const units = h.count - 1;
        for (std::uint32_t i = 0; i < units; ++i) {
            auto const unit = static_cast<std::uint16_t>(body[i]);
            if (!unit) { fail(Status::Malformed, Reason::EmbeddedNul); return out; }
            if (unit >= 0xD800 && unit <= 0xDBFF) {
                if (++i == units || body[i] < 0xDC00 || body[i] > 0xDFFF) {
                    fail(Status::Malformed, Reason::InvalidSurrogate); return out;
                }
            } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
                fail(Status::Malformed, Reason::InvalidSurrogate); return out;
            }
        }
        if (!units) { out.status = Status::Empty; return out; }
        int const bytes = Convert(body.data(), static_cast<int>(units), nullptr, 0);
        if (bytes <= 0) { fail(Status::Unavailable, Reason::ConversionSize); return out; }
        if (static_cast<std::uint32_t>(bytes) > limits.maxUtf8Bytes) { fail(Status::Unavailable, Reason::Utf8Limit); return out; }
        out.utf8.resize(static_cast<std::size_t>(bytes));
        if (Convert(body.data(), static_cast<int>(units), out.utf8.data(), bytes) != bytes) {
            fail(Status::Unavailable, Reason::ConversionWrite); return out;
        }
        out.status = Status::Text;
    } catch (std::bad_alloc const&) { fail(Status::Unavailable, Reason::AllocationFailure); }
    catch (...) { fail(Status::Unavailable, Reason::InternalFailure); }
    return out;
}

char const* Name(Status status) noexcept {
    switch (status) {
    case Status::Text: return "text";
    case Status::Empty: return "empty";
    case Status::Unavailable: return "unavailable";
    case Status::Malformed: return "malformed";
    }
    return "unknown";
}
char const* Name(Reason reason) noexcept {
    switch (reason) {
    case Reason::None: return "none";
    case Reason::InvalidLimits: return "invalid-limits";
    case Reason::MissingReader: return "missing-reader";
    case Reason::NullHeader: return "null-header";
    case Reason::AddressOverflow: return "address-overflow";
    case Reason::HeaderUnreadable: return "header-unreadable";
    case Reason::HeaderChanged: return "header-changed-during-read";
    case Reason::BodyUnreadable: return "body-unreadable";
    case Reason::InconsistentHeader: return "inconsistent-header";
    case Reason::CodeUnitLimit: return "code-unit-limit";
    case Reason::CapacityLimit: return "capacity-limit";
    case Reason::MissingTerminator: return "missing-final-terminator";
    case Reason::EmbeddedNul: return "embedded-nul";
    case Reason::InvalidSurrogate: return "invalid-utf16-surrogate";
    case Reason::Utf8Limit: return "utf8-byte-limit";
    case Reason::ConversionSize: return "utf8-size-query-failed";
    case Reason::ConversionWrite: return "utf8-conversion-count-mismatch";
    case Reason::AllocationFailure: return "host-allocation-failed";
    case Reason::InternalFailure: return "internal-failure";
    }
    return "unknown";
}
#if defined(MEVR_BOUNDED_UTF16_TEST)
void testing::SetConversionFault(ConversionFault fault) noexcept { gFault = fault; }
#endif
} // namespace mevr::qa::utf16
