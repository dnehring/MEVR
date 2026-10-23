// SPDX-License-Identifier: MIT
#include "core/vr/TestRequest.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace mevr::vr::test_request {
namespace {

std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool IsKeyChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

// `key=value` with a non-empty key of key characters.
bool IsOption(std::string_view token, std::string_view& key, std::string_view& value) {
    size_t const eq = token.find('=');
    if (eq == std::string_view::npos || eq == 0) {
        return false;
    }
    for (size_t i = 0; i < eq; ++i) {
        if (!IsKeyChar(token[i])) {
            return false;
        }
    }
    key = token.substr(0, eq);
    value = token.substr(eq + 1);
    return true;
}

} // namespace

bool Request::Has(std::string_view key) const {
    return options.find(Lower(key)) != options.end();
}

std::string Request::Get(std::string_view key, std::string_view fallback) const {
    auto const it = options.find(Lower(key));
    return it != options.end() ? it->second : std::string(fallback);
}

double Request::Number(std::string_view key, double fallback) const {
    auto const it = options.find(Lower(key));
    if (it == options.end() || it->second.empty()) {
        return fallback;
    }
    char* end = nullptr;
    double const value = std::strtod(it->second.c_str(), &end);
    if (end == it->second.c_str() || *end != '\0' || !std::isfinite(value)) {
        return fallback;
    }
    return value;
}

bool Request::Flag(std::string_view key, bool fallback) const {
    auto const it = options.find(Lower(key));
    if (it == options.end()) {
        return fallback;
    }
    std::string const value = Lower(it->second);
    if (value == "1" || value == "true" || value == "yes" || value == "on") {
        return true;
    }
    if (value == "0" || value == "false" || value == "no" || value == "off") {
        return false;
    }
    return fallback;
}

Request Parse(std::string_view text) {
    Request request;
    std::vector<std::string_view> free;
    size_t i = 0;
    // A UTF-8 byte order mark (PowerShell 5's Out-File writes one).
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        i = 3;
    }
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
            ++i;
        }
        size_t const start = i;
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i])) == 0) {
            ++i;
        }
        if (i == start) {
            break;
        }
        std::string_view const token = text.substr(start, i - start);
        std::string_view key, value;
        if (IsOption(token, key, value)) {
            request.options[Lower(key)] = std::string(value);
        } else {
            free.push_back(token);
        }
    }
    for (size_t t = 0; t < free.size(); ++t) {
        if (t > 0) {
            request.argument += ' ';
        }
        request.argument += free[t];
    }
    if (request.argument.size() >= 2 &&
        ((request.argument.front() == '"' && request.argument.back() == '"') ||
            (request.argument.front() == '\'' && request.argument.back() == '\''))) {
        request.argument = request.argument.substr(1, request.argument.size() - 2);
    }
    return request;
}

std::string JsonString(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (char const c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "\\u%04x", static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buffer;
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

std::string JsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%.6g", value);
    return buffer;
}

} // namespace mevr::vr::test_request
