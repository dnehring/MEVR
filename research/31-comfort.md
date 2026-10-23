# 31: Comfort options: snap and smooth turning, the comfort vignette, walking toward the left hand (2026-10-23)

**Question (the main menu's Comfort page, research/28).** Make the rows shown as "Soon" real: turn style (snap or
smooth), the snap angle, turn speed in degrees a second, a comfort vignette while the stick turns or moves the
player, and "walk toward: left hand". LE1, Halcyon's renderer, our host and mod.

**[inference]** marks our reasoning, **[verify]** what a headset session has to settle (notes/next-vr-session.md
step 30).

## Summary

| Option | What it does | Where it hooks |
|--------|--------------|----------------|
| Turn style: Smooth (default) | The right stick turns the player at `turnSpeed` deg/s at full deflection, linear past a 0.15 dead zone | The mod (`game/Comfort`) adds the yaw to ControlRotation in its tick, before arm IK; the game sees no right-stick X from the controllers (`gamepad::kFlagModTurns`) |
| Turn style: Snap | A flick past 0.7 turns by `snapDeg` (30/45/60) at once; the next needs the stick back inside 0.3 and 0.25 s | Same path; `vr::comfort::StepSnap` |
| (file only) `turn = game` | The game's own turn, exactly as before | Nothing of ours runs |
| Turn speed | Degrees a second for Smooth | `[Comfort] turnSpeed` (the game's own rate isn't one number: below) |
| Comfort vignette | The view's edges darken (clear inside 30 degrees, dark past 58) while the stick turns you, or also while it walks you; fades in and out over 0.15 s; a 0.2 s pulse after each snap | The mod sends a target each tick (HostServices v17 `setVignette`); the host fades it and draws a head-locked quad (`plugin/Vignette`, inside `HalcyonOverlays`) |
| Walk toward: Left hand | The left stick's forward is the left controller's pointing, laid flat | The mod sets `gamepad::kFlagHandWalk` on foot; the host turns the controllers' left stick before it merges them (`HalcyonBridge` `WalkTowardHand`) |
| Pause while open | Unchanged (`[Menu] pause`, research/28) | |

## 1. How the game turns the player, and what "turn speed" can map to

- **The game's turn is per camera mode, with acceleration.** `BioPlayerInput.ComputeStickResponse(RawStickValue,
  SFXCameraInput Input, DeltaTime)` shapes the look stick (LExSDKv2 LE1 `SFXGame_classes.hpp`, `UBioPlayerInput`:
  `LookStickResponseCurve`, `AccumulatedRotationSpeed`, `AccumulatedRotationDecayRate`, `GlobalStickFactor`).
  Each camera mode has its own `SFXCameraInput` (`USFXCameraMode::Input`, `+0xF4`) with `CameraSensitivity` (x, y),
  `TimeToReachFullSpeed`, `MaxCameraRotationSpeed` and `StickDeadZone`. On top sits the engine's
  `PlayerInput.LookRightScale` (`+0x1F0`). The bytecode of `ComputeStickResponse` wasn't read this time (no
  package reader is kept in the repo, research/24 "nothing extracted is kept"): **not found** how the four
  combine. **[inference]** the turn rate differs between Explore, Combat and TightAim and ramps up while the
  stick is held.
- **Halcyon's "look sensitivity" isn't the stick.** `lookSensitivity` scales the head's yaw and pitch for head look
  (`xr_session.cpp`: `pitchDeg * cfg.lookSensitivity`, `yawDeg * cfg.lookSensitivity`); `decoupledYaw` only zeroes
  the stick's X (`vr_menu.cpp`). Neither is a turn rate.
- **Scaling the pad can't speed it up.** A full deflection is already the most the game reads; scaling the
  stick could only slow the game's turn, and its curve and dead zone would bend any mapping.
- **So:** Smooth is now the mod's own turn at an exact rate (`turnSpeed`, 60-240 deg/s in the menu, 120 by
  default), the same in every on-foot camera mode, linear and without acceleration (acceleration is the less
  comfortable of the two in VR). `[Comfort] turn = game` keeps the game's own. The mod logs each camera mode's
  `SFXCameraInput` once (`comfort: the game's own stick turn in ...`), so a session tells the game's numbers
  **[verify]**.

## 2. Where the turn goes in, and why not in the PlayerTick handler

The player's yaw is ControlRotation (`Actor.Rotation` of the player controller); the pawn follows it in combat
(`UpdateRotation`'s `FaceRotation`), not in explore (it faces where it runs, research/29).

Within one engine tick N: the controller's `PlayerTick` (input, recoil, `UpdateRotation`, then `game/ViewLevel`'s
post-handler), the pawns and their meshes (`UpdateSkelPose` writes arm IK's goals: published in the mod's tick
N-1, in component space), then the cameras (`UWorld::Tick`, after every tick group), then the frame is drawn;
then the host calls the mod's tick N.

- **Turning in the PlayerTick handler** (as the level camera's pitch) shows the turn in frame N, but frame N's
  bones were posed in tick N-1 for the old heading: the hands and shoulders would sit 45 degrees off for a frame,
  then (combat) the pawn turns at tick N+1's `UpdateRotation` and the component-space goals swing with it for
  another. Exactly the "shoulder swing" to avoid.
- **Turning in the mod's tick** (built): ControlRotation's yaw changes after frame N is drawn and before arm IK
  runs in the same tick. If the pawn faces where the controller does (within 2 degrees: combat), it turns with
  it at once (`Actor.SetRotation` through ProcessEvent, never the SDK's wrapper), so its mesh transform, which arm
  IK reads, is already the turned one. The view anchor (`game/ViewAnchor`, `NoteTurn`) then maps the view as the
  next camera update will show it: the camera's yaw plus the turn, and its location turned about the pawn when
  the pawn turned (the first-person camera hangs off Shepard's mesh). That holds until the camera updates (its
  yaw or location changes), so a shot fired in tick N+1 before the camera update also aims from the turned view.
  Frame N+1 shows the turn with the bones posed for it. The cost: the turn shows one frame later than the
  game's own would (11 ms at 90 Hz).
- **The handoff:** the mod turns only once the host has had its flag for a tick (`gOwnPublished`), and the
  host keeps the right stick's X from the game only while the flag is fresh (250 ms, as every pad flag), so the
  game and the mod never both turn.
- **The level camera** (`game/ViewLevel`) is unchanged: it holds pitch and roll in `PlayerTick`; yaw is the
  player's, which now includes ours.

**Body follow through a snap.** `core/ik/BodyFollow`'s state (the body's yaw, the feet) is in tracking space, and a
turn of the world doesn't move tracking space: the head's tracking pose is the same before and after, so it
neither re-anchors (no "tracking jump") nor eases. The body's facing in the world is the view frame's mapping of
that yaw, so it turns with the view in the same frame; in explore the pawn keeps its facing and the whole-body
turn (research/29) takes the difference at once. `TestComfort` checks both: the guessed camera maps a controller
to the same world point as the turned camera, 45 degrees round the head from before; a `BodyFollow` fed the same
inputs across a snap doesn't re-anchor and its world facing turns by exactly the snap.

**When the mod turns** (`game/Comfort` `Why`): on foot (game mode 0), a head-tracked world view (not Halcyon's
menu or a flat screen), our main menu closed, a pawn, not in cover, not on a ladder, a camera mode where the hand
aims and the stick only turns (Explore, ExploreStorm, Combat, CombatStorm, TightAim, or a blend toward one: not
the sniper scope, cover, the Mako, cinematics), the squad menu closed, tracked controllers, a v17 host. Anywhere
else the game turns as before (the native weapon and power wheels are game modes 2 and 3, so the right stick
steers them as before). A real pad's right stick still reaches the game (only the motion controllers' X is
zeroed): a player on a real pad keeps the game's turn.

## 3. The comfort vignette

- **The mod's side:** each tick, `vignetteStrength` while the player turns (the mod's smooth turn turned this
  tick, or the game's stick turn past the dead zone where the game turns) or, with `vignetteWhen = move`, while the
  left stick is past 0.2; and for 0.2 s after each snap. 0 with the vignette off, in menus, off foot.
  HostServices v17 `setVignette(amount)`.
- **The host's side (`plugin/Vignette`):** fades toward the target at 1/0.15 s (`vr::comfort::VignetteFade`; stale
  after 250 ms: fades out), and while above zero submits one quad in a VIEW reference space of its own (made
  once per session), 1 m ahead, sized from the projection layer's FOVs to cover both eyes (each angle clamped
  at 75 degrees, plus the eyes' offset). Its picture is premultiplied black whose alpha rises with the angle from
  the centre (smoothstep from 30 to 58 degrees), times the faded strength. It goes first among our quads (under
  the HUD panel, the reticle and the sight), only on world frames, never with Halcyon's or our menu up.
- **Not in the frame loop:** it lives in `HalcyonOverlays::OverlayLayers`, which `xr_session` already calls for our
  quads; nothing in `xr_session.cpp` or `d3d_capture.cpp` changed. It takes a slot only when it shows, and it is
  asked for last, so it never takes one from the squad menu or the wheel (they don't show while the player turns
  or walks).
- **Cost.** Inactive (the usual case): one atomic load and a compare, no layer, no swapchain work. While fading: the
  128 x 128 texture is refilled from the CPU when the strength crosses one of 64 levels (about 14 refills per
  full fade), uploaded (64 KB) and copied into a swapchain image. Measured offline (`mevr-tests`, the dev
  machine): the mask 0.21 ms once a session (the first frame it shows), a refill 0.007 ms of CPU; the upload and
  the copy are a few microseconds of CPU and a 64 KB copy on the GPU. Steady (held at strength): only the layer is
  submitted (the runtime keeps the last released image). Logged every 10 s while used: `vignette: shown in N
  frames ..., M texture refills (X ms CPU each on the render thread)` **[verify in game]**. The compositor blends
  one more quad over the edges **[verify the GPU cost with `tools/perf-probe.ps1` while turning]**.
- **Snapshots:** the layer list carries it as `vignette` (`testloop::NameSwapchain`, its pixels given as a static
  image on each refill); the composite places it from the rendered head as head-locked layers are.

## 4. Walking toward the left hand

Halcyon's `moveFollowsHead` turns the merged left stick left by its head look yaw (`RotateMoveStickByHeadLook`,
after our merge, by `RenderHook::HeadLookYawUU`). With `[Comfort] walk = hand` the mod sets `kFlagHandWalk` on
foot (not in menus, the squad menu or the wheels), and the host turns the motion controllers' left stick before
the merge by `HandWalkUnits(handYaw, headLook)` = head look yaw minus the hand's yaw (Unreal units, `RotateStick`'s
"+ right"): with Halcyon's turn after it, forward is the hand's yaw. The hand's yaw is the left controller's aim
pose's forward laid flat (`FlatYawLeft`); pointing within about 75 degrees of straight up or down keeps the last
usable yaw. With `moveFollowsHead` off, Halcyon doesn't turn the stick and ours alone is the hand's yaw. The
pointing preselect of the squad trigger and the support grip read the controllers' poses, never this stick.
`TestComfort` composes ours and Halcyon's rotation for several hand and head yaws: forward lands on the hand's yaw
within 0.2 degrees, the magnitude kept.

## 5. HostServices v17

`kHostServicesVersion` 16 -> 17. One field appended: `void (*setVignette)(float amount)`. Two pad flags in
`setSupportGrip`'s flags: `gamepad::kFlagModTurns` (bit 17) and `gamepad::kFlagHandWalk` (bit 18). Compatibility:
a v16 host ignores the flags and has no `setVignette`, so the mod (`MEVR_HOST_HAS(setVignette)`) leaves turning to
the game there and logs `comfort: the game turns (the host ASI is older than v17 ...)`; an older mod never sets
the flags, so a v17 host behaves as before. The host forgets the vignette when the mod unloads.

## 6. Settings (`[Comfort]` in `ASI\LE1MEVR.ini`, appended with defaults; the menu writes key by key)

`turn = smooth` (smooth | snap | game), `snapDeg = 45`, `turnSpeed = 120`, `deadzone = 0.15`, `snapThreshold = 0.7`,
`snapRearm = 0.3`, `snapCooldownS = 0.25`, `vignette = 0`, `vignetteStrength = 0.6`, `vignetteWhen = turn` (turn |
move), `walk = head` (head | hand). The menu's rows: Turn style, Snap angle (30/45/60), Turn speed (60-240 deg/s),
Comfort vignette, Vignette when, Vignette strength (10-100%), Walk toward. "Where I look" also turns Halcyon's
`moveFollowsHead` on (as before); "Left hand" leaves it.

## 7. Logs

- Mod: `comfort: settings: turning snap 45 deg per flick (...) | smooth 120 deg/s ... | the game's own; vignette
  on at 60% while turning | off; walk toward ...`; `comfort: the mod turns the player` / `comfort: the game turns
  (<why>)` (each change, at most every 2 s); `comfort: snap right 45 deg (stick +0.83; ControlRotation yaw now ...)`
  (the first 20); `comfort: the pawn faced where the controller does, so it turned with it (Actor.SetRotation): its
  mesh turned +45.0 deg at once` (once; if the mesh waits, the line says the arms may lag a frame in combat);
  `comfort: the game's own stick turn in SFXCameraMode_Explore: SFXCameraInput MaxCameraRotationSpeed ...` (once
  per mode); the heartbeat `comfort: <why>; last 5 s: N snaps, smooth X deg, the pawn turned with it N times;
  vignette wanted in N ticks (peak P%); walking toward ...`.
- Host: `halcyon bridge: the mod turns the player (comfort ...): the game sees no right-stick X from the motion
  controllers` / `... the game turns the player with the right stick again`; `halcyon bridge: the left stick walks
  toward the left controller's pointing ...` / `... where the player looks again`; `vignette: swapchain 128x128`,
  `vignette: first shown ...`, `vignette: shown in N frames of the last 10 s, peak P%, M texture refills ...`.
- Snapshots: the mod's `"comfort"` (turn, snapDeg, turnSpeed, why, modTurns, cameraMode, snaps, vignette target,
  walk, handWalk) and the `vignette` layer.

## 8. Risks [verify in headset]

- **The pawn's SetRotation in combat:** if its mesh transform only updates in the game's next tick, the arms lag
  one frame after a snap in combat (the log line says). Explore isn't affected (the pawn doesn't turn).
- **A camera mode that eases its yaw** would make a snap a fast turn; none is known on foot (the zoom snap is
  dropped by the level camera).
- **Smooth turning feels different from the game's** (no acceleration, the same rate in every mode). `turn = game`
  brings the game's back; the game's per-mode numbers are logged.
- **The vignette's shape** (clear inside 30 degrees, dark past 58) is a judgement call; on a wide headset the
  quad is up to 2 x tan(75) m wide at 1 m. Its 1 m distance gives it a little parallax between the eyes, which a
  soft gradient hides **[verify]**.
- **Walking toward the hand** while the hand points down to the floor keeps the last usable direction; a hand
  waved about while walking steers the walk (that's the option).
