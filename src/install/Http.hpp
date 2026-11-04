// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

// HTTPS for the setup app (WinHTTP): the update check (GitHub's Releases API) and a download the player has
// confirmed (Mass Effect Modder's command-line build from its GitHub releases). Never called without the player's
// action. Tested against a local server (research/33 section 10: tools/setup/test-network.py, MEVR-Setup's
// --test-server), never against GitHub itself.
namespace mevr::install::http {

struct Response {
    int status = 0;
    std::string body;
    std::string error;
    // GitHub's answer for an exhausted API quota (403 or 429 with X-RateLimit-Remaining: 0, or 429 alone).
    bool rateLimited = false;
};

// GET `url` into memory (at most `maxBytes`).
Response Get(std::string_view url, std::size_t maxBytes = 4u << 20);

struct Progress {
    std::uint64_t received = 0;
    std::uint64_t total = 0;  // 0: unknown
    double seconds = 0.0;
};

// What the finished download must be (from the release's own listing): its size and SHA-256 (lower-case hex). Empty /
// 0: not checked.
struct Expect {
    std::uint64_t size = 0;
    std::string sha256;
};

// GET `url` into `file` (through a .part file, which never stays behind), calling `progress` now and then. `cancel`
// stops it between reads. A short body (the server's Content-Length, or `expect.size`), a different SHA-256 or a full
// disk is an error and leaves nothing. Redirects are followed (never from https to http). "" or an error.
std::string Download(std::string_view url, std::filesystem::path const& file, std::function<void(Progress const&)> const& progress,
    std::atomic<bool> const* cancel, Expect const& expect = {});

// ---- Tests only (MEVR-Setup's --test-server; research/33 section 10) ---------------------------------------------
// Sends every request to `base` instead: https://<host>/<path> goes to <base>/<host>/<path>
// (https://api.github.com/repos/... -> http://127.0.0.1:8123/api.github.com/repos/...), so a local server answers for
// GitHub and nothing reaches the internet. `base` must be http://127.0.0.1[:port] (no proxy is used for it) or an
// https:// URL; anything else is refused (false) and nothing changes; "" turns it off. Never set by default.
bool SetTestServer(std::string_view base);
// Where a request for `url` goes: `url` itself unless a test server is set.
std::string EffectiveUrl(std::string_view url);

// A WinHTTP error code in words ("couldn't connect (error 12029)").
std::string ErrorText(unsigned long code);

// "1.2 GB", "176 MB", "940 KB".
std::string SizeText(std::uint64_t bytes);
// About how long a download of `bytes` takes at `bytesPerSecond`: "about 3 minutes".
std::string TimeText(std::uint64_t bytes, double bytesPerSecond);

} // namespace mevr::install::http
