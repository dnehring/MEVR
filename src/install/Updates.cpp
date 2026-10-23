// SPDX-License-Identifier: MIT
#include "install/Updates.hpp"

#include "core/Json.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <vector>

namespace mevr::install::updates {
namespace {

bool ValidName(std::string_view s) {
    if (s.empty() || s.size() > 100) {
        return false;
    }
    for (char const c : s) {
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

std::vector<long> Parts(std::string_view v) {
    if (!v.empty() && (v.front() == 'v' || v.front() == 'V')) {
        v.remove_prefix(1);
    }
    std::size_t const cut = v.find_first_of("-+ ");
    v = v.substr(0, cut);
    std::vector<long> parts;
    std::size_t start = 0;
    while (start <= v.size()) {
        std::size_t const dot = v.find('.', start);
        std::string const piece(v.substr(start, dot == std::string_view::npos ? std::string_view::npos : dot - start));
        parts.push_back(std::strtol(piece.c_str(), nullptr, 10));
        if (dot == std::string_view::npos) {
            break;
        }
        start = dot + 1;
    }
    while (!parts.empty() && parts.back() == 0) {
        parts.pop_back();
    }
    return parts;
}

} // namespace

std::string LatestReleaseApi(std::string_view repoUrl) {
    constexpr std::string_view kPrefix = "https://github.com/";
    if (repoUrl == kPlaceholderRepo || repoUrl.rfind(kPrefix, 0) != 0) {
        return {};
    }
    std::string_view rest = repoUrl.substr(kPrefix.size());
    while (!rest.empty() && rest.back() == '/') {
        rest.remove_suffix(1);
    }
    std::size_t const slash = rest.find('/');
    if (slash == std::string_view::npos) {
        return {};
    }
    std::string_view const owner = rest.substr(0, slash);
    std::string_view repo = rest.substr(slash + 1);
    if (repo.size() > 4 && repo.substr(repo.size() - 4) == ".git") {
        repo.remove_suffix(4);
    }
    if (!ValidName(owner) || !ValidName(repo) || owner == "OWNER") {
        return {};
    }
    return "https://api.github.com/repos/" + std::string(owner) + "/" + std::string(repo) + "/releases/latest";
}

std::optional<Release> ParseLatestRelease(std::string_view text) {
    auto const v = json::Parse(text);
    if (!v || !v->IsObject()) {
        return std::nullopt;
    }
    Release r;
    r.tag = v->String("tag_name");
    r.name = v->String("name");
    r.page = v->String("html_url");
    if (r.tag.empty() || r.page.rfind("https://github.com/", 0) != 0) {
        return std::nullopt;
    }
    return r;
}

int CompareVersions(std::string_view a, std::string_view b) {
    std::vector<long> const x = Parts(a);
    std::vector<long> const y = Parts(b);
    for (std::size_t i = 0; i < std::max(x.size(), y.size()); ++i) {
        long const p = i < x.size() ? x[i] : 0;
        long const q = i < y.size() ? y[i] : 0;
        if (p != q) {
            return p < q ? -1 : 1;
        }
    }
    return 0;
}

} // namespace mevr::install::updates
