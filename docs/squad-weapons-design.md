# Squad weapons in VR (design proposal, not built; 2026-10-22)

Shepard's own weapons now come from body holsters ([research/19](../research/19-body-holsters.md)).
Squadmates' weapons still need the game's weapon wheel (the left trigger, LB). This note proposes a
VR-native way to choose them, and recommends one. Nothing here is built.

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
  reset to 4, Shepard's primary weapons updated, then `ApplyTacticalOrders`.
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
