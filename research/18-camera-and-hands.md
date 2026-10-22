# 18: LE1's camera, the off-hand IK and aimed fire (2026-10-21)

Three questions from VRIK ([14](14-vrik.md), `notes/todo-vrik.md`), answered from the decrypted
code (Ghidra, [17](17-ghidra.md)) and from the game's own packages (default properties read from
`SFXGame.pcc` and `BIOG_Male_Player_C.pcc`, see "Method" at the end). LE1 2.0.0.48602. Addresses
are RVAs; the names are in `tools/ghidra/symbols-le1.csv`. The `exec` addresses below were found
before the natives dump existed and then matched it exactly (`natives-LE1-60AEEFA2.csv`, written
by the game run of 2026-10-21 21:06).

## Summary

| Question | Answer | Confidence |
|----------|--------|------------|
| Why does Halcyon's first-person camera follow what we write? | The camera hangs off a **bone of Shepard's body mesh named `Camera`** (index 104 on `HMM_ARM_MEDb_MDL`, a child of `God`), read from `SpaceBases` through `GetBoneLocation`. It isn't the head bone. Commit 489ef51 wrote every bone, `Camera` included, so the view moved with the body | High (code + package defaults). Frame order from `UWorld::Tick`: the camera updates after the actors tick, same frame |
| Can we lean or move the torso and head without the camera following? | **Yes**, as long as the `Camera` bone keeps its animated transform. The torso subtree (`LowerBack` down) and the head don't contain it, so the head-keeping workaround (`TorsoGoal::keepBone`) isn't what stops the loop | High |
| Does the native left-hand IK (`LeftArm`, LHIK) run in combat? | **Yes, whenever the `AddCombatIdleOffset` blend node has weight**: its strength is that node's weight (`bSetStrengthFromAnimNode`). The weapon draw and holster code switches it off and back on | High (code + AnimTree defaults); "which stances weight that node" **[verify]** |
| Does our foregrip solve fight it? | No. It runs inside `UpdateSkelPose`, before our post-hook, and our write replaces the left arm. The native solve is wasted work, nothing worse | High |
| Is the aimed-fire bonus tied to the camera mode? | **No.** It's the weapon's `bIsZoomed` flag, set from input (`BioPlayerInput.bWantsToZoom`). The TightAim camera is chosen *from* that flag. Halcyon's camera-offset writes don't touch it, and the spread is applied after our hand-aim rewrite | High |

## 1. Where the player camera's location comes from

### The native chain

The camera mode's script `Tick` sets `m_pov.Location = GetCameraLocation()` (MassFPS source,
[11](11-massfps.md)). `GetCameraLocation` and its helpers are natives on `SFXCameraMode`:

| Function | Address | What it does |
|----------|---------|--------------|
| `execGetCameraLocation` | `0xB8F690` | Calls the C++ virtual at vtable slot `0x2C0` |
| `USFXCameraMode::GetCameraLocation` | `0xCB2240` | Slot `0x2C0` in `SFXCameraMode`'s vtable (`0x1310858`) and in four subclass vtables (`0x12E5C58`, `0x12E5F28`, `0x12E6200`, `0x12E64D8`): **no subclass overrides it**. Gets the hook from `GetActorCameraHook` (slot `0x2C8`), then returns `hook − R(m_pov pitch, yaw, roll 0) · Offset`. Without a hook it returns `ViewTarget.Target.Location` |
| `USFXCameraMode::GetActorCameraHook` | `0xCB1CE0` | Slot `0x2C8`. For a **BioPawn** target: `CalculateOffsetHook(1.0)`. For another pawn: the bone `HookName` in world space plus `HookOffset` rotated by the pawn's yaw |
| `USFXCameraMode::CalculateOffsetHook` | `0xCAEF50` | The hook point (below), plus `HookOffset · Distance` rotated by a yaw-only rotation (pitch and roll zeroed) taken from the view target's controller or pawn. Stores the hook in `LastHookPos` (`+0xCC`) |
| `USFXCameraMode::GetCameraHook` | `0xCB20B0` | The same hook point without the offset |
| `USkeletalMeshComponent::GetBoneLocation` | `0x583410` | `(out, BoneName, Space)`: space 1 returns the `SpaceBases` translation (component space); any other space goes through `GetBoneMatrix` (`0x583540`), i.e. `SpaceBases[bone]` times the component's `LocalToWorld` |

The hook point, by `HookName` (`SFXCameraMode +0xE4`):
- `CoverSlot`: the location of an actor the BioPawn holds at `+0x8BC` (the cover slot);
- `None`: the pawn's `Location` minus its cylinder's `CollisionHeight` (its feet);
- anything else: `GetBoneLocation(Pawn.Mesh, HookName, world)` on the pawn's main mesh
  (`APawn::Mesh`, `+0x480`), i.e. **the bone as it currently sits in `SpaceBases`**.

`bFirstPerson` (`+0x124` bit `0x20`) isn't read anywhere in this chain. (The comment next to
`kSFXCameraModeFlags` in `src/halcyon/le1/le1_game.h`, "renders first-person off the head
HookOffset", doesn't match the code; the flag's effect is on rendering the player, H12.)

### Which bone: `Camera`

Default properties, read from `SFXGame.pcc`:

| Object | `HookName` | `HookOffset` | `Offset` |
|--------|-----------|--------------|----------|
| `Default__SFXCameraMode` | **`Camera`** | (0, 0, 98) | |
| `Default__SFXCameraMode_Combat` | (inherited) | (−30, 0, 80) | (80, −44, 15) |
| `Default__SFXCameraMode_Explore` | (inherited) | (0, 0, 120) | (200, 0, 40) |
| `SFXCameraSetup.CombatCam0` | (inherited) | (−30, 20, 90) | (90, −22, 25) |
| `SFXCameraSetup.CombatCrouchCam0` | (inherited) | (−30, 20, 45) | (90, −22, 25) |
| `SFXCameraSetup.CombatTightAim0` | (inherited) | (−30, 20, 80) | (50, −20, 15) |
| Cover peek, blind fire, power cover, cover lean modes | `CoverSlot` | various | various |
| `SniperTightAimCrouch0` | `God` | (0, 5, 28) | |

So exploration, combat, crouch and tight aim all hang off the bone named `Camera`. Shepard's
skeleton has one: `[104] Camera`, parent `God` (index 0), next to the IK goal bones `RFIK`,
`LFIK` and `RHIK`, which also hang off `God` (LE1MEVR.log skeleton dump, 2026-10-08). It's an
animated bone, so the camera moves with the animation's camera track; that fits the log lines
where "first person camera vs Shepard's head bone" wandered between −14 and +44 units forward
within a minute.

The camera is therefore `Camera bone (world) + yaw · HookOffset − R(view pitch, yaw) · Offset`.
Halcyon's first person writes `Offset` (`head_aim.cpp` `FpOwnAndWrite`); `HookOffset` differs per
mode (crouch is 45 units lower), which is why it needs per-state offsets.

### Why 489ef51 looped, and why the current build doesn't need `keepBone`

- 489ef51 moved the whole body "every bone, rigidly" (`TorsoPose.cpp`: with a shift, it wrote
  from bone 0). That moved `Camera`, so the hook, so the view, so the next frame's goal: the
  loop the user felt as judder.
- Today the torso writes only the subtree of the bottom spine joint (`LowerBack`, under `Root`),
  and the arms write their own chains. `Camera` sits under `God`, outside all of them, so the
  camera can't follow these writes. `TorsoGoal::keepBone` (the head keeps its animated
  transform) isn't what prevents the loop; its comments in `TorsoPose.hpp` and `ArmIk.cpp`
  ("Halcyon's camera follows the head bone") are wrong.

### Order within the frame

`UpdateSkelPose` runs in the mesh component's `Tick` (`0x58D709`, [14](14-vrik.md)); our
post-hook writes right after. The camera reads the bone when the camera mode ticks, inside the
camera's `UpdateCamera` event. `UWorld::Tick` (`0x41AC30`, read in the project rebuilt from the
clean dump; `UGameEngine::Tick` calls it with level tick 2) runs, in this order:
1. the tick groups `0x40D630`, `0x40B240`, `0x40BB60` (the `+0x1C8` group counter goes 0, 1, 2)
   and `0x40C450` (4), which tick the actors and their components;
2. then a loop over the controllers (`GetFirstController_guess` `0x616660`, `NextController`
   `+0x3EC`): for each player controller, `PlayerCamera` (`+0x6B0`) gets an event through
   `ProcessEvent` with the frame's `DeltaTime`. That's stock UE3's "update cameras last", i.e.
   `UpdateCamera`;
3. then the last group (`0x40CD40`, counter 3).

So the camera reads `SpaceBases` **after** our post-hook in the same frame, unless the player's
mesh ticks in that last group (stock UE3 ticks pawns in the first) **[verify: a sequence log of
UpdateSkelPose for the player mesh vs `Camera.UpdateCamera` in our ProcessEvent hook]**. A write
to the `Camera` bone moves the view in the same frame, which is why the loop was immediate.
Halcyon renders both eyes from that one camera result.

## 2. The native left-hand IK (`LeftArm` → `LHIK`)

### How the engine applies skeletal controls

| Function | Address | Role |
|----------|---------|------|
| `ComposeSkeleton` | `0x580A60` | Calls `0x57AD90` (named `ComposeSkeletonBody_guess`) |
| `ComposeSkeletonBody_guess` | `0x57AD90` | `LocalAtoms` → `SpaceBases`, bone by bone; for the bones past the plain-copy prefix it calls `ApplyControlsForBone_guess` |
| `ApplyControlsForBone_guess` | `0x5776F0` | Looks up the bone's control list (per-bone index bytes at `+0x3C4`, or `+0x3D4` for post-physics controls; `0xFF` = none) in the AnimTree's lists, walks `NextControl` (`+0xA0`). Strength = `ControlStrength` (`+0xA8`), times `AnimMetadataWeight` (`+0xBC`) when `bControlledByAnimMetada`. Skips the control when the strength is ≤ 1e-5, when its LOD limit (`+0xC8`) is at or below the mesh's LOD, or when `bIgnoreWhenNotRendered` applies. Otherwise calls the control's virtuals: `+0x2F0` (affected bones), **`+0x2F8` (`CalculateNewBoneTransforms`)**, `+0x300`, `+0x310` (alpha, then the `BlendType` curve) |
| `USkelControlLimb::CalculateNewBoneTransforms` | `0x54F360` | The two-bone solve. Reads the effector and joint target in their spaces (bone spaces read `SpaceBases`; `BCS_BaseMeshSpace` = 6 reads the base actor's mesh), and handles stretch (`StretchLimits`, `+0xFC`) |
| `USkelControlBase::TickSkelControl` | `0x568560` | With **`bSetStrengthFromAnimNode`** (`+0xCC` bit `0x2`): caches the AnimTree nodes whose `NodeName` is in `StrengthAnimNodeNameList` (`+0x68`), and sets `StrengthTarget` and `ControlStrength` to the sum of their `NodeTotalWeight` (`+0xD0`) for the relevant ones (`+0xE0` bit 1), clamped to 1. Then it blends `ControlStrength` toward `StrengthTarget` over `BlendTimeToGo` |
| `USkelControlBase::SetSkelControlActive` | `0x568330` | `StrengthTarget` = 0 or 1, `BlendTimeToGo` from `BlendInTime`/`BlendOutTime`; follows `NextControl` while `bPropagateSetActive`. `execSetSkelControlActive` (`0x34D750`) calls it |

So every control runs **inside `UpdateSkelPose`, before our post-hook**.

### The player's `LeftArm` control

From `BIOG_Male_Player_C.pcc`, `BIOG_AnimTree_Humanoid.BIOG_Player_O.SkelControlLimb_2`:
- `ControlName = LeftArm`, on `LeftWrist`;
- effector: `BCS_OtherBoneSpace` on bone `LHIK`; joint target: `BCS_OtherBoneSpace` on
  `LeftShoulder-Swivel`; `BoneAxis = AXIS_Y`, both axes inverted;
- **`bSetStrengthFromAnimNode = 1`, `StrengthAnimNodeNameList = [AddCombatIdleOffset]`**.

`AddCombatIdleOffset` is a `BioAnimNodeBlendAdditive` in the same tree. The two leg controls
(`RightLeg` → `RFIK`, `LeftLeg` → `LFIK`) use the same node. So the off-hand IK is on exactly as
much as that additive combat-idle branch is blended in; the probe's "strength 0.00" lines
(2026-10-08 20:44 and 20:48) were taken when it wasn't.

The weapon switch code drives it too (`UBioActiveSelectWeapon`, natives only):
- `0xBE4A50` (named `StartActive_guess`) finds the control by name (`FindSkelControl_guess`
  `0x582300` with `L"LeftArm"`) and keeps it in `m_oWeaponIKControl` (`+0x88`);
- `0xBF1890` (`WeaponIKOff_guess`, draw or holster) clears its `bSetStrengthFromAnimNode` and
  calls `SetSkelControlActive(false)`: the hand blends off the gun;
- `0xBC7E90` (`WeaponIKRestore_guess`) sets `bSetStrengthFromAnimNode` again and forgets the
  pointer: the node weight takes over.

A debug HUD function (`0xA1A760`) prints `WeaponIK: true/false` for the same `LeftArm` control.

### What it means for `SupportGrip` / `ApplySupport`

- **No fight.** The native solve pulls the left hand to the *animated* `LHIK` while the pose is
  composed. Our post-hook then writes the right arm (which carries `Prop01` and `LHIK` rigidly)
  and the left arm (to the foregrip). Our write is last, so it wins every frame; the native
  result is only wasted.
- The "animated left hand" our grip measures is already the native IK's output whenever the
  control is on, so "the animated left wrist is near LHIK" means "the control is on".
- During a draw or holster the control is off (blending), so the animated left hand is off the
  gun then. `LHIK` stays a good foregrip either way: it's under `Prop01`, so it moves with our
  right-hand write.

## 3. The aimed-fire accuracy bonus

| Function | Address | What it reads |
|----------|---------|---------------|
| `ABioWeaponRanged::GetPlayerAimError(Accuracy)` | `0xC59920` (slot `0x908`; `exec` `0xB72E80`) | Asks `Stats` (`+0x4C0`) for the aim-error range, picked by **`bIsZoomed`** (`+0x548` bit `0x4`), and lerps it by `Accuracy` |
| `ABioWeaponRanged::GetRecoil` | `0xC59F90` (slot `0x900`; `exec` `0xB72E30`) | `Stats` recoil picked by `bIsZoomed`; × 0.25 under a condition on the instigator's controller and a global object **[verify what]** |

Who sets `bIsZoomed` (script, `SFXGame.pcc` bytecode references):
- `SFXGameModeDefault.TightAim` (the left-trigger action) sets `BioPlayerInput.bWantsToZoom`
  (when combat is enabled); `StopTightAim` / `TurnOffTightAim` clear it.
- `BioPlayerInput.PostProcessInput` calls `SetZoomed` every frame from `bWantsToZoom`, forced
  off while switching weapons or overheated (`bForceNoZoom`).
- `BioPlayerController.SetZoomed` passes it to `Pawn.Weapon.SetZoomed`, which sets
  `bIsZoomed` (and the aim mode from `Stats.AimModes`).
- `SFXCameraSetup.GetCombatCamera` picks `CombatTightAim` / `CombatCrouchTightAim` when
  `IsZoomed()`: **the camera follows the flag**, not the other way round.

`BioWeaponRanged.GetAdjustedAim` (script) first takes `Instigator.GetAdjustedAimFor(...)`, which
our hand aim rewrites, and only then, if `Stats.bEnableAccuracy`, adds a random error of
`GetPlayerAimError(Inventory.Accuracy)`. So the zoomed (smaller) spread applies to hand-aimed
shots too.

Halcyon writes `Offset` on the active camera mode, including the TightAim mode, and nothing in
this chain reads the camera mode. **The bonus applies in first person under Halcyon** as long as
LT reaches the game's TightAim action.

## Proposals

**Status (2026-10-21):** 1-5 are built (mod only, no host redeploy); 6 isn't needed unless a
stance's camera hooks a bone we write. Test steps: `notes/next-vr-session.md` steps 8 and 9.
- 1: `engine/BonePose` skips the bones named `Camera` and `God` (`Request::keep`); arm IK logs
  once per skeleton whether a written subtree contains them, the heartbeat if a solve moved one.
  The rule is in [14](14-vrik.md), "Camera hook bones".
- 2: `keepBone` is no longer set; the comments are fixed. First-person lean stays `relative`:
  the Camera bone's animation wanders tens of units against the head, so aiming the eyes at the
  view would follow the animation's camera track, not the player.
- 3: `game/CameraProbe` logs each mode change (`camera: mode ...`), with GetCameraLocation's
  formula from the hook bone against the mode's `m_pov` **[verify: gap near 0]**.
- 4: the `LeftArm` control's strength, target and `bSetStrengthFromAnimNode`, per gun and on each
  change (`support grip: the AnimTree's off-hand IK (LeftArm) is on|off|blending`); the wrist-
  to-LHIK distance is kept as a cross-check.
- 5: `bIsZoomed` on each change, with whether our grip forces LT (`support grip: the weapon's
  bIsZoomed is on (our grip forces LT)`).

Offsets checked against the LE1 SDK headers (static_asserts in the code): `SFXCameraNativeBase.
CurrentCameraMode +0x568`, `SFXCameraMode` `m_pov +0x98`, `Offset +0xB4`, `HookOffset +0xC0`,
`LastHookPos +0xCC`, `HookName +0xE4`, `SFXCameraMode_Interpolate.To +0x168`, `SkelControlBase`
`ControlStrength +0xA8`, `StrengthTarget +0xB4`, `bSetStrengthFromAnimNode +0xCC bit 0x2`,
`BioWeaponRanged.bIsZoomed +0x548 bit 0x4`: all as above.

The original proposals:


1. **Never move the `Camera` bone.** In `engine/BonePose` `Write`, after building the pose,
   copy the animated `SpaceBases` entry of the bone named `Camera` back (find it by name once
   per mesh, like `LHIK`). That's cheap insurance, and it makes a whole-body shift (the 489ef51
   idea: put Shepard's eyes at the view, then lean) safe again: move every bone except `God`
   and `Camera`.
2. **Drop `TorsoGoal::keepBone = head`** in first person, or keep it only if the hidden head
   misbehaves. It doesn't break the camera loop, and keeping the head while the neck leans
   separates them (the head mesh is hidden, but its shadow may not be). Fix the comments that
   say the camera follows the head bone.
3. **Log the camera hook** once per mode change: the mode's class, `HookName`, `HookOffset`,
   `Offset`, and the `Camera` bone's component position. That confirms the bone in game and
   explains the forward offset that varies by stance.
4. **Read `LeftArm`'s strength directly** instead of the distance test in `SupportGrip.cpp`
   (`ControlStrength`, `+0xA8`; the control is already found by `SkeletonProbe`), and log it in
   combat. Optional: while gripping, setting its `ControlStrength` to 0 saves the native solve,
   but changes nothing visible.
5. **Focused fire stays as built** (LT forced while gripped). A cheaper check than the headset:
   log `bIsZoomed` (`BioWeaponRanged +0x548` bit `0x4`) while the grip holds LT. Setting
   `BioPlayerInput.bWantsToZoom` directly would also work, without the pad bit.
6. **Fallback seam, only if 1 isn't enough** (e.g. a stance whose camera hook is another bone):
   a post-handler on `SFXCameraMode.GetCameraLocation` (script → native through
   `CallFunction`) that adds `animated − written` of the hook bone to the result, or a detour on
   `0xCB2240`. Both see every mode, since no subclass overrides it.

## Method (local research data, nothing committed)

- **Native names before the dump.** The natives table (`int<Class>exec<Name>` strings paired
  with function pointers) lives in `.data`, which is **encrypted on disk** (entropy 8.0 bits per
  byte), so it can't be read from the file. Two things still worked:
  - `exec` thunks sit in `.text` in class order, with each class's functions in the
    **reverse** order of their name strings in `.rdata`. Counting from a known anchor
    (`execGetMuzzleLoc`, `execTickFire`) found `SFXCameraMode`'s six thunks at
    `0xB8F200`–`0xB8F800`; the dump later confirmed all of them;
  - a byte pattern for "no-parameter thunk that returns a vector" (the shape of
    `execGetMuzzleLoc`) lists such thunks with the vtable slot they call.
- **Default properties.** LE1 packages are Oodle-compressed. The game's own
  `oo2core_8_win64.dll` (`OodleLZ_Decompress`) unpacks them read-only. Header: the compression
  flags (`0x400`) and chunk table follow the generations, engine and cooker versions and two
  more ints; each chunk starts with the package tag, a block size, then (compressed,
  uncompressed) size pairs. LE1 export entries have no component map. Property tags (name, type,
  size, array index, then a struct or enum name for those types) give `HookName` and friends.
  Script function bodies reference final functions and properties by 4-byte object index, and
  virtual calls (`0x1B`) by 8-byte name, which is how the zoom chain above was traced.
  **Proposal:** turn the scratch scripts into `tools/pcc/` (decompress, list exports, dump
  properties, find references) so this doesn't need Legendary Explorer.
- The clean attach-time `.text` dump (2026-10-21 21:06) has only `0x102DC0` patched (another
  ASI); the Ghidra project was rebuilt from it the same evening.
