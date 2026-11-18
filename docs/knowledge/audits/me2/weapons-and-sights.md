---
type: Audit
title: ME2 weapons and sights against ME1's contracts
description: Contract by contract, what ME2 gets of ME1's hand aim, support grip, sights, scope, zoom, gun kick and level camera, and why its rifles zoom differently.
tags: [audit, me2, parity, weapons, sights, scope, zoom]
status: draft
timestamp: 2026-11-18T08:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:00:00Z }
game: LE2
against: ../../me1/weapons-and-sights.md
commit: 940a75c6
---

# Verdict

Audited against [ME1 weapons and sights](../../me1/weapons-and-sights.md) by reading code and notes at 940a75c6; no game
was run.

- ME2 runs ME1's sight code, not a copy: `game/Sights`, `core/vr/Ads`, `core/vr/Sights` and the host's `SightLayers`
  are the same files, with ME1's defaults. 22 of 34 contracts are `same` or `adapter`.
- **Fixed in part 2026-11-18 (lane AU2, the owner's "sniper zoom is unified"): ME1's 3.5x, then 7x after a flick, on ME2 and
  ME3; the zoomed render is still ME1's only.** Was: the sniper rifle zooms 8.0x on ME2 where ME1 shows 3.5x, and from an
  enlarged eye image, not a zoomed render.
  ME2's one `ZoomFOV` (10 degrees on the Mantis) is put against ME1's 70 degree base FOV, which nobody checked for ME2,
  and scope capture does not exist there. Nothing steps the zoom.
- **Until 2026-11-17 the game's own aim zoom magnified the whole view** whenever the support grip sent LT (assault
  rifle, shotgun). The renderer now pins the window (commit 2222cdee), but the game still enters its zoomed state: the
  cause (LT means "zoom" on ME2) is still there, only its picture is gone.
- ME1's measured optics have no ME2 counterpart: no ME2 mesh has an optic profile, so there is no see-through window
  for a dot on a solid housing, and lens sizes are the generic ones.

| rating | count | ids |
|---|---|---|
| same | 16 | WPN-02, 03, 06, 07, 12, 13, 15, 16, 17, 19, 23, 24, 25, 27, 28, 31 (see its note) |
| same core, other inputs | 7 | WPN-05, 08, 09, 10, 18, 20, 22 |
| adapter | 4 | WPN-01, 04, 29, 30 |
| partial | 1 | WPN-11 |
| missing | 2 | WPN-14, 26 |
| n/a | 3 | WPN-21, 32, 33 |
| unknown | 1 | WPN-34 |

"Same" means the same code path with the same numbers. Only the rows whose "seen" column says so were run on ME2 (fake
headset, 2026-11-16); none was seen in a headset.

# What a player gets, weapon by weapon

| ME2 weapon in hand, raised to the eye | ME2 | ME1 |
|---|---|---|
| heavy pistol | red dot, one eye, automatic placement (19.8 units behind the muzzle, 8.5 above the bore, measured). Seen on the fake headset | red dot on the model's sight bone |
| shotgun | red dot, automatic placement; not run | red dot |
| assault rifle | red dot; **no window through the housing** (no optic profile); with both hands at the shoulder the game gets LT and enters its zoomed aim (view no longer magnified since 2222cdee) | red dot seen through the housing's box; LT at the shoulder, the view never zoomed |
| SMG | the assault rifle's profile (dot, the rifle's calibration); not run (Save 4 carries none) | no SMG in ME1 |
| sniper rifle | ADS gate, pull to the eye, `Scope_Lens` hidden, duplex reticle, **8.0x from the other eye's image**, one step only | 3.5x (7x after a flick) from a real zoomed render, 1024 px |
| heavy weapon | no sight at all: its type is unknown to `weapon::Type` | no heavy weapons in ME1 |

# Contract by contract

| id | rating | how ME2 gets it | where it differs | evidence |
|---|---|---|---|---|
| WPN-01 | adapter | post-handler on `SFXWeapon.GetAdjustedAim` and the start-trace function, ME1's rule | names only. 34 shots 0.4 to 0.8 degrees from the barrel | [HandAim.cpp](../../../../src/game/HandAim.cpp):474-486, 555; ledger G02 |
| WPN-02 | same | `Actor.Trace` and `ConvergedShot` on every game | none. Seen: shots land on what the sight line hits | [Sights.cpp](../../../../src/game/Sights.cpp):41, 991-1008; research/le2-vrik.md:290-295 |
| WPN-03 | same | the mod's `setAimRay`, the ring drawn by ME1's `HandReticle` | the Mako half is n/a | [SequelWeaponLayers.cpp](../../../../src/plugin/SequelWeaponLayers.cpp):38-62 |
| WPN-04 | adapter | `ApplyMevrBaseline` pins no head aim after every ini load | Halcyon's `VehicleHeadMode` is not pinned (see the vehicles audit) | [me2_menu.cpp](../../../../src/halcyon/le2/me2_menu.cpp):490, 713 |
| WPN-05 | same core, other inputs | `body_calibration::StanceFor(PresentationProfile())` | the calibration is read from `ASI\LE2MEVR.ini`: a player calibrated in ME1 has none in ME2. The SMG borrows the rifle's stance | [HandAim.cpp](../../../../src/game/HandAim.cpp):198; [Weapon.hpp](../../../../src/game/Weapon.hpp):90-99 |
| WPN-06 | same | `game/SupportGrip` has no game guards; the host merge is ME1's | none in code. In ME2 only the take was logged (during a vault run); the zone's size was never checked against ME2's guns | [SupportGrip.cpp](../../../../src/game/SupportGrip.cpp); research/le2-vrik.md:211; ledger G05 "U" |
| WPN-07 | same | same code | not run on ME2 | as WPN-06 |
| WPN-08 | same core, other inputs | same code | needs ME2's own calibration (WPN-05) | as WPN-06 |
| WPN-09 | same core, other inputs | the same flag forces LT 255 | ME2's LT is `SFXGameModeDefault.TightAim`, which calls `SetZoomed(true)`: the gun zooms. It magnified the whole view until 2026-11-17, and it made the game refuse the vault until lane C4 cleared the flag for that call. `weapon::Zoomed()` answers nothing on ME2 | [SupportGrip.cpp](../../../../src/game/SupportGrip.cpp):278-292; [Weapon.cpp](../../../../src/game/Weapon.cpp):1006-1017; [calcview_hook.cpp](../../../../src/halcyon/le2/calcview_hook.cpp):1020-1027; research/le2-vrik.md:211-226 |
| WPN-10 | same core, other inputs | the same `[Sights]` block and defaults | five weapon families on four profiles: the SMG takes the rifle's; a heavy weapon has no family and gets "none (the held weapon's type is unknown)" | [HandCalibration.cpp](../../../../src/game/HandCalibration.cpp):425-426; [Weapon.cpp](../../../../src/game/Weapon.cpp):448-460; [Sights.cpp](../../../../src/game/Sights.cpp):1026 |
| WPN-11 | partial | sight bone by name (`Scope_Eye` found on the Mantis), else the bones' top, else the typical place | `OpticFor` (the measured eyepieces and housings) is called on ME1 only; no ME2 mesh has a profile, so eyepiece offsets and lens radii are generic | [Sights.cpp](../../../../src/game/Sights.cpp):414-422 |
| WPN-12 | same | host `SightLayers`, unchanged | none. Seen: `dotEye: right` on the pistol | [SequelWeaponLayers.cpp](../../../../src/plugin/SequelWeaponLayers.cpp):12-37 |
| WPN-13 | same | same host and mod code | not run | as WPN-12 |
| WPN-14 | missing | - | the window needs `opticApplies`, which needs an optic profile; ME2 has none, so the flag is never set. Whether ME2's assault rifles have solid housings that hide the target was not looked at | [Sights.cpp](../../../../src/game/Sights.cpp):419-422, 1058 |
| WPN-15 | same | `assistDotMaxM = 0` from the shared block | none | [Sights.hpp](../../../../src/core/vr/Sights.hpp) |
| WPN-16 | same | the ADS gate in `Current` | none. Seen: `ADS on` 0.1 s after the raise | research/le2-vrik.md:296-299 |
| WPN-17 | same | `core/vr/Ads`, same numbers | none | [Ads.hpp](../../../../src/core/vr/Ads.hpp) |
| WPN-18 | same core, other inputs | same code | needs a stock point from ME2's own calibration | as WPN-05 |
| WPN-19 | same | virtual eyepiece, the other eye's image | none in the rule. Seen: `pictureSource: eye copy`, 1,922 frames | research/le2-vrik.md:296-299 |
| WPN-20 | same core, other inputs | `SFXWeapon.ZoomFOV`, sniper rifles only, one mode | the FOV is divided into ME1's 70 degree base (`kGameCombatFovDeg`); ME2's base FOV was not read. Result 8.0x for the Mantis (10 degrees) against ME1's 3.5x. Other families never read it: fallback 1.5x if set to `scope` | [Sights.cpp](../../../../src/game/Sights.cpp):69-70, 846-856; [Sights.hpp](../../../../src/core/vr/Sights.hpp):208-210 |
| WPN-21 | n/a | the flick code runs but ME2's weapons have one zoom step, so it never steps | ME2 itself has no second magnification | [Sights.cpp](../../../../src/game/Sights.cpp):875-892 |
| WPN-22 | same core, other inputs | one aim-error call with `bIsZoomed` set, through `SFXWeapon.GetAimError`; put back in a `__finally` since 2026-11-17 | the game's own zoom state does start on ME2 for non-sniper guns (WPN-09); its picture is suppressed in the renderer, not at the source. R3 is kept from the game, so no native magnification step | [Sights.cpp](../../../../src/game/Sights.cpp):46-56, 240-273; [Weapon.cpp](../../../../src/game/Weapon.cpp):1083-1096 |
| WPN-23 | same | same assist code and limits | not measured on ME2's eyepiece geometry | [Sights.cpp](../../../../src/game/Sights.cpp) |
| WPN-24 | same | same | not run | as WPN-23 |
| WPN-25 | same | `HideBone` on every game; `auto` without an optic profile hides `Scope_Lens` on a sniper rifle | none. Seen: `hid 'Scope_Lens'` on the Mantis. Other sniper meshes not checked | [ScopeParts.cpp](../../../../src/game/ScopeParts.cpp):28-30; [Sights.cpp](../../../../src/core/vr/Sights.cpp):173-181 |
| WPN-26 | missing | - | `scope_capture::Create` returns unless `runtime::Ready()` (ME1 only); the ME2 host does not serve `setScopeCapture`; its scene-capture hooks stand down (ledger R31). The 8x picture is the other eye's image enlarged | [ScopeCapture.cpp](../../../../src/game/ScopeCapture.cpp):317; ledger lines 176, 815 |
| WPN-27 | same | duplex; the game-art search is compiled for ME1 only | none | [Sights.cpp](../../../../src/game/Sights.cpp):910-974 |
| WPN-28 | same | the ring hides 0.3 s after an eye looked through the sight | none | [SequelWeaponLayers.cpp](../../../../src/plugin/SequelWeaponLayers.cpp):33-37, 57 |
| WPN-29 | adapter | recoil from `SFXWeapon.GetWeaponRecoil`, shots from ViewLevel's `SetWeaponRecoil` handler | names only. `gun kick: first kick` seen | [GunKick.cpp](../../../../src/game/GunKick.cpp):86-95; ledger G09 |
| WPN-30 | adapter | the same `PlayerTick` hold; ME2 also has a yaw kick, taken out of the queue | the allowed camera modes are ME1's class names. Pitch held in 4,723 of 4,723 ticks | [ViewLevel.cpp](../../../../src/game/ViewLevel.cpp):427-445; [ViewLevel.cpp (core)](../../../../src/core/vr/ViewLevel.cpp):71-81 |
| WPN-31 | same | the same two handlers and names | never measured on ME2 (ledger G10). The cover entries in the list are ME1's (`EnterCover`, `HipAimCover`); ME2 has no `HipAimCover`, so in ME2's cover cameras the removal would not apply. VR never snaps to cover, so this may never matter | [ViewLevel.cpp (core)](../../../../src/core/vr/ViewLevel.cpp):126-139; [engine_probe.cpp](../../../../src/halcyon/le2/engine_probe.cpp):1080 |
| WPN-32 | n/a | thermal clips, no heat. The counterpart, the reload by hand, is off by the owner's word (`[Reload] enabled = 0`) | - | [HandCalibration.cpp](../../../../src/game/HandCalibration.cpp):661-672 |
| WPN-33 | n/a | as WPN-32 | - | [Vent.cpp](../../../../src/game/Vent.cpp):184 |
| WPN-34 | unknown | `game/HudPieces` has ME2 branches (the ammo power's socket) that were not read for this audit | a snapshot's `hudPieces` with the gun lowered and raised would settle whether the panel leaves the gun at 0.55 | [HudPieces.cpp](../../../../src/game/HudPieces.cpp):37, 202-271 |

# Reinvented

Nothing in this category is a second implementation. The one near-copy: ME2's zoom is read by its own six lines beside
ME1's `AimModes` reader, both feeding `sv::AddAimMode`. That is an adapter, as intended.

# Rediscovered problems

| problem on ME2 | ME1's catalog already had | cost |
|---|---|---|
| Aiming an assault rifle or shotgun magnified the whole view; the owner found it in the headset on 2026-11-17 | "Setting `bIsZoomed` ... switched the camera to the scope view" (research/18, research/21) and the catalog's "Nothing zooms the player's view" | shipped to the owner's headset; fixed in the renderer the same day (2222cdee) |
| The aim-error call left `bIsZoomed` set if the script faulted | ME1's rule "never where the camera can see it" | found by review, fixed 2026-11-17 (30cf473c) |
| The vault refused while both hands held the gun, because LT had zoomed it | same root: LT is a zoom | one lane (C4), 93acd329 |

All three come from one fact that ME1's notes state and the port did not carry over as a rule: on these games LT is
not "aimed fire", it is the game's zoom.

# Forced by the game

| difference | evidence |
|---|---|
| One zoom step per weapon, on the weapon itself | `SFXWeapon.ZoomFOV`; research/le2-vrik.md:243 |
| No heat, so no vent and no rack | thermal clips; ledger G41 |
| The aim-error function has another shape | `GetPlayerAimError()` takes no accuracy on ME2; [Sights.cpp](../../../../src/game/Sights.cpp):52-55 |
| A yaw recoil kick exists | [ViewLevel.cpp](../../../../src/game/ViewLevel.cpp):427-431 |
| Five weapon families and heavy weapons | [Weapon.cpp](../../../../src/game/Weapon.cpp):73-84 |

# Not forced

| difference | what it is |
|---|---|
| 8.0x on the sniper rifle | ME1's base FOV constant used for ME2; and no decision that 8x from an eye copy is what the player should see |
| No scope capture | the host service and ME2's capture hooks were not finished (ledger R31 has ME2's addresses) |
| No optic profiles, so no dot window and generic lens sizes | nobody measured ME2's meshes; research/le2-le3-vr-optics.md lists it as a follow-up |
| LT from the support grip starts the game's zoom | `supportAims` was carried over unchanged; whether ME2 should get LT at all, or the aim error by the one-call route as scopes do, was never asked |
| Calibration per game file | see the body audit, BOD-36 |
| A heavy weapon has no sight | no profile mapped for it |

# What to do

1. **Decide the sniper rifle's magnification** (owner question: ME1's 3.5x feel, or the game's own 8x). Then read ME2's
   real base FOV instead of 70 in `ReadGameZoom` ([Sights.cpp](../../../../src/game/Sights.cpp):846). Check:
   `tools/le2-halcyon-run/sights.ps1`, the log's `ZoomFOV ... x` line and a snapshot of the aiming eye.
2. **Stop the game's zoom at its source for non-sniper guns**, or confirm the renderer pin is enough: while LT is forced,
   check `bIsZoomed`, the camera mode and the HUD on ME2 (`mevr calltrace 5 setzoomed tightaim`), and make
   `weapon::Zoomed()` answer on ME2 ([Weapon.cpp](../../../../src/game/Weapon.cpp):1006). Owner question: should the
   two-handed hold send LT on ME2 at all?
3. **Scope capture on ME2**: finish ledger R31 (`sceneCaptureVerified`), serve `setScopeCapture` in the ME2 host, change
   `Ready()` to `Up()` in [ScopeCapture.cpp](../../../../src/game/ScopeCapture.cpp):317 with ReflectCheck rows. Check:
   snapshot `sights.pictureSource` reads `capture`.
4. **Measure ME2's sights** (Avenger, Vindicator, Mantis, Viper, Predator, the SMGs) and add them to `kOptics`; make
   `OpticFor` run on every game ([Sights.cpp](../../../../src/game/Sights.cpp):419). Check: `sights: the ... model is ...:
   the game's ... sight` and, where a housing is solid, `[MEVR-SIGHT] first window`.
5. **Run what was only read**: support grip zone and two-hand aim, shoulder ADS, the pull, aim assist removal, the gun
   panel (WPN-06 to 08, 18, 23, 24, 31, 34), by a replay with the left grip in the zone and snapshots.
6. A heavy weapon in hand: say in the log and to the owner that it has no sight, or map it to a profile.

# Proposal: the shouldered gun's accuracy without the game's zoom

Recorded 2026-11-18 at the owner's request ("find a way to not introduce the negative side effects of the zoom, since it
likely has an impact on accuracy as well"). A proposal from reading the code: not built, not run in the game.

**The problem (WPN-09).** Both hands on a long gun at the shoulder set `options.supportAims`
([SupportGrip.cpp](../../../../src/game/SupportGrip.cpp), the "Aimed fire" lines), which forces LT to 255. On ME1 LT is
the game's aimed fire and nothing shows. On ME2 LT is `SFXGameModeDefault.TightAim`, which calls `SetZoomed(true)`: the
weapon enters the game's zoomed state. The renderer keeps the picture from zooming since 2222cdee, but the state itself
stays on, with whatever else it changes.

**What exists already (WPN-22).** The spread of a shot is one call. On ME2 it is `SFXWeapon.GetAimError(Accuracy)`,
which lerps a range that `SFXWeapon.GetWeaponAimErrorRange` picks by `bIsZoomed`: `MinZoomAimError`/`MaxZoomAimError`
when set, `MinAimError`/`MaxAimError` otherwise (research/le2-vrik.md, "The sights"). `OnAimError` in
[Sights.cpp](../../../../src/game/Sights.cpp) is a post-handler on that call. It finds the accuracy the unzoomed answer
was made from, then asks `weapon::ZoomedAimError` ([Weapon.cpp](../../../../src/game/Weapon.cpp)) for the same
accuracy with `bIsZoomed` set for that one call (`WithFlagSet`: journalled, put back in a `__finally`), and writes the
zoomed answer into the result. The camera and the rest of the game never see the flag. It runs on ME2 today, but only
while a scope's picture is up (`gScopeUp && gAimedFire`).

**The change.**

1. On ME2, do not send LT from the shouldered support grip (`options.supportAims` false there, or the flag kept from the
   pad). The game never enters `TightAim` from the gesture.
2. Open `OnAimError`'s gate for the same condition that sends LT today: the support grip held and the gun at the shoulder
   (`AtShoulder()`), for every weapon type the grip aims with (not the sniper rifle, which has the scope's route).
   `support_grip` publishes the condition; `sights` reads it beside `gScopeUp`.

The shot then gets ME2's zoomed spread and the weapon is never zoomed.

**What this does not carry over.** Anything else ME2's zoomed state changes besides the spread range. What
`SetZoomed(true)` touches on ME2 has not been traced: recoil, damage, movement speed and a sniper rifle's time dilation
are the candidates. A slowdown is a loss the owner would want; a recoil bonus would need its own one-call route. Trace
it before building (Ghidra or the script: `SFXWeapon.SetZoomed`, `StartZoom`, and every reader of `bIsZoomed`).

**The trace (2026-11-18, lane AU2; ME2's and ME3's SFXGame scripts read with `tools/conversation-scan --script grep:bIsZoomed`
and `grep:IsZoomed(`, `--game LE2|LE3`; natives not traced).** The zoomed state changes much more than the spread's range, so the
proposal above carries only part of the zoom's accuracy and none of its other effects. What reads it:
- *Accuracy itself* (both): the inventory manager's per-tick `UpdatePlayerAccuracy` starts from a zoomed base inaccuracy
  (`Base_Acc_Zoom` instead of `Base_Acc_Standing`) and uses the weapon's zoomed fire penalty and recovery (`ZoomAccFirePenalty`,
  `ZoomAccFireInterpSpeed`). That `Accuracy` is what `GetAimError(Accuracy)` lerps, so the one-call route (the range only) leaves
  the shouldered shot with the hip's accuracy. The crosshair's size follows it too (`GetPlayerCrosshairValue`).
- *Recoil* (both): `GetWeaponRecoil` (`ZoomRecoil`), `SetWeaponRecoil` (a yaw bias and `ZoomRecoilCap`), `UpdateKickbackFade`
  (`RecoilZoomFadeSpeed`). Recoil goes into the view rotation, which the hand aim and view level make moot in VR.
- *ME3 only:* **a sniper rifle fired unzoomed deals less damage** (`SFXWeapon_SniperRifle_Base.GetFireModeBaseDamage` multiplies
  by `SniperRifleDamagePenalty` when not zoomed): every VR sniper shot on ME3 is penalized today, scope picture or not. The
  shotgun's pellet cone narrows past 5 m when zoomed (`PollLatentFire`, `ZoomAccuracyBonus`). `SetZoomed` also sets the audio's
  player position (an RTPC) and a scope mod's zoom sound.
- *Sniper extras* (both): scope sway when zoomed with a sniper camera (`ApplySniperDrift`, a loss) and ME2's Infiltrator-style
  slowed time (`SFXGameEffect_SniperTimeDilation`, a gain), both keyed on zoomed + a sniper rifle.
- *Admission* (both): `TryMelee` and `TryHeavyMelee` do nothing while zoomed; ME3's `TryRoll`/`TryForwardRoll` and cover moves
  ask it too; aim assist's friction runs only while zoomed (`PreProcessInput`).
- Not found in script: a movement-speed change (natives not traced).
Options for the owner (none built): (A) the proposal as written (the range only); (B) the shot's own calls zoomed for their
duration only while shouldered or scoped (`bIsZoomed` set around `UpdatePlayerAccuracy`, `GetWeaponAimErrorRange`, ME3's
`GetFireModeBaseDamage` and `PollLatentFire`, journalled and put back in a `__finally`, as `weapon::ZoomedAimError` does for one
call): the accuracy and ME3's sniper damage carried over, the camera, sway, time dilation and the melee/roll refusals never
seen; (C) as B plus the sniper's time dilation (a class gain the game ties to the scope). Recommendation: B, and on ME3 the
sniper's damage call zoomed whenever the scope's picture is up, which fixes a loss that exists today.

**ME1.** The same change would work there (its handler is the same code on `BioWeaponRanged.GetPlayerAimError`), and
both games would then shoulder the same way. It changes ME1's behaviour, so it waits for the owner's word.

**The check that closes it.** On the fake headset with replayed hands at the shoulder
(`tools/make-walk-recording.py --hands`, a recording with the left grip in the support zone): the log's
`support grip: aimed fire on` without the weapon's `bIsZoomed` going on, `sights: first scoped shot with aimed fire:
aim error a -> b` with b below a for a rifle, and `mevr calltrace 10 tightaim setzoomed` printing nothing while shouldered.

# Citations

- [The catalog file](../../me1/weapons-and-sights.md); [the parity ledger](../../../trilogy-parity-ledger.md) rows G02
  to G10, G41, R31. Where this audit disagrees with it: G06 "shared V" is right for the code but hides the 8x zoom and
  the missing optic profiles; G05 "shared U" has never been run beyond one log line.
- [research/le2-vrik.md](../../../../research/le2-vrik.md), [research/le2-le3-vr-optics.md](../../../../research/le2-le3-vr-optics.md),
  [research/21](../../../../research/21-sights-and-scopes.md), [research/39](../../../../research/39-scope-capture.md).
