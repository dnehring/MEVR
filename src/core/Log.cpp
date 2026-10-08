#include "core/Log.hpp"

#include <Windows.h>
#include <share.h>

#include <cstdio>
#include <mutex>

namespace mevr::log {
namespace {

std::mutex gMutex;
std::FILE* gFile = nullptr;
std::filesystem::path gPath;
Sink gSink = nullptr;

std::filesystem::path ExeDirectory() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path LocalAppDataDirectory() {
    wchar_t buffer[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH) == 0) {
        return {};
    }
    return std::filesystem::path(buffer) / L"MEVR";
}

std::FILE* TryOpen(std::filesystem::path const& directory, std::wstring_view fileName) {
    if (directory.empty()) {
        return nullptr;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    auto const path = directory / fileName;
    std::FILE* file = _wfsopen(path.c_str(), L"w", _SH_DENYWR);
    if (file != nullptr) {
        gPath = path;
    }
    return file;
}

} // namespace

bool Open(std::wstring_view fileName) {
    std::lock_guard lock(gMutex);
    if (gFile != nullptr) {
        return true;
    }
    gFile = TryOpen(ExeDirectory() / L"Logs", fileName);
    if (gFile == nullptr) {
        gFile = TryOpen(LocalAppDataDirectory(), fileName);
    }
    return gFile != nullptr;
}

void Close() {
    std::lock_guard lock(gMutex);
    if (gFile != nullptr) {
        std::fclose(gFile);
        gFile = nullptr;
    }
}

std::filesystem::path const& Path() {
    return gPath;
}

void SetSink(Sink sink) {
    gSink = sink;
}

void Write(std::string_view level, std::string_view message) {
    if (gSink != nullptr) {
        gSink(std::string(level).c_str(), std::string(message).c_str());
        return;
    }

    SYSTEMTIME t{};
    GetLocalTime(&t);
    auto const line = std::format("[{:02}:{:02}:{:02}.{:03}] [{}] {}\n",
        t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, level, message);

    std::lock_guard lock(gMutex);
    if (gFile != nullptr) {
        std::fwrite(line.data(), 1, line.size(), gFile);
        std::fflush(gFile);
    }
    OutputDebugStringA(line.c_str());
}

} // namespace mevr::log
