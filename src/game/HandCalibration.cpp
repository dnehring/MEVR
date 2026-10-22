#include "game/HandCalibration.hpp"

#include "core/Log.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/HandFrame.hpp"

#include <LESDK/Headers.hpp>

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <string>

namespace mevr::game::hand_calibration {
namespace {

namespace fs = std::filesystem;

struct Values {
    double pitch = 0.0;
    double yaw = 0.0;
    double roll = 0.0;
    double gunRoll = 0.0;
    bool bodyFollowsHead = true;
    double armStretch = ik::kMaxArmStretch;
    double trackingScale = 0.0;
    double armLength = 0.0;  // [Arms] armLengthM
    // [Controls]
    bool leftTrigger = false;
    bool leftTriggerLB = true;
    bool gripLB = false;
    bool supportGrip = true;
    double supportAcquire = 0.12;
    double supportRelease = 0.22;
    bool sniperAims = false;
    bool aimAtShoulder = true;
    bool operator==(Values const&) const = default;
};

// The menu's arm-length sample ([Arms], src/plugin/MevrMenuTab.cpp).
struct Sample {
    bool hasRequest = false;
    long long request = 0;
    ik::ArmLengthSample poses;
    int fields = 0;  // how many of the nine coordinates were present
};

Values gValues;
std::array<ik::Quat, 2> gTrims{};
ULONGLONG gLastCheckMs = 0;
fs::file_time_type gStamp{};
bool gHaveStamp = false;
bool gApplied = false;
bool gHaveRequest = false;
long long gLastRequest = 0;

constexpr char const* kDefaultFile =
    "; ME VR Attempt settings. Edited while the game runs: changes apply within a second.\n"
    "; Halcyon's menu (VR hands tab) edits some of these too.\n"
    "\n"
    "[Hands]\n"
    "; How Shepard's hands sit on the controllers, in degrees, the same for both hands (mirrored for\n"
    "; the left). handPitch: positive tilts the fingers down. handYaw: positive turns them outward\n"
    "; for the right hand. handRoll: positive rolls the thumb side outward for the right hand.\n"
    "handPitch = 0\n"
    "handYaw = 0\n"
    "handRoll = 0\n"
    "; A held gun's barrel follows the controller's aim and its top the thumb side of the hand.\n"
    "; gunRoll: degrees about the barrel if the gun looks rolled; positive rolls its top to the right.\n"
    "gunRoll = 0\n"
    "; bodyFollowsHead: 1 turns and leans Shepard's spine so his shoulders sit under the headset.\n"
    "bodyFollowsHead = 1\n"
    "; armStretch: how far Shepard's arms may stretch to keep the hands on the controllers (1-1.25).\n"
    "armStretch = 1.25\n"
    "; trackingScale: Unreal units per meter of hand and head motion; 0 = the view's own scale.\n"
    "trackingScale = 0\n"
    "\n"
    "[Arms]\n"
    "; Arm-length calibration (the VR hands tab samples a T pose; the mod writes armLengthM, the\n"
    "; player's shoulder-to-grip length in meters, and scales reach with it; 0 or absent = off).\n";

// Appended to a settings file that has no [Controls] yet (and part of a new file).
constexpr char const* kControlsBlock =
    "\n"
    "[Controls]\n"
    "; The two-handed grip: squeeze the left grip with the left hand on the gun's foregrip (within\n"
    "; supportAcquireM meters) to hold it with both hands; it lets go when the grip opens or the hand\n"
    "; moves supportReleaseM away. While held, the barrel points from the right hand toward the left,\n"
    "; and the game sees the left trigger (LT) held: aimed, focused fire. Elsewhere the left grip is LB.\n"
    "supportGrip = 1\n"
    "supportAcquireM = 0.12\n"
    "supportReleaseM = 0.22\n"
    "; sniperAims: 1 also holds LT for a sniper rifle (its aimed mode is the game's scope camera).\n"
    "sniperAims = 0\n"
    "; aimAtShoulder: 1 = aimed fire (LT) needs the gun at the shoulder as well (the right hand\n"
    "; close under the eyes, near the cheek); 0 = both hands on the gun is enough.\n"
    "aimAtShoulder = 1\n"
    "; leftTrigger: lb = the weapon wheel (the default); lt = the game's LT (aim, zoom), as before;\n"
    "; none = the game doesn't see it. Aimed fire comes from holding the gun with both hands at the shoulder.\n"
    "leftTrigger = lb\n"
    "; leftGripAway: what a left grip squeezed away from the gun does. none (the default): nothing, the\n"
    "; grip only takes the gun; lb = the weapon wheel, as before.\n"
    "leftGripAway = none\n";

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

std::string Strip(std::string const& text) {
    auto const first = text.find_first_not_of(" \t\r");
    auto const last = text.find_last_not_of(" \t\r");
    return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
}

// "[Section]" -> "key" -> value text, for "key = value" and "key=value" alike.
using Ini = std::map<std::string, std::map<std::string, std::string>>;

Ini ReadIni(fs::path const& path) {
    Ini ini;
    std::ifstream in(path);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        std::string const text = Strip(line);
        if (text.empty() || text[0] == ';' || text[0] == '#') {
            continue;
        }
        if (text[0] == '[') {
            auto const close = text.find(']');
            section = close == std::string::npos ? std::string() : text.substr(1, close - 1);
            continue;
        }
        auto const equals = text.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        ini[section][Strip(text.substr(0, equals))] = Strip(text.substr(equals + 1));
    }
    return ini;
}

bool Number(Ini const& ini, char const* section, char const* key, double& out) {
    auto const s = ini.find(section);
    if (s == ini.end()) {
        return false;
    }
    auto const k = s->second.find(key);
    if (k == s->second.end() || k->second.empty()) {
        return false;
    }
    char* end = nullptr;
    double const value = std::strtod(k->second.c_str(), &end);
    if (end == k->second.c_str() || !std::isfinite(value)) {
        return false;
    }
    out = value;
    return true;
}

Values ReadValues(Ini const& ini) {
    Values values;
    Number(ini, "Hands", "handPitch", values.pitch);
    Number(ini, "Hands", "handYaw", values.yaw);
    Number(ini, "Hands", "handRoll", values.roll);
    Number(ini, "Hands", "gunRoll", values.gunRoll);
    double flag = 1.0;
    if (Number(ini, "Hands", "bodyFollowsHead", flag)) {
        values.bodyFollowsHead = flag != 0.0;
    }
    if (Number(ini, "Hands", "armStretch", values.armStretch)) {
        values.armStretch = std::clamp(values.armStretch, 1.0, ik::kMaxArmStretch);
    }
    if (Number(ini, "Hands", "trackingScale", values.trackingScale) && !(values.trackingScale > 0.0)) {
        values.trackingScale = 0.0;
    }
    if (Number(ini, "Arms", "armLengthM", values.armLength) && !(values.armLength >= 0.3 && values.armLength <= 1.2)) {
        values.armLength = 0.0;
    }
    if (auto const s = ini.find("Controls"); s != ini.end()) {
        if (auto const k = s->second.find("leftTrigger"); k != s->second.end()) {
            std::string value = k->second;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            values.leftTrigger = value == "lt";
            values.leftTriggerLB = value == "lb";
        }
        if (auto const k = s->second.find("leftGripAway"); k != s->second.end()) {
            std::string value = k->second;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            values.gripLB = value == "lb";
        }
    }
    if (Number(ini, "Controls", "aimAtShoulder", flag)) {
        values.aimAtShoulder = flag != 0.0;
    }
    if (Number(ini, "Controls", "supportGrip", flag)) {
        values.supportGrip = flag != 0.0;
    }
    double acquire = values.supportAcquire;
    double release = values.supportRelease;
    Number(ini, "Controls", "supportAcquireM", acquire);
    Number(ini, "Controls", "supportReleaseM", release);
    values.supportAcquire = std::clamp(acquire, 0.02, 0.5);
    values.supportRelease = std::clamp(release, values.supportAcquire, 1.0);
    if (Number(ini, "Controls", "sniperAims", flag)) {
        values.sniperAims = flag != 0.0;
    }
    return values;
}

Sample ReadSample(Ini const& ini) {
    Sample sample;
    double request = 0.0;
    sample.hasRequest = Number(ini, "Arms", "measureRequest", request);
    sample.request = static_cast<long long>(request);
    struct Field {
        char const* key;
        double* value;
    };
    Field const fields[] = {
        {"measureHeadX", &sample.poses.head.x}, {"measureHeadY", &sample.poses.head.y}, {"measureHeadZ", &sample.poses.head.z},
        {"measureLeftX", &sample.poses.left.x}, {"measureLeftY", &sample.poses.left.y}, {"measureLeftZ", &sample.poses.left.z},
        {"measureRightX", &sample.poses.right.x}, {"measureRightY", &sample.poses.right.y},
        {"measureRightZ", &sample.poses.right.z},
    };
    for (Field const& field : fields) {
        sample.fields += Number(ini, "Arms", field.key, *field.value) ? 1 : 0;
    }
    return sample;
}

bool WriteArmLength(double meters) {
    std::wstring const value = meters > 0.0 ? std::format(L"{:.3f}", meters) : std::wstring(L"0");
    return WritePrivateProfileStringW(L"Arms", L"armLengthM", value.c_str(), FilePath().wstring().c_str()) != 0;
}

double gCameraWritten[3] = {1e9, 1e9, 1e9};

} // namespace

void WriteCameraOffset(double forward, double right, double up) {
    double const values[3] = {forward, right, up};
    bool changed = false;
    for (int i = 0; i < 3; ++i) {
        changed = changed || std::abs(values[i] - gCameraWritten[i]) > 0.5;
    }
    if (!changed) {
        return;
    }
    wchar_t const* const keys[3] = {L"measuredForward", L"measuredRight", L"measuredUp"};
    for (int i = 0; i < 3; ++i) {
        std::wstring const value = std::format(L"{:.1f}", values[i]);
        WritePrivateProfileStringW(L"Camera", keys[i], value.c_str(), FilePath().wstring().c_str());
        gCameraWritten[i] = values[i];
    }
}

ik::Quat Trim(int side) {
    return gTrims[side == 0 ? 0 : 1];
}

double GunRoll() {
    return gValues.gunRoll;
}

bool BodyFollowsHead() {
    return gValues.bodyFollowsHead;
}

double ArmStretch() {
    return gValues.armStretch;
}

double TrackingScale() {
    return gValues.trackingScale;
}

double ArmLength() {
    return gValues.armLength;
}

Controls GetControls() {
    Controls controls;
    controls.leftTrigger = gValues.leftTrigger;
    controls.leftTriggerLB = gValues.leftTriggerLB;
    controls.gripLB = gValues.gripLB;
    controls.supportGrip = gValues.supportGrip;
    controls.acquireMeters = gValues.supportAcquire;
    controls.releaseMeters = gValues.supportRelease;
    controls.sniperAims = gValues.sniperAims;
    controls.aimAtShoulder = gValues.aimAtShoulder;
    return controls;
}

std::optional<ik::ArmLengths> MeasureArmLength(ik::ArmLengthSample const& sample) {
    auto const arms = ik::ArmLengthFromTPose(sample);
    if (!arms) {
        return std::nullopt;
    }
    bool const saved = WriteArmLength(arms->mean);
    gValues.armLength = arms->mean;
    log::Info("hands: arm length {:.3f} m (left {:.3f}, right {:.3f}, shoulder to grip, from a T pose){}", arms->mean, arms->left,
        arms->right, saved ? "; saved as [Arms] armLengthM" : "; NOT saved (ini write failed)");
    return arms;
}

void ClearArmLength() {
    gValues.armLength = 0.0;
    WriteArmLength(0.0);
    log::Info("hands: arm length cleared: reach is not scaled");
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
        std::ofstream(path) << kDefaultFile << kControlsBlock;
        log::Info("hands: wrote default calibration to {}", path.string());
    }
    auto stamp = fs::last_write_time(path, ec);
    if (ec || (gHaveStamp && stamp == gStamp)) {
        return;
    }
    Ini ini = ReadIni(path);
    if (ini.find("Controls") == ini.end()) {
        std::ofstream(path, std::ios::app) << kControlsBlock;  // an older file: show the new settings
        log::Info("hands: added the [Controls] defaults to {}", path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    gStamp = stamp;
    gHaveStamp = true;
    Values const values = ReadValues(ini);
    if (!gApplied || values != gValues) {
        gApplied = true;
        Apply(values);
        log::Info("hands: calibration pitch {:.1f}, yaw {:.1f}, roll {:.1f} degrees, gun roll {:.1f} degrees; body follows head {}, "
                  "arm stretch {:.2f}, tracking scale {}, arm length {} (from {})",
            values.pitch, values.yaw, values.roll, values.gunRoll, values.bodyFollowsHead ? "on" : "off", values.armStretch,
            values.trackingScale > 0.0 ? std::format("{:.1f} units/m", values.trackingScale) : std::string("the view's"),
            values.armLength > 0.0 ? std::format("{:.3f} m", values.armLength) : std::string("not calibrated"),
            path.filename().string());
        log::Info("controls: two-handed grip {} (acquire {:.2f} m, release {:.2f} m, sniper rifles {}), aimed fire {}, "
                  "left trigger {}",
            values.supportGrip ? "on" : "off", values.supportAcquire, values.supportRelease,
            values.sniperAims ? "aim too" : "steady only, no LT",
            values.aimAtShoulder ? "with both hands and the gun at the shoulder" : "with both hands",
            values.leftTrigger ? "= LT" : values.leftTriggerLB ? "= LB (weapon wheel)" : "not sent to the game");
        log::Info("controls: a left grip away from the gun is {}", values.gripLB ? "LB (weapon wheel)" : "nothing (it only takes the gun)");
    }
    // The menu's arm-length sample: each new measureRequest value is one T-pose sample. One
    // already there at startup with a result saved is old news.
    Sample const sample = ReadSample(ini);
    if (sample.hasRequest && (!gHaveRequest || sample.request != gLastRequest)) {
        bool const old = !gHaveRequest && values.armLength > 0.0;
        gHaveRequest = true;
        gLastRequest = sample.request;
        if (old) {
            return;
        }
        if (sample.fields != 9) {
            log::Warn("hands: arm-length sample #{} is incomplete ({} of 9 coordinates); ignored", sample.request, sample.fields);
        } else if (!MeasureArmLength(sample.poses)) {
            ik::ArmLengthSample const& p = sample.poses;
            log::Warn("hands: arm-length sample #{} isn't a T pose (head ({:.2f}, {:.2f}, {:.2f}), left ({:.2f}, {:.2f}, {:.2f}), "
                      "right ({:.2f}, {:.2f}, {:.2f}) m): hold both arms straight out to the sides; armLengthM unchanged",
                sample.request, p.head.x, p.head.y, p.head.z, p.left.x, p.left.y, p.left.z, p.right.x, p.right.y, p.right.z);
        }
    }
}

} // namespace mevr::game::hand_calibration
