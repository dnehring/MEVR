// SPDX-License-Identifier: MIT
#include "install/Detect.hpp"

#include "core/Json.hpp"
#include "install/Manifest.hpp"

#include <Windows.h>
#include <TlHelp32.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cwctype>
#include <system_error>

namespace mevr::install::detect {
namespace {

namespace fs = std::filesystem;

std::string Lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool Contains(std::string_view hay, std::string_view needle) {
    return Lower(hay).find(Lower(needle)) != std::string::npos;
}

std::optional<std::wstring> RegString(HKEY root, wchar_t const* key, wchar_t const* value, REGSAM view) {
    wchar_t buffer[2048]{};
    DWORD size = sizeof buffer;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | view, nullptr, buffer, &size) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return std::wstring(buffer);
}

// Every quoted string pair `"key" "value"` in a VDF / ACF file, in order.
std::vector<std::pair<std::string, std::string>> VdfPairs(std::string_view text) {
    std::vector<std::pair<std::string, std::string>> pairs;
    std::vector<std::string> tokens;
    std::size_t i = 0;
    auto flushLine = [&] {
        if (tokens.size() == 2) {
            pairs.emplace_back(tokens[0], tokens[1]);
        }
        tokens.clear();
    };
    while (i < text.size()) {
        char const c = text[i];
        if (c == '\n' || c == '{' || c == '}') {
            flushLine();
            ++i;
        } else if (c == '"') {
            std::string token;
            ++i;
            while (i < text.size() && text[i] != '"') {
                if (text[i] == '\\' && i + 1 < text.size()) {
                    token += text[i + 1];
                    i += 2;
                    continue;
                }
                token += text[i++];
            }
            ++i;
            tokens.push_back(std::move(token));
        } else {
            ++i;
        }
    }
    flushLine();
    return pairs;
}

} // namespace

std::string Utf8(std::wstring const& w) {
    if (w.empty()) {
        return {};
    }
    int const n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring Wide(std::string_view s) {
    if (s.empty()) {
        return {};
    }
    int const n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

std::vector<fs::path> SteamLibraries(std::string_view vdf) {
    std::vector<fs::path> libraries;
    for (auto const& [key, value] : VdfPairs(vdf)) {
        if (Lower(key) == "path" && !value.empty()) {
            libraries.emplace_back(Wide(value));
        }
    }
    return libraries;
}

std::optional<std::string> AppManifestInstallDir(std::string_view acf) {
    for (auto const& [key, value] : VdfPairs(acf)) {
        if (Lower(key) == "installdir" && !value.empty()) {
            return value;
        }
    }
    return std::nullopt;
}

bool IsGameRoot(fs::path const& root) {
    std::error_code ec;
    return !root.empty() && fs::exists(root / L"Game" / L"ME1" / L"Binaries" / L"Win64" / L"MassEffect1.exe", ec);
}

std::optional<fs::path> RootFromPicked(fs::path const& picked) {
    for (fs::path dir = picked; !dir.empty(); dir = dir.parent_path()) {
        if (IsGameRoot(dir)) {
            return dir;
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    // A folder above the root (a Steam library's common folder, say).
    std::error_code ec;
    for (fs::path const& child : {picked / L"Mass Effect Legendary Edition", picked / L"steamapps" / L"common" / L"Mass Effect Legendary Edition"}) {
        if (IsGameRoot(child)) {
            return child;
        }
    }
    return std::nullopt;
}

std::vector<GameInstall> FindInstalls() {
    std::vector<GameInstall> found;
    auto add = [&](fs::path const& root, char const* source) {
        if (!IsGameRoot(root)) {
            return;
        }
        std::error_code ec;
        fs::path const canonical = fs::weakly_canonical(root, ec);
        for (GameInstall const& g : found) {
            if (_wcsicmp(fs::weakly_canonical(g.root, ec).c_str(), canonical.c_str()) == 0) {
                return;
            }
        }
        found.push_back({root, source});
    };
    // Steam: every library in libraryfolders.vdf with appmanifest_1328670.acf.
    if (auto const steam = RegString(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", 0)) {
        fs::path const steamPath = fs::path(*steam).make_preferred();
        std::vector<fs::path> libraries{steamPath};
        if (auto const vdf = ReadFile(steamPath / L"steamapps" / L"libraryfolders.vdf")) {
            for (fs::path const& lib : SteamLibraries(*vdf)) {
                libraries.push_back(lib);
            }
        }
        for (fs::path const& lib : libraries) {
            if (auto const acf = ReadFile(lib / L"steamapps" / (std::wstring(L"appmanifest_") + Wide(kSteamAppId) + L".acf"))) {
                if (auto const dir = AppManifestInstallDir(*acf)) {
                    add(lib / L"steamapps" / L"common" / fs::path(Wide(*dir)), "Steam");
                }
            }
        }
    }
    for (REGSAM const view : {RRF_SUBKEY_WOW6464KEY, RRF_SUBKEY_WOW6432KEY}) {
        // Steam's uninstall entry.
        if (auto const dir = RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 1328670",
                L"InstallLocation", view)) {
            add(fs::path(*dir), "Steam");
        }
        // The EA app / Origin: HKLM\SOFTWARE\BioWare\<...Legendary Edition> "Install Dir" (as MEM and ME3Tweaks read it).
        if (auto const dir = RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\BioWare\\Mass Effect Legendary Edition", L"Install Dir", view)) {
            add(fs::path(*dir), "EA app");
        }
    }
    return found;
}

std::optional<ModuleVersion> FileVersion(fs::path const& file) {
    DWORD handle = 0;
    DWORD const size = GetFileVersionInfoSizeW(file.c_str(), &handle);
    if (size == 0) {
        return std::nullopt;
    }
    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(file.c_str(), 0, size, data.data())) {
        return std::nullopt;
    }
    VS_FIXEDFILEINFO* info = nullptr;
    UINT infoSize = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &infoSize) || info == nullptr || infoSize < sizeof(VS_FIXEDFILEINFO)) {
        return std::nullopt;
    }
    return ModuleVersion{HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS), HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
}

std::optional<ModuleVersion> ParseVersion(std::string_view text) {
    std::uint16_t parts[4]{};
    for (int i = 0; i < 4; ++i) {
        std::size_t const dot = text.find('.');
        std::string_view const part = i < 3 ? text.substr(0, dot) : text;
        if (part.empty() || (i < 3 && dot == std::string_view::npos)) {
            return std::nullopt;
        }
        auto const [end, ec] = std::from_chars(part.data(), part.data() + part.size(), parts[i]);
        if (ec != std::errc{} || end != part.data() + part.size()) {
            return std::nullopt;
        }
        if (i < 3) {
            text.remove_prefix(dot + 1);
        }
    }
    return ModuleVersion{parts[0], parts[1], parts[2], parts[3]};
}

VersionCheck CheckVersion(std::optional<ModuleVersion> const& version) {
    if (!version) {
        return VersionCheck::Missing;
    }
    return *version == kSupportedGameVersion ? VersionCheck::Supported : VersionCheck::Different;
}

DllIdentity ReadIdentity(fs::path const& dll) {
    DllIdentity id;
    std::error_code ec;
    id.exists = fs::exists(dll, ec);
    if (!id.exists) {
        return id;
    }
    DWORD handle = 0;
    DWORD const size = GetFileVersionInfoSizeW(dll.c_str(), &handle);
    if (size == 0) {
        return id;
    }
    std::vector<std::byte> data(size);
    if (!GetFileVersionInfoW(dll.c_str(), 0, size, data.data())) {
        return id;
    }
    struct Translation {
        WORD language;
        WORD codePage;
    }* translations = nullptr;
    UINT bytes = 0;
    std::wstring prefix = L"\\StringFileInfo\\040904b0\\";
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &bytes) && bytes >= sizeof(Translation)) {
        wchar_t buffer[64];
        swprintf_s(buffer, L"\\StringFileInfo\\%04x%04x\\", translations[0].language, translations[0].codePage);
        prefix = buffer;
    }
    auto field = [&](wchar_t const* name) {
        wchar_t* value = nullptr;
        UINT length = 0;
        std::wstring const query = prefix + name;
        if (VerQueryValueW(data.data(), query.c_str(), reinterpret_cast<void**>(&value), &length) && value != nullptr && length > 0) {
            return Utf8(std::wstring(value, length > 0 && value[length - 1] == L'\0' ? length - 1 : length));
        }
        return std::string();
    };
    id.company = field(L"CompanyName");
    id.product = field(L"ProductName");
    id.description = field(L"FileDescription");
    return id;
}

BinkState ClassifyBink(DllIdentity const& bink, bool originalExists) {
    if (!bink.exists) {
        return BinkState::Missing;
    }
    if (Contains(bink.company, "RAD Game Tools")) {
        return BinkState::Original;
    }
    if (Contains(bink.product, "MEVR")) {
        return originalExists ? BinkState::Ours : BinkState::Broken;
    }
    if (Contains(bink.product, "LEBinkProxy") || Contains(bink.company, "ME3Tweaks")) {
        return originalExists ? BinkState::LEBinkProxy : BinkState::Broken;
    }
    return originalExists ? BinkState::OtherProxy : BinkState::Broken;
}

char const* BinkStateText(BinkState state) {
    switch (state) {
    case BinkState::Missing: return "bink2w64.dll is missing: verify the game's files in Steam or the EA app";
    case BinkState::Original: return "No ASI loader yet: ours will be installed";
    case BinkState::Ours: return "Our ASI loader is installed";
    case BinkState::LEBinkProxy: return "ME3Tweaks' ASI loader is installed: it will load the mod";
    case BinkState::OtherProxy: return "Another bink2w64.dll proxy is installed: it will be used as it is";
    case BinkState::Broken: return "bink2w64.dll is a proxy without bink2w64_original.dll behind it: verify the game's files";
    }
    return "";
}

std::string ClassifyRuntime(std::string_view manifestPath, std::string_view runtimeName) {
    if (manifestPath.empty()) {
        return "";
    }
    std::string const file = Lower(fs::path(Wide(manifestPath)).filename().string());
    std::string const name = Lower(runtimeName);
    if (file == "steamxr_win64.json" || Contains(name, "steamvr")) {
        return "SteamVR";
    }
    if (file.find("oculus") != std::string::npos || Contains(name, "oculus") || Contains(name, "meta")) {
        return "Meta";
    }
    if (file.find("virtualdesktop") != std::string::npos || Contains(name, "virtual desktop") || Contains(name, "vdxr")) {
        return "Virtual Desktop";
    }
    if (file == "mixedrealityruntime.json" || Contains(name, "mixed reality")) {
        return "Windows Mixed Reality";
    }
    if (file.find("pimax") != std::string::npos || Contains(name, "pimax")) {
        return "Pimax";
    }
    if (file.find("varjo") != std::string::npos || Contains(name, "varjo")) {
        return "Varjo";
    }
    return "Other";
}

XrRuntime ActiveRuntime() {
    XrRuntime r;
    wchar_t env[2048]{};
    std::wstring manifest;
    if (GetEnvironmentVariableW(L"XR_RUNTIME_JSON", env, 2048) > 0) {
        manifest = env;
    } else if (auto const value = RegString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_SUBKEY_WOW6464KEY)) {
        manifest = *value;
    }
    r.manifest = Utf8(manifest);
    if (!manifest.empty()) {
        if (auto const text = ReadFile(fs::path(manifest))) {
            if (auto const json = json::Parse(*text)) {
                r.name = json->Get("runtime").String("name");
            }
        }
    }
    r.kind = ClassifyRuntime(r.manifest, r.name);
    return r;
}

std::optional<EyeSize> EyeSizeFromHalcyonLog(std::string_view log) {
    constexpr std::string_view kKey = "recommended eye image ";
    std::size_t const at = log.rfind(kKey);
    if (at == std::string_view::npos) {
        return std::nullopt;
    }
    std::string_view rest = log.substr(at + kKey.size());
    EyeSize size;
    auto const [afterWidth, e1] = std::from_chars(rest.data(), rest.data() + rest.size(), size.width);
    if (e1 != std::errc{} || afterWidth >= rest.data() + rest.size() || *afterWidth != 'x') {
        return std::nullopt;
    }
    auto const [afterHeight, e2] = std::from_chars(afterWidth + 1, rest.data() + rest.size(), size.height);
    (void)afterHeight;
    if (e2 != std::errc{} || size.width < 256 || size.height < 256 || size.width > 16384 || size.height > 16384) {
        return std::nullopt;
    }
    return size;
}

fs::path HalcyonLogPath() {
    wchar_t buffer[MAX_PATH]{};
    GetEnvironmentVariableW(L"LOCALAPPDATA", buffer, MAX_PATH);
    return fs::path(buffer) / L"MELEVR" / L"MELEVR_Log.txt";
}

bool ProcessRunning(wchar_t const* exeName) {
    HANDLE const snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof entry;
    bool running = false;
    for (BOOL ok = Process32FirstW(snapshot, &entry); ok && !running; ok = Process32NextW(snapshot, &entry)) {
        running = _wcsicmp(entry.szExeFile, exeName) == 0;
    }
    CloseHandle(snapshot);
    return running;
}

bool GameRunning() {
    return ProcessRunning(L"MassEffect1.exe") || ProcessRunning(L"MassEffectLauncher.exe");
}

bool ReadOnlyFile(fs::path const& file) {
    DWORD const attributes = GetFileAttributesW(file.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) != 0;
}

bool HasMelevrMarker(std::string_view bytes) {
    // As MELE-VR.bat's `findstr /m "MELEVR"` (ASCII), and the UTF-16 spelling a version resource would use.
    static constexpr char kWide[] = {'M', 0, 'E', 0, 'L', 0, 'E', 0, 'V', 0, 'R', 0};
    return bytes.find("MELEVR") != std::string_view::npos || bytes.find(std::string_view(kWide, sizeof kWide)) != std::string_view::npos;
}

UpstreamHalcyon FindUpstreamHalcyon(fs::path const& win64) {
    UpstreamHalcyon u;
    std::error_code ec;
    fs::path const dxgi = win64 / L"dxgi.dll";
    if (!fs::is_regular_file(dxgi, ec) || fs::file_size(dxgi, ec) > (64u << 20)) {
        return u;
    }
    if (auto const bytes = ReadFile(dxgi)) {
        u.dxgi = HasMelevrMarker(*bytes);
    }
    u.openxrLoader = u.dxgi && fs::is_regular_file(win64 / L"openxr_loader.dll", ec);
    return u;
}

bool CanWrite(fs::path const& dir) {
    fs::path const probe = dir / L"mevr-write-test.tmp";
    HANDLE const file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

} // namespace mevr::install::detect
