---
type: Audit
title: ME2 against ME1 - summary of the thirteen category audits
description: The 458 ME1 contracts rated on ME2 in one table, the five mechanisms by which behaviour was lost in the port, the findings ordered by what a player notices, the catalog corrections the audits found, and the questions only the owner can answer.
tags: [le2, audit, parity, trilogy, summary]
status: stable
timestamp: 2026-11-18T08:30:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T08:30:00Z }
game: LE2
against: ../../me1/index.md
commit: 940a75c6
---

# Verdict

- ME2 mostly **uses** the shared code: of 458 contracts, 250 run ME1's rule (same, same core with other inputs, or an
  adapter) and **one** is a true reimplementation. The waste the owner suspected is less "the wheel rebuilt" than "the
  wheel fitted and never turned": 124 contracts are missing, and 62 run the shared rule with inputs nobody translated.
- The three complaints each have a named cause. Menus fall to Halcyon's flat panel for every screen the tag list does not
  know ([menus](menus-and-game-screens.md)). The sniper's zoom is computed against ME1's base FOV, and the support grip's
  LT is ME2's own zoom ([weapons](weapons-and-sights.md)). A left-grip squeeze falls through to a squad order because
  ME1's first claimants do not exist on ME2 ([input](gestures-and-input.md)).
- Everything here was read from code and notes at main 940a75c6. No game was run. Five cited findings were re-read by the
  lead (marked "re-read" below); the others rest on the auditing agents' reading.
- [The parity ledger](../../../trilogy-parity-ledger.md) rates by code and so reads better than this: several of its
  "shared V" cells hold a module whose gate never opens on ME2 (see "Where the ledger is wrong").

# Ratings

| audit | contracts | same | same core, other inputs | adapter | reinvented | partial | missing | n/a | unknown |
|---|---|---|---|---|---|---|---|---|---|
| [Gestures and input](gestures-and-input.md) | 44 | 13 | 11 | 5 | 0 | 2 | 6 | 3 | 4 |
| [Weapons and sights](weapons-and-sights.md) | 34 | 16 | 7 | 4 | 0 | 1 | 2 | 3 | 1 |
| [Holsters, grenades, melee](holsters-throwables-melee.md) | 38 | 13 | 4 | 8 | 0 | 2 | 0 | 10 | 1 |
| [Body and IK](body-and-ik.md) | 40 | 21 | 3 | 3 | 0 | 3 | 8 | 1 | 1 |
| [Powers and squad](powers-and-squad.md) | 39 | 15 | 6 | 6 | 0 | 7 | 2 | 3 | 0 |
| [Vehicles](vehicles.md) | 36 | 0 | 0 | 0 | 0 | 1 | 34 | 0 | 1 |
| [Menus and game screens](menus-and-game-screens.md) | 40 | 12 | 8 | 7 | 1 | 6 | 5 | 1 | 0 |
| [Presentation modes](presentation-modes.md) | 30 | 5 | 6 | 6 | 0 | 2 | 7 | 1 | 3 |
| [Rendering and stereo](rendering-and-stereo.md) | 36 | 7 | 7 | 6 | 0 | 3 | 10 | 2 | 1 |
| [Settings and defaults](settings-and-defaults.md) | 30 | 13 | 6 | 3 | 0 | 4 | 2 | 2 | 0 |
| [Comfort and movement](comfort-and-movement.md) | 33 | 15 | 3 | 5 | 0 | 1 | 5 | 1 | 3 |
| [Conversations and scenes](conversations-and-scenes.md) | 37 | 0 | 1 | 3 | 0 | 1 | 31 | 1 | 0 |
| [Audio](audio.md) | 21 | 2 | 0 | 0 | 0 | 1 | 12 | 6 | 0 |
| **all** | **458** | **132** | **62** | **56** | **1** | **34** | **124** | **34** | **15** |

Of the 124 missing, 65 are two deferred systems (the Hammerhead as the Mako, first-person conversations), by the owner's
decision. The other 59 are spread over every category, and for most of them nothing records a decision.

# The five mechanisms

How behaviour was lost, each seen in several audits. A port lane should check for all five before calling a system done.

| mechanism | what happens | where it was seen |
|---|---|---|
| 1. A gate written in ME1's terms | A shared module compiles for ME2 but tests ME1's game-mode number, camera-mode class names, or a field declared without `kEveryGame`. The gate is silently never (or wrongly) true | `HandReticle.cpp:142` and `MenuPause.cpp:187` (mode 1 is ME1's Mako, ME2's power wheel); `core/vr/ViewLevel.cpp:71` (five ME1 camera-mode names gate the comfort turn); `Player.cpp:41` `gCrouched` (body scale takes "crouched" every tick, re-read); `SquadAck.cpp:142` and `ScopeCapture.cpp:317` gate on `runtime::Ready()` (re-read) |
| 2. Half a feature ported | The mod half publishes and the host half is not in ME2's host (or the reverse), so nothing shows | `setWheelPanel` is LE1-only, so the game's wheels never reach the hand; `plugin/PresentationHold` is not in the LE2 host; scope capture's host source; the rail's Equipment tab is drawn and does nothing |
| 3. The same signal means something else | ME1's pad mapping runs unchanged, but ME2 binds the button to another action | LT from the support grip is ME2's zoom; X is reload; R3 is the area map; B leaves cover and the level. Three "never" rules shipped broken and were fixed 2026-11-17 |
| 4. A claimant missing from the chain | An arbitration order assumes every claimant exists; where one does not, the input falls to the next | A left-grip squeeze at the chest or over the receiver (ME1: grenade slot, vent) becomes a squad follow on ME2 |
| 5. ME1's constant taken as the game's | A number measured on ME1 is used as if it were the engine's | Sniper zoom is `ZoomFOV` against ME1's 70-degree base FOV: 8.0x for 3.5x (re-read); ME1's optic profiles and view offsets; ME1's whole settings block written into `LE2MEVR.ini`; ME2's 26-row power table drops Singularity, Shockwave, Charge and Combat Drone (re-read) |

A sixth, smaller one: Halcyon's ME2 tree has behaviours of its own that the baseline does not pin off
(`ConvoFirstPerson`, `VehicleHeadMode = 3`, recenter on a double tap of Back). ME2 then does things ME1 never did
without any of our code asking.

# Findings by what a player notices

| # | finding | audit | forced by the game? |
|---|---|---|---|
| 1 | Shop, research, lockers, squad selection and both minigames show on Halcyon's flat panel (1.6 m away, 2.2 m wide, no pointer) instead of in the room or on the wrist | [menus](menus-and-game-screens.md) | no: the tag list in `core/vr/WorldUi.cpp` only knows the pause menu's pages |
| 2 | The game's weapon and power wheels stay on the HUD panel 1.5 m ahead, never at the hand | [menus](menus-and-game-screens.md) | no |
| 3 | The sniper scope shows 8.0x from an enlarged eye image; no zoomed render, no second step | [weapons](weapons-and-sights.md) | partly: ME2 has one `ZoomFOV`, but the base FOV and the capture are ours |
| 4 | The support grip at the shoulder makes the game enter its own zoom | [weapons](weapons-and-sights.md) | the binding is ME2's; sending LT is ours |
| 5 | **Fixed 2026-11-18 (lane AU6, c8f6987e): the zones are held, a squeeze there does nothing (no buzz).** Was: a left-grip squeeze where ME1 has the grenade slot or the vent gives a squad order | [input](gestures-and-input.md) | no |
| 6 | No recenter from the controllers (ME1: double-click the right stick) | [input](gestures-and-input.md) | no |
| 7 | Adept, Vanguard and Engineer powers missing from the hand wheel | [powers](powers-and-squad.md) | no |
| 8 | **Addressed 2026-11-18 (lane AU2): the crouch handle is every game's (it was ME1's only); not seen settling in the game yet.** Was: the body probably stays at scale 1 whatever the player's height | [body](body-and-ik.md) | no |
| 9 | **Fixed 2026-11-18 (lanes AU3, AU5, AU6): the start (AccelRate), the constant eye, the speed from the push (half a push 167 u/s), the stop at once exploring, the draw rate (0.16 s); AU6 c8f6987e.** Was: walking is the third-person walk (ramps, start and stop steps); draws take 0.62 s; the eye point moves on a draw | [body](body-and-ik.md), [movement](comfort-and-movement.md) | no |
| 10 | Snap turn may stand down silently | [movement](comfort-and-movement.md) | no |
| 11 | No hold at start-up or on entering VR: possible flashes of the world | [presentation](presentation-modes.md) | no |
| 12 | Every headset renders 4096x2304; the menu's Display group is greyed | [rendering](rendering-and-stereo.md), [settings](settings-and-defaults.md) | no; **fixed in code, lane AU4 (b1fb0c50): the first VR start sizes it from the headset, at least 16:9; Render resolution and Upscale un-greyed** |
| 13 | No motion-blur guard, no kept locks at quit, no body culling pad | [rendering](rendering-and-stereo.md) | no (ME3's host has the first); **kept locks and the culling pad fixed in code, lane AU4 (b1fb0c50); the motion-blur guard installed, lane AU6 (d9ba2593; research/le2-motion-blur-guard.md)** |
| 14 | A player calibrated in ME1 is uncalibrated in ME2 | [body](body-and-ik.md) | no: an owner question |
| 15 | A placed squad hold gets no standing order and no answer | [powers](powers-and-squad.md) | no: `GiveLe2Order` duplicates `Le2Order` |

# Reinvented, and problems met twice

- Overlay composition is written twice (`HalcyonOverlays.cpp` for LE1, `SequelOverlays.cpp` and `MenusLe2.cpp` for the
  sequels). The flat panel met two of ME1's solved problems again on 2026-11-17: it followed the face, and it lay at the
  floor ([menus](menus-and-game-screens.md)).
- The on-foot gate and the body-yaw frame exist three times, in `Melee.cpp`, `Throwables.cpp` and `Holsters.cpp`
  ([holsters](holsters-throwables-melee.md)).
- The galaxy map's mod side is a second flow (`GalaxyProjectorFlow` beside `GalaxyMap`).
- Three of ME1's input rules (an R3 tap casts nothing, B never melees, a grab asks whether combat is on) shipped broken on
  ME2 and were fixed a day later. Each was in ME1's "already solved" list ([input](gestures-and-input.md)).

# Where the ledger is wrong

| ledger cell | what the audit read |
|---|---|
| G14 body scale, "shared" | the reference cannot settle: `player::Crouched` is ME1's handle |
| G32 squad placement | ME2's placement bypasses the dispatcher |
| R11 character shadows, "shared V" | off on ME2 always, not "while the second eye is a replay" |
| the generated table's `MakoControls` / `MakoGunner` as LE1 only | `CMakeLists.txt` compiles the cores for every game; unresolved |

`CLAUDE.md` still says ME2 and ME3 keep dynamic shadows on; `kVrDynamicShadows` is false for every game.

# Corrections the audits found in the catalog

Applied: PWR-24's game modes (0, 2 or 3), and REN-14's "must stay" (ME1's measurement, not a trilogy rule).

Not applied, still to check:
- HOL-09 is `[verify]` though `research/le2-vrik.md` records an ME1 run on 2026-11-17.
- The `releaseGrace` struct default (0.3) against the documented 0.2.
- `Melee.cpp:829` walks `PawnList` raw.
- `[Comfort] walk` is read by the mod, not the host.
- SET-08's section list may lack `Thespian`.
- WPN-22's "nothing zooms the view", against a comment in Halcyon ME2's `calcview_hook.cpp:1015` that calls zoom-fill
  "what ME1's working ADS does".

# Questions for the owner

1. Should the support grip at the shoulder send LT on ME2 at all, given LT is ME2's zoom?
   **2026-11-18:** the owner asked for the accuracy without the zoom's side effects. The proposal is written in the
   [weapons audit](weapons-and-sights.md) ("Proposal: the shouldered gun's accuracy without the game's zoom"): no LT,
   and the existing one-call zoomed aim error opened for the shouldered grip. Not built; what else ME2's zoom state
   changes is untraced.
2. What should a left-grip squeeze at the chest do on ME2, where there is no grenade slot?
   **Answered 2026-11-18:** no haptic cue at the chest on ME2. Read as: the chest is not a slot there, so it gives no
   buzz and takes no action (the lead's reading). **Built 2026-11-18 (lane AU6, c8f6987e):** the chest and the receiver are held,
   a squeeze there gives no squad order (seen on Save 4).
3. Is the fifth reach zone (the SMG at the right thigh), on by default, approved?
   **Answered 2026-11-18:** "Right thigh may work for now." Kept.
4. One calibration for the trilogy, or one per game?
   **Answered 2026-11-18:** shared, unless a game's VRIK approach differs; then one per game. The
   [body audit](body-and-ik.md) rates ME2's arm IK as ME1's shared code, so ME2 should read ME1's calibration.
5. Should ME2's sniper show ME2's own magnification (8x by its FOV) or ME1's feel?
   **Answered 2026-11-18:** "sniper zoom is unified": one rule for the trilogy, read as ME1's.
6. Halcyon ME2's `VehicleHeadMode = 3` and `ConvoFirstPerson`: pin both off in the baseline?
   **2026-11-18:** explained to the owner (mode 3 is head aim for the Hammerhead's cannon; `ConvoFirstPerson` is upstream's
   own first-person conversation, off in the ini but not pinned). The owner: "vehicle head mode likely shouldn't be on.
   We'll be building out the models for first person default." Both pinned by the baseline: `VehicleHeadMode = 1` (free
   look), `ConvoFirstPerson` off.

# What to do

Parity work only, ordered by what the player notices. Each audit's own list has the files and the closing check.

1. Sweep mechanism 1: every raw game-mode number, camera-mode name list, `Ready()` gate and field without `kEveryGame`
   in a module ME2 compiles. One grep-driven pass closes findings 8 and 10 and parts of 2, 3 and 15.
2. Extend the world-UI tag list to ME2's shop, research, locker, squad and minigame screens (finding 1).
3. Serve the wheel panel and the presentation hold from the sequel host (findings 2, 11).
4. Read ME2's base FOV, decide question 5, and bring scope capture's two halves to ME2 (finding 3).
5. Make the power table open-ended or complete (finding 7).
6. Bring `game/CombatStance`'s walk, constant eye and draw rate to ME2 (finding 9).
7. Give the render size an owner on ME2 and un-grey the Display rows that work (finding 12).
8. Install the three guards (finding 13).
9. Add a row to the port checklist: "audited against the catalog's contracts", next to the ledger row.

# Citations

- [How to catalog a system and audit a port against it](../../how-to-catalog.md)
- [ME1's systems](../../me1/index.md)
- [The parity ledger](../../../trilogy-parity-ledger.md), [the trilogy plan](../../../trilogy-plan.md)
