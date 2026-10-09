#include "game/HandCalibration.hpp"

#include "core/Log.hpp"
#include "core/ik/HandFrame.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace mevr::game::hand_calibration {
namespace {

namespace fs = std::filesystem;

struct Values {
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
    double gunRoll = 0.0;
    bool operator==(Values const&) const = default;
};

Values gValues;
std::array<ik::Quat, 2> gTrims{};
ULONGLONG gLastCheckMs = 0;
fs::file_time_type gStamp{};
bool gHaveStamp = false;
bool gApplied = false;

constexpr char const* kDefaultFile =
    "; ME VR Attempt settings. Edited while the game runs: changes apply within a second.\n"
    "\n"
    "[Hands]\n"
    "; How Shepard's hands sit on the controllers, in degrees, the same for both hands (mirrored for\n"
    "; the left). handPitch: positive tilts the fingers down. handYaw: positive turns them outward\n"
    "; for the right hand. handRoll: positive rolls the thumb side outward for the right hand.\n"
    "handPitch = 0\n"
    "handYaw = 0\n"
    "handRoll = 0\n";

fs::path FilePath() {
    wchar_t buffer[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return fs::path(buffer).parent_path() / L"ASI" / (SDK_TARGET_NAME_W L"MEVR.ini");
}

void Apply(Values const& values) {
    gValues = values;
    gTrims[1] = ik::GripTrim(values.pitch, values.yaw, values.roll);
    gTrims[0] = ik::GripTrim(values.pitch, -values.yaw, -values.roll);
}

Values Read(fs::path const& path) {
    Values values;
    std::ifstream in(path);
    std::string line;
    bool hands = false;
    while (std::getline(in, line)) {
        auto const first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == ';' || line[first] == '#') {
            continue;
        }
        if (line[first] == '[') {
            hands = line.compare(first, 7, "[Hands]") == 0;
            continue;
        }
        auto const equals = line.find('=');
        if (!hands || equals == std::string::npos) {
            continue;
        }
        std::string key = line.substr(first, equals - first);
        while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) {
            key.pop_back();
        }
        double const value = std::strtod(line.c_str() + equals + 1, nullptr);
        if (key == "handPitch") {
            values.pitch = value;
        } else if (key == "handYaw") {
            values.yaw = value;
        } else if (key == "handRoll") {
            values.roll = value;
        } else if (key == "gunRoll") {
            values.gunRoll = value;
        }
    }
    return values;
}

} // namespace

ik::Quat Trim(int side) {
    return gTrims[side == 0 ? 0 : 1];
}

double GunRoll() {
    return gValues.gunRoll;
}

void Refresh() {
    ULONGLONG const now = GetTickCount64();
    if (gHaveStamp && now - gLastCheckMs < 1000) {
        return;
    }
    gLastCheckMs = now;
    fs::path const path = FilePath();
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::ofstream(path) << kDefaultFile;
        log::Info("hands: wrote default calibration to {}", path.string());
    }
    auto const stamp = fs::last_write_time(path, ec);
    if (ec || (gHaveStamp && stamp == gStamp)) {
        return;
    }
    gStamp = stamp;
    gHaveStamp = true;
    Values const values = Read(path);
    if (!gApplied || values != gValues) {
        gApplied = true;
        Apply(values);
        log::Info("hands: calibration pitch {:.1f}, yaw {:.1f}, roll {:.1f} degrees, gun roll {:.1f} degrees (from {})",
            values.pitch, values.yaw, values.roll, values.gunRoll, path.filename().string());
    }
}

} // namespace mevr::game::hand_calibration
