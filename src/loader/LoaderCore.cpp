#include "loader/LoaderCore.hpp"

#include <algorithm>
#include <cwchar>
#include <cwctype>

namespace mevr::loader {
namespace {

bool EqualNoCase(std::wstring_view a, std::wstring_view b) {
    return a.size() == b.size() &&
        std::equal(a.begin(), a.end(), b.begin(), [](wchar_t x, wchar_t y) { return std::towlower(x) == std::towlower(y); });
}

int HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

} // namespace

std::optional<spi::Game> HostFromExeName(std::wstring_view exe) {
    if (EqualNoCase(exe, L"MassEffect1.exe")) {
        return spi::Game::LE1;
    }
    if (EqualNoCase(exe, L"MassEffect2.exe")) {
        return spi::Game::LE2;
    }
    if (EqualNoCase(exe, L"MassEffect3.exe")) {
        return spi::Game::LE3;
    }
    if (EqualNoCase(exe, L"MassEffectLauncher.exe")) {
        return spi::Game::Launcher;
    }
    return std::nullopt;
}

std::optional<Pattern> ParsePattern(std::string_view text, bool* tooLong) {
    if (tooLong != nullptr) {
        *tooLong = false;
    }
    if (text.size() > 299) {
        if (tooLong != nullptr) {
            *tooLong = true;
        }
        return std::nullopt;
    }
    Pattern p;
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ') {
            ++i;
            continue;
        }
        std::size_t end = text.find(' ', i);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view const token = text.substr(i, end - i);
        i = end;
        if (token == "?" || token == "??") {
            p.bytes.push_back(0);
            p.mask.push_back(false);
            continue;
        }
        if (token.size() != 2 || HexDigit(token[0]) < 0 || HexDigit(token[1]) < 0) {
            return std::nullopt;
        }
        p.bytes.push_back(static_cast<std::uint8_t>(HexDigit(token[0]) * 16 + HexDigit(token[1])));
        p.mask.push_back(true);
    }
    if (p.bytes.size() > 100) {
        if (tooLong != nullptr) {
            *tooLong = true;
        }
        return std::nullopt;
    }
    if (p.bytes.empty() || !p.mask.front()) {
        return std::nullopt;  // nothing to anchor on
    }
    return p;
}

std::optional<std::size_t> FindPattern(std::span<std::uint8_t const> memory, Pattern const& pattern) {
    std::size_t const n = pattern.bytes.size();
    if (n == 0 || memory.size() < n) {
        return std::nullopt;
    }
    std::uint8_t const first = pattern.bytes.front();
    for (std::size_t at = 0; at + n <= memory.size(); ++at) {
        if (memory[at] != first) {
            continue;
        }
        bool match = true;
        for (std::size_t k = 1; k < n && match; ++k) {
            match = !pattern.mask[k] || memory[at + k] == pattern.bytes[k];
        }
        if (match) {
            return at;
        }
    }
    return std::nullopt;
}

bool LooksDecrypted(int differing, int samples) {
    return samples > 0 && differing * 10 >= samples * 9;
}

std::vector<std::size_t> SampleOffsets(std::size_t size, int samples, std::size_t window) {
    std::vector<std::size_t> offsets;
    if (size < window || samples <= 0) {
        return offsets;
    }
    std::size_t const span = size - window;
    for (int k = 0; k < samples; ++k) {
        offsets.push_back(span * static_cast<std::size_t>(k) / static_cast<std::size_t>(samples > 1 ? samples - 1 : 1));
    }
    return offsets;
}

bool ShouldAttach(int gameFlags, int minVersion, std::optional<spi::Game> host) {
    if (minVersion > static_cast<int>(spi::kVersion)) {
        return false;
    }
    if (!host) {
        return true;
    }
    return (gameFlags & (1 << static_cast<int>(*host))) != 0;
}

std::vector<std::wstring> SortAsiNames(std::vector<std::wstring> names) {
    std::sort(names.begin(), names.end(), [](std::wstring const& a, std::wstring const& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
    return names;
}

} // namespace mevr::loader
