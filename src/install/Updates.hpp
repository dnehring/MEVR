// SPDX-License-Identifier: MIT
#pragma once

#include "mevr/Version.hpp"  // generated from project(MEVR VERSION ...) in CMakeLists.txt

#include <optional>
#include <string>
#include <string_view>

// The setup app's update check against the mod's GitHub Releases (research/33). The repository is a setting
// (MEVR-Setup.ini [Update] repo = https://github.com/<owner>/<repo>); the placeholder means "no check". It only reads
// the latest release's tag and page: nothing is downloaded, the player is sent to the release page. Tested against a
// local server (research/33 section 10).
namespace mevr::install::updates {

// This build's version: the mod's, the setup's and the release's (one number, from CMake).
inline constexpr char kVersion[] = MEVR_VERSION;
// The placeholder: "no check" (an MEVR-Setup.ini can set it to turn the check off).
inline constexpr char kPlaceholderRepo[] = "https://github.com/OWNER/REPO";
// The project's public repository, whose Releases the setup checks by default.
inline constexpr char kDefaultRepo[] = "https://github.com/dnehring/MEVR";

// "https://api.github.com/repos/<owner>/<repo>/releases/latest" for a repository page URL; "" for the placeholder
// or anything that isn't https://github.com/<owner>/<repo>.
std::string LatestReleaseApi(std::string_view repoUrl);

struct Release {
    std::string tag;   // "v0.2.0"
    std::string name;
    std::string page;  // html_url
    bool prerelease = false;  // "prerelease" or "draft" (GitHub's /releases/latest never returns one; never offered)
};
std::optional<Release> ParseLatestRelease(std::string_view json);

// Compares dotted versions, ignoring a leading "v" and anything after a '-' or '+': <0, 0, >0.
int CompareVersions(std::string_view a, std::string_view b);

// What the check found, for the setup's Settings row (and the command line's --check-update).
struct CheckResult {
    enum class State {
        Newer,        // a newer release: `page` is its GitHub page
        Latest,       // this version, or newer than the newest release
        NoRelease,    // GitHub lists none (404: no release published yet, or no such repository)
        RateLimited,  // GitHub's quota without an account (60 an hour) is used up
        Failed,       // offline, an error, or an answer that isn't a release
        Off,          // no repository set (the placeholder)
    };
    State state = State::Failed;
    std::string text;  // for the player
    std::string page;  // Newer only
    std::string tag;   // the newest release's, when GitHub named one
};
char const* StateName(CheckResult::State state);
// Pure: GitHub's answer to the latest-release request (status, body, the transport's error, its rate-limit flag)
// against `mine`.
CheckResult Interpret(int status, std::string_view body, std::string_view error, bool rateLimited, std::string_view mine = kVersion);
// The whole check for `repoUrl` (MEVR-Setup.ini's [Update] repo): one request to GitHub's Releases API (or the test
// server: http::SetTestServer). Off for the placeholder or anything that isn't a GitHub repository page.
CheckResult Check(std::string_view repoUrl);

} // namespace mevr::install::updates
