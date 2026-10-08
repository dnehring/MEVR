#include "core/Version.hpp"

#include <Windows.h>

#include <cstddef>
#include <format>
#include <vector>

namespace mevr {

std::string ModuleVersion::ToString() const {
    return std::format("{}.{}.{}.{}", major, minor, build, revision);
}

std::optional<ModuleVersion> GetHostExeVersion() {
    wchar_t path[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
        return std::nullopt;
    }

    DWORD handle = 0;
    DWORD const size = GetFileVersionInfoSizeW(path, &handle);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(path, 0, size, data.data())) {
        return std::nullopt;
    }

    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) ||
        info == nullptr || infoSize < sizeof(VS_FIXEDFILEINFO)) {
        return std::nullopt;
    }

    return ModuleVersion{
        HIWORD(info->dwFileVersionMS),
        LOWORD(info->dwFileVersionMS),
        HIWORD(info->dwFileVersionLS),
        LOWORD(info->dwFileVersionLS),
    };
}

} // namespace mevr
