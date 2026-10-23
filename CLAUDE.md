# ME VR Attempt

Native VR mod for Mass Effect Legendary Edition (BioWare's UE3 fork): a C++ ASI plugin loaded by
ME3Tweaks' LEBinkProxy, on top of LExSDKv2. One VR system for LE1/LE2/LE3, built LE1 first. Game
mechanics may change to fit VR (see `docs/trilogy-design.md`).

## Git

- **Commit freely.** The user gave standing permission (2026-10-07) to commit without asking.
  Commit at natural checkpoints (milestones, verified changes). No remote is configured; ask
  before pushing anywhere.

## Licensing rules (GPL-3.0 project since 2026-10-08)

- The project is GPL-3.0 (`LICENSE`, `NOTICE.md`). Code from GPL-3.0-compatible sources may be
  copied and modified: GPL-3.0 (Halcyon's MassEffect1/2/3-VR, Overlord, h2-mod, Legendary
  Explorer) and permissive (REFramework, vrframework, MinHook, ImGui). Keep their copyright and
  license notices, and list them in `NOTICE.md`. GPL-2.0-only code is **not** compatible.
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
  Shift + `\` its own. Our own VR hotkeys are off on LE1.
  Recenter is yaw-only as upstream, except that a recenter the player asks for also removes a badly
  tilted base space (rolled past 90 degrees, or tilted past `recenterTiltDeg` for 1.5 s: SteamVR's
  upside-down start; [MEVR] `RecenterOrientation`, `core/vr/Recenter`, log `[MEVR-RECENTER]`).
- **The player's menu is ours** (since 2026-10-23, [research/28](research/28-main-menu.md)): the approved
  glass board in RmlUi 6.3 (`external/RmlUi`, FreeType in `external/freetype`; static, in the host,
  LE1 only, `MEVR_MAIN_MENU`). Holding both sticks or `\` opens and closes it; Resume or B close it. It's a
  quad fixed in the room where it opens (`[Menu] distanceM`, `widthM`); the game sees no input, the mod no
  controllers, and on foot or in the Mako the mod pauses the game (`[Menu] pause`, HostServices v16,
  `game/MenuPause`: `WorldInfo.PauseGame` like the game's weapon wheel). Pointer: a controller's aim ray is
  the mouse (trigger clicks, right stick scrolls, haptic ticks); the pad or left stick moves the focus, A
  activates, B backs out then closes; on Display holding A compares the colour. Sections: Comfort, Controls,
  Body & calibration (Start calibration opens `plugin/Calibration`), Display, HUD & sights, Advanced
  (Halcyon's settings in sub-tabs). Every row drives a real setting (`plugin/MainMenuSettings`: our
  `LE1MEVR.ini` key by key, Halcyon's live config saved with its own `SaveProfile` when the menu closes;
  never a reset written over a profile) or is marked Soon. Code: `src/plugin/menu` (game-independent:
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
- `reference/`: read-only third-party clones (gitignored). Recreate with `tools/fetch-references.ps1`.
- `notes/game-inventory.md`: exe versions and hashes from `tools/inventory-game.ps1`. Re-run after
  game updates. All three exes are 2.0.0.48602 (the build LExSDKv2's RVAs target).
- `mods/`: raw mod downloads (gitignored). Unpack `.m3m` merge mods with `tools/extract-m3m.ps1`.
  The scripts we build on live in `src/script/` (e.g. `src/script/le1/massfps-base/`, MassFPS by
  Aphar, fully permissive).
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
  `Mod.cpp` retries every second before installing the ProcessEvent hook.
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
  is in `game/GamepadMapping` (unit-tested).
- Physical grenades and melee (LE1, `game/Throwables` + `core/ik/Throwing`, research/20,
  `[Throwables]`): a new grip squeeze at the grenade slot (left side of the chest) takes one, a fast
  release throws it (the game's `UseGrenade`; its disc re-aimed from the hand in a post-handler of
  `BioGrenade.LaunchGrenadeAtPoint`), a slow one puts it back; a fast stroke with an enemy in reach
  is `TryMelee`. Claimed squeezes run before holsters; a left squeeze in the support grip's zone is the
  support grip, never a grenade. B/X stay unless
  `meleeButton`/`grenadeButton = 0` (host v14).
- No cover snapping, physical crouch (LE1, VR on foot; `game/Posture` + `core/ik/Posture`, research/23,
  `[Hands] noCover`, `physicalCrouch`, `crouchBelow`/`standAbove`/`crouchDwellS`): ME1 takes cover by
  itself (BioPawn's native test, no button; A only leaves it). `engine/cover_acquire` pre-hooks that
  test (`0xAB1430`, mod side) and skips it for the player's pawn; a cover already taken is left with
  `TryExitCover`. A real crouch below 0.75 of the standing eye height (hysteresis 0.85, 0.2 s) is
  `BioPawn.ShouldCrouch` (ProcessEvent), on physical changes only: L3 still toggles in between.
- LE1 level camera (`game/view_level`, `core/vr/ViewLevel`, [research/24](research/24-view-level.md)):
  each shot's recoil goes into the player controller's rotation (ControlRotation) and the rifles' pitch
  kick never fades (`RecoilFadeSpeed=0`); with the stick's pitch off (`decoupledPitch`) the world tilted
  (+30 degrees after 204 shots). A `PlayerTick` post-handler (before the camera update) holds its pitch and
  roll at 0 on foot while the hand aims, and drops aim assist's zoom-snap target; not in cover, the
  scope, the Mako or cinematic modes. The recoil is a hand pulse instead. `[Hands] levelCamera`,
  `recoilHaptic`; log `view level:`. Aim assist's friction and adhesion (`SFXModule_AimAssist.ViewFriction`
  / `ViewAdhesion`, which slowed and bent the stick's turn near enemies) are zeroed by CallFunction
  post-handlers under the same conditions (cover included; not the sniper scope or the Mako):
  `[Hands] aimAssist = 0` (1 keeps them), log `aim assist:` (research/24 section 7).
- LE1 guided calibration ([research/25](research/25-calibration.md), HostServices v15): our menu's Body &
  calibration > Start calibration (or the VR hands tab's Calibrate, or a one-time prompt for uncalibrated
  controllers) runs about a minute in the headset
  (`plugin/Calibration`; look in `plugin/CalibrationDraw`, the main menu's style; offline look:
  `mevr-calibration-preview`): stand tall, T pose, arms ahead, crouch, a pistol orb and two rifle orbs aimed
  at three targets (captured on a 1 s hold, no buttons; B back a step, both sticks held cancel), a red-dot
  check, save. Why an aim isn't captured is on the panel and the board; an aim off the other two is redone alone,
  and after two tries the agreeing pair is kept (`vr::calibration::AimSet`; rifle 4.5 degrees, pistol 3; research/25
  section 9); a cyan ring marks each hand's grab point (`[Calibration] grabOffset`). Solved in `core/vr/Calibration` (unit-tested): the gun frame (eye-to-target line, level) in
  the right GRIP frame, the line's offset from the grip, the eye, the rifle's foregrip, left trim and stock.
  Saved per controller type (interaction profile + headset) as `[Calibration.<key>]` in `ASI\LE1MEVR.ini`;
  `game/BodyCalibration` applies it: the barrel follows grip * trim (not the aim pose), the gun's sight is
  moved onto the calibrated line (`gunShift`, the assist on top), the foregrip point takes the left grip,
  two-hand aim keeps the calibrated hands line, the shoulder zone, the crouch thresholds, the holsters'
  eye height. While it runs the game sees a neutral pad and the mod no controllers. `[Calibration] use = 0`
  or no section: everything as before. Log `calibration:`.
- LE1 body holsters (`game/Holsters`, `core/ik/BodySlots`, research/19): a right-grip squeeze at a
  body slot draws that weapon through `SFXGameModeDefault.SwitchToWeapon` (ProcessEvent). The
  right grip holds the gun (`gripHolds = 1`): let go at its slot to holster (`TryHolster`),
  elsewhere it goes back after `releaseGrace`; the grip is never RB, RB (power wheel / mapped
  power) is the right stick click (`powerWheel`). Contact haptics while the hand is in a slot
  (host `core/vr/Haptics`); the slots scale with the standing eye height from the floor (host
  `core/xr/Floor`, STAGE). Squad weapons: below. Settings: `[Holsters]` in `ASI\LE1MEVR.ini`.
- LE1 HUD (Halcyon's renderer, Stereo 2): the game's UI draws go to a body-locked HUD panel quad,
  and a hand reticle on the aim ray replaces the crosshair (`src/plugin/HalcyonOverlays.cpp`,
  [research/15](research/15-hud.md)). Scaleform's non-"over" blend modes (multiply...) go there too,
  through a tint texture the panel's alpha is made from (they used to stay in the eyes: the doubled
  wheel). While the game's weapon or power wheel is up, its part of the UI (`core/vr/WheelCrop`:
  guessed, then measured once per session) is cut out of the HUD panel and shown on a quad of its
  own at the left / right hand ("wheel"). Toggles: `MELEVR.ini` `hudPanel*` / `handReticle*`, our menu's
  HUD & sights and Advanced > HUD placement, or the classic menu's HUD tab; the wheel panel's in `[Squad]`.
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
- LE1 gun sights ([research/21](research/21-sights-and-scopes.md), HostServices v13): a collimated red
  dot / holo per gun (a quad 50 m out along the sight line, `eyeVisibility` = the eye looking through the
  window) and a 4x picture-in-picture scope on the sniper rifle (a disc at the eyepiece for the aiming
  eye, magnifying the OTHER eye's image, copied as Halcyon releases it: `testloop::SetReleaseTap`). The
  mod places them (`game/sights`, `[Sights]` in `ASI\LE1MEVR.ini`); the host draws them
  (`src/plugin/SightLayers.cpp`, pictures and shader in `SightPictures.cpp`; offline look:
  `mevr-sight-preview`). Scoped shots get the zoomed aim error via a `GetPlayerAimError` post-handler
  (`bIsZoomed` set for one native call only: never set it where the camera can see it, research/18).
  Alignment assist (research/21 section 8): raised to the face, the gun slides so the sight line runs
  through the aiming eye (`vr::sights::AssistTarget`/`AssistSpring`, `[Sights] assist*`, arm IK's
  `Request::gunShift`, measured without its own shift); hands within `[Hands] reachNearFaceM` of the eyes
  aren't reach-scaled by the arm-length calibration.
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
  preselects through the HUD's kept stick angle (`BioSFHandler_HUD.nLastAngle`). `style = custom`:
  our drawn menu (left stick = squadmate, right stick flick = weapon, let go or A = give it, B =
  cancel; `plugin/SquadPanel` draws it with its own ImGui context as the "squad" quad;
  `gamepad::kFlagSquadMenu`, v11). `game/SquadWeapons` (mod) drives both; `core/vr/SquadMenu` is the
  shared, unit-tested part (the native ring's tables in `squad_menu::wheel`). Settings: `[Squad]` in
  `ASI\LE1MEVR.ini`.
- Trilogy check: `build-trilogy/` (configured with `-DMEVR_BUILD_LE2=ON -DMEVR_BUILD_LE3=ON`) must
  keep building all three games. Per-game field spellings go in `src/engine/GameAdapter.hpp`;
  per-game addresses sit next to their use (e.g. `engine/SceneView.cpp`).
- Offline tests: `cmake --build build --config Release --target mevr-tests`, then
  `ctest -C Release --test-dir build`. Put game-independent math there.
- `tools/vr-game-settings.ps1` sets VR-friendly GamerSettings.ini values (VSync, motion blur,
  film grain off, HDR to SDR); `-Restore` puts the user's originals back. On LE1 it also does what
  Halcyon's installer (`MELE-VR.bat`) does: a borderless window (a bordered one is capped at the
  monitor's height, which broke stereo), dynamic shadows off, frame smoothing off, and its
  near-square stereo target (`-Quality`, default Performance 3072x3188), mirrored into
  `MELEVR.ini`. The deploy installs Halcyon's *release* `MELEVR.ini` (its tuned code defaults),
  not the older `src/halcyon/le1/runtime/MELEVR.ini`, which pins stale values.
- The mod connects to Windows' active OpenXR runtime (Oculus, or SteamVR for Steam Link). The
  user currently uses Steam Link / SteamVR.
- The game's D3D11 device is SINGLETHREADED: no deferred contexts, and all D3D/XR work happens
  on the render thread inside the host's Present callback. Its backbuffer is FP16 (scRGB, linear).
- Quitting from the headset's menu (session EXITING) must turn VR off, not recreate the session.
  The host hooks ExitProcess so XR shuts down before the Oculus DLLs unload.
- Our targets build with `/W4 /WX /permissive`. The SDK's generated code emits warnings
  (C4315, C4789) that aren't ours.
- The LExSDKv2 submodule tracks the `RVA` branch (hardcoded RVAs for 2.0.0.48602). The plugin
  fails closed on any other exe version.

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
- **Replay:** `tools/replay.ps1 [-Path f] [-Loop] [-Inputs] [-Wait]` / `-Stop` writes
  `replay.request` (`<path|latest|stop> [loop=1] [inputs=1]`). The recorded head (the runtime's
  FOVs kept) and controller poses stand in for the live ones; buttons and sticks only with
  `-Inputs`. Replay + snapshot = look at a recorded pose: `tools/replay.ps1 -Loop`,
  `tools/snapshot.ps1 -Delay 2`, `tools/replay.ps1 -Stop`.
- **Player triggers:** End = snapshot, Home = recording on/off (game focused); a quick tap of both
  stick clicks = snapshot (both down within 100 ms of each other and up within 300 ms of the first
  press; held 400 ms they open our main menu instead, never both: `gamepad::UpdateStickChord` in
  `HalcyonBridge`; L3 is held back 100 ms so the tap is never a crouch, and RB from R3 starts only
  after 100 ms alone); our menu's Advanced > Diagnostics (Snapshot, Record) and the classic menu's VR
  hands tab ("Test loop": snapshot in 3 s, record, replay the last recording). There's no controller
  chord for recording (both grips are the gun and the squad menu since 2026-10-22).
- **Frame timing** ([research/22](research/22-performance-levers.md)): `tools/perf-probe.ps1 [-Seconds 20
  -Delay s -Tag x]` writes `perf.request` (or our menu's Diagnostics, or the VR hands tab: "Measure frame
  timing"); `plugin/PerfProbe`
  records each frame's steps on the render thread (time, busy time, draws, GPU timestamps mapped onto the
  CPU clock, access violations) and each game-thread tick into `perf\<stamp>-frames.csv`, `-ticks.csv` and
  `-probe.json`, then `python tools/perf-probe.py [--latest]` says whether the render thread, the GPU or
  the game thread sets the pace. Seams: `MevrBridge::PerfPresentScope` / `PerfPass0Captured` /
  `g_perfDraws` and the wrapped `xrWaitFrame` / `xrBeginFrame` / `xrEndFrame`. `tools/perf-monitor.ps1`
  adds whole-GPU load; `tools/sample-render-thread.ps1` samples the render thread's stack from outside
  (`mevr-stack-sampler`): where its time goes by module and function, no redeploy.
- `testloop.status` (JSON, once a second): recording, replay, last snapshot, `framesRunning`.
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

- Don't `cd` in Bash tool calls; it moves the session's working directory. Use absolute paths.
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
  `tools/splice_text.py` turns it into a disassemblable copy.
  Local research data: never commit or share dumps or spliced exes.
- **Read native code with Ghidra, not dumpbin:** `tools/ghidra/decompile.ps1 0xC621E0` (RVA,
  `exe+0x...` or a name; `-Search`, `-Rename`/`-Comment -Record`) prints C, callers, callees and
  vtable slots. Setup and the MCP server: [tools/ghidra/README.md](tools/ghidra/README.md). One
  process at a time on the project (decompile.ps1 or the MCP server, never both).

- Windows PowerShell 5.1 turns native stderr (e.g. git progress) into errors under
  `$ErrorActionPreference = 'Stop'`. Check `$LASTEXITCODE` instead.
- msbuild isn't on PATH; use a VS developer shell or CMake.
