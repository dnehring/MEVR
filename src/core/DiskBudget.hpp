// SPDX-License-Identifier: MIT
#pragma once

// What the diagnostics keep on disk (docs/release-checklist.md section 1): the test loop's snapshots, recordings and
// frame-timing captures in %LOCALAPPDATA%\MEVR were never removed (44 GB of snapshots on the development machine; a
// quick tap of both stick clicks takes one). PlanPrune is pure (tests/Main.cpp); Prune walks a folder.
// The players' build keeps a budget per folder, oldest files out first, never one younger than minAgeSeconds (it
// may still be being written or about to be zipped into a report); development builds keep everything by default.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <system_error>
#include <vector>

namespace mevr::disk_budget {

struct Entry {
    std::uintmax_t bytes = 0;
    std::int64_t ageSeconds = 0;
};

// The indices of the entries to delete, oldest first, until the rest fit in budgetBytes. budgetBytes 0: no limit.
inline std::vector<std::size_t> PlanPrune(std::vector<Entry> const& entries, std::uintmax_t budgetBytes,
                                          std::int64_t minAgeSeconds) {
    std::vector<std::size_t> out;
    if (budgetBytes == 0) {
        return out;
    }
    std::uintmax_t total = 0;
    for (Entry const& e : entries) {
        total += e.bytes;
    }
    if (total <= budgetBytes) {
        return out;
    }
    std::vector<std::size_t> order(entries.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
        [&](std::size_t a, std::size_t b) { return entries[a].ageSeconds > entries[b].ageSeconds; });
    for (std::size_t const i : order) {
        if (total <= budgetBytes || entries[i].ageSeconds < minAgeSeconds) {
            break;  // oldest first: everything after this one is younger
        }
        out.push_back(i);
        total -= entries[i].bytes;
    }
    return out;
}

struct Result {
    std::size_t files = 0;      // files deleted
    std::uintmax_t bytes = 0;   // bytes freed
    std::uintmax_t kept = 0;    // bytes left in the folder
};

// Deletes the oldest regular files under dir (recursively) until it fits in budgetBytes.
inline Result Prune(std::filesystem::path const& dir, std::uintmax_t budgetBytes, std::int64_t minAgeSeconds) {
    namespace fs = std::filesystem;
    Result r;
    std::error_code ec;
    if (dir.empty() || !fs::is_directory(dir, ec)) {
        return r;
    }
    std::vector<fs::path> paths;
    std::vector<Entry> entries;
    auto const now = fs::file_time_type::clock::now();
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc) || fileEc) {
            continue;
        }
        std::uintmax_t const size = it->file_size(fileEc);
        auto const written = it->last_write_time(fileEc);
        if (fileEc) {
            continue;
        }
        paths.push_back(it->path());
        entries.push_back({size, std::chrono::duration_cast<std::chrono::seconds>(now - written).count()});
        r.kept += size;
    }
    for (std::size_t const i : PlanPrune(entries, budgetBytes, minAgeSeconds)) {
        std::error_code removeEc;
        if (fs::remove(paths[i], removeEc) && !removeEc) {
            ++r.files;
            r.bytes += entries[i].bytes;
            r.kept -= entries[i].bytes;
        }
    }
    return r;
}

} // namespace mevr::disk_budget
