#pragma once

#include "core/ik/Rigid.hpp"
#include "game/GamepadMapping.hpp"

#include <cstdint>
#include <optional>

// The two-handed grip ("focused fire", notes/todo-vrik.md): the left grip squeezed with the left
// hand on the held gun's foregrip holds the gun with both hands. While held:
//   - the game sees LT held (aimed fire's accuracy bonus) instead of LB (gamepad::UpdateSupportGrip,
//     run where the pad is built: the host's XInput hook with Halcyon's renderer, game/gamepad_bridge
//     with ours);
//   - the barrel points from the right hand toward the left, the roll staying with the right wrist
//     (ik::TwoHandRotation, in engine/bone_pose's writer);
//   - the left wrist goes to the foregrip on the gun as written (the AnimTree's own off-hand IK,
//     LeftWrist -> LHIK, runs before our right-arm write, so it would hold the hand where the
//     animation had the gun).
// Shots and the reticle follow the gun model as before (game/hand_aim reads the muzzle).
//
// The foregrip, per gun, in the right hand bone's axes, from the first that fits on the gun
// (ik::OnGun): a weapon-mesh socket named for the off hand (game/weapon); the body's LHIK bone (the
// off-hand IK goal, carried with the right hand); the animated left hand; else under the grip
// (pistols). Settled while it holds still, logged once per gun. Game thread unless noted.
namespace mevr::game::support_grip {

struct Foregrip {
    ik::Vec3 position;       // the left wrist, in the right hand bone's axes (units)
    bool hasRotation = false;
    ik::Quat rotation;       // the left hand bone in the right hand bone's axes
    bool twoHandAim = true;  // the span runs along the gun, so both hands define the barrel
    char const* source = "";
};

// The AnimTree's own off-hand IK as the engine has it this tick: the SkelControlLimb `LeftArm`
// (LeftWrist -> LHIK; research/18). Its strength follows the `AddCombatIdleOffset` blend node's
// weight while `fromAnimNode`; the weapon draw and holster code clears that and blends it off.
struct NativeIkSample {
    double strength = 0.0;  // ControlStrength (+0xA8)
    double target = 0.0;    // StrengthTarget (+0xB4)
    bool fromAnimNode = false;  // bSetStrengthFromAnimNode (+0xCC bit 0x2)
};

// What arm IK sees this tick, all in the right hand bone's axes (units).
struct Candidates {
    std::uintptr_t gun = 0;  // game/weapon's Key
    ik::Vec3 barrel;
    ik::Vec3 muzzle;
    ik::Vec3 up;  // the gun's top: the hand's thumb side (ik::GunUpInHand)
    std::optional<ik::Vec3> socket;
    std::optional<ik::Vec3> lhik;
    std::optional<ik::Transform> animatedLeft;  // before our writes
    std::optional<NativeIkSample> nativeIk;     // nullopt: no AnimTree or no LeftArm control
};

// Arm IK, each tick it aims a held gun with a measured grip. Logs the candidates once per gun and
// the native off-hand IK's state (off, blending, on) on each change.
void Measure(Candidates const& candidates);
// The settled foregrip for the gun last measured.
std::optional<Foregrip> Current();

// The grab zone ([Controls] supportZone, 2026-10-22): a capsule along the gun (ik::MakeSupportZone) from
// `frontMeters` ahead of the right hand to `pastMeters` past the muzzle, plus the foregrip, of radius
// supportAcquireM (meters of left-controller motion; the writer measures, the pad mapping compares).
struct Zone {
    bool capsule = true;  // false: the foregrip alone (supportZone = point)
    double frontMeters = 0.10;
    double pastMeters = 0.10;
};
Zone CurrentZone();
// Nonzero while the support grip holds, a new value for each hold (the writer captures the line between
// the hands at its start: engine::bone_pose::Support::hold).
std::uint32_t HoldSerial();

// Each mod tick, after arm IK. `requested`: arm IK asked the writer for the support grip this tick
// (engine::bone_pose::Support), so its distance is this gun's. Updates the distance and settings the
// pad mapping reads, the blend arm IK uses next tick, and logs holds and releases, and each change
// of the held weapon's bIsZoomed (the game's aimed fire, research/18) with whether our grip is
// forcing LT then.
void Tick(double deltaSeconds, bool requested);
// The pad mapping's answer (any thread): the support grip is held.
void SetHeld(bool held);
bool Held();
// 0 one-handed .. 1 both hands (eased over 0.1 s, like Overlord's grip blend).
double Blend();
// For the pad mapping (any thread): the left trigger setting and the support grip's input.
gamepad::Options PadOptions();

void LogStats();
void Shutdown();

} // namespace mevr::game::support_grip
