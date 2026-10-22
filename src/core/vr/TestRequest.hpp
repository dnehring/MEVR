#pragma once

#include <map>
#include <string>
#include <string_view>

// The test loop's request files (plugin/TestLoop: %LOCALAPPDATA%\MEVR\snapshot.request,
// record.request, replay.request) and the small JSON it writes. Pure; tested in tests/Main.cpp.
namespace mevr::vr::test_request {

// A request file's text: whitespace-separated `key=value` options (keys are letters, digits and
// '_'; case-insensitive) and everything else, joined with single spaces, as one free argument (a
// path, which may contain spaces, or a word like "stop"). Quotes around the argument are dropped.
//   C:\Users\me\AppData\Local\MEVR\recordings\2026-10-21_14-03-07.mevrrec loop=1 inputs=1
struct Request {
    std::string argument;
    std::map<std::string, std::string> options;  // keys lower-case

    bool Has(std::string_view key) const;
    std::string Get(std::string_view key, std::string_view fallback = {}) const;
    // A number, or `fallback` when missing or not a number.
    double Number(std::string_view key, double fallback) const;
    // 1, true, yes, on (any case) are true; 0, false, no, off false; otherwise `fallback`.
    bool Flag(std::string_view key, bool fallback) const;
};

Request Parse(std::string_view text);

// `text` as a JSON string literal, quotes included (UTF-8 passes through; control characters
// escaped).
std::string JsonString(std::string_view text);

// A finite float for JSON (NaN and infinities become null).
std::string JsonNumber(double value);

} // namespace mevr::vr::test_request
