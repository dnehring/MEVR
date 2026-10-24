// SPDX-License-Identifier: MIT
#pragma once

// Text into a fixed buffer, for code that must not allocate (the crash report, research/36: the heap may be
// what broke). Appends past the end are dropped and remembered; the text is always zero-terminated.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mevr::text {

class Appender {
public:
    Appender(char* buffer, std::size_t capacity) : buffer_(buffer), capacity_(capacity) {
        if (capacity_ > 0) {
            buffer_[0] = '\0';
        }
    }

    char const* Data() const { return buffer_; }
    std::size_t Size() const { return size_; }
    bool Truncated() const { return truncated_; }
    std::string_view View() const { return {buffer_, size_}; }
    void Clear() {
        size_ = 0;
        truncated_ = false;
        if (capacity_ > 0) {
            buffer_[0] = '\0';
        }
    }
    // Back to an earlier Size() (drops what came after).
    void Rewind(std::size_t size) {
        if (size < size_) {
            size_ = size;
            buffer_[size_] = '\0';
        }
    }

    Appender& Char(char c) {
        if (size_ + 1 < capacity_) {
            buffer_[size_++] = c;
            buffer_[size_] = '\0';
        } else {
            truncated_ = true;
        }
        return *this;
    }
    Appender& Str(std::string_view s) {
        for (char c : s) {
            Char(c);
        }
        return *this;
    }
    Appender& Str(char const* s) { return s != nullptr ? Str(std::string_view(s)) : *this; }
    Appender& Repeat(char c, std::size_t count) {
        for (std::size_t i = 0; i < count; ++i) {
            Char(c);
        }
        return *this;
    }
    // Pads with spaces up to `column` characters since `start` (a Size() taken before the field).
    Appender& PadTo(std::size_t start, std::size_t column) {
        std::size_t const written = size_ - start;
        return written < column ? Repeat(' ', column - written) : *this;
    }

    Appender& Dec(std::int64_t value) {
        if (value < 0) {
            Char('-');
            return UDec(static_cast<std::uint64_t>(-(value + 1)) + 1);
        }
        return UDec(static_cast<std::uint64_t>(value));
    }
    Appender& UDec(std::uint64_t value) {
        char digits[20];
        int n = 0;
        do {
            digits[n++] = static_cast<char>('0' + value % 10);
            value /= 10;
        } while (value != 0);
        while (n > 0) {
            Char(digits[--n]);
        }
        return *this;
    }
    // "0x" and at least `minDigits` upper-case hex digits.
    Appender& Hex(std::uint64_t value, int minDigits = 1) {
        Str("0x");
        char digits[16];
        int n = 0;
        do {
            digits[n++] = "0123456789ABCDEF"[value & 0xF];
            value >>= 4;
        } while (value != 0);
        for (int i = n; i < minDigits && i < 16; ++i) {
            Char('0');
        }
        while (n > 0) {
            Char(digits[--n]);
        }
        return *this;
    }
    Appender& Ptr(void const* p) { return Hex(reinterpret_cast<std::uintptr_t>(p), 12); }
    // A signed count of thousandths as "-12.345" (milliseconds from microseconds...).
    Appender& Milli(std::int64_t thousandths) {
        bool const negative = thousandths < 0;
        std::uint64_t const magnitude =
            negative ? static_cast<std::uint64_t>(-(thousandths + 1)) + 1 : static_cast<std::uint64_t>(thousandths);
        if (negative) {
            Char('-');
        }
        UDec(magnitude / 1000);
        Char('.');
        std::uint64_t const frac = magnitude % 1000;
        Char(static_cast<char>('0' + frac / 100));
        Char(static_cast<char>('0' + frac / 10 % 10));
        Char(static_cast<char>('0' + frac % 10));
        return *this;
    }
    // A UTF-16 string as ASCII ('?' for anything else), up to `max` units or its terminator.
    Appender& Wide(wchar_t const* s, std::size_t max = 260) {
        for (std::size_t i = 0; s != nullptr && i < max && s[i] != 0; ++i) {
            Char(s[i] < 0x80 ? static_cast<char>(s[i]) : '?');
        }
        return *this;
    }

private:
    char* buffer_;
    std::size_t capacity_;
    std::size_t size_ = 0;
    bool truncated_ = false;
};

} // namespace mevr::text
