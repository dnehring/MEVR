<!-- SPDX-License-Identifier: MIT -->
# VRIK: how it is built in LE1, and what porting it to LE2 and LE3 takes

This is a map of MEVR's body and hands system ("VRIK") as it runs in Mass Effect 1 (LE1). It is for someone who wants the
same system in the other Legendary Edition games. It says where each piece lives, how the pieces talk to each other, which
parts are game-independent and which are LE1-specific, and what the guided calibration does.

Paths are relative to the repository root. The research notes cited (`research/NN-*.md`) hold the measurements and the
reasoning behind each decision; read them before changing behaviour.

## 1. What VRIK does

- **Arms:** the player's controllers drive Shepard's arms. The animation poses the body; after it, we rewrite the arm bones
  so the hands land on the controllers (two-bone IK, elbows placed by a bend plane). Everything below the hand (fingers, the
  gun) follows rigidly.
- **The gun in the hand:** the held weapon's barrel points where the right controller aims, using the player's calibrated
  grip frame. Shots leave the muzzle along the barrel, so the model, the shots and the reticle agree.
- **Two-handed grip:** the left grip squeezed on the gun's foregrip holds the gun with both hands. The barrel then runs from
  the right hand toward the left.
- **Torso and body:** the spine bends and twists so the shoulders sit under the headset. The whole body turns and steps
  under the view, the legs keep the run animation while moving, and Shepard is scaled to the player's height.
- **Extras built on the same pose writer:** fingers that follow the triggers and touch sensors, the gun's visual kick,
  holsters on the body, the physical vent, grenades and melee, physical crouch.

## 2. The layers

The code is split so that the maths never touches the engine and the engine access never decides behaviour.

| Layer | Folder | What it is | Engine-specific? |
|---|---|---|---|
| IK maths | `src/core/ik/` | Pure functions on vectors and whole-skeleton poses, unit-tested | No |
| VR rules | `src/core/vr/` | Calibration solve, sights, ADS, comfort, haptics: pure, unit-tested | No |
| OpenXR | `src/core/xr/` | Actions, poses, floor (render thread, host side) | No (OpenXR only) |
| Engine access | `src/engine/`, `src/ue3/` | Reflection layer, hooks, bone layouts, addresses | Yes: per game |
| Game features | `src/game/` | Ties the rules to the game's classes and functions | Yes: mostly LE1 |
| Host | `src/plugin/` | The ASI that owns the renderer, OpenXR, the menus and the calibration UI | Partly (LE1 uses Halcyon's renderer) |

The host (`LE1MEVR.asi`) and the mod (`LE1MEVR_mod.dll`, hot-reloaded in development builds) talk through
`HostServices`, declared in [`src/mod/ModApi.hpp`](../src/mod/ModApi.hpp). Every field there is versioned and size-checked.

## 3. Data flow, one game tick

```
render thread (host)                         game thread (mod tick, src/mod/Mod.cpp)
---------------------                        ----------------------------------------
OpenXR: controllers, head, floor   ──HostServices──►  body_calibration::Tick   (calibration file + host state)
(core/xr/Input, core/xr/Floor)                        weapon::Tick             (which gun, its muzzle and grip)
host view (core/vr/HostView)                          body_follow::NoteFloor   (recenter counter)
                                                      gun_kick::Tick
                                                      arm_ik::Tick ──► hand_targets::Current
                                                          │              └► view_anchor::Current
                                                          │                   (tracking space → world)
                                                          ├► body_follow, body_scale, fingers
                                                          └► bone_pose::Publish(Request)
                                                      holsters / throwables / vent / squad ...
                                                      support_grip::Tick
                                                      hand_aim::Tick ──► setAimRay (host: reticle)
                                                      sights (via hud and hand aim) ──► setSight (host)

engine (any thread that poses the mesh)
USkeletalMeshComponent::UpdateSkelPose ──post-hook──► engine/BonePose: reads the latest Request,
                                                      rewrites SpaceBases (torso, arms, support grip,
                                                      gun kick, fingers, legs, body scale)
```

The order in `src/mod/Mod.cpp` matters: the weapon is read before arm IK (arm IK needs the grip), the gun kick before arm
IK (it goes into the right hand's goal), the support grip and hand aim after it (they read what arm IK asked for).

## 4. Where everything lives

### 4.1 Getting controllers into the world

- [`src/core/xr/Input.hpp`](../src/core/xr/Input.hpp): our OpenXR action set and bindings (Touch, Index, Reverb G2, WMR,
  Vive wands, the simple controller), attached to whichever session owns OpenXR. Render thread.
- [`src/core/vr/Controllers.hpp`](../src/core/vr/Controllers.hpp): the per-frame controller state (grip and aim poses,
  buttons, touch sensors), handed from the render thread to the game thread through `HostServices::controllers`.
- [`src/game/ViewAnchor.hpp`](../src/game/ViewAnchor.hpp): **the one place that maps tracking space to the world.** With
  our own renderer (LE2/LE3 today) it uses the render path's anchor from `game/HeadTracking`; with the host's renderer (LE1:
  Halcyon's) it uses the host's published view placed on the game camera. Fails closed without a head-tracked view.
- [`src/engine/XrToUnreal.hpp`](../src/engine/XrToUnreal.hpp): OpenXR (right-handed, -Z forward) to UE3 (left-handed,
  +X forward, +Z up) conversions, units per metre.
- [`src/game/HandTargets.hpp`](../src/game/HandTargets.hpp): where Shepard's hands should be this tick: each grip pose in
  the world, the wrist 7 units behind the grip. Chooses the anchor: first person (the view's own frame), third person
  (tracking origin at Shepard's animated head bone), or none. **The only file that knows the hand source.**

### 4.2 The arm IK driver and the bone writer

- [`src/game/ArmIk.hpp`](../src/game/ArmIk.hpp) / `.cpp`: each tick on foot, finds the player's body mesh, resolves both
  arm chains by bone name (once per skeleton), checks the bone layout against the engine's own `GetBoneLocation`, turns the
  hand targets into component space and builds the writer's `Request`. Holding a gun, the right hand is turned so the
  barrel follows the calibrated grip frame. Anything that doesn't resolve stops all writes.
- [`src/engine/BonePose.hpp`](../src/engine/BonePose.hpp) / `.cpp`: **the writer.** A post-hook on
  `USkeletalMeshComponent::UpdateSkelPose` (LE1 RVA in [`src/engine/Addresses.hpp`](../src/engine/Addresses.hpp)): after
  the AnimTree has composed `SpaceBases` (component space), it rewrites the torso before the arms (so they reach from
  where the shoulders end up), then the arms, the support grip, the gun kick and the fingers, and scales the body last
  (the seated Mako driver's legs use the same request). The game thread `Publish`es a `Request`; the hook only reads it,
  and writes nothing unless the mesh, its owner, the world, the bone count and the chain names still match and the request
  is recent. One journalled write per call (`core/WriteJournal.hpp`, crash breadcrumbs).
- [`src/engine/Skeleton.hpp`](../src/engine/Skeleton.hpp): reading bones from engine memory: `RefSkeleton` (names,
  parents), `SpaceBases` (`FBoneAtom`, 32 bytes), `LocalToWorld`, sockets, attachments. Fields come through the reflection
  layer; the element layouts are research/14's LE1 layouts. `Supported()` is false for LE2/LE3 today.
- **Never write the `Camera` or `God` bone** ([`src/core/ik/CameraHook.hpp`](../src/core/ik/CameraHook.hpp),
  research/18): LE1's camera hangs off them through `SpaceBases`, so moving them moves the view and loops. The writer keeps
  them (`Request::keep`). Check this for each game's player skeleton.

### 4.3 The maths (game-independent, `src/core/ik/`)

All in OpenXR LOCAL space (metres) or component space, unit-tested in `tests/Main.cpp`:

| File | What |
|---|---|
| [`Ik.hpp`](../src/core/ik/Ik.hpp) | Body facing, shoulders, elbow placement for a hand target |
| [`ArmPose.hpp`](../src/core/ik/ArmPose.hpp) | Arm IK on a whole skeleton pose, carrying everything below the upper arm (adapted from Overlord, GPL-3.0) |
| [`TorsoPose.hpp`](../src/core/ik/TorsoPose.hpp) | Spine bend and twist so the shoulders sit under the head |
| [`BodyFollow.hpp`](../src/core/ik/BodyFollow.hpp) | Head pivot, feet that step, body yaw, the legs keeping the run |
| [`BodyScale.hpp`](../src/core/ik/BodyScale.hpp) | Shepard at the player's size, goals mapped through the inverse |
| [`HandFrame.hpp`](../src/core/ik/HandFrame.hpp) | Grip pose to hand bone rotation, from the skeleton's own finger bones |
| [`TwoHand.hpp`](../src/core/ik/TwoHand.hpp) | The support grip: grab zone, two-hand rotation, the shoulder zone |
| [`Fingers.hpp`](../src/core/ik/Fingers.hpp) | Finger curls about flexion axes measured from the rest pose |
| [`GunKick.hpp`](../src/core/ik/GunKick.hpp) | The gun's visual recoil spring |
| [`Rigid.hpp`](../src/core/ik/Rigid.hpp) | Quaternions and `FBoneAtom`-convention transforms |
| [`BoneNames.hpp`](../src/core/ik/BoneNames.hpp) | Finding hands, sides and parts in any naming convention |
| [`PoseGraft.hpp`](../src/core/ik/PoseGraft.hpp) | Putting the combat pose's arms onto the explore pose |
| [`ArmLength.hpp`](../src/core/ik/ArmLength.hpp), [`BodySlots.hpp`](../src/core/ik/BodySlots.hpp), [`Posture.hpp`](../src/core/ik/Posture.hpp), [`Throwing.hpp`](../src/core/ik/Throwing.hpp), [`Vent.hpp`](../src/core/ik/Vent.hpp), [`TouchReference.hpp`](../src/core/ik/TouchReference.hpp) | Reach scaling, holsters, crouch, grenades and melee, the vent, the Touch controller's frame |

### 4.4 The weapon, shots and the two-handed grip

- [`src/game/Weapon.hpp`](../src/game/Weapon.hpp): the held ranged weapon as the game draws it: `Pawn.Weapon` (an
  `SFXGame.BioWeaponRanged` in LE1), its mesh, the muzzle socket, the barrel, and the **grip** (muzzle and barrel in the
  right hand bone's axes, measured while the hand holds still). Verified once per weapon against the game's own
  `GetMuzzleLoc`. Takes the gun kick back out of every reading. Mostly `#if defined(MEVR_GAME_LE1)`.
- [`src/game/HandAim.hpp`](../src/game/HandAim.hpp): rewrites `BioPlayerController.GetAdjustedAimFor` (and the shot's
  start trace) so shots follow the gun's barrel from its muzzle; publishes the aim ray for the host's reticle.
- [`src/game/SupportGrip.hpp`](../src/game/SupportGrip.hpp): the two-handed grip: the foregrip per gun (a weapon socket, the
  body's `LHIK` bone, the animated left hand, or under the grip), the hold's blend, the game's aimed fire (LT) while held.
- [`src/game/GunKick.hpp`](../src/game/GunKick.hpp), [`src/game/Fingers.hpp`](../src/game/Fingers.hpp),
  [`src/game/BodyFollow.hpp`](../src/game/BodyFollow.hpp), [`src/game/BodyScale.hpp`](../src/game/BodyScale.hpp),
  [`src/game/Holsters.hpp`](../src/game/Holsters.hpp): the extras of section 1, each feeding the writer's `Request` or the
  game's functions.
- [`src/game/HandCalibration.hpp`](../src/game/HandCalibration.hpp): every `[Hands]`, `[Holsters]`, `[Sights]` setting in
  `<GAME>MEVR.ini`, re-read within a second of a change. Despite the name, this is the settings reader, not the guided
  calibration.

### 4.5 The guided calibration

The calibration measures the player, not the guns: any gun is then placed by its own sight geometry.

- **Solve:** [`src/core/vr/Calibration.hpp`](../src/core/vr/Calibration.hpp) (pure, unit-tested). The player grabs an orb
  that stands for a gun and aims it at three targets. The sight line (aiming eye to target) is the gun frame's forward, the
  world's up made perpendicular to it. Expressed in the right controller's **grip** frame (not its aim pose, which differs per
  runtime), it is the **trim**: the barrel follows `grip * trim`. The line's nearest point to the grip says where the sight
  belongs relative to the hand. Aims that disagree are redone; the agreeing pair is kept after two tries.
- **Sequence and UI (host):** [`src/plugin/Calibration.hpp`](../src/plugin/Calibration.hpp) and `CalibrationDraw`: about a
  minute in the headset: stand tall, T pose and arms ahead (arm length, shoulder width, each empty hand's neutral trim),
  crouch (or seated), the pistol orb's three aims, the rifle orbs' three aims with both hands (the foregrip, the left trim, the
  stock point), a red-dot check, save. Samples are taken on a one-second hold, no buttons. While it runs the game sees a
  neutral pad and the mod sees no controllers (`HostServices` v15). It is driven from the LE1 host's overlay path
  (`src/plugin/HalcyonOverlays.cpp`).
- **Storage:** `[Calibration.<key>]` in `ASI\<GAME>MEVR.ini`, one section **per controller type** (interaction profile plus
  headset, `vr::calibration::ProfileKey`). Keys: `standEyeM`, `crouchEyeM`, `armLengthM`, `shoulderWidthM`, `eye`,
  `pistolTrim`, `rifleTrim`, `pistolLine`, `rifleLine`, the spreads and eye reliefs, `rifleForegrip`, `rifleLeftTrim`,
  `rifleHandsLine`, `rifleStock`, `handTrimLeft`, `handTrimRight`. A save keeps groups this run didn't measure.
- **Use (mod):** [`src/game/BodyCalibration.hpp`](../src/game/BodyCalibration.hpp) reads it and offers:
  - `StanceFor(weaponType)`: the pistol stance for pistols, the rifle stance for long guns (the pistol's grip frame if no
    rifle stance). Used by arm IK (the barrel's frame, the sight moved onto the calibrated line), sights and holsters.
  - `TwoHanded()`: the rifle stance with both hands (support grip: the calibrated span between the hands).
  - `ShoulderZone()`: a zone around the calibrated stock point (support grip's aimed fire; sights: the scope or dot on at once
    at the shoulder, research/21 section 20).
  - `HandTrim(side)`: each empty hand's neutral frame (arm IK's empty hands: `grip * trim`).
  - `CrouchParams`, `StandingEyeHeight`: physical crouch thresholds, holster heights, body scale.
- `[Calibration] use = 0` or no section: everything runs as before the calibration existed. Research: `research/25-calibration.md`.

### 4.6 Reading the engine

- **Reflection layer** [`src/ue3/`](../src/ue3/) (research/34): `ue3::ClassRef`, `FieldRef`, `FunctionRef`, `ue3::Params`,
  `ue3::runtime::Call`. Every class, field, function or parameter resolved needs a row in
  [`src/engine/ReflectCheck.cpp`](../src/engine/ReflectCheck.cpp); a failed lookup stands the feature down with a log line.
- **Raw addresses** live only in [`src/engine/Addresses.hpp`](../src/engine/Addresses.hpp), each with a byte pattern
  checked at start-up (`tools/rva-patterns.py`; ctest `mevr-rva-table`). LE2/LE3 have their own tables there, mostly `0`
  (not found yet).
- **Per-game spellings** (game modes, field names that differ) go in
  [`src/engine/GameAdapter.hpp`](../src/engine/GameAdapter.hpp).
- **Calling game functions:** always through `ue3::runtime::Call` (ProcessEvent; it clears natives' opcode index for the
  call) and **only on the game thread**. Never call LExSDKv2's generated wrappers; LExSDKv2 is being removed (no licence) and
  must not be added to new code paths.

## 5. What is LE1-specific today

| Piece | Why it doesn't run in LE2/LE3 yet |
|---|---|
| `engine/BonePose` hook | `kUpdateSkelPose` is `0` in the LE2/LE3 address tables: the function and its pattern aren't found yet |
| `engine/Skeleton` | `FBoneAtom` and `FMeshBone` element layouts verified for LE1 only; `Supported()` is false elsewhere |
| `game/Weapon`, `HandAim`, `SupportGrip` | LE1's `BioWeaponRanged`, `BioPlayerController.GetAdjustedAimFor` and the muzzle functions; LE2/LE3 use `SFXWeapon` classes and different aim functions |
| `game/Holsters`, `Throwables`, `Vent`, `Posture` | LE1's weapon switch, grenade, heat and cover functions |
| Bone names and hook bones | `RightWrist`, `LHIK`, `Prop01`, `Camera`, `God` are LE1's player rig; the camera hook bones (`core/ik/CameraHook`) must be re-checked per game |
| Calibration UI | Drawn by the LE1 host's overlay path on Halcyon's renderer; LE2/LE3 hosts use our own renderer (`core/vr/Output`) |
| Halcyon's renderer | `src/halcyon/le1` is ME1's; LE2/LE3 still use our renderer until Halcyon's ME2/ME3 code is brought in |

What carries over unchanged: everything in `src/core/ik/` and `src/core/vr/`, `src/core/xr/Input`, `game/HandTargets`,
`game/ViewAnchor` (already supports our renderer), the `Request` structure and the writer's stages, the calibration solve and
its file format, and the `HostServices` contract.

## 6. A porting plan for LE2 or LE3

Work one stage at a time; each stage ends with something measurable in the log.

1. **Find `UpdateSkelPose`** in the target exe with Ghidra (`tools/ghidra/decompile.ps1`), add its RVA and a pattern to the
   game's table in `engine/Addresses.hpp`. Confirm the same post-hook point (after `ComposeSkeleton`, before anything reads
   `SpaceBases`) as research/14 does for LE1.
2. **Verify the bone layouts** (`FBoneAtom` size and order, `RefSkeleton` element, `SpaceBases` offset) and turn on
   `skeleton::Supported()` for that game. `arm_ik` already cross-checks the layout against the engine's `GetBoneLocation`
   once per mesh and stops if they disagree; that line in the log is the proof.
3. **Map the player skeleton:** arm chains (`ArmIk` resolves by name, `core/ik/BoneNames` helps), spine and clavicles, the
   camera hook bones to keep, the hand and finger bones, the off-hand IK goal if the rig has one.
4. **Arms first, no gun:** hand targets from the controllers through `view_anchor` (our renderer's anchor), the writer posing
   both arms. Check with the test loop (snapshots, a recorded controller replay) before touching weapons.
5. **The weapon:** port `game/Weapon`'s reads (the held weapon, its mesh, the muzzle socket, the attachment bone) to the
   game's weapon classes, behind `GameAdapter`/reflection rows, then `HandAim`'s aim and start-trace rewrites.
6. **Calibration:** the solve and the `[Calibration.<key>]` file are game-independent, so an LE1 calibration file's values are
   valid for the same player and controllers. What's needed is the sequence's UI on the LE2/LE3 host's renderer (or a shared
   overlay path), and `body_calibration` wired into the new game's mod.
7. **Then the extras**, each behind its own setting: support grip, fingers, body follow, body scale, holsters, the gun kick.

## 7. Rules that bit us

- Only write `SpaceBases` from the writer, after the animation, and never the camera's bones.
- Never run UnrealScript (ProcessEvent) off the game thread; the writer's hook only reads the published request.
- Every new write into engine memory gets a `write_journal::Wrote(...)` right after it.
- Fail closed: if any lookup, layout check or anchor is missing, write nothing and log why once.
- `docs/knowledge/gotchas/index.md` lists every problem that bit twice; read it first when a symptom sounds familiar.

## 8. Further reading

- `research/14-vrik.md`: the writer, bone layouts, the weapon and muzzle (the base of everything here).
- `research/18-camera-and-hands.md`: the camera hook bones, why `Camera` and `God` are never written.
- `research/25-calibration.md`: the guided calibration's design and measurements.
- `research/29-body-follow.md`, `research/58-body-scale.md`, `research/43-fingers.md`, `research/19-body-holsters.md`,
  `research/21-sights-and-scopes.md`: the extras.
- `research/34-reflection.md`: the reflection layer and the LExSDKv2 removal.
- `docs/trilogy-design.md`: the one-system-for-three-games principle; `docs/development-approach.md`: how we work (Ghidra,
  the test loop, parallel sessions).
- `CLAUDE.md`: the current state of every LE1 feature in one place.
