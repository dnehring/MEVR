// SPDX-License-Identifier: MIT
#include "core/qa/BoundedUtf16.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <vector>
#if defined(_DEBUG)
#include <crtdbg.h>
#endif

namespace { bool failAllocation{}; }
// Fail one ordinary host allocation; the engine reader and fixture storage do
// not allocate during the tested call. This tests std::string allocation failure.
void* operator new(std::size_t size) {
    if (failAllocation) { failAllocation = false; throw std::bad_alloc(); }
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* memory) noexcept { ::operator delete(memory); }
void operator delete[](void* memory, std::size_t) noexcept { ::operator delete(memory); }

namespace u = mevr::qa::utf16;
namespace {
unsigned checks{};
void Check(bool condition, char const* message) { ++checks; if (!condition) throw std::runtime_error(message); }
struct Memory {
    static constexpr std::uintptr_t headerAddress = 0x1003, bodyAddress = 0x2001;
    std::array<std::byte, 24> storage{};
    std::vector<char16_t> body;
    unsigned headerReads{}, bodyReads{};
    std::size_t bodyBytes{};
    bool unreadableHeader{}, unreadableBody{}, unreadableAgain{}, changeHeader{}, failOutputAllocation{};
    void Header(u::Header header) { std::memcpy(storage.data() + 3, &header, u::kHeaderBytes); }
    void Text(std::initializer_list<char16_t> text) {
        body.assign(text);
        Header({bodyAddress, static_cast<std::uint32_t>(body.size()), static_cast<std::uint32_t>(body.size())});
    }
    static bool Read(std::uintptr_t source, void* destination, std::size_t size, void* raw) noexcept {
        auto& m = *static_cast<Memory*>(raw);
        if (source == headerAddress && size == u::kHeaderBytes) {
            ++m.headerReads;
            if (m.unreadableHeader || (m.unreadableAgain && m.headerReads == 2)) return false;
            std::memcpy(destination, m.storage.data() + 3, size);
            if (m.changeHeader && m.headerReads == 2) static_cast<std::byte*>(destination)[8] ^= std::byte{1};
            // Result's debug-STL container proxy already exists. Fail the
            // output resize allocation, not MSVC's noexcept debug bookkeeping.
            if (m.failOutputAllocation && m.headerReads == 2) failAllocation = true;
            return true;
        }
        ++m.bodyReads; m.bodyBytes = size;
        if (source != bodyAddress || m.unreadableBody || size > m.body.size() * sizeof(char16_t)) return false;
        std::memcpy(destination, m.body.data(), size);
        return true;
    }
    u::Result Run(u::Limits limits = {}) { headerReads = bodyReads = 0; bodyBytes = 0; return u::ReadFString(headerAddress, Read, this, limits); }
};
void Is(u::Result const& result, u::Status status, u::Reason reason, char const* label) {
    Check(result.status == status && result.reason == reason && (!result.Available() ? result.utf8.empty() : true), label);
}
}
int main() {
#if defined(_DEBUG)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    try {
        Memory m;
        Is(m.Run(), u::Status::Empty, u::Reason::None, "null empty");
        Check(m.headerReads == 2 && m.bodyReads == 0, "empty snapshots header without body scan");
        m.Header({Memory::bodyAddress, 0, 100});
        Is(m.Run(), u::Status::Empty, u::Reason::None, "allocated count-zero SDK empty");
        Check(m.bodyReads == 0, "unused capacity never read");
        m.Text({0}); Is(m.Run(), u::Status::Empty, u::Reason::None, "allocated NUL-only empty");
        m.Text({u'A', 0xE9, 0xD83D, 0xDE00, 0});
        auto text = m.Run();
        Check(text.status == u::Status::Text && text.utf8 == "A\xC3\xA9\xF0\x9F\x98\x80", "unaligned header and body snapshot, BMP and surrogate UTF8");
        m.Text({u'e', 0x301, 0}); Check(m.Run().utf8 == "e\xCC\x81", "no Unicode normalization");
        m.Header({0, 1, 1}); Is(m.Run(), u::Status::Malformed, u::Reason::InconsistentHeader, "null pointer nonzero count");
        m.Header({0, 0, 1}); Is(m.Run(), u::Status::Malformed, u::Reason::InconsistentHeader, "null pointer nonzero capacity");
        m.Header({Memory::bodyAddress, 0, 0}); Is(m.Run(), u::Status::Malformed, u::Reason::InconsistentHeader, "nonnull pointer zero capacity");
        m.Header({Memory::bodyAddress, 2, 1}); Is(m.Run(), u::Status::Malformed, u::Reason::InconsistentHeader, "count exceeds capacity");
        m.Header({Memory::bodyAddress, UINT32_MAX, UINT32_MAX}); Is(m.Run(), u::Status::Unavailable, u::Reason::CodeUnitLimit, "huge unsigned count bounded");
        Check(m.bodyReads == 0, "limit checked before body allocation or read");
        m.Text({u'a', 0}); m.Header({Memory::bodyAddress, 2, 4097});
        Is(m.Run(), u::Status::Unavailable, u::Reason::CapacityLimit, "capacity capped");
        m.Header({Memory::bodyAddress, 2, 4000});
        Check(m.Run().utf8 == "a" && m.bodyBytes == 4, "copy count not capacity");
        m.Text({u'a', u'b'}); Is(m.Run(), u::Status::Malformed, u::Reason::MissingTerminator, "explicit final terminator required");
        m.Text({u'a', 0, u'b', 0}); Is(m.Run(), u::Status::Malformed, u::Reason::EmbeddedNul, "embedded NUL never truncates");
        for (auto invalid : {std::initializer_list<char16_t>{0xD800, 0}, {0xD800, u'a', 0}, {0xDC00, 0}, {0xD800, 0xD800, 0}}) {
            m.Text(invalid); Is(m.Run(), u::Status::Malformed, u::Reason::InvalidSurrogate, "invalid surrogate rejected");
        }
        m.Text({u'a', 0}); m.unreadableHeader = true;
        Is(m.Run(), u::Status::Unavailable, u::Reason::HeaderUnreadable, "unreadable header"); m.unreadableHeader = false;
        m.unreadableBody = true; Is(m.Run(), u::Status::Unavailable, u::Reason::BodyUnreadable, "unreadable body"); m.unreadableBody = false;
        m.unreadableAgain = true; Is(m.Run(), u::Status::Unavailable, u::Reason::HeaderUnreadable, "header recheck unreadable"); m.unreadableAgain = false;
        m.changeHeader = true; Is(m.Run(), u::Status::Unavailable, u::Reason::HeaderChanged, "changed header is unavailable not text"); m.changeHeader = false;
        m.Header({UINT64_MAX - 1, 2, 2}); Is(m.Run(), u::Status::Unavailable, u::Reason::AddressOverflow, "body span overflow");
        Is(u::ReadFString(UINTPTR_MAX - 1, Memory::Read, &m), u::Status::Unavailable, u::Reason::AddressOverflow, "header span overflow");
        Is(u::ReadFString(0, Memory::Read, &m), u::Status::Unavailable, u::Reason::NullHeader, "missing header distinct from empty string");
        Is(u::ReadFString(Memory::headerAddress, nullptr, &m), u::Status::Unavailable, u::Reason::MissingReader, "reader required");
        m.Text({u'a', 0});
        Is(m.Run({u::kHardCodeUnits + 1, 4096, 4096}), u::Status::Unavailable, u::Reason::InvalidLimits, "hard bound cannot be raised");
        Is(m.Run({1, 4096, 4096}), u::Status::Unavailable, u::Reason::CodeUnitLimit, "caller limit includes NUL");
        m.Text({0xE9, 0}); Is(m.Run({2, 2, 1}), u::Status::Unavailable, u::Reason::Utf8Limit, "UTF8 byte bound independent of UTF16 count");
        Check(m.Run({2, 2, 2}).utf8 == "\xC3\xA9", "exact limits accepted");
        for (auto fault : {u::testing::ConversionFault::SizeQuery, u::testing::ConversionFault::WriteZero, u::testing::ConversionFault::WriteShort}) {
            u::testing::SetConversionFault(fault);
            Is(m.Run(), u::Status::Unavailable, fault == u::testing::ConversionFault::SizeQuery ? u::Reason::ConversionSize : u::Reason::ConversionWrite,
                "strict two-pass conversion failure clears text");
        }
        u::testing::SetConversionFault(u::testing::ConversionFault::None);
        m.body.assign(100, u'x'); m.body.back() = 0; m.Header({Memory::bodyAddress, 100, 100});
        m.failOutputAllocation = true;
        auto failed = m.Run();
        bool const allocationIntercepted = !failAllocation; failAllocation = false;
        m.failOutputAllocation = false;
        Check(allocationIntercepted, "output host allocation was intercepted");
        Is(failed, u::Status::Unavailable, u::Reason::AllocationFailure, "allocation failure does not escape noexcept or return partial text");
        Check(m.Run().utf8.size() == 99, "conversion succeeds after independent failure");
        std::cout << checks << " bounded UTF16/header/conversion checks passed\n";
        return 0;
    } catch (std::exception const& error) { failAllocation = false; std::cerr << error.what() << '\n'; return 1; }
}
