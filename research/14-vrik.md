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
- **Hand targets** (`game/HandTargets`, one function):
  - the grip poses (tracking space) are turned into the world by `game/view_anchor`, the one
    tracking-to-world mapping for whichever renderer draws VR (Halcyon's: its published view on
    the game camera, `engine::FromCameraView`; ours: the body yaw, `engine::FromBodyYaw`);
  - positions are measured from **tracking space's origin**, not the live head
    (`engine::AtOrigin`, unit-tested in `TestHandFrame`). The origin is where the head was at
    the start or the last recenter (OpenXR LOCAL; Halcyon's recenter re-origins its app space at
    the head), and it is placed at **Shepard's head bone** (world, from SpaceBases) rather than
    the camera, so it works in third person (as route 1 did). Measured from the live head,
    leaning moved the hands the wrong way (0.2 m forward with still hands: the view moved ~30
    units forward, the hands ~20 back). Now leaning or 6DOF moves only the view. Without a head
    bone, the origin goes where the view puts it (`engine::OriginWorld`). Since the reach work
    (below): the view's own scale, and in first person the view's own frame;
  - orientation: the grip's axes through the same anchor (`engine::OrientationToWorld`);
  - the wrist is 7 cm behind the grip along grip +Z [verify in headset];
  - `DesktopTest` only where no headset can be followed, decided every tick by the mod: our
    renderer with no VR session, or Halcyon's before any headset session has shown up (no view
    or controller sample ever). A view lost for a moment (menu, flat conversation, tracking
    blip, hot reload) leaves the arms to the animation instead.
- **To component space** (`game/ArmIk`): through the inverse of the mesh's `LocalToWorld`. The
  body axes come from the pawn's yaw.
- **Hand rotation in VR is relative for now:** when a hand starts following, the offset between
  the controller and the animated hand is captured, and from then on the hand turns with the
  controller. A calibrated grip-to-hand offset comes later.
- **Desktop test:** with no headset (see above), `.` turns arm IK on and holds the right wrist in
  front of the chest at shoulder height. The target is computed in the hook from the live
  shoulders: midpoint + (0.70 forward, 0.15 outward, −0.05 up) × arm length.
- **Which mesh gets the write:** the hook matches the published request by component address,
  then also by the component's owner actor, by GWorld and by age (published within 500 ms). A
  level load runs no mod tick, and a new component with the same skeleton can land at a freed
  address; the owner and world checks reject that (logged as a rejection).

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

## The gun in the hand: muzzle, barrel and shots (2026-10-08, decrypted code)

Headset feedback: holding the controller normally, the gun pointed about 90 degrees up, and
"the shots are not corresponding to the gun model". The first is most likely the grip-to-hand
mapping: `core/ik/HandFrame` maps the hand's wrist-to-knuckles axis onto OpenXR grip -Z, which
the spec puts along the tube the closed fingers make (pinky to thumb, the controller's handle),
about a right angle from where a fist's knuckles and a gun's barrel point. The second: hand aim sent shots
along the controller's aim ray from the game's own start, while the gun model pointed wherever
the hand put it. Fix: the gun decides. The hand turns so the barrel follows the controller's aim
ray, and shots leave the muzzle along the barrel.

### How LE1 fires (read in the dump)

| What | LE1 | Evidence |
|------|-----|----------|
| Fire tick: `BioWeaponRanged.TickFire`'s body (**confirmed** in [research/17](17-ghidra.md): the native `execTickFire` `0xB72660` calls vtable slot `0x8A0`, which holds it) | `0xC621E0` (this, DeltaTime) | Counts down `FireDelay`/`ReFireDelay` (`+0x4F0`/`+0x4F4`), sets the firing bits at `+0x548`, branches on `Stats.FireMode` (`Stats +0x4C0`, mode `+0x259`: 1 single, 3 burst with `RemainingBurstFireCount +0x4EC`, else automatic; another path `0xC62650` when a check on the instigator (`0x4A7550`) passes and byte `+0xDF4` of what the weapon's virtual `+0x890` returns is 3 or 4). Every firing path does the next three steps |
| Shot start | `Instigator.GetWeaponStartTraceLocation(None)` **through ProcessEvent** (vtable slot `0x250`; the event thunk `0xC63C10`, parms `CurrentWeapon +0x0`, `ReturnValue +0x8`) | The caller reads the return value back from the parms, so a ProcessEvent post-handler can rewrite it. For the player it's `SFXGame.BioPawn.GetWeaponStartTraceLocation` (native; `SFXPawn_Player` doesn't override it) |
| Shot aim | `this.GetAdjustedAim(start)` through ProcessEvent (parms `StartFireLoc +0x0`, `ReturnValue +0xC`), whose script asks `BioPlayerController.GetAdjustedAimFor` (script to script: `UObject::CallFunction`) | Matches the in-game log of 2026-10-08: the GetAdjustedAim post-handler saw `StartFireLoc` 59 units behind the hand, so the pawn's answer is about the camera, not the muzzle |
| The trace | `0xC589F0(this, &start, &aim)`: end = start + `Vector(aim)` × `GetTraceRange()` (`0xC5C9D0`), pellets from `Stats.PelletSpread` (`+0xE4`, count `+0xEC`), `ShotCount++` (`+0x4DC`) | `Weapon.GetPhysicalFireStartLoc` isn't on this path |
| `BioWeaponRanged::GetMuzzleLoc` (C++) | `0xC59870` | `SkelMeshComp` (`BioWeapon +0x3E8`) and `m_MuzzleSocketName` (`+0x490`, not None): `SkelMeshComp->GetSocketWorldLocationAndRotation(name, &loc, &rot, 0)`; otherwise `Instigator` (`+0x1A4`)'s `Location` (`+0x108`), else its own |
| `USkeletalMeshComponent::GetSocketWorldLocationAndRotation` (C++) | `0x584640` | `SkeletalMesh` (`+0x254`) → `FindSocket` (`0x5A8C70`) → `MatchRefBone(socket.BoneName)` (`0x587D30`) → `GetBoneMatrix` (`0x583540`) → `FRotationTranslationMatrix(RelativeRotation, RelativeLocation)` (`0xBF810`) × bone matrix. **No `RelativeScale`.** Space 1 multiplies by the inverse `LocalToWorld` (`+0xC0`); the rotator comes from `0x145230` |
| `USkeletalMesh::FindSocket` | `0x5A8C70` | `Sockets` data `+0x144`, count `+0x14C`; compares `SocketName` `+0x84` (index) and `+0x88` (number) |
| `USkeletalMeshSocket` | `RelativeLocation +0x60`, `RelativeRotation +0x6C`, `RelativeScale +0x78`, `SocketName +0x84`, `BoneName +0x8C` | `0x584640`, `0x57A120` (SDK agrees; `static_assert`ed) |
| `BioWeapon.AttachToMesh` (native) | `0xC3EC70` | With `SkelMeshComp` and a socket name: `MeshComponent->AttachComponentToSocket(this->Mesh (Weapon +0x394), SocketName)` (`0x57A120`), then `AttachedMesh +0x3F8` = the body, `AttachedBone +0x400` = **the socket's name** (not a bone), `AttachPawn +0x408` and `Instigator` = the body's owner |
| `AttachComponentToSocket` | `0x57A120` | `FindSocket`, then `AttachComponent(component, socket.BoneName, &RelativeLocation, &RelativeRotation, &RelativeScale)` (`0x5795F0`) |
| `SkeletalMeshComponent.Attachments` | data `+0x3B4`, count `+0x3BC`, `FAttachment` `0x34` bytes (`Component +0x0`, `BoneName +0x8`, `RelativeLocation +0x10`, `RelativeRotation +0x1C`, `RelativeScale +0x28`) | A transform update (`0x593110`, by its shape) loops `imul 34h` over them, calling `BeginDeferredUpdateTransform` (`0x2C7C90`) on each component |
| After `UpdateSkelPose` in `Tick` | `0x58D70E`: with `bForceUpdateAttachmentsInTick` (`+0x438` bit `0x40000`) the transform update runs now (`0x2C9120` → vtable `+0x2D8`), otherwise `BeginDeferredUpdateTransform` (`0x2C7C90`) defers it to later in the frame | So attached components take their transforms from the bones **after** our post-hook writes them: the gun should follow the written hand in the same frame **[verify: the follow-check log line]** |

The player's skeleton has `Prop01` (98) under `RightWrist` (82), and `LHIK` (109) under `Prop01`
(the off hand's grip target), so the weapon most likely hangs on `Prop01`. The arm writer carries
every bone below the upper arm rigidly, `Prop01` included.

### What the mod does (`game/weapon`, `game/arm_ik`, `game/hand_aim`)

- **Which weapon** (`game/weapon`, LE1 only): `Pawn.Weapon` (`+0x4A8`) when it's a
  `BioWeaponRanged`; its `SkelMeshComp`; the socket `m_MuzzleSocketName`.
- **The muzzle**, computed the engine's way from memory (`engine::skeleton::FindSocket`): the
  socket's offset, on its bone in the weapon's `SpaceBases`, through the weapon mesh's
  `LocalToWorld`. **Verified once per weapon** against `GetMuzzleLoc` called through ProcessEvent
  (a plain return value, which works); more than 1 unit apart: no weapon, logged (fail closed).
  The same pass calls `GetSocketWorldLocationAndRotation` once and logs whether its struct out
  parameters came back through ProcessEvent (`H17`) **[verify]**.
- **The barrel** is the muzzle socket's X axis (the engine spawns muzzle effects with the
  socket's rotation), accepted if it's within 60 degrees of the direction from the weapon's
  origin (its grip) to the muzzle; failing that, the weapon mesh's X axis; failing both, no
  weapon. Its up is the same frame's Z. **[verify: the `barrel:` log line]**
- **Held in the right hand**: the weapon mesh is in the body's `Attachments` on a bone at or
  below the right hand (`ik::FindArmChain`).
- **The grip**: muzzle, barrel and up in the right hand bone's own axes, measured from the world
  while the hand holds still (moved under 0.5 units and 1 degree per tick for 5 ticks that agree),
  so it doesn't matter whether the weapon updates before or after the hand is written. Kept per
  weapon; a still measurement more than 1 unit or 2 degrees away replaces it (logged).
- **The hand** (`game/arm_ik`): with a grip, the right hand bone's rotation is solved so the
  barrel points along the right controller's OpenXR aim ray (aim pose -Z) and the gun's top
  follows the aim pose's +Y as far as the barrel allows (`ik::HandForBarrel`, unit-tested). The
  gun's top is the **hand's thumb side**, not the model's up (see "Gun roll" below). No grip yet,
  or no weapon: the old grip mapping (`HandFrame` plus the `[Hands]` trims). The wrist still goes
  where the grip pose puts it.
- **Shots** (`game/hand_aim`), only while arm IK points the gun this tick and the writer is
  writing the pose (`arm_ik::RightHandAimsGun`): `GetAdjustedAimFor` returns the barrel's
  direction, and a ProcessEvent post-handler on `GetWeaponStartTraceLocation` (the local
  player's pawn only) moves the start to the muzzle. Both read the weapon live at the shot. The
  `GetAdjustedAim` post-handler checks every such shot's `StartFireLoc` against the muzzle.
  Otherwise the controller's ray as before (direction only).
- **The reticle's ray** (`hand_aim::AimRay`, published every tick through
  `HostServices::setAimRay`, v6): the muzzle and the barrel in tracking space, through the view
  anchor's frame inverted (`engine::ToTracking`, `DirectionToTracking`, unit-tested). `valid`
  false whenever shots don't follow the barrel (no weapon, not on foot, hand aim off, no grip yet,
  no anchor).
- **Does the gun follow the written hand?** Every tick with a written pose, the muzzle where the
  engine put it (the weapon mesh's `LocalToWorld`) is compared with where the body's current pose
  and the attachment put it (`FAttachment` offset, then the bone without scale **[verify]**), and
  with where last tick's pose put it. After 40 ticks of motion it logs once per weapon: same
  frame, one tick behind, or not at all. The heartbeat repeats the error.
  - Expected: same frame (the table's last row).
  - If it lags: in the bone writer's post-hook, after writing, call
    `BeginDeferredUpdateTransform` (`0x2C7C90`) on each component in the body's `Attachments`,
    as the engine's own transform update does. Not done blind: the log decides.

### Gun roll: from the hand, not the model (2026-10-08)

Headset feedback: the gun was upside down. The roll came from the weapon model's axes (the muzzle
socket's Z), and those disagree between weapons. The 15:56 log measured two grips in the right
hand's axes with the same barrel `(0.19, -0.965, -0.18)`: the pistol's up `(-0.160, 0.151, -0.975)`
and the rifle's `(0.968, 0.214, -0.130)`, 90 degrees apart. Against the hand's own frame (from the
logged finger bases), the pistol's model up is about 180 degrees from the thumb side (upside down) and
the rifle's about 90 (on its side) (`TestGunHold`, on those numbers).

**Fix** (Overlord's rule: the controller decides, the gun is the master and the hand follows):
- The gun sits at a fixed pose on the controller: the barrel along the aim pose's forward, its top
  along the aim pose's up, so its roll follows the wrist. Without an aim pose, the forward of the
  **Touch reference frame** (Overlord's `touch_controller_reference.hpp`: from the OpenXR grip,
  translation (+/-7, -1.83, 101.9) mm and a 20.6 degree right-handed X rotation, inverted;
  `core/ik/TouchReference`, its forward is the grip's pitched 20.6 degrees down).
- Which way is the gun's top in the hand: the hand's **thumb side** (anatomical +Y, pinky
  knuckle to index knuckle, from the finger bases) made perpendicular to the barrel
  (`ik::GunUpInHand`). A hand holds every gun with the top of its grip toward the index finger
  and thumb, so this is the same for every weapon (the pistol and the rifle come out less than 1
  degree apart). The model's up is only logged.
- `[Hands] gunRoll` (degrees, `<GAME>MEVR.ini`, live) rolls the gun about its barrel if it still
  looks rolled in the headset **[verify]**.
- The muzzle and barrel measurement, shots from the muzzle and the reticle's ray are unchanged
  (they use the barrel and the muzzle, never the up).
- Log: `arm IK: gun held thumb side up, roll trim 0.0 degrees: up in the hand's axes (...); the
  weapon model's up is N degrees from it (ignored)`, once per weapon (expect about 180 for the
  pistol, about 90 for the rifle).

### What the log shows

- At startup: `weapon: natives: BioWeaponRanged.GetMuzzleLoc exe+0x..., TickFire exe+0x...`
  (ties the code reading to the script functions). Seen 2026-10-08: `exe+0xB72D30`, `exe+0xB72660`,
  `GetWeaponStartTraceLocation` `exe+0xB939E0`, `GetSocketWorldLocationAndRotation` `exe+0x5740C0`;
  each `exec` thunk calls the C++ body through a vtable slot ([research/17](17-ghidra.md)).
- Per weapon: `weapon: <weapon> (mesh ...): muzzle socket '...' on bone ...: muzzle (...) vs
  GetMuzzleLoc (...): match`, the `GetSocketWorldLocationAndRotation` line, `weapon: barrel: ...`,
  `weapon: ... in the right hand: its mesh hangs on Prop01(98) under RightWrist(82)`,
  `weapon: grip measured in the right hand's axes: ...`, later
  `weapon: follow check, attached to ...: ... follows the written hand in the same frame`.
- `arm IK: driving: ... (the right hand points the gun's barrel along the controller's aim)`.
- `hand aim: shots follow the gun barrel from the muzzle (...)` or `... the right controller's aim
  ray (why not the barrel)`; on the first shot: `shot start moved to the muzzle: the pawn said
  (...), the muzzle is at (...)`, `first shot along the gun barrel`, `the shot started at the
  muzzle`.
- `hand aim: aim ray from the muzzle along the barrel` / `none (why)`, and the first ray in
  tracking space.

## Reach: shoulders under the headset, hands on the controllers (2026-10-08)

Headset feedback: with the arms fully out, Shepard's hands stopped short of the controllers. The
15:55 log's first writes ended "out of reach" by 20 units, from shoulders where the combat stance
put them, with fixed arm lengths (upper arm 28.9 + forearm 26.3).

### Why the first aim ray was 2.12 m from the head

`hand aim: first aim ray (tracking space): muzzle (0.201, -2.141, 0.061) m, 2.12 m from the head`
(15:56:18). The reticle's ray maps the muzzle back through the **view's** frame, which Halcyon
publishes at **50 units per meter** (`halcyon bridge: first world view: ... 50.0 units per meter`;
its lean and its 1.6-unit half-eye, research/13). The hands had gone into the world at **100**
(`engine::kUnitsPerMeter`), measured from Shepard's head bone. So every hand offset came back
doubled: the controller hung about 0.75 m below the head, plus the pistol (muzzle 36.8 units from
the wrist, 0.37 m at 100) is about 1.1 m, which reads as 2.1-2.2 m at 50. The direction
`(-0.098, -0.967, -0.234)` (straight down) fits a hand hanging during the "hold still" grip
measurement. `TestReachScale` reproduces the doubling and checks the fix. Second, smaller cause:
the head bone and the view's eye point aren't the same point.

**Fix** (`game/HandTargets`):
- Hands (and the headset, for the torso) go into the world at **the view's own scale**
  (`TrackingFrame::unitsPerMeter`, Halcyon's 50), or `[Hands] trackingScale` if set: the scale
  at which the player sees the world, so a hand lands where the player sees the controller.
- **First person** (the view within 45 units of Shepard's animated head bone; back to third
  person beyond 75): the view's own frame, so the controllers are exactly where the player sees
  them. **Third person**: tracking space's origin at Shepard's **animated** head bone, as before.
- The anchor is the head bone **before our writes** (`bone_pose::AnimatedHead`, kept by the
  writer from the freshly animated pose): the spine writes move the head, and an anchor read
  after them would chase its own writes.
- The wrist offset behind the grip is now 7 of Shepard's units (it's his hand), not 7 cm of
  tracking space.
- Log: `arm IK: body anchor: first person (the view's frame), view N units from Shepard's
  animated head bone, 50.0 units per meter`; the first-ray line now also gives the muzzle's
  distance from the right grip (expect about the gun's length at the view's scale, 0.5-0.8 m for
  the pistol) **[verify]**.
- Consequence: at 50 units per meter Shepard is about twice the player's size as seen, so a
  player's 0.6 m reach is 30 units, about half of his arm. The hands are on the controllers, the
  elbows bent. The arm-length calibration (below) trades that back if wanted. **[verify in
  headset which feels right; `trackingScale = 100` restores the old scale.]**

### Shoulders from the headset (`core/ik/TorsoPose`, `engine/BonePose`)

Overlord rebases its weapon arms at the view anchor; we bend Shepard's spine instead (his body is
on screen):
- **Chain** (`ik::FindTorsoChain`): the top of the spine is the nearest common ancestor of the
  upper arms (LE1: `Chest2`); below it, ancestors named like a spine (`Chest1`, `Chest`,
  `LowerBack`; stops at `Root`); clavicles are the bones between the top and each upper arm
  (`LeftCollar`, `RightCollar`). Logged: `arm IK: torso: spine LowerBack(2) > Chest(3) >
  Chest1(4) > Chest2(5); clavicles LeftCollar(6) and RightCollar(7); eyes Eye_Left(52) and
  Eye_Right(51)` **[verify]**.
- **Goal**: the reference point (Shepard's eyes in first person, his head bone in third) goes to
  the headset; the shoulders face the body's heading. The heading is the headset's yaw through
  `ik::BodyYaw` (34 degree deadzone, slow recenter), pulled halfway toward the hands when both
  are tracked, at least 15 cm out and within 100 degrees of the face.
- **Solve** (`ik::PoseTorso`): the upper body turns rigidly about the bottom spine joint, twist
  about the body's up (at most 60 degrees) then the lean that points the reference at the
  headset (at most 35 degrees), spread over the spine joints (each turns about its own position
  and carries everything above it, three passes). Rotation only: no bone stretches, the
  reference keeps its distance from the pivot, and a crouch or a taller player is left to the
  arms. Unit-tested on a synthetic rig (reachable goal, limits, identity, failures, rigidity).
- **Clavicles** (`ik::PoseCollar`): a hand target beyond 85% of the arm's length turns the
  clavicle toward it by half the angle, at most 15 degrees (reaching up or forward shrugs).
- Written in the same post-`UpdateSkelPose` pass, before the arms, which then reach from where
  the shoulders ended up; the elbow poles use the new facing. Transactional like the arms.
- `[Hands] bodyFollowsHead = 0` turns it off (live).
- Log: `arm IK: first write, torso (component space): shoulders (...) -> (...) (wanted (...),
  N short, limited), twist N deg, bend N deg, clavicles N/N deg, arm stretch N/N, N bones`, and
  the same in the heartbeat.

### Hands always on the controllers: stretch

`ik::PoseArm` lengthens both segments by `clamp((distance + .001) / (upper + lower), 1, max)`,
`max` = `[Hands] armStretch` (default and limit 1.25, Overlord's `max_arm_stretch_ratio` in
`hands/pose_solver.hpp` `solve_limb`). The mesh stretches between the bones; bones hanging from
the upper arm (twist bones) keep their offsets from the shoulder. Unit-tested.

### Arm-length calibration

- The VR hands tab (host, `src/plugin/MevrMenuTab.cpp`) samples a T pose into `LE1MEVR.ini`
  `[Arms]`: `measureHead/Left/RightX/Y/Z` (tracking space, meters) and `measureRequest` (a
  counter, written last). `game/HandCalibration` re-reads the file within a second; each new
  `measureRequest` is measured (`ik::ArmLengthFromTPose`: shoulders on the grip line, 18 cm either
  side of the head's foot point; each arm is its grip's distance from its shoulder; rejected if
  not a T pose or outside 0.3-1.2 m) and saved as `[Arms] armLengthM` (shown by the tab).
  A request already in the file at startup, with `armLengthM` set, is not re-measured.
- With `armLengthM` set, each wrist target's distance from the (moved) shoulder is scaled by
  Shepard's arm length / (`armLengthM` x units per meter), within 0.5-3: the player's full reach
  is Shepard's full reach. 0 or absent: no scaling (hands on the controllers).
- Function API for code: `hand_calibration::MeasureArmLength(ik::ArmLengthSample)` (measures,
  saves, applies; nullopt if not a T pose), `ClearArmLength()`, `ArmLength()`.
- Logs: `hands: arm length 0.630 m (left ..., right ..., shoulder to grip, from a T pose); saved as
  [Arms] armLengthM`, or `hands: arm-length sample #N isn't a T pose (...)`.

## Two-handed grip = focused fire (built 2026-10-08, not tested in the headset)

The plan agreed with the user (notes/todo-vrik.md): the left grip squeezed on the gun's
foregrip holds the gun with both hands; while held the game sees LT (aimed fire's accuracy bonus)
and the barrel points between the hands. One-handed is hip fire.

**Prior art: Overlord** (GPL-3.0; NOTICE.md). `gameplay/support_grip.hpp`: a lease that only a
*new* squeeze within `acquire` takes, released when the grip opens or the hand passes `release`
(defaults 0.10 / 0.22 m, `weapon_profile.hpp` `profile_defaults`). `grip_presenter.hpp`: the
distance is checked at the one-hand pose to acquire and at the solved two-hand pose to retain
(a held rifle must not drop when the rear wrist turns); hands closer than 0.08 m count as
released; a 0.1 s blend. `weapon_profile.hpp` `aimed_rotation`: the rear hand's rotation swung by
`from_to(rear * span, support - rear)`, which keeps the rear hand's roll with no world-up
singularity.

**Where the foregrip is (`game/support_grip`, per gun, in the right hand bone's axes).** The
first candidate that lies on the gun (`ik::OnGun`: not on the right hand, between 20 units behind
the grip and 5 past the muzzle, within 20 of the barrel's line) [verify thresholds]:
1. a socket of the weapon's mesh whose name suggests the off hand (`game/weapon` logs every socket
   name once per weapon: we don't know LE1's names yet [verify]);
2. the body bone `LHIK` (109, under `Prop01` under `RightWrist`): the goal of the AnimTree's
   `LeftArm` limb control. It hangs below the right hand, so our right-arm write carries it and
   its place in the hand's axes is the animation's either way;
3. the animated left hand (the pose before our writes, `engine::bone_pose::AnimatedHands`);
4. otherwise under the grip (`ik::UnderGrip`: 8 units below the thumb side, 2 forward), for
   pistols [verify where Shepard's pistol stance puts the left hand].
It settles after 5 ticks within 0.5 units and isn't moved while held. The left hand's rotation on
the gun comes from the animated left hand when that is within 4 units of the point, else from the
controller. A span with less than 10 units along the barrel (the pistol's cup) doesn't define the
barrel: the right hand keeps aiming. Once per gun the log says how far the animated left wrist is
from LHIK, i.e. whether the AnimTree's off-hand IK actually holds it there (the probe found the
limb controls at strength 0 outside combat) [verify].

**Left arm: solved to the foregrip, not left to the native LHIK.** By the code: the `LeftArm`
limb control runs in ComposeSkeleton, inside UpdateSkelPose, **before** our post-hook moves the
right arm (and with it the gun). Skipping our left-arm write would leave the left hand where the
animation had the gun, not on the gun we moved. So while held the writer puts the left wrist on
the foregrip as written: `right wrist goal + rotation * span` (logged with the foregrip).

**The writer (`engine::bone_pose`, `Request::support`).** Both arm goals are computed first
(clavicles, reach scaling), then `ApplySupport`: the left goal's distance from the foregrip, in
meters of left-controller motion (written units / (view units per meter x the left arm's reach
scale)), at the one-hand pose, or the two-hand pose while held; hands closer than 0.08 m report
99 m (release). With `blend` > 0 the right hand turns toward `ik::TwoHandRotation` (slerped by the
blend) and the left wrist moves to the foregrip (lerped). Shots and the reticle need nothing new:
`game/hand_aim` reads the muzzle and barrel from the gun model, which follows the written hand.

**The pad (`game/GamepadMapping`, unit-tested).** `UpdateSupportGrip` runs on the left grip's
latch: a new squeeze within `acquire` = Support (no LB; LT forced to 255), else Shoulder (LB, as
before) for the whole squeeze; Support lost by distance or the gun = nothing until the grip opens
(no surprise LB, which opens LE1's power wheel). Left grip + menu stays Back. The pad is built
in the host's XInput hook with Halcyon's renderer, so the mod sends its distance and settings
each tick through **HostServices v7** (`setSupportGrip(distance, acquire, release, flags)`, read
back with `supportGripHeld()`; stale after 250 ms: defaults). With our renderer (LE2/LE3)
`game/gamepad_bridge` runs the same mapping in the mod. **A host change: redeploy.**

**Settings** (`ASI\LE1MEVR.ini [Controls]`, appended to older files, live): `supportGrip`,
`supportAcquireM = 0.12`, `supportReleaseM = 0.22`, `sniperAims = 0` (a sniper rifle's LT is
the game's scope camera, which Halcyon's first person leaves to the game: no LT by default
[verify]), `leftTrigger = lt | none` (default `lt`: nothing changes until the user picks a use).

**Does LT zoom under Halcyon?** By the code, no: in gameplay (not a flat cinematic) Halcyon's
`xr_session.cpp` calls `RenderHook::SetTargetFovHalfAngles(headset FOV x fill,
vrFovFillEnabled)` every frame (`vrFovFillEnabled` defaults on, and the release `MELEVR.ini`
doesn't change it), and `render_hook.cpp` `ApplyTargetFovOverride` rewrites the scene view's
projection after CalcSceneView (`[RENDER_FOV_MATCH] applied ...`); the compositor is told the same
FOV. So the narrower FOV of `SFXCameraMode_TightAim` never reaches the projection. Its first-person
offset (`fpCombatAim` -70/19.8/20.0) is within 1 unit of `fpCombat` (-70/19.9/21.0). The sniper
mode is not first-person-written (`FpStateForClass`: "leave the game's scope view"), hence
`sniperAims = 0`. [verify in the headset: no zoom, no camera jump when the grip takes hold.] If
it did zoom, the minimal fix would be a marked seam forcing `SetTargetFovHalfAngles` on while the
support grip holds, not a change to Halcyon's camera code.

Logs: `weapon: <weapon> (<type>); mesh sockets: ...; off-hand socket: ...`; `arm IK: torso: ...;
off-hand IK goal LHIK(109)`; `support grip: candidates ...`, `support grip: foregrip set from
...`; `halcyon bridge: support grip taken / let go`; `support grip: held ... / released ...`;
heartbeat `support grip: ...`; `controls: two-handed grip on (acquire 0.12 m, release 0.22 m,
...), left trigger = LT`.

## Open risks [verify in game]

- The desktop-test pose must appear on screen. If the log shows writes but Shepard's arm doesn't
  move, something after UpdateSkelPose rebuilds `SpaceBases` (or the render update ran before
  it): look for a second hook point (the `0x2C9120` update, or ComposeSkeleton itself).
- UpdateSkelPose from another thread: the hook is thread-safe, and the log lists thread ids.
- The weapon should follow the hand: by the code it does, in the same frame (the follow check
  says). The barrel axis (socket X) and the attachment's scale handling are read from code, not
  seen yet.
- Shots now start at the muzzle: a muzzle poked through a wall fires from the far side. The game
  has `BioPawn.GetPulledInMuzzleLocation(Wpn, AimDir)` (a script event), probably for this; not
  used yet.
- The off-hand: the AnimTree's `LeftArm` limb control pulls the left wrist to `LHIK` during
  ComposeSkeleton, before our write. In the desktop test the right hand moves but the left stays
  on the gun's old grip. In VR both arms are ours; with the two-handed grip the left wrist is put
  on the foregrip as written (above).
- Two-handed grip: the acquire distance is in meters of controller motion through the view's
  scale and the reach scale; with Shepard about twice the player's size, the foregrip may sit
  farther out than a real rifle's. Tune `supportAcquireM` or the foregrip choice from the log.
- Scale: hands now follow the view's scale (Halcyon: 50 units per meter), so Shepard looks
  about twice the player's size and his arms stay bent at the player's full reach; see "Reach".
- Without a gun, the VR hand rotation is the grip mapping (`HandFrame`) plus the `[Hands]` trims,
  not calibrated per player.

## Plan

1. **Next run:** desktop test (notes/next-vr-session.md). Confirm bone names and chains, the
   layout check, writes, and the visible pose.
2. **VR:** arms follow the controllers in third person. Then once Halcyon's renderer owns the
   camera: swap `hand_targets::Current()` to its camera transform.
3. **First-person body:** MassFPS-style head hiding (research/11). The torso turns with a body
   yaw (deadzone, `ik::BodyYaw`).
4. **Weapon in hand:** built 2026-10-08 (above): the hand aims the gun, shots leave the muzzle
   along the barrel, the aim ray goes to the renderer's reticle. Next: confirm in the headset.
5. **Comfort and polish:** grip-to-wrist calibration (a "hold arms out" step), arm length
   scaling, elbow pole smoothing, limits for cover and roadie-run animations.

## Earlier (route 1 driver, superseded 2026-10-08)

`game/ArmIk` first drove the AnimTree's `SkelControlLimb`s: effector = controller, joint target =
`ik::ElbowPole`, strength 1, with the controls' settings saved and restored. The probe found no
right-arm control, so it was replaced by the route 2 driver above. `ik::EstimateUpperBody`,
`ik::ElbowPole` and `ik::BodyYaw` remain, tested, for the body-yaw step.

## Hand aim and the view anchor

**Hand aim (`,`):** shots follow the right controller's aim ray, on foot. It goes through a new
`UObject::CallFunction` hook (LE1 `0x12AE40`), because the weapon asks
`BioPlayerController.GetAdjustedAimFor` from script, which ProcessEvent never sees. ME2VR's
logs show the same approach on LE2. Since 2026-10-08 the controller reaches the world through
`game/view_anchor` (an `engine::TrackingFrame`), which also works with Halcyon's renderer: it
publishes its view relative to the game camera ([13](13-existing-me-vr-mods.md)). **Arm IK
should take its hand positions from the same anchor.** Hand aim sets only the direction:
shots still start at the game's StartFireLoc, so they run parallel to the controller's ray,
offset by the distance between them. The log measured it on 2026-10-08: the start was 59 units
behind the hand along the ray and 24 off it. That's now the fallback: with a gun in the hand,
shots leave the muzzle along the barrel (above, "The gun in the hand").
