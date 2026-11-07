<!-- SPDX-License-Identifier: MIT -->
# Codex adversarial review, 2026-11-06

Codex CLI 0.167, `gpt-6-astra` at `max` effort, read-only sandbox, seven parallel runs (game A-L, game M-Z, plugin,
core, engine+ue3+mod+loader+voice, installer+release scripts, our Halcyon seams since af23aca). 71 findings, each
re-traced in the code by a separate verifier: **35 confirmed, 35 real but narrower than claimed, 1 false positive.**
Most are dev-only (hot reload, console probes) or behind non-default settings. Raw output (JSON per area) was kept in the
session's scratchpad.

## Worth fixing (medium or above after verification)

All 13 fixed on 2026-11-06 (303238d; built in all three builds, ctest green).

**Checked in the game on the fake headset, 2026-11-06 14:48-14:58** (host and mod from adc80d7, Eden Prime save):
- halcyon#3: a held key (GetAsyncKeyState after keybd_event; a key the hook swallows never reaches the key state) with
  our menu open: swallowed with the game in front (False), reaches Windows with SteamVR's window in front (True).
  This was the user's dead keyboard and mouse at 14:40-14:46, in another session's build without the fix.
- halcyon#6: `mevr gui Journal`, then `mevr omni close`, same pawn: `[FPHIDE] cleared (save menu)` at 14:54:07, `[FPHIDE]
  applied` again at 14:54:12.
- gameM#2: pistol held at 100% heat (`mevr vent heat 1.0 hold`; the game sets m_bOverheated itself there, it clears a
  `setprop` below it), the rack replay (`tools/make-rack-recording.py`): `vent: VENTED ... overheated: latch cleared`,
  and the vents after it saw no overheat.
- gameM#3: `mevr call 0 EnterWeaponWheel`, our menu opened (no pause in mode 3), the wheel closed with ExitWeaponWheel:
  `menu pause: the game paused` 24 s later, at once after the close.
- halcyon#4: vrmonitor stopped: session state 6 (STOPPING) and 8 (EXITING) each logged `[XR] [MEVR] session stood down`;
  SteamVR then killed the game 5 s later as it does to any scene app that doesn't quit (vrserver.txt), so the monitor's
  HUD after it wasn't seen.
- Not checked in the game: the voice tail (needs a microphone listen), the LLM paths, squad roster, DOF restore, one Draw's
  fault path, the DrawReplay refusal, the installer fixes (Setup's tests are offline: `tools/setup/test-fake-game.ps1`).

| id | where | what | who |
|---|---|---|---|
| halcyon#3 | `src/halcyon/le1/vr_menu.cpp:320` | Global low-level key/mouse hooks swallow input while any menu is open, with no foreground check; ours widened it to the main menu and made the close key need the game in front, so another app in front gets no clicks, Alt+Tab or `\` | upstream, made worse by us |
| halcyon#6 | `src/halcyon/le1/head_aim.cpp:1695` | The fp-disabled / mode 7 branch clears the hides but keeps `g_fpHideSig`/`g_fpActiveFrames`, so on return the same signature skips reapplying: the camera sits inside Shepard's head after every game screen (mode 7 covers all our wrist screens) | upstream |
| halcyon#4 | `src/halcyon/le1/xr_session.cpp:777` | XR STOPPING/EXITING leaves `SetSfrEnabled` and the HUD redirect (`gArmed`) on; the monitor loses its HUD when play continues flat | mixed |
| engine#7 | `src/voice/Voice.cpp:478` | The tail-to-drain transition lives only in the capture packet loop: with no microphone (or one unplugged) a released listen stays in phase 2 forever and the 12 dB duck never lifts | ours |
| gameM#2 | `src/game/Vent.cpp:252` | `Address()` returns null for Bool fields, so `m_bOverheated` is never cleared; the vent zeroes heat and reports success but the overheat latch stays | ours |
| gameM#3 | `src/game/MenuPause.cpp:71` | The pause is tried once per opening; opened over a weapon/power wheel (mode 3/2) it fails, the wheel closes, and the game runs behind the menu | ours |
| gameM#1 | `src/game/SquadWeapons.cpp:1269` | `Inactive()` and many early returns keep the cached squad pawn pointers; SquadContext/SquadOrders/SquadPlace dereference them after a level change | ours |
| plugin#6 | `src/plugin/RenderLevers.cpp:1254` | sharePass 5 (default): a caught replay fault sets `gOneDrawFaulted` but never clears `g_oneDrawStereo`, so the right eye goes stale for the session; replay errors never count toward the fallback | ours |
| plugin#2 | `src/plugin/DrawReplay.cpp:2202` | If MinHook refuses a variant target, the `<0>` detour is put in the vtable with a null `gNextV`, and VariantSweep then hooks our own detour with itself | ours |
| install#3 | `src/install/Manifest.cpp:907` | A failed loader delete keeps that entry but marks the rename-back "already back"; the retry removes our loader and never restores `bink2w64_original.dll`: the game can't start | ours |
| core#1 | `src/core/voice/Llm.cpp:168` | `path.string()` on the model paths throws for non-ANSI folders, on a worker thread with no catch: std::terminate (the CLAUDE.md rule) | ours |
| gameA#7 | `src/game/ConversationWorld.cpp:744` | `DofBack` clears its flag before commands that fail without a controller; DOF then stays off, persisted via BIOEngine.ini (feature off by default) | ours |
| install#11 | `src/install/Installer.cpp:692` | Repair checks only manifest entries, so a first install that failed before copying the ASI "repairs" successfully without it (re-running Install recovers) | ours |

## Low (real, narrow, dev-only or latent)

Fixed too, as the low ones a player can actually meet (2026-11-06, the owner: "player reachable, not the obscure"):
install#17 (the self-uninstall helper retries the delete once a second for 10 minutes; checked against a file held open
6 s: gone at 6 s, then the folder), install#2 with it (cmd and ping by their System32 paths), install#6 (`WriteFileAtomic`
checks the close and the size), gameM#4 (a new world drops the standing orders and the order history), gameM#5 (a model
answer is dropped once the rules gave a newer order, or after 5 s). The rest below stay as they are.

- **Hot-reload teardown**: core#2/engine#1 (Output's D3D/XR teardown on the game thread, LE2/LE3 only), engine#2 (trampoline removed before the drain; the natives are game-thread anyway), gameA#1 (detached `galaxy press` thread), gameA#2 (XInput unhook without a drain; not installed on LE1), gameA#4 (power wheel left open), gameM#6 (omni-tool effect left at 3600 s; `omni_tool::Shutdown` has no caller), gameM#7 (jets latch).
- **Races**: core#6 (the breadcrumb seqlock writes the payload before invalidating), engine#4 (keyterms log), plugin#4 (blocker strings), plugin#5 (voice status strings), halcyon#7 (metric latch vs head look, about 1 mm), halcyon#8 (one frame of old-space controllers at recenter), halcyon#1 (upstream's worker-thread CopyResource at SteamVR bring-up).
- **Voice file feed (dev)**: engine#5 (WAVs under 10 Hz never advance), engine#6 (a cancelled feeder feeds and stops the next listen).
- **State**: gameA#5 (ramp record lost across the Mako), gameA#6 (Flash map left hidden when the hologram is turned off), gameM#4 (standing orders cross level changes; capped at 180 s), gameM#5 (a late model answer overrides a newer order), plugin#8/#9/#10/#11 (pointer and menu latches in rare paths), plugin#12 (leak with perEyeOcclusion=1), plugin#1/#3/#7 (only with settings never used, or a hooker or draw pattern not seen), engine#3 (DllMain voice join without the ExitProcess hook), engine#8 (stance restore after a caught fault), core#4 (fallback output latches failure), core#5 (a full GUI queue drops a release), core#3 (no work budget in the GFx expansion for crafted movies), gameA#3/#8/#9 (console probes), halcyon#2 (POV swap not `__finally`; rewritten next tick), halcyon#5 (UTF-16 MELEVR.ini parses as empty and is saved over).
- **Locale (latent: nothing calls setlocale)**: core#7, core#8, plugin#13 (`strtod`/`%g`/`wcstod`/`swprintf` against the core/Ini rule).
- **Journal policy**: plugin#14 (host POV and FScene writes not journalled).
- **Installer**: install#1/#2 (manifest path containment, bare `cmd.exe`/`ping`; not a privilege boundary, hardening), #4 (Rename deletes before a failing move), #5 (ini written before its undo record is saved), #6 (`WriteFileAtomic` ignores the close/flush error), #8 (no lock between Setup processes), #9/#12 (UTF-16/BOM ini edits and redaction), #10 (an unreadable ini counted as restored), #14 (`-NoBuild` skips the mono check; the LESDK gate mostly catches it), #16 (a blocking pipe read defeats cancel), #17 (self-delete gives up after 2 s while the window is open); deploy-asi.ps1 #7 (`-Remove` deletes whole folders), #15 (copies by size only: a same-size GGUF isn't redeployed).

## False positive

- install#13: ZIP entry names: the only variable part is the perf tag, empty on the players' paths and `SafeTag`-filtered on the dev one.
