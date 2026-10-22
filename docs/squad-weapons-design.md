# Squad weapons in VR (option A built 2026-10-22, to test in the headset)

**Since the evening of 2026-10-22 the default is the game's own wheel** (`[Squad] style = native`,
the next section): the user wanted ME1's actual UI, its pause and its rules rather than our drawing.
Our drawn menu ("As built" below) stays as `style = custom`.

## The game's own wheel (style = native; built 2026-10-22, to test)

The gesture stays; what it opens is ME1's weapon wheel (`SFXGameModeWeaponWheel`, game mode 3), shown
on a panel at the hand. The game then does everything the flat game does: its look, its pause (its
`Activated` calls `PauseGame`), which squadmate can hold what, and applying the choice when it closes.
How the game's wheel works: [research/19](../research/19-body-holsters.md) section 10. How it's cut
out of the UI: [research/15](../research/15-hud.md), "The wheels on their own panel". Test steps:
notes/next-vr-session.md step 13.

| | Built | Why |
|-|-------|-----|
| Open | The same gesture as before (point and squeeze the left grip away from the gun; or hold it `openHoldS` at nobody; a squeeze at nobody next to the foregrip is a missed grab), then `SFXGameModeDefault.EnterWeaponWheel()` through ProcessEvent: exactly what holding LB does once the binding's 0.3 s have passed | Holding LB through the pad would add the 0.3 s, and a squeeze let go sooner would be LB's *tap*: `TrySwapWeapon` (Shepard's weapon swapped). The exec has no such case |
| Close | Letting go of the grip: an A for the highlighted entry if there's something new to give (below), then `SFXGameModeWeaponWheel.ExitWeaponWheel()`, what releasing LB does; its `Deactivated` applies the queue. B closes without the A. If the game closes it itself (LB, the pause menu), we just follow | The wheel needs A to pick (its own rule); "let go to give" was the user's control for our menu and is the natural VR gesture |
| Pick | The highlighted entry's A is sent through the pad (`kFlagWheelPressA`, 80 ms), then we wait (at most 0.4 s) until that member's squad entry has taken it (`GetMemberSwitchWeapon`), then exit. No A when the entry is their weapon in hand or one they don't carry, or one already queued. A by hand works as in the flat game (pick for several squadmates, then let go) | The console movie only queues on A (`selectWeaponOrPowerInTacticalMode`); waiting for the queue makes sure the exit doesn't beat it |
| Steer | The right stick, as the game binds it (`wheelStick = left` or `either`: the left stick, on the hand that holds the grip, sent as the right stick). A stick already pushed when it opens (turning) reaches the game only after it has been centred once; the left stick never walks; B and the stick clicks are kept from the game; whatever is held when it closes stays off until let go | The wheel reads the right stick's angle; the gate stops a turn from picking an entry the moment it opens |
| Pointing preselects | Yes: the wheel has no "active squadmate", only one ring whose angle picks a member and a weapon together. Its HUD keeps the last stick angle while the stick is centred (`BioSFHandler_HUD.nLastAngle`), so on opening we set it to the pointed squadmate's weapon in hand. Pushing the stick takes over at once | "Steer it there with simulated input" without simulating anything: the same state the stick would leave. A no-op if confirmed (their own weapon), so nothing happens by accident **[verify]** |
| Feedback | A tick in the steering hand on each new highlight (read from the HUD's `nAngle`, mapped through the movie's own table), a soft buzz on a weapon they don't carry, a firmer click on the pick, softer on a cancel; the game's own highlight sound plays too | Meta's haptics guide, as before |
| Presentation | The host cuts the wheel's part out of the UI frame and shows it on its own quad, placed when it opens 0.27 m above the left hand (`panelDistanceM`), 0.40 m wide (`panelWidthM`), fixed in the room while it's open; the HUD panel shows the rest with that part cut out. Any way the wheel opens (our gesture, LB) it goes there; the power wheel (R3 held) goes to the right hand (`powerWheelAt`) | The user's request; the HUD stays where it is |
| Only what they carry | The game's own rule (the movie greys what they don't have) | |
| Time | The game's wheel pauses the game (`Activated` → `PauseGame`); `pause` is for our menu only | Wanted: like the flat game |
| Shepard's weapon | Opening it draws Shepard's weapon if holstered (`EnterWeaponWheel` calls `DrawPrimaryWeapon`), as in the flat game | Kept as the game does it; **question for the user** |

**[verify] in the headset:** that the console HUD movie (`ME_HUD`) is the one the wheel is drawn by
with a pad (the log names the HUD handler and whether its panel uses the gamepad); that A from our pad
queues while the game is paused; the crop (measured once per session; `wheelCrop` by hand); the
preselect; that nothing of the wheel stays in the eyes.

**Not done:** a "point at the squadmate's arc" remap of the stick (the stick's direction picks the
entry absolutely, as in the flat game, so the stick has to go to the squadmate's side of the ring).

## Our drawn menu (style = custom)

Shepard's own weapons now come from body holsters ([research/19](../research/19-body-holsters.md)).
Squadmates' weapons needed the game's weapon wheel (the left trigger, LB). This note proposed a
VR-native way to choose them and recommended one; the user approved **option A** on 2026-10-22 with
a UI concept (two squadmate cards, a four-way radial of weapon tiles, the left stick for the
squadmate, the right stick for the weapon, confirm and cancel hints; dark translucent blue panels,
bevelled or hexagonal tiles, cyan outlines, amber highlight). The proposal below is kept as written;
**"As built"** says where the build differs and why. Test steps: notes/next-vr-session.md step 12.

## As built

| | Built | Why |
|-|-------|-----|
| Open | Point the left hand at a squadmate (within `pointConeDeg` 10° of the left aim ray and `pointMaxM` 40 m; the smallest angle, a tie within a degree going to the nearer) and squeeze the left grip away from the gun's foregrip: it opens at once with that card chosen | Pointing is explicit (Arizona Sunshine 2's orders) |
| Open without pointing | Keep the grip squeezed `openHoldS` (0.35 s): it opens with the squadmate last given a weapon, else the one nearest the ray | **Held, because** a squeeze at nobody is often a grab that missed the foregrip: opening at once would put a menu in the player's face mid-fight. A miss right next to the gun (within the support grip's release distance) never opens it, and a quick squeeze-and-let-go at nobody does nothing |
| Who decides it's "away from the gun" | The host's pad mapping, on the new squeeze (the same decision that makes it the support grip or not), counted and reported (`HostServices::leftGripAway`, v11); the two-handed grip keeps priority, and taking it closes the menu | No race between the mod's tick and the mapping |
| The squadmate | Left stick left or right picks the left or right card. The cards are ordered by where the two stand (the one further left on the left) when it opens | The concept's layout, and it matches the world |
| The weapon | A flick of the **right** stick into a new sector (past 0.55, kept until back inside 0.35, 12° of hysteresis at the diagonals) highlights that tile; centred, the choice stays. Fixed places: assault rifle up, shotgun left, sniper rifle right, pistol down | The user's controls (the proposal had the left stick); fixed places are muscle memory, like the holsters. A stick already pushed when it opens (walking, turning) chooses nothing until it moves on |
| Only what they carry | Tiles only for the types in their quick slots; the others are hidden (since f27cae4; first faint outlines), and a flick toward one gives a soft refusal | The user's rule |
| Confirm / cancel | Letting go of the grip confirms the highlighted weapon, or cancels with none; A confirms (with one chosen), B cancels. `confirmOnRelease = 0`: letting go keeps it open (A gives it; B, a new squeeze or 15 s untouched cancels) | The user's controls |
| Input while open | The host keeps the sticks, A, B and both stick clicks from the game (`gamepad::kFlagSquadMenu`), and each one in use when it closes stays off until released or centred | The right stick turns and R3 is the power wheel; the flick and the confirming A must not leak |
| Time | First built running (`pause = 0`); since f27cae4 (user, 2026-10-22) **`pause = 1` by default**: `WorldInfo.PauseGame` like ME1's weapon wheel, unpaused before applying, as the wheel's close does | The user wanted the flat game's pause. Slowing time (`TimeDilation`) was not built: the game sets it too, so we'd fight it |
| The panel | A quad above the left hand: placed once when it opens, 0.27 m (`panelDistanceM`) from the grip, mostly up and a little further out from the head, level and facing the eyes, 0.40 m wide (`panelWidthM`), fixed in the room while open (not on the jittery hand); placed again on a recenter | The user's numbers |
| Drawing | Its own ImGui context and its own texture and swapchain in the host (Present thread, the game's context saved and restored), drawn as the "squad" quad layer: in snapshots as `-squad.png` and in the eye composites. `tools/squad-preview` renders it to PNGs offline | Halcyon's menu keeps its own context; Claude can check the look without a headset |
| Haptics | A tick on each highlight (the hand whose stick moved), a firmer click on confirm, softer on cancel or refusal (`hapticAmplitude`) | Meta's haptics guide |
| Applying | What the weapon wheel's close does, for that one member (below) | The game's own path |

**Adapted from the concept:**
- kept: two cards left and right (name, class, a ten-segment health bar, the weapon in hand), the
  central four-way radial with a dial pointing at the choice, amber highlight, cyan outlines, dark
  translucent blue panels, hexagonal tiles, the stick and A/B hints;
- portraits: a hexagonal class badge with initials, coloured by the class's discipline (combat
  red, tech blue, biotic violet), instead of the game's portrait textures (not used: no game
  images);
- weapons: ME1's four types as simple vector silhouettes and labels;
- left out: the HUD around it (objective, own health, ammo), which the game's HUD panel already
  shows; "hold LB" became "release the grip to confirm".

**Applying, verified in the code** (research/19 section 9): `SFXGameModeWeaponWheel.Deactivated`
applies the recorded choices only while **Shepard's weapon is away** (`!IsInCombatActionState`):
`ChangeToQuickSlot(GetMemberSwitchWeapon(i))` on each member's `BioEquipment`, then
`SetMemberSwitchWeapon(i, 4)`, then Shepard's own `UpdatePrimaryAndSecondaryWeapons` and
`DrawPrimaryWeapon`. With his weapon **out** it calls `BioPlayerController.ApplyTacticalOrders()`,
whose native resets the record and has each member's AI controller switch (vtable `0xB38`). We do
the same for the chosen member after `SquadOrderEquipWeapon(type, pawn)`, minus the two steps for
Shepard's own weapon (his weapon isn't being changed, and drawing it would undo a holstered gun).
The cheat `BioCheatManager.ChangeWeapon` is the same pair: `SquadOrderEquipWeapon`, then
`ApplyTacticalOrders`.

**[verify] in the headset:** the AI switch with Shepard's weapon out (does it animate, how long);
the instant switch with it away; that the other member's orders aren't disturbed by
`ApplyTacticalOrders`; the names (the class gives Liara, Wrex, Garrus, Tali, Ashley and Kaidan;
**[inference]** from `EBioPartyMemberClassBase`, the log shows the game's own strings); the panel's
place and size; whether the pause option is wanted.

## How ME1 does it

From research/19 section 2 (Ghidra, `SFXGame.pcc` bytecode, the SDK):
- Every squad member has a `BioEquipment` with four quick slots (`m_QuickSlotArray`, pistol,
  shotgun, assault rifle, sniper rifle; empty = none carried) and a current slot. Any class can
  carry any type in ME1; untrained ones are just inaccurate.
- **The weapon wheel** (game mode 3, paused) shows Shepard and both squadmates. Picking a weapon for
  someone calls `BioPlayerController.SquadOrderEquipWeapon(nWeapon, Pawn)`
  (`0xC77FD0`), which only records the choice in that member's squad entry (switch weapon =
  `nWeapon`, delayed switch = 4).
- **Closing the wheel** (`SFXGameModeWeaponWheel.Deactivated`) applies it: for each member whose
  recorded switch isn't 4, `ChangeToQuickSlot` on their equipment (instant: the game is paused),
  reset to 4, Shepard's primary weapons updated, then `ApplyTacticalOrders`. (Corrected when it was
  built, research/19 section 9: that loop runs only with Shepard's weapon away; with it out the
  wheel calls only `ApplyTacticalOrders`, whose native has the AI switch.)
- Holding LB opens the wheel, a tap swaps Shepard's weapon (`TrySwapWeapon`); the wheel is steered
  with the right stick.

So a VR way only has to: know who and which weapon, record it with `SquadOrderEquipWeapon`, and run
the same apply step the wheel's closing runs (or call `ChangeToQuickSlot` on that member and reset
the record, mirroring it). Everything through ProcessEvent, never the SDK's wrappers.

## What VR practice suggests

- **Radial beats linear.** In VR, radial menus were faster than linear ones both head-locked and
  placed in the world, with no difference in errors (Santos et al. 2017); with 3D input devices,
  radial menus were faster, more accurate and preferred (Chertoff et al.; Komerska and Ware 2004
  for haptic pie menus). Placement near the body (hand, arm, waist) was studied by Lediaeva and
  LaViola (2020).
- **Point to choose who.** Arizona Sunshine 2 commands its dog with a context-sensitive button and
  point-and-click orders: send it at the nearest zombie, to a place, to fetch an item (reviews:
  UploadVR, Mixed, Android Central). Pointing is explicit; gaze would fire whenever you look.
- **A stick click opens a menu, the same stick chooses.** Half-Life: Alyx's weapon select
  (research/19 section 1). One tester got accidental turns from it, so the choice should need
  a deliberate flick, not a small push.
- **Voice works with a small grammar, not free speech.** Star Trek: Bridge Crew added voice orders
  to the AI crew (IBM Watson); a hands-on found most orders worked but ship names and some
  phrasings weren't understood (Road to VR).
- **Haptics mark the steps:** a tick per segment, a click on confirm (Meta's haptics design
  system; research/19 section 7).

## Options

### A. Point at a squadmate, a radial at the hand (recommended)

1. **Point** the left controller at Garrus (or anyone in the squad) and **squeeze the left grip**
   (away from the gun's foregrip, where it does nothing else now). The pointed squadmate is the one
   nearest the left aim ray within about 10° (and within 40 m), whatever is in between.
2. A small **radial** appears 25 cm in front of the left hand, facing the head: the squadmate's
   name, and only the weapon types they carry (from their quick slots), the current one marked. A
   thin line from the radial to them says whose it is.
3. **Flick the left stick** toward a weapon (past 0.6, so a resting thumb doesn't choose); each new
   segment ticks in the left controller. Moving the hand toward a segment could also choose
   (marking-menu style), as an option.
4. **Let go of the grip** to apply (a click); let go with the stick centered to cancel.
5. Applying: `SquadOrderEquipWeapon(weapon, pawn)`, then the wheel's apply step for that member.

Details:
- **Time:** ME1's wheel pauses the game. A flick takes about a second, so no pause by default;
  `pauseWhileChoosing = 1` could call the same pause the wheel uses.
- **Input while open:** the host's pad mapping gets a flag (like the holsters'): no left-stick
  movement and no stick clicks while the radial is open, and the left grip isn't LB or the
  support grip for that squeeze.
- **Drawing it:** a third quad layer in Halcyon's renderer next to the HUD panel and the reticle
  (`src/plugin/HalcyonOverlays.cpp`), its texture drawn with the same D3D path; icons from the
  game's own HUD textures if they can be found, else simple shapes and text.
- **Finding the squad:** the player squad's members (`BioPlayerSquad`), each pawn's world position
  mapped into tracking space with the view anchor (`game/ViewAnchor`), the left aim ray from the
  host's controllers.
- **Conflicts:** the test loop's chord is both grips plus a stick *click*; the radial uses a stick
  *tilt* and reserves the clicks while open. A left grip that misses the foregrip while pointing at
  nobody does nothing, as now.

**For:** fast (one squeeze, one flick, one release), no pause needed, uses the hand that's free
while the right holds the gun, and it is where you look anyway in a fight. **Against:** needs a new
overlay quad and pointing at someone in the middle of a fight; two squadmates side by side need the
10° cone and a nearest-first rule.

### B. A wrist panel

Turn the left wrist up: a panel on it shows both squadmates with their four weapon slots (and
later their health, powers and ammo). Pick with the right hand's laser pointer
(`src/plugin/MevrMenuPointer.cpp` already does that for Halcyon's menu), or by touching it.

**For:** shows everything at once, good between fights and for an overview. **Against:** both
hands and a look down; the right hand is holding the gun. A good later addition for status, not the
quick in-fight switch.

### C. Voice

Push to talk (for example the left menu button held), then "Garrus, shotgun" or "Ashley, sniper".
Windows' built-in offline speech recognition with a fixed grammar (squad names × weapon names) can
run inside the ASI, with no network.

**For:** no hands, no aiming, natural for a squad leader. **Against:** a microphone route through
Steam Link or Link, recognition latency (about a second), accents and noise, names the recognizer
gets wrong (Bridge Crew's experience); it needs a confirmation (a voice line or a haptic). An
optional layer on top of A.

### D. Gesture only, through the body slots

Point at a squadmate with the left hand, squeeze, then touch one of *your own* holsters with the
left hand: "give them that type". It reuses a layout you already know and needs no UI. **Against:**
the slots are laid out for the right hand (the right hip, the right shoulder), and confusing your
own draw with a squad order mid-fight is a real risk. Not recommended.

## Recommendation

Build **A** (point, left grip, a radial at the hand, flick the left stick, let go), keep the
game's weapon wheel as the fallback, and add **C** (voice) later as an option. **B** is worth
building as a squad status panel, not as the main way to switch weapons.

Order of work when it's picked up:
1. Read the squad and their quick slots; apply a choice through `SquadOrderEquipWeapon` plus the
   wheel's apply step, triggered by a debug key first **[verify that a henchman's switch outside
   the wheel animates or swaps cleanly]**.
2. The pointing (cone test) and its log.
3. The radial quad and the input flag in the host (needs a host redeploy).
4. Haptics: ticks and the confirm click.

## Sources

- [Santos et al. 2017, A Comparative Study of Menus in Virtual Reality Environments (ISS 2017 poster)](https://iss2017.acm.org/program/posters)
- [Chertoff et al.: Evaluation of Menu Techniques Using a 3D Game Input Device (poster)](https://www.eecs.ucf.edu/~jjl/pubs/chertoff.pdf)
- [Komerska and Ware 2004: a study of haptic linear and pie menus in a 3D fish tank VR environment](https://scholars.unh.edu/ccom/295)
- [Lediaeva and LaViola, Graphics Interface 2020: menus placed in space, on the arm, hand and waist, linear and radial](https://www.eecs.ucf.edu/~jjl/pubs/GI2020-1.pdf)
- Arizona Sunshine 2's dog: [UploadVR review](https://uploadvr.com/arizona-sunshine-2-review), [Mixed review](https://mixed-news.com/en/arizona-sunshine-2-review/), [Android Central review](https://androidcentral.com/gaming/virtual-reality/arizona-sunshine-2-review)
- Star Trek: Bridge Crew voice orders: [Road to VR hands-on](https://www.roadtovr.com/hands-ibm-watson-brings-voice-commands-star-trek-vr/), [UploadVR](https://uploadvr.com/star-trek-bridge-crew-getting-voice-commands-via-ibm-watson/)
- [Meta: haptics design system](https://developers.meta.com/horizon/design/haptics-system/)
- Half-Life: Alyx weapon select: research/19 section 1 (Road to VR, UploadVR)
