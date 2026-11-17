<!-- SPDX-License-Identifier: MIT -->
# Thermal clips you can see: ME2 and ME3 (lane TC, 2026-11-16)

The reload by hand on the sequels (research/le2-vrik.md "The reload by hand: the game's side", `game/Reload`,
`core/vr/ReloadGesture`) works, but nothing is seen: no clip in the hand, nothing going into the gun. The owner: the flow
"sounds very counter intuitive if we won't even see the asset", and later the same evening "putting my left hand to the hip
makes no sense ... don't enable that flow" (the coordinator: the hand reload goes off by default). This note answers what
the games have to show, how a clip can be shown, and lists the reload gestures as **options for the owner to choose from**;
it recommends no gesture.

How it was read: the installed packages, read only, with Legendary Explorer's library (`LegendaryExplorerCore` from the
LEX nightly in `mod-manager/`) through a private scanner in the lane's scratch folder (modes: exports by class, properties,
skeletons and sockets, static mesh bounds, decompiled script for reading). No game data, mesh, texture or script text is in
the repository; script behaviour is described in words. One existing snapshot was looked at (below); no new game run was
made (the run lock was held by lane OM2 the whole time).

## The answer

- **Neither game ever draws a clip in Shepard's hand or going into the gun.** The only clip either game shows is the OLD
  one: one mesh particle (`BioVFX_C_Reload.Mesh.HeatSink`) thrown out of the gun's `Cooldown` socket with sparks, smoke
  and a steam vent, at a fixed fraction of the reload (ME2 0.75 s after the reload begins, every weapon; ME3 per weapon,
  0.34-1.38 s). Shepard's reload animations carry no mesh, no attach notify and no clip bone. Alien guns eject nothing,
  they only vent heat (ME2's Geth Pulse Rifle; ME3's Collector and Geth rifles, Geth shotgun, Scorpion, Thor, Disciple,
  Graal and Javelin): the owner decides whether those get a clip too.
- **Two of the game's own meshes can be put in the hand**, both already loaded in gameplay:
  `BioApl_Uti_AmmoClip01` (the dropped thermal clip pickup, in `SFXGame.pcc`, always loaded, lit, about 10 x 30 x 10 cm)
  and `BioVFX_C_Reload.Mesh.HeatSink` (the ejected sink, in the package of every player gun that ejects one, unlit and
  glowing, 4-7 cm at the eject's scale).
- **So the player can see a clip in the hand and see it go into the gun**, with a prop of ours made from the game's mesh
  (the LE1 cockpit's proven route: a spawned `InterpActor` with `SetStaticMesh`), held at the left hand and slid into the
  gun's `Cooldown` socket as the game's reload begins; the old clip leaving is the game's own eject. **Cost: one lane, about
  a day** for the shared prop on both sequels (5-7 h with the ME2 check, 1-2 h more for LE1's same-pose check because the
  spawn helper moves out of LE1-only code), plus **1-6 h for whichever gesture the owner picks** (table below). ME3's
  in-game check waits for a combat fixture (docs/trilogy-plan.md T05), like the rest of ME3's reload.
- Nine weapon models have a clip-like PART of their own (`Clip`, `Magazine`, `ChargeClip`, `Reload_Latch_*`,
  `Clip_Flap_*`, `RearClip`); those could move with the hand like ME1's vent rack. The other 64 models have none.

## 1. The assets

Units: about 1 cm each (Shepard's forearm bone, elbow to wrist, is 26.3 units in ME2's `HMM_ARM_HVYa_MDL`).

| Asset | ME2 | ME3 | What it is | Size | Material | Loaded |
|---|---|---|---|---|---|---|
| The pickup clip | `BioApl_Uti_AmmoClip01.Meshes.AmmoClip01` (StaticMesh, `SFXGame.pcc`) | `BioApl_Uti_AmmoClip01.AmmoClip01` (`SFXGame.pcc`) | the model enemies drop (`SFXDroppedAmmo`, `SFXAmmoContainer`), 196 vertices, simple box collision | bounds origin (0, 0, 5.0), extent (5.1, 15.0, 5.0): 10 x 30 x 10, standing on its base | `BioApl_Uti_AmmoClip01[.Materials].AmmoClip01_MatInst` (parent `BIOG_APL_MASTER_MATERIAL.Placeable_MM`: lit, diffuse/mask/normal textures, `Emissive_color` and `Simple_Specular_color`); `Clip_MM` and `Clip_MM_Disabled` are the pickup's own | always: `Default__SFXDroppedAmmo.DroppedAmmoMesh0` (the class's default object in `SFXGame`) references it |
| The ejected heat sink | `BioVFX_C_Reload.Mesh.HeatSink` (144 vertices; extent 23.1 x 28.1 x 41.1) | same name, another model (178 vertices; extent 43.0 x 16.5 x 30.8) | the mesh of the eject's `HeatSink` emitter | at the eject's start size: ME2 1.9 x 2.2 x 6.6 (x the socket's scale: 0.62 on the Predator, so about 1.2 x 1.4 x 4.1 [verify]); ME3 6.0 x 2.0 x 4.3 | `BioVFX_C_Reload.Materials.HeatSink`: unlit (a glow), flagged for sprites, beams and sub-UV; the static-mesh path is the default usage [verify that it draws on a `StaticMeshComponent`] | with the weapon: in all five ME2 `SFXWeapon_*.pcc` (the name also in three `SFXHeavyWeapon_*.pcc`); in 37 of ME3's 45 player gun packages (not the eight vent-only guns) |
| The eject effect | `BioVFX_C_Reload.Particles.Reload_HeatSinkEject` (each weapon's `ShellCasingPSC0`, except the Geth Pulse Rifle: `Reload_VentHeat_Geth_2`, and the MiniGun: `Reload_VentHeat`) | same name, except eight vent-only guns: Collector AR and Graal `Reload_VentHeat`; Geth AR, Scorpion, Thor, Javelin `Reload_VentHeat_Geth_2`; Disciple `...Reload_VentHeat_Disciple`; Geth shotgun `...Reload_VentHeat_GSG` | emitters `HeatSink` (one mesh particle: a burst of 1, 2 s life, 200-300 units/s along the socket's X with up to 50 sideways, falling at 1000-2000 units/s², 2 bounces at 0.3, spinning up to 3 rad/s), `sparks`, `distort_Dup_Dup_Dup`, `Smoke`, `Smoke_Dup`, `glow`; sounds `Wwise_VFX_Reload.Play_vfx_reload_eject_shell`, `Play_weapon_reload_heatsynch` | | | with the weapon (each weapon's `ShellCasingPSC0`) |
| The vent | ME2 `Default__SFXWeapon.ReloadVent0`, template `BioVFX_C_Reload.Particles.ClipEnd_VentHeat` | each weapon's `ReloadVent0` | steam from the port | | | with the weapon |
| An ammo crate | none found by name | `BioApl_Con_AmmoBox04.Ammo_Box04_A` (`SFXGame.pcc`, 4,186 vertices, 90 x 326 x 57) | a world prop, not a hand prop | | | always |

**Shepard carries clips in the fiction?** ME2's armour has a left-forearm ammo piece (`SFXGameEffect_Gear_LeftArmAmmoPack`
in `SFXGame.pcc`, next to `..._HeavyDampingGauntlets` and `..._StabilizationGauntlets`) [verify its shop name and whether
it has a visible mesh]. No clip mesh or "ammo" bone is on Shepard's body models: the armour's sockets are the hands' props
(`Socket_01` on `Prop01`, `Socket_02` on `Prop02`) and the weapons' places on the back (`Socket_06` on `LowerBack`,
`Socket_05` on `Chest`, `Socket_03/04/07` on `Chest2`). ME3: nothing by name.

## 2. What each game does in a reload today

**ME2** (`SFXGame.pcc`, `SFXWeapon`): the button's `TryReload` puts the weapon in state `StartReload` (waits while the
pawn leans out of cover or is in an animated transition), then `Reloading`. `Reloading.BeginState` sets three timers from
the duration (`GetReloadDuration`: `ReloadSuccessDuration` or `ReloadFailureDuration`, **1.5 s for every one of the 16
weapon classes in the base game's five weapon packages**: `Default__SFXWeapon` sets 1.5 and none overrides it) and `EjectShellCasingTimeRatio` (0.5):
`DoEarlyReload` at 0.6 x 0.5 x 1.5 = 0.45 s, **`DoReload` at 0.75 s**, `FinishReloading` at 1.5 s. `DoReload` moves the
ammo and calls **`PlayReloadEject`**, which un-hides and activates `PSC_ShellCasing` (the eject effect) and `PSC_ReloadVent`
and hides both 5 s later (`HideReloadEmitters`). Both components sit on the gun's **`ShellCasingSocketName = Cooldown`**
socket (`AttachWeaponTo` -> `AttachMuzzleEffectsComponents`; `Root` if the model has no such socket); the muzzle effects on
`MuzzleSocketName = Flash_2`. The pawn plays `BIOG_HMM_CB_A.HMM_CB_<Family>Auto_CB_Reload` (pistol 1.5 s, 45 frames, in
`Startup_INT.pcc`; rifle 1.37 s; shotgun and sniper and the `_override_` variants in `BIOG_Male_Player_C.pcc`): notifies are
foley sounds and the script notify `FadeOutReloadAnim` at 1.2 s; its tracks cover the whole upper body, both hands' fingers,
**`Prop01` (the gun's bone, under `RightWrist`) and `Prop02` (the left hand's prop bone, under `LeftWrist`)**, `LHIK`/`RHIK`.

**ME3** (`SFXGame.pcc`, `SFXWeapon`): `Reloading.DoAReload` plays `WeaponReloadSound`, sets **`DoReload` at
`ReloadDuration x EjectShellCasingTimeRatio`** and calls `BioPawn.ReloadWeapon()` (the pawn's animation); `DoReload` moves
the ammo and calls `PlayReloadEject` (the same two components, plus the `EjectRumble` force feedback). The same `Cooldown`
and `Flash_2` defaults. The reload animation set comes with each weapon (`ReloadAnimInfo` -> `SFXAnimSetCookSpec`, e.g.
the Predator's `BIOG_HMM_CB_A.HMM_CB_PistolAuto_AlternateFast`). Per weapon:

| Weapon | `ReloadDuration` | ratio | the old clip leaves at | animation and its notifies |
|---|---|---|---|---|
| Pistol Predator | 0.7 s | 0.563 | 0.39 s | `PistolAuto_AlternateFast_CB_Reload` 0.7 s: `IKOff` 0.006, `FadeOutReloadAnim` 0.57, `IKOn` 0.61 |
| Pistol Carnifex, SMG Tempest, SMG Hurricane | 1.5 s | 0.558 | 0.84 s | |
| Assault Rifle Avenger | 0.8 s | 0.421 | 0.34 s | `RifleAuto_FastAlternate` |
| Assault Rifle Mattock | 1.37 s | 0.486 | 0.67 s | |
| Shotgun Katana | 1.5 s | 0.358 | 0.54 s | |
| Shotgun Claymore | 2.57 s | 0.405 | 1.04 s | `ShotgunAuto_AlternateSlow` 2.57 s: foley only |
| Sniper Mantis, Widow | 2.97 s | 0.463 | 1.38 s | `SniperAuto_AlternateSlow` 2.97 s: `IKOff` 0.66, `IKOn` 2.62 |

(`RifleAuto_SlowAlternate` 2.9 s, `IKOff` 0, `IKOn` 2.40: Argus, Falcon, Geth, Revenant, Saber.) `IKOff`/`IKOn` turn the
game's own left-hand IK off the gun and back. No ME3 weapon has a reload animation of its own (the weapon anim sets hold
only base pose, expand, collapse and, on the Cobra, Thor and Graal, charge).

**What the player sees now** (ME2, Save 4, lane WP's stretch `20261116-224515-586-LE2-wp-weapons`, snapshot
`%LOCALAPPDATA%\MEVR\snapshots\2026-11-16_22-48-02-503-left.png`, a small copy in `%LOCALAPPDATA%\MEVR\tc-lane\`): 0.2 s
into the game's reload, the pistol in the right hand as before, the gun panel's holographic counter on it, the left hand out
of view, no clip anywhere. The eject at 0.75 s was not captured: run V1 below.

## 3. Where a clip goes into the gun: per weapon model

Every player weapon model of both games (the base `CookedPCConsole` packages; 14 ME2 models, 59 ME3) has the sockets
`Flash_1`, `Flash_2`, `Tracer`, `Cooldown`, `Sabotage`, `Modal`, `Ammo` (ME3 adds `Taser`, `Barrel`, `Laser`, `Scope` on
some). **`Cooldown` is the port the game ejects from** (its +X is the eject direction; its rotation and scale differ per
model); `Ammo` is where ME2/ME3 hang the ammo power's icon hologram (`AttachComponentToSocket(AmmoPowerIconPSCO, 'Ammo')`),
on the top or side of the receiver. All `Cooldown` sockets are on `Root` except ME3's Arc Projector (on
`FrontBarrel_Top_Main`) and the Cain (none). Location in model units, rotation in UE units (16384 = 90 degrees).

ME2:

| Model (package) | `Cooldown` (location; rotation pitch, yaw, roll; scale) | `Ammo` | clip-like part |
|---|---|---|---|
| `WPN_PSTa_MDL` heavy pistol (HeavyPistols) | (0, 0, 0); 16384, 0, 0; 0.62 | on `Barrel_Upper_rear` (12.1, 1.4, 4.0) | none |
| `WPN_PSTb_MDL` hand cannon | (5, 0, 0); 20024, 9102, 0; 0.7/0.62/0.62 | (7.2, -0.7, 2.3) | none |
| `WPN_PSTc_MDL` (AutoPistols) | (0, 0, 0); 16384, 0, 0 | (6.7, 0.8, 2.8) | none |
| `WPN_PSTd_MDL` | (0, 0, 0); 16384, 0, 0 | (5.0, -2.8, 2.7) | `Clip` |
| `WPN_ASLa_MDL` (AssaultRifles) | (5, -7, 0); 20024, 0, 0 | on `Upper_Rear` (2.7, -2.6, 4.1) | none |
| `WPN_ASLb_MDL` | (0, -5, -10); 0, -3640, 0 | (3.0, -9.4, 2.5) | none |
| `WPN_ASLc_MDL` | (6, -6, 0); 0, 0, 0 | (13.5, -7.2, 4.9) | `Reload_Latch_Left`, `Reload_Latch_Right` |
| `WPN_GTH_ASLa_MDL` | (0, -6, -10); 3640, 0, 0 | (5.7, -8.8, 1.3) | none |
| `WPN_BLSa_MDL` (Shotguns) | (5, -1.5, 0); 21845, 16384, 0 | (20.1, -5.2, 3.6) | (`Muzzle_Flap_*`: the muzzle) |
| `WPN_BLSb_MDL` | (15, -3, 0); 20024, 9102, 0 | (14.1, -3.8, 4.2) | none |
| `WPN_BLSc_MDL` | (0, 0, 0); 16384, 0, 0 | (20.7, -7.4, 5.1) | `Clip_Flap_Left`, `Clip_Flap_Right` |
| `WPN_SNPa_MDL` (SniperRifles) | (10, -5, 0); 20024, 3640, 7281; 1/1.2/1 | (-2.8, -9.0, 3.0) | none |
| `WPN_SNPb_MDL` | (0, -3.2, -14.0); 0, 0, -16384; 0.9/1.2/1 | (3.8, -6.9, -1.7) | none |
| `WPN_SNPc_MDL` | (0, -1.9, 0); 16384, 0, 0 | (0.8, -7.6, 5.2) | none |

ME3: the models ME2 had keep ME2's `Cooldown` (Avenger = ASLa, Revenant = ASLc, Geth rifle `ASLh` = ME2's GTH_ASLa,
Predator = PSTa, Shuriken = PSTc, Tempest = PSTd, Scimitar = BLSa, Katana = BLSb, Claymore = BLSc, Mantis = SNPa,
Widow/Black Widow = SNPc) except three moved: Vindicator (ASLb: 4.0, -7.5, 0; 0, 0, 0), Carnifex/Ivory/EndGameCarnifex
(PSTb: 10.56, -0.74, -0.09; 13432, -17211, 31488) and Viper (SNPb: 6.28, -2.25, 0.85; 11258, -32768, -32768); some
`Ammo` sockets moved too (Claymore's to 7.4, -5.4, 5.1). The new models' `Cooldown` sockets mostly point sideways (roll or yaw about -16384: a side port), e.g. Argus/Falcon (4.96, -2.37, 1.48; 11346,
-15857, -15648), Cobra (-4.50, -5.27, 1.86), Mattock (4.78, -5.58, 2.06; 13300, -16384, -16384), Phalanx (12.3, -0.75,
1.0), Eagle (7.5, -3.75, 0), Talon (16.5, 0, 0), Locust (8.2, -1.7, -0.3), Hurricane (11.25, 0, 0), Graal (13.8, -3.5,
0), Raptor (9.7, -3.1, 1.0), Javelin (2.2, -4.3, -12.2). Clip-like parts in ME3: Hurricane `Magazine`, Tempest `Clip`,
Graal `ChargeClip`, Revenant `Reload_Latch_Left/Right`, Claymore `Clip_Flap_Left/Right`, Titan missile launcher `RearClip`.
The full per-model list is reproducible with the scanner's skeleton mode over `SFXWeapon_*.pcc` (section 8).

**The place differs per model**, by up to about 20 units along the gun and by the port's side (top, right side, under):
read the live gun's `Cooldown` socket each time (`Engine.SkeletalMeshComponent.GetSocketWorldLocationAndRotation`,
already a ReflectCheck row), never a table of offsets.

## 4. Showing it: the pieces every gesture shares

Build one shared piece and small adapters (the trilogy rule: a shared core plus a per-game adapter; ME1 has no clips).

**P1. The clip prop** (new shared `core/vr/ClipPresentation`, game-independent and unit-tested: states Hidden, InHand,
Seating, Seated, Dropped; the seating curve; and a shared game module, e.g. `game/ClipProp`, used by `game/Reload`):
1. Find the mesh on the game thread. The pickup clip: `Core.Object.DynamicLoadObject("BioApl_Uti_AmmoClip01.Meshes.AmmoClip01"
   (ME2) / "BioApl_Uti_AmmoClip01.AmmoClip01" (ME3), class StaticMesh, MayFail true)` (the cockpit's `LoadObject` does this).
   It finds the object `SFXGame.pcc` already loaded [verify the in-memory path on both games].
   The heat sink: walk the live `Engine.StaticMesh` objects for the name `HeatSink` whose outer is `Mesh` in
   `BioVFX_C_Reload` (the cockpit's `ForEachLive`). A missing mesh stands the prop down with one log line.
2. Spawn a movable mesh actor: move `SpawnActor` and the `ActorKind` table out of `game/CockpitProbe.cpp` (LE1-only today)
   into a shared helper with `ue3::kEveryGame` handles: `Engine.Actor.Spawn` on the player controller, class
   `Engine.InterpActor` (its `bStatic`/`bNoDelete` defaults cleared for the call if set, as the cockpit does),
   `bNoCollisionFail` true. Moving that code touches LE1: a same-pose LE1 check before and after.
3. Dress it: `Engine.StaticMeshComponent.SetStaticMesh` on the actor's `DynamicSMActor.StaticMeshComponent`;
   `Engine.Actor.SetCollision(false, false)`; `Engine.Actor.SetDrawScale` (the pickup clip at about 0.4 is 4 x 12 x 4 cm;
   the heat sink at the eject's size times 1.5-2 for legibility: a choice for the owner); no dynamic shadow; the pawn's
   light (`Engine.PrimitiveComponent.SetLightEnvironment` with the pawn's `LightEnvironment`, what `SFXWeapon.AttachWeaponTo`
   does for the gun) so the lit pickup clip isn't black. Hidden until wanted (`Engine.Actor.SetHidden`).
4. Hold it at the left hand each tick: from the left controller's grip pose (the same input arm IK uses, so no frame of lag)
   with a fixed offset into the palm; or attached to Shepard's `Prop02` bone (`Socket_02`), [verify] that an attachment
   follows the pose `engine/bone_pose` writes. Every write journalled; ReflectCheck rows for every name.
5. Seat it: when the game's reload is accepted (`game/Reload`'s "the game reloads"), slide the prop from the hand to the gun's
   `Cooldown` socket and along the socket's -X (into the port; the eject leaves along +X [verify]) over about 0.12 s, then
   hide it. On a refusal (full magazine, no spare ammo) the prop stays in the hand and a buzz says no; let go, it drops
   (hidden after 1 s, or a short fall).
6. Lifetime: destroyed at a level change, unload and hot reload (the cockpit's park and destroy); never more than one.

**P2. The old clip leaving** is the game's own eject at `DoReload` (no work). If the owner wants it at the gesture's moment
(e.g. a flick, or the hand pulling it out): call `SFXWeapon.PlayReloadEject` ourselves (a script function without
parameters) and skip the game's own call inside `DoReload` with `engine/call_function`'s skip handler (it fits functions
without parameters); the ammo still moves in `DoReload`. ME3's `EjectRumble` comes with the call. 1-2 h.

**P3. Clip parts on the gun** (9 models, the list above): ME1's visible vent rack (`[Vent] animate`, `engine/bone_pose`'s
weapon-part stage, `ik::vent::Rack`) is the pattern: the part follows the hand back and springs home. 3-4 h plus a look at
each model [verify what each part is].

**The game's reload animation under arm IK.** Arm IK writes both arms after the animation, so the hands follow the
controllers and the animation's hand motion isn't seen; ME3's `IKOff`/`IKOn` only switch the game's own left-hand IK.
Two risks: the animation's `Prop01` track may turn the gun in the right hand for 0.7-3 s (it is the gun's bone, under the
wrist), and its chest and neck tracks may move the shoulders the arms hang from. Check with V1's burst. If the gun turns:
hold `Prop01` at its local place from before the reload while the weapon is in `Reloading` (ME1's `core/ik/PoseGraft`
pattern), or keep the pawn from playing it (`BioPawn.ReloadWeapon` on ME3) [verify the side effects: the reload's end and
its sounds hang on the animation's notifies].

## 5. The gesture: options for the owner

Every option uses P1 (the clip seen in the hand or at the gun, sliding in) and the game's own reload through
`SFXGameModeDefault.TryReload` (the coordinator's one request; the game counts the ammo). Hours are on top of P1. The
public descriptions of other games are from reviews and design write-ups (sources at the end), not their code; where a
detail wasn't confirmed it says [verify].

| | Where the clip comes from | How the old one leaves | How the new one goes in | Cost | Main risks |
|---|---|---|---|---|---|
| A | nowhere: a button | the game's eject | the clip appears at the port and slides in | 1-2 h | needs a free button |
| B | nowhere: the gun hand alone (tilt or flick) | the game's eject, or at the flick (P2) | appears at the port, slides in | 3-4 h | false triggers |
| C | the gun: the left hand at the port | pulled out by the left hand (P2) | a fresh one in the hand, pushed back in | 4-6 h | the support grip's and vent's zones |
| D | over the left shoulder | the game's eject | pushed into the port | 2-3 h | tracking behind the head |
| E | the chest | the game's eject | pushed into the port | 2-3 h | grenade slot and self-cast palm |
| F | the left forearm (the omni-tool makes one) | the game's eject | pushed into the port | 4-6 h | palm-up squeeze conflicts |
| G | the left hip pouch (built) | the game's eject | pushed into the port | 0 h | the owner rejected it |
| H | none: the game's own reload (R, the empty auto-reload) | the game's eject | appears at the port, slides in | in P1 | no player action at all |

**A. A button, with a visible clip.** The player presses one button; the game reloads; the heat sink pops out of the port
(ME2 0.75 s later) and a clip appears at the port and slides in. Conventions: the closed ME2VR mod reloads on X (its
`README_CONTROLS.md`: "X | Reload; hold to swap weapon"); flat-ported VR games often keep a reload button. Fiction: Shepard
does it by hand in the flat game without the player's hands mattering. Cost 1-2 h (the mapping in `game/GamepadMapping`,
its tests). Risks: a button must be freed on foot (on ME1's map X is the hand power wheel, B squad placement, A use;
[verify] the sequels' current map); least physical.

**B. The gun hand alone: tilt or flick.** Point the gun down past about 60 degrees for 0.2 s, or flick it (a fast turn of
the right controller): the heat sink flies out (at the flick if P2), a new clip slides in by itself. Conventions: some
arcade shooters reload without the off hand (Pistol Whip: not hand-based, reportedly by pointing the gun down [verify]);
the flick to drop a magazine is common in VR shooters [verify per title]. Fiction: ME's guns eject their spent sink by
themselves. Cost 3-4 h (the gesture in `core/vr/ReloadGesture` with tests, tuning on recordings). Risks: shooting down,
crouching behind cover or walking with the gun low trigger it; a lowered gun near the hip is also the holster's let-go.

**C. At the gun: the left hand takes the old one out.** The open left hand comes to the port side of the gun and squeezes:
the hot sink comes out into the hand (the game's eject suppressed, P2), the player throws or drops it (it falls and fades),
squeezes again at the port: a fresh clip is in the hand and pushed in. One place, two squeezes, no body slot. Conventions:
a "magazine swap at the gun" is close to Half-Life: Alyx's and Pavlov's magazine handling at the gun, minus the pouch.
Fiction: thermal clips are hot and spring out; pulling one by hand is a VR liberty. Cost 4-6 h (two zones on the gun from
`Cooldown`, the drop, P2). Risks: the left hand at the gun is the support grip (a capsule along the gun) and, on a hot ME1
gun, the vent's receiver zone; the port's zone must be separate (by side of the gun and by the open hand), and two
squeezes take longer than the game's 0.7-3 s reload.

**D. Over the left shoulder.** The left hand reaches up behind the shoulder or head and squeezes: a clip is in the hand;
pushed into the port: the reload. Conventions: Half-Life: Alyx takes ammunition from the backpack over the shoulder
(widely described). Fiction: Shepard's armour carries the weapons on the back (`Socket_06` on `LowerBack`, `Chest2`
sockets); nothing shows clips there. Cost 2-3 h (a head-relative region instead of the hip's body slot in
`core/vr/ReloadGesture`, the recording script). Risks: an inside-out headset (the owner's Quest) can lose a controller
behind the head [verify how far back]; tiring over long fights; seated players reach it well.

**E. The chest.** The left hand to the middle or right of the chest, squeeze: a clip; into the port. Conventions: chest
rigs and vest pouches in military VR shooters (Into the Radius, Pavlov's modes [verify which]). Cost 2-3 h. Risks: the
grenade slot is the left chest (either hand) and the self-cast powers use the open left palm on the chest; a reload there
needs its own spot (the right chest, a cross-body reach) and the grip, never the open palm.

**F. The left forearm: the omni-tool gives one.** Turn the left palm up and squeeze (or tap the forearm with the gun's
muzzle side [verify]): the omni-tool flashes orange and a clip is in the left hand; into the port. Fiction: ME2's armour has
a left-forearm ammo pack piece (section 1) and the omni-tool on the left forearm fabricates things in the fiction; the mod
already wears the game's omni-tool there (`game/OmniTool`). Cost 4-6 h (palm-up detection, the flash from `game/OmniTool`).
Risks: the palm-up squeeze must not collide with the omni-tool's wrist tap, the power wheel at the left palm (X) or the
support grip; a "made" clip is a stretch of the fiction for some players.

**G. The left hip pouch** (built: `[Reload] pouch`): the owner rejected it (2026-11-16). Listed only for completeness; the
conventions behind it are belt and hip pouches (Pavlov, Boneworks, Arizona Sunshine by their descriptions [verify]).

**H. No gesture: show the game's own reload.** R or the game's empty-magazine auto reload, as today, with P1 showing a clip
slide into the port at the reload's start and the game's eject. Can sit beside any of A-F (the game's R and auto reload
stay in every option).

**The vent-only guns** (section 1: they vent heat and eject nothing): either no clip at all (the vent, as the flat game
shows them; the closest to ME1's vent) or the same clip as the others. Also the owner's call.

What the owner would see at each implementation step:
1. P1 alone (with H): a clip slides into the port whenever the game reloads; the old one pops out as now.
2. The chosen gesture: the clip in the left hand from its source, held, pushed into the port; refused with a buzz on a full
   magazine (the clip stays in the hand until let go).
3. P2 if chosen: the old clip leaves at the gesture's moment instead of the game's fraction.
4. P3 if chosen: on the nine models with a clip part, the part moves with the hand.

## 6. Prior art

- **Overlord** (GPL-3.0, Modern Warfare 2 Campaign Remastered; `reference/Overlord`, the pin in
  research/le2-overlord-reload-reuse.md), read for the approach only: it shows the game's OWN magazine. It finds the
  magazine's faces in the gun's model by the magazine bone (`native_magazine_assets.cpp`), makes a separate rigid part of
  them, hides that part on the gun once the magazine is out (a hide mask, `viewmodel_visibility`), and draws the part at the
  hand, as dropped magazines (up to 8) and as the inserted one, lit like the gun (`physical_reload_presenter.cpp`). ME's
  guns have no clip part in 64 of the 73 models counted (14 ME2, 59 ME3; the nine in section 3, one of them a heavy
  weapon), so the closest ME equivalent is a separate prop of the game's own clip mesh (P1), and Overlord's way only for the
  models with parts (P3).
- **vrframework's guides** (MIT): nothing on reloads or magazines.
- **ME2VR** (closed; its README and configs only): reload is the X button; nothing on visible clips.
- **Other VR games**, by public descriptions: Half-Life: Alyx's ammunition from the backpack over the shoulder and weapon
  specific physical reloads; Pavlov's physical magazine reloads ("grab the mag, slam it in, rack the bolt"); Pistol Whip's
  reload not hand-based. Sources: [Road to VR on Half-Life: Alyx's details](https://roadtovr.com/these-details-make-half-life-alyx-unlike-any-other-vr-game-inside-xr-design/2/),
  [Road to VR on shotguns in VR](https://www.roadtovr.com/inside-xr-design-best-shotguns-in-vr-what-makes-them-great-feedback/2/),
  [a Pavlov review](https://gamegeeker.com/de/games/pavlov/review), [a VR gun games list](https://5thscape.com/blog/best-vr-gun-games/).
  UEVR and TF2VR were not read (all rights reserved).

## 7. ME1

ME1 has no clips: heat and the vent. Its visible vent rack (`[Vent] animate`: a part of the gun's own model follows the
hand back and springs home) is the pattern for P3 and for the feel of a part moving with the hand; the clip itself (P1) is
new and sequel-only. Nothing here changes ME1 except moving the cockpit's spawn helper into shared code (P1 step 2), which
needs the same-pose LE1 check.

## 8. Checks and how to reproduce

- **V1, the game's reload as the player sees it now** (ME2 Save 4, under `tools\le2-halcyon-run\run-lock.ps1`; prep,
  launch and load as `tools/le2-halcyon-run/README.md` says): `tools\console.ps1 "mevr weapon pistol"`, one shot (the replay
  of `tools/make-reload-recording.py`'s first second, or the game's fire), then start
  `tools\snapshot.ps1 -Count 10 -Interval 0.15 -Delay 0.3 -Tag tc-reload` and at once `tools\console.ps1 "mevr reload try"`.
  Look for: the heat sink at 0.75 s (its size and where it flies), the gun's place in the right hand through the 1.5 s (does
  `Prop01` turn it), the shoulders. Copies of the pictures to `%LOCALAPPDATA%\MEVR\tc-lane`, never into the repository.
- **V2, P1 on ME2:** the prop's mesh found (log), spawned, in the left hand on a snapshot, lit (not black), sliding into the
  port as `reload: the game reloads` logs, hidden after; none left after a level change and a hot reload.
- **V3, ME3:** the same with a combat fixture (research/le3-combat-fixtures.md).
- **The asset facts** came from a private scanner over `LegendaryExplorerCore` (scratch only, not committed): exports by
  class in `SFXWeapon_HeavyPistols.pcc` / `SFXWeapon_Pistol_Predator.pcc`; properties of `Default__SFXWeapon*`,
  `Default__SFXDroppedAmmo`, `Reload_HeatSinkEject`'s modules and the reload `AnimSequence`s; skeletons and sockets of every
  `WPN_*` SkeletalMesh in `SFXWeapon_*.pcc` and of `HVYa.HMM_ARM_HVYa_MDL`; static mesh bounds of `AmmoClip01` and `HeatSink`;
  `SFXWeapon`'s reload states read decompiled in scratch. `tools/conversation-scan`'s `--script` and `--class-dump` modes do
  the script and property parts for LE2; LE3 needs its game switch.
