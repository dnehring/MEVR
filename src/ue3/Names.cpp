#include "ue3/Names.hpp"

#include <charconv>
#include <cstring>

namespace mevr::ue3::names {
namespace {

// Longest name text we read: the entry header has room for a 9-bit length; anything longer means
// we're not looking at a name entry.
constexpr std::size_t kMaxChars = 1024;

void AppendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}

bool IsWide(Layout const& layout, void const* entry) {
    std::uint32_t header = 0;
    std::memcpy(&header, entry, sizeof(header));
    return (header & layout.nameWideFlag) != 0;
}

// Splits "Foo_2" into base "Foo" and stored number 3; a name without a numeric suffix is number 0.
// UE3 only numbers a suffix without a leading zero ("Foo_02" is a plain name).
void SplitNumber(std::string_view text, std::string_view& base, std::int32_t& number) {
    base = text;
    number = 0;
    std::size_t const underscore = text.rfind('_');
    if (underscore == std::string_view::npos || underscore + 1 >= text.size()) {
        return;
    }
    std::string_view const digits = text.substr(underscore + 1);
    if (digits.size() > 1 && digits[0] == '0') {
        return;
    }
    for (char c : digits) {
        if (c < '0' || c > '9') {
            return;
        }
    }
    int value = 0;
    auto const [ptr, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
    if (ec != std::errc() || ptr != digits.data() + digits.size() || value >= 0x7FFFFFFF) {
        return;
    }
    base = text.substr(0, underscore);
    number = value + 1;
}

} // namespace

char Fold(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool EqualsNoCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (Fold(a[i]) != Fold(b[i])) {
            return false;
        }
    }
    return true;
}

void const* Entry(Layout const& layout, Pools const& pools, std::int32_t index) {
    if (index == 0 || pools.bases == nullptr) {
        return nullptr;
    }
    std::uint32_t const raw = static_cast<std::uint32_t>(index);
    std::uint32_t const pool = (raw >> layout.namePoolShift) & layout.namePoolMask;
    if (pool >= pools.count || pools.bases[pool] == 0) {
        return nullptr;
    }
    return reinterpret_cast<void const*>(pools.bases[pool] + (raw & layout.nameOffsetMask));
}

std::string BaseText(Layout const& layout, Pools const& pools, std::int32_t index) {
    if (index == 0) {
        return "None";
    }
    void const* const entry = Entry(layout, pools, index);
    if (entry == nullptr) {
        return {};
    }
    std::string out;
    auto const* chars = static_cast<std::uint8_t const*>(entry) + layout.nameChars;
    if (IsWide(layout, entry)) {
        for (std::size_t i = 0; i < kMaxChars; ++i) {
            char16_t c = 0;
            std::memcpy(&c, chars + i * 2, sizeof(c));
            if (c == 0) {
                break;
            }
            AppendUtf8(out, c);
        }
    } else {
        for (std::size_t i = 0; i < kMaxChars && chars[i] != 0; ++i) {
            AppendUtf8(out, chars[i]);  // Latin-1
        }
    }
    return out;
}

std::string ToString(Layout const& layout, Pools const& pools, Name name) {
    std::string text = BaseText(layout, pools, name.index);
    if (name.number != 0) {
        text += '_';
        text += std::to_string(name.number - 1);
    }
    return text;
}

bool BaseEquals(Layout const& layout, Pools const& pools, std::int32_t index, std::string_view text) {
    if (index == 0) {
        return EqualsNoCase(text, "None");
    }
    void const* const entry = Entry(layout, pools, index);
    if (entry == nullptr) {
        return false;
    }
    auto const* chars = static_cast<std::uint8_t const*>(entry) + layout.nameChars;
    bool const wide = IsWide(layout, entry);
    for (std::size_t i = 0; i <= text.size(); ++i) {
        std::uint32_t c = 0;
        if (wide) {
            char16_t w = 0;
            std::memcpy(&w, chars + i * 2, sizeof(w));
            c = w;
        } else {
            c = chars[i];
        }
        if (i == text.size()) {
            return c == 0;
        }
        if (c == 0 || c >= 0x80 || Fold(static_cast<char>(c)) != Fold(text[i])) {
            return false;
        }
    }
    return false;
}

bool Equals(Layout const& layout, Pools const& pools, Name name, std::string_view text) {
    if (name.number == 0) {
        return BaseEquals(layout, pools, name.index, text);
    }
    std::string_view base;
    std::int32_t number = 0;
    SplitNumber(text, base, number);
    return number == name.number && BaseEquals(layout, pools, name.index, base);
}

} // namespace mevr::ue3::names
