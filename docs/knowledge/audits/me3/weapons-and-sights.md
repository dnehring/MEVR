---
type: Audit
title: ME3 weapons and sights against ME1's contracts
description: Contract by contract, what ME3 gets of ME1's hand aim, support grip, sights, scope, zoom, gun kick and level camera; why its rifles zoom differently from ME1's and ME2's; and whether ME2's shouldered-gun proposal carries over.
tags: [audit, me3, parity, weapons, sights, scope, zoom]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/weapons-and-sights.md
commit: 583591c6
---

# Verdict

Audited against [ME1 weapons and sights](../../me1/weapons-and-sights.md) by reading code and notes at 583591c6. No game
was run, and **nothing in this category has ever run on ME3 with a gun in the hand**: the only ME3 saves that load are on
the Normandy with a non-combat pawn ([research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md)). Every
"same" below means "the same code path compiles and its handles resolve", not "seen".

- ME3 runs ME1's sight code, not a copy: `game/Sights`, `core/vr/Ads`, `core/vr/Sights`, `game/SupportGrip` and the host's
  `SightLayers` are the same files with ME1's defaults. 26 of 34 contracts are `same`, `same core, other inputs` or `adapter`.
- **A scope's magnification is not the weapon's on ME3: every sniper rifle shows a fixed 4.0x.** `ReadGameZoom` has an ME1
  branch (`Stats.AimModes`) and an ME2 branch (`SFXWeapon.ZoomFOV`) and nothing for ME3; the fallback constant answers.
  ME1 shows 3.5x then 7x, ME2 8.0x, ME3 4.0x: three games, three numbers, none of them chosen. The stick flick never steps.
  **Fixed in part 2026-11-18 (lane AU2): ME1's 3.5x and 7x (the flick steps) on both sequels; not seen in the game (the
  combat fixture's Soldier carries no sniper rifle); the zoomed render still ME1's only.**
- **The support grip at the shoulder sends LT, which on ME3 (as on ME2) is the game's zoom**, for every gun but a sniper
  rifle. The renderer pins the picture (2222cdee, both sequels); the weapon's zoomed state still starts. ME2's proposal
  (the last section of [its audit](../me2/weapons-and-sights.md)) applies to ME3 with the same route.
- Three things ME1 has are absent: the scope's zoomed render (scope capture), the see-through window for a dot on a solid
  housing (no ME3 optic profiles), and the weapon panel on the gun (`game/HudPieces` stands down by name on ME3).
- ME3-only gaps the catalog has no row for: a heavy weapon has no sight; a scope **weapon mod** is not looked at, so a rifle
  with a scope mod gets the family's dot.

| rating | count | ids |
|---|---|---|
| same | 16 | WPN-02, 03, 06, 07, 12, 13, 15, 16, 17, 19, 23, 24, 25, 27, 28, 31 |
| same core, other inputs | 6 | WPN-05, 08, 09, 10, 18, 22 |
| adapter | 4 | WPN-01, 04, 29, 30 |
| reinvented | 0 | |
| partial | 1 | WPN-11 |
| missing | 5 | WPN-14, 20, 21, 26, 34 |
| n/a | 2 | WPN-32, 33 |
| unknown | 0 | |

# What a player gets, weapon by weapon

Raised to the eye, by reading. "Profile" is `weapon::PresentationProfile`
([Weapon.hpp](../../../../src/game/Weapon.hpp):90-99); the family comes from the weapon's class
([Weapon.cpp](../../../../src/game/Weapon.cpp):448-461).

| ME3 weapon in hand | ME3 | ME2 | ME1 |
|---|---|---|---|
| pistol (`SFXWeapon_Pistol_Base`) | red dot, one eye; placed on a sight bone if the mesh has one by name, else the bones' top, else a typical place. No optic profile | the same; seen on the fake headset | red dot on the model's sight bone |
| shotgun (`SFXWeapon_Shotgun_Base`) | red dot, automatic placement. Both hands at the shoulder: LT, the game's zoomed state (view not magnified) | the same | red dot; LT at the shoulder is aimed fire only |
| assault rifle (`SFXWeapon_AssaultRifle_Base`) | red dot; **no window through a solid housing**; LT at the shoulder as the shotgun | the same | red dot seen through the housing's box |
| SMG (`SFXWeapon_SMG_Base`) | the assault rifle's profile: dot, the rifle's calibration and stance | the same | no SMG |
| sniper rifle (`SFXWeapon_SniperRifle_Base`) | ADS gate, pull to the eye, `Scope_Lens` hidden if the mesh has a bone of that name, duplex reticle, **4.0x fixed, from the other eye's image**, no step. No LT from the grip | 8.0x (the Mantis's `ZoomFOV` over ME1's 70 degrees), eye image, no step | 3.5x, 7x after a flick, from a zoomed render at 1024 px |
| any of the above with a **scope mod** | the family's sight: the mod is not read (the mod's aim mode and reticle exist in the game, [research/le2-le3-vr-optics.md](../../../../research/le2-le3-vr-optics.md) "What the sequel packages establish") | no weapon mods | no weapon mods |
| heavy weapon (`SFXHeavyWeapon`) | no sight: no family, `none (the held weapon's type is unknown)` | the same | none in ME1 |
| the weapon panel (ammo, silhouette) | stays on the HUD panel; never on the gun | on the gun (`Layout::Me2`) | on the gun, leaves while raised |

# Rifle zoom on ME3, traced

| question | ME3 today | evidence |
|---|---|---|
| where the magnification is read | nowhere. `ReadGameZoom`: `#if LE1` `Stats.AimModes`, `#elif LE2` `SFXWeapon.ZoomFOV`, `#else (void)type` | [Sights.cpp](../../../../src/game/Sights.cpp):814-859 |
| what shows instead | `FallbackZoom`: 4.0x for profile 3 (sniper), 1.5x for any other type set to `scope` in the file | [Sights.cpp](../../../../src/game/Sights.cpp):862-864, 891-892 |
| where ME3 keeps it | `SFXWeapon.AimModes` (an `FSFXWeaponAimMode` array; `GetZoomFOV()` reads the current entry; a scoped flag and a scope resource per mode). "Not read yet: left for the ME3 sniper fixture" | [research/le2-vrik.md](../../../../research/le2-vrik.md) "The sights on the sequels" table; research/le2-le3-vr-optics.md |
| the right-stick flick | never steps: it needs `gZoom.game.count > 1`, and the count is 0 | [Sights.cpp](../../../../src/game/Sights.cpp):876 |
| the base FOV the ratio would use | ME1's 70 degrees (`ZoomFromFov`); ME3's own base FOV has not been read either (ME2's defect waiting to recur the day `AimModes` is read) | [Sights.hpp](../../../../src/core/vr/Sights.hpp) `ZoomFromFov` |
| the game's own zoom picture | pinned off in the renderer: the render keeps the headset's window whatever the game's aim FOV (`frac = 1`) | [calcview_hook.cpp](../../../../src/halcyon/le3/calcview_hook.cpp):1024-1031, 1416-1423 |
| the game's own zoomed state | still starts whenever LT is held: the support grip at the shoulder forces LT 255 for every family but the sniper | [SupportGrip.cpp](../../../../src/game/SupportGrip.cpp):277-279 |
| is the state visible to the mod | yes on ME3 (not on ME2): `weapon::Zoomed()` reads `SFXWeapon_NativeBase.bIsZoomed`; the support grip logs it | [Weapon.cpp](../../../../src/game/Weapon.cpp):1006-1017, 90 |
| a scoped shot's spread | ME1's route with ME3's names: `SFXWeapon.GetPlayerAimError(Accuracy)` post-handler, `bIsZoomed` set for one call in a `__finally` | [Sights.cpp](../../../../src/game/Sights.cpp):49-51, 240-273; [Weapon.cpp](../../../../src/game/Weapon.cpp):1083-1096 |
| the scope's picture | the other eye's image enlarged; scope capture returns at `runtime::Ready()` (ME1 only) and the ME3 host serves no `setScopeCapture` | [ScopeCapture.cpp](../../../../src/game/ScopeCapture.cpp):317; ledger G07 |
| the lens part | `HideBone` on every game; `auto` hides `Scope_Lens` on a sniper profile. No ME3 mesh was checked for that bone | [ScopeParts.cpp](../../../../src/game/ScopeParts.cpp):28-30, 216-226 |
| the pull, the assist, ADS | ME1's code and numbers; ME3's eyepiece geometry is generic (no optic profile) | [Sights.cpp](../../../../src/game/Sights.cpp):414-422 |
| R3 (a native magnification step) | kept from the game on both sequels (`keepPowerClick`) | [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):372-377 |

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| WPN-01 | adapter | ME2's branch: post-handlers on `SFXWeapon.GetAdjustedAim` and `BioPawn.GetWeaponStartTraceLocation` | names only; never run with a weapon (ledger G02 "adapted U") | [HandAim.cpp](../../../../src/game/HandAim.cpp):474-486, 637-645 |
| WPN-02 | same | `Actor.Trace` (`kEveryGame`) and `ConvergedShot` | not run | [Sights.cpp](../../../../src/game/Sights.cpp):41, 991-1008 |
| WPN-03 | same | the mod's aim ray; the ring by ME1's `HandReticle` in mode 0 | the ring is asked for on foot only (`OverlayReticleMode`): in the Atlas there is no ring (see the vehicles audit) | [SequelWeaponLayers.cpp](../../../../src/plugin/SequelWeaponLayers.cpp):45-67; [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp):244-246 |
| WPN-04 | adapter | ME3's `ApplyMevrBaseline` pins no head aim after every ini load | names only | CLAUDE.md "ME3's renderer pins ME2's baseline"; `src/halcyon/le3/me2_menu.cpp` |
| WPN-05 | same core, other inputs | `body_calibration::StanceFor(PresentationProfile())` | the calibration is read from `ASI\LE3MEVR.ini`: a player calibrated in ME1 or ME2 has none in ME3 (the owner answered "shared" on 2026-11-18; not applied). The SMG borrows the rifle's stance | [Weapon.hpp](../../../../src/game/Weapon.hpp):90-99; the body audit, BOD-36 |
| WPN-06 | same | `game/SupportGrip` has no game guards; the host merge is ME1's | the zone's size was never checked against an ME3 gun | [SupportGrip.cpp](../../../../src/game/SupportGrip.cpp); ledger G05 "shared U" |
| WPN-07 | same | same code | not run | as WPN-06 |
| WPN-08 | same core, other inputs | same code | needs ME3's own calibration (WPN-05) | as WPN-06 |
| WPN-09 | same core, other inputs | the same flag forces LT 255 | LT on ME3 is the game's tight aim: `SetZoomed` resets the aim-mode index and runs the zoom transition, scope-mod audio and cover-dependent audio. The picture is pinned; the state is on. ME3's own traversal adapter refuses a request while `IsZoomed` (the class of ME2's refused vault) | [SupportGrip.cpp](../../../../src/game/SupportGrip.cpp):277-279, 292-306; research/le2-le3-vr-optics.md; [Le3Traversal.cpp](../../../../src/game/Le3Traversal.cpp):76; [research/le3-traversal-adapter.md](../../../../research/le3-traversal-adapter.md) |
| WPN-10 | same core, other inputs | the same `[Sights]` block and defaults | five families on four profiles (SMG as the rifle); a heavy weapon has none; the sight is chosen by family, never by the installed scope mod | [Weapon.cpp](../../../../src/game/Weapon.cpp):448-461; [Sights.cpp](../../../../src/game/Sights.cpp):1021-1029 |
| WPN-11 | partial | a sight bone by name, else the bones' top, else the typical place | `OpticFor` runs under `MEVR_GAME_LE1` only; no ME3 mesh is measured. Whether a scope mod's mesh is part of the weapon's skeleton (so its bones are seen) is not known | [Sights.cpp](../../../../src/game/Sights.cpp):414-422 |
| WPN-12 | same | host `SightLayers`, unchanged, in the ME3 host | not run | [SequelWeaponLayers.cpp](../../../../src/plugin/SequelWeaponLayers.cpp):12-39 |
| WPN-13 | same | same host and mod code | not run | as WPN-12 |
| WPN-14 | missing | - | needs `opticApplies`, which needs an optic profile; ME3 has none | [Sights.cpp](../../../../src/game/Sights.cpp):419-422, 1058 |
| WPN-15 | same | `assistDotMaxM = 0` from the shared block | - | [Sights.hpp](../../../../src/core/vr/Sights.hpp) |
| WPN-16 | same | the ADS gate in `Current` | not run | [Sights.cpp](../../../../src/game/Sights.cpp):1011-1068 |
| WPN-17 | same | `core/vr/Ads`, same numbers | - | [Ads.hpp](../../../../src/core/vr/Ads.hpp) |
| WPN-18 | same core, other inputs | same code | needs a stock point from ME3's own calibration | as WPN-05 |
| WPN-19 | same | the virtual eyepiece, the other eye's image | not run | ledger G07 |
| WPN-20 | missing | a constant: 4.0x for a sniper rifle | "the held weapon's own" is not read at all on ME3; see the table above | [Sights.cpp](../../../../src/game/Sights.cpp):856-864 |
| WPN-21 | missing | the flick is read under ADS and can never step | ME3's weapons do have an aim-mode array; whether any has two scoped modes is not known (a `mevr props` of a held sniper rifle's `AimModes` would say). Rated missing, not n/a, until then | [Sights.cpp](../../../../src/game/Sights.cpp):876-890 |
| WPN-22 | same core, other inputs | one aim-error call with `bIsZoomed` set, ME3's function of ME1's shape | the game's zoomed state does start for non-sniper guns (WPN-09); its picture is suppressed in the renderer, not at the source. R3 never reaches ME3 | [Sights.cpp](../../../../src/game/Sights.cpp):49-51; [Weapon.cpp](../../../../src/game/Weapon.cpp):101-103, 1083-1096 |
| WPN-23 | same | same assist code and limits | on a generic eyepiece place | [Sights.cpp](../../../../src/game/Sights.cpp) `PullLimit`, `StepAssist` |
| WPN-24 | same | same | not run | as WPN-23 |
| WPN-25 | same | `HideBone` / `UnHideBone` / `IsBoneHidden` are `kEveryGame` | which ME3 sniper meshes have `Scope_Lens` is unchecked; the material log is ME1's | [ScopeParts.cpp](../../../../src/game/ScopeParts.cpp):28-31 |
| WPN-26 | adapter, not seen yet | Since 0601ef40 (lane AU3): ME1's `game/ScopeCapture` on every game (handles `kEveryGame`, `Up()`), the ME3 host serves `setScopeCapture`; seen on ME2 (3.5x and 7x from the capture) | No post-process chain (`engine::adapter::kCapturePostProcess`: ME3's galaxy captures came out black with one); `plugin/Le3SceneCapture` binds a capture's final pass. Not seen in ME3 (2026-11-18): the combat fixture carries no sniper rifle; the game's `GiveWeapon` does nothing, `mevr give <class>` (Engine.Pawn.CreateInventory) works only for a loaded class and ME3 loads `SFXWeapon_SniperRifle_Mantis.pcc` for the loadout only; the Avenger made a 3.5x scope (`[Sights] assaultRifle = scope 3.5`) never reached ADS under the replayed aim (host sight kind none). Lane AU5 (2026-11-18): the fixture carries a Black Widow now (`me3-combat.ps1 -Loadout '2 1 all'`: `mevr loadout`, the loadout screen's own pick); the sight sits on its `Scope_Rear` bone at 3.5x and 7x, but ADS is never reached: arm IK stays in its third-person anchor in the combat pose (the view 53 units from Shepard's head bone, past the 45-unit entry; BOD-22), so the drawn gun is 0.5 m from the controller (`mod.armIk.hands.right.fromGripM` 0.47) and the eye 1.5 m behind the muzzle. The capture waits for the eye at Shepard's head (research/38 "The sequels' constant eye") | [ScopeCapture.cpp](../../../../src/game/ScopeCapture.cpp):317; [Le3SceneCapture.cpp](../../../../src/plugin/Le3SceneCapture.cpp) |
| WPN-27 | same | duplex; the game-art search is compiled for ME1 only | ME3's own scope reticle art (the mods', the Javelin's) is not used | [Sights.cpp](../../../../src/game/Sights.cpp):910-911 |
| WPN-28 | same | the ring hides 0.3 s after an eye looked through the sight | - | [SequelWeaponLayers.cpp](../../../../src/plugin/SequelWeaponLayers.cpp):40-44, 60 |
| WPN-29 | adapter | ME2's branch: `SFXWeapon.GetWeaponRecoil`, shots from ViewLevel's `SetWeaponRecoil` handler | names only; not run (ledger G09 "adapted U") | [GunKick.cpp](../../../../src/game/GunKick.cpp):86-95 |
| WPN-30 | adapter | the same `PlayerTick` hold, ME2's yaw-kick removal | the allowed camera modes are five exact ME1 class names; ME3 adds `Melee`, `Roll`, `HitReaction`, `EnterCover`, `Atlas` cameras, in which the hold stands down (the body audit's first finding is the same list). Whether ME3 queues a yaw kick is `[verify]` in the code itself | [ViewLevel.cpp](../../../../src/game/ViewLevel.cpp):427-445; [ViewLevel.cpp (core)](../../../../src/core/vr/ViewLevel.cpp):72-79 |
| WPN-31 | same | the same two handlers; ME3 has the names (ledger G10) | never measured on a sequel | [ViewLevel.cpp (core)](../../../../src/core/vr/ViewLevel.cpp):127-137 |
| WPN-32 | n/a | thermal clips. The counterpart, the reload by hand, compiles for ME3 and is off (`[Reload] enabled = 0`, the owner 2026-11-16) | - | [Reload.cpp](../../../../src/game/Reload.cpp):66-82, 420-428 |
| WPN-33 | n/a | as WPN-32 | - | [Vent.cpp](../../../../src/game/Vent.cpp):184 |
| WPN-34 | missing | `hud_pieces::Tick` logs its stand-down and returns; the ME3 host serves no `setHudPieces` | ME2 has this (`Layout::Me2`); ME3 chose to wait for an adapter rather than fall back to ME1's placement | [HudPieces.cpp](../../../../src/game/HudPieces.cpp):62-68, 177-185; ledger G28 |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| The sniper rifle shows 8.0x because ME2's `ZoomFOV` is put against ME1's 70 degree base | different: ME3 reads nothing and shows the 4.0x constant. The base-FOV defect is latent in `ZoomFromFov` for the day `AimModes` is read |
| The picture is an enlarged eye image; no scope capture | same defect. ME3's host has its own capture plumbing, so the gap is smaller |
| LT from the support grip starts the game's zoom; picture pinned, cause not | same defect, same pin (one commit, both renderers) |
| `weapon::Zoomed()` answers nothing | fine on ME3: it reads `bIsZoomed` |
| The zoom blocked the vault (lane C4) | same class, unfixed and unrun: ME3's traversal admission refuses while zoomed. Dormant while ME3's traversal gestures are off; whether the game's own roll and storm on A are refused under a forced LT is not known |
| No optic profiles: no dot window, generic lens sizes | same defect |
| Calibration per game file | same defect |
| A heavy weapon has no sight | same defect |
| WPN-21 n/a (one zoom step) | different: on ME3 it is missing, the game has an aim-mode array |
| WPN-34 unknown on ME2 | different: missing on ME3 by an explicit stand-down |
| The aim-error flag left set if the script faulted | fine: the `__finally` is shared code |

# The shouldered gun's accuracy without the game's zoom, on ME3

The ME2 audit's proposal: stop sending LT from the shouldered grip and open `OnAimError`'s gate for "support grip held and
at the shoulder", so the shot gets the zoomed spread through the one-call route and the weapon never zooms.

- **The same problem exists.** `options.supportAims` is computed without a game guard
  ([SupportGrip.cpp](../../../../src/game/SupportGrip.cpp):277-279) and LT on ME3 enters the game's zoom.
- **The same route exists.** `OnAimError` is registered on ME3 (`SFXWeapon.GetPlayerAimError(Accuracy)`), the range is
  picked by `bIsZoomed` in `SFXWeapon.GetWeaponAimErrorRange` as on ME2 (research/le2-vrik.md's table), and
  `weapon::ZoomedAimError` runs on ME3. The handler's gate is the same `gScopeUp && gAimedFire`
  ([Sights.cpp](../../../../src/game/Sights.cpp):243).
- **What differs on ME3.** `weapon::Zoomed()` works, so the closing check can read the flag directly instead of inferring
  it. `SetZoomed` on ME3 also resets the aim-mode index and plays scope-mod and cover audio; not zooming loses those. What
  else ME3's zoomed state changes (recoil, movement speed, a class's time dilation while scoped) is untraced, as on ME2.
- **It cannot be checked yet.** The proposal's closing check needs a held weapon; ME3 has no fixture with one.

# Reinvented

Nothing in this category is a second implementation on ME3. Two places are one step from becoming one:

| risk | what exists | what to do instead |
|---|---|---|
| ME3's zoom read will be a third `#if` branch in `ReadGameZoom` | ME1 reads an array of modes, ME2 one field; ME3's `SFXWeapon.AimModes` has ME1's shape (`ZoomFOV`, a scoped flag) on the weapon instead of the stats object | make ME1's loop the shared one with the array's owner and field names as the adapter ([Sights.cpp](../../../../src/game/Sights.cpp):57-70), and read each game's base FOV beside it |
| ME3's scope capture | `plugin/Le3SceneCapture` (ME3's own RVAs, built for the galaxy map) beside ME1's `RenderLevers` capture hooks and `game/ScopeCapture` | serve `setScopeCapture` from the existing ME3 capture path; do not write an ME3 scope module |

# Rediscovered problems

| problem on ME3 | where ME1 or ME2 already had it | cost |
|---|---|---|
| Aiming magnified the whole view | ME1's "Nothing zooms the player's view" (research/18, research/21); found on ME2 in the owner's headset on 2026-11-17 | fixed for both sequels in one commit (2222cdee); ME3 was spared a second discovery only because ME2 ran first |
| LT is a zoom, not "aimed fire" | the ME2 audit's three rediscoveries | carried to ME3 unchanged: the cause is still shared code with no per-game decision |
| A magnification taken from a constant written for another game | ME2's 8.0x from ME1's 70 degrees | ME3's 4.0x is ME1's fallback for "none read", shown as if it were the rifle's |

# Forced by the game

| difference | evidence |
|---|---|
| Zoom modes live on the weapon (`SFXWeapon.AimModes`), and a mod can change them | research/le2-le3-vr-optics.md "What the sequel packages establish" |
| No heat, so no vent and no rack | thermal clips; ledger G41 |
| Five weapon families, heavy weapons, weapon mods | [Weapon.cpp](../../../../src/game/Weapon.cpp):73-84 |
| A yaw recoil queue of the same shape as ME2's | [ViewLevel.cpp](../../../../src/game/ViewLevel.cpp):427-432 |
| More camera modes than ME1's five | the body audit, `src/halcyon/le3/engine_probe.cpp` |

# Not forced

| difference | what it is |
|---|---|
| 4.0x on every sniper rifle, no step | the `AimModes` read was deferred to a fixture that does not exist; the names are in the SDK headers and need no fixture to write, only to verify |
| No scope capture | `Ready()` instead of `Up()`, and one host service |
| No optic profiles, no dot window | nobody measured ME3's meshes (possible offline from the packages, as the holsters' socket survey was) |
| LT from the support grip | `supportAims` carried over; never asked per game |
| The weapon panel absent | an explicit stand-down pending an ME3 crop |
| A scope mod is ignored | the profile is chosen by family only |
| Calibration per game file | the owner's answer of 2026-11-18 not yet applied |

# What to do

1. **Get a weapon in the hand on ME3.** Everything below is unverifiable until then. The route is
   research/le3-combat-fixtures.md's (the Citadel range, or a rewritten save). Check: `weapon:` names a held gun in the log.
2. **Decide the scope's magnification for the trilogy** (owner question, already open from the ME2 audit: ME1's 3.5x feel
   or each game's own). Then read ME3's `SFXWeapon.AimModes` and each game's real base FOV in one shared loop
   ([Sights.cpp](../../../../src/game/Sights.cpp):814-859) with ReflectCheck rows. Check: the log's `own aim modes` line on
   a held sniper rifle, with and without a scope mod; the flick steps if a second scoped mode exists.
3. **LT from the shouldered grip**: with ME2's item 2 and proposal, one decision for both sequels. Before building, trace
   on ME3 what the zoomed state changes (`mevr calltrace 10 setzoomed tightaim` with LT held; readers of `bIsZoomed`).
   Check: `support grip: aimed fire on` with `bIsZoomed` off.
4. **Scope capture on ME3** from `plugin/Le3SceneCapture`: serve `setScopeCapture`, `Ready()` to `Up()` in
   [ScopeCapture.cpp](../../../../src/game/ScopeCapture.cpp):317. Check: snapshot `sights.pictureSource` reads `capture`.
5. **Measure ME3's sights offline** (the base rifles, the scope mods' meshes, the sniper rifles' eyepieces) and make
   `OpticFor` run on every game ([Sights.cpp](../../../../src/game/Sights.cpp):419). Check: `sights: the ... sight sits on
   its '<bone>' bone` and `[MEVR-SIGHT] first window` where a housing is solid.
6. **The weapon panel**: serve `setHudPieces` in the ME3 host with an ME3 crop (ledger G28). Check: snapshot
   `mod.hudPieces` with the gun lowered and raised.
7. Owner questions, no design proposed here: what a heavy weapon shows; whether an installed scope mod should change the
   sight a rifle gets.

# Citations

- [The catalog file](../../me1/weapons-and-sights.md); [the ME2 audit](../me2/weapons-and-sights.md);
  [the parity ledger](../../../trilogy-parity-ledger.md) rows G02 to G10, G28, G41, R31. Where this audit disagrees with
  its LE3 cells: G06 "shared U" is right for the code but does not say that the scope shows a constant; G07 "adapted U:
  same" hides that ME3 already has capture plumbing ME2 lacks; G05 "shared U" says nothing about what LT means on ME3.
- [research/le2-vrik.md](../../../../research/le2-vrik.md), [research/le2-le3-vr-optics.md](../../../../research/le2-le3-vr-optics.md),
  [research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md),
  [research/le3-traversal-adapter.md](../../../../research/le3-traversal-adapter.md),
  [research/21](../../../../research/21-sights-and-scopes.md), [research/39](../../../../research/39-scope-capture.md).
- Sibling ME3 audits: [body and IK](body-and-ik.md), [holsters, throwables, melee](holsters-throwables-melee.md),
  [gestures and input](gestures-and-input.md), [vehicles](vehicles.md).
- From reading alone, not from a repository note: that ME3 binds LT to its tight aim. The holsters' deny-list names
  `TightAim` on both sequels and the optics note describes `SetZoomed`; the binding line itself was not traced.
