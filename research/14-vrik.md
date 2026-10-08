# 14: VRIK for ME LE: arms and body following the controllers (2026-10-08)

**Goal:** Shepard's arms and hands follow the motion controllers, and the body follows the
head, so the player sees their own body in first person. The weapon sits in the hand. This is
the feature that sets us apart: none of the existing ME LE VR mods
([13](13-existing-me-vr-mods.md)) do it in the open.
- Halcyon's mods have no motion controls.
- The closed ME2VR has arm IK, but no source.

## Prior art

| Project | How it does arms | Usable by us |
|---------|------------------|--------------|
| ME2VR (closed, LE2) | **Writes IK results back into the pose after animation.** Its log strings mention a post-animation pose-update hook, "live left/right-arm IK writeback", an "ArmDriver" with a captured rest pose, and the weapon following a `Prop01` writeback. It resolves bones by walking the skeletal mesh's ref skeleton (BoneResolver) | Concept only (never disassembled) |
| Overlord (GPL-3.0, MW2CR) | Analytic two-bone arm IK plus finger posing, on a yaw-deadzone body frame. Both arm segments are carried through one bend plane; the elbow pole is a rest bend swung with the reach | **Adapted** into `core/ik/ArmPose.cpp` since the project went GPL (NOTICE.md) |
| UE3 engine | **`SkelControlLimb`**: native two-bone IK in the AnimTree, driven by `EffectorLocation` and `JointTargetLocation` in a chosen space | Only where the AnimTree has one (see route 1) |

## Route 1 (drive the AnimTree's SkelControlLimb): out

First in-game probe (2026-10-08): Shepard's AnimTree `BIOG_AnimTree_Humanoid.BIOG_Player_O` has
three skeletal control lists, all `SkelControlLimb`, all at strength 0:
- `RightAnkle` (control `RightLeg`, effector bone `RFIK`, joint target `RightHip-Swivel`);
- `LeftAnkle` (`LeftLeg`, `LFIK`, `LeftHip-Swivel`);
- `LeftWrist` (`LeftArm`, effector space 5 = other bone, bone `LHIK`, joint target
  `LeftShoulder-Swivel`): the gun's off-hand grip.

There is no right-arm control. Adding controls means building AnimTree objects at runtime or
patching the asset, so we write the bones ourselves instead (route 2), like ME2VR.

## Why the bone queries failed (2026-10-08, from the decrypted code)

The first run logged `skeleton: 0 bones` for `HMM_ARM_MEDb_MDL` and `arm IK: head bone not
found`: `SkeletalMeshComponent.GetBoneNames` through `UObject::ProcessEvent` returned nothing.

- **LE1's ProcessEvent (`0x12DD20`)** is late UE3's. It rejects a function only if it has neither
  `FUNC_Native` nor `FUNC_Defined`, fails `IsProbing`, the object is pending kill, `iNative` is
  non-zero, or `ProcessRemoteFunction` claims it. It copies the parms into a fresh frame, builds
  `OutParms` records pointing at the **caller's** parms, and calls `Function->Func` with
  `Result = Parms + ReturnValueOffset`. There is no copy-back of the whole parms block.
- **A native called this way reads its parameters from its own bytecode**, one
  `EX_NativeParm` (0x29) token per parameter. Its handler (`0x11F210`, UE3's
  `execNativeParm`) does nothing when the native passes a null result buffer. With a buffer, it
  binds an out parameter to its `OutParms` address, and copies an in parameter from the frame.
- **`execGetBoneNames` (`0x594950`)** zeroes `GPropAddr`, steps its out-array parameter with a
  **null** result (UE3's `P_GET_TARRAY_REF`), then writes into `GPropAddr` if set, else into a
  local temporary. Through ProcessEvent `GPropAddr` stays null, so the names go into the
  temporary and are freed. The caller's array stays empty. No crash, no error.
- **So any native with an `out array<>` parameter returns nothing through ProcessEvent.**
  Natives that return through `Result` (`MatchRefBone`, `GetBoneLocation`, `GetBoneMatrix`) or
  take plain inputs work. That includes `SceneCaptureComponent.SetFrameRate`, so the
  reflection fix is fine: the log shows `raised 1, failed 0` when the second capture appeared,
  and later passes skip captures already at 1000. Halcyon's `SetHidden`, `HideBoneByName` and
  `SetOwnerNoSee` calls take plain inputs too.
- **In-process confirmation [verify on the next run]:** the probe logs one `GetBoneNames
  through ProcessEvent returned N names` line. It also prints the native's address, flags and
  `iNative`, its first bytecode bytes (expect `29 ...`), and `GNatives[0x29]` and
  `GNatives[0x48]` (expect `0x11F210` and `0x122050`).

**Fix:** read the skeleton from memory instead of asking script. The offsets below were each
confirmed in the code that uses them.

| Data | LE1 layout | Evidence |
|------|------------|----------|
| `USkeletalMesh::RefSkeleton` | `+0xB4` (SDK: declared `TArray<int>`, really `TArray<FMeshBone>`) | `execGetBoneName` `0x594870`: `RefSkeleton.Data[i*0x50]`, bounds `[mesh+0xBC]` |
| `FMeshBone` | `0x50` bytes: `Name` `+0x00` (8-byte SFXName), `Orientation` `+0x10`, `Position` `+0x20`, `ParentIndex` `+0x40` (root: 0) | `execGetParentBoneIndex` `0x594E70` reads `+0x40`; `execGetBoneNames` loops with stride `0x50`; `UpdateSkelPose` reads `+0x20..0x28` of bone 0 for root motion |
| `USkeletalMeshComponent::SpaceBases` | `+0x2B0`, **`TArray<FBoneAtom>`, 0x20 bytes**: rotation (x, y, z, w) `+0x00`, translation `+0x10`, scale `+0x1C`, component space. The SDK's `TArray<FMatrix>` is wrong | `UpdateSkelPose` sizes it with element size `0x20` and fills identity atoms; `GetBoneMatrix` (`0x583540`) indexes it `<< 5` and builds a matrix from quat, translation and scale |
| `USkeletalMeshComponent::LocalAtoms` | `+0x2C0`, `TArray<FBoneAtom>` (parent space) | `UpdateSkelPose` |
| `UPrimitiveComponent::LocalToWorld` | `+0xC0`, `FMatrix` (UE3 row-vector layout) | `GetBoneMatrix` multiplies by `[comp+0xC0..0xFC]` |
| `ParentAnimComponent` / `ParentBoneMap` | `+0x304` / `+0x30C` | `GetBoneMatrix` reads the parent's SpaceBases through the map |

`engine/Skeleton` reads these, LE1 only. Before the first write to a mesh, `game/ArmIk` checks
our reading of the head and hand bones against the engine's own `GetBoneLocation` (component and
world space, through ProcessEvent). On a mismatch it doesn't write.

## Route 2: write the arm bones after animation (built 2026-10-08)

### The hook point: `USkeletalMeshComponent::UpdateSkelPose`, LE1 `0x592440`

It's a `.pdata` function start (`tools/verify_rvas.py`). The arguments are `(this, FLOAT
DeltaTime, UBOOL bTickFaceFX)`; callers also set r9, which it never reads.

What it does (disassembly of the decrypted code dump):
1. Without a `SkeletalMesh` it sets `bBioComputedValidPose` and returns.
2. It sizes `SpaceBases` and `LocalAtoms` (and a byte array at `+0x6D8`) to `RefSkeleton.Num()`
   with 0x20-byte elements.
3. With `bNoSkeletonUpdate` or a `ParentAnimComponent`, it fills the reference pose and stops.
4. Otherwise it blends the AnimTree (`Animations` `+0x26C`, virtual `+0x358`) into `LocalAtoms`
   and handles root motion.
5. It calls `ComposeSkeleton` (`0x580A60`, only caller): `LocalAtoms` become `SpaceBases`, with
   the skeletal controls applied.
6. Then FaceFX, `0x641130`, and the rigid-body joint motors.

Callers:
- the per-frame one, in `USkeletalMeshComponent::Tick`, at `0x58D709` with the frame's
  `DeltaTime`. It's followed by the transform and attachment update (`0x2C9120` / `0x2C7C90`),
  which pushes the pose to the render thread and moves attached components;
- about 24 others with `DeltaTime = 0`: Attach (`0x579340`), reattach and force-update paths.

After it returns, `SpaceBases` is the final pose for that update. Everything that shows it reads
it afterwards: the render data update, attachments and sockets (`GetBoneMatrix`). So a
**post-hook** that rewrites bones in `SpaceBases` changes what's rendered and what's attached to
the hand, without touching animation or gameplay.

Our hook (`engine/BonePose`):
- compares `this` with the published player mesh (one atomic load for every other mesh);
- copies the request under a lock, re-checks the bone count, the pose size and the chain bones'
  names;
- solves and writes, inside SEH. An access violation stops writes for good;
- is installed the first time arm IK is turned on, and removed (waiting for in-flight calls)
  before the mod unloads;
- LE2/LE3: no address yet. It logs and stays inert.

### How bones are written, and why children stay correct

`SpaceBases` holds **component-space** transforms. A bone's skinning matrix is derived from its
own entry alone, so moving a bone without moving its children would tear the mesh. That's why
"rewrite the hand bone" isn't enough: fingers, twist bones and anything on the hand must move
with it.

`core/ik/PoseArm` works on the whole pose (`ik::Transform` per bone, parents before children, as
UE3 orders RefSkeleton):

1. **Lengths come from the current pose:** upper arm = |elbow − shoulder|, forearm =
   |wrist − elbow|. The shoulder (upper-arm joint) stays where the animation put it.
2. **Two-bone solve** (`SolveTwoBone`): the elbow sits in the plane of shoulder, target and
   pole. The pole is a rest bend (out to the side and down) swung from body-forward to the reach
   direction (Overlord's `elbow_pole`), so it has no singularity for front, side or overhead
   reaches. Out of reach: a straight arm pointing at the target. No stretching.
3. **Segment rotations:** each segment's delta maps its animated frame (direction + bend-plane
   normal) onto the solved one (Overlord's `orient_arm`). Both segments share the plane, so the
   elbow turns only about its animated hinge axis (tested). A straight animated arm falls back
   to the upper arm's own axes.
4. **Every bone below the upper arm is tagged** with the segment it hangs from: upper arm,
   forearm or hand, found by one pass in index order. Each moves **rigidly with its segment**:
   `new = pivot' + Δ·(old − pivot)`, rotation `Δ·old`. So each keeps its whole transform relative
   to its segment, the animated finger pose included. That's equivalent to recomposing the
   subtree from parent-relative transforms, but taken from `SpaceBases`, so skeletal-control
   results on the children survive.
5. **The hand:** with a target rotation it takes that rotation (fingers follow). Without one it
   keeps its animated pose relative to the forearm.
6. **Transactional:** any non-finite value or invalid chain writes nothing. Re-solving a solved
   pose changes nothing (tested), which matters because UpdateSkelPose can run more than once a
   frame or resubmit the previous pose.

`LocalAtoms` aren't written: nothing after ComposeSkeleton rebuilds `SpaceBases` from them in a
normal frame. If ragdoll or physics blending ever does, our arms would be overridden for that
frame, not broken.

### Bones and targets

- **Chains** (`ik::FindArmChain`): the hand is found by name (`HandSide`: hand/wrist plus a side
  token). Then come its nearest ancestors named like a forearm (`elbow`, `forearm`, `lowerarm`)
  and an upper arm (`shoulder`, `upperarm`, `arm`). Roll, twist, swivel and IK helpers are
  skipped, e.g. ME's `LeftShoulder-Swivel`. That handles ME (`RightShoulder` > `RightElbow` >
  `RightWrist`, expected), Mixamo and Biped names (tested). Unfamiliar names fall back to the
  nearest non-helper ancestors. Logged once per skeleton asset.
- **Hand targets** (`game/HandTargets`, one function, about to change for Halcyon's renderer):
  - today: OpenXR grip poses (LOCAL), placed through `head_tracking::Anchor()` with
    `engine::ToWorld`, the wrist 7 cm behind the grip along grip +Z [verify in headset];
  - orientation via `engine::ToUnrealOrientation`;
  - `DesktopTest` when no VR session runs.
- **To component space** (`game/ArmIk`): through the inverse of the mesh's `LocalToWorld`. The
  body axes come from the pawn's yaw.
- **Hand rotation in VR is relative for now:** when a hand starts following, the offset between
  the controller and the animated hand is captured, and from then on the hand turns with the
  controller. A calibrated grip-to-hand offset comes later.
- **Desktop test:** with no VR session, `.` turns arm IK on and holds the right wrist in front of
  the chest at shoulder height. The target is computed in the hook from the live shoulders:
  midpoint + (0.70 forward, 0.15 outward, −0.05 up) × arm length.

### Logging (first in-game run)

- `arm IK: UpdateSkelPose at ... (entry bytes ...)`, then `hooked ...`.
- Probe: the GetBoneNames diagnostic, every bone with its parent, both arm chains, and key bone
  positions from SpaceBases.
- `arm IK: skeleton ... left arm A(i) > B(j) > C(k); right arm ...; head ...`.
- `arm IK: layout check <bone> component|world: SpaceBases (...) vs GetBoneLocation (...):
  match`, then `bone layout verified`.
- Status changes, e.g. `driving: right hand held in front of the chest (desktop test)`, or why
  not.
- `arm IK: first write, right arm (component space): target, wrist before -> after (distance
  from target), elbow before -> after, shoulder, lengths, bones written`.
- Heartbeat: UpdateSkelPose calls in total and for the player mesh (callers, threads), writes,
  rejections with the last reason, and the latest before/after per arm.

## Open risks [verify in game]

- The desktop-test pose must appear on screen. If the log shows writes but Shepard's arm doesn't
  move, something after UpdateSkelPose rebuilds `SpaceBases` (or the render update ran before
  it): look for a second hook point (the `0x2C9120` update, or ComposeSkeleton itself).
- UpdateSkelPose from another thread: the hook is thread-safe, and the log lists thread ids.
- The weapon should follow the hand (an attachment via sockets, updated after the pose).
  Unknown until seen.
- The off-hand: the AnimTree's `LeftArm` limb control pulls the left wrist to `LHIK` during
  ComposeSkeleton, before our write. In the desktop test the right hand moves but the left stays
  on the gun's old grip. In VR both arms are ours.
- Scale: 1 unit = 1 cm is assumed for the 7 cm wrist offset; the log's arm lengths will tell.
- The VR hand rotation is relative (captured at the start), not calibrated.

## Plan

1. **Next run:** desktop test (notes/next-vr-session.md). Confirm bone names and chains, the
   layout check, writes, and the visible pose.
2. **VR:** arms follow the controllers in third person. Then once Halcyon's renderer owns the
   camera: swap `hand_targets::Current()` to its camera transform.
3. **First-person body:** MassFPS-style head hiding (research/11). The torso turns with a body
   yaw (deadzone, `ik::BodyYaw`).
4. **Weapon in hand:** check the weapon attachment follows the IK'd hand; then the aim ray
   from the muzzle unifies hand aim with what you see.
5. **Comfort and polish:** grip-to-wrist calibration (a "hold arms out" step), arm length
   scaling, elbow pole smoothing, limits for cover and roadie-run animations.

## Earlier (route 1 driver, superseded 2026-10-08)

`game/ArmIk` first drove the AnimTree's `SkelControlLimb`s: effector = controller, joint target =
`ik::ElbowPole`, strength 1, with the controls' settings saved and restored. The probe found no
right-arm control, so it was replaced by the route 2 driver above. `ik::EstimateUpperBody`,
`ik::ElbowPole` and `ik::BodyYaw` remain, tested, for the body-yaw step.
