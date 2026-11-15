<!-- SPDX-License-Identifier: MIT -->
# ME2 as a continuation of ME1: the plan

Date: 2026-11-14, against main `544067a3`; the owner's answers to its questions the same day (below). The why and the
evidence are in [ME2 from ME1](le2-from-le1.md) (sections 0 and 4) and
[research/le2-renderer-integration.md](../research/le2-renderer-integration.md); this is the work list. Each step names its
files, its check and what it waits for, so a session can pick one up without this conversation.

## The rule

> "For Mass Effect 1, this should really just feel like a continuation of the prior mod, not a radically new
> experience... Everything should really try to use ME1 as default and only deviate where necessary." (the owner,
> 2026-11-14)

- **Port, don't redesign.** Every ME2 feature is ME1's module, settings, defaults and resources (the same menus, panels,
  fonts, sounds, haptics). A step that changes behaviour names the ME2 fact that forces it in its notes.
- **One code path for both games.** No ME2 copy of a host module: where a module reaches into Halcyon's renderer it goes
  through one interface (M2), and where it reads the game it goes through `src/ue3` and `engine/GameAdapter.hpp` (M1).
- **Settings already decided:** no head aim (VRIK fast-tracked), no stick pitch, conversations and cutscenes on a screen
  (the flat cine panel until M5), first person, flat menus, no profile hotkeys, no recenter on R. Pinned in
  `ApplyMevrBaseline` (`src/halcyon/le2/me2_menu.cpp`).

## Where it stands (2026-11-14)

| Done | Where |
|---|---|
| Halcyon's ME2 renderer in our LE2 host: XR session, stereo, our controller actions on its session, Touch as pad 0, its render-thread ProcessEvents queued for the game thread | `src/halcyon/le2`, `plugin/HalcyonLe2*`, research "Imported and linked" |
| ME1's renderer choices on ME2's keys, hotkeys only with the game in front | `ApplyMevrBaseline`, `MevrBridge::HotkeyDown` |
| Upstream's `MELE2VR.ini` from the deploy (first person from the load, 2048x2304 eyes with the borderless window) | `tools/deploy-asi.ps1`, `tools/vr-game-settings.ps1 -Game LE2` |
| The first-gameplay-frame fault (the mod's `scale set` during a load with depth of field on) fixed | `game/SystemSettings`, gotchas/me2-scale-set-during-load |
| Checked on the fake headset only; never in a headset | research "The first run on the fake headset", "Defaults from ME1" |

**The gap, measured:** the mod can use 60 host services (`HostServices` v39, `src/mod/ModApi.hpp`); the LE2 host gives 3
(controllers, the ProcessEvent callback, crash breadcrumbs). And the mod's game code has no reflection layout for LE2. So
almost every ME1 feature stands down on ME2 today.

## Milestones

Sizes: **S** a day or less of one session, **M** a few sessions, **L** a week or more. "Check" is what must be seen on the
fake headset (or offline) before the milestone lands; headset checks are collected in M7.

### M1. The mod's engine access on ME2 (M)

Waits for nothing. Runs beside M2 (different files: `src/engine`, `src/ue3`).

1. Open ME2's decrypted `.text` in Ghidra (`%LOCALAPPDATA%\MEVR\dump\MassEffect-60AEEFA5.text.bin`, `tools/ghidra`,
   `local-tools/splice_text.py`). Never commit it.
2. `ReflectionLayout()` for `MEVR_GAME_LE2` in `engine/GameAdapter.hpp`, each offset read from the native function that
   uses it (research/34 section 2 is the recipe; candidates in research/le2-engine-access-discovery.md).
3. `ue3::memory` for LE2 (GMalloc's alloc and free by pattern), so `OwnedString`, `FreeString` and the console work.
4. LE2 rows in `engine/Addresses.hpp` (patterns from `tools/rva-patterns.py`) and in `engine/ReflectCheck`.
5. Move `engine/Legacy`'s LE2 reads and the diagnostic observers onto `ue3` handles, one ReflectCheck row each.
6. **A save loaded by script** (the owner, 2026-11-14: no computer use for driving the game), once the console works:
   `tools/le2-halcyon-run/load.ps1` reaches a save's gameplay without the Load menu. Take LE1's route (the career's
   quicksave rewritten, the console's `QuickLoad`) if ME2's console has it [verify], else the route `tools/qa-le2`'s
   scripted `load-fixture` takes in the diagnostic host (research/le2-validation-evidence.md "Scripted loading"). Never
   overwrite a save the owner made.

**Check:** an LE2 development build logs `reflect: N of N checks agree`; `PlayerController.ConsoleCommand` through
`ue3::runtime::Call` returns its text (`tools/console.ps1` works on ME2); ctest `mevr-rva-table` holds for LE2;
`load.ps1` reaches Freedom's Progress gameplay from the VR Shepard career's Save 4, and `restore.ps1` finds the owner's
saves unchanged.

### M2. The host's services on ME2 (L)

Waits for nothing. Runs beside M1 (files: `src/plugin`, `src/halcyon/le2`).

1. **The test loop first (S):** a `[MEVR]` `WrapSessionFunctions` seam in `me2_xr.cpp`'s function resolution (as LE1's
   `ResolveSessionFunctions`), so `plugin/TestLoop`'s snapshots, recordings and replays work in ME2. Every later check
   uses them instead of desktop captures.
2. **One renderer interface (M):** a header in `src/plugin` declaring what host modules need from the renderer (session
   and spaces, the view of each frame, our composition layers, recenter, the menu key and `KeyDown`, the game window, the
   stereo and cinematic state, swapchains). LE1's implementation forwards to `MELEVR::MevrBridge` with no behaviour
   change; the about 25 modules that call `MELEVR::` today (`HalcyonBridge`, `HalcyonOverlays`, `MainMenu`,
   `MainMenuSettings`, `Calibration`, `RenderLevers`, `GamePointer`, `WorldUi`, `CineScreen`, `TestLoop` ...) move onto
   it. **Check:** LE1 eye snapshots at a recorded pose are identical before and after.
3. **ME2's implementation (M):** `[MEVR]` seams in `me2_xr.cpp` and `calcview_hook.cpp`: the view each frame (head pose,
   eye offsets, units per metre: LE1's `OnViewBuilt`), our layers appended at `xrEndFrame` (LE1's overlay layers), the
   STAGE floor, recenter (`OnRecentered`), the session's stop (`OnSessionStopped`).
4. **The HUD panel (M):** the game's UI drawn onto our body-locked panel quad instead of into the eyes, LE1's way
   (`HudPanel*` seams, `plugin/HalcyonOverlays`). ME2's renderer duplicates the UI into each eye today (`UiDupEnabled`,
   `PcHud`): the panel replaces that. **Done 2026-11-14** (lane B, third agent; `plugin/HudPanel`, `plugin/HandReticle`,
   research/le2-renderer-integration.md "Step 4"); what is left of M2 is listed there ("What is left of M2").
5. **`gServices` for LE2 from the same list as LE1's** (`plugin/ModLoader.cpp`): one builder, the renderer interface
   behind it, version 39.
6. **The rest of M2, done 2026-11-15** (lane B, fourth agent; research/le2-renderer-integration.md steps 5 to 9): ME2's
   crosshair hidden under the hand reticle (the mod's `game/ScreenReticle`), the pad merge with the mod's flags and the L3 + R3
   priority (`plugin/PadMerge`, both hosts), the HUD pieces (`plugin/HudPieceLayers`, both hosts), the floor lock's path
   (its correction [verify] in a headset) and `plugin/GameThreadCalls` on ME2. LE1 checked unchanged. Still `older than` on
   ME2: the squad menu (v11), comfort's test (v17), the interaction label (v21), the power wheel (v28), voice (v31), the
   modules that haven't come over (listed there, "What is left of M2").

**Implementation landed.** The wrist display and native pointer routing landed in `827cd3af` (M4.3). The scripted
Save 4 run verified both eyes, the wrist panel and pointer input. Keep the floor-lock correction on the physical-headset
checklist. The five startup notices above track work in M6. For the services that have landed, check startup for missing
view data, ABI size mismatches and missing callbacks.

### M3. VRIK on ME2, fast-tracked (L)

Waits for M1 and M2.

**Lane C, 2026-11-14 (fake headset; [research/le2-vrik.md](../research/le2-vrik.md)):** the mod's side of steps 1-3 runs on
ME2: hand aim (shots along the barrel, 30 degrees off the view with the controller turned 30), the view level (no camera
recoil: the pitch held at 0 in every tick of 34 shots, ME2's extra yaw kick taken out), the camera shakes off, arm IK,
the torso and the fingers through UpdateSkelPose (0x79B8A0), the weapon's frame and the gun kick. ME2's skeleton and camera
hook are ME1's (`Camera` under `God`). Left for the host (lane B2): the reticle and sights' drawing, the two-handed grip,
holsters, the floor lock; the mod-side sights (`game/Sights`) port once the host takes `setSight`.

1. **Hand aim (M):** `game/HandAim` on LE2 (`GetAdjustedAimFor`), the aim ray to the host, the hand reticle. `HeadAim`
   stays off. **No camera recoil, from the first shot** (ME1's, the owner: it "wasn't popular"): `game/ViewLevel` holds
   the view level while the hand aims, and `game/CameraShake` turns ME2's firing shakes off at their source
   (research/le2-vrik-audit.md "No camera recoil on ME2 either"). **Check:** a replayed right controller turned 30
   degrees: shots land along it (`hand aim: shot N`), and the view's pitch and roll stay 0 through 20 shots (`view level:`).
2. **Arms and body (M):** `engine/BonePose` (`UpdateSkelPose`'s LE2 RVA from Ghidra; ME2's bone names [verify]; never
   write `Camera` or `God`), `game/ArmIk`, `BodyFollow`, `BodyScale` and the floor lock, fingers. **Check:** a snapshot with
   replayed hands shows Shepard's hands on the controllers; `mevr camera trace` and `tools/skeleton-audit.py` clean.
3. **The gun in the hand (M):** the weapon's frame, sights (red dot, scope picture), gun kick, the support grip.
   **Check:** a scoped shot hits the sight line; `sights:` lines name ME2's sight bones.
4. **Holsters (S):** ME2's weapon switching, "only holsters draw". **Check:** a replayed draw at the hip slot draws the
   pistol.
5. **Reload (M, ME2's own):** ME2 reloads thermal clips; a physical reload gesture through the ME2 validation session's
   `PhysicalReloadRequest` coordinator, with `core/ik/BodySlots` (a clip pouch) and `core/ik/Vent`'s gesture-in-the-gun's
   -frame pattern. **Check:** the Freedom's Progress pistol baseline (12/0/60 -> 11/1/60 -> 12/0/59) reproduced by a
   replayed gesture instead of `R`.

### M4. The menus, as ME1 has them (M)

Waits for M2; the game-side parts for M1.

1. **Our main menu (M):** RmlUi in the LE2 host (`MEVR_MAIN_MENU`, today LE1-only), the same document and styles
   (`assets/menu`), settings rows mapped to ME2's keys (`MainMenuSettings`; Halcyon's rows through the M2 interface).
   Both sticks held or `\` open it, as in ME1.
2. **Calibration (S):** `plugin/Calibration` and `game/BodyCalibration` on LE2.
3. **The omni-tool (M):** the left-wrist tap and the rail (`plugin/OmniToolRail`), ME2's own omni-tool on Shepard's arm
   (`game/OmniTool` with ME2's effect names: the deviation), its entries opening ME2's screens (`game/GameMenuRequest`
   with ME2's functions [verify]), the screens on the wrist (`game/WorldUi` over ME2's GUI panels, `GuiKey`).

**Check:** on the fake headset the menu opens, a setting saves to `LE2MEVR.ini`, a replayed wrist tap opens the rail and
its Squad entry opens ME2's squad screen.

### M5. Conversations and cutscenes on ME1's 3D cinema screen (M)

Builds on M2. The implementation and fake-headset cinema check are in [ME2 cinema](../research/le2-cinema.md).
Timed interrupt acceptance at Veetor and the physical-headset check remain open.

1. The cinema screen's seams in `me2_xr.cpp` (research/52: the decision, parked head look, projection, plan, submit, pace,
   the conversation movie at its authored size), `plugin/CineScreen`, the floor and horizon (`CinemaBackdrop`), the dip
   from black (`TransitionFade`). Replaces the flat cine panel; `ApplyMevrBaseline` keeps VR conversations off.
2. ME2's interrupts (ME2's own), **on the controllers' triggers as ME2 has them on a pad** (the owner, 2026-11-14):
   while the game shows an interrupt prompt the triggers reach it as pad 0's LT and RT (Touch as pad 0 already does; LT
   Paragon, RT Renegade, le2-from-le1.md 3.7), nothing of ours claims them in the conversation mode (the squad's left
   trigger, `squad_menu::OwnTrigger`; the game pointer's trigger latch; our menu's), and the prompt shows where the player
   looks (the screen's UI layer). The game's own route (`SFXGameModeConversation.InterruptParagon/Renegade`, then
   `BioSFHandler_Conversation.HandleEvent`, then `BioConversation.SelectInterruption`) stays untouched.

**Check:** a snapshot during Freedom's Progress's first conversation shows the screen in stereo with the floor around it;
at Veetor's door (the validation session's fixture) a replayed trigger inside the prompt's window takes the interrupt and
one outside it doesn't, from a recording, not a pad pulse.

### M6. The rest of ME1, one module at a time (L)

M6 follows the first ME2 release (M7). The current pass covers the items below. The Hammerhead has its own later pass.

The [detailed M6 implementation guide](le2-m6-implementation-guide.md) audits the remaining work against ME1,
splits it into seven implementation packages, and specifies the existing code, ME2 adapters, dependencies and
acceptance checks for each. It also lists validation still owed for the five areas with gameplay code on main.

| Module | ME1 reuse and ME2 status | Next acceptance check |
| --- | --- | --- |
| Comfort and vignette | The shared `game/Comfort`, pad merge and vignette layer now expose snap turning, head- or hand-relative walking and vignette strength in ME2's settings. [Comfort controls](../research/31-comfort.md) | Replay two separate right-stick flicks, sustained movement with the left hand yawed away from the head, and menu entry with sticks held. Check the actual heading, motion, vignette, and return to neutral. |
| Wrist and gun HUD | Both use shared HUD panel crops and ME2's own HUD. The gun readout sits beside the native ammo effect. [Placement and replay evidence](../research/41-hud-pieces.md#me2s-ammo-display) | Check legibility in a headset across small and large guns, and after an ammo-power change. |
| Interaction labels | ME2 now reports the native `BioPlayerSelection` target in the shared snapshot channel. ME2's `UpdateSelection` is native and takes no parameters. The head-gaze hook and the HUD crop for the floating label still need ME2-specific work. [ME1 selection and label design](../research/40-interaction.md) | Capture an unobstructed door and NPC prompt. Confirm the localized crop and the target A actually uses, including when head and body face different objects. |
| Cover, crouch and sprint | The earlier ME2 adapter exercised physical crouch, cover protection and native Storm. The crouch toggle is now exposed in the shared menu. Sprint continues through ME2's native Storm and Halcyon's existing VR handling. [Native replay evidence](../research/23-cover.md#7-me1s-cover-system-on-me2-2026-11-14-lane-f) | Recheck crouch with stand/seated calibration, protection and traversal on the current build. Test sprint heading and release in a headset. |
| Player powers | The ME1 wheel, gesture detectors and coach use ME2's native power roster, cooldown and activation. Freedom's Progress verified the chest and drawn-rifle gestures. [Power mappings and replay evidence](../research/55-hand-cast-powers.md#mass-effect-2) | Natural combat fixture for a successful left-hand push against a hostile, controller selection, evolved powers and other weapons. |
| Squad roster and placement | The squad roster uses the native player squad. The existing right-hand wheel and arc use ME2's `HenchmanOrderHoldPosition` and `HenchmanOrderFollow`. Opt in with `[SquadOrders] place = 1` in the ME2 mod INI. [ME1 orders and placement](../research/54-voice-squad-commands.md) | Start with Miranda and Jacob on Freedom's Progress. Verify the selected living recipient walks to the marked floor, holds position, and returns on a double tap. Replay the invalid landing and loss of tracking cases. |
| Squad weapons, orders and voice | The ME1 menu assumes ME1 quick slots and weapon types. Adapt native ME2 inventory and equip confirmation. Commands that use powers should queue through the verified ME2 tactical path. Reuse the speech recognizer, radio gesture, and parser after the game-side orders work. [ME1 orders and voice](../research/54-voice-squad-commands.md) | Verify weapon changes and attack/power orders for each recipient, including unavailable weapons, cooldowns, and combat-to-dialogue transitions. |
| Thespian mode and conversations in the world | ME2 already has the cinema screen and native trigger routing. Reuse the reply matcher and panel with ME2's native conversation handler, then adapt the world camera and authored movement handoff independently. [Thespian](../research/70-thespian-mode.md) · [World conversations](../research/59-conversations-in-world.md) · [ME2 interrupt fixtures](../research/le2-interrupt-acquisition.md) | Prove normal and Investigate replies, both interrupts, and camera/controls returning after a dialogue choice and a scripted walk. |
| Galaxy map and scanning | Reuse ME1's table and pointer. The ME2 diagnostic orbital observer and Trilogy Save Editor fixtures provide probe and discovery evidence. ME2 supplies continuous scanning and the native launch and reveal logic. [Probe fixtures and observer](../research/le2-scanning-readiness.md) · [Zanethu state transitions](../research/le2-galaxy-fixture-source.md) | Normal-load the Normandy fixture, enter the galaxy and orbit through the native UI, then measure a single probe launch and persistent discovery. Use the zero-probe fixture to verify refusal. |

The comfort/placement pass uses [the protected ME2 runner](../tools/le2-halcyon-run/README.md), `make-walk-recording.py`,
`replay.ps1 -Inputs`, `snapshot.ps1` and Freedom's Progress Save 4. The comfort run timed out before ME2 started.
The runner verified zero differences after restoration. Current native behavior for the new comfort, target, and squad paths remains to be measured.
Use `mevr place status` and the snapshot's `squad` and `squadPlace` records to check the roster, recipient and dispatch.
The `mevr place aim` / `hand` / `release` diagnostics isolate native dispatch from physical input. A dispatched event alone
does not prove the AI reached the marker or held position.

### M7. Ship it (M)

**ME2 ships after M5** (the owner, 2026-11-14): the first ME2 release has M1 to M5 (engine access, the host's services,
VRIK, the menus, the cinema screen and interrupts); M6's modules follow it. Step 2 waits for nothing; it starts when M5
is close (the owner, 2026-11-14).

1. **The players' build without LExSDKv2 for LE2** (needs M1's step 5): the CMake guard and
   `tools/package-release.ps1`'s refusal, as for LE1.
2. **MEVR Setup for ME2:** the loader into `Game\ME2\Binaries\Win64`, the borderless window and VR settings,
   `MELE2VR.ini`, uninstall byte for byte (`src/install`, `tools/setup`).
3. **The headset pass:** every `notes/headset-checklist.md` ME2 line, in the owner's headset.

## Who and when

**Parallel sessions** (the owner, 2026-11-14): each lane is a session of its own in its own worktree, started and
coordinated by the session that wrote this plan.

| Lane | Takes | Starts | Notes |
|---|---|---|---|
| A | M1 | now | Ghidra on ME2 (the only lane that opens Ghidra until M1 lands); touches `src/engine`, `src/ue3` |
| B | M2, then M5 | now; M5 once M2 lands | the renderer seams; touches `src/plugin`, `src/halcyon/le2` |
| C | M3 | once M1 and M2 have landed | VRIK; touches `src/game`, `src/engine/BonePose`, `src/core/ik` |
| D | M4 | once M2 has landed (its game side once M1 has) | the menus; touches `plugin/MainMenu*`, `plugin/Calibration`, `plugin/OmniToolRail`, `game/OmniTool`, `game/WorldUi` |
| ME2 validation session | reload fixtures and the coordinator (M3.5), saves, scenarios (`tools/qa-le2`) | already running | its protected runner becomes the ME2 test runner once its responder and Halcyon share one LE2 host (le2-from-le1.md 3.3) |

**One game run at a time on this machine** (every lane, the validation session, the owner): `tools/le2-halcyon-run`'s
`run-lock.ps1` names who runs the game; its `prep.ps1` takes the lock and backs up saves and settings once at the start
of a stretch of runs, its `restore.ps1` puts them back once at the end and releases it; take the lock by hand around an
LE1 run. A game running with the lock free is someone else's: leave it and wait. **Runs are driven by script only, no
computer use** (the owner, 2026-11-14): launch, saves (`load.ps1`, M1.6), the console, the test loop's requests and
replays with `-Inputs`; a check that needs what no script reaches yet waits for the script. Every milestone lands on
main (`tools/land-on-main.py`) only after `build-trilogy`, `build`, `build-mono` and ctest pass; the owner pushes.

## The owner's answers (2026-11-14)

1. **Interrupts in VR** (M5.2): **the triggers**, as ME2 has them on a pad.
2. **Lanes:** **parallel sessions** (the table above).
3. **The Hammerhead** (M6): **ME1's Mako controls and cockpit**, for now.
4. **When to ship ME2:** **after M5** (M1-M5 in the first release; M6 after it).
5. **HDR** (later the same day): ME2 makes a 16-bit float swap chain whenever Windows HDR is on for the monitor, which
   Halcyon's ME2 renderer can't use (no stereo, no UI treatment). **SDR is the stop gap**: LE2's `[Colour] hdrDisplay =
   -1` answers the game's display query as SDR, the same 8-bit frame ME1 renders ("If it's cheap to do sdr, then we can do
   that as a stop gap while you figure out HDR"). The owner wants native HDR ("it looks so much better for VR"): the study
   and its plan are `research/le2-hdr.md`; the default goes back once HDR works on ME2 with stereo, the UI and at least the
   SDR frame's quality.
6. **HDR out of play; the best SDR picture instead** (later the same day): "We can take the HDR lane out of play. I'm
   just trying to figure out how we can get the best picture, when we know that me2 has a lot of dark scenes." ME1 answers
   the game's display query as SDR by default too (`[Colour] hdrDisplay = -1`), so neither game's picture depends on HDR
   settings; HDR's later steps (research/le2-hdr.md) are dropped. ME1's colour pipeline (per-headset profiles, mid-tones,
   black level, dither: research/27) comes to ME2, measured in ME2's dark scenes (`research/le2-picture.md`).
7. **No cover snapping on ME2 either**: "We won't snap to cover for VR, it's not a realistic experience. It risks the
   same problems of our skeleton not matching our players position in space." ME1's cover system transfers (research/
   le2-vrik-audit.md "ME1's cover system on ME2"): no snap, physical crouch, the VR cover rule. The vault (the owner's
   double A, with weapons held) starts from where the player stands.
8. **The menu on ME2**: ME1's rows ME2 doesn't have yet stay greyed ("Keep them greyed until I can see them"); `\`
   stays the menu key, and holding both stick clicks always wins ("We'll maintain the same key, but always prioritize the
   L3 + R3 stick click"): ME1's `gamepad::UpdateStickChord` rules on ME2's pad path, named first in the controller guide.
