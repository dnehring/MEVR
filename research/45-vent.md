# 45: The physical vent of LE1's weapon heat (2026-10-31)

[docs/trilogy-design.md](../docs/trilogy-design.md)'s "Ammo" row: ME1's guns don't reload, they overheat and cool
by themselves; the design adds an optional **vent** that ejects the heat at once, as a physical gesture, the way the
heat-sink reload will work in LE2/LE3. This note records what the weapon keeps about its heat, the gesture, how it is
told apart from the two-handed grip and the grenades, and what to check in the headset. It's a mechanic change, so
it's a setting (`[Vent]` in `ASI\LE1MEVR.ini`) and in that file's change log.

Code: `core/ik/Vent` (the classification, unit-tested in `tests/Main.cpp` `TestVent`), `game/Vent` (ME1, hot-reloaded),
one measurement added to the pose writer (`engine/BonePose` `SupportSample::upMeters` / `sideMeters`), one mask in
`game/SupportGrip`. Log prefix `vent:`; the snapshot's mod status `"vent"`.

## 1. The weapon's heat (LExSDKv2's LE1 headers, read for names and offsets only)

Everything is on `SFXGame.BioWeaponRanged` (the held weapon, `Pawn.Weapon`; `game/weapon` already verifies it):

| Member | Offset | What |
|---|---|---|
| `m_fCurrentHeat` (float) | +0x500 | the heat now |
| `m_bOverheated` (bool bit) | +0x548 bit 0x10 | the gun has overheated and won't fire until it cools |
| `m_bShootingWhileOverheatedSoundPlayed` | +0x548 bit 0x20 | the "click" sound was played once |
| `m_coolDownDuration`, `m_CooldownTime` (float) | +0x4E4, +0x4E8 | the overheat's cool-down timing (not touched) |
| `TickHeat(float)` | script | the per-tick heat loss (and the overheat's end) |
| `PlayOverheatSound()`, `PlayShootingWhileOverheatedSound()` | script | the sounds |
| `event DoCoolDownEffect()` | script event, no parameters | the cool-down's own feedback (effect and sound) |
| `ResetFiringState()` | script | the trigger state; not heat |

The per-shot numbers live elsewhere: `SFXWeaponStatsBase.HeatPerShot`, `HeatLossRate`, `OverheatedHeatLossRate` (the
weapon's `Stats`, +0x4C0; derived from `HeatPerShotRange` etc. and the item's heatsink efficiency), and the item's
attributes `BioAttributesItemWeaponRanged.m_HeatPerShot`, `m_HeatMax`, `m_HeatMin`, `m_HeatLossRate`,
`m_HeatLossRateWhileOverheated` (complex attribute structs). The HUD's heat bar is `HUD_INVOKELABEL_UpdateWeaponHeat`.

**There is no function that cools a weapon** (no `ResetHeat`, `ClearOverheat` or the like on `BioWeaponRanged`, its
stats or the pawn), so the vent **writes the two fields**: `m_fCurrentHeat = 0` and `m_bOverheated = false`, each
through our reflection layer (`ue3::FieldRef`, rows in `engine/ReflectCheck`) and journalled
(`write_journal::Wrote("Vent.heat" / "Vent.overheated")`, research/36). It then calls the game's own
`DoCoolDownEffect()` through ProcessEvent on the game thread (the mod's tick; `[Vent] effect = 0` leaves it out), so
the player gets the game's cool-down feedback rather than silence. **[verify]** in the headset: that the event's
effect is the one the player hears when an overheated gun becomes ready, and that firing resumes at once after a vent
from the overheated state (if `TickHeat` keeps a timer in `m_CooldownTime` that blocks the trigger, that field goes
in the write too).

**The heat's scale [verify].** `ventMinHeat` is a fraction of `ventMaxHeat`, and `ventMaxHeat` defaults to 1.0, the
normalized heat ME1's HUD bar suggests. If the game keeps `m_fCurrentHeat` in another scale (the item's `m_HeatMax`),
the log says so: each overheat logs `vent: the <weapon> overheated at heat <m_fCurrentHeat> (ventMaxHeat ...)` and
adds a hint when the two are more than 15% apart; the heartbeat line carries the largest heat seen. Set `ventMaxHeat`
to that value then.

## 2. The gesture

With a weapon drawn, the **left hand** squeezes its grip on the gun's **receiver**: over and just ahead of the right
hand (the slide, the charging handle, the ejection port), behind the foregrip. Then it **pulls back and away in one
fast stroke and lets go**, like racking a slide. The heat is gone, both hands pulse, and the vent is spent for
`ventCooldownS` (4 s). Only with heat worth venting (`ventMinHeat`, 0.3 of the overheat): a squeeze at the receiver
of a cool gun is just a grip (below).

Measured **in the gun's frame, relative to the right hand**, not in the room: the pose writer already measures the left
hand's goal along the barrel for the support grip (`SupportSample::alongMeters`); it now also measures its height above
the bore and its distance beside it (`upMeters`, `sideMeters`), using the gun's top that arm IK gives the gun
(`ik::GunUpInHand`, passed as `Support::up`). So both hands swinging together is no rack, and the stroke's direction is
"back along this gun", whichever way the gun points.

The receiver zone (`[Vent] zone*`, `ik::vent::Zone`): a box from 0.06 m behind the right hand to 0.14 m ahead, 0.14 m
above the bore and 0.03 m below it, 0.09 m to either side, with a 2 cm hysteresis once the hand is in. The bottom is
shallow on purpose: a hand cupped under a pistol's grip (the support grip's place on a pistol, `ik::UnderGrip`) is below
it and stays the support grip.

The classification (`ik::vent::Machine`, the grenades' conventions from research/20): a NEW left squeeze with the hand
in the zone takes the receiver (`Grabbed`); the analog grip lets go at 0.4 or early when it opens during a fast motion
(`throwing::UpdateGrip`); at the release the velocity is the fastest moment of the last ~120 ms
(`throwing::EstimateRelease`) in the gun's frame, and the rack counts when its speed is at least `ventSpeedMS` (1.0 m/s),
at least 30% of it points back along the gun (`backFraction`; a stroke forward or straight away isn't a rack), and the
hand came back at least `ventTravelM` (0.05 m) since the squeeze. Slower, forward or short: `Slow`, nothing. A squeeze
held longer than `ventHoldMaxS` (1.5 s) lapses (it's a grip, not a rack; nothing when it opens). The frame going away
(weapon holstered, arm IK off) drops the hold.

Haptics: a soft tick in the left hand entering the receiver zone of a HOT gun (so the player learns where it is and
when it matters), the grenades' grab click on the squeeze, a strong 110 Hz pulse in **both** hands on the vent, the
refused buzz for a squeeze that can't vent (cooldown, cool gun).

## 3. Arbitration of a left squeeze

CLAUDE.md's rule stands: "a left squeeze in the support grip's zone is the support grip, never a grenade". The support
grip's grab zone is a capsule along the gun from `supportFrontM` (0.10 m) ahead of the right hand to past the muzzle,
radius `supportAcquireM` (0.18 m), so it covers the receiver too, and the pad mapping (the host's XInput hook,
`gamepad::UpdateSupportGrip`) decides on a **new squeeze only**, from the distance `game/support_grip` publishes. The
order, in `Mod.cpp`'s tick:

1. **The support grip (host).** While the left hand is in the receiver zone of a **hot** gun (heat at or above
   `ventMinHeat`, no cooldown running) and nothing holds the support grip, `support_grip::Tick` publishes "no grab
   zone" (`vent::MasksSupportZone`), so a squeeze there counts as "away from the gun" (the host's `leftGripAway`
   counter) and reaches the mod; it stays "away" until the grip opens (the mapping never takes a support grip
   mid-squeeze). A **held** support grip is never let go for it (the hand may slide back along the gun while holding),
   and a **cool** gun's receiver stays the support grip as before. Log `support grip: the grab zone is hidden from the
   pad mapping (...)` / `back for ...`.
2. **The grenades** (`throwables::Claim`): a new squeeze "away" at the grenade slot takes a grenade (the left hand only
   with `[Throwables] hands = left | both`).
3. **The vent** (`vent::Claim`): a new squeeze "away" the grenades didn't claim, with the hand in the receiver zone.
4. The squad menu and holsters never take the left grip (since 2026-10-22), so nothing else does.

With a host older than v11 (no `leftGripAway` report) the mod's own latch of the grip stands in, as the grenades do,
and warns once.

Conditions for the vent to be active at all (`vent:` log line on each change): `[Vent] enabled`, the heat fields found
(`vent: BioWeaponRanged.m_fCurrentHeat and m_bOverheated found` at startup), on foot (game mode 0), a head-tracked
world view, controllers active, a verified ranged weapon in the right hand (`weapon::HeldObject`), arm IK asking the
writer for the support grip (that's where the gun frame comes from). A squeeze while the pawn is switching weapons
(`BioPawn.IsSwitchingWeapons`) is refused ("not now").

## 4. What to check in the headset

- The log at startup: `vent: BioWeaponRanged.m_fCurrentHeat and m_bOverheated found; DoCoolDownEffect found`, and
  `reflect:` agreeing on the three new rows (`m_fCurrentHeat`, `m_bOverheated`, `DoCoolDownEffect`).
- Fire the assault rifle until it overheats: `vent: the <weapon> overheated at heat X` — X should be about 1.0; if not,
  set `ventMaxHeat`.
- Hot gun, left hand over the receiver: the soft tick, `vent: the left hand is at the receiver (... along, ... above,
  ... beside; heat N%): a squeeze and a pull back vents it`, and `support grip: the grab zone is hidden`. The
  `along`/`up`/`side` numbers say whether the zone's defaults fit the player's hands and each gun (pistol slide, rifle
  receiver); `zone*` tunes them live.
- Squeeze, pull back, let go: `vent: VENTED the <weapon>: heat ... -> 0 ...`, the HUD's heat bar empty, both hands
  pulsing, the game's cool-down sound (`effect`). Fire again at once, from the overheated state too.
- Slow or forward strokes: `vent: the left hand let go at ... : no vent (too slow | not back along the gun | too short a
  pull)`; tune `ventSpeedMS` / `ventTravelM` if real racks fail.
- The support grip still works: a cool gun's receiver, the foregrip of a hot gun (0.25 m and more ahead), a pistol
  cupped from below. A hot gun's receiver must NOT start the two-handed grip (the mask is in time: the mod publishes the
  distance each tick; a squeeze within a tick of entering the zone may still be the support grip).
- The grenades: `[Throwables] hands = both`, a left squeeze at the chest slot still takes a grenade with a hot gun in
  hand.
- The snapshot's `"mod"."vent"`: `onGun`, `inZone`, `holding`, `masksSupportZone`, `heat`, `cooldownLeft`, `last`.

## 5. Not done, and open

- The heat's scale and `DoCoolDownEffect`'s meaning are **[verify]** (section 1).
- No animation: the gun's slide doesn't move. A visible rack would need the weapon's own bones (research/21 lists them
  per model); later, if the headset says the gesture is worth it.
- The mod writes engine memory for this (two fields); the design doc's rule 3 ("the game stays authoritative") is bent
  because the game offers no function to ask. Both writes are journalled and the game's own tick (`TickHeat`) runs on
  the new values as it would after a cool-down.
