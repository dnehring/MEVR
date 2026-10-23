// SPDX-License-Identifier: MIT
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>

namespace mevr {

struct ModuleVersion {
    std::uint16_t major = 0;
    std::uint16_t minor = 0;
    std::uint16_t build = 0;
    std::uint16_t revision = 0;

    auto operator<=>(ModuleVersion const&) const = default;
    std::string ToString() const;
};

// The only exe build LExSDKv2's hardcoded RVAs are valid for (all three games ship it).
inline constexpr ModuleVersion kSupportedGameVersion{2, 0, 0, 48602};

// Numeric file version of the host exe. The version resource's FileVersion *string* reads
// "2.0.0.0"; the build number only appears in the fixed numeric fields.
std::optional<ModuleVersion> GetHostExeVersion();

} // namespace mevr
