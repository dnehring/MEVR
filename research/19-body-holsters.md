# 19: Body holsters: VR conventions, and how LE1 switches weapons (2026-10-22)

The user wants Shepard's weapons on the body, so you reach for them instead of opening the
weapon wheel. The proposed layout:
- pistol at the right hip;
- assault rifle over the right shoulder;
- shotgun at the lower front of the body;
- sniper rifle over the left shoulder.

This note covers:
1. how established VR shooters and VR mods do body slots (web sources, plus Overlord's code);
2. how LE1 equips and holsters a weapon natively (Ghidra, the natives dump, `SFXGame.pcc`
   bytecode, the LExSDKv2 headers);
3. the design we build (`core/ik/BodySlots`, `game/Holsters`), and what is still open.

Web sources are listed at the end and in [sources.md](sources.md). Claims marked
**[search summary]** come from a search engine's summary of a page that didn't load directly
(Nexus returns 403, and the Pavlov wiki and Capcom manual failed to load). **[inference]** is our
reasoning, not a source.

## Summary

| Question | Answer |
|----------|--------|
| What are slots anchored to? | Not the raw head yaw: head-locked belts turn away when you glance aside or look down (H3VR, ITR "Head Lock"). Not a free body estimate either: those drift and break muscle memory (Contractors, ITR1). The pattern that works in the open ports is a yaw that **lags the head** (a deadzone, or a slow follow with a maximum lag). It is **frozen while a hand is in a slot**, and it pivots about the neck so looking down doesn't move the slots (GoldenEye VR, Quake VR, Overlord) |
| Heights | They scale with the player: from standing eye height (GoldenEye VR), or with a one-time calibration (VRIK for Skyrim). A fixed eye-to-pouch distance fails tall players (ITR2) |
| Zone sizes | **10-14 cm** for slots you can see (hips, front); **~20 cm** for blind shoulder slots, with **3-5 cm of exit hysteresis**. Overlapping zones are resolved by distance divided by radius |
| Grab input | Squeeze the grip inside the slot: Pavlov, VRIK, Doom 3 BFG VR, GoldenEye VR, Fallout 4 Virtual Holsters. Physical holstering by letting go is the most error-prone part (Boneworks drops guns). Safer variants: Medal of Honor's float-back, HL2VR Unleashed's auto-holster, or the drawn gun going back to its own slot |
| Feedback | A short haptic pulse on entering a slot is standard (Quake VR 0.25 s; Overlord 22 ms on draw and stow). Visual hints are optional and limited to the slot the hand is in |
| Shoulders and handedness | Behind the head at about ear height, bigger zones, no gaze needed. Defined as gun-hand and off-hand sides, so a left-handed mode mirrors |
| The gun already in hand | It goes back to its own slot on a swap (GoldenEye VR, Doom 3 BFG VR, Overlord's exchange). Nobody drops it |
| Seated | No game found has a dedicated seated layout. The tools are a height offset, a stand toggle or sit mode, and a non-physical fallback (a weapon wheel or menu) |
| LE1: equip weapon type X | **`SFXGameModeDefault.SwitchToWeapon(Slot)`**, the same exec the PC number keys use. Called through ProcessEvent on the player controller's default game mode. Quick slot index = weapon type (0 pistol, 1 shotgun, 2 assault rifle, 3 sniper rifle). In combat it starts the animated draw (`BioActiveSelectWeapon`); holstered, it changes the slot and draws |
| LE1: holster | **`SFXGameModeDefault.TryHolster()`**, the holster key. It's a toggle (holstered: draws the primary), so we call it only while `BioPawn.IsInCombatActionState()` is true |
| LE1: which weapons the player has | `BioPawn.m_oBehavior` → `BioPawnBehavior.m_oEquipment` → `BioEquipment.m_QuickSlotArray[4]`: an empty slot means no weapon of that type is equipped. `m_eQuickSlotCurrent` is the current slot |

## 1. How VR shooters do body slots

### Open-source ports with real numbers

**GoldenEye VR** (MrSco/goldeneye-vr; its own code MIT per its README; we read its docs and code
only, nothing copied) has nearly our layout:
- **Layout:** a pistol at each hip (each hip only for its own hand), rifles and SMGs over the
  gun-hand shoulder, heavy weapons (shotgun, sniper, launchers) over the off-hand shoulder,
  throwables on the chest.
- **Anchor:**
  - a torso yaw that chases the head (`VrArmBodyFollow = 0.02` per 60 Hz tick, a time constant
    of about 0.8 s **[inference]**);
  - it lags the head by at most 60° and resets on a jump of more than 45°;
  - it freezes while a hand is in a front slot or the head pitches below −20°. Their first try
    used −35°.
- **Height:** fractions of standing eye height. At 170 cm eye height, measured from the eye held
  upright over a neck pivot:

  | Slot | Below the eye | To the side | Ahead (− = behind) | Radius |
  |------|---------------|-------------|--------------------|--------|
  | Hips | 65 cm | 20 cm | −10 cm | 13 cm |
  | Shoulders | 8 cm | 19 cm | −20 cm (behind the head, ear height) | 20 cm ("reached blind") |
  | Chest | 32.7 cm | 12.9 cm | −0.8 cm | 10 cm |
  | Belt | 33.5 cm | 16.8 cm | +2.2 cm | 10 cm |

- **Grab radii:**
  - a Small/Normal/Large setting scales the radii above by 0.8, 1.0 or 1.25;
  - the hand has left a slot only 5 cm past the radius (was 3 cm);
  - overlapping slots are resolved by the smallest distance/radius ratio.
- **Input:** squeeze in a slot. The slot's weapon goes to the hand and the held weapon goes back
  to its own slot.
- **Feedback:**
  - a light buzz on entry;
  - after a 150 ms dwell, a label and ring, but only for the slot a hand is in (testers disliked
    rings blinking on several slots);
  - a weapon wheel inside a slot opens only if the head faces the slot (40° to open, 55° to
    keep). Shoulders can't be looked at, so reaching is enough there.
- **Wrong grabs:** a throwable needs a 350 ms pause below 0.35 m/s first. The doc lists a known
  bug: reaching for a magazine sometimes drew the holstered gun, partly because the belt and the
  hip used different reference frames.
- **Fit:** a "body slot fit" mode (put the hand where you want the slot and squeeze) and a
  distance readout for placing shoulder slots by reach. Seated reach for the shoulders is listed
  as still to check.

Sources: [docs/body-slots.md](https://github.com/MrSco/goldeneye-vr/blob/main/docs/body-slots.md),
[release notes v0.4.15](https://newreleases.io/project/github/MrSco/goldeneye-vr/release/v0.4.15).

**Quake VR** (vittorioromeo/quakevr; GitHub reports GPL-2.0, so concepts only under our rules):
- **Torso yaw:** from the head, with a lagged head history (0.3 s) and a pull toward the hands
  when both are in front. An 8° deadzone; the torso turns at 12°/s and snaps to the head past 70°
  of neck turn.
- **Holster distances:** 6.5-7.8 Quake units. At Quake's usual ~32 units per meter that's about
  20-25 cm **[inference]**.
- **Crouch:** holsters shift as you crouch.
- **Haptics:** a one-shot 0.25 s buzz on reaching a holster.

Source: `Quake/vr/vr_cvars.inc`, `Quake/vr/vr_body.cpp` in the
[repository](https://github.com/vittorioromeo/quakevr).

**Doom 3 BFG VR, "Fully Possessed"**:
- **Input and feedback:** a controller vibrates in a slot, and the grip grabs, places or swaps.
- **Slots:** the right hip holsters (grabbing an occupied one swaps); behind the weapon-hand
  shoulder is the next weapon, the weapon-hand lower back the previous one; the left shoulder or
  head is the flashlight.
- **Settings:** an Enable/Disable option for holster slots.

Source: [README](https://github.com/CarlKenner/DOOM-3-BFG-VR).

### Overlord (MW2 VR, GPL-3.0, `reference/Overlord`), read closely

`docs/vr-body-equipment.md`, `docs/vr-weapon-carry.md`, and under
`src/client/component/vr/gameplay/`: `weapon_holsters.hpp`, `body_reach_volume.hpp` and
`shoulder_anchors.hpp`.

- **One shared body estimate** for every equipment consumer (chest, waist holsters, back). Each
  consumer must not run its own filter. Head-only, no hands:
  - **neck pivot:** it compensates an eye point 8 cm ahead of a neck pivot;
  - **position:** 7 cm of horizontal head translation tolerance, then a 120 ms follow capped at
    15 cm of separation;
  - **crouch:** followed with an 80 ms time constant;
  - **yaw:** 35° of head yaw allowed against the body, then the excess is followed with a 180 ms
    time constant, at most 180°/s.
  - **Resets:** on recenter, tracking gaps over 150 ms, jumps over 75 cm and view resets.
  - **No hands, on purpose:** aiming, crossed arms and handling equipment give misleading
    directions.
- **Holster layout** (`holster_layout`, meters, configurable as dvars):
  - waist width 0.23, waist down 0.60, waist radius 0.16;
  - back slot 0.22 behind and 0.24 down, radius 0.20.
  - The waist volume is a box swept by a sphere (`body_reach_volume`). Its extra reach goes
    behind, outside and below the anchor only: "it must not turn ordinary forward hand motions
    into inventory actions".
  - Overlapping volumes resolve by distance to the original anchor divided by radius.
- **Carry rules:**
  - hold the side grip to carry;
  - release inside an empty compatible slot to stow, inside an occupied one to exchange;
  - long guns don't fit the waist;
  - tracking loss, menu focus and recentering never count as a release;
  - a rejected release needs a fresh press and release to retry.
- **Feedback:** one light 22 ms pulse on a successful pickup, draw, stow or exchange.
  Rejections are silent.
- **Native switch:** a VR draw uses the game's own immediate weapon switch
  (`switchtoweaponimmediate`), so the game keeps owning selection, ammo and notifications.

What we take, in our own code (no Overlord code copied):
- the shared body frame, pitch-compensated about a neck pivot, with the anchor's height smoothed
  at 80 ms;
- the slot-size scale (16-20 cm), distance/radius tie-breaking and exit hysteresis;
- one light pulse per event;
- the rule that the game's own switch path does the switching.

### Commercial games

- **Half-Life: Alyx:**
  - Valve's "over the shoulder" contextual inventory: ammo comes from over the off-hand shoulder,
    and quick weapon select is on the weapon hand
    ([Road to VR](https://roadtovr.com/half-life-alyx-release-date-no-delay/)).
  - Weapon select is a HUD menu on a button (trackpad on Index, right stick click elsewhere). One
    tester got accidental snap turns from the stick-click binding
    ([UploadVR](https://www.uploadvr.com/half-life-alyx-valve-tested/)).
  - Options: height adjust, crouch toggle, a stand toggle for seated players, a single-controller
    mode ([Road to VR](https://roadtovr.com/half-life-alyx-accessibility-options-subtitles-seated-single-controller/)),
    and a left or right weapon hand ([GameSpot](https://www.gamespot.com/articles/half-life-alyx-accessibility-options-guide/1100-6475045/)).
  - Zone sizes: not found.
- **Boneworks / Bonelab** ([Steam thread](https://steamcommunity.com/app/823500/discussions/0/3963662449068210745/)):
  - **Complaints:** players drop pistols, and sidearm slots feel "inside of my armpits". They grab
    a magazine instead of the pistol, and the shoulder slot is nearly impossible to hit.
    Holstering while crouched is unreliable, and haptics are weak.
  - **Cause, according to players:** the holsters hang off the physics body, so crouching moves
    them.
  - **Requests:** a larger zone for the last-used slot, auto-holster when a gun is dropped near a
    slot, and a dedicated holster gesture.
- **Hot Dogs, Horseshoes & Hand Grenades:**
  - The quickbelt is placed from the headset, so leaning moves it
    ([Steam](https://steamcommunity.com/app/450540/discussions/0/604143843941595363/)).
  - The developer declined an inferred shoulder anchor: guessing the shoulder from head and hand
    data is unreliable in odd poses
    ([Steam](https://steamcommunity.com/app/450540/discussions/0/1484358860943006720/)).
- **Contractors:** inventory follows an estimated body built from the head, hands and walking
  direction. Players say it "wobbles", which breaks muscle memory and causes wrong grabs. They
  ask for head-locked inventory and adjustable mounts
  ([Steam](https://steamcommunity.com/app/963930/discussions/0/5792223132453011488)).
- **Into the Radius:**
  - **ITR1:** body slots rotate with head turns, and players asked for controller-based turning
    ([Steam](https://steamcommunity.com/app/1012790/discussions/0/3823032780423878516/)).
  - **ITR2 modes:** Head Lock, Hybrid and Fixed. Players asked for a rig lock while looking down
    and more head turn before the body follows
    ([Steam](https://steamcommunity.com/app/2307350/discussions/0/4848778277271288558/)).
  - **ITR2 height:** the player-height setting moves the body but keeps a fixed eye-to-pouch
    distance, so for tall players the holster ends up in the armpit
    ([Steam](https://steamcommunity.com/app/2307350/discussions/0/4848777893535603042/)).
- **Pavlov:** the hand goes to a circle on the chest and the grip holsters; lefties get a mirrored
  layout **[search summary]**. It uses the runtime's height, and players fake a lower one as a
  workaround ([Steam](https://steamcommunity.com/app/555160/discussions/0/154644787622699656)).
- **Medal of Honor: Above and Beyond:**
  - **Layout:** a weapon over each shoulder, the pistol at the waist, grenades and items on the
    chest.
  - **Release:** letting go makes the weapon float back to its slot, so it is never dropped
    ([review](https://sometimesiplaygames.com/medal-of-honor-vr-above-and-beyond-is-good/)).
  - Reviewers report grabbing the wrong item **[search summary]**.
- **The Walking Dead: Saints & Sinners:**
  - **Layout:** thigh holsters for one-handers, a long gun over the right shoulder, the backpack
    over the left shoulder
    ([Game Chronicles](https://gamechronicles.com/the-walking-dead-saints-sinners-review-oculus-quest/),
    [Game Developer](https://gamedeveloper.com/design/intuitive-inventory-systems-in-vr-games)).
  - **Seated:** the Game Developer article calls seated slot positions an open problem.
- **Resident Evil 4 VR:**
  - **Layout:** the handgun at the right hip, long guns over the right shoulder, the knife on the
    chest.
  - **Fallback:** a "Quick Select" wheel that pauses time.
  - **Seated:** supported. All of this is **[search summary]**.
- **Blade & Sorcery:** two shoulder holsters behind the body and two easier hip holsters
  ([Steam](https://steamcommunity.com/app/629730/discussions/0/3886101131592520502/)).
- **Arizona Sunshine 2:** magazines come from the belt, and the shotgun is slung on the back
  ([IMFDB](https://www.imfdb.org/wiki/Arizona_Sunshine_2)). Anchoring: not found.
- **Not found** (no usable sources): Onward, Zero Caliber and Hellsplit Arena specifics, and how
  Bonelab, Pavlov and Arizona Sunshine anchor their slots.

### Mods

- **VRIK (Skyrim VR):**
  - **Holsters:** 14, from calves to forearms.
  - **Stashing:** hover until a haptic pulse, then squeeze and release; later, carry the weapon
    in with the grip held and release at the holster. Hold the grip on a holster for 2 s to move
    it.
  - **Settings:** per-holster hand rules, and a calibration spell for height and body size
    ([SynergyVR](https://synergyvr.org/mgo/mod-highlights/vrik/)). Hover spheres are off by
    default, with haptic strength and a holster lock **[search summary]**.
- **Virtual Holsters (Fallout 4 VR):** seven zones, a configuration mode to move and resize them,
  and haptics near a zone. The ini has `DisplaySpheres` and `EnableHaptics` **[search summary]**.
- **Half-Life 2 VR:** the base mod has a radial menu for weapons
  ([Road to VR](https://roadtovr.com/?p=171232)). The "Unleashed" fork auto-holsters the last
  weapon when the grip is released and brings it back on the next press; this can be turned off
  ([Mixed](https://mixed-news.com/en/half-life-2-vr-mod-unleashed/)).

### Design writing

- **Body size:** a belt that fit the developer failed for larger waists, since the controller
  would have to pass through the body. The author proposes calibrating the waist once
  ([Madeline Miller](https://madelinemiller.dev/blog/the-issue-of-body-size-in-vr/)).

### Conventions we follow

1. **Anchor:**
   - a body yaw that lags the head, frozen while the hand is in a slot;
   - the anchor is the eye held upright over the neck pivot, so looking down moves nothing;
   - the height is smoothed so crouching is followed but jitter isn't.
2. **Size:**
   - visible slots 13-14 cm; blind shoulder slots 20 cm;
   - 5 cm exit hysteresis;
   - nearest by distance/radius.
3. **Input:**
   - only a **new** squeeze inside a zone acts; a grip already held while the hand moves in never
     grabs (Overlord's support-grip rule, which we already use);
   - one squeeze, one action.
4. **The current gun** goes back to its own slot on a swap. In LE1 that's what the game does
   anyway.
5. **Holstering is conservative:** squeeze and release at the gun's own slot. The hand leaving the
   zone or the trigger being pulled cancels it.
6. **Feedback:** a light pulse on entry and a firmer one on draw or holster. No visuals for now.
7. **The wheel stays as a fallback** (the setting `weaponWheel`) until the slots are tried.

## 2. How LE1 equips and holsters a weapon

Found with GhidraMCP on the LE1 project, the natives dump (`natives-LE1-60AEEFA2.csv`), the
bytecode of `SFXGame.pcc` (decompressed read-only with the game's Oodle DLL, as in
[18](18-camera-and-hands.md) "Method") and the LExSDKv2 headers. RVAs for 2.0.0.48602. The names
are in `tools/ghidra/symbols-le1.csv`.

### The data

| Object | Field | Offset | Meaning |
|--------|-------|--------|---------|
| `BioPawn` | `m_oBehavior` (`BioPawnBehavior`) | `+0x7D4` | |
| `BioPawnBehavior` | `m_oEquipment` (`BioEquipment`) | `+0x424` | |
| `BioEquipment` | `m_QuickSlotArray[4]` (`BioWeapon*`) | `+0x94` | One weapon per quick slot; null = none equipped |
| `BioEquipment` | `m_eQuickSlotCurrent` (byte) | `+0xB8` | The current slot |
| `BioPlayerController` | `GameModeManager` (`SFXGameModeManager`) | `+0xA70` | Its `GameModes` array (`+0x70`) holds the `SFXGameModeDefault` |

The slot index is the weapon type:
- `BioBaseSquad.ESwitchToWeapon`: 0 pistol, 1 shotgun, 2 assault rifle, 3 sniper rifle, 4 none;
- `EBioItemWeaponRangedType` uses the same numbers;
- the wheel passes the `ESwitchToWeapon` value straight to `ChangeToQuickSlot`.

`game/Holsters` still checks this once per weapon set. It asks each quick slot's weapon for
`GetRangedWeaponType` and logs the map.

### The callers (from `SFXGame.pcc` bytecode)

Script calls to `BioEquipment.ChangeToQuickSlot` (a virtual call by name) come from:
- `SFXGameModeDefault.SwitchToWeapon`, `NextWeapon`, `PrevWeapon` (player input);
- `SFXGameModeWeaponWheel.Deactivated` (the wheel closing);
- `SFXPawn_Player.DrawPrimaryWeapon`;
- the save loaders and cheats.

`BioPlayerController.SquadOrderEquipWeapon` (final native) is called from
`BioSFHandler_HUD.HandleEvent` (the wheel's UI) and `BioCheatManager.ChangeWeapon`.

**The weapon wheel** (pad LB; our left trigger by default):
- **While open:** `BioSFHandler_HUD.HandleEvent` → `SquadOrderEquipWeapon(nWeapon, Pawn)`. Its
  body (`0xC77FD0`) finds the pawn's squad member index and stores the choice in that member's
  squad entry:
  - switch weapon = `nWeapon` (`0x9E0220`, entry byte `+0x71`);
  - delayed switch = 4 (`0x9E0240`, byte `+0x70`);
  - unless a pawn virtual (slot `0x978`, role unknown) says no.
- **On close:** `SFXGameModeWeaponWheel.Deactivated` goes through the squad. For each member whose
  `GetMemberSwitchWeapon` isn't 4:
  - `ChangeToQuickSlot(it)` on that member's `BioEquipment`;
  - resets it to 4;
  - for the player: `UpdatePrimaryAndSecondaryWeapons`, then `DrawPrimaryWeapon` (when not in
    combat);
  - then `ApplyTacticalOrders`.
- So the wheel swaps instantly (the game is paused while it's open), for Shepard and the squad.

**The exec Shepard's own input uses: `SFXGameModeDefault.SwitchToWeapon(byte Slot)`.** From its
bytecode (`Outer` is the player controller):

```
if (!PlayerInput.IsCombatEnabled()) return;
MyBP = BioPawn(Outer.Pawn); if (MyBP == None) return;
if (!MyBP.IsInCombatActionState()) {            // holstered (explore)
    MyBP.m_oBehavior.m_oEquipment.ChangeToQuickSlot(Slot);
    MyBP.UpdatePrimaryAndSecondaryWeapons();
    MyBP.m_oBehavior.SetActionState(1);          // BIO_ACTION_STATE_COMBAT: draw
} else if (MyBP.m_oBehavior.IsAbleToInstigate() && !MyBP.IsSwitchingWeapons()
           && Slot != m_oEquipment.m_eQuickSlotCurrent) {
    ... ResetReticule on the active super mode ...
    Active = BioActiveSelectWeapon(MyBP.m_oBehavior.AcquireActive(class'BioActiveSelectWeapon'));
    Active.m_eWeaponToSwitchTo = Slot; Active.m_oEquipment = ...; Active.m_oPawnBehavior = ...;
    MyBP.m_oBehavior.StartActive(Active);        // the animated holster-and-draw
}
```

(A reconstruction from the references in the bytecode: the order and the constants are certain,
the exact branch structure is our reading.)

**Holster: `SFXGameModeDefault.TryHolster()`** (the holster key):
- **holstered:** `MyBP.DrawPrimaryWeapon()`. It's a **toggle**;
- **drawn, and not switching or in an animated transition:** it cancels a modal power, and if the
  pawn isn't firing, sets the action state to 0 (`BIO_ACTION_STATE_EXPLORE`): holstered.

### What we call

All of these go through ProcessEvent directly, never the SDK wrappers:
- **Equip type X:** `SFXGameModeDefault.SwitchToWeapon(X)` on the `SFXGameModeDefault` in the
  player controller's `GameModeManager.GameModes`. The parameters are one byte. It is what the
  game does for its own keys: the animated draw in combat, a change and draw when holstered, and
  nothing when combat is disabled (hubs) or while already switching.
- **Holster:** `SFXGameModeDefault.TryHolster()`, only while `BioPawn.IsInCombatActionState()`
  (native final, through ProcessEvent) is true, because it toggles.
- **Available types:** `m_QuickSlotArray[X] != null`.
- **Current type:** `m_eQuickSlotCurrent`.
- **Drawn:** `IsInCombatActionState()`.
- **Mid-switch:** `IsSwitchingWeapons()` (native final).

Not used:
- `ChangeToQuickSlot` directly: no animation and no draw;
- `SquadOrderEquipWeapon`: it only records a wheel choice, which the wheel applies when it closes.

**Class restrictions:** in ME1 every class can carry every weapon type (untrained ones are just
inaccurate), so the question is which quick slots hold a weapon. A slot without one gets a
"nothing there" pulse and log line, and no call.

**[verify] in game:**
- that `SwitchToWeapon` from our tick draws the weapon like the number keys;
- that the hand-held weapon (`game/weapon`) follows within the animation's time;
- that `TryHolster` puts it away;
- the slot-to-type map logged at startup.

## 3. The design (built 2026-10-22)

### Body frame

All in Halcyon's LOCAL tracking space (meters; +X right, +Y up, −Z forward).

- **Yaw:**
  - arm IK's torso yaw (`game/ArmIk`: the head yaw past a 34° deadzone, eased back, pulled toward
    the hands when both are in front) while arm IK drives the torso;
  - otherwise our own `ik::BodyYaw` on the head yaw (same deadzone);
  - **held** while the right hand is in a slot or a grab or holster is in progress, so reaching
    or looking at the hip doesn't move the slot.
- **Anchor:** the eye as it would be with the head upright. The head turns about a neck pivot
  8 cm behind and 8 cm below the eyes, and the anchor puts the eye back above that pivot at the
  head's yaw (`NeutralEye`). Its height follows with an 80 ms time constant (crouching moves the
  slots; jitter doesn't). Horizontally it follows the anchor directly.
- **Scale:** offsets are given for a 1.65 m standing eye height and scaled by
  `eyeHeightM / 1.65`. Radii aren't scaled.

  **Why a setting:** Halcyon's space is LOCAL, whose origin is the head at recenter, so the
  floor, and with it the player's height, isn't known. A host-side `STAGE` or `LOCAL_FLOOR`
  space could measure it later (proposal).

### Default slots

Offsets are right, up, forward from the anchor, in meters at a 1.65 m eye height.

| Slot (ini key) | Weapon | Right | Up | Forward | Radius | From |
|----------------|--------|-------|----|---------|--------|------|
| `hipRight` | pistol | 0.20 | −0.62 | −0.05 | 0.14 | GoldenEye VR hips (20 / −65 / −10), Overlord waist (0.23, −0.60, r 0.16) |
| `shoulderRight` | assault rifle | 0.18 | −0.08 | −0.20 | 0.20 | GoldenEye VR shoulders (19 / −8 / −20, r 20) |
| `belly` | shotgun | 0.00 | −0.50 | 0.12 | 0.13 | The user's "lower midsection", front; 10-13 cm like visible front slots |
| `shoulderLeft` | sniper rifle | −0.18 | −0.08 | −0.20 | 0.20 | Mirror of the right shoulder |

Checks against poses that must not grab **[inference, to tune in the headset]**:
- **The shouldered aim zone** (the support grip's "at the shoulder": the right grip 5-35 cm under
  the eyes, ±30 cm to the side, −15 to +40 cm ahead). Its usual point (0.15, −0.15, +0.10) is
  0.31 m from the right-shoulder slot and 0.38 m from the belly slot: outside both zones and
  outside the 5 cm "near" margin.
- **A low hip-fire hand** (0.15, −0.45, +0.30) is 0.24 m from the belly slot and 0.39 m from the
  hip: outside both zones and their near margins (the near margin only keeps a new squeeze from
  being RB).

### Grab

- The right hand (the grip pose's position) enters a slot. A **new** right-grip squeeze there:
  - **another weapon type is held, and the player has this one:** `SwitchToWeapon` draws it. The
    gun appears in the hand through arm IK and `game/weapon` as usual;
  - **the player has no weapon of that type:** a weaker double-length pulse, logged, no call;
  - **it's the gun already in hand:** this arms a holster (below), or does nothing with
    `holster = 0`.
- **The right grip and RB:**
  - Today the right grip reaches the game as RB, which opens the power wheel.
  - Within a slot's radius plus 5 cm, the mod sets `kFlagRightGripFree`. The host's pad mapping
    then keeps a **new** squeeze off RB until the grip opens.
  - A squeeze begun elsewhere stays RB. A grip already held while moving in neither grabs nor
    loses RB.
  - The host counts the squeezes it kept from the game (`HostServices::rightGripFree`, v9). The
    mod grabs only on such a squeeze, so the game never sees RB for a grab.
  - With Halcyon's menu open, no squeeze counts.
  - An older host (v8): the mod falls back to its own squeeze edge, and RB still reaches the game
    (logged once: redeploy the host).
- **Only on foot:** game mode 0, a player BioPawn, Halcyon's world view valid, the right
  controller tracked.

### Holster (`holster` setting)

- `1` (default): squeeze **and release** at the held gun's own slot. The hand leaving that slot
  (past the 5 cm hysteresis) or the trigger being pulled while squeezed cancels it.
- `2`: also at a slot whose weapon you don't carry (an empty holster).
- `0`: never; holster with the game's own key.

Since the grip isn't "hold the gun" here (below), "release the grip at the slot" becomes squeeze
and release. Pulling the trigger while holstered makes ME1 draw again, so an accidental holster
is cheap to undo **[verify]**.

### Feedback

Through `HostServices::hapticPulse` (v9), with an OpenXR vibration action on the host's action
set (`/output/haptic` on Touch, Index and the simple controller). Applied on Halcyon's frame
thread after the action sync.

| Event | Duration | Amplitude |
|-------|----------|-----------|
| Entering a slot | 25 ms | `hapticAmplitude` (0.5) |
| Draw or holster | 60 ms | 0.8 |
| Nothing there | 120 ms | 0.3 |

`hapticAmplitude = 0` turns them off.

### The weapon wheel

`[Holsters] weaponWheel = 1` (default) keeps it: the left trigger stays LB if `leftTrigger = lb`.
`0` turns LB off while holsters are on: no left-trigger LB, no left-grip LB. The wheel is also
how squadmates' weapons are picked, so turning it off loses that until the squad has another way.

### Diagnostics

Everything is logged once per change, with `holsters:` lines:
- entering and leaving a slot;
- the grab and what was called;
- the type equipped and how long it took;
- holstering;
- refusals: no such weapon, combat disabled, already switching.

There is also a heartbeat line, and a `holsters` object in the test loop's mod status (the slots'
centers in tracking space, the hand's distance to each, the phase, the frame's yaw source).

## 4. The right grip as "hold the gun" (proposal, not built)

Most VR shooters use grip to hold and release to drop or holster: Overlord, Boneworks, H3VR,
Pavlov. In ME1 that would mean:
- **squeeze at a slot:** draw;
- **keep the grip held:** carry and fire;
- **release:** holster at a slot, or keep the gun (ME1 can't drop weapons) or holster it
  anywhere else.

**For:**
- It is the convention, and holstering becomes "let go at the slot".
- It frees RB's power wheel from the right grip entirely. RB could move to a button.

**Against:**
- The game has no "dropped" state to map a release to.
- Holding the grip through a whole fight is tiring, and ME1 fights are long **[inference]**.
- It changes a mapping the user has been testing.
- The two-handed grip already uses the left grip.

**Proposal:** keep the grip as a squeeze to draw (built). Ask the user whether to try "hold to
carry" as an option (`[Holsters] gripHolds = 1`): release away from a slot keeps the gun, release
at a slot holsters it.

## 5. Open

- **[verify]** All of section 2's calls in game, and the slot-to-type map.
- **[verify]** The default positions and radii in the headset: the right shoulder reached
  without hitting the head, the belly slot not triggered by low hip fire.
- The player's height: `eyeHeightM` by hand until a floor-relative space exists (host proposal).
  The arm-length calibration could also give a height estimate.
- Seated play: no preset yet. Move the low slots with their ini lines; a `seated` preset is a
  follow-up.
- Left-handed play: not supported anywhere in the mod yet. When it is, the slots mirror
  (gun-hand shoulder, off-hand shoulder).
- Visual hints (a ring or label for the slot the hand is in) need an overlay in Halcyon's
  renderer: a later step, if the pulses aren't enough.
