// SPDX-License-Identifier: MIT
#include "core/Ini.hpp"

#include <charconv>
#include <cstdint>
#include <cmath>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mevr::ini {
namespace {

char LowerChar(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Calls `line(text)` for each line of `text` ('\n', "\r\n" or NUL separated).
template <typename F>
void ForEachLine(std::string_view text, F&& line) {
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find_first_of(std::string_view("\n\0", 2), start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        line(text.substr(start, end - start));
        start = end + 1;
    }
}

// A "key = value" line into `section` (first occurrence wins).
void AddKey(Section& section, std::string_view line) {
    std::string const text = Trim(line);
    if (text.empty() || text[0] == ';' || text[0] == '#') {
        return;
    }
    std::size_t const equals = text.find('=');
    if (equals == std::string::npos) {
        return;
    }
    std::string key = Lower(Trim(std::string_view(text).substr(0, equals)));
    if (key.empty()) {
        return;
    }
    section.emplace(std::move(key), Trim(std::string_view(text).substr(equals + 1)));
}

std::string_view StripUtf8Bom(std::string_view text) noexcept {
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.remove_prefix(3);
    }
    return text;
}

} // namespace

std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = LowerChar(c);
    }
    return out;
}

std::string Trim(std::string_view text) {
    std::size_t const first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    std::size_t const last = text.find_last_not_of(" \t\r");
    return std::string(text.substr(first, last - first + 1));
}

bool CaseLess::operator()(std::string_view a, std::string_view b) const noexcept {
    std::size_t const n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        char const x = LowerChar(a[i]);
        char const y = LowerChar(b[i]);
        if (x != y) {
            return static_cast<unsigned char>(x) < static_cast<unsigned char>(y);
        }
    }
    return a.size() < b.size();
}

Encoding Detect(std::string_view bytes) noexcept {
    auto const at = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
    if (bytes.size() >= 2 && at(0) == 0xFF && at(1) == 0xFE) {
        return Encoding::Utf16Le;
    }
    if (bytes.size() >= 2 && at(0) == 0xFE && at(1) == 0xFF) {
        return Encoding::Utf16Be;
    }
    if (bytes.size() >= 3 && at(0) == 0xEF && at(1) == 0xBB && at(2) == 0xBF) {
        return Encoding::Utf8Bom;
    }
    // No BOM: ASCII text in UTF-16 has a NUL in every other byte.
    if (bytes.size() >= 4) {
        if (at(0) != 0 && at(1) == 0 && at(2) != 0 && at(3) == 0) {
            return Encoding::Utf16Le;
        }
        if (at(0) == 0 && at(1) != 0 && at(2) == 0 && at(3) != 0) {
            return Encoding::Utf16Be;
        }
    }
    return Encoding::Narrow;
}

bool IsUtf16(Encoding e) noexcept {
    return e == Encoding::Utf16Le || e == Encoding::Utf16Be;
}

std::string Decode(std::string_view bytes, Encoding encoding) {
    if (!IsUtf16(encoding)) {
        return std::string(StripUtf8Bom(bytes));
    }
    bool const little = encoding == Encoding::Utf16Le;
    std::string out;
    out.reserve(bytes.size() / 2);
    std::size_t i = 0;
    auto const unit = [&](std::size_t at) -> std::uint32_t {
        auto const a = static_cast<unsigned char>(bytes[at]);
        auto const b = static_cast<unsigned char>(bytes[at + 1]);
        return little ? (a | (b << 8)) : ((a << 8) | b);
    };
    if (bytes.size() >= 2 && unit(0) == 0xFEFF) {
        i = 2;
    }
    for (; i + 1 < bytes.size(); i += 2) {
        std::uint32_t cp = unit(i);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < bytes.size()) {
            std::uint32_t const low = unit(i + 2);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        if (cp >= 0xD800 && cp <= 0xDFFF) {
            cp = 0xFFFD;  // a lone surrogate
        }
        AppendUtf8(out, cp);
    }
    return out;
}

File Parse(std::string_view text) {
    text = StripUtf8Bom(text);
    File file;
    Section* current = &file[""];  // keys before any section header (Win32 ignores them; kept apart)
    ForEachLine(text, [&](std::string_view line) {
        std::string const trimmed = Trim(line);
        if (!trimmed.empty() && trimmed[0] == '[') {
            std::size_t const close = trimmed.find(']');
            std::string const name = Lower(Trim(close == std::string::npos ? std::string_view(trimmed).substr(1)
                                                                             : std::string_view(trimmed).substr(1, close - 1)));
            auto const [it, inserted] = file.try_emplace(name);
            current = inserted ? &it->second : nullptr;  // a repeated section: Win32 reads the first one only
            return;
        }
        if (current != nullptr) {
            AddKey(*current, line);
        }
    });
    if (auto const top = file.find(""); top != file.end() && top->second.empty()) {
        file.erase(top);
    }
    return file;
}

Section ParseKeys(std::string_view lines) {
    Section section;
    ForEachLine(StripUtf8Bom(lines), [&](std::string_view line) { AddKey(section, line); });
    return section;
}

Loaded Read(std::filesystem::path const& path) {
    Loaded loaded;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return loaded;
    }
    std::string const bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    loaded.exists = true;
    loaded.encoding = Detect(bytes);
    loaded.file = Parse(Decode(bytes, loaded.encoding));
    return loaded;
}

std::optional<double> Number(std::string_view text) noexcept {
    std::size_t const first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return std::nullopt;
    }
    text.remove_prefix(first);
    if (!text.empty() && text[0] == '+') {
        text.remove_prefix(1);  // from_chars doesn't take a '+'; strtod did
    }
    double value = 0.0;
    auto const [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end == text.data() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

std::string Fixed(double value, int decimals) {
    char buffer[64];
    auto const [end, ec] = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed, decimals);
    if (ec != std::errc()) {
        return "0";
    }
    std::string out(buffer, end);
    if (out.size() > 1 && out[0] == '-' && out.find_first_not_of("-0.") == std::string::npos) {
        out.erase(0, 1);  // "-0.00" -> "0.00"
    }
    return out;
}

std::optional<std::string> Get(File const& file, std::string_view section, std::string_view key) {
    auto const s = file.find(section);
    if (s == file.end()) {
        return std::nullopt;
    }
    auto const k = s->second.find(key);
    if (k == s->second.end()) {
        return std::nullopt;
    }
    return k->second;
}

std::optional<double> GetNumber(File const& file, std::string_view section, std::string_view key) {
    std::optional<std::string> const text = Get(file, section, key);
    return text ? Number(*text) : std::nullopt;
}

std::string Utf8(std::filesystem::path const& path) {
    try {
        std::u8string const u = path.u8string();
        return std::string(u.begin(), u.end());
    } catch (...) {
        // Not valid UTF-16 (a lone surrogate in a file name): ASCII, with '?' for the rest.
        std::string out;
        for (wchar_t const c : path.native()) {
            out.push_back(c < 128 ? static_cast<char>(c) : '?');
        }
        return out;
    }
}

} // namespace mevr::ini
