<!-- SPDX-License-Identifier: MIT -->
# First person on the sequels: ME3 pinned, no cover snapping, the camera's collision (lane FP3, 2026-11-17)

The owner will test ME2 and ME3 in the headset and asked us to finish what he would notice. Two of his rules ME3 didn't
follow: **first person only** (ME1's one supported configuration, `MevrBridge::ModernBaseline`; ME2's `ApplyMevrBaseline`)
and **no cover snapping in VR** (docs/trilogy-plan.md rule 7). And the parity ledger's G12 (ME1's `game/CameraCollision`)
was "missing" on both sequels without anyone having looked. This note is what was changed, what the runs showed, and what
is still open. Ledger rows R32, G12, G39; plan entries T01, T28, T31.

## 1. First person on ME3, ME2's baseline

**Before:** Halcyon ME3 kept first person in `engine_probe.cpp`'s `g_fpEnabled`, false at start, toggled only by K (no
`[FirstPerson] Enabled` key in `MELE3VR.ini`), so ME3 started in third person. Its `me2_menu.cpp` pinned only life-size
stereo after an ini load; the rest of ME1's/ME2's baseline was either compiled out (`#if !defined(MEVR_ASI)`) or left to the
file (`DecoupledPitch` read with default 0: the right stick tilted the view; `ProfileHotkeys` 1: 1-4 loaded settings
profiles; `RecenterKey` R, ME3's reload; `MenuKey` Insert).

**Now** (`[MEVR]` in `src/halcyon/le3/me2_menu.cpp` and `engine_probe.cpp`):
- `ApplyMevrBaseline()` runs at the end of every `LoadValues()` (the menu's start and every profile load), ME2's list on ME3's
  keys, each change named in one line `[MEVR-BASELINE] N setting(s) in MELE3VR.ini set to the MEVR baseline: ...` (or
  `... already matches the MEVR baseline`). Nothing is written to the file (the menu's own Save writes what is in force).
  The items and the rule each one keeps (docs/trilogy-plan.md "What the game should be"):
  - stereo mode 4, the Quest image fix, the view filled, lean, look speed 1: ME1's one configuration (rule 1);
  - flat menus (`MonoMenus`): ME1's choice;
  - **first person**, with **Shepard's head hidden** in every first-person state (`[FirstPerson] S*_hideHead`): ME1's
    `fpEnabled`, `fpHideHead` (rule 1; the owner: first person only);
  - **no head aim** (rule 4; ME3's host also forces it off in `SetHeadAimEnabled`);
  - **the stick doesn't pitch the view** (`DecoupledPitch`, rule 6);
  - **no VR conversations or cutscenes** (`CineVrConvo`, `CineVrCutscene` off): a conversation stays a cinematic, shown on
    the shared 3D cinema screen when ME3's renderer offers it (`me2_xr.cpp`'s `mevrScreen`, rule 9) or on Halcyon's flat
    cine panel otherwise; never a conversation rendered in the world (first-person conversations stay deferred, rule 9);
  - no profile hotkeys (ME3's 1-4 are its own keys), no recenter on R (ME3's reload; double-tapping Back, the menu's
    Recenter view and opening our menu still recenter), the menu key `\` instead of upstream's Insert (rule 16): ME2's.
- `g_fpEnabled` starts true in the host build (`MEVR_ASI`), as ME2's does, so first person holds from the first frame.
- K stays the developer toggle for the session (ME2 keeps it too); the next ini load pins first person again.
- The renderer preview's own `MELE3VR.ini` (`tools/le2-halcyon-run/session-common.ps1`, `Get-PreviewRendererConfig`) needs no
  `[FirstPerson]` key: the pin is in memory and wins over the file, and the runner's check that the file is unchanged holds.

**Seen on the fake headset** (ME3 renderer preview, Save 31, 2026-11-17 06:24-06:30, session `20261117-062321-170-LE3-fp3-me3`):
- `[MEVR-BASELINE] 3 setting(s) in MELE3VR.ini set to the MEVR baseline: profile hotkeys (ProfileHotkeys), the recenter key
  R (RecenterKey), the menu key \ (MenuKey; upstream Insert)` (the preview's file already sets the rest); the snapshot's
  `firstPerson.actualEnabled` true, `rendererReportsOwned` true with no `firstperson` request.
- At rest with no controllers (`06-25-05-975`): the CIC door ahead at eye height, no head; Shepard's shoulders fill the
  bottom of the view: with no controllers neither arm IK nor the body follower runs (`torso.driven` false), and ME3's
  first-person game camera stands 35 uu behind the pawn's centre along the view (measured below; ME2's stands 30 uu
  ahead of it). With the hands replayed (`06-25-18-160`, `06-25-36-404`): the body slid under the view (`torso: first person:
  the whole body under the view`), both hands on the controllers in view (`armIk.hands` in view, 0.07 m from the grips), no
  shoulders. In the headset the controllers are always there; the fake headset without a replay is the only case that
  shows them.
- A walk (the stick forward 9 s) and a turned walk (the head 70 degrees right: Shepard walks and turns that way) kept first
  person throughout; the map on the wrist (`mevr omni map`, `06-28-25-001`): the rail beside it, the omni-tool's gauntlet and
  fan on Shepard's arm (`omni tool: on Shepard's arm (... OmniTool_LeftFull_VCFX_M ...)`), the view held at Shepard's
  (`[MEVR-HELDVIEW] gm 9: a screen of ours on the wrist holds Shepard's view on foot: first person's hides kept`, 624 held
  views), back to game mode 0 on close with `the held view ended`; the Mission Computer's squad page opened and closed the
  same way (on the flat panel, as lane OM2 left it). No stall: every snapshot came on time, `game thread calls: ... 0
  faulted`, no `off the game thread` line. ME3 restored with `verification: 0 differences`.
- War Assets wasn't walked to: the change only makes first person the default; OM3's runs of the room screen already had
  first person on (research/le3-native-gui.md), and its view hold (`WorldViewHeld`) is untouched.

## 2. No cover snapping on ME3

**Before:** `game/Posture.cpp`'s LE3 branch refused `SFXGameModeDefault.FindCover` inside `TryAcquireCover` only after an
explicit `mevr cover snap off` (and ME3's console had no `cover` route, so nothing could ask for it). ME3 snapped by default.

**Now:** the LE3 branch follows ME1's and ME2's `[Hands] noCover` (default 1; `mevr cover snap on|off|auto` for an A/B):
`OnLe3FindCover` answers FindCover's result false for the player's mode inside `TryAcquireCover` (A's exec reaches
TryAcquireCover through ProcessEvent; FindCover inside it is a script call, so the CallFunction frame post-handler sees it:
gotchas/binding-execs-come-through-process-event), on foot with the host's controls fresh, the native movement admission
and no authored action (ME3's adapter rules, unchanged). `RefusingCover()` no longer waits for the CallFunction hook to be
in already: Mod.cpp puts it in while the snapping is refused, as on ME2. The refusal logs like ME2's (`posture: refused a
cover entry: SFXGameModeDefault.FindCover found cover for TryAcquireCover ... ([Hands] noCover: no snapping in VR)`, at
most every 2 s; the heartbeat counts the rest and `TryAcquireCover's FindCover found nothing N times`). Cover already
taken (a script's) is left to the game: ME3's adapter never forces an exit (research/le3-locomotion-audit.md).

VR cover's damage rule on ME3 (`game/Le3Cover.inc`, unchanged as a rule) answers `IsInCover` only while the snapping is
refused, so it is **on by default too** (the coordinator's yes, 2026-11-17: the owner's rule 7, "the game's cover
protection is kept"). Each answer is now logged plainly: `vr cover: LE3 answered IsInCover TRUE for this hit through
<CoverLink> slot <n>: the game's own cover rules decide the damage` or `... covered but no authored cover slot within reachM
qualified: answered nothing, the game's full damage` (at most one line every 0.5 s; the heartbeat counts both). Its new
`[Cover]` block text says what it does (a file that has the old block keeps it).

Console on ME3 (new routes in `RunOursLe3`): `mevr cover [snap on|off|auto | where | links [n] | acquire [wide]]`; `acquire`
calls the player's mode's `TryAcquireCover(bWideCheck, bTestOnly = true)` through ProcessEvent (a test only: the game takes
nothing) and says whether the refusal's handler saw its FindCover.

**Seen on Save 31:** `posture: settings: cover snapping off (noCover = 1)`, `vr cover LE3: scoped real-slot candidate
registered; it judges hits while the snapping is refused`, `mevr cover where`: `cover snapping refused` with the hands
replayed (`the game's` without them: the host's controls aren't fresh). The Normandy has **no cover**: `mevr cover links`
found 0 CoverLinks, and `mevr cover acquire [wide]` returned FALSE with the handler reached 0 times: on the Normandy's
non-combat pawn `TryAcquireCover` returns before it asks FindCover. So neither the refusal nor VR cover's answers were
seen in the game; research/le3-combat-fixtures.md "First on the combat save" is the check, before anything else there.

**Not ported:** ME2 hides the HUD's cover prompt again after every `ShowCoverWidget` (A offers nothing). ME3's prompt is
`SFXGUIInteraction.ShowCoverWidget(PlayerController)` / `HideCoverWidget(PlayerController)`: a ProcessEvent post-handler on
the first calling the second with the same controller would do it; not built, since no save here shows a prompt.

## 3. The camera's collision in first person (G12)

ME1's symptom (gotchas/camera-collision-in-first-person): each camera mode's `bCollisionEnabled` pulls the camera back to
its hook when anything stands between them, and Halcyon ME1's first-person camera sits 0.5 m ahead of the hook, so a wall
just ahead dropped the eye behind Shepard's knees. ME1's fix is `game/CameraCollision`.

**On the sequels Halcyon already does it:** `FpApplyOffset` in `src/halcyon/le2/engine_probe.cpp` and
`src/halcyon/le3/engine_probe.cpp` clears `bCollisionEnabled` (bit 0x02 of `SFXCameraMode`'s flag dword, ME2 +0x214, ME3
+0x11C, both as LExSDKv2's layout says) on every mode it writes the first-person offset to, and puts it back with the
vanilla offset; ME2 clears it on the interpolation too, ME3 on the interpolation's To.

**Measured** with `mevr camera trace` (ME3 too since this lane; on the sequels its rows carry `modeCollision` and
`toCollision`, the current mode's flag and an interpolation's To's; ReflectCheck rows `SFXGame.SFXCameraMode.bCollisionEnabled`
for LE2 and LE3, and ME3's camera classes): the game camera against the pawn in the pawn's facing, while a replayed stick
pressed Shepard into geometry.
- **ME3, Save 31:** forward into the galaxy map's ring (the pawn held at 12 uu/s for 5 s) and backward into the corridor's
  end (12 uu/s for 5 s): the flag 0 in every tick (1,204 and 915 rows), the camera 35.0 uu behind the pawn's centre along
  the view and 55 up, unchanged (34.9 to 35.3) while pressed; no interpolation happened (`toCollision` -1).
- **ME2, Save 4 (Freedom's Progress):** forward into a wall (the pawn stopped dead for 4 s with the stick pushed) and backward
  into another (5 s): the flag 0 in every tick, the camera 30.0 uu ahead of the pawn, 5 left and 75 up, unchanged to 0.1 uu.
  This is ME1's geometry (the camera ahead of the hook, the wall between them) and it did not pull back.

So G12 is **n/a** on both: nothing to port. One gap: ME3 never clears the `SFXCameraMode_Interpolate` object's own flag
(only its To), so during a blend between camera modes (a draw, a sprint's storm camera) the game could pull the camera for
the blend's length. On the combat fixture, a draw under `mevr camera trace` shows it (`modeCollision` 1 on the Interpolate's
rows with the forward offset dropping); if so, clear it there as ME2 does (`[MEVR]` in `FpApplyOffset`'s Interpolate branch).

## Runs and restores

ME3: `prep.ps1 -Holder fp3 -Game LE3 -RendererPreview -Tag fp3-me3`, launch, `load.ps1 ... -Save
John_12_Vanguard_021024_46e6e39\Save_0031.pcsav`, the run, `-Stop`, restore (`verification: 0 differences`). ME2: the same
without the ME3 switches, Save 4 (`verification: 0 differences`). The run's steps: a snapshot at rest; `mevr camera trace 40`;
`make-walk-recording.py --hands view --pitch -15 --plan stand:4,forward:9,stand:30` replayed with `-Inputs`; snapshots;
`mevr cover where|links|acquire [wide]`; then `--plan stand:2,back:10,stand:40` the same way, a turned walk (`--yaw 70`), and
on ME3 `mevr omni map|squad|close` with the hands still replayed. LE1 isn't touched: its preprocessed code is the same (the
CameraProbe change is behind `MEVR_GAME_LE2 || MEVR_GAME_LE3`; Posture's and the console's under `MEVR_GAME_LE3`).
