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
3. the design we build (`core/ik/BodySlots`, `game/Holsters`), and what is still open;
4. the right grip as "hold the gun" (user decision 2026-10-22, built);
5. what is still open;
6. where RB went: what ME1 binds to it and to every other pad control, and why RB moved to the
   right stick click;
7. contact haptics (Blade & Sorcery style) and how they map to OpenXR;
8. the eye height from the floor;
9. squad weapons: how the weapon wheel applies them, and the VR menu built from [docs/squad-weapons-design.md](../docs/squad-weapons-design.md);
10. the game's own weapon wheel opened from the VR gesture and shown at the hand: how LB opens and
    closes it, its one ring in the HUD movie, the stick angle, and what we drive.

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
| LE1: what RB does on foot | `Console_PowerWheel`: **hold 0.3 s = power wheel** (`EnterPowerWheel`, game mode 2, paused, steered with the right stick, closed by releasing RB), **tap = the mapped power** (`TryUseMappedPower`). In the Mako RB fires the cannon (section 6) |
| Where RB went (gripHolds = 1) | **The right stick click**, whose own command (`ChangeMagnification`, a scoped weapon's zoom step) does nothing in Halcyon's view; hold it and tilt the same stick to choose, like Half-Life: Alyx's weapon menu. Guarded against Halcyon's double-click recenter, its both-sticks menu chord and clicks while turning (section 6). `[Holsters] powerWheel = y / x / b` are the alternatives |
| Contact haptics | A continuous buzz while the hand overlaps a slot, stronger and higher-pitched toward its middle, a firmer edge crossing in, a crisp click on a draw and a softer one on a holster. Played as short refreshed OpenXR vibrations that stop by themselves if the mod stops asking (section 7) |
| Eye height | Measured by the host in an OpenXR `STAGE` space (`LOCAL_FLOOR` if listed; Halcyon's 1.0 instance doesn't enable it), taken at each recenter, then a slow-rising maximum; `eyeHeightM` is the fallback (section 8) |
| The game's weapon wheel from VR | `SFXGameModeDefault.EnterWeaponWheel()` opens it (LB held), `SFXGameModeWeaponWheel.ExitWeaponWheel()` closes and applies it (LB let go). One ring in the console HUD movie: the right stick's angle (`BioSFHandler_HUD.nAngle`, kept while centred) picks a member and a weapon together; A queues the highlighted entry (section 10) |

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
5. **Holstering:** first built conservative (squeeze and release at the gun's own slot); since the
   user's decision of 2026-10-22 the grip holds the gun and letting go at its slot holsters it,
   letting go elsewhere returns it after a grace (section 4).
6. **Feedback:** first a light pulse on entry and a firmer one on draw or holster; now contact
   haptics (section 7). No visuals (user decision).
7. **The wheel stays as a fallback** (the setting `weaponWheel`), and for squad weapons (section 9).

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
- **Corrected 2026-10-22 (section 9):** that loop runs only while Shepard's weapon is away; with
  it out, the close calls only `ApplyTacticalOrders`, and the members' AI switches.

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
    or looking at the hip doesn't move the slot. (With the grip holding the gun, carrying it
    doesn't hold the yaw: only being in a slot does.)
- **Anchor:** the eye as it would be with the head upright. The head turns about a neck pivot
  8 cm behind and 8 cm below the eyes, and the anchor puts the eye back above that pivot at the
  head's yaw (`NeutralEye`). Its height follows with an 80 ms time constant (crouching moves the
  slots; jitter doesn't). Horizontally it follows the anchor directly.
- **Scale:** offsets are given for a 1.65 m standing eye height and scaled by
  `eyeHeight / 1.65`. Radii aren't scaled. The eye height is measured from the floor since the
  host's v10 (section 8); `eyeHeightM` is the fallback.

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
- **The right grip and RB with `gripHolds = 0`** (the first design; with `gripHolds = 1` the grip
  is never RB, section 4):
  - The right grip reaches the game as RB, which opens the power wheel.
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

- `1` (default): at the held gun's own slot. With `gripHolds = 1`: let go there (section 4). With
  `gripHolds = 0`: squeeze **and release** there; the hand leaving that slot (past the 5 cm
  hysteresis) or the trigger being pulled while squeezed cancels it.
- `2`: also at a slot whose weapon you don't carry (an empty holster).
- `0`: never by hand; holster with the game's own key.

Pulling the trigger while holstered makes ME1 draw again, so an accidental holster is cheap to
undo **[verify]**.

### Feedback

Contact haptics since 2026-10-22 (section 7). The first version, still what a v9 host gets: one
pulse per event through `HostServices::hapticPulse`, with an OpenXR vibration action on the
host's action set (`/output/haptic` on Touch, Index and the simple controller), applied on
Halcyon's frame thread after the action sync. `hapticAmplitude = 0` turns all haptics off.

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

## 4. The right grip holds the gun (user decision 2026-10-22; built, `[Holsters] gripHolds = 1`)

Most VR shooters use the grip to hold and letting go to drop or holster (Overlord, Boneworks,
H3VR, Pavlov). The user chose that convention for ME1. ME1 has no "dropped" state, so a let-go gun
goes back to its own holster: what GoldenEye VR does on a swap, Medal of Honor's float-back and
Half-Life 2 VR Unleashed's auto-holster do on release (section 1). `gripHolds = 0` keeps the first
design (squeeze to draw, the grip is RB elsewhere).

| You | What happens |
|-----|--------------|
| squeeze at a slot (nothing out, or another gun) | that weapon is drawn (`SwitchToWeapon`); keep squeezing to carry it |
| squeeze at the slot of the gun that's out | you take it (no call) |
| squeeze anywhere else | you take hold of whatever is out; with nothing out, nothing happens (and no RB) |
| let go at the gun's own slot | holstered at once (`TryHolster`); `holster = 2`: also at an empty slot |
| let go anywhere else | it goes back after `releaseGrace` (0.3 s) unless you squeeze again; `releaseGrace = -1`: it stays out |
| let go while firing | the grace's clock stops while the trigger is held: never in the middle of a burst |
| let go at its slot while firing, or while the draw is still running | it goes back once the trigger is let go or the gun is out (waiting at most 2 s for a draw), with no grace |
| the game swaps or puts the gun away during the grace | the return is off |
| a squeeze that began in a menu, a wheel or a conversation, with a gun out | taken as holding it (the host's report says the squeeze is kept from the game), so letting go still puts it away |
| the game draws a gun by itself, grip open | left alone: only a let-go we saw acts. (`TryUseMappedPower`, `UseFirstAid`, `UseGrenade`, `TryMelee` and `EnterPowerWheel` all call `DrawPrimaryWeapon` when holstered, by their bytecode) |

**Why a grace, and why never mid-burst [inference]:** a hand relaxes for an instant while
aiming, and holstering on every slip would be the error-prone release research/19 section 1
warns about (Boneworks drops guns). A player firing one-handed with a loose grip expects the gun to
keep firing.

**Where the grip holds:** on foot (game mode 0) and in the two wheels opened from there (2 the
power wheel, 3 the weapon wheel: the game is paused in them, and the hand still holds the gun). In
the Mako (1) the right grip is RB as always (the cannon, section 6), and in menus and
conversations too. A squeeze keeps the meaning it began with, whatever changes while it's held, so
a gun held into a menu never becomes RB when it closes.

**Pieces:**
- The pad mapping (`game/GamepadMapping`, host): `kFlagRightGripHolds` (32) and the RB source in
  flag bits 8-11 (`kRbSourceMask`), from the mod each tick. A new right squeeze with the flag is
  never RB (`Latches::rightHold`) and is counted like a holster squeeze, so the mod sees every one
  (`HostServices::rightGripFree`). With Halcyon's menu open it isn't counted (`Options::countKept`).
- The machine (`ik::body_slots::Machine`, `Input::gripHolds`, unit-tested in
  `TestBodySlotHolding`): the actions `ReturnArmed`, `ReturnCancelled`, `Return`, the phase
  `Returning`. Carrying a gun doesn't hold the body yaw (only being in a slot does).
- A host older than v10 ignores the new flag, so the mod keeps the old mapping and logs it once.

**The two-handed grip and aimed fire** are unchanged: the left grip takes the gun's foregrip on a
new squeeze there, aimed fire needs it plus the gun at the shoulder. The held right grip isn't RB,
so the game sees nothing new.

**The test loop's chords (both grips + a stick click)** were safe because both grips were rarely
held in play (the right grip was RB). Now both grips are the ordinary two-handed aim, and the
stick clicks then are crouch (L3) and RB (R3). So with `gripHolds` the chord only counted while the
left grip was **not** on the foregrip. **Superseded 2026-10-22 (section 9):** the left grip away
from the gun opens the squad menu now, so the grip chords are gone. A snapshot is a quick tap of
both stick clicks (`gamepad::UpdateStickChord`); recording is Home or the menu's button.

## 5. Open

- **[verify]** All of section 2's calls in game, and the slot-to-type map.
- **[verify]** The default positions and radii in the headset: the right shoulder reached
  without hitting the head, the belly slot not triggered by low hip fire.
- **[verify]** `gripHolds`: holding the grip through a long fight (fatigue, research/19's old
  objection), slips that start the grace, the 0.3 s grace itself.
- **[verify]** R3 as RB: the power wheel opened and steered with the right stick while R3 is held
  (section 6), the mapped power about 0.35 s after a click, no casts on a double-click recenter.
- **[verify]** Whether the runtime honors the vibration frequency (section 7).
- **[verify]** The standing eye height from the floor on SteamVR (Steam Link) and the Oculus
  runtime, and a waist bend's effect on the low slots (section 8).
- Seated play: no preset (the user doesn't want one). The floor-based scale uses the standing
  height, so a player who recenters seated gets a smaller layout until they stand once.
- Left-handed play: not supported anywhere in the mod yet. When it is, the slots mirror
  (gun-hand shoulder, off-hand shoulder).
- No visual slot marker (user decision): the contact haptics are the feedback.
- Squad weapons: the VR menu is built (section 9), and since the evening of 2026-10-22 the gesture
  opens the game's own wheel at the hand by default (section 10) **[verify]** in the headset.

## 6. Where RB went

### What ME1 binds to the pad

The gamepad bindings are config: `BIOInput.ini` inside `CookedPCConsole\Coalesced_INT.bin`
(parsed read-only: a count of files, then per file its name, sections and key/value pairs as length-
prefixed UTF-16 strings), and the same values as the defaults of `SFXGameModeBase.Bindings` /
`SFXGameModeDefault.Bindings` in `SFXGame.pcc` (decompressed read-only with the game's Oodle DLL,
as in [18](18-camera-and-hands.md)). Each game mode has its own `Bindings`
(`SFXGameModeBase.Bindings`, `CPF_Config`, SDK). On foot (`SFXGameModeDefault`):

| Pad | Command | What it does | Touch, our mapping |
|-----|---------|--------------|--------------------|
| left stick | `Console_Strafe` / `Console_Movement` | move | left stick |
| right stick | `Console_LookX` / `Console_LookY` | turn and look | right stick (Halcyon: turning) |
| LT | `Console_Aim`: `TightAim` / `StopTightAim` | aimed fire (`bIsZoomed`) | the support grip at the shoulder; the left trigger is LB by default |
| RT | `Console_Shoot` | fire | right trigger |
| LB | `Console_WeaponWheel`: `OnHold 0.3 EnterWeaponWheel \| OnTap 0.3 TrySwapWeapon` | weapon wheel; tap = the other weapon | left trigger (`leftTrigger = lb`) |
| **RB** | **`Console_PowerWheel`: `OnHold 0.3 EnterPowerWheel \| OnTap 0.3 TryUseMappedPower`** | **power wheel; tap = the mapped power** | right grip (`gripHolds = 0`); **right stick click** (`gripHolds = 1`) |
| A | `Console_Action`: `OnRelease StormOff \| OnHold 0.3 StormOn \| OnTap 0.3 TapAction` | use, take cover; hold = sprint | A |
| B | `Console_Melee`: `TryMelee` | a melee power: `Pistol_Whip` with the pistol, else `Rifle_Butt` (bytecode), with a cooldown | B |
| X | `Console_Grenade`: `UseGrenade` | throw a grenade | X |
| Y | `Console_FirstAid`: `UseFirstAid` | medi-gel (`INTERNAL_POWER_ID_HEAL`) | Y |
| d-pad | `Console_Squad{Attack,Follow,Move1,Move2}` | squad orders | not mapped from Touch |
| L3 | `Console_Crouch`: `ToggleCrouch` | crouch | left stick click |
| R3 | `Console_ChangeMagnification` | only while zoomed: the weapon's `CycleMagnification` (a scope's zoom step), and a GUI sound | right stick click (Halcyon: double-click = recenter) |
| Start | `Console_Menu`: `ShowMenuGui` | pause menu | left menu button |
| Back | `Console_Holster`: `TryHolster` | holster | left grip + menu |

Other modes:
- **Power wheel** (`SFXGameModePowerWheel`, mode 2): the right stick (`Console_LookX/Y`: it steers
  the selection), **RB = `OnRelease ExitPowerWheel`**, Start, the d-pad's squad orders. Its
  `Activated` pauses the game and swaps the sticks only when the profile's stick option (26) is 1
  (bytecode), so by default the right stick steers.
- **Weapon wheel** (mode 3): the same with LB's release.
- **Mako** (`SFXGameModeVehicle`, mode 1): **RB = `FireCannon`**, LB boost, LT aim, RT the gun,
  A jump jets, B exit, Y repair, X leave the world, R3 magnification.
- Cinematics, movies, minigames, the GUI and photo mode bind nothing to RB.
- At runtime `SFXGameModeManager.UpdateControllerTriggerBindings` swaps the bumpers with the
  triggers, `UpdateControllerStickBindings` the sticks (southpaw) and `UpdateControllerButtonBindings`
  A and B in menus, each from a profile option (bytecode: their only key names are those pairs).
  The defaults (`m_nDefaultTriggerConfiguration = 0`, `m_nDefaultSouthpawFlag = 0` in
  `BIOGame.ini`) leave the table as is.
- The mapped power: `TryUseMappedPower` uses `BioPlayerInput.MappedPower` (`SetCurrentPower`,
  then `AcquireActive`); it returns at once when combat is disabled or nothing is mapped
  (bytecode). The natives dump also has `BioPlayerController.QuickCommandUseMappedAction`
  (`0xBACCD0` → `0xC76AD0`, GhidraMCP): a mapped action per slot, 4 and 5 being the two squad
  members.

### Choosing RB's new control

The power wheel is **hold RB, steer with the right stick, let go to apply**; a tap casts the
mapped power. So RB's new control must be held while the right stick moves, and the right hand
now holds the gun (grip) and fires (trigger).

| Control | Its job now | As RB | Verdict |
|---------|-------------|-------|---------|
| **R3** | `ChangeMagnification`: a zoom step while zoomed, which Halcyon's view doesn't show (it replaces the game's projection each frame, research/14) **[verify]**; Halcyon's double-click recenter and both-sticks menu read it | hold it and tilt the same stick to choose, as Half-Life: Alyx's weapon menu does on a stick click (section 1); the thumb is already there | **chosen**: nothing important is lost |
| Y | first aid | the left thumb holds, the right steers: comfortable | the alternative (`powerWheel = y`); first aid then only through the power wheel (its HUD lists the heal and grenade powers: `PopulateHealPower`, `PopulateGrenadePower`) |
| X | grenade | as Y | `powerWheel = x`; grenades through the wheel |
| B | melee | the right thumb can't hold B and steer the right stick | `powerWheel = b`: taps only, really |
| L3 | crouch | a held left click while the right thumb steers works, but clicks while walking would cast | no |
| left grip away from the gun | nothing (`leftGripAway = none`) | a grab that misses the foregrip would open the wheel or cast: the complaint that made the left grip "only take the gun" (7b80f5d) | no |
| left trigger | the weapon wheel (LB), kept for squad weapons | | no |
| A, menu | action/sprint/cover, pause | | no |

**R3's guards** (`gamepad::RbFromStickClick`, pure, unit-tested in `TestRbFromStickClick`):
- **Turning:** a click with the stick pushed past half (snap or smooth turning) isn't RB.
- **Halcyon's both-sticks menu chord:** RB starts only once R3 has been held 100 ms alone; the left
  click joining within that cancels it.
- **Halcyon's double-click recenter** (press to press within 400 ms, `vr_menu.cpp`): a quick click
  (released within the 100 ms) becomes an 80 ms RB tap only after that window has passed with no
  second press; a second press in the window is never RB. The pair is used up as Halcyon does.
- **The test loop's chord** reserved the clicks (until 2026-10-22; now the both-sticks tap, section
  9, uses the same 100 ms join window as RB's commit, so a chord is never RB).
- **The squad menu** (section 9) keeps both clicks from the game while it's open, and a click held
  then until it's released.
- R3 itself still reaches the game, since Halcyon reads it there.
- **L3** (crouch) is held back up to 100 ms too since 2026-10-22 (`gamepad::GateClick`): held that
  long alone it reaches the game; a quicker click reaches it as an 80 ms press when let go; one the
  right click joins never does. So a both-sticks chord pressed left first is never a crouch.

**Cost:** the mapped power comes about 0.35 s after a quick click, and the wheel opens about 0.4 s
after the press (0.1 s commit plus the game's 0.3 s). If that's too slow in play,
`[Holsters] powerWheel = y` moves RB to Y with no delays (and first aid to the wheel).

## 7. Haptics as contact

The user asked for feedback like Blade & Sorcery's collision haptics: the hand *feels* the slot,
continuously, rather than one buzz on entry, and no visual marker.

**Sources:**
- We found no write-up of Blade & Sorcery's own haptic rules. Its patch notes list improved haptics
  with the Index controller update and haptics on imbue and spell charge **[search summary]**; the
  reference here is how it feels to the user.
- Meta's haptics design system: a hover is "a short, sharp transient"; a grab confirms the grasp;
  the release "mirrors the grab haptic at lower amplitude and frequency"; success is two events
  over 175 ms. Its continuous effects are amplitude and frequency envelopes (0-1).
- Studies: continuous vibrotactile cues carry proximity well (Kruijff et al. 2022, Frontiers in
  Virtual Reality), and collision vibration improved performance over visuals alone (Bloomfield &
  Badler 2007).
- Section 1: Fallout 4 VR's Virtual Holsters buzz near a zone, Doom 3 BFG VR vibrates in a slot,
  VRIK pulses on hover.
- **OpenXR** (`XrHapticVibration`, `xrApplyHapticFeedback`): a vibration has a duration (ns;
  `XR_MIN_HAPTIC_DURATION` for the shortest), a frequency (Hz; `XR_FREQUENCY_UNSPECIFIED` leaves
  it to the runtime) and an amplitude (0-1); duration and frequency "may be clamped to
  implementation-dependent ranges". A new haptic event from the session on the same action and
  hand **must** interrupt and replace the one playing; an unfocused session gets
  `XR_SESSION_NOT_FOCUSED` and no vibration. `xrStopHapticFeedback` stops it. The spec page names
  no infinite duration, so a continuous buzz is short chunks re-sent.

**The model** (`ik::body_slots` `Depth`, `Contact`, `EnterEdge`, `DrawClick`, `HolsterClick`,
`Refused`, unit-tested in `TestContactHaptics`), at `hapticAmplitude = 0.5` (it scales them all
linearly, clamped at 1; 0 = none):

| Event | Duration | Amplitude | Frequency |
|-------|----------|-----------|-----------|
| hand in a slot (continuous) | while there | 0.08 at the edge (and in the 5 cm exit band) to 0.35 at the center; half while carrying a gun | 70 to 200 Hz with depth |
| crossing in | 35 ms | 0.55 | 160 Hz |
| draw | 20 ms | 0.9 | 320 Hz |
| holster (at the slot, or the return) | 20 ms | 0.65 | 200 Hz (the draw's mirror, lower) |
| no weapon of that type | 120 ms | 0.3 | 60 Hz |

Depth is `1 − distance / radius`. The halving while carrying a gun keeps a gun hand resting near
the hip from droning **[inference]**.

**How it's played** (`core/vr/Haptics`, `vr::haptics::Channel`, unit-tested in
`TestHapticChannel`; the host's frame thread, after the action sync):
- The mod asks for the buzz each tick with a 0.15 s expiry (`HostServices::hapticContinuous`, v10).
- The host sends chunks of at most 100 ms, again when 40 ms are left (if the request outlasts the
  chunk) or when the amplitude moves by 0.02 or the frequency by 5 Hz. No chunk outlasts the
  request, so a stalled or unloaded mod goes quiet within 0.15 s; amplitude 0 sends a stop.
- Pulses (`hapticPulseHz`, v10, or `hapticPulse`) interrupt the buzz, which resumes after them.
- Five failed vibrations in a row stop vibration for the session (logged once). The mod unloading
  clears both hands.
- A v9 host has no continuous buzz: the mod sends the crossing pulse only (logged once).

**[verify]:** whether SteamVR (Steam Link) and the Oculus runtime honor the frequency on Quest
controllers. If not, depth is carried by the amplitude alone, which still rises toward the center.

## 8. The eye height from the floor

**What Halcyon has:** a 1.0 instance with `XR_KHR_D3D11_enable` (and `XR_FB_display_refresh_rate`
when offered); a LOCAL space it re-origins at each recenter (its app space), a LOCAL base space and
a VIEW space. No floor.

**The host** (`core/xr/Floor`, `xr::FloorSpace`, created in `MevrBridge::OnSessionCreated`):
- Lists the session's reference spaces (`xrEnumerateReferenceSpaces`) and takes `LOCAL_FLOOR` if
  listed (core in OpenXR 1.1; a 1.0 instance needs `XR_EXT_local_floor`, which Halcyon doesn't
  enable, so normally not), else `STAGE` (core, optional per runtime). Neither moves with Halcyon's
  recenter, which only re-origins its own app space.
- Makes its own VIEW space and locates it in the floor space each frame at the predicted display
  time: its `y` is the eye height above the floor.
- Publishes it (`HostServices::floor`, v10, `vr::floor::State`, with a size check) with the count
  of recenters and of sessions. A new session gets new spaces (the old ones died with the old
  session).
- Logs `floor: the head's height above the floor comes from STAGE (reference spaces: ...)` and the
  first sample; a runtime with no floor space logs that the setting stays.

**The mod** (`ik::body_slots::StandingHeight`, unit-tested in `TestStandingHeight`;
`game/Holsters`):
- A sample outside 0.9-2.3 m isn't an eye above a floor (no floor set up, or the floor at the head)
  and is ignored.
- **At a recenter** that sample becomes the standing eye height: the player is told to recenter
  standing straight. Up or down, so a new player or a wrong earlier value is fixed by recentering.
- **Otherwise a slow-rising maximum:** higher samples pull it up with a 2 s time constant (a
  tiptoe or a jump barely moves it), lower ones never pull it down.
- A new session starts over. `eyeHeightFromFloor = 0`, an older host, no floor space or no
  plausible sample: `[Holsters] eyeHeightM`. The source is logged when it changes
  (`holsters: eye height 1.62 m from the floor (STAGE), standing (slot scale 0.98)`), and the
  heartbeat and the snapshot's status carry the height, its source and the head's current height.

**Crouching (decided):** the slots' *spacing* comes from the standing height, so crouching doesn't
shrink the layout toward the head (body-height-relative, as the user suggested), while the anchor
still follows the head down (80 ms), so a squat or sitting down carries the slots with the hips.
That is what the sources do: GoldenEye VR places by fractions of standing eye height, Overlord
follows crouch with 80 ms, Quake VR shifts holsters with crouch, and Boneworks' complaint was
holsters that didn't stay where the body was. Not chosen: slots fixed above the floor, which a
squat would leave above the hips. The known weak case is bending at the waist (the head drops, the
hips don't), which moves the low slots down **[verify in headset]**.

## 9. Squad weapons

The game's weapon wheel stays on the left trigger as the fallback. The VR way, option A of
[docs/squad-weapons-design.md](../docs/squad-weapons-design.md), was approved and built on
2026-10-22 (`game/SquadWeapons`, `core/vr/SquadMenu`, the host's `plugin/SquadPanel`): point the
left hand at a squadmate, squeeze the left grip away from the gun, flick the right stick toward a
weapon, let go. The design doc's "As built" has the decisions; this section has what the game does.

### How the weapon wheel applies a squad weapon (verified 2026-10-22)

From the bytecode of `SFXGame.pcc` (decompressed read-only as in [18](18-camera-and-hands.md),
"Method"; jump targets resolved by counting each token's in-memory size, object references being 8
bytes in memory and 4 on disk) and GhidraMCP on the LE1 project. RVAs for 2.0.0.48602.

`SFXGameModeWeaponWheel.Deactivated` (and `SFXGameModeCommand.Deactivated`, the same code):

```
Super.Deactivated();
Outer.WorldInfo.PauseGame(false);
MyBP = BioPawn(Outer.Pawn);
if (MyBP != None) {
    if (!MyBP.IsInCombatActionState()) {            // Shepard's weapon is away
        Squad = MyBP.m_oBehavior.Squad;
        for (Idx = 0; Idx < Squad.GetSquadSize(); ++Idx) {
            if (Squad.GetMember(Idx) != None && Squad.GetMemberSwitchWeapon(Idx) != 4) {
                SquadBehavior = BioPawnBehavior(Squad.GetMember(Idx).oBioComponent);
                if (SquadBehavior != None && SquadBehavior.m_oEquipment != None) {
                    SquadBehavior.m_oEquipment.ChangeToQuickSlot(Squad.GetMemberSwitchWeapon(Idx));
                    Squad.SetMemberSwitchWeapon(Idx, 4);
                    MyBP.UpdatePrimaryAndSecondaryWeapons();
                }
            }
        }
        MyBP.DrawPrimaryWeapon();
    } else {                                         // Shepard's weapon is out
        Outer.ApplyTacticalOrders();
    }
}
Outer.GetScaleFormManager().PlayGuiSound(...);
```

(Reconstructed from the references and jumps; the order, the constants and the branch targets are
certain.) So section 2's "on close" was half right: the per-member loop only runs with Shepard's
weapon away.

The natives (GhidraMCP; names recorded in `tools/ghidra/symbols-le1.csv`):
- `SquadOrderEquipWeapon(nWeapon, Pawn)` (`0xC77FD0`): the member's index (squad vtable `0x828`);
  unless pawn vtable `0x978` says no and if `nWeapon < 4`: **`eAction` = 4** (no tactical action,
  `0x9E0240`, which `execSetMemberTacticalAction` also calls: it was misnamed "delayed switch"
  before) and **`eSwitchWeapon` = nWeapon** (`0x9E0220`, entry byte `+0x71`). Entries are 0x74
  bytes (`FMemberData`, the SDK's packing agrees), the pawn at `+0x48`.
- `ApplyTacticalOrders` (`0xBAD490` -> `0xC71DB0`): unless Shepard is in a vehicle-like state, for
  each member `0xC71A70`: with no power order, a switch weapon of 4 re-issues an ordered attack;
  any other value is reset to 4 and, if it differs from the member's `m_eQuickSlotCurrent`, the
  member's AI controller (pawn `+0x400`) switches through its vtable `0xB38` (skipped while it runs
  an active of a certain class). **[verify]** whether that switch animates.
- `GetMemberDelayedSwitchWeapon` (`0x9D97A0`) reads the AI controller's current active (byte
  `+0x88`), and `0xB180E0` feeds the switch and delayed switch to the HUD movie's
  `ChangeSelectedWeapon` (UI only). Nothing else reads `eSwitchWeapon`: a recorded choice that no
  close applies just sits there (the cheat `BioCheatManager.ChangeWeapon` is `SquadOrderEquipWeapon`
  then `ApplyTacticalOrders`).

### What we call

For the one member chosen, after `SquadOrderEquipWeapon(type, pawn)` (and checking
`GetMemberSwitchWeapon` took it): with Shepard's weapon out, `ApplyTacticalOrders()`; with it away,
`ChangeToQuickSlot(type)` on the member's `BioEquipment` and `SetMemberSwitchWeapon(index, 4)`. The
wheel's two steps for Shepard himself (`UpdatePrimaryAndSecondaryWeapons`, `DrawPrimaryWeapon`) are
left out: his weapon isn't being changed, and drawing it would undo a holstered gun. The squad comes
from the player pawn's `BioPawnBehavior.Squad` through `GetSquadSize` / `GetMember` (what the wheel
uses), Shepard skipped. All through ProcessEvent.

The rest of what the menu shows:
- **Quick slots:** as Shepard's (section 2): `m_QuickSlotArray[4]`, each weapon's
  `GetRangedWeaponType`, `m_eQuickSlotCurrent`; logged with the "slot = weapon type" check.
- **Class:** `BioPawnBehavior.GetClass()` = `BioAttributesPawnParty.m_ClassBase`
  (`EBioPartyMemberClassBase`: soldier, engineer, adept, infiltrator, savant (sentinel), reaver
  (vanguard), asari scientist, krogan old one, turian spectre, quarian tinker, supersoldier, woman
  veteran, man thinker). The henchmen have classes of their own, so the class names them
  **[inference]**: 6 Liara, 7 Wrex, 8 Garrus, 9 Tali, 11 Ashley, 12 Kaidan.
- **Name otherwise:** `BioActorBehavior.ActorGameName`, else its actor type's; in LE1 the game
  usually uses the string ref instead (`bUseStringRefActorGameName`, through the TLK), which we don't
  read: `GetActorGameName` returns an FString the caller must free, and Halcyon traced a heap
  fast-fail to that pattern (`pchud.cpp`). Then the pawn's tag. All candidates are logged once.
- **Health:** `BioEpicPawnBehavior.GetPercentHealth()` (script: `m_HealthCurrent / GetMaxHealth()`).
- **Pause (option):** `WorldInfo.PauseGame(true/false)`, the wheel's own call.

### Controls

The both-sticks chord, the L3 gate and the squad menu's input block are in section 6's guard list.
The left grip away from the gun was "nothing" (`leftGripAway = none`); with `[Squad] enabled` it is
the squad menu and never LB.

## 10. The game's own weapon wheel at the hand (2026-10-22)

The user wanted the squad menu to be ME1's actual UI, with its pause and its rules, rather than our
drawing. So the VR gesture (point, squeeze the left grip away from the gun) now opens the game's own
weapon wheel, the host shows it on a panel at the hand ([research/15](15-hud.md), "The wheels on
their own panel"), and the controllers steer it ([docs/squad-weapons-design.md](../docs/squad-weapons-design.md),
"The game's own wheel"). Sources, all read-only and local: `BIOInput.ini` in `Coalesced_INT.bin`
(section 6), the bytecode of `SFXGame.pcc` (as in section 9), the HUD movies `GUI_SF_HUD.ME_HUD` and
`GUI_SF_HUD.PC_ME_HUD` in `Startup_INT.pcc` (GFxMovieInfo exports; the GFX file inside parsed for its
display list and ActionScript 2 bytecode), and GhidraMCP. RVAs for 2.0.0.48602, names recorded in
`tools/ghidra/symbols-le1.csv`.

### How LB opens and closes it

- **Bindings:** on foot LB is `Console_WeaponWheel` = `OnHold 0.3 EnterWeaponWheel | OnTap 0.3
  TrySwapWeapon`. In the wheel's own mode (`SFXGameModeWeaponWheel`, `DisableMoveTime = 0.25`): the
  right stick is `Console_LookX/Y` (the wheel's steering), LB is `Console_ExitWeaponWheel` =
  `OnRelease ExitWeaponWheel`, Start the pause menu, the d-pad the squad orders. Nothing else.
- **`SFXGameModeDefault.EnterWeaponWheel`** (exec, script): returns if `PlayerInput.IsCombatEnabled()`
  is false; checks the HUD panel (`GetScaleFormManager().GetPanelByTag`); if Shepard's weapon is away
  (`!IsInCombatActionState`) calls `DrawPrimaryWeapon`; `ShowTutorial`; then
  `GameModeManager.EnableMode(3)`.
- **`SFXGameModeWeaponWheel.Activated`:** `SetZoomed` (off), `WorldInfo.PauseGame(true)`, a GUI
  sound, the command target, the profile's stick option (`Helper_SwapStaticConsoleSticks`) and
  `SFXEngine.ResetForcedInactiveParticleSystems`. So the wheel pauses the game itself.
- **`SFXGameModeWeaponWheel.ExitWeaponWheel`:** `GameModeManager.DisableMode(3)`, whose
  `Deactivated` applies the choices (section 9).
- **Why we call the execs, not LB:** holding LB through the pad mapping would add the binding's
  0.3 s, and a squeeze let go sooner would be LB's tap, `TrySwapWeapon` (Shepard's weapon swapped).
  Calling `EnterWeaponWheel` is what the binding does after its 0.3 s, and `ExitWeaponWheel` what
  releasing LB does. LB itself (the left trigger) still opens and closes it as before.

### The wheel in the HUD movie

- **The movies:** `ME_HUD` (console) and `PC_ME_HUD` (PC), 1280x720 stages; ActionScript sets
  `Stage.align = "TL"` and `ScreenResized` snaps the edge groups to `Stage.visibleRect` (GFx's visible
  area). The wheel is the clip `squadMC`, centred on the stage: (298, 129)-(982, 603) in `ME_HUD`
  (with the squadmates' portraits and the weapon tiles), (357, 77)-(914, 633) in `PC_ME_HUD`.
- **One ring:** InitializeHud builds three tables (in array order):
  - `weaponWheelAngleBoundaries` = 1, 12, 51, 90, 129, 169, 193, 214, 238, 270, 302, 326, 347, 361;
  - `weaponWheelBoundaryToSlotMap` = 0, 1, 4, 3, 2, 1, 4, 3, 2, 1, 4, 3, 2, 1;
  - `weaponWheelBoundaryToMemberMap` = -1, 2, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2.

  The movie's `UpdateHUDMode(inputMode, actionState, thumbAngle)` turns the native angle into an arrow
  angle (361 - clamp(thumbAngle, 1, 360)) and `updateArrowAngleInfo` takes the first boundary above it:
  member 0 (Shepard) owns 12-169, member 1 169-270, member 2 270-361 and 1-12, four slots each.
  A new entry is highlighted (`highlightWeapon(slot + member * 5)`) with the
  `HUDWeaponPadNewHighlight` sound. (The power wheel's tables have 26 entries the same way.) In
  `PC_ME_HUD` that function says "this function should be deprecated now" and returns: the PC movie's
  wheel is mouse-driven (`hitTest` in `updateOnMouseMove`), so the pad's wheel is the console movie's
  **[inference; the mod logs the HUD handler and whether its panel uses the gamepad]**.
- **A queues:** the console movie's key handler sends A (and X) to `selectWeaponOrPowerInTacticalMode`,
  which marks the entry and sends the FSCommand `SelectWeaponHUD` "index,member" to
  `BioSFHandler_HUD.HandleEvent` (command 11). Its bytecode: slot 1 -> 3 (sniper rifle), 2 -> 0
  (pistol), 3 -> 2 (assault rifle), anything else -> 1 (shotgun); member 1 -> `GetMember(m_nHench1Index)`,
  2 -> `GetMember(m_nHench2Index)`, else `GetMember(m_nShepardIndex)`; then `SquadOrderEquipWeapon`.
  The native that fills the wheel's tiles (`0xB180E0`, `ChangeSelectedWeapon`) passes the quick slots
  in the order 3, 0, 2, 1: the same map.
- **So the wheel can't be told "this squadmate's ring":** there is none. Pointing can only put the
  highlight on one of their entries (below).

### The stick angle (native)

- `BioSFHandler_HUD.HandleInputEvent` (script) stores input events 2 and 3 in `vInput.X` / `.Y`
  (`+0xDC`): the right stick.
- `UBioSFHandler_HUD::UpdateHUDModes` (`0xB12410`, vtable `0x378` of `execUpdateHUDModes`): inputMode 5
  while mode 3 (the weapon wheel) is active, 6 for mode 2 (the power wheel), else 0 and `nAngle = -1`.
  While either axis is past a dead zone and the stick isn't moving back in (radius at least the last
  one), `nAngle` (`+0x134`) = the yaw of `vInput` in degrees, 0..359 (atan2(up, right)), and
  `nLastAngle` (`+0x138`) = `nAngle`; otherwise `nAngle = nLastAngle`. Then the movie's
  `UpdateHUDMode(inputMode, actionState, nAngle)`. So the highlight stays when the stick springs back,
  and the HUD's kept angle is what a centred stick shows.

### What we drive (game/SquadWeapons, `[Squad] style = native`)

- **Open:** the gesture (unchanged: pointing opens at once, at nobody after `openHoldS`, a missed
  foregrip grab never), then `EnterWeaponWheel()` on the controller's `SFXGameModeDefault`; the wheel is
  open when the mode manager's `CurrentMode` is 3 (given up after 0.6 s: combat disabled in a hub).
- **Preselect:** pointed at a squadmate, the HUD's `nLastAngle` and `nAngle` are set to the centre of
  their weapon-in-hand's arc (their wheel member from `m_nHench1Index` / `m_nHench2Index`, the slot from
  the map above). A centred stick shows it; pushing the stick takes over **[verify]**.
- **Highlight:** read back from `nAngle` through the same tables each tick: a tick in the steering
  hand per new entry, a soft buzz on a weapon the member doesn't carry, logged with the member's name.
- **Close:** letting go of the grip sends A through the pad for 80 ms if the highlighted entry is a
  weapon they carry, not in hand, not yet queued; waits until their squad entry's switch weapon is it
  (`GetMemberSwitchWeapon`, at most 0.4 s); then `ExitWeaponWheel()` on the `SFXGameModeWeaponWheel`.
  B closes without the A. A mode that leaves 3 by itself (LB, Start) is followed.
- **Pad** (host, `gamepad::kFlagNativeWheel`): the steering stick (the right one; `wheelStick = left`
  or `either` sends the left one as the right) reaches the game only after it has been centred once
  since the wheel opened; the left stick, B and the stick clicks are kept from the game; A passes (and
  the mod's A, `kFlagWheelPressA`); what is held when it closes stays kept until let go.
- **The HUD handler:** `PlayerController.GetScaleFormManager()` (native, through ProcessEvent) ->
  `MassEffectGuiManager.m_oSFHudHandler` (`+0x1A4`), a `BioSFHandler_HUD`; its `m_nShepardIndex`,
  `m_nHench1Index`, `m_nHench2Index` (`+0x10C`..`+0x114`).

### Halcyon's decoupled pitch cut the wheels' steering

Halcyon's XInput hooks zero the right stick's Y (decoupled pitch, on in the user's profile) or X
(decoupled yaw) except while a full-screen menu (mode 7) owns the screen (`MenuOwnsLook`). The wheels
(modes 2 and 3) are steered with both axes, so with decoupled pitch only the left and right entries
could be reached. Fixed in `vr_menu.cpp` (`[MEVR]`): modes 2 and 3 count as menus there.

### The power wheel

R3 held (RB, section 6) opens `SFXGameModePowerWheel` (mode 2), the same ring with 26 entries for the
powers. The mod doesn't drive it; the host shows it on its own panel at the right hand
(`[Squad] powerWheelAt`), cut out of the UI the same way.
