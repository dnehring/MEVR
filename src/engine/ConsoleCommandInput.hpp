// SPDX-License-Identifier: MIT
#pragma once

#include <Windows.h>

#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace mevr::engine::detail {
// Console input only: match the control protocol's byte limit and C0/DEL ban.
// Call before constructing any SDK string or dispatching. This validates text,
// not command permissions, target identity, or native/output ownership.
inline std::wstring ConsoleCommandInput(std::string_view utf8) {
    if (utf8.empty() || utf8.size() > 4096) {
        throw std::invalid_argument("console command must contain 1..4096 UTF-8 bytes");
    }
    for (unsigned char byte : utf8) {
        if (byte < 32 || byte == 127) {
            throw std::invalid_argument("console command contains NUL or control input");
        }
    }
    // The byte bound above makes the narrowing safe. Explicit lengths avoid
    // scanning beyond the view; std::wstring supplies the final terminator.
    int const bytes = static_cast<int>(utf8.size());
    int const required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), bytes, nullptr, 0);
    if (required == 0) {
        DWORD const error = GetLastError();
        if (error == ERROR_NO_UNICODE_TRANSLATION) {
            throw std::invalid_argument("console command is not valid UTF-8");
        }
        throw std::system_error(static_cast<int>(error), std::system_category(), "console UTF-8 sizing failed");
    }
    if (required < 0 || required > bytes) {
        throw std::runtime_error("console UTF-8 sizing returned an invalid length");
    }
    std::wstring wide(static_cast<std::size_t>(required), L'\0');
    int const written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
        utf8.data(), bytes, wide.data(), required);
    if (written == 0) {
        DWORD const error = GetLastError();
        throw std::system_error(static_cast<int>(error), std::system_category(), "console UTF-8 conversion failed");
    }
    if (written != required) {
        throw std::runtime_error("console UTF-8 conversion returned an unexpected length");
    }
    return wide;
}
} // namespace mevr::engine::detail
