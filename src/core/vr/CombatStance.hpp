// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string_view>

// The weapon drawn, looking and feeling like exploring (research/38): the game-independent parts, unit-tested in
// tests/Main.cpp.
//   A. Body pose: when the explore stance applies (Decide), and whether the AnimTree ticks before our
//      UpdateSkelPose hook composes the pose (OrderCheck), which "tick as combat, compose as explore" needs.
//   B. View shift: one eye point for every standing on-foot camera mode (ExploreEye, ConstantEyeOffset,
//      ConstantEyeApplies).
//   C. Movement: the combat speed ramp written to the explore values (RampNeedsWrite).
//   D. Draw delay: the draw and holster animations' play rate (ClampDrawRate, RateNeedsWrite).
namespace mevr::vr::combat_stance {

// ---- A. Body pose ---------------------------------------------------------------------------------------------

// Why the body composes as explore (Active) or as the game has it.
enum class Why : std::uint8_t {
    Active,          // drawn, standing, first person, VR on foot: the body composes as explore
    Off,             // [Hands] exploreStance = 0
    NoView,          // no head-tracked world view (VR off, a menu, flat screen)
    NotOnFoot,       // the Mako, conversations, cutscenes, menus, wheels
    NoPawn,          // the player has no BioPawn (the Mako)
    NotFirstPerson,  // third person (Halcyon's K), or arm IK isn't placing the hands in the view
    Holstered,       // not in the combat action state: the body is already the explore stance
    Crouched,        // the explore crouch is a static pose: the legs would freeze crouch-walking
    Cover,
    Scoped,          // the sniper scope's camera mode
    Storming,        // sprinting with [Hands] exploreStanceStorm = 0
    NoNode,          // the player's AnimTree has no CombatMode node with NonCombat and Combat children
    NoHook,          // our UpdateSkelPose hook isn't in (arm IK never turned on)
    OrderUnknown,    // waiting for OrderCheck to see the tree tick before the hook
    OrderInside,     // the tree ticks inside UpdateSkelPose: this seam can't work; stood down
    NoHandBones,     // the arms (upper arm and everything below it: hands, fingers, Prop01, LHIK) weren't found
    LearningHands,   // the combat pose of the arms and hands for this weapon isn't known yet: the game's pose until
                     // it has held still (then it is kept for the weapon type, research/38 A "the arms keep combat")
};
char const* Name(Why why);

struct Inputs {
    bool enabled = true;           // [Hands] exploreStance
    bool headTrackedView = false;  // the host's head-tracked view is valid
    bool onFoot = false;           // game mode 0
    bool pawn = false;             // the player's pawn is a BioPawn
    bool firstPerson = false;      // arm IK anchors the hands in the view (first person)
    bool drawn = false;            // BioPawn.IsInCombatActionState
    bool crouched = false;         // Pawn.bIsCrouched
    bool cover = false;            // BioPawn.CoverType or CoverAction set
    bool scoped = false;           // the camera mode (or the one it blends to) is the sniper scope
    bool storming = false;         // sprinting (BioPlayerController.bStorming)
    bool stanceWhileStorming = true;  // [Hands] exploreStanceStorm
    bool node = false;             // CombatMode found in the player's AnimTree
    bool hook = false;             // our UpdateSkelPose hook is in
    bool orderKnown = false;       // OrderCheck has an answer...
    bool orderInside = false;      // ...and it is "the tree ticks inside the hook"
    bool handBones = false;        // both arms' bones found (the graft's bone set)
    bool handPose = false;         // the combat pose of the arms and hands is known for the weapon in the hand
};
Why Decide(Inputs const& in);

// Whether the AnimTree's tick (weights, times, notifies) runs before UpdateSkelPose (what our pre-stage needs: it
// changes only what is composed) or inside it (then our weights would feed the tick). Fed with each UpdateSkelPose
// call on the player's mesh: the delta time and the CombatMode node's NodeTickTag at the call's start and end.
class OrderCheck {
public:
    enum class Order : std::uint8_t { Unknown, Before, Inside };
    // Ticked calls (the tag moved since the previous call, with the tag unchanged across the call) needed for Before.
    static constexpr int kNeeded = 30;
    void Feed(float deltaTime, std::int32_t startTag, std::int32_t endTag);
    Order Result() const { return result_; }
    int Confirmations() const { return confirmations_; }
    int InsideCalls() const { return inside_; }
    void Reset() { *this = OrderCheck{}; }

private:
    Order result_ = Order::Unknown;
    bool havePrevious_ = false;
    std::int32_t previousEnd_ = 0;
    int confirmations_ = 0;
    int inside_ = 0;
};
char const* Name(OrderCheck::Order order);

// ---- B. View shift ----------------------------------------------------------------------------------------------

struct Vec {
    double x = 0.0;  // forward
    double y = 0.0;  // right
    double z = 0.0;  // up
};

// The camera is the hook bone + HookOffset - Offset (view pitch 0, yaw-relative; research/18): the eye from the bone
// that exploring gives, from the Explore mode's HookOffset and Halcyon's fpExplore Offset. (0,0,120) and
// (-50.1, 0, 25.1) give (50.1, 0, 94.9).
Vec ExploreEye(Vec exploreHook, Vec exploreOffset);
// The Offset that puts a mode's eye at `eye`: HookOffset - eye.
Vec ConstantEyeOffset(Vec modeHook, Vec eye);
// Whether a first-person camera mode (Halcyon already chose it: Explore, Combat, TightAim and the sprint modes; never
// cover, the scope or the Mako) takes the constant eye: every standing one. Crouch modes (their object or class name
// says Crouch) keep Halcyon's values: crouching lowers the view as before.
bool ConstantEyeApplies(std::string_view className, std::string_view objectName);
// Whether a mode is the explore mode whose HookOffset defines the eye (Explore, not its sprint or crouch).
bool IsExploreMode(std::string_view className, std::string_view objectName);
// A HookOffset worth using: finite, within 1000 units.
bool PlausibleHook(Vec hook);

// ---- C. Movement ------------------------------------------------------------------------------------------------

// The combat speed ramp (BioPawnBehavior.m_fMaxDesSpdTac{Accel,Decel}Rate) set to the explore values
// (m_fMaxDesSpd{Accel,Decel}Rate). A rate is usable when finite and in (0, 100]; a write is needed when they differ.
bool UsableRate(float rate);
bool RampNeedsWrite(float tacAccel, float tacDecel, float exploreAccel, float exploreDecel);

// ---- D. Draw delay ----------------------------------------------------------------------------------------------

// [Hands] drawRate: 1 = the game's own speed; clamped to [1, kMaxDrawRate] (NaN: 1). The animation still plays, so every
// notify that runs the switch fires in order (an advance issues the notifies it passes); 6 (the default since 2026-10-25)
// finishes a 1 s draw in about 0.16 s.
inline constexpr double kDefaultDrawRate = 6.0;
inline constexpr double kMaxDrawRate = 10.0;
double ClampDrawRate(double rate);
// Whether a playing draw or holster sequence needs its Rate written (it differs from `wanted` by more than 0.001).
bool RateNeedsWrite(float current, double wanted);

} // namespace mevr::vr::combat_stance
