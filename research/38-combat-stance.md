# 38: The weapon drawn, looking and feeling like exploring (2026-10-25)

User report (VR, first person, LE1): with a weapon drawn (`BioPawn.IsInCombatActionState()`, which shooting
needs, so we keep it) four things change that don't belong in VR: (1) Shepard's body takes the combat stance
(bladed, feet apart, shoulders shifted) and no longer matches the player's when looking down; (2) the view
jumps or slides when the weapon comes out or goes away; (3) walking feels different; (4) the draw and holster
animations delay things. This note says what causes each one and proposes how to neutralise it, in first
person on foot, without leaving the combat action state.

Builds on [18](18-camera-and-hands.md) (the `Camera` bone, the camera formula, `LeftArm` IK),
[19](19-body-holsters.md) (SwitchToWeapon, TryHolster, BioActiveSelectWeapon), [23](23-cover.md)
(PlayerMoveExplore / PlayerMoveCombat, crouch), [29](29-body-follow.md) (body follow).

## Sources and method (local research data, nothing extracted is committed)

- `BIOG_Male_Player_C.pcc` (the player's AnimTree `BIOG_AnimTree_Humanoid.BIOG_Player_O`), `SFXGame.pcc`
  (camera modes, movement script, draw/holster animations and their notifies, BioPawnBehavior),
  `Engine.pcc` (the movement 2DA), decompressed read-only with the game's `oo2core_8_win64.dll` and read
  with a rebuilt scratch reader (exports, tagged properties, Bio2DA cells, a UE3 bytecode printer), as in
  [18](18-camera-and-hands.md) "Method". The scratch scripts live outside the repo.
- Field offsets: the LExSDKv2 LE1 headers (reference only; new code resolves them through `src/ue3`).
- The game's own measurements: `game/CameraProbe`'s `camera: mode ...` lines in `LE1MEVR.log` /
  `LE1MEVR.prev.log` (2026-10-23 15:54-15:57, Halcyon first person, standing on foot), with the
  `holsters: grab ... drawing` lines of the same minutes.
- **Not available this session:** Ghidra. The project was locked by another process (the GhidraMCP server
  started from the main checkout, `tools/ghidra/mcp.ps1`), so `decompile.ps1` and the MCP tools both
  refused. Everything native below (what `BSCbt_CombatSwitch` reads, `GetMaxSpeed`, `CanFireWeapon`,
  `BioActiveSelectWeapon`'s state machine) is marked **[verify]**, with the function to read when Ghidra
  is free.

## Summary

| | Cause | Proposal | Confidence |
|---|---|---|---|
| A. Body pose | One AnimTree node, **`CombatMode`** (`BioAnimNodeBlendState_5`, behaviour `BSCbt_CombatSwitch`), switches the **whole body** between a `NonCombat` and a `Combat` branch in 0.1 s. No upper/lower split: only the `Camera` bone is filtered out (it always takes `CB_IdlePose`). Firing, grenade, power and draw/holster animations are additives *inside* the Combat branch, whose top node skips its tick at zero weight | Let the tree tick as the game wants (Combat weight 1, every notify fires), then, just before the pose is composed (a pre-stage in our `UpdateSkelPose` hook), set `CombatMode`'s child weights to NonCombat 1 / Combat 0 for the player's mesh, and put them back after. Not while crouched, in cover, scoped, in the Mako | Tree: high (package). The hook order (tick before compose): **[verify]** |
| B. View shift | Not the body: the `Camera` bone stays put (the filter above; logged bone height identical before and after). It's the **camera modes' offsets**: with Halcyon's per-mode `Offset`s the eye sits 50.1 forward / 94.9 up of the bone exploring and 40.0 / 69.0 in combat: **26 units down and 10 back on draw**, 9 more down aiming (TightAim), 20 to the right sprinting with the weapon out; the game blends between modes over 0.5 s (quarter-sine) | Give every on-foot first-person mode the Offset that puts the eye where exploring puts it: `Offset = HookOffset(mode) − (50.1, 0, 94.9)`. Step 1, settings only: `MELEVR.ini` `fpCombat = -80.1, 20, -4.9`, `fpCombatStorm = -60.1, 40, -4.9`, `fpCombatAim = -80.1, 20, -14.9`. Step 2 (crouch too): a small `[MEVR]` seam in `FpOwnAndWrite` computing it from each mode's own `HookOffset` | High (logs + package) |
| C. Movement | Same speeds (the player's 2DA row: 400 run, 130 walk both ways). Differences: how fast the speed ramps (explore 1.5/s up and down, combat 3/s up, 2/s down: combat is twice as snappy), facing (combat strafes around ControlRotation, explore turns the pawn toward the stick), crouch (explore stands up on any move; combat crouch-walks), sprint (500 vs 525, stamina 2.5 vs 4 s), a separate **zoomed** walking mode (our support grip holds LT) | Write the player's `BioPawnBehavior.m_fMaxDesSpdTac{Accel,Decel}Rate` to the explore values while VR is on foot (restored on unload); leave facing, sprint and crouch as the game has them. Measure the zoomed walk before changing it | Values: high. `GetMaxSpeed`'s mode table: **[verify]** |
| D. Delay | The draw and holster are `CB_Draw` / `CB_Holster` additives on the `WeaponEquip` node, driven by a `BioActiveSelectWeapon` modal active; three AnimNotify_Script notifies run it: `SwapDrawAnim` (the gun goes from holster to hand), `FadeOutDrawAnim`, `FinishDrawAnim` (the active ends: `IsSwitchingWeapons` false). Shotgun: 0.41 / 0.67 / 0.97 s of 1.17 s; sniper 0.33 / 0.80 / 1.10 of 1.33; holster 0.61 / 0.87 / 1.17. The action state and the camera switch at once; firing waits **[verify: native CanFireWeapon]** | Play the player's `WeaponEquip` draw and holster sequences faster (`AnimNodeSequence.Rate`, `[Hands] drawRate = 2.5`): every notify still fires, in order, just sooner. Fallback: call the three notify functions ourselves. Measure first: log how long `IsSwitchingWeapons` stays true per weapon | Notify names/times: high. Rate as the lever: **[verify]** |

**Built (2026-10-25):** all four, each behind a `[Hands]` setting (on by default): see "Built" under each section, and
`notes/next-vr-session.md` step 35 for the headset session. B is step 2 (the seam), not step 1's settings.

## A. What switches the body to the combat stance

### The tree (`BIOG_Player_O`, top down, the parts that matter)

```
AnimTree BIOG_Player_O
 CameraShake1 > CameraShake2 > CameraShake3
  Actions (BioAnimNodeBlendByAction)            Posture | Mount | Fall | Dead | Dying | Matinee | GetUp | Gestures ...
   [Posture] AddDamage0 > AddDamage1 > AddEyeballNoise > AddEyeBlinkNoise
    EX_Posture (BioAnimNodeBlendState, BSAct_Posture; 0.33 s)   Standing | Crouching | Storm
     [Standing], [Crouching]: Filter-CameraBone (AnimNodeBlendPerBone, BranchStartBoneName = Camera)
       [Source] CombatMode (BioAnimNodeBlendState_5, BioAnimNodeBlendStateCombatBehavior = BSCbt_CombatSwitch;
                            BlendToChildTimes 0.1 s both ways)
         [NonCombat] AddIdleOffset > AddIdleLoop > (Standing: NonCombatSpeed idle/walk/run, starts, stops,
                     skid turns, walk/run leans | Crouching: EX_Crouch, a static loop | Storm)
         [Combat]    AddCombatIdleOffset (BioAnimNodeBlendAdditive, bSkipTickWhenZeroWeight)
                       > WeaponEquipAdd [additive: WeaponEquip = BioAnimNodeBlendWeaponEquip: Holster CB_Holster |
                                         Draw CB_Draw | None]
                       > PowerAdd [additive: Power: BC_Start / BC_End / BC_Loop]
                       > CombatActionAdd [additive: CombatActionList: FireSequence (CB_FireStart/Loop/End),
                                          ThrowGrenade CB_Grenade1]
                       > CoverSwitch > Not In Cover: AddPortArms > (Standing: StandingAimAdd = CombatSpeed2
                         (StandingAimTurn | CombatWalking | CombatRunning, directional) + StandingGunAiming
                         (BioAnimNodeBlendByAim) | Crouching: CombatCrouchWalk2 + CrouchingGunAiming | Storm)
                       (+ CB_IdleOffset additive, CB_IdlePose base)
       [Target] CB_IdlePose          (weight 1 for the Camera bone only: Child2PerBoneWeight[104] = 1)
     [Storm]: AddStormCamera > the same Filter-CameraBone
```

- **One switch for the whole body.** `CombatMode` picks the branch for every bone; there is no
  AnimNodeBlendPerBone splitting legs from torso. The only per-bone node is `Filter-CameraBone`, which
  always takes the `Camera` bone from `CB_IdlePose` (this is why the camera hook doesn't move with the
  stance: B).
- **What decides it:** `BioAnimNodeBlendStateCombatBehavior` with `CombatBehavior = BSCbt_CombatSwitch`
  (enum: None, CoverSwitch, CoverDirection, CoverState, CoverBlocked, CombatSwitch, CoverPredictDirection,
  CoverBlockType). The behaviour's code is native: most likely the pawn's action state
  (`BioPawnBehavior.m_eCurrentActionState`, `+0x344`) or `IsInCombatActionState` **[verify: the behaviour's
  virtual in Ghidra, find it from the class's vtable; `-Search BlendStateCombatBehavior`]**. Changing the
  behaviour byte doesn't help (below).
- **Firing, recoil, grenades, powers, draw and holster are additives inside the Combat branch.** The Combat
  branch's top (`AddCombatIdleOffset`) and most nodes below it have `bSkipTickWhenZeroWeight = 1`.
- `AddCombatIdleOffset`'s weight also drives the native `LeftArm` IK and the two leg IKs (`RightLeg` →
  `RFIK`, `LeftLeg` → `LFIK`; [18](18-camera-and-hands.md)).

### Why not just force the explore branch

Making `CombatMode` choose `NonCombat` (writing its target child, or changing the behaviour to `BSCbt_None`)
gives the Combat branch zero weight while the tree ticks. Its top node then **skips its tick**, so
`CB_Draw` / `CB_Holster` never advance and their notifies (`SwapDrawAnim`, `FadeOutDrawAnim`,
`FinishDrawAnim`, D) never fire. `BioActiveSelectWeapon` would never finish: `IsSwitchingWeapons` stays true
and the weapon can't fire. The grenade throw (`CB_Grenade1`) and the power cast animations live there too
**[verify whether their notifies drive gameplay: their AnimNotify_Script names]**.

### Proposal: tick as combat, compose as explore

USkeletalMeshComponent's tick runs the AnimTree's `TickAnim` (weights, times, notifies) and then
`UpdateSkelPose` (`0x592440`, our hook in `engine/bone_pose`), which asks the tree for bone atoms by the
children's `Weight` and composes `SpaceBases` **[verify the order: a one-time log comparing `CombatMode`'s
`Children[1].Weight` and `CB_Draw`'s `CurrentTime` at the start and end of our hook]**. So:

1. Find the player's node once per AnimTree: walk `SkeletalMeshComponent.Animations` through
   `AnimNodeBlendBase.Children` (`+0xE8`, `FAnimBlendChild` 0x40 bytes: `Anim +0x28`, `Weight +0x30`)
   matching `AnimNode.NodeName` (`+0xC0`) = `CombatMode`, class `BioAnimNodeBlendState`, two children
   named `NonCombat` and `Combat`. (Walking in C++ avoids `FindAnimNode`, a script call.) Through
   `src/ue3` (`FieldRef`s for `Engine.AnimNode.NodeName`, `Engine.AnimNodeBlendBase.Children`,
   `Engine.AnimBlendChild.Anim/Weight`, `Engine.SkeletalMeshComponent.Animations`) with `ReflectCheck` rows.
2. **Pre-stage in `UpdateSkelPoseHook`** (before `gOrig`), only for the player's mesh and only while the
   mod's per-tick decision says so: save the two weights, write `NonCombat = 1, Combat = 0`
   (`write_journal::Wrote("BonePose.combatStance", ...)`). **After `gOrig`, restore them**, so the
   BlendState's own blend state is untouched next tick (it may read its children's weights as the start of
   its next blend).
3. The decision (mod tick, game thread; the hook reads one atomic): `[Hands] exploreStance = 1`, VR on foot
   (game mode 0), first person, the player in the combat action state, **not** crouched (explore crouch is a
   static `EX_Crouch` pose: the legs would freeze while crouch-walking), not in cover, not scoped (sniper),
   not storming if it looks wrong **[verify]**, not in the Mako.
4. Log `stance: the body composes as explore (CombatMode weights N/M taken from the game's)` on each
   change, a line per 10 s while active, and the snapshot JSON `mod.stance` (on, why not, the game's
   weights).

What it keeps: every combat node ticks with the weights the game chose, so the draw, holster, fire, grenade
and power notifies fire exactly as now, `AddCombatIdleOffset`'s `NodeTotalWeight` stays 1 (the native
`LeftArm` and leg IKs keep their strength; our arm IK writes the arms afterwards anyway). What it loses on
screen: the combat additives (fire kick on the arms, the draw/holster motion, power gestures) and the aim
twist (`StandingGunAiming`). In VR the arms are ours (VRIK), the gun is in the hand and recoil is a haptic
pulse, so nothing the player relies on.

Risks:
- **Legs while strafing:** in combat the pawn faces ControlRotation and moves sideways or backwards; the
  explore branch only has forward walk/run, so the legs run forward while Shepard slides sideways
  (body follow's moonwalk risk, [29](29-body-follow.md), now in combat too). Mostly out of view in first
  person. If it shows: C's optional facing change, or keep the combat branch while the stick moves
  sideways (`[Hands] exploreStanceMoving = 0`).
- **The explore branch's own state:** it ticks at weight 0 while drawn **[verify that its sequences
  advance at weight 0: `NonCombatSpeed`'s children are not `bSkipTickWhenZeroWeight`]**; relevance
  callbacks (start/stop animations, the random twitches) may restart when the weapon is holstered.
- **Shadow and mirrors:** the third-person shadow shows the explore pose too (intended).
- Halcyon reads no bones (research/29), and `Camera`/`God` aren't touched: no view feedback.

### Built (2026-10-25)

`[Hands] exploreStance = 1` (default; `exploreStanceStorm = 1` keeps it while sprinting), the mod (hot-reloads):
- `game/CombatStance` walks the player's AnimTree once per tree through `src/ue3` (`SkeletalMeshComponent.Animations`,
  `AnimNodeBlendBase.Children[].Name/Anim/Weight`, `AnimNode.NodeName`; ReflectCheck rows for each) for every node named
  `CombatMode` with children `NonCombat` and `Combat` (up to four: the postures may each have their own), and the
  `WeaponEquip` node's `Draw` / `Holster` sequences (D). Logged once per tree: `stance: the player's AnimTree ...`.
- The decision (`vr::combat_stance::Decide`, unit-tested) each mod tick: the setting, VR's view, on foot, a BioPawn,
  first person (`arm_ik::FirstPerson()`: arm IK anchors the hands in the view), drawn (`IsInCombatActionState`), not
  crouched (`Pawn.bIsCrouched`), not in cover (`CoverType` / `CoverAction`), not the sniper scope (the camera mode's
  class), the node found, the hook in, and the order confirmed (next).
- `engine/bone_pose`'s pre-stage (`PublishStance`): for the published mesh, on the game thread, with the mesh's tree
  and each node's Children buffer and count unchanged, it saves the two weights, writes NonCombat 1 / Combat 0
  (`write_journal` `BonePose.combatStance`), calls the original, and restores them if they are still ours (else counts
  an override and keeps the call's).
- **The [verify] is a gate, not just a log:** the pre-stage reads the node's `NodeTickTag` at the start and end of
  every call (`vr::combat_stance::OrderCheck`). Only after 30 calls in which the tag moved between calls and never
  during one does it write (`stance: [verify] the tree ticks before UpdateSkelPose composes`); two calls with the tag
  moving inside the call stand it down for the session (`... ticks inside UpdateSkelPose`). The first call with
  `CB_Draw` playing is logged once with the weights, `CB_Draw`'s `CurrentTime` and the tag at both ends.
- Logs: `stance:` on each change of the decision, every 10 s while active (the game's weights, calls composed,
  overrides, rejections, and whether the explore branch ticked at weight 0: the `NonCombatSpeed` [verify] above, FROZEN
  if not), the heartbeat's `stance:` line; the snapshot's `mod.stance`.
- Not built: `exploreStanceMoving` (the strafing legs) until the headset says the legs bother.

## B. What moves the view on draw and holster

### The numbers

The camera is `Camera bone + yaw · HookOffset − R(view) · Offset` ([18](18-camera-and-hands.md)). Halcyon
writes each first-person mode's `Offset` by class (`FpStateForClass`, `head_aim.cpp`; our deploy's
`MELEVR.ini` = upstream's `vr_config.h` defaults). With the game's `HookOffset`s (package defaults,
`SFXCameraSetup` subobjects and class defaults), the eye relative to the bone (forward, right, up; view
pitch 0, which `game/ViewLevel` holds on foot):

| Mode (object) | HookOffset | Halcyon `Offset` | Eye from the `Camera` bone |
|---|---|---|---|
| Explore (`ExploreCam0`, class default) | (0, 0, 120) | fpExplore (−50.1, 0, 25.1) | **(50.1, 0, 94.9)** |
| ExploreStorm (sprint) | (0, 0, 120) | fpExploreStorm (−50.1, 0, 25.1) | (50.1, 0, 94.9) |
| Combat (`CombatCam0`) | (−30, 20, 90) | fpCombat (−70, 19.9, 21) | **(40.0, 0.1, 69.0)** |
| CombatStorm (class default) | (−10, 40, 90) | fpCombatStorm (−70, 19.9, 21) | (60.0, 20.1, 69.0) |
| TightAim (`CombatTightAim0`) | (−30, 20, 80) | fpCombatAim (−70, 19.8, 20) | (40.0, 0.2, 60.0) |
| Combat crouch (`CombatCrouchCam0`, class Combat) | (−30, 20, 45) | fpCombat | (40.0, 0.1, 24.0) |
| Crouch tight aim (`CombatCrouchTightAim0`) | (−30, 20, 45) | fpCombatAim | (40.0, 0.2, 25.0) |

The probe's lines agree to 0.1 unit, e.g. 15:57:42 (draw) and 15:57:47 (holster): bone at
(−38098.2, 153019.4, −387.7) in both modes, camera 50.1 / 94.9 from it exploring and 40.0 / 69.0 in combat.

- **The `Camera` bone doesn't move with the stance.** `Filter-CameraBone` takes it from `CB_IdlePose` in
  every posture, and its logged height is identical across draw and holster. The stance animation isn't
  the cause.
- **The jump:** drawing drops the eye 25.9 units and moves it 10.1 back (26 cm at the life-size 100 units
  per metre); tight aim (LT, which our support grip holds) drops it another 9; combat sprint moves it 20
  right and 20 forward; the game's crouch in combat drops it 45 on top of the physical crouch's lean (the
  open question in [23](23-cover.md) section 5).
- **The slide:** each change goes through `SFXCameraMode_Interpolate` (`DefaultTime 0.5`, quarter-sine;
  `SFXCameraSetup`: `DefaultCombatTransitionTime 0.5`, `EnterAimTransitionTime 0.08`, `EnterStormTransitionTime 1.0`,
  `ExitStormTransitionTime 0.5`, `CrouchCameraTransitionTime 0.2`). The probe saw draw → Combat in
  0.50 s and Combat ↔ TightAim in 0.25 s. Halcyon owns the blend's destination so it glides, but the
  eye still travels the difference.
- The action state switches the camera at once: the draw at 15:54:20.067 started the blend at .082.
- Upstream lowered `fpCombat_up` on purpose ("rebaked 2026-07-11: lowered from 25.1, was too high",
  `vr_config.h`): on a flat screen the combat stance's head is lower. In our VR the view is the player's
  head and body follow puts Shepard's head under it ([29](29-body-follow.md)), so the stance's head height
  no longer matters.

### Proposal: one eye point for every on-foot mode

`Offset(mode) = HookOffset(mode) − E`, with `E = (50.1, 0, 94.9)`, the explore eye. If every mode gives the
same point, the blend between them is a blend between equal positions: nothing moves.

**Step 1 (settings only, the deploy's `MELEVR.ini`; reversible):**

| Key | Now | Constant eye |
|---|---|---|
| `fpCombat_fwd/right/up` | −70.0, 19.9, 21.0 | **−80.1, 20.0, −4.9** |
| `fpCombatStorm_fwd/right/up` | −70.0, 19.9, 21.0 | **−60.1, 40.0, −4.9** |
| `fpCombatAim_fwd/right/up` | −70.0, 19.8, 20.0 | **−80.1, 20.0, −14.9** |

This fixes draw, holster, aiming and sprinting while standing. The combat crouch shares `fpCombat`'s class
(`SFXCameraMode_Combat`), so it stays 45 lower (as today), and the crouched tight aim ends 35 lower.
`tools/deploy-asi.ps1`'s per-slot fixes are where the deploy would write these. Our menu's Advanced tab edits
the same keys live, so it can be tried in the headset first.

**Step 2 (crouch, a `[MEVR]` seam):** in `FpOwnAndWrite`, when `MevrBridge::ConstantFpEye()` is on, read the
mode's own `HookOffset` (`+0xC0`, next to the `Offset` it already reads) and write `HookOffset − E` instead
of the class's values (`E` from `fpExplore` and the Explore HookOffset (0, 0, 120)). That covers the crouch
modes too: the physical crouch then lowers the view only through Halcyon's lean (the player's real head).
L3's crouch without a physical crouch would no longer lower the view, so the seam needs a
`[Camera] crouchDrop` choice. Cover, sniper and Mako modes stay excluded as now.

Risks: the longer `Offset` (83 units against explore's 56) swings more with view pitch (`R(view)` includes
pitch), so it's exact only while `game/ViewLevel` holds pitch at 0 (on foot with the hand aiming; not in the
scope). The Interpolate mode's aim-point preservation (`AimPointDistance`, `bDisableAimPointPreservation`)
may still turn the view during a blend **[verify: the yaw logged across a draw]**. Check in the headset:
`tools/snapshot.ps1` before and 1 s after a draw (the JSON's `hostView` position against the hook bone),
and the `camera: mode` lines (the formula's "N up of the game camera" should read 94.9 in every mode).

### Built (2026-10-25)

Step 2 directly (the seam covers every standing mode; crouch keeps today's values, so no `crouchDrop` choice yet).
`[Hands] constantEye = 1` (default) in `ASI\LE1MEVR.ini`, read by the host once a second: a `[MEVR]` seam in
Halcyon's `FpOwnAndWrite` (`head_aim.cpp`, after the liveness guard) asks `MevrBridge::ConstantFpEye`
(`plugin/HalcyonBridge.cpp`) for the Offset to write. For a standing mode (`vr::combat_stance::ConstantEyeApplies`:
Explore, Combat, TightAim and the sprint modes; not when the object or class name says Crouch; Halcyon already leaves out
cover, the scope and the Mako) it returns the mode's own `HookOffset` (read at `+0xC0`) minus `E`, where `E` = the last
Explore mode's `HookOffset` (the package's (0, 0, 120) until one is seen) minus `fpExplore`. The math is unit-tested
against the table above. Logged once per mode change: `constant eye: CombatCam0 (SFXCameraMode_Combat): HookOffset
(-30.0, 20.0, 90.0) -> Offset (-80.1, 20.0, -4.9): the eye (50.1, 0.0, 94.9) from the hook bone, as exploring
(Halcyon's (-70.0, 19.9, 21.0))`; crouch modes log that they kept Halcyon's. With it on, Halcyon's `fpCombat`,
`fpCombatStorm` and `fpCombatAim` sliders only act in the crouch modes. Host side: needs a redeploy.

### The blend's stored offset: the jump on a first draw (2026-10-25)

User (headset): "when I take out the sniper, it seems to move me forward/back". `LE1MEVR.prev.log` 18:27:55.588: the draw
starts `SFXCameraMode_Interpolate_42` toward `SFXGameModeDefault_1.SFXCameraSetup_2.SFXCameraMode_Combat_36` (the sniper
rifle's own camera setup; the other guns use `SFXCameraSetup_1`). At .611 arm IK logs `body anchor: third person ...
view 184 units from Shepard's animated head bone`, first person again at .921; the blend ends at 56.100. Halcyon's log:
`[FPCAM] owned mode ... vanilla=(90,-22,25)` at 55.600 and the next `[FPCAM] write` reads the target back, and our
`constant eye: SFXCameraMode_Combat ...` line at .601. So Halcyon **did** write the destination, within a frame. The
hypothesis (the destination never written) was half right: it had the over-the-shoulder Offset **when the blend
started**, and the blend never looks at it again.

**How the blend works** (SFXGame.pcc script, the scratch bytecode printer of the "Sources" above; native parts from
[18](18-camera-and-hands.md)):
- `SFXPlayerCamera.UpdateViewTarget` → `PickCameraMode` → `SFXGameModeManager.GetCameraMode` (→ the active
  `SFXCameraSetup`'s `GetCameraMode` / `GetCombatCamera` / `GetExploreCameraMode`) → `SetBehavior(NewMode, Transition,
  Time, PreserveTarget)`, all script on the game thread inside the camera update. `SetBehavior` calls
  `Mode.Initialize()`, makes a new `SFXCameraMode_Interpolate` from the template and calls
  `InitializeTransition(PreviousBehavior, Mode, Time, PreserveTarget)`, then switches to it. `PreserveTarget` is 1
  (0 for `bInstantTransition` modes).
- `InitializeTransition`: `bFirstPerson = From.bFirstPerson && To.bFirstPerson`; `From`/`To` (`+0x160`/`+0x168`);
  with `PreserveTarget` and not `bDisableAimPointPreservation` it traces along the current camera (`Trace`: from
  `CameraCache.POV`, 10000 units) and, on a hit, `AimPointDistance` and `AimPoint`, `To.AimAtPoint(V)` twice and
  `To.DoCameraCollision`; unless the collision pulled it in (`CollisionDistance > 0.5`), the hit is closer than
  `MinAimPointPreservationDistance`, or steep (`AimZDistance > AimPointDistance * 0.5`), `To` keeps the aimed rotation;
  otherwise (and with no hit) `To.m_pov.Rotation = From.m_pov.Rotation` and `To.m_pov.Location = To.GetCameraLocation()`.
  Then `PC.SetRotation(To.m_pov.Rotation)`, and once: **`RotationOffset = To.m_pov.Rotation - From.m_pov.Rotation`
  (`+0x148`), `LocationOffset = To.m_pov.Location - From.m_pov.Location` (`+0x154`)**.
- `SFXCameraMode.AimAtPoint(V)` turns the mode's yaw and pitch so that its camera (`GetCameraLocation`, from its own
  Offset) looks at `V`: the parallax between the From camera and the To camera becomes a turn of the controller.
- `Tick(dt)`: `CurrentTime += dt` (`bComplete` at `TotalTime`), `To.Tick(dt)` (its `m_pov` from the controller's
  rotation and `GetCameraLocation()`), `From` is **not** ticked; `T = Interpolate(CurrentTime / TotalTime)` (the
  `Curve`); `m_pov.Location = To.m_pov.Location - Lerp(LocationOffset, 0, T)`, the rotation the same with
  `RotationOffset`; FOV and the camera input's sensitivities lerp From → To. `GetActorCameraHook` lerps the two hooks.
  `UpdateCameraMode` switches to `To` once `bComplete`.

So the view starts exactly at From's camera and ends at To's, whatever To's Offset is **at the start**. Halcyon writes
Offsets on the Present thread a frame later. For a destination it already owned (written in an earlier blend) nothing
changes. For a fresh one (vanilla `(90, -22, 25)`: the eye 120 behind and 42 right of the hook point), its write moves
`To` by `-R(pitch, yaw) * (new - old)`, 178 units, and the stored offset doesn't follow: the view is suddenly 1.78 m
ahead of the eye and slides back over the rest of the blend. The arm IK line's 184 is that plus the head's place.

Which transitions: every blend into a mode Halcyon hasn't written yet. Halcyon's owned table starts empty with each
level's new mode objects, and a mode it releases (cover, a state turned off) or restores (save menu, first person off)
gets its vanilla Offset back. Seen in the logs: the sniper rifle's first draw (`SFXCameraSetup_2`, a setup of its own;
vanilla (90, -22, 25)), the first draw after a load (`SFXCameraSetup_1.SFXCameraMode_Combat_18`, owned at 18:35:22.284
with the same vanilla; the controllers weren't tracked then, so no arm IK line), the first tight aim (`TightAim_7`,
vanilla (50, -20, 15): `body anchor: third person ... 110 units` at 18:23:45.158, back at .191, its 0.08 s blend), and the
first combat crouch (`Combat_19`). The first sprints (`ExploreStorm`, `CombatStorm`) work the same way. The level's first
Explore comes from a cinematic: that blend glides from the cinematic camera, and its write (vanilla (200, 0, 40)) jumped
it 250 units too. Upstream's first person runs the same code, so it has the same jump.

**Built:** a `[MEVR]` seam in Halcyon's first-person tick (`head_aim.cpp`, the Interpolate branch): it reads the
destination's Offset before `FpOwnAndWrite` and then calls `MevrBridge::InterpolateDestinationWritten` (`plugin/
HalcyonBridge.cpp`). When the Offset moved (more than 0.01 on any axis, Halcyon's own test) and the blend isn't complete,
it moves the stored `LocationOffset` by the destination's own move: `LocationOffset - R * (new - old)`, R the
destination's rotation at the start (`From.m_pov.Rotation + RotationOffset`: From isn't ticked, so that is exact), which
is what `InitializeTransition` would have stored with the new Offset (`vr::combat_stance::BlendLocationOffset`,
unit-tested with the sniper's numbers). The view stays at From's camera and glides to To's: with the constant eye both
are the same point, so it doesn't move. `[Hands] blendFix = 1` (default; 0 = upstream), read once a second; logged once
per newly written destination: `camera blend: <interpolate> toward <setup>.<mode> (<class>) at <t> of <total> s: Halcyon
wrote its Offset (...) -> (...) after the blend stored the destination's position; the stored LocationOffset (...) -> (...):
the view stays where it was (it would have jumped N units and slid back), correction #K`. The write goes into the crash
breadcrumbs (`HalcyonBridge.blendLocationOffset`). Host side: a redeploy with the game closed.

Not fixed, by design or for later:
- **The turn.** When the trace hits and the conditions above allow it, `AimAtPoint` turned the controller by the
  parallax between From's camera and the over-the-shoulder one (a few degrees at a few metres) before Halcyon's write,
  and the blend glides the view through that turn. The real fix is writing the destination before
  `InitializeTransition` runs, on the game thread (a pre-call on `SFXCameraMode.Initialize`, which `SetBehavior` calls
  on the new mode just before), with Halcyon's ownership table made safe for it, or owning every first-person mode of
  the player's camera setups ahead of time (the game mode's `CameraSetup +0xD0`, each weapon's `CameraSetup`). Both
  are larger changes to Halcyon; check in the headset first whether the turn is noticeable **[verify]**.
- Between `InitializeTransition` and Halcyon's write (a frame) the game's own blend moves toward the shoulder camera by
  T(1 frame / 0.5 s) of 178 units, a few units (6 at 90 Hz with a quarter-sine curve): not noticeable. If the game thread reads `LocationOffset` while the Present
  thread writes it, one frame can mix them (a race Halcyon's own Offset writes already have).
- From a mode Halcyon doesn't write (cover, the scope) the blend still glides from that camera, as upstream.

## C. Movement: explore vs combat

### What the script does (`BioPlayerController.PlayerWalking`)

`PlayerMove` calls `PlayerMoveStorming` while sprinting, else `PlayerMoveCombat` when
`IsInCombatActionState()` (and not in a cinematic), else `PlayerMoveExplore`, then `ProcessMove`.

| | `PlayerMoveExplore` | `PlayerMoveCombat` |
|---|---|---|
| Speed fraction | stick above the run threshold (0.70): 1; above walk (0.65): `WalkSpeed / GroundSpeed`; below: proportional | the same with `CombatWalkSpeed` (unless `bIsWalking`) |
| Acceleration | `AccelRate` along the **stick's direction** (`MoveRot`) | `AccelRate` × normalised `aForward · X + aStrafe · Y` of the **pawn's** axes |
| Facing | `SetDesiredRotation(MoveRot)`: the pawn turns toward where it runs; standing, it keeps its rotation | `SetDesiredRotation(ControlRotation yaw)` (cover: the cover normal) |
| Crouch | any move stands Shepard up (`ShouldCrouch(false)`) | crouch-walk allowed |

### The numbers

The player's appearance names the CreatureSpeeds row `Demo` (`PLR_Male_Character_Appr.CreatureSpeed`);
`Engine.pcc` `BIOG_2DA_Movement_X.MovementTables_CreatureSpeeds`, row `Demo`:

| Column | Explore | Combat ("Tac") |
|---|---|---|
| GroundSpeed / TacticalSpeed | 400 | 400 |
| WalkSpeed / TacWalkSpeed | 130 | 130 |
| ExploreSprintSpeed / SprintSpeed | 462.5 | 525 |
| MaxDSAccel / MaxDSAccelTac | **1.5** | **3** |
| MaxDSDecel / MaxDSDecelTac | **1.5** | **2** |
| HighTurnSpeed, LowTurnSpeed / CombatTurnSpeed | 270, 270 | 540 |
| TacCrouchSpeed / TacCrouchWalkSpeed | | 190 / 130 |
| AccelRate | 1500 | 1500 |

BioPawn defaults: `StormSpeed 500`, `CombatStormSpeed 525`, `CrouchGroundSpeed`/`CombatGroundSpeed` filled from
the row at run time; BioPlayerController: `MaxNonCombatStormStamina 2.5`, `MaxStormStamina 4.0`,
`StormRegenRateNonCombat 0.375`, `StormRegenRate 0.6`. The row's values land in the pawn behaviour
(`BioPawnBehavior.m_fMaxDesSpdAccelRate +0x4E8`, `DecelRate +0x4EC`, `TacAccelRate +0x4F0`, `TacDecelRate
+0x4F4`, `TacCrouch... +0x4F8/+0x4FC`, `m_fTurnRate* `), transient, set by the native `UpdateDefaultSpeeds`
**[verify when it runs: spawn only, or each state change]**. `GetMaxSpeed` / `GetBaseSpeed` /
`GetWalkingSpeedMode` are native; `EWalkingSpeedMode` has ExploreRun, ExploreWalk, ExploreStorming,
ExploreCrouched, CombatRun, CombatWalk, CombatStorming, CombatCrouched, **CombatZoomed**, CoverMove,
CoverCrouched **[verify each mode's speed in `GetMaxSpeed`]**.

So the top speeds match. What "feels different":
1. **The ramp.** `DesiredSpeed` moves toward the stick's fraction at `MaxDS*` per second
   (`BioPawn.CalcSpeedInterpolation`) **[verify units]**: exploring, 0.67 s to full speed and 0.67 s to a
   stop; in combat 0.33 s and 0.5 s. Combat walking starts and stops twice as abruptly.
2. **Zoomed walking.** Our support grip holds LT (focused fire, [18](18-camera-and-hands.md)), which puts the
   pawn in `CombatZoomed`, very likely slower **[verify]**: two-handed walking would be slow.
3. Facing: the pawn strafes without turning. With body follow the body turns to the view either way, and
   Halcyon's `moveFollowsHead` turns the stick at input level for both, so the direction of travel is the same.
4. Sprint: 525 vs 462.5 (CombatStorm gives 525, `StormSpeed` 500: which the explore sprint uses
   **[verify]**), with more stamina in combat. Gameplay; left alone.

### Proposal

- **`[Hands] exploreRamp = 1`:** each tick in VR on foot, write the player's behaviour's
  `m_fMaxDesSpdTacAccelRate` / `TacDecelRate` = `m_fMaxDesSpdAccelRate` / `DecelRate` (read, not hard-coded),
  remembering the originals and restoring them on 0, VR off and unload. One write per change, journalled.
  Log `movement: combat walking ramps like exploring (accel A → B, decel C → D per s)`.
- **Measure the zoomed walk** before touching it: log the pawn's `GetWalkingSpeedMode` and `Velocity` size
  each second while moving (`movement: mode CombatZoomed, speed N u/s`), drawn, LT held and not. If
  CombatZoomed is slower, the fix is the support grip's LT (set `bIsZoomed` only for the shot, the way
  `GetPlayerAimError`'s post-handler already does for the scope, research/21), not the movement.
- **Not proposed:** calling `PlayerMoveExplore` while drawn, or turning the pawn toward the stick in combat.
  Both need a pre-handler or rewriting `DesiredRotation`, and the pawn's facing may gate shots
  (`BioPawn.m_fWeaponLOSThreshold 0.666` **[verify what reads it]**). Only worth it if A's sliding legs bother.

### Built (2026-10-25)

- `[Hands] exploreRamp = 1` (default), the mod: in VR on foot, `game/CombatStance` writes the player's
  `BioPawnBehavior.m_fMaxDesSpdTac{Accel,Decel}Rate` to `m_fMaxDesSpd{Accel,Decel}Rate` (read each tick; written only
  when they differ: `vr::combat_stance::RampNeedsWrite`), remembers the game's values (again if the game sets its own
  since, e.g. `UpdateDefaultSpeeds`), and puts them back (if still ours) when off, out of VR or on foot, on a new pawn and
  at unload. `movement: combat walking ramps like exploring (accel 3.00 -> 1.50, decel 2.00 -> 1.50 per s)`.
- The measurement: `BioPawn.GetWalkingSpeedMode` (native, through ProcessEvent) and the pawn's horizontal `Velocity`,
  one line per mode change: `movement: walking mode CombatZoomed (drawn, the support grip holds the gun (LT)), speed N
  u/s; CombatRun held N s, top speed N u/s`. Nothing about the zoomed walk is changed until those lines are in.

## D. The draw and holster delay

### What plays and what gates it

- **Drawing** from explore is `BioPawnBehavior.SetActionState(1)` (`SFXPawn_Player.DrawPrimaryWeapon`,
  `SFXGameModeBase.FireWeapon` when holstered, `SwitchToWeapon`); holstering is `SetActionState(0)`
  (`TryHolster`, after `ReleaseActive` / `CancelActive`). Switching while drawn acquires a
  `BioActiveSelectWeapon` (states None, Wait, Draw, Holster, Switch, FadeOut, Finishing, Finished; fields
  `m_oSelectWeaponAnimNode +0x98`, `m_fDelay +0xA0`, `m_state +0xA9`, the `m_isWeaponDrawn` bits `+0xA4`).
  The plain draw from explore is very likely the same active **[verify]**.
- The animation: the `WeaponEquip` node (`BioAnimNodeBlendWeaponEquip_3`: `Holster` = CB_Holster,
  `Draw` = CB_Draw, `None`), an additive inside the Combat branch. Its sequences come from the weapon's or the
  body's draw animset (`SFXWeaponStatsBase.DrawAnimset`, `Bio_Appr_Character_Body.WeaponAnimSpec.m_drawAnimSet`).
- **The notifies** (`AnimNotify_Script`, bIgnoreWeightThreshold 0), handled by `BioPawn.SwapDrawAnim` etc.,
  which forward to the current modal `BioActiveSelectWeapon` (natives):

| Sequence | Length | SwapDrawAnim / SwapHolsterAnim | FadeOut... | Finish... |
|---|---|---|---|---|
| `HMM_CB_ShotgunDraw` CB_Draw | 1.17 s | 0.41 | 0.67 | 0.97 |
| `HMM_CB_SniperDraw` CB_Draw | 1.33 s | 0.33 | 0.80 | 1.10 |
| `HMM_CB_ShotgunHolster` CB_Holster | 1.17 s | 0.61 | 0.87 | 1.17 |

  Pistol and assault rifle (`HMM_CB_PistolDraw`, `HMM_CB_RifleDraw`, ...) are imported by the player
  package from `BIOG_HMM_CB_A`, which isn't in SFXGame, Engine or Startup: not found this session
  **[verify their times]**.
- **Gates:** Swap moves the gun from its holster socket into the right hand (until then our hand is empty,
  `game/Weapon` finds no weapon there). While the active runs, `IsSwitchingWeapons()` is true: script refuses
  sprint (`BioPlayerInput.PostProcessInput`), zoom (`bForceNoZoom`), melee, weapon switches and holstering.
  Firing: `SFXGameModeBase.FireWeapon` only sets `bFire`; the native fire path refuses while switching
  **[verify: `BioPawn.CanFireWeapon` (native) in Ghidra]**. The native `LeftArm` IK is switched off at the
  start and back on at FadeOut or Finish ([18](18-camera-and-hands.md)). Our arm IK doesn't care.

### Proposal

- **First measure:** `game/Holsters` already reads `IsSwitchingWeapons`: log
  `holsters: drawn: the pistol could fire after N s (IsSwitchingWeapons from true to false), the gun in the hand after M s`
  per draw and holster, per weapon type.
- **`[Hands] drawRate = 2.5` (1 = the game's):** each mod tick, while the player's `WeaponEquip` node plays
  `Draw` or `Holster`, set that child sequence's `AnimNodeSequence.Rate` (`+0x110`) to `drawRate`, and back
  to 1 when it stops (the native may reset Rate when it starts the animation: write it while `bPlaying`,
  `+0x130` bit 1). The node is found like A's (`NodeName = WeaponEquip`) or read from the active
  (`m_oSelectWeaponAnimNode`). At 2.5 the shotgun's gun reaches the hand at 0.16 s and firing is allowed at
  0.39 s. Notifies fire in order even when several fall in one tick. Player only (the sequence assets are
  shared with squadmates, so not `AnimSequence.RateScale`).
- **Fallback, if the rate is ignored:** at the draw's start (`IsSwitchingWeapons` turning true, our own
  `SwitchToWeapon` or a holstered trigger pull), call `BioPawn.SwapDrawAnim`, then on later ticks
  `FadeOutDrawAnim`, `FinishDrawAnim`, through ProcessEvent on the game thread: what the notifies do. A late
  notify from the still-playing animation then only logs "Failed to forward function to
  BioActiveSelectWeapon" (the pawn's version checks the modal active). **Risk:** the native state machine may
  need `m_bHasAnimTicked` / the Draw state first **[verify in Ghidra before building]**.
- With A on, the faster draw is invisible anyway (the additive isn't composed), so only the timing matters.

### Built (2026-10-25)

- **The measurement:** each draw, holster or switch (the action state changing, or `IsSwitchingWeapons` rising) is
  followed until it settles: `draw: drew the shotgun: action state changed at 0.00 s, IsSwitchingWeapons true for N s
  (from T s), the gun reached the hand at M s; the WeaponEquip sequence played at rate R to S s of its time`. The gun
  "in the hand" is `game/weapon`'s verified weapon on the right hand (a holster: when it's gone).
- **`[Hands] drawRate = 2.0`** (default; 1 = the game's, at most 4): each tick in VR on foot, every `Draw` / `Holster`
  sequence under the player's `WeaponEquip` node that `bPlaying` gets `Rate = drawRate` (written again whenever the
  native resets it), and its own rate back once it stops (or the setting goes to 1, or at unload, when the tree is still
  the player's). Player only (the node is in the player's tree instance; the `AnimSequence` assets are untouched). The
  notify functions are never called (the fallback above is not built). Whether the native honours `Rate` (and whether
  `BioActiveSelectWeapon` has a timer of its own) is the headset's to say: the `draw:` lines at 1 and at 2.
- With 2.0 instead of the proposed 2.5: the user's request named 2 as the default.

## In the headset (one session, all logs)

1. **B, step 1:** set the three `MELEVR.ini` rows (or our menu's Advanced), draw and holster standing, aim
   (support grip), sprint: the view should stay put. Snapshot before and after; the `camera: mode` lines'
   "N up of the game camera" should read 94.9 everywhere.
2. **D:** draw each weapon from each holster and holster it again: the new `holsters: drawn ...` lines give the
   real delays (and the pistol/rifle notify times indirectly).
3. **C:** walk drawn and holstered, with and without the support grip: the `movement:` lines (speed mode,
   speed).
4. **A** (after it's built): look down while drawn, standing, walking forward and strafing; crouch-walk
   (should keep the combat crouch); throw a grenade and use a power (they must still work: the notifies);
   fire while walking.

## Not found

- Pistol and assault rifle draw/holster sequences: not in `SFXGame.pcc`, `Engine.pcc`, `Startup_RA.pcc`.
- Movement speeds in `Coalesced_INT.bin`: none (they're the CreatureSpeeds 2DA, not config).
