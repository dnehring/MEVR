// SPDX-License-Identifier: GPL-3.0-only
#include "halcyon/le3/logger.h"
#include <filesystem>
#include <string>
#include <cstdio>

// Exercise the actual persistent renderer writer, not a substitute sharing policy.
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 1;
    const auto root = std::filesystem::path(argv[1]) / std::to_wstring(GetCurrentProcessId());
    std::filesystem::create_directories(root);
    if (!SetEnvironmentVariableW(L"LOCALAPPDATA", root.c_str())) return 2;
    constexpr char marker[] = "renderer-log-reader-test";
    ME2VR::Log::Line(marker);
    const auto path = ME2VR::Log::LogPath();
    const HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (reader == INVALID_HANDLE_VALUE) return 3;
    char buffer[8192]{};
    DWORD count{};
    const bool read = ReadFile(reader, buffer, sizeof(buffer), &count, nullptr) != FALSE;
    CloseHandle(reader);
    if (!read || std::string(buffer, count).find(marker) == std::string::npos) return 4;
    const HANDLE writer = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (writer != INVALID_HANDLE_VALUE) { CloseHandle(writer); return 5; }
    if (GetLastError() != ERROR_SHARING_VIOLATION) return 6;
    ME2VR::Log::Line("reader closed; original writer remains usable");
    std::puts("PASS live renderer log readable; competing writer refused");
    return 0;
}
