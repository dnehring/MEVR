# 14: VRIK for ME LE: arms and body following the controllers (2026-10-08)

**Goal:** Shepard's arms and hands follow the motion controllers, and the body follows the
head, so the player sees their own body in first person. The weapon sits in the hand. This is
the feature that sets us apart: none of the existing ME LE VR mods
([13](13-existing-me-vr-mods.md)) do it in the open.
- Halcyon's mods have no motion controls.
- The closed ME2VR has arm IK, but no source.

## Prior art (concepts only)

| Project | How it does arms | Usable by us |
|---------|------------------|--------------|
| ME2VR (closed, LE2) | **Writes IK results back into the pose after animation.** Its log strings mention a post-animation pose-update hook, "live left/right-arm IK writeback", an "ArmDriver" with a captured rest pose, and the weapon following a `Prop01` writeback. It resolves bones by walking the skeletal mesh's ref skeleton (BoneResolver) | Concept only (never disassembled) |
| Overlord (GPL, MW2CR) | Analytic two-bone arm IK plus finger posing, on a yaw-deadzone body frame | Our solver is our own; Overlord's may be copied since we went GPL-3.0 |
| UE3 engine | **`SkelControlLimb`**: native two-bone IK in the AnimTree, driven by `EffectorLocation` and `JointTargetLocation` in a chosen space | Engine feature; driving it is plain gameplay code |

## Two ways to pose the arms in UE3-LE

1. **Drive engine IK controls (preferred if available).** If the player's AnimTree already has a
   `SkelControlLimb` on each hand bone, set its `EffectorLocation` (world space) to the
   controller and its `JointTargetLocation` to our elbow hint, at strength 1. The engine solves
   and blends every frame, and attachments (the weapon) follow the hand bone.
   - The SDK exposes everything needed: `UAnimTree::SkelControlLists` (per bone, linked by
     `NextControl`), the `USkelControlLimb` fields, `ControlStrength` / `StrengthTarget`, and
     `USkeletalMeshComponent::InitSkelControls`.
   - If the tree has no such controls, add them: construct a `SkelControlLimb` at runtime
     (`StaticConstructObject`, LE1 `0x16D610`, a function start) and link it into the hand
     bone's list. Or patch the AnimTree asset in the package as a mod file.
2. **Write the bones ourselves (fallback).** After animation, overwrite the arm bones in
   `SpaceBases` (component-space matrices, exposed at `USkeletalMeshComponent+0x2B0`) using
   `core/ik`'s solution. This needs a hook after the pose is computed and before it's rendered.
   That's the ME2VR route, and its LE1 address is unknown; finding it needs the decrypted code
   dump.

## What's built (2026-10-08)

- **`core/ik` (unit-tested):**
  - `BodyYaw`: the body follows head yaw past a deadzone and slowly re-centres while the head
    holds still.
  - `EstimateUpperBody`: neck and shoulders from the head and body yaw, with average adult
    proportions (calibration later).
  - `SolveTwoBone`: an analytic shoulder–elbow–hand solver. It keeps exact bone lengths, uses the
    pole plane, straightens toward out-of-reach targets, and handles a degenerate pole.
  - `ElbowPole`: an elbow hint below, outside and behind the shoulder.
- **`game/SkeletonProbe`** (runs by itself, logs once per new player mesh):
  - every bone name;
  - component-space positions of the key bones (pelvis, spine, neck, head, clavicles, arms,
    hands, fingers, weapon and prop sockets);
  - every skeletal control in the AnimTree, with limb details.

  **The next launch answers which route (1 or 2) is open.**
- **Hand aim (`,`):** shots follow the right controller's aim ray, on foot. It goes through a new
  `UObject::CallFunction` hook (LE1 `0x12AE40`), because the weapon asks
  `BioPlayerController.GetAdjustedAimFor` from script, which ProcessEvent never sees. ME2VR's
  logs show the same approach on LE2. Since 2026-10-08 the controller reaches the world through
  `game/view_anchor` (an `engine::TrackingFrame`), which also works with Halcyon's renderer: it
  publishes its view relative to the game camera ([13](13-existing-me-vr-mods.md)). **Arm IK
  should take its hand positions from the same anchor.** Hand aim sets only the direction:
  shots still start at the game's StartFireLoc, so they run parallel to the controller's ray,
  offset by the distance between them. The log measures that offset once **[verify]**; if it's
  large (third person), the fix is a start-location override or aiming from StartFireLoc at the
  ray's hit point (a world trace).

**Route 1 driver (built 2026-10-08, `.` toggles, off by default): `game/ArmIk`.**

- **Finding the controls.** For the player's mesh it finds the first `SkelControlLimb` on each
  hand bone. Hand bones are matched by name: `Hand_R`, `R_Hand`, `RightHand`, `Wrist_L` and so on.
- **Driving them.** While on, it sets each control's effector to that controller's grip pose in
  world space. The joint target is the `core/ik` elbow pole. Strength is 1.
- **Mapping to the world.** Controller positions are placed relative to Shepard's **head bone**
  (world location via `GetBoneLocation`), in the frame of the pawn's facing. That works in third
  person too, so route 1 can be tested before a first-person camera exists. Every tick reads
  fresh from the live pawn; no object pointers are kept across ticks (review finding).
- **Safety.** It saves the controls' settings and restores them when turned off or when tracking
  is lost. Every tick it re-finds the controls by walking the live AnimTree and writes only to
  those found. Bone names are matched as whole tokens (`core/ik/BoneNames`, tested).
- **When there's nothing to drive.** If the probe finds no limb controls on the hand bones, it
  logs that once and stays inert.

**Research tooling:** `tools/xref.py` finds callers and references, and maps UE3's native table
(`execGetBoneNames`-style names) to addresses. It runs on the disassemblable exe built from the
mod's code dump. This is how route 2's post-animation hook will be found.

## Plan

1. **Read the probe log.**
   - Bone names give the arm chain: clavicle, upper arm, forearm, hand, and the weapon socket.
   - Positions give arm lengths in game units, to check against `kUnitsPerMeter`.
   - Existing `SkelControlLimb`s decide route 1 or 2.
2. **Route 1 (if the controls exist).** Each frame on the game thread:
   - a. Controller grip pose (LOCAL) to world. Use the same mapping as the head (game yaw, plus
     camera position when 6DOF is on), with a per-controller grip offset so the hand bone sits
     in the palm.
   - b. Set `EffectorLocation` and `JointTargetLocation` (world space), strength 1.
   - c. Hand rotation: `bMaintainEffectorRelRot` or a `SkelControlSingleBone` on the hand bone.
3. **First-person body:**
   - MassFPS-style head hiding (research/11), so the camera sits in the head.
   - The torso turns with `BodyYaw` (the pawn's rotation, or a spine `SkelControlSingleBone`).
4. **Weapon in hand:** the weapon is attached to a hand socket, so it follows the IK'd hand. The
   aim ray then comes from the weapon's muzzle, not the controller, which unifies hand aim with
   what you see.
5. **Comfort and polish:**
   - arm length calibration (T-pose or a "hold arms out" step);
   - smoothing the elbow pole;
   - limits for the cover and roadie-run animations.

## Open questions [verify in game]

- Does LE's ProcessEvent run native functions (`GetBoneNames`, `SetFrameRate`)? The probe and
  the reflection fix log whether results come back.
- World scale: does 1 m = 100 units match the bone positions?
- Is Shepard's head a separate mesh (MassFPS says yes, H5)? The body mesh's bones then stop at
  the neck.
