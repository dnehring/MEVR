---
type: Audit
title: ME3 against ME1 - summary of the thirteen category audits
description: The 458 ME1 contracts rated on ME3 in one table beside ME2's, which ME2 findings recur on ME3, ME3's own losses, the findings ordered by what a player notices, and the questions only the owner can answer.
tags: [le3, audit, parity, trilogy, summary]
status: stable
timestamp: 2026-11-18T10:30:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:30:00Z }
game: LE3
against: ../../me1/index.md
commit: 583591c6
---

# Verdict

- ME3 looks like ME2: of 458 contracts, 244 run ME1's rule (same, same core with other inputs, or an adapter), three are
  reimplemented, 93 are missing and 66 are n/a. Most of ME2's findings recur, because the two sequels share the host code
  that carries them. A fix made in that shared code closes both.
- **Every "same" on ME3 is weaker than on ME2.** ME3 has only been run on the Normandy with a non-combat pawn. Nothing in
  the weapon, input, holster, melee, power or vehicle categories has been seen on ME3 with a gun in the hand. "Same" there
  means the same code compiles and its names resolve.
- Everything was read from code and notes at main 583591c6. No game was run. Six cited findings were re-read by the lead
  (marked "re-read"); the others rest on the auditing agents' reading, and eight of the thirteen files were written by
  their sub-agents without a line-by-line recheck.
- ME3 has losses of its own that ME2 does not: no HUD pieces (and a wrist tap that misfires behind them), a flat panel
  larger and further than either other game's, a 3D screen gated to two game modes, a melee with its own rule set, and a
  gun that is never put away.

# Ratings

| audit | contracts | same | same core, other inputs | adapter | reinvented | partial | missing | n/a | unknown |
|---|---|---|---|---|---|---|---|---|---|
| [Gestures and input](gestures-and-input.md) | 44 | 11 | 11 | 6 | 1 | 4 | 3 | 5 | 3 |
| [Weapons and sights](weapons-and-sights.md) | 34 | 16 | 6 | 4 | 0 | 1 | 5 | 2 | 0 |
| [Holsters, grenades, melee](holsters-throwables-melee.md) | 38 | 10 | 8 | 4 | 0 | 5 | 0 | 10 | 1 |
| [Body and IK](body-and-ik.md) | 40 | 20 | 3 | 3 | 1 | 3 | 7 | 1 | 2 |
| [Powers and squad](powers-and-squad.md) | 39 | 14 | 9 | 7 | 0 | 5 | 1 | 3 | 0 |
| [Vehicles](vehicles.md) | 36 | 0 | 0 | 0 | 0 | 1 | 3 | 30 | 2 |
| [Menus and game screens](menus-and-game-screens.md) | 40 | 10 | 7 | 6 | 1 | 8 | 5 | 2 | 1 |
| [Presentation modes](presentation-modes.md) | 30 | 4 | 7 | 6 | 0 | 2 | 7 | 1 | 3 |
| [Rendering and stereo](rendering-and-stereo.md) | 36 | 7 | 8 | 7 | 0 | 3 | 9 | 2 | 0 |
| [Settings and defaults](settings-and-defaults.md) | 30 | 12 | 7 | 4 | 0 | 3 | 2 | 2 | 0 |
| [Comfort and movement](comfort-and-movement.md) | 33 | 12 | 8 | 3 | 0 | 1 | 6 | 1 | 2 |
| [Conversations and scenes](conversations-and-scenes.md) | 37 | 0 | 1 | 3 | 0 | 1 | 31 | 1 | 0 |
| [Audio](audio.md) | 21 | 0 | 0 | 0 | 0 | 1 | 14 | 6 | 0 |
| **ME3, all** | **458** | **116** | **75** | **53** | **3** | **38** | **93** | **66** | **14** |
| ME2, all ([its summary](../me2/summary.md)) | 458 | 132 | 62 | 56 | 1 | 34 | 124 | 34 | 15 |

ME3's missing count is lower than ME2's mostly because the vehicle rows are n/a there (ME3 has no Mako; ME2's Hammerhead
is deferred). 31 of ME3's 93 missing are first-person conversations, deferred by the owner.

One inconsistency between files: the input audit rates the melee stroke (INP-28) `reinvented`, because ME3 runs
`Rules::Staged` with its own admission list; the holsters audit rates the same rules (HOL-31 to 36) "same core, other
inputs". Both describe the same code.

# ME2's findings on ME3

The numbers are ME2's summary's.

| ME2 finding | on ME3 |
|---|---|
| 1. Unknown game screens fall to Halcyon's flat panel | recurs, worse: 2.2 m away and 3.2 m wide (re-read, le3 `me2_xr.cpp:93-94`), against ME1's 1.4 m / 2.35 m and ME2's 1.6 m / 2.2 m. The squad record and most ME3-only screens show there |
| 2. The game's wheels never come to the hand | recurs (`setWheelPanel` is LE1-only) |
| 3. Sniper zoom wrong | different: a fixed 4.0x. `ReadGameZoom` has an ME1 and an ME2 branch and none for ME3 (re-read), so the fallback answers and the stick flick can never step. ME1 3.5x then 7x, ME2 8.0x, ME3 4.0x: none of the three was chosen |
| 4. The support grip's LT is the game's zoom | recurs. [ME2's proposal](../me2/weapons-and-sights.md) carries over: the same one-call route exists on `SFXWeapon.GetPlayerAimError` |
| 5. A left-grip squeeze at the chest or receiver becomes a squad order | **fixed 2026-11-18 (lane AU6, c8f6987e)**: held, as on ME2 |
| 6. No recenter from the controllers | recurs |
| 7. Powers missing from the hand wheel | fine: ME3's roster comes from the game. But every aimed power is `Aim::Place` (re-read, `PowerWheel.cpp:323-324`), so ME1's "needs a locked enemy" refusal never happens |
| 8. Body scale cannot settle | recurs, twice over: `player::Crouched` is ME1's handle, and the reference needs "holstered", which ME3 never is. **Both addressed 2026-11-18 (lane AU2): the crouch handle is every game's, and with `le3PutAway = 1` a gun let go is away; not seen settling in the game yet** |
| 9. Third-person walk, slow draws, the eye moving on a draw | **mostly fixed 2026-11-18 (lanes AU5, AU6)**: the constant eye through the shared seam, the explore eye at Shepard's face (dadb7461, the owner's answer), the speed from the push (half a push 146 u/s) and the draw rate (holstered 0.16 s) (c8f6987e); the explore start is still the game's (owner question) |
| 10. Snap turn may stand down | recurs, with a known trigger: ME3 leaves the five listed camera modes for about 0.5 s on a roll, melee, hit reaction or cover entry |
| 11. No start-up or entry hold | recurs (`plugin/PresentationHold` is in LE1's source list only, re-read) |
| 12. No owner for the render size | recurs: a fixed 3072x2216 for every headset; **fixed in code, lane AU4 (b1fb0c50): the first VR start sizes it from the headset (multiples of 8)** |
| 13. Guards not installed | partly: the motion-blur guard is installed on ME3; the culling pad (both functions found, constants left 0) and the kept locks are not; **both fixed in code, lane AU4 (b1fb0c50)** |
| 14. Calibration per game file | recurs; the owner answered "shared unless the VRIK differs" on 2026-11-18, not applied |
| 15. Placement bypasses the dispatcher, no squad answer | recurs |

Fine on ME3 where ME2 was not: the pointer works on the flat panel; `ConvoFirstPerson` is forced off at its setter;
`MenuPause` translates game modes; `HandReticle`'s raw mode-1 branch cannot fire; `weapon::Zoomed()` answers; no float
swap chain.

# ME3's own losses

| # | finding | audit | forced by the game? |
|---|---|---|---|
| 1 | No wrist or gun HUD pieces: health and ammo stay on the HUD panel 1.5 m ahead. Behind that, the omni-tool's wrist tap never learns a weapon is drawn, so it fires where ME1 refuses it, and its target sits at the controller, not on the drawn forearm | [menus](menus-and-game-screens.md) | no |
| 2 | The 3D cinema screen is admitted only in game modes 7 and 8 (re-read, le3 `me2_xr.cpp:1761`); a cinematic in any other mode goes flat. How many scenes that affects is unmeasured | [presentation](presentation-modes.md) | no: upstream removed its mode list, our gate put one back |
| 3 | The menu's Comfort page offers Flat only and says the 3D screen is "not available yet", while the renderer shows it by default; the menu also refuses writes for voice, the microphone and the wrist screens, which work | [presentation](presentation-modes.md), [settings](settings-and-defaults.md) | no |
| 4 | **Fixed 2026-11-18 (lane AU2): `le3PutAway = 1` by default, run on the combat fixture.** Was: a gun let go is never put away (`[Holsters] le3PutAway = 0`, re-read: "off until a combat fixture checks it"). "The trigger never draws", "the fist punches" and "only holsters draw" have no weapon-away state to act in | [holsters](holsters-throwables-melee.md), [input](gestures-and-input.md) | partly: ME3 keeps a gun out on a mission |
| 5 | Melee is another rule set: the striking hand must have every control released, so the hand holding the gun never strikes and there is no fist; ME1's speed and travel keys are ignored | [input](gestures-and-input.md), [holsters](holsters-throwables-melee.md) | partly: ME3 has light and heavy melee; the owner approved "stroke melee as on ME1 and ME2" |
| 6 | **Fixed 2026-11-18 (lane AU2, the roll seen on the combat fixture: first person and level, now the floor lock, view level, the snap turn and no shakes through ME3's action cameras; melee, hit and cover-entry cameras not seen yet).** Was: the view probably goes third person for about 0.5 s on a roll, melee, hit reaction, cover entry or in the Atlas: first person and the floor lock are tied to five camera-mode names | [body](body-and-ik.md) | no |
| 7 | An equipped grenade is thrown by the left-hand push | [powers](powers-and-squad.md) | grenades are powers on ME3; no owner answer covers the gesture |
| 8 | A standing squad order never ends as "calm" on a mission: `scene.combat` means "a gun is held" | [powers](powers-and-squad.md) | no; **fixed in code, lane AU4 (690b632f): the game's own combat flag** |
| 9 | In the Atlas the player may have neither crosshair nor ring | [vehicles](vehicles.md) | no |
| 10 | The per-level engine settings test `mode > 4` (re-read, `SystemSettings.cpp:183`) while ME3's gameplay modes run to 6 | [rendering](rendering-and-stereo.md) | no |
| 11 | A cover already taken is never left; where ME3's admission fails, the game's own sprint runs | [movement](comfort-and-movement.md) | no |
| 12 | Scope weapon mods are ignored and a heavy weapon has no sight (no catalog row for either) | [weapons](weapons-and-sights.md) | no |

A likely defect, not re-read: the melee's 75-degree enemy cone uses a body yaw ME3's feed may never set
(`Melee.cpp:70, 817, 1139`).

# The mechanisms, across both sequels

[ME2's summary](../me2/summary.md) names five. ME3 shows all five again and two more.

| mechanism | on ME3 |
|---|---|
| 1. A gate written in ME1's terms | `ViewLevel.cpp:71`, `CameraShake.cpp:288`, `SystemSettings.cpp:183`, the screen's raw 7/8, `player::Crouched`, `squad_ack`'s `Ready()` |
| 2. Half a feature ported | HUD pieces (neither half), and reversed for the cinema screen: the renderer half is in, the menu half is stale |
| 3. The same signal means something else | LT zooms, X reloads, B melees, A sprints and rolls, R3 is navigation and the map. ME3's bindings in the input audit are from the agent's knowledge of the game, not a repository note |
| 4. A claimant missing from the chain | the left grip, as on ME2 |
| 5. ME1's constant taken as the game's | the 4.0x fallback, ME1's settings block in `LE3MEVR.ini`, ME1's "holstered" and "combat" signals |
| 6. Untested defaults kept off | `le3PutAway`, the heavy melee gesture: a feature exists and ships off "until a fixture checks it", and the rules that depend on it are then inert |
| 7. The baseline is a hand-kept list | a third copy of the pin list; `MoveFollowsHead` and the flat panel's size are not on it |

# Corrections found in the catalog

Not applied; to check against ME1's code.

- BOD-12 lists "a menu" among the floor lock's gates; since 2026-11-15 the shared main menu no longer switches it off.
- PWR-31 and PWR-32 say standing orders end when calm, "without combat"; ME1's "combat" there is the weapon being drawn.
- `me1/body-and-ik.md` has no row for camera shakes or the hidden head.
- SET-21 omits the reapply on returning from a non-gameplay mode.
- The settings catalog says DynamicShadows is LE1-only; `tools/vr-game-settings.ps1` writes it for every game.
- The conversations and audio code maps say the modules "stand down off LE1"; on LE3 they are compiled out of `Mod.cpp`.

`CLAUDE.md` is stale in two places: it says ME2 and ME3 keep dynamic shadows on (the constant is false for every game),
and that ME3's conversations are on the native flat screen (the 3D screen shows by default).

# Questions for the owner

1. Is the 3D cinema screen meant to be ME3's default today? The code says yes; the menu and `CLAUDE.md` say no.
   **Answered 2026-11-18:** "Likely yes if it's default for the other two games": yes; the menu is to agree.
2. Does 3072x2216 stand for every headset, or was it a stand-in until the headset rule?
   **Answered 2026-11-18:** "Follows headset like me1."
3. Should a gun let go be put away on ME3 (`le3PutAway`)? Three ME1 rules depend on it.
   **Answered 2026-11-18:** "Obviously yes, that should be default."
4. Is ME3's melee rule set (no strike from the gun hand, no fist) what "stroke melee as on ME1 and ME2" meant?
   **Answered 2026-11-18:** "Me3 melee will require letting go of the guns and doing a fist to summon the Omni blade for
   strikes."
5. Should the left-hand push throw an equipped grenade?
   **Answered 2026-11-18:** yes.
6. Should the rail's Squad entry be greyed on ME3 until its figure is ported, as Holster is?
   **Answered 2026-11-18:** "That's fine for now."
7. During a roll or hit reaction with snap turn chosen: should the mod keep the turn, or should nobody turn?
   **2026-11-18:** the owner doesn't yet know how a roll should look in VR; a proposal went back (first person, the view
   level and sliding with the body, the turn the player's). The owner: "For roll we'll do your recommendation."
8. Which magnification should a sniper rifle show across the trilogy (ME1 3.5x and 7x, ME2 8.0x, ME3 4.0x today)?
   **Answered 2026-11-18:** "sniper zoom is unified": one rule, read as ME1's.

9. Should ME3's explore eye sit at Shepard's face as ME1's and ME2's do (research/38 "The sequels' constant eye")?
   **Answered 2026-11-18:** "Me3 eyes should match behavior from other games." Built (lane AU6, dadb7461): the renderer's standing
   rows (-35, 0, 65), forward 34 units of the head bone exploring and 26 in combat, arm IK first person in a fight; the scope
   capture then seen on ME3 at 3.5x and 7x.

ME2's questions 1 (LT from the support grip) and 4 (one calibration) apply to ME3 unchanged. Question 4 is built for the
three games (2026-11-18, lane AU7, 88e9a775; research/25 section 17.1): ME3's host and mod read the same shared file, not
run on ME3.

# What to do

Parity work only. Each audit's own list has the files and the closing check.

1. Get a combat fixture for ME3 first (research/le3-combat-fixtures.md). **Done 2026-11-18 (lane AU2):** `open BioP_SPDish`
   from Save 31, `tools/le2-halcyon-run/me3-combat.ps1`; it found ME3's lock, squad context and VR cover blind (no
   `BioPawn.IsHostile` on ME3; fixed: `engine::adapter::kIsHostileFunction`).
2. Do ME2's step 1 (the sweep of gates written in ME1's terms) for both sequels at once; ME3 adds `SystemSettings.cpp:183`,
   `CameraShake.cpp:288` and the screen's 7/8.
3. Fix in the shared sequel host what both games lack: the wheel panel, the presentation hold, the world-UI tag list, the
   render-size owner, the culling pad and kept locks.
4. Bring HUD pieces to ME3 and feed the wrist tap its weapon gate and body target (ME3's loss 1).
5. Make the menu agree with the renderer on ME3 (loss 3): one capability set, read by both.
6. Read ME3's `SFXWeapon.AimModes` for the zoom (ME2 finding 3), after question 8.
7. Write ME3's pad contract from the game's own bindings, so mechanism 3 rests on a note and not on memory.

# Citations

- [How to catalog a system and audit a port against it](../../how-to-catalog.md)
- [ME1's systems](../../me1/index.md), [ME2's summary](../me2/summary.md)
- [The parity ledger](../../../trilogy-parity-ledger.md), [the trilogy plan](../../../trilogy-plan.md)
