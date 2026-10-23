#include "game/HandCalibration.hpp"

#include "core/Log.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/HandFrame.hpp"
#include "core/vr/Sights.hpp"

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
#include <vector>

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
    double reachNearFace = 0.30;  // [Hands] reachNearFaceM
    double armLength = 0.0;  // [Arms] armLengthM
    // [Controls]
    bool leftTrigger = false;
    bool leftTriggerLB = true;
    bool gripLB = false;
    bool supportGrip = true;
    bool supportCapsule = true;
    double supportAcquire = 0.18;
    double supportRelease = 0.26;
    double supportFront = 0.10;
    double supportPast = 0.10;
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
    "; reachNearFaceM: with armLengthM set, a hand this close to your eyes (meters) isn't reach-scaled: it\n"
    "; sits exactly on its controller (a gun at the cheek lines up with your eye); 0 = always scaled.\n"
    "reachNearFaceM = 0.30\n"
    "\n"
    "[Arms]\n"
    "; Arm-length calibration (the VR hands tab samples a T pose; the mod writes armLengthM, the\n"
    "; player's shoulder-to-grip length in meters, and scales reach with it; 0 or absent = off).\n";

// Appended to a settings file that has no [Controls] yet (and part of a new file).
constexpr char const* kControlsBlock =
    "\n"
    "[Controls]\n"
    "; The two-handed grip: squeeze the left grip with the left hand anywhere on the gun to hold it with\n"
    "; both hands: within supportAcquireM meters of the gun from supportFrontM ahead of your right hand to\n"
    "; supportPastMuzzleM past the muzzle (supportZone = capsule; point = near the foregrip only). Shepard's\n"
    "; left hand goes to the gun's foregrip; it lets go when the grip opens or the hand moves supportReleaseM\n"
    "; from the gun. While held, the barrel turns with the line between your hands, and at the shoulder the\n"
    "; game sees LT held: aimed, focused fire. Away from the gun the left grip does nothing.\n"
    "supportGrip = 1\n"
    "supportZone = capsule\n"
    "supportAcquireM = 0.18\n"
    "supportReleaseM = 0.26\n"
    "supportFrontM = 0.10\n"
    "supportPastMuzzleM = 0.10\n"
    "; sniperAims: 1 also holds LT for a sniper rifle (its aimed mode is the game's scope camera).\n"
    "sniperAims = 0\n"
    "; aimAtShoulder: 1 = aimed fire (LT) needs the gun at the shoulder as well (the right hand\n"
    "; close under the eyes, near the cheek); 0 = both hands on the gun is enough.\n"
    "aimAtShoulder = 1\n"
    "; leftTrigger: lb = the weapon wheel (the default; with [Squad] on, the trigger opens the game's wheel at\n"
    "; your left hand at once, and letting go gives the highlighted weapon); lt = the game's LT (aim, zoom), as\n"
    "; before; none = the game doesn't see it. Aimed fire comes from holding the gun with both hands at the shoulder.\n"
    "leftTrigger = lb\n"
    "; leftGripAway: what a left grip squeezed away from the gun does. none (the default): nothing, the\n"
    "; grip only takes the gun; lb = the weapon wheel (only with [Squad] off).\n"
    "leftGripAway = none\n";

// Appended to a settings file that has no [Holsters] yet (and part of a new file).
constexpr char const* kHolstersBlock =
    "\n"
    "[Holsters]\n"
    "; Body holsters: reach to a slot on your body with the right hand and squeeze the right grip to\n"
    "; draw that weapon. The controller buzzes while your hand is in a slot (stronger toward its middle).\n"
    "enabled = 1\n"
    "; gripHolds: 1 = the right grip holds the gun: keep it squeezed to carry the gun, let go at the gun's\n"
    "; slot to holster it. Let go anywhere else and it goes back to its holster after releaseGrace seconds,\n"
    "; unless you squeeze again (never while the trigger is held; -1 = it stays out). The right grip is then\n"
    "; never RB: powerWheel is (hold = power wheel, tap = your mapped power): r3 (the right stick click), y,\n"
    "; x, b or none. 0 = the old mapping: the right grip is RB, except a squeeze at a slot.\n"
    "gripHolds = 1\n"
    "releaseGrace = 0.3\n"
    "powerWheel = r3\n"
    "; weaponWheel: 1 keeps the game's weapon wheel (LB: the left trigger, see leftTrigger) as a fallback;\n"
    "; 0 turns LB off while holsters are on. The wheel also picks your squad's weapons.\n"
    "weaponWheel = 1\n"
    "; holster: 1 = put the gun away at its own slot (gripHolds 1: let go there; 0: squeeze and release\n"
    "; there, leaving the slot or pulling the trigger first cancels); 2 = also at a slot you have no weapon\n"
    "; for; 0 = never by hand.\n"
    "holster = 1\n"
    "; eyeHeightFromFloor: 1 = the slots scale with your standing eye height, measured from the headset's\n"
    "; floor (taken when you recenter standing straight, then only ever rising). eyeHeightM is used when\n"
    "; there's no usable floor, or with 0: your standing eye height in meters (about your height - 0.12).\n"
    "eyeHeightFromFloor = 1\n"
    "eyeHeightM = 1.65\n"
    "; hapticAmplitude: how strong the holster haptics are, 0..1 (0.5 as designed, 0 = no vibration).\n"
    "hapticAmplitude = 0.5\n"
    "; Each slot: weapon, right, up, forward, radius. weapon: pistol, shotgun, rifle, sniper or none.\n"
    "; Meters from your eyes (as if the head were upright), in your body's frame (it turns with you, not\n"
    "; with every glance), for a 1.65 m eye height: right is + to the right, up is + up, forward is +\n"
    "; ahead and - behind. radius: how close the hand must come (shoulders are bigger: you can't see them).\n"
    "hipRight = pistol, 0.20, -0.62, -0.05, 0.14\n"
    "shoulderRight = rifle, 0.18, -0.08, -0.20, 0.20\n"
    "belly = shotgun, 0.00, -0.50, 0.12, 0.13\n"
    "shoulderLeft = sniper, -0.18, -0.08, -0.20, 0.20\n";

// Appended to a settings file that has no [Squad] yet (and part of a new file).
constexpr char const* kSquadBlock =
    "\n"
    "[Squad]\n"
    "; Squad weapons: pull the left trigger to open the weapon wheel above your left hand (at once); point your\n"
    "; left hand at a squadmate as you pull to put it on their weapon. The left grip never opens it.\n"
    "enabled = 1\n"
    "; style: native = the game's own weapon wheel (ME1's look; it pauses the game like the flat game's wheel):\n"
    "; the right stick highlights a weapon, letting go of the trigger gives it (A queues one too, B closes it\n"
    "; without). custom = our drawn menu: the left stick picks the squadmate, a flick of the right stick a\n"
    "; weapon; let go (or A) to give it, B cancels.\n"
    "style = native\n"
    "; wheelStick (native): the stick that steers the wheel: right (the game's own) | left | either.\n"
    "wheelStick = right\n"
    "; wheelPreselect (native): 1 = pointing at a squadmate when you pull puts the wheel on their weapon in hand.\n"
    "wheelPreselect = 1\n"
    "; wheelPanel: 1 = the game's weapon and power wheels show on a panel of their own (however they open);\n"
    "; weaponWheelAt / powerWheelAt: where: left (hand) | right | front | hud (stays on the HUD panel).\n"
    "wheelPanel = 1\n"
    "weaponWheelAt = left\n"
    "powerWheelAt = hud\n"
    "; wheelCrop: the wheel's part of the game's UI frame: auto (measured the first time a wheel opens) or\n"
    "; x, y, width, height as fractions of the frame (the log's [MEVR-WHEEL] lines say what was measured).\n"
    "wheelCrop = auto\n"
    "; pointConeDeg: how close to your left hand's pointing a squadmate must be to be the one you point at\n"
    "; (degrees); pointMaxM: how far away at most (meters).\n"
    "pointConeDeg = 10\n"
    "pointMaxM = 40\n"

    "; panelDistanceM: the menu's centre from your left hand (meters); panelWidthM: its width (meters).\n"
    "panelDistanceM = 0.27\n"
    "panelWidthM = 0.40\n"
    "; confirmOnRelease: 1 = letting go of the trigger gives the chosen weapon (nothing chosen: cancels);\n"
    "; 0 = the menu stays open when you let go: A gives it, B or a new pull cancels.\n"
    "confirmOnRelease = 1\n"
    "; pause (custom): 1 (the default) = the game pauses while the menu is open, like its own weapon wheel; 0 = it\n"
    "; keeps running. The native wheel always pauses (it's the game's).\n"
    "pause = 1\n"
    "; hapticAmplitude: how strong the menu's ticks and clicks are, 0..1 (0.5 as designed, 0 = none).\n"
    "hapticAmplitude = 0.5\n";

// Appended to a settings file that has no [Throwables] yet (and part of a new file).
constexpr char const* kThrowablesBlock =
    "\n"
    "[Throwables]\n"
    "; Physical grenades: reach to the grenade slot on your body (the left side of the chest), squeeze a\n"
    "; grip to take a grenade, throw it by letting go while the hand moves. A slow release puts it back.\n"
    "enabled = 1\n"
    "; hands: right (the default) | left | both. The left grip on the gun (anywhere along it) is still\n"
    "; the two-handed grip.\n"
    "hands = right\n"
    "; slot: right, up, forward, radius in meters from your eyes, like the holsters' slots.\n"
    "slot = -0.13, -0.33, 0.00, 0.10\n"
    "; throwSpeedMinMS: a release slower than this (m/s) isn't a throw. throwGain: the throw's speed is the\n"
    "; hand's times this. throwSpeedMaxMS: the hand's speed is capped first.\n"
    "throwSpeedMinMS = 1.5\n"
    "throwGain = 2.5\n"
    "throwSpeedMaxMS = 15\n"
    "; flight: disc = the game's own grenade (a disc that flies straight and sticks), sent where your throw\n"
    "; would land; arc = the game's unused ballistic grenade, thrown with your hand's velocity (experimental).\n"
    "flight = disc\n"
    "; fromHand: 1 = the grenade starts at your hand. drawForThrow: 1 = taking a grenade with your weapon\n"
    "; away draws it (ME1 only throws with a weapon out). earlyRelease: the grip lets go this far below its\n"
    "; tightest squeeze while the hand moves fast (0 = only when opened).\n"
    "fromHand = 1\n"
    "drawForThrow = 1\n"
    "earlyRelease = 0.3\n"
    "; grenadeButton: 1 = X still throws a grenade the game's way; 0 = only the physical throw (on foot).\n"
    "grenadeButton = 1\n"
    "; Physical melee: a fast stroke of either hand toward an enemy in reach is the game's melee (pistol whip\n"
    "; or rifle butt). Speeds in m/s relative to your head; travel in meters; cooldown in seconds.\n"
    "melee = 1\n"
    "meleeSpeedMS = 3.0\n"
    "meleeGunSpeedMS = 3.5\n"
    "meleeTravelM = 0.12\n"
    "meleeCooldownS = 0.8\n"
    "; meleeNeedsEnemy: 1 = only with a living enemy within meleeReachM meters and meleeConeDeg degrees of your front.\n"
    "meleeNeedsEnemy = 1\n"
    "meleeReachM = 2.0\n"
    "meleeConeDeg = 75\n"
    "; meleeButton: 1 = B still melees; 0 = only the physical stroke (on foot).\n"
    "meleeButton = 1\n"
    "; hapticAmplitude: 0..1 (0.5 as designed, 0 = none). holdBuzz: 1 = a faint buzz while a grenade is held.\n"
    "hapticAmplitude = 0.5\n"
    "holdBuzz = 1\n";

// Appended to a settings file that has no [Sights] yet (and part of a new file).
constexpr char const* kSightsBlock =
    "\n"
    "[Sights]\n"
    "; Gun sights (research/21). Each weapon type gets: none, dot (a red dot), holo (a ring and a dot), or scope with\n"
    "; its magnification (scope 4: a magnified picture at the eyepiece). A dot shows only to the eye looking through\n"
    "; the sight's window, and stays on the target wherever that eye is behind it.\n"
    "enabled = 1\n"
    "pistol = dot\n"
    "shotgun = holo\n"
    "assaultRifle = holo\n"
    "sniperRifle = scope 4\n"
    "; Where the sight sits on each gun: auto, or back, height: units behind the muzzle along the barrel, units above\n"
    "; the bore (the log's `sights:` lines say what auto chose).\n"
    "pistolPlace = auto\n"
    "shotgunPlace = auto\n"
    "assaultRiflePlace = auto\n"
    "sniperRiflePlace = auto\n"
    "; dotColor: hex RRGGBB, or red, green, amber, cyan, white. dotSizeDeg, ringSizeDeg: how big the dot and the holo\n"
    "; ring look (degrees). windowUnits: the sight window's radius (gun units). frame: 1 = a faint ring marks it.\n"
    "dotColor = ff3020\n"
    "dotSizeDeg = 0.25\n"
    "ringSizeDeg = 1.6\n"
    "windowUnits = 3\n"
    "frame = 1\n"
    "; zeroM: where the sight line crosses the shot (game meters); 0 = parallel to the barrel (zeroed at infinity).\n"
    "zeroM = 0\n"
    "; Scopes. lensUnits: the eyepiece's radius (gun units). reticle: duplex, mildot or dot; reticleColor: its lit\n"
    "; centre. source: other = magnify the other eye's picture (the gun's own scope model doesn't block it), same =\n"
    "; the aiming eye's. focusM: where both eyes' pictures agree (game meters). eyeReliefM: how close (meters) your eye\n"
    "; must be to the eyepiece for the full picture. resolution: the picture's pixels across.\n"
    "lensUnits = 2\n"
    "reticle = duplex\n"
    "reticleColor = ff2020\n"
    "source = other\n"
    "focusM = 40\n"
    "eyeReliefM = 0.12\n"
    "resolution = 768\n"
    "; replaceRing: 1 = the floating ring reticle hides while you look through a sight.\n"
    "replaceRing = 1\n"
    "; aimedFire: 1 = looking through a scope gives the shots the game's aimed-fire accuracy (no scope camera).\n"
    "aimedFire = 1\n"
    "; assist: 1 = with the gun raised to your face and your aiming eye within assistConeDeg of the sight line,\n"
    "; the gun slides (at most assistMaxM meters) so the sight line runs through that eye; it lets go as you\n"
    "; lower the gun. assistStrength: how much of the way (0..1). assistRangeM: how far the sight may be from\n"
    "; the eye for it (meters).\n"
    "assist = 1\n"
    "assistConeDeg = 10\n"
    "assistStrength = 1\n"
    "assistMaxM = 0.10\n"
    "assistRangeM = 0.65\n";

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
    if (Number(ini, "Hands", "reachNearFaceM", values.reachNearFace)) {
        values.reachNearFace = std::clamp(values.reachNearFace, 0.0, 0.8);
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
        if (auto const k = s->second.find("supportZone"); k != s->second.end()) {
            std::string value = k->second;
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (value == "capsule" || value == "gun") {
                values.supportCapsule = true;
            } else if (value == "point" || value == "foregrip") {
                values.supportCapsule = false;
            } else {
                log::Warn("controls: can't read supportZone = {} (capsule or point); capsule kept", k->second);
            }
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
    if (Number(ini, "Controls", "supportFrontM", values.supportFront)) {
        values.supportFront = std::clamp(values.supportFront, 0.0, 0.5);
    }
    if (Number(ini, "Controls", "supportPastMuzzleM", values.supportPast)) {
        values.supportPast = std::clamp(values.supportPast, 0.0, 0.5);
    }
    if (Number(ini, "Controls", "sniperAims", flag)) {
        values.sniperAims = flag != 0.0;
    }
    return values;
}

// "weapon, right, up, forward, radius": the weapon alone keeps the slot's default place.
bool ReadSlot(std::string const& text, ik::body_slots::Slot& slot) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text + ",") {
        if (c == ',') {
            parts.push_back(Strip(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    auto const weapon = parts.empty() ? std::nullopt : ik::body_slots::ParseWeapon(parts[0]);
    if (!weapon) {
        return false;
    }
    double values[4] = {slot.offset.x, slot.offset.y, slot.offset.z, slot.radius};
    for (std::size_t i = 1; i < parts.size() && i <= 4; ++i) {
        char* end = nullptr;
        double const value = std::strtod(parts[i].c_str(), &end);
        if (end == parts[i].c_str() || !std::isfinite(value)) {
            return false;
        }
        values[i - 1] = value;
    }
    slot.weapon = *weapon;
    slot.offset = {std::clamp(values[0], -1.0, 1.0), std::clamp(values[1], -1.5, 0.5), std::clamp(values[2], -1.0, 1.0)};
    slot.radius = std::clamp(values[3], 0.03, 0.5);
    return true;
}

Holsters ReadHolsters(Ini const& ini) {
    Holsters holsters;
    double value = 0.0;
    if (Number(ini, "Holsters", "enabled", value)) {
        holsters.enabled = value != 0.0;
    }
    if (Number(ini, "Holsters", "weaponWheel", value)) {
        holsters.weaponWheel = value != 0.0;
    }
    if (Number(ini, "Holsters", "holster", value)) {
        int const mode = static_cast<int>(std::lround(value));
        holsters.holster = mode <= 0 ? ik::body_slots::Holster::Off
                         : mode == 1 ? ik::body_slots::Holster::OwnSlot
                                     : ik::body_slots::Holster::OwnOrEmptySlot;
    }
    if (Number(ini, "Holsters", "gripHolds", value)) {
        holsters.gripHolds = value != 0.0;
    }
    if (Number(ini, "Holsters", "releaseGrace", value)) {
        holsters.releaseGrace = value < 0.0 ? -1.0 : std::min(value, 5.0);
    }
    if (Number(ini, "Holsters", "eyeHeightFromFloor", value)) {
        holsters.eyeHeightFromFloor = value != 0.0;
    }
    if (Number(ini, "Holsters", "eyeHeightM", value)) {
        holsters.layout.eyeHeight = std::clamp(value, 0.8, 2.2);
    }
    if (Number(ini, "Holsters", "hapticAmplitude", value)) {
        holsters.hapticAmplitude = std::clamp(value, 0.0, 1.0);
    }
    if (auto const s = ini.find("Holsters"); s != ini.end()) {
        if (auto const k = s->second.find("powerWheel"); k != s->second.end()) {
            std::string text = k->second;
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (text == "r3" || text == "rs" || text == "stick") {
                holsters.powerWheel = gamepad::RbSource::RightStickClick;
            } else if (text == "y") {
                holsters.powerWheel = gamepad::RbSource::Y;
            } else if (text == "x") {
                holsters.powerWheel = gamepad::RbSource::X;
            } else if (text == "b") {
                holsters.powerWheel = gamepad::RbSource::B;
            } else if (text == "none" || text == "off") {
                holsters.powerWheel = gamepad::RbSource::None;
            } else {
                log::Warn("holsters: can't read powerWheel = {} (r3, y, x, b or none); r3 kept", k->second);
            }
        }
        for (ik::body_slots::Slot& slot : holsters.layout.slots) {
            if (auto const k = s->second.find(slot.name); k != s->second.end()) {
                ik::body_slots::Slot parsed = slot;
                if (ReadSlot(k->second, parsed)) {
                    slot = parsed;
                } else {
                    log::Warn("holsters: can't read {} = {} (weapon, right, up, forward, radius); default kept", slot.name, k->second);
                }
            }
        }
    }
    return holsters;
}

std::string Describe(Holsters const& h) {
    std::string const grip = h.gripHolds
        ? std::format("the right grip holds the gun (let go elsewhere: back after {}), RB from {}",
              h.releaseGrace >= 0.0 ? std::format("{:.2f} s", h.releaseGrace) : std::string("never"), gamepad::Name(h.powerWheel))
        : std::string("the right grip is RB except a squeeze at a slot (gripHolds 0)");
    std::string text = std::format("{}, {}, weapon wheel {}, holster {}, eye height {} (setting {:.2f} m), haptics {:.2f}; slots:",
        h.enabled ? "on" : "off", grip, h.weaponWheel ? "kept (LB)" : "off", static_cast<int>(h.holster),
        h.eyeHeightFromFloor ? "from the floor" : "from the setting", h.layout.eyeHeight, h.hapticAmplitude);
    for (ik::body_slots::Slot const& slot : h.layout.slots) {
        text += std::format(" {} {} ({:.2f}, {:.2f}, {:.2f}) r {:.2f};", slot.name, ik::body_slots::Name(slot.weapon), slot.offset.x,
            slot.offset.y, slot.offset.z, slot.radius);
    }
    return text;
}

Holsters gHolsters;
std::string gHolstersText;

Squad ReadSquad(Ini const& ini) {
    Squad squad;
    double value = 0.0;
    if (Number(ini, "Squad", "enabled", value)) {
        squad.enabled = value != 0.0;
    }
    if (Number(ini, "Squad", "pointConeDeg", value)) {
        squad.pointConeDeg = std::clamp(value, 1.0, 45.0);
    }
    if (Number(ini, "Squad", "pointMaxM", value)) {
        squad.pointMaxM = std::clamp(value, 1.0, 200.0);
    }
    if (Number(ini, "Squad", "panelDistanceM", value)) {
        squad.panelDistanceM = std::clamp(value, 0.1, 0.8);
    }
    if (Number(ini, "Squad", "panelWidthM", value)) {
        squad.panelWidthM = std::clamp(value, 0.15, 1.0);
    }
    if (Number(ini, "Squad", "confirmOnRelease", value)) {
        squad.confirmOnRelease = value != 0.0;
    }
    if (Number(ini, "Squad", "pause", value)) {
        squad.pause = value != 0.0;
    }
    if (Number(ini, "Squad", "hapticAmplitude", value)) {
        squad.hapticAmplitude = std::clamp(value, 0.0, 1.0);
    }
    if (Number(ini, "Squad", "wheelPreselect", value)) {
        squad.wheelPreselect = value != 0.0;
    }
    if (Number(ini, "Squad", "wheelPanel", value)) {
        squad.wheelPanel = value != 0.0;
    }
    auto const text = [&](char const* key) -> std::optional<std::string> {
        auto const s = ini.find("Squad");
        if (s == ini.end()) {
            return std::nullopt;
        }
        auto const k = s->second.find(key);
        if (k == s->second.end()) {
            return std::nullopt;
        }
        std::string t = k->second;
        std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return t;
    };
    if (auto const style = text("style")) {
        if (*style == "native" || *style == "game") {
            squad.native = true;
        } else if (*style == "custom" || *style == "ours") {
            squad.native = false;
        } else {
            log::Warn("squad: can't read style = {} (native or custom); native kept", *style);
        }
    }
    if (auto const stick = text("wheelStick")) {
        if (*stick == "right" || *stick == "r") {
            squad.wheelStick = gamepad::WheelStick::Right;
        } else if (*stick == "left" || *stick == "l") {
            squad.wheelStick = gamepad::WheelStick::Left;
        } else if (*stick == "either" || *stick == "both") {
            squad.wheelStick = gamepad::WheelStick::Either;
        } else {
            log::Warn("squad: can't read wheelStick = {} (right, left or either); right kept", *stick);
        }
    }
    auto const place = [&](char const* key, int& out) {
        if (auto const where = text(key)) {
            if (*where == "left") {
                out = 0;
            } else if (*where == "right") {
                out = 1;
            } else if (*where == "front") {
                out = 2;
            } else if (*where == "hud" || *where == "off" || *where == "none") {
                out = -1;
            } else {
                log::Warn("squad: can't read {} = {} (left, right, front or hud); default kept", key, *where);
            }
        }
    };
    place("weaponWheelAt", squad.weaponWheelAt);
    place("powerWheelAt", squad.powerWheelAt);
    if (auto const crop = text("wheelCrop"); crop && *crop != "auto" && !crop->empty()) {
        double values[4] = {};
        int n = 0;
        char const* p = crop->c_str();
        while (*p != '\0' && n < 4) {
            char* end = nullptr;
            double const v = std::strtod(p, &end);
            if (end == p) {
                ++p;  // separators
                continue;
            }
            values[n++] = v;
            p = end;
        }
        bool const ok = n == 4 && values[2] > 0.02 && values[3] > 0.02 && values[0] >= 0.0 && values[1] >= 0.0 &&
            values[0] + values[2] <= 1.0001 && values[1] + values[3] <= 1.0001;
        if (ok) {
            std::copy(values, values + 4, squad.wheelCrop);
        } else {
            log::Warn("squad: can't read wheelCrop = {} (auto, or x, y, width, height as fractions of the frame); auto kept", *crop);
        }
    }
    return squad;
}

char const* PlaceName(int place) {
    switch (place) {
    case 0: return "the left hand";
    case 1: return "the right hand";
    case 2: return "in front";
    default: return "the HUD panel";
    }
}

std::string Describe(Squad const& s) {
    std::string const style = s.native
        ? std::format("native (the game's wheel; {} steers it, {}{})", gamepad::Name(s.wheelStick),
              s.confirmOnRelease ? "letting go of the trigger gives the highlighted weapon" : "A queues, letting go closes",
              s.wheelPreselect ? ", pointing preselects" : "")
        : std::format("custom (our menu; {}; the game {} while it's open)",
              s.confirmOnRelease ? "letting go confirms" : "A confirms, letting go keeps it open", s.pause ? "pauses" : "keeps running");
    return std::format("{}, the left trigger opens it, style {}; pointing within {:.0f} degrees and {:.0f} m preselects; the panel "
                       "{:.2f} m from the hand, {:.2f} m wide; haptics {:.2f}; the game's wheels {} (weapon wheel at {}, power wheel at "
                       "{}, crop {})",
        s.enabled ? "on" : "off", style, s.pointConeDeg, s.pointMaxM, s.panelDistanceM, s.panelWidthM,
        s.hapticAmplitude, s.wheelPanel ? "on a panel of their own" : "on the HUD panel", PlaceName(s.weaponWheelAt),
        PlaceName(s.powerWheelAt),
        s.wheelCrop[2] > 0.0 ? std::format("({:.3f}, {:.3f}) {:.3f} x {:.3f}", s.wheelCrop[0], s.wheelCrop[1], s.wheelCrop[2], s.wheelCrop[3])
                             : std::string("auto"));
}

Squad gSquad;
std::string gSquadText;

Throwables ReadThrowables(Ini const& ini) {
    Throwables t;
    double value = 0.0;
    auto const flag = [&](char const* key, bool& out) {
        if (Number(ini, "Throwables", key, value)) {
            out = value != 0.0;
        }
    };
    auto const number = [&](char const* key, double& out, double lo, double hi) {
        if (Number(ini, "Throwables", key, value)) {
            out = std::clamp(value, lo, hi);
        }
    };
    flag("enabled", t.enabled);
    number("throwSpeedMinMS", t.throwSpeedMin, 0.3, 6.0);
    number("throwGain", t.throwGain, 0.5, 6.0);
    number("throwSpeedMaxMS", t.throwSpeedMax, 2.0, 30.0);
    flag("fromHand", t.fromHand);
    flag("drawForThrow", t.drawForThrow);
    number("earlyRelease", t.earlyRelease, 0.0, 0.9);
    flag("grenadeButton", t.grenadeButton);
    flag("melee", t.melee);
    number("meleeSpeedMS", t.meleeSpeed, 1.0, 10.0);
    number("meleeGunSpeedMS", t.meleeGunSpeed, 1.0, 10.0);
    number("meleeTravelM", t.meleeTravel, 0.02, 0.6);
    number("meleeCooldownS", t.meleeCooldown, 0.1, 5.0);
    flag("meleeNeedsEnemy", t.meleeNeedsEnemy);
    number("meleeReachM", t.meleeReach, 0.5, 6.0);
    number("meleeConeDeg", t.meleeConeDeg, 10.0, 180.0);
    flag("meleeButton", t.meleeButton);
    number("hapticAmplitude", t.hapticAmplitude, 0.0, 1.0);
    flag("holdBuzz", t.holdBuzz);
    auto const s = ini.find("Throwables");
    if (s == ini.end()) {
        return t;
    }
    auto const text = [&](char const* key) -> std::optional<std::string> {
        auto const k = s->second.find(key);
        if (k == s->second.end()) {
            return std::nullopt;
        }
        std::string v = k->second;
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return v;
    };
    if (auto const hands = text("hands")) {
        if (*hands == "both") {
            t.leftHand = t.rightHand = true;
        } else if (*hands == "left") {
            t.leftHand = true;
            t.rightHand = false;
        } else if (*hands == "right") {
            t.leftHand = false;
            t.rightHand = true;
        } else {
            log::Warn("throwables: can't read hands = {} (both, left or right); both kept", *hands);
        }
    }
    if (auto const flight = text("flight")) {
        if (*flight == "disc") {
            t.arc = false;
        } else if (*flight == "arc") {
            t.arc = true;
        } else {
            log::Warn("throwables: can't read flight = {} (disc or arc); disc kept", *flight);
        }
    }
    if (auto const slot = text("slot")) {
        // "right, up, forward, radius": the same numbers as a holster slot, without the weapon.
        ik::body_slots::Slot parsed = t.slot;
        if (ReadSlot("none, " + *slot, parsed)) {
            parsed.name = "grenade";
            parsed.weapon = ik::body_slots::Weapon::None;
            t.slot = parsed;
        } else {
            log::Warn("throwables: can't read slot = {} (right, up, forward, radius); default kept", *slot);
        }
    }
    return t;
}

std::string Describe(Throwables const& t) {
    return std::format("{}: grenades with {} from the slot ({:.2f}, {:.2f}, {:.2f}) r {:.2f}; a throw from {:.1f} m/s, x{:.1f} "
                       "(hand capped at {:.0f} m/s), flight {}{}{}, early release {:.2f}; X {}; melee {} (free hand {:.1f} m/s, gun "
                       "hand {:.1f} m/s, {:.2f} m, cooldown {:.1f} s, {}); B {}; haptics {:.2f}{}",
        t.enabled ? "on" : "off", t.leftHand && t.rightHand ? "either hand" : t.leftHand ? "the left hand" : "the right hand",
        t.slot.offset.x, t.slot.offset.y, t.slot.offset.z, t.slot.radius, t.throwSpeedMin, t.throwGain, t.throwSpeedMax,
        t.arc ? "arc (experimental)" : "disc", t.fromHand ? ", from the hand" : "", t.drawForThrow ? ", draws the weapon if away" : "",
        t.earlyRelease, t.grenadeButton ? "kept" : "off on foot", t.melee ? "on" : "off", t.meleeSpeed, t.meleeGunSpeed, t.meleeTravel,
        t.meleeCooldown,
        t.meleeNeedsEnemy ? std::format("an enemy within {:.1f} m and {:.0f} degrees", t.meleeReach, t.meleeConeDeg)
                          : std::string("any time"),
        t.meleeButton ? "kept" : "off on foot", t.hapticAmplitude, t.holdBuzz ? ", hold buzz" : "");
}

Throwables gThrowables;
std::string gThrowablesText;

vr::sights::Settings ReadSights(Ini const& ini) {
    namespace sv = vr::sights;
    sv::Settings out;
    auto const section = ini.find("Sights");
    if (section == ini.end()) {
        return out;
    }
    auto const text = [&](char const* key) -> std::optional<std::string> {
        auto const k = section->second.find(key);
        if (k == section->second.end() || k->second.empty()) {
            return std::nullopt;
        }
        return k->second;
    };
    double value = 0.0;
    if (Number(ini, "Sights", "enabled", value)) {
        out.enabled = value != 0.0;
    }
    for (int w = 0; w < sv::kWeaponTypes; ++w) {
        std::string const key = sv::WeaponKey(w);
        if (auto const t = text(key.c_str())) {
            if (auto const gun = sv::ParseGun(*t, out.guns[w])) {
                out.guns[w] = *gun;
            } else {
                log::Warn("sights: can't read {} = {} (none, dot, holo, or scope with a magnification); default kept", key, *t);
            }
        }
        std::string const placeKey = key + "Place";
        if (auto const t = text(placeKey.c_str()); t && !sv::ParsePlace(*t, out.guns[w])) {
            log::Warn("sights: can't read {} = {} (auto, or back, height in gun units); auto kept", placeKey, *t);
            out.guns[w].autoPlace = true;
        }
    }
    auto const color = [&](char const* key, std::uint32_t& into) {
        if (auto const t = text(key)) {
            if (auto const c = sv::ParseColor(*t)) {
                into = *c;
            } else {
                log::Warn("sights: can't read {} = {} (hex RRGGBB or red, green, amber, cyan, white); default kept", key, *t);
            }
        }
    };
    color("dotColor", out.dotColor);
    color("reticleColor", out.scopeColor);
    auto const number = [&](char const* key, float& into, float lo, float hi) {
        if (Number(ini, "Sights", key, value)) {
            into = static_cast<float>(std::clamp(value, static_cast<double>(lo), static_cast<double>(hi)));
        }
    };
    number("dotSizeDeg", out.dotSizeDeg, 0.05f, 3.0f);
    number("ringSizeDeg", out.ringSizeDeg, 0.3f, 6.0f);
    number("windowUnits", out.windowUnits, 0.5f, 20.0f);
    number("zeroM", out.zeroM, 0.0f, 500.0f);
    number("lensUnits", out.lensUnits, 0.5f, 10.0f);
    number("focusM", out.focusM, 2.0f, 1000.0f);
    number("eyeReliefM", out.eyeReliefM, 0.03f, 0.5f);
    if (Number(ini, "Sights", "frame", value)) {
        out.frame = value != 0.0;
    }
    if (Number(ini, "Sights", "replaceRing", value)) {
        out.replaceRing = value != 0.0;
    }
    if (Number(ini, "Sights", "aimedFire", value)) {
        out.aimedFire = value != 0.0;
    }
    if (Number(ini, "Sights", "assist", value)) {
        out.assist.enabled = value != 0.0;
    }
    number("assistConeDeg", out.assist.coneDeg, 1.0f, 30.0f);
    number("assistStrength", out.assist.strength, 0.0f, 1.0f);
    number("assistMaxM", out.assist.maxShiftM, 0.0f, 0.25f);
    number("assistRangeM", out.assist.rangeM, 0.15f, 1.5f);
    if (Number(ini, "Sights", "resolution", value)) {
        out.resolution = static_cast<int>(std::clamp(value, 128.0, 2048.0));
    }
    if (auto const t = text("reticle")) {
        if (auto const r = sv::ParseScopeReticle(*t)) {
            out.scopeReticle = *r;
        } else {
            log::Warn("sights: can't read reticle = {} (duplex, mildot or dot); duplex kept", *t);
        }
    }
    if (auto const t = text("source")) {
        std::string lower = *t;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower == "other") {
            out.otherEye = true;
        } else if (lower == "same") {
            out.otherEye = false;
        } else {
            log::Warn("sights: can't read source = {} (other or same); other kept", *t);
        }
    }
    return out;
}

std::string Describe(vr::sights::Settings const& s) {
    namespace sv = vr::sights;
    if (!s.enabled) {
        return "off";
    }
    std::string guns;
    for (int w = 0; w < sv::kWeaponTypes; ++w) {
        sv::Gun const& g = s.guns[w];
        guns += std::format("{}{} {}{}{}", guns.empty() ? "" : ", ", sv::WeaponName(w), sv::Name(g.kind),
            g.kind == sv::Kind::Scope ? std::format(" {:.1f}x", g.zoom) : std::string(),
            g.kind == sv::Kind::None ? std::string()
            : g.autoPlace         ? std::string(" (placed auto)")
                                  : std::format(" ({:.1f} back, {:.1f} up)", g.back, g.height));
    }
    return std::format("on: {}; dot #{:06X} {:.2f} deg, ring {:.2f} deg, window {:.1f} units{}, zero {}; scope lens {:.1f} units, "
                       "{} reticle #{:06X}, {} eye's picture, focus {:.0f} m, eye relief {:.2f} m, {} px; ring reticle {}, scoped "
                       "aimed fire {}; alignment assist {}",
        guns, s.dotColor, s.dotSizeDeg, s.ringSizeDeg, s.windowUnits, s.frame ? " (framed)" : "",
        s.zeroM > 0.0f ? std::format("{:.0f} m", s.zeroM) : std::string("parallel"), s.lensUnits, sv::Name(s.scopeReticle),
        s.scopeColor, s.otherEye ? "the other" : "the same", s.focusM, s.eyeReliefM, s.resolution,
        s.replaceRing ? "hidden while sighting" : "kept", s.aimedFire ? "on" : "off",
        s.assist.enabled ? std::format("on (the aiming eye within {:.0f} degrees of the sight line, {:.0f}% of the way, at most {:.2f} m, "
                                       "the sight within {:.2f} m of the eye)",
                               s.assist.coneDeg, s.assist.strength * 100.0f, s.assist.maxShiftM, s.assist.rangeM)
                         : std::string("off"));
}

vr::sights::Settings gSights;
std::string gSightsText;

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

double ReachNearFace() {
    return gValues.reachNearFace;
}

Controls GetControls() {
    Controls controls;
    controls.leftTrigger = gValues.leftTrigger;
    controls.leftTriggerLB = gValues.leftTriggerLB;
    controls.gripLB = gValues.gripLB;
    controls.supportGrip = gValues.supportGrip;
    controls.supportCapsule = gValues.supportCapsule;
    controls.acquireMeters = gValues.supportAcquire;
    controls.releaseMeters = gValues.supportRelease;
    controls.frontMeters = gValues.supportFront;
    controls.pastMeters = gValues.supportPast;
    controls.sniperAims = gValues.sniperAims;
    controls.aimAtShoulder = gValues.aimAtShoulder;
    return controls;
}

Holsters GetHolsters() {
    return gHolsters;
}

Squad GetSquad() {
    return gSquad;
}

Throwables GetThrowables() {
    return gThrowables;
}

vr::sights::Settings GetSights() {
    return gSights;
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
        std::ofstream(path) << kDefaultFile << kControlsBlock << kHolstersBlock << kSquadBlock << kSightsBlock << kThrowablesBlock;
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
    if (ini.find("Holsters") == ini.end()) {
        std::ofstream(path, std::ios::app) << kHolstersBlock;
        log::Info("hands: added the [Holsters] defaults to {}", path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    if (ini.find("Squad") == ini.end()) {
        std::ofstream(path, std::ios::app) << kSquadBlock;
        log::Info("hands: added the [Squad] defaults to {}", path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    if (ini.find("Throwables") == ini.end()) {
        std::ofstream(path, std::ios::app) << kThrowablesBlock;
        log::Info("hands: added the [Throwables] defaults to {}", path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    if (ini.find("Sights") == ini.end()) {
        std::ofstream(path, std::ios::app) << kSightsBlock;
        log::Info("hands: added the [Sights] defaults to {}", path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    // Keys added to [Holsters] since a file was written: appended with their defaults, so the file
    // shows them (their comments are in a new file's block and research/19).
    struct NewKey {
        wchar_t const* key;
        char const* name;
        wchar_t const* value;
    };
    static constexpr NewKey kNewHolsterKeys[] = {
        {L"gripHolds", "gripHolds", L"1"},
        {L"releaseGrace", "releaseGrace", L"0.3"},
        {L"powerWheel", "powerWheel", L"r3"},
        {L"eyeHeightFromFloor", "eyeHeightFromFloor", L"1"},
    };
    std::string added;
    for (NewKey const& k : kNewHolsterKeys) {
        auto const s = ini.find("Holsters");
        if (s != ini.end() && s->second.find(k.name) == s->second.end() &&
            WritePrivateProfileStringW(L"Holsters", k.key, k.value, path.wstring().c_str()) != 0) {
            added += std::format("{}{}", added.empty() ? "" : ", ", k.name);
        }
    }
    if (!added.empty()) {
        log::Info("hands: added {} (defaults) to [Holsters] in {}", added, path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    // Likewise [Squad]'s keys for the game's own wheel (2026-10-22).
    static constexpr NewKey kNewSquadKeys[] = {
        {L"style", "style", L"native"},
        {L"wheelStick", "wheelStick", L"right"},
        {L"wheelPreselect", "wheelPreselect", L"1"},
        {L"wheelPanel", "wheelPanel", L"1"},
        {L"weaponWheelAt", "weaponWheelAt", L"left"},
        {L"powerWheelAt", "powerWheelAt", L"hud"},
        {L"wheelCrop", "wheelCrop", L"auto"},
    };
    std::string addedSquad;
    for (NewKey const& k : kNewSquadKeys) {
        auto const s = ini.find("Squad");
        if (s != ini.end() && s->second.find(k.name) == s->second.end() &&
            WritePrivateProfileStringW(L"Squad", k.key, k.value, path.wstring().c_str()) != 0) {
            addedSquad += std::format("{}{}", addedSquad.empty() ? "" : ", ", k.name);
        }
    }
    if (!addedSquad.empty()) {
        log::Info("hands: added {} (defaults) to [Squad] in {}", addedSquad, path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    // The support grip's grab zone (2026-10-22): a file without supportZone was written for the point
    // zone, where supportAcquireM / supportReleaseM were distances from the foregrip. They are now the
    // capsule's radii: the old defaults (0.12 / 0.22) become the new ones (0.18 / 0.26); numbers the
    // player set are kept (now as the radii). The marker supportZone makes it happen once.
    if (auto const c = ini.find("Controls"); c != ini.end() && c->second.find("supportZone") == c->second.end()) {
        std::wstring const file = path.wstring();
        auto const old = [&](char const* key, double fallback) {
            auto const k = c->second.find(key);
            if (k == c->second.end()) {
                return fallback;
            }
            char* end = nullptr;
            double const v = std::strtod(k->second.c_str(), &end);
            return end == k->second.c_str() ? fallback : v;
        };
        double const acquire = old("supportAcquireM", 0.12);
        double const release = old("supportReleaseM", 0.22);
        bool const defaults = std::abs(acquire - 0.12) < 1e-6 && std::abs(release - 0.22) < 1e-6;
        if (defaults) {
            WritePrivateProfileStringW(L"Controls", L"supportAcquireM", L"0.18", file.c_str());
            WritePrivateProfileStringW(L"Controls", L"supportReleaseM", L"0.26", file.c_str());
        }
        WritePrivateProfileStringW(L"Controls", L"supportZone", L"capsule", file.c_str());
        if (c->second.find("supportFrontM") == c->second.end()) {
            WritePrivateProfileStringW(L"Controls", L"supportFrontM", L"0.10", file.c_str());
        }
        if (c->second.find("supportPastMuzzleM") == c->second.end()) {
            WritePrivateProfileStringW(L"Controls", L"supportPastMuzzleM", L"0.10", file.c_str());
        }
        log::Info("hands: the two-handed grip's grab zone is a capsule along the gun now: {} in [Controls] of {}",
            defaults ? "supportAcquireM 0.12 -> 0.18 and supportReleaseM 0.22 -> 0.26 (the old defaults), supportZone, supportFrontM "
                       "and supportPastMuzzleM added"
                     : std::format("your supportAcquireM {:.2f} and supportReleaseM {:.2f} kept, now the capsule's radii; supportZone, "
                                   "supportFrontM and supportPastMuzzleM added", acquire, release),
            path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    // [Hands] reachNearFaceM and the [Sights] alignment assist (2026-10-22), appended with their defaults.
    static constexpr struct {
        wchar_t const* section;
        char const* sectionName;
        wchar_t const* key;
        char const* name;
        wchar_t const* value;
    } kNewKeys[] = {
        {L"Hands", "Hands", L"reachNearFaceM", "reachNearFaceM", L"0.30"},
        {L"Sights", "Sights", L"assist", "assist", L"1"},
        {L"Sights", "Sights", L"assistConeDeg", "assistConeDeg", L"10"},
        {L"Sights", "Sights", L"assistStrength", "assistStrength", L"1"},
        {L"Sights", "Sights", L"assistMaxM", "assistMaxM", L"0.10"},
        {L"Sights", "Sights", L"assistRangeM", "assistRangeM", L"0.65"},
    };
    std::string addedKeys;
    for (auto const& k : kNewKeys) {
        auto const s = ini.find(k.sectionName);
        if (s != ini.end() && s->second.find(k.name) == s->second.end() &&
            WritePrivateProfileStringW(k.section, k.key, k.value, path.wstring().c_str()) != 0) {
            addedKeys += std::format("{}{}.{}", addedKeys.empty() ? "" : ", ", k.sectionName, k.name);
        }
    }
    if (!addedKeys.empty()) {
        log::Info("hands: added {} (defaults) to {}", addedKeys, path.string());
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    gStamp = stamp;
    gHolsters = ReadHolsters(ini);
    if (std::string const text = Describe(gHolsters); text != gHolstersText) {
        gHolstersText = text;
        log::Info("holsters: {}", text);
    }
    gSquad = ReadSquad(ini);
    if (std::string const text = Describe(gSquad); text != gSquadText) {
        gSquadText = text;
        log::Info("squad: settings: {}", text);
    }
    gThrowables = ReadThrowables(ini);
    if (std::string const text = Describe(gThrowables); text != gThrowablesText) {
        gThrowablesText = text;
        log::Info("throwables: settings: {}", text);
    }
    gSights = ReadSights(ini);
    if (std::string const text = Describe(gSights); text != gSightsText) {
        gSightsText = text;
        log::Info("sights: settings: {}", text);
    }
    gHaveStamp = true;
    Values const values = ReadValues(ini);
    if (!gApplied || values != gValues) {
        gApplied = true;
        Apply(values);
        log::Info("hands: calibration pitch {:.1f}, yaw {:.1f}, roll {:.1f} degrees, gun roll {:.1f} degrees; body follows head {}, "
                  "arm stretch {:.2f}, tracking scale {}, arm length {} (from {}), {}",
            values.pitch, values.yaw, values.roll, values.gunRoll, values.bodyFollowsHead ? "on" : "off", values.armStretch,
            values.trackingScale > 0.0 ? std::format("{:.1f} units/m", values.trackingScale) : std::string("the view's"),
            values.armLength > 0.0 ? std::format("{:.3f} m", values.armLength) : std::string("not calibrated"),
            path.filename().string(),
            values.reachNearFace > 0.0
                ? std::format("hands within {:.2f} m of the eyes unscaled (1:1 with the controllers)", values.reachNearFace)
                : std::string("reach scaled near the face too"));
        log::Info("controls: two-handed grip {} ({}, grab radius {:.2f} m, lets go past {:.2f} m, sniper rifles {}), aimed fire {}, "
                  "left trigger {}",
            values.supportGrip ? "on" : "off",
            values.supportCapsule
                ? std::format("a capsule along the gun from {:.2f} m ahead of the right hand to {:.2f} m past the muzzle, and the foregrip",
                      values.supportFront, values.supportPast)
                : std::string("the foregrip alone"),
            values.supportAcquire, values.supportRelease,
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
