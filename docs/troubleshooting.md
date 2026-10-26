# MEVR troubleshooting

This guide is for two readers:

- **An AI assistant** (ChatGPT, Claude, Gemini...) that a player has given a MEVR report bundle
  (`MEVR-report-<date>.zip`) and a description of the problem. Start at [section 1](#1-how-to-use-this-guide-for-an-ai-assistant).
- **A player** reading it as an FAQ. Skip to the [quick triage tables](#2-quick-triage) and find your symptom.

MEVR is a VR mod for **Mass Effect 1, Legendary Edition** (`MassEffect1.exe` 2.0.0.48602) on PC. It is an ASI plugin
(`LE1MEVR.asi`) with Halcyon's MassEffect1-VR renderer built in, plus our motion controls: the hand aims, the arms
follow the controllers, body holsters, a two-handed grip, sights, grenades, comfort options and a VR menu.

Everything here is grounded in the mod's source code and research notes (`research/*.md`) as of 2026-10-26. Log lines
are quoted as the code writes them; `{}`, `N` or `...` stand for values. **[uncertain]** marks what hasn't been
confirmed.

---

## 1. How to use this guide (for an AI assistant)

### What's in the bundle

| File | Where it comes from | What it answers |
|------|---------------------|-----------------|
| `REPORT.md` | the report button | A summary: versions, headset, the player's words. Read it first. |
| `LE1MEVR.log` | `Game\ME1\Binaries\Win64\Logs\` (or `%LOCALAPPDATA%\MEVR\` if that folder isn't writable) | The mod's log for the **last run**: did it load, which game version, every feature's settings, the 5-second heartbeat, warnings. Most answers are here. |
| `LE1MEVR.prev.log` | same folder | The run before. If the problem was in an earlier session (or the game crashed and was restarted), it's here. |
| `MELEVR_Log.txt` | `%LOCALAPPDATA%\MELEVR\` | Halcyon's renderer: the OpenXR session (`[XR]` lines), the stereo target, its settings file, its crash handler. **It grows across runs** (it isn't cleared at start): use the timestamps, and read from the last `[XR] starting OpenXR session thread` line. The bundle may hold only its tail. |
| `MEVRLoader.log` | `Game\ME1\Binaries\Win64\Logs\` | Our ASI loader (`bink2w64.dll`): did it run, did it find and attach `LE1MEVR.asi`. Missing if ME3Tweaks' LEBinkProxy is the loader (its log is `bink2w64_proxy.log`). |
| `crash-breadcrumbs.txt` | `%LOCALAPPDATA%\MEVR\` | Written once when the game crashes: the exception, the native stack, the script frames, the last 128 script events, the mod's handler calls and the mod's writes into engine memory. Its time stamp says which crash it is. |
| `reflect-check.json` | `%LOCALAPPDATA%\MEVR\` | The mod's self-check of its engine access (`mismatches` should be 0). |
| `LE1MEVR.ini` | `Game\ME1\Binaries\Win64\ASI\` | The mod's settings, and the calibration (`[Calibration.<controllers>]`). |
| `MELEVR.ini` | `Game\ME1\Binaries\Win64\ASI\` | Halcyon's renderer settings: four profiles, the one named in `[active]` is the one in use. |
| `MEVR-install.json` | `Game\ME1\Binaries\Win64\ASI\` | What the installer did: files added, replaced or renamed, the loader used, ini edits. |
| File listing | the report button | Which files are in the game's `Binaries\Win64` and `ASI` folders, with dates. Shows a missing loader, other mods' DLLs, Halcyon's upstream MELE-VR. |

### The order to check things

1. **Read the player's description and `REPORT.md`.** Note the headset, the runtime (SteamVR, Meta Link, Virtual
   Desktop...) and when the problem happened.
2. **Is `LE1MEVR.log` from the run with the problem?** Its lines carry only the time of day (`[01:40:44.592]`), no
   date. Compare with the file listing's dates and the player's account. A stale log (from days before) means the mod
   didn't load this time: go to [2.1](#21-the-mod-doesnt-load).
3. **The first lines of `LE1MEVR.log`:** `MEVR {} (monolithic) attaching to LE1`, then `game version 2.0.0.48602 OK;
   ...`. An `[error]` here means the mod switched itself off ([2.1](#21-the-mod-doesnt-load)).
4. **A crash?** Read `crash-breadcrumbs.txt` ([2.3](#23-crashes), [3.4](#34-crash-breadcrumbstxt)).
5. **No VR?** Read Halcyon's `[XR]` lines ([2.2](#22-no-vr-a-black-or-frozen-headset)).
6. **Everything else:** search `LE1MEVR.log` for the prefix of the feature (table in [3.1](#31-le1mevrlog)), its
   `[warn]` and `[error]` lines, and the heartbeat block nearest the time of the problem.
7. **Check the settings** the feature reads (`LE1MEVR.ini`, and the `[active]` profile of `MELEVR.ini`) against the
   defaults in [2.11](#211-settings-where-they-live-and-how-to-reset-one-safely).

### Rules

- **Don't guess.** Every diagnosis should rest on a line you can quote from the bundle and a row of this guide. Say
  which line. If two causes fit, say both and what would tell them apart.
- **If the cause isn't here, say so** and help the player write a clear issue ([section 5](#5-writing-a-good-issue)).
  An honest "this looks like a bug in the mod" with the evidence is worth more than a made-up setting.
- **Never tell the player to delete `LE1MEVR.ini`** when it holds a `[Calibration.<...>]` section: that's their
  calibration (about a minute in the headset to redo). Reset one section instead ([2.11](#211-settings-where-they-live-and-how-to-reset-one-safely)).
- **Don't suggest editing or replacing game files** (`MassEffect1.exe`, DLLs). The mod only supports the unmodified
  2.0.0.48602 exe. Repairs go through `MEVR-Setup.exe` (Repair) or Steam / the EA app.
- `[warn] first-chance access violation ... at LE1MEVR.asi+0x...` lines are **normal**: they are guarded memory
  probes that are caught (thousands per session; the log shows each address at most 3 times). They matter only in a
  crash report, as context.
- Logs and the ini files can contain the player's **Windows user name** in paths. Don't repeat it back needlessly.

---

## 2. Quick triage

Each table: the symptom, the line(s) that confirm it, the cause, and the fix. "The log" is `LE1MEVR.log` unless
named otherwise.

### 2.1 The mod doesn't load

What a good start looks like (the first lines of `LE1MEVR.log`):

```
[...] [info] MEVR 0.1.0 (monolithic) attaching to LE1
[...] [info] log file: ...\Binaries\Win64\Logs\LE1MEVR.log (pid N)
[...] [info] game version 2.0.0.48602 OK; 42 of 42 addresses at their RVAs (...)
[...] [info] crash breadcrumbs: on (...)
[...] [info] halcyon: renderer attached (its log: %LOCALAPPDATA%\MELEVR\MELEVR_Log.txt)
[...] [info] hooked UObject::ProcessEvent (host-owned)
[...] [info] mod startup (built <date> <time>); VR renderer: the host's (Halcyon)
[...] [info] host attached
```

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| No `LE1MEVR.log` at all, or one from an earlier day | The file listing: no `bink2w64_original.dll` next to `bink2w64.dll`, or no `ASI\LE1MEVR.asi`; no `MEVRLoader.log` for this run | No ASI loader ran, so nothing loaded the mod. | Run `MEVR-Setup.exe` (the copy in `Binaries\Win64\ASI` works too) and use **Repair**. |
| The same, right after Steam's "verify integrity of game files" (or an EA app repair) | `bink2w64_original.dll` is still there, but `bink2w64.dll` is RAD's own (the listing's date/size match the original's) | The repair put the game's own Bink DLL back over our loader. | **Repair** in `MEVR-Setup.exe`. |
| `MEVRLoader.log` exists but doesn't mention `LE1MEVR.asi` | `no ASI folder (...)`, or `... didn't load (error N)` | The `ASI` folder or the mod file is missing, or Windows refused to load it (error 126: a dependency missing; 193: not a 64-bit DLL; an antivirus quarantine shows as missing). | Repair. If an antivirus removed `LE1MEVR.asi`, restore it and add an exception. |
| The loader waited and gave up | `the exe's code still looks encrypted after N ms (...): attaching anyway`, then, probably, the mod's `our RVA table doesn't match` | The game's DRM hadn't decrypted the exe within 60 s (a very slow start, or the exe isn't the real one). | Start the game again. If it repeats, report it with both logs. |
| `[error] unsupported game version {} (need 2.0.0.48602); VR disabled, game untouched (...)` | That line | `MassEffect1.exe` isn't version 2.0.0.48602. The mod's addresses are only valid for that build, so it switches itself off and the game runs flat. | Update the game in Steam or the EA app. A downgraded or modified exe isn't supported. |
| `[error] game version 2.0.0.48602, but our RVA table doesn't match it (...); VR disabled, game untouched` | That line; the summary counts `with different bytes` | The version number is right but the code isn't what the mod expects: a modified exe, or (rarely) an exe still encrypted when the mod attached. Code that **another mod hooked** is recognised and counted as `hooked by another module`, which is fine. | Restore the original exe (Steam: verify files, **then** Repair the loader). Report it if the exe is unmodified. |
| `[error] failed to resolve UGameEngine::Tick; VR disabled` or `[error] failed to hook UGameEngine::Tick; VR disabled` | That line | A hook couldn't go in, usually because another tool patched the same place. | Report it, with the list of other mods and overlays. |
| Two loaders | The listing shows ME3Tweaks' LEBinkProxy (`bink2w64_proxy.log`) and `MEVR-install.json` says `"loader": "lebinkproxy"` | Not a problem: with Mod Manager installed, the setup uses its loader and doesn't install ours. | Nothing. Mod Manager's ASI management must keep `LE1MEVR.asi` in `ASI`. |
| Halcyon's own **MELE-VR** is installed too | The listing shows `Binaries\Win64\dxgi.dll` and `openxr_loader.dll` (not renamed to `*.mevr-off`); Halcyon's log also has `[XR] openxr_loader.dll loaded.` (upstream's loader; MEVR's own logs `[XR] using the statically linked OpenXR loader (MEVR).`) | MEVR has Halcyon's renderer built in. With upstream's `dxgi.dll` as well, two copies of the renderer run. **[uncertain]** what exactly happens then (no VR, a crash at start, or a broken picture): it's untested. | Run `MEVR-Setup.exe` **Install** or **Repair**: it renames those two files to `*.mevr-off` (Uninstall puts them back). Or remove upstream with its own `MELE-VR.bat`. |
| `[error] ProcessEvent hook failed; script-level features off` | That line | The script-event hook failed: VR may run, but the hands, holsters, menu pause and most features don't. | Report it. |
| `heartbeat #N: ..., script hooks waiting for script packages` for minutes in game | The heartbeat, and the `reflect:` lines | The mod's reflection layer didn't come up (`reflect: our reflection layer is off: ...`), so its features stay off. | Report it with `reflect-check.json`. |

### 2.2 No VR, a black or frozen headset

The renderer creates its OpenXR session **once, at the game's start**. If the runtime or the headset isn't ready
then, VR stays off for that run. **Start SteamVR (or Meta Link, Virtual Desktop) and wake the headset first, then the
game.**

A good bring-up in `MELEVR_Log.txt`:

```
[XR] starting OpenXR session thread (game device captured, backbuffer WxH fmt N).
[XR] using the statically linked OpenXR loader (MEVR).
[XR] xrCreateInstance (XR_KHR_D3D11_enable ...): XrResult 0
[XRRUNTIME] name='SteamVR/OpenXR' ...
[XR] xrGetSystem (HMD): XrResult 0
[XR] recommended eye image WxH; ...
[XR] xrCreateSession (D3D11 game device): XrResult 0
[XR] session bring-up complete - head-tracked session armed (Insert = menu).
[XR] session state -> 1 ... -> 2
[XR] xrBeginSession: XrResult 0
[XR] session running - head-tracked frames submitting now.
[XR] session state -> 3 ... -> 4 ... -> 5
```

(Its "Insert = menu" is upstream's text: in MEVR, `\` opens our menu.)

**Session states** (`[XR] session state -> N`): 1 idle, 2 ready, 3 synchronized, 4 visible, 5 focused (normal
play), 6 stopping, 7 loss pending, 8 exiting. The renderer runs frames from 2 to 6; the headset shows them from 4.

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| The game runs flat, the headset shows the runtime's home | No `[XR] starting OpenXR session thread` line for this run | The renderer never captured the game's D3D11 device. | Check [2.1](#21-the-mod-doesnt-load) first (the mod has to load). Then report it. |
| Same | `[XR] xrCreateInstance (...): XrResult -51` (runtime unavailable), or another negative result, then `[XR] session bring-up FAILED - see the [XR] line above for the failing call.` | No active OpenXR runtime, or it didn't start. | Set your headset's runtime as the active OpenXR runtime (SteamVR: Settings > OpenXR > Set SteamVR as OpenXR runtime; Meta app: Settings > General > OpenXR runtime). Start it, then the game. |
| Same | `[XR] xrGetSystem (HMD): XrResult -35` (form factor unavailable) | The runtime is up but sees no headset (not connected, asleep, Link/Steam Link not connected). | Connect and wake the headset, then restart the game. |
| `[XRRUNTIME] name='...'` names a runtime you didn't expect | That line | Windows' active OpenXR runtime isn't the one the headset uses (e.g. Meta's while you play over SteamVR). | Make the right runtime active (above). |
| The headset was fine, then went black or froze; the desktop game keeps running | `[XR] session state -> 6`, `-> 7` or `-> 8` | The runtime stopped the session (SteamVR closed or restarted, the headset disconnected, Link dropped, "quit" from the headset's menu). VR stays off for the rest of this run, by design. | Restart the game. Our menu closes by itself when VR stops (`main menu: closed (no VR frames for over 1 s: ...)`), so the desktop game isn't left paused. |
| A grey or frozen picture while the runtime's dashboard is open, or the headset is off your face | `[XR] session state -> 4` (visible, not focused), the log's `OpenXR: controllers idle (session not focused)` | Normal: the runtime has the focus. The game gets no controller input until state 5. | Close the dashboard, put the headset on. |
| The world is upside down or tilted at start | `[MEVR-RECENTER] the world has looked upside down for 5 s (...)` | SteamVR sometimes starts with a rolled base space. | Look straight ahead and recenter: R on the keyboard, double-click the right stick, or the menu's **Recenter**. The log says `[MEVR-RECENTER] tilt-corrected recenter: ...`. |
| Stereo looks broken, cutscenes letterboxed | Halcyon's log: `[TARGETCHK] Stereo 2 target MISMATCH: live=WxH ... configured=WxH ...` | The game's window isn't the size the stereo renderer expects (a bordered window is capped at the monitor's height; the game or another tool changed `GamerSettings.ini`). | In `MEVR-Setup.exe`, **Settings > Apply** (the VR game settings: a borderless window at the VR resolution). Or pick the render resolution again in our menu's **Display**. Restart the game. |
| No stereo in gameplay | Halcyon's log has no `[PASSCAP] published complete stereo pair` lines | The two eye renders aren't being captured. Check `vrMode = 2` in the `[active]` profile of `MELEVR.ini` (2 = Stereo). | Set `vrMode = 2` in the active profile, or report it. |

**SteamVR vs Meta's runtime.** MEVR is tested only through **SteamVR** (a Quest 3 over Steam Link, a PS VR2). Meta's
own runtime (Link, Air Link) ran an early build but **hasn't been tested with the current renderer**. If a Quest
player has trouble on Link, trying SteamVR (Steam Link, or SteamVR as the active runtime over Link) is a useful
comparison: say which one the logs show (`[XRRUNTIME] name=...`).

### 2.3 Crashes

First find **when** it crashed, then **where**.

| When | Confirm with | What it usually means |
|------|--------------|-----------------------|
| At start, before the menu | `LE1MEVR.log` ends early; `crash-breadcrumbs.txt` has the matching time | Read the exception's module (below). A crash in `LE1MEVR.asi` here is ours. |
| In game | The log runs, then stops; breadcrumbs written | Read the exception, the script frames and the last events. |
| On quit | `game exiting (code N); the mod stays loaded and the process ends` right before the crash time | A crash after that line is during the game's own teardown. Builds before 2026-10-25 had a known quit crash (`access violation at ntdll.dll+0x1A12D, writing 0x24`, a few ms after "game exiting"); it was fixed by leaving the mod loaded at exit. Compare the breadcrumbs' `host build` date. A quit crash loses nothing (the game has saved), but report it. |

**Reading `crash-breadcrumbs.txt`** (details in [3.4](#34-crash-breadcrumbstxt)):

1. The `exception 0x... (name) at <module>+0x<offset>` line names the module the fault happened in:
   - `LE1MEVR.asi`: **our code** (Halcyon's renderer is inside it too). A bug: report it with the file.
   - `MassEffect1.exe`: **the engine**. Look at the script frames and the last ProcessEvents: if they show `IN MOD`
     entries, or the mod's writes just before, the mod probably caused it; if they show only the game's own events,
     it may be the game itself (ME1 does crash on its own). Either way, report it with the file: we can't tell for
     sure without it.
   - `ntdll.dll`, `KERNELBASE.dll`, `ucrtbase.dll`: a system library doing work for someone else. Go down the
     `native stack` to the first module that isn't Windows' own; that one called it.
   - A GPU driver (`nvwarp64.dll`, `nvlddmkm`, `amdxx64.dll`, `d3d11.dll`...) or a VR runtime's DLL (`vrclient_x64.dll`,
     `LibOVRRT64_1.dll`, ...): the driver or runtime crashed while the game used it. Update the driver or the runtime;
     report it if it repeats. **[uncertain]** which runtime DLL names appear: read the stack as it is.
   - Another mod's or overlay's DLL (an overlay, a recording tool, another ASI): try without it.
2. `script frames`: the UnrealScript functions running on the crashing thread, innermost first. They say what the
   game was doing (a weapon switch, a conversation...).
3. `ProcessEvent: the last 128 dispatches`: `IN ENGINE` = no return seen (the game was inside it when it crashed),
   `IN MOD` = inside one of the mod's handlers. Names like `<class?> <object?> (not in GObjects)` mean the objects
   were already destroyed (normal during quit).
4. `the mod's writes into engine memory`: what the mod last changed in the game (tags like `HalcyonBridge.blendLocationOffset`).
5. The `fatal-code first-chance exceptions before it` list is mostly caught probes; only the last entries, close to
   `-0.000 ms`, are the crash itself.

Halcyon's log has its own crash lines too (`[CRASH] code=...`, `[CRASH] stack[...]`): they should agree with the
breadcrumbs.

**After a crash, a player can try (in this order):** reset `[Performance]` in `LE1MEVR.ini` (delete the section: its
render levers are experimental); turn off other ASI mods and overlays; update the GPU driver. Then report it.

### 2.4 Controllers

The bindings: Touch (and the PS VR2 Sense, which SteamVR presents as Touch), Index, HP Reverb G2, WMR, Vive wands and
the simple controller. Only Touch (Quest 3, PS VR2 under SteamVR) is tested.

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| Some buttons don't exist | `[warn] halcyon bridge: controller profile {}: {}`, e.g. `... htc/vive_controller: a trackpad for the stick, a grip button and no A/B: A, B, X and Y aren't mapped`, `... microsoft/motion_controller: a grip button and no A/B: the trackpad's click is A; B, X and Y aren't mapped` | The runtime bound a controller profile with fewer inputs. | Expected for WMR and Vive wands (untested). On Touch, PS VR2 Sense or Index this warning shouldn't appear: check the runtime's binding for the game. |
| No grip, no sticks: nothing works except the trigger | `... khr/simple_controller: no grip and no sticks (select and menu only): the guns, holsters, walking and turning don't work` | The runtime fell back to the simple controller (SteamVR does this when it doesn't recognise the bindings, **[uncertain]** for which setups). | In SteamVR's controller bindings for the game, choose a Touch or Index binding. |
| Which controllers the runtime reports | `halcyon bridge: the runtime's interaction profile: left {}, right {} (xrGetCurrentInteractionProfile)` | Informational. | Quote it in the issue. |
| No controllers at all | `[error] OpenXR: no controller profile accepted; motion controllers unavailable`, or `halcyon bridge: controller actions failed; motion controllers unavailable` | The runtime refused every binding. | Report it with the runtime and headset. A real Xbox pad still works. |
| Controllers ignored for a while | `OpenXR: controllers idle (session not focused)` | The session isn't focused (dashboard open, headset off the face). | Close the dashboard. |
| The hand or gun freezes or jumps | The heartbeat's `holsters: the right controller isn't tracked ...`, `throwables: the controllers aren't active ...`, `throwables: the {} hand lost tracking holding a grenade: it goes back` | Tracking lost (out of the cameras' view, behind the back, low light, low battery). | Keep the controllers in view; check the runtime's tracking. Nothing in the mod to change. |
| The menu button does nothing | `halcyon bridge: the {} controller's menu button reached us (/input/menu/click, ...)` appears or not | Nothing in MEVR depends on a menu button (it may not reach the game, e.g. a Quest over Link). | Use our menu's **Game menu** button for the game's pause menu. |
| Buttons fire in game right after closing a menu | `halcyon bridge: buttons 0x.... held as our menu or the calibration let go of the pad: kept from the game until released` | By design they are held back until released. If something still fires, it's a bug. | Report it. |

### 2.5 Weapons: holsters, the grip, sights, shots

Defaults: pistol at the right hip, assault rifle over the right shoulder, shotgun at the left hip (a cross-draw),
sniper rifle over the left shoulder; grenades at the left side of the chest. **The right grip holds the gun**: keep it
squeezed to carry it, let go at its slot to holster it, let go elsewhere and it goes back after 0.3 s.

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| Squeezing at a holster draws nothing | `holsters: squeeze near a slot but in none (nearest {} at {:.2f} m): nothing drawn, no RB` | The hand was near a slot but not in it. | Reach a little further; or move the slot (`[Holsters] hipRight = pistol, right, up, forward, radius`, metres). Check the eye height (below). |
| Same | `holsters: grab at {}: you carry no {} (its quick slot is empty)` | Shepard has no weapon of that type equipped. | Equip one in the game's inventory. |
| Same | `holsters: the game didn't {} the {} within N s (combat disabled here, the pawn busy, or mid-switch); ...` | The game refused (no weapons allowed here, an animation running). | Normal in places where ME1 doesn't allow weapons. |
| Same, and the log says the hand is in no slot while you're sure it is | `holsters: eye height {:.2f} m from [Holsters] eyeHeightM (the head is 0.00 m above the STAGE floor: not a plausible eye height, is the floor set up?) ...` | The runtime has no usable floor, so the slots use the 1.65 m default, which may not match your body. | Set up the floor in the runtime (SteamVR room setup), then recenter standing straight; or set `[Holsters] eyeHeightM` to your standing eye height (about your height minus 0.12 m). Running the calibration also sets it. |
| The gun goes back by itself | `holsters: let go of the {} {}: it goes back {} unless you squeeze again` | The grip was released (the right grip holds the gun). | Keep the grip squeezed; `[Holsters] releaseGrace` (seconds; `-1` = it stays out). |
| A button that used to draw the gun doesn't any more | `holsters: the game wanted to draw your weapon ({}): refused, only a grab at a holster draws ...` (with a short buzz) | `onlyHolstersDraw = 1` (default since 2026-10-25): on foot in VR, melee, grenade, the weapon wheel and the holster key no longer draw. Medi-gel and powers still do (`holsters: the game draws your weapon for {}: let through (medi-gel and powers need combat mode)`). | Draw from a holster; or `[Holsters] onlyHolstersDraw = 0` for the game's own draws. |
| The gun appeared without a grab | `holsters: the {} came out without a holster grab (...)` | Some other route drew it (a script, a cutscene). Informational. | Nothing, unless it repeats: then report it. |
| Holstering at the slot doesn't work during the draw animation | `holsters: the {} waits for the game's weapon switch to finish before it goes back` | The game ignores a holster while the draw animates; the mod waits, then holsters. | Normal. |
| The gun isn't in Shepard's hand, or points off | `weapon: grip measured in the right hand's axes: ...` missing, or many `weapon: grip moved by ...` lines; `weapon: {} not in the right hand (...)` | The gun is measured in the hand a moment after a draw. | Hold the hand still for a second after drawing. Repeated `grip moved` lines while standing still: report them. |
| The gun points somewhere other than the controller | The calibration lines (below); `calibration: the held gun follows the calibrated {} stance: ...` | Without a calibration, the controller's own aim pose is used, which differs per controller. | Run the calibration (our menu: **Body & calibration > Start calibration**). |
| The two-handed grip doesn't take | `support grip: held (...)` / `support grip: released (...)` lines, `controls: two-handed grip ...` at start | The left hand has to be inside the grab zone along the gun (radius `[Controls] supportAcquireM`, 0.18 m) when the left grip closes. | Hold the left hand on the gun's front. The left grip away from the gun does nothing (it never opens the squad wheel since 2026-10-22: the left trigger does). |
| No red dot or scope | `sights: settings: ...` at start; `sights: the host draws no sight`, `[MEVR-SIGHT] no eye through the sight` | The dot shows only to an eye looking through the sight's window. | Bring the gun up to the eye, aligned. `[Sights] enabled`, `pistol = dot`, `sniperRifle = scope 4` etc. |
| The scope looks too small or too big | `[MEVR-SIGHT] first scope picture: ...`, `sights: the scope's picture was up N frames: ...` | | `[Sights] scopeFieldDeg` (22), `scopeEyeBoxM` (0.05), `scopeMaxM` (0.70). |
| The dot doesn't sit on the gun's sight | `sights: the {}'s sight sits on its '{}' bone: ...`, the heartbeat's `sights:` line | The sight's place is read from the gun model. Two-handed, the assault rifle's dot can be off (known, see [section 4](#4-known-limitations)). | Note which gun and one- or two-handed; a snapshot helps. `[Sights] <gun>Place = back, height` overrides it. |
| Shots don't land on the dot | `hand aim: shot N: from the muzzle (...) toward what the sight line hit, ...` vs `... along the barrel (no sight line to converge ...)` | Shots converge on what the sight line hits (`[Sights] converge = 1`). Without a calibration the sight line itself may be off. | Calibrate. If shots still miss the dot, report it with a snapshot and the `hand aim: shot` lines. |
| The world tilts while shooting | `view level:` lines | The level camera holds pitch and roll at 0 on foot while the hand aims (`[Hands] levelCamera = 1`). | Check `levelCamera = 1`. |
| Grenade or melee doesn't happen | `throwables:` lines (`refused`, `put back`) | A slow release puts the grenade back; melee needs an enemy within `meleeReachM` (2 m) and `meleeConeDeg`. | `[Throwables]` keys. X and B still work unless `grenadeButton`/`meleeButton = 0`. |
| The squad's weapon wheel | `squad:` lines; `halcyon bridge: the game's weapon wheel {}` | The **left trigger** opens it (`[Controls] leftTrigger = lb`), pointing the left hand at a squadmate preselects them, letting go gives the highlighted weapon. | `[Squad]` keys; `style = native` (the game's wheel) or `custom`. |

### 2.6 The body: arms, stance, calibration

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| Shepard's arms don't follow | `arm IK writeback: idle (...)`, `arm IK: no hand targets (...)` in the heartbeat | No tracked controllers, or not in first person. | First person is Halcyon's K key (its state is saved per profile). |
| The arms or hands look wrong | `arm IK: {} arm upper {:.1f}, forearm {:.1f} units: IMPLAUSIBLE, not driven`; `arm IK: {} hand: finger base bones not found ...` | The skeleton isn't one the IK knows (a mod that changes Shepard's model? **[uncertain]**). | Report it with the log. |
| The view jumps or the body shows from outside for a moment | `arm IK: body anchor: third person (...), view ... from Shepard's animated head bone, ...` | The camera was briefly in a third-person mode. | Report when it happens (after a draw, a load, cover...). |
| The body lags behind or turns oddly when you turn | `torso: re-anchored (...)`, the heartbeat's `torso:` line | Body follow turns the whole body by what the spine can't, and steps under the view (`[Hands] bodyMove = 1`, `leanM` 0.20, `bodyMoveMaxM` 0.75). | Recenter (re-anchors). `[Hands] bodyMove = 0` is the old spine-only lean. |
| The body looks like combat when a weapon is drawn | `stance: off ([Hands] exploreStance = 0)` | Default: the game's combat stance. The relaxed "explore" stance is experimental and **off by default**. | `[Hands] exploreStance = 1` to try it (our menu: Controls > Weapon drawn). |
| Shepard crouches when you don't, or doesn't | `posture: standing eye height {:.2f} m, from {}; Shepard crouches below ...`, `posture: physical {}: {}` | Physical crouch below 0.75 of the standing eye height (stand again above 0.85, after 0.2 s). A wrong standing height makes it wrong. | Calibrate (it measures standing and crouched), or `[Hands] crouchBelow`, `standAbove`; `physicalCrouch = 0` turns it off. L3 still toggles. |
| Shepard takes no cover | `posture: refused a cover entry: ...` | By design: `[Hands] noCover = 1` (ME1's automatic cover snapping is off in VR on foot). | `noCover = 0` brings it back (its camera is third person). |
| The calibration prompt keeps appearing, or never | `calibration: first-run prompt for {} ([{}] has no calibration)`, `calibration: the prompt was dismissed (...)` / `timed out (...)` | Shown once per controller type without a calibration. | Run it from **Body & calibration > Start calibration** any time. |
| An aim won't capture | `calibration: {} aim {}: not capturing: {}` and `calibration: {} aim {}: the hold broke after {:.2f} s: ...` | Each aim is taken on a 1 s steady hold; the panel says why not. | Hold still for a second, aimed at the target. B goes back a step; both sticks held cancel. |
| An aim is redone | `calibration: the {} aims judged: ...`, `calibration: {}: after {} tries aim {} was still {:.2f} deg off the other two: kept the other two ...` | Three aims must agree within 4.5 degrees (rifle) or 3 (pistol). | Normal; after two tries the agreeing pair is kept. |
| The calibration isn't applied | `calibration: controllers {} (section [{}]): {}`; `calibration: the {} stance in [{}] isn't used: its aims disagreed by ...` | Calibrations are saved **per controller type and headset** (`[Calibration.<profile>__<system>]`). Different controllers or another headset need their own. `[Calibration] use = 0` turns it off. | Calibrate with these controllers. |
| Holsters too high or low for your body | `holsters: eye height {:.2f} m from {} (slot scale {:.2f})` | The slots scale with the standing eye height (from the floor, or `eyeHeightM`). | See 2.5's eye height row. |

### 2.7 Comfort and view

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| Turning too fast / slow, accelerating | `comfort: the game turns ([Comfort] turn = game) ...`, `comfort: the game's own stick turn in {}: SFXCameraInput ...` | Default: the game's own turn (it accelerates, differs per camera mode, and its rate can't be set). | Snap turning: `[Comfort] turn = snap`, `snapDeg = 30/45/60` (our menu: Comfort). Or `turn = smooth` with `turnSpeed` (degrees/s; file only). |
| Snap turn doesn't happen | `comfort: {} (camera mode {})` says who turns and why (e.g. `the game turns (no tracked controllers)`) | The mod turns only on foot, not in the Mako, wheels, menus, cover or the scope. | Expected there. |
| No vignette | `vignette: the VIEW space couldn't be made (...)` or `... its swapchain or texture ... couldn't be made ...` | Off by default (`[Comfort] vignette = 0`); or the runtime refused it. | `vignette = 1`, `vignetteStrength`, `vignetteWhen = turn` or `move`. |
| Walking goes where you look, you want the hand | `halcyon bridge: the left stick walks toward {}` | `[Comfort] walk = head` (default). | `walk = hand`: toward where the left controller points. |
| The view jumps forward or back when drawing a weapon (the sniper rifle most) | `camera blend: {} toward {} ... the view stays where it was (it would have jumped N units and slid back), correction #K` shows the fix working; `camera blend: on: ...` at start | The game's camera blend toward the combat camera. Fixed since 2026-10-25 (`[Hands] blendFix = 1`, default). | Make sure `blendFix` isn't 0. If it still jumps, report it with the `camera blend:` lines. |
| The view drops when drawing | `constant eye: on: every standing on-foot camera mode puts the eye where exploring does ([Hands] constantEye = 1, ...)` | Default on. Crouch modes keep their own offset (`constant eye: {} ({}): a crouch mode, Halcyon's Offset ... kept`). | `[Hands] constantEye = 1` (our menu: Steady view). |
| The view turns a few degrees on a first draw | (no line) | Known: the game's aim-point preservation during the camera blend ([section 4](#4-known-limitations)). | None yet. |
| The view is offset, facing the wrong way | `[MEVR-RECENTER] yaw-only recenter (upstream)...` | Recenter is yaw-only, except a badly tilted base space. | Look ahead and recenter (R, double-click the right stick, or the menu). |
| Things look too big or small, double vision on near objects | `[MEVR-STEREO] stereo mode {}: ...; eye distance {:.4f} m = ...` | Life-size stereo is on in our deploy (`sfr2Metric = 1` in the active `MELEVR.ini` profile): mode 1 or 2 in gameplay. **Mode 0** (`stereo mode 0: upstream (eye separation and convergence)`) is normal only on a flat screen (cutscenes, menus); in gameplay it means life-size stereo is off, and upstream's convergence made near things read too far. | Check `sfr2Metric = 1` in the active profile; our menu's Display has the row. Double vision that persists in gameplay with mode 1/2: report it with a snapshot **[uncertain]** (not a known issue). |
| A distant light (the sun) in one eye only | `render levers: one Draw a frame renders both eyes on {} ([Performance] oneDrawWindow = 0)` | Known limit of each eye's own frustum: screen-space effects like the sun's glare are placed through the shared window. | `[Performance] oneDrawWindow = 1` (default). |

### 2.8 Performance

Stereo renders the scene twice a frame. In the heaviest scenes the render thread needs about 11 ms for both, so 90 Hz
isn't held everywhere. **SteamVR halves the frame rate when frames come late** (45 fps at 90 Hz, with reprojection):
a steady 45 isn't the game's real speed.

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| Low frame rate everywhere | `render levers: last 10 s: N frames; scene renders per frame ...` (frames per 10 s / 10 = frame rate); Halcyon's `recommended eye image WxH` vs the `[TARGETCHK]` size | A render resolution above the headset's own costs GPU time. | Our menu: **Display > render resolution > Headset** (the headset's own eye size), or Headset × 0.85 / × 0.75 with **Upscale: FSR** (sharpens it back up). |
| Low frame rate in heavy scenes only | `render levers:` per-render times (`#0 N ms`, `#1 N ms`) | The render thread (CPU) is the limit there. | A lower resolution helps only if the GPU is the limit. Close other programs; on SteamVR, its per-app settings. Measure first (below). |
| Shadows or grass look different from the flat game | `system settings: scale set DynamicShadows False, MotionBlur False, FoliageDrawRadiusMultiplier 0.5 (...)` | The mod turns dynamic shadows and motion blur off and halves the grass radius in VR (each was a large cost; motion blur smears one eye). | `[SystemSettings] apply = 0` keeps the engine's values; or override one key in `[SystemSettings]`. |
| FSR does nothing | `upscale: {} ([Upscale] method = {}, ...)`; `upscale: FSR 1 WxH -> WxH: ...` | FSR runs only when the render target is smaller than the headset's eye. | Pick Headset × 0.85 or × 0.75. |
| Stutter, hitches | | **[uncertain]**: no single known cause. | Measure, then report. |

**Measuring:** our menu's **Advanced > Diagnostics > Measure frame timing** records 20 s into
`%LOCALAPPDATA%\MEVR\perf\` (`<stamp>-frames.csv`, `-ticks.csv`, `-probe.json`; log `perf probe: N frames and N ticks
in N s (...) -> ...`). Attach those three files to a performance report: they say whether the render thread, the GPU
or the game thread sets the pace. The `[Performance]` keys in `LE1MEVR.ini` are experimental render levers: a
player's file with values other than the defaults is untested territory (delete the section to go back).

### 2.9 The menus

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| How to open our menu | `main menu: opened ({})` / `main menu: closed ({})` | **Hold both thumbsticks** (clicked in, about 0.4 s), or press `\` on the keyboard (`^` on a German layout) with the game window in front. A quick tap of both stick clicks takes a snapshot instead. | |
| The menu doesn't appear | `main menu: the document didn't load from {} (deployed? see the rmlui: lines); no menu`, `main menu: swapchain ... failed ...` | The menu files (`ASI\menu`, `ASI\fonts`) are missing, or the runtime refused its layer. | Repair. |
| `\` does nothing | | Since 2026-10-26 the keys count only with the game's window in front (a `\` typed in another program never opens it). | Click the game window. |
| The game is paused | `menu pause: the game paused (...)` / `menu pause: the game runs again (...)` | Our menu pauses the game on foot and in the Mako (`[Menu] pause = 1`). | Close it (Resume or B). `pause = 0` keeps the game running. |
| How to reach the game's own pause menu | `main menu: Game menu: the game's Start goes to the game once it reads the pad again (its pause menu)`, then `main menu: sent the game's {}: held N ms` | Our menu's top bar: **Game menu** (the game's Start) and **Holster** (the game's Back). | If `main menu: the game's {} not sent: {}` appears, quote it in an issue. |
| "Can't save settings" in the menu | `main menu: can't write {} to {} (error N): the game's ASI folder or the file is read-only; ...`, `hands: can't create {} (is the game's ASI folder read-only?): ...` | The `ASI` folder or `LE1MEVR.ini` is read-only (a game under `Program Files` with restricted rights, a read-only attribute, a sync tool). Changes apply for this session only. | Remove the read-only attribute from `LE1MEVR.ini` (and `MELEVR.ini`), or give your user write access to `Binaries\Win64\ASI`. The mod retries every 10 s and logs `main menu: the settings file is writable again (...)`. |
| Renderer settings didn't stick | Halcyon's log: `[config] [MEVR] NOT saved: ...` or `[config] [MEVR] FAILED to save ...`; `main menu: renderer settings not saved ({}): they were reset this session` | `MELEVR.ini` couldn't be read at launch (so saving would have overwritten all four profiles with defaults), or isn't writable. | Fix the file's permissions; restart the game. |
| The menu closed by itself | `main menu: closed (no VR frames for over 1 s: the VR session stopped or the headset sleeps; the game gets its input back)` | By design, so the desktop game is never left paused without input. A running calibration is cancelled the same way. | Wake the headset and reopen it. |
| Halcyon's old menu | | **Ctrl or Shift + `\`**, or Advanced > Diagnostics > Classic renderer menu. A developer fallback. | |

### 2.10 Colour and HDR

| Symptom | Confirm with | Cause | Fix |
|---------|--------------|-------|-----|
| Washed out on a Quest, or too contrasty | `colour: headset: runtime '{}', system '{}', ... -> [Colour.<headset>]; automatic: {} ({})`, `colour: profile {} ...` | `[Colour] profile = auto` picks **quest** for a streamed Quest (Steam Link, or a system name with "quest") and **neutral** otherwise (the PS VR2 included). The presets are judgement calls, not measurements. | Our menu: **Display** (profiles auto / neutral / quest / vivid / custom; hold A to compare with the uncorrected picture). Custom values are saved per headset in `[Colour.<headset>]`. |
| No colour correction at all | `colour: no eye shaders (...): no colour correction`, `colour: eye shader {} failed (...)`, `colour: the eye blit hasn't drawn in {} eye copies: the eyes go back to the raw copy, uncorrected` | The shaders didn't compile (a missing `d3dcompiler_47.dll`?) or the blit path isn't running. | Report it with the log. |
| HDR: colours or brightness off | `hdr display: the display reports colour space {} ({}), {} bits per colour, {:.0f} nits; [Colour] hdrDisplay = {}` | No headset we know of takes HDR output. With the game's HDR range on (and Windows HDR on, or `[Colour] hdrDisplay = 1`), the mod tonemaps the float picture (`[Colour] tonemap = aces`). Measured: **the game's own SDR picture keeps more gradation** than the float path. | Use the game in SDR: `MEVR-Setup.exe` **Settings > Apply** sets HDR off; keep `[Colour] hdrDisplay = 0` (default). |

### 2.11 Settings: where they live, and how to reset one safely

| File | Owner | Holds |
|------|-------|-------|
| `Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini` | the mod | Hands, controls, holsters, squad, grenades, sights, comfort, colour, menu, performance, and **the calibration**. Edited live: changes apply within a second. |
| `Game\ME1\Binaries\Win64\ASI\MELEVR.ini` | Halcyon's renderer | VR mode, resolution, HUD panel, first person, stereo. Four profiles (`[profile:Default]`, `[profile:Custom 1..3]`); `[active] profile=` names the one in use. Saved when our menu closes. |
| `Game\ME1\BioGame\Config\GamerSettings.ini` (and `BIOEngine.ini` there) | the game | Window size and graphics. The setup's Settings page applies the VR values and can restore yours. |

**Resetting one section of `LE1MEVR.ini`** (with the game closed, or live):

1. Open `LE1MEVR.ini` in a text editor (Notepad).
2. Delete the whole section that misbehaves: its `[Name]` line and every line under it, up to the next `[...]`.
3. Save. The mod writes the defaults back for `[Controls]`, `[Holsters]`, `[Squad]`, `[Throwables]`, `[Comfort]` and
   `[Sights]` (log `hands: added the [{}] defaults to {}`); for the others, a missing key simply means its default.
4. **Keep** every `[Calibration.<...>]` section and `[Calibration]`, and `[Arms]` (the arm-length measurement):
   that's the player's calibration. To redo a calibration, run it again; it overwrites its own section.

Deleting the whole file also works (the mod writes a new one: `hands: wrote default calibration to {}`), but it
loses the calibration and every choice made in the menus.

For `MELEVR.ini`: our menu's **Advanced > Diagnostics > Reset renderer settings** resets Halcyon's settings to its
defaults for the session (the menu key, head aim and resolution kept). Don't delete `MELEVR.ini` by hand: without it
the renderer runs on its built-in defaults (Halcyon's log: `[config] no MELEVR.ini at ... - using defaults`), which
aren't the values the setup installs (upstream's menu key is Insert, not `\`). The setup's Install copies it only
when it's missing; **[uncertain]** whether Repair does.

**The keys that matter most** (`LE1MEVR.ini`, defaults):

| Section | Key | Default | What it does |
|---------|-----|---------|--------------|
| `[Hands]` | `handPitch`, `handYaw`, `handRoll`, `gunRoll` | 0, 0, 0, 0 | How the hands and gun sit on the controllers (without a calibration). |
| | `bodyMove`, `leanM`, `bodyMoveMaxM` | 1, 0.20, 0.75 | Body follow: the body turns and steps under the view. |
| | `levelCamera`, `recoilHaptic` | 1, 0.5 | The view stays level while shooting; recoil as a buzz. |
| | `aimAssist` | 0 | 0 = the game's aim-assist friction and adhesion off while the hand aims. |
| | `noCover`, `physicalCrouch`, `crouchBelow`, `standAbove` | 1, 1, 0.75, 0.85 | No cover snapping; physical crouch thresholds. |
| | `exploreStance` | 0 | Experimental relaxed stance with a weapon drawn. |
| | `drawRate` | 6.0 | Draw and holster speed (1 = the game's). |
| | `constantEye`, `blendFix` | 1, 1 | The view doesn't move or jump on a draw. |
| `[Holsters]` | `enabled`, `gripHolds`, `releaseGrace` | 1, 1, 0.3 | Holsters on; the right grip holds the gun; seconds before a let-go gun goes back. |
| | `onlyHolstersDraw` | 1 | Only a holster grab draws on foot (medi-gel and powers excepted). |
| | `eyeHeightFromFloor`, `eyeHeightM` | 1, 1.65 | Slot scale from the floor, or this height. |
| | `hipRight`, `shoulderRight`, `hipLeft`, `shoulderLeft` | pistol, rifle, shotgun, sniper | `weapon, right, up, forward, radius` in metres from the eyes. |
| | `powerWheel`, `hapticAmplitude` | r3, 0.5 | The power wheel / mapped power button; holster buzz. |
| `[Controls]` | `supportGrip`, `supportAcquireM`, `supportReleaseM` | 1, 0.18, 0.26 | The two-handed grip and its grab zone. |
| | `leftTrigger` | lb | The left trigger opens the (squad) weapon wheel. |
| `[Sights]` | `enabled`, `pistol`, `shotgun`, `assaultRifle`, `sniperRifle` | 1, dot, dot, dot, scope 4 | The sight per gun: none, dot, holo, scope N. |
| | `assist`, `converge` | 1, 1 | The gun slides to line the sight up with your eye; shots converge on the sight line. |
| | `scopeFieldDeg`, `scopeEyeBoxM`, `scopeMaxM` | 22, 0.05, 0.70 | The scope's picture size, eye tolerance, range. |
| `[Comfort]` | `turn`, `snapDeg` | game, 45 | game / snap / smooth. |
| | `vignette`, `vignetteStrength`, `vignetteWhen` | 0, 0.6, turn | The comfort vignette. |
| | `walk` | head | head / hand. |
| `[Menu]` | `pause`, `distanceM`, `widthM` | 1, 1.30, 1.45 | Our menu pauses the game; its place and size. |
| `[Calibration]` | `use` | 1 | 0 ignores the calibration. |
| `[Colour]` | `profile`, `hdrDisplay` | auto, 0 | Colour profile; answer HDR to the game (leave at 0). |
| `[Upscale]` | `method` | off | `fsr` upscales a smaller render target. |

In `MELEVR.ini` (the active profile): `vrMode` (2 = Stereo), `sfr2ResX`/`sfr2ResY` (the stereo target), `sfr2Metric`
(1 = life-size stereo), `menuKey` (220 = `\`), `combatHeadAim` and `makoHeadAim` (0: the hand aims, not the head),
`hudPanel` and `handReticle` (the HUD on a panel, the reticle on the aim ray).

---

## 3. Reading each file

### 3.1 LE1MEVR.log

**Format:** `[hh:mm:ss.mmm] [info|warn|error] text`. The time of day only. Indented lines (four spaces) belong to the
heartbeat above them. A crash report (the same text as `crash-breadcrumbs.txt`) is appended at the end after a crash.

**The start.** After the lines in [2.1](#21-the-mod-doesnt-load): the OpenXR and colour lines (`OpenXR: controller
actions ready`, `calibration: headset system "{}"`, `colour: headset: ...`, `floor: the head's height above the floor
comes from {} ...`), the renderer's (`[MEVR-STEREO] ...`, `[MEVR-RECENTER] ...`, `overlays: ...`), then, once the
game's scripts are loaded, `reflect: ready: ...`, `reflect: N of M checks agree (...)`, `head tracking ready (...)`,
`ProcessEvent handlers live on the host's hook (N handlers)`, and **each feature's settings** (`holsters: on, ...`,
`squad: settings: ...`, `throwables: settings: ...`, `comfort: settings: ...`, `sights: settings: ...`,
`hands: calibration ...`, `controls: ...`, `posture: settings: ...`). Those settings lines are what the mod
actually uses: compare them with what the player thinks is set.

**The heartbeat, every 5 seconds** (the `[time] [info]` prefixes left out):

```
heartbeat #N: F fps, GWorld=0x..., objects=N, script hooks live
    head tracking: path camera, source ..., on foot yes
    weapon: ...: in the right hand, grip measured
    host view: valid, head look on (...), head aim off, view offset ...
    arm IK: ...
    arm IK writeback: ...
    holsters: ...; this period: N entries, N draws, N holsters, N refusals, N game draws refused, ...
    squad: ...
    throwables: ...
    posture: ...
    view level: ...
    aim assist: ...
    stance: ...
    comfort: ...
```

- `F fps` is the game loop's rate over the last 5 s (the first heartbeat after a load is low: the load is counted).
- `script hooks waiting for script packages` is normal for the first seconds; minutes of it means the features are off.
- Lines appear only for features that have something to say (sights, the support grip, the torso, calibration...).
- "this period" counts are since the previous heartbeat.
- Every 10 s, separately: `render levers: last 10 s: ...` (frames, scene renders per frame, each render's time),
  `colour: GPU time per draw ...`, `upscale: FSR 1 ...`, and `main menu: open N s: ...` while our menu is open.

**Prefixes:**

| Prefix | Feature |
|--------|---------|
| `game version`, `exe:`, `exe (mod):` | The exe check at start |
| `halcyon:`, `halcyon bridge:` | The renderer and our seams into it: controllers, profiles, menus, first person, the left stick |
| `OpenXR:` | Our controller actions on the renderer's session |
| `reflect:` | Our engine-access layer (and `reflect-check.json`) |
| `head tracking`, `host view:` | The head-tracked view |
| `hand aim:` | Shots and the aim ray |
| `weapon:` | The gun in the hand, its muzzle and grip |
| `arm IK:`, `arm IK writeback:` | Arms and hands following the controllers |
| `torso:` | Body follow |
| `stance:`, `draw:` | The stance with a weapon drawn, the draw speed |
| `support grip:`, `controls:` | The two-handed grip, the left trigger and grip |
| `holsters:` | Body holsters, eye height |
| `squad:` | The squad's weapon wheel |
| `throwables:` | Grenades and melee |
| `posture:`, `cover:` | Physical crouch, no cover snapping |
| `view level:`, `aim assist:` | The level camera, aim assist |
| `sights:`, `[MEVR-SIGHT]` | Red dots, the scope |
| `calibration:`, `hands:` | The guided calibration and its use; the settings file |
| `comfort:`, `vignette:` | Turning, the vignette, walking |
| `constant eye:`, `camera blend:` | The view on a weapon draw |
| `[MEVR-STEREO]` | Life-size stereo: mode, eye distance, fields of view |
| `[MEVR-RECENTER]` | Recentering |
| `floor:` | The floor height from the runtime |
| `colour:`, `[MEVR-COLOUR]`, `hdr display:` | Colour correction, HDR |
| `upscale:` | FSR |
| `overlays:`, `[MEVR-HUD]`, `world panels:` | The HUD panel, reticle, wheel panel, flat screens |
| `main menu:`, `menu pause:`, `menu:`, `rmlui:` | Our menu (and Halcyon's classic menu's VR hands tab) |
| `system settings:` | The engine settings the mod applies |
| `render levers:`, `draw replay:`, `frame loop:`, `perf probe:` | Rendering and performance |
| `process event:` | `process event: off the game thread: ...` (a script call from the wrong thread: worth reporting) |
| `test loop:` | Snapshots, recordings |
| `crash breadcrumbs:` | The crash recorder |
| `first-chance ...` | Caught exceptions: normal (see [section 1](#rules)) |

### 3.2 MELEVR_Log.txt (Halcyon's renderer)

**Format:** `YYYY-MM-DD hh:mm:ss.mmm | [TAG] text`. The file is appended to on every run: find the run by date. Useful
tags:

| Tag | What it says |
|-----|--------------|
| `[XR]` | The OpenXR session: bring-up, `XrResult` codes (0 = success, negative = error), session states ([2.2](#22-no-vr-a-black-or-frozen-headset)) |
| `[XRRUNTIME]` | The runtime's name (`SteamVR/OpenXR`, Meta's...) |
| `[TARGETCHK]` | `Stereo 2 target OK` or `MISMATCH`: the game's window against the stereo target |
| `[PASSCAP]` | `published complete stereo pair`: both eyes captured (normal in gameplay) |
| `[config]` | `MELEVR.ini` loaded, saved, or not saved (`[config] [MEVR] NOT saved: ...`) |
| `[CRASH]` | Its own crash handler's lines |
| `[MEVR-HUD]`, `[MENUVIS]` | The HUD panel and menu quads |

Most other tags are Halcyon's diagnostics; they rarely matter for a player's problem.

### 3.3 MEVRLoader.log (our ASI loader)

**Format:** `hh:mm:ss.mmm text`. A good run:

```
MEVR ASI loader (bink2w64.dll proxy, SPI N) in MassEffect1.exe
loaded LE1MEVR.asi: "..." ...
the exe's code is decrypted (...)
attaching LE1MEVR.asi
LE1MEVR.asi attached
done: N ASI(s)
```

Problems: `no ASI folder (...)`, `{} didn't load (error N)`, `{} faulted in SpiOnAttach (exception 0x...): left
unattached`, `MinHook didn't initialise (...): plugins can't hook`, `the exe's code still looks encrypted after N ms
(...): attaching anyway`. `the launcher: no ASIs here` is normal (the loader also runs in the launcher). The loader
loads **every** `ASI\*.asi`: other ASI mods there are loaded too.

With ME3Tweaks' LEBinkProxy instead, its log is `Logs\bink2w64_proxy.log` (its own format).

### 3.4 crash-breadcrumbs.txt

Written once per crashing process by the mod's unhandled-exception filter (handled exceptions never write it). The
previous one is kept as `crash-breadcrumbs.prev.txt`. Sections, in order:

1. `==== MEVR crash breadcrumbs (research/36) ====`, the date and time, the pid, the exe's base, `host build <date>`.
2. `exception 0x<code> (<name>) at <module>+0x<offset>[, reading|writing 0x<address>]`. Codes: `0xC0000005`
   access violation (reading address 0x0-0xFFFF: a null pointer), `0xC0000409` stack buffer overrun (fast fail),
   `0xC0000374` heap corruption, `0xE06D7363` C++ exception. (A stack overflow writes no report: there's no stack
   left to write it with. Halcyon's `[CRASH]` lines may still have it.)
3. `faulting thread: tid N; the game thread is N`: the same tid = the game thread; another = the render thread or a
   worker.
4. `registers:` (for developers).
5. `native stack (unwound from the fault):` `[0] module+0xoffset`, innermost first.
6. `script frames (FFrames on this thread's stack, innermost first):` the UnrealScript functions, or `(none found on
   this thread's stack)` (a render-thread or teardown crash).
7. `fatal-code first-chance exceptions before it (most were handled):`, times in ms before the crash.
8. `ProcessEvent: the last 128 dispatches, oldest first (...)`: `returned`, `IN ENGINE` or `IN MOD`, the thread
   (`(game)` for the game thread), the function and the object.
9. `the mod's CallFunction handlers: the last 128, oldest first (...)`.
10. `the mod's writes into engine memory, by latest write (...)`.
11. `==== end of crash breadcrumbs ====`.

Rough guide: a crash in `LE1MEVR.asi`, an `IN MOD` entry at the end, or a mod write a few milliseconds before the
crash point at the mod. A crash on a worker thread inside `MassEffect1.exe` with only the game's own events and no
mod writes nearby points at the game (or at memory the mod corrupted earlier: we can't rule that out from here).
**Either way, attach the file.**

### 3.5 reflect-check.json

The mod checks its own engine access (offsets, classes, functions) against a reference at every start:

```json
{ "status": "ready: N objects (...)", "checks": 505, "agree": 505, "mismatches": 0, "notReflected": 0, "rows": [...] }
```

- `mismatches` above 0: some rows have `"agree": false` (`ours` vs `sdk`). Features using them may misbehave or stand
  down. The log says `reflect: N of M checks agree` and lists each one (`reflect:   {} {}: ours {}, LExSDKv2 {}`).
  Report it: it means the game's data isn't laid out as expected.
- A `status` other than `ready: ...`: the layer didn't start (`reflect: our reflection layer is off: {}` in the log).

### 3.6 MEVR-install.json

The installer's record (paths relative to the `Mass Effect Legendary Edition` folder):

- `modVersion`, `setupVersion`, `installedAt`.
- `loader`: `mevr` (ours), `lebinkproxy` (ME3Tweaks'), `other`.
- `files[]`: each with `action` = `added`, `replaced`, `renamed` (e.g. the game's `bink2w64.dll` to
  `bink2w64_original.dll`; Halcyon's upstream `dxgi.dll` to `dxgi.dll.mevr-off`) or `edited` (ini keys, with
  `before`/`after`); `userData: true` marks the player's settings.
- `gameSettings: true`: the VR game settings were applied.
- `packs[]`: texture packs installed through the setup.

Compare it with the file listing: a file the record says was `added` but is now missing (or a `bink2w64.dll` that
isn't ours any more) is what Repair fixes.

### 3.7 The file listing

Look for:

- `Binaries\Win64\bink2w64.dll` and `bink2w64_original.dll` (our loader), or LEBinkProxy's files.
- `Binaries\Win64\ASI\LE1MEVR.asi`, `MELEVR.ini`, `LE1MEVR.ini`, `menu\`, `fonts\`.
- **Other `.asi` files** in `ASI`: other mods that load with ours (untested together).
- `Binaries\Win64\dxgi.dll`, `openxr_loader.dll`, `d3d11.dll`, `dinput8.dll`...: other injectors, Halcyon's upstream
  MELE-VR, ReShade **[uncertain]** which of these conflict. Mention them in the diagnosis.
- Dates: does `LE1MEVR.log` date from the session with the problem?

---

## 4. Known limitations

As of 2026-10-26:

- **LE1 only.** LE2 and LE3 build for development but aren't playable in VR.
- **Releases:** players' builds (MEVR-Setup) go on the GitHub Releases page; before the first one, players build
  from source.
- **Headsets:** tested on a **Quest 3 over Steam Link** and a **PS VR2**, both through **SteamVR**. Meta's runtime
  (Link, Air Link) ran an early build (a Quest Pro, 2026-10-07) but is untested with the current renderer. Virtual
  Desktop is untested. Bindings exist for Index, HP Reverb G2, WMR, Vive wands and the simple controller, but only
  Touch-type controllers have been tried.
- **Performance:** in the heaviest scenes the render thread needs about 11 ms a frame for the two scene renders, so
  90 Hz isn't held everywhere, even on an RTX 4090. A lower render resolution helps only where the GPU is the limit.
- **Much of it is new:** the project started in October 2026; many features have been tried in the headset a few
  times, some not at all.
- **Steam's "verify files"** removes the ASI loader: run the setup's Repair.
- **The assault rifle's dot two-handed:** with the support grip, the two-hand aim turns the barrel off the
  calibrated one-handed stance while the sight's shift stays the one-handed one, so the dot can sit off the sight.
  Not fixed yet.
- **The view turns a few degrees on a first draw** (the game's aim-point preservation during its camera blend): not
  fixed; the jump itself is.
- **The relaxed stance with a weapon drawn** (`exploreStance`) is experimental and off by default.
- **HDR:** the float (HDR) path isn't better than the game's own SDR picture; no headset we know of takes HDR output.
- **Each eye's own frustum** (`[Performance] oneDrawWindow = 0`) misplaces screen-space effects (the sun in one eye).
- **Colour presets** (quest, vivid) are judgement calls, not measured profiles.
- **Halcyon's upstream MELE-VR** must not be installed alongside (the setup moves it aside).

---

## 5. Writing a good issue

Open a **Bug report** at <https://github.com/dnehring/MEVR/issues/new/choose>. Include:

1. **What you did and what happened:** the steps, where in the game (planet, mission, menu), what you expected and
   what you saw. One problem per issue.
2. **How often:** every time, sometimes, once. Since when (a new build, a setting changed).
3. **Headset and runtime:** e.g. "Quest 3 over Steam Link, SteamVR 2.x" or "PS VR2, SteamVR". Your GPU for anything
   about performance.
4. **The report bundle** (`MEVR-report-<date>.zip`, from the **Report a problem** button), or these files:
   - `Game\ME1\Binaries\Win64\Logs\LE1MEVR.log` and `LE1MEVR.prev.log`;
   - after a crash: `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt`;
   - `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt` (the end of it is enough: the last run);
   - `Game\ME1\Binaries\Win64\Logs\MEVRLoader.log`;
   - `Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini` and `MELEVR.ini`.
5. **For anything visual, a snapshot:** our menu's **Advanced > Diagnostics > Snapshot**, or a quick tap of both
   stick clicks, while the problem is visible. The PNGs and JSON go to `%LOCALAPPDATA%\MEVR\snapshots`.
6. **For performance:** the three files from **Measure frame timing** (`%LOCALAPPDATA%\MEVR\perf`), taken where it's
   slow.
7. **For something about movement or aim:** **Record a session** (Advanced > Diagnostics) while you do it; attach the
   `.mevrrec` from `%LOCALAPPDATA%\MEVR\recordings`.
8. **If an AI assistant helped:** paste its diagnosis and the log lines it quoted. Say what you already tried.

**Logs can contain your Windows user name** in file paths. Look through them, and replace it if you prefer, before
you attach them.
