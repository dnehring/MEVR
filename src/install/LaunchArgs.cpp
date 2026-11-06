// SPDX-License-Identifier: MIT
#include "install/LaunchArgs.hpp"

#include <Windows.h>
#include <ShlObj.h>

#include <cstdlib>
#include <cwctype>
#include <fstream>
#include <map>
#include <sstream>

namespace mevr::install::launch {
namespace {

namespace fs = std::filesystem;

std::string Trim(std::string s) {
    while (!s.empty() && (s.back() == '\r' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    return s.substr(start);
}

std::wstring Lower(std::wstring s) {
    for (wchar_t& c : s) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return s;
}

// A whole decimal number 1..9, else nullopt.
std::optional<int> GameNumber(std::wstring const& text) {
    if (text.size() != 1 || text[0] < L'1' || text[0] > L'9') {
        return std::nullopt;
    }
    return text[0] - L'0';
}

} // namespace

LauncherConfig ParseLauncherConfig(std::string_view text) {
    LauncherConfig c;
    std::istringstream in{std::string(text)};
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        std::size_t const eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string const key = Trim(line.substr(0, eq));
        std::string const value = Trim(line.substr(eq + 1));
        if (key == "Language" && !value.empty()) {
            c.language = value;
        } else if (key == "EnglishVOEnabled") {
            c.englishVoices = value == "true";
        } else if (key == "SubtitleSize") {
            int const n = std::atoi(value.c_str());
            if (n > 0 && n < 100) {
                c.subtitleSize = n;
            }
        }
    }
    return c;
}

fs::path LauncherConfigPath() {
    PWSTR documents = nullptr;
    fs::path path;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) {
        path = fs::path(documents) / L"BioWare" / L"Mass Effect Legendary Edition" / L"LauncherConfig.cfg";
    }
    CoTaskMemFree(documents);
    return path;
}

LauncherConfig ReadLauncherConfig() {
    fs::path const path = LauncherConfigPath();
    if (path.empty()) {
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream text;
    text << in.rdbuf();
    return ParseLauncherConfig(text.str());
}

std::string Le1Language(LauncherConfig const& c) {
    // {local voices, English voices}.
    static std::map<std::string, std::pair<char const*, char const*>> const kCodes = {
        {"en_US", {"INT", "INT"}}, {"fr_FR", {"FR", "FE"}}, {"de_DE", {"DE", "GE"}}, {"it_IT", {"IT", "IE"}},
        {"ja_JP", {"JA", "JA"}}, {"es_ES", {"ES", "ES"}}, {"ru_RU", {"RA", "RU"}}, {"pl_PL", {"PLPC", "PL"}},
    };
    auto const it = kCodes.find(c.language);
    if (it == kCodes.end()) {
        return "INT";
    }
    return c.englishVoices ? it->second.second : it->second.first;
}

std::wstring Le1Arguments(LauncherConfig const& c) {
    std::string const language = Le1Language(c);  // ASCII codes only
    return L"-NoHomeDir -SeekFreeLoadingPCConsole -locale {locale} -Subtitles " + std::to_wstring(c.subtitleSize) + L" -OVERRIDELANGUAGE=" +
        std::wstring(language.begin(), language.end());
}

LauncherBoot ParseLauncherArgs(std::vector<std::wstring> const& args) {
    LauncherBoot boot;
    for (std::size_t i = 0; i < args.size(); ++i) {
        std::wstring const a = Lower(args[i]);
        if (a == L"-autoterminate") {
            boot.autoTerminate = true;
        } else if (a == L"-game" && i + 1 < args.size()) {
            if (auto const n = GameNumber(args[i + 1]); n && *n <= 3) {
                boot.game = n;
                ++i;
            }
        } else if (a.rfind(L"-game=", 0) == 0) {
            if (auto const n = GameNumber(a.substr(6)); n && *n <= 3) {
                boot.game = n;
            }
        }
    }
    return boot;
}

} // namespace mevr::install::launch
