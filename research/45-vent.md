# 45: The physical vent of LE1's weapon heat (2026-10-31)

[docs/trilogy-design.md](../docs/trilogy-design.md)'s "Ammo" row: ME1's guns don't reload, they overheat and cool
by themselves; the design adds an optional **vent** that ejects the heat at once, as a physical gesture, the way the
heat-sink reload will work in LE2/LE3. This note records what the weapon keeps about its heat, the gesture, how it is
told apart from the two-handed grip and the grenades, and what to check in the headset. It's a mechanic change, so
it's a setting (`[Vent]` in `ASI\LE1MEVR.ini`) and in that file's change log.

Code: `core/ik/Vent` (the classification, unit-tested in `tests/Main.cpp` `TestVent`), `game/Vent` (ME1, hot-reloaded),
one measurement added to the pose writer (`engine/BonePose` `SupportSample::upMeters` / `sideMeters`), one mask in
`game/SupportGrip`. Log prefix `vent:`; the snapshot's mod status `"vent"`. The visible rack (section 6, 2026-11-04):
`ik::vent::Rack` / `ChoosePart` (`TestVentRack`), `engine/BonePose`'s weapon-part stage, `game/vent` `TickRack`.

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

- The heat's scale and `DoCoolDownEffect`'s meaning are **[verify]** (section 1; section 6 adds a log line that says what
  the effect is).
- ~~No animation: the gun's slide doesn't move.~~ Built 2026-11-04: the visible rack (section 6), **[verify]** in the
  game: it was written without a game run.
- The mod writes engine memory for this (two fields); the design doc's rule 3 ("the game stays authoritative") is bent
  because the game offers no function to ask. Both writes are journalled and the game's own tick (`TickHeat`) runs on
  the new values as it would after a cool-down.

## 6. The visible rack (2026-11-04, built without a game run: everything here is [verify])

The owner chose it for the first release: while the left hand holds the vent grip on a hot gun, a visible part of the
gun's own model follows the hand back along the gun; at the release it snaps forward on a fast spring, vented or not.
Mod-only (hot reload; no HostServices or shared-struct change). `[Vent] animate = 1` by default.

**The part per model.** research/21 records the player's guns as 8 meshes (section 16: `WPN_<type><a|b>_MDL`), their
sight bones and, from the 2026-10-25 log (section 13 item 2), a few other bone names; no full bone list is in the
repository. What that gives (`ik::vent::AutoPartList`, `PartTravel`):

| Type | Auto list (first the model has) | Known from | Travel | How sure |
|---|---|---|---|---|
| Pistol (PSTa, PSTb) | `Barrel_Upper_front`, `Slide`, `Barrel_Upper` | a pistol's `Barrel_Upper_front` (and `Barrel_Lower_front`, `Guard`, `handle`), research/21 s13; which of PSTa/PSTb is unknown | 3 units | medium for that model, low for the other: the upper barrel's front is the top of the gun ahead of the hand, the part the draw's unfolding slides forward, so pulling it back reads as a slide; whether its vertices are the whole top is unknown |
| Shotgun (BLSa, BLSb) | the pistols' names, `Slide`, `Pump` | nothing recorded | 4 | unknown: probably stands down until its bones are logged |
| Assault rifle (ASLa, ASLb) | `Scope` | s16: ASLa's 12.8-unit housing over the grip, ASLb's 6-unit sight behind it | 3 (ASLb's short sight 2) | medium-low: the receiver's top where the left hand takes hold. It is also the red dot's bone (`game/sights` follows it live), but the move is along the bore, so the sight's line doesn't change; the dot's window rides back and forth with it |
| Sniper rifle (SNPa, SNPb) | none | s16: only scope parts over the receiver (`Scope_Eye`, `Scope_Lens`; `Scope_Rear`, `Scope_Mid`, `Scope_Forward`) | 4 | stands down: the scope's eyepiece, its picture and its hidden lens sit on those bones |

Failing the list, auto takes the first bone named for a moving part (slide, bolt, cover, heat sink, sink, vent, receiver,
charging, breech, hatch, eject), never one named for a sight or a grip. Never the root, a bone a socket hangs on or one with
a socket below it (the muzzle `Flash_*`, `Tracer`, `Cooldown`, `Sabotage`, all on `Root` so far), nor a part of more than 32
bones. `[Vent] <type>Part = <bone names>` picks one by hand (the first the model has), `none` turns one type off. A model
without a part logs `vent: rack: the <gun> model <mesh> has no part to rack (<why>): it stays still on a vent. Its bones:
0 Root (root), 1 ... (under ...), ...`; a model with one logs its choice and its bone list too, once per model and setting.
A read-only dump of `BIOC_Materials.pcc`'s eight meshes (research/21 s16's method) would settle the shotguns and confirm the
rest; the in-game lines do the same on the first draw of each.

**How the bone moves** (`engine/bone_pose` weapon-part stage; `game/vent` `TickRack`). The UpdateSkelPose hook already sees
every skeletal mesh; for the published gun mesh it now adds a component-space offset to the part's bone and every bone below
it in `SpaceBases`, right after the engine composed the gun's pose and before the render data update reads it (the same seam
arm IK uses on Shepard's mesh). Checked on each call: the mesh by address, the bone count, each bone's RefSkeleton name, the
game thread, the request's age; inside SEH; journalled (`BonePose.ventPart`). The write is absolute: an entry that still holds
our last write (a call that didn't recompose the pose) is moved from the animation's place remembered then, never twice. The
offset is `back x units`: back = minus the muzzle's barrel (`weapon::Current`) taken into the gun mesh's component space;
units = `ik::vent::RackTarget(Machine::Travel, unitsPerMeter, maxUnits)`, the hand's pull back along the gun since the grab
(meters in the gun's frame, section 2) times the view's world units per meter over the mesh's scale, clamped to the part's
travel (`partTravelUnits` overrides). The mod publishes each tick (one frame of latency); 0.25 s after the part is home the
stage is cleared. Rejected alternatives: a SkelControl (the guns' AnimTrees have none we know of, and building one at run
time is far more engine surgery), `HideBone` (no motion).

**A gun not posed every frame [verify].** If the stage sees no UpdateSkelPose of the gun for 3 ticks while the part moves
(`bNoSkeletonUpdate`, a pose cache, a gun not "recently rendered"), the mod calls `SkeletalMeshComponent.ForceSkelUpdate`
(ProcessEvent, its tick) each tick while the part moves, so the stage runs at once; if not even that reaches the stage 3
times, the rack stands down for that gun. Logs `vent: rack: the <gun>'s pose didn't update for 3 ticks ...`.

**The spring** (`ik::vent::Rack`, unit-tested `TestVentRack`): while held the part sits where the hand put it; let go, a
spring (`partReturnHz` 9, `partDamping` 0.5; 2 ms steps, so any frame time gives the same motion) takes it forward and it
stops at its rest place without swinging through, like a slide slamming home: about 43 ms, four frames at 90 Hz. A hand still
pulling back at the release doesn't carry it further. The same on a vent, a slow or short stroke, a lapsed hold (it springs
back at the lapse) or a lost frame.

**The cool-down effect.** LExSDKv2's headers (names only) say `BioWeaponRanged` keeps six visual effects
(`m_visualEffects[EBioWeaponVisualEffect]`, `BIO_WEAPON_VISUAL_EFFECT_COOL_DOWN = 3`) and a cached `BioWeaponVFXAppearance`
(`m_pCachedVFXAppearance`) with `nmCoolDown` (a socket name: every model has a `Cooldown` socket on `Root`), `vfxCoolDown`
(a `BioVFXTemplate`) and `fCooldownPerSecond`. So `DoCoolDownEffect` most likely spawns that template at the `Cooldown`
socket: the game's own steam or vent effect, already called on every vent (`[Vent] effect = 1`). Its script wasn't read
(UnrealScript in SFXGame, not reachable here). The first vent of each weapon appearance now logs `vent: the game's cool-down
effect on the <weapon>: <appearance>: template <vfx>, at the '<socket>' socket, fCooldownPerSecond N; its effect slot <before>
-> <after>: the call made one | the slot already held it | the call made none`, which says whether it plays.

**Settings** (`[Vent]`, appended to older files): `animate`, `pistolPart`, `shotgunPart`, `assaultRiflePart`,
`sniperRiflePart` (auto), `partTravelUnits` (auto), `partReturnHz` (9), `partDamping` (0.5). Logs `vent: rack:` (the choice
per model, each stroke for the first 20 then every 20th: `the <gun>'s '<bone>' went X units back (Y m of hand pull at N
units per meter; at most M) and snapped forward in T ms: vented | no vent, <why>`, the standing state, the heartbeat's
stage counts). Snapshot: `"mod"."vent"."rack"` (`mesh`, `part`, `partBones`, `source`, `maxUnits`, `unitsPerMeter`,
`offset`, `holding`, `published`, `forced`, `strokes`, `why`, `last`). New reflection rows: `ForceSkelUpdate` (function and
parms), `m_pCachedVFXAppearance`, `m_visualEffects`, `nmCoolDown`, `vfxCoolDown`, `fCooldownPerSecond`.

**Checking it without the controllers** (`mevr vent`, game/ConsoleRequest): `mevr weapon pistol` draws one; `mevr vent part`
prints the model, its part, the back axis, the units per meter and the bone list; `mevr vent rack 1 3` pulls the part its
whole travel for 3 s and lets it go (no vent), so `tools/snapshot.ps1 -Delay 1` catches it back and a second snapshot after
4 s at rest; `mevr vent heat 0.9 hold` keeps the gun hot for a replayed stroke (`mevr vent heat off`). The whole gesture on
the fake headset: `python tools/make-rack-recording.py rack.mevrrec 30 6 0` writes a pistol held ahead of the chest and a left
hand that takes the receiver every 6 s and racks it back at 1.5 m/s over 12 cm; `tools/replay.ps1 -Path rack.mevrrec -Loop
-Inputs` plays it (the left hand's place on the gun is a guess: if the snapshot's `"mod"."vent"` `onGun` stays outside the
zone, the tool's `along` and `up` arguments move it).

**Open [verify]:** that the stage sees the gun's pose each frame (heartbeat: poses seen against ticks; `forced` in the
snapshot); that the chosen bones are the visible slide and cover (a snapshot with `mevr vent rack`); the units per meter
(a 5 cm pull should put the part at its 3-unit stop); the cool-down effect line.
