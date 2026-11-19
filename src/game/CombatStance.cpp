// SPDX-License-Identifier: MIT
#include "game/CombatStance.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/ik/ArmPose.hpp"
#include "core/ik/CameraHook.hpp"
#include "core/ik/PoseGraft.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/vr/Controllers.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/BonePose.hpp"
#include "engine/CallFunction.hpp"
#if !defined(MEVR_GAME_LE1)
#include "engine/GameAdapter.hpp"  // the sequels' walk (engine::adapter::kWalk*)
#endif
#include "engine/ProcessEvent.hpp"
#include "engine/Skeleton.hpp"
#include "game/ArmIk.hpp"
#include "game/HandCalibration.hpp"
#include "game/HeadTracking.hpp"
#include "game/Player.hpp"
#include "game/Weapon.hpp"
#include "ue3/Handles.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mevr::game::combat_stance {
namespace {

namespace cs = vr::combat_stance;

// The stance's "still active" line.
constexpr double kActiveLogSeconds = 10.0;
// The tree is walked again at most this often while the pre-stage reports mismatches.
constexpr double kRewalkSeconds = 5.0;
// A draw/holster event closes once the switch has been over this long (or after kEventMaxSeconds).
constexpr double kEventSettleSeconds = 0.3;
constexpr double kEventMaxSeconds = 5.0;
// Walking-mode lines at most this often (a stick near the walk/run threshold flaps).
constexpr double kMoveLogSeconds = 0.25;
// Limits of the tree walk.
constexpr int kMaxNodes = 4000;
constexpr int kMaxChildren = 64;

#if defined(MEVR_GAME_LE1)
// Our reflection layer (research/34), each found once by name; engine/ReflectCheck compares each with LExSDKv2.
ue3::ClassRef gBlendBase{"Engine.AnimNodeBlendBase"};
ue3::ClassRef gSequenceClass{"Engine.AnimNodeSequence"};
ue3::ClassRef gCameraNativeBase{"SFXGame.SFXCameraNativeBase"};
ue3::ClassRef gCameraModeInterpolate{"SFXGame.SFXCameraMode_Interpolate"};
ue3::FieldRef gAnimations{"Engine.SkeletalMeshComponent", "Animations"};
ue3::FieldRef gNodeName{"Engine.AnimNode", "NodeName"};
ue3::FieldRef gNodeTickTag{"Engine.AnimNode", "NodeTickTag"};
ue3::FieldRef gChildren{"Engine.AnimNodeBlendBase", "Children"};
ue3::FieldRef gChildName{gChildren, "Name"};
ue3::FieldRef gChildAnim{gChildren, "Anim"};
ue3::FieldRef gChildWeight{gChildren, "Weight"};
ue3::FieldRef gSeqName{"Engine.AnimNodeSequence", "AnimSeqName"};
ue3::FieldRef gSeqRate{"Engine.AnimNodeSequence", "Rate"};
ue3::FieldRef gSeqTime{"Engine.AnimNodeSequence", "CurrentTime"};
ue3::FieldRef gSeqPlaying{"Engine.AnimNodeSequence", "bPlaying"};
ue3::FieldRef gIsCrouched{"Engine.Pawn", "bIsCrouched"};
ue3::FieldRef gVelocity{"Engine.Actor", "Velocity"};
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType"};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction"};
ue3::FieldRef gStorming{"SFXGame.BioPlayerController", "bStorming"};
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior"};
ue3::FieldRef gAccelRate{"SFXGame.BioPawnBehavior", "m_fMaxDesSpdAccelRate"};
ue3::FieldRef gDecelRate{"SFXGame.BioPawnBehavior", "m_fMaxDesSpdDecelRate"};
ue3::FieldRef gTacAccelRate{"SFXGame.BioPawnBehavior", "m_fMaxDesSpdTacAccelRate"};
ue3::FieldRef gTacDecelRate{"SFXGame.BioPawnBehavior", "m_fMaxDesSpdTacDecelRate"};
ue3::FieldRef gPawnAccelRate{"Engine.Pawn", "AccelRate"};
// The game's start, stop and skid steps playing (the walk's response, section C).
ue3::FieldRef gAnimStartState{"SFXGame.BioPawnBehavior", "m_eAnimStartState"};
ue3::FieldRef gAnimStopState{"SFXGame.BioPawnBehavior", "m_eAnimStopState"};
ue3::FieldRef gAnimSkidState{"SFXGame.BioPawnBehavior", "m_eAnimSkidState"};
ue3::FieldRef gPlayerCamera{"Engine.PlayerController", "PlayerCamera"};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode"};
ue3::FieldRef gInterpolateTo{"SFXGame.SFXCameraMode_Interpolate", "To"};
ue3::FunctionRef gIsInCombatActionStateFn{"SFXGame.BioPawn.IsInCombatActionState"};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons"};
ue3::FunctionRef gGetWalkingSpeedModeFn{"SFXGame.BioPawn.GetWalkingSpeedMode"};
#endif

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// The walk on the sequels (section C: ME1's rules on each game's fields, engine::adapter::kWalkOwnerClass, kWalkStepFlags,
// kWalkSpeedRamps; the ME2 and ME3 audits' MOV-14 to MOV-16). Their rows: engine/ReflectCheck's CheckSequelWalk.
ue3::FieldRef gVelocity{"Engine.Actor", "Velocity", ue3::kEveryGame};
ue3::FieldRef gPawnAccelRate{"Engine.Pawn", "AccelRate", ue3::kEveryGame};
#if defined(MEVR_GAME_LE2)
ue3::FieldRef gBehavior{"SFXGame.BioPawn", "m_oBehavior", ue3::kEveryGame};
ue3::FieldRef gEnableStartAnim{engine::adapter::kWalkOwnerClass, "m_bEnableStartAnim", ue3::kEveryGame};
ue3::FieldRef gEnableStopAnim{engine::adapter::kWalkOwnerClass, "m_bEnableStopAnim", ue3::kEveryGame};
ue3::FieldRef gEnableSkidAnim{engine::adapter::kWalkOwnerClass, "m_bEnableSkidAnim", ue3::kEveryGame};
#endif
ue3::FieldRef gAnimStartState{engine::adapter::kWalkOwnerClass, "m_eAnimStartState", ue3::kEveryGame};
ue3::FieldRef gAnimStopState{engine::adapter::kWalkOwnerClass, "m_eAnimStopState", ue3::kEveryGame};
ue3::FieldRef gAnimSkidState{engine::adapter::kWalkOwnerClass, "m_eAnimSkidState", ue3::kEveryGame};
ue3::FunctionRef gGetWalkingSpeedModeFn{"SFXGame.BioPawn.GetWalkingSpeedMode", ue3::kEveryGame};

// The object holding the move control's state (engine::adapter::kWalkOwnerClass): ME2's pawn behaviour, ME3's pawn.
ue3::Object* WalkOwner(ue3::Object* pawn) {
#if defined(MEVR_GAME_LE2)
    return pawn != nullptr ? gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
#else
    return pawn;
#endif
}
#endif

bool gStarted = false;
double gNow = 0.0;  // seconds since startup (the mod's ticks)

// ---- A: the tree ------------------------------------------------------------------------------------------------

struct Tree {
    ue3::Object const* mesh = nullptr;
    void const* tree = nullptr;  // the walked AnimTree (compared by address)
    std::vector<engine::bone_pose::StanceNode> combat;
    ue3::Object* nonCombatAnim = nullptr;  // node 0's NonCombat child: does it tick at zero weight?
    std::vector<ue3::Object*> draws;       // the WeaponEquip node's Draw sequences
    std::vector<ue3::Object*> holsters;    // ...and Holster sequences
    std::string drawNames;                 // for the log
    std::string holsterNames;
    int nodes = 0;
    bool weaponEquip = false;
};
Tree gTree;
double gLastWalk = -1e9;
std::uint64_t gRejectsSeen = 0;

// ---- A: the decision ----------------------------------------------------------------------------------------------

cs::Why gWhy = cs::Why::Off;
bool gWhyLogged = false;
cs::Why gLoggedWhy = cs::Why::Off;
double gActiveSince = 0.0;
double gActiveLogAt = 0.0;
int gLoggedOrder = 0;
bool gLoggedFirstDraw = false;
// The explore branch's NodeTickTag across ticks while active: whether it ticks at weight 0.
std::int32_t gNonCombatTag = 0;
bool gHaveNonCombatTag = false;
unsigned gNonCombatTicked = 0;
unsigned gNonCombatTicks = 0;
std::uint64_t gAppliedAtLog = 0;
std::uint64_t gOverriddenAtLog = 0;
std::uint64_t gGraftedAtLog = 0;
std::uint64_t gNotGraftedAtLog = 0;
std::string gModeClass = "none";

// ---- A: the arms keep the combat pose (research/38 A, "Redesign") ------------------------------------------------

// The arms' bones, per skeleton asset: each upper arm and everything below it (forearm, twist bones, hand, fingers,
// Prop01, LHIK...), without the camera hook bones (Camera, God) and their subtrees, parents first.
struct Arms {
    ue3::Object const* asset = nullptr;  // compared by address only
    int boneCount = 0;
    std::string assetName;
    bool ok = false;
    std::string why = "not looked up";
    std::vector<int> bones;
    std::vector<int> parents;
    std::vector<std::uint64_t> keys;
    std::vector<std::string> names;
    int leftHand = -1;
    int rightHand = -1;
    std::uint64_t leftKey = 0;
    std::uint64_t rightKey = 0;
};
Arms gArms;

// The combat pose of the arms, learned per skeleton asset and weapon type (the animations are per weapon type).
struct Learned {
    std::string key;                   // asset | weapon
    std::vector<std::uint64_t> keys;   // the bones it was learned for
    std::vector<ik::Transform> relative;
    ik::Transform leftInRight;
};
std::vector<Learned> gLearned;
int gHandIndex = -1;           // the one in use (gLearned), -1: none
std::string gLastWeaponKey;    // the last weapon key with a gun in the hand (a switch between guns keeps it)
ik::StillPose gLearn;
std::string gLearnKey;
std::uint64_t gLearnSerial = 0;
bool gLearning = false;        // captures requested this tick

// ---- C: the ramp --------------------------------------------------------------------------------------------------

// The walk's speed ramps (the pawn behaviour's) and acceleration (the pawn's): the game's own values, kept to put back, and ours
// (cs::WantedRamp).
struct Ramp {
    ue3::Object* behavior = nullptr;
    ue3::Object* pawn = nullptr;
    bool written = false;
    cs::WalkRamp saved;  // the game's
    cs::WalkRamp wrote;  // ours
    int writes = 0;
};
Ramp gRamp;

std::string RampText(cs::WalkRamp const& r) {
    return std::format("explore {:.2f}/{:.2f}, combat {:.2f}/{:.2f} per s, acceleration {:.0f} u/s^2", r.accel, r.decel, r.tacAccel,
        r.tacDecel, r.accelRate);
}
std::string gRampWhy = "not started";

// The speed from the push and the start/stop steps (section C below, LE1).
enum class MoveRan : std::uint8_t { None, Explore, Combat };

struct Stick {
    bool wanted = false;        // the mod's tick: VR on foot, head-tracked, [Hands] linearStick
    double fullAt = cs::kDefaultLinearFullAt;
    std::string why = "not started";
    MoveRan ran = MoveRan::None;  // the PlayerMove this tick ran (CallFunction post), until its PlayerTick ends
    std::uint64_t moves = 0;      // PlayerTicks after a move, this heartbeat period
    std::uint64_t applied = 0;    // ... with the speed set from the push
    std::uint64_t kept = 0;       // ... with the game's own left (no push, a walk toggle, a crouch, cover, ignored input)
    double moveMag = 0.0;         // the last push read
    double fraction = -1.0;       // the last fraction set (-1: the game's)
    double gameTarget = 0.0;      // the game's own target just before (m_fDesiredMaxSpeed)
    bool loggedFirst = false;
    bool loggedThresholds = false;
};
Stick gStick;
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
// The sequels' let-go (OnSequelMove): [Hands] linearWalk in VR on foot skips the move functions' 0.5 s blend down.
struct Stop {
    bool wanted = false;
    std::uint64_t stops = 0;  // let-goes stopped at once
    bool logged = false;
};
Stop gStop;
#endif

// The start, stop and skid steps: the game's flags kept to put back, each only if still ours.
struct Steps {
    ue3::Object* behavior = nullptr;
    bool written = false;
    std::array<bool, 3> saved{};  // the game's m_bEnableStartAnim, m_bEnableStopAnim, m_bEnableSkidAnim
    int writes = 0;
    std::string why = "not started";
};
Steps gSteps;
std::string StepsText(std::array<bool, 3> const& flags) {
    return std::format("start {}, stop {}, skid {}", flags[0] ? "on" : "off", flags[1] ? "on" : "off", flags[2] ? "on" : "off");
}

// The snapshot's "stick" and "steps".
std::string StickJson() {
    namespace json = vr::test_request;
    return std::format("{{\"wanted\": {}, \"why\": {}, \"fullAt\": {}, \"moveMag\": {}, \"fraction\": {}, \"gameTarget\": {}}}",
        gStick.wanted ? "true" : "false", json::JsonString(gStick.why), json::JsonNumber(gStick.fullAt), json::JsonNumber(gStick.moveMag),
        json::JsonNumber(gStick.fraction), json::JsonNumber(gStick.gameTarget));
}

std::string StepsJson() {
    namespace json = vr::test_request;
    return std::format("{{\"why\": {}, \"written\": {}, \"writes\": {}, \"game\": [{}, {}, {}]}}", json::JsonString(gSteps.why),
        gSteps.written ? "true" : "false", gSteps.writes, gSteps.saved[0] ? "true" : "false", gSteps.saved[1] ? "true" : "false",
        gSteps.saved[2] ? "true" : "false");
}

// The walking mode (EWalkingSpeedMode) and speed.
#if defined(MEVR_GAME_LE1)
constexpr char const* kWalkModes[] = {"ExploreRun", "ExploreWalk", "ExploreStorming", "ExploreCrouched", "CombatRun", "CombatWalk",
    "CombatStorming", "CombatCrouched", "CombatZoomed", "CoverMove", "CoverCrouched"};
#else
constexpr char const* kWalkModes[] = {"ExploreRun", "ExploreWalk", "ExploreStorming", "ExploreCrouched", "CombatRun", "CombatWalk",
    "CombatStorming", "CombatCrouched", "CombatSniping", "CombatZoomed", "CoverMove", "CoverCrouched", "Crawling"};
#endif
int gWalkMode = -1;
double gWalkModeSince = 0.0;
double gWalkTop = 0.0;
double gLastMoveLog = -1e9;
double gSpeed = 0.0;

// ---- D: draws -----------------------------------------------------------------------------------------------------

struct Event {
    bool open = false;
    char const* kind = "draw";
    std::string weapon;
    double age = 0.0;
    double switchStart = -1.0;
    double switchEnd = -1.0;
    double stateAt = -1.0;
    double inHandAt = -1.0;
    double lastChange = 0.0;
    float maxRate = 0.0f;    // the fastest draw/holster sequence rate seen while it played
    float maxSeqTime = 0.0f;  // its furthest time
    bool sawSequence = false;
    std::uintptr_t startKey = 0;  // weapon::Key() when it opened
};
Event gEvent;
bool gPrevDrawn = false;
bool gPrevSwitching = false;
bool gHavePrev = false;
std::string gPrevWeapon = "none";
bool gDrawn = false;
bool gSwitching = false;

struct RateWrite {
    ue3::Object* sequence = nullptr;
    float original = 1.0f;
    float wrote = 1.0f;
};
std::vector<RateWrite> gRates;
unsigned gRateWrites = 0;  // this heartbeat period

#if defined(MEVR_GAME_LE1)

std::string Lower(std::string_view text) {
    std::string out;
    for (char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

char const* WeaponName(weapon::RangedType type) {
    switch (type) {
    case weapon::RangedType::Pistol: return "pistol";
    case weapon::RangedType::Shotgun: return "shotgun";
    case weapon::RangedType::AssaultRifle: return "assault rifle";
    case weapon::RangedType::Sniper: return "sniper rifle";
    case weapon::RangedType::Smg: return "SMG";
    case weapon::RangedType::Unknown: break;
    }
    return weapon::Key() != 0 ? "unknown weapon" : "none";
}

#endif
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
ue3::Function* gIsInCombatActionState = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;
ue3::Function* gGetWalkingSpeedMode = nullptr;
#endif
#if defined(MEVR_GAME_LE1)

bool CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

#endif
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
int CallByte(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return -1;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    std::optional<std::uint8_t> const value = parms.Return<std::uint8_t>();
    return value ? static_cast<int>(*value) : -1;
}

#endif
#if defined(MEVR_GAME_LE1)

std::string NodeName(ue3::Object const* node) {
    std::optional<ue3::Name> const name = gNodeName.Read<ue3::Name>(node);
    return name ? ue3::runtime::NameText(*name) : std::string();
}

// The AnimNodeSequences at or under `node` (a few levels: the WeaponEquip child's sequence may be wrapped).
void Sequences(ue3::Object* node, int depth, std::vector<ue3::Object*>& out, std::string& names) {
    if (node == nullptr || depth > 4 || out.size() >= 8) {
        return;
    }
    if (ue3::IsA(node, gSequenceClass)) {
        if (std::find(out.begin(), out.end(), node) == out.end()) {
            out.push_back(node);
            std::optional<ue3::Name> const seq = gSeqName.Read<ue3::Name>(node);
            names += std::format("{}{} ({})", names.empty() ? "" : ", ", NodeName(node), seq ? ue3::runtime::NameText(*seq) : std::string("?"));
        }
        return;
    }
    if (!ue3::IsA(node, gBlendBase)) {
        return;
    }
    if (std::optional<ue3::Elements> const children = gChildren.ElementsOf(node, kMaxChildren)) {
        for (std::int32_t i = 0; i < children->count; ++i) {
            Sequences(gChildAnim.Read<ue3::Object*>(children->At(i)).value_or(nullptr), depth + 1, out, names);
        }
    }
}

void Walk(ue3::Object* node, std::vector<ue3::Object const*>& visited, Tree& tree) {
    if (node == nullptr || static_cast<int>(visited.size()) >= kMaxNodes ||
        std::find(visited.begin(), visited.end(), node) != visited.end()) {
        return;
    }
    visited.push_back(node);
    if (!ue3::IsA(node, gBlendBase)) {
        return;
    }
    std::optional<ue3::Elements> const children = gChildren.ElementsOf(node, kMaxChildren);
    if (!children) {
        return;
    }
    std::string const name = Lower(NodeName(node));
    if (name == "combatmode" && tree.combat.size() < 4) {
        int nonCombat = -1;
        int combat = -1;
        for (std::int32_t i = 0; i < children->count; ++i) {
            std::optional<ue3::Name> const childName = gChildName.Read<ue3::Name>(children->At(i));
            std::string const text = childName ? Lower(ue3::runtime::NameText(*childName)) : std::string();
            if (text == "noncombat") {
                nonCombat = i;
            } else if (text == "combat") {
                combat = i;
            }
        }
        if (nonCombat >= 0 && combat >= 0) {
            engine::bone_pose::StanceNode n;
            n.node = node;
            n.children = children->data;
            n.childCount = children->count;
            n.nonCombat = nonCombat;
            n.combat = combat;
            if (tree.combat.empty()) {
                tree.nonCombatAnim = gChildAnim.Read<ue3::Object*>(children->At(nonCombat)).value_or(nullptr);
            }
            tree.combat.push_back(n);
        }
    }
    if (name == "weaponequip") {
        tree.weaponEquip = true;
        for (std::int32_t i = 0; i < children->count; ++i) {
            std::optional<ue3::Name> const childName = gChildName.Read<ue3::Name>(children->At(i));
            std::string const text = childName ? Lower(ue3::runtime::NameText(*childName)) : std::string();
            ue3::Object* const anim = gChildAnim.Read<ue3::Object*>(children->At(i)).value_or(nullptr);
            if (text == "draw") {
                Sequences(anim, 0, tree.draws, tree.drawNames);
            } else if (text == "holster") {
                Sequences(anim, 0, tree.holsters, tree.holsterNames);
            }
        }
    }
    for (std::int32_t i = 0; i < children->count; ++i) {
        Walk(gChildAnim.Read<ue3::Object*>(children->At(i)).value_or(nullptr), visited, tree);
    }
}

// The rates we wrote into a tree that's going away are dropped, not restored (its nodes may be gone).
void ForgetRates() {
    gRates.clear();
}

void WalkTree(ue3::Object const* mesh, ue3::Object* root) {
    Tree tree;
    tree.mesh = mesh;
    tree.tree = root;
    std::vector<ue3::Object const*> visited;
    visited.reserve(512);
    Walk(root, visited, tree);
    tree.nodes = static_cast<int>(visited.size());
    if (root != gTree.tree) {
        ForgetRates();
    }
    bool const changed = root != gTree.tree || tree.combat.size() != gTree.combat.size() || tree.draws.size() != gTree.draws.size();
    gTree = std::move(tree);
    gLastWalk = gNow;
    if (changed) {
        log::Info("stance: the player's AnimTree {} ({} nodes): {} CombatMode node{} with NonCombat and Combat children{}; "
                  "WeaponEquip {} (Draw: {}; Holster: {})",
            ue3::runtime::FullName(static_cast<ue3::Object const*>(gTree.tree)), gTree.nodes, gTree.combat.size(),
            gTree.combat.size() == 1 ? "" : "s", gTree.combat.empty() ? " (the explore stance can't apply)" : "",
            gTree.weaponEquip ? "found" : "NOT found", gTree.drawNames.empty() ? "none" : gTree.drawNames,
            gTree.holsterNames.empty() ? "none" : gTree.holsterNames);
    }
}

std::string BoneText(Arms const& arms, int bone) {
    auto const at = std::find(arms.bones.begin(), arms.bones.end(), bone);
    return at != arms.bones.end() ? std::format("{}({})", arms.names[static_cast<std::size_t>(at - arms.bones.begin())], bone)
                                  : std::format("bone {}", bone);
}

// The arms' bones of the mesh's skeleton asset (once per asset; logged).
void ResolveArms(ue3::Object* mesh) {
    ue3::Object const* const asset = mesh != nullptr ? player::SkeletalMesh(mesh) : nullptr;
    int const count = mesh != nullptr ? engine::skeleton::RefBoneCount(mesh) : -1;
    if (asset == nullptr || count <= 0) {
        gArms = {};
        gArms.why = "no skeleton";
        return;
    }
    if (gArms.asset == asset && gArms.boneCount == count) {
        // Same address and size; also the same hand bones (a level load can put another asset there).
        if (!gArms.ok || (engine::skeleton::RefBoneKey(mesh, gArms.leftHand) == gArms.leftKey &&
                             engine::skeleton::RefBoneKey(mesh, gArms.rightHand) == gArms.rightKey)) {
            return;
        }
    }
    Arms arms;
    arms.asset = asset;
    arms.boneCount = count;
    arms.assetName = ue3::runtime::FullName(asset);
    std::optional<engine::skeleton::RefSkeleton> const skeleton = engine::skeleton::ReadRefSkeleton(mesh);
    if (!skeleton || static_cast<int>(skeleton->names.size()) != count) {
        arms.why = "the skeleton couldn't be read";
        gArms = std::move(arms);
        log::Warn("stance: {}: {}; the explore stance can't keep the arms' combat pose, so it stays off", gArms.assetName, gArms.why);
        return;
    }
    std::vector<int> const hooks = ik::FindCameraHookBones(skeleton->names);
    std::array<std::optional<ik::ArmChain>, 2> chains;
    std::array<std::vector<int>, 2> subtrees;
    for (int side : {0, 1}) {
        chains[side] = ik::FindArmChain(skeleton->names, skeleton->parents, side);
        if (chains[side]) {
            subtrees[side] = ik::Subtree(skeleton->parents, chains[side]->upper, hooks);
        }
    }
    if (!chains[0] || !chains[1] || subtrees[0].empty() || subtrees[1].empty()) {
        arms.why = "an arm wasn't found";
    } else {
        arms.bones = subtrees[0];
        arms.bones.insert(arms.bones.end(), subtrees[1].begin(), subtrees[1].end());
        std::sort(arms.bones.begin(), arms.bones.end());  // parents come before children in the skeleton
        arms.bones.erase(std::unique(arms.bones.begin(), arms.bones.end()), arms.bones.end());
        arms.parents = ik::ParentsOf(skeleton->parents, arms.bones);
        for (int bone : arms.bones) {
            arms.keys.push_back(skeleton->keys[static_cast<std::size_t>(bone)]);
            arms.names.push_back(skeleton->names[static_cast<std::size_t>(bone)]);
        }
        arms.leftHand = chains[0]->hand;
        arms.rightHand = chains[1]->hand;
        arms.leftKey = skeleton->keys[static_cast<std::size_t>(arms.leftHand)];
        arms.rightKey = skeleton->keys[static_cast<std::size_t>(arms.rightHand)];
        if (static_cast<int>(arms.bones.size()) > engine::bone_pose::kMaxStanceHandBones) {
            arms.why = std::format("{} arm bones, more than {}", arms.bones.size(), engine::bone_pose::kMaxStanceHandBones);
        } else {
            arms.ok = true;
            arms.why.clear();
        }
    }
    gArms = std::move(arms);
    gHandIndex = -1;
    gLearn.Reset();
    gLearnKey.clear();
    if (!gArms.ok) {
        log::Warn("stance: {}: {}; the explore stance can't keep the arms' combat pose, so it stays off", gArms.assetName, gArms.why);
        return;
    }
    std::string props;
    for (std::size_t i = 0; i < gArms.bones.size(); ++i) {
        std::string const lower = Lower(gArms.names[i]);
        if (lower.find("prop") != std::string::npos || lower.find("ik") != std::string::npos || lower.find("weapon") != std::string::npos) {
            props += std::format("{}{}({})", props.empty() ? "" : ", ", gArms.names[i], gArms.bones[i]);
        }
    }
    std::string excluded;
    for (int bone : hooks) {
        excluded += std::format("{}{}({})", excluded.empty() ? "" : ", ", skeleton->names[static_cast<std::size_t>(bone)], bone);
    }
    log::Info("stance: with the explore stance the arms keep the combat pose: {} bones of {} ({}): left {} and {} below it, right {} "
              "and {} below it; gun and IK bones among them: {}; never touched: {}",
        gArms.bones.size(), count, gArms.assetName, BoneText(gArms, chains[0]->upper), subtrees[0].size() - 1,
        BoneText(gArms, chains[1]->upper), subtrees[1].size() - 1, props.empty() ? "none" : props, excluded.empty() ? "no camera hook bones" : excluded);
}

// The weapon whose arms pose applies: the gun in the hand by type, else the last one (between guns).
std::string WeaponKey() {
    if (weapon::Key() != 0) {
        gLastWeaponKey = std::format("{}|{}", gArms.assetName, WeaponName(weapon::Type()));
    }
    return gLastWeaponKey;
}

int FindLearned(std::string const& key) {
    for (std::size_t i = 0; i < gLearned.size(); ++i) {
        if (gLearned[i].key == key && gLearned[i].keys == gArms.keys) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::string RelativeText(std::vector<ik::Transform> const& relative, ik::Transform const& leftInRight) {
    std::string text;
    for (std::size_t i = 0; i < gArms.bones.size() && i < relative.size(); ++i) {
        std::string const lower = Lower(gArms.names[i]);
        if (lower.find("prop") != std::string::npos || lower == "lhik") {
            ik::Vec3 const t = relative[i].translation;
            text += std::format("{}{} at ({:.1f}, {:.1f}, {:.1f}) on {}", text.empty() ? "" : "; ", gArms.names[i], t.x, t.y, t.z,
                BoneText(gArms, gArms.parents[i]));
        }
    }
    ik::Vec3 const l = leftInRight.translation;
    return text + std::format("{}the left hand {:.1f} units from the right ({:.1f}, {:.1f}, {:.1f})", text.empty() ? "" : "; ",
                      ik::Length(l), l.x, l.y, l.z);
}

// Learning the arms' combat pose for `key` while the decision says LearningHands: one capture per call composed as
// combat (the hook's), settled by StillPose (it holds still for 10 frames, or 2 s pass), then kept.
void UpdateLearning(double deltaSeconds, std::string const& key, bool capturing) {
    engine::bone_pose::HandCapture const& capture = engine::bone_pose::LatestHandCapture();
    if (!capturing || key != gLearnKey) {
        // Starts (or stops) learning: only captures from now on count.
        gLearn.Reset();
        gLearnKey = capturing ? key : std::string();
        gLearnSerial = capture.serial;
        return;
    }
    if (capture.serial == gLearnSerial || capture.count != static_cast<int>(gArms.bones.size())) {
        return;
    }
    gLearnSerial = capture.serial;
    std::vector<ik::Transform> sample(capture.relative.begin(), capture.relative.begin() + capture.count);
    sample.push_back(capture.leftInRight);
    ik::StillPose::State const state = gLearn.Feed(sample, deltaSeconds);
    if (state == ik::StillPose::State::Learning) {
        return;
    }
    Learned learned;
    learned.key = key;
    learned.keys = gArms.keys;
    learned.relative = gLearn.Latest();
    learned.leftInRight = learned.relative.back();
    learned.relative.pop_back();
    ik::PoseDifference const d = gLearn.LastDifference();
    log::Info("stance: learned the combat pose of the arms for {} ({}: {:.2f} s, {}): {}; from now on the body composes as explore "
              "with these arms",
        key.substr(key.find('|') + 1), state == ik::StillPose::State::Still ? "held still" : "STILL MOVING, taken anyway",
        gLearn.Seconds(),
        state == ik::StillPose::State::Still ? std::format("{} frames within 0.3 units and 1.5 degrees", gLearn.StillFrames())
                                             : std::format("the last frame moved {:.2f} units, {:.2f} degrees", d.units, d.degrees),
        RelativeText(learned.relative, learned.leftInRight));
    int const existing = FindLearned(key);
    if (existing >= 0) {
        gLearned[static_cast<std::size_t>(existing)] = std::move(learned);
    } else {
        gLearned.push_back(std::move(learned));
    }
    gLearn.Reset();
    gLearnKey.clear();
}

// The camera mode's class (or the one it blends to), for the scope test.
std::string CameraModeClass(ue3::Object* controller) {
    ue3::Object* const base = gPlayerCamera.Read<ue3::Object*>(controller).value_or(nullptr);
    if (base == nullptr || !ue3::IsA(base, gCameraNativeBase)) {
        return "none";
    }
    ue3::Object* mode = gCurrentCameraMode.Read<ue3::Object*>(base).value_or(nullptr);
    if (mode != nullptr && ue3::IsA(mode, gCameraModeInterpolate)) {
        mode = gInterpolateTo.Read<ue3::Object*>(mode).value_or(nullptr);
    }
    if (mode == nullptr || !ue3::runtime::IsAlive(mode)) {
        return "none";
    }
    return ue3::runtime::Get().NameText(ue3::runtime::Get().ClassOf(mode));
}

std::string Weights(float const (&w)[2]) {
    return std::format("{:.2f}/{:.2f}", w[0], w[1]);
}

void LogWhy(cs::Why why, engine::bone_pose::StanceStats const& stats) {
    if (gWhyLogged && why == gLoggedWhy) {
        return;
    }
    gWhyLogged = true;
    gLoggedWhy = why;
    if (why == cs::Why::Active) {
        gActiveSince = gNow;
        gActiveLogAt = gNow;
        log::Info("stance: the body composes as explore (CombatMode weights NonCombat/Combat {} taken from the game's; camera mode {})",
            stats.last.valid ? Weights(stats.last.startWeights) : std::string("?/?"), gModeClass);
    } else {
        log::Info("stance: the game's pose: {}", cs::Name(why));
    }
}

// The one-time [verify] lines (research/38 A): which order the hook sees, and a call during a draw.
void LogVerify(engine::bone_pose::StanceStats const& stats) {
    if (stats.order != gLoggedOrder && stats.order != static_cast<int>(cs::OrderCheck::Order::Unknown)) {
        gLoggedOrder = stats.order;
        engine::bone_pose::StanceProbe const& p = stats.last;
        log::Info("stance: [verify] {} ({} calls with the node ticked between calls, {} with it ticked during one; the last: "
                  "dt {:.4f}, NodeTickTag {} -> {}, CombatMode weights NonCombat/Combat {} at the start, {} at the end){}",
            cs::Name(static_cast<cs::OrderCheck::Order>(stats.order)), stats.confirmations, stats.insideCalls, p.deltaTime, p.startTag,
            p.endTag, Weights(p.startWeights), Weights(p.endWeights),
            stats.order == static_cast<int>(cs::OrderCheck::Order::Before) ? ": composing as explore is safe"
                                                                           : ": the explore stance stands down (research/38 A needs another seam)");
    }
    if (!gLoggedFirstDraw && stats.firstDraw.valid) {
        gLoggedFirstDraw = true;
        engine::bone_pose::StanceProbe const& p = stats.firstDraw;
        bool const timeMoved = std::fabs(p.drawEnd - p.drawStart) > 1e-6f;
        bool const weightsMoved = !p.wrote && (p.startWeights[0] != p.endWeights[0] || p.startWeights[1] != p.endWeights[1]);
        log::Info("stance: [verify] during a draw, in UpdateSkelPose (dt {:.4f}{}): CombatMode weights NonCombat/Combat {} at the "
                  "start, {} at the end; CB_Draw time {:.3f} -> {:.3f}; NodeTickTag {} -> {}: {}",
            p.deltaTime, p.wrote ? ", ours written" : "", Weights(p.startWeights), Weights(p.endWeights), p.drawStart, p.drawEnd,
            p.startTag, p.endTag,
            timeMoved || weightsMoved || p.startTag != p.endTag ? "the tree ticked INSIDE the call"
                                                                 : "nothing moved inside the call: the tree ticked before it");
    }
}

void UpdateStance(double deltaSeconds, Link const& link, ue3::Object* controller, ue3::Object* pawn, ue3::Object* mesh, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    engine::bone_pose::StanceStats const stats = engine::bone_pose::GetStanceStats();
    ue3::Object* const root = mesh != nullptr ? gAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) : nullptr;
    // The tree is walked for A and for D's sequences.
    bool const walk = (settings.exploreStance || cs::ClampDrawRate(settings.drawRate) > 1.0005 || cs::ClampHolsterRate(settings.holsterRate) > 1.0005) && link.headTrackedView && onFoot &&
        pawn != nullptr && root != nullptr;
    bool const wanted = walk && settings.exploreStance;
    // A tree we no longer walk (a level loaded into a cutscene or conversation, the pawn gone) may be freed: its nodes
    // are never read again, nor the rates we wrote into them restored.
    if (!walk && gTree.tree != nullptr && (mesh != gTree.mesh || root != gTree.tree)) {
        ForgetRates();
        gTree = {};
    }
    if (walk && (root != gTree.tree || mesh != gTree.mesh ||
                      (stats.rejected != gRejectsSeen && gNow - gLastWalk >= kRewalkSeconds))) {
        WalkTree(mesh, root);
    }
    gRejectsSeen = stats.rejected;

    cs::Inputs in;
    in.enabled = settings.exploreStance;
    in.headTrackedView = link.headTrackedView;
    in.onFoot = onFoot;
    in.pawn = pawn != nullptr;
    in.firstPerson = arm_ik::FirstPerson();
    in.drawn = gDrawn;
    if (pawn != nullptr) {
        in.crouched = gIsCrouched.ReadBool(pawn).value_or(false);
        in.cover = gCoverType.Read<std::uint8_t>(pawn).value_or(0) != 0 || gCoverAction.Read<std::uint8_t>(pawn).value_or(0) != 0;
    }
    gModeClass = controller != nullptr ? CameraModeClass(controller) : std::string("none");
    in.scoped = Lower(gModeClass).find("sniper") != std::string::npos;
    in.storming = controller != nullptr && gStorming.ReadBool(controller).value_or(false);
    in.stanceWhileStorming = settings.exploreStanceStorm;
    in.node = wanted && !gTree.combat.empty() && gTree.tree == root;
    in.hook = engine::bone_pose::Installed();
    in.orderKnown = stats.order != static_cast<int>(cs::OrderCheck::Order::Unknown);
    in.orderInside = stats.order == static_cast<int>(cs::OrderCheck::Order::Inside);
    // The arms keep the combat pose: their bones, and the pose learned for the weapon in the hand.
    if (wanted) {
        ResolveArms(mesh);
    }
    std::string const weaponKey = wanted && gArms.ok ? WeaponKey() : std::string();
    gHandIndex = weaponKey.empty() ? -1 : FindLearned(weaponKey);
    in.handBones = wanted && gArms.ok;
    in.handPose = gHandIndex >= 0;
    gWhy = cs::Decide(in);
    LogWhy(gWhy, stats);
    LogVerify(stats);
    // Learned from calls composed as combat, with the gun in the hand and the draw over.
    bool const capturing = gWhy == cs::Why::LearningHands && !gSwitching && weapon::Key() != 0;
    gLearning = capturing;
    UpdateLearning(deltaSeconds, weaponKey, capturing);

    // Published whenever the tree is known and VR is on foot, so the order check runs before the first draw.
    if (in.node && in.hook && !stats.broken) {
        ue3::PropertyInfo const* const animations = gAnimations.Get();
        ue3::PropertyInfo const* const children = gChildren.Get();
        ue3::PropertyInfo const* const inner = gChildren.Inner();
        ue3::PropertyInfo const* const weight = gChildWeight.Get();
        ue3::PropertyInfo const* const tag = gNodeTickTag.Get();
        ue3::PropertyInfo const* const time = gSeqTime.Get();
        ue3::PropertyInfo const* const playing = gSeqPlaying.Get();
        if (animations != nullptr && children != nullptr && inner != nullptr && weight != nullptr && tag != nullptr &&
            inner->elementSize > 0 && weight->offset + 4 <= inner->elementSize) {
            engine::bone_pose::Stance s;
            s.mesh = mesh;
            s.tree = root;
            s.nodeCount = static_cast<int>(std::min<std::size_t>(gTree.combat.size(), s.nodes.size()));
            for (int i = 0; i < s.nodeCount; ++i) {
                s.nodes[i] = gTree.combat[i];
            }
            s.animationsOffset = static_cast<std::uint32_t>(animations->offset);
            s.childrenOffset = static_cast<std::uint32_t>(children->offset);
            s.childStride = static_cast<std::uint32_t>(inner->elementSize);
            s.weightOffset = static_cast<std::uint32_t>(weight->offset);
            s.tickTagOffset = static_cast<std::uint32_t>(tag->offset);
            s.apply = gWhy == cs::Why::Active;
            if (gArms.ok && in.handBones) {
                engine::bone_pose::StanceHands& h = s.hands;
                h.count = static_cast<int>(gArms.bones.size());
                for (std::size_t i = 0; i < gArms.bones.size(); ++i) {
                    h.bones[i] = gArms.bones[i];
                    h.parents[i] = gArms.parents[i];
                    h.keys[i] = gArms.keys[i];
                }
                h.leftHand = gArms.leftHand;
                h.rightHand = gArms.rightHand;
                h.leftKey = gArms.leftKey;
                h.rightKey = gArms.rightKey;
                h.capture = capturing;
                Learned const* const learned = gHandIndex >= 0 ? &gLearned[static_cast<std::size_t>(gHandIndex)] : nullptr;
                if (s.apply && learned != nullptr && learned->relative.size() == gArms.bones.size()) {
                    h.apply = true;
                    std::copy(learned->relative.begin(), learned->relative.end(), h.relative.begin());
                    h.hasLeftInRight = true;
                    h.leftInRight = learned->leftInRight;
                }
            }
            if (!gTree.draws.empty() && time != nullptr && playing != nullptr && playing->kind == ue3::Kind::Bool) {
                s.drawSequence = gTree.draws.front();
                s.currentTimeOffset = static_cast<std::uint32_t>(time->offset);
                s.playingOffset = static_cast<std::uint32_t>(playing->offset);
                s.playingMask = playing->boolMask;
            }
            engine::bone_pose::PublishStance(s);
        } else {
            engine::bone_pose::ClearStance();
        }
    } else {
        engine::bone_pose::ClearStance();
    }

    // While active: does the explore branch tick at weight 0 (research/38 A, [verify])? And the 10 s line.
    if (gWhy == cs::Why::Active && gTree.nonCombatAnim != nullptr) {
        if (std::optional<std::int32_t> const t = gNodeTickTag.Read<std::int32_t>(gTree.nonCombatAnim)) {
            if (gHaveNonCombatTag) {
                ++gNonCombatTicks;
                gNonCombatTicked += *t != gNonCombatTag ? 1u : 0u;
            }
            gNonCombatTag = *t;
            gHaveNonCombatTag = true;
        }
    } else {
        gHaveNonCombatTag = false;
    }
    if (gWhy == cs::Why::Active && gNow - gActiveLogAt >= kActiveLogSeconds) {
        gActiveLogAt = gNow;
        log::Info("stance: the body composes as explore for {:.0f} s (CombatMode weights NonCombat/Combat {} taken from the game's; "
                  "{} calls composed as explore, {} with the arms put back at their combat pose{}, {} where the call changed our "
                  "weights, {} rejected{}; the explore branch ticked {} of {} ticks at weight 0{})",
            gNow - gActiveSince, stats.last.valid ? Weights(stats.last.startWeights) : std::string("?/?"),
            stats.applied - gAppliedAtLog, stats.grafted - gGraftedAtLog,
            stats.notGrafted != gNotGraftedAtLog ? std::format(", {} WITHOUT", stats.notGrafted - gNotGraftedAtLog) : std::string(),
            stats.overridden - gOverriddenAtLog, stats.rejected,
            stats.lastReject != nullptr ? std::format(", last: {}", stats.lastReject) : std::string(), gNonCombatTicked, gNonCombatTicks,
            gNonCombatTicks > 0 && gNonCombatTicked == 0 ? ": FROZEN (its nodes skip their tick at weight 0: the legs won't walk)" : "");
        gAppliedAtLog = stats.applied;
        gOverriddenAtLog = stats.overridden;
        gGraftedAtLog = stats.grafted;
        gNotGraftedAtLog = stats.notGrafted;
        gNonCombatTicked = gNonCombatTicks = 0;
    }
}

#endif  // MEVR_GAME_LE1 (A)

// ---- C ------------------------------------------------------------------------------------------------------------
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)

bool IsPlayersBehavior(ue3::Object* behavior) {
    ue3::Object* const pc = player::Controller();
    ue3::Object* const pawn = pc != nullptr ? player::BioPawn(pc) : nullptr;
#if defined(MEVR_GAME_LE1)
    return behavior != nullptr && pawn != nullptr && gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) == behavior &&
        ue3::runtime::IsAlive(behavior);
#else
    return behavior != nullptr && pawn != nullptr && WalkOwner(pawn) == behavior && ue3::runtime::IsAlive(behavior);
#endif
}

#if defined(MEVR_GAME_LE1)
struct RampFields {
    float* accel = nullptr;
    float* decel = nullptr;
    float* tacAccel = nullptr;
    float* tacDecel = nullptr;
    float* accelRate = nullptr;
    bool Ok() const { return accel != nullptr && decel != nullptr && tacAccel != nullptr && tacDecel != nullptr && accelRate != nullptr; }
    cs::WalkRamp Read() const { return {*accel, *decel, *tacAccel, *tacDecel, *accelRate}; }
};

RampFields FieldsOf(ue3::Object* behavior, ue3::Object* pawn) {
    RampFields f;
    if (behavior == nullptr || pawn == nullptr) {
        return f;
    }
    f.accel = gAccelRate.In<float>(behavior);
    f.decel = gDecelRate.In<float>(behavior);
    f.tacAccel = gTacAccelRate.In<float>(behavior);
    f.tacDecel = gTacDecelRate.In<float>(behavior);
    f.accelRate = gPawnAccelRate.In<float>(pawn);
    return f;
}
#else
// The sequels' walk has only the pawn's acceleration (engine::adapter::kWalkSpeedRamps).
struct RampFields {
    float* accelRate = nullptr;
    bool Ok() const { return accelRate != nullptr; }
};

RampFields FieldsOf(ue3::Object* /*owner*/, ue3::Object* pawn) {
    RampFields f;
    if (pawn != nullptr) {
        f.accelRate = gPawnAccelRate.In<float>(pawn);
    }
    return f;
}
#endif

void Put(float* field, float value, char const* what) {
    *field = value;
    write_journal::Wrote(what, field, sizeof(float));
}

#if defined(MEVR_GAME_LE1)
// Puts the game's walk back, each value only if it is still ours (the game may have set its own since).
void RestoreRamp(char const* why, bool playerCheck) {
    if (gRamp.behavior == nullptr || !gRamp.written) {
        gRamp = {};
        return;
    }
    bool restored = false;
    if ((!playerCheck || IsPlayersBehavior(gRamp.behavior)) && ue3::runtime::IsAlive(gRamp.behavior) && gRamp.pawn != nullptr &&
        ue3::runtime::IsAlive(gRamp.pawn)) {
        RampFields const f = FieldsOf(gRamp.behavior, gRamp.pawn);
        if (f.Ok()) {
            auto const back = [](float* field, float wrote, float saved) {
                if (*field == wrote) {
                    Put(field, saved, "CombatStance.ramp(restore)");
                }
            };
            back(f.accel, gRamp.wrote.accel, gRamp.saved.accel);
            back(f.decel, gRamp.wrote.decel, gRamp.saved.decel);
            back(f.tacAccel, gRamp.wrote.tacAccel, gRamp.saved.tacAccel);
            back(f.tacDecel, gRamp.wrote.tacDecel, gRamp.saved.tacDecel);
            back(f.accelRate, gRamp.wrote.accelRate, gRamp.saved.accelRate);
            restored = true;
        }
    }
    log::Info("movement: walking ramps as the game has it again ({}){}", why,
        restored ? ": " + RampText(gRamp.saved) : std::string(" (left as it was)"));
    gRamp = {};
}

// The walk's ramps and acceleration (cs::WantedRamp): linear to the stick in VR on foot ([Hands] linearWalk), else combat
// walking ramps like exploring ([Hands] exploreRamp); the game's own values kept and put back.
void UpdateRamp(Link const& link, ue3::Object* pawn, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    ue3::Object* const behavior = pawn != nullptr ? gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    char const* why = !settings.exploreRamp && !settings.linearWalk ? "[Hands] exploreRamp = 0 and linearWalk = 0"
                    : !link.headTrackedView                         ? "no head-tracked view"
                    : !onFoot                                       ? "not on foot"
                    : behavior == nullptr                           ? "no player pawn behaviour"
                                                                    : nullptr;
    gRampWhy = why != nullptr ? why : settings.linearWalk ? "linear to the stick" : "explore ramp";
    if (why != nullptr) {
        RestoreRamp(why, true);
        return;
    }
    if (behavior != gRamp.behavior || pawn != gRamp.pawn) {
        RestoreRamp("the pawn changed", true);
        gRamp.behavior = behavior;
        gRamp.pawn = pawn;
    }
    RampFields const f = FieldsOf(behavior, pawn);
    if (!f.Ok()) {
        gRampWhy = "fields not found";
        return;
    }
    cs::WalkRamp const now = f.Read();
    // The game's values: the first time, and each one the game set (or reset) since our write (UpdateDefaultSpeeds).
    auto const keep = [](float current, float wrote, float& saved) {
        if (!gRamp.written || current != wrote) {
            saved = current;
        }
    };
    keep(now.accel, gRamp.wrote.accel, gRamp.saved.accel);
    keep(now.decel, gRamp.wrote.decel, gRamp.saved.decel);
    keep(now.tacAccel, gRamp.wrote.tacAccel, gRamp.saved.tacAccel);
    keep(now.tacDecel, gRamp.wrote.tacDecel, gRamp.saved.tacDecel);
    keep(now.accelRate, gRamp.wrote.accelRate, gRamp.saved.accelRate);
    cs::WalkRamp const want = cs::WantedRamp(gRamp.saved, settings.exploreRamp, settings.linearWalk, settings.linearWalkS);
    if (!cs::Differs(now, want)) {
        return;
    }
    Put(f.accel, want.accel, "CombatStance.ramp");
    Put(f.decel, want.decel, "CombatStance.ramp");
    Put(f.tacAccel, want.tacAccel, "CombatStance.ramp");
    Put(f.tacDecel, want.tacDecel, "CombatStance.ramp");
    Put(f.accelRate, want.accelRate, "CombatStance.ramp");
    ++gRamp.writes;
    bool const changed = !gRamp.written || cs::Differs(want, gRamp.wrote);
    gRamp.written = true;
    gRamp.wrote = want;
    if (changed) {
        log::Info("movement: walking {}: {} (the game's: {}; written {} time{})",
            settings.linearWalk ? std::format("follows the stick within {:.2f} s", cs::ClampLinearWalkS(settings.linearWalkS))
                                : std::string("ramps like exploring with a weapon out"),
            RampText(want), RampText(gRamp.saved), gRamp.writes, gRamp.writes == 1 ? "" : "s");
    }
}

#else
// The sequels: the pawn's acceleration back, if it is still ours.
void RestoreRamp(char const* why, bool playerCheck) {
    if (gRamp.pawn == nullptr || !gRamp.written) {
        gRamp = {};
        return;
    }
    bool restored = false;
    if ((!playerCheck || IsPlayersBehavior(gRamp.behavior)) && ue3::runtime::IsAlive(gRamp.pawn)) {
        if (float* const rate = gPawnAccelRate.In<float>(gRamp.pawn); rate != nullptr) {
            if (*rate == gRamp.wrote.accelRate) {
                Put(rate, gRamp.saved.accelRate, "CombatStance.accelRate(restore)");
            }
            restored = true;
        }
    }
    log::Info("movement: walking as the game has it again ({}){}", why,
        restored ? std::format(": acceleration {:.0f} u/s^2", gRamp.saved.accelRate) : std::string(" (left as it was)"));
    gRamp = {};
}

// The walk linear to the stick on the sequels ([Hands] linearWalk): the pawn's acceleration takes the fastest run within
// linearWalkS (cs::WantedAccelRate, ME1's rule); the game's value kept and put back.
void UpdateRamp(Link const& link, ue3::Object* pawn, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    ue3::Object* const owner = WalkOwner(pawn);
    char const* why = !settings.linearWalk  ? "[Hands] linearWalk = 0"
                    : !link.headTrackedView ? "no head-tracked view"
                    : !onFoot               ? "not on foot"
                    : owner == nullptr      ? "no player pawn"
                                            : nullptr;
    gRampWhy = why != nullptr ? why : "linear to the stick (the pawn's acceleration: this game has no speed ramps)";
    if (why != nullptr) {
        RestoreRamp(why, true);
        return;
    }
    if (owner != gRamp.behavior || pawn != gRamp.pawn) {
        RestoreRamp("the pawn changed", true);
        gRamp.behavior = owner;
        gRamp.pawn = pawn;
    }
    RampFields const f = FieldsOf(owner, pawn);
    if (!f.Ok()) {
        gRampWhy = "fields not found";
        return;
    }
    float const now = *f.accelRate;
    if (!gRamp.written || now != gRamp.wrote.accelRate) {
        gRamp.saved.accelRate = now;  // the game's: the first time, and whenever it set its own since our write
    }
    float const want = cs::WantedAccelRate(gRamp.saved.accelRate, true, settings.linearWalkS);
    if (std::fabs(now - want) <= 1e-4f * std::max(1.0f, std::fabs(want))) {
        return;
    }
    Put(f.accelRate, want, "CombatStance.accelRate");
    ++gRamp.writes;
    bool const changed = !gRamp.written || gRamp.wrote.accelRate != want;
    gRamp.written = true;
    gRamp.wrote.accelRate = want;
    if (changed) {
        log::Info("movement: walking follows the stick within {:.2f} s: acceleration {:.0f} u/s^2 (the game's: {:.0f}; this game has no "
                  "speed ramps; written {} time{})",
            cs::ClampLinearWalkS(settings.linearWalkS), want, gRamp.saved.accelRate, gRamp.writes, gRamp.writes == 1 ? "" : "s");
    }
}
#endif

// The walk's response, measured (research/38 "Linear walking"): from the left stick pushed to 0.3 or more (from under 0.2) or let
// go (while moving), the pawn's speed then and 0.1, 0.2, 0.4 and 0.8 s later, logged once the last is in.
struct Response {
    bool open = false;
    bool start = false;
    double since = 0.0;
    double stick = 0.0;
    double lastStick = 0.0;
    double yawFrom = 0.0;  // the pawn's facing when it began (degrees): how far it turned meanwhile
    std::array<double, 5> speeds{};
    std::size_t next = 0;
    std::array<int, 3> steps{};  // ticks of it with the game's start, stop and skid steps playing
    double fraction = -1.0;      // the speed from the push at its start (-1: the game's)
};
Response gResponse;
constexpr std::array<double, 5> kResponseAt{0.0, 0.1, 0.2, 0.4, 0.8};

void StepResponse(double speed, double yawDeg, ue3::Object* behavior) {
    vr::controllers::State const controllers = vr::controllers::Latest();
    double const stick = controllers.active ? std::hypot(controllers.left.stickX, controllers.left.stickY) : 0.0;
    Response& r = gResponse;
    if (!r.open) {
        bool const start = r.lastStick < 0.2 && stick >= 0.3;
        bool const stop = r.lastStick >= 0.3 && stick < 0.2 && speed > 20.0;
        if (start || stop) {
            r.open = true;
            r.start = start;
            r.since = gNow;
            r.stick = start ? stick : r.lastStick;
            r.next = 0;
            r.yawFrom = yawDeg;
            r.steps = {};
            r.fraction = -1.0;
        }
    }
    if (r.open) {
        if (behavior != nullptr) {
            r.steps[0] += gAnimStartState.Read<std::uint8_t>(behavior).value_or(0) != 0 ? 1 : 0;
            r.steps[1] += gAnimStopState.Read<std::uint8_t>(behavior).value_or(0) != 0 ? 1 : 0;
            r.steps[2] += gAnimSkidState.Read<std::uint8_t>(behavior).value_or(0) != 0 ? 1 : 0;
        }
        if (r.fraction < 0.0 && gStick.fraction >= 0.0) {
            r.fraction = gStick.fraction;
        }
        while (r.next < kResponseAt.size() && gNow - r.since >= kResponseAt[r.next] - 1e-6) {
            r.speeds[r.next++] = speed;
        }
        if (r.next == kResponseAt.size()) {
            r.open = false;
            double const turned = std::fabs(std::remainder(yawDeg - r.yawFrom, 360.0));
            log::Info("movement: response: the stick {} ({:.2f}): speed {:.0f}, {:.0f}, {:.0f}, {:.0f}, {:.0f} u/s at 0, 0.1, 0.2, 0.4, 0.8 s; the pawn "
                      "turned {:.0f} deg meanwhile ({}; the game's start/stop/skid steps {}/{}/{} ticks; {})",
                r.start ? "pushed" : "let go", r.stick, r.speeds[0], r.speeds[1], r.speeds[2], r.speeds[3], r.speeds[4], turned, gRampWhy,
                r.steps[0], r.steps[1], r.steps[2],
                r.fraction >= 0.0 ? std::format("the push set {:.0f}% of the run", r.fraction * 100.0) : std::string("the game's speed"));
        }
    }
    r.lastStick = stick;
}

#if !defined(MEVR_GAME_LE1)
// The response's first ticks on the sequels, one line per push and per let-go (research/38 "ME3's exploring start and stop"):
// what the pawn's walk had each tick (its speed and acceleration with their directions against the pawn's facing, the facing it
// turns to, the fraction of the run, ME3's m_fDesiredMaxSpeed, the mesh's root motion mode and ME3's animation nodes' root motion
// requests): where a slow start or a slide after a stop comes from.
ue3::FieldRef gTraceDesiredRotation{"Engine.Pawn", "DesiredRotation", ue3::kEveryGame};
ue3::FieldRef gTraceAcceleration{"Engine.Actor", "Acceleration", ue3::kEveryGame};
ue3::FieldRef gTraceMoveMag{"SFXGame.BioPawn", "fMoveMag", ue3::kEveryGame};
ue3::FieldRef gTraceRootMotionMode{"Engine.SkeletalMeshComponent", "RootMotionMode", ue3::kEveryGame};
#if defined(MEVR_GAME_LE3)
ue3::FieldRef gTraceDesiredMaxSpeed{"SFXGame.BioPawn", "m_fDesiredMaxSpeed", ue3::kEveryGame};
// ME3's animation nodes ask the pawn for a root motion mode while they play (BioPawn.RootMotionOverride): the entries.
ue3::FieldRef gTraceOverrides{"SFXGame.BioPawn", "RootMotionOverrides", ue3::kEveryGame};
ue3::FieldRef gTraceOverrideNode{gTraceOverrides, "Node"};
ue3::FieldRef gTraceOverrideMode{gTraceOverrides, "RMMode"};
ue3::FieldRef gTraceNodeName{"Engine.AnimNode", "NodeName", ue3::kEveryGame};
std::string gStartTraceNodes;
#endif
constexpr double kStartTraceSeconds = 0.8;
constexpr std::size_t kStartTraceTicks = 64;
std::string gStartTrace;
std::size_t gStartTraceTicks = 0;
struct TraceWindow {
    bool open = false;
    bool start = false;  // a push (else a let-go)
    double since = 0.0;
    double lastStick = 0.0;
};
TraceWindow gTraceWindow;

void StepStartTrace(ue3::Object* pawn, double yawDeg) {
    // Its own window: from a push (the stick from under 0.2 to 0.3 or more) or a let-go (back under 0.2), whatever the speed.
    vr::controllers::State const controllers = vr::controllers::Latest();
    double const stick = controllers.active ? std::hypot(controllers.left.stickX, controllers.left.stickY) : 0.0;
    TraceWindow& w = gTraceWindow;
    bool const pushed = w.lastStick < 0.2 && stick >= 0.3;
    bool const letGo = w.lastStick >= 0.3 && stick < 0.2;
    w.lastStick = stick;
    if (w.open && (pushed || letGo || gNow - w.since > kStartTraceSeconds)) {
        w.open = false;
        log::Info("movement: response ticks ({}; t s: speed u/s @ its direction from the facing, acceleration u/s^2 @ its direction, the "
                  "facing wanted, fMoveMag, m_fDesiredMaxSpeed, root motion mode, the nodes' root motion requests):{}",
            w.start ? "the stick pushed" : "the stick let go", gStartTrace);
        gStartTrace.clear();
        gStartTraceTicks = 0;
#if defined(MEVR_GAME_LE3)
        if (!gStartTraceNodes.empty()) {
            log::Info("movement: response: the animation nodes that asked for root motion:{}", gStartTraceNodes);
            gStartTraceNodes.clear();
        }
#endif
    }
    if (pushed || letGo) {
        w.open = true;
        w.start = pushed;
        w.since = gNow;
    }
    if (!w.open || gStartTraceTicks >= kStartTraceTicks) {
        return;
    }
    auto const relative = [yawDeg](double yaw) { return std::remainder(yaw - yawDeg, 360.0); };
    auto const direction = [&relative](std::optional<ue3::Vector> const& v, double size) {
        return v && size > 1.0 ? relative(std::atan2(v->y, v->x) * 180.0 / 3.14159265358979) : 0.0;
    };
    std::optional<ue3::Vector> const velocity = gVelocity.Read<ue3::Vector>(pawn);
    std::optional<ue3::Vector> const accel = gTraceAcceleration.Read<ue3::Vector>(pawn);
    double const accelSize = accel ? std::hypot(static_cast<double>(accel->x), static_cast<double>(accel->y)) : -1.0;
    std::optional<ue3::Rotator> const wanted = gTraceDesiredRotation.Read<ue3::Rotator>(pawn);
    double const wantedYaw = wanted ? relative(static_cast<double>(wanted->yaw) * 360.0 / 65536.0) : 0.0;
    float const moveMag = gTraceMoveMag.Read<float>(pawn).value_or(-1.0f);
#if defined(MEVR_GAME_LE3)
    float const desiredMax = gTraceDesiredMaxSpeed.Read<float>(pawn).value_or(-1.0f);
#else
    float const desiredMax = -1.0f;
#endif
    ue3::Object* const mesh = player::Mesh(pawn);
    int const rmm = mesh != nullptr ? gTraceRootMotionMode.Read<std::uint8_t>(mesh).value_or(255) : 255;
    std::string overrides;
#if defined(MEVR_GAME_LE3)
    if (std::optional<ue3::Elements> const entries = gTraceOverrides.ElementsOf(pawn, 64)) {
        ue3::Reflection& reflection = ue3::runtime::Get();
        for (std::int32_t i = 0; i < entries->count; ++i) {
            void* const entry = entries->At(i);
            ue3::Object* const node = gTraceOverrideNode.Read<ue3::Object*>(entry).value_or(nullptr);
            int const mode = gTraceOverrideMode.Read<std::uint8_t>(entry).value_or(255);
            overrides += std::format("{}{}", overrides.empty() ? "" : "/", mode);
            if (node != nullptr && ue3::runtime::IsAlive(node)) {
                std::optional<ue3::Name> const name = gTraceNodeName.Read<ue3::Name>(node);
                std::string const text = std::format(" {} ({}, mode {})", reflection.FullName(node),
                    name ? reflection.ToString(*name) : std::string("?"), mode);
                if (gStartTraceNodes.find(text) == std::string::npos) {
                    gStartTraceNodes += text;
                }
            }
        }
    }
#endif
    gStartTrace += std::format(" | {:.2f}: {:.0f} @{:+.0f}, {:.0f} @{:+.0f}, {:+.0f}, {:.2f}, {:.2f}, {}, [{}]", gNow - w.since, gSpeed,
        direction(velocity, gSpeed), accelSize, direction(accel, accelSize), wantedYaw, moveMag, desiredMax, rmm, overrides);
    ++gStartTraceTicks;
}
#endif

// The walking mode and speed, once per change (research/38 C: the zoomed walk).
void UpdateWalk(ue3::Object* pawn, Link const& link) {
    int const mode = CallByte(pawn, gGetWalkingSpeedMode);
    std::optional<ue3::Vector> const velocity = gVelocity.Read<ue3::Vector>(pawn);
    gSpeed = velocity ? std::sqrt(static_cast<double>(velocity->x) * velocity->x + static_cast<double>(velocity->y) * velocity->y) : 0.0;
    std::optional<ue3::Rotator> const rotation = player::Rotation(pawn);
#if defined(MEVR_GAME_LE1)
    StepResponse(gSpeed, rotation ? static_cast<double>(rotation->yaw) * 360.0 / 65536.0 : 0.0,
        gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr));
#else
    StepResponse(gSpeed, rotation ? static_cast<double>(rotation->yaw) * 360.0 / 65536.0 : 0.0, WalkOwner(pawn));
    StepStartTrace(pawn, rotation ? static_cast<double>(rotation->yaw) * 360.0 / 65536.0 : 0.0);
#endif
    if (mode == gWalkMode) {
        gWalkTop = std::max(gWalkTop, gSpeed);
        return;
    }
    if (gNow - gLastMoveLog < kMoveLogSeconds && gWalkMode >= 0) {
        return;  // flapping: the next tick tries again
    }
    auto const name = [](int m) { return m >= 0 && m < static_cast<int>(std::size(kWalkModes)) ? kWalkModes[m] : "unknown"; };
#if defined(MEVR_GAME_LE1)
    log::Info("movement: walking mode {} ({}{}), speed {:.0f} u/s{}", name(mode), gDrawn ? "drawn" : "holstered",
        link.supportHeld ? ", the support grip holds the gun (LT)" : "", gSpeed,
        gWalkMode >= 0 ? std::format("; {} held {:.1f} s, top speed {:.0f} u/s", name(gWalkMode), gNow - gWalkModeSince, gWalkTop)
                       : std::string());
#else
    log::Info("movement: walking mode {} ({}), speed {:.0f} u/s{}", name(mode), mode >= 4 ? "weapon out" : "exploring", gSpeed,
        gWalkMode >= 0 ? std::format("; {} held {:.1f} s, top speed {:.0f} u/s", name(gWalkMode), gNow - gWalkModeSince, gWalkTop)
                       : std::string());
    (void)link;
#endif
    gLastMoveLog = gNow;
    gWalkMode = mode;
    gWalkModeSince = gNow;
    gWalkTop = gSpeed;
}

#endif  // every game (C: the ramp and the response)
#if defined(MEVR_GAME_LE1)

// ---- C: the speed from the push; the start and stop steps (research/38 "Linear walking", 2026-11-13) --------------

// The game's own speed from the stick is a crawl under 0.65 of MoveMag and a full run past 0.70
// (BioPlayerController.PlayerWalking.PlayerMoveExplore/PlayerMoveCombat); linear ([Hands] linearStick), each tick one of them
// ran, the PlayerTick that ran it is followed by BioPawn.SetDesiredSpeed(LinearSpeed(MoveMag)) on the player's pawn (the
// target its ramp moves toward). MoveMag is the game's own: |RawJoyUp, RawJoyRight| of the player's input (its
// ComputeMovementStickAngle, native). The game's own cases stand: no push, ignored input, a walk toggle, a crouch (exploring)
// or cover (combat), the sprint (PlayerMoveStorming).
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput"};
ue3::FieldRef gRawJoyUp{"Engine.PlayerInput", "RawJoyUp"};
ue3::FieldRef gRawJoyRight{"Engine.PlayerInput", "RawJoyRight"};
ue3::FieldRef gIgnoreMoveInput{"Engine.PlayerController", "bIgnoreMoveInput"};
ue3::FieldRef gIdleThreshold{"SFXGame.BioPlayerController", "MoveStickIdleThreshold"};
ue3::FieldRef gWalkThreshold{"SFXGame.BioPlayerController", "MoveStickWalkThreshold"};
ue3::FieldRef gRunThreshold{"SFXGame.BioPlayerController", "MoveStickRunThreshold"};
ue3::FieldRef gIsWalking{"Engine.Pawn", "bIsWalking"};
ue3::FieldRef gAcceleration{"Engine.Actor", "Acceleration"};
ue3::FieldRef gDesiredSpeed{"Engine.Pawn", "DesiredSpeed"};
ue3::FieldRef gDesiredMaxSpeed{"SFXGame.BioPawnBehavior", "m_fDesiredMaxSpeed"};
ue3::FunctionRef gSetDesiredSpeedFn{"SFXGame.BioPawn.SetDesiredSpeed"};
// The start, stop and skid steps: BioPawn's move control (native TickMoveControl) plays a start animation from a standstill
// (CanAnimMoveStart: m_bEnableStartAnim, then m_eAnimStartState) during which Shepard doesn't move for about 0.15 s, a stop
// animation that slides him to a stop at a rate fitted to it (m_bEnableStopAnim, m_fMoveStopDSDecelRate) and a skid on a sharp
// turn (m_bEnableSkidAnim). Off in VR on foot ([Hands] startStopAnims = 0): the flags cleared on the player's behaviour, the
// game's values kept and put back.
ue3::FieldRef gEnableStartAnim{"SFXGame.BioPawnBehavior", "m_bEnableStartAnim"};
ue3::FieldRef gEnableStopAnim{"SFXGame.BioPawnBehavior", "m_bEnableStopAnim"};
ue3::FieldRef gEnableSkidAnim{"SFXGame.BioPawnBehavior", "m_bEnableSkidAnim"};

std::vector<ue3::Function*> gMoveExplore;  // BioPlayerController's PlayerMoveExplore (each state's)
std::vector<ue3::Function*> gMoveCombat;
std::vector<ue3::Function*> gStickTicks;   // PlayerTick on the player controllers
ue3::Function* gSetDesiredSpeed = nullptr;

bool Listed(std::vector<ue3::Function*> const& list, ue3::Function* f) {
    return std::find(list.begin(), list.end(), f) != list.end();
}

void OnMoveCall(ue3::Object* context, ue3::Function* function, void* /*result*/) {
    if (!gStick.wanted || context == nullptr || context != ue3::runtime::LocalPlayerController()) {
        return;
    }
    gStick.ran = Listed(gMoveCombat, function) ? MoveRan::Combat : MoveRan::Explore;
}

void ApplyStick(ue3::Object* controller, MoveRan ran) {
    ue3::Object* const pawn = player::BioPawn(controller);
    ue3::Object* const input = gPlayerInput.Read<ue3::Object*>(controller).value_or(nullptr);
    ue3::Object* const behavior = pawn != nullptr ? gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    if (pawn == nullptr || input == nullptr || behavior == nullptr || gSetDesiredSpeed == nullptr) {
        return;
    }
    ++gStick.moves;
    double const up = gRawJoyUp.Read<float>(input).value_or(0.0f);
    double const right = gRawJoyRight.Read<float>(input).value_or(0.0f);
    gStick.moveMag = std::hypot(up, right);
    std::optional<float> const idle = gIdleThreshold.Read<float>(controller);
    if (!gStick.loggedThresholds) {
        gStick.loggedThresholds = true;
        log::Info("movement: the game's stick thresholds: idle {:.2f}, walk {:.2f}, run {:.2f} (of MoveMag, the push past its dead zone)",
            idle.value_or(-1.0f), gWalkThreshold.Read<float>(controller).value_or(-1.0f), gRunThreshold.Read<float>(controller).value_or(-1.0f));
    }
    std::optional<ue3::Vector> const accel = gAcceleration.Read<ue3::Vector>(pawn);
    bool const accelerating = accel && (std::fabs(accel->x) + std::fabs(accel->y)) > 1.0f;
    bool const keepGame = !accelerating || gIgnoreMoveInput.Read<std::uint8_t>(controller).value_or(1) != 0 ||
        gIsWalking.ReadBool(pawn).value_or(true) || (ran == MoveRan::Explore && gIsCrouched.ReadBool(pawn).value_or(true)) ||
        (ran == MoveRan::Combat && (gCoverType.Read<std::uint8_t>(pawn).value_or(1) != 0 || gCoverAction.Read<std::uint8_t>(pawn).value_or(1) != 0));
    std::optional<double> const speed = keepGame ? std::nullopt : cs::LinearSpeed(gStick.moveMag, idle.value_or(0.0f), gStick.fullAt);
    if (!speed) {
        ++gStick.kept;
        gStick.fraction = -1.0;
        return;
    }
    gStick.gameTarget = gDesiredMaxSpeed.Read<float>(behavior).value_or(-1.0f);
    ue3::Params parms(ue3::runtime::Get(), gSetDesiredSpeed);
    parms.Set("fDesiredSpeed", static_cast<float>(*speed));
    parms.SetBool("bForce", false);
    if (!ue3::runtime::Call(pawn, gSetDesiredSpeed, parms)) {
        return;
    }
    ++gStick.applied;
    gStick.fraction = *speed;
    if (!gStick.loggedFirst) {
        gStick.loggedFirst = true;
        log::Info("movement: the speed follows the push: MoveMag {:.2f} -> {:.0f}% of the run ({}; the game's own target was {:.2f})",
            gStick.moveMag, *speed * 100.0, ran == MoveRan::Combat ? "weapon drawn" : "exploring", gStick.gameTarget);
    }
}

void OnStickTick(ue3::Object* context, ue3::Function* /*function*/, void* /*parms*/, void* /*result*/) {
    MoveRan const ran = gStick.ran;
    gStick.ran = MoveRan::None;
    if (ran == MoveRan::None || !gStick.wanted || context == nullptr || context != ue3::runtime::LocalPlayerController()) {
        return;
    }
    ApplyStick(context, ran);
}

void UpdateStick(Link const& link, ue3::Object* pawn, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    char const* const why = !settings.linearStick  ? "[Hands] linearStick = 0"
                          : gMoveExplore.empty() || gStickTicks.empty() || gSetDesiredSpeed == nullptr ? "the game's move functions not found"
                          : !engine::call_function::Installed() ? "no CallFunction hook"
                          : !link.headTrackedView ? "no head-tracked view"
                          : !onFoot ? "not on foot"
                          : pawn == nullptr ? "no player pawn"
                                            : nullptr;
    bool const wanted = why == nullptr;
    gStick.fullAt = cs::ClampLinearFullAt(settings.linearFullAt);
    std::string const now = why != nullptr ? std::string(why) : std::format("in proportion to the push, the run at {:.2f}", gStick.fullAt);
    if (wanted != gStick.wanted || now != gStick.why) {
        log::Info("movement: the walk's speed {}", wanted ? now : "as the game has it (" + now + ")");
    }
    gStick.wanted = wanted;
    gStick.why = now;
    if (!wanted) {
        gStick.ran = MoveRan::None;
    }
}

#endif  // MEVR_GAME_LE1 (the speed from the push)
#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)

// ---- C: the speed from the push on the sequels (the audits' MOV-15; research/38 "The speed from the push on the sequels") --

// ME1's rule (cs::LinearSpeed, [Hands] linearStick and linearFullAt) on each sequel's own output. Their move functions (ME2
// BioPlayerController.PlayerWalking.PlayerMoveCombatOrExplore; ME3 PlayerMoveExplore and PlayerMoveCombat, as ME1's; each
// called by PlayerMove from script) end by writing the run's fraction into BioPawn.fMoveMag (the game's own rule ME1's: a crawl
// under MoveStickWalkThreshold, the walk at it, the run past MoveStickRunThreshold; no SetDesiredSpeed), the fraction of the
// pawn's top speed it walks at. A CallFunction post on each writes LinearSpeed(MoveMag) there (journalled; the game writes its
// own again every tick) unless the game's own case stands: no push, ignored input (no acceleration, or bIgnoreMoveInput), the
// walk toggle, a crouch, cover, the sprint (ME2's function runs the storm too; ME3's is PlayerMoveStorming, not hooked). MoveMag
// is ME1's |RawJoyUp, RawJoyRight| (ComputeMovementStickAngle's: the stick past its dead zone).
ue3::FieldRef gPlayerInput{"Engine.PlayerController", "PlayerInput", ue3::kEveryGame};
ue3::FieldRef gRawJoyUp{"Engine.PlayerInput", "RawJoyUp", ue3::kEveryGame};
ue3::FieldRef gRawJoyRight{"Engine.PlayerInput", "RawJoyRight", ue3::kEveryGame};
ue3::FieldRef gIgnoreMoveInput{"Engine.PlayerController", "bIgnoreMoveInput", ue3::kEveryGame};
ue3::FieldRef gWalkThreshold{"SFXGame.BioPlayerController", "MoveStickWalkThreshold", ue3::kEveryGame};
ue3::FieldRef gRunThreshold{"SFXGame.BioPlayerController", "MoveStickRunThreshold", ue3::kEveryGame};
ue3::FieldRef gIsWalking{"Engine.Pawn", "bIsWalking", ue3::kEveryGame};
ue3::FieldRef gIsCrouched{"Engine.Pawn", "bIsCrouched", ue3::kEveryGame};
ue3::FieldRef gAcceleration{"Engine.Actor", "Acceleration", ue3::kEveryGame};
ue3::FieldRef gCoverType{"SFXGame.BioPawn", "CoverType", ue3::kEveryGame};
ue3::FieldRef gCoverAction{"SFXGame.BioPawn", "CoverAction", ue3::kEveryGame};
ue3::FieldRef gMoveMagField{"SFXGame.BioPawn", "fMoveMag", ue3::kEveryGame};
#if defined(MEVR_GAME_LE2)
ue3::FieldRef gStormingSequel{"SFXGame.BioPlayerController", "bStorming", ue3::kEveryGame};  // PlayerMove passes it (bIsStorming)
constexpr float kGameIdle = 0.01f;  // PlayerMoveCombatOrExplore's MoveMagIdle (a local constant; ME2 has no idle field)
constexpr char const* kMoveFunctions[] = {"PlayerMoveCombatOrExplore"};
#else
ue3::FieldRef gIdleThreshold{"SFXGame.BioPlayerController", "MoveStickIdleThreshold", ue3::kEveryGame};
constexpr char const* kMoveFunctions[] = {"PlayerMoveExplore", "PlayerMoveCombat"};
#endif
std::vector<ue3::Function*> gMoveFunctions;  // the BioPlayerController's (each state's)

void OnSequelMove(ue3::Object* context, ue3::Function* /*function*/, void* /*result*/) {
    if ((!gStick.wanted && !gStop.wanted) || context == nullptr || context != ue3::runtime::LocalPlayerController()) {
        return;
    }
    ue3::Object* const pawn = player::BioPawn(context);
    ue3::Object* const input = gPlayerInput.Read<ue3::Object*>(context).value_or(nullptr);
    float* const fraction = pawn != nullptr ? gMoveMagField.In<float>(pawn) : nullptr;
    if (pawn == nullptr || input == nullptr || fraction == nullptr) {
        return;
    }
    ++gStick.moves;
    double const up = gRawJoyUp.Read<float>(input).value_or(0.0f);
    double const right = gRawJoyRight.Read<float>(input).value_or(0.0f);
    gStick.moveMag = std::hypot(up, right);
#if defined(MEVR_GAME_LE2)
    float const idle = kGameIdle;
#else
    float const idle = gIdleThreshold.Read<float>(context).value_or(0.01f);
#endif
    if (!gStick.loggedThresholds) {
        gStick.loggedThresholds = true;
        log::Info("movement: the game's stick thresholds: idle {:.2f}, walk {:.2f}, run {:.2f} (of MoveMag, the push past its dead zone)", idle,
            gWalkThreshold.Read<float>(context).value_or(-1.0f), gRunThreshold.Read<float>(context).value_or(-1.0f));
    }
    std::optional<ue3::Vector> const accel = gAcceleration.Read<ue3::Vector>(pawn);
    bool const accelerating = accel && (std::fabs(accel->x) + std::fabs(accel->y)) > 1.0f;
    bool const ignored = gIgnoreMoveInput.Read<std::uint8_t>(context).value_or(1) != 0;
    // The let-go (ME1's linear walk stops at once, research/38 "Linear walking"; item "ME2's quick stop"): exploring, both
    // sequels' move functions blend a lower fraction in over MoveWalkModifierBlendTime (0.5 s), so with the stick let go the
    // pawn kept walking (ME2: 374 -> 158 u/s in 0.1 s, about 60 at 0.8 s). The blend's target is 0 there: written at once.
    // Only while the game is blending (0 < fMoveMag < 1: ignored input writes 1, the scripts' own walks with it).
    if (gStop.wanted && !accelerating && !ignored && gStick.moveMag <= idle && *fraction > 0.0f && *fraction < 1.0f) {
        float const was = *fraction;
        *fraction = 0.0f;
        write_journal::Wrote("CombatStance.fMoveMag(stop)", fraction, sizeof(float));
        ++gStop.stops;
        if (!gStop.logged) {
            gStop.logged = true;
            log::Info("movement: the stick let go: the walk stops at once (BioPawn.fMoveMag {:.2f} -> 0; the game blends it down over 0.5 s "
                      "exploring)", was);
        }
    }
    if (!gStick.wanted) {
        return;
    }
    bool const keepGame = !accelerating || ignored ||
        gIsWalking.ReadBool(pawn).value_or(true) || gIsCrouched.ReadBool(pawn).value_or(true) ||
        gCoverType.Read<std::uint8_t>(pawn).value_or(1) != 0 || gCoverAction.Read<std::uint8_t>(pawn).value_or(1) != 0
#if defined(MEVR_GAME_LE2)
        || gStormingSequel.ReadBool(context).value_or(true)
#endif
        ;
    std::optional<double> const speed = keepGame ? std::nullopt : cs::LinearSpeed(gStick.moveMag, idle, gStick.fullAt);
    if (!speed) {
        ++gStick.kept;
        gStick.fraction = -1.0;
        return;
    }
    gStick.gameTarget = *fraction;  // the game's own fraction, just written
    float const want = static_cast<float>(*speed);
    if (*fraction != want) {
        *fraction = want;
        write_journal::Wrote("CombatStance.fMoveMag", fraction, sizeof(float));
    }
    ++gStick.applied;
    gStick.fraction = *speed;
    if (!gStick.loggedFirst) {
        gStick.loggedFirst = true;
        log::Info("movement: the speed follows the push: MoveMag {:.2f} -> {:.0f}% of the run (BioPawn.fMoveMag; the game's own was {:.2f})",
            gStick.moveMag, *speed * 100.0, gStick.gameTarget);
    }
}

void UpdateStick(Link const& link, ue3::Object* pawn, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    char const* const why = !settings.linearStick                  ? "[Hands] linearStick = 0"
                          : gMoveFunctions.empty()                  ? "the game's move functions not found"
                          : !engine::call_function::Installed()     ? "no CallFunction hook"
                          : !link.headTrackedView                   ? "no head-tracked view"
                          : !onFoot                                 ? "not on foot"
                          : pawn == nullptr                         ? "no player pawn"
                                                                    : nullptr;
    bool const wanted = why == nullptr;
    gStick.fullAt = cs::ClampLinearFullAt(settings.linearFullAt);
    std::string const now = why != nullptr ? std::string(why) : std::format("in proportion to the push, the run at {:.2f}", gStick.fullAt);
    if (wanted != gStick.wanted || now != gStick.why) {
        log::Info("movement: the walk's speed {}", wanted ? now : "as the game has it (" + now + ")");
    }
    gStick.wanted = wanted;
    gStick.why = now;
    bool const stop = settings.linearWalk && !gMoveFunctions.empty() && engine::call_function::Installed() && link.headTrackedView && onFoot &&
        pawn != nullptr;
    if (stop != gStop.wanted) {
        log::Info("movement: the let-go {}", stop ? "stops the walk at once ([Hands] linearWalk)" : "is the game's (its 0.5 s blend exploring)");
    }
    gStop.wanted = stop;
}

// `mevr animtree [text]`: the player's AnimTree, node by node (class, NodeName, children's names; a sequence's AnimSeqName,
// Rate and whether it plays), to the log; only nodes whose line holds `text` (any case). How a sequel's draw and holster nodes
// were found (research/38 D on the sequels). Game thread.
ue3::FieldRef gTreeAnimations{"Engine.SkeletalMeshComponent", "Animations", ue3::kEveryGame};
ue3::FieldRef gTreeNodeName{"Engine.AnimNode", "NodeName", ue3::kEveryGame};
ue3::FieldRef gTreeChildren{"Engine.AnimNodeBlendBase", "Children", ue3::kEveryGame};
ue3::FieldRef gTreeChildName{gTreeChildren, "Name"};
ue3::FieldRef gTreeChildAnim{gTreeChildren, "Anim"};
ue3::FieldRef gTreeSeqName{"Engine.AnimNodeSequence", "AnimSeqName", ue3::kEveryGame};
ue3::FieldRef gTreeSeqRate{"Engine.AnimNodeSequence", "Rate", ue3::kEveryGame};
ue3::FieldRef gTreeSeqPlaying{"Engine.AnimNodeSequence", "bPlaying", ue3::kEveryGame};
ue3::ClassRef gTreeBlendBase{"Engine.AnimNodeBlendBase", ue3::kEveryGame};
ue3::ClassRef gTreeSequence{"Engine.AnimNodeSequence", ue3::kEveryGame};

std::string LowerText(std::string_view text) {
    std::string out;
    for (char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

std::string TreeNodeName(ue3::Object const* node) {
    std::optional<ue3::Name> const name = gTreeNodeName.Read<ue3::Name>(node);
    return name ? ue3::runtime::NameText(*name) : std::string();
}

// ---- D: the draw rate on the sequels (research/38 "The draw rate on the sequels") -------------------------------------

// ME1's rule ([Hands] drawRate, holsterRate; cs::ClampDrawRate, RateNeedsWrite): while the player's draw or holster sequence
// plays in VR on foot it plays at that rate (every notify still fires: the weapon's EquipFinished ends the draw), its own rate
// back once it stops. ME2 plays them as ME1 does, in BioAnimNodeBlendByWeaponEquip nodes' Draw and Holster children (four:
// standing, crouching, high and low cover); ME3 in AnimNodeSlot channels. So the sequences are found by name
// (engine::adapter::kDrawSequencePrefix, kHolsterSequencePrefix), every slot channel's read again each tick (a channel plays one
// animation, then another).
ue3::ClassRef gTreeSlot{"Engine.AnimNodeSlot", ue3::kEveryGame};
std::vector<ue3::Object*> gSlotSequences;  // every AnimNodeSequence in a slot's channel
void const* gSlotTree = nullptr;
double gSlotWalked = -1e9;
std::string gDrawLogged;  // the sequences whose first write was logged (once per tree)

void WalkSlots(ue3::Object* node, std::vector<ue3::Object const*>& visited) {
    if (node == nullptr || visited.size() >= static_cast<std::size_t>(kMaxNodes) ||
        std::find(visited.begin(), visited.end(), node) != visited.end()) {
        return;
    }
    visited.push_back(node);
    if (!ue3::IsA(node, gTreeBlendBase)) {
        return;
    }
    std::optional<ue3::Elements> const children = gTreeChildren.ElementsOf(node, kMaxChildren);
    if (!children) {
        return;
    }
    bool const slot = ue3::IsA(node, gTreeSlot);
    for (std::int32_t i = 0; i < children->count; ++i) {
        ue3::Object* const child = gTreeChildAnim.Read<ue3::Object*>(children->At(i)).value_or(nullptr);
        bool take = false;
        if (child != nullptr && ue3::IsA(child, gTreeSequence)) {
            // ME3: channel i of a slot (0 is its Source), whatever it plays now; ME2: a sequence that is a draw or a holster (its
            // BioAnimNodeBlendByWeaponEquip nodes' Draw and Holster children, ME1's WeaponEquip node's form).
            std::optional<ue3::Name> const seqName = gTreeSeqName.Read<ue3::Name>(child);
            std::string const name = seqName ? ue3::runtime::NameText(*seqName) : std::string();
            take = (slot && i > 0) || name.starts_with(engine::adapter::kDrawSequencePrefix) ||
                name.starts_with(engine::adapter::kHolsterSequencePrefix);
        }
        if (take && std::find(gSlotSequences.begin(), gSlotSequences.end(), child) == gSlotSequences.end()) {
            gSlotSequences.push_back(child);
        }
        WalkSlots(child, visited);
    }
}

void RestoreSequelRate(RateWrite const& w) {
    float* const rate = gTreeSeqRate.In<float>(w.sequence);
    if (rate != nullptr && *rate == w.wrote) {
        *rate = w.original;
        write_journal::Wrote("CombatStance.drawRate(restore)", rate, sizeof(float));
    }
}

void UpdateSequelRates(bool wanted, double drawRate, double holsterRate, ue3::Object* mesh) {
    ue3::Object* const root = mesh != nullptr ? gTreeAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) : nullptr;
    if (root != gSlotTree) {
        gRates.clear();  // a tree going away: dropped, not restored (its nodes may be gone)
        gSlotSequences.clear();
        gSlotTree = root;
        gSlotWalked = -1e9;
        gDrawLogged.clear();
    }
    if (root != nullptr && gNow - gSlotWalked >= kRewalkSeconds) {
        std::vector<ue3::Object*> before = gSlotSequences;
        gSlotSequences.clear();
        std::vector<ue3::Object const*> visited;
        visited.reserve(512);
        WalkSlots(root, visited);
        gSlotWalked = gNow;
        if (gSlotSequences.size() != before.size()) {
            log::Info("draw: the player's AnimTree ({} nodes): {} sequences where the draw ({}*) and the holster ({}*) play (slot "
                      "channels, the WeaponEquip nodes' Draw and Holster)", visited.size(), gSlotSequences.size(), engine::adapter::kDrawSequencePrefix,
                engine::adapter::kHolsterSequencePrefix);
        }
    }
    for (ue3::Object* const seq : gSlotSequences) {
        auto const written = std::find_if(gRates.begin(), gRates.end(), [seq](RateWrite const& w) { return w.sequence == seq; });
        bool const playing = gTreeSeqPlaying.ReadBool(seq).value_or(false);
        float* const rate = gTreeSeqRate.In<float>(seq);
        double want = 0.0;
        std::string name;
        if (playing && rate != nullptr) {
            std::optional<ue3::Name> const seqName = gTreeSeqName.Read<ue3::Name>(seq);
            name = seqName ? ue3::runtime::NameText(*seqName) : std::string();
            want = name.starts_with(engine::adapter::kDrawSequencePrefix)      ? drawRate
                 : name.starts_with(engine::adapter::kHolsterSequencePrefix) ? holsterRate
                                                                              : 0.0;
        }
        if (wanted && want > 1.0005 && cs::RateNeedsWrite(*rate, want)) {
            float const original = written != gRates.end() && *rate == written->wrote ? written->original : *rate;
            *rate = static_cast<float>(want);
            write_journal::Wrote("CombatStance.drawRate", rate, sizeof(float));
            ++gRateWrites;
            if (written != gRates.end()) {
                written->original = original;
                written->wrote = *rate;
            } else {
                gRates.push_back({seq, original, *rate});
            }
            if (gDrawLogged.find("|" + name + "|") == std::string::npos) {
                gDrawLogged += "|" + name + "|";
                log::Info("draw: {} plays at rate {:.2f} (the game's {:.2f}; [Hands] {} {:.1f})", name, want, original,
                    name.starts_with(engine::adapter::kDrawSequencePrefix) ? "drawRate" : "holsterRate", want);
            }
        } else if (written != gRates.end() && (want <= 1.0005 || !wanted)) {
            RestoreSequelRate(*written);
            gRates.erase(written);
        }
    }
}

#endif  // the sequels (the speed from the push)
#if defined(MEVR_GAME_LE1) || defined(MEVR_GAME_LE2)  // engine::adapter::kWalkStepFlags

std::array<ue3::FieldRef*, 3> const kStepFlags{&gEnableStartAnim, &gEnableStopAnim, &gEnableSkidAnim};

void RestoreSteps(char const* why) {
    if (gSteps.behavior == nullptr || !gSteps.written) {
        gSteps = {.why = gSteps.why};
        return;
    }
    bool restored = false;
    if (IsPlayersBehavior(gSteps.behavior)) {
        for (std::size_t i = 0; i < kStepFlags.size(); ++i) {
            if (gSteps.saved[i] && !kStepFlags[i]->ReadBool(gSteps.behavior).value_or(true)) {
                kStepFlags[i]->WriteBool(gSteps.behavior, true);  // journalled by FieldRef
            }
        }
        restored = true;
    }
    log::Info("movement: the game's start, stop and skid steps as it has them again ({}){}", why,
        restored ? ": " + StepsText(gSteps.saved) : std::string(" (left as they were)"));
    gSteps = {.why = gSteps.why};
}

void UpdateSteps(Link const& link, ue3::Object* pawn, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
#if defined(MEVR_GAME_LE1)
    ue3::Object* const behavior = pawn != nullptr ? gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
#else
    ue3::Object* const behavior = WalkOwner(pawn);
#endif
    char const* const why = settings.startStopAnims ? "[Hands] startStopAnims = 1"
                          : !link.headTrackedView   ? "no head-tracked view"
                          : !onFoot                 ? "not on foot"
                          : behavior == nullptr     ? "no player pawn behaviour"
                                                    : nullptr;
    gSteps.why = why != nullptr ? why : "off in VR on foot";
    if (why != nullptr) {
        RestoreSteps(why);
        return;
    }
    if (behavior != gSteps.behavior) {
        RestoreSteps("the pawn changed");
        gSteps.behavior = behavior;
    }
    bool wrote = false;
    for (std::size_t i = 0; i < kStepFlags.size(); ++i) {
        std::optional<bool> const on = kStepFlags[i]->ReadBool(behavior);
        if (!on) {
            gSteps.why = "flags not found";
            return;
        }
        if (!gSteps.written || *on) {
            gSteps.saved[i] = *on;  // the first time, and each one the game set again since
        }
        if (*on) {
            kStepFlags[i]->WriteBool(behavior, false);  // journalled by FieldRef
            wrote = true;
        }
    }
    bool const first = !gSteps.written;
    gSteps.written = true;
    if (wrote) {
        ++gSteps.writes;
        if (first || gSteps.writes <= 3) {
            log::Info("movement: the game's start, stop and skid steps off in VR on foot (the game's: {}; written {} time{})",
                StepsText(gSteps.saved), gSteps.writes, gSteps.writes == 1 ? "" : "s");
        }
    }
}

#endif  // the steps (ME1, ME2)
#if defined(MEVR_GAME_LE3)

// ME3's start and stop steps (research/38 "ME3's exploring start and stop"; the owner's option A, 2026-11-18): ME3 has none of
// ME1's three flags. Exploring, the player's AnimTree starts every walk with a BioAnimNodeLocomotionStart node
// (ExploreLocomotion-Start-Idle, -Walk, -Run) and ends it with a BioAnimNodeLocomotionStop node, each playing its animation and,
// while it plays, asking the pawn for the animation's root motion (BioPawn.RootMotionOverride: an entry in
// BioPawn.RootMotionOverrides; the start's RMM_Accel, the mesh's RootMotionMode 2 -> 3). So for a start's first ~0.25 s the
// pawn moved as the start animation's root does, not as the stick asks (0, 7, 147, 389 u/s at 0, 0.1, 0.2, 0.4 s exploring;
// combat has no such node: 350 at 0.1), and after a stop the stop animation walked it on about 0.35 m. ME1's rule ([Hands]
// startStopAnims = 0, in VR on foot: no start or stop step moving Shepard): each such request is answered with RMM_Ignore (the
// entry's RMMode written 2, journalled), so the move function's acceleration moves the pawn from the first tick and the stop
// stays a stop; the animations still play on the legs. Making the start node blend out at once (BlendOutRelPos 0) is not a
// lever: the pawn then didn't move at all. Requests we answered that are still there when this stands down get their own mode
// back. Game thread.
ue3::ClassRef gLocomotionStart{"SFXGame.BioAnimNodeLocomotionStart", ue3::kEveryGame};
ue3::ClassRef gLocomotionStop{"SFXGame.BioAnimNodeLocomotionStop", ue3::kEveryGame};
ue3::FieldRef gPawnOverrides{"SFXGame.BioPawn", "RootMotionOverrides", ue3::kEveryGame};
ue3::FieldRef gPawnOverrideNode{gPawnOverrides, "Node"};
ue3::FieldRef gPawnOverrideMode{gPawnOverrides, "RMMode"};
constexpr std::uint8_t kRootMotionIgnore = 2;  // ERootMotionMode RMM_Ignore

struct Answered {
    ue3::Object* node = nullptr;
    std::uint8_t mode = 0;  // what the node asked for
};
struct StartSteps {
    ue3::Object* pawn = nullptr;
    std::vector<Answered> answered;  // the requests we answer now
    std::uint64_t starts = 0;        // requests answered
    std::uint64_t stops = 0;
    bool on = true;                  // a test's switch (`mevr loco start game|on`)
    bool loggedStart = false;
    bool loggedStop = false;
};
StartSteps gStart;

// Gives the requests we answered that are still in the pawn's list their own mode back.
void RestoreStarts(char const* why) {
    std::size_t restored = 0;
    if (gStart.pawn != nullptr && !gStart.answered.empty() && ue3::runtime::IsAlive(gStart.pawn)) {
        if (std::optional<ue3::Elements> const entries = gPawnOverrides.ElementsOf(gStart.pawn, 64)) {
            for (std::int32_t i = 0; i < entries->count; ++i) {
                void* const entry = entries->At(i);
                ue3::Object* const node = gPawnOverrideNode.Read<ue3::Object*>(entry).value_or(nullptr);
                std::uint8_t* const mode = gPawnOverrideMode.In<std::uint8_t>(entry);
                auto const ours = std::find_if(gStart.answered.begin(), gStart.answered.end(), [node](Answered const& a) { return a.node == node; });
                if (ours != gStart.answered.end() && mode != nullptr && *mode == kRootMotionIgnore) {
                    *mode = ours->mode;
                    write_journal::Wrote("CombatStance.stepRootMotion(restore)", mode, 1);
                    ++restored;
                }
            }
        }
    }
    if (restored > 0) {
        log::Info("movement: the game's start and stop steps as it has them again ({}): {} root motion request{} given back", why,
            restored, restored == 1 ? "" : "s");
    }
    gStart.answered.clear();
}

void UpdateStartSteps(Link const& link, ue3::Object* pawn, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    char const* const why = settings.startStopAnims ? "[Hands] startStopAnims = 1"
                          : !gStart.on              ? "the game's start and stop steps (mevr loco start game)"
                          : !link.headTrackedView   ? "no head-tracked view"
                          : !onFoot                 ? "not on foot"
                          : pawn == nullptr         ? "no player pawn"
                                                    : nullptr;
    gSteps.why = why != nullptr ? why
                                : "the start and stop steps' root motion ignored in VR on foot (BioAnimNodeLocomotionStart, -Stop)";
    if (pawn != gStart.pawn) {
        RestoreStarts("the pawn changed");
        gStart.pawn = pawn;
    }
    if (why != nullptr) {
        RestoreStarts(why);
        return;
    }
    std::optional<ue3::Elements> const entries = gPawnOverrides.ElementsOf(pawn, 64);
    if (!entries) {
        gStart.answered.clear();
        return;
    }
    std::vector<Answered> present;  // a request the node released is gone from the list (nothing to give back)
    for (std::int32_t i = 0; i < entries->count; ++i) {
        void* const entry = entries->At(i);
        ue3::Object* const node = gPawnOverrideNode.Read<ue3::Object*>(entry).value_or(nullptr);
        std::uint8_t* const mode = gPawnOverrideMode.In<std::uint8_t>(entry);
        bool const start = node != nullptr && ue3::IsA(node, gLocomotionStart);
        if (mode == nullptr || (!start && (node == nullptr || !ue3::IsA(node, gLocomotionStop)))) {
            continue;
        }
        auto const known = std::find_if(gStart.answered.begin(), gStart.answered.end(), [node](Answered const& a) { return a.node == node; });
        Answered a{node, known != gStart.answered.end() ? known->mode : *mode};
        if (*mode != kRootMotionIgnore) {
            a.mode = *mode;  // the node's request (again, or a new one)
            *mode = kRootMotionIgnore;
            write_journal::Wrote("CombatStance.stepRootMotion", mode, 1);
            ++(start ? gStart.starts : gStart.stops);
            bool& logged = start ? gStart.loggedStart : gStart.loggedStop;
            if (!logged) {
                logged = true;
                log::Info("movement: {} asked for its animation's root motion (RootMotionMode {}): answered with RMM_Ignore in VR on foot "
                          "(the stick moves the pawn; [Hands] startStopAnims = 0)", TreeNodeName(node), a.mode);
            }
        }
        if (a.mode != kRootMotionIgnore) {
            present.push_back(a);
        }
    }
    gStart.answered = std::move(present);
}

#endif  // ME3's start step
#if defined(MEVR_GAME_LE1)

// ---- D ------------------------------------------------------------------------------------------------------------

void RestoreRate(RateWrite const& w) {
    float* const rate = gSeqRate.In<float>(w.sequence);
    if (rate != nullptr && *rate == w.wrote) {
        *rate = w.original;
        write_journal::Wrote("CombatStance.drawRate(restore)", rate, sizeof(float));
    }
}

void UpdateRates(bool wanted, double drawRate, double holsterRate) {
    std::vector<ue3::Object*> sequences = gTree.draws;
    sequences.insert(sequences.end(), gTree.holsters.begin(), gTree.holsters.end());
    for (std::size_t i = 0; i < sequences.size(); ++i) {
        ue3::Object* const seq = sequences[i];
        double const want = i < gTree.draws.size() ? drawRate : holsterRate;  // the holster its own, faster (2026-11-06)
        bool const playing = gSeqPlaying.ReadBool(seq).value_or(false);
        float* const rate = gSeqRate.In<float>(seq);
        if (rate == nullptr) {
            continue;
        }
        auto const written = std::find_if(gRates.begin(), gRates.end(), [seq](RateWrite const& w) { return w.sequence == seq; });
        if (playing && gEvent.open) {
            gEvent.sawSequence = true;
            gEvent.maxRate = std::max(gEvent.maxRate, *rate);
            gEvent.maxSeqTime = std::max(gEvent.maxSeqTime, gSeqTime.Read<float>(seq).value_or(0.0f));
        }
        if (wanted && playing && want > 1.0005 && cs::RateNeedsWrite(*rate, want)) {
            float const original = written != gRates.end() && *rate == written->wrote ? written->original : *rate;
            *rate = static_cast<float>(want);
            write_journal::Wrote("CombatStance.drawRate", rate, sizeof(float));
            ++gRateWrites;
            if (written != gRates.end()) {
                written->original = original;
                written->wrote = *rate;
            } else {
                gRates.push_back({seq, original, *rate});
            }
        } else if (written != gRates.end() && (!playing || !wanted)) {
            RestoreRate(*written);
            gRates.erase(written);
        }
    }
}

void CloseEvent() {
    auto const at = [](double t) { return t >= 0.0 ? std::format("{:.2f} s", t) : std::string("never"); };
    double const switching = gEvent.switchStart >= 0.0 ? (gEvent.switchEnd >= 0.0 ? gEvent.switchEnd : gEvent.age) - gEvent.switchStart : -1.0;
    std::string const weaponNow = WeaponName(weapon::Type());
    std::string const weaponText = std::string(gEvent.kind) == "holster" ? gEvent.weapon
        : weaponNow != "none"                                            ? weaponNow
                                                                         : gEvent.weapon;
    log::Info("draw: {} the {}: action state changed at {}, IsSwitchingWeapons {}, the gun {} the hand at {}; {}",
        std::string(gEvent.kind) == "draw" ? "drew" : std::string(gEvent.kind) == "holster" ? "holstered" : "switched to", weaponText,
        at(gEvent.stateAt),
        switching >= 0.0 ? std::format("true for {:.2f} s (from {:.2f} s{})", switching, gEvent.switchStart,
                               gEvent.switchEnd >= 0.0 ? "" : ", still at the end")
                         : std::string("never true"),
        std::string(gEvent.kind) == "holster" ? "left" : "reached", at(gEvent.inHandAt),
        gEvent.sawSequence ? std::format("the WeaponEquip sequence played at rate {:.2f} to {:.2f} s of its time", gEvent.maxRate, gEvent.maxSeqTime)
                           : std::string("no WeaponEquip draw/holster sequence seen playing"));
    gEvent = {};
}

void UpdateDraw(double deltaSeconds) {
    std::string const weaponNow = WeaponName(weapon::Type());
    if (!gHavePrev) {
        gHavePrev = true;
        gPrevDrawn = gDrawn;
        gPrevSwitching = gSwitching;
        gPrevWeapon = weaponNow;
        return;
    }
    bool const stateChanged = gDrawn != gPrevDrawn;
    bool const switchRose = gSwitching && !gPrevSwitching;
    if (!gEvent.open && (stateChanged || switchRose)) {
        gEvent.open = true;
        gEvent.kind = stateChanged ? (gDrawn ? "draw" : "holster") : "switch";
        gEvent.weapon = gPrevWeapon != "none" ? gPrevWeapon : weaponNow;
        gEvent.age = 0.0;
        gEvent.lastChange = 0.0;
        gEvent.startKey = weapon::Key();
        if (gSwitching) {
            gEvent.switchStart = 0.0;
        }
        if (stateChanged) {
            gEvent.stateAt = 0.0;
        }
    } else if (gEvent.open) {
        gEvent.age += deltaSeconds;
        if (gSwitching && gEvent.switchStart < 0.0) {
            gEvent.switchStart = gEvent.age;
            gEvent.lastChange = gEvent.age;
        }
        if (!gSwitching && gPrevSwitching && gEvent.switchStart >= 0.0 && gEvent.switchEnd < 0.0) {
            gEvent.switchEnd = gEvent.age;
            gEvent.lastChange = gEvent.age;
        }
        if (stateChanged) {
            gEvent.stateAt = gEvent.age;
            gEvent.lastChange = gEvent.age;
            if (std::string(gEvent.kind) == "switch") {
                gEvent.kind = gDrawn ? "draw" : "holster";
            }
        }
    }
    if (gEvent.open && gEvent.inHandAt < 0.0) {
        // The verified weapon in the right hand (game/weapon): gone for a holster, a new one for a draw or switch.
        std::uintptr_t const key = weapon::Key();
        if (std::string(gEvent.kind) == "holster" ? key == 0 : key != 0 && key != gEvent.startKey) {
            gEvent.inHandAt = gEvent.age;
            gEvent.lastChange = std::max(gEvent.lastChange, gEvent.age);
        }
    }
    if (gEvent.open && ((!gSwitching && gEvent.age - gEvent.lastChange >= kEventSettleSeconds && gEvent.age >= kEventSettleSeconds) ||
                           gEvent.age >= kEventMaxSeconds)) {
        CloseEvent();
    }
    gPrevDrawn = gDrawn;
    gPrevSwitching = gSwitching;
    if (weaponNow != "none") {
        gPrevWeapon = weaponNow;
    } else if (!gDrawn) {
        gPrevWeapon = gEvent.open ? gPrevWeapon : std::string("none");
    }
}

void Idle(std::string why) {
    engine::bone_pose::ClearStance();
    gWhy = cs::Why::NotOnFoot;
    gRampWhy = std::move(why);
    gStick.wanted = false;
    gStick.ran = MoveRan::None;
}

#endif  // MEVR_GAME_LE1

} // namespace

bool TryStartup() {
#if defined(MEVR_GAME_LE1)
    gIsInCombatActionState = gIsInCombatActionStateFn.Get();
    gIsSwitchingWeapons = gIsSwitchingWeaponsFn.Get();
    gGetWalkingSpeedMode = gGetWalkingSpeedModeFn.Get();
    gStarted = gIsInCombatActionState != nullptr && gIsSwitchingWeapons != nullptr;
    log::Info("stance: script functions {} (IsInCombatActionState {}, IsSwitchingWeapons {}, GetWalkingSpeedMode {})",
        gStarted ? "found" : "MISSING: the weapon-drawn features are off", gIsInCombatActionState != nullptr,
        gIsSwitchingWeapons != nullptr, gGetWalkingSpeedMode != nullptr);
    // The speed from the push (research/38 "Linear walking"): the game's move functions (CallFunction posts: PlayerMove calls
    // them from script) and the PlayerTick that runs PlayerMove (ProcessEvent posts, as view_level's).
    gSetDesiredSpeed = gSetDesiredSpeedFn.Get();
    gMoveExplore.clear();
    gMoveCombat.clear();
    gStickTicks.clear();
    if (ue3::runtime::Ready()) {
        ue3::Reflection& reflection = ue3::runtime::Get();
        auto const players = [&reflection](char const* name, bool engineToo, std::vector<ue3::Function*>& out) {
            for (ue3::Function* function : reflection.FindFunctionsNamed(name)) {
                std::string const full = reflection.FullName(function);
                if (full.starts_with("Function SFXGame.BioPlayerController.") ||
                    (engineToo && full.starts_with("Function Engine.PlayerController."))) {
                    out.push_back(function);
                }
            }
        };
        players("PlayerMoveExplore", false, gMoveExplore);
        players("PlayerMoveCombat", false, gMoveCombat);
        players("PlayerTick", true, gStickTicks);
    }
    if (!gMoveExplore.empty() && !gStickTicks.empty() && gSetDesiredSpeed != nullptr) {
        for (ue3::Function* function : gMoveExplore) {
            engine::call_function::AddPostHandler(function, &OnMoveCall);
        }
        for (ue3::Function* function : gMoveCombat) {
            engine::call_function::AddPostHandler(function, &OnMoveCall);
        }
        for (ue3::Function* function : gStickTicks) {
            engine::process_event::AddPostHandler(function, &OnStickTick);
        }
    }
    log::Info("movement: the speed from the push: {} PlayerMoveExplore, {} PlayerMoveCombat, {} PlayerTick, SetDesiredSpeed {}",
        gMoveExplore.size(), gMoveCombat.size(), gStickTicks.size(), gSetDesiredSpeed != nullptr ? "found" : "MISSING");
    return gStarted;
#elif defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    // The sequels: section C's ramp, steps and response (the stance, the speed from the push and the draw rates are ME1's
    // still: the audits' MOV-15, BOD-23, BOD-27).
    gGetWalkingSpeedMode = gGetWalkingSpeedModeFn.Get();
    gStarted = ue3::runtime::Up();
    gMoveFunctions.clear();
    if (gStarted) {
        ue3::Reflection& reflection = ue3::runtime::Get();
        for (char const* name : kMoveFunctions) {
            for (ue3::Function* function : reflection.FindFunctionsNamed(name)) {
                if (reflection.FullName(function).starts_with("Function SFXGame.BioPlayerController.")) {
                    gMoveFunctions.push_back(function);
                }
            }
        }
        if (gMoveMagField.Get() == nullptr || gPlayerInput.Get() == nullptr || gRawJoyUp.Get() == nullptr || gRawJoyRight.Get() == nullptr) {
            gMoveFunctions.clear();  // the speed from the push stands down (UpdateStick says why)
        }
        for (ue3::Function* function : gMoveFunctions) {
            engine::call_function::AddPostHandler(function, &OnSequelMove);
        }
    }
    log::Info("movement: the walk on this game: the pawn's acceleration linear to the stick{}, the speed from the push ({} move "
              "function{} found: BioPawn.fMoveMag), the response measured (GetWalkingSpeedMode {}); the stance and the draw rates are ME1's only",
        engine::adapter::kWalkStepFlags ? ", the start, stop and skid steps off" : "", gMoveFunctions.size(),
        gMoveFunctions.size() == 1 ? "" : "s", gGetWalkingSpeedMode != nullptr ? "found" : "MISSING");
    return gStarted;
#else
    return false;
#endif
}

void Tick(double deltaSeconds, Link const& link) {
    gNow += std::max(deltaSeconds, 0.0);
#if defined(MEVR_GAME_LE1)
    if (!gStarted || !ue3::runtime::Ready()) {
        Idle("not started");
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr && player::IsBioPlayerController(controller) ? player::BioPawn(controller) : nullptr;
    ue3::Object* const mesh = pawn != nullptr ? player::Mesh(pawn) : nullptr;
    bool const onFoot = head_tracking::OnFoot();
    if (pawn == nullptr) {
        gDrawn = gSwitching = false;
        gHavePrev = false;
        if (gEvent.open) {
            gEvent = {};
        }
        UpdateStance(deltaSeconds, link, controller, nullptr, nullptr, onFoot);
        UpdateRamp(link, nullptr, onFoot);
        UpdateStick(link, nullptr, onFoot);
        UpdateSteps(link, nullptr, onFoot);
        return;
    }
    gDrawn = CallBool(pawn, gIsInCombatActionState);
    gSwitching = CallBool(pawn, gIsSwitchingWeapons);
    UpdateStance(deltaSeconds, link, controller, pawn, mesh, onFoot);
    UpdateRamp(link, pawn, onFoot);
    UpdateStick(link, pawn, onFoot);
    UpdateSteps(link, pawn, onFoot);
    if (onFoot) {
        UpdateWalk(pawn, link);
        UpdateDraw(deltaSeconds);
    }
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    double const rate = cs::ClampDrawRate(settings.drawRate);
    double const holsterRate = cs::ClampHolsterRate(settings.holsterRate);
    bool const rateWanted = (rate > 1.0005 || holsterRate > 1.0005) && link.headTrackedView && onFoot && mesh != nullptr &&
        gAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) == gTree.tree && gTree.tree != nullptr;
    UpdateRates(rateWanted, rate, holsterRate);
#elif defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    if (!gStarted || !ue3::runtime::Up()) {
        gRampWhy = "not started";
        return;
    }
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr && player::IsBioPlayerController(controller) ? player::BioPawn(controller) : nullptr;
    bool const onFoot = head_tracking::OnFoot();
    UpdateRamp(link, pawn, onFoot);
    UpdateStick(link, pawn, onFoot);
#if defined(MEVR_GAME_LE2)
    UpdateSteps(link, pawn, onFoot);
#else
    UpdateStartSteps(link, pawn, onFoot);
#endif
    if (onFoot && pawn != nullptr) {
        UpdateWalk(pawn, link);
    }
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    double const rate = cs::ClampDrawRate(settings.drawRate);
    double const holsterRate = cs::ClampHolsterRate(settings.holsterRate);
    bool const rateWanted = (rate > 1.0005 || holsterRate > 1.0005) && link.headTrackedView && onFoot && pawn != nullptr;
    UpdateSequelRates(rateWanted, rate, holsterRate, pawn != nullptr ? player::Mesh(pawn) : nullptr);
#else
    (void)link;
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
    engine::bone_pose::StanceStats const stats = engine::bone_pose::GetStanceStats();
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    std::string learned;
    for (Learned const& l : gLearned) {
        learned += std::format("{}{}", learned.empty() ? "" : ", ", json::JsonString(l.key));
    }
    return std::format(
        "{{\"exploreStance\": {}, \"active\": {}, \"why\": {}, \"order\": {}, \"confirmations\": {}, \"insideCalls\": {}, "
        "\"combatModeNodes\": {}, \"gameWeights\": [{}, {}], \"composed\": {}, \"overridden\": {}, \"rejected\": {}, \"broken\": {}, "
        "\"cameraMode\": {}, \"drawn\": {}, \"switching\": {}, "
        "\"ramp\": {{\"enabled\": {}, \"linear\": {}, \"state\": {}, \"written\": {}, \"savedAccel\": {}, \"savedDecel\": {}, "
        "\"savedAccelRate\": {}, \"wroteAccel\": {}, \"wroteAccelRate\": {}}}, "
        "\"walk\": {{\"mode\": {}, \"speed\": {}}}, \"stick\": {}, \"steps\": {}, "
        "\"draw\": {{\"rate\": {}, \"sequences\": {}, \"written\": {}}}, "
        "\"arms\": {{\"bones\": {}, \"why\": {}, \"learned\": [{}], \"inUse\": {}, \"learning\": {}, \"grafted\": {}, "
        "\"notGrafted\": {}, \"captured\": {}}}}}",
        settings.exploreStance ? "true" : "false", gWhy == cs::Why::Active ? "true" : "false", json::JsonString(cs::Name(gWhy)),
        json::JsonString(cs::Name(static_cast<cs::OrderCheck::Order>(stats.order))), stats.confirmations, stats.insideCalls,
        gTree.combat.size(), json::JsonNumber(stats.last.startWeights[0]), json::JsonNumber(stats.last.startWeights[1]), stats.applied,
        stats.overridden, stats.rejected, stats.broken ? "true" : "false", json::JsonString(gModeClass), gDrawn ? "true" : "false",
        gSwitching ? "true" : "false", settings.exploreRamp ? "true" : "false", settings.linearWalk ? "true" : "false",
        json::JsonString(gRampWhy), gRamp.written ? "true" : "false", json::JsonNumber(gRamp.saved.accel), json::JsonNumber(gRamp.saved.decel),
        json::JsonNumber(gRamp.saved.accelRate), json::JsonNumber(gRamp.wrote.accel), json::JsonNumber(gRamp.wrote.accelRate),
        json::JsonString(gWalkMode >= 0 && gWalkMode < static_cast<int>(std::size(kWalkModes)) ? kWalkModes[gWalkMode] : "unknown"),
        json::JsonNumber(gSpeed), StickJson(), StepsJson(), json::JsonNumber(cs::ClampDrawRate(settings.drawRate)), gTree.draws.size() + gTree.holsters.size(),
        gRates.size(), gArms.ok ? gArms.bones.size() : 0, json::JsonString(gArms.ok ? std::string("found") : gArms.why), learned,
        json::JsonString(gHandIndex >= 0 ? gLearned[static_cast<std::size_t>(gHandIndex)].key : std::string("none")),
        gLearning ? "true" : "false", stats.grafted, stats.notGrafted, stats.captured);
}

void LogStats() {
    if (!gStarted) {
        return;
    }
    engine::bone_pose::StanceStats const stats = engine::bone_pose::GetStanceStats();
    log::Info("    stance: {}; order: {} ({} confirmations, {} inside); {} calls, {} composed as explore ({} with the arms' combat "
              "pose put back, {} without), {} captured as combat, {} overridden, {} rejected{}{}; arms: {}; ramp: {}{}; speed: {} ({} of {} "
              "moves from the push, {} the game's; last MoveMag {:.2f}{}); start/stop steps: {}; draw rate writes this period: {}",
        cs::Name(gWhy), cs::Name(static_cast<cs::OrderCheck::Order>(stats.order)), stats.confirmations, stats.insideCalls, stats.calls,
        stats.applied, stats.grafted, stats.notGrafted, stats.captured, stats.overridden, stats.rejected,
        stats.lastReject != nullptr ? std::format(" (last: {})", stats.lastReject) : std::string(), stats.broken ? ", STOPPED after a fault" : "",
        gArms.ok ? std::format("{} bones, the combat pose learned for {} weapon type{}{}", gArms.bones.size(), gLearned.size(),
                       gLearned.size() == 1 ? "" : "s", gLearning ? ", learning" : "")
                 : gArms.why,
        gRampWhy, gRamp.written ? std::format(" (written {} times; the game's {})", gRamp.writes, RampText(gRamp.saved)) : std::string(),
        gStick.why, gStick.applied, gStick.moves, gStick.kept, gStick.moveMag,
        gStick.fraction >= 0.0 ? std::format(" -> {:.0f}% of the run", gStick.fraction * 100.0) : std::string(),
        gSteps.written ? gSteps.why + " (the game's: " + StepsText(gSteps.saved) + ")" : gSteps.why, gRateWrites);
    gRateWrites = 0;
    gStick.moves = gStick.applied = gStick.kept = 0;
}

#if defined(MEVR_GAME_LE3)
std::string StartLevers(std::string const& which) {
    if (which == "game") {
        gStart.on = false;
    } else if (which == "on") {
        gStart.on = true;
    }
    return std::format("mevr loco start: {} (root motion requests answered with RMM_Ignore: {} start{}, {} stop{}; {})",
        gStart.on ? "the start and stop steps' root motion ignored" : "the game's start and stop steps", gStart.starts,
        gStart.starts == 1 ? "" : "s", gStart.stops, gStart.stops == 1 ? "" : "s", gSteps.why);
}
#endif

#if defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
std::string DumpAnimTree(std::string const& filter) {
    ue3::Object* const controller = player::Controller();
    ue3::Object* const pawn = controller != nullptr ? player::BioPawn(controller) : nullptr;
    ue3::Object* const mesh = pawn != nullptr ? player::Mesh(pawn) : nullptr;
    ue3::Object* const root = mesh != nullptr ? gTreeAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) : nullptr;
    if (root == nullptr) {
        return "mevr animtree: no player AnimTree (no pawn, mesh or Animations)";
    }
    std::string const want = LowerText(filter);
    std::vector<ue3::Object const*> visited;
    int printed = 0;
    auto const walk = [&](auto const& self, ue3::Object* node, int depth, std::string const& via) -> void {
        if (node == nullptr || visited.size() >= static_cast<std::size_t>(kMaxNodes) ||
            std::find(visited.begin(), visited.end(), node) != visited.end()) {
            return;
        }
        visited.push_back(node);
        ue3::Reflection& reflection = ue3::runtime::Get();
        std::string line = std::format("{} {} '{}' (via '{}')", depth, reflection.NameText(reflection.ClassOf(node)), TreeNodeName(node), via);
        std::optional<ue3::Elements> const children = ue3::IsA(node, gTreeBlendBase) ? gTreeChildren.ElementsOf(node, kMaxChildren) : std::nullopt;
        if (ue3::IsA(node, gTreeSequence)) {
            std::optional<ue3::Name> const seq = gTreeSeqName.Read<ue3::Name>(node);
            line += std::format(" seq {} rate {:.2f}{}", seq ? ue3::runtime::NameText(*seq) : std::string("?"),
                gTreeSeqRate.Read<float>(node).value_or(-1.0f), gTreeSeqPlaying.ReadBool(node).value_or(false) ? " playing" : "");
        }
        if (children) {
            line += " children:";
            for (std::int32_t i = 0; i < children->count; ++i) {
                std::optional<ue3::Name> const childName = gTreeChildName.Read<ue3::Name>(children->At(i));
                line += std::format(" {}", childName ? ue3::runtime::NameText(*childName) : std::string("?"));
            }
        }
        if (want.empty() || LowerText(line).find(want) != std::string::npos) {
            log::Info("animtree: {}", line);
            ++printed;
        }
        if (children) {
            for (std::int32_t i = 0; i < children->count; ++i) {
                std::optional<ue3::Name> const childName = gTreeChildName.Read<ue3::Name>(children->At(i));
                self(self, gTreeChildAnim.Read<ue3::Object*>(children->At(i)).value_or(nullptr), depth + 1,
                    childName ? ue3::runtime::NameText(*childName) : std::string("?"));
            }
        }
    };
    walk(walk, root, 0, "root");
    return std::format("mevr animtree: {} nodes walked, {} logged (`animtree:` lines in the log){}", visited.size(), printed,
        want.empty() ? "" : " matching '" + filter + "'");
}
#endif

void Shutdown() {
    engine::bone_pose::ClearStance();
#if defined(MEVR_GAME_LE1)
    if (ue3::runtime::Ready()) {
        RestoreRamp("the mod unloads", true);
        RestoreSteps("the mod unloads");
        ue3::Object* const pc = player::Controller();
        ue3::Object* const pawn = pc != nullptr ? player::BioPawn(pc) : nullptr;
        ue3::Object* const mesh = pawn != nullptr ? player::Mesh(pawn) : nullptr;
        if (mesh != nullptr && gTree.tree != nullptr && gAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) == gTree.tree) {
            for (RateWrite const& w : gRates) {
                RestoreRate(w);
            }
        }
    }
    gIsInCombatActionState = gIsSwitchingWeapons = gGetWalkingSpeedMode = nullptr;
    gSetDesiredSpeed = nullptr;
    gMoveExplore.clear();
    gMoveCombat.clear();
    gStickTicks.clear();
    gStick = {};
    gSteps = {};
#elif defined(MEVR_GAME_LE2) || defined(MEVR_GAME_LE3)
    if (ue3::runtime::Up()) {
        RestoreRamp("the mod unloads", true);
#if defined(MEVR_GAME_LE2)
        RestoreSteps("the mod unloads");
#else
        RestoreStarts("the mod unloads");
#endif
        for (RateWrite const& w : gRates) {
            if (ue3::runtime::IsAlive(w.sequence)) {
                RestoreSequelRate(w);
            }
        }
    }
    gSlotSequences.clear();
    gSlotTree = nullptr;
    gSlotWalked = -1e9;
    gDrawLogged.clear();
    gGetWalkingSpeedMode = nullptr;
    gMoveFunctions.clear();
    gStick = {};
    gStop = {};
    gSteps = {};
#endif
    gRamp = {};
    gRates.clear();
    gTree = {};
    gEvent = {};
    gHavePrev = false;
    gWhyLogged = false;
    gLoggedOrder = 0;
    gLoggedFirstDraw = false;
    gHaveNonCombatTag = false;
    gNonCombatTicked = gNonCombatTicks = 0;
    gAppliedAtLog = gOverriddenAtLog = gGraftedAtLog = gNotGraftedAtLog = 0;
    gArms = {};
    gLearned.clear();
    gHandIndex = -1;
    gLastWeaponKey.clear();
    gLearn.Reset();
    gLearnKey.clear();
    gLearnSerial = 0;
    gLearning = false;
    gRejectsSeen = 0;
    gLastWalk = -1e9;
    gWalkMode = -1;
    gStarted = false;
    gWhy = cs::Why::Off;
    gRampWhy = "not started";
}

} // namespace mevr::game::combat_stance
