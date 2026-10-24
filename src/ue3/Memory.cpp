// SPDX-License-Identifier: MIT
#include "ue3/Memory.hpp"

#include <cstring>
#include <limits>
#include <utility>

namespace mevr::ue3::memory {
namespace {

Allocator gAllocator;

void AppendUtf8(std::string& out, std::uint32_t c) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

} // namespace

void Use(Allocator allocator) {
    gAllocator = allocator;
}

bool Ready() {
    return gAllocator.malloc != nullptr && gAllocator.free != nullptr;
}

void* Malloc(std::size_t bytes) {
    if (!Ready() || bytes == 0 || bytes > std::numeric_limits<std::uint32_t>::max()) {
        return nullptr;
    }
    return gAllocator.malloc(static_cast<std::uint32_t>(bytes), kAlignment, 0);
}

void Free(void* pointer) {
    if (pointer != nullptr && Ready()) {
        gAllocator.free(pointer, 0);
    }
}

OwnedString::OwnedString(std::wstring_view text) {
    if (text.empty()) {
        return;
    }
    std::size_t const count = text.size() + 1;
    if (count > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max() / 2)) {
        valid_ = false;
        return;
    }
    auto* const data = static_cast<wchar_t*>(Malloc(count * sizeof(wchar_t)));
    if (data == nullptr) {
        valid_ = false;
        return;
    }
    std::memcpy(data, text.data(), text.size() * sizeof(wchar_t));
    data[text.size()] = L'\0';
    string_.data = data;
    string_.count = static_cast<std::int32_t>(count);
    string_.max = static_cast<std::int32_t>(count);
}

OwnedString::~OwnedString() {
    Release();
}

OwnedString::OwnedString(OwnedString&& other) noexcept : string_(other.string_), valid_(other.valid_) {
    other.string_ = {};
    other.valid_ = true;
}

OwnedString& OwnedString::operator=(OwnedString&& other) noexcept {
    if (this != &other) {
        Release();
        string_ = std::exchange(other.string_, {});
        valid_ = std::exchange(other.valid_, true);
    }
    return *this;
}

void OwnedString::Release() {
    Free(string_.data);
    string_ = {};
}

void FreeString(FString& string) {
    Free(string.data);
    string = {};
}

std::wstring Widen(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    for (std::size_t i = 0; i < utf8.size();) {
        auto const b = static_cast<unsigned char>(utf8[i]);
        std::uint32_t c = 0xFFFD;
        std::size_t n = 1;
        if (b < 0x80) {
            c = b;
        } else if ((b & 0xE0) == 0xC0) {
            n = 2;
            c = b & 0x1F;
        } else if ((b & 0xF0) == 0xE0) {
            n = 3;
            c = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            n = 4;
            c = b & 0x07;
        }
        if (n > 1) {
            bool ok = i + n <= utf8.size();
            for (std::size_t k = 1; ok && k < n; ++k) {
                auto const next = static_cast<unsigned char>(utf8[i + k]);
                ok = (next & 0xC0) == 0x80;
                c = (c << 6) | (next & 0x3F);
            }
            if (!ok) {
                c = 0xFFFD;
                n = 1;
            }
        }
        i += n;
        if (c >= 0x10000) {
            c -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (c >> 10));
            out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
        } else {
            out += static_cast<wchar_t>(c);
        }
    }
    return out;
}

std::string Narrow(FString const& string) {
    std::string out;
    wchar_t const* const chars = string.Chars();
    for (std::size_t i = 0; chars[i] != L'\0' && (string.count <= 0 || i < static_cast<std::size_t>(string.count)); ++i) {
        std::uint32_t c = static_cast<std::uint16_t>(chars[i]);
        if (c >= 0xD800 && c < 0xDC00) {
            std::uint32_t const low = static_cast<std::uint16_t>(chars[i + 1]);
            if (low >= 0xDC00 && low < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            } else {
                c = 0xFFFD;
            }
        } else if (c >= 0xDC00 && c < 0xE000) {
            c = 0xFFFD;
        }
        AppendUtf8(out, c);
    }
    return out;
}

} // namespace mevr::ue3::memory
