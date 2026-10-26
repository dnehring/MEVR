// SPDX-License-Identifier: MIT
#include "install/Report.hpp"

#include "core/Json.hpp"
#include "install/Detect.hpp"
#include "install/Ini.hpp"
#include "install/Installer.hpp"
#include "install/Manifest.hpp"
#include "install/ReportMarks.hpp"
#include "install/Updates.hpp"
#include "install/Zip.hpp"
#include "mevr/BuildInfo.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cwctype>
#include <format>
#include <fstream>
#include <map>
#include <system_error>
#include <unordered_map>

namespace mevr::install::report {
namespace {

namespace fs = std::filesystem;

// ---- Text helpers ---------------------------------------------------------------------------------------------------
char Lower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

bool WordChar(char c) {
    auto const u = static_cast<unsigned char>(c);
    return std::isalnum(u) != 0 || c == '_' || u >= 0x80;
}

// ASCII case-insensitive find.
std::size_t IFind(std::string_view text, std::string_view needle, std::size_t from = 0) {
    if (needle.empty() || needle.size() > text.size()) {
        return std::string_view::npos;
    }
    char const first = Lower(needle[0]);
    for (std::size_t i = from; i + needle.size() <= text.size(); ++i) {
        if (Lower(text[i]) != first) {
            continue;
        }
        std::size_t k = 1;
        while (k < needle.size() && Lower(text[i + k]) == Lower(needle[k])) {
            ++k;
        }
        if (k == needle.size()) {
            return i;
        }
    }
    return std::string_view::npos;
}

bool IEquals(std::string_view a, std::string_view b) {
    return a.size() == b.size() && (a.empty() || IFind(a, b) == 0);
}

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

// Calls `f` with each line (without its line ending).
template <class F>
void Lines(std::string_view text, F&& f) {
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        f(line);
        start = end + 1;
    }
}

// At most `max` bytes, cut on a UTF-8 character boundary, with an ellipsis when cut.
std::string Cut(std::string_view s, std::size_t max) {
    if (s.size() <= max) {
        return std::string(s);
    }
    std::size_t n = max;
    while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) {
        --n;
    }
    return std::string(s.substr(0, n)) + "...";
}

// Between `open` and `close` after `from` in `line`; "" if not there.
std::string Between(std::string_view line, std::string_view open, std::string_view close, std::size_t from = 0) {
    std::size_t const a = line.find(open, from);
    if (a == std::string_view::npos) {
        return {};
    }
    std::size_t const b = line.find(close, a + open.size());
    if (b == std::string_view::npos) {
        return {};
    }
    return std::string(line.substr(a + open.size(), b - a - open.size()));
}

// Replaces `needle` (ASCII case-insensitively) where it doesn't continue a word on either side.
std::string ReplaceWord(std::string_view text, std::string_view needle, std::string_view with, bool checkStart, int& count) {
    std::string out;
    std::size_t at = 0;
    for (;;) {
        std::size_t const hit = IFind(text, needle, at);
        if (hit == std::string_view::npos) {
            break;
        }
        std::size_t const end = hit + needle.size();
        bool const startOk = !checkStart || hit == 0 || !WordChar(text[hit - 1]);
        bool const endOk = end == text.size() || !WordChar(text[end]);
        if (startOk && endOk) {
            if (out.empty()) {
                out.reserve(text.size());
            }
            out.append(text.substr(at, hit - at));
            out.append(with);
            ++count;
        } else {
            out.append(text.substr(at, end - at));
        }
        at = end;
    }
    if (at == 0) {
        return std::string(text);
    }
    out.append(text.substr(at));
    return out;
}

bool LocalPartChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' || c == '%' || c == '+' || c == '-';
}

bool DomainChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '-';
}

std::string RedactEmails(std::string_view text, int& count) {
    std::string out;
    std::size_t copied = 0;
    for (std::size_t i = text.find('@'); i != std::string_view::npos; i = text.find('@', i + 1)) {
        if (i < copied) {
            continue;
        }
        std::size_t l = i;
        while (l > copied && LocalPartChar(text[l - 1])) {
            --l;
        }
        while (l < i && (text[l] == '.' || text[l] == '-')) {
            ++l;
        }
        std::size_t r = i + 1;
        while (r < text.size() && DomainChar(text[r])) {
            ++r;
        }
        while (r > i + 1 && (text[r - 1] == '.' || text[r - 1] == '-')) {
            --r;
        }
        std::string_view const local = text.substr(l, i - l);
        std::string_view const domain = text.substr(i + 1, r - i - 1);
        std::size_t const dot = domain.rfind('.');
        bool valid = !local.empty() && dot != std::string_view::npos && dot > 0 && domain.size() - dot - 1 >= 2;
        if (valid) {
            for (char c : domain.substr(dot + 1)) {
                valid = valid && std::isalpha(static_cast<unsigned char>(c)) != 0;
            }
        }
        if (!valid) {
            continue;
        }
        out.append(text.substr(copied, l - copied));
        out.append("<email>");
        copied = r;
        ++count;
        i = r - 1;
    }
    if (copied == 0) {
        return std::string(text);
    }
    out.append(text.substr(copied));
    return out;
}

std::string EnvUtf8(wchar_t const* name) {
    wchar_t buffer[1024]{};
    DWORD const n = GetEnvironmentVariableW(name, buffer, 1024);
    return n == 0 || n >= 1024 ? std::string() : detect::Utf8(buffer);
}

// ---- Reading files ------------------------------------------------------------------------------------------------
struct Excerpt {
    bool found = false;
    bool locked = false;
    std::uint64_t size = 0;
    std::string text;
    std::string note;
};

// Opens shared with a writer (the game's logs are open while it runs).
HANDLE OpenShared(fs::path const& file) {
    return CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
}

bool ReadAt(HANDLE h, std::uint64_t offset, std::size_t count, std::string& out) {
    LARGE_INTEGER position{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(h, position, nullptr, FILE_BEGIN)) {
        return false;
    }
    out.resize(count);
    std::size_t done = 0;
    while (done < count) {
        DWORD got = 0;
        DWORD const ask = static_cast<DWORD>(std::min<std::size_t>(count - done, 1u << 24));
        if (!::ReadFile(h, out.data() + done, ask, &got, nullptr) || got == 0) {
            break;
        }
        done += got;
    }
    out.resize(done);
    return true;
}

std::string MbText(std::uint64_t bytes) {
    return std::format("{:.1f} MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
}

// The first `head` and the last `tail` bytes of `text`, cut at line ends, with a line saying what was left out.
std::string CutMiddle(std::string_view text, std::size_t head, std::size_t tail, std::string* note) {
    if (text.size() <= head + tail) {
        return std::string(text);
    }
    std::size_t h = text.rfind('\n', head);
    h = h == std::string_view::npos ? head : h + 1;
    std::size_t t = text.find('\n', text.size() - tail);
    t = t == std::string_view::npos ? text.size() - tail : t + 1;
    if (t <= h) {
        return std::string(text);
    }
    std::string out(text.substr(0, h));
    out += std::format("[... MEVR report: {} of this log left out here ...]\n", MbText(t - h));
    out.append(text.substr(t));
    if (note != nullptr) {
        *note = std::format("the first {} and the last {} of {}", MbText(h), MbText(text.size() - t), MbText(text.size()));
    }
    return out;
}

// The whole file, or only its last `tailOnly` bytes when it's bigger (starting at a line).
Excerpt ReadText(fs::path const& file, std::uint64_t tailOnly = 0) {
    Excerpt e;
    HANDLE const h = OpenShared(file);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD const error = GetLastError();
        e.locked = error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
        e.found = e.locked;
        if (e.locked) {
            e.note = "locked by the running game: not included";
        }
        return e;
    }
    e.found = true;
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    e.size = static_cast<std::uint64_t>(size.QuadPart);
    std::uint64_t from = 0;
    if (tailOnly != 0 && e.size > tailOnly) {
        from = e.size - tailOnly;
    }
    ReadAt(h, from, static_cast<std::size_t>(e.size - from), e.text);
    CloseHandle(h);
    if (from > 0) {
        std::size_t const nl = e.text.find('\n');
        if (nl != std::string::npos) {
            e.text.erase(0, nl + 1);
        }
    }
    return e;
}

std::string FileTimeText(fs::path const& file) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(file.c_str(), GetFileExInfoStandard, &data)) {
        return {};
    }
    FILETIME local{};
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&data.ftLastWriteTime, &local);
    FileTimeToSystemTime(&local, &st);
    return std::format("{:04}-{:02}-{:02} {:02}:{:02}", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
}

// ---- The machine --------------------------------------------------------------------------------------------------
std::wstring RegString(HKEY root, wchar_t const* key, wchar_t const* value) {
    wchar_t buffer[512]{};
    DWORD size = sizeof buffer;
    if (RegGetValueW(root, key, value, RRF_RT_REG_SZ, nullptr, buffer, &size) != ERROR_SUCCESS) {
        return {};
    }
    return buffer;
}

DWORD RegDword(HKEY root, wchar_t const* key, wchar_t const* value) {
    DWORD data = 0;
    DWORD size = sizeof data;
    if (RegGetValueW(root, key, value, RRF_RT_REG_DWORD, nullptr, &data, &size) != ERROR_SUCCESS) {
        return 0;
    }
    return data;
}

std::string WindowsVersion() {
    wchar_t const* const key = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
    std::string product = detect::Utf8(RegString(HKEY_LOCAL_MACHINE, key, L"ProductName"));
    std::string const display = detect::Utf8(RegString(HKEY_LOCAL_MACHINE, key, L"DisplayVersion"));
    std::string const build = detect::Utf8(RegString(HKEY_LOCAL_MACHINE, key, L"CurrentBuildNumber"));
    DWORD const ubr = RegDword(HKEY_LOCAL_MACHINE, key, L"UBR");
    // Windows 11 still says "Windows 10" in ProductName.
    if (std::atoi(build.c_str()) >= 22000 && product.rfind("Windows 10", 0) == 0) {
        product.replace(0, 10, "Windows 11");
    }
    std::string out = product.empty() ? std::string("Windows") : product;
    if (!display.empty()) {
        out += " " + display;
    }
    if (!build.empty()) {
        out += " (build " + build + (ubr != 0 ? "." + std::to_string(ubr) : std::string()) + ")";
    }
    return out;
}

void Gpu(System& s) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.GetAddressOf())))) {
        s.gpu = "unknown (DXGI unavailable)";
        return;
    }
    std::string names;
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0) {
            continue;
        }
        std::string const name = std::format("{} ({:.0f} GB)", detect::Utf8(desc.Description),
            static_cast<double>(desc.DedicatedVideoMemory) / (1024.0 * 1024.0 * 1024.0));
        if (s.gpu.empty()) {
            // The first hardware adapter is the one the game uses unless Windows' graphics settings say otherwise.
            s.gpu = name;
            LARGE_INTEGER umd{};
            if (SUCCEEDED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &umd))) {
                unsigned const a = HIWORD(umd.HighPart), b = LOWORD(umd.HighPart), c = HIWORD(umd.LowPart), d = LOWORD(umd.LowPart);
                s.gpuDriver = std::format("{}.{}.{}.{}", a, b, c, d);
                if (desc.VendorId == 0x10DE) {
                    unsigned const nv = (c % 10) * 10000 + d;
                    s.gpuDriver += std::format(" (NVIDIA {}.{:02})", nv / 100, nv % 100);
                }
            }
        } else if (name != s.gpu && names.find(name) == std::string::npos) {
            names += (names.empty() ? "" : ", ") + name;  // another GPU (the same one listed again isn't)
        }
    }
    if (!names.empty()) {
        s.gpu += "; also " + names;
    }
    if (s.gpu.empty()) {
        s.gpu = "unknown";
    }
}

std::optional<fs::path> KnownFolder(KNOWNFOLDERID const& id) {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path)) || path == nullptr) {
        return std::nullopt;
    }
    fs::path const out(path);
    CoTaskMemFree(path);
    return out;
}

std::string Listing(Layout const& layout) {
    std::string out;
    std::error_code ec;
    auto add = [&](fs::path const& dir, bool recursive, std::string const& title) {
        out += title + "\n";
        if (!fs::is_directory(dir, ec)) {
            out += "  (not there)\n";
            return;
        }
        std::vector<std::pair<std::string, std::uintmax_t>> rows;
        auto take = [&](fs::directory_entry const& entry) {
            if (rows.size() >= 400) {
                return;
            }
            std::string name = detect::Utf8(fs::relative(entry.path(), dir, ec).wstring());
            if (entry.is_directory(ec)) {
                rows.emplace_back(name + "\\", 0);
            } else {
                rows.emplace_back(name, entry.file_size(ec));
            }
        };
        if (recursive) {
            for (auto const& entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                take(entry);
            }
        } else {
            for (auto const& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                take(entry);
            }
        }
        std::sort(rows.begin(), rows.end());
        for (auto const& [name, size] : rows) {
            out += name.back() == '\\' ? std::format("  {}\n", name) : std::format("  {:<56} {:>12}\n", name, size);
        }
        if (rows.size() >= 400) {
            out += "  (more not listed)\n";
        }
    };
    out += "File names and sizes only: no other mod's contents are in this report.\n\n";
    add(layout.Win64(), false, "Game\\ME1\\Binaries\\Win64 (top level):");
    out += "\n";
    add(layout.Asi(), true, "Game\\ME1\\Binaries\\Win64\\ASI (everything):");
    return out;
}

std::string Fenced(std::string_view text) {
    std::string s(text);
    // A fence inside would end ours.
    for (std::size_t at = s.find("```"); at != std::string::npos; at = s.find("```", at)) {
        s.replace(at, 3, "'''");
    }
    return s;
}

bool SameValue(std::string_view a, std::string_view b) {
    a = Trim(a);
    b = Trim(b);
    if (IEquals(a, b)) {
        return true;
    }
    char* endA = nullptr;
    char* endB = nullptr;
    std::string const sa(a), sb(b);
    double const x = std::strtod(sa.c_str(), &endA);
    double const y = std::strtod(sb.c_str(), &endB);
    return !sa.empty() && !sb.empty() && *endA == '\0' && *endB == '\0' && std::fabs(x - y) < 1e-6;
}

} // namespace

// ---- Redaction ----------------------------------------------------------------------------------------------------
Redactions& Redactions::operator+=(Redactions const& o) {
    userName += o.userName;
    computerName += o.computerName;
    profilePath += o.profilePath;
    email += o.email;
    return *this;
}

Redactor::Redactor(std::vector<std::string> userNames, std::vector<std::string> computerNames, std::vector<std::string> profileDirs) {
    auto keep = [](std::vector<std::string>& into, std::string name) {
        name = std::string(Trim(name));
        if (name.size() >= 3 && std::none_of(into.begin(), into.end(), [&](std::string const& n) { return IEquals(n, name); })) {
            into.push_back(std::move(name));
        }
    };
    for (std::string& n : userNames) {
        keep(users_, std::move(n));
    }
    for (std::string& n : computerNames) {
        keep(computers_, std::move(n));
    }
    for (std::string dir : profileDirs) {
        while (!dir.empty() && (dir.back() == '\\' || dir.back() == '/')) {
            dir.pop_back();
        }
        std::size_t const slash = dir.find_last_of("\\/");
        if (dir.size() < 4 || slash == std::string::npos) {
            continue;
        }
        std::string const parent = dir.substr(0, slash);
        std::size_t const parentSlash = parent.find_last_of("\\/");
        std::string const parentName = parentSlash == std::string::npos ? parent : parent.substr(parentSlash + 1);
        bool const underUsers = IEquals(parentName, "Users");
        std::string const leaf = dir.substr(slash + 1);
        keep(users_, leaf);
        // Its spellings: as is, with forward slashes, JSON-escaped.
        std::string forward = dir;
        std::replace(forward.begin(), forward.end(), '\\', '/');
        std::string escaped;
        for (char c : dir) {
            escaped += c == '\\' ? std::string("\\\\") : std::string(1, c);
        }
        for (std::string const& spelling : {dir, forward, escaped}) {
            std::string replacement = "<profile>";
            if (underUsers) {
                replacement = spelling.substr(0, spelling.size() - leaf.size()) + "<user>";
            }
            if (std::none_of(paths_.begin(), paths_.end(), [&](auto const& p) { return p.first == spelling; })) {
                paths_.emplace_back(spelling, replacement);
            }
        }
    }
    // Longest first, so a longer spelling isn't half-replaced by a shorter one.
    std::sort(paths_.begin(), paths_.end(), [](auto const& a, auto const& b) { return a.first.size() > b.first.size(); });
    std::sort(users_.begin(), users_.end(), [](auto const& a, auto const& b) { return a.size() > b.size(); });
}

Redactor Redactor::ForThisMachine() {
    std::vector<std::string> computers{EnvUtf8(L"COMPUTERNAME")};
    wchar_t host[256]{};
    DWORD size = 256;
    if (GetComputerNameExW(ComputerNameDnsHostname, host, &size)) {
        computers.push_back(detect::Utf8(host));
    }
    std::vector<std::string> profiles{EnvUtf8(L"USERPROFILE")};
    if (auto const profile = KnownFolder(FOLDERID_Profile)) {
        profiles.push_back(detect::Utf8(profile->wstring()));
    }
    return Redactor({EnvUtf8(L"USERNAME")}, computers, profiles);
}

std::string Redactor::Apply(std::string_view text, Redactions* counts) const {
    Redactions local;
    std::string out(text);
    for (auto const& [spelling, replacement] : paths_) {
        out = ReplaceWord(out, spelling, replacement, false, local.profilePath);
    }
    for (std::string const& user : users_) {
        out = ReplaceWord(out, user, "<user>", true, local.userName);
    }
    for (std::string const& computer : computers_) {
        out = ReplaceWord(out, computer, "<computer>", true, local.computerName);
    }
    out = RedactEmails(out, local.email);
    if (counts != nullptr) {
        *counts += local;
    }
    return out;
}

// ---- What went wrong ----------------------------------------------------------------------------------------------
std::string ProblemKey(std::string_view text) {
    std::string key;
    key.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        char const c = text[i];
        bool const hex = c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X');
        if (hex) {
            std::size_t j = i + 2;
            while (j < text.size() && std::isxdigit(static_cast<unsigned char>(text[j])) != 0) {
                ++j;
            }
            if (i > 0 && text[i - 1] == '+') {
                key.append(text.substr(i, j - i));  // a module offset: where it happened
            } else {
                key += "0x#";
            }
            i = j;
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) != 0 || text[i] == '.')) {
                ++i;
            }
            key += '#';
        } else {
            key += c;
            ++i;
        }
    }
    return key;
}

namespace {

struct Collector {
    std::vector<Problem> items;
    std::unordered_map<std::string, std::size_t> index;
    void Add(std::string_view level, std::string_view time, std::string_view text) {
        std::string key = std::string(level) + '\x1f' + ProblemKey(text);
        auto const it = index.find(key);
        if (it != index.end()) {
            ++items[it->second].count;
            return;
        }
        index.emplace(std::move(key), items.size());
        items.push_back({std::string(level), std::string(time), Cut(Trim(text), 300), 1});
    }
    Problems Finish(std::size_t maxPerLevel) {
        Problems p;
        p.distinct = static_cast<int>(items.size());
        std::stable_sort(items.begin(), items.end(), [](Problem const& a, Problem const& b) {
            if (a.level != b.level) {
                return a.level == "error";
            }
            return a.count > b.count;
        });
        std::size_t errors = 0, warnings = 0;
        for (Problem& item : items) {
            std::size_t& n = item.level == "error" ? errors : warnings;
            if (n < maxPerLevel) {
                p.items.push_back(std::move(item));
            } else {
                p.capped = true;
            }
            ++n;
        }
        return p;
    }
};

bool HasWord(std::string_view line, std::string_view word) {
    for (std::size_t at = IFind(line, word); at != std::string_view::npos; at = IFind(line, word, at + 1)) {
        std::size_t const end = at + word.size();
        if ((at == 0 || !std::isalpha(static_cast<unsigned char>(line[at - 1]))) &&
            (end == line.size() || !std::isalpha(static_cast<unsigned char>(line[end])))) {
            return true;
        }
    }
    return false;
}

} // namespace

Problems ExtractProblems(std::string_view modLog, std::size_t maxPerLevel) {
    Collector c;
    int errors = 0, warnings = 0;
    Lines(modLog, [&](std::string_view line) {
        if (line.size() < 4 || line[0] != '[') {
            return;
        }
        std::size_t const close = line.find(']');
        if (close == std::string_view::npos || close + 2 >= line.size() || line.substr(close + 1, 2) != " [") {
            return;
        }
        std::size_t const levelEnd = line.find(']', close + 3);
        if (levelEnd == std::string_view::npos) {
            return;
        }
        std::string_view const level = line.substr(close + 3, levelEnd - close - 3);
        bool const error = level == "error";
        bool const warn = level == "warn" || level == "warning";
        if (!error && !warn) {
            return;
        }
        (error ? errors : warnings)++;
        std::string_view const text = levelEnd + 1 < line.size() ? line.substr(levelEnd + 1) : std::string_view();
        c.Add(error ? "error" : "warn", line.substr(1, close - 1), text);
    });
    Problems p = c.Finish(maxPerLevel);
    p.errors = errors;
    p.warnings = warnings;
    return p;
}

Problems ExtractRendererProblems(std::string_view halcyonLog, std::size_t max) {
    Collector c;
    int count = 0;
    Lines(halcyonLog, [&](std::string_view line) {
        std::size_t const bar = line.find(" | ");
        if (bar == std::string_view::npos || bar > 30) {
            return;
        }
        std::string_view const text = line.substr(bar + 3);
        if (!HasWord(text, "error") && !HasWord(text, "failed") && !HasWord(text, "exception")) {
            return;
        }
        ++count;
        std::string_view const stamp = line.substr(0, bar);
        c.Add("error", stamp.size() > 11 ? stamp.substr(11) : stamp, text);
    });
    Problems p = c.Finish(max);
    p.errors = count;
    return p;
}

std::string_view LastHalcyonSession(std::string_view log) {
    std::size_t const banner = log.rfind("(Halcyon) inside");
    if (banner == std::string_view::npos) {
        // No start banner in what was read: from the last VR session's start.
        std::size_t const session = log.rfind("[XR] starting OpenXR session thread");
        if (session == std::string_view::npos) {
            return log;
        }
        std::size_t const lineStart = log.rfind('\n', session);
        return log.substr(lineStart == std::string_view::npos ? 0 : lineStart + 1);
    }
    // Back to the "=====" line before the banner's line.
    std::size_t lineStart = log.rfind('\n', banner);
    lineStart = lineStart == std::string_view::npos ? 0 : lineStart;
    if (lineStart > 0) {
        std::size_t const previous = log.rfind('\n', lineStart - 1);
        std::size_t const start = previous == std::string_view::npos ? 0 : previous + 1;
        if (log.substr(start, lineStart - start).find("=====") != std::string_view::npos) {
            return log.substr(start);
        }
        return log.substr(lineStart + 1);
    }
    return log;
}

LogFacts ReadLogFacts(std::string_view modLog, std::string_view halcyonLog) {
    LogFacts f;
    Lines(modLog, [&](std::string_view line) {
        if (std::size_t const at = line.find("] MEVR "); at != std::string_view::npos && line.find(" attaching to ") != std::string_view::npos) {
            std::string_view rest = line.substr(at + 7);
            std::size_t const space = rest.find(' ');
            f.modVersion = std::string(rest.substr(0, space));
            f.modBuild = Between(rest, "(", ")");
            std::size_t const commit = f.modBuild.find("commit ");
            f.commit = commit == std::string::npos ? std::string() : f.modBuild.substr(commit + 7);
            if (std::size_t const comma = f.commit.find_first_of(",;) "); comma != std::string::npos) {
                f.commit.resize(comma);
            }
        } else if (std::size_t const g = line.find("] game version "); g != std::string_view::npos) {
            std::string_view v = line.substr(g + 15);
            f.gameVersion = std::string(Trim(v.substr(0, v.find(';'))));
        } else if (line.find("colour: headset: runtime '") != std::string_view::npos) {
            f.xrRuntime = Between(line, "runtime '", "'");
            f.headsetSystem = Between(line, "system '", "'");
        } else if (std::size_t const h = line.find("calibration: headset system \""); h != std::string_view::npos && f.headsetSystem.empty()) {
            f.headsetSystem = Between(line, "system \"", "\"");
        } else if (std::size_t const p = line.find("colour: profile "); p != std::string_view::npos) {
            std::string_view v = line.substr(p + 16);
            std::size_t const end = v.find("]:");
            f.colourProfile = end == std::string_view::npos ? Cut(v, 120) : std::string(v.substr(0, end + 1));
        } else if (std::size_t const ip = line.find("the runtime's interaction profile: "); ip != std::string_view::npos) {
            f.interactionProfile = std::string(Trim(line.substr(ip + 35)));
            if (std::size_t const paren = f.interactionProfile.find(" (xrGetCurrent"); paren != std::string::npos) {
                f.interactionProfile.resize(paren);
            }
        } else if (std::size_t const cp = line.find("controllers' interaction profile "); cp != std::string_view::npos && f.interactionProfile.empty()) {
            std::string const v(Trim(line.substr(cp + 33)));
            if (v.find("none yet") == std::string::npos) {
                f.interactionProfile = v;
            }
        }
    });
    std::string halcyonRuntime, halcyonSystem;
    Lines(halcyonLog, [&](std::string_view line) {
        if (line.find("[XR] recommended eye image ") != std::string_view::npos) {
            std::string_view v = line.substr(line.find("[XR] recommended eye image ") + 27);
            f.eyeSize = std::string(v.substr(0, v.find(';')));
            f.renderSize = Between(line, "game render source ", ";");
        } else if (line.find("[XRRUNTIME] name='") != std::string_view::npos) {
            halcyonRuntime = Between(line, "name='", "'");
        } else if (line.find("[MEVR-COLOUR] headset: runtime '") != std::string_view::npos) {
            halcyonSystem = Between(line, "system '", "'");
        }
    });
    if (f.xrRuntime.empty()) {
        f.xrRuntime = halcyonRuntime;
    }
    if (f.headsetSystem.empty()) {
        f.headsetSystem = halcyonSystem;
    }
    return f;
}

// ---- The crash ----------------------------------------------------------------------------------------------------
Crash ParseCrash(std::string_view breadcrumbs, std::size_t maxFrames) {
    Crash c;
    enum class Section { None, Native, Script } section = Section::None;
    Lines(breadcrumbs, [&](std::string_view line) {
        std::string_view const t = Trim(line);
        if (t.empty()) {
            section = Section::None;
            return;
        }
        if (c.when.empty() && t.size() >= 23 && t.rfind("20", 0) == 0 && t[4] == '-') {
            c.when = std::string(t.substr(0, 23));
        } else if (t.rfind("exception ", 0) == 0 && c.exception.empty()) {
            c.exception = std::string(t.substr(10));
            std::size_t const at = c.exception.find(" at ");
            if (at != std::string::npos) {
                std::string loc = c.exception.substr(at + 4);
                loc = loc.substr(0, loc.find_first_of(", "));
                c.location = loc;
            }
        } else if (t.rfind("faulting thread:", 0) == 0) {
            c.thread = std::string(t);
        } else if (t.rfind("native stack", 0) == 0) {
            section = Section::Native;
        } else if (t.rfind("script frames", 0) == 0) {
            section = Section::Script;
        } else if (section == Section::Native && t.front() == '[') {
            if (c.nativeFrames.size() < maxFrames) {
                std::string_view frame = t.substr(t.find(']') + 1);
                frame = Trim(frame.substr(0, frame.find("(rsp")));
                c.nativeFrames.emplace_back(frame);
            }
        } else if (section == Section::Script) {
            if (c.scriptFrames.size() < maxFrames) {
                c.scriptFrames.push_back(Cut(t, 200));
            }
        } else if (section == Section::Native) {
            section = Section::None;
        }
    });
    c.present = !c.exception.empty();
    return c;
}

// ---- Settings -----------------------------------------------------------------------------------------------------
std::vector<IniDefault> const& ModIniDefaults() {
    // The code's defaults (plugin/MainMenuSettings, game/*, plugin/Colour), as the menu shows them.
    static std::vector<IniDefault> const defaults = {
        {"Comfort", "turn", "game"}, {"Comfort", "snapDeg", "45"}, {"Comfort", "vignette", "0"}, {"Comfort", "vignetteStrength", "0.6"},
        {"Comfort", "vignetteWhen", "turn"}, {"Comfort", "walk", "head"},
        {"Hands", "physicalCrouch", "1"}, {"Hands", "aimAssist", "0"}, {"Hands", "recoilHaptic", "0.5"}, {"Hands", "exploreStance", "0"},
        {"Hands", "constantEye", "1"}, {"Hands", "drawRate", "6"}, {"Hands", "handPitch", "0"}, {"Hands", "handYaw", "0"},
        {"Hands", "handRoll", "0"},
        {"Holsters", "enabled", "1"}, {"Holsters", "gripHolds", "1"}, {"Holsters", "hapticAmplitude", "0.5"},
        {"Controls", "leftTrigger", "lb"},
        {"Throwables", "enabled", "1"}, {"Throwables", "melee", "1"},
        {"Sights", "pistol", "dot"}, {"Sights", "assaultRifle", "dot"}, {"Sights", "sniperRifle", "scope 4"}, {"Sights", "assist", "1"},
        {"Sights", "converge", "1"},
        {"Squad", "style", "native"},
        {"Colour", "profile", "auto"}, {"Colour", "compare", "0"}, {"Colour", "dither", "0.5"},
        {"Calibration", "use", "1"},
        {"Upscale", "method", "off"},
        {"Menu", "pause", "1"}, {"Menu", "distanceM", "1.3"}, {"Menu", "widthM", "1.45"}, {"Menu", "assets", ""},
    };
    return defaults;
}

std::vector<SettingDiff> DiffModIni(std::string_view text) {
    std::vector<SettingDiff> out;
    for (IniDefault const& d : ModIniDefaults()) {
        auto const v = ini::Get(text, d.section, d.key);
        if (v && !SameValue(*v, d.value)) {
            out.push_back({"LE1MEVR.ini", d.section, d.key, *v, d.value});
        }
    }
    return out;
}

std::vector<SettingDiff> DiffHalcyonIni(std::string_view text, std::string_view templateText) {
    std::vector<SettingDiff> out;
    std::string templateSection;
    std::vector<std::string> keys;
    bool inFirst = false;
    Lines(templateText, [&](std::string_view line) {
        std::string_view const t = Trim(line);
        if (t.empty() || t.front() == ';' || t.front() == '#') {
            return;
        }
        if (t.front() == '[') {
            std::string_view const name = t.substr(1, t.find(']') - 1);
            inFirst = templateSection.empty() && name.rfind("profile:", 0) == 0;
            if (inFirst) {
                templateSection = std::string(name);
            }
            return;
        }
        if (inFirst) {
            std::size_t const eq = t.find('=');
            if (eq != std::string_view::npos) {
                keys.emplace_back(Trim(t.substr(0, eq)));
            }
        }
    });
    std::string const active = "profile:" + ini::Get(text, "active", "profile").value_or("Default");
    for (std::string const& key : keys) {
        std::string const def = ini::Get(templateText, templateSection, key).value_or("");
        auto const v = ini::Get(text, active, key);
        if (!v) {
            out.push_back({"MELEVR.ini", active, key, "(not in the file: Halcyon's code default)", def});
        } else if (!SameValue(*v, def)) {
            out.push_back({"MELEVR.ini", active, key, *v, def});
        }
    }
    return out;
}

// ---- REPORT.md ----------------------------------------------------------------------------------------------------
std::string Headline(Bundle const& b) {
    if (b.crash.present && !b.crash.stale) {
        std::string e = b.crash.exception;
        if (std::size_t const comma = e.find(','); comma != std::string::npos) {
            e.resize(comma);
        }
        return "Crash: " + e;
    }
    for (Problem const& p : b.problems.items) {
        return (p.level == "error" ? "Error: " : "Warning: ") + Cut(p.text, 160);
    }
    return "No errors or warnings in the last session's log";
}

namespace {

std::string Or(std::string const& s, char const* fallback = "unknown") {
    return s.empty() ? std::string(fallback) : s;
}

void ProblemBlock(std::string& md, Problems const& p, std::size_t limit) {
    md += "```text\n";
    std::size_t shown = 0;
    for (Problem const& item : p.items) {
        if (shown++ >= limit) {
            break;
        }
        md += std::format("[{}] x{} (first {}) {}\n", item.level, item.count, item.time, Fenced(item.text));
    }
    md += "```\n";
    if (p.capped || p.items.size() > limit) {
        md += std::format("({} distinct lines in all; the full log is in the zip.)\n", p.distinct);
    }
}

std::string BuildWith(Bundle const& b, std::size_t problemLimit, std::size_t diffLimit) {
    Bundle const& x = b;
    std::string md;
    md += "# MEVR problem report\n\n";
    md += std::format(
        "MEVR is a free, open-source VR mod for Mass Effect Legendary Edition (Mass Effect 1 first): a C++ ASI plugin the game "
        "loads at start that renders the game in stereo through OpenXR (Halcyon's renderer, built in), with hand-tracked aiming, arm "
        "IK, body holsters, a VR menu and comfort options. It runs only on MassEffect1.exe 2.0.0.48602. Repository: {}. "
        "Troubleshooting guide: {}. Issues: {}.\n\n",
        kRepoUrl, kTroubleshootingUrl, kIssuesUrl);
    md += std::format(
        "This report was made by MEVR Setup {} on the player's PC. Personal details were removed before anything was written "
        "({} redactions: Windows user name, computer name, profile folder, e-mail addresses). Nothing was uploaded: the player "
        "decides what to share.\n\n",
        Or(x.system.setupVersion), x.redactions.Total());

    md += "## System\n\n| | |\n|---|---|\n";
    auto row = [&](char const* name, std::string const& value) { md += std::format("| {} | {} |\n", name, Or(value)); };
    row("MEVR running (from the log)", x.system.runningMod);
    row("MEVR installed (install record)", x.system.installedVersion);
    row("Game (MassEffect1.exe)", x.system.gameVersion + (x.system.store.empty() ? "" : ", " + x.system.store));
    row("Windows", x.system.windows);
    row("GPU", x.system.gpu);
    row("GPU driver", x.system.gpuDriver);
    row("Active OpenXR runtime", x.system.xrRuntime);
    row("Headset (runtime's system name)", x.system.headset);
    row("Controllers (interaction profile)", x.system.controllers);
    row("Colour profile", x.system.colourProfile);
    row("Render resolution", x.system.resolution);
    md += "\n";

    md += "## What went wrong\n\n";
    if (x.crash.present) {
        md += x.crash.stale ? "### An earlier crash (before the last two sessions: maybe not this problem)\n\n" : "### The game crashed\n\n";
        md += std::format("- When: {}\n- Exception: {}\n- Faulting module+offset: {}\n", Or(x.crash.when), x.crash.exception, Or(x.crash.location));
        if (!x.crash.thread.empty()) {
            md += "- " + x.crash.thread + "\n";
        }
        md += "\nTop native frames (innermost first):\n\n```text\n";
        for (std::string const& f : x.crash.nativeFrames) {
            md += Fenced(f) + "\n";
        }
        md += "```\n\nScript frames (UnrealScript on the faulting thread):\n\n```text\n";
        for (std::string const& f : x.crash.scriptFrames) {
            md += Fenced(f) + "\n";
        }
        if (x.crash.scriptFrames.empty()) {
            md += "(none)\n";
        }
        md += "```\n\nThe full breadcrumbs (the last ProcessEvents, script handler calls and mod writes) are in logs/crash-breadcrumbs.txt.\n\n";
    }
    md += std::format("### Last session (logs/LE1MEVR.log): {} errors, {} warnings\n\n", x.problems.errors, x.problems.warnings);
    if (x.problems.items.empty()) {
        md += "None.\n\n";
    } else {
        ProblemBlock(md, x.problems, problemLimit);
        md += "\n";
    }
    if (!x.previousProblems.items.empty()) {
        md += std::format("### The session before (logs/LE1MEVR.prev.log): {} errors, {} warnings\n\n", x.previousProblems.errors,
            x.previousProblems.warnings);
        ProblemBlock(md, x.previousProblems, std::max<std::size_t>(problemLimit / 3, 3));
        md += "\n";
    }
    if (!x.rendererProblems.items.empty()) {
        md += std::format("### Renderer log (Halcyon's, logs/MELEVR_Log.txt): {} lines with error, failed or exception\n\n", x.rendererProblems.errors);
        ProblemBlock(md, x.rendererProblems, std::max<std::size_t>(problemLimit / 3, 3));
        md += "\n";
    }

    md += "## Settings that differ from the defaults\n\n";
    if (x.diffs.empty()) {
        md += "None of the keys we compare (or the settings files weren't there).\n\n";
    } else {
        std::size_t shown = 0;
        for (SettingDiff const& d : x.diffs) {
            if (shown++ >= diffLimit) {
                md += std::format("- ... and {} more (see settings/ in the zip)\n", x.diffs.size() - diffLimit);
                break;
            }
            md += std::format("- {} [{}] {} = {} (default {})\n", d.file, d.section, d.key, d.value, d.defaultValue.empty() ? "empty" : d.defaultValue);
        }
        md += "\nOnly the keys our templates name are compared: LE1MEVR.ini's player settings, and MELEVR.ini's active profile against the "
              "release file the setup installs (the resolution keys differ by design).\n\n";
    }

    md += "## Files in this zip\n\n";
    md += "- REPORT.md: this summary\n";
    for (File const& f : x.files) {
        md += std::format("- {} ({}{}{})\n", f.name, SizeText(f.bytes.size()), f.note.empty() ? "" : "; ", f.note);
    }
    if (!x.missing.empty()) {
        md += "\nNot found on this PC (not an error by itself): ";
        for (std::size_t i = 0; i < x.missing.size(); ++i) {
            md += (i == 0 ? "" : ", ") + x.missing[i];
        }
        md += ".\n";
    }
    md += "\nFor an assistant reading the zip: logs/LE1MEVR.log is the mod's own log of this session (lines \"[time] [level] text\"); "
          "logs/MELEVR_Log.txt is the renderer's (Halcyon's) log of its last session; logs/MEVRLoader.log (or LEBinkProxy's bink2w64_proxy.log) is the ASI loader's; "
          "perf/ (if included) is a frame-timing capture: tools/perf-probe.py in the repository reads it; "
          "system/folders.txt lists the game's Win64 and ASI folders (other mods' DLLs there can conflict).\n\n";

    md += "## Paste this into your AI assistant\n\n```text\n";
    md += std::format(
        "I'm reporting a problem with MEVR, a VR mod for Mass Effect Legendary Edition. Here is my report bundle. Read the "
        "troubleshooting guide at {}, tell me the likely cause and anything I can try, then help me write a GitHub issue with: what "
        "I did, what I expected, what happened, and this summary. Ask me what I was doing when it happened.\n",
        kTroubleshootingUrl);
    md += "```\n";
    return md;
}

} // namespace

std::string BuildReportMd(Bundle const& bundle) {
    std::size_t problems = 25, diffs = 60;
    std::string md = BuildWith(bundle, problems, diffs);
    while (md.size() > kReportMdBudget && (problems > 3 || diffs > 10)) {
        problems = std::max<std::size_t>(problems / 2, 3);
        diffs = std::max<std::size_t>(diffs / 2, 10);
        md = BuildWith(bundle, problems, diffs);
    }
    return md;
}

std::string IssueSummary(Bundle const& b) {
    System const& s = b.system;
    std::string out;
    out += "MEVR: " + Or(s.runningMod.empty() ? s.installedVersion : s.runningMod) + "\n";
    out += "Game: " + Or(s.gameVersion) + (s.store.empty() ? "" : " (" + s.store + ")") + "\n";
    out += "Windows: " + Or(s.windows) + "\n";
    out += "GPU: " + Or(s.gpu) + ", driver " + Or(s.gpuDriver) + "\n";
    out += "OpenXR runtime: " + Or(s.xrRuntime) + "\n";
    out += "Headset: " + Or(s.headset) + "\n";
    out += "Controllers: " + Or(s.controllers) + "\n";
    out += "Problem: " + Headline(b) + "\n";
    return out;
}

std::string UrlEncode(std::string_view text) {
    std::string out;
    for (char ch : text) {
        auto const c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~') {
            out += ch;
        } else {
            out += std::format("%{:02X}", c);
        }
    }
    return out;
}

std::string IssueUrl(Bundle const& b) {
    std::string title = Headline(b);
    if (title.rfind("No errors", 0) == 0) {
        title = "Problem report";
    }
    title = Cut(title, 90);
    std::string const version = b.system.runningMod.empty() ? b.system.installedVersion : b.system.runningMod;
    std::string summary = IssueSummary(b);
    std::string url;
    for (;;) {
        url = std::string(kNewIssueUrl) + "?template=bug.yml&title=" + UrlEncode("[Bug] " + title) + "&version=" + UrlEncode(version) +
              "&summary=" + UrlEncode(summary);
        if (url.size() < 7000 || summary.size() < 200) {
            break;
        }
        summary = Cut(summary, summary.size() / 2);
    }
    return url;
}

std::string SizeText(std::uint64_t bytes) {
    if (bytes < 1024) {
        return std::format("{} B", bytes);
    }
    if (bytes < 1024 * 1024) {
        return std::format("{:.1f} KB", static_cast<double>(bytes) / 1024.0);
    }
    return MbText(bytes);
}

char const* BuildCommit() {
    return MEVR_GIT_COMMIT;
}

std::string StoreFor(fs::path const& gameRoot) {
    if (gameRoot.empty()) {
        return {};
    }
    std::wstring lower = gameRoot.wstring();
    std::transform(lower.begin(), lower.end(), lower.begin(), ::towlower);
    std::error_code ec;
    if (lower.find(L"\\steamapps\\common\\") != std::wstring::npos ||
        fs::exists(Layout{gameRoot}.Full(paths::kSteamAppId), ec)) {
        return "Steam";
    }
    for (detect::GameInstall const& install : detect::FindInstalls()) {
        if (fs::equivalent(install.root, gameRoot, ec)) {
            return install.source;
        }
    }
    return "a folder picked by hand";
}

// ---- Collect ------------------------------------------------------------------------------------------------------
Bundle Collect(Options const& options) {
    return Collect(options, Redactor::ForThisMachine());
}

Bundle Collect(Options const& options, Redactor const& redactor) {
    Bundle b;
    b.snapshot = options.snapshot;
    fs::path const local = options.localData.empty() ? LocalDataDir() : options.localData;
    fs::path const halcyonLog = options.halcyonLog.empty() ? detect::HalcyonLogPath() : options.halcyonLog;
    Layout const layout{options.gameRoot};
    bool const game = !options.gameRoot.empty();
    std::error_code ec;

    auto addText = [&](std::string name, std::string text, std::uint64_t original, std::string note) -> File& {
        File f;
        f.name = std::move(name);
        Redactions r;
        f.bytes = redactor.Apply(text, &r);
        f.redactions = r.Total();
        b.redactions += r;
        f.originalSize = original;
        f.note = std::move(note);
        b.files.push_back(std::move(f));
        return b.files.back();
    };
    // A text file as is, or its first and last part when it's big.
    auto addFile = [&](std::string const& name, std::vector<fs::path> const& candidates, std::size_t head, std::size_t tail) -> bool {
        for (fs::path const& path : candidates) {
            if (path.empty()) {
                continue;
            }
            Excerpt e = ReadText(path);
            if (!e.found) {
                continue;
            }
            if (e.locked) {
                b.missing.push_back(name + " (" + e.note + ")");
                return false;
            }
            std::string note;
            std::string text = head + tail > 0 ? CutMiddle(e.text, head, tail, &note) : std::move(e.text);
            addText(name, std::move(text), e.size, note);
            return true;
        }
        b.missing.push_back(name);
        return false;
    };

    // ---- Logs.
    fs::path const logs = game ? layout.Win64() / L"Logs" : fs::path();
    addFile("logs/LE1MEVR.log", {game ? logs / L"LE1MEVR.log" : fs::path(), local / L"LE1MEVR.log"}, 1u << 20, 3u << 20);
    addFile("logs/LE1MEVR.prev.log", {game ? logs / L"LE1MEVR.prev.log" : fs::path(), local / L"LE1MEVR.prev.log"}, 1u << 20, 2u << 20);
    std::string halcyonSession;
    {
        Excerpt e = ReadText(halcyonLog, 48ull << 20);
        if (e.found && !e.locked) {
            std::string_view const session = LastHalcyonSession(e.text);
            std::string note;
            std::string text = CutMiddle(session, 512u << 10, 5u << 19, &note);
            if (e.size > session.size()) {
                note = std::format("trimmed to its last session{}{}; the file holds {} from every session", note.empty() ? "" : ", ", note, MbText(e.size));
            }
            halcyonSession = addText("logs/MELEVR_Log.txt", std::move(text), e.size, note).bytes;
        } else {
            b.missing.push_back("logs/MELEVR_Log.txt");
        }
    }
    // The ASI loader's log: ours, or ME3Tweaks' LEBinkProxy's (whichever is there; both if both are).
    if (game) {
        std::error_code exists;
        bool const ours = fs::exists(logs / L"MEVRLoader.log", exists);
        bool const proxy = fs::exists(logs / L"bink2w64_proxy.log", exists);
        if (ours || !proxy) {
            addFile("logs/MEVRLoader.log", {logs / L"MEVRLoader.log"}, 0, 0);
        }
        if (proxy) {
            addFile("logs/bink2w64_proxy.log", {logs / L"bink2w64_proxy.log"}, 0, 0);
        }
    } else {
        b.missing.push_back("logs/MEVRLoader.log");
    }
    if (addFile("logs/crash-breadcrumbs.txt", {local / L"crash-breadcrumbs.txt"}, 0, 0)) {
        b.files.back().note = "written " + FileTimeText(local / L"crash-breadcrumbs.txt");
    }
    if (addFile("logs/crash-breadcrumbs.prev.txt", {local / L"crash-breadcrumbs.prev.txt"}, 0, 0)) {
        b.files.back().note = "written " + FileTimeText(local / L"crash-breadcrumbs.prev.txt");
    }
    addFile("logs/reflect-check.json", {local / L"reflect-check.json"}, 0, 0);

    // ---- Settings.
    if (game) {
        addFile("settings/LE1MEVR.ini", {layout.Full(paths::kModIni)}, 0, 0);
        addFile("settings/MELEVR.ini", {layout.Full(paths::kHalcyonIni)}, 0, 0);
        addFile("settings/MEVR-install.json", {layout.ManifestFile()}, 0, 0);
        addText("system/folders.txt", Listing(layout), 0, "file names and sizes");
    }

    // ---- The snapshot.
    if (options.snapshot) {
        fs::path newest;
        fs::file_time_type newestTime{};
        for (auto const& entry : fs::directory_iterator(local / L"snapshots", ec)) {
            if (entry.path().extension() == L".json") {
                auto const t = entry.last_write_time(ec);
                if (newest.empty() || t > newestTime) {
                    newest = entry.path();
                    newestTime = t;
                }
            }
        }
        if (newest.empty()) {
            b.missing.push_back("snapshot (none taken yet)");
        } else {
            std::wstring const stem = newest.stem().wstring();
            addFile("snapshot/" + detect::Utf8(newest.filename().wstring()), {newest}, 0, 0);
            for (wchar_t const* eye : {L"-left.png", L"-right.png"}) {
                fs::path const png = newest.parent_path() / (stem + eye);
                if (auto bytes = ReadFile(png)) {
                    File f;
                    f.name = "snapshot/" + detect::Utf8(png.filename().wstring());
                    f.originalSize = bytes->size();
                    f.bytes = std::move(*bytes);
                    f.text = false;
                    f.note = "what that eye showed";
                    b.files.push_back(std::move(f));
                }
            }
        }
    }

    // ---- The newest frame-timing capture (plugin/PerfProbe: <stamp>-probe.json, -frames.csv, -ticks.csv).
    if (options.perf) {
        fs::path newest;
        fs::file_time_type newestTime{};
        for (auto const& entry : fs::directory_iterator(local / L"perf", ec)) {
            std::wstring const name = entry.path().filename().wstring();
            if (name.size() > 11 && name.compare(name.size() - 11, 11, L"-probe.json") == 0) {
                auto const t = entry.last_write_time(ec);
                if (newest.empty() || t > newestTime) {
                    newest = entry.path();
                    newestTime = t;
                }
            }
        }
        if (newest.empty()) {
            b.missing.push_back("perf (no frame-timing capture yet)");
        } else {
            std::wstring const name = newest.filename().wstring();
            std::wstring const stem = name.substr(0, name.size() - 11);
            for (wchar_t const* part : {L"-probe.json", L"-frames.csv", L"-ticks.csv"}) {
                fs::path const file = newest.parent_path() / (stem + part);
                if (addFile("perf/" + detect::Utf8(file.filename().wstring()), {file}, 0, 0)) {
                    b.files.back().note = "frame timing, written " + FileTimeText(file);
                }
            }
        }
    }

    // ---- What it says. (The pointers above may have moved with the vector: find the files by name.)
    auto text = [&](char const* name) -> std::string_view {
        for (File const& f : b.files) {
            if (f.name == name) {
                return f.bytes;
            }
        }
        return {};
    };
    b.problems = ExtractProblems(text("logs/LE1MEVR.log"));
    b.previousProblems = ExtractProblems(text("logs/LE1MEVR.prev.log"), 8);
    b.rendererProblems = ExtractRendererProblems(halcyonSession);
    b.crash = ParseCrash(text("logs/crash-breadcrumbs.txt"));
    if (b.crash.present) {
        // The crash's session wrote its log (LE1MEVR.prev.log once the game started again) until it crashed: breadcrumbs
        // older than that log's end are from a session before the last two.
        auto const crashTime = report_marks::Modified(local / L"crash-breadcrumbs.txt");
        auto const prevTime = report_marks::Modified(game ? logs / L"LE1MEVR.prev.log" : local / L"LE1MEVR.prev.log");
        using namespace std::chrono_literals;
        b.crash.stale = crashTime && prevTime && *crashTime + 60s < *prevTime;
    }
    b.facts = ReadLogFacts(text("logs/LE1MEVR.log").empty() ? text("logs/LE1MEVR.prev.log") : text("logs/LE1MEVR.log"), halcyonSession);
    b.diffs = DiffModIni(text("settings/LE1MEVR.ini"));
    for (SettingDiff& d : DiffHalcyonIni(text("settings/MELEVR.ini"), ini::HalcyonReleaseIni())) {
        if (!text("settings/MELEVR.ini").empty()) {
            b.diffs.push_back(std::move(d));
        }
    }

    // ---- The system.
    System& s = b.system;
    s.setupVersion = std::format("{} (commit {})", updates::kVersion, BuildCommit());
    if (auto const value = json::Parse(text("settings/MEVR-install.json"))) {
        s.installedVersion = value->String("modVersion");
        if (std::string const at = value->String("installedAt"); !at.empty()) {
            s.installedVersion += ", installed " + at;
        }
        std::string const loader = value->String("loader");
        if (!loader.empty()) {
            s.installedVersion += ", loader " + loader;
        }
    }
    LogFacts const& f = b.facts;
    if (!f.modVersion.empty()) {
        s.runningMod = f.modVersion + (f.modBuild.empty() ? "" : " (" + f.modBuild + ")");
    }
    if (game) {
        if (auto const v = detect::FileVersion(layout.Full(paths::kExe))) {
            s.gameVersion = v->ToString() + (detect::CheckVersion(v) == detect::VersionCheck::Supported ? " (supported)" : " (NOT the supported 2.0.0.48602)");
        }
        s.store = options.store.empty() ? StoreFor(options.gameRoot) : options.store;
    }
    s.windows = WindowsVersion();
    Gpu(s);
    detect::XrRuntime const xr = detect::ActiveRuntime();
    s.xrRuntime = xr.kind.empty() ? std::string("none active") : xr.kind + (xr.name.empty() || xr.name == xr.kind ? "" : " (" + xr.name + ")");
    s.headset = f.headsetSystem.empty() ? std::string() : f.headsetSystem + (f.xrRuntime.empty() ? "" : " on " + f.xrRuntime);
    s.controllers = f.interactionProfile;
    s.colourProfile = f.colourProfile;
    if (!f.eyeSize.empty()) {
        s.resolution = "headset " + f.eyeSize + " per eye" + (f.renderSize.empty() ? "" : "; rendering " + f.renderSize);
    } else if (std::string_view const melevr = text("settings/MELEVR.ini"); !melevr.empty()) {
        std::string const active = "profile:" + ini::Get(melevr, "active", "profile").value_or("Default");
        auto const x = ini::Get(melevr, active, "sfr2ResX");
        auto const y = ini::Get(melevr, active, "sfr2ResY");
        if (x && y) {
            s.resolution = "stereo target " + *x + "x" + *y + " (MELEVR.ini; no VR session in the log)";
        }
    }
    // The summary's own fields pass the redactor too (a GPU or runtime name never holds a user name, but cheap to be sure).
    for (std::string* field : {&s.runningMod, &s.installedVersion, &s.gameVersion, &s.store, &s.windows, &s.gpu, &s.gpuDriver, &s.xrRuntime,
             &s.headset, &s.controllers, &s.colourProfile, &s.resolution}) {
        *field = redactor.Apply(*field, &b.redactions);
    }
    return b;
}

Result Write(Bundle& bundle, Options const& options) {
    Result r;
    fs::path const local = options.localData.empty() ? LocalDataDir() : options.localData;
    std::time_t const t = options.now != 0 ? static_cast<std::time_t>(options.now) : std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    std::string const stamp = std::format("{:04}-{:02}-{:02}-{:02}{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min);

    Redactor const redactor = Redactor::ForThisMachine();
    bundle.reportMd = redactor.Apply(BuildReportMd(bundle), &bundle.redactions);
    zip::Writer writer(static_cast<std::int64_t>(t));
    writer.Add("REPORT.md", bundle.reportMd);
    for (File const& f : bundle.files) {
        writer.Add(f.name, f.bytes);
    }
    std::string const& archive = writer.Finish();

    std::vector<std::pair<fs::path, std::string>> folders;
    if (!options.outputDir.empty()) {
        folders.emplace_back(options.outputDir, detect::Utf8(options.outputDir.wstring()));
    } else {
        if (auto const desktop = KnownFolder(FOLDERID_Desktop)) {
            folders.emplace_back(*desktop, "Desktop");
        }
        if (auto const documents = KnownFolder(FOLDERID_Documents)) {
            folders.emplace_back(*documents, "Documents");
        }
        if (!local.empty()) {
            folders.emplace_back(local / L"reports", detect::Utf8((local / L"reports").wstring()));
        }
    }
    std::error_code ec;
    for (auto const& [folder, label] : folders) {
        fs::create_directories(folder, ec);
        fs::path file = folder / detect::Wide("MEVR-report-" + stamp + ".zip");
        for (int n = 2; fs::exists(file, ec) && n < 100; ++n) {
            file = folder / detect::Wide(std::format("MEVR-report-{}-{}.zip", stamp, n));
        }
        std::ofstream out(file, std::ios::binary | std::ios::trunc);
        out.write(archive.data(), static_cast<std::streamsize>(archive.size()));
        out.close();
        if (out) {
            r.ok = true;
            r.zip = file;
            r.folder = label;
            r.bytes = archive.size();
            break;
        }
        fs::remove(file, ec);
        r.error = "couldn't write to " + label;
    }
    if (!local.empty()) {
        if (r.ok) {
            report_marks::Touch(report_marks::LastMark(local), detect::Utf8(r.zip.filename().wstring()));
        }
        json::Value result = json::Value::Object();
        result["ok"] = r.ok;
        result["name"] = r.ok ? detect::Utf8(r.zip.filename().wstring()) : std::string();
        result["folder"] = r.folder;
        result["path"] = r.ok ? detect::Utf8(r.zip.wstring()) : std::string();
        result["error"] = r.error;
        result["time"] = NowIso();
        result["redactions"] = bundle.redactions.Total();
        fs::create_directories(local, ec);
        WriteFileAtomic(report_marks::ResultFile(local), json::Write(result));
    }
    return r;
}

Result CreateReport(Options const& options) {
    Bundle bundle = Collect(options);
    return Write(bundle, options);
}

} // namespace mevr::install::report
