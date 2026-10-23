# 24: Why the world tilted while shooting, and the level camera

Bug (user, 2026-10-22, PSVR 2 on SteamVR): over a play session the world tilted more and more in
pitch, while SteamVR's own view stayed level. This note finds the cause and records the fix
(`game/view_level`, `core/vr/ViewLevel`, `[Hands] levelCamera`). Section 7: aim assist's friction and
adhesion, turned off while the hand aims (`[Hands] aimAssist`).

Sources, all read-only and local: the session's logs (ours from 19:52:38, Halcyon's
`MELEVR_Log.txt`) and its two test-loop snapshots; `SFXGame.pcc`, `Engine.pcc` decompressed with the
game's Oodle DLL and read with the scratch bytecode disassembler of [20](20-melee-and-grenades.md);
`BIOWeapon.ini` in `CookedPCConsole\Coalesced_INT.bin`; GhidraMCP on the LE1 project (RVAs for
2.0.0.48602); Halcyon's `vr_menu.cpp` and `render_hook.cpp`. Nothing extracted is kept or committed.

## 1. What the session shows

- **Halcyon's view** is the game camera's rotation (the camera mode's POV, from the player
  controller's `Rotation`, i.e. ControlRotation) with render-side head look on top: the head's
  yaw and pitch, read fresh from the head pose every frame (`ApplyHeadRotation`, roll left to the
  compositor). Head look can't drift; the game camera can.
- **The snapshots measure the camera's pitch.** The walkway at Eden Prime runs level, so its
  vanishing point is on the horizon:
  - 19:57:19 (Halcyon's menu open, so no head look: the image is the game camera alone): the
    vanishing point sits **about 31 degrees below** the image's centre (FOV 53 degrees up and down).
    The game camera looked about 31 degrees up.
  - 19:57:00 (head look on, the head pitched -28.4 degrees): the vanishing point is at the centre.
    The camera's +28-31 and the head's -28.4 cancel: the player had to look down about 30 degrees to
    see the world level.
- **The head pitch drifted with the shooting.** Halcyon's `[HEADCTRL]` pitch (the head's own, every
  4 s) was about -5 degrees before the first fight, -15 to -30 after the first 80 shots and -25 to
  -43 after 204 (the heartbeat's `hand aim: ... N aim queries`, one per shot of the player's).
- So by 19:57 the game camera was pitched about **+30 degrees**, after 204 shots, about 0.15
  degrees a shot (more than the user's "about 5 degrees").

## 2. How LE1 applies recoil

| Step | Where | What |
|------|-------|------|
| A shot | `SFXPawn_Player.OnWeaponFired` (script) | `SFXInventoryManager.SetWeaponRecoil(Weapon.GetRecoil() * 182.044)` (degrees to rotator units); also the pad rumble (`WeaponFireRumble`) and a camera shake (`FiringShake` / `TightAimFiringShake`) |
| The kick's size | `ABioWeaponRanged::GetRecoil` `0xC59F90` | `Stats.Recoil`, or `ZoomRecoil` with `bIsZoomed` (the support grip's aimed fire), times the talent curves (`0xCDC520`), times 0.25 under a condition on a global game object **[verify what]** |
| Queued | `SetWeaponRecoil`'s native `0xCB9220` (the exec `0xB95A80` calls vtable `+0x810`; this one found by its fields and signature, named `_guess` in Ghidra, like the two below) | `WeaponRecoilOffset.Pitch` (`+0x2CC`) and `TotalRecoil.Pitch` (`+0x2D8`) += the kick, while `TotalRecoil` is under `RecoilCap` (40 degrees); a yaw kick `sin(time * RecoilYawFrequency) * kick * RecoilYawScale` goes to `.Yaw` the same way |
| Applied | `BioPlayerInput.UpdateViewRotation`, from `BioPlayerInput.PlayerInput`, inside `PlayerController.PlayerTick`, when `SFXGameModeManager.AllowsCameraMods()` | `UpdateKickback` (`0xCBA430`) moves the pending offset into a delta (`FInterpTo` `0x142800` at `RecoilInterpSpeed`, 100/s for rifles); `UpdateKickbackFade` (`0xCBA550`) takes back what was applied; then **`Outer.SetRotation(Outer.Rotation + Kickback - Fade)`**: the controller's rotation itself |
| Faded back | `UpdateKickbackFade` | pitch: `RecoilFadeSpeed` (or `RecoilZoomFadeSpeed`) x `GetRecoil()` x shots per second (`0x8F8`: `RateOfFire / 60`) x 182 units a second; yaw: `FInterpTo` at a fixed 5/s. Once both steps round to 0, `TotalRecoil` is reset |

`BIOWeapon.ini` sets `RecoilFadeSpeed=0.0` and `RecoilZoomFadeSpeed=0.0` for **every assault
rifle** (Avenger, Banshee, Breaker, Crossfire, Geth, Kovalyov, Lancer, Raptor, Spectre, Terminator,
Thunder, Tsunami), the sniper rifles and several pistols; the shotguns don't set it (the class
default, 0). Only some pistols and shotguns fade (`Edge`, `Kessler`, `Stiletto`, `Brawler`...).
**So the pitch kick of a rifle is permanent; its yaw kick isn't.** The Avenger's `Recoil=0.1`,
`ZoomRecoil=0.2` (degrees a shot) matches the measured 0.15 average: the 0.25 factor didn't apply
in this session.

A flat-screen player pulls the stick down against it. In our VR setup nothing does:
- Halcyon's `decoupledPitch = 1` zeroes the right stick's Y (`vr_menu.cpp`, XInput and raw input),
  so `aLookUp` is 0;
- head aim is off (`combatHeadAim = 0`), so Halcyon never writes ControlRotation;
- the hand aims (`GetAdjustedAimFor`), so the player has no reason to.

ControlRotation's pitch climbs shot by shot and the head-tracked world pitches with the camera.

## 3. The other candidates

| Candidate | Finding |
|-----------|---------|
| Aim assist: friction and adhesion | `BioPlayerController.ProcessViewRotation` runs `SFXModule_AimAssist.ProcessRotation` when camera mods are allowed and the input is a gamepad (Touch reaches the game as pad 0): friction scales the stick's turn near a target; adhesion adds a turn after the target (pitch too), **only while a stick moves** (looking or strafing). A small contributor, both ways; the hold covers its pitch, and since section 7 both are off while the hand aims |
| Aim assist: zoom snap | `BioPlayerInput.PostProcessInput` calls `SetZoomed` every tick; zoomed (our support grip holds LT), `ZoomSnap()` picks a target within 5-40 m (rifles). When the camera switches to `TightAim`, `SFXCameraSetup.GetCameraMode` starts `SFXCameraTransition_ZoomSnap`, which aims the mode at the target and **calls `PC.SetRotation`** each tick of the transition: pitch and yaw toward the enemy, inside the camera update. The support grip turned aimed fire on 21 times in under three minutes of this session. A plausible contributor (whether it fired isn't in the old logs), and a yaw jump of the world; the hold drops the target |
| Camera modes | `SFXCameraMode.ProcessViewRotation` only recentres the pitch **toward 0** when `bRecenterCamera`; the storm modes override it with nothing. `PlayerController.LimitViewRotation` clamps to the pawn's pitch limits. Not a drift source |
| Cover, crouch | `BioPawn.ProcessViewRotation` in cover only changes the cover action (the lean). The cover cameras and crouch change offsets. Not a drift source; cover stays outside the hold (and with [23](23-cover.md)'s `noCover`, VR on foot rarely enters it). A physical crouch is the `Combat` class's crouch instance: held |
| Our hand aim | `HandAim` rewrites `GetAdjustedAimFor`'s return value only; it never writes ControlRotation |
| Halcyon's head look | absolute angles from the head pose each frame; no integration, no drift. Head aim (the one place it writes ControlRotation, with its injection removed on release) was off all session |
| Camera shake | `FiringShake` is a camera modifier on the POV, transient; it doesn't touch the controller |

## 4. The fix: keep the game camera level

`game/view_level` (mod, hot-reloads), `[Hands] levelCamera = 1` (default):

- **Where:** a ProcessEvent post-handler on `PlayerTick` (every `PlayerController` and
  `BioPlayerController` state's version: the native tick sends the event to the current state's).
  That is after the input, the recoil (`UpdateViewRotation`), aim assist and `UpdateRotation`, and
  before the cameras update: `UWorld::Tick` (`0x41AC30`) updates every player camera after all tick
  groups have run ([18](18-camera-and-hands.md), "Order within the frame"). The weapon's own fire
  (later in the frame) only queues the next kick, which the next `PlayerTick` applies and the
  handler takes back. So the camera never shows a kick. Rejected:
  - zeroing the kick at its source (`UpdateKickback`'s out parameter through CallFunction): recoil
    only, not aim assist or anything else;
  - the mod's tick: it runs after the engine tick, so after the camera update: each kick would show
    for one frame. It is the fallback if no `PlayerTick` post is seen for three ticks (logged);
  - before `Camera.UpdateCamera`: our ProcessEvent handlers run after the original only.
- **What:** ControlRotation (`Actor.Rotation`, `+0x114`) pitch and roll set to 0 (level, what the
  camera modes recentre to). Yaw is the player's (stick turning) and stays. A pending zoom-snap
  target (`SFXModule_AimAssist.LastZoomSnapTarget`) is dropped, so the snap transition never
  starts.
- **When:** on foot (game mode 0), with a head-tracked view from the host, the hand aiming (the mod
  told the host), the host's head aim off (never fight a writer of ControlRotation), a pawn, not in
  cover (`CoverType`/`CoverAction`), not on a ladder (`PHYS_Ladder`), and in a camera mode that has
  no pitch of its own: `Explore`, `Combat`, `TightAim`, `ExploreStorm`, `CombatStorm` (or an
  `Interpolate` toward one). Not the sniper scope (`SniperAim`), cover (`EnterCover`, `HipAimCover`),
  the Mako (mode 1, `Vehicle*`), photo, cinematic, conversations, cutscenes, menus. (No on-foot
  turret is known in LE1; one would come with a camera mode outside this list, so it keeps its pitch.)
- **Taking hold of a pitched camera** (the setting turned on, back from cover or a conversation)
  levels it at once, a snap, not a smooth turn of the world (smooth artificial rotation is the
  less comfortable of the two). Logged.
- **First person:** the hold doesn't test Halcyon's K; a level camera is right in third person too.
- The right stick's up/down no longer pitches the view on foot while the hand aims: that is
  `decoupledPitch`'s job anyway.

### The recoil, felt instead

- **Haptics (built):** a CallFunction post-handler on `SFXInventoryManager.SetWeaponRecoil` (each
  of the player's shots) pulses the right controller, and the left at 60% while both hands hold
  the gun: a 30 ms tap for a rifle's 0.1-degree kick, up to 70 ms for a shotgun's or sniper's
  (`vr::view_level::RecoilPulse`; `[Hands] recoilHaptic`, 0..1, default 0.5). The game's own pad
  rumble never reaches the Touch controllers (no XInput vibration path).
- **Visual kick of the gun model (proposed, not built):** arm IK could add a short decaying pitch
  and pull-back to the right hand's target in the gun's own frame (2-4 degrees and 1-2 cm, about
  80 ms back), so the gun kicks in the hand while the view stays still. The aim ray follows the
  barrel, so the reticle would kick with it; the shot itself is already fired.

## 5. The logs (ours)

- `view level: handlers on PlayerController.PlayerTick, ... (N PlayerTick functions); shots from
  SFXInventoryManager.SetWeaponRecoil` at startup.
- `view level: <state> (camera mode ..., pitch as the game left it ...)` when the hold's state
  changes (at most every 2 s): `holding level`, `in cover`, `a camera mode with its own pitch`...
- `view level: the game moved the camera's pitch +0.62 deg (+113 UU) in 1.4 s (6 shots, recoil
  queued +0.60 deg); ControlRotation as the game left it: pitch ..., yaw ..., roll ...; put back to
  level` each time the game's pitch (or roll) moved 0.5 degrees since the last line, at most once a
  second. With the hold off it says `kept (<why>)` and the pitch climbs.
- `view level: took hold of a camera pitched ...: levelled at once`.
- `view level: first shot seen (...): pitch kick ... queued`.
- The heartbeat: `view level: <state> (camera mode ...); ControlRotation as the game left it: ...;
  last 60 s: the game moved the pitch X (recoil queued Y by N shots; aim assist and the rest X-Y),
  pitch as left min..max, roll min..max; the hold put back Z in H of F ticks; K zoom snaps
  dropped; ... PlayerTick posts`.
- Snapshots carry `"viewLevel"` in the mod's status (the state, the rotation, the last minute).

## 6. [verify] in the headset

- The `PlayerTick` posts count up in the heartbeat (else the fallback line appears).
- Fire a rifle for a minute: the horizon stays put; the heartbeat's "the game moved the pitch" is
  about shots x 0.1-0.2 degrees and "the hold put back" its negative.
- The recoil taps in the hand; the left hand too with the support grip.
- Cover, the Mako, conversations and the sniper scope behave as before; leaving them levels the
  camera with one logged snap.
- The 0.25 factor in `GetRecoil` (what global condition) and whether the zoom snap fired in this
  session (the new heartbeat's count) are open.
- Aim assist (section 7): turning past an enemy with the stick keeps its speed, strafing past one
  doesn't turn the world; the heartbeat's `aim assist:` counts show what was removed.

## 7. Aim assist's friction and adhesion, off while the hand aims

The stick only turns the body in VR, and the hand aims; ME1's controller aim assist still acted as if
the stick aimed. Sources as above (the `SFXGame.pcc` bytecode, `Coalesced_INT.bin`'s `BIOGame.ini` and
`BIOWeapon.ini`, GhidraMCP).

### How ME1 applies them (script, LE1)

| Step | Where | What |
|------|-------|------|
| The gate | `BioPlayerController.ProcessViewRotation(DeltaTime, out ViewRotation, DeltaRot)`, from `UpdateRotation` inside `PlayerTick` | if `GameModeManager.AllowsCameraMods()` and (`IsConsoleBuild()` or `PlayerInput.bUsingGamepad`): the controller's `SFXModule_AimAssist` module runs `CacheCurrentAimAssistTarget()` (with a ranged weapon) and `ProcessRotation(DeltaTime, DeltaRot)`; then `PlayerController.ProcessViewRotation` adds `DeltaRot` to the rotation |
| The target | `CacheCurrentAimAssistTarget` -> native `BioPlayerController.GetAimAssistTarget` (`execGetAimAssistTarget` `0xBADEF0` -> `0xC747F0`, named `ABioPlayerController_GetAimAssistTarget_impl`) | from the game camera's POV (`CameraCache.POV`, the body's view, not the head's or the hand's): recently rendered, hostile, living pawns with an `SFXModule_AimAssistTarget`, scored 1/distance with a bonus for the current selection; per aim box (`AimAssistRegions`: width, height, soft margin, bone) `0xC74BB0` (`AimAssistTarget_BoxSoftMargin_guess`) gives `CurrentAimAssistSoftMargin` 1 inside the box, falling to 0 across the margin. Dropped if dead or behind the camera's trace hit |
| Combined | `SFXModule_AimAssist.ProcessRotation` (script, final) | with a ranged weapon and stats: `AdhesionRot = ViewAdhesion(dt, W)`, `FrictionFactor = ViewFriction(dt, W)`, **`DeltaRot = (1 - FrictionFactor) * DeltaRot + AdhesionRot`** |
| Friction | `SFXModule_AimAssist.ViewFriction` (script) | 0 unless `Stats.bFrictionEnabled`, a `BioPawn` target and a soft margin > 0; then `GetRangeValueByPct(Stats.FrictionMultiplierRange, SoftMargin) * GlobalFrictionFactor`, times `GetFrictionMultiplier()` (the aim mode's) when zoomed. Also sets `LastFrictionMultiplier` / `LastFrictionTarget` (debug draw only) |
| Adhesion | `SFXModule_AimAssist.ViewAdhesion` (script) | `rot(0,0,0)` unless `Stats.bAdhesionEnabled` and **a stick moves** (`|RawJoyLookRight, RawJoyLookUp|` or `|RemappedJoyRight|`, the strafe), a target at least `MinAdhesionDistance` away and a soft margin > 0. Then the aim point is the camera ray at the target's distance plus the target's motion this tick x `EnemyMovementAdhesionFactor` (0.75; `AimCorrectionAmount = 0` means it never pulls toward the bone itself), seen from where the camera will be after the player's own motion; the view turns toward it by `FClamp(AdhesionStrength(SoftMargin) [x zoomed multiplier] * GlobalAdhesionFactor * (LookMag + MoveMag), 0, 1)`: it follows a moving target and cancels the player's strafe past it. Returns `Rotator(new) - CamRot` |

Values (`BIOGame.ini` `[SFXGame.SFXModule_AimAssist]`: `GlobalFrictionFactor=1.8`, `GlobalAdhesionFactor=0.5`,
`EnemyMovementAdhesionFactor=0.75`; `BIOWeapon.ini` `[SFXGame.SFXWeaponStatsBase]`: friction and adhesion on,
`FrictionMultiplierRange=(0, 0.38)`, `AdhesionStrengthRange=(0, 1)`, `MinAdhesionDistance=100`; shotguns'
friction 0..0.25, sniper rifles' 0..0.19 and adhesion 0..0.5, scoped x0.667 / x0.5): friction takes up to
about 68% of a rifle's stick turn over an enemy; adhesion can reach a full blend per tick while both
sticks move.

### The game's own settings

- **Gameplay option "Target Assist"** (`BioGamerProfile` option 15, `EOptionTargetAssist`: High, Normal,
  Low; `m_nDefaultTargetingAssistModifier*` in the profile): no Off, and its modifier is a native
  (`GetTargetingAssistModifier` `0xB9CB10`, profile vtable `+0x468`) that no script calls; neither
  friction's and adhesion's script nor `GetAimAssistTarget`'s native reads it. Not a switch for this, and
  it is the player's saved option.
- **`SFXGameConfig.bAimAssistEnabled`** (`BIOGame.ini`: false) only changes the camera's rotation
  smoothing (`SFXPlayerCamera.UpdateCameraMode`, `RotationalDecay`), not aim assist.
- **`GlobalFrictionFactor` / `GlobalAdhesionFactor`** are config values on the module (from the
  Coalesced file, not an option). Zeroing them at runtime would remove friction exactly, but adhesion
  would still return `Rotator(Vector(CamRot)) - CamRot` (no roll, a rounding unit), and they would have
  to be saved and put back per module (a new one each level) and on unload. Rejected for the below.

### What we do

`game/view_level`, `[Hands] aimAssist = 0` (default; 1 keeps the game's): CallFunction post-handlers on
`SFXModule_AimAssist.ViewAdhesion` and `ViewFriction` (script calls them from script, so ProcessEvent never
sees them; the same hook as hand aim's `GetAdjustedAimFor` and the recoil's `SetWeaponRecoil`) set the
returned rotator to 0 and the factor to 0, so `ProcessRotation` passes the stick's `DeltaRot` through
unchanged. Only for the local player's module (its `Outer` is the controller), and only when, decided at
each call (`vr::view_level::DecideAssist`): on foot (game mode 0: not the Mako), a head-tracked view (not
the flat game), the hand aiming (told the host), the host's head aim off, a pawn, and a camera mode where
the hand aims and the stick only turns: `Explore`, `ExploreStorm`, `Combat`, `CombatStorm`, `TightAim`,
`EnterCover`, `HipAimCover` (or an `Interpolate` toward one). The sniper scope (`SniperAim`), `Vehicle*`,
photo and cinematic modes keep the game's. Cover is in (unlike the level camera's hold): the hand aims there
too. Nothing in the game is written, so nothing needs restoring: once a condition ends, the VR session
stops or the mod unloads (the hook goes), the game's values pass through again. Magnetism
(`GetAdjustedAimFor`, aimed fire) was already replaced by the hand's aim (`hand_aim`), and the zoom snap is
dropped by the level camera (section 4).

### Logs

- Startup: `view level: handlers on ...; aim assist from SFXModule_AimAssist.ViewFriction and ViewAdhesion`
  (or `aim assist: ... not found: the game's friction and adhesion stay`); the setting, whenever the ini is
  read: `hands: aim assist's friction and adhesion off on foot while the hand aims ([Hands] aimAssist = 0)`.
- `aim assist: friction and adhesion removed (the hand aims; ...) (camera mode ...)` and `aim assist: the
  game's friction and adhesion (<why>)` on each change (at most every 2 s; the heartbeat counts them).
- `aim assist: first friction seen (...): N% of the stick's turn taken away (the look stick turning);
  removed`, `aim assist: first adhesion seen (...): it would have turned the view pitch ..., yaw ...`.
- The heartbeat, under `view level:`: `aim assist: <state> (K state changes); last 60 s: friction would
  have taken M% of the stick's turn on average (X% at most) in T ticks with a target in the soft margin, S
  of them while the look stick turned; removed in R; adhesion would have turned the view in A ticks (yaw
  ... deg, pitch ... deg in all); removed in B; F ViewFriction and G ViewAdhesion calls in all`.
- Snapshots: `"viewLevel": {..., "aimAssist": {"keep", "why", "frictionCalls", "adhesionCalls",
  "lastMinute": {...}}}`.
