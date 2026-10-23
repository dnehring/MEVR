#include "install/Http.hpp"

#include "install/Detect.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <vector>

namespace mevr::install::http {
namespace {

constexpr wchar_t kAgent[] = L"MEVR-Setup/0.1 (+https://github.com)";

struct Handle {
    HINTERNET h = nullptr;
    Handle() = default;
    explicit Handle(HINTERNET handle) : h(handle) {}
    ~Handle() {
        if (h != nullptr) {
            WinHttpCloseHandle(h);
        }
    }
    Handle(Handle const&) = delete;
    Handle& operator=(Handle const&) = delete;
    explicit operator bool() const { return h != nullptr; }
};

struct Request {
    Handle session, connection, request;
    int status = 0;
    std::uint64_t length = 0;
    std::string error;
};

std::string LastError(char const* what) {
    return std::string(what) + " failed (error " + std::to_string(GetLastError()) + ")";
}

bool Open(std::string_view url, Request& r) {
    std::wstring const wide = detect::Wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof parts;
    wchar_t host[256]{};
    wchar_t path[2048]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 2048;
    wchar_t extra[2048]{};
    parts.lpszExtraInfo = extra;
    parts.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS) {
        r.error = "not an https URL";
        return false;
    }
    r.session.h = WinHttpOpen(kAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!r.session) {
        r.error = LastError("WinHttpOpen");
        return false;
    }
    WinHttpSetTimeouts(r.session.h, 10000, 10000, 15000, 30000);
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(r.session.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof redirect);
    r.connection.h = WinHttpConnect(r.session.h, host, parts.nPort, 0);
    if (!r.connection) {
        r.error = LastError("WinHttpConnect");
        return false;
    }
    std::wstring const object = std::wstring(path) + extra;
    r.request.h = WinHttpOpenRequest(r.connection.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!r.request) {
        r.error = LastError("WinHttpOpenRequest");
        return false;
    }
    wchar_t const headers[] = L"Accept: application/vnd.github+json, */*\r\n";
    if (!WinHttpSendRequest(r.request.h, headers, static_cast<DWORD>(-1L), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(r.request.h, nullptr)) {
        r.error = LastError("the request");
        return false;
    }
    DWORD status = 0;
    DWORD size = sizeof status;
    WinHttpQueryHeaders(r.request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size,
        WINHTTP_NO_HEADER_INDEX);
    r.status = static_cast<int>(status);
    wchar_t lengthText[32]{};
    size = sizeof lengthText;
    if (WinHttpQueryHeaders(r.request.h, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lengthText, &size, WINHTTP_NO_HEADER_INDEX)) {
        r.length = _wcstoui64(lengthText, nullptr, 10);
    }
    return true;
}

} // namespace

Response Get(std::string_view url, std::size_t maxBytes) {
    Response response;
    Request r;
    if (!Open(url, r)) {
        response.error = r.error;
        return response;
    }
    response.status = r.status;
    std::vector<char> buffer(1 << 15);
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(r.request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            response.error = LastError("reading");
            break;
        }
        if (read == 0) {
            break;
        }
        response.body.append(buffer.data(), read);
        if (response.body.size() > maxBytes) {
            response.error = "the answer is too large";
            break;
        }
    }
    return response;
}

std::string Download(std::string_view url, std::filesystem::path const& file, std::function<void(Progress const&)> const& progress,
    std::atomic<bool> const* cancel) {
    Request r;
    if (!Open(url, r)) {
        return r.error;
    }
    if (r.status != 200) {
        return "the server answered " + std::to_string(r.status);
    }
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path part = file;
    part += L".part";
    std::ofstream out(part, std::ios::binary | std::ios::trunc);
    if (!out) {
        return "couldn't write " + detect::Utf8(part.wstring());
    }
    auto const start = std::chrono::steady_clock::now();
    Progress p;
    p.total = r.length;
    std::vector<char> buffer(1 << 16);
    auto lastReport = start;
    for (;;) {
        if (cancel != nullptr && cancel->load()) {
            out.close();
            std::filesystem::remove(part, ec);
            return "cancelled";
        }
        DWORD read = 0;
        if (!WinHttpReadData(r.request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            out.close();
            std::filesystem::remove(part, ec);
            return LastError("reading");
        }
        if (read == 0) {
            break;
        }
        out.write(buffer.data(), read);
        p.received += read;
        auto const now = std::chrono::steady_clock::now();
        if (progress && now - lastReport > std::chrono::milliseconds(200)) {
            lastReport = now;
            p.seconds = std::chrono::duration<double>(now - start).count();
            progress(p);
        }
    }
    out.close();
    if (p.total != 0 && p.received != p.total) {
        std::filesystem::remove(part, ec);
        return "the download stopped early";
    }
    std::filesystem::rename(part, file, ec);
    if (ec) {
        return "couldn't save " + detect::Utf8(file.wstring());
    }
    if (progress) {
        p.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        progress(p);
    }
    return {};
}

std::string SizeText(std::uint64_t bytes) {
    char buffer[32];
    double const b = static_cast<double>(bytes);
    if (b >= 1e9) {
        std::snprintf(buffer, sizeof buffer, "%.1f GB", b / 1e9);
    } else if (b >= 1e6) {
        std::snprintf(buffer, sizeof buffer, "%.0f MB", b / 1e6);
    } else {
        std::snprintf(buffer, sizeof buffer, "%.0f KB", std::ceil(b / 1e3));
    }
    return buffer;
}

std::string TimeText(std::uint64_t bytes, double bytesPerSecond) {
    if (bytesPerSecond <= 0.0) {
        return "unknown time";
    }
    double const seconds = static_cast<double>(bytes) / bytesPerSecond;
    if (seconds < 60.0) {
        return "under a minute";
    }
    int const minutes = static_cast<int>(std::lround(seconds / 60.0));
    if (minutes < 90) {
        return "about " + std::to_string(minutes) + (minutes == 1 ? " minute" : " minutes");
    }
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "about %.1f hours", seconds / 3600.0);
    return buffer;
}

} // namespace mevr::install::http
