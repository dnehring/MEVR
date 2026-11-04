// SPDX-License-Identifier: MIT
#include "install/Http.hpp"

#include "install/Detect.hpp"
#include "install/Manifest.hpp"

#include <Windows.h>
#include <winhttp.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <format>
#include <fstream>
#include <mutex>
#include <system_error>
#include <vector>

namespace mevr::install::http {
namespace {

constexpr wchar_t kAgent[] = L"MEVR-Setup/0.1 (+https://github.com/dnehring/MEVR)";

// The test server (SetTestServer): "" = off, the shipped behaviour.
std::mutex gTestMutex;
std::string gTestBase;
bool gTestLoopback = false;

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
    bool rateLimited = false;
    std::string error;
};

std::string LastError(char const* what) {
    DWORD const code = GetLastError();
    return std::string(what) + " failed: " + ErrorText(code);
}

bool Open(std::string_view url, Request& r) {
    bool loopbackAllowed = false;
    {
        std::lock_guard const lock(gTestMutex);
        loopbackAllowed = gTestLoopback;
    }
    std::wstring const wide = detect::Wide(EffectiveUrl(url));
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
    if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts)) {
        r.error = "not an https URL";
        return false;
    }
    bool const secure = parts.nScheme == INTERNET_SCHEME_HTTPS;
    // Plain http only to the test server on this PC (tests only: never set by default).
    bool const loopback = !secure && parts.nScheme == INTERNET_SCHEME_HTTP && loopbackAllowed && std::wstring_view(host) == L"127.0.0.1";
    if (!secure && !loopback) {
        r.error = "not an https URL";
        return false;
    }
    r.session.h = WinHttpOpen(kAgent, loopback ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
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
        secure ? WINHTTP_FLAG_SECURE : 0);
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
    // GitHub's API quota: 60 requests an hour without an account (403 with X-RateLimit-Remaining: 0, or 429).
    wchar_t remaining[32]{};
    size = sizeof remaining;
    bool const quotaGone = WinHttpQueryHeaders(r.request.h, WINHTTP_QUERY_CUSTOM, L"X-RateLimit-Remaining", remaining, &size,
                               WINHTTP_NO_HEADER_INDEX) && std::wstring_view(remaining) == L"0";
    r.rateLimited = r.status == 429 || (r.status == 403 && quotaGone);
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
    response.rateLimited = r.rateLimited;
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
    std::atomic<bool> const* cancel, Expect const& expect) {
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
    // Every way out but success leaves no .part behind.
    auto fail = [&](std::string why) {
        out.close();
        std::filesystem::remove(part, ec);
        return why;
    };
    auto const start = std::chrono::steady_clock::now();
    Progress p;
    p.total = r.length != 0 ? r.length : expect.size;
    std::vector<char> buffer(1 << 16);
    auto lastReport = start;
    for (;;) {
        if (cancel != nullptr && cancel->load()) {
            return fail("cancelled");
        }
        DWORD read = 0;
        if (!WinHttpReadData(r.request.h, buffer.data(), static_cast<DWORD>(buffer.size()), &read)) {
            return fail(LastError("reading"));
        }
        if (read == 0) {
            break;
        }
        out.write(buffer.data(), read);
        if (!out) {
            return fail("couldn't write " + detect::Utf8(part.wstring()) + " (is the disk full?)");
        }
        p.received += read;
        if (expect.size != 0 && p.received > expect.size) {
            return fail(std::format("the download is larger than the {} the release lists", expect.size));
        }
        auto const now = std::chrono::steady_clock::now();
        if (progress && now - lastReport > std::chrono::milliseconds(200)) {
            lastReport = now;
            p.seconds = std::chrono::duration<double>(now - start).count();
            progress(p);
        }
    }
    out.close();
    if (out.fail()) {
        return fail("couldn't write " + detect::Utf8(part.wstring()) + " (is the disk full?)");
    }
    if (r.length != 0 && p.received != r.length) {
        return fail(std::format("the download stopped early ({} of {} bytes)", p.received, r.length));
    }
    if (expect.size != 0 && p.received != expect.size) {
        return fail(std::format("the download is {} bytes, the release lists {}", p.received, expect.size));
    }
    if (!expect.sha256.empty() && Sha256File(part) != expect.sha256) {
        return fail("the download's SHA-256 isn't the one the release lists");
    }
    std::filesystem::rename(part, file, ec);
    if (ec) {
        std::filesystem::remove(part, ec);
        return "couldn't save " + detect::Utf8(file.wstring());
    }
    if (progress) {
        p.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        progress(p);
    }
    return {};
}

bool SetTestServer(std::string_view base) {
    while (!base.empty() && base.back() == '/') {
        base.remove_suffix(1);
    }
    bool loopback = false;
    if (base.empty()) {
        // Off again: the shipped behaviour.
    } else if (base.rfind("https://", 0) == 0) {
        std::string_view const host = base.substr(8);
        if (host.empty() || host.find_first_of(" \t\r\n\\") != std::string_view::npos) {
            return false;
        }
    } else if (base.rfind("http://127.0.0.1", 0) == 0) {
        std::string_view port = base.substr(16);
        if (!port.empty()) {
            if (port.front() != ':' || port.size() < 2 || port.size() > 6) {
                return false;
            }
            port.remove_prefix(1);
            for (char const c : port) {
                if (c < '0' || c > '9') {
                    return false;
                }
            }
        }
        loopback = true;
    } else {
        return false;
    }
    std::lock_guard const lock(gTestMutex);
    gTestBase = std::string(base);
    gTestLoopback = loopback;
    return true;
}

std::string EffectiveUrl(std::string_view url) {
    std::lock_guard const lock(gTestMutex);
    if (gTestBase.empty() || url.rfind(gTestBase + "/", 0) == 0) {
        return std::string(url);
    }
    std::size_t const scheme = url.find("://");
    return gTestBase + "/" + std::string(scheme == std::string_view::npos ? url : url.substr(scheme + 3));
}

std::string ErrorText(unsigned long code) {
    char const* text = nullptr;
    switch (code) {
    case ERROR_WINHTTP_TIMEOUT: text = "no answer in time"; break;
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: text = "the server's name wasn't found (is the PC online?)"; break;
    case ERROR_WINHTTP_CANNOT_CONNECT: text = "couldn't connect (is the PC online, or is a firewall in the way?)"; break;
    case ERROR_WINHTTP_CONNECTION_ERROR: text = "the connection was dropped"; break;
    case ERROR_WINHTTP_SECURE_FAILURE: text = "the secure connection failed (is the PC's clock right?)"; break;
    case ERROR_WINHTTP_INVALID_SERVER_RESPONSE: text = "the server's answer wasn't valid"; break;
    case ERROR_WINHTTP_REDIRECT_FAILED: text = "a redirect was refused"; break;
    case ERROR_WINHTTP_OPERATION_CANCELLED: text = "cancelled"; break;
    default: break;
    }
    return text != nullptr ? std::format("{} (error {})", text, code) : std::format("error {}", code);
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
