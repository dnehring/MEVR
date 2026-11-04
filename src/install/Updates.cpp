// SPDX-License-Identifier: MIT
#include "install/Updates.hpp"

#include "core/Json.hpp"
#include "install/Http.hpp"

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
    r.prerelease = v->Bool("prerelease") || v->Bool("draft");
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

char const* StateName(CheckResult::State state) {
    switch (state) {
    case CheckResult::State::Newer: return "newer";
    case CheckResult::State::Latest: return "latest";
    case CheckResult::State::NoRelease: return "no release";
    case CheckResult::State::RateLimited: return "rate limited";
    case CheckResult::State::Failed: return "failed";
    case CheckResult::State::Off: return "off";
    }
    return "";
}

CheckResult Interpret(int status, std::string_view body, std::string_view error, bool rateLimited, std::string_view mine) {
    using State = CheckResult::State;
    CheckResult out;
    if (status == 0) {
        out.text = "Couldn't check: " + std::string(error.empty() ? std::string_view("no answer") : error) + ".";
        return out;
    }
    if (rateLimited) {
        out.state = State::RateLimited;
        out.text = "GitHub's limit for checks without an account is reached for now (60 an hour): try again later.";
        return out;
    }
    if (status == 404) {
        // /releases/latest answers 404 while a repository has no published release (drafts and pre-releases don't
        // count), and for a repository that doesn't exist.
        out.state = State::NoRelease;
        out.text = "No release is published on GitHub yet.";
        return out;
    }
    if (status != 200) {
        out.text = "Couldn't check: GitHub answered HTTP " + std::to_string(status) + ".";
        return out;
    }
    if (!error.empty()) {
        out.text = "Couldn't check: " + std::string(error) + ".";
        return out;
    }
    auto const release = ParseLatestRelease(body);
    if (!release) {
        out.text = "Couldn't check: GitHub's answer wasn't a release.";
        return out;
    }
    out.tag = release->tag;
    int const order = CompareVersions(release->tag, mine);
    out.state = State::Latest;
    if (order > 0 && !release->prerelease) {
        out.state = State::Newer;
        out.text = "Version " + release->tag + " is out.";
        out.page = release->page;
    } else if (order < 0 && !release->prerelease) {
        out.text = "This version is newer than the latest release (" + release->tag + ").";
    } else {
        out.text = "This is the latest version.";
    }
    return out;
}

CheckResult Check(std::string_view repoUrl) {
    std::string const api = LatestReleaseApi(repoUrl);
    if (api.empty()) {
        CheckResult off;
        off.state = CheckResult::State::Off;
        off.text = "No release page set (MEVR-Setup.ini, [Update] repo).";
        return off;
    }
    http::Response const r = http::Get(api);
    return Interpret(r.status, r.body, r.error, r.rateLimited);
}

} // namespace mevr::install::updates
