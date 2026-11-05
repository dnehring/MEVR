// SPDX-License-Identifier: MIT
#include "game/HandCalibration.hpp"

#include "core/Ini.hpp"
#include "core/Log.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/HandFrame.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/vr/InteractLabel.hpp"
#include "core/vr/Sights.hpp"

#include "engine/GameAdapter.hpp"
#include "engine/SettingsFile.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <sstream>
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
    bool levelCamera = true;     // [Hands] levelCamera
    bool bodyMove = true;        // [Hands] bodyMove
    double leanM = 0.20;         // [Hands] leanM
    double bodyMoveMaxM = 0.75;  // [Hands] bodyMoveMaxM
    double recoilHaptic = 0.5;   // [Hands] recoilHaptic
    bool aimAssist = false;      // [Hands] aimAssist
    bool exploreStance = false;      // [Hands] exploreStance (research/38 A; off since 2026-10-25: the gun's bones follow the explore pose)
    bool exploreStanceStorm = true;  // [Hands] exploreStanceStorm
    bool exploreRamp = true;         // [Hands] exploreRamp (research/38 C)
    double drawRate = vr::combat_stance::kDefaultDrawRate;  // [Hands] drawRate (research/38 D)
    bool fingers = true;          // [Hands] fingers (research/43)
    double fingerCurlDeg = 70.0;  // [Hands] fingerCurlDeg
    bool fingerTouch = true;      // [Hands] fingerTouch (research/43 "Touch sensors")
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
    "; bodyMove: 1 (with bodyFollowsHead) also turns and steps Shepard's whole body (not God or Camera) under the\n"
    "; view: his hips take the turn his spine can't (an explore-mode Shepard faces where he runs), and his feet\n"
    "; follow your head once it leans more than leanM meters from them, at most bodyMoveMaxM from where the game\n"
    "; put him (research/29). 0: the spine alone, as before.\n"
    "bodyMove = 1\n"
    "leanM = 0.20\n"
    "bodyMoveMaxM = 0.75\n"
    "; armStretch: how far Shepard's arms may stretch to keep the hands on the controllers (1-1.25).\n"
    "armStretch = 1.25\n"
    "; trackingScale: Unreal units per meter of hand and head motion; 0 = the view's own scale.\n"
    "trackingScale = 0\n"
    "; reachNearFaceM: with armLengthM set, a hand this close to your eyes (meters) isn't reach-scaled: it\n"
    "; sits exactly on its controller (a gun at the cheek lines up with your eye); 0 = always scaled.\n"
    "reachNearFaceM = 0.30\n"
    "; Physical cover and crouch (VR on foot only). noCover: 1 = the game's cover snapping is off: take\n"
    "; cover by crouching or leaning behind things (a cover you're already in is left as A leaves it).\n"
    "; physicalCrouch: 1 = crouching for real crouches Shepard (a smaller target, ducked behind low cover);\n"
    "; L3 still toggles it until your next crouch or stand. crouchBelow / standAbove: the eye height, as a\n"
    "; fraction of your standing eye height, below which Shepard crouches and above which he stands again;\n"
    "; crouchDwellS: how long (seconds) the head must stay past it first.\n"
    "noCover = 1\n"
    "physicalCrouch = 1\n"
    "crouchBelow = 0.75\n"
    "standAbove = 0.85\n"
    "crouchDwellS = 0.2\n"
    "; levelCamera: 1 keeps the game camera level on foot while the hand aims (the guns' recoil and aim assist\n"
    "; no longer tilt the world; research/24). recoilHaptic: how strongly a shot's recoil buzzes the hand, 0..1.\n"
    "levelCamera = 1\n"
    "recoilHaptic = 0.5\n"
    "; aimAssist: 0 turns the game's aim assist friction and adhesion off on foot while the hand aims (the\n"
    "; stick's turn is never slowed or bent toward enemies; research/24); 1 keeps them. The Mako and the\n"
    "; sniper scope keep theirs either way.\n"
    "aimAssist = 0\n"
    "; The weapon drawn, like exploring (research/38). exploreStance: 1 = with a weapon out (standing, first person, VR\n"
    "; on foot) Shepard's body keeps the explore pose instead of the combat stance, the arms, hands and gun keep the\n"
    "; combat pose (learned on each weapon type's first draw); the game still plays its combat\n"
    "; animations underneath, so draws, shots, grenades and powers work as before. exploreStanceStorm: 1 = also while\n"
    "; sprinting. exploreRamp: 1 = walking with a weapon out speeds up and slows down as gently as exploring.\n"
    "; drawRate: how fast the draw and holster animations play (1 = the game's own, up to 10). constantEye (read by\n"
    "; the host): 1 = drawing, holstering, aimed fire and sprinting with a weapon out leave your view where it is.\n"
    "; blendFix (read by the host): 1 = a camera blend (the sniper's first draw, cover) doesn't jump your view.\n"
    "; crouchDrop (read by the host): 0 = when you crouch for real only your head lowers the view; 1 = the game's lower\n"
    "; crouch camera lowers it a second time (as before 2026-11-04). A crouch by the button while standing always lowers it.\n"
    "exploreStance = 0\n"
    "exploreStanceStorm = 1\n"
    "exploreRamp = 1\n"
    "drawRate = 6.0\n"
    "constantEye = 1\n"
    "blendFix = 1\n"
    "crouchDrop = 0\n"
    "; Fingers (research/43). fingers: 1 = in first person with tracked controllers each hand's index finger curls with its\n"
    "; controller's trigger and the other fingers with its grip (the thumb a little); with the weapon drawn the right hand\n"
    "; keeps its hold on the gun and only the index finger moves (off the trigger until you squeeze it), and the left hand\n"
    "; too while it holds the foregrip. fingerCurlDeg: how far each finger joint turns at a full curl, degrees (0-120).\n"
    "; fingerTouch: 1 = controllers with touch sensors (Touch, Index) say more: a finger lifted off the trigger points\n"
    "; (along the frame on the gun), a thumb lifted off the stick, buttons and thumb rest is up.\n"
    "fingers = 1\n"
    "fingerCurlDeg = 70\n"
    "fingerTouch = 1\n"
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
    "; onlyHolstersDraw: 1 = only a grab at a holster draws a weapon (the game's melee, grenade and power buttons and\n"
    "; its wheels don't draw it for you); 0 = the game draws your weapon whenever it wants one, as in the flat game.\n"
    "onlyHolstersDraw = 1\n"
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
    "hipLeft = shotgun, -0.20, -0.62, 0.00, 0.14\n"
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
    "; its magnification (scope 4: a magnified picture at the eyepiece; scope auto: the held weapon's own, as the game\n"
    "; zooms it). A dot shows only to the eye looking through the sight's window, and stays on the target wherever that\n"
    "; eye is behind it.\n"
    "enabled = 1\n"
    "pistol = dot\n"
    "shotgun = dot\n"
    "assaultRifle = dot\n"
    "sniperRifle = scope auto\n"
    "; Where the sight sits on each gun: auto, or back, height[, side]: units behind the muzzle along the barrel, units\n"
    "; above the bore, units to the right (the log's `sights:` lines say what auto chose: a bone of the model named for a\n"
    "; sight, followed live, else the bones' top).\n"
    "pistolPlace = auto\n"
    "shotgunPlace = auto\n"
    "assaultRiflePlace = auto\n"
    "sniperRiflePlace = auto\n"
    "; dotColor: hex RRGGBB, or red, green, amber, cyan, white. dotSizeDeg, ringSizeDeg: how big the dot and the holo\n"
    "; ring look (degrees). windowUnits: the sight window's radius (gun units). frame: 1 = a faint ring marks it.\n"
    "; eyeBox: your eye sees the dot while its line along the sight passes this close to the window's centre (gun units).\n"
    "dotColor = ff3020\n"
    "dotSizeDeg = 0.25\n"
    "ringSizeDeg = 1.6\n"
    "windowUnits = 3\n"
    "eyeBox = 6\n"
    "frame = 1\n"
    "; zeroM: where the sight line crosses the barrel's line (game meters); 0 = parallel to the barrel. converge: 1 = each\n"
    "; shot leaves the muzzle toward whatever the sight line hits, so it lands on the dot at any distance.\n"
    "zeroM = 0\n"
    "converge = 1\n"
    "; Scopes. lensUnits: the eyepiece's radius (gun units) on a model the mod doesn't know. reticle: game (the game's\n"
    "; own sniper scope view: its reticle, lens and tube), duplex, mildot or dot; reticleColor: their lit centre.\n"
    "; zoomStick: 1 = aiming down a scope with a second magnification of its own, a flick of the right stick forward\n"
    "; zooms in, back zooms out. source: other = magnify the other eye's picture (the gun's own scope model doesn't\n"
    "; block it), same = the aiming eye's. focusM: where both eyes' pictures agree (game meters). eyeReliefM: how close\n"
    "; (meters) your eye must be to the eyepiece for the full picture. resolution: the picture's pixels across.\n"
    "lensUnits = 2\n"
    "reticle = game\n"
    "zoomStick = 1\n"
    "reticleColor = ff2020\n"
    "source = other\n"
    "focusM = 40\n"
    "eyeReliefM = 0.12\n"
    "resolution = 768\n"
    "; The virtual eyepiece (you can't get your eye to a real scope's 8 cm): scopeFieldDeg: how wide the scope's picture\n"
    "; looks (degrees) wherever your eye is behind the eyepiece (0 = the physical lens above); scopeEyeBoxM: your eye this\n"
    "; far off the sight line shades half the picture. scopeMaxM: with ads = 0 only, how far behind the eyepiece it shows.\n"
    "scopeFieldDeg = 22\n"
    "scopeEyeBoxM = 0.05\n"
    "scopeMaxM = 0.70\n"
    "; Aiming down the sight (ADS): ads = 1 shows a scope's picture only once you bring the gun up to your eye (your\n"
    "; head behind the gun, looking along it, your eyes within adsReachM meters of the eyepiece, for a tenth of a second;\n"
    "; it goes when you lower it). Below that you see the gun's own scope. ads = 0: the picture whenever an eye is behind\n"
    "; the eyepiece (up to scopeMaxM). The pull: raised to your eye, a gun slides back along its line until its sight is\n"
    "; scopeReliefM in front of your eye (never inside the camera's near plane): a holo 5 cm at most, a low-power scope\n"
    "; (under 3x) 10 cm, other scopes scopePullMaxM, and a scope under ADS scopeAdsPullMaxM (faster); the picture then\n"
    "; sits on the model's eyepiece. scopePullMaxM = 0: no scope pulls. <gun>Lens: the model's eyepiece radius (gun\n"
    "; units; auto: the game's model's own, measured). <gun>Hide: the model's bones hidden while the scope's picture is up\n"
    "; (auto: the game's model's own; none: nothing).\n"
    "ads = 1\n"
    "adsReachM = 0.40\n"
    "scopeReliefM = 0.12\n"
    "scopePullMaxM = 0.15\n"
    "scopeAdsPullMaxM = 0.35\n"
    "assaultRifleLens = auto\n"
    "sniperRifleLens = auto\n"
    "sniperRifleHide = auto\n"
    "; replaceRing: 1 = the floating ring reticle hides while you look through a sight.\n"
    "replaceRing = 1\n"
    "; aimedFire: 1 = looking through a scope gives the shots the game's aimed-fire accuracy (no scope camera).\n"
    "aimedFire = 1\n"
    "; assist: 1 = as you raise the gun to your face and look along it, it slides so the sight line runs through your\n"
    "; aiming eye, more the more it's raised (at most assistMaxM meters for a scope, assistDotMaxM for a red dot or holo),\n"
    "; and lets go as you lower it; also the pull above. assistStrength: how much of the way (0..1).\n"
    "; assistConeDeg and assistRangeM are no longer used (since 2026-10-27 the ADS approach decides how raised the gun is).\n"
    "assist = 1\n"
    "assistStrength = 1\n"
    "assistMaxM = 0.10\n"
    "assistDotMaxM = 0.03\n"
    "; scopeCapture: 1 = the scope shows a real zoomed render of the world (the engine's scene capture, only while your eye\n"
    "; is behind the scope; research/39) instead of the other eye's picture enlarged (about 0.5 ms of render thread a frame\n"
    "; while you look through it). scopeCaptureSize: its pixels across. scopeCaptureRate: renders a second at most (150:\n"
    "; every frame, never twice in one). scopeCapturePost: 1 = the game's post-processing on it (its colour grade, but also a\n"
    "; black speckle in dark areas, a known issue); 0 = clean but ungraded. A change applies after scopeCapture goes 0 and 1.\n"
    "scopeCapture = 1\n"
    "scopeCaptureSize = 1024\n"
    "scopeCaptureRate = 150\n"
    "scopeCapturePost = 1\n";

// Appended to a settings file that has no [Comfort] yet (and part of a new file).
constexpr char const* kComfortBlock =
    "\n"
    "[Comfort]\n"
    "; Turning with the right stick on foot (research/31). turn: game = the game's own turn (the menu's Smooth);\n"
    "; snap = turn by snapDeg degrees per flick (flick past snapThreshold, back inside snapRearm for the next,\n"
    "; at least snapCooldownS apart); smooth = the mod's own steady turn at turnSpeed degrees a second (file only:\n"
    "; the game's rate can't be set, so the menu has no speed). The menu's Comfort page edits turn and snapDeg.\n"
    "turn = game\n"
    "snapDeg = 45\n"
    "deadzone = 0.15\n"
    "snapThreshold = 0.7\n"
    "snapRearm = 0.3\n"
    "snapCooldownS = 0.25\n"
    "; vignette: 1 = the edges of your view darken while the stick turns you (vignetteWhen = turn) or also while it\n"
    "; walks you (move), fading in and out over 0.15 s; vignetteStrength: how dark, 0..1.\n"
    "vignette = 0\n"
    "vignetteStrength = 0.6\n"
    "vignetteWhen = turn\n"
    "; walk: head = the left stick walks where you look; hand = where your left controller points (laid flat).\n"
    "walk = head\n";

// Appended to a settings file that has no [Interact] yet (and part of a new file).
constexpr char const* kInteractBlock =
    "\n"
    "[Interact]\n"
    "; Using things with A (research/40). gaze: 1 = the game picks what A uses from where you look (0: from where the\n"
    "; game's camera faces, the body and the stick). label: 1 = the game's prompt (the object's name, and the action\n"
    "; with the A button once you're close enough) shown big at the object instead of small on the HUD; labelHeightDeg:\n"
    "; its size (degrees of your view; the name's letters are about a seventh of it). combatLabel: 1 = an enemy's name\n"
    "; and health bars at the enemy too. crop = x y width height (fractions of the HUD frame) sets the prompt's part of\n"
    "; the HUD by hand; empty: the game's own place.\n"
    "gaze = 1\n"
    "label = 1\n"
    "labelHeightDeg = 6\n"
    "combatLabel = 0\n"
    "crop =\n";
// Appended to a settings file that has no [SquadContext] yet (and part of a new file).
constexpr char const* kSquadContextBlock =
    "\n"
    "[SquadContext]\n"
    "; The squad's world as squad orders see it (research/54): the party with its powers, the enemies in groups with words\n"
    "; like left/far/above, what the gun and your look point at, and a 4 s history of where you pointed and looked.\n"
    "; `mevr context [word]` in the console prints it. refreshS: how often it's read. joinM: enemies within this of a\n"
    "; group join it, unless they stand splitAboveM higher or lower. maxGroups: the nearest groups kept. nearM / farM: the\n"
    "; distance bands.\n"
    "enabled = 1\n"
    "refreshS = 0.25\n"
    "joinM = 6\n"
    "splitAboveM = 2.5\n"
    "maxGroups = 6\n"
    "nearM = 10\n"
    "farM = 30\n";
// Appended to a settings file that has no [SquadOrders] yet (and part of a new file).
constexpr char const* kSquadOrdersBlock =
    "\n"
    "[SquadOrders]\n"
    "; The game's squad orders by gesture (research/54): squeeze the LEFT grip with the hand away from the gun and the\n"
    "; grenade slot, point, let go: on an enemy the squad attacks it; on the ground farther than followWithinM they move\n"
    "; there; near you, on a squadmate or on nothing they regroup. Pointing at a squadmate as you squeeze orders them alone.\n"
    "; gesture = 0 keeps only the console's `mevr order`. enemyConeDeg / memberConeDeg: how close the ray must pass.\n"
    "; minHoldS: a shorter squeeze is a blip. maxHoldS: held longer, the order is called off.\n"
    "enabled = 1\n"
    "gesture = 1\n"
    "followWithinM = 2\n"
    "enemyConeDeg = 8\n"
    "memberConeDeg = 12\n"
    "minHoldS = 0.08\n"
    "maxHoldS = 8\n"
    "; minConfidence: `mevr say <words>` (the typed stand-in for speech) gives an order only when the rule parser is this sure.\n"
    "minConfidence = 0.6\n"
    "; Standing orders: \"hold the bridge\" is kept and re-ordered when they drift (holdRadiusM, every reassertS at most), ends\n"
    "; calmS after the fighting stops (then they come back), and is given up when hurt (retreatHealth) or overwhelmed\n"
    "; (overwhelmCount hostiles within 8 m). standing = 0: every order is one-shot, as the game's own.\n"
    "standing = 1\n"
    "reassertS = 4\n"
    "holdRadiusM = 4\n"
    "calmS = 8\n"
    "retreatHealth = 0.3\n"
    "overwhelmCount = 3\n";
// Appended to a settings file that has no [Vent] yet (and part of a new file).
constexpr char const* kVentBlock =
    "\n"
    "[Vent]\n"
    "; The physical vent (research/45; a mechanic change, docs/trilogy-design.md): with a weapon drawn, squeeze the\n"
    "; left grip on the gun's receiver (over and just ahead of your right hand, behind the foregrip) and pull it back\n"
    "; and away in one fast stroke, letting go, like racking a slide: the gun's heat is gone at once. Both hands pulse.\n"
    "enabled = 1\n"
    "; ventCooldownS: seconds between vents. ventMinHeat: below this much heat (0..1 of the overheat) there's nothing\n"
    "; to vent (a squeeze there is just a grip). ventMaxHeat: the heat at which the gun overheats (the game's scale; the\n"
    "; log line 'vent: ... overheated at' says what it saw).\n"
    "ventCooldownS = 4\n"
    "ventMinHeat = 0.3\n"
    "ventMaxHeat = 1.0\n"
    "; ventSpeedMS: how fast the hand must leave (m/s, relative to the gun). ventTravelM: how far back along the gun\n"
    "; it must have come (m). ventHoldMaxS: a squeeze held longer than this is a grip, not a rack.\n"
    "ventSpeedMS = 1.0\n"
    "ventTravelM = 0.05\n"
    "ventHoldMaxS = 1.5\n"
    "; effect: 1 = the game's own cool-down effect plays on a vent. hapticAmplitude: 0..1 (0.5 as designed, 0 = none).\n"
    "effect = 1\n"
    "hapticAmplitude = 0.5\n"
    "; The receiver zone, meters from your right hand in the gun's frame: behind it, ahead of it, above the bore, below\n"
    "; it (shallow: a hand cupped under a pistol's grip stays the two-handed grip), to either side.\n"
    "zoneBackM = 0.06\n"
    "zoneFrontM = 0.14\n"
    "zoneAboveM = 0.14\n"
    "zoneBelowM = 0.03\n"
    "zoneSideM = 0.09\n";

// Appended to a settings file that has no [Hud] yet (and part of a new file).
constexpr char const* kHudBlock =
    "\n"
    "[Hud]\n"
    "; HUD pieces on the body (research/41). wristStatus: 1 = your health and shields (the arc with the squad's portraits\n"
    "; and the medi-gel count) on the back of your left wrist, wristWidthM wide (meters); wristOffset = x y z moves it, in\n"
    "; the left controller's grip axes (meters: x out of the palm, y toward the elbow, z down the handle). gunStatus: 1 =\n"
    "; the weapon panel (the gun, its heat strip, the grenade count) on the drawn gun, gunWidthM wide, gunBackM behind the\n"
    "; muzzle and gunUpM above the bore. wristCrop / gunCrop = x y width height (fractions of the HUD frame) set a piece's\n"
    "; part of the HUD by hand; empty: the measured place.\n"
    "wristStatus = 1\n"
    "wristWidthM = 0.08\n"
    "wristOffset = -0.03 0.06 0.05\n"
    "gunStatus = 1\n"
    "gunWidthM = 0.06\n"
    "gunBackM = 0.15\n"
    "gunUpM = 0.04\n"
    "wristCrop =\n"
    "gunCrop =\n";

// Appended to a settings file that has no [Mako] yet (and part of a new file).
constexpr char const* kMakoBlock =
    "\n"
    "[Mako]\n"
    "; The Mako in VR (research/44). handAim: 1 = the cannon and machine gun fire where your right controller points\n"
    "; (at what its ray hits), not where the camera faces. turret: 1 = the turret model turns there too, through the\n"
    "; game's own turret aim; turretSeam: 1 = asked after each frame, 2 = asked inside the vehicle's own tick, 4 = written before the pose; add them (3 = the first two, 7 = all)\n"
    "; (the log says which one the game lets stand). reticle: 1 = the hand reticle shows in the Mako, on the controller's\n"
    "; ray. vignette: 1 = the comfort vignette ([Comfort] vignette, vignetteWhen) also while the Mako turns (turnRate\n"
    "; degrees a second) or, with vignetteWhen = move, drives (moveSpeed units a second; 100 units = 1 m). aimRangeM: how\n"
    "; far the ray is traced. calibratedLine: 1 = a saved pistol calibration's line stands in for the controller's own\n"
    "; aim pose, as on foot.\n"
    "handAim = 1\n"
    "turret = 1\n"
    "turretSeam = 3\n"
    "reticle = 1\n"
    "vignette = 1\n"
    "moveSpeed = 150\n"
    "turnRate = 20\n"
    "aimRangeM = 200\n"
    "calibratedLine = 1\n"
    "; Physical controls (research/46). controls: 1 = a throttle lever on the seat's left armrest (grip it, push forward or\n"
    "; pull back; let go and it springs back to neutral; X is jump and upright, Y is boost while held) and a drive stick on the right one\n"
    "; (grip it, TILT it left and right to steer, forward and back to pitch the Mako while its thrusters fire; it springs back;\n"
    "; stickTwist = 1: twist it to steer instead; the trigger fires the machine gun, A the cannon, the thumbstick aims the\n"
    "; turret while held; let go and your hand aims it). Until you grab one the game's own controls work as always; 0 = they\n"
    "; stay. grabRadiusM: how near a hand must be. stickTiltDeg: the tilt for full lock; stickHeightM: the grip's height over\n"
    "; the stick's pivot (0.20: about 7 cm of hand travel for full lock). leverReturns: 1 = let go, the lever goes back to\n"
    "; neutral. stickTwistDeg: the twist for full lock (stickTwist = 1); stickDeadZoneDeg: the tilt or twist that\n"
    "; steers nothing; stickInvert: 1 = twist the other way. throttleTravelM: hand travel from neutral to full;\n"
    "; throttleDetent: the neutral notch, a fraction of the travel. leverOffset, stickOffset: where they sit from your head\n"
    "; when you enter the Mako or recenter (meters: right, up, forward). stickAimRate: degrees a second at a full thumbstick.\n"
    "; rumble: the engine's vibration at full speed (0 = none). cameraFollow: 1 = while you drive with them the chase camera\n"
    "; keeps its angle to the Mako. thrusterBurnScale: the jump jets' and booster's fuel burns this fast (0.4 = they last 2.5\n"
    "; times as long; 1 = the game's). thrusterStabilize: 1 = while the jump jets fire the Mako is held level, tipped by the\n"
    "; stick up to thrusterPitchDeg forward or back and thrusterRollDeg to a side (the jets push it that way), turning at most\n"
    "; thrusterRateDeg a second; the booster is the game's own. stickPitchDeadZoneDeg: the tilt that tips nothing. steerInPlaceBelow: below this throttle the stick turns the Mako on the spot.\n"
    "controls = 1\n"
    "grabRadiusM = 0.15\n"
    "stickTwist = 0\n"
    "stickTiltDeg = 20\n"
    "stickHeightM = 0.20\n"
    "leverReturns = 1\n"
    "stickTwistDeg = 42\n"
    "stickDeadZoneDeg = 4\n"
    "stickInvert = 0\n"
    "throttleTravelM = 0.055\n"
    "throttleDetent = 0.15\n"
    "leverOffset = -0.362 -0.446 0.286\n"
    "stickOffset = 0.375 -0.415 0.314\n"
    "stickAimRate = 90\n"
    "rumble = 0.12\n"
    "cameraFollow = 1\n"
    "thrusterBurnScale = 0.4\n"
    "thrusterStabilize = 1\n"
    "thrusterPitchDeg = 20\n"
    "thrusterRollDeg = 15\n"
    "thrusterRateDeg = 60\n"
    "stickPitchDeadZoneDeg = 8\n"
    "steerInPlaceBelow = 0.2\n"
    "tiltSteerMax = 0.5\n"
    "spinRateDeg = 60\n";

// Appended to a settings file that has no [Cockpit] yet (and part of a new file).
constexpr char const* kCockpitBlock =
    "\n"
    "[Cockpit]\n"
    "; The Mako's cockpit (research/47). enabled: 1 = in the Mako you sit in the cockpit, the Mako's third-person view on its\n"
    "; centre screen (needs the cockpit's package installed); 0 = the game's chase camera as before. script: the cockpit's\n"
    "; layout, from this folder. followCamera: 1 = the screen's chase camera turns with the Mako. seated: 1 = Shepard sits in\n"
    "; the chair under you, the hands on the lever and stick while you hold them. hazard: 1 = with the hull's health under\n"
    "; hazardBelow (a fraction) the cabin's lights turn red and flash every hazardPeriodS seconds (off again once repaired\n"
    "; past it by 0.10); hazardWash: 1 = the whole cabin goes red with the lamps, 0 = only the lamps.\n"
    "enabled = 1\n"
    "script = cockpit\\cockpit.txt\n"
    "followCamera = 1\n"
    "seated = 1\n"
    "hazard = 1\n"
    "hazardBelow = 0.30\n"
    "hazardPeriodS = 1.1\n"
    "hazardWash = 1\n"
    "; showTurret: 1 = the Mako's own turret stays in view over the cabin and its shots come from the gun's muzzle; 0 = hidden,\n"
    "; the game's shot start (in front of your face).\n"
    "showTurret = 1\n";

// Appended to a settings file that has no [Game] yet (and part of a new file).
constexpr char const* kGalaxyBlock =
    "\n"
    "[Galaxy]\n"
    "; The Normandy's galaxy map (research/51). hologram = 1: a hologram of the map (clusters, systems, planets in 3D): point\n"
    "; at one with a controller and pull the trigger to select it. inWorld = 1: it stands over the CIC's projector, where the\n"
    "; game shows its own; walk up and pick from it on foot (the map opens behind the scenes, you stay in the CIC; walk away\n"
    "; to close it). worldRadiusM its size, worldTiltDeg how far its far edge is raised, worldLiftM up from the projector.\n"
    "; window = 1: once the map is open, the game's own map (its 3D scene in stereo, its labels and buttons) on a window over\n"
    "; the table, pointed at with a controller as the game's mouse; windowWidthM its width, windowDepth its depth, windowRound\n"
    "; = 1 a disc like the projector's own hologram (0: a rectangle), windowOnHologram = 1 where that hologram is, at its tilt\n"
    "; and size (0: upright over the table, windowWidthM wide). windowBackdrop: how opaque the map's space is behind its\n"
    "; galaxy and stars (0..1; 1 as on the full screen, 0 only the bright parts cover the room).\n"
    "; inWorld = 0: the map opens as the game's, with the hologram on a table in front of you (tableDistanceM / tableBelowM\n"
    "; ahead of and below your eyes, tableRadiusM its size) and the game's map behind it as a screen. hologram = 0: the\n"
    "; game's map on the flat screen only.\n"
    "hologram = 1\n"
    "inWorld = 1\n"
    "worldRadiusM = 1.40\n"
    "worldTiltDeg = 35\n"
    "worldLiftM = 0\n"
    "window = 1\n"
    "windowWidthM = 2.0\n"
    "windowDepth = 0.03\n"
    "windowRound = 1\n"
    "windowOnHologram = 1\n"
    "windowBackdrop = 0.8\n"
    "tableDistanceM = 0.70\n"
    "tableBelowM = 0.45\n"
    "tableRadiusM = 0.48\n"
    "distanceM = 0.9\n"
    "belowM = 0.30\n";

constexpr char const* kWorldUiBlock =
    "\n"
    "[WorldUi]\n"
    "; The game's full-screen screens in the room (research/53): with on = 1 each one (the shop, loot, equipment, journal, map,\n"
    "; the minigame...) stands as a panel in front of you where it opened, toward the vendor or container, the world around, its dark\n"
    "; parts see-through like a hologram; point at it with a controller as the game's mouse. widthM its width, distanceM how\n"
    "; far ahead, belowM its centre below your eyes, backdrop how opaque its dark parts are (1: solid). screens: the screens\n"
    "; put in the room (the game's own names: shop, Loot, Inventory, XMods, CharacterRecord, Specialization, Journal, Codex,\n"
    "; Map, PartySelect, MenuBrowser, Options, Save, Load, MessageBox, Achievement, AdditionalContent, SkillGame, DesignerUI,\n"
    "; GameOver; all = every one). The loot stands at its container, the minigame and a locker's equipment at what you used.\n"
    "; on = 0: the game's screens on the flat panel, the world gone (Halcyon's own). Inventory, XMods and CharacterRecord show a\n"
    "; 3D model of their own in the game's UI world, which isn't in the room yet.\n"
    "; wrist: the screens projected from your left wrist instead, Shepard's omni-tool (SkillGame: the bypass minigame; none: all\n"
    "; in the room), wristWidthM wide, its bottom edge wristAboveM over the wrist as you see it, moved wristTowardM toward your\n"
    "; eyes (< 0 away); wristFollow 1: it rides on the wrist, 0: it stays where it was projected; wristBeam: a faint fan of\n"
    "; light from the wrist to it; omniTool 1: your left forearm wears the game's own omni-tool hologram while it's up.\n"
    "on = 1\n"
    "widthM = 1.20\n"
    "distanceM = 0.95\n"
    "belowM = 0.50\n"
    "backdrop = 0.80\n"
    "screens = all\n"
    "wrist = SkillGame\n"
    "wristWidthM = 0.30\n"
    "wristAboveM = 0.10\n"
    "wristTowardM = 0.00\n"
    "wristFollow = 1\n"
    "wristBeam = 1\n"
    "omniTool = 1\n";

constexpr char const* kGameBlock =
    "\n"
    "[Game]\n"
    "; The game's own behaviour around VR. pauseUnfocused: 1 = the game freezes everything but you while its window\n"
    "; doesn't have the focus, as Mass Effect does (a click beside the window or the headset's dashboard can do that);\n"
    "; 0 = it runs on.\n"
    "pauseUnfocused = 1\n";

fs::path FilePath() {
    return engine::SettingsFile();  // <exe dir>\ASI\<GAME>MEVR.ini, the host's file too
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

// "[Section]" -> "key" -> value text, read as Win32's profile API reads it (core/Ini: names case-insensitive, the
// first occurrence wins, a BOM stripped, UTF-16 decoded), so the mod, the host and the key appends below agree.
using Ini = ini::File;

ini::Encoding gEncoding = ini::Encoding::Narrow;  // the settings file's, as last read

Ini ReadIni(fs::path const& path) {
    ini::Loaded loaded = ini::Read(path);
    gEncoding = loaded.encoding;
    return std::move(loaded.file);
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
    std::optional<double> const value = ini::Number(k->second);
    if (!value) {
        return false;
    }
    out = *value;
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
    if (Number(ini, "Hands", "levelCamera", flag)) {
        values.levelCamera = flag != 0.0;
    }
    if (Number(ini, "Hands", "bodyMove", flag)) {
        values.bodyMove = flag != 0.0;
    }
    if (Number(ini, "Hands", "leanM", values.leanM)) {
        values.leanM = std::clamp(values.leanM, 0.0, 0.6);
    }
    if (Number(ini, "Hands", "bodyMoveMaxM", values.bodyMoveMaxM)) {
        values.bodyMoveMaxM = std::clamp(values.bodyMoveMaxM, 0.0, 3.0);
    }
    if (Number(ini, "Hands", "recoilHaptic", values.recoilHaptic)) {
        values.recoilHaptic = std::clamp(values.recoilHaptic, 0.0, 1.0);
    }
    if (Number(ini, "Hands", "aimAssist", flag)) {
        values.aimAssist = flag != 0.0;
    }
    if (Number(ini, "Hands", "exploreStance", flag)) {
        values.exploreStance = flag != 0.0;
    }
    if (Number(ini, "Hands", "exploreStanceStorm", flag)) {
        values.exploreStanceStorm = flag != 0.0;
    }
    if (Number(ini, "Hands", "exploreRamp", flag)) {
        values.exploreRamp = flag != 0.0;
    }
    if (Number(ini, "Hands", "drawRate", values.drawRate)) {
        values.drawRate = vr::combat_stance::ClampDrawRate(values.drawRate);
    }
    if (Number(ini, "Hands", "fingers", flag)) {
        values.fingers = flag != 0.0;
    }
    if (Number(ini, "Hands", "fingerCurlDeg", values.fingerCurlDeg)) {
        values.fingerCurlDeg = std::clamp(values.fingerCurlDeg, 0.0, 120.0);
    }
    if (Number(ini, "Hands", "fingerTouch", flag)) {
        values.fingerTouch = flag != 0.0;
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
        std::optional<double> const value = ini::Number(parts[i]);
        if (!value) {
            return false;
        }
        values[i - 1] = *value;
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
    if (Number(ini, "Holsters", "onlyHolstersDraw", value)) {
        holsters.onlyHolstersDraw = value != 0.0;
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
            auto k = s->second.find(slot.name);
            // The shotgun's slot was "belly" until 2026-10-25: a belly line with its old default values is ignored (the
            // new hipLeft default applies), one the player changed is kept.
            if (k == s->second.end() && std::string_view(slot.name) == "hipLeft") {
                if (auto const belly = s->second.find("belly"); belly != s->second.end()) {
                    ik::body_slots::Slot old = slot;
                    bool const isOldDefault = ReadSlot(belly->second, old) && old.weapon == ik::body_slots::Weapon::Shotgun &&
                        old.offset.x == 0.0 && old.offset.y == -0.50 && old.offset.z == 0.12 && old.radius == 0.13;
                    if (isOldDefault) {
                        log::Info("holsters: the old belly slot's default line is ignored: the shotgun is at the left hip "
                                  "(hipLeft) now");
                    } else {
                        log::Info("holsters: your belly slot line is kept for the shotgun's slot (hipLeft is its name now)");
                        k = belly;
                    }
                }
            }
            if (k != s->second.end()) {
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
    std::string text = std::format("{}, {}, weapon wheel {}, holster {}, {}, eye height {} (setting {:.2f} m), haptics {:.2f}; slots:",
        h.enabled ? "on" : "off", grip, h.weaponWheel ? "kept (LB)" : "off", static_cast<int>(h.holster),
        h.onlyHolstersDraw ? "only holsters draw" : "the game may draw",
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
        char const* p = crop->data();
        char const* const last = crop->data() + crop->size();
        while (p < last && n < 4) {
            double v = 0.0;
            auto const [end, ec] = std::from_chars(p, last, v);  // locale-independent
            if (ec != std::errc() || end == p) {
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
            log::Warn("throwables: can't read hands = {} (both, left or right); {} kept", *hands,
                t.leftHand && t.rightHand ? "both" : t.leftHand ? "left" : "right");
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

vr::comfort::Settings ReadComfort(Ini const& ini) {
    namespace cf = vr::comfort;
    cf::Settings c;
    double value = 0.0;
    auto const number = [&](char const* key, float& out) {
        if (Number(ini, "Comfort", key, value)) {
            out = static_cast<float>(value);
        }
    };
    number("snapDeg", c.snapDegrees);
    number("turnSpeed", c.turnSpeed);
    number("deadzone", c.deadzone);
    number("snapThreshold", c.snapThreshold);
    number("snapRearm", c.snapRearm);
    number("snapCooldownS", c.snapCooldown);
    number("vignetteStrength", c.vignetteStrength);
    if (Number(ini, "Comfort", "vignette", value)) {
        c.vignette = value != 0.0;
    }
    if (auto const s = ini.find("Comfort"); s != ini.end()) {
        auto const text = [&](char const* key) -> std::optional<std::string> {
            auto const k = s->second.find(key);
            return k != s->second.end() && !k->second.empty() ? std::optional<std::string>(k->second) : std::nullopt;
        };
        if (auto const t = text("turn")) {
            if (auto const turn = cf::ParseTurn(*t)) {
                c.turn = *turn;
            } else {
                log::Warn("comfort: can't read turn = {} (smooth, snap or game); {} kept", *t, cf::Name(c.turn));
            }
        }
        if (auto const t = text("vignetteWhen")) {
            if (auto const when = cf::ParseVignetteWhen(*t)) {
                c.vignetteWhen = *when;
            } else {
                log::Warn("comfort: can't read vignetteWhen = {} (turn or move); {} kept", *t, cf::Name(c.vignetteWhen));
            }
        }
        if (auto const t = text("walk")) {
            if (auto const walk = cf::ParseWalk(*t)) {
                c.walk = *walk;
            } else {
                log::Warn("comfort: can't read walk = {} (head or hand); {} kept", *t, cf::Name(c.walk));
            }
        }
    }
    return cf::Sanitize(c);
}

std::string Describe(vr::comfort::Settings const& c) {
    namespace cf = vr::comfort;
    std::string const turn = c.turn == cf::TurnStyle::Snap
        ? std::format("snap {:.0f} deg per flick (past {:.2f}, again once back inside {:.2f}, at least {:.2f} s apart)", c.snapDegrees,
              c.snapThreshold, c.snapRearm, c.snapCooldown)
        : c.turn == cf::TurnStyle::Smooth ? std::format("smooth {:.0f} deg/s at full deflection (dead zone {:.2f})", c.turnSpeed, c.deadzone)
                                          : std::string("the game's own (turn = game)");
    std::string const vignette = c.vignette
        ? std::format("on at {:.0f}% while {}", c.vignetteStrength * 100.0f,
              c.vignetteWhen == cf::VignetteWhen::TurningAndMoving ? "turning or moving" : "turning")
        : std::string("off");
    return std::format("turning {}; vignette {}; walk toward {}", turn, vignette,
        c.walk == cf::WalkToward::LeftHand ? "the left hand" : "where you look");
}

vr::comfort::Settings gComfort;
std::string gComfortText;

SquadContext ReadSquadContext(Ini const& ini) {
    SquadContext s;
    double value = 0.0;
    if (Number(ini, "SquadContext", "enabled", value)) {
        s.enabled = value != 0.0;
    }
    if (Number(ini, "SquadContext", "refreshS", value)) {
        s.refreshS = std::clamp(value, 0.05, 5.0);
    }
    if (Number(ini, "SquadContext", "joinM", value)) {
        s.joinM = std::clamp(value, 0.5, 50.0);
    }
    if (Number(ini, "SquadContext", "splitAboveM", value)) {
        s.splitAboveM = std::clamp(value, 0.5, 50.0);
    }
    if (Number(ini, "SquadContext", "maxGroups", value)) {
        s.maxGroups = static_cast<int>(std::clamp(value, 1.0, 12.0));
    }
    if (Number(ini, "SquadContext", "nearM", value)) {
        s.nearM = std::clamp(value, 1.0, 200.0);
    }
    if (Number(ini, "SquadContext", "farM", value)) {
        s.farM = std::clamp(value, s.nearM, 500.0);
    }
    return s;
}

std::string Describe(SquadContext const& s) {
    return s.enabled ? std::format("on, read every {:.2f} s; groups join within {:.1f} m, split across {:.1f} m of height, at most {}; near under "
                                   "{:.0f} m, far past {:.0f} m", s.refreshS, s.joinM, s.splitAboveM, s.maxGroups, s.nearM, s.farM)
                     : std::string("off");
}

SquadContext gSquadContext;
std::string gSquadContextText;

SquadOrders ReadSquadOrders(Ini const& ini) {
    SquadOrders s;
    double value = 0.0;
    if (Number(ini, "SquadOrders", "enabled", value)) {
        s.enabled = value != 0.0;
    }
    if (Number(ini, "SquadOrders", "gesture", value)) {
        s.gesture = value != 0.0;
    }
    if (Number(ini, "SquadOrders", "followWithinM", value)) {
        s.followWithinM = std::clamp(value, 0.0, 20.0);
    }
    if (Number(ini, "SquadOrders", "enemyConeDeg", value)) {
        s.enemyConeDeg = std::clamp(value, 1.0, 45.0);
    }
    if (Number(ini, "SquadOrders", "memberConeDeg", value)) {
        s.memberConeDeg = std::clamp(value, 1.0, 45.0);
    }
    if (Number(ini, "SquadOrders", "minHoldS", value)) {
        s.minHoldS = std::clamp(value, 0.0, 2.0);
    }
    if (Number(ini, "SquadOrders", "maxHoldS", value)) {
        s.maxHoldS = std::clamp(value, 0.5, 60.0);
    }
    if (Number(ini, "SquadOrders", "minConfidence", value)) {
        s.minConfidence = std::clamp(value, 0.0, 1.0);
    }
    if (Number(ini, "SquadOrders", "standing", value)) {
        s.standing = value != 0.0;
    }
    if (Number(ini, "SquadOrders", "reassertS", value)) {
        s.reassertS = std::clamp(value, 0.5, 60.0);
    }
    if (Number(ini, "SquadOrders", "holdRadiusM", value)) {
        s.holdRadiusM = std::clamp(value, 1.0, 50.0);
    }
    if (Number(ini, "SquadOrders", "calmS", value)) {
        s.calmS = std::clamp(value, 1.0, 120.0);
    }
    if (Number(ini, "SquadOrders", "retreatHealth", value)) {
        s.retreatHealth = std::clamp(value, 0.0, 1.0);
    }
    if (Number(ini, "SquadOrders", "overwhelmCount", value)) {
        s.overwhelmCount = static_cast<int>(std::clamp(value, 1.0, 20.0));
    }
    return s;
}

std::string Describe(SquadOrders const& s) {
    return !s.enabled ? std::string("off")
         : std::format("{}; follow within {:.1f} m; cones {:.0f} deg (enemy) / {:.0f} deg (squadmate); hold {:.2f}..{:.0f} s",
               s.gesture ? "the left-grip gesture" : "the console only", s.followWithinM, s.enemyConeDeg, s.memberConeDeg, s.minHoldS, s.maxHoldS);
}

SquadOrders gSquadOrders;
std::string gSquadOrdersText;

vr::interact::Settings ReadInteract(Ini const& ini) {
    vr::interact::Settings i;
    double value = 0.0;
    if (Number(ini, "Interact", "gaze", value)) {
        i.gaze = value != 0.0;
    }
    if (Number(ini, "Interact", "label", value)) {
        i.label = value != 0.0;
    }
    if (Number(ini, "Interact", "combatLabel", value)) {
        i.combatLabel = value != 0.0;
    }
    if (Number(ini, "Interact", "labelHeightDeg", value)) {
        i.heightDeg = static_cast<float>(std::clamp(value, 1.0, 30.0));
    }
    if (auto const s = ini.find("Interact"); s != ini.end()) {
        if (auto const k = s->second.find("crop"); k != s->second.end() && !k->second.empty()) {
            float parts[4] = {};
            int n = 0;
            std::string const& text = k->second;
            for (std::size_t at = 0; at < text.size() && n < 4;) {
                std::size_t const end = text.find_first_of(" ,\t", at);
                std::string const token = text.substr(at, end == std::string::npos ? std::string::npos : end - at);
                if (!token.empty()) {
                    if (auto const v = ini::Number(token)) {
                        parts[n++] = static_cast<float>(*v);
                    } else {
                        n = -1;
                        break;
                    }
                }
                at = end == std::string::npos ? text.size() : end + 1;
            }
            if (n == 4 && parts[2] > 0.0f && parts[3] > 0.0f && parts[0] >= 0.0f && parts[1] >= 0.0f && parts[0] + parts[2] <= 1.0f &&
                parts[1] + parts[3] <= 1.0f) {
                std::copy(parts, parts + 4, i.crop);
            } else {
                log::Warn("interact: can't read crop = {} (x y width height, fractions of the HUD frame); the game's own place kept",
                    text);
            }
        }
    }
    return i;
}

std::string Describe(vr::interact::Settings const& i) {
    return std::format("A uses what {}; the prompt {}{}{}", i.gaze ? "you look at" : "the game's camera faces",
        i.label ? std::format("at the object, {:.1f} deg tall", i.heightDeg) : std::string("on the HUD only"),
        i.label && i.combatLabel ? ", enemies' too" : "",
        i.crop[2] > 0.0f ? std::format(", band ({:.3f}, {:.3f}) {:.3f} x {:.3f} by hand", i.crop[0], i.crop[1], i.crop[2], i.crop[3])
                         : std::string());
}

vr::interact::Settings gInteract;
std::string gInteractText;
Vent ReadVent(Ini const& ini) {
    Vent v;
    double value = 0.0;
    auto const flag = [&](char const* key, bool& out) {
        if (Number(ini, "Vent", key, value)) {
            out = value != 0.0;
        }
    };
    auto const number = [&](char const* key, double& out, double lo, double hi) {
        if (Number(ini, "Vent", key, value)) {
            out = std::clamp(value, lo, hi);
        }
    };
    flag("enabled", v.enabled);
    number("ventCooldownS", v.cooldownS, 0.0, 60.0);
    number("ventMinHeat", v.minHeat, 0.0, 1.0);
    number("ventMaxHeat", v.maxHeat, 0.01, 1000.0);
    number("ventSpeedMS", v.speedMin, 0.2, 6.0);
    number("ventTravelM", v.travel, 0.0, 0.5);
    number("ventHoldMaxS", v.holdMax, 0.2, 10.0);
    flag("effect", v.effect);
    number("hapticAmplitude", v.hapticAmplitude, 0.0, 1.0);
    number("zoneBackM", v.zone.backMeters, 0.0, 0.5);
    number("zoneFrontM", v.zone.frontMeters, 0.0, 0.5);
    number("zoneAboveM", v.zone.aboveMeters, 0.0, 0.5);
    number("zoneBelowM", v.zone.belowMeters, 0.0, 0.5);
    number("zoneSideM", v.zone.sideMeters, 0.0, 0.5);
    return v;
}

std::string Describe(Vent const& v) {
    return std::format("{}: cooldown {:.1f} s, from {:.2f} of the overheat ({:.2f}); a rack at {:.1f} m/s over {:.2f} m, held at most "
                       "{:.1f} s; zone {:.2f} behind .. {:.2f} ahead, {:.2f} above .. {:.2f} below, {:.2f} beside; effect {}; haptics {:.2f}",
        v.enabled ? "on" : "off", v.cooldownS, v.minHeat, v.maxHeat, v.speedMin, v.travel, v.holdMax, v.zone.backMeters,
        v.zone.frontMeters, v.zone.aboveMeters, v.zone.belowMeters, v.zone.sideMeters, v.effect ? "the game's" : "none",
        v.hapticAmplitude);
}

Vent gVent;
std::string gVentText;

// [Hud] (research/41): the shared reader (core/vr/HudPieces), its complaints logged here.
vr::hud_pieces::Settings ReadHud(Ini const& ini) {
    std::string problems;
    vr::hud_pieces::Settings const s = vr::hud_pieces::ReadSettings(ini, &problems);
    if (!problems.empty()) {
        log::Warn("hud pieces: [Hud] {}", problems);
    }
    return s;
}

vr::hud_pieces::Settings gHud;
std::string gHudText;

Mako ReadMako(Ini const& ini) {
    Mako m;
    double value = 0.0;
    if (Number(ini, "Mako", "handAim", value)) {
        m.handAim = value != 0.0;
    }
    if (Number(ini, "Mako", "turret", value)) {
        m.turret = value != 0.0;
    }
    if (Number(ini, "Mako", "turretSeam", value)) {
        m.turretSeam = static_cast<int>(std::clamp(value, 0.0, 7.0));
    }
    if (Number(ini, "Mako", "reticle", value)) {
        m.reticle = value != 0.0;
    }
    if (Number(ini, "Mako", "vignette", value)) {
        m.vignette = value != 0.0;
    }
    if (Number(ini, "Mako", "moveSpeed", value)) {
        m.moveSpeed = static_cast<float>(std::clamp(value, 1.0, 100000.0));
    }
    if (Number(ini, "Mako", "turnRate", value)) {
        m.turnRate = static_cast<float>(std::clamp(value, 0.1, 3600.0));
    }
    if (Number(ini, "Mako", "aimRangeM", value)) {
        m.aimRangeM = static_cast<float>(std::clamp(value, 5.0, 2000.0));
    }
    if (Number(ini, "Mako", "calibratedLine", value)) {
        m.calibratedLine = value != 0.0;
    }
    // The physical controls (research/46).
    vr::mako_controls::Settings& c = m.controlSettings;
    if (Number(ini, "Mako", "controls", value)) {
        c.controls = value != 0.0;
    }
    if (Number(ini, "Mako", "grabRadiusM", value)) {
        c.grabRadiusM = static_cast<float>(std::clamp(value, 0.02, 1.0));
    }
    if (Number(ini, "Mako", "stickTwist", value)) {
        c.stickTwist = value != 0.0;
    }
    if (Number(ini, "Mako", "stickTiltDeg", value)) {
        c.stickTiltDeg = static_cast<float>(std::clamp(value, 5.0, 60.0));
    }
    if (Number(ini, "Mako", "leverReturns", value)) {
        c.leverReturns = value != 0.0;
    }
    if (Number(ini, "Mako", "stickHeightM", value)) {
        c.stickHeightM = static_cast<float>(std::clamp(value, 0.03, 0.5));
    }
    if (Number(ini, "Mako", "thrusterBurnScale", value)) {
        m.thrusterBurnScale = static_cast<float>(std::clamp(value, 0.05, 1.0));
    }
    if (Number(ini, "Mako", "thrusterStabilize", value)) {
        m.thrusterStabilize = value != 0.0;
    }
    if (Number(ini, "Mako", "thrusterPitchDeg", value)) {
        m.thrusterPitchDeg = static_cast<float>(std::clamp(value, 0.0, 45.0));
    }
    if (Number(ini, "Mako", "thrusterRollDeg", value)) {
        m.thrusterRollDeg = static_cast<float>(std::clamp(value, 0.0, 25.0));  // the game resists rolls past 30 (StayUprightRollResistAngle)
    }
    if (Number(ini, "Mako", "gunner", value)) {
        m.gunner.on = value != 0.0;
    }
    if (Number(ini, "Mako", "gunnerRangeM", value)) {
        m.gunner.rangeM = static_cast<float>(value);
    }
    if (Number(ini, "Mako", "gunnerHandbackS", value)) {
        m.gunner.handbackS = static_cast<float>(value);
    }
    if (Number(ini, "Mako", "gunnerCannon", value)) {
        m.gunner.cannon = value != 0.0;
    }
    if (Number(ini, "Mako", "gunnerCrowd", value)) {
        m.gunner.crowdCount = static_cast<int>(std::lround(value));
    }
    if (Number(ini, "Mako", "gunnerCrowdRadiusM", value)) {
        m.gunner.crowdRadiusM = static_cast<float>(value);
    }
    m.gunner = vr::mako_gunner::Sanitized(m.gunner);
    if (Number(ini, "Mako", "spinRateDeg", value)) {
        m.spinRateDeg = static_cast<float>(std::clamp(value, 0.0, 180.0));
    }
    if (Number(ini, "Mako", "thrusterRateDeg", value)) {
        m.thrusterRateDeg = static_cast<float>(std::clamp(value, 10.0, 180.0));
    }
    if (Number(ini, "Mako", "stickPitchDeadZoneDeg", value)) {
        c.stickPitchDeadZoneDeg = static_cast<float>(std::clamp(value, 0.0, 30.0));
    }
    if (Number(ini, "Mako", "tiltSteerMax", value)) {
        c.tiltSteerMax = static_cast<float>(std::clamp(value, 0.0, 1.0));
    }
    if (Number(ini, "Mako", "steerInPlaceBelow", value)) {
        c.steerInPlaceBelow = static_cast<float>(std::clamp(value, 0.0, 1.0));
    }
    if (Number(ini, "Mako", "stickTwistDeg", value)) {
        c.stickTwistDeg = static_cast<float>(std::clamp(value, 5.0, 170.0));
    }
    if (Number(ini, "Mako", "stickDeadZoneDeg", value)) {
        c.stickDeadZoneDeg = static_cast<float>(std::clamp(value, 0.0, 30.0));
    }
    if (Number(ini, "Mako", "stickInvert", value)) {
        c.stickInvert = value != 0.0;
    }
    if (Number(ini, "Mako", "throttleTravelM", value)) {
        c.throttleTravelM = static_cast<float>(std::clamp(value, 0.03, 0.6));
    }
    if (Number(ini, "Mako", "throttleDetent", value)) {
        c.throttleDetent = static_cast<float>(std::clamp(value, 0.0, 0.5));
    }
    if (Number(ini, "Mako", "stickAimRate", value)) {
        c.stickAimRate = static_cast<float>(std::clamp(value, 1.0, 720.0));
    }
    if (Number(ini, "Mako", "rumble", value)) {
        c.rumble = static_cast<float>(std::clamp(value, 0.0, 1.0));
    }
    if (Number(ini, "Mako", "cameraFollow", value)) {
        c.cameraFollow = value != 0.0;
    }
    auto const offset = [&ini](char const* key, XrVector3f& out) {
        auto const text = ini::Get(ini, "Mako", key);
        if (!text) {
            return;
        }
        float numbers[3] = {};
        if (vr::hud_pieces::ParseNumbers(*text, 3, numbers) && std::abs(numbers[0]) <= 2.0f && std::abs(numbers[1]) <= 2.0f &&
            std::abs(numbers[2]) <= 2.0f) {
            out = {numbers[0], numbers[1], numbers[2]};
        } else {
            log::Warn("hands: [Mako] {} = {} isn't three numbers (meters: right, up, forward; each within 2): its default kept", key, *text);
        }
    };
    offset("leverOffset", c.leverOffset);
    offset("stickOffset", c.stickOffset);
    return m;
}

std::string Describe(Mako const& m) {
    return std::format("the controller aims the turret {} (model {}, seam {}), reticle {}, vignette {} (moving from {:.0f} units/s, "
                       "turning from {:.0f} deg/s), ray traced {:.0f} m, calibrated line {}",
        m.handAim ? "on" : "off", m.turret ? "turned" : "the game's",
        std::format("{}{}{}{}", (m.turretSeam & 1) != 0 ? "the mod's tick " : "", (m.turretSeam & 2) != 0 ? "the vehicle's tick " : "",
            (m.turretSeam & 4) != 0 ? "the pose " : "", m.turretSeam == 0 ? "none" : ""),
        m.reticle ? "on" : "off", m.vignette ? "on" : "off", m.moveSpeed, m.turnRate, m.aimRangeM, m.calibratedLine ? "used" : "ignored") +
        "; " + vr::mako_controls::Describe(m.controlSettings);
}

Mako gMako;
std::string gMakoText;

Cockpit ReadCockpit(Ini const& ini) {
    Cockpit c;
    double value = 0.0;
    if (Number(ini, "Cockpit", "enabled", value)) {
        c.enabled = value != 0.0;
    }
    if (auto const text = ini::Get(ini, "Cockpit", "script"); text && !Strip(*text).empty()) {
        c.script = Strip(*text);
    }
    if (Number(ini, "Cockpit", "followCamera", value)) {
        c.followCamera = value != 0.0;
    }
    if (Number(ini, "Cockpit", "seated", value)) {
        c.seated = value != 0.0;
    }
    if (Number(ini, "Cockpit", "hazard", value)) {
        c.hazard.on = value != 0.0;
    }
    if (Number(ini, "Cockpit", "hazardBelow", value)) {
        c.hazard.below = static_cast<float>(value);
        c.hazard.clearAbove = c.hazard.below + 0.10f;
    }
    if (Number(ini, "Cockpit", "hazardPeriodS", value)) {
        c.hazard.periodS = static_cast<float>(value);
    }
    if (Number(ini, "Cockpit", "hazardWash", value)) {
        c.hazard.wash = value != 0.0;
    }
    c.hazard = vr::cockpit_hazard::Sanitized(c.hazard);
    if (Number(ini, "Cockpit", "showTurret", value)) {
        c.turret = value != 0.0;
    }
    return c;
}

std::string Describe(Cockpit const& c) {
    return std::format("{}, script {}, the chase camera {}{}; {}; {}", c.enabled ? "on" : "off", c.script, c.followCamera ? "follows the Mako" : "the game's",
        c.seated ? ", Shepard seated" : "", vr::cockpit_hazard::Describe(c.hazard),
        c.turret ? std::string("the Mako's turret over the cabin, shots from its muzzle") : std::string("the turret hidden, the game's shot start"));
}

Cockpit gCockpit;
std::string gCockpitText;

// [Spectator] (research/49).
vr::spectator::Settings ReadSpectator(Ini const& ini) {
    vr::spectator::Settings s;
    double value = 0.0;
    auto const flag = [&](char const* key, bool& into) {
        if (Number(ini, "Spectator", key, value)) {
            into = value != 0.0;
        }
    };
    auto const number = [&](char const* key, float& into, double lo, double hi) {
        if (Number(ini, "Spectator", key, value)) {
            into = static_cast<float>(std::clamp(value, lo, hi));
        }
    };
    flag("enabled", s.enabled);
    if (Number(ini, "Spectator", "width", value)) {
        s.width = static_cast<int>(std::clamp(value, 320.0, 3840.0));
    }
    if (Number(ini, "Spectator", "height", value)) {
        s.height = static_cast<int>(std::clamp(value, 180.0, 2160.0));
    }
    number("fovDeg", s.fovDeg, 20.0, 150.0);
    number("smoothS", s.smoothS, 0.0, 5.0);
    number("rate", s.rate, 1.0, 1000.0);
    number("near", s.nearUnits, 1.0, 500.0);
    number("far", s.farUnits, 0.0, 10000000.0);
    flag("post", s.postProcess);
    flag("mirror", s.mirror);
    flag("layers", s.layers);
    flag("viewActor", s.viewActor);
    flag("hideHead", s.hideHead);
    if (auto const text = ini::Get(ini, "Spectator", "camera"); text && !Strip(*text).empty()) {
        std::string const lower = ini::Lower(Strip(*text));
        if (lower == "head") {
            s.camera = vr::spectator::kHead;
        } else if (lower == "free") {
            s.camera = vr::spectator::kFree;
        } else {
            log::Warn("spectator: can't read camera = {} (head or free); head kept", *text);
        }
    }
    if (auto const text = ini::Get(ini, "Spectator", "format"); text && !Strip(*text).empty()) {
        std::string const lower = ini::Lower(Strip(*text));
        if (lower == "float") {
            s.floatTarget = true;
        } else if (lower == "rgba8") {
            s.floatTarget = false;
        } else {
            log::Warn("spectator: can't read format = {} (float or rgba8); {} kept", *text, s.floatTarget ? "float" : "rgba8");
        }
    }
    return s;
}

std::string Describe(vr::spectator::Settings const& s) {
    if (!s.enabled) {
        return "off ([Spectator] enabled = 0)";
    }
    return std::format("on: {}x{} {} render target, {:.0f} degrees across, the {} camera{}, post process {}, near plane {:.0f} units, "
                       "the desktop window {}, our layers {}, view actor {}, head hidden {}",
        s.width, s.height, s.floatTarget ? "float (FloatRGB)" : "8-bit (A8R8G8B8)", s.fovDeg, s.camera == vr::spectator::kHead ? "head" : "free",
        s.camera == vr::spectator::kHead ? std::format(" (smoothed over {:.2f} s)", s.smoothS) : std::string(), s.postProcess ? "on" : "off",
        s.nearUnits, s.mirror ? "shows it" : "keeps Halcyon's mirror", s.layers ? "composited in" : "left out", s.viewActor ? "the pawn" : "untouched",
        s.hideHead ? "for the head camera" : "never");
}

vr::spectator::Settings gSpectator;
std::string gSpectatorText;

// [Galaxy] (research/51).
vr::galaxy_map::Settings ReadGalaxy(Ini const& ini) {
    vr::galaxy_map::Settings s;
    double value = 0.0;
    if (Number(ini, "Galaxy", "hologram", value)) {
        s.hologram = value != 0.0;
    }
    if (Number(ini, "Galaxy", "distanceM", value)) {
        s.distanceM = static_cast<float>(std::clamp(value, 0.3, 3.0));
    }
    if (Number(ini, "Galaxy", "belowM", value)) {
        s.belowM = static_cast<float>(std::clamp(value, -1.0, 1.5));
    }
    if (Number(ini, "Galaxy", "tableDistanceM", value)) {
        s.tableDistanceM = static_cast<float>(std::clamp(value, 0.3, 2.0));
    }
    if (Number(ini, "Galaxy", "tableBelowM", value)) {
        s.tableBelowM = static_cast<float>(std::clamp(value, -0.5, 1.5));
    }
    if (Number(ini, "Galaxy", "tableRadiusM", value)) {
        s.tableRadiusM = static_cast<float>(std::clamp(value, 0.15, 1.0));
    }
    if (Number(ini, "Galaxy", "inWorld", value)) {
        s.inWorld = value != 0.0;
    }
    if (Number(ini, "Galaxy", "worldRadiusM", value)) {
        s.worldRadiusM = static_cast<float>(std::clamp(value, 0.3, 2.5));
    }
    if (Number(ini, "Galaxy", "worldTiltDeg", value)) {
        s.worldTiltDeg = static_cast<float>(std::clamp(value, 0.0, 80.0));
    }
    if (Number(ini, "Galaxy", "worldLiftM", value)) {
        s.worldLiftM = static_cast<float>(std::clamp(value, -1.0, 1.5));
    }
    if (Number(ini, "Galaxy", "window", value)) {
        s.window = value != 0.0;
    }
    if (Number(ini, "Galaxy", "windowWidthM", value)) {
        s.windowWidthM = static_cast<float>(std::clamp(value, 0.5, 4.0));
    }
    if (Number(ini, "Galaxy", "windowDepth", value)) {
        s.windowDepth = static_cast<float>(std::clamp(value, 0.0, 0.05));
    }
    if (Number(ini, "Galaxy", "windowRound", value)) {
        s.windowRound = value != 0.0;
    }
    if (Number(ini, "Galaxy", "windowOnHologram", value)) {
        s.windowOnHologram = value != 0.0;
    }
    if (Number(ini, "Galaxy", "windowBackdrop", value)) {
        s.windowBackdrop = static_cast<float>(std::clamp(value, 0.0, 1.0));
    }
    return s;
}

std::string Describe(vr::galaxy_map::Settings const& s) {
    if (!s.hologram) {
        return "the galaxy map on Halcyon's panel ([Galaxy] hologram = 0)";
    }
    if (s.inWorld) {
        return std::format("the galaxy map as our hologram over the CIC's projector, {:.2f} m in radius, tilted {:.0f} deg, lifted {:.2f} m",
                           s.worldRadiusM, s.worldTiltDeg, s.worldLiftM);
    }
    return std::format("the galaxy map as our hologram on a table {:.2f} m ahead, {:.2f} m below the eyes, {:.2f} m in radius",
                       s.tableDistanceM, s.tableBelowM, s.tableRadiusM);
}

vr::galaxy_map::Settings gGalaxy;
std::string gGalaxyText;

// [WorldUi] (research/53).
vr::world_ui::Settings ReadWorldUi(Ini const& ini) {
    vr::world_ui::Settings s;
    double value = 0.0;
    if (Number(ini, "WorldUi", "on", value)) {
        s.on = value != 0.0;
    }
    if (Number(ini, "WorldUi", "widthM", value)) {
        s.widthM = static_cast<float>(std::clamp(value, 0.3, 4.0));
    }
    if (Number(ini, "WorldUi", "distanceM", value)) {
        s.distanceM = static_cast<float>(std::clamp(value, 0.4, 4.0));
    }
    if (Number(ini, "WorldUi", "belowM", value)) {
        s.belowM = static_cast<float>(std::clamp(value, -1.0, 1.0));
    }
    if (Number(ini, "WorldUi", "backdrop", value)) {
        s.backdrop = static_cast<float>(std::clamp(value, 0.0, 1.0));
    }
    // A list of the game's screen tags ("all"; "none" or nothing: no screen).
    auto const screenList = [](std::string const& text, char const* key) {
        std::uint32_t bits = 0;
        std::string word;
        std::istringstream words(text);
        while (std::getline(words, word, ',')) {
            std::string const w = Strip(word);
            std::string lower = w;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower == "all") {
                bits = 0xFFFFFFFFu;
            } else if (vr::world_ui::Screen const screen = vr::world_ui::FromTag(w); screen != vr::world_ui::Screen::None) {
                bits |= vr::world_ui::Bit(screen);
            } else if (!w.empty() && lower != "none") {
                log::Warn("world ui: [WorldUi] {}: '{}' isn't one of the game's screens we place (research/53)", key, w);
            }
        }
        return bits;
    };
    if (auto const section = ini.find("WorldUi"); section != ini.end()) {
        if (auto const key = section->second.find("screens"); key != section->second.end()) {
            s.screens = screenList(key->second, "screens");
        }
        if (auto const key = section->second.find("wrist"); key != section->second.end()) {
            s.wristScreens = screenList(key->second, "wrist");
        }
    }
    if (Number(ini, "WorldUi", "wristWidthM", value)) {
        s.wristWidthM = static_cast<float>(std::clamp(value, 0.1, 1.0));
    }
    if (Number(ini, "WorldUi", "wristAboveM", value)) {
        s.wristAboveM = static_cast<float>(std::clamp(value, -0.2, 0.5));
    }
    if (Number(ini, "WorldUi", "wristTowardM", value)) {
        s.wristTowardM = static_cast<float>(std::clamp(value, -0.3, 0.5));
    }
    if (Number(ini, "WorldUi", "wristFollow", value)) {
        s.wristFollow = value != 0.0;
    }
    if (Number(ini, "WorldUi", "wristBeam", value)) {
        s.wristBeam = value != 0.0;
    }
    if (Number(ini, "WorldUi", "omniTool", value)) {
        s.omniTool = value != 0.0;
    }
    return s;
}

std::string Describe(vr::world_ui::Settings const& s) {
    if (!s.on) {
        return "the game's screens on Halcyon's flat panel ([WorldUi] on = 0)";
    }
    std::string names;
    for (unsigned i = 1; i < static_cast<unsigned>(vr::world_ui::Screen::Count); ++i) {
        if ((s.screens & (1u << i)) != 0) {
            names += (names.empty() ? "" : ", ") + std::string(vr::world_ui::Name(static_cast<vr::world_ui::Screen>(i)));
        }
    }
    std::string wrist;
    for (unsigned i = 1; i < static_cast<unsigned>(vr::world_ui::Screen::Count); ++i) {
        if ((s.wristScreens & (1u << i)) != 0) {
            wrist += (wrist.empty() ? "" : ", ") + std::string(vr::world_ui::Name(static_cast<vr::world_ui::Screen>(i)));
        }
    }
    return std::format("the game's screens in the room ({}): {:.2f} m wide, {:.2f} m ahead, {:.2f} m below the eyes, backdrop {:.2f}; "
                       "projected from the left wrist: {}{}",
                       names.empty() ? "none" : names, s.widthM, s.distanceM, s.belowM, s.backdrop, wrist.empty() ? "none" : wrist,
                       wrist.empty() ? std::string()
                                     : std::format(" ({:.2f} m wide, {:.2f} m over the wrist, {:.2f} m toward the eyes, {}, {}, {})", s.wristWidthM,
                                           s.wristAboveM, s.wristTowardM, s.wristFollow ? "riding on it" : "left where it was projected",
                                           s.wristBeam ? "a fan of light from the wrist" : "no fan of light",
                                           s.omniTool ? "the game's omni-tool on the forearm" : "no omni-tool"));
}

vr::world_ui::Settings gWorldUi;
std::string gWorldUiText;

Game ReadGame(Ini const& ini) {
    Game g;
    double value = 0.0;
    if (Number(ini, "Game", "pauseUnfocused", value)) {
        g.pauseUnfocused = value != 0.0;
    }
    return g;
}

Game gGame;
std::string gGameText;

Posture ReadPosture(Ini const& ini) {
    Posture p;
    double value = 0.0;
    if (Number(ini, "Hands", "noCover", value)) {
        p.noCover = value != 0.0;
    }
    if (Number(ini, "Hands", "physicalCrouch", value)) {
        p.physicalCrouch = value != 0.0;
    }
    Number(ini, "Hands", "crouchBelow", p.crouch.crouchBelow);
    Number(ini, "Hands", "standAbove", p.crouch.standAbove);
    Number(ini, "Hands", "crouchDwellS", p.crouch.dwellSeconds);
    p.crouch = ik::posture::Sanitize(p.crouch);
    return p;
}

std::string Describe(Posture const& p) {
    return std::format("cover snapping {} in VR on foot; physical crouch {}", p.noCover ? "off (noCover = 1)" : "kept (noCover = 0)",
        p.physicalCrouch ? std::format("on: crouch below {:.2f} of the standing eye height, stand above {:.2f}, after {:.2f} s",
                               p.crouch.crouchBelow, p.crouch.standAbove, p.crouch.dwellSeconds)
                         : std::string("off"));
}

Posture gPosture;
std::string gPostureText;

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
            log::Warn("sights: can't read {} = {} (auto, or back, height[, side] in gun units); auto kept", placeKey, *t);
            out.guns[w].autoPlace = true;
        }
        // A scope's eyepiece radius and the model's parts hidden while its picture is up (research/21 section 14).
        std::string const lensKey = key + "Lens";
        if (Number(ini, "Sights", lensKey.c_str(), value)) {
            out.guns[w].lens = static_cast<float>(std::clamp(value, 0.5, 10.0));
        }  // auto (or anything else): 0, the game's model's own (research/21 section 16)
        std::string const hideKey = key + "Hide";
        if (auto const s = section->second.find(hideKey); s != section->second.end()) {
            out.guns[w].hide = s->second;  // empty: nothing hidden (the key there without a value)
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
    number("eyeBox", out.eyeBoxUnits, 0.5f, 30.0f);
    if (Number(ini, "Sights", "converge", value)) {
        out.converge = value != 0.0;
    }
    number("zeroM", out.zeroM, 0.0f, 500.0f);
    number("lensUnits", out.lensUnits, 0.5f, 10.0f);
    number("focusM", out.focusM, 2.0f, 1000.0f);
    number("eyeReliefM", out.eyeReliefM, 0.03f, 0.5f);
    number("scopeFieldDeg", out.scopeFieldDeg, 0.0f, 60.0f);
    number("scopeEyeBoxM", out.scopeEyeBoxM, 0.01f, 0.20f);
    number("scopeMaxM", out.scopeMaxM, 0.20f, 1.50f);
    number("scopeReliefM", out.scopeReliefM, 0.0f, 0.50f);
    number("scopePullMaxM", out.scopePullMaxM, 0.0f, 0.30f);
    number("scopeAdsPullMaxM", out.scopeAdsPullMaxM, 0.0f, 0.50f);
    if (Number(ini, "Sights", "ads", value)) {
        out.ads = value != 0.0;
    }
    number("adsReachM", out.adsReachM, 0.0f, 1.20f);  // 0: Overlord's corridor alone
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
    number("assistStrength", out.assist.strength, 0.0f, 1.0f);
    number("assistMaxM", out.assist.maxShiftM, 0.0f, 0.25f);
    number("assistDotMaxM", out.assist.dotMaxShiftM, 0.0f, 0.10f);
    // assistConeDeg, assistRangeM: unused since 2026-10-27 (the ADS approach weight, research/21 section 15); left in files.
    if (Number(ini, "Sights", "resolution", value)) {
        out.resolution = static_cast<int>(std::clamp(value, 128.0, 2048.0));
    }
    if (auto const t = text("reticle")) {
        if (auto const r = sv::ParseScopeReticle(*t)) {
            out.scopeReticle = *r;
        } else {
            log::Warn("sights: can't read reticle = {} (game, duplex, mildot or dot); the game's kept", *t);
        }
    }
    if (Number(ini, "Sights", "zoomStick", value)) {
        out.zoomStick = value != 0.0;
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
        guns += std::format("{}{} {}{}{}{}", guns.empty() ? "" : ", ", sv::WeaponName(w), sv::Name(g.kind),
            g.kind == sv::Kind::Scope ? std::format(" {:.1f}x", g.zoom) : std::string(),
            g.kind == sv::Kind::None ? std::string()
            : g.autoPlace         ? std::string(" (placed auto)")
                                  : std::format(" ({:.1f} back, {:.1f} up, {:.1f} right)", g.back, g.height, g.side),
            g.kind == sv::Kind::Scope
                ? std::format(" (eyepiece radius {:.1f} units{})", sv::LensUnits(s, w),
                      sv::ParseBoneList(g.hide).empty() ? std::string() : std::format(", hides '{}' while its picture is up", g.hide))
                : std::string());
    }
    return std::format("on: {}; dot #{:06X} {:.2f} deg, ring {:.2f} deg, window {:.1f} units{}, eye box {:.1f} units, zero {}, shots {}; "
                       "scope lens {:.1f} units, "
                       "{} reticle #{:06X}, {} eye's picture, focus {:.0f} m, eye relief {:.2f} m, {} px, {}; ring reticle {}, scoped "
                       "aimed fire {}; {}; alignment assist {}",
        guns, s.dotColor, s.dotSizeDeg, s.ringSizeDeg, s.windowUnits, s.frame ? " (framed)" : "", std::max(s.eyeBoxUnits, s.windowUnits),
        s.zeroM > 0.0f ? std::format("{:.0f} m", s.zeroM) : std::string("parallel"),
        s.converge ? "converge on what the sight line hits" : "parallel to the sight line", s.lensUnits, sv::Name(s.scopeReticle),
        s.scopeColor, s.otherEye ? "the other" : "the same", s.focusM, s.eyeReliefM, s.resolution,
        s.scopeFieldDeg > 0.0f ? std::format("virtual eyepiece {:.0f} deg across {}, eye box {:.3f} m{}", s.scopeFieldDeg,
                                     s.ads ? std::string("under ADS") : std::format("to {:.2f} m", s.scopeMaxM), s.scopeEyeBoxM,
                                     s.scopePullMaxM > 0.0f && s.scopeReliefM > 0.0f
                                         ? std::format(", pulled to {:.2f} m from the eye (at most {:.2f} m, {:.2f} m under ADS): the picture "
                                                       "is the eyepiece seen from there", s.scopeReliefM, s.scopePullMaxM,
                                               s.ads ? std::max(s.scopePullMaxM, s.scopeAdsPullMaxM) : s.scopePullMaxM)
                                         : std::string(", no pull"))
                               : std::string("the physical lens"),
        s.replaceRing ? "hidden while sighting" : "kept", s.aimedFire ? "on" : "off",
        s.ads ? std::format("a scope's picture only while aiming down it (ADS: Overlord's corridor, the eyes within {:.2f} m of the "
                            "eyepiece, {:.2f} m to leave)", s.adsReachM, s.adsReachM > 0.0f ? s.adsReachM + 0.10f : 0.0f)
              : std::string("a scope's picture whenever an eye is behind it (ads = 0)"),
        s.assist.enabled ? std::format("on (as raised: Overlord's ADS approach weight; {:.0f}% of the way, at most {:.2f} m for a scope, "
                                       "{:.2f} m for a dot or holo)",
                               s.assist.strength * 100.0f, s.assist.maxShiftM, s.assist.dotMaxShiftM)
                         : std::string("off"));
}

vr::sights::Settings gSights;
std::string gSightsText;

// [Sights] scopeCapture* (research/39), beside the sights' own keys.
vr::scope_capture::Settings ReadScopeCapture(Ini const& ini) {
    vr::scope_capture::Settings out;
    double value = 0.0;
    if (Number(ini, "Sights", "scopeCapture", value)) {
        out.enabled = value != 0.0;
    }
    if (Number(ini, "Sights", "scopeCaptureSize", value)) {
        out.size = static_cast<int>(std::clamp(value, 256.0, 2048.0));
    }
    auto const number = [&](char const* key, float& into, float lo, float hi) {
        if (Number(ini, "Sights", key, value)) {
            into = static_cast<float>(std::clamp(value, static_cast<double>(lo), static_cast<double>(hi)));
        }
    };
    number("scopeCaptureRate", out.rate, 1.0f, 1000.0f);
    number("scopeCaptureMargin", out.margin, 0.0f, 1.0f);
    number("scopeCaptureNear", out.nearUnits, 0.0f, 500.0f);
    number("scopeCaptureFar", out.farUnits, 0.0f, 10000000.0f);
    number("scopeCaptureHold", out.holdSeconds, 0.0f, 5.0f);
    if (Number(ini, "Sights", "scopeCapturePost", value)) {
        out.postProcess = value != 0.0;
    }
    if (auto const s = ini.find("Sights"); s != ini.end()) {
        if (auto const k = s->second.find("scopeCaptureFormat"); k != s->second.end() && !k->second.empty()) {
            std::string lower = k->second;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower == "float") {
                out.floatTarget = true;
            } else if (lower == "rgba8") {
                out.floatTarget = false;
            } else {
                log::Warn("scope capture: can't read scopeCaptureFormat = {} (float or rgba8); float kept", k->second);
            }
        }
    }
    return out;
}

std::string Describe(vr::scope_capture::Settings const& s) {
    if (!s.enabled) {
        return "off ([Sights] scopeCapture = 0): the scope magnifies the other eye's picture";
    }
    return std::format("on: {} px {} render target, {:.0f} renders a second at most, post process {}, field margin {:.0f}%, near plane "
                       "{:.0f} units past the muzzle, far plane {}, kept on {:.1f} s after the eye leaves",
        s.size, s.floatTarget ? "float (FloatRGB)" : "8-bit (A8R8G8B8)", s.rate,
        s.postProcess ? "on (known issue: a black speckle in dark areas, research/39 section 12)" : "off", s.margin * 100.0f,
        s.nearUnits, s.farUnits > 0.0f ? std::format("{:.0f} units", s.farUnits) : std::string("the engine's (0)"), s.holdSeconds);
}

vr::scope_capture::Settings gScopeCapture;
std::string gScopeCaptureText;

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
        changed = changed || std::abs(values[i] - gCameraWritten[i]) > 1.0;
    }
    if (!changed) {
        return;
    }
    // At most once a minute: the sample drifts with the head, and every write of the file makes each module that
    // watches it re-read (research/35: the settings applier re-ran its engine commands every 5 s on these).
    static ULONGLONG lastWriteMs = 0;
    ULONGLONG const nowMs = GetTickCount64();
    if (lastWriteMs != 0 && nowMs - lastWriteMs < 60000) {
        return;
    }
    lastWriteMs = nowMs;
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

BodyMove GetBodyMove() {
    return {gValues.bodyMove, gValues.leanM, gValues.bodyMoveMaxM};
}

bool LevelCamera() {
    return gValues.levelCamera;
}

double RecoilHaptic() {
    return gValues.recoilHaptic;
}

bool AimAssist() {
    return gValues.aimAssist;
}

CombatStance GetCombatStance() {
    return {gValues.exploreStance, gValues.exploreStanceStorm, gValues.exploreRamp, gValues.drawRate};
}

Fingers GetFingers() {
    return {gValues.fingers, gValues.fingerCurlDeg, gValues.fingerTouch};
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

Posture GetPosture() {
    return gPosture;
}

vr::comfort::Settings GetComfort() {
    return gComfort;
}

vr::interact::Settings GetInteract() {
    return gInteract;
}

SquadContext GetSquadContext() {
    return gSquadContext;
}

SquadOrders GetSquadOrders() {
    return gSquadOrders;
}

Vent GetVent() {
    return gVent;
}

vr::hud_pieces::Settings GetHud() {
    return gHud;
}

Cockpit GetCockpit() {
    return gCockpit;
}

vr::spectator::Settings GetSpectator() {
    return gSpectator;
}

vr::galaxy_map::Settings GetGalaxy() {
    return gGalaxy;
}

vr::world_ui::Settings GetWorldUi() {
    return gWorldUi;
}

Game GetGame() {
    return gGame;
}

Mako GetMako() {
    return gMako;
}

vr::sights::Settings GetSights() {
    return gSights;
}

vr::scope_capture::Settings GetScopeCapture() {
    return gScopeCapture;
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

// Appends a block of defaults with plain narrow text, which only fits an ANSI or UTF-8 file: a UTF-16 one (saved so
// by an editor; Win32 reads it) would get bytes it reads as garbage. Then the block is left out, once warned.
bool AppendBlock(fs::path const& path, char const* block, char const* name) {
    if (ini::IsUtf16(gEncoding)) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            log::Warn("hands: {} is saved as UTF-16: new settings blocks ([{}] ...) aren't appended to it (save it as "
                      "ANSI or UTF-8 to get them); their defaults apply", ini::Utf8(path), name);
        }
        return false;
    }
    std::ofstream out(path, std::ios::app);
    out << block;
    out.close();
    if (!out) {
        log::Warn("hands: couldn't add the [{}] defaults to {} (read-only?); they apply without it", name, ini::Utf8(path));
        return false;
    }
    log::Info("hands: added the [{}] defaults to {}", name, ini::Utf8(path));
    return true;
}

void Refresh() {
    ULONGLONG const now = GetTickCount64();
    // Once a second; every 10 s while the file can't be created or read (an unwritable ASI folder): six callers a
    // tick used to retry the create each time.
    if (gLastCheckMs != 0 && now - gLastCheckMs < (gHaveStamp ? 1000u : 10000u)) {
        return;
    }
    gLastCheckMs = now;
    fs::path const path = FilePath();
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        {
            std::ofstream out(path);
            out << kDefaultFile << kControlsBlock << kHolstersBlock << kSquadBlock << kSightsBlock << kThrowablesBlock
                << kComfortBlock << kInteractBlock << kVentBlock << kHudBlock << kMakoBlock << kCockpitBlock << kGalaxyBlock << kWorldUiBlock << kGameBlock;
            out.close();
            static bool warned = false;
            if (!out || !fs::exists(path, ec)) {
                if (!warned) {
                    warned = true;
                    log::Warn("hands: can't create {} (is the game's ASI folder read-only?): the defaults apply, and nothing "
                              "the menus change is saved; tried again every 10 s", ini::Utf8(path));
                }
                if (!gApplied) {
                    gApplied = true;
                    Apply(Values{});
                }
                return;
            }
            warned = false;
        }
        log::Info("hands: wrote default calibration to {}", ini::Utf8(path));
    }
    auto stamp = fs::last_write_time(path, ec);
    if (ec || (gHaveStamp && stamp == gStamp)) {
        return;
    }
    Ini ini = ReadIni(path);
    for (auto const& [name, block] : {std::pair{"Controls", kControlsBlock}, std::pair{"Holsters", kHolstersBlock},
             std::pair{"Squad", kSquadBlock}, std::pair{"Throwables", kThrowablesBlock}, std::pair{"Comfort", kComfortBlock},
             std::pair{"Sights", kSightsBlock}, std::pair{"Interact", kInteractBlock}, std::pair{"Vent", kVentBlock}, std::pair{"Hud", kHudBlock}, std::pair{"Mako", kMakoBlock},
             std::pair{"Cockpit", kCockpitBlock}, std::pair{"Galaxy", kGalaxyBlock}, std::pair{"WorldUi", kWorldUiBlock}, std::pair{"Game", kGameBlock}, std::pair{"SquadContext", kSquadContextBlock}, std::pair{"SquadOrders", kSquadOrdersBlock}}) {
        if (ini.find(name) == ini.end() && AppendBlock(path, block, name)) {  // an older file: show the new settings
            ini = ReadIni(path);
            stamp = fs::last_write_time(path, ec);
        }
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
        {L"onlyHolstersDraw", "onlyHolstersDraw", L"1"},  // 2026-10-25
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
        log::Info("hands: added {} (defaults) to [Holsters] in {}", added, ini::Utf8(path));
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
        log::Info("hands: added {} (defaults) to [Squad] in {}", addedSquad, ini::Utf8(path));
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
            return ini::Number(k->second).value_or(fallback);
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
            ini::Utf8(path));
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    // [Sights] since 2026-10-23 (research/21 sections 9-10): a file without eyeBox predates the red-dot defaults for the
    // shotgun and the assault rifle; their old default (holo) becomes the new one (dot), anything else is kept.
    if (auto const s = ini.find("Sights"); s != ini.end() && s->second.find("eyebox") == s->second.end() &&
        s->second.find("eyeBox") == s->second.end()) {
        std::string moved;
        for (auto const& [wkey, key] : {std::pair{L"shotgun", "shotgun"}, std::pair{L"assaultRifle", "assaultRifle"}}) {
            auto const k = s->second.find(key);
            std::string value = k != s->second.end() ? k->second : std::string();
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (value == "holo" && WritePrivateProfileStringW(L"Sights", wkey, L"dot", path.wstring().c_str()) != 0) {
                moved += std::format("{}{}", moved.empty() ? "" : " and ", key);
            }
        }
        if (!moved.empty()) {
            log::Info("hands: [Sights] {} holo (the old default) -> dot (the new one) in {}", moved, ini::Utf8(path));
            ini = ReadIni(path);
            stamp = fs::last_write_time(path, ec);
        }
    }
    // [Sights] since 2026-10-27 (research/21 section 15): the assault rifle is a red dot again. Section 14's one-day
    // migration had turned its old default (dot) into `scope 1.5`; a file without the marker `ads` gets exactly that value
    // back to dot once (vr::sights::MigrateAssaultRifle), any other choice is the player's and kept. (A file from before
    // section 14 keeps its dot: that migration is gone.)
    if (auto const s = ini.find("Sights"); s != ini.end() && s->second.find("ads") == s->second.end()) {
        auto const k = s->second.find("assaultRifle");
        if (k != s->second.end()) {
            if (auto const back = vr::sights::MigrateAssaultRifle(k->second)) {
                std::wstring const value(back->begin(), back->end());
                if (WritePrivateProfileStringW(L"Sights", L"assaultRifle", value.c_str(), path.wstring().c_str()) != 0) {
                    log::Info("hands: [Sights] assaultRifle {} (section 14's default) -> {} (a red dot again; research/21 section 15) in {}",
                        k->second, *back, ini::Utf8(path));
                    ini = ReadIni(path);
                    stamp = fs::last_write_time(path, ec);
                }
            } else if (vr::sights::ParseGun(k->second, vr::sights::Gun{}).value_or(vr::sights::Gun{}).kind != vr::sights::Kind::Dot) {
                log::Info("hands: [Sights] assaultRifle = {} kept (your choice; the default is dot)", k->second);
            }
        }
    }
    // [Sights] since 2026-11-03 (research/21 section 16): the game's own optics. A file without the marker zoomStick gets each
    // old default, exactly as written, turned into the new one (the sniper rifle's magnification, the eyepieces and the hidden
    // part the game's model's own, its reticle the game's); any other value is the player's and kept.
    if (auto const s = ini.find("Sights"); s != ini.end() && s->second.find("zoomStick") == s->second.end()) {
        static constexpr struct {
            wchar_t const* key;
            char const* name;
            char const* old;
            wchar_t const* now;
        } kMoved[] = {
            {L"sniperRifle", "sniperRifle", "scope 4", L"scope auto"},
            {L"reticle", "reticle", "duplex", L"game"},
            {L"assaultRifleLens", "assaultRifleLens", "1.6", L"auto"},
            {L"sniperRifleLens", "sniperRifleLens", "2", L"auto"},
            {L"sniperRifleHide", "sniperRifleHide", "Scope_Lens", L"auto"},
        };
        std::string moved;
        std::string kept;
        for (auto const& m : kMoved) {
            auto const k = s->second.find(m.name);
            if (k == s->second.end()) {
                continue;
            }
            if (_stricmp(k->second.c_str(), m.old) == 0 && WritePrivateProfileStringW(L"Sights", m.key, m.now, path.wstring().c_str()) != 0) {
                moved += std::format("{}{} {} -> {}", moved.empty() ? "" : ", ", m.name, m.old, ini::Utf8(fs::path(m.now)));
            } else {
                kept += std::format("{}{} = {}", kept.empty() ? "" : ", ", m.name, k->second);
            }
        }
        if (!moved.empty() || !kept.empty()) {
            log::Info("hands: [Sights] the game's own optics (research/21 section 16): {}{}{} in {}", moved.empty() ? "nothing moved" : moved,
                kept.empty() ? "" : "; kept as you set them: ", kept, ini::Utf8(path));
        }
        if (!moved.empty()) {
            ini = ReadIni(path);
            stamp = fs::last_write_time(path, ec);
        }
    }
    // [Sights] since 2026-11-04 (research/39 section 12): the scope's zoomed render is on by default. A file without the marker
    // scopeCapturePost that still holds the old default scopeCapture = 0 gets 1; any other value is the player's and kept.
    if (auto const s = ini.find("Sights"); s != ini.end() && s->second.find("scopeCapturePost") == s->second.end()) {
        if (auto const k = s->second.find("scopeCapture"); k != s->second.end()) {
            if (k->second == "0" && WritePrivateProfileStringW(L"Sights", L"scopeCapture", L"1", path.wstring().c_str()) != 0) {
                log::Info("hands: [Sights] scopeCapture 0 -> 1: the scope's zoomed render is on by default (research/39 section 12) in {}",
                    ini::Utf8(path));
                ini = ReadIni(path);
                stamp = fs::last_write_time(path, ec);
            } else if (k->second != "0") {
                log::Info("hands: [Sights] scopeCapture = {} kept (your choice; the default is 1)", k->second);
            }
        }
    }
    // [Mako] since 2026-11-02 (research/48 section 22): the controls' grips are the cockpit models' on the seat's armrests and
    // the throttle travels the model's slot. A file holding all three old defaults exactly gets the new ones; any other
    // value is the player's and kept.
    if (auto const s = ini.find("Mako"); s != ini.end()) {
        static constexpr struct {
            wchar_t const* key;
            char const* name;
            char const* old;
            wchar_t const* now;
        } kMoved[] = {
            {L"throttleTravelM", "throttleTravelM", "0.15", L"0.055"},
            {L"leverOffset", "leverOffset", "-0.20 -0.55 0.25", L"-0.362 -0.446 0.286"},
            {L"stickOffset", "stickOffset", "0.20 -0.55 0.25", L"0.375 -0.415 0.314"},
        };
        bool const allOld = std::ranges::all_of(kMoved, [&](auto const& m) {
            auto const k = s->second.find(m.name);
            return k != s->second.end() && k->second == m.old;
        });
        if (allOld) {
            bool written = true;
            for (auto const& m : kMoved) {
                written = WritePrivateProfileStringW(L"Mako", m.key, m.now, path.wstring().c_str()) != 0 && written;
            }
            if (written) {
                log::Info("hands: [Mako] throttleTravelM, leverOffset, stickOffset: the old defaults -> the cockpit's grips (research/48 section 22) in {}",
                    ini::Utf8(path));
            }
            ini = ReadIni(path);
            stamp = fs::last_write_time(path, ec);
        }
    }
    // [Hands] reachNearFaceM, the [Sights] alignment assist, [Hands]' physical cover and crouch
    // (research/23) and levelCamera / recoilHaptic (research/24) (2026-10-22), appended with their defaults.
    static constexpr struct {
        wchar_t const* section;
        char const* sectionName;
        wchar_t const* key;
        char const* name;
        wchar_t const* value;
    } kNewKeys[] = {
        {L"Hands", "Hands", L"reachNearFaceM", "reachNearFaceM", L"0.30"},
        {L"Hands", "Hands", L"levelCamera", "levelCamera", L"1"},
        {L"Hands", "Hands", L"recoilHaptic", "recoilHaptic", L"0.5"},
        {L"Sights", "Sights", L"assist", "assist", L"1"},
        {L"Sights", "Sights", L"assistStrength", "assistStrength", L"1"},
        {L"Sights", "Sights", L"assistMaxM", "assistMaxM", L"0.10"},
        {L"Sights", "Sights", L"eyeBox", "eyeBox", L"6"},
        {L"Sights", "Sights", L"converge", "converge", L"1"},
        // The scope's virtual eyepiece (2026-10-25, research/21 section 13).
        {L"Sights", "Sights", L"scopeFieldDeg", "scopeFieldDeg", L"22"},
        {L"Sights", "Sights", L"scopeEyeBoxM", "scopeEyeBoxM", L"0.05"},
        {L"Sights", "Sights", L"scopeMaxM", "scopeMaxM", L"0.70"},
        // The scope's pull to the eye, per-gun eyepieces and the hidden lens (2026-10-27, research/21 section 14). The
        // marker scopeReliefM comes after the assault rifle's migration above.
        {L"Sights", "Sights", L"scopeReliefM", "scopeReliefM", L"0.12"},
        {L"Sights", "Sights", L"scopePullMaxM", "scopePullMaxM", L"0.15"},
        // Aiming down the sight (2026-10-27, research/21 section 15). The marker ads comes after the assault rifle's
        // migration above.
        {L"Sights", "Sights", L"ads", "ads", L"1"},
        {L"Sights", "Sights", L"adsReachM", "adsReachM", L"0.40"},
        {L"Sights", "Sights", L"scopeAdsPullMaxM", "scopeAdsPullMaxM", L"0.35"},
        {L"Sights", "Sights", L"assistDotMaxM", "assistDotMaxM", L"0.03"},
        {L"Sights", "Sights", L"assaultRifleLens", "assaultRifleLens", L"auto"},
        {L"Sights", "Sights", L"sniperRifleLens", "sniperRifleLens", L"auto"},
        {L"Sights", "Sights", L"sniperRifleHide", "sniperRifleHide", L"auto"},
        // The game's own optics (2026-11-03, research/21 section 16). The marker zoomStick comes after their migration above.
        {L"Sights", "Sights", L"zoomStick", "zoomStick", L"1"},
        // The scope's real zoomed render (2026-10-27, research/39), on since 2026-11-04 (section 12). The marker
        // scopeCapturePost comes after its migration above.
        {L"Sights", "Sights", L"scopeCapture", "scopeCapture", L"1"},
        {L"Sights", "Sights", L"scopeCaptureSize", "scopeCaptureSize", L"1024"},
        {L"Sights", "Sights", L"scopeCaptureRate", "scopeCaptureRate", L"150"},
        {L"Sights", "Sights", L"scopeCapturePost", "scopeCapturePost", L"1"},
        // The spectator camera (2026-11-03, research/49), off by default: the rest of [Spectator] is file-only.
        {L"Spectator", "Spectator", L"enabled", "enabled", L"0"},
        {L"Hands", "Hands", L"noCover", "noCover", L"1"},
        {L"Hands", "Hands", L"physicalCrouch", "physicalCrouch", L"1"},
        {L"Hands", "Hands", L"crouchBelow", "crouchBelow", L"0.75"},
        {L"Hands", "Hands", L"standAbove", "standAbove", L"0.85"},
        {L"Hands", "Hands", L"crouchDwellS", "crouchDwellS", L"0.2"},
        {L"Hands", "Hands", L"aimAssist", "aimAssist", L"0"},
        {L"Hands", "Hands", L"bodyMove", "bodyMove", L"1"},
        {L"Hands", "Hands", L"leanM", "leanM", L"0.20"},
        {L"Hands", "Hands", L"bodyMoveMaxM", "bodyMoveMaxM", L"0.75"},
        // The weapon drawn, like exploring (2026-10-25, research/38).
        {L"Hands", "Hands", L"exploreStance", "exploreStance", L"0"},
        {L"Hands", "Hands", L"exploreStanceStorm", "exploreStanceStorm", L"1"},
        {L"Hands", "Hands", L"exploreRamp", "exploreRamp", L"1"},
        {L"Hands", "Hands", L"drawRate", "drawRate", L"6.0"},
        {L"Hands", "Hands", L"constantEye", "constantEye", L"1"},
        {L"Hands", "Hands", L"blendFix", "blendFix", L"1"},  // read by the host (research/38 B)
        {L"Hands", "Hands", L"crouchDrop", "crouchDrop", L"0"},  // read by the host (2026-11-04: no double crouch drop)
        // Fingers that follow the controllers (research/43).
        {L"Hands", "Hands", L"fingers", "fingers", L"1"},
        {L"Hands", "Hands", L"fingerCurlDeg", "fingerCurlDeg", L"70"},
        {L"Hands", "Hands", L"fingerTouch", "fingerTouch", L"1"},
        // The Mako cockpit's hazard lights (research/47 "Hazard lights", 2026-11-04).
        {L"Cockpit", "Cockpit", L"hazard", "hazard", L"1"},
        {L"Cockpit", "Cockpit", L"hazardBelow", "hazardBelow", L"0.30"},
        {L"Cockpit", "Cockpit", L"hazardPeriodS", "hazardPeriodS", L"1.1"},
        {L"Cockpit", "Cockpit", L"hazardWash", "hazardWash", L"1"},
        {L"Cockpit", "Cockpit", L"showTurret", "showTurret", L"1"},
        // The Mako's gunner (research/47 "The gunner", 2026-11-04).
        {L"Mako", "Mako", L"gunner", "gunner", L"1"},
        {L"Mako", "Mako", L"gunnerRangeM", "gunnerRangeM", L"120"},
        {L"Mako", "Mako", L"gunnerHandbackS", "gunnerHandbackS", L"3"},
        {L"Mako", "Mako", L"gunnerCannon", "gunnerCannon", L"1"},
        {L"Mako", "Mako", L"gunnerCrowd", "gunnerCrowd", L"3"},
        {L"Mako", "Mako", L"gunnerCrowdRadiusM", "gunnerCrowdRadiusM", L"6"},
        // [Comfort] (2026-10-23, research/31): a section our menu started (it writes only the keys it changes).
        {L"Comfort", "Comfort", L"turn", "turn", L"game"},
        {L"Comfort", "Comfort", L"snapDeg", "snapDeg", L"45"},
        {L"Comfort", "Comfort", L"vignette", "vignette", L"0"},
        {L"Comfort", "Comfort", L"vignetteStrength", "vignetteStrength", L"0.6"},
        {L"Comfort", "Comfort", L"vignetteWhen", "vignetteWhen", L"turn"},
        {L"Comfort", "Comfort", L"walk", "walk", L"head"},
        // The physical Mako controls (2026-10-31, research/46).
        {L"Mako", "Mako", L"controls", "controls", L"1"},
        {L"Mako", "Mako", L"grabRadiusM", "grabRadiusM", L"0.15"},
        {L"Mako", "Mako", L"stickTwistDeg", "stickTwistDeg", L"42"},
        {L"Mako", "Mako", L"stickDeadZoneDeg", "stickDeadZoneDeg", L"4"},
        {L"Mako", "Mako", L"stickInvert", "stickInvert", L"0"},
        {L"Mako", "Mako", L"throttleTravelM", "throttleTravelM", L"0.055"},
        {L"Mako", "Mako", L"throttleDetent", "throttleDetent", L"0.08"},
        {L"Mako", "Mako", L"leverOffset", "leverOffset", L"-0.362 -0.446 0.286"},
        {L"Mako", "Mako", L"stickOffset", "stickOffset", L"0.375 -0.415 0.314"},
        {L"Mako", "Mako", L"stickAimRate", "stickAimRate", L"90"},
        {L"Mako", "Mako", L"rumble", "rumble", L"0.12"},
        {L"Mako", "Mako", L"cameraFollow", "cameraFollow", L"1"},
        // Screens projected from the wrist (2026-11-04, research/53 section 6).
        {L"WorldUi", "WorldUi", L"wrist", "wrist", L"SkillGame"},
        {L"WorldUi", "WorldUi", L"wristWidthM", "wristWidthM", L"0.30"},
        {L"WorldUi", "WorldUi", L"wristAboveM", "wristAboveM", L"0.10"},
        {L"WorldUi", "WorldUi", L"wristTowardM", "wristTowardM", L"0.00"},
        {L"WorldUi", "WorldUi", L"wristFollow", "wristFollow", L"1"},
        {L"WorldUi", "WorldUi", L"wristBeam", "wristBeam", L"1"},
        {L"WorldUi", "WorldUi", L"omniTool", "omniTool", L"1"},
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
        log::Info("hands: added {} (defaults) to {}", addedKeys, ini::Utf8(path));
        ini = ReadIni(path);
        stamp = fs::last_write_time(path, ec);
    }
    gStamp = stamp;
    gPosture = ReadPosture(ini);
    if (std::string const text = Describe(gPosture); text != gPostureText) {
        gPostureText = text;
        log::Info("posture: settings: {}", text);
    }
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
    gComfort = ReadComfort(ini);
    if (std::string const text = Describe(gComfort); text != gComfortText) {
        gComfortText = text;
        log::Info("comfort: settings: {}", text);
    }
    gInteract = ReadInteract(ini);
    if (std::string const text = Describe(gInteract); text != gInteractText) {
        gInteractText = text;
        log::Info("interact: settings: {}", text);
    }
    gSquadContext = ReadSquadContext(ini);
    if (std::string const text = Describe(gSquadContext); text != gSquadContextText) {
        gSquadContextText = text;
        log::Info("squad context: settings: {}", text);
    }
    gSquadOrders = ReadSquadOrders(ini);
    if (std::string const text = Describe(gSquadOrders); text != gSquadOrdersText) {
        gSquadOrdersText = text;
        log::Info("squad orders: settings: {}", text);
    }
    gVent = ReadVent(ini);
    if (std::string const text = Describe(gVent); text != gVentText) {
        gVentText = text;
        log::Info("vent: settings: {}", text);
    }
    gHud = ReadHud(ini);
    if (std::string const text = vr::hud_pieces::Describe(gHud); text != gHudText) {
        gHudText = text;
        log::Info("hud pieces: settings: {}", text);
    }
    gMako = ReadMako(ini);
    if (std::string const text = Describe(gMako); text != gMakoText) {
        gMakoText = text;
        log::Info("mako: settings: {}", text);
    }
    gCockpit = ReadCockpit(ini);
    if (std::string const text = Describe(gCockpit); text != gCockpitText) {
        gCockpitText = text;
        log::Info("cockpit: settings: {}", text);
    }
    gSpectator = ReadSpectator(ini);
    if (std::string const text = Describe(gSpectator); text != gSpectatorText) {
        gSpectatorText = text;
        log::Info("spectator: settings: {}", text);
    }
    gGalaxy = ReadGalaxy(ini);
    if (std::string const text = Describe(gGalaxy); text != gGalaxyText) {
        gGalaxyText = text;
        log::Info("galaxy map: settings: {}", text);
    }
    gWorldUi = ReadWorldUi(ini);
    if (std::string const text = Describe(gWorldUi); text != gWorldUiText) {
        gWorldUiText = text;
        log::Info("world ui: settings: {}", text);
    }
    gGame = ReadGame(ini);
    if (std::string const text = gGame.pauseUnfocused ? "the game pauses while its window has no focus" : "the game runs on without focus"; text != gGameText) {
        gGameText = text;
        log::Info("game: settings: {}", text);
    }
    gSights = ReadSights(ini);
    if (std::string const text = Describe(gSights); text != gSightsText) {
        gSightsText = text;
        log::Info("sights: settings: {}", text);
    }
    gScopeCapture = ReadScopeCapture(ini);
    if (std::string const text = Describe(gScopeCapture); text != gScopeCaptureText) {
        gScopeCaptureText = text;
        log::Info("scope capture: settings: {}", text);
    }
    gHaveStamp = true;
    Values const values = ReadValues(ini);
    if (!gApplied || values != gValues) {
        gApplied = true;
        Apply(values);
        log::Info("hands: calibration pitch {:.1f}, yaw {:.1f}, roll {:.1f} degrees, gun roll {:.1f} degrees; body follows head {} (whole body {}, lean {:.2f} m, at most {:.2f} m), "
                  "arm stretch {:.2f}, tracking scale {}, arm length {} (from {}), {}",
            values.pitch, values.yaw, values.roll, values.gunRoll, values.bodyFollowsHead ? "on" : "off",
            values.bodyMove ? "turns and steps" : "off", values.leanM, values.bodyMoveMaxM, values.armStretch,
            values.trackingScale > 0.0 ? std::format("{:.1f} units/m", values.trackingScale) : std::string("the view's"),
            values.armLength > 0.0 ? std::format("{:.3f} m", values.armLength) : std::string("not calibrated"),
            ini::Utf8(path.filename()),
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
        log::Info("hands: level camera {} (on foot while the hand aims: recoil and aim assist don't pitch the view), recoil haptic {:.2f}",
            values.levelCamera ? "on" : "off", values.recoilHaptic);
        log::Info("hands: aim assist's friction and adhesion {} on foot while the hand aims ([Hands] aimAssist = {})",
            values.aimAssist ? "kept" : "off", values.aimAssist ? 1 : 0);
        log::Info("hands: weapon drawn: explore stance {}{}, explore speed ramp {}, draw and holster at {:.2f}x (research/38)",
            values.exploreStance ? "on" : "off", values.exploreStance ? (values.exploreStanceStorm ? " (sprinting too)" : " (not sprinting)") : "",
            values.exploreRamp ? "on" : "off", values.drawRate);
        log::Info("hands: fingers {} ({:.0f} degrees per joint at a full curl, touch sensors {}; research/43)",
            values.fingers ? "follow the controllers" : "off", values.fingerCurlDeg, values.fingerTouch ? "used" : "not used");
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
