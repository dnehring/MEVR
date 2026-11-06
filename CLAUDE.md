# ME VR Attempt

Native VR mod for Mass Effect Legendary Edition (BioWare's UE3 fork): a C++ ASI plugin loaded by
our own `bink2w64.dll` loader (`src/loader`) or ME3Tweaks' LEBinkProxy, on top of LExSDKv2. One VR system for LE1/LE2/LE3, built LE1 first. Game
mechanics may change to fit VR (see `docs/trilogy-design.md`).

## Git

- **Commit freely.** The user gave standing permission (2026-10-07) to commit without asking.
  Commit at natural checkpoints (milestones, verified changes).
- **Public repository:** `origin` is https://github.com/dnehring/MEVR (public since 2026-10-23). **The user
  pushes**; prepare a push by checking what `origin/main..main` adds: commit emails only the no-reply address
  (`17148851+dnehring@users.noreply.github.com`, set in this repo's config), no game data, dumps, recordings,
  snapshots or binaries, no personal paths, no decompiled or near-whole reconstructed game code in notes, every
  file with its SPDX line (`tools/check-spdx.py`). Push only `main`: never the local `backup/pre-rewrite-main`
  or `worktree-agent-*` branches. `notes/publication-audit.md` stays local.

## Licensing rules (GPL-3.0 project since 2026-10-08)

- The project as a whole is GPL-3.0 (`LICENSE`, `NOTICE.md`); our original files are MIT
  (`LICENSES/MIT.txt`, README "Licensing"). Code from GPL-3.0-compatible sources may be
  copied and modified: GPL-3.0 (Halcyon's MassEffect1/2/3-VR, Overlord, h2-mod, Legendary
  Explorer) and permissive (REFramework, vrframework, MinHook, ImGui). Keep their copyright and
  license notices, and list them in `NOTICE.md`. GPL-2.0-only code is **not** compatible.
- **SPDX line on every new file** (first line, its comment syntax): `SPDX-License-Identifier: MIT`
  for our own code, `GPL-3.0-only` for anything in `src/halcyon/` or containing code adapted from a
  GPL source (and then list it in `NOTICE.md`). Copying GPL code into an MIT file makes it GPL: change
  its line. `python tools/check-spdx.py` lists files without one (`--add MIT <files>` adds it).
- Halcyon's code lives in `src/halcyon/`: imported verbatim in its own commit, then modified, so
  `git log` shows exactly what we changed.
- Closed binaries (e.g. the ME2VR download): never disassemble, decompile or run them. Their
  READMEs, configs and log strings are fair game for concepts.
- UEVR and TF2VR are all rights reserved: never clone or copy.
- LExASIs and LEBinkProxy have no license: read for patterns; ask ME3Tweaks before copying.
- LExSDKv2: use as a git submodule only, never vendored into `src/`.

## Renderer (LE1): Halcyon's, inside our host

- **Direction (user, 2026-10-08): our work is VRIK** (controller aim, arm IK, the gun in the hand,
  two-handed grip, the reticle along the gun). Halcyon's experience is the core: touch it as little
  as possible, only to fix its bugs (like the HDR blue screen) or where VRIK needs a seam. Before
  changing one of its settings or behaviours, compare with upstream (`reference/MassEffect1-VR/
  release/`, `vr_config.h` defaults). **No head aim:** the deploy writes `combatHeadAim = 0` and
  `makoHeadAim = 0`; the hand aims, the head drives the view only.
- Since 2026-10-08 LE1's VR renderer is Halcyon's (`src/halcyon/le1`, built as the `halcyon-le1`
  static library and linked into the **host** ASI, so it isn't hot-reloaded). It owns OpenXR,
  Present, stereo (two renders per present), first person (K), head look and its ImGui menu.
  Its log is `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`; its settings are `MELEVR.ini` in
  `Binaries\Win64\ASI`. Its keys: R recenter (or double-click the right stick), K first person; its
  `menuKey` (`220`, `\`, in our deploy; upstream Insert) now opens **our** menu (below), and Ctrl or
  Shift + `\` its own. Our own VR hotkeys are off on LE1. All of its key polls (menu key, R, K, P, profile keys)
  count only with a window of the game in front (`[MEVR]` seam `MevrBridge::KeyDown`; a `\` typed in Discord
  opened our menu). The menu names keys by the layout (`GetKeyNameText`: `\` is `^` on a German keyboard).
  Its `MELEVR.ini` save (`vr_config.cpp WriteIniFromSlots`, `[MEVR]`) writes a `.tmp` and moves it over the file,
  and is refused for the session if the file existed but couldn't be read at launch (it would put code defaults
  over all four profiles).
  Recenter is yaw-only as upstream, except that a recenter the player asks for also removes a badly
  tilted base space (rolled past 90 degrees, or tilted past `recenterTiltDeg` for 1.5 s: SteamVR's
  upside-down start; [MEVR] `RecenterOrientation`, `core/vr/Recenter`, log `[MEVR-RECENTER]`).
- **The player's menu is ours** (since 2026-10-23, [research/28](research/28-main-menu.md)): the approved
  glass board in RmlUi 6.3 (`external/RmlUi`, FreeType in `external/freetype`; static, in the host,
  LE1 only, `MEVR_MAIN_MENU`). Holding both sticks or `\` opens and closes it; Resume or B close it. It's a
  quad fixed in the room where it opens (`[Menu] distanceM`, `widthM`); the game sees no input, the mod no
  controllers, and on foot or in the Mako the mod pauses the game (`[Menu] pause`, HostServices v16,
  `game/MenuPause`: `WorldInfo.PauseGame` like the game's weapon wheel). Buttons and triggers down when it (or the
  calibration) closes are kept from the game until released (`gamepad::CloseLatch` in the XInput hook). Not drawn for over 1 s (the XR session stopped, lost or exiting; the headset
  asleep) it closes by itself and a running calibration is cancelled (`main_menu::OnPresent`, every Present from
  `PerfPresentEnd`), so the desktop game never stays paused with no input. **Its top bar is the way into the game's own
  menus** (since 2026-10-25, research/28 "The game's menus"; no controller menu button can be counted on, e.g. a Quest over
  Link): Game menu (the game's pause menu) and Holster close our menu and ask the mod (HostServices v19,
  `takeGameMenuRequest`; `core/vr/GameMenuRequest`, unit-tested), which, 2 ticks after the menu's pause is lifted, calls the
  game's own function on the game thread (`game/GameMenuRequest`): `ShowMenuGui` (Start's `Console_Menu`) on the ACTIVE
  game mode, looked up on that object's class so the Mako's or a wheel's override runs; `SFXGameModeDefault.TryHolster`
  (Back's `Console_Holster`) on foot, only while a weapon is drawn (it's a toggle). No pad press: the old fake Start
  (2026-10-25) never reached the game (2026-10-26). Logs `main menu request:`, `main menu: opened the game's pause menu
  (<class>.ShowMenuGui returned 1; ...)`, `main menu: holstered the weapon`; snapshots `"mod"."gameMenu"`. The menu button and each hand's interaction profile are only logged (`halcyon bridge:`). Pointer: a controller's aim ray is
  the mouse (trigger clicks, right stick scrolls, haptic ticks); the pad or left stick moves the focus, A
  activates, B backs out then closes; on Display holding A compares the colour. Sections: Controller guide (every button and gesture,
  texts from the settings: `vr::menu_settings::GuideTexts`; opened once on a first run, `[Menu] controlsShown`), Comfort, Controls,
  Body & calibration (Start calibration opens `plugin/Calibration`), Display, HUD & sights, Advanced
  (Halcyon's settings in sub-tabs). Every row drives a real setting (`plugin/MainMenuSettings`: our
  `LE1MEVR.ini` key by key, Halcyon's live config saved with its own `SaveProfile` when the menu closes;
  never a reset written over a profile; Comfort's turning, vignette and walking rows since 2026-10-23,
  research/31; Display's render resolution offers the headset's own eye size, × 0.85 and × 0.75 of it before
  Halcyon's ladder, and the Upscale row is `[Upscale] method`, since 2026-10-23, research/28's mapping table) or is
  marked Soon. Code: `src/plugin/menu` (game-independent:
  RmlUi's own DX11 renderer wrapped for the game's context, our `chamfer`/`backdrop`/`icon` decorators,
  the data model over `core/vr/MenuSettings`' schema, the frosted backdrop from the left eye's image),
  `src/plugin/MainMenu` (the quad, input, cost). The look is `assets/menu/main.rml` + `main.rcss`
  (deployed to `ASI\menu`, reloaded within a second of a change; `[Menu] assets` can point at the repo).
  Closed it costs nothing. Offline look: `mevr-menu-preview` (PNGs of every section and state, and the
  frame time). Log `main menu:` (its cost every 10 s while open), `menu:`/`rmlui:`; snapshots `"mainMenu"`
  and a `mainmenu` layer.
- **Halcyon's own menu** is the developer fallback: Ctrl or Shift + `\`, or Advanced > Diagnostics >
  Classic renderer menu (both thumbsticks held no longer open it). It takes the pad (Touch merged in as
  Xbox pad 0: left stick/d-pad, A, B; upstream never set ImGui's `HasGamepad` backend flag, so pad nav
  was dead until we did) and our laser pointer (`src/plugin/MevrMenuPointer.cpp`: a controller's aim ray
  on the menu quad, world-locked by default (`worldLockedPanels`, `src/plugin/WorldPanels.cpp`), is the
  mouse, trigger clicks, right stick scrolls). Our tab there, "VR hands" (`src/plugin/MevrMenuTab.cpp`),
  edits `[Hands]` and `[Squad] style` in `ASI\LE1MEVR.ini` (the mod re-reads it) and writes the arm-length
  sample (`[Arms] measureRequest` counter plus grip/head positions; `armLengthM` is shown if present).
- Our seams into it are marked `[MEVR]` and declared in `src/halcyon/le1/mevr_bridge.h`
  (implemented in `src/plugin/HalcyonBridge.cpp`): static OpenXR loader, our controller actions
  on its session, Touch → Xbox pad 0 in its XInput hooks. Keep changes to its files small and
  marked; `git log -p -- src/halcyon` must show every change.
- The mod learns from `HostServices::controllers` that the host owns VR, and then skips our own
  VR output (`core/vr/Output`), scene view hook, reflection fix, XInput hook and VR hotkeys.
- **UObject::ProcessEvent is hooked by the host at attach and never removed** (all games); the
  mod's handlers run through `HostServices::setProcessEventCallback`. Halcyon hooks it too, with
  its own MinHook, on top of ours: never uninstall a hook another hooker may sit on.
  LE2/LE3 still use our own renderer until Halcyon's ME2/ME3 code is brought in.
- **Never call ProcessEvent (or anything that runs UnrealScript) off the game thread** (the engine tick's).
  The script VM keeps its state in process-wide globals (GPropAddr, GRuntimeUCFlags...), so a call from the
  Present/render thread races the game thread's script. That caused the 2026-10-23 crash (Halcyon's
  Present-thread hides; [research/36](research/36-crash-breadcrumbs.md)). Halcyon's `ProcessEventSEH`
  (head_aim.cpp, game_camera.cpp) queues off-thread calls for the next tick (`[MEVR]` seam,
  `MevrBridge::QueueProcessEvent`, `plugin/GameThreadCalls`). The host hook logs any other call as
  `process event: off the game thread: <fn>`.
- **Crash breadcrumbs** (research/36, HostServices v18): the host keeps the last 128 ProcessEvents, the mod's
  CallFunction handlers and the mod's writes into engine memory. Every new write into engine memory gets a
  `write_journal::Wrote("Module.what", address, size)` right after it (`core/WriteJournal.hpp`).

## How we work

`docs/development-approach.md`: build on Halcyon and own the VRIK; let Claude see the game through
the test loop (eye snapshots, pose recording and replay); read the engine with Ghidra
(`tools/ghidra`, GhidraMCP) instead of by hand; parallel agents in worktrees, merged only after all
three builds and the tests pass.

## Before each VR milestone

Read the matching chapter of `reference/vrframework/guides/` (MIT, a 17-part guide on adding
VR to closed engines): 07 frame timing, 08 stereo, 09 camera math, 10 submission and TAA,
11 HUD, 12 input, 14 engine quirks. Don't re-derive what it already explains.
`research/12-vr-frameworks-survey.md` explains why we keep our own small core rather than
switching frameworks.

## Layout and workflow

- `research/`: sourced findings. Record new findings here with sources. Mark unverified UE3
  assumptions **[verify]** and update the hypothesis table in `research/08-ue3-hypotheses.md` as
  they're confirmed or refuted.
- `docs/`: plans and decisions. Keep them consistent with `research/`.
- `docs/knowledge/`: the knowledge bundle ([Open Knowledge Format](https://okf.md/spec/): markdown with YAML
  frontmatter, `type` the only required key). `gotchas/` holds every problem that bit twice or cost hours: the symptom
  in every wording it had, the cause, the fastest check, the fix in place, the dates it bit (`bitten`). **When a
  symptom sounds familiar, read `docs/knowledge/gotchas/index.md` before investigating; when one bites again, add the
  date and the new wording there.** New ones follow `docs/knowledge/how-to-write.md`; `python tools/check-okf.py
  docs/knowledge` (ctest `mevr-okf`) checks the bundle.
- `reference/`: read-only third-party clones (gitignored). Recreate with `tools/fetch-references.ps1`.
- `notes/headset-checklist.md`: everything that still needs the user in the headset, by area and owning session, plus
  what Claude can check alone on the fake headset. Add a line when you build something that needs the headset; delete it
  (and write the result in its research note) once it's checked.
- `notes/game-inventory.md`: exe versions and hashes from `tools/inventory-game.ps1`. Re-run after
  game updates. All three exes are 2.0.0.48602 (the build LExSDKv2's RVAs target).
- `mods/`: raw mod downloads (gitignored). Unpack `.m3m` merge mods with `tools/extract-m3m.ps1`.
  MassFPS by Aphar (Nexus mod 1731, fully permissive) was studied locally
  ([research/11](research/11-massfps.md)); its files aren't in the repository: download it from Nexus
  and extract it with `tools/extract-m3m.ps1` to read them.
- Game install: Steam app 1328670, currently `C:\Program Files (x86)\Steam\steamapps\common\Mass Effect Legendary Edition`.
- Dev machine: RTX 4090, Quest via the Oculus OpenXR runtime, VS 2022 Build Tools (no IDE), CMake.

## Build

- Configure: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64`. Build:
  `cmake --build build --config Release --target MEVR-LE1`, producing `build/Release/LE1MEVR.asi`.
- Deploy: `tools/deploy-asi.ps1 -Game LE1` (needs the ME3MM ASI loader). The log is at
  `<Win64>/Logs/LE1MEVR.log` or `%LOCALAPPDATA%/MEVR/`.
- **Hot reload (default build):** `LE1MEVR.asi` is a small host; the code lives in
  `LE1MEVR_mod.dll`, which the host reloads within about a second of each rebuild (the
  `.modpath` file points at `build/Release`). So during a play session, just rebuild
  `MEVR-LE1-mod`; the user stays in game. Changes to `src/plugin/` (the host) need the game
  closed and a redeploy. Release builds use `-DMEVR_HOT_RELOAD=OFF` (one ASI; e.g. `build-mono/`).
- Script-level hooks must wait for the script packages to load (they aren't there at attach).
  `Mod.cpp` retries every second before installing the ProcessEvent hook (LE1: `ue3::runtime::Init`, then
  `Engine.Camera.UpdateCamera` through our reflection layer, research/34 stage 3; LE2/LE3: `engine/Legacy`).
  Engine reads go through `src/ue3/` (handles, `Params`); new LExSDKv2 use belongs only in `engine/Legacy`,
  ReflectCheck's reference side until stage 6 removes it (the hooks and RVAs are ours since stage 4: `core/Hook`'s
  `hook::Context`, every raw address in `engine/Addresses.hpp`, research/34).
- Hotkeys: not F-keys (bound in game), not backtick (UE console). `]` toggles VR (the XR
  session) on and off, and with it every VR feature: once a session runs, alternate-eye stereo,
  6DOF, hand aim and arm IK all come on; VR off turns them off. The rest are dev overrides:
  `[` switches between head-tracked and screen mode; `\` cycles the head path (render-only /
  camera / off); `'` cycles the desktop head simulator (sweep / nod / off, used while VR isn't
  head-tracked); within a session `;` (stereo), `/` (6DOF), `,` (hand aim: shots follow the right
  controller, on foot) and `.` (VRIK arm IK) switch one feature each, until the next `]` on;
  `=` toggles the automatic virtual screen for conversations, cutscenes and menus (default on);
  `-` toggles the lockstep (game thread waits for each Present; default off). These are for our
  own renderer (LE2/LE3); on LE1 Halcyon's keys apply instead.
- Motion controllers reach the game as Xbox pad 0 (`game/GamepadBridge`, XInput hooks). Mapping
  is in `game/GamepadMapping` (unit-tested). The bindings are ours only (`core/xr/Input`, on Halcyon's session; it
  has no action set): Touch, Index, HP Reverb G2 (`XR_EXT_hp_mixed_reality_controller`, enabled by Halcyon's
  instance only when the runtime lists it, `[MEVR]`), WMR (`microsoft/motion_controller`: trackpad click = A, no B),
  Vive wands (trackpad = stick, no A/B) and `khr/simple_controller` (no grip, no sticks). A limited profile logs
  `halcyon bridge: controller profile ...: <what's missing>`. Nothing depends on a menu or system button.
- Physical grenades and melee (LE1, `game/Throwables` + `core/ik/Throwing`, research/20,
  `[Throwables]`): a new grip squeeze at the grenade slot (left side of the chest) takes one, a fast
  release throws it (the game's `UseGrenade`; its disc re-aimed from the hand in a post-handler of
  `BioGrenade.LaunchGrenadeAtPoint`), a slow one puts it back; a fast stroke with an enemy in reach
  is `TryMelee`. Claimed squeezes run before holsters; a left squeeze in the support grip's zone is the
  support grip, never a grenade. X stays unless `grenadeButton = 0` (host v14). **Melee is the gesture only** (research/20
  "Gesture only", 2026-11-05): B no longer melees on foot (`meleeButton = 0`, migrated once); with the weapon away the
  right FIST (grip and trigger) punches through `BioPlayerInput.ActivatePower(Pistol_Whip)` (`meleeFist`; the left hand
  doesn't, its trigger and grip are the squad's); both hands share one cooldown that also waits out the game's melee
  power's (`ik::throwing::RouteMelee`, `MeleeWait`). `mevr melee punch|strike` tests it without controllers.
- No cover snapping, physical crouch (LE1, VR on foot; `game/Posture` + `core/ik/Posture`, research/23,
  `[Hands] noCover`, `physicalCrouch`, `crouchBelow`/`standAbove`/`crouchDwellS`): ME1 takes cover by
  itself (BioPawn's native test, no button; A only leaves it). `engine/cover_acquire` pre-hooks that
  test (`0xAB1430`, mod side) and skips it for the player's pawn; a cover already taken is left with
  `TryExitCover`. A real crouch below 0.75 of the standing eye height (hysteresis 0.85, 0.2 s) is
  `BioPawn.ShouldCrouch` (ProcessEvent), on physical changes only: L3 still toggles in between.
  **VR cover** (research/23 section 6, 2026-11-05; `core/vr/VrCover` for all three games, `game/VrCover` LE1; `[Cover]
  protect`, `reachM` 1.2): the game's cover cut (LE1 `ProxyTakeDamage`'s cone: all of a hit from the cover's front beyond
  3 m on Casual and Normal, 90% above) for a VR player whose head is hidden from the shooter by a surface within reachM of
  the eyes (traced from the eyes): inside that damage call only, `engine/call_function`'s answer handlers (a call without
  parameters answered, with the calling frame's Locals) say `IsInCover` true and `IsInCoverLeaning` false for the player
  and `BioPawn.CoverNormal` holds the surface's facing until the call returns; the game's own code does the rest. The head
  raised over the cover is the game's "leaning out". Logs `vr cover:`; snapshot `"mod"."vrCover"`; `mevr cover [on|off | test | pawns | hit <damage> <n>]` (`hit`: the game's
  own `BioActorBehavior.TakeDamage` on Shepard from pawn n's place with a weapon's damage object, health and shields before and after;
  `probe <yawDeg>` finds a surface). Measured on the fake headset: covered, no damage; exposed or off, full damage.
  LE2/LE3's damage rules and the plan: research/23 section 6.3.
- LE1 level camera (`game/view_level`, `core/vr/ViewLevel`, [research/24](research/24-view-level.md)):
  each shot's recoil goes into the player controller's rotation (ControlRotation) and the rifles' pitch
  kick never fades (`RecoilFadeSpeed=0`); with the stick's pitch off (`decoupledPitch`) the world tilted
  (+30 degrees after 204 shots). A `PlayerTick` post-handler (before the camera update) holds its pitch and
  roll at 0 on foot while the hand aims, and drops aim assist's zoom-snap target; not in cover, the
  scope, the Mako or cinematic modes. The recoil is a hand pulse instead. `[Hands] levelCamera`,
  `recoilHaptic`; log `view level:`. Aim assist's friction and adhesion (`SFXModule_AimAssist.ViewFriction`
  / `ViewAdhesion`, which slowed and bent the stick's turn near enemies) are zeroed by CallFunction
  post-handlers under the same conditions (cover included; not the sniper scope or the Mako):
  `[Hands] aimAssist = 0` (1 keeps them), log `aim assist:` (research/24 section 7). **The gun model kicks instead**
  (research/24 section 8, mod only; `game/gun_kick`, math `core/ik/GunKick`, unit-tested; `[Hands] gunKick`, `gunKickScale`;
  the menu's Controls > Weapon drawn): each shot (the `SetWeaponRecoil` handler; its queued kick, else
  `BioWeaponRanged.GetRecoil`, else the weapon's last) turns the gun muzzle up about the hand and moves it back along the
  barrel (0.8-4 degrees, 0.4-2.2 cm by the recoil; a critically damped spring, peak at 22 ms, a tenth by ~120 ms; stacked
  under a soft cap of 6 degrees and 3 cm), half with both hands, its back motion faded as the sights' approach weight
  (`sights::RaisedWeight`) brings it to the eye. Arm IK's `Request::gunKick`, applied by the writer after the support grip;
  each call's kick goes to `bone_pose::WrittenGunKicks` (the last two: an attachment updated late shows the one before) and
  **`weapon::Current`, `BoneWorld` and `ModelInGunFrame` take it back out**, so the aim ray, the shots, ADS, the assist and
  the scope capture follow the controller; only the sight's published centre (the window, the eyepiece) is put back on the
  kicked model (`weapon::ToDrawn`). `mevr kick [recoilDeg] [shots] [ms]` fires synthetic shots. Logs `gun kick:` (heartbeat),
  the weapon line's `the gun kick taken out of N readings`; snapshot `"mod"."gunKick"`.
- LE1 guided calibration ([research/25](research/25-calibration.md), HostServices v15): our menu's Body &
  calibration > Start calibration (or the VR hands tab's Calibrate, or a one-time prompt for uncalibrated
  controllers: its 8 s delay and 45 s timeout count only while the session has input focus, and its text says where
  calibration lives later) runs about a minute in the headset
  (`plugin/Calibration`; look in `plugin/CalibrationDraw`, the main menu's style; offline look:
  `mevr-calibration-preview`): stand tall, T pose, arms ahead, crouch, a pistol orb and two rifle orbs aimed
  at three targets (captured on a 1 s hold, no buttons; B back a step, both sticks held cancel), a red-dot
  check, save. Why an aim isn't captured is on the panel and the board; an aim off the other two is redone alone,
  and after two tries the agreeing pair is kept (`vr::calibration::AimSet`; rifle 4.5 degrees, pistol 3; research/25
  section 9); a cyan ring marks each hand's grab point (`[Calibration] grabOffset`). Solved in `core/vr/Calibration` (unit-tested): the gun frame (eye-to-target line, level) in
  the right GRIP frame, the line's offset from the grip, the eye, the rifle's foregrip, left trim and stock; **each empty
  hand's neutral trim** (research/25 section 10, 2026-11-06: arms ahead, thumbs up, wrists straight, the fingers along shoulder
  to grip, the thumb side up, checked against the T pose; `handTrimLeft`/`handTrimRight`; arm IK's empty hand is grip * trim,
  the `[Hands]` hand angle a fine trim around its 90/0/0 default, `hand_calibration::FineTrim`).
  Saved per controller type (interaction profile + headset) as `[Calibration.<key>]` in `ASI\LE1MEVR.ini`;
  `game/BodyCalibration` applies it: the barrel follows grip * trim (not the aim pose), the gun's sight is
  moved onto the calibrated line (`gunShift`, the assist on top), the foregrip point takes the left grip,
  two-hand aim keeps the calibrated hands line, the shoulder zone, the crouch thresholds, the holsters'
  eye height. While it runs the game sees a neutral pad and the mod no controllers. `[Calibration] use = 0`
  or no section: everything as before. Log `calibration:`.
- LE1 body holsters (`game/Holsters`, `core/ik/BodySlots`, research/19): a right-grip squeeze at a
  body slot draws that weapon through `SFXGameModeDefault.SwitchToWeapon` (ProcessEvent). The
  right grip holds the gun (`gripHolds = 1`): let go at its slot to holster (`TryHolster`; while the draw still
  animates, `IsSwitchingWeapons`, the game ignores it, so the holster waits for the switch's end unless the grip
  closes again), elsewhere it goes back after `releaseGrace`; the grip is never RB. **The right stick click held 0.3 s opens
  the game's power wheel, let go closes it, a tap casts nothing** (the user, 2026-11-05; research/19 "Hold only"): the mod calls
  `EnterPowerWheel`/`ExitPowerWheel` itself (`gamepad::PowerHoldFromStickClick`) and the click is never RB (`kFlagModPowerHold`),
  because the game's RB casts the mapped power for any press under its 0.3 s; `powerWheel = y/x/b` are still RB.
  Contact haptics while the hand is in a slot
  (host `core/vr/Haptics`); the slots scale with the standing eye height from the floor (host
  `core/xr/Floor`, STAGE). Squad weapons: below. Settings: `[Holsters]` in `ASI\LE1MEVR.ini`.
  **Only holsters draw** (`onlyHolstersDraw = 1`, 2026-10-25): on foot in VR with tracked controllers, once the
  CallFunction hook is in, the game's own draws with the weapon away (`SFXPawn_Player.DrawPrimaryWeapon`: melee and
  grenade buttons, the weapon wheel, the holster key's toggle) are skipped by `engine/call_function`'s skip handler (it
  consumes the call's EX_EndFunctionParms; only for a script function without parameters), with a short buzz in the
  right hand. The trigger too (RT is `SFXGameModeBase.FireWeapon`, which draws first), without the buzz (a fist pulls it).
  **The skip only works since 2026-11-04** (research/19 "The trigger's draw"): FFrame is packed on 4 bytes, `Node` at 0x14 in
  LE1 (`engine::adapter::kFrame`), and the old 0x18 made every skip fail silently. Medi-gel and powers still draw (callers `SFXGameModeDefault.UseFirstAid`, `TryUseMappedPower`,
  `EnterPowerWheel`, matched against the skip handler's caller; the lead's decision 2026-10-26). A grenade taken with
  the weapon away is ours (`holsters::AllowGameDraw`). Logs `holsters: the game wanted to draw your weapon (<caller>):
  refused`, `... for <caller>: let through` and, for a draw by any other route, `holsters: the <gun> came out without a
  holster grab (triggers, grips)`. The shotgun's slot is the left hip (`hipLeft`, a cross-draw).
- LE1 body under the player ([research/29](research/29-body-follow.md), `core/ik/BodyFollow`, `game/BodyFollow`):
  first person, Shepard's whole body (the `Root` subtree; never `God`/`Camera`) turns by what the spine's 30
  degrees can't (the explore pawn faces where it runs) and slides under the view (feet that step after the
  head's pivot, `[Hands] leanM` 0.20, at most `bodyMoveMaxM` 0.75); the spine leans the head bone to the
  pivot. Re-anchors on start, recenter (host floor counter) and tracking jumps. `[Hands] bodyMove = 0`: the
  old spine-only lean. Log `torso:`; snapshot `"torso"`. **Moving, the legs keep the run** (2026-11-04, research/29
  "The legs keep the run"; `core/ik/BodyFollow` `LegsFollow`, `TorsoGoal::legsWeight`): with `Engine.Actor.Velocity`
  above 60 u/s the legs keep the pawn's facing (where the run, strafe and backpedal animations point) and the spine
  twists up to `[Hands] legsTwistDeg` 60 from them; the whole body turns only by the rest, keeping its side when
  backing up (`legsFollowMotion = 0`: the old rule; log `torso: legs:`, snapshot `torso.legs`; replay
  `tools/make-walk-recording.py`'s walk with `-Inputs`). Room-scale (the pawn following real steps past the cap) is
  designed there, not built.
- LE1 body at the player's size ([research/58](research/58-body-scale.md), 2026-11-06; the user: "vital for release"; `core/ik/BodyScale`,
  `game/BodyScale`, `engine/BonePose`'s `Request::bodyScale`): on foot in first person the bone writer scales every bone but
  Camera and God by s about the floor under the view, after the solves (the skinning honours `FBoneAtom::scale`, and the gun
  and anything attached grow with the hand: measured), and maps every goal through the inverse before them, so the hands land
  on the controllers 1:1 (reach scaling only without it). `[Body] scale = auto` (default): s puts Shepard's eyes at the view
  (the view's standing height above his floor over his standing holstered eye height, both measured live; 0.85..1.25, 0.1/s);
  `off`, or a number. So the view's height decides the size: the user's 1.84 m eye height against Shepard's 1.69 asks 1.089,
  their profile's `cameraOffsetUp = -0.082` held it at 1.048. `mevr body` measures (bones above the floor, arms, the gun), `mevr
  body scale <s>|auto|off`; log `body scale:`; snapshot `"mod"."bodyScale"`. [verify] in the headset.
- LE1 weapon drawn, like exploring ([research/38](research/38-combat-stance.md), `game/CombatStance`, rules in
  `core/vr/CombatStance`; `[Hands]` in `ASI\LE1MEVR.ini`, on by default except `exploreStance`; our menu's Controls > Weapon drawn): VR on
  foot, first person. `exploreStance` (A, mod; **off by default until the headset confirms the redesign**; no menu row since
  2026-11-04, the owner: file-only until then): drawn and
  standing (not crouched, in cover or scoped), the player's AnimTree `CombatMode` node composes from its NonCombat child:
  `engine/bone_pose`'s pre-stage writes the two child weights around UpdateSkelPose and puts them back, so the tree still
  ticks as combat and every notify fires; it acts only once the node's `NodeTickTag` showed the tree ticks before the
  hook (`stance: [verify]`). **The arms keep the combat pose** (2026-10-25 redesign: the explore animations also drive
  `Prop01`, the gun's bone, and `LHIK`, the foregrip, so the gun sat differently in the hand and the foregrip wandered):
  each upper arm and everything below it (hands, fingers, `Prop01`, `LHIK`; never `Camera`/`God`) is put back, after the
  explore compose and before arm IK, at the combat pose's place relative to its parent (`core/ik/PoseGraft`), learned per
  skeleton and weapon type from calls composed as combat (the first standing draw of each type shows the combat pose until
  it holds still, about 0.15 s); AnimatedHands' left hand is the combat one on the gun. No learned pose: no explore
  compose. `constantEye` (B,
  host): a `[MEVR]` seam in Halcyon's `FpOwnAndWrite`, `MevrBridge::ConstantFpEye`: every standing mode's Offset = its
  own HookOffset minus the explore eye; crouch modes keep Halcyon's, except while the player is physically crouched
  (the mod's `gamepad::kFlagPhysicalCrouch`, 2026-11-04): then only the real head lowers the view (`[Hands] crouchDrop = 1`:
  the old double drop; research/38 "the physical crouch keeps the eye"). `exploreRamp` (C): the combat speed ramp set to the
  explore one (restored). `drawRate = 6` (D, 1..10; the animation still plays, so every notify fires): the WeaponEquip node's Draw/Holster sequences play faster (their Rate
  while playing). Logs `stance:`, `constant eye:`, `movement:`, `draw:`; snapshot `"stance"`.
- LE1 comfort ([research/31](research/31-comfort.md), HostServices v17; the menu's Comfort page, `[Comfort]` in
  `ASI\LE1MEVR.ini`): `turn = game` (default, the menu's Smooth) keeps the game's own turn (per-camera-mode
  `SFXCameraInput`, logged; its rate can't be set, so the menu has no turn speed: user, 2026-10-23); with `snap`
  (`snapDeg` 30/45/60 per flick, re-armed near the centre, 0.25 s apart) or the file-only `smooth` (`turnSpeed`
  deg/s, linear) the mod turns the player itself on foot (`game/Comfort`, rules in `core/vr/Comfort`,
  unit-tested). The game then sees no right-stick X from the controllers (`gamepad::kFlagModTurns`;
  a real pad's still turns). The yaw goes into ControlRotation in the **mod's tick, before arm IK**, the pawn
  turns with it when it faces the controller's way (combat; `Actor.SetRotation` via ProcessEvent), and
  `view_anchor::NoteTurn` maps the view as the next camera update shows it: no frame with the arms or shoulders
  posed for the old heading (body follow's state is in tracking space: no re-anchor). Not in the Mako, wheels,
  squad menu, scope, cover, menus. The vignette (`vignette`, `vignetteStrength`, `vignetteWhen = turn|move`): the
  mod sends a target each tick (`setVignette`), the host fades it over 0.15 s and draws a head-locked quad in a
  VIEW space of its own (`plugin/Vignette`, first of our quads in `HalcyonOverlays`; nothing while unwanted; not
  in `xr_session`'s loop). `walk = hand` (`kFlagHandWalk`): the host turns the controllers' left stick to the
  left controller's flat yaw before Halcyon's `moveFollowsHead` turn (`WalkTowardHand` in `HalcyonBridge`). The menu's Walk
  toward offers Where I look and Left hand only (Body, `moveFollowsHead` off, brought back Halcyon's "look left, move right"
  bug: removed 2026-11-04, the owner; also gone: Advanced's Look speed, head rotation not 1:1, `MELEVR.ini` only). Logs
  `comfort:`, `vignette:`, `halcyon bridge: the mod turns ...`; snapshots `"comfort"` and a `vignette` layer.
- LE1 conversations and cutscenes on a 3D cinema screen ([research/52](research/52-cine-3d-screen.md); `[Cinematics]` in
  `ASI\LE1MEVR.ini`, on by default; our menu's Comfort > Conversations and cutscenes: In VR / 3D screen / Flat screen): a VR cine
  renders both Stereo 2 passes at the game's own projection from two parallel cameras on the director's camera (head look and
  lean parked; `PlanStereo`'s screen branch, mode 1) and each eye's render goes on its own quad (eye visibility) at one pose,
  fixed in the room by `PlacePanel` (`widthM` 3.0, `distanceM` 2.6). Each eye drops `depth / (1 + depth)` of its width on
  opposite sides, which puts infinity `depth` (1.5% of the width) behind the screen; the cameras' distance follows each shot's
  lens so the scene `convergeM` (1.2 m at 100 units per metre) away lands on the plane; infinity's disparity is capped at 80% of
  the eye distance (`core/vr/CineScreen`, unit-tested). `[MEVR]` seams in xr_session.cpp (decision, park, projection, lean, plan,
  submit, and the conversation movie at its authored size instead of Halcyon's half-size VR layout: the wheel 1.75x larger); host
  `plugin/CineScreen`. Logs `cine screen:`, `[VRSTATE] ... -> 3D screen (MEVR)`; snapshot `"cineScreen"`.
- LE1 start and front end on one panel ([research/56](research/56-start-and-front-end.md), `core/vr/PresentationHold`, host
  `plugin/PresentationHold`; three `[MEVR]` seams in xr_session.cpp's decision): from start-up until the first load from the menus
  or 30 frames of a gameplay mode, every other mode is the menu (the flat panel, no second eye): the unreadable byte at start
  (Halcyon showed the title's 3D backdrop as plain VR for 1.5-2 s), the logos' movie mode, the title and its camera flight (6,
  which latched a cutscene: the 3D screen came and went); after the menus a load's -1 and 8 too. The camera's name latches a
  cine only when the byte is unreadable (Halcyon's own comment). Plain VR after another view waits 100 ms (the title's 6, 0, 7
  flashed the world for a frame). Log `presentation:`. **Since 2026-11-05 the front end is on the 3D cinema screen**
  (`[Cinematics] frontEnd = 1`, default; the owner: the flat panel's picture moved with the head, Halcyon keeps head look on
  there): the title and main menu a cutscene on the screen (`MevrBridge::FrontEndOnScreen`, `CineInputs::frontEndScreen` in
  `DecideCine`/`CineFinishDecision`, the second-eye gate as 6), head look parked, the title in stereo on a fixed screen; the boot
  before the title flat with head look parked (`FrontEnd::BeforeTitle`, `frontEndBoot`), movies flat at once, the load from the
  menus flat from its first frame. Checked on the fake headset. research/56 "The front end on the 3D screen".
- LE1 HUD (Halcyon's renderer, Stereo 2): the game's UI draws go to a body-locked HUD panel quad,
  and a hand reticle on the aim ray replaces the crosshair (`src/plugin/HalcyonOverlays.cpp`,
  [research/15](research/15-hud.md)). Scaleform's non-"over" blend modes (multiply...) go there too,
  through a tint texture the panel's alpha is made from (they used to stay in the eyes: the doubled
  wheel). While the game's weapon or power wheel is up, its part of the UI (`core/vr/WheelCrop`:
  guessed, then measured once per session) is cut out of the HUD panel and shown on a quad of its
  own at the left / right hand ("wheel"). Toggles: `MELEVR.ini` `hudPanel*` / `handReticle*`, our menu's
  HUD & sights and Advanced > HUD placement, or the classic menu's HUD tab; the wheel panel's in `[Squad]`.
- LE1 using things with A ([research/40](research/40-interaction.md), HostServices v21; `[Interact]` in `ASI\LE1MEVR.ini`):
  the game chooses what A uses in `BioPlayerSelection`'s native UpdateSelection (`0xC79D70`) from the player camera's
  `CameraCache.POV` (a cone around its forward, half its FOV, the distance-scaled selection angle, line of sight), which
  follows the body and the stick, not the head. `engine/SelectionGaze` (mod) hooks it and, for that call only, puts the
  head's world place and forward into the POV (put back in `__finally`, journalled; `gaze = 1`). The game's prompt is the
  HUD's target status widget at the top centre (name, action, the A glyph only in use range): `game/Interact` sends the
  selected object's place (above its collision bounds' top, capped) to the host (`setInteractLabel`), and
  `plugin/HalcyonOverlays` cuts the widget's band (`vr::interact::DefaultCrop`: stage x 384..896, y 30..100, or `crop`)
  out of the HUD panel onto a quad at the object, facing the eyes, `labelHeightDeg` (6) tall at any distance (`label = 1`;
  combat targets stay on the HUD unless `combatLabel = 1`). Logs `interact:`, `[MEVR-INTERACT]`; snapshot `"mod"."interact"`,
  a `label` layer.
- LE1 fingers ([research/43](research/43-fingers.md), `core/ik/Fingers`, `game/Fingers`; `[Hands] fingers`, `fingerCurlDeg` 70): first
  person with tracked controllers, each hand's index curls with its trigger and the other fingers with its grip (thumb x 0.35),
  applied in `engine/BonePose` after arm IK (`Request::fingers`, inside its one journalled SpaceBases write). Weapon drawn: the right
  hand keeps the animation's grip but the index rests off the trigger (-0.5 curl) until squeezed; the left hand on the foregrip is
  untouched. **Touch sensors** (research/43 "Touch sensors", 2026-11-04, `[Hands] fingerTouch`): Touch and Index controllers report
  a finger on the trigger and a thumb on the stick/buttons/thumb rest (`core/xr/Input`'s optional `trigger_touch`/`thumb_touch`
  actions, suggested with a fallback without them; `vr::controllers::Hand::triggerTouch[Known]`, `thumbTouch[Known]`, so host and mod
  deploy together; recordings' button bits 6-9): a lifted finger points (on the gun: lies along the frame until touched), a lifted
  thumb is up (open hand, thumbs-up in a fist); rules `ik::fingers::FromController` (unit-tested), eased over 0.08 s. Bones `{Left,Right}{Thumb,Index,Middle,Ring,Pink}Finger[1,2]` under `LeftWrist`/`RightWrist`; the flexion axes are
  measured from the rest pose (palm plane from the knuckles, palm side from the thumb). **Side finding (2026-10-31):
  `BonePose::TakeRequest` never copied `Request::gunShift`** since 24fed58, so the sight assist and the scope pull never reached the
  pose until then. Logs `fingers:`; snapshot `"mod"."fingers"`.
- LE1 HUD pieces ([research/41](research/41-hud-pieces.md), HostServices v22 `setHudPieces`; `core/vr/HudPieces`, `game/HudPieces`,
  host in `HalcyonOverlays`; `[Hud]` in `ASI\LE1MEVR.ini`): the HUD's player status (health/shield arc, squad portraits, medi-gel;
  bottom CENTRE of the HUD) on a quad at the left wrist (`wristStatus`, `wristWidthM` 0.08, `wristOffset` in the left grip's axes),
  and the weapon panel (gun silhouette, heat strip, grenades; bottom left) over the gun's receiver (`gunStatus`, `gunBackM` 0.15,
  `gunUpM` 0.04; the mod publishes the anchor from the aim ray and `sights::GunTop`; raised to the eye, `sights::RaisedWeight`
  over 0.55 and back under 0.35, it leaves the gun for the HUD panel so the sights stay clear: the owner's QA, 2026-11-05,
  "I can't actually see through my iron sights"). Both masked out of the HUD panel (the panel
  shader takes three masks now); the radar and power bar stay. The HUD frame is ~1306 stage px tall and its bottom widgets are
  anchored to the frame's bottom, so the crops are bottom-anchored (`DefaultWristCrop`/`DefaultGunCrop`; `wristCrop`/`gunCrop` by
  hand). Logs `hud pieces:`, `[MEVR-HUD] ... wrist/gun`; snapshot `"mod"."hudPieces"`, layers `wrist`, `gun`.
- LE1 vent ([research/45](research/45-vent.md), `core/ik/Vent`, `game/Vent`; `[Vent]`; a mechanic change, docs/trilogy-design.md's
  change log): weapon drawn, the left grip squeezed in the receiver zone (a box in the gun's frame over and ahead of the right hand,
  `zone*`) of a HOT gun (heat >= `ventMinHeat` 0.3 of `ventMaxHeat` 1.0 [verify the scale: the log says what heat each overheat
  happened at]) then racked back fast (`ventSpeedMS` 1, `ventTravelM` 0.05) and released: `BioWeaponRanged.m_fCurrentHeat = 0`,
  `m_bOverheated = false` (journalled), `DoCoolDownEffect` through ProcessEvent, both hands pulse, `ventCooldownS` 4. Arbitration:
  while the left hand is in a hot gun's receiver zone and no support grip is held, `support_grip` reports the squeeze "away from the
  gun" (`vent::MasksSupportZone`), so it reaches the mod: grenades first, then the vent; a cool gun's receiver is the support grip.
  No game function cools a weapon. Logs `vent:`; snapshot `"mod"."vent"`. **The visible rack** (section 6, 2026-11-04,
  `[Vent] animate`, mod only, [verify]): while the hold lasts a part of the gun's own model (`<type>Part = auto`: pistol
  `Barrel_Upper_front`, assault rifle `Scope`, sniper none; a model without one logs its bone list once) follows the hand
  back along the gun, clamped to `partTravelUnits`, and springs home at the release (`ik::vent::Rack`), written into the
  gun's SpaceBases after its UpdateSkelPose (`engine/bone_pose` weapon-part stage, `BonePose.ventPart`; ForceSkelUpdate if
  the gun isn't posed by itself). `mevr vent heat <f> [hold] | rack [f] [s] | part` test it without controllers,
  `tools/make-rack-recording.py` the whole gesture under a replay with `-Inputs`. Logs `vent: rack:`; snapshot
  `"mod"."vent"."rack"`.
- LE1 game pointer ([research/42](research/42-game-pointer.md), host `plugin/GamePointer`, math `core/vr/GamePointer`; `[Pointer]`):
  while Halcyon shows the game as its flat quad (`menuFlat` mode 7, `galaxyFlat` mode 9; `flatCine = 0` keeps flat conversations and
  cutscenes out), a controller's aim ray on it is the game's mouse: the hit's UV is the window's client pixel (the backbuffer fills
  the quad edge to edge), sent as `WM_MOUSEMOVE`/`WM_LBUTTON*`/`WM_MOUSEWHEEL` posted to the game window (`moveMessages`) and as
  `SetCursorPos` (`setCursor`), `sendInput = 1` for clicks by SendInput (foreground only); the trigger latches 0.6/0.4, the right
  stick is the wheel (`wheelPerSecond` 6), a dot (`dotDeg` 0.6) 5 mm in front of the quad. Not while our menu, Halcyon's menu or
  the calibration is up. One `[MEVR]` seam: `MevrBridge::OnFlatQuad` from `xr_session.cpp`'s flat quad block. Which of the two
  mouse paths LE's Scaleform menus take is [verify]. The VR dialogue wheel (in the eye images) is not covered. Logs `game pointer:`;
  snapshot `"gamePointer"`, layer `gamepointer`. **The cursor and messages are posted from a worker thread** (`postThread = 1`,
  2026-11-03): the identical click posted from the Present callback was ignored by a Flash message box (research/51 section 8);
  `logMessages = 1` logs each. While its ray is on the flat screen (or on the galaxy hologram) the pad's triggers are kept from
  the game (a held RT blocked the box's click). **Touch** (research/53 section 6.2, Half-Life: Alyx's way, 2026-11-04): a fingertip
  (`touchTipM` 0.04 ahead of the aim pose) within `touchHoverM` (0.06) of a screen's visible part takes over from the rays, pressing
  at `touchPressM` (0.005) from the front, letting go past `touchReleaseM` (0.015); haptic tick on arrival, click on press; the
  world UI passes its crop as the touchable part (`NoteGameUiQuad`'s `touchArea`); `touch = 0` off. Rules unit-tested.
  **The pointer works the game's menus through their own GUI events** (2026-11-05, research/57 section 6, `[Pointer]
  gameCursor`): LE1's Flash screens put their mouse where `GetCursorPos` says (the client area scaled to the stage) and read it
  only on mouse input, posted messages never moved it and the Windows cursor can't leave the monitor (the game's window is
  taller), so until then the pointer's clicks landed wherever the game's cursor was. The host hooks `GetCursorPos` at attach and
  answers the pointer's point while it's on a game menu (not the galaxy map), and queues GUI events (`core/vr/GuiEvents`); the mod
  takes them (HostServices v30 `takeGuiEvents`) and sends them through `SFXGameModeBase.GuiKey` on the active mode (6 motion,
  50/52 the left button, 28/29 the wheel; `game/GuiCursor`): exact and at once, no real input, no window focus (tabs, the squad tab
  below the monitor's edge, rows, Equip and a swipe's scroll, checked in the game). Without a mod that takes them the host falls back
  to `SendInput` (a one-count nudge per move, buttons after it, dropped without focus; some clicks then landed a tap late).
  `mevr guimouse` prints the movie's mouse, `mevr guikey <event|w> ...` sends events. The wrist tap isn't taken while the fingertip
  goes for the wrist screen or the rail (`omni_rail::GoingForPanel`: the wrist sits in front of their lower part). A tap clicks where the finger touched (pinned through the tap); the hand a wrist screen is projected
  from neither touches nor points at it (`NoteGameUiQuad`'s `projectingHand`), nor at the omni-tool's rail.
- LE1 galaxy map in the CIC ([research/51](research/51-galaxy-map.md) section 8, HostServices v26; `game/GalaxyMap`,
  `core/vr/GalaxyHologram`, host `plugin/GalaxyHologram` + `GalaxyHologramDraw`; `[Galaxy] hologram`, `inWorld`, `world*`): on foot
  near the console our hologram of the galaxy (from the 2DA tables) stands over the projector (the game's own projector hologram
  hidden); a trigger pick opens the map behind the scenes (`OnArtPlaceableUsed`; the sequence's 2.5 s `BioSeqAct_Delay_0` set to
  0.05 s), the view is held at Shepard's (`Camera.UpdateCamera` post-handler; Halcyon seams `MevrBridge::GalaxyHologram`/`GalaxyHeld`:
  mode 9 and the opening/closing mode 6 shown as on foot), the map's Flash movie hidden, and the pick made through the map's
  cross-hair (`Actor.SetLocation`) and a posted click. The game's travel question (its own message box) shows on the UI panel over
  the table, clicked by the game pointer. Walking away closes the map. **The cluster view's nebula, axes and rings are the map
  level's 3D scene, not Flash** (`BIOA_GalaxyMap_T`'s art; section 7's "all Flash" was measured at the galaxy level). **With the map open,
  the game's own map is a stereo window over the table** (section 8.1; `[Galaxy] window`, `windowWidthM`, `windowDepth`,
  `windowRound`: a disc like the projector's hologram, by default; `windowOnHologram`: lying where that hologram is, at its 29-degree
  tilt and 6 m size, measured live from `InterpActor_0`, by default; `windowBackdrop` 0.8: the map's space covers the room that
  much, as the full screen's starfield did, 0 see-through): two scene captures from the map camera (`game/SceneCaptureRig`,
  CineScreen's parallel cameras and crop), its Flash layer on the UI panel at the same place, the view held at Shepard's at render
  time only (`[MEVR]` seam `MevrBridge::GalaxyViewSwapBegin/End` around the main view's `CalcSceneView`). Tools: `mevr galaxy dump|where|pick|click|
  press|trace|input|movie|invoke|var|aim`, `mevr props`/`setprop`, `tools/make-galaxy-pick-recording.py` (`--world`, `--snapshot`,
  `--panel-uv`), offline look `mevr-galaxy-preview`. Logs `galaxy hologram:`; snapshot `"galaxyHologram"` (objects' places), layers
  `galaxy-left`/`-right`.
- LE1 game screens in the room ([research/53](research/53-world-screens.md), HostServices v27 `setWorldUi`; `core/vr/WorldUi`,
  `game/WorldUi`, host `plugin/WorldUi`; `[WorldUi]` in `ASI\LE1MEVR.ini`): research/53 audits all 35 of the game's screens (its GUI
  library, `BioSFManager.HandlerLibrary`). **Every flat screen goes in the room** (the user, 2026-11-04; `screens = all`; the front
  end stays flat). In game modes 7 and 10 (the bypass minigame) the mod reads the GUI manager's panels (`m_oCachedScaleFormManager`,
  `m_lstPanels`, each `nmTag` a library tag, only `IsVisible` ones); the first one of ours places the screen; any visible one not in
  `screens` leaves the stack on Halcyon's panel. **The equipment, XMods and squad screens' squad member** lives in the game's **UI
  world**, which the game renders and ticks only while a screen is full screen (research/57 section 7, `game/UiWorldFigure`, host
  `plugin/UiWorldFigure`, `[WorldUi] figure`, `figureOpacity`): the mod ticks it itself (its native Tick, vtable slot 104 =
  `0xB3B810`: the command queue the screen's `SpawnPawn`/`TriggerEvent` fill, then `UWorld::Tick` of it) and points a scene capture
  of ours with its `CameraActor_3`; the host skips that capture's Umbra camera query (a hook on `0x7526C0`) and gives it the UI
  world's FScene in the eye renderer's first field for its call (the capture loop's hook in `RenderLevers`), and the world UI panel
  draws its picture under the game's UI over the 16:9 stage. Shepard and Ashley switch with the screen's tabs (checked in the game).
  A `bFullScreen` panel makes the game render that UI world (a blurred
  backdrop stage, its own camera) instead of the level: cleared on our screens while they stand in the room (journalled, put back
  if ours goes down). Shepard's view on foot is held at render time through the galaxy map's `CalcSceneView` seam; one `[MEVR]` seam
  in xr_session.cpp sets Halcyon's `liveWorldGui` while `MevrBridge::WorldUiActive()`. The game's UI goes to the HUD panel, shown as
  the 16:9 stage's part (`DefaultCrop`; the shop's x 0.14..0.86), placed once per opening `distanceM` (0.95) ahead toward what it
  belongs to (the shop: its vendor, the pawn nearest `BioSFHandler_Shop.m_shop`; loot: the container or body its inventory belongs
  to; the minigame: its handler's `m_oDependent`; a screen a use opens: what A would have used) at counter height (`belowM` 0.50), `widthM` 1.20,
  its dark parts at `backdrop` 0.80; the game pointer maps the whole window on its plane (`WholeWindow`). Mode 10 also needs the
  `[MEVR]` seam in render_hook.cpp's `SfrModeAllowsReplay`. Tools: `mevr panel`, `mevr gui <Equipment|Squad|Codex|Journal|Map|Menu>`,
  `mevr classes <text>`, `mevr instances <Class> [text]`, `mevr loot <n>`, `mevr lootcorpse <n>`. Eden Prime's real loot and minigame
  cases: research/53 section 5. Logs `world ui:`; snapshot `"mod"."worldUi"`.
  **The bypass minigame is projected from the left wrist** (research/53 section 6, the omni-tool; Fallout 4 VR's Pip-Boy and
  Half-Life: Alyx's multitool holograms the pattern; `[WorldUi] wrist` (since research/57 every personal screen too), `wristWidthM`
  0.30, `wristAboveM` 0.06, `wristTowardM`, `wristFollow`, `wristBeam`; the menu's Comfort > Game screens > Omni-tool screens): the host places it each frame from
  the HUD's left wrist point (`WristPlace`: facing the eyes, its bottom edge 10 cm above the wrist across the line of sight), its
  frame only (`WristCrop`), unfolding over 0.18 s, a faint fan of light from the wrist (plugin/WristBeam, a `beam` snapshot layer;
  premultiplied in linear light); in the room without a tracked left controller. The State grew (size-checked, v27 unchanged). No
  auto-hack (dropped, the user). **Shepard's forearm wears the game's own omni-tool** while it's up (research/53 section 6.1,
  `[WorldUi] omniTool`, `game/OmniTool`: `BIOG_V_T_Omni_Tool_Z`'s orange gauntlet and display through
  `BioVisualEffect.CreateCrustEffect`, its particle systems moved onto Shepard's mesh at the same bones, since the minigame's
  `bPlayersOnly` ticks only the player's actors); arm IK runs while a wrist screen is up (`view_anchor::Hold`, the on-foot frame);
  the screen and its now orange fan project from the display's rendered place (`State::emitter`, grew again), `wristAboveM` 0.10.
  Half-Life: Alyx's way is the model for working it by hand. `mevr skillgame start [type] [difficulty] [n]` opens the minigame on any placeable without a save
  (the level's `BioSkillGame.InitializeSkillGame`); `mevr panel SkillGame invoke PreGameButtonBOnPress` cancels it. **The puzzle is played with a touch d-pad**
  (research/53 section 6.3; the user, 2026-11-05: "We may need a large d-pad asset"): the game's own puzzle (`GUI_SF_SkillGame.PCMiniGame`:
  a marker from outside three turning rings to their centre; up/A/X a ring in, down/B out, left/right turn it while held; red blocks
  send it back to the start, orange ones are walls) takes the game's d-pad GUI events (`CONTROL_UP` 11 ... and their releases 30-33
  through `SFXGameModeBase.GuiKey`), and any mouse motion on it turns the marker without end (a ray anywhere on the window spun it
  2.9 rad/s). So while the handler's `m_bGameActive` is set (the manual override; `world_ui::State::puzzle`, a former pad byte, same
  size) the host keeps the game pointer off every quad of the game's and doesn't answer `GetCursorPos` until a second after the last
  frame of play (`game_pointer::HoldOffForPuzzle`; a hitch once brought Halcyon's flat panel back for a frame), the mod sends it only
  pad events (`game/GuiCursor`), and a 0.17 m d-pad stands to the right of the screen (host `plugin/OmniPad`, rules `core/vr/OmniPad`,
  `assets/menu/omnipad.rml`/`.rcss`, the puzzle's own colours): the right fingertip or a ray and trigger holds an arm (the 45-degree
  sector around the centre), sliding to the next arm changes direction; `[OmniTool] pad`, `padSizeM`. Checked on the fake headset:
  replayed fingertip presses turned the marker (right held 3.3 s: pi/2 to -3.06 rad) and moved it a ring in. Logs `omni-tool pad:`, `world ui: the bypass puzzle started`;
  snapshot `"omniPad"`, layer `omnipad`; offline `mevr-menu-preview`'s `pad-*.png`.
  The skill `game-screen-to-hologram` is the recipe for the next screens.
- LE1 omni-tool menu ([research/57](research/57-omni-tool-menu.md), 2026-11-04, the user: "the entire menu system available via
  omni-tool", "a menu on the left side to allow selecting all of the different menus"): **every personal screen is projected from
  the left wrist by default** (`[WorldUi] wrist` lists SkillGame, Inventory, XMods, CharacterRecord, Specialization, Journal, Codex,
  Map, MenuBrowser, Options, Save, Load; an old file's `SkillGame` alone moves once, marker `wristMenuWidthM`; a screen a use opened,
  a locker's equipment, still stands by what was used: `world_ui::OnWrist`; `wristMenuWidthM` 0.70 for a whole stage (0.55 until
  2026-11-05, the user wanted them larger; an old file's 0.55 moves once, marker `figure`; the menu's Wrist screen size slider),
  `wristWidthM` 0.30 the minigame's frame; the menu's row is Comfort > Game screens > Omni-tool screens). **A rail of the game's screens stands to its
  left** (`core/vr/OmniRail`, host `plugin/OmniToolRail`, `assets/menu/omnitool.rml`/`.rcss`, `[OmniTool]` enabled/tap/aloneHeightM):
  Squad, Equipment, Map, Journal, Codex, Computer, Close; a second RmlUi context of the main menu's library (`menu::Ui` allows one
  per document now), `vr::game_pointer::Pointer` on its quad (ray and trigger, or the fingertip), its trigger kept from the game
  (`omni_rail::OwnsTriggers`). **Tap the left wrist with the right index fingertip** to open it alone on the wrist (4 cm from
  8 cm, not with a gun drawn; `omni_rail::Tap`), **at a target button on Shepard's drawn left forearm, measured from his drawn
  right index fingertip** (2026-11-05: the first headset try found nothing to aim at; the user: "I want it to be the wrist
  target"): the mod sends the arm bones each tick in `hud_pieces::Pieces` (size-checked: host and mod deploy together), the
  target 30% from the wrist to the elbow lifted 3.5 cm toward the eyes (`omni_rail::BodyTarget`/`BodyFingertip`); the
  controllers when the arms aren't drawn first person (`[OmniTool] tapOnBody = 0`: always); faint orange with the wrist raised,
  brighter with the right hand near, cyan and a tick in tap range (`omni_rail::TargetLookFor`; log `omni-tool rail: a reach for
  the wrist target`, each try's closest approach; snapshot `"omniRail"."target"`); it shows by itself beside a screen of its own; a tap or Close shuts it and the
  screens. Its entries go to the mod through the main menu's request slot (`main_menu::PutGameRequest`, `game_menu::Action` 3..8):
  `game/GameMenuRequest` backs out of the screen up with the player's B as the game hears it, **`SFXGameModeBase.GuiKey(13)` on the
  active mode** (it hands B to the GUI manager, which offers it to every handler on the stack and closes through
  `MassEffectGuiManager.HidePauseMenu`/`HideBrowserWheel`; B to the focused panel's handler alone never closed anything: research/57
  section 3), 4 ticks apart, at most 6, until the game leaves mode 7, then calls `SFXGameModeBase.Show<Screen>Gui(true)` on the mode
  that has the input (no mode overrides them; in mode 7 only the Flash wheel opens screens); Computer (the pause menu, which the game
  won't open over its screens) the same way. A screen taking over inside mode 7 is a new world-UI opening (placed, labelled, unfolded
  again). Wrist screens never look wider than 60 degrees (`world_ui::WristNearest`). **Checked on the fake headset 2026-11-04** (a
  replayed wrist tap and a fingertip press on the rail, `tools/make-wrist-recording.py`): all six entries and Close, one B each.
  [verify] with a person: the tap's distances, legibility. Logs `omni-tool:`, `omni-tool rail:`; snapshot layer `omnirail`; offline look `mevr-menu-preview`'s
  `rail-*.png`. **Replays with controller poses move the real cursor** when a screen falls back to the flat panel (the game
  pointer's `SetCursorPos`): `[Pointer] setCursor = 0` for such tests. `mevr omni <equipment|squad|map|journal|codex|computer|
  close|holster>` hands the mod the request an entry gives (no hand on the rail). **The tap shows the game's omni-tool too**
  (2026-11-04, the user): while the rail is shown the host sets `kOmniRailOpen` (4) in `HostServices::mainMenu`'s bits (no version
  change), and on foot with no screen up the mod's `world_ui` keeps `omni_tool::Tick` on (`[WorldUi] omniTool`); log `world ui: the
  omni-tool's rail is open on foot`. The rail alone is still placed from the controller's wrist, not the display [verify].
  **The rail is drawn with ME1's own art** (research/57 section 5; the user, 2026-11-04: "ME1's screens, exactly"; hand-drawn
  imitations were rejected twice): the mod (`game/GameArt`) copies six of the game's Scaleform movies' `RawData` to
  `%LOCALAPPDATA%\MEVR\gameart` once per level (the player's own data: **never commit or ship game art**); the host
  (`plugin/RailArt`, `core/gfx`: a GFx reader, a CPU rasterizer with morph shapes and nine-slices, DefineFont3 to TrueType)
  registers Biomass, Tigensu and Myriad with RmlUi ("ME1 Biomass"...) and makes `mevr:me1-tab*` (PC_Journal's `leftTab` states,
  the game's orange on hover) and `mevr:me1-panel` (PCInventory's frame #324, its title notch at native size); `art` on the
  document's body switches the RCSS to them, else the rail's own look. Offline: `mevr-menu-preview --gameart <folder>`,
  `mevr-gfx-dump`. A second `menu::Ui`'s Shutdown releases RmlUi's render managers before its renderer (RmlUi's rule).
- LE1 Mako ([research/44](research/44-mako.md), `game/Mako`; `[Mako]` in `ASI\LE1MEVR.ini`): game mode 1 with a
  `BioVehicleBase` pawn, the right controller's aim ray (or the pistol calibration's line) traced in the world (`Actor.Trace`,
  `aimRangeM` 200) is the aim point: shots through HandAim's `GetAdjustedAimFor` handler (direction from the start the
  `BioVehicleWheeled.GetWeaponStartTraceLocation` post-handler saw), the turret model through the game's own
  `BioVehicleBehaviorBase.SetDesiredTurretAimPoint` (ProcessEvent) at three seams (`turretSeam` bits: the mod's tick, a
  `BioVehicleWheeled.Tick` post-handler, a bone_pose pre-stage rewriting `BioSkelControl_TurretConstrained.DesiredBoneRotation`
  before UpdateSkelPose, journalled `BonePose.makoTurret`); the heartbeat's held/changed counts say which seam the game
  overrides [verify]. The hand reticle shows in mode 1 on a valid mod ray (host, `HalcyonOverlays`); `core/vr/Comfort`'s
  `VehicleMotion` (speed, yaw rate with the 65536 wrap) drives the vignette in the Mako (`vignetteWhen` applies). Halcyon's
  `makoHeadAim = 0` stays. Logs `mako:`; snapshot `"mod"."mako"`.
- LE1 Mako controls, stage 1 ([research/46](research/46-mako-controls.md), HostServices v23 `setMakoControls`; `core/vr/MakoControls`, `game/MakoControls`;
  `[Mako] controls`): a grabbed throttle lever (left, springs back to neutral when let go: `leverReturns = 1`, the user 2026-11-04; X jump, Y boost) and a stick (right, springs back, trigger machine gun, A cannon,
  thumbstick turret) at the seat, drawn as two flat quads; the values overwrite `Vehicle.Throttle`/`Steering` in a `BioVehicleBase.SetInputs` post-handler.
  **The stick tilts** (2026-11-03, research/47 "The stick tilts"): the hand's travel from the grab as a tilt; left/right steer; while the jump jets (`bJumpJetsOn`)
  fire, the tilt tips the Mako's rigid body (forward/back `thrusterPitchDeg` 20, left/right `thrusterRollDeg` 15, since 2026-11-04; the
  jets push along its up, so it drifts that way: a full tilt gains 15.5 m/s forward or 11-13 m/s sideways per burst, measured; the
  sideways tilt doesn't steer then, or the driven track turns the Mako 90 degrees; the booster is the game's own; each burst logged
  with the speed it gained, research/47 "Flying on the jets"); their fuel is partly given back while they fire
  (`thrusterBurnScale` 0.4: 2.5x as long; LE1's jets have 0.7 s); `stickTwist = 1` is stage 1's twist. The tilt steers up to `tiltSteerMax` (0.5), the twist adds to it and alone spins the Mako on the spot at
  zero throttle, and the cockpit's grip model turns with it about its tilted axis (`Output::twistShownDeg`, 2026-11-05) (`spinRateDeg`; the body's turn axes measured: research/47 "Which way the body turns"); the jump is only the lever's X. `mevr vehicle jets <s>`
  fires them for tests, `mevr vehicle angvel [body] X Y Z` measures the body's turn.
  **Reaching the Mako without a save** (research/47 "Reaching the Mako"): `tools/mako-start.ps1` (`open BIOA_UNC20`, then `mevr vehicle party`:
  the squad screen's own `FinalizeParty`, after which the level's rover drop makes the Mako the pawn, game mode 1). Never `mevr vehicle enter`
  before the drop. The controls haven't been grabbed in the game yet (the fake headset's controllers aren't tracked).
- LE1 cockpit, in progress ([research/47](research/47-cockpit-in-game.md); `game/CockpitProbe`, `mevr cockpit ...`, `mevr vehicle ...`, inert until called): `tools/cockpit`
  splits, decimates (`game_ready.py`: the packager prefers `concept/assets/game-ready/` made from the current models, `--raw` skips it) and packages our models
  (`concept/`, gitignored except `assets/game-ready/` and the packager's source models, in git on Git LFS since 2026-11-03: MIT, generated on the owner's paid Tripo plan, `NOTICE.md`; the whole procedure is the `tripo-game-ready` skill; `hull_assemble.py` builds the hull: a clean shell modelled to the messy
  Tripo template's shape and the side panes' frame, open framed windows, the roof on the panes' caps, swept profiles from
  `hull_shapes.py` and a baked texture of its own, the Tripo parts placed in it and made game-ready, packaged as four `Hull_*`
  meshes on the hull's origin, [research/48](research/48-hull-assembly.md)) with the Legendary Explorer nightly in `mod-manager/`; the package goes in `CookedPCConsole` with
  a `PCConsoleTOC.bin` entry (undo in `%LOCALAPPDATA%/MEVR/cockpit-install/UNDO.txt`). A scene capture nests inside the eye's render command: `RenderLevers` hooks `0x742C80` and
  `0x773D30` so it takes no eye index and its final post pass binds its own target. Set `bUpdateMatrices` false or `SetView` is overwritten by the actor's transform.
  `TextureRenderTarget2D.Create` takes only formats 2, 3 and 9. **Riding in the Mako** (`mevr cockpit ride`,
  research/47 "Riding in the Mako"): a `Camera.UpdateCamera` post-handler keeps the game's chase POV for the console's screen, puts the parts in the
  vehicle's full frame and moves `CameraCache.POV` to the cockpit's eye (minus the head's offset when the ride began: the seat), the Mako's
  components `bOwnerNoSee`. `tools/make-drive-recording.py` drives the Mako by replay on the fake headset. An enum value with a numeric suffix goes in a package as the name and
  the suffix plus one (`TEXTUREGROUP_Environment`, 1025), or the game reads past the enum's end (`LODGroup 27`: one mip resident, the model one colour). The parts are placed by writing `Location`/`Rotation` and flushing.
  **In the Mako by itself** (`[Cockpit]`, research/47 "The cockpit by itself"): entering runs `ASI\cockpit\cockpit.txt` (`tools/cockpit/place.py
  --script`), leaving parks it. **`[Game] pauseUnfocused = 0`** (`game/FocusPause`, the default since 2026-11-05) undoes ME1's freeze while the window has no focus
  (`bPlayersOnly`; it also refused the Mako's exit); the pad reaches the game only with focus, the cockpit's controls without.
  **Shepard in the seat** (`[Cockpit] seated`, research/47 "Shepard in the seat"): the script's `seat` line; the driver (hidden by the game
  at the vehicle's origin) is shown eyes-only (`bOnlyOwnerSee`, owned by the vehicle), its head's meshes hidden, lit by the vehicle's light
  environment (its own drew it black), its hips put on the H-point each camera update (slid back until the head bone is 0.10 m behind
  the eye); `arm_ik::DriveSeated` poses the legs (two-bone IK, `ik::FindLegChain`, a knee pole) and the hands (controllers; a held
  control's hand closed on the handle's axis, `seat ... axes` from `place.py`'s measure of the models, the fist's middle on the grip:
  research/47 "The hands on the handles"). With the cockpit up the controls grab at the models (`cockpit_probe::ControlSeat`) and
  stage 1's flat lever and dial are hidden.
  **The repair button and the extraction lever** (research/48 section 27; `tools/cockpit/panel_controls.py`, `game/CockpitPanel`,
  rules in `core/vr/CockpitPanel`, unit-tested): the script's `panel` line (place.py from the manifest's `panels`); a fingertip
  pushes the button on the right desk shelf, held 2 s it calls `SFXGameModeVehicle.RepairVehicle`; a grip pulls the overhead handle
  down (5 cm of the hand's travel along the cabin's down swing it its 10 degrees, as its arrows point; it took a push up until
  2026-11-05), held at the end it calls `LeaveWorld` (the game's own functions on the game thread, never the pad); the ring's nine lamps
  show the hull's health (`Light_*_MIC` swapped onto `Repair_Base`'s elements). While a hand works them (or the right hand is at the
  handle) the gunner keeps the turret, at once (`mako_gunner::Hands::busy`; the user: "the aim should remain in auto mode").
  Pressed while the repair can't run (the vehicle's `VehicleRepair` power: `IsRequiredResourceAvailable`, `m_fCooldownRemaining`)
  the plate's squares are solid red with no omni-gel and flash red every 1.2 s while it cools down; no hold, no call. **The
  repair is also the left thumbstick's click while the left hand holds the throttle lever** (the user, 2026-11-05: the button
  was too far away), cockpit or not, at once (`cp::ThrottleClick`; a refusal shows on the squares 2.4 s, two buzzes; L3 is
  unbound in the vehicle mode); `mevr cockpit panel throttle`. Shepard's
  hand closes on the held handle's bar as it swings (`CurrentSeat`'s held control) and points its index at the cap it presses.
  `mevr cockpit panel [repair|extract|ask]`; log `cockpit panel:`; snapshot `"cockpitProbe"."panel"`.
  **Hazard lights** (research/47 "Hazard lights", 2026-11-04; `[Cockpit] hazard`, `hazardBelow` 0.30, `hazardPeriodS`, `hazardWash`):
  with the hull's health (the vehicle behavior's `GetHealthAndShieldsScaled`) low, every cockpit element showing a package instance
  (not glass or screens) gets ours over `ProcBuildingSimpleMaterial` with a red render target as `LightTexture` and a beacon's
  `LightmapBrightness` each tick (rules `core/vr/CockpitHazard`, unit-tested); own materials back when repaired. `mevr cockpit hazard
  [on|off|auto|health <f>]`; log `cockpit hazard:`.
  **The cannon over the cabin** (research/47, 2026-11-04; `[Cockpit] showTurret`): the cockpit's eye sits under the Mako's turret
  (`place.py`'s ride `0 1.0 0.25`; at 0.8 it was inside it), the turret's five meshes (hung on `Turret_Mount_*`) stay in view while
  riding, and the shots start at the firing gun's `GetMuzzleLoc` (`Mako.cpp`'s start-trace handler; the game's start was the camera's,
  in front of the face). `mevr vehicle guns` and `fire [cannon|mg] [s]` measure it.
  **The gunner** (research/47 "The gunner", 2026-11-04; `core/vr/MakoGunner`, `game/MakoGunner`; `[Mako] gunner*`): riding, a
  squadmate aims the turret at the nearest visible hostile (the driver's `IsHostile`, `FastTrace` from the muzzle, dormant pawns
  skipped) and fires the machine gun in bursts and the cannon at heavy targets or crowds (`gunnerCrowd` 3 within
  `gunnerCrowdRadiusM` 6); the stick's thumbstick or the free right hand aiming (finger on the trigger, ray out the front) takes it
  at once, back after `gunnerHandbackS` 3. `mevr gunner [wake <n>]`; log `mako gunner:`; snapshot `"mod"."makoGunner"`.
- LE1 life-size stereo ([research/30](research/30-metric-stereo.md), `MELEVR.ini sfr2Metric`, on in our deploy):
  each Stereo 2 pass renders from its eye's real place (`xrLocateViews`, at the view's units per metre: Halcyon's
  50 x lean gain, life-size at 100), no convergence shift, swap ignored (pass 0 = left). In gameplay with the HUD
  panel taking the UI (mode 2) the GPU matrix gets each eye's own off-centre frustum and the submit declares it;
  the CPU matrices (culling, Umbra, HUD markers) keep Halcyon's symmetric window. Where UI is in the eye images
  (VR cine, panel off) it stays on the symmetric window (mode 1). Upstream's swap-on convergence was divergent
  (1.27 deg per eye: far things beyond infinity, a pistol at 0.5 m read at 0.68 m). Mode 2 misplaces what the CPU
  projects through the symmetric window (the sun's sprite and glare: in one eye only near the edges, research/37
  2026-10-25 18:22), the same with two Draws as with one. Plan and logs
  `plugin/MetricStereo` (`[MEVR-STEREO]`), math `core/vr/MetricStereo` (unit-tested), render seams `render_hook`
  `SetSfrMetric` / records by head-look arm; snapshot `"stereo"`; `sfr2MetricDepth` ("Extra depth"),
  `sfr2MetricEyeFrustum = 0` for mode 1 only; `tools/stereo-check.py` measures far-scenery disparity.
- LE1 colour per headset ([research/27](research/27-colour.md)): brightness and RGB gains, contrast,
  saturation, mid-tones (gamma) and black level, all on sRGB-ENCODED values, plus a ±0.5-step triangular
  dither (`core/vr/Colour`, unit-tested; `tools/colour-preview` checks the shader against the CPU reference:
  ctest `mevr-colour-shader`). It goes on the world eye images (the end of Halcyon's eye blit shader:
  `[MEVR] colour` seams, `MevrBridge::EyeColour*`, variants compiled from its source with our prelude) and
  the game's UI (HUD and wheel panels), not on our own quads or Halcyon's menu. Neutral is today's path
  exactly (Halcyon's own bytecode, its raw copy); when on, a same-size eye takes the blit instead of the
  raw copy. The headset (`xrGetInstanceProperties` runtime, `xrGetSystemProperties` system; the user's PS
  VR2 is `SteamVR/OpenXR : playstation_vr2`) picks the profile Auto means: "quest" for streamed Quests,
  "neutral" otherwise. `[Colour]` / `[Colour.<headset>]` in `ASI\LE1MEVR.ini` (`plugin/Colour`): `profile =
  auto|neutral|quest|vivid|custom`, the custom values per headset, `compare`, `dither`; our menu's Display
  page (and the VR hands tab's Colour section) edits them live; holding A there is `colour::HoldCompare()`. Log
  `[MEVR-COLOUR]` / `colour:` (the GPU cost every 10 s); snapshots show the corrected picture (`"colour"`).
  **HDR tonemap** (research/27 section 7b, 2026-10-23): with the game in its HDR range
  (`tools/vr-game-settings.ps1 -Hdr`: `[HDR] DesiredRange = DynamicRange_HDR10_Float`, a float scRGB backbuffer;
  `-Sdr` back), Halcyon's blit maps the scene's paper white to 1.0 and `[Colour] tonemap = aces | hable |
  reinhard` (`exposure` stops, `white`) takes the place of its per-pixel clip (`MEVR_TONE` in `ToEye`, the
  `[MEVR]` seam; `clip` = Halcyon's own). An 8-bit source never gets a curve. No headset we have takes HDR
  output (the PS VR2's PC adapter has none): the gain is the gradation above paper white, not brightness. The game
  makes the float backbuffer only where the display answers HDR-capable (`IDXGIOutput6::GetDesc1`'s colour space;
  windowed, an HDR range falls back to SDR otherwise), which is Windows HDR on for the monitor the game window sits
  on; **`[Colour] hdrDisplay = 1`** (research/27 section 7c, 2026-10-25) answers HDR10 for an SDR display instead (a
  `[MEVR]` seam on the output vtable Halcyon's `d3d_capture` already patches, slot 27, `MevrBridge::HdrDisplayDesc`;
  log `hdr display:`), so the float backbuffer comes with Windows HDR off: the same frame as with the Windows switch,
  nothing measurable on the GPU, one Draw healthy on it, the flat window on the monitor a normal picture. ACES gives a
  warmer, deeper, unclipped frame against the game's SDR tonemap. `hdrDisplay = 0` (the default) passes the display's
  own answer through. **The curve is applied hue-preserving** (research/27 section 7d, 2026-10-26): on the pixel's
  largest channel with the pixel scaled by the ratio (`[Colour] toneHue`, 1; 0 per channel as before), a path to white
  for pixels far above the curve (`toneDesat`, 0.15: the sun white, not pink), and the scene's chroma scaled before
  the curve (`toneSat`, 0.7: the game's own SDR pass's saturation at Eden Prime, measured; 1 the scene's), because the
  per-channel ACES fit turned the frame's oranges red at saturation 0.77 and the float frame is more saturated than
  the game's SDR pass leaves it. **Verdict at the same pose (research/27 section 7d): the game's own SDR pass keeps
  more gradation at both ends** (the sky at the sun 50,000 distinct colours against 19,000, the darkest patch 5,900
  against 400 with 9% black) and carries the artists' grade; the float path is not an improvement over it until it
  carries the game's own toe and grade. `hdrDisplay = 0` is the game's SDR pass.
- LE1 eye upscale ([research/32](research/32-ue3-vr-performance.md), `[Upscale]` in `ASI\LE1MEVR.ini`, `method = fsr` by default
  since 2026-11-05 with the render target at 0.85 of the eye; before that off by
  default): with `method = fsr` and the stereo target below the headset's eye size (`tools/vr-game-settings.ps1
  -Scale 0.75`, or the main menu's Display > Headset × 0.75 / × 0.85 and Upscale > FSR), Halcyon's eye blit scales each 8-bit eye up with FSR 1 (EASU into an intermediate, RCAS into the
  eye, the colour correction at the end; `sharpness` 0..1) instead of its bilinear fill: seam `[MEVR] upscale` in
  `BlitTextureToEyeWithFill`, `MevrBridge::EyeUpscale` in `plugin/Upscale`, the math and shader text in
  `core/vr/Upscale` (unit-tested), AMD's MIT headers in `external/fsr1` embedded at build time. Offline look and
  check: `mevr-upscale-preview` (PNGs; `--check` is ctest `mevr-upscale-shader`; `--bench` times an eye). Log
  `upscale:`; the probe JSON's `"upscale"`.
- LE1 gun sights ([research/21](research/21-sights-and-scopes.md), HostServices v13): a collimated red
  dot / holo per gun (a quad 50 m out along the sight line, `eyeVisibility` = the eye looking through the
  window) and a 4x picture-in-picture scope on the sniper rifle (a disc at the eyepiece for the aiming
  eye, magnifying the OTHER eye's image, copied as Halcyon releases it: `testloop::SetReleaseTap`). Since
  2026-10-25 a **virtual eyepiece** (research/21 section 13): with the eye up to `[Sights] scopeMaxM` (0.70 m)
  behind it the picture looks `scopeFieldDeg` (22) across, the zoom true across it, a dark rim, a shadow crescent
  at `scopeEyeBoxM` (0.05 m) off the line (`vr::sights::VirtualView`; `scopeFieldDeg = 0`: the old 2 cm lens; log
  `sights: the scope's picture was up N frames: ...`; the Sight struct grew: host and mod deploy together). Since
  2026-10-27 **scopes are pulled to the eye** (research/21 section 14, mod only): raised, a scoped gun slides back along its
  line (the assist's weights and spring, at most `[Sights] scopePullMaxM` 0.15) until the model's eyepiece is
  `scopeReliefM` (0.12 m; never inside the near plane, `LocalPlayer.NearClipPlane` 10 units, + 2 cm) from the aiming eye,
  and the picture is that eyepiece seen from there (`<type>Lens`, gun units); `<type>Hide` bones (sniper `Scope_Lens`, its
  glow [verify]) are hidden while the picture is up (`game/scope_parts`: HideBone through ProcessEvent, shown again on a
  switch, holster or unload; the gun's materials logged once per model). Since 2026-10-27 (later) **a scope's picture
  only while aiming down the sight** (research/21 section 15, mod only; `core/vr/Ads`, Overlord's ADS, GPL-3.0): Overlord's
  corridor (the centre eye 0.15-1.5 m behind the muzzle, 9 cm beside the bore, -2.5/+16 cm, 30 degrees; exit 0.10-1.7,
  13 cm, -6/+22, 40), 100 ms dwell each way, plus our reach (the eye within `[Sights] adsReachM` 0.40 m of the sight, 0.50
  to leave), all on the gun WITHOUT the assist's move; below ADS the mod publishes no scope (kind None: no picture, the
  model's own lens, no scope capture), `scopeMaxM` only with `ads = 0`. The assist's 10-degree cone is gone: Overlord's
  continuous approach weight (angle 5-45 degrees, smooth corridor edges) x distance weight drives the sideways move (dots
  and holos at most `assistDotMaxM` 0.03) and the pull (dot none, holo 5 cm, scope under 3x 10 cm, scopes
  `scopePullMaxM`; under ADS `scopeAdsPullMaxM` 0.35 with Overlord's thermal spring, 24/s, 0.9 m/s, so the eye reaches the
  relief). Logs `sights: ADS on/off (...)`; snapshot `sights.ads`. The assault rifle is a red dot again (`scope 1.5`, the
  day-old migrated value, back to `dot` once; marker `ads`). The
  mod places them (`game/sights`, `[Sights]` in `ASI\LE1MEVR.ini`); the host draws them
  (`src/plugin/SightLayers.cpp`, pictures and shader in `SightPictures.cpp`; offline look:
  `mevr-sight-preview`). Scoped shots get the zoomed aim error via a `GetPlayerAimError` post-handler
  (`bIsZoomed` set for one native call only: never set it where the camera can see it, research/18).
  Alignment assist (research/21 section 8): raised to the face, the gun slides so the sight line runs
  through the aiming eye (`vr::sights::AssistTarget`/`AssistSpring`, `[Sights] assist*`, arm IK's
  `Request::gunShift`, measured without its own shift); hands within `[Hands] reachNearFaceM` of the eyes
  aren't reach-scaled by the arm-length calibration. The dot shows to one eye, the nearest within `[Sights] eyeBox`
  of the window. Shots converge on what the sight line hits (`Actor.Trace` each tick, `[Sights] converge`, log
  `hand aim: shot N:`). The sight sits on the model: a bone named for a sight (`vr::sights::SightBone`; a scope's
  eyepiece `Scope_Eye` before its front lens), followed live in the world every tick (`weapon::BoneWorld`: ME1's guns
  unfold for a second after a draw, and the first tick's model isn't aimed yet), with its side offset; else the
  height from the bones' top (`vr::sights::ModelTop`; the mesh bounds are a loose 15-22-unit box), re-read every
  0.25 s. Log `sights: the <gun>'s sight sits on its '<bone>' bone: ...`. Long guns without a rifle calibration borrow the pistol's grip
  frame. **Our quads are metric; Halcyon's Stereo 2 isn't** (research/21 section 9: eyes rendered `sfr2HalfEyeUU`
  from the midpoint, each image moved 1.27 degrees outward at submit by `sfr2Convergence` with swap-eyes): the fix is
  a metric stereo mode in Halcyon (a separate job), not per-eye corrections of our quads. **The game's own optics** (research/21
  section 16, 2026-11-03): the player's guns are 8 meshes (a and b per type); the rifles' measured eyepieces and housings
  (`vr::sights::OpticFor`: SNPa's glass is 13.5 units behind `Scope_Eye`, behind the grip), `<type>Lens`/`<type>Hide = auto`;
  the held weapon's magnifications from its `Stats.AimModes` (`sniperRifle = scope auto`: 3.5x, 7.0x; a right-stick flick under
  ADS steps them, `zoomStick`); `reticle = game`: the game's sniper view (the mod publishes `GUI_SF_HUD.Reticule_I4E/I4C/I41`'s
  resources in the sight; the host composes two layers once, `SightLayers` PrepareArt, layout `vr::scope_art` from
  `tools/scope-art.py`; the duplex if not found). **It can't load** (the game frees those textures, research/21 section 16),
  so **`reticle = duplex` is the default** since 2026-11-04 (the owner; an old file's `game` is kept and falls back to it).
  The sight struct grew: host and mod deploy together. **A red dot on the game's solid housing is seen through** (research/21
  section 17, 2026-11-05; the assault rifles' boxes, `[Sights] dotWindow`): the mod sets `kWindow` (a `flags` bit, no size change)
  and the host draws, for the dot's eye and behind its ring and dot, the other eye's image at 1x on a disc at the window
  (`sv::WindowView`, the scope shader's window style, the eye copy; not the capture: its grade would show). Dots no longer slide
  toward the eye (`assistDotMaxM` 0; 0.03 migrated once, marker `dotWindow`: the slide toggled at the corridor's edge while the
  eye already sat on the line), and the dot's eye survives a 120 ms dropout (`sv::EyeHold`). Log `[MEVR-SIGHT] first window`,
  `sights: the dot's window was up`; snapshot `sights.window`.
- LE1 scope capture ([research/39](research/39-scope-capture.md), HostServices v20; **on by default since 2026-11-04**,
  `[Sights] scopeCapture = 1`, an old file's 0 moved once (marker `scopeCapturePost`); measured in research/39 section 12: the
  render thread +0.5 ms in a light scene, no GPU cost visible, the picture as sharp as the display instead of 2.2x below it;
  **known issue, kept for now (the user):** the player's post chain adds a black speckle in dark areas, `scopeCapturePost = 0`
  removes it and the grade; README's known issues): the scope's picture from a real zoomed render instead of the other
  eye's image enlarged. The mod (`game/ScopeCapture`, hot-reloaded) spawns a `SceneCapture2DActor` (Actor.Spawn on the
  player controller; the class's `bStatic`/`bNoDelete` defaults cleared for the call if set) with a
  `TextureRenderTarget2D` of its own (`Create` on its default object; `scopeCaptureSize` 1024, FloatRGB), the update
  skips off, the player's post-process chain, `FrameRate` `scopeCaptureRate` (150: once a frame under Halcyon's two Draws,
  whichever clock the engine compares); enabled only while an eye is behind a scope (the host's `sightView` bits, held 0.5
  s), pointed along the sight line each tick (`SetView` then `ForceUpdateComponents`, the field `2 atan(1.25 tan(11
  deg) / zoom)`, the near plane 20 units past the muzzle), destroyed at shutdown, forgotten on a level change. It renders
  through `FSceneRenderer::Render` before the eye render, into its own target, so RenderLevers runs it with index -1 (a
  "capture" in the levers line). The host (`plugin/ScopeCaptureSource`) finds the render target's ID3D11Texture2D behind
  `UTexture.Resource` by a guarded search (vtable in d3d11.dll, QueryInterface, the size; the target's own sits behind an
  unaligned pointer, `+0xA0 -> +0x8`, found by the walk for objects of a texture's class, `NoteDevice`), keeps a
  reference, and SightLayers samples it with the pose the presented Draw rendered (`[MEVR]` seams `NoteDrawArm` at the
  pair-arm latch in `render_hook.cpp` and `OverlayFrame::pairArm` in `xr_session.cpp`), else the eye copy as before; the
  reticle stays collimated. Logs `scope capture:`, `scope capture source:`; snapshots `"mod"."scopeCapture"`,
  `"sights"."pictureSource"`/`"capture"`; offline `mevr-sight-preview`'s `scope-capture-*.png`.
- LE1 squad weapons ([docs/squad-weapons-design.md](docs/squad-weapons-design.md), research/19
  sections 9-11): **pull the left trigger** (`[Controls] leftTrigger = lb`): the wheel opens at the left
  hand at once; pointing the left hand at a squadmate preselects them; letting go of the trigger gives
  the highlighted entry. The squad takes the trigger per pull on foot and in the wheels
  (`squad_menu::OwnTrigger`: the game sees no LB then; the Mako keeps LB). **The left grip never opens
  it** (since 2026-10-22): it's only the support grip (grab zone: a capsule along the gun,
  `ik::MakeSupportZone`, `[Controls] supportZone`; the grab's line captured, `ik::CapturedSpan`) and
  grenades. `[Squad] style = native` (default, HostServices v12): the GAME's weapon wheel
  (`SFXGameModeDefault.EnterWeaponWheel` through ProcessEvent; it pauses the game itself), shown at
  the left hand; the right stick steers it (`gamepad::kFlagNativeWheel`: the stick reaches the game
  once centred), letting go sends A for the highlighted entry and then `ExitWeaponWheel`; pointing
  preselects through the HUD's kept stick angle (`BioSFHandler_HUD.nLastAngle`). A wheel nobody holds is taken over
  (`wheel::Step::adopted`): one that opens after the 0.6 s timeout (within 10 s), or any weapon wheel up at a new pull,
  so letting go closes it. Shepard's weapon is put back after a holstered wheel only when the game drew it (not while
  holsters refuse the game's draws, nor after a holster draw since). `style = custom`:
  our drawn menu (left stick = squadmate, right stick flick = weapon, let go or A = give it, B =
  cancel; `plugin/SquadPanel` draws it with its own ImGui context as the "squad" quad;
  `gamepad::kFlagSquadMenu`, v11). `game/SquadWeapons` (mod) drives both; `core/vr/SquadMenu` is the
  shared, unit-tested part (the native ring's tables in `squad_menu::wheel`). Settings: `[Squad]` in
  `ASI\LE1MEVR.ini`.
- LE1 squad context ([research/54](research/54-voice-squad-commands.md), the voice and gesture squad orders' world; `core/vr/SquadContext`, unit-tested; `game/SquadContext`; `[SquadContext]` in `ASI\LE1MEVR.ini`): on foot the mod reads the scene every 0.25 s (the party with powers and cooldowns from `m_powers`, the living hostiles from the pawn list with a type word, the gun's sight-line hit and the gaze selection) and keeps a 4 s history of the head's and the hands' rays; the hostiles are clustered into groups whose E-ids stay through an encounter, each with egocentric words (left/ahead/right, near/mid/far, above/level/below, nearest, leftmost, biggest), "those" is resolved at the word's time from the history, and every order (a gesture, a spoken "name, power", a model's line `G|SR|-|attack|PT1`) becomes one `Action` validated against the scene. **Landmarks** (research/54 "Landmarks", 2026-11-05 evening): the level's own cover (`Engine.CoverLink` slots within `[SquadContext] landmarkRangeM` 25 m, read every 2 s) are `L<id>` lines with the groups' words; "get behind that crate", "take cover behind the wall on the left", "the nearest cover" are a hold at the slot on the cover's far side from the nearest group (`LandmarkPoint`; the rays score cover beside groups, so a look or a point picks one; an attack on cover is refused); every piece is "cover" until the mesh words are learned [verify]; `tests/LandmarkTests.cpp`. **The flat orders by gesture** (`core/vr/SquadGesture`, unit-tested; `game/SquadOrders`; `[SquadOrders]`): a LEFT grip squeeze that no other feature claimed (away from the gun's support zone, the grenade slot and a hot receiver) points an order with the left aim ray; letting go on an enemy group = attack, on the ground past `followWithinM` = move there, near Shepard, on a squadmate or on nothing = follow; the squadmate pointed at when the squeeze began gets it alone. The one dispatcher turns any validated `Action` into `BioPlayerController.QuickCommandAttackTarget(iSquadMember, pTarget)` / a move = the HOLD formation at the point plus the squadmate's AI `PushMoveToPoint` (since 2026-11-05 evening, the placement's way: `QuickCommandMoveTo` ignores the point in VR, 30 m along the head's facing whatever the controller's `Rotation` says; `holdFormation = 0` keeps the old call), held until it's calm, relieved or "stay there" / `QuickCommandFollowPlayer(iSquadMember)`; powers through `SquadOrderUsePower` + `ApplyTacticalOrders` (or the AI's `AddUsePower`, `powerRoute`). `mevr order ALL|-|-|attack|E1` gives a line through it. **The rule parser** (`core/vr/SquadParser`, unit-tested): a transcript becomes one `Action` per clause in a millisecond (fuzzy present names, the roster's power words, everyday order verbs, deictics as `PT n` with word positions, descriptions against the groups' words, AIM/GAZE/OTHER/LAST, corrections, cancels) with a confidence and the unresolved words a model would take over; `mevr say <words>` runs it against the live scene and dispatches (the typed stand-in for speech; below `[SquadOrders] minConfidence` it refuses and says what was unresolved). **Standing orders** (`core/vr/SquadStanding`, unit-tested; `[SquadOrders] standing`): a hold or an attack is kept per squadmate and re-asserted through the game's orders (back to the point when they drift past `holdRadiusM`, the group again every `reassertS`), ends when it's calm (`calmS` after the last hostile near the point and the last combat; then they follow unless "stay there"), when the group is gone, or "until I say so", and is given up with a follow when hurt (`retreatHealth`) or overwhelmed (`overwhelmCount` within 8 m) unless "no matter what"; follow/regroup and "belay that" drop them. A hold is the game's own HOLD formation (`SetMemberHoldPosition` + `SetSquadFollowFormation(HOLD, force, member)`, read back with `GetMemberFormation`; `holdFormation = 0` keeps the re-asserted move), released by the follow order. A retreat (hurt or overwhelmed) is a hold at cover between them and the threat (`core/vr/SquadRetreat`: a point `retreatM` back toward Shepard and away from the hostiles, the nearest `Engine.CoverLink` slot behind them within `coverSearchM`; `retreatToCover = 0` falls back to a follow). The parser also splits clauses at a squadmate's name (a vocative) and knows "target" and "support/cover/back up X" (X's standing target, else her point, else her last target, else her side); an equip dispatches through `squad_weapons::GiveWeapon`. **The corpus** (`tests/SquadPhrasings.cpp`, run by `mevr-tests`): 196 lines a player would say, each with the line it must become or the reason it's refused; 173 of 173 orders parse as said, 18 of 18 pieces of talk and impossible orders ("hold fire", "medi-gel") are refused by name, 5 known gaps on record (research/54 "The corpus"); the parser knows phrasal verbs ("take them out"), directions (`Target::Direction`: "go left" is a point 8 m that way, never the group there), the implied owner of a power ("throw them" is Wrex), "stay on him" (attack) against "stay on the bridge" (hold), bare "cover the door" (a hold), two named squadmates (both), talk split into its own clause. LE1's squadmates voice only the follow order (`EBioSoundSetType.GiveCommandFollow`; attack and move sets are UNUSED), so re-asserted orders never chatter. Whether the AI stays put under HOLD, takes the cover slot, and accepts being re-ordered every few seconds, is [verify]. Built, not yet run in the game (the squad index base is [verify]). Log `squad orders:`; snapshot `"mod"."squadOrders"`. `mevr context [word [secondsAgo]]` prints the block; log `squad context:`; snapshot `"mod"."squadContext"`.
- LE1 squad placement by an arc ([research/54](research/54-voice-squad-commands.md) "Placement by an arc", 2026-11-05; HostServices v32
  `setSquadPlace`; rules `core/vr/SquadPlace`, unit-tested; `game/SquadPlace`; host `plugin/SquadPlaceDraw`): **hold the right B on foot**:
  an arc leaves the right hand's aim like a thrown ball (`[SquadOrders] placeSpeedMS` 14, `placeRangeM` 30; traced segment by segment with
  `Actor.Trace`, level geometry only), a ring lies where it lands on floor; let go and they go there and hold it until called back (`squad_orders::GiveAt`'s hold until
  relieved, plus each one's AI `BioAiController.PushMoveToPoint`: **`QuickCommandMoveTo` ignores the point in VR**, it lands along the
  head's facing; `CallHold` sets the HOLD formation before the point, a drifted hold re-asserts the point, never a move; measured 0.6 m
  from the ring; the
  one dispatcher). **Who goes: the squad wheel at the right hand** (2026-11-05; B and the stick share the thumb): with two squadmates B
  first opens the power wheel's look at the right hand (`plugin/PowerWheel`'s second canvas, `squadwheel` layer; first squadmate left,
  Squad up, second right); the hand's travel lights a tile at 2.5 cm and picks it at 7 cm (one pulse for the first, two for the second,
  a long one for the squad), the wheel closes and the arc comes up; one squadmate: the arc at once. The ring names them (amber for one,
  blue for the squad). **B double-tapped: the whole squad back to Shepard.** Within `followWithinM` of Shepard: a follow
  (green); a wall, a steep slope or nothing in reach: called off (red). Off while `meleeButton = 1`, in the squad's or the power wheel,
  anywhere but on foot (`place = 0` off). The host draws it in a projection layer of its own (galaxy_draw's renderer, over the world).
  `mevr place aim <pitchDeg> [yawDeg] | release` without controllers. Logs `squad place:`; snapshot `"mod"."squadPlace"`, layers
  `squadplace-left`/`-right`.
- LE1 voice orders ([research/54](research/54-voice-squad-commands.md) "The voice path built", 2026-11-05; HostServices v31): **hold the
  LEFT grip at the left ear (the radio), speak, let go;** the open left hand hums in the radio zone, stronger nearer the ear
  (`[Voice] earCue`, `earCueStrength`; the holsters' contact buzz, `game/Voice` `EarCue`), so the spot is found by feel. The host owns the microphone and the recognizer: `ASI\voice\MEVR-Voice.dll`
  (`src/voice`, our C interface `VoiceApi.h` over Moonshine Voice's prebuilt MIT library and ONNX Runtime; `tools/fetch-voice.ps1` fetches
  them into `external/moonshine-voice` and the models into `models/voice`, both gitignored; `tools/deploy-asi.ps1` copies them to
  `ASI\voice`): WASAPI capture, a Moonshine stream per listen with `tailMs` after the release, partials every `intervalMs`, key-term
  biasing from the roster, word timestamps, the game's audio ducked; `plugin/Voice` loads it on a worker (no DLL or models: voice off, one
  log line), listens on the mod's request or `[Voice] key`, reads `%LOCALAPPDATA%\MEVR\voice.request` (`wav <path>`, `listen <s>`,
  `keyterms`, `stop`). The mod (`game/Voice`, rules `core/vr/VoiceGesture`, unit-tested) claims the squeeze before the flat orders, joins a
  listen's lines and gives them to `squad_orders::Say`; a settled partial that parses whole is given early (`earlyDispatch`, `settleMs`);
  a refusal is two buzzes. **The squad's answer is the game's own line** (`game/SquadAck`: `BioPawn.OnChatterLinePlayed` on the squadmate's pawn is the
  answer, `SquadHandleChatterEvent` / `HandleChatterEvent` / `PlaySoundSet` heard too; traced 2026-11-05: a follow's bark is Shepard's own, a
  hold is answered by the squadmate through the HOLD formation, a `QuickCommandMoveTo` by nothing, so the order's sound set is played by us
  after `ackFallbackS` when no line came; `[SquadOrders] ack*`); power orders through `powerRoute = tactical | ai` [verify]. **The model tier**: `core/voice/Grammar` (per-scene GBNF, unit-tested), `core/voice/Llm`
  (llama.cpp's shared build loaded by name, the prefix cached, greedy under the grammar; compiled only with `external/llama-cpp`),
  `plugin/VoiceModel` (`[Voice] llm`, a GGUF in `ASI\voice\llm`), asked by the mod when the rules say "not sure"; data and fine-tune in
  `tools/voice-model` (`pipeline.py`: one config per run, resumable steps, sweeps, `runs.tsv`; research/54 "The fine-tune": the
  first LoRA on Qwen3-0.6B, 20,000 synthetic lines, 58 min on the 4090, 939 of 1,000 held-out lines exact; its q8_0 GGUF is
  `models/voice/llm/model.gguf`, which deploy-asi ships; under llama.cpp 285 of 300 exact at 0.62 s median on 4 quiet threads,
  **only with the chat template around the prompt** (`Llm`'s `Settings::chatTemplate`, the trainer's `SYSTEM` line; `[Voice]
  llmChat`) and `-` allowed as the grammar's order: without them 2% and 81%). Benches: `mevr-voice-bench` + `tools/voice-bench.py` over `tools/voice-tts-corpus.ps1`'s WAVs,
  `mevr-llm-bench`. Logs `voice:`, `voice model:`, `squad orders: ... answered`; snapshots `"voice"` (host), `"mod"."voice"`,
  `"mod"."squadAck"`. Run in the game on the fake headset with a file feed (research/54 "In the game"); a real microphone and the
  fine-tuned model's answers in play are the checklist's (section 1).
- LE1 power wheel at the left hand ([research/55](research/55-hand-cast-powers.md), HostServices v28 `setPowerWheel`, v29 `setPowerLock`; `core/vr/PowerWheel`,
  `game/PowerWheel`, host `plugin/PowerWheel` + `PowerWheelDraw` + `PowerIcons` + `Glow`; `[PowerWheel]` in `ASI\LE1MEVR.ini`, appended by
  the module itself): on foot in VR, X held opens it at the left palm, fixed in the room, facing the eyes, with the player's mappable
  powers (the behaviour's `m_powers` through the shared table `FindPower`: the 2DA label, the shown name, discipline, icon); the left
  hand's travel toward a tile or the left stick picks; letting go of X equips (X tapped: a left trigger pinch, a left grip squeeze or X
  again). Equip = `BioPlayerInput.MappedPower` (journalled), which the push casts (the right stick click's tap no longer does, 2026-11-05).
  **The lock and the push** (research/55 section 7, `[PowerWheel] lock*`/`push*`): wheel closed, the left arm raised (`lockReachM` ahead of
  the eyes), the hostile nearest the left aim ray (`IsHostile`, not dead or dormant, `FastTrace` from the hand; 9 degrees to take, 16 to
  keep, 35 m, 0.35 s of blocked sight; nothing pointed at: the automatic resolver, the best within 30 degrees of the hand or 20 of the
  gaze, `autoLock`) is locked (`UpdateLock`) and marked by the host's `powerlock` quad (brackets facing the eyes); an open
  left hand pushed along its ray (1.6 m/s over 0.10 m, `StepPush`) casts the mapped power with `BioPlayerInput.ActivatePower(nmPower,
  oTarget, vTargetLocation)` at it (a place power with none: where `Actor.Trace` along the ray lands; no aim: at once); measured
  2026-11-05: the running `BioActivePower` keeps that target and point through the release, and Throw ragdolled the locked
  hostile 1.6 s after the push; dormant = `BioActorBehavior.bActive` false or hidden. Console `mevr powerwheel
  push|aimhead on|lock`; powers on a save without them: `GiveBonusTalent 49` then `ScaleTalentByIncrement 49 1` (Throw; 57
  Singularity: the cheat takes the 2DA's BonusID, research/55 section 7). **The player's powers fly nothing** (they land at the
  release, about 0.58 s after the cast; only other casters' fly, research/55 section 8), so **a bolt from the hand** (a prototype,
  section 9; `mevr powerwheel bolt [on|off|speed n]`, `vfx [all]` maps every power's effects) spawns the enemy version's projectile
  effect (Throw's: a biotic hand) at the left hand with `BioVisualEffect.CreateVFXOnPosition` and flies it to the target in the
  release delay learned from `BioPawn.ReleasePower`; where no enemy version is loaded, the power's own charge or omni-tool effect.
  **A cast with the weapon away** spent the cooldown for nothing (the release is the gun-out casting animation's notify): the mod
  sends `BioPawn.ReleasePower` itself at the learned delay (`mevr powerwheel unarmed on|off`), so casts work with the gun out
  (left hand alone) and with it holstered. **Self-cast powers by gesture** (section 10; `[PowerWheel] selfGestures`): the open
  left palm on the chest (Barrier, Shield Boost, Immunity, Adrenaline Burst), slapped onto the gun (Marksman, Overkill, Carnage,
  Assassination) or raised above the head (Unity), never the left trigger or grip (`vr::power_wheel::StepSelfCast`;
  `mevr powerwheel selfcast`); powers that use the omni-tool (tech, or the omni-tool in their own effects) wear the gauntlet
  while aimed, gestured and 2.5 s after the cast. While on, X never throws a grenade on foot (the pad's `grenadeButton` off); while open the left stick, L3, X, the
  left trigger and grip are its own (`gamepad::kFlagPowerWheel`, `Latches::powerHeld` until released) and the squad's trigger stands down
  (`power_wheel::OwnsLeftHand`). **The look is the omni-tool's projection** (research/55 section 5 "The omni-tool's projection", 03a6a08;
  the squad wheel shares it): orange light over see-through tiles, the chosen tile ME1 cyan, light from an emitter in the wheel's gap
  (no rings, spokes or N7 mark), the game's own Biomass from the game-art cache (Rajdhani without it), icons through
  `power_icons::Hologram`; a two-level bloom (`plugin/Glow`, its own device
  context state) adds light in the premultiplied quad. Offline look: `mevr-power-wheel-preview`. Tests: `tests/PowerWheelTests.cpp`
  (`RunPowerWheelTests`). Console: `mevr powerwheel [open|hand x y|pick n|equip|close|roster]` (no tracked controllers needed). Logs
  `power wheel:`; snapshot `"mod"."powerWheel"`, layers `powerwheel`, `powerhint`, `powerlock`.
- Players' installer ([research/33](research/33-installer.md)): `MEVR-Setup.exe` (`tools/setup`, the in-game menu's
  RmlUi glass style; pages Welcome, Install, Optional content, Settings, Repair, Uninstall, Launch) over `src/install`
  (unit-tested): finds the game (Steam, EA app, a picked folder), checks the exe version and the OpenXR runtime,
  installs the monolithic ASI, fonts, menu, Halcyon's release `MELEVR.ini` only if missing (then deploy-asi's per-slot
  fixes), the VR game settings, and records it all in `ASI\MEVR-install.json` with backups and key-level ini edits (one
  per line of a repeated key), so Uninstall restores the game exactly. QA fixes (research/33 section 7): Halcyon's
  upstream MELE-VR (`dxgi.dll` marked "MELEVR", `openxr_loader.dll`) is renamed to `*.mevr-off` and put back on
  uninstall; a `bink2w64.dll` another tool put over ours is never touched; the mod's run-time files (`LE1MEVR.ini`, its
  logs) are recorded as user data, so a full uninstall deletes them (and `%LOCALAPPDATA%\MEVR` only when its box is
  ticked); "installed" means the manifest has the mod; a read-only `GamerSettings.ini` is a warning, not a failure. The ASI loader is our own `bink2w64.dll` (`src/loader`, `MEVR-Loader`: 80
  forwarded Bink exports, ME1's 79 and the launcher's `BinkOpenMiles`, the SPI interface, the DRM wait by sampling `.text`;
  ctest `mevr-loader`), unless LEBinkProxy is there; **it goes into `Game\Launcher` too** (research/33 section 12), where
  with `-game 1` it starts Mass Effect 1 with the launcher's arguments and with `-autoterminate` ends the launcher.
  **One click** (the user, 2026-11-05): the Welcome page's Install installs at once with the defaults (Options opens the
  Install page), then the same button is Launch. Launch starts SteamVR first when it is the active runtime (waits up to 60
  s; other runtimes get a line saying what to start), then, on Steam, asks Steam for `-applaunch 1328670 -game 1
  -autoterminate` (`MassEffect1.exe` started directly hands its start back to Steam, which then waits on its "launch with
  these arguments?" dialog: research/33 section 11); the EA app build still starts `MassEffect1.exe` directly [verify].
  `MEVR-Setup.exe --launch` does the same from the command line. Tested on the real game (sections 11, 13): install, verify
  then Repair, uninstall byte for byte, Launch through Steam to gameplay in one click. The game copies its live settings into
  `BIOEngine.ini`'s `[SystemSettings]` during play, so Setup records the VR keys there too (`settings::ApplyEngineSettings`);
  what the mod writes there later (Halcyon's profile resolution, the first VR start's eye size, `DepthOfField`) Uninstall
  puts back too (`settings::ModWrites` from `MELEVR.ini`, `ini::Record`). A game never started (no
  GamerSettings.ini) keeps the chosen settings as `pendingSettings` in the manifest and offers "Start the game once";
  the resolution's default without a known headset size is **Automatic** (`auto`, 2026-11-04, the owner): Performance now,
  the manifest's `eyeSizeAtFirstStart`, and the host's `main_menu::FirstStartEyeSize` sets 0.85 of the headset's eye size
  (FSR, on by default, scales it up; Setup's "Headset" choice is the same 0.85) at the first VR start for the launches after
  it, once (`[Display] eyeSizeSet = 1`; log `first VR start:`);
  Repair applies them once the file exists, puts back what Steam's "verify files" removed (loader, TOC entry) and the
  status lists what it would fix (research/33 section 9). Texture packs: the pack's page
  plus Mass Effect Modder's CLI, never bundled or auto-downloaded (Nexus terms). In game, Display > Optional content
  writes `%LOCALAPPDATA%\MEVR\setup.request`; the host's ExitProcess hook starts `ASI\MEVR-Setup.exe --request`.
  Test only on a fake game: `tools/setup/test-fake-game.ps1` (`scratch\fakegame`); pages offline:
  `MEVR-Setup.exe --preview <folder>`. Release: `tools/package-release.ps1 [-Zip]` into `dist\` (local only; it copies
  the third-party licence texts into `licenses\` and refuses while the ASI links LExSDKv2 unless `-LexSdkPermission`).
  **LExSDKv2 has no licence, so the players' build has none of it** (2026-10-26): the release (monolithic) build sets
  `MEVR_LE1_LESDK=OFF` (CMake; default = `MEVR_HOT_RELOAD`), LE1 links no `LESDK-LE1` and `engine/ReflectCheck`'s
  cross-check compiles out (`MEVR_NO_LESDK`: it logs `reflect: no LExSDKv2 cross-check in this build`). Development
  builds keep it. Never add LExSDKv2 use to an LE1 code path.
- Report a problem ([research/33](research/33-installer.md) section 8): MEVR Setup's page (or `--report [--snapshot]
  [--perf]`), the menu's Advanced > Diagnostics > Report (`plugin/ReportRequest`: starts `ASI\MEVR-Setup.exe --report
  --from-game` at once, background priority, no window) and the crash offer (`install/ReportMarks`) write a redacted
  `MEVR-report-<date-time>.zip` to the Desktop with REPORT.md first (`install/Report`, `install/Zip`; unit-tested). Never
  uploads anything. Issue form: `.github/ISSUE_TEMPLATE/bug.yml`. The build's commit: `tools/cmake/BuildInfo.cmake`.
- **The owner's dev settings are the shipped defaults** (2026-11-05, after the first QA pass of the release found the
  release without them): `[Hands] handPitch = 90` (`vr::menu_settings::kHandPitchDefault`: the game's hand model sits 90
  degrees off the grip pose; the menu's hand-angle row trims around it), `[Performance] sharePass = 5`,
  `umbraSecondEyeClearBits = 2`, `[Upscale] method = fsr`, `[Game] pauseUnfocused = 0`, the render target at 0.85 of the
  headset's eye size, and in Halcyon's fresh `MELEVR.ini` (`ini::HalcyonReleaseIni`, `tools/deploy-asi.ps1`) `decoupledPitch = 1`,
  `cameraOffsetEnabled = 1` (right 0.078, up -0.082) and `screenDistanceEnabled = 1` (0.674). A new setting the owner tunes in
  their own file is a candidate default: compare `ASI\LE1MEVR.ini` and the active `MELEVR.ini` profile with a fresh install's.
- Release engineering (docs/release-checklist.md section 5): **the version is written once**, `project(MEVR VERSION x.y.z)`
  in `CMakeLists.txt` (generated `mevr/Version.hpp` for the C++ and the `.rc` files; the binaries' ProductVersion is
  `<version>+<commit>`; the menu's About shows both). `tools/package-release.ps1` refuses a dirty tree (`-AllowDirty`), a
  version or tag other than CMake's, binaries not built from HEAD, check-spdx or ctest failures (`-SkipTests`), and puts the
  PDBs in `dist\MEVR-<version>-symbols` (the binaries name theirs without a path: `/PDBALTPATH`). CI:
  `.github/workflows/build.yml` (windows-2022 for VS 2022; the players' build without LExSDKv2, ctest, check-spdx, check-okf)
  and `release.yml` (a `v*` tag: the package without the cockpit, as artifacts; nothing published; it signs the three
  binaries through SignPath Foundation once the repository has the SignPath variables and token, docs/code-signing.md,
  `.signpath/artifact-configuration.xml`). Every binary's ProductName is "MEVR" (SignPath's rule; Setup finds our loader
  by "MEVR" in it). Setup's icon:
  `assets/icon/mevr.ico` from `tools/make-icon.py`. Player-facing changes go in `CHANGELOG.md`.
- Trilogy check: `build-trilogy/` (configured with `-DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=ON`) must
  keep building all three games. Per-game field spellings go in `src/engine/GameAdapter.hpp`;
  per-game addresses sit next to their use (e.g. `engine/SceneView.cpp`).
- Offline tests: `cmake --build build --config Release --target mevr-tests`, then
  `ctest -C Release --test-dir build`. Put game-independent math there.
- `tools/vr-game-settings.ps1` sets VR-friendly GamerSettings.ini values (VSync, motion blur,
  film grain off, HDR to SDR, 16x anisotropic filtering); `-Restore` puts the user's originals back. On
  LE1 it also does what Halcyon's installer (`MELE-VR.bat`) does: a borderless window (a bordered one is
  capped at the monitor's height, which broke stereo), dynamic shadows off, frame smoothing off, and the
  stereo target, mirrored into `MELEVR.ini`: Halcyon's ladder (`-Quality`, Performance 3072x3188) on the
  first apply or when asked, else `-ResX/-ResY`; a later apply keeps the target. Since 2026-10-23 it is
  the headset's own eye size (2824x2880 on the user's PS VR2: the GPU was over budget at p90 rendering 18%
  more pixels than displayed, research/22). The deploy installs Halcyon's *release* `MELEVR.ini` (its tuned
  code defaults), not the older `src/halcyon/le1/runtime/MELEVR.ini`, which pins stale values.
  **BIOEngine.ini's `[SystemSettings]` wins over GamerSettings'** for `DynamicShadows` and `MotionBlur` (measured
  2026-10-23, research/35), and the game rewrites that file (after every `scale set`; a launch put the defaults
  back once), so **the mod applies the VR values itself** (`game/SystemSettings`, hot-reloaded): `scale set
  DynamicShadows False`, `MotionBlur False`, `FoliageDrawRadiusMultiplier 0.5` through the local player's console
  once a player exists, **again after every level change, each boolean through its opposite first** (research/35
  attempt 49, 2026-10-28: the save's level rendered with the blur although the ini and the title's `scale set` said
  off, and the same `False` there did nothing while True then False turned it off; in stereo the blur's velocity is
  the other eye's view, so every surface smeared its own way in runs of frames: the "assets shift in different
  directions" report); `[SystemSettings]` in `ASI\LE1MEVR.ini` overrides any key (`apply = 0` off); log `system
  settings:`. The tool writes the same three into BIOEngine.ini too. At a heavy spot shadows off and the foliage
  radius took the render thread from 9.35 to 6.88 ms and the GPU from 8.4 to 4.9 (the grass costs per instance,
  twice a frame in stereo; 0.25 pops the grass in too close); motion blur on smeared one eye and not the other
  every few seconds (the blur's "previous frame" is the other eye's view: one shared view state). The engine's
  `show <category>` console toggles attribute a frame's cost by category; `scale set <key> <value>` sets a
  SystemSettings key live (works for those, not for DetailMode or the LOD distance scales).
- The mod connects to Windows' active OpenXR runtime (Oculus, or SteamVR for Steam Link). The
  user currently uses Steam Link / SteamVR.
- The game's D3D11 device is SINGLETHREADED: no deferred contexts, and all D3D/XR work happens
  on the render thread inside the host's Present callback. Its backbuffer is FP16 (scRGB, linear).
- Quitting from the headset's menu (session EXITING) must turn VR off, not recreate the session.
  The host hooks ExitProcess so XR shuts down before the Oculus DLLs unload.
- Our targets build with `/W4 /WX /permissive`. The SDK's generated code emits warnings
  (C4315, C4789) that aren't ours.
- The LExSDKv2 submodule tracks the `RVA` branch (hardcoded RVAs for 2.0.0.48602). The plugin
  fails closed on any other exe version, and (since research/34 stage 4) unless every address in
  `engine/Addresses.hpp` is where the table says (`core/Exe`: .pdata bounds and byte patterns; the log names each one
  that moved). A new raw address goes in that table with a pattern (`tools/rva-patterns.py`); ctest `mevr-rva-table`
  fails if a `k...Rva` constant isn't in it.

## Test loop (LE1): see what the headset shows, replay a session

Use it to check VR changes yourself instead of asking the user what they see
(`src/plugin/TestLoop.hpp`; host side, so changes need a redeploy). Everything lives in
`%LOCALAPPDATA%\MEVR`; the log lines start with `test loop:`.
- **Snapshots:** `tools/snapshot.ps1 [-Count N -Interval s -Delay s -Tag x]` writes
  `snapshot.request` and waits. Output in `snapshots\`: `<stamp>-left.png` / `-right.png` (what
  each eye shows: the world plus our HUD panel, reticle, squad menu, the game's wheel and Halcyon's
  menu, sight, scope, composited like the runtime does), `-left-world.png` (world layer alone), `-hud.png`,
  `-squad.png`, `-wheel.png`, `-menu.png`, and `<stamp>.json` (head and eye poses/FOVs,
  controllers, aim ray, host view, the mod's arm IK / hand aim / weapon / support grip / holsters /
  squad state, every layer). The JSON
  is written last. Read the PNGs to look.
- **Recording:** `tools/record.ps1 -Seconds N` (or `-Start` / `-Stop`) writes `record.request`;
  frames go to `recordings\<stamp>.mevrrec` (head eye views, both controllers' poses and inputs).
  `python tools/mevrrec.py --latest [--csv out.csv]` summarizes or dumps one.
- **Replay:** `tools/replay.ps1 [-Path f] [-Loop] [-Inputs] [-Fovs] [-Wait]` / `-Stop` writes
  `replay.request` (`<path|latest|stop> [loop=1] [inputs=1] [fovs=1]`). The recorded head (the runtime's
  FOVs kept, or the recorded per-eye FOVs with `-Fovs`: a PS VR2 recording's asymmetric projections on the
  fake headset, research/37) and controller poses stand in for the live ones; buttons and sticks only with
  `-Inputs`. Replay + snapshot = look at a recorded pose: `tools/replay.ps1 -Loop`,
  `tools/snapshot.ps1 -Delay 2`, `tools/replay.ps1 -Stop`.
- **Player triggers:** End = snapshot, Home = recording on/off (game focused); a quick tap of both
  stick clicks = snapshot (both down within 100 ms of each other and up within 300 ms of the first
  press; held 400 ms they open our main menu instead, never both: `gamepad::UpdateStickChord` in
  `HalcyonBridge`; L3 is held back 100 ms so the tap is never a crouch, and R3 opens the power wheel only
  after 300 ms alone); our menu's Advanced > Diagnostics (Snapshot, Record) and the classic menu's VR
  hands tab ("Test loop": snapshot in 3 s, record, replay the last recording). There's no controller
  chord for recording (both grips are the gun and the squad menu since 2026-10-22).
- **Frame timing** ([research/22](research/22-performance-levers.md)): `tools/perf-probe.ps1 [-Seconds 20
  -Delay s -Tag x]` writes `perf.request` (or our menu's Diagnostics, or the VR hands tab: "Measure frame
  timing"); `plugin/PerfProbe`
  records each frame's steps on the render thread (time, busy time, draws, GPU timestamps mapped onto the
  CPU clock, access violations) and each game-thread tick into `perf\<stamp>-frames.csv`, `-ticks.csv` and
  `-probe.json`, then `python tools/perf-probe.py [--latest]` says whether the render thread, the GPU or
  the game thread sets the pace. Its "poses -> submit, wall" row is what fpsVR / SteamVR call the CPU frametime
  (xrWaitFrame's return to xrEndFrame, wall time: holds and idles count; research/35 attempt 41); `tools/fpsvr-session.py`
  reads fpsVR's own per-session records (`%LOCALAPPDATA%psVR`) of what the player's headset sessions showed. Seams: `MevrBridge::PerfPresentScope` / `PerfPass0Captured` /
  `g_perfDraws` and the wrapped `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`. `tools/perf-monitor.ps1`
  adds whole-GPU load; `tools/sample-render-thread.ps1` samples the render thread's stack from outside
  (`mevr-stack-sampler`): where its time goes by module and function, no redeploy.
  **Measuring without a person** (research/35 attempts 40-43): `tools/fake-headset.ps1 -On` (SteamVR's null driver; `-Off`
  gives the real headset back; it also registers the null driver's chaperone universe "2", else vrmonitor launches room setup
  after every SteamVR start and vrserver kills the game at the title 5 s later, silently: research/35 "Setup for measuring"), `tools/launch-game.ps1` (EA's Link2EA from a scheduled task, PLAY and the title's Enter by
  keyboard from a foreground shell; never start the game from this shell: the harness's job object breaks the EA app;
  **`-Steam`** asks the running Steam instead, which works while the EA app sits in a job: research/50),
  `tools/console.ps1 "mevr density 2000"` (the densest mesh cells), `"mevr crowd 1000"` (where the characters stand),
  `"mevr stream list"` / `"mevr stream touch <BioTriggerStream_N>"` (a hub's streaming volumes: touching one streams its area
  in as the player's walk would; the Citadel's Wards, Presidium, C-Sec: research/50), `"god" "ghost"` then `"set SFXPawn_Player Location
  (X=..,Y=..,Z=..)"` to move Shepard (Actor.SetLocation through ProcessEvent did nothing until 2026-11-03: the gotcha below), `tools/spot-survey.ps1`
  (each thread's work, the GPU's, draws and characters near per spot under a replayed head recording; a spot's fourth word
  is a volume to touch first; `tools/make-look-recording.py` writes a head turning a full circle, so every direction counts), `tools/measure-levers.ps1 -Key k -Values 0,1 [-Inputs]`
  (an A/B under the replay; `-Inputs` walks and shoots as recorded), `tools/fpsvr-session.py` (what fpsVR recorded).
  Eden Prime's heaviest cells: (-31000, 145000, 900) and (-33000, 147000, 400), 4,100 draws. The Citadel (John's Save 96,
  research/50): crowds are cheap (skinned meshes 1.0-1.15 ms of render thread), the Presidium's 3,200 draws its peak.
  LE1's menus take real input (`keybd_event`, a click) with the game window in front; posted keys reach them only as Enter. Live render levers
  (`plugin/RenderLevers`, [research/32](research/32-ue3-vr-performance.md)) in `[Performance]` of `ASI\LE1MEVR.ini`:
  `sharePass` (**default 5 since 2026-11-05**, the owner: "the dev settings should be default"; 1: the second eye renders
  on the first's scene renderer, no second visibility; 2: as 1
  but the second eye is a replay of the first's recorded D3D11 calls with the view's bytes substituted,
  `plugin/DrawReplay`; **3** ([research/37](research/37-single-pass-stereo.md), 2026-10-25): the second render keeps
  its Umbra resolve (two Draws with one resolve deadlock the game thread in Umbra's command ring) but its pass
  functions return at once and its finish replays the first eye's tail with the translated view-projection replaced
  whole by `T(pvt2 - pvt1) x TVP2` (the eye lives in every draw's LocalToWorld, not in the per-view matrix); a correct
  second eye at 10.9 ms against 11.4 with both engine renders at the heaviest cell: the recording costs what the
  replay saves (the recorder holds references in a generation set, holds none for samplers, shaders and state objects,
  and records from the tail mark, 2026-10-26; the probe's "before the wait" step counted the present path twice under
  the early frame wait until the same day); **5**: one FViewportClient::Draw a frame (Halcyon's second Draw skipped through `g_oneDrawStereo`,
  pass 0 recording what pass 1's view would be, `MevrBridge::RecordPass1View`), the first render recorded and replayed
  in its own finish hook with the second view synthesised in the host (a scene render into another target than the eye renders', a
  SceneCapture2D's, takes no eye index and runs untouched, counted as a capture in the levers line, 2026-10-27): the
  engine's own right eye to within temporal noise, 9.1 ms against 11.4 (20%) at the heaviest cell, `replayInverse = 0` drops the inverse-matrix pair; **both eyes
  render on the symmetric window** (the plan's mode 1) because the frame's only render decides visibility for both eyes
  and its occlusion tests see only its own frustum (an object in the other eye's outer wedge stayed "occluded" under the
  PS VR2's projections; mode 1's angular resolution is the price, a wider `-ResX` buys it back, and 24% of each eye's
  rendered columns are never shown, so `oneDrawWindow = 0` renders each eye on its own frustum instead when the GPU is
  the limit; `eyeScissor = 1` (2026-10-26, off by default) scissors each eye's scene draws to the columns it shows with
  the occlusion queries exempt, through scissor-on clones of the engine's rasterizer states, which keep scissoring off:
  worth 1% of GPU at the heavy cell, since the one render's visibility needs depth over the whole window and the cell
  is draw-bound; research/37's 2026-10-26 section, with the vtable re-entrancy lesson); while no record of
  pass 1's view matches the rendered one for 3 frames (the intro's cine camera after a level load: a view edited after
  pass 0 recorded it) the host clears `g_oneDrawStereo` and Halcyon's second Draw resumes until 30 frames match again
  (log `one Draw: ... Halcyon's second Draw resumes`, the levers line's `its health:`); **the engine's 4 MB dynamic
  vertex and index rings wrap (a DISCARD map) inside the recorded tail** on 4-5% of quiet frames and far more in combat,
  and the draws before the wrap would read the post-wrap contents in the replay (the strobing right eye of the first
  headset session: the fog quad drawn from garbage): the recorder copies such a buffer on the GPU into a shadow just
  before the discard and the replay binds the shadow for those draws (`large buffers discarded inside the tail: N frames
  (M discards, K shadowed)` in the levers line; an unshadowed one blocks the frame); the recording's 1.1 ms
  is 33,000 hook entries a frame, not copies (its maps are 17 KB a frame; the 7.7 MB are UpdateSubresource's). **d3d11
  keeps a vtable per context and swaps its draw, clear, copy and dispatch entries between variants of each method as
  the context's state changes**, so a hook in that vtable or on one variant's body misses calls: the recorder hears
  the draws through `[MEVR]` seams in Halcyon's own hooks (`MevrBridge::Record*`) and hooks the other entries inline
  per variant. **A fault on the render thread inside a hook ends that thread behind the engine's own handler and the
  game thread then waits forever** (no crash, no log line but the first-chance one `CrashBreadcrumbs` now writes, three
  per address): after `FSceneRenderer::Render` returns, the renderer's views are freed, so read them inside the render
  (a pass or finish hook) or at the deleter, and wrap render-thread work in SEH), `umbraQueryWait` (0: the Umbra resolve never spins for a query result), `pCoresOnly`,
  `perEyeOcclusion` (**default 0** since 2026-10-24, research/35 attempts 34-37: the frame's second scene render gets its
  own copy of the engine's per-primitive occlusion history, a 0x48-byte TSet at the view state's +0xD10, swapped in
  around it; it removed the second eye's 4-5 ms spin for the first eye's queries, but in the headset every ~10th frame
  then drew three times the meshes, and the wait only moved into Umbra's own result reads), **`umbraEyeMatrix`**
  (default 1, attempt 36: Umbra's occlusion queries are drawn with the eye's matrix (+0x1D0's world-space equivalent put
  into +0x260 around `UmbraCommander::IssueOcclusionQueries`), which fixed the objects popping with head motion in the
  life-size stereo's mode 2; the engine's own occlusion boxes already used +0x1D0), **`keepBoundShaderStates`** (default 1 since
  2026-10-24, research/35 attempts 42-43: the D3D11 bound shader states the dynamic-mesh drawing policies rebuild every draw
  (ID3D11Device::CreateInputLayout each time, 300 a frame at a heavy spot) are kept alive by one extra reference through the
  RHI's own AddRef once their key (declaration, VS, PS) is constructed a second time; 15.8-16.5 -> 10.5-11.1 ms at the
  heaviest Eden Prime spot in a fresh session, ~1 ms warm; the kept set plateaus near 5,000 and memory stays flat; hazard
  noted there: a kept state outliving its vertex declaration), `minScreenRadiusPx` (**default 8 since 2026-11-04**, the
  owner; 0 measures only; attempt 45: Umbra's visible
  instances whose bounding sphere projects below that radius in pixels of a 2160-high eye are not marked visible; the levers
  line carries the size distribution; 8 px is invisible and worth 0.3 ms at the heavy spot, 32 px 0.8 ms with pop-in),
  `umbraSharedVisibility` (default 0, attempt 46: three failed forms of "Umbra's resolve once a frame", kept as the record
  of Umbra's query pipeline, which runs across consecutive resolves regardless of eye; never set it); log
  `render levers:` (scene renders per frame, each one's time with its Umbra resolve's parts, the draw replay's counts);
  `tools/perf-ab.ps1 -Key <key> -Values a,b` alternates one and captures each; `tools/eye-diff.ps1` compares one
  frame's two eyes call by call (run it first: it says whether the replay's substitution covers the eye's constants).
  There is no depth pre-pass to skip: `0x76C1D0` is the base pass (research/32).
- **Call trace:** `tools/console.ps1 "mevr calltrace 10 drawprimary fireweapon -canfire"` logs every script call (with its
  caller, the frame's Node) and ProcessEvent whose function path holds a word and no `-word`, for that many seconds, at most
  600 lines (`game/call_trace`, `call trace:`): how to find which route the game takes. Nothing while off.
- **Console:** `tools/console.ps1 "UMBRA OCCLUSIONCULLING"` writes `console.request`; the mod (`game/ConsoleRequest`,
  hot-reloaded) runs each line through `PlayerController.ConsoleCommand` on the game thread and writes what it
  printed to `console.result` (log `console:`). Flat or in VR, wherever there is a local player: A/Bs without a restart.
- `testloop.status` (JSON, once a second): recording, replay, last snapshot, `framesRunning`.
- **The request files are a development-build feature** (since 2026-11-04, `core/DevChannels`, docs/release-checklist.md):
  the hot-reload build reads `console.request`, `snapshot`/`record`/`replay`/`perf`/`eyediff.request`; the players'
  (monolithic) build ignores them unless `[Debug] requests = 1` in `ASI\LE1MEVR.ini` (so set it when testing a
  `build-mono` deploy with these tools). Its classic menu has no replay, its open-ended recordings stop after 30 minutes,
  and it keeps the newest 2 GB of snapshots, 512 MB of recordings and 256 MB of perf captures (`[Debug] snapshotsMB`,
  `recordingsMB`, `perfMB`; 0 no limit; development builds keep everything). Halcyon's `MELEVR_Log.txt` over 16 MB moves
  to `MELEVR_Log.prev.txt` at a launch (`[MEVR]` in its logger). The mod's status block (heartbeat plus every module's
  `LogStats`) comes every 30 s in the players' build, every 5 s in development builds (`[Debug] heartbeatS`, read once).
- **After a crash:** read `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt` (the one before is `.prev.txt`; the same text
  is at the end of `LE1MEVR.log`). It holds the exception, the registers, the native stack, the script frames
  (FFrames) on the faulting thread, and the last ProcessEvents, script handler calls and mod writes, with names
  (research/36). The unhandled-exception filter writes it once per process; handled AVs don't write it. **A game that just
  vanishes** (no report; Windows' Application log has an APPCRASH): the game ends some faults its own way, past the filter, so
  the first-chance handler writes the same report to `crash-firstchance.txt` (`.prev.txt` before it) when the game's own code
  reads or writes a null-ish address, the first time at that address, at most 3 a process (2026-11-05: leaving the Mako ended
  the game three times at `MassEffect1.exe+0x307120` reading 0x0 with no report; not reproduced on the fake headset).
- **Console commands:** `tools/console.ps1 "<command>" ...` runs them through the local PlayerController's
  `ConsoleCommand` (`game/ConsoleRequest`, mod side) and prints the output. `tools/console.ps1 pause` freezes the
  world but keeps rendering, which holds a scene still for snapshots and A/B captures.
- Needs a running VR session (Halcyon only runs frames between READY and STOPPING). Not focused
  (headset off the face, VISIBLE) is fine: replay drives the view and controllers. SYNCHRONIZED or
  asleep: no eye images (`shouldRender` false) or no frames at all. See notes/next-vr-session.md.
- How it hooks in: one seam in Halcyon's `ResolveSessionFunctions` (`MevrBridge::
  WrapSessionFunctions`) wraps `xrAcquireSwapchainImage` / `xrReleaseSwapchainImage` (copy each
  image to staging before release, only while a snapshot is armed), `xrEndFrame` (the layers) and
  `xrLocateViews` (record the head; substitute it in a replay). Our quads report their images from
  `HalcyonOverlays` (HUD, reticle, wheel) and `SquadPanel`. Staging copies are mapped 2+ frames later and converted on a
  worker thread.
- The squad menu's look can be checked offline: `build\Release\mevr-squad-preview.exe <folder>`
  renders sample squads to PNGs with the host's own drawing code (`src/plugin/SquadPanelDraw.cpp`).

## Gotchas

- **The long forms live in [docs/knowledge/gotchas](docs/knowledge/gotchas/index.md)** (OKF, since 2026-10-30, after
  motion blur bit a third time under a third name): symptom, cause, check, fix in place, the dates each bit. Read it
  when something looks familiar; add the date when it bites again. The bullets below are the short forms.
- Don't `cd` in Bash tool calls; it moves the session's working directory. Use absolute paths.
- An inline `python - <<'EOF'` loses one backslash of every pair (the Bash tool); write scripts with backslashes to a file.
- `Windows.h` macros eat ordinary names (`SetProp`, `far`, `near`, `min`, `max`): don't name ours after them; build `build-mono/` too.
- **Our settings file is `<exe dir>\ASI\<GAME>MEVR.ini` for the host and the mod** (`engine/SettingsFile`; the host's
  `MevrBridge::SettingsPath()` returns it, whatever the host ASI is called). **Read it with `core/Ini`** (or Win32's
  profile API): names case-insensitive, the first section and key win (as Win32), a UTF-8 BOM stripped, UTF-16
  decoded (the mod then appends no narrow blocks; `WritePrivateProfileStringW` handles it). Numbers in and out
  through `ini::Number` / `ini::Fixed` / `std::format` (`std::from_chars`/`to_chars`), never `strtod`/`sscanf`/
  `%f` (the C locale). Paths to text: `ini::Utf8(path)`, never `path.string()` (it throws for characters outside the
  ANSI code page, e.g. a Cyrillic folder); RmlUi opens files through our UTF-8 file interface (`menu/MenuUi`). An
  unwritable ASI folder: the mod warns once and retries every 10 s; our menu shows "Can't save settings" in its rail.
  Halcyon's own readers (`vr_config.cpp` `sscanf %lf`, `fprintf %.3f`, `xr_session.cpp` `atof`) still use the C
  locale, harmless while nothing calls `setlocale`.
- **New engine access goes through our reflection layer `src/ue3/`** (research/34): `ue3::ClassRef`,
  `FieldRef`, `FunctionRef`, `ue3::Params` + `ue3::runtime::Call`. LExSDKv2 is being removed (licence);
  don't add new uses of it. The mod's start logs `reflect: N of M checks agree` and writes
  `%LOCALAPPDATA%\MEVR\reflect-check.json` (our layer against LExSDKv2, while both exist).
  Since stage 2 no `src/game` file includes LExSDKv2: the player's shared handles are in `game/Player`,
  hook blocks are read through parameter handles (`FieldRef{functionRef, "ReturnValue"}`), engine
  strings through `ue3::memory` (`OwnedString`, `FreeString`), and LE2/LE3's few all-game reads through
  `engine/Legacy` until their layouts exist. Every class, field, function or parameter you resolve
  needs a row in `engine/ReflectCheck`; a failed lookup stands the feature down with a log line.
- **ProcessEvent skips any native with an opcode of its own** (`UFunction.iNative` != 0: `Trace`, `Destroy`, `SetLocation`, `SetOwner`...):
  `ue3::runtime::Call` clears the index for its call, so always call through it (gotchas/process-event-skips-opcode-natives).
- **Never call LExSDKv2's generated function wrappers** (e.g. `USceneCaptureComponent::SetFrameRate`).
  All 5,301 LE1 native wrappers do `FunctionFlags |= ~0x400` (meant `&=`), which permanently sets
  every flag on that UFunction. Find the UFunction yourself and call ProcessEvent directly.
  Class layouts, `StaticClass()` and `IsA()` are fine.
- **Never write Shepard's `Camera` or `God` bone** (VRIK): LE1's camera hangs off them through
  `SpaceBases`, so the view would follow the body (research/18). `engine/BonePose` skips them
  (`Request::keep`); any whole-body move must exclude them (research/14, "Camera hook bones").
- The exe's `.text` is encrypted on disk (EA DRM). Check RVAs statically with
  `tools/verify_rvas.py` (`.pdata` function bounds); byte checks only work in-process. The host
  saves the decrypted `.text` once per exe build to `%LOCALAPPDATA%\MEVR\dump`, at attach before
  any hook patches it (the `.txt` lists starts other ASIs already patched);
  `local-tools/splice_text.py` (gitignored, never published: it rebuilds a decrypted copy of the exe) turns it into
  a disassemblable copy.
  Local research data: never commit or share dumps or spliced exes.
- **Read native code with Ghidra, not dumpbin:** `tools/ghidra/decompile.ps1 0xC621E0` (RVA,
  `exe+0x...` or a name; `-Search`, `-Rename`/`-Comment -Record`) prints C, callers, callees and
  vtable slots. Setup and the MCP server: [tools/ghidra/README.md](tools/ghidra/README.md). One
  process at a time on the project (decompile.ps1 or the MCP server, never both).

- Windows PowerShell 5.1 turns native stderr (e.g. git progress) into errors under
  `$ErrorActionPreference = 'Stop'`. Check `$LASTEXITCODE` instead.
- msbuild isn't on PATH; use a VS developer shell or CMake.
