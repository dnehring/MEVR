<!-- SPDX-License-Identifier: MIT -->
# ME2 from ME1: what the LE1 port already solved, and the bridge plan

Date: 2026-11-14, against main `a784b5ef`. Written by the LE1 conversations session for the owner and the ME2 lead after
reading the ME2 work on main: `docs/le2-*.md`, the 26 `research/le2-*.md` notes, the LE2 code and `tools/qa-le2`.
Companion to the [implementation plan](le2-port-implementation-plan.md) and the [reuse map](le2-port-reuse-map.md); it
does not replace them. Status words as there: **confirmed** (seen in the running game), **inferred**, **[verify]**.

## Summary

1. **ME2 has not rendered in VR yet.** Everything that ran in ME2 (2026-11-14) ran flat, in an opt-in monolithic
   diagnostic ASI with no Present hook and no OpenXR (`MEVR_LE2_DIAGNOSTIC`). The renderer (P2) is an audit only.
2. **The ME2 code reads the engine only through LExSDKv2**, which can't ship (no licence). LE1 solved this with its own
   reflection layer (`src/ue3`, research/34); LE2 has no `ReflectionLayout()` yet, so none of LE1's reflection-based
   modules can run there, and no LE2 players' build is possible.
3. **The fastest route to VR in ME2 is LE1's own route:** Halcyon's renderer imported verbatim, then linked into our host
   through `[MEVR]` seams (four commits on 2026-10-08). Once it runs, the whole LE1 test loop (fake headset, eye
   snapshots, pose recording and replay) works for ME2 too, and replaces desktop captures and pad pulses.
4. **The ME2 work has real assets worth keeping:** a strict v30 save codec with a Trilogy Save Editor oracle and one proven
   native load/save/reload cycle; the native routes for interrupts, scanning, reload, menus and saves; and a pure reload
   coordinator.
5. **Several ME2 problems have known ME1 answers.** Pad pulses that got zero polls: the game reads the pad only while its
   window has focus, and menus can be driven without the pad. The console's allocator and string question: `ue3/Memory`.
   Halcyon's 50 units per metre with forward emphasis: metric stereo (research/30). Two full renders a frame: one-Draw
   stereo (research/37).

## 1. Where ME2 stands (facts)

| Area | State on main | Source |
|---|---|---|
| VR rendering | None. No `src/halcyon/le2`, no XR session in ME2. The ordinary LE2 build still has the old `core/vr/Output` path; most features are `#if defined(MEVR_GAME_LE1)` | `CMakeLists.txt:897`, research/le2-renderer-integration.md:89 ("CanCompile is currently unproven") |
| Engine access | LExSDKv2 typed structs and offsets (`Le2DiagnosticObservation.cpp`, 2,411 lines, and six `.inc` observers), `engine/Legacy`; from `src/ue3` only `runtime::Guarded`. No ReflectCheck rows for LE2 | `engine/GameAdapter.hpp:73` ("LE2 and LE3 aren't analysed yet") |
| SDK rule | `LESDK_WITH_LE2 = MEVR_BUILD_LE2`: every LE2 build links LExSDKv2, the monolithic diagnostic one included. LE1's rule "monolithic = players' build = SDK-free" has no LE2 counterpart | `CMakeLists.txt:46-55` |
| Control | A run-bound file channel (`Le2ControlHost`, `core/qa/ControlProtocol`, `tools/qa-le2/control*.py`): lease, PID and start time, module hashes, fixed verbs. General ConsoleCommand is never admitted | research/le2-native-console-reuse.md:10 |
| Input | `core/qa/DiagnosticPad` pulses through `game/GamepadBridge`. Worked for squad selection (2026-11-14 16:41Z), then three runs had zero polls | research/le2-validation-evidence.md:85-125 |
| Saves | `tools/le2-save.py` (v30, allowlisted fields, CRC), TSE oracle, prepared variants; **confirmed:** eight edited resources survived a native New Save and a fresh reload (15:29Z) | research/le2-save-adapter.md, le2-validation-evidence.md |
| Scenarios | Reload, interrupt and scanning: 0 of 3 accepted. Fixtures found offline (Lazarus/ProCer, Veetor, Zanethu) | research/le2-runtime-observer.md:54 |
| Reload | `src/core/vr/PhysicalReloadRequest.hpp`, 18 tests, linked into nothing | research/le2-reload-core.md:4 |

## 2. Keep these

- The save codec, the TSE oracle and the protected persistence cycle. The ME1 tools never proved a save round trip through
  the game this strictly.
- The native routes found offline. Interrupts: `SFXGameModeConversation.InterruptParagon/Renegade`, then
  `BioSFHandler_Conversation.HandleEvent` (6/5), then `BioConversation.SelectInterruption`; the Veetor door fixture.
  Scanning: `SFXGameModeOrbital.StartScanning/StopScanning/LaunchProbe`. Reload: `Reloading.CancelReload` calls
  `DoReload` when `bEarlyReload` is set, so a cancel can commit ammo. Also the menu and save handlers.
- The reload coordinator as a pure, engine-free core. It belongs next to LE1's `core/ik/Vent` and `core/vr/Ads`.
- The ownership discipline: one native owner, backups, restoration receipts. Section 3.3 suggests one lock instead of two.

## 3. The gaps, and what ME1 has for each

Ranked by how much they block. Each lists what ME1 has, the step, and how to check it.

### 3.1 No VR renderer: import Halcyon's ME2 the way LE1 did

**ME1 has:** the recipe, in four commits on 2026-10-08:
1. `af23acaa` imported Halcyon's source verbatim into `src/halcyon/le1` (upstream `3c51514`), alone in its commit, so
   `git log -p -- src/halcyon` shows every change since.
2. `9074cea8` ran it inside our host ASI as the static library `halcyon-le1`. It uses our static OpenXR loader and our
   controller actions (`core/xr/Input`) on its session, merges Touch into its XInput hooks as Xbox pad 0, and puts the
   seams in `mevr_bridge.h` (implemented in `plugin/HalcyonBridge.cpp`).
3. `c82ff76b` let the motion controllers navigate its menu.
4. `c32420db` gave it controller aim (no head aim).

**ME2 needs:** the same steps for `reference/MassEffect2-VR` at `4784e90`. It has 15,355 lines in `src/`: `me2_xr.cpp`
2,615, `d3d_capture.cpp` 4,584, `calcview_hook.cpp` 1,849, `convo_fp.cpp`, `pchud.cpp`, `me2_menu.cpp`. Leave out
`dllmain.cpp` and `dxgi_proxy.cpp`, as research/le2-renderer-integration.md already plans.

**Steps:**
1. Import the source verbatim into `src/halcyon/le2`, GPL-3.0 SPDX lines, `NOTICE.md`, alone in one commit.
2. Add a `halcyon-le2` static library and link it into the LE2 host only. The diagnostic ASI stays separate: it has no
   Present hook.
3. Copy LE1's `[MEVR]` seams one by one into an LE2 `mevr_bridge.h`: static loader, session function wrap, controller
   actions, pad merge, menu key gated on a game window in front (`MevrBridge::KeyDown`), and the `ExitProcess` shutdown.
4. Build all three configurations (dev, `build-mono`, `build-trilogy`); then run the first native session on the fake
   headset (3.3).

**Carry these ME1 fixes in at import.** Each one bit LE1, and Halcyon's ME2 code shares the LE1 design: its
`calcview_hook.cpp:734` reads "matches ME1 exactly (xr_session.cpp kMetersToUU=50, kFwdLeanEmphasis=3)".

| ME1 lesson | Where it lives in LE1 | Why ME2 needs it |
|---|---|---|
| Life-size stereo at 100 units per metre, each eye at its real place; upstream's convergence was divergent | research/30, `plugin/MetricStereo`, `core/vr/MetricStereo` | Halcyon ME2 uses the same 50 u/m and 3x forward lean |
| No script calls off the game thread | research/36, `plugin/GameThreadCalls`, `MevrBridge::QueueProcessEvent` | `pchud.cpp` hooks ProcessEvent, and `convo_fp.cpp` calls `DisableDOF` (a ProcessEvent) from its `Tick` [verify which thread]. The host already logs `process event: off the game thread` |
| Quit-time fault: no waiting in the ExitProcess hook, the exe's static locks never deleted | gotchas/quit-time-fault-in-a-deleted-lock, `plugin/KeptLocks` | Same engine teardown |
| Swapchain budget (16 per session on SteamVR) | `plugin/Swapchains`, gotchas/openxr-swapchain-limit | Every quad we add |
| Motion blur and dynamic shadows applied by the mod (BIOEngine.ini wins over GamerSettings and is rewritten by the game) | research/35, `game/SystemSettings`, gotchas | Stereo motion blur smears each eye its own way |
| Borderless window (a bordered one is capped at the monitor's height, which broke stereo) | `tools/vr-game-settings.ps1` | Same window code |
| One-Draw stereo (the second eye replayed): 20% at the heaviest cell | research/37, `plugin/DrawReplay`, `RenderLevers` `sharePass 5` | Halcyon ME2 renders the whole frame twice a present (its README) |
| HDR's float backbuffer handled in the eye blit | research/27 sections 7-7d | Same blue-screen risk |
| Recenter of a tilted start (SteamVR upside down) | `core/vr/Recenter`, `[MEVR] RecenterOrientation` | Same runtime behaviour |

The addresses differ: `plugin/RenderLevers` and `DrawReplay` hook LE1 RVAs. Each lever is a later port, not part of the
first session.

**Check:** one XR session on the fake headset. Both eyes in a snapshot with the world in stereo; the controllers' poses
published; a clean quit with no `crash-exit.txt`; the host log has no `process event: off the game thread`.

### 3.2 Engine access without LExSDKv2: the reflection layer for LE2

**ME1 has:** `src/ue3` (`ClassRef`, `FieldRef`, `FunctionRef`, `Params`, `runtime::Call`, `memory::OwnedString`,
`FreeString`) on a per-game `ue3::Layout` (`engine/GameAdapter.hpp`, `ReflectionLayout()`). There is also a run-time
cross-check against LExSDKv2 in development builds (`engine/ReflectCheck`, over 400 rows for LE1, `reflect-check.json`), and
an address table checked at attach (`engine/Addresses.hpp`, `core/Exe`, `tools/rva-patterns.py`, ctest `mevr-rva-table`).
research/34 is the stage-by-stage record of how LE1 left the SDK.

**ME2 has:** candidate RVAs from the SDK (research/le2-engine-access-discovery.md): GObjects `0x173CC48`, GNames
`0x1668A10`, GEngine `0x175EF28`, GWorld `0x1762340`, GMalloc `0x1682378`, GNatives `0x16A99B0`, ProcessEvent `0xD8D50`,
CallFunction `0xD6770`, GameEngineTick `0x546980`, and UFunction offsets `0xD0/D4/E2/E4/F0` (LE1: `0xD8/DC/EA/EC/F8`).
That note recommends exactly this layer for LE2, but it hasn't been built.

**Steps:**
1. Get LE2's decrypted `.text`: an ordinary (not diagnostic) LE2 build's host writes it to `%LOCALAPPDATA%\MEVR\dump` at
   attach. The diagnostic build skips `code_dump`. Then open a Ghidra project for `MassEffect2.exe` as for LE1
   (`tools/ghidra`).
2. Fill `ReflectionLayout()` for `MEVR_GAME_LE2` the way research/34 section 2 did for LE1: each offset read from the
   function that uses it, not taken from the SDK.
3. Add the LE2 rows to `engine/Addresses.hpp` with byte patterns, so the attach check holds for LE2 too.
4. Run ReflectCheck in an LE2 development build: our layer against LExSDKv2, the same rows as LE1.
5. Move the diagnostic observers from SDK structs onto `FieldRef` handles one by one. Each needs a ReflectCheck row, and a
   failed lookup stands the feature down with a log line.
6. Add an LE2 SDK guard to CMake: a monolithic LE2 build that links LExSDKv2 must say "development diagnostic" in its
   ProductName or version, and `tools/package-release.ps1` refuses it, as it refuses LE1's.

**This also settles the console's open question.** "General ConsoleCommand still lacks allocator and returned-string
ownership proof" (research/le2-native-console-reuse.md): LE1 answers it with `ue3::memory` (GMalloc's appMalloc and
appFree found by pattern, research/34 stage 2), with `OwnedString` for the command and `FreeString` for the returned
FString. `game/ConsoleRequest` has run `PlayerController.ConsoleCommand` that way in every LE1 test since October.

**Check:** the LE2 log says `reflect: N of N checks agree`; the RVA table holds on 2.0.0.48602; one `ConsoleCommand` through
`ue3::runtime::Call` returns its text.

### 3.3 Testing in VR: the fake headset and the test loop for ME2

**ME1 has:**
- `tools/fake-headset.ps1`: SteamVR's null driver, with the game's runtime pinned through `[Debug] openxrRuntime`, so the
  machine's active runtime is never changed.
- `tools/fake-headset-session.ps1`: deploy, backups, launch through Steam, run script, restore.
- The host test loop (`plugin/TestLoop`): eye snapshots (both eyes as the headset shows them), pose recordings, replays
  with inputs, perf probes. It works through one seam in Halcyon's `ResolveSessionFunctions`
  (`MevrBridge::WrapSessionFunctions`).
- Recording generators: `tools/make-walk-recording.py` and friends write controller poses, sticks and buttons.

**ME2 has:** desktop captures (PrintWindow, with a desktop BitBlt fallback; several came back black) and pad pulses.
research/le2-runtime-observer.md correctly found the LE1 wrapper hardcodes ME1. **It also found a real LE1 bug:**
`fake-headset-session.ps1` deploys before it backs up (lines 98-110).

**Steps:**
1. Make `fake-headset.ps1` and `fake-headset-session.ps1` take `-Game LE1|LE2`: the game folder, the save folder, the ASI
   ini name, the log name, the build to deploy. Back up before deploying. Keep one script, not two.
2. With 3.1's seam, the test loop runs unchanged in ME2. Use snapshots for every visual assertion, never a desktop capture.
   Use recordings with inputs for VR interaction: poses, triggers and grips go through the same path the player's
   controllers do.
3. One ownership mechanism. Today `tools/game-slot.ps1` (`game-slot\lock.json`) and `qa-cafeteria.lock` (qa-le2,
   qa-cafeteria) coexist, and `session.py` refuses when the other exists. Agree on one lock file with a game field.

**Check:** `tools/fake-headset-session.ps1 -Game LE2` loads a protected fixture and takes a snapshot whose left and right
PNGs show the Normandy in stereo, and the saves and inis are restored hash for hash afterwards.

### 3.4 Input without focus: why the pulses got zero polls

**ME1 has:** the lesson that the game reads the pad only while its window has focus. See CLAUDE.md's `game/FocusPause`
line ("the pad reaches the game only with focus") and the gotcha that LE1's menus take real input only with the game in
front. Windows refuses `SetForegroundWindow` from a background process (`core/Window.cpp` logs `game window to foreground:
refused by Windows`). ME1 therefore drives screens without the pad: `SFXGameModeBase.GuiKey` on the active mode, from
the game thread (`game/GuiCursor`, `game/GameMenuRequest` `StepSwitch`). The events are 6 motion, 50/52 left button,
28/29 wheel, 13 back, and the d-pad codes 11... with releases 30-33. Gameplay input in tests goes through the mod
(`mevr convo stick`, `mevr powerwheel ...`) or a replayed recording.

**ME2 saw:** "zero positive polls, neutral polls and output writes", and "A final focus observation reported foreground PID 0"
(research/le2-validation-evidence.md:107-112). **Inferred:** the game wasn't the foreground window and didn't call
XInputGetState at all. The successful runs had the game in front.

**Steps:**
1. Log the foreground window's PID at each pulse's first eligible poll, and refuse a pulse whose game isn't in front
   (say so in the result).
2. For menus, use `SFXGameModeBase.GuiKey` on the active mode (declared in LE2, research/le2-powers-input.md:37) instead
   of pulses. It needs no focus and no timing. Note that `SFXGameModeGUI.GuiKey` doesn't exist (le2-powers-input.md:87).
3. For gameplay, use pulses only with the game in front. Launch from the interactive session as `tools/launch-game.ps1`
   does; once VR runs, replace pulses with recordings (3.3).

**Check:** ten pulses in a row each record their polls with the game in front; a pulse sent with another window in front is
refused by name.

### 3.5 One request channel

**ME1 has:** `console.request` / `console.result` (`game/ConsoleRequest`, `tools/console.ps1`): any console line, plus the
`mevr ...` verbs (inside LE1 guards today), on the game thread, gated by `core/DevChannels`.

**ME2 has:** a second channel with fixed verbs, and a valid objection to the first: `console.request` names no game, so a
file meant for ME2 could be consumed by ME1 (research/le2-runtime-observer.md:229-239).

**Steps:**
1. Add a game field to the request (or a per-game name, `console.LE2.request`); ME1 ignores what isn't its own. Fix this
   in `game/ConsoleRequest` and `tools/console.ps1` once, for both games.
2. With 3.2 done, admit the general console in LE2 through `ue3::runtime::Call`. The run-bound identity and lease can
   stay as a wrapper around the same handler, not a second verb set.
3. Move `mevr` verbs out of the LE1 guard module by module as each feature ports (6.x).
4. [verify] `QuickLoad` as a faster fixture load in LE2 (`BioPlayerController.QuickLoad`, exec,
   research/le2-save-load-entrypoints.md:45). LE1 writes the fixture over the loaded career's own quicksave
   (`tools/conversation-launch.ps1 -Save`; else QuickLoad refuses "cannot be accessed"). The menu route ME2 already proved
   stays the reference path.

### 3.6 Porting the features, in order

The LE1 modules below are each LE1-guarded today. What blocks each in LE2:

| LE1 module | Needs in LE2 | ME2 native names known so far |
|---|---|---|
| Host-side: main menu (RmlUi), calibration, vignette, transition fade, colour, recenter, swapchains | 3.1 only: they live in the host and draw quads, not game data | none |
| `engine/BonePose`, `game/ArmIk`, `game/BodyFollow`, fingers | 3.2; `USkeletalMeshComponent` UpdateSkelPose hook RVA; the skeleton's bone names [verify] (LE1: `Root`, `Camera`, `God`, `LeftWrist`...). **Never write the Camera or God bones** | none |
| `game/HandAim`, `Weapon`, `SupportGrip`, `Sights`, `GunKick` | 3.2; `GetAdjustedAimFor` (`engine/Legacy` already has the LE2 block), the weapon mesh and muzzle sockets | `SFXWeapon` (`CurrentSpareAmmo` `0x908`, `AmmoUsedCount` `0x90C` in the SDK) |
| `game/Holsters`, `core/ik/BodySlots` | weapon switching through the game mode | `SFXGameModeDefault` weapon functions [verify] |
| Reload (new for ME2) | the coordinator plus `core/ik/BodySlots` for the clip pouch, `core/ik/Vent`'s gesture-in-the-gun's-frame pattern, haptics | `SFXWeapon.TryReload`, `CanReload`, `ReloadNearFinished`, `Reloading.CancelReload` (commits ammo when `bEarlyReload`) |
| `game/VrCover`, `core/vr/VrCover` | the LE2 damage rule; research/23 section 6.3 has the plan | `SFXGame.ModifyDamage` (`bIgnoresCoverDirection`), `SFXShield_Base` (`bFastCoverRegen`) |
| `game/Posture`, `Sprint`, `Comfort`, `ViewLevel` | per-mode camera input classes; traversal kept | `StormOn/Off`, `TryExitCover(bForceExit)` (cancels reload), `TryClimbOrMantleCover` |
| `game/PowerWheel`, `SquadOrders`, `Voice` | the power list and casting call | `UsePower`/`UsePowerOnTarget` variants [verify] |
| `game/WorldUi`, `GuiCursor`, `GamePointer` (host) | the GUI manager's panel list; `GuiKey` | `BioSFHandler_*`, `SFXSFHandler_*`; `m_lstPanels` at `0xA8` (SDK) |
| Conversations: 3.7 | | |
| Galaxy and scanning: 3.8 | | |

### 3.7 Conversations and interrupts

This is where ME1 has the most built (research/52, 59, 69, 70). Do it in this order:

1. **The 3D cinema screen first** (LE1's default; research/52). It is host-side: `plugin/CineScreen`, the decision seams in
   Halcyon's `xr_session.cpp`, `core/vr/CineScreen`, and the floor and horizon (`plugin/CinemaBackdrop`). Halcyon ME2's
   own first-person conversation camera (`convo_fp.cpp`) is the alternative, and it writes ProcessEvent calls itself
   (3.1's thread rule). LE1's rule: one presentation owner. Halcyon's conversation path or ours, never both.
2. **The wheel and subtitles off the eye images**: LE1 puts the conversation movie on the HUD panel and crops the wheel
   (`core/vr/WheelCrop`, `DefaultSubtitleCrop`/`DefaultWheelCrop`). UI left in the eye images crossed the eyes on the
   screen (research/52 "Cross-eyed"), so the UI stands on its own layer in front.
3. **Interrupts in VR.** Timed prompts on the triggers (LT Paragon, RT Renegade). In LE1 the controllers' triggers are
   the squad's and the gun's on foot (the squad takes the left trigger per pull, `squad_menu::OwnTrigger`); check that
   nothing of ours owns either trigger in the conversation mode before mapping them. Test the taken and the missed window on Veetor's door
   with a recording (3.3), not a pad pulse. A hand gesture instead of a trigger is a design question for the owner.
4. **Input latches across the hand-over:** the stick that picked a reply must be let go before it walks
   (`cw::StickWalks`); buttons held when a menu closes are kept from the game until released (`gamepad::CloseLatch`).
   Both bit LE1.
5. **Later, and experimental:** conversations in the world (research/59/69). Its rules are engine-generic UE3
   (`BioConversation.m_pSpeaker`, `m_EntryList`, `SetPositionRotator`, `SkipNode`), but every name needs its LE2 check.
   It is weeks of tuning in LE1, and not part of the proof.

### 3.8 Galaxy, scanning, Hammerhead

- Show the native galaxy and orbital screens on a world panel first (LE1's `game/WorldUi` pattern, the game pointer, and
  touch on the panel). LE1's CIC hologram (`game/GalaxyMap`, research/51) is LE1 art and a later milestone, as the ME2
  plan already says.
- Scanning is a held input, but probes are edges. Follow the ME2 plan's warning: one probe per press, never per sampled
  frame.
- The Hammerhead can reuse the Mako's control geometry and the panel code (`core/vr/MakoControls`, `CockpitPanel`), not
  its physics writes.

### 3.9 Build and test hygiene

- Every LE1 build compiles the LE2 test targets under `/W4 /WX`, and LE1's ctest runs 16 Python LE2 suites (timeouts up to
  60 s). A break there breaks LE1's build. Keep it, but list the LE2 suites in the release checklist's test count.
- The 2,411-line observer and its `.inc` files compile only in the isolated diagnostic configuration, so the dev, mono and
  trilogy builds never see them. Add that configuration to the pre-merge builds, or CI.
- Stale status headers: `research/le2-save-flow-observation.md:5` and `:378`, `le2-resource-observation.md:129` and
  `le2-weapon-observation.md:205` say "pending" or "untested" for things `le2-validation-evidence.md` records as done.

## 4. Order of work

**Wave A: offline, no game.**
- A1: the Halcyon ME2 import (3.1 steps 1-2) and its seams (step 3), compile and link in all three builds.
- A2: the LE2 reflection layout and RVA table (3.2 steps 1-3). Step 1 needs one short native attach of an ordinary LE2
  build for the `.text` dump.
- A3: `-Game` in the fake-headset scripts, back up before deploying, and one lock (3.3 steps 1 and 3).
- A4: a game field in `console.request` (3.5 step 1).
- A5: the focus log and refusal for pulses, and `GuiKey` for menus (3.4).

**Wave B: one native owner, fake headset.** The first XR session in ME2 (3.1's check), then ReflectCheck in an LE2
development build (3.2's check), then the general console through `ue3` (3.5 step 2).

**Wave C: features.** Arm IK and hand aim with the gun (the minimal proof's "Firing"), then the reload scenario in VR with
recordings, then conversations on the 3D screen with interrupts, then scanning on a panel.

**Who.** This session knows LE1's Halcyon seams, the test loop and the conversation code, so it could take A1, A3, A4 and,
later, 3.7. The ME2 lead keeps P4 and P9, the fixtures and the scenarios. A2 needs Ghidra on `MassEffect2.exe`: either
session. Both sessions' native runs go through the one lock (3.3 step 3). The owner decides the split.

## 5. Statements about LE1 in the ME2 notes, checked

- "deploys before backup and protects only ME1 saves/INIs" (le2-runtime-observer.md:121): **true**, an LE1 bug; fix in A3.
- `console.request` "has no game ID ... An LE2-intended file can be consumed by ME1": **true**; fix in A4.
- `GameMenuRequest`'s "repeated backouts are inappropriate" (le2-powers-input.md:56): the backouts are bounded (B as the
  game hears it, `GuiKey(13)` on the active mode, 4 ticks apart, at most 6, until the game leaves mode 7). For a single
  controlled trial a single `GuiKey(13)` is the same call.
- "LE1 injects through XR replay or its simulated stick": **true**. That is why 3.3 makes VR the injection path for ME2.
- UFunction offsets `0xD8/DC/EA/EC/F8` and ProcessEvent slot `0x250` for LE1: **true** (`GameAdapter.hpp`).
