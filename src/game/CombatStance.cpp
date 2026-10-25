// SPDX-License-Identifier: MIT
#include "game/CombatStance.hpp"

#include "core/Log.hpp"
#include "core/WriteJournal.hpp"
#include "core/vr/CombatStance.hpp"
#include "core/vr/TestRequest.hpp"
#include "engine/BonePose.hpp"
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
ue3::FieldRef gPlayerCamera{"Engine.PlayerController", "PlayerCamera"};
ue3::FieldRef gCurrentCameraMode{"SFXGame.SFXCameraNativeBase", "CurrentCameraMode"};
ue3::FieldRef gInterpolateTo{"SFXGame.SFXCameraMode_Interpolate", "To"};
ue3::FunctionRef gIsInCombatActionStateFn{"SFXGame.BioPawn.IsInCombatActionState"};
ue3::FunctionRef gIsSwitchingWeaponsFn{"SFXGame.BioPawn.IsSwitchingWeapons"};
ue3::FunctionRef gGetWalkingSpeedModeFn{"SFXGame.BioPawn.GetWalkingSpeedMode"};
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
std::string gModeClass = "none";

// ---- C: the ramp --------------------------------------------------------------------------------------------------

struct Ramp {
    ue3::Object* behavior = nullptr;
    bool written = false;
    float savedAccel = 0.0f;  // the game's combat ramp before ours
    float savedDecel = 0.0f;
    float wroteAccel = 0.0f;
    float wroteDecel = 0.0f;
};
Ramp gRamp;
std::string gRampWhy = "not started";

// The walking mode (EWalkingSpeedMode) and speed.
constexpr char const* kWalkModes[] = {"ExploreRun", "ExploreWalk", "ExploreStorming", "ExploreCrouched", "CombatRun", "CombatWalk",
    "CombatStorming", "CombatCrouched", "CombatZoomed", "CoverMove", "CoverCrouched"};
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
    case weapon::RangedType::Unknown: break;
    }
    return weapon::Key() != 0 ? "unknown weapon" : "none";
}

ue3::Function* gIsInCombatActionState = nullptr;
ue3::Function* gIsSwitchingWeapons = nullptr;
ue3::Function* gGetWalkingSpeedMode = nullptr;

bool CallBool(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return false;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    return parms.ReturnBool().value_or(false);
}

int CallByte(ue3::Object* object, ue3::Function* function) {
    if (object == nullptr || function == nullptr) {
        return -1;
    }
    ue3::Params parms(ue3::runtime::Get(), function);
    ue3::runtime::Call(object, function, parms);
    std::optional<std::uint8_t> const value = parms.Return<std::uint8_t>();
    return value ? static_cast<int>(*value) : -1;
}

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

void UpdateStance(Link const& link, ue3::Object* controller, ue3::Object* pawn, ue3::Object* mesh, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    engine::bone_pose::StanceStats const stats = engine::bone_pose::GetStanceStats();
    ue3::Object* const root = mesh != nullptr ? gAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) : nullptr;
    // The tree is walked for A and for D's sequences.
    bool const walk = (settings.exploreStance || cs::ClampDrawRate(settings.drawRate) > 1.0005) && link.headTrackedView && onFoot &&
        pawn != nullptr && root != nullptr;
    bool const wanted = walk && settings.exploreStance;
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
    gWhy = cs::Decide(in);
    LogWhy(gWhy, stats);
    LogVerify(stats);

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
                  "{} calls composed as explore, {} where the call changed our weights, {} rejected{}; the explore branch ticked "
                  "{} of {} ticks at weight 0{})",
            gNow - gActiveSince, stats.last.valid ? Weights(stats.last.startWeights) : std::string("?/?"),
            stats.applied - gAppliedAtLog, stats.overridden - gOverriddenAtLog, stats.rejected,
            stats.lastReject != nullptr ? std::format(", last: {}", stats.lastReject) : std::string(), gNonCombatTicked, gNonCombatTicks,
            gNonCombatTicks > 0 && gNonCombatTicked == 0 ? ": FROZEN (its nodes skip their tick at weight 0: the legs won't walk)" : "");
        gAppliedAtLog = stats.applied;
        gOverriddenAtLog = stats.overridden;
        gNonCombatTicked = gNonCombatTicks = 0;
    }
}

// ---- C ------------------------------------------------------------------------------------------------------------

bool IsPlayersBehavior(ue3::Object* behavior) {
    ue3::Object* const pc = player::Controller();
    ue3::Object* const pawn = pc != nullptr ? player::BioPawn(pc) : nullptr;
    return behavior != nullptr && pawn != nullptr && gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) == behavior &&
        ue3::runtime::IsAlive(behavior);
}

// Puts the game's combat ramp back (only if it is still ours: the game may have set its own since).
void RestoreRamp(char const* why, bool playerCheck) {
    if (gRamp.behavior == nullptr || !gRamp.written) {
        gRamp = {};
        return;
    }
    bool restored = false;
    if (!playerCheck || IsPlayersBehavior(gRamp.behavior)) {
        float* const accel = gTacAccelRate.In<float>(gRamp.behavior);
        float* const decel = gTacDecelRate.In<float>(gRamp.behavior);
        if (accel != nullptr && decel != nullptr && *accel == gRamp.wroteAccel && *decel == gRamp.wroteDecel) {
            *accel = gRamp.savedAccel;
            *decel = gRamp.savedDecel;
            write_journal::Wrote("CombatStance.ramp(restore)", accel, sizeof(float));
            write_journal::Wrote("CombatStance.ramp(restore)", decel, sizeof(float));
            restored = true;
        }
    }
    log::Info("movement: combat walking ramps as the game has it again ({}){}", why,
        restored ? std::format(": accel {:.2f}, decel {:.2f} per s", gRamp.savedAccel, gRamp.savedDecel) : std::string(" (left as it was)"));
    gRamp = {};
}

void UpdateRamp(Link const& link, ue3::Object* pawn, bool onFoot) {
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    ue3::Object* const behavior = pawn != nullptr ? gBehavior.Read<ue3::Object*>(pawn).value_or(nullptr) : nullptr;
    char const* why = !settings.exploreRamp ? "[Hands] exploreRamp = 0"
                    : !link.headTrackedView ? "no head-tracked view"
                    : !onFoot               ? "not on foot"
                    : behavior == nullptr   ? "no player pawn behaviour"
                                            : nullptr;
    gRampWhy = why != nullptr ? why : "explore ramp";
    if (why != nullptr) {
        RestoreRamp(why, true);
        return;
    }
    if (behavior != gRamp.behavior) {
        RestoreRamp("the pawn changed", true);
        gRamp.behavior = behavior;
    }
    float* const accel = gTacAccelRate.In<float>(behavior);
    float* const decel = gTacDecelRate.In<float>(behavior);
    std::optional<float> const exploreAccel = gAccelRate.Read<float>(behavior);
    std::optional<float> const exploreDecel = gDecelRate.Read<float>(behavior);
    if (accel == nullptr || decel == nullptr || !exploreAccel || !exploreDecel) {
        gRampWhy = "fields not found";
        return;
    }
    if (!cs::RampNeedsWrite(*accel, *decel, *exploreAccel, *exploreDecel)) {
        return;
    }
    // The game's values: the first time, or when it set its own since our write (UpdateDefaultSpeeds).
    if (!gRamp.written || *accel != gRamp.wroteAccel || *decel != gRamp.wroteDecel) {
        gRamp.savedAccel = *accel;
        gRamp.savedDecel = *decel;
    }
    *accel = *exploreAccel;
    *decel = *exploreDecel;
    write_journal::Wrote("CombatStance.ramp", accel, sizeof(float));
    write_journal::Wrote("CombatStance.ramp", decel, sizeof(float));
    gRamp.written = true;
    gRamp.wroteAccel = *accel;
    gRamp.wroteDecel = *decel;
    log::Info("movement: combat walking ramps like exploring (accel {:.2f} -> {:.2f}, decel {:.2f} -> {:.2f} per s)", gRamp.savedAccel,
        *accel, gRamp.savedDecel, *decel);
}

// The walking mode and speed, once per change (research/38 C: the zoomed walk).
void UpdateWalk(ue3::Object* pawn, Link const& link) {
    int const mode = CallByte(pawn, gGetWalkingSpeedMode);
    std::optional<ue3::Vector> const velocity = gVelocity.Read<ue3::Vector>(pawn);
    gSpeed = velocity ? std::sqrt(static_cast<double>(velocity->x) * velocity->x + static_cast<double>(velocity->y) * velocity->y) : 0.0;
    if (mode == gWalkMode) {
        gWalkTop = std::max(gWalkTop, gSpeed);
        return;
    }
    if (gNow - gLastMoveLog < kMoveLogSeconds && gWalkMode >= 0) {
        return;  // flapping: the next tick tries again
    }
    auto const name = [](int m) { return m >= 0 && m < static_cast<int>(std::size(kWalkModes)) ? kWalkModes[m] : "unknown"; };
    log::Info("movement: walking mode {} ({}{}), speed {:.0f} u/s{}", name(mode), gDrawn ? "drawn" : "holstered",
        link.supportHeld ? ", the support grip holds the gun (LT)" : "", gSpeed,
        gWalkMode >= 0 ? std::format("; {} held {:.1f} s, top speed {:.0f} u/s", name(gWalkMode), gNow - gWalkModeSince, gWalkTop)
                       : std::string());
    gLastMoveLog = gNow;
    gWalkMode = mode;
    gWalkModeSince = gNow;
    gWalkTop = gSpeed;
}

// ---- D ------------------------------------------------------------------------------------------------------------

void RestoreRate(RateWrite const& w) {
    float* const rate = gSeqRate.In<float>(w.sequence);
    if (rate != nullptr && *rate == w.wrote) {
        *rate = w.original;
        write_journal::Wrote("CombatStance.drawRate(restore)", rate, sizeof(float));
    }
}

void UpdateRates(bool wanted, double drawRate) {
    std::vector<ue3::Object*> sequences = gTree.draws;
    sequences.insert(sequences.end(), gTree.holsters.begin(), gTree.holsters.end());
    for (ue3::Object* seq : sequences) {
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
        if (wanted && playing && cs::RateNeedsWrite(*rate, drawRate)) {
            float const original = written != gRates.end() && *rate == written->wrote ? written->original : *rate;
            *rate = static_cast<float>(drawRate);
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
        UpdateStance(link, controller, nullptr, nullptr, onFoot);
        UpdateRamp(link, nullptr, onFoot);
        return;
    }
    gDrawn = CallBool(pawn, gIsInCombatActionState);
    gSwitching = CallBool(pawn, gIsSwitchingWeapons);
    UpdateStance(link, controller, pawn, mesh, onFoot);
    UpdateRamp(link, pawn, onFoot);
    if (onFoot) {
        UpdateWalk(pawn, link);
        UpdateDraw(deltaSeconds);
    }
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    double const rate = cs::ClampDrawRate(settings.drawRate);
    bool const rateWanted = rate > 1.0005 && link.headTrackedView && onFoot && mesh != nullptr &&
        gAnimations.Read<ue3::Object*>(mesh).value_or(nullptr) == gTree.tree && gTree.tree != nullptr;
    UpdateRates(rateWanted, rate);
#else
    (void)link;
#endif
}

std::string StatusJson() {
    namespace json = vr::test_request;
    engine::bone_pose::StanceStats const stats = engine::bone_pose::GetStanceStats();
    hand_calibration::CombatStance const settings = hand_calibration::GetCombatStance();
    return std::format(
        "{{\"exploreStance\": {}, \"active\": {}, \"why\": {}, \"order\": {}, \"confirmations\": {}, \"insideCalls\": {}, "
        "\"combatModeNodes\": {}, \"gameWeights\": [{}, {}], \"composed\": {}, \"overridden\": {}, \"rejected\": {}, \"broken\": {}, "
        "\"cameraMode\": {}, \"drawn\": {}, \"switching\": {}, "
        "\"ramp\": {{\"enabled\": {}, \"state\": {}, \"written\": {}, \"savedAccel\": {}, \"savedDecel\": {}}}, "
        "\"walk\": {{\"mode\": {}, \"speed\": {}}}, "
        "\"draw\": {{\"rate\": {}, \"sequences\": {}, \"written\": {}}}}}",
        settings.exploreStance ? "true" : "false", gWhy == cs::Why::Active ? "true" : "false", json::JsonString(cs::Name(gWhy)),
        json::JsonString(cs::Name(static_cast<cs::OrderCheck::Order>(stats.order))), stats.confirmations, stats.insideCalls,
        gTree.combat.size(), json::JsonNumber(stats.last.startWeights[0]), json::JsonNumber(stats.last.startWeights[1]), stats.applied,
        stats.overridden, stats.rejected, stats.broken ? "true" : "false", json::JsonString(gModeClass), gDrawn ? "true" : "false",
        gSwitching ? "true" : "false", settings.exploreRamp ? "true" : "false", json::JsonString(gRampWhy), gRamp.written ? "true" : "false",
        json::JsonNumber(gRamp.savedAccel), json::JsonNumber(gRamp.savedDecel),
        json::JsonString(gWalkMode >= 0 && gWalkMode < static_cast<int>(std::size(kWalkModes)) ? kWalkModes[gWalkMode] : "unknown"),
        json::JsonNumber(gSpeed), json::JsonNumber(cs::ClampDrawRate(settings.drawRate)), gTree.draws.size() + gTree.holsters.size(),
        gRates.size());
}

void LogStats() {
    if (!gStarted) {
        return;
    }
    engine::bone_pose::StanceStats const stats = engine::bone_pose::GetStanceStats();
    log::Info("    stance: {}; order: {} ({} confirmations, {} inside); {} calls, {} composed as explore, {} overridden, {} rejected{}{}; "
              "ramp: {}{}; draw rate writes this period: {}",
        cs::Name(gWhy), cs::Name(static_cast<cs::OrderCheck::Order>(stats.order)), stats.confirmations, stats.insideCalls, stats.calls,
        stats.applied, stats.overridden, stats.rejected,
        stats.lastReject != nullptr ? std::format(" (last: {})", stats.lastReject) : std::string(), stats.broken ? ", STOPPED after a fault" : "",
        gRampWhy, gRamp.written ? std::format(" (written; the game's accel {:.2f}, decel {:.2f})", gRamp.savedAccel, gRamp.savedDecel)
                                : std::string(),
        gRateWrites);
    gRateWrites = 0;
}

void Shutdown() {
    engine::bone_pose::ClearStance();
#if defined(MEVR_GAME_LE1)
    if (ue3::runtime::Ready()) {
        RestoreRamp("the mod unloads", true);
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
    gAppliedAtLog = gOverriddenAtLog = 0;
    gRejectsSeen = 0;
    gLastWalk = -1e9;
    gWalkMode = -1;
    gStarted = false;
    gWhy = cs::Why::Off;
    gRampWhy = "not started";
}

} // namespace mevr::game::combat_stance
