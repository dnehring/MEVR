// SPDX-License-Identifier: MIT
// The arm-length sample's writer (plugin/ArmSample.hpp): the keys and order of LE1's VR hands tab (MevrMenuTab.cpp
// WriteArmSample), so the mod's reader (game/HandCalibration) needs nothing new.

#include "plugin/ArmSample.hpp"

#include "core/Log.hpp"
#include "plugin/Renderer.hpp"
#include "core/vr/SettingsFile.hpp"

#include <Windows.h>

#include <cmath>
#include <format>
#include <string>

namespace mevr::host::arm_sample {
namespace {

bool Put(std::wstring const& path, wchar_t const* key, double value, int decimals) {
    std::string const text = std::format("{:.{}f}", value, decimals);
    std::wstring const wide(text.begin(), text.end());
    return mevr::vr::settings_file::Write(L"Arms", key, wide.c_str(), path.c_str()) != 0;
}

} // namespace

void Write(XrVector3f const& l, XrVector3f const& r, XrVector3f const* head, char const* why) {
    std::wstring const& path = SettingsPath();
    if (path.empty()) {
        log::Warn("arm sample: no settings file: not taken ({})", why != nullptr ? why : "?");
        return;
    }
    // The counter as the file has it now: the mod (or LE1's tab) may have raised it since.
    long const request = static_cast<long>(mevr::vr::settings_file::Int(L"Arms", L"measureRequest", 0, path.c_str()));
    float const dx = r.x - l.x, dy = r.y - l.y, dz = r.z - l.z;
    float const span = std::sqrt(dx * dx + dy * dy + dz * dz);
    bool ok = Put(path, L"measureLeftX", l.x, 4) && Put(path, L"measureLeftY", l.y, 4) && Put(path, L"measureLeftZ", l.z, 4) &&
              Put(path, L"measureRightX", r.x, 4) && Put(path, L"measureRightY", r.y, 4) && Put(path, L"measureRightZ", r.z, 4);
    if (head != nullptr) {
        ok = Put(path, L"measureHeadX", head->x, 4) && Put(path, L"measureHeadY", head->y, 4) &&
             Put(path, L"measureHeadZ", head->z, 4) && ok;
    }
    ok = Put(path, L"measureSpanM", span, 4) && ok;
    // The counter last, so a reader that sees it change finds the sample already written.
    ok = Put(path, L"measureRequest", static_cast<double>(request + 1), 0) && ok;
    log::Info("arm sample: {} taken ({}): grips {:.2f} m apart, left ({:.2f}, {:.2f}, {:.2f}), right ({:.2f}, {:.2f}, {:.2f}), "
              "head {}{}",
        request + 1, why != nullptr ? why : "?", span, l.x, l.y, l.z, r.x, r.y, r.z, head != nullptr ? "recorded" : "unknown",
        ok ? "" : " (NOT all written: the settings file is read-only?)");
}

} // namespace mevr::host::arm_sample
