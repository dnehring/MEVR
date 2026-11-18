<!-- SPDX-License-Identifier: MIT -->
# SteamVR launcher prototype

**Archived 2026-11-13.** The prototype is shelved. The source and opt-in build instructions are retained for reference.

An opt-in, standalone host for `mevr-menu`, the in-game RmlUi/D3D11 renderer. It renders a rebuilt Legendary Edition
picker into a world-anchored OpenVR overlay, two metres wide and two metres ahead of the headset. Only ME1 is enabled.
This prototype uses the same yaw-only panel placement as the in-game main menu. SteamVR's system laser feeds the same
RmlUi pointer events. The game's captured Scaleform screens still need a running game; this picker uses our menu UI.

The [continuous startup plan](../../docs/vr-launch-continuity-plan.md) covers the remaining focus/theater transition,
comparison with other mods, architecture experiments, failure handling, and acceptance criteria.
The findings and evidence limits are indexed under [research/64](../../research/64-vr-launch-continuity.md).

Build from the repository root in the existing Visual Studio configuration:

```powershell
cmake -S . -B build-mono -DMEVR_VR_LAUNCHER=ON
cmake --build build-mono --config Release --target MEVR-VRLauncher mevr-launcher-tests -- /m:1 /nodeReuse:false /p:TrackFileAccess=false
ctest --test-dir build-mono -C Release -R '^mevr-launcher-tests$' --output-on-failure
& .\build-mono\Release\MEVR-VRLauncher.exe
```

The build copies menu assets and fonts to `vr-launcher/` beside the executable. Keep that directory with it. SteamVR
must be installed and able to track the headset. The program starts SteamVR if needed. It detects the game installation
and uses `install::launch::StartLe1`, including the existing Steam/EA paths and Legendary Edition loader autoboot.
If detection is ambiguous, specify `--game-root "<Legendary Edition folder>" --store steam` (or `ea`). Steam installs
require an autoboot-capable launcher loader; use MEVR Setup to repair it if needed. SteamVR must also be the game's
active OpenXR runtime. Runtime selection is checked and explained in the headset; the prototype does not switch it.

Point at **Launch in VR** and pull the trigger. **Recenter** places the panel at your current heading.
**Close** exits the overlay. Closing while a launch is underway leaves that launch running.
The log is `%LOCALAPPDATA%\MEVR\VRLauncher.log`. Console output mirrors it.

The loading panel waits for an identified `MassEffect1.exe` under the selected install to own changing compositor
frames. It then hides and releases its laser input. Success additionally requires a fresh OpenXR FOCUSED state in
this launch's renderer log and a closed SteamVR dashboard. If focus is still missing after five seconds, a passive
panel explains Resume. It never calls ShowDashboard or synthesizes a system-button press. After two minutes without
a completed handoff, or if the game exits for 20 seconds (allowing an authentication restart), it shows a failure state.
A successful Steam launch request is not readiness.

Useful development switches:

- `--preview <png>` renders the same menu using WARP without SteamVR or the game.
- `--preview-state focus` with `--preview` renders the passive Resume guidance, without interactive controls.
- `--exercise-input` with `--preview` clicks Launch via the overlay-event adapter, checks that a second click is ignored,
  and captures the loading state. It never launches a game.
- `--capture <png>` saves the menu texture shown at startup, not a headset/compositor capture.
- `--capture-headset <png>` saves the compositor's undistorted left-eye mirror one second after showing the menu.
- `--auto-launch` activates Launch after two seconds, for bounded fake-headset runs.
- `--seconds <1..600>` closes the overlay after that many seconds. It leaves a started game running. Exit code 4 means
  the bounded run ended before focus was confirmed; 0 is normal close or confirmed handoff, 1 is initialization failure,
  and 3 is launch/handoff failure.
- `--assets <folder>`, `--fonts <folder>`, `--steamvr-root <folder>`, `--log <file>` override detected paths.
- `--allow-runtime-mismatch` is for private QA builds that select SteamVR inside the game process. It neither changes
  the active OpenXR runtime nor propagates an environment override through an already-running Steam process.

Scope and acceptance gaps:

- This is a SteamVR prototype. Other OpenXR runtimes need a different host strategy.
- The stock EA/Legendary Edition windows may still appear on the monitor. The VR picker replaces their game selection
  when the existing autoboot path is available; it does not capture authentication or error dialogs into VR.
- ME2/ME3 are visible but unavailable because this project currently ships ME1 VR gameplay.
- Controller-event translation can be exercised offline. Real PS VR2 pointing, readability, comfort, and focus handoff
  still require wearing the headset. Null-driver results alone cannot establish those.
- The existing game starts with its own flat boot presentation before its VR title screen. This prototype changes the
  experience before the game owns rendering, not that game's boot sequence.
- The prototype is off by default and is not included in the release package. The OpenVR v2.5.1 header is vendored under
  BSD-3-Clause; the API binary comes from the user's installed SteamVR.

Sources: [Valve's overlay API](https://github.com/ValveSoftware/openvr/blob/v2.5.1/headers/openvr.h) and
[overlay overview](https://github.com/ValveSoftware/openvr/wiki/IVROverlay_Overview).

## Validation, 2026-11-08

Release builds and the handoff tests passed. WARP previews exercise the actual RmlUi document, renderer, pointer-event
translation and duplicate-launch guard. The preview was inspected and the Launch button's overflow was corrected.

A 55-second null-driver run with SteamVR's dashboard enabled reached these milestones after the overlay initialized:

| Time | Observed result |
|---|---|
| 0.41 s | OpenVR accepted the room-anchored menu texture and ShowOverlay. |
| 2.46 s | Shared Steam launch requested LE1. |
| 6.77 s | Correct installed LE1 process detected. |
| 23.69 s | SteamVR reported that process as both scene owner and latest frame renderer. |
| 23.79 s | Fresh OpenXR state VISIBLE (4) observed. |
| 23.99 s | Launcher hid the overlay and released system laser input. |
| 29.02 s | Passive Resume guidance shown because focus was still missing. |
| 55.45 s | Bounded run ended with incomplete-handoff exit code 4. |

The dashboard remained open throughout. No FOCUSED (5) state was observed. This proves the launch/render/release path
on the null driver and exercises the honest incomplete-handoff path; it does **not** establish that Resume is eliminated.
The run used the existing private QA ASI that selects SteamVR inside the game process, because this machine's registered
runtime is Virtual Desktop. The installed ASI and all nine saved configuration/marker paths were restored and verified
by hash or original absence. Evidence is local under `scratch/vr-launcher-20261108-173502/`.

A follow-up with the dashboard disabled and a compositor capture was deferred because an independent performance-test
session started using the game and SteamVR. The optional compositor-capture path is compiled but has not been validated
live yet. The screenshots in `scratch/vr-launcher-preview/` are renderer previews, not headset captures.
