# MEVR: Mass Effect 1 in VR

<!-- DRAFT for the GitHub release (research/33). tools/package-release.ps1 copies it into the release as README.md. -->

**Source code:** https://github.com/dnehring/MEVR (tag v<version>), GPL-3.0.

VR for **Mass Effect 1, Legendary Edition** on PC: stereo 3D, head tracking, and your hands as Shepard's hands. You
aim with the controller, carry guns in body holsters, hold a rifle with both hands, and throw real grenades. Built on
Halcyon's MassEffect1-VR renderer.

**Mass Effect 1 only.** Mass Effect 2 and 3 are not playable in VR with this mod. The setup changes nothing in their
folders, so they play as before.

## What you need

- **Mass Effect Legendary Edition** from Steam or the EA app, up to date: `MassEffect1.exe` version **2.0.0.48602**.
  The mod stays switched off on any other version.
- **A PC VR headset** and its OpenXR runtime, set as the active one: SteamVR, the Meta app (Quest over Link), or
  Virtual Desktop. See [Headsets and controllers](#headsets-and-controllers) for what has actually been tried.
- Windows 10 or 11 (64-bit) and a recent GPU. VR renders the game twice per frame.

You **don't** need ME3Tweaks Mod Manager. If you already use it, that's fine: the setup finds its ASI loader and uses
it.

## Headsets and controllers

**Headsets tried so far**, all with SteamVR as the OpenXR runtime:

- **PS VR2** (with Sony's PC adapter): the headset most test sessions used.
- **Quest 3 over Steam Link:** used until 2026-10-21, before most of today's features existed.
- **Meta's own runtime** (Quest Link, Air Link): only an early build ran on it (a Quest Pro, 2026-10-07), before the
  current renderer. Not tried since.
- **Virtual Desktop** and every other headset: not tried.

Many features added since late October were checked only on SteamVR's simulated headset, not yet by a person wearing
one.

**Controllers.** The mod offers the runtime bindings for six controller types; the runtime picks one:

| Controllers | What works |
|-------------|------------|
| Touch (Quest, Rift); the PS VR2 Sense controllers, which SteamVR presents as Touch | Everything. The only ones tried |
| Valve Index | Everything; the left controller's A and B are X and Y. Not tried |
| HP Reverb G2 (when the runtime offers its profile) | Everything, laid out as on Touch. Not tried |
| Windows Mixed Reality motion controllers | The trackpad's click is A; no B, X or Y. Not tried |
| HTC Vive wands | The trackpad is the stick; no A, B, X or Y. Not tried |
| The runtime's simple controller (a fallback) | Trigger and menu only: guns, holsters, walking and turning don't work |

The log names the profile the runtime chose and what's missing on it (`halcyon bridge: controller profile ...`).

## Before you start

- **Start SteamVR** (or Quest Link, or Virtual Desktop) **and wake the headset before you start the game.** The game
  connects to VR once, when it starts. If the runtime isn't ready then, the game runs flat on the monitor for that
  run: quit and start it again.
- **Windows SmartScreen and antivirus.** `MEVR-Setup.exe`, the mod (`LE1MEVR.asi`) and its loader (`bink2w64.dll`)
  are not code-signed, and the loader takes the place of one of the game's DLLs (the game's own is kept as
  `bink2w64_original.dll`). SmartScreen may say "Windows protected your PC" (**More info > Run anyway**), and some
  antivirus programs flag DLLs that do this. To check that your download is the published one, compare its SHA-256
  with the one on the release page. In PowerShell: `Get-FileHash MEVR-<version>.zip -Algorithm SHA256`. The source
  code is public, so you can also build it yourself.

## Install

1. Download `MEVR-<version>.zip` from the Releases page and unzip it anywhere. Keep the `payload` folder next to
   `MEVR-Setup.exe`.
2. Start the game once and quit, so it creates its settings file.
3. Run **MEVR-Setup.exe**. It finds the game, checks its version and your VR runtime, and tells you what it will do.
4. Press **Install**.
5. Press **Launch**. It starts Mass Effect 1 directly, past the launcher's game picker.

In the game, both thumbsticks clicked in and held (or `\` on the keyboard) open the VR menu. With new controllers,
the game first offers a calibration of about a minute in the headset, so the guns line up with your hands and the
holsters with your body (any time later: the VR menu's **Body & calibration > Start calibration**).

### What Install changes

Everything goes into the game's own folder, and every change is recorded (`Binaries\Win64\ASI\MEVR-install.json`)
with a backup, so **Uninstall puts the game back exactly**:

- `Game\ME1\Binaries\Win64\ASI\`: the mod (`LE1MEVR.asi`), its menu and fonts, the Mako cockpit's layout
  (`cockpit\cockpit.txt`), Halcyon's renderer settings (`MELEVR.ini`, only if you have none yet; in one you already
  have, Install sets a few values this mod needs, such as the menu key on `\` and head aim off, and keeps the rest),
  and a copy of the setup (to uninstall or add optional content later).
- `bink2w64.dll`: the ASI loader that loads the mod. The game's own file is kept as `bink2w64_original.dll`. This is
  skipped if ME3Tweaks' loader is already installed. If another tool replaces our loader later, Uninstall and Repair
  leave that tool's loader alone.
- **The Mako's cockpit:** its 3D model package, `BioGame\CookedPCConsole\BIOG_MEVR_Cockpit.pcc`, and one entry for it
  in the game's file table, `BioGame\PCConsoleTOC.bin` (the game loads only the packages that table lists). Install
  adds just that entry, and Uninstall takes just that entry out again, so other mods' entries stay. The cockpit is
  always installed; `[Cockpit] enabled = 0` in `ASI\LE1MEVR.ini` keeps the game's own chase camera instead.
- Halcyon's own **MELE-VR** (its `dxgi.dll` and `openxr_loader.dll` in `Binaries\Win64`), if you had it: this mod has
  Halcyon's renderer built in, so Install renames those two files to `*.mevr-off` and Uninstall puts them back. Your
  `MELEVR.ini` stays.
- With a Steam copy of the game: `steam_appid.txt` next to `MassEffect1.exe`, so **Launch** can start the game
  directly.
- The VR game settings (if you leave them on): in `GamerSettings.ini` a borderless window at the VR resolution, VSync,
  motion blur, film grain, dynamic shadows and HDR off, and 16x texture filtering; in `BIOEngine.ini` frame smoothing
  off (the game's 60 fps cap). Your own values are kept, and **Settings > Restore mine** puts them back.

**Steam's "verify files"**, the EA app's repair or a game update can put the game's own `bink2w64.dll` and
`PCConsoleTOC.bin` back: that removes the loader (no VR at all) and the cockpit's table entry (no cockpit in the
Mako). Run **Repair** in the `MEVR-Setup.exe` from the downloaded zip (the copy in the game's `ASI` folder can't
repair: it has no `payload` folder). Repair puts the loader back, adds the cockpit's entry again, and restores any of
the mod's files that are missing or changed.

## Controls

Your controllers reach the game as its gamepad, with hand gestures on top. The **grip** is the button under your
middle finger, the **trigger** the one under your index finger. Left-handed play isn't supported yet. Settings named
below are in `Game\ME1\Binaries\Win64\ASI\LE1MEVR.ini`; most also have a row in the VR menu.

### On foot

| Control | What it does |
|---------|--------------|
| Left stick | Walk, toward where you look (or where the left controller points: VR menu, Comfort) |
| Right stick | Turn (the game's own turn; snap turning in the VR menu's Comfort page) |
| Right trigger | Fire |
| Right grip at a holster | Draw that weapon. Keep the grip squeezed to hold the gun. Let go at its holster to put it away; let go anywhere else and it goes back after 0.3 s unless you squeeze again |
| Left grip on the gun | Hold it with both hands (anywhere along the gun). With the gun raised to your cheek the game gives you its aimed fire |
| Left trigger | The weapon wheel, at your left hand. Point your left hand at a squadmate as you pull to start on their weapon; the right stick highlights a weapon; let go of the trigger to give it. B closes the wheel without a change |
| Right stick click | Hold it and steer with the right stick for the game's own power wheel; let go to close it. A quick click does nothing (cast your equipped power with a push of your left hand) |
| Right stick, double-click | Recenter the view |
| Left stick click | Crouch on or off |
| A | Use what you look at (talk, open, pick up). Hold to sprint |
| B | Nothing on foot (melee is a real strike); backs out of menus |
| X | Hold: the power wheel opens around your left palm; move your hand (or the left stick) toward a power and let go to equip it. Tap: it stays open until you pinch the left trigger. On foot X no longer throws a grenade the game's way (`[PowerWheel] on = 0` gives it back) |
| Left hand pushed forward | Raise your left arm and point at an enemy: brackets lock onto it. Push your open hand toward it to cast your equipped power there (placed powers like Singularity land where you point if nothing is locked) |
| Y | Medi-gel |
| Both stick clicks, held | Open or close the VR menu (or `\` on the keyboard) |
| Both stick clicks, tapped | Save what each eye sees, for a bug report (`%LOCALAPPDATA%\MEVR\snapshots`) |
| Left menu button | The game's pause menu (with the left grip held: holster). Not every headset sends it (a Quest over Link may not): the VR menu's **Game menu** button does the same |

The gamepad's d-pad (squad orders) has no controller button.

**Holsters:** the pistol at your right hip, the assault rifle over your right shoulder, the shotgun at your left hip (a
cross-draw with the right hand), the sniper rifle over your left shoulder. The controller buzzes while your hand is in a
holster. Their places scale with your standing height; the calibration measures it. Only a holster draws a weapon on
foot: the game's own buttons don't pull your gun out for you (medi-gel and powers still do).

**With your hands:**

- **Grenade:** reach across to the left side of your chest with your right hand and squeeze the right grip: you hold
  a grenade. Throw it with a quick release; let go slowly and it goes back. If your weapon is away, taking a grenade
  draws it (Mass Effect 1 only throws with a weapon out). `[Throwables] hands = both` lets the left hand take them too.
- **Melee:** a fast punch or swing at an enemy within about 2 m in front of you, about once a second at most. With your
  weapon away, make a fist with your right hand (squeeze the grip and the trigger) and punch.
- **Vent:** cools a hot gun at once (a change to Mass Effect 1's rules). When the gun's heat is at least 30%, put your
  left hand on the gun just above and ahead of your right hand (a tick in the controller says you're there), squeeze
  the left grip, pull back fast and let go, like racking a slide. Once every 4 s. `[Vent] enabled = 0` turns it off.
- **Crouch:** crouch for real and Shepard crouches; stand up and he stands. The game's cover snapping is off in VR:
  take cover by crouching or leaning behind things.
- **Use:** look at what you want to use. Its name and action float at the object; the A button appears once you're
  close enough.
- **Fingers:** Shepard's index finger follows your trigger, his other fingers your grip.

### Screens and menus

- **VR menu:** both stick clicks held (or `\`). Point with either controller and pull the trigger, or move with the
  left stick and press A. The right stick scrolls; B goes back, then closes. Its top bar has **Game menu** (the game's
  pause menu) and **Holster**.
- **The game's screens** (shop, loot, equipment, journal, codex, map, pause menu) stand as a panel in the room. Point a
  controller at one: its ray is the mouse, the trigger clicks, the right stick scrolls. The gamepad controls work
  there too. The screen's button hints switch to mouse and keyboard once you point; a press of A switches them back.
- **Hacking** (the bypass minigame) is projected from your left wrist, like the omni-tool. Point at it with the right
  controller and pull the trigger. VR menu: Comfort > Game screens > Hacking screen (On my wrist, In the room).
- **Galaxy map:** walk up to the console in the CIC and the galaxy stands over the projector. Point at a cluster,
  system or planet and pull the trigger to open it. The game's own map then shows over the table; answer its travel
  question with the pointer. Walk away from the table to close the map.
- **Conversations and cutscenes** play on a 3D screen in front of you. VR menu: Comfort > Conversations and cutscenes
  (In VR, 3D screen, Flat screen). The dialogue wheel works as on a gamepad.

### In the Mako

Until you take hold of a control, the Mako drives as in the flat game: the left stick drives, the right stick turns the
camera, the left trigger boosts, A fires the jump jets, B gets out. The right controller aims the turret wherever you
point it: the right trigger fires the machine gun, the right grip the cannon.

**The cockpit's controls** (`[Mako] controls = 1`, on by default): you sit in the Mako's cockpit, with the throttle
lever at your left armrest and the drive stick at your right. Bring your left hand to the lever, or your right hand to
the stick, and squeeze its grip to take it. The chase view is on the centre screen, the turret's view on the right one.

| Control | What it does |
|---------|--------------|
| Lever (left grip held) | Push forward to drive, pull back to reverse. It has a notch at neutral and returns there when you let go. X: jump jets (they also right a flipped Mako). Y: boost |
| Stick (right grip held) | Tilt left or right to steer. Twist it to steer harder; with the lever at neutral, a twist turns the Mako on the spot. While the jets or the boost fire, tilt it forward or back to pitch the Mako. It springs back when you let go. Trigger: machine gun. A: cannon. Thumbstick: aims the turret |
| Right hand off the stick | Point to aim the turret. Trigger: machine gun. Grip: cannon |
| B | Get out |

Once you've taken a control, the left stick doesn't drive any more, X, Y and the left trigger work only with the lever
held, and A only on the stick. The game's own controls come back when you get out, open the VR menu, or a controller loses tracking.
`[Cockpit] enabled = 0` keeps the game's chase camera (the lever and stick then show as two flat markers at your knees);
`[Mako] controls = 0` keeps the game's own controls.

## Optional content: texture packs

The **Optional content** page lists texture packs made by others, such as ALOT for ME1, Improved Static Lighting
and LEUITM. They're never included in this download:

- Their authors' terms don't allow re-uploads.
- Nexus Mods only lets premium members' apps download files directly.

So each entry opens the pack's own page. You download it there, point the setup at the file, and it installs the pack
with **Mass Effect Modder** (GPL-3.0, downloaded from its own GitHub releases once you agree).

Texture packs change the game's files **for good**:

- Install them after every other content mod.
- Only a full restore or reinstall of the game removes them.
- Uninstalling the VR mod leaves them in place.

From inside the game, **Display > Optional content > Install after I quit** opens the setup on that page once you
quit.

## Uninstall

Run `MEVR-Setup.exe` (or the copy in `Game\ME1\Binaries\Win64\ASI`) and use **Uninstall**. It removes what it added,
puts back what it replaced, and returns your game settings, except any you changed yourself since. It also deletes
your VR settings, calibration and the mod's logs in the game folder, unless you choose **Keep my VR settings** (for a
later reinstall). **Also delete the mod's diagnostics** (off by default) deletes `%LOCALAPPDATA%\MEVR` as well:
snapshots, recordings, frame-timing captures, crash reports and the downloaded Mass Effect Modder.

If your `GamerSettings.ini` is read-only, the install goes ahead without the VR game settings and says so: untick
Read-only in the file's Properties, then use **Settings > Apply**.

## Known issues

- **Mass Effect 1 only.** Mass Effect 2 and 3 aren't playable in VR.
- **Much of it is new.** The project started in October 2026. Many features were tried in a headset only a few times,
  and the newest (the game's screens in the room, the galaxy map, the 3D cinema screen, the vent, the fingers, the
  wrist and gun panels) only on a simulated headset so far; the Mako's cockpit had two drives in a headset.
- **Performance:** in the heaviest scenes 90 Hz isn't held everywhere, even on a fast PC. A lower render resolution
  (VR menu: Display) helps when the GPU is the limit.
- **Steam's "verify files"** or a game update can remove the loader and the cockpit's table entry: run **Repair**.
- **Equipment and character record** show no 3D model of Shepard or the squadmate while they stand in the room. To
  put those screens back on the flat screen, model included, list every screen but `Inventory`, `XMods` and
  `CharacterRecord` in `ASI\LE1MEVR.ini`:
  `[WorldUi] screens = shop, Loot, Specialization, Journal, Codex, Map, PartySelect, MenuBrowser, Options, Save, Load,
  MessageBox, Achievement, AdditionalContent, SkillGame, DesignerUI, GameOver` (one line).
- **The galaxy map's travel question** may not take the pointer's click.
- **Panels have no depth:** the game's UI panels draw over everything, so the radar can cover the cockpit's throttle,
  and the galaxy map's disc the console's front.
- **The dialogue wheel is small** on the 3D cinema screen. A wider screen (VR menu: Comfort) makes it bigger.
- **Scopes show a plain reticle**, not the game's own scope view.
- **The sniper scope's dark areas show a black speckle.** `[Sights] scopeCapturePost = 0` removes it, but the scope's
  view then loses the game's colour grade.
- **The assault rifle's red dot held with two hands** can sit off the gun's sight.

## Troubleshooting

- **"Unsupported game version"**: update the game in Steam or the EA app.
- **The launcher shows its game picker**: use the setup's **Launch** button, or pick Mass Effect 1 in the launcher.
- **No VR**: start SteamVR (or your headset's runtime) and wake the headset first, then the game. Make sure your
  headset's runtime is the active OpenXR runtime (SteamVR: Settings > OpenXR; Meta app: General > OpenXR runtime).
- **No VR or no cockpit after Steam's "verify files"**: run **Repair** in the `MEVR-Setup.exe` from the downloaded zip.
- **Logs**:
  - the mod: `Game\ME1\Binaries\Win64\Logs\LE1MEVR.log`
  - the loader: `Game\ME1\Binaries\Win64\Logs\MEVRLoader.log`
  - the renderer: `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt`

## Reporting a problem

The troubleshooting guide covers the common problems: <https://github.com/dnehring/MEVR/blob/main/docs/troubleshooting.md>
(your AI assistant can read it).

**The easy way: Report a problem.** In **MEVR Setup**, open **Report a problem**; in the game, the VR menu's
**Advanced > Diagnostics > Report** does the same in the background. It saves `MEVR-report-<date-time>.zip` on your
Desktop with the logs, your VR settings, a list of the files in the game's Win64 and ASI folders, and `REPORT.md`: a
summary (versions, headset, GPU, the errors of the last session, the crash if there was one, the settings you changed)
that ends with a prompt to paste into your AI assistant. Your Windows user name, the computer's name, your profile
folder and e-mail addresses are taken out of every file first. **Nothing is uploaded or sent**: open the zip and look.
Tick the latest snapshot (for anything visual) or frame-timing measurement (for stutter) to add them. After a crash
the setup and the VR menu offer it by themselves.

Then **Open a GitHub issue** (on the same page) opens the issue form with the versions and the error filled in;
attach the zip there.

**By hand**, if you prefer: open an issue at <https://github.com/dnehring/MEVR/issues> with what you did, what you
expected and what happened, your headset and OpenXR runtime, and these files:

- the mod's log: `Game\ME1\Binaries\Win64\Logs\LE1MEVR.log` (and `LE1MEVR.prev.log`, the run before);
- after a crash: `%LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt`;
- the renderer's log: `%LOCALAPPDATA%\MELEVR\MELEVR_Log.txt` (it grows across sessions: its end is enough);
- the loader's log: `Game\ME1\Binaries\Win64\Logs\MEVRLoader.log` (`bink2w64_proxy.log` with ME3Tweaks' loader).

Logs written by hand can contain your Windows user name in file paths: replace it if you prefer.

## Licences and credits

- **This project:** GPL-3.0 (`LICENSE`); our own files are also MIT (`licenses\MEVR-MIT.txt`); third-party notices
  in `NOTICE.md`. Source code: https://github.com/dnehring/MEVR (tag v<version>).
- **Halcyon's MassEffect1-VR renderer:** GPL-3.0. Copyright (C) 2026 Halcyon.
- **Overlord** by Aeka0 (GPL-3.0): parts of the arm IK, two-hand aim, support grip and sight assist.
- **Libraries:** MinHook (BSD-2-Clause), Dear ImGui (MIT), RmlUi (MIT, with its bundled containers' MIT notices),
  FreeType (FreeType License), the Khronos OpenXR loader (Apache-2.0) with jsoncpp (public domain / MIT), AMD
  FidelityFX FSR 1 (MIT). Their licence texts are in the `licenses` folder.
- **Fonts:** Rajdhani and Titillium Web (SIL OFL 1.1; their licences are in `licenses` and `payload\fonts`).
- **Not included:** LExSDKv2 (no licence given). This build runs on our own reflection layer; LExSDKv2 was the
  prior art it was checked against during development.
- **Never included:** RAD's Bink DLL (the setup only renames your own copy), Mass Effect Modder, and any texture
  pack.

Mass Effect is a trademark of EA International (Studio and Publishing) Ltd. This is a fan project, not affiliated with
or endorsed by EA or BioWare.
