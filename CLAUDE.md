# ME VR Attempt

Native VR mod for Mass Effect Legendary Edition (BioWare's UE3 fork): a C++ ASI plugin loaded by
ME3Tweaks' LEBinkProxy, on top of LExSDKv2. One VR system for LE1/LE2/LE3, built LE1 first. Game
mechanics may change to fit VR (see `docs/trilogy-design.md`).

## Git

- **Commit freely.** The user gave standing permission (2026-10-07) to commit without asking.
  Commit at natural checkpoints (milestones, verified changes). No remote is configured; ask
  before pushing anywhere.

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
- LE1 body under the player ([research/29](research/29-body-follow.md), `core/ik/BodyFollow`, `game/BodyFollow`):
  first person, Shepard's whole body (the `Root` subtree; never `God`/`Camera`) turns by what the spine's 30
  degrees can't (the explore pawn faces where it runs) and slides under the view (feet that step after the
  head's pivot, `[Hands] leanM` 0.20, at most `bodyMoveMaxM` 0.75); the spine leans the head bone to the
  pivot. Re-anchors on start, recenter (host floor counter) and tracking jumps. `[Hands] bodyMove = 0`: the
  old spine-only lean. Log `torso:`; snapshot `"torso"`.
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
  left controller's flat yaw before Halcyon's `moveFollowsHead` turn (`WalkTowardHand` in `HalcyonBridge`). Logs
  `comfort:`, `vignette:`, `halcyon bridge: the mod turns ...`; snapshots `"comfort"` and a `vignette` layer.
- LE1 HUD (Halcyon's renderer, Stereo 2): the game's UI draws go to a body-locked HUD panel quad,
  and a hand reticle on the aim ray replaces the crosshair (`src/plugin/HalcyonOverlays.cpp`,
  [research/15](research/15-hud.md)). Scaleform's non-"over" blend modes (multiply...) go there too,
  through a tint texture the panel's alpha is made from (they used to stay in the eyes: the doubled
  wheel). While the game's weapon or power wheel is up, its part of the UI (`core/vr/WheelCrop`:
  guessed, then measured once per session) is cut out of the HUD panel and shown on a quad of its
  own at the left / right hand ("wheel"). Toggles: `MELEVR.ini` `hudPanel*` / `handReticle*`, our menu's
  HUD & sights and Advanced > HUD placement, or the classic menu's HUD tab; the wheel panel's in `[Squad]`.
- LE1 life-size stereo ([research/30](research/30-metric-stereo.md), `MELEVR.ini sfr2Metric`, on in our deploy):
  each Stereo 2 pass renders from its eye's real place (`xrLocateViews`, at the view's units per metre: Halcyon's
  50 x lean gain, life-size at 100), no convergence shift, swap ignored (pass 0 = left). In gameplay with the HUD
  panel taking the UI (mode 2) the GPU matrix gets each eye's own off-centre frustum and the submit declares it;
  the CPU matrices (culling, Umbra, HUD markers) keep Halcyon's symmetric window. Where UI is in the eye images
  (VR cine, panel off) it stays on the symmetric window (mode 1). Upstream's swap-on convergence was divergent
  (1.27 deg per eye: far things beyond infinity, a pistol at 0.5 m read at 0.68 m). Plan and logs
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
  output (the PS VR2's PC adapter has none): the gain is the gradation above paper white, not brightness.
- LE1 eye upscale ([research/32](research/32-ue3-vr-performance.md), `[Upscale]` in `ASI\LE1MEVR.ini`, off by
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
  eye, magnifying the OTHER eye's image, copied as Halcyon releases it: `testloop::SetReleaseTap`). The
  mod places them (`game/sights`, `[Sights]` in `ASI\LE1MEVR.ini`); the host draws them
  (`src/plugin/SightLayers.cpp`, pictures and shader in `SightPictures.cpp`; offline look:
  `mevr-sight-preview`). Scoped shots get the zoomed aim error via a `GetPlayerAimError` post-handler
  (`bIsZoomed` set for one native call only: never set it where the camera can see it, research/18).
  Alignment assist (research/21 section 8): raised to the face, the gun slides so the sight line runs
  through the aiming eye (`vr::sights::AssistTarget`/`AssistSpring`, `[Sights] assist*`, arm IK's
  `Request::gunShift`, measured without its own shift); hands within `[Hands] reachNearFaceM` of the eyes
  aren't reach-scaled by the arm-length calibration. The dot shows to one eye, the nearest within `[Sights] eyeBox`
  of the window. Shots converge on what the sight line hits (`Actor.Trace` each tick, `[Sights] converge`, log
  `hand aim: shot N:`). The sight sits on the model: a bone or socket named for a sight, else the height from the
  weapon mesh's bounds (`weapon::ModelInGunFrame`). Long guns without a rifle calibration borrow the pistol's grip
  frame. **Our quads are metric; Halcyon's Stereo 2 isn't** (research/21 section 9: eyes rendered `sfr2HalfEyeUU`
  from the midpoint, each image moved 1.27 degrees outward at submit by `sfr2Convergence` with swap-eyes): the fix is
  a metric stereo mode in Halcyon (a separate job), not per-eye corrections of our quads.
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
- Players' installer ([research/33](research/33-installer.md)): `MEVR-Setup.exe` (`tools/setup`, the in-game menu's
  RmlUi glass style; pages Welcome, Install, Optional content, Settings, Repair, Uninstall, Launch) over `src/install`
  (unit-tested): finds the game (Steam, EA app, a picked folder), checks the exe version and the OpenXR runtime,
  installs the monolithic ASI, fonts, menu, Halcyon's release `MELEVR.ini` only if missing (then deploy-asi's per-slot
  fixes), the VR game settings, and records it all in `ASI\MEVR-install.json` with backups and key-level ini edits, so
  Uninstall restores the game exactly. The ASI loader is our own `bink2w64.dll` (`src/loader`, `MEVR-Loader`: 79
  forwarded Bink exports, the SPI interface, the DRM wait by sampling `.text`; ctest `mevr-loader`), unless
  LEBinkProxy is there. Launch starts `MassEffect1.exe` with the launcher's arguments. Texture packs: the pack's page
  plus Mass Effect Modder's CLI, never bundled or auto-downloaded (Nexus terms). In game, Display > Optional content
  writes `%LOCALAPPDATA%\MEVR\setup.request`; the host's ExitProcess hook starts `ASI\MEVR-Setup.exe --request`.
  Test only on a fake game: `tools/setup/test-fake-game.ps1` (`scratch\fakegame`); pages offline:
  `MEVR-Setup.exe --preview <folder>`. Release: `tools/package-release.ps1 [-Zip]` into `dist\` (local only).
  **LExSDKv2 has no licence: a public release needs its author's permission.**
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
  once a player exists; `[SystemSettings]` in `ASI\LE1MEVR.ini` overrides any key (`apply = 0` off); log `system
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
  (`mevr-stack-sampler`): where its time goes by module and function, no redeploy. Live render levers
  (`plugin/RenderLevers`, [research/32](research/32-ue3-vr-performance.md)) in `[Performance]` of `ASI\LE1MEVR.ini`:
  `sharePass` (experimental; 1: the second eye renders on the first's scene renderer, no second visibility; 2: as 1
  but the second eye is a replay of the first's recorded D3D11 calls with the view's bytes substituted,
  `plugin/DrawReplay`), `umbraQueryWait` (0: the Umbra resolve never spins for a query result), `pCoresOnly`,
  **`perEyeOcclusion`** (default 1, 2026-10-23, research/35 attempts 34-35: the frame's second scene render gets its own
  copy of the engine's per-primitive occlusion history, a 0x48-byte TSet at the view state's +0xD10, swapped in around
  it, so it reads its own previous frame's queries instead of spinning 4-5 ms for the first eye's on the GPU); log
  `render levers:` (scene renders per frame, each one's time with its Umbra resolve's parts, the draw replay's counts);
  `tools/perf-ab.ps1 -Key <key> -Values a,b` alternates one and captures each; `tools/eye-diff.ps1` compares one
  frame's two eyes call by call (run it first: it says whether the replay's substitution covers the eye's constants).
  There is no depth pre-pass to skip: `0x76C1D0` is the base pass (research/32).
- **Console:** `tools/console.ps1 "UMBRA OCCLUSIONCULLING"` writes `console.request`; the mod (`game/ConsoleRequest`,
  hot-reloaded) runs each line through `PlayerController.ConsoleCommand` on the game thread and writes what it
  printed to `console.result` (log `console:`). Flat or in VR, wherever there is a local player: A/Bs without a restart.
- `testloop.status` (JSON, once a second): recording, replay, last snapshot, `framesRunning`.
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

- Don't `cd` in Bash tool calls; it moves the session's working directory. Use absolute paths.
- **New engine access goes through our reflection layer `src/ue3/`** (research/34): `ue3::ClassRef`,
  `FieldRef`, `FunctionRef`, `ue3::Params` + `ue3::runtime::Call`. LExSDKv2 is being removed (licence);
  don't add new uses of it. The mod's start logs `reflect: N of M checks agree` and writes
  `%LOCALAPPDATA%\MEVR\reflect-check.json` (our layer against LExSDKv2, while both exist).
  Since stage 2 no `src/game` file includes LExSDKv2: the player's shared handles are in `game/Player`,
  hook blocks are read through parameter handles (`FieldRef{functionRef, "ReturnValue"}`), engine
  strings through `ue3::memory` (`OwnedString`, `FreeString`), and LE2/LE3's few all-game reads through
  `engine/Legacy` until their layouts exist. Every class, field, function or parameter you resolve
  needs a row in `engine/ReflectCheck`; a failed lookup stands the feature down with a log line.
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
