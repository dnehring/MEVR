# Goal: driving the Mako from its cockpit (LE1)

**The goal.** In the headset, the player gets into the Mako through the game's own drop or entry, sits as Shepard in our cockpit, and
plays a whole Mako section from it: drives with the lever and the stick, aims and fires the turret, and sees the game's third-person
view on the centre screen and the turret's view on the right one. Nothing breaks the feeling of being in the Mako (no Mako body in the
eyes, no cockpit on the screens, no lag between the cockpit and the vehicle), and the frame stays within the headset's budget.

Decisions this rests on (the user's, 2026-11-02/03): the cockpit stays with the Mako, pitching and rolling with it; the centre screen is
the third-person view, the right one the aiming picture; Shepard sits in the seat with the hands on the stick and throttle; the Mako's
size doesn't matter (in and out through full blackout transitions); the cockpit keeps each level's colour grade.

Where it stands (2026-11-03, research/47 "Reaching the Mako", "Riding in the Mako"): a drivable Mako without a save
(`tools/mako-start.ps1`); `mevr cockpit ride` keeps the cockpit fixed to the Mako in its full frame, the camera at the cockpit's eye
(seated from the head at entry), the Mako's body hidden from the eyes, the game's chase view live on the centre screen. All of it from
console lines, on the fake headset, controllers untracked.

## Done means

Each line is checked in the game (fake headset with replays where it can be, the headset where it can't) and recorded in research/47.

1. **No console.** In a Mako level with the cockpit installed, entering the Mako puts the player in the cockpit, leaving it (or a
   cutscene, a game menu, death, a level change) takes them out, all by themselves. A setting turns the cockpit off (the chase camera as
   today). Saving and loading inside the Mako come back into the cockpit.
2. **The view.** The cockpit is fixed round the eye at every speed and slope, the seat taken at entry and at each recenter, no part of the
   Mako's body in either eye, no terrain or level geometry through the cabin in a normal drive [verify: the cabin is bigger than the Mako].
3. **The centre screen** shows a third-person view that trails the Mako as the game's own camera does on a pad (behind it, turning with
   it), live at 30 renders a second or better, with no cockpit in it.
4. **The right screen** shows the view along the turret's aim, with a reticle on what the shot will hit and the zoom on R3 (the game's
   `Console_ChangeMagnification`).
5. **Driving.** Grabbing the throttle lever and the stick at the models drives the Mako (research/46: throttle that stays, twist to steer,
   spin on the spot, X jump, Y boost, trigger machine gun, A cannon, thumbstick turret), the models move with the hands (lever travel, stick
   twist), with the haptics of research/46 section 4. Checked with tracked controllers.
6. **Aiming.** Shots land where the right screen's reticle and the cockpit's aim show, from the stick's thumbstick or the free right hand.
7. **Shepard in the seat:** the body seated (pelvis at the seat's `hPoint`, heels on the floor), the hands on the grips while held and
   free when not (arm IK), no Camera/God bone written.
8. **The HUD in the cockpit:** the Mako's own HUD pieces (health and shields, the cannon's charge, the radar) on the cockpit's displays or
   at fixed places in it; nothing of the HUD panel over the screens.
9. **Comfort:** the vignette works for the Mako's turns and bumps (`vignetteWhen`), a recenter in the seat works, and a short headset
   drive is comfortable for the user.
10. **Cost:** at a heavy Mako spot, the cockpit (the parts' per-frame updates, both captures) measured with `tools/perf-probe.ps1`
    against the cockpit off, and the frame within the headset's budget (90 Hz: 11.1 ms) on the dev machine.
11. **Players get it:** the monolithic release build (no LExSDKv2) does all of the above, and MEVR Setup installs and uninstalls the
    package with its TOC entry. The models' licence is settled (2026-11-03: `NOTICE.md`,
    `concept/assets/game-ready/README.md`).

## Status (2026-11-03 12:30; research/47)

| Check | State |
|---|---|
| 1. In and out by itself | Done on the fake headset: the drop cutscene (no cockpit through modes 8, 7, 6; built at mode 1), exit, level load, a save loaded in the Mako, the setting off, the pawn's death (`suicide`). A death by enemy fire and the game-over reload: [verify] |
| 2. The view | Done on the fake headset: fixed at speed and on slopes, no Mako body in the eyes, no terrain in the cabin on a 16 s drive. A nose into a wall: [verify] in the headset |
| 3. The centre screen | Done: the game's chase view, trailing, 30 a second |
| 4. The right screen | Done: along the turret, the reticle on the traced point, R3 zoom 30/12/5 degrees |
| 5. Driving | Done with synthetic tracked hands (lever, the stick now tilting, the models following, the haptics sent); the feel needs the user's headset |
| 6. Aiming | Done: shots land at the reticle |
| 7. Shepard seated | Done: the hips on the H-point, legs to the heels, the hands on the grips while held, shown only while posed; no Camera/God write |
| 8. The HUD | Done: the Mako's status and radar on the left display, the rest high in the windshield |
| 9. Comfort | Needs the user's headset |
| 10. Cost | Done: within 11.1 ms at p90 at `BIOA_UNC52` (the heaviest uncharted world) with the cockpit riding |
| 11. Players get it | Done: the release build, the package and script in the payload, MEVR Setup's install, repair and uninstall with the TOC entry, tested on the fake game |

Also asked for on 2026-11-03: the stick tilts (left/right steers, forward/back pitches the Mako while the thrusters fire) and the
thrusters last 2.5 times as long (research/47 "The stick tilts").

## The work

**Mod (this session's side):**
- Turn the probe into a feature: `game/Cockpit` (spawn the parts on a Mako level's load, ride on entry, out on exit, everything restored),
  `[Cockpit]` in `ASI\LE1MEVR.ini`, the menu row; the probe stays for experiments. Unit-test the frame math in `core/`.
- The trailing chase camera for the centre screen: follow the Mako's yaw (MakoControls' `cameraFollow` for every drive, or our own boom
  behind the vehicle) [decide by trying both].
- The turret's view for the right screen, from the turret's bone along `SetDesiredTurretAimPoint`'s aim; the reticle on the traced point.
- Seat on recenter (the host's floor counter), and the seat kept through a level's own camera changes.
- The lever's and the stick's grab points on the models' grips in the cockpit's frame; the models moved by the controls' state.
- Shepard seated: done 2026-11-03 (research/47 "Shepard in the seat"): the driver shown eyes-only on the H-point, legs and hands by IK.
- The HUD pieces' places in the cockpit (research/41's crops) and the HUD panel moved off the screens while riding.
- Measurements: the frame cost (item 10: done on the fake headset at `BIOA_UNC52`, research/47), comfort settings in the headset.

**Asset Modeling (the models, the package, research/48):**
- The lever and the stick as separately movable pieces with their pivots; the seat's `hPoint`, knee and heel points in the manifest.
- Lights inside the cabin if the lit materials are wanted; displays for the HUD pieces if they go on the console.
- Anything the in-game checks above show (clipping, scale, the screens' size and placement).

**The user:** the headset drives (items 5, 6, 7, 9), the push.

## Blockers

- **Actor.Trace never hits** through ProcessEvent (research/47 "The aim reticle"; task chip "Find why Actor.Trace via ProcessEvent never
  hits"): the right screen's reticle, the Mako's aim point and the sights all need a working trace.
- ~~The models' licence~~ Settled 2026-11-03: the Tripo parts were generated on the owner's paid Max subscription (Tripo's
  Terms 5.2.2: all rights the user's), released MIT with our hull geometry; `concept/assets/game-ready/` and the
  packager's eleven source models are in git on Git LFS (`NOTICE.md`, the folder's README). The raw outputs, concept
  images and working files stay local.

## Order

1. The feature (`game/Cockpit`, no console) with the trailing chase screen: items 1-3.
2. The trace fix, then the turret screen and the aim: items 4 and 6.
3. Controls on the models, checked with tracked controllers in the headset: item 5.
4. Shepard seated, the HUD in the cockpit: items 7 and 8.
5. Comfort and cost in the headset: items 9 and 10.
6. The release: item 11.
