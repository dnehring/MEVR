# Research log

Raw findings behind the plans in `docs/`. Each file records what was found, where it came from,
and what is still unverified. The [docs/](../docs) folder holds the conclusions.

| File | Topic | Date |
|------|-------|------|
| [01-feasibility.md](01-feasibility.md) | Can ME LE get VR at all? Why "build in UDK and port it" fails, and what exists today | 2026-10-07 |
| [02-titanfall2-vr.md](02-titanfall2-vr.md) | CircuitLord's Titanfall 2 VR mod | 2026-10-07 |
| [03-overlord-mw2cr-vr.md](03-overlord-mw2cr-vr.md) | Aeka0's Overlord (MW2 Campaign Remastered VR): features and architecture | 2026-10-07 |
| [04-me-le-modding-ecosystem.md](04-me-le-modding-ecosystem.md) | ME3Tweaks, LEBinkProxy, LExASIs, LExSDKv2, Legendary Explorer, the UDK workflow | 2026-10-07 |
| [05-vr-frameworks.md](05-vr-frameworks.md) | UEVR, REFramework | 2026-10-07 |
| [06-repo-licenses.md](06-repo-licenses.md) | License and size check for every reference repo (GitHub API) | 2026-10-07 |
| [08-ue3-hypotheses.md](08-ue3-hypotheses.md) | UE3 engine mechanisms we plan to lean on (all unverified against ME LE) | 2026-10-07 |
| [09-me3tweaks-code-survey.md](09-me3tweaks-code-survey.md) | LExASIs, LExSDKv2 and LEBinkProxy, verified from cloned source | 2026-10-07 |
| [10-vr-code-survey.md](10-vr-code-survey.md) | REFramework VR internals (extraction notes) and Overlord architecture, from cloned source | 2026-10-07 |
| [11-massfps.md](11-massfps.md) | MassFPS (LE1 first-person camera mod): permissions, how it works, what to keep or change for VR | 2026-10-07 |
| [12-vr-frameworks-survey.md](12-vr-frameworks-survey.md) | Any framework to build on instead? UEVR, VorpX, Geo-11/3Dmigoto, **vrframework** (MIT guide + core) | 2026-10-07 |
| [13-existing-me-vr-mods.md](13-existing-me-vr-mods.md) | **Prior art:** Halcyon's GPL MassEffect1/2/3-VR (working stereo for all three games) and the closed ME2VR (motion controls). What they teach us | 2026-10-07 |
| [14-vrik.md](14-vrik.md) | VRIK: arms and body following the controllers. Why route 1 (SkelControlLimb) is out, why bone queries through ProcessEvent failed, LE1 skeleton layouts, the UpdateSkelPose writeback (route 2), the plan | 2026-10-08 |
| [15-hud.md](15-hud.md) | The HUD in VR (M5): BioSFPanel variables, GFxMovie render-to-texture, the options in order of effort | 2026-10-08 |
| [16-performance.md](16-performance.md) | LE1 VR performance with Halcyon's renderer on Steam Link: the focused-session stall, depth capture (~30%), the cutscene cap, the desktop mirror | 2026-10-08 |
| [17-ghidra.md](17-ghidra.md) | Reading native code with headless Ghidra (`tools/ghidra`): what analysis gives, and the fire path decompiled against research/14 (confirmed; `execTickFire` reaches `0xC621E0` through vtable slot `0x8A0`) | 2026-10-21 |
| [18-camera-and-hands.md](18-camera-and-hands.md) | LE1's camera hangs off the body bone `Camera` (not the head), so body writes that leave it alone can't move the view; the native off-hand IK (`LeftArm` → `LHIK`) follows the combat-idle node's weight and runs before our write; the aimed-fire bonus is the weapon's `bIsZoomed`, set from input, not the camera mode | 2026-10-21 |
| [19-body-holsters.md](19-body-holsters.md) | Body holsters: how VR shooters and VR mods place, size and trigger body slots (sources), Overlord's holsters; how LE1 equips (`SFXGameModeDefault.SwitchToWeapon`, quick slot = weapon type) and holsters (`TryHolster`); the design built | 2026-10-22 |
| [20-melee-and-grenades.md](20-melee-and-grenades.md) | Physical melee and grenades: how VR shooters throw and strike (Overlord, SteamVR, HIGGS, Quake VR...); how LE1 starts them (`TryMelee`, `UseGrenade`), what aims and flies (the native `BioActiveGrenade`, `LaunchGrenadeAtPoint`, the "smart disc" `BioGrenadeProjectileFrisbee` and the unused ballistic class), the melee cones; the design built | 2026-10-22 |
| [21-sights-and-scopes.md](21-sights-and-scopes.md) | Gun sights: Overlord's lens magnifier and optic docs, render-to-texture vs picture-in-picture, collimation; the design built (a collimated dot/holo per eye through `eyeVisibility`, a PiP scope magnifying the other eye's image, aimed fire for scoped shots without the scope camera) | 2026-10-22 |
| [22-performance-levers.md](22-performance-levers.md) | Why LE1 VR sits at half the refresh rate (SteamVR's lock), how a Stereo 2 frame is made, the costs found in the code (a blocking readback, the probe exception storm, our HUD seam's per-draw queries, 15% of pixels outside the lens), and the levers ranked: settings to try, a frame-breakdown probe to build first, code changes by bottleneck | 2026-10-22 |
| [23-cover.md](23-cover.md) | Cover and crouch in VR: ME1 takes cover by itself (BioPawn's native test, no button; A only leaves it) and its cover camera is third person; the test refused for the player (`engine/cover_acquire`); how L3 crouches (`BioPawn.ShouldCrouch`); the physical crouch built (eye height over standing, hysteresis, edges only) | 2026-10-22 |
| [25-calibration.md](25-calibration.md) | The guided calibration: a pistol orb and two rifle orbs aimed at three targets give the gun frame in the controller's grip frame (no more aim pose), where the sight belongs for the player's eye, the dominant eye, the rifle's foregrip, left-hand trim and stock; plus eye heights, arm length, shoulder width; saved per controller type; how the mod applies each; HostServices v15 | 2026-10-22 |
| [24-view-level.md](24-view-level.md) | Why the world tilted while shooting: LE1's recoil is added to the player controller's rotation and the rifles' pitch kick never fades (`RecoilFadeSpeed=0`), with the stick's pitch off nothing brings it back (snapshots: +30 degrees after 204 shots); aim assist's zoom snap turns it too; the level camera built (pitch and roll held at 0 after `PlayerTick`), recoil as a hand pulse | 2026-10-22 |
| [27-colour.md](27-colour.md) | Colour correction per headset: what the eyes get today (the blit already runs), the headsets' OpenXR names (the user's: all SteamVR; PS VR2 `SteamVR/OpenXR : playstation_vr2`), the Quest 3's LCD and its streamed picture (Meta's 13/255 floor, codecs, what users set), the model (on encoded values), the presets (judgement calls), the seam (neutral = identical bytecode) and its cost | 2026-10-22 |
| [28-main-menu.md](28-main-menu.md) | Our VR main menu: RmlUi 6.3 with its own DX11 renderer wrapped for the game's context, our chamfer/backdrop/icon decorators, the frosted glass from the left eye's image, the quad, pointer and pad input, pausing through HostServices v16, what every row drives, the cost | 2026-10-23 |
| [29-body-follow.md](29-body-follow.md) | Why the shoulders left the player: the explore-mode pawn faces where it runs (the spine twisted 60 degrees at most), the torso followed the animation while the hands followed the view (25 units apart), steps moved only the view, head turns moved the eye point, a recenter left the body yaw; the fix: the whole body (not God or Camera) turns and steps under the view, re-anchored on recenter and jumps | 2026-10-23 |
| [30-metric-stereo.md](30-metric-stereo.md) | Life-size stereo: how Halcyon's Stereo 2 renders (eyes on the body's right axis, one symmetric window, a pass-bound submit shift that is divergent with swap on: 1.27 deg per eye, a pistol at 0.5 m read at 0.68 m, far things beyond infinity) and the metric mode: each eye's real place at the view's scale, no shift, each eye's own frustum on the GPU where no UI is in the eyes, records by head-look arm | 2026-10-23 |
| [31-comfort.md](31-comfort.md) | Comfort options: the game's turn is per camera mode with acceleration (no single rate to bind), so smooth and snap turning are the mod's, in its tick before arm IK with the view anchor told (no frame posed for the old heading; body follow doesn't re-anchor); the head-locked comfort vignette and its cost; walking toward the left hand through Halcyon's moveFollowsHead; HostServices v17 | 2026-10-23 |
| [32-ue3-vr-performance.md](32-ue3-vr-performance.md) | UE3 VR performance, what can go: how other VR injectors and engines handle the second eye (shared culling, AER, draw replay, depth stereo), UE3/LE1 specifics (no pre-pass switches in LE1), Umbra, driver/OS/SteamVR settings, and the levers ranked (the second pass from the first pass's scene renderer first) | 2026-10-23 |
| [33-installer.md](33-installer.md) | A standalone installer (`MEVR-Setup.exe`) without Mod Manager: our own `bink2w64.dll` ASI loader (79 forwarded Bink exports, the SPI interface, the DRM wait by sampling `.text`), LEBinkProxy coexistence, launching LE1 past the launcher's picker, LE1 texture packs (ALOT, ISL, LEUITM: Nexus terms forbid automated downloads), Mass Effect Modder's CLI, the manifest and its exact uninstall, the licensing blocker (LExSDKv2) | 2026-10-23 |
| [sources.md](sources.md) | Every URL consulted | 2026-10-07 |

## Conventions

- **Sourced** statements cite a URL from [sources.md](sources.md).
- **[verify]** marks a hypothesis from general UE3/UDK knowledge that hasn't been checked against
  ME LE yet.
- **Not found** results are recorded too, so we don't repeat dead-end searches.
