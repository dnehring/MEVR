// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <string>
#include <string_view>

// The setup app's update check against the mod's GitHub Releases (research/33). The repository is a setting
// (MEVR-Setup.ini [Update] repo = https://github.com/<owner>/<repo>); the placeholder means "no check". It only reads
// the latest release's tag and page: nothing is downloaded, the player is sent to the release page.
namespace mevr::install::updates {

// This build's version (the mod's MEVR_VERSION).
inline constexpr char kVersion[] = "0.1.0";
// The placeholder until the repository exists.
inline constexpr char kPlaceholderRepo[] = "https://github.com/OWNER/REPO";

// "https://api.github.com/repos/<owner>/<repo>/releases/latest" for a repository page URL; "" for the placeholder
// or anything that isn't https://github.com/<owner>/<repo>.
std::string LatestReleaseApi(std::string_view repoUrl);

struct Release {
    std::string tag;   // "v0.2.0"
    std::string name;
    std::string page;  // html_url
};
std::optional<Release> ParseLatestRelease(std::string_view json);

// Compares dotted versions, ignoring a leading "v" and anything after a '-' or '+': <0, 0, >0.
int CompareVersions(std::string_view a, std::string_view b);

} // namespace mevr::install::updates
