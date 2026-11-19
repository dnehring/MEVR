<!-- SPDX-License-Identifier: MIT -->
# Continuous VR startup: launcher, authentication and scene handoff

**Archived 2026-11-13.** The custom launcher is shelved. This record preserves the prototype results,
source comparisons, and open questions. The startup diagnostics are separate work.

Research initiative, 2026-11-08. Status: source review and a bounded SteamVR null-headset prototype run.
An uninterrupted, focused transition on PS VR2 has **not** been demonstrated.

The [full implementation plan](../docs/vr-launch-continuity-plan.md) records the architecture alternatives,
experiments, failure cases, implementation sequence and acceptance matrix. The [prototype README](../tools/vr-launcher/README.md)
describes what is built. This note records the findings and unresolved questions behind that plan.

## Target and scope

For an already authenticated player, selecting ME1 in our VR picker should retain a tracked VR presentation through
loading, the title screen and gameplay, without Steam's Theater Screen or an extra Resume action. A menu can remain
a panel within that environment; it need not become stereoscopic geometry. The existing in-game menu renderer and
panel placement are reusable, but they do not establish ownership of the runtime's scene or input.

The initial route is the standalone MEVR launcher, with SteamVR/PS VR2 as the first physical acceptance configuration.
Intercepting ordinary Steam Play is deferred. Steam/EA still perform authentication; first-run login, account linking
and client updates need an explicit recovery path. A remembered sign-in acknowledgement cannot establish valid auth.

Related research: [installer and launch chain](33-installer.md), [our menu renderer](28-main-menu.md),
[in-game front end](56-start-and-front-end.md), [Titanfall 2](02-titanfall2-vr.md) and [Overlord](03-overlord-mw2cr-vr.md).

## What the prototype established

The opt-in host renders an RmlUi picker as an OpenVR overlay, takes SteamVR laser events and launches through the existing
Steam/EA paths. It observes the actual game's scene/frame ownership and releases its own overlay interaction. An overlay
can accompany another scene; it does not supply a persistent scene of its own.

The 2026-11-08 null-driver run showed:

| Observation | Evidence boundary |
|---|---|
| Panel visible at about 0.41 s; launch requested at 2.46 s; expected game observed at 6.77 s | Launch and overlay operation, not headset comfort or controller usability |
| Game became frame renderer at 23.69 s; overlay input released at 23.99 s | Rendering ownership and input release, not successful input transfer |
| Dashboard telemetry remained true; game reached OpenXR VISIBLE (4), never FOCUSED (5) before the 55.45 s test ended | No successful seamless handoff; no actual Resume-button press was observed |
| Earlier Theater-on/off runs both remained VISIBLE while rendering VR | That setting alone did not resolve focus in those runs |

Local evidence: `scratch/vr-launcher-20261108-173502/` and `scratch/steamvr-theater-20261108-162741/`. These are private
scratch artifacts, not packaged test fixtures. The run used a private QA ASI to select SteamVR inside the game while
the registered runtime was Virtual Desktop; it does not validate public-build runtime selection. The installed ASI
and nine guarded configuration/marker paths were restored and verified. The follow-up disabled-dashboard experiment
was not run because another test session owned the game/headset.

The original user's report stopped before an XR instance-creation result. That remains a separate diagnostic problem;
the local dashboard/focus result does not establish its cause.

## Three different flat presentations

| Presentation | Distinguishing evidence |
|---|---|
| Steam Theater Screen displays the desktop game window | Runtime presentation/capture and application identity |
| MEVR displays its own boot movie or menu on a tracked panel | Submitted layers and semantic boot/title state |
| Game renders behind the dashboard with inactive input | Dashboard state, fresh XR state and action synchronization |

[Valve's Theater Screen announcement](https://store.steampowered.com/news/posts/?appids=250820&enddate=1700338492&feed=steam_community_announcements)
distinguishes that feature from the older Desktop Game Theater. A flat image in the headset cannot by itself identify
which path is active. The game already submits a black projection beneath quad-only SteamVR frames; adding that again
is not the missing fix.

## What other mods actually show

**Titanfall 2 VR:** the [official installer README](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/README.md)
documents a separate VR entry, an EA sign-in prerequisite and a normal launch that stays unmodded. Its
[Titanfall installer](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/src/Installer/Installers/Titanfall2Installer.cs)
installs a Northstar-based VR launcher, a profile, a runtime probe and a crash monitor, then launches through the monitor.
Its [UI](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/src/Installer/MainWindow.xaml.cs)
asks the player to confirm EA sign-in. Conflict checks, launch monitoring and first-run preparation are useful patterns.
The renderer/monitor implementation was not inspected; there is no evidence here of a particular focus trick or a
guaranteed transition without Theater Screen. The [MIT license](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/LICENSE)
covers the installer and explicitly excludes mod packages/release assets. Big Walk's startup watcher in that repository
is not Titanfall's implementation.

**Overlord:** the [installation guide](https://github.com/Aeka0/Overlord/blob/main/docs/client-installation.md) and
[current entry point](https://github.com/Aeka0/Overlord/blob/main/src/client/main.cpp) show a dedicated executable,
separate launcher/game processes and OpenXR by default with an explicit OpenVR backup. Process isolation is useful;
its OpenVR registration does not establish an equivalent OpenXR handoff. Current upstream differs from our older
local reference checkout, and its loader is not MEVR's Steam/EA chain.

**Half-Life 2: VR Mod:** the [official FAQ](https://halflife2vr.com/faq/) describes a separate Steam application and
SteamVR requirement. That distribution gives it a different launch identity from a mod of the existing Legendary
Edition Steam application. It does not solve our client authentication or process transition by analogy.

## API constraints and open questions

- OpenXR controls visibility and focus. [Session state](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrSessionState.html)
  and [action synchronization](https://registry.khronos.org/OpenXR/specs/1.0/man/html/xrSyncActions.html) must be checked
  separately from successful instance creation or frame submission. An unfocused session has inactive actions.
- Valve added `IVRApplications_008::RegisterSubprocess` in [OpenVR 2.15.6](https://github.com/ValveSoftware/openvr/releases/tag/v2.15.6).
  The [versioned header](https://github.com/ValveSoftware/openvr/blob/v2.15.6/headers/openvr.h) describes transition-UI
  suppression and requires the caller to exit immediately on success. `LaunchInternalProcess` also requires caller
  exit. Our pinned 2.5.1 header exposes `_007`; support must be queried, not assumed or accessed through a cast.
- Unproven: whether the installed runtime supports `_008`, whether Steam/EA's detached process topology qualifies,
  and whether the application association survives the game's OpenXR initialization. These are experiment gates.
- `IdentifyApplication` is not a documented focus-forcing command. No public `HideDashboard` operation was found in
  the reviewed overlay interface. `PerformApplicationPrelaunchCheck` can quit the current scene application, so it
  must not be treated as a passive diagnostic check.
- [Windows foreground requests](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow)
  can be refused and are distinct from XR input focus. A player-opened dashboard must remain usable.
- The old scene must release ownership before waiting for the new scene to focus. Keeping it alive until that event
  can deadlock the transition and violate the handoff API's lifetime contract.

## Next experiments and decision criteria

First isolate the current overlay: compare no-overlay launches, interaction release timing, genuinely closed/open
dashboard states, a known-good fake-headset XR scene, and desktop versus VR launch classification. The detailed
[A1-A5 experiments](../docs/vr-launch-continuity-plan.md#6-experiments-before-committing-to-architecture-b) specify controls.

If that route meets the physical acceptance criteria, retain it. Otherwise test a registered VR scene bootstrap plus
a separate launch monitor: capability query, two small scene applications, real client process topology,
OpenVR-to-OpenXR transfer, then ME1 integration (B1-B5). A permanent external compositor would require a much larger
renderer/pose/input rewrite and is outside this feature's scope. Global dashboard/theater changes are diagnostic
controls or explicit workarounds, not the default solution.

The implementation plan covers correlated launch status, PID plus creation-time identity, session generations,
authentication restarts, stale-log rejection, runtime mismatch, timeouts, crash recovery and continuous in-game startup
presentation. Acceptance requires actual imagery, fresh FOCUSED state and working controller input through title and
gameplay. The proposed primary-route campaign is 20 warm and five cold physical-headset launches; EA-only and other
runtimes are separate coverage. Fake-headset success alone cannot close that acceptance gap.

All primary links are also catalogued in [sources.md](sources.md#continuous-vr-startup-and-focus-handoff-research64-2026-11-08).

## EA-owned launch follow-up: Titanfall reports and source trace

Reviewed 2026-11-08 after the owner confirmed that the reporting player bought Legendary Edition through EA and
also tried adding it to Steam as a non-Steam shortcut. Our local Steam-managed installation is not an EA-only
authentication-path test. The original report already identified the installation as EA, recorded direct-launch
attempts, and showed MEVR attached inside the supported ME1 executable. Its missing XR instance-creation result
is distinct from our null-headset run that reached VISIBLE without FOCUSED. No private report payloads are reproduced here.

### Reports: close analogues, not a confirmed common cause

| Primary report | What was reported | What it establishes for this investigation |
|---|---|---|
| [Installer #68](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/68), opened October 4 | Directly starting the VR launcher looked for Northstar.dll outside the TF2VR profile. An [EA-install user](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/68#issuecomment-5993470312) said launch from the installer worked, while direct executable launch and the attempted Steam shortcut did not. | Closest store/entry-path analogue. EA ownership itself does not prevent VR. Missing launcher-supplied configuration can make an apparently equivalent shortcut fail. |
| [Installer #32](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/32), opened October 1 | After playing VR, both VR and flat Steam launches failed with an EA account/password error. The reporter also mentioned an Xbox Game Pass account. | EA/client failures also occur in Titanfall, but this is not a clean EA-purchase-only case or a demonstrated VR initialization fault. Open with no comments when checked. |
| [Installer #105](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/105) | EA-app players reached VR gameplay but reported constant crouching. | Evidence that the EA route reaches VR for some players; a different failure, not a launch fix. |
| [Installer #35](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/35) | EA Play and Steam users reported an upside-down picture; discussion mentions runtime startup timing, overlays and recentering. | Startup/rendering symptoms cross store boundaries. Suggestions and individual successes do not prove one cause. |
| [Installer #45](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/45), opened October 2 | The reporter authenticated successfully, then the mod and standalone XR probe failed at instance creation on ARM64 SteamOS/Proton. | Useful separation of authentication from XR failure, but the platform is different from Windows/PS VR2. It is not evidence for that player's root cause. |
| [Installer #51](https://github.com/CircuitLord/CircuitLordVRModInstaller/issues/51) | A Linux user could run flat Titanfall, but a separately added installer could not find EA in its Proton environment. | Another environment/entry-path distinction, not a Windows fix. |

Issue #68 was closed as completed on October 5, but the visible discussion contains user workarounds rather than a
maintainer explanation of a shipped fix. One user supplied a wrapper and another reported using a similar script.
Do not present that script as official guidance or copy its hardcoded arguments into MEVR. The source below explains
why the profile matters independently of those comments.

Search scope: public installer issues (EA, launch, OpenXR, flat and PSVR queries), issue comments and broader web
searches for EA-owned Titanfall VR startup failures. No exact Windows/PS VR2 equivalent with a confirmed fix was found
in the reviewed material. Private Discord support was not inspected. A separate Titanfall VR project also appeared
in search; its behavior is not attributed to CircuitLord's mod.

### Installer launch flow, verified in source

Source snapshot: CircuitLordVRModInstaller `3594ce8dabb13c7340a3bfe26af64341d53349f6` (current main when checked).

1. **Locate the game.** Despite its name, [SteamGameLocator.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/src/Installer/Services/SteamGameLocator.cs)
   checks the supplied game's registry key before Steam library folders. Titanfall supplies the Respawn/Titanfall2
   install key. Folder selection validates Titanfall2.exe. The locator returns a directory, not a store entitlement.
2. **Prepare EA sign-in.** [MainWindow.xaml.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/src/Installer/MainWindow.xaml.cs)
   checks that EA's registered client executable exists, opens it visibly and stores the player's sign-in acknowledgement.
   That flag controls the setup badge; it is not a live login-token query. The launch button is enabled from installed/busy
   state, not that acknowledgement. The missing-EA helper offers Steam's first launch without classifying ownership;
   this is a limitation, not an EA-only solution to copy.
3. **Prepare a controlled VR process.** [Titanfall2Installer.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/src/Installer/Installers/Titanfall2Installer.cs)
   uses the same Play/CreateLaunchInfo path regardless of the store used to find the folder. It sets the working directory,
   removes inherited TF2VR/selected VR runtime overrides, enables TF2VR_OPENXR, and starts crash_monitor.exe with the TF2VR
   profile, renamed Northstar launcher and arguments from launch.json. This VR path does not call Steam's game app ID.
4. **Keep installation elevation separate from normal launch.** Its folder-access helper can obtain write access to a
   protected install. [LaunchPermissions.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/src/Installer/Services/LaunchPermissions.cs)
   checks the installer, Steam and EA process tokens; the UI warns that elevated processes can prevent VR starting.
   This is a diagnostic candidate for the MEVR report, whose player tried elevation, not proof of its cause.
5. **Capture the launch separately from rendering.** It rejects conflicting DirectX proxies, records file presence,
   monitor PID and Windows launch errors. [CrashReportService.cs](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/src/Installer/Services/CrashReportService.cs)
   bundles launcher, engine, runtime, frame and incident/latest-session artifacts, with installed and captured mod hashes.

The visible Play method starts the monitor directly. Although xr_probe.exe is installed and the UI displays a headset
resolution-check message, these sources do not show a current direct probe invocation on that path. The
[validation source](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/tests/InstallerValidation/Program.cs)
even checks that Play no longer produces probe-dimension output. The monitor might do additional work internally;
its closed implementation was not inspected. Do not infer successful readiness checking from the UI wording.

### What Northstar adds beneath the installer

CircuitLord pins Northstar release 1.31.13. That release tag's
[flake.lock](https://github.com/R2Northstar/Northstar/blob/v1.31.13/flake.lock) pins NorthstarLauncher to
`1b0ce15673a0027b21639746fd74d4f7da1e504d`. The following trace uses that source revision, rather than assuming current
upstream is identical. This is a build-recipe/source trace, not verification of the downloaded executable's contents.

In [primelauncher/main.cpp](https://github.com/R2Northstar/NorthstarLauncher/blob/1b0ce15673a0027b21639746fd74d4f7da1e504d/primedev/primelauncher/main.cpp):

- `EnsureOriginStarted` returns immediately if Origin.exe or EADesktop.exe exists. Otherwise it finds the client in the
  registry, starts it minimized, waits for its process, then waits for local LSX service data with an eight-second sleep
  in that loop. This is not a robust proof of completed account authentication; the already-running path skips the wait.
- `LoadNorthstar` defaults to the R2Northstar profile unless `-profile=` is supplied. Missing TF2VR selection therefore
  explains the kind of DLL lookup failure in #68. Renaming the launcher does not choose the profile.
- The program loads tier0.dll, Northstar.dll and the game's launcher.dll, then invokes LauncherMain inside its own
  process. It is a game-specific engine launcher, not just a desktop shortcut that spawns the stock game executable.

That last distinction limits transfer to MEVR. Our current code starts MassEffect1.exe, or asks Steam to start the
Legendary Edition launcher and lets our loader start ME1 with inherited client context. Northstar's engine bootstrap
is not an available generic EA launch API for Legendary Edition.

The [installer license](https://github.com/CircuitLord/CircuitLordVRModInstaller/blob/3594ce8dabb13c7340a3bfe26af64341d53349f6/LICENSE)
is MIT for installer source only, excluding mod packages/release assets. This review read public installer and Northstar
source; it did not run, disassemble or copy the closed VR renderer/monitor.

### Consequences for MEVR

- Keep the EA-owned route a first-class test case. Adding an EA executable to Steam does not turn it into our Steam
  app-ID route. Our currently correct EA detection is not proof that the direct launch preserves all needed context.
- Preserve a normal EA-app launch baseline and compare it with Setup's direct launch using the same diagnostic build,
  runtime and headset state. Identify the final ME1 process and correlate renderer startup before blaming the entry route.
- Record client/game integrity levels, launch working directory and arguments, actual process identity, active runtime
  and per-launch XR milestones. Retain meaningful user runtime overrides rather than copying Titanfall's blanket cleanup.
- Add a bounded, isolated XR probe with explicit stage/result reporting if the new instance-creation logs still leave
  ambiguity. A separate probe can distinguish a runtime-wide failure from one that occurs only inside ME1. Release its
  session before game startup; do not treat a passing probe as proof of in-game focus.
- A full process/session monitor and richer reports are transferable patterns. Requiring our launcher for all MEVR
  injection, copying Northstar's eight-second wait, or assuming EA ownership explains this XR failure are not supported
  conclusions. MEVR's ASI already loaded in the player's recorded game session.
