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
// action. [untested: nothing was downloaded while this was written; research/33]
namespace mevr::install::http {

struct Response {
    int status = 0;
    std::string body;
    std::string error;
};

// GET `url` into memory (at most `maxBytes`).
Response Get(std::string_view url, std::size_t maxBytes = 4u << 20);

struct Progress {
    std::uint64_t received = 0;
    std::uint64_t total = 0;  // 0: unknown
    double seconds = 0.0;
};

// GET `url` into `file` (through a .part file), calling `progress` now and then. `cancel` stops it. "" or an error.
std::string Download(std::string_view url, std::filesystem::path const& file, std::function<void(Progress const&)> const& progress,
    std::atomic<bool> const* cancel);

// "1.2 GB", "176 MB", "940 KB".
std::string SizeText(std::uint64_t bytes);
// About how long a download of `bytes` takes at `bytesPerSecond`: "about 3 minutes".
std::string TimeText(std::uint64_t bytes, double bytesPerSecond);

} // namespace mevr::install::http
