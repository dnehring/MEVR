<!-- SPDX-License-Identifier: MIT -->
# Release checklist (LE1, first public release)

What stands between main and a release that feels premium, as of 2026-11-04. Compiled from `notes/next-vr-session.md`,
`notes/todo-vrik.md`, `notes/cpu-frame-time-plan.md`, research/14-53, `docs/troubleshooting.md`, the gotchas bundle, the
menu (`assets/menu/main.rml`, `src/plugin/MainMenuSettings.cpp`), the shipped defaults (`src/game/HandCalibration.cpp`)
and the packaging (`tools/package-release.ps1`, `src/install`, `tools/setup`). Tick items here as they land; add the commit.

Who does it: **[H]** needs a person in a headset (comfort, feel, what can't be captured); **[C]** code or docs work
that can be done and measured without one (the fake headset, snapshots, probes); **[D]** a decision for the owner.

The two facts behind most of this list:
- **Most features since about 2026-10-30 were checked only on the fake headset** (SteamVR's null driver: no real head
  motion, no compositor cost, controllers only from replays), and several are on by default.
- **Several mod calls did nothing until 2026-11-03** (`Actor.Trace`, `SetRotation`, `Destroy` through ProcessEvent:
  gotchas/process-event-skips-opcode-natives). Shot convergence, grenade landings, the Mako's aim point and the comfort
  turn's pawn rotation have never been seen working in a headset.

## 1. Release blockers

### Must fix
- [ ] **[C] Motion blur comes back after a fresh launch.** The mod logs "a new level: applied again" and the blur is still
  on; each eye smears its own way (the player's "shimmering floor", "assets shift each its own way"). The workaround
  today is two console lines by hand. Fix, then verify from a cold Steam launch with the frame-flip count of research/35
  attempt 49. (gotchas/motion-blur-in-stereo, bitten 4 times, last 2026-11-02)
  **In: b3778d8** (each level gets the toggles again 3 s and 15 s after it becomes the world). **Unproven:** on 2026-11-04
  two cold Steam launches into the Mako autosave were sharp both with and without it, so the failure didn't reproduce
  on the fake headset (the gotcha has the details and two measuring traps). Next: a headset session's first minutes
  after a cold launch, or a reproduction; until then the two console lines stay the check.
- [x] **[C] Developer channels open in the player build.** Done, 38d35b5: the players' build ignores the request files
  unless `[Debug] requests = 1` (`core/DevChannels`); development builds unchanged. `console.request` runs any console command (`god`, `open`,
  `set`, `mevr vehicle enter`, which must never run before the Mako drop) for any local process
  (`src/game/ConsoleRequest.cpp:858-918`); the test loop's request files include replay, which swaps the live head pose
  for a recording. Gate both behind a `[Debug]` switch that is off in the release, or compile them out of the monolithic
  build.
- [x] **[C] Unbounded disk use.** Done, 38d35b5: budgets per folder in the players' build (2 GB / 512 MB / 256 MB,
  `core/DiskBudget`), a 30-minute cap on open-ended recordings, Halcyon's log rotated at 16 MB. Snapshots, recordings and perf captures are never pruned (44 GB of snapshots on the dev
  machine); a quick tap of both stick clicks takes a snapshot and a recording has no length cap
  (`src/plugin/TestLoop.cpp:1109-1124`). Halcyon's `MELEVR_Log.txt` never resets (README "Reporting a bug"). Cap or prune
  all of them; consider a single-file log size limit.
- [x] **[C] Halcyon's classic menu breaks the design.** Done, e7f459b and 38d35b5: head aim and Mako head aim default
  off in its code (so Reset keeps them off), and the players' build's menu has no replay. Still reachable as the
  developer fallback; its wording is a polish item (section 4). Reachable by Ctrl/Shift + `\` and two rows of our menu; its
  per-item Reset puts "Head drives aim" back ON (`vr_config.h:59`, `vr_menu.cpp:1012`) and it exposes Mako head aim, the
  EXPERIMENTAL camera hold, Depth pop and "Test loop (for Claude)". Hide it from players (dev builds or a `[Debug]` switch)
  or make its resets use our deploy's values.
- [x] **[C] Menu row bug: Hacking screen only switches one way.** Done, f1f8d8b (the galaxy session). `Settings::Read` doesn't read `[WorldUi]`
  (`MainMenuSettings.cpp:204`), so after the write flushes the row shows "On my wrist" again and a click on it is ignored.
- [x] **[C] Licence notices.** Done: NOTICE and Lucide in ac66710; CMake adds the LExSDKv2 submodule only when
  something links it (the players' build configured, built and passed its tests without it, 2026-11-04). NOTICE.md calls `src/install` MIT, but `src/install/Toc.cpp` is GPL-3.0-only; Lucide's ISC
  text isn't among the licences `package-release.ps1` copies. The GPL source release still needs the unlicensed LExSDKv2
  submodule to configure (`CMakeLists.txt:35` adds it unconditionally): make it optional when `MEVR_LE1_LESDK=OFF`
  (research/34 stage 6).

### Must run once before tagging
- [ ] **[H] Quest 3 over Meta Link, one full session.** The main audience; never run on Meta's runtime with the current
  renderer (the Quest was last used over Steam Link on 2026-10-21). Covers: the colour preset and the system name that keys
  calibration, frame times, per-eye quads (`eyeVisibility`) honoured, haptic frequency, floor height, controller bindings.
- [ ] **[H] Our own loader in the real game, installed by MEVR-Setup on a clean install**, Steam and EA app builds: first
  launch, Repair after Steam's "verify files", Uninstall restoring the game exactly. The dev install still runs
  LEBinkProxy (research/33:401-402).
- [ ] **[H] The headset pass over everything on by default and never seen by a person** (section 2). Record the session
  (`tools/record.ps1`) so problems can be replayed offline.
- [ ] **[H+C] Real-headset frame times.** The last fpsVR numbers on the PS VR2 (CPU p90 16.4-22.1 ms, GPU p90 13.5 against
  11.1) predate `keepBoundShaderStates`; nothing newer exists for the default path. Re-measure at Eden Prime's heavy cell,
  the Presidium and a fight, then settle the defaults in section 3.

## 2. On by default, never seen by a person in a headset

Each needs a headset verdict, or ships off. Source: notes/next-vr-session.md steps 41-49 unless noted.

- [ ] **[H] 3D cinema screen** (conversations and cutscenes): comfort, the dialogue wheel's readability (about 0.9 degrees
  tall), the pointer on it (research/52).
- [ ] **[H] Game screens in the room** (`[WorldUi] screens = all`, default since 2026-11-04): shop, loot, journal, codex,
  map, the wrist minigame in a real mission; the pointer's click. Listed but untested: PartySelect, Options, Save, Load,
  GameOver, Specialization and others (research/53:53-81).
- [ ] **[H] Fingers**: palm side (could bend backward), curl amount, the index off the trigger, the thumb (research/43).
- [ ] **[H] Wrist status and the gun's heat panel**: readability, offsets, the sniper scope hiding the gun panel
  (research/41).
- [ ] **[H] Vent**: whether real racks register; the heat scale (`ventMaxHeat`, the log says what heat each overheat
  happened at) (research/45).
- [ ] **[H] Using things with A by gaze** and the label at the object. The player's last report: "the prompt is far too
  small and A doesn't reliably work" (research/40).
- [ ] **[H] Sights step 49**: the assault rifle's red dot ("not aligned well at all", 10-25; "doesn't line up perfectly",
  10-27), the sniper's eyepiece and shorter pull, the 3.5x/7x flick, ADS without flicker.
- [ ] **[H] Scope capture** (on since 2026-11-04): sharp, steady, upright; its cost in a dense scene (+0.5 ms measured
  only in a light one) (research/39).
- [x] **[H] Mako controls and the cockpit** (both on): the fixes after the player's two drives (stabiliser, spin
  direction, lever notch, seat, hands on the handles) were checked on the fake headset only; comfort of the full pitch and
  roll; the turret following the hand; a doubled crosshair (research/44, 46, 47).
  **Validated by the owner** in the headset on 2026-11-03 ("I did validate the mako play yesterday", 2026-11-04).
- [ ] **[H] Galaxy map in the CIC**: going to the galaxy level and back out (research/51).
- [ ] **[H] Grenades and melee**: landings were never right before 2026-11-03; false melee strikes while aiming or reloading.
- [ ] **[H] Calibration's redesigned rifle step** (it looped for the player on 2026-10-23); without it long guns borrow the
  pistol's grip frame (research/25).
- [ ] **[H] Snap turn and the vignette**, walking toward the hand, recenter fixing a tilted start (research/31).
- [ ] **[H] Life-size stereo's verdict**: sizes, near-object comfort, fog/AO/light shafts per eye (research/30).

## 3. Defaults to decide

The owner decided these on 2026-11-04 (asked one by one); the rows below say what each answer changed.

- [x] **[D] `exploreStance`** is off until a headset confirms the redesign: confirm it, or ship off and hide its row.
  **Decided: ship off, hide its row** (the setting stays in the ini). Done: the row is gone from Controls > Weapon drawn.
- [x] **[D] Scope capture's speckle**: ship `scopeCapturePost = 1` (grade, speckle) or 0 (clean, no grade); give it a row.
  **Decided: ship with the grade** (`scopeCapturePost = 1`, today's default); the speckle stays a known issue in the README.
- [x] **[D] `reticle = game` can't work**: the game frees the scope art's textures, so every scope falls back to the
  duplex (research/21:863-870). Build one of the two noted routes, or make `duplex` the default.
  **Decided: `duplex` the default**, the broken choice dropped for now. Done: `reticle = duplex` in the code and the ini text
  (an old file's `game` is kept: it falls back to the duplex anyway).
- [x] **[D] `sharePass = 5`** (one render a frame, 20% at the heaviest cell, symmetric window) stays experimental, or
  becomes the default for over-budget machines after the real-headset numbers.
  **Decided: off, experimental**; revisit after the real-headset numbers.
- [x] **[D] Starting resolution.** The setup defaults to Performance (3072x3188) unless an old Halcyon log gives the eye
  size; its headset Detect never ran. Choose per headset and GPU class; FSR at x0.85 as the fallback.
  **Decided: the headset's own eye size**, read at the first VR start and set once; Performance until then. Done: the
  setup's new default Automatic (`auto`) records `eyeSizeAtFirstStart` in the manifest; the host's `FirstStartEyeSize`
  sets it once (`[Display] eyeSizeSet`, log `first VR start:`), for the launches after it. Never run in the game.
- [ ] **[D] Equipment and character record lose their 3D models in the room**: exclude them from `screens = all` until
  fixed.
  **Open:** the owner saw both screens flat and in the room (snapshots 2026-11-04 20:30-20:31) and chose the omni-tool instead:
  every personal screen projected from the left wrist with a rail of the screens beside it (research/54, 7d727b2; built,
  [verify] in the game). The figure itself (the concept art's squad holograms on a disc) is still to come: a scene capture in
  the game's UI world.
- [x] **[D] Cockpit on by default with no opt-out**: the setup always installs its package (and edits `PCConsoleTOC.bin`);
  add an optional-content checkbox and a menu row, or ship it off.
  **Decided: keep as is**: always installed; the menu's "Sit in the cockpit" (e38c381) turns it off.
- [x] **[D] Cutscene frame cap** (`cineFpsCap = 1`: cutscenes at 45 fps at 90 Hz). Less visible on the 3D screen; decide
  for the In VR choice.
  **Decided: keep it on** (Halcyon's default) for the 3D screen and In VR.
- [x] **[D] `Walk toward: Body`** turns Halcyon's `moveFollowsHead` off, which brings back its "look left, move right"
  bug (`vr_menu.cpp:1025-1029`). Remove the choice.
  **Decided: remove Body.** Done: Where I look and Left hand only.
- [x] **[D] "Look speed"** (head rotation not 1:1) is a nausea risk on a plain slider: remove it or move it to Advanced
  with a warning.
  **Decided: remove the row** (it had moved to Advanced > Tracking with a warning in e38c381); `MELEVR.ini` still sets it.

## 4. Premium feel

### First minutes
- [x] **[C] A welcome and controls primer in the headset.** `firstRunDone = 1` suppresses Halcyon's welcome and ours
  doesn't exist; the only hint is the calibration prompt. Nothing teaches holsters, grenades, the left-trigger wheel,
  powers on R3, the vent, B/X melee and throw, or the Mako's lever and stick. A Controls page in the menu with both
  controllers drawn, and a first-run card that points to it.
  **Done 2026-11-04 (e38c381):** the menu's Controller guide (texts from the settings, unit-tested), opened once on a first run (`[Menu] controlsShown`); when it fires was checked only by reading the code.
- [x] **[C] Installer: the game must have run once.** Without `GamerSettings.ini` the setup skips all game settings; the
  window stays bordered (stereo breaks) and the resolution stays the template's 4096x4250. Detect it in the setup and in
  the headset, or write the settings anyway.
  **Done 2026-11-04 (9e6768b):** the settings wait in the manifest, "Start the game once", Repair applies them; tested on the fake game, the real launch not run.
- [x] **[C] Upgrading from Halcyon's MELE-VR**: an existing `MELEVR.ini` keeps `vrMode`, `fpEnabled 0`, `firstRunDone 0`
  and the mirror rate, so a player can start in third person under Halcyon's welcome ("INSERT opens this menu"). Normalise
  those slots on install.
  **Done 2026-11-04 (9e6768b):** those keys normalised per slot where upstream wrote them, recorded for Uninstall; unit-tested and on the fake game.
- [x] **[C] SteamVR must be up before the game** or VR stays off for the run: say so in the setup's Launch and the README,
  or start the runtime from Launch.
  **Done 2026-11-04 (9e6768b, 11c626e):** Launch starts SteamVR first when it is the runtime (waits up to 60 s; never run for real), other runtimes get a line; the READMEs say so.

### Conversations and cutscenes (the core of Mass Effect)
- [x] **[C] A readable dialogue wheel**: its own panel near the screen or the hand instead of 0.9 degrees on the 3D
  screen; the pointer on it (research/52:94-96, research/42:129-133).
  **Improved 2026-11-04 (db55151):** the conversation movie at its authored size on the 3D screen: the wheel 1.75x larger, its options' capitals 0.7 degrees (measured on the fake headset, research/52). A panel of its own and the pointer on it are not built; not yet read by a person.

### Hands, weapons, body
- [ ] **[C] The assault rifle's dot held two-handed** sits off the sight (the one-handed shift is kept).
  Not changed (2026-11-04): the cause (research/21 cause 4) is unsettled and the fake headset has no rifle calibration to reproduce it; needs the heartbeat lines from a headset session first.
- [x] **[C] Crouch drops the view twice**: the game's crouch camera (45 units, 35 aiming) on top of the real crouch; the
  constant eye covers standing modes only (research/23:153-156, research/38:272-273).
  **Built 2026-11-04 (f6997d4):** a physical crouch keeps the standing eye in the crouch camera modes (`[Hands] crouchDrop = 1`: the old drop); the flag reaches the host in the game, the combat crouch itself is [verify] (research/38).
- [x] **[C] Shots from a muzzle through a wall** fire from the far side (`GetPulledInMuzzleLocation` unused,
  research/14:542-544).
  **Built 2026-11-04 (6318954):** the shot starts 2 units short of what lies between the pawn's start and the muzzle; [verify] in the game.
- [ ] **[C] Seated play**: no seated preset; holsters and crouch come from a standing recenter (research/19:570-571).
- [ ] **[C] Legs moonwalk** when strafing or backing up; Shepard is left behind past 0.75 m (research/29:160-171).
  **Built 2026-11-04 (ceb6edd):** moving, the legs keep the run and only the spine (up to 60 degrees, `legsTwistDeg`) turns to
  the view; [verify] in the game. Room-scale (Shepard following you past 0.75 m) is designed in research/29, not built.
- [ ] [C] The grenade isn't in the hand while held; the vent has no animation; no visible gun kick.
  **Built 2026-11-04:** the gun kick (799ae64: the model kicks up and back by the gun's own recoil, aim and sights unmoved;
  `[Hands] gunKick`, `gunKickScale`, a menu row) and the vent's rack (e7a7955: a part of the gun's own model follows the hand
  back and springs forward; the bones per model are guesses, logged with each model's bone list; `[Vent] animate`). Both
  [verify] in the game.
  **The owner 2026-11-04: build the gun kick, the vent's rack and the legs (the moonwalk and the 0.75 m cap) for the first
  release**; the grenade in the hand not chosen.

### Menus and screens
- [x] **[C] Player-facing words in our menu**: "Halcyon's ladder", "the renderer's own settings", "Back to Halcyon's
  defaults", "explore pose", raw "Eye distance 3.4" and "Convergence 0.012", "VR mode: Mono / Stereo / Depth" (untested with
  VRIK), "Quest field-of-view match (no effect on SteamVR)", Desktop mirror "Normal" (every 8th frame), "Record a session ...
  to replay" (developer feature). Descriptions missing on Pistol sight, HUD Distance/Size, Place, Left trigger.
  **Done 2026-11-04 (e38c381).**
- [x] **[C] Duplicate controls**: Life-size movement and Lean gain both write `leanGain`; HUD Size and HUD panel Width both
  write `hudPanelWidthM`; Watch them overlaps Conversations/Cutscenes.
  **Done 2026-11-04 (e38c381):** one control per setting; Conversations and Cutscenes each a three-way row.
- [x] **[C] "Move and resize a screen ... in the classic renderer menu's HUD tab, for now"** (`main.rml:365`): build it in
  our menu or drop the row.
  **Done 2026-11-04 (e38c381):** Advanced > Game UI moves and sizes the conversation wheel and the mission overlays.
- [x] **[C] Rows for what is file-only today**: Mako controls, cockpit, galaxy hologram, game screens in the room, wrist and
  gun panels, vent, fingers, gaze labels, scope capture's post. A player who dislikes one has no way out but the ini.
  **Done 2026-11-04 (e38c381):** ten on/off rows.
- [x] **[C] About shows no version or commit** (`main.rml:412-413`); reports need it.
  **Done 2026-11-04 (9ab2cf1):** "MEVR 0.1.0 · commit <hash>".
- [ ] **[C] Pointer on the game's screens**: clicks need the window in front and a settled hover; the galaxy map's travel
  box ignores the click in window mode (research/51:222); the first move flips the button glyphs to mouse and keyboard.
- [ ] [C] Overlay quads have no depth: the radar draws over the throttle; the galaxy disc over the console.

### Performance
- [ ] **[C] Walking spikes and Mako streaming hitches** (54 ms) are unmeasured or unexplained (cpu-frame-time-plan:25-26,
  research/47:1118-1131).
- [ ] [C] A big fight measured (research/50 had only Saren's arena).
- [ ] [C] Levers measured but off: `minScreenRadiusPx 8` (~0.3 ms, invisible), `CompositeDynamicLights` (0.3-0.5 ms).
  **The owner 2026-11-04: `minScreenRadiusPx 8` on by default** (done, host: `RenderLevers`).

## 5. Release engineering and documentation

- [x] **[C] One version number.** 0.1.0 is a literal in `src/plugin/Entry.cpp:37`, `src/install/Updates.hpp:14`,
  `src/loader/Loader.rc` and `package-release.ps1`; `-Version 0.2.0` would package binaries saying 0.1.0. One source (CMake
  `project(VERSION)`), a git tag per release as `docs/release-readme.md` promises.
  **Done 2026-11-04 (16fc28e):** `project(MEVR VERSION ...)` only; a generated header for the code and the .rc files.
- [x] **[C] package-release checks**: a clean tree, version consistency, ctest, `check-spdx`, a cockpit package built from
  the current models. `dist\MEVR-0.1.0` predates today's commits.
  **Done 2026-11-04 (663adb8, 42db650):** clean tree, tag, version and commit in the binaries, check-spdx, ctest; a warning for a stale cockpit package; checksums and release notes with `-Zip`.
- [ ] **[C] CI needs real work (owner, 2026-11-04).** Today there is none: `.github` holds only the issue forms. At least a
  Windows workflow (VS 2022) that builds the players' configuration (monolithic, `MEVR_LE1_LESDK=OFF`, which since
  2026-11-04 configures without the LExSDKv2 submodule) and the setup, runs ctest (`mevr-tests`, the colour and upscale
  shader checks, `mevr-rva-table`, `mevr-okf`) and `check-spdx` on every push, and the trilogy build on a schedule. Then
  a release workflow: tag -> the zip from `package-release.ps1`, its checksums, the PDBs kept as an artifact. The cockpit
  package needs Legendary Explorer and the models on Git LFS: build it in CI or attach a reviewed one.
  **Started 2026-11-04 (663adb8):** `build.yml` (players' build, tests, SPDX and OKF checks on every push and pull request) and `release.yml` (on a `v*` tag: the zip, checksums and symbols as artifacts, nothing published), both on `windows-2022` (GitHub's `windows-latest` moved to VS 2026). Reproduced locally without the submodule; **never run on GitHub yet**. `trilogy.yml` (2026-11-04): LE1-3's development build and tests every Monday and on demand, with LExSDKv2 checked out for that job only (nothing uploaded). Still to do: the cockpit package, a first real run.
- [x] **[D] Code signing.** The setup, the ASI and a `bink2w64.dll` proxy are unsigned: expect SmartScreen and antivirus
  warnings. Sign (an open-source signing programme) or document it in the release README.
  **Decided by the owner 2026-11-04: document it only** (no signing plans); `docs/release-readme.md` already explains it.
- [x] **[C] Binary metadata**: an icon and VERSIONINFO for MEVR-Setup.exe and the ASI; no absolute build paths in the PDBs
  (`/PDBALTPATH`); archive each release's PDBs so crash-report stacks can be symbolised.
  **Done 2026-11-04 (16fc28e):** version resources on the ASI, loader and setup, the setup's icon, PDBs referenced by name only, a separate symbols zip.
- [x] **[C] Player docs**: a Controls section; supported headsets and the six controller profiles (README lists three);
  "LE1 only" stated in the release README and the setup; "What Install changes" missing the cockpit package and the TOC
  edit; Steam's "verify files" also drops the cockpit's TOC entry; the DRAFT marker in `docs/release-readme.md`.
  **Done 2026-11-04 (11c626e).**
- [x] **[C] `docs/troubleshooting.md`** is dated 2026-10-26: rows for the Mako and cockpit, vent, fingers, wrist and gun
  panels, labels, the game pointer, screens in the room, the galaxy map, the 3D screen; stale sights defaults.
  **Done 2026-11-04 (11c626e):** dated 2026-11-04, sections for every newer feature.
- [x] **[C] Installer paths never run**: the update check, Mass Effect Modder's download and the texture pack, the headset
  Detect, "Install after I quit". Run them once or hide them for this release.
  **Done offline 2026-11-04 (fe3b156; research/33 section 10):** ctest `mevr-setup-network` (111 checks) runs the update check,
  MEM's lookup, download and unpacking, a pack install with a stand-in MEM and "Install after I quit" against a local server;
  nine bugs found and fixed (a pack install refused while any game copy ran, a stale work folder, MEM's quoting with a trailing
  `\`, settings re-applied over the player's after MEM, update-check messages, downloads' size and SHA-256 checks, 7-Zip as
  a fallback, the uninstall's texture note, cancel read as failure). Still untested: live GitHub (no release yet; its `digest`
  format is [verify]), the real MEM, the window's own clicks, the headset Detect, the game's side of the request.
- [ ] **[C] Logs**: no levels, a flush per line, about 60 heartbeat sites (7 MB in one session). A quieter release default;
  `[verify]` text in Warn lines lands in players' reports (`MakoControls.cpp:652`).
  The Warn line with `[verify]` reworded (4fa704f). **2026-11-04:** the players' build writes the status block every 30 s
  instead of 5 (the bulk of the volume: about 5 MB an hour to under 1; `[Debug] heartbeatS`); the three lines that said
  "last 5 s" say "this period". Levels not added; the flush per line stays (a crash keeps every line).
- [x] [C] `.github`: a feature-request form, CHANGELOG, release notes template.
  **Done 2026-11-04 (663adb8).**
- [ ] [H+C] **Promo footage**: the spectator camera has never run in the game (research/49); a trailer needs it.

### Found on 2026-11-04 while doing these
- [x] [C] **The Mako's left trigger** may still reach the game as LB (boost) while a cockpit control is held: research/47
  says it reaches the game as nothing then, but `GamepadMapping.cpp` (around line 476) zeroes only the analog LT, and with
  the default `leftTrigger = lb` it is LB; the unit test covers only `leftTrigger = lt`.
  **Fixed 2026-11-04:** the trigger's LB is cleared off the lever too (a boost begun on the lever's Y stays); unit-tested.
- [x] [D] **The Mako lever**: CLAUDE.md, `MakoControls.hpp` and the default ini text say it holds its place, but the default is
  `leverReturns = 1` (back to neutral); the ini text contradicts itself. Decide, then make the texts agree.
  **Decided by the owner 2026-11-04: it springs back** (the default); the texts say so now.
- [x] [D] **The game pointer on the 3D cinema screen** is off by default (`[Pointer] flatCine = 0`), so the wheel can't be
  clicked there.
  **Decided by the owner 2026-11-04: stick only.** (The pointer never reached the 3D screen at all; `flatCine` covers only the
  Flat screen choice and stays 0.)
- [ ] [C] **The cockpit package** in `build\cockpit-hull` predates commit 891ba5e (`tools/cockpit`): rebuild it before packaging.
- [ ] [H] **The setup's "Start the game once" and its SteamVR start** never ran for real (only their decisions, unit-tested).

## 6. Delivery channels (owner, 2026-11-04: consider more than GitHub)

GitHub Releases stays the source of truth (the setup's update check reads it). **The owner decided on 2026-11-04: GitHub
only for the first release**: Nexus not for now, ME3Tweaks Mod Manager not at all, Steam (guide, forum post) not for the
first release. The notes below stay for later. Where else players look:
- [ ] **[D] Nexus Mods** is where Mass Effect LE players find mods. Host the same zip there (not just a link), with the
  Controls and Known issues sections as the page's description. Vortex doesn't install LE mods, so the page points at
  MEVR-Setup (or ME3Tweaks Mod Manager, below). Expect the unsigned setup and `bink2w64.dll` proxy to be flagged by
  Nexus's or players' virus scanners: signing (section 5) or a clear note.
- [ ] **[D] ME3Tweaks Mod Manager's ASI manager.** Most LE players already have Mod Manager; its ASI list takes ASI mods
  with public source and a direct download link such as a GitHub release, which MEVR has. Being listed puts MEVR one
  click away for them (the ASI only; the VR game settings, `MELEVR.ini` and the cockpit package stay MEVR-Setup's job,
  so the listing says to run Setup's Repair once, or Mod Manager's ASI install gets a small first-run step in the mod
  that applies them). Ask ME3Tweaks; their LEBinkProxy and our loader must not both load the ASI.
- [ ] **[D] Steam.** The game has no Steam Workshop, and a separate Steam app for a mod of EA's game needs EA's and
  Valve's agreement, so Steam is for being found and for the launch: a Steam Community guide and a pinned discussion in
  the game's forum linking the release. Most players will start the game from Steam's Play button, not the setup's
  Launch (which starts `MassEffect1.exe` itself): check that path after an install (the EA app in between, SteamVR
  started first).

## 7. After the first release

Left-handed play; a curved menu layer and desktop keyboard navigation of the menu; room-scale body; squad orders by
pointing and gesture powers; `XR_FB_color_space` and per-path colour presets; 10-bit eye swapchains; the HDR path; LE2 and
LE3; the galaxy map as the game's own projector hologram; tapping the wrist minigame.
