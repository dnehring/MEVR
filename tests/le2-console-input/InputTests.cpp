// SPDX-License-Identifier: MIT
// Actual Windows UTF-8 conversion; no SDK, game allocator, or native dispatch.
#include "engine/ConsoleCommandInput.hpp"

#include <iostream>
#include <vector>

namespace {
using mevr::engine::detail::ConsoleCommandInput;
void Check(bool ok, char const* why) { if (!ok) throw std::runtime_error(why); }
void Rejected(std::string_view input) {
    bool rejected = false;
    try { (void)ConsoleCommandInput(input); }
    catch (std::invalid_argument const&) { rejected = true; }
    Check(rejected, "invalid command was accepted");
}
void ValidText() {
    Check(ConsoleCommandInput("show stats") == L"show stats", "ASCII changed");
    // Two-, three-, and four-byte UTF-8, including a UTF-16 surrogate pair.
    Check(ConsoleCommandInput("say \xC3\xA9 \xE4\xB8\xAD \xF0\x9F\x98\x80") ==
        std::wstring({L's', L'a', L'y', L' ', 0x00E9, L' ', 0x4E2D, L' ', 0xD83D, 0xDE00}),
        "valid Unicode was replaced or mis-sized");
    Check(ConsoleCommandInput("\xF4\x8F\xBF\xBF") == std::wstring({0xDBFF, 0xDFFF}),
        "maximum Unicode scalar changed");
    Check(ConsoleCommandInput("\xEF\xBF\xBD") == std::wstring({0xFFFD}),
        "explicit valid replacement character should be preserved");
    std::string const backing = "valid\nignored";
    auto const converted = ConsoleCommandInput(std::string_view(backing.data(), 5));
    Check(converted == L"valid" && converted.c_str()[converted.size()] == L'\0',
        "explicit input length or output termination incorrect");
}
void Boundaries() {
    Check(ConsoleCommandInput("x") == L"x", "one byte rejected");
    Check(ConsoleCommandInput(std::string(4096, 'x')) == std::wstring(4096, L'x'), "4096 bytes rejected");
    Rejected(std::string(4097, 'x'));
    std::string const boundary = std::string(4092, 'x') + "\xF0\x9F\x98\x80";
    auto const wide = ConsoleCommandInput(boundary);
    Check(wide.size() == 4094 && wide[4092] == 0xD83D && wide[4093] == 0xDE00,
        "supplementary character at byte boundary changed");
    Rejected(std::string(4093, 'x') + "\xF0\x9F\x98\x80");
    Rejected(std::string(4095, 'x') + "\xC2"); // exactly 4096, incomplete final code point
    // A malformed oversized command must fail at the length gate, not decoding.
    try {
        (void)ConsoleCommandInput(std::string(4096, 'x') + "\xFF");
        Check(false, "oversized malformed input accepted");
    } catch (std::invalid_argument const& error) {
        Check(std::string_view(error.what()).find("4096") != std::string_view::npos,
            "oversized input reached conversion");
    }
}
void InvalidText() {
    Rejected({});
    for (unsigned int c = 0; c <= 127; ++c) {
        if (c < 32 || c == 127) {
            std::string input = "before";
            input.push_back(static_cast<char>(c));
            input += "after";
            Rejected(input);
        }
    }
    std::vector<std::string> const invalid{
        "\x80", "\xBF", "\xC2", "\xE2\x82", "\xF0\x9F\x98", // stray/truncated
        "\xC2x", "\xE2x\xA1", "\xF0\x28\x8C\xBC", // invalid continuation
        "\xC0\x80", "\xC0\xAF", "\xC1\xBF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF", // overlong
        "\xED\xA0\x80", "\xED\xBF\xBF", "\xED\xA0\xBD\xED\xB8\x80", // surrogate/CESU-8
        "\xF4\x90\x80\x80", "\xF5\x80\x80\x80", "\xFE", "\xFF" // above Unicode limit
    };
    for (auto const& input : invalid) Rejected(input);
}
}
int main() {
    try {
        ValidText(); Boundaries(); InvalidText();
        std::cout << "Console UTF-8 input checks passed using Windows conversion API\n";
        return 0;
    } catch (std::exception const& error) { std::cerr << error.what() << '\n'; return 1; }
}
