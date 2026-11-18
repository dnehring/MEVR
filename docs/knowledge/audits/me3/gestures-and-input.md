---
type: Audit
title: ME3 gestures and input against ME1's contracts
description: Contract by contract, what each controller input and hand gesture does on ME3, who claims it, what the game still sees, and every action ME3 takes that ME1 does not.
tags: [audit, me3, parity, input, gestures, arbitration]
status: draft
timestamp: 2026-11-18T10:00:00Z
generated: { by: claude-code/fable-5.2, at: 2026-11-18T10:00:00Z }
game: LE3
against: ../../me1/gestures-and-input.md
commit: 583591c6
---

# Verdict

Audited against [ME1 gestures and input](../../me1/gestures-and-input.md) by reading code and notes at 583591c6; no game
was run. ME3 has only been run on the Normandy with a non-combat pawn
([research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md)), so no on-foot combat input has been seen
in the game. Rows that lean on the sibling audits say so: [holsters](holsters-throwables-melee.md),
[powers and squad](powers-and-squad.md), [weapons](weapons-and-sights.md), [body](body-and-ik.md), [vehicles](vehicles.md).

- The mapping layer is ME1's own: one `GamepadMapping`, one `PadMerge`, one tick order in `Mod.cpp`, the same flags, and
  the ME3 host shares ME2's merge function line for line (`HalcyonSequelBridge`). ME3 has no second input system.
- **"Gestures registering actions ME1 doesn't" has the same two causes as on ME2**, both unfixed for ME3. The same pad
  signal means something else (LT zooms, X reloads, B is melee and heavy melee, A is one context button for use, sprint,
  roll and cover, R3 is navigation and the map), and ME1's first claimants of the left grip (grenade slot, vent) do not
  exist, so those squeezes fall through to the radio and a squad order.
- **ME3 adds a third cause: its melee is a second rule set.** ME1's strokes (`Rules::Physical`) are replaced by
  `Rules::Staged` with its own admission list in `Mod.cpp`: the striking hand must have every control released, so the hand
  holding the gun never strikes and there is no fist. This is the one `reinvented` row.
- **A gun let go is never put away** (`[Holsters] le3PutAway = 0`), so ME1's right-grip contract is half there: a slot
  grab is a weapon switch, and the "weapon away" rules (trigger never draws, fist punches) have no state to act in.
- Recentering from the controllers is gone, as on ME2: it is a double tap of Back.

| rating | count | ids |
|---|---|---|
| same | 11 | INP-01, 02, 04, 05, 13 (its vent exception n/a), 15, 17, 19, 23, 24, 40 |
| same core, other inputs | 11 | INP-03, 10, 11, 14, 18, 25, 27, 29, 30, 34, 42 |
| adapter | 6 | INP-06, 16, 21, 32, 41, 44 |
| reinvented | 1 | INP-28 |
| partial | 4 | INP-07, 12, 31, 33 |
| missing | 3 | INP-09, 22, 26 |
| n/a | 5 | INP-08, 35, 36, 38, 43 |
| unknown | 3 | INP-20, 37, 39 |

# ME3's input table

On foot is game mode 0 in every game. ME3's other numbers differ from both ME1's and ME2's (1 vehicle, 2 Atlas, 3 power
wheel, 4 weapon wheel, 5 command, 6 injured Shepard, 7 conversation, 8 cinematic, 9 GUI, 10 movie, 11 galaxy, 12 orbital);
shared code asks `engine::adapter::GameMode` ([GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):39-66).

What ME3 itself binds to a pad signal is taken from the repository where it has it (RB and R3: read from `Coalesced.bin`,
[research/le2-vrik.md](../../../../research/le2-vrik.md) "Sequel controls"). For A, B, X, Y and LT the repository has no
binding line; those cells are marked "(game knowledge)" and are the first thing to trace.

| input or gesture | on ME3 | first claimant, then | what ME3 itself sees | against ME1 |
|---|---|---|---|---|
| right trigger | fires | game pointer on a screen > the game | RT; in a conversation RT is the Renegade interrupt | same on foot; "never draws" is vacuous (no weapon-away state by default) |
| right grip | holds the gun; a new squeeze at a slot switches to that weapon; with a heavy weapon in hand a slot grab is refused | holsters | nothing (never RB) | ME1 holsters on letting go at the slot; **ME3 never puts the gun away** (`le3PutAway = 0`) |
| right fist | nothing | - | nothing | ME1 punches with the weapon away |
| left trigger | opens our drawn squad menu | hand power wheel > squad menu (mode 0 only) > the game's LB | nothing on foot; **LT / RT in conversations, the galaxy map and orbital scanning** | ME1 opens the game's own wheel, also from the wheel modes |
| left grip, new squeeze | support grip, radio at the ear, else a squad order | open power wheel > support zone (host) > radio > squad order | nothing; LT 255 while the support grip is at the shoulder (not a sniper rifle) | ME1 asks the grenade slot and the vent first; on ME3 those squeezes become orders. LT there starts ME3's zoom |
| A | the game's A | - (the double-A traversal gesture is off: `gTraversalGestures = false`) | A: use, sprint while held, a roll with the stick pushed, cover (game knowledge); the cover snap is refused by the mod (`FindCover` answered false inside `TryAcquireCover`) | ME1's A only uses. **The roll and sprint on A are actions ME1's A never takes** |
| B | squad placement (hold, double tap) | squad placement | nothing | same; ME3 loses melee (tap) and heavy melee (hold) on B (game knowledge) and the level's leave (the menu's Leave entry stands in) |
| X | hand power wheel | power wheel | nothing | same; X is ME3's reload (game knowledge), so the controllers have no reload button |
| Y | the game's Y | - | Y | what ME3 does with it: not established |
| left stick | walks toward the head | power wheel > squad menu > walk | the stick, after ME3's own traversal-presentation compensation; zeroed in cinematics | ME1 has neither adjustment |
| left stick click | the game's L3 after 100 ms | chord > the game | L3 | same rule; what ME3 binds to L3 not established |
| right stick X | turn (the game's, or snap) | squad menu > placement selector > comfort | stick X unless the mod turns | same, plus ME3's native locomotion admission |
| right stick Y | nothing of ours | - | stick Y | ME1 steps the scope's zoom under ADS; ME3's zoom modes are not read |
| right stick click, held 0.3 s | the game's power wheel (`SFXGameModeBase.EnterPowerWheel`) | chord > the mod's hold | **nothing: R3 never reaches ME3** | ME1 still sends R3 |
| right stick click, tap | nothing | - | nothing | same |
| right stick double-click | nothing | - | nothing | **ME1 recenters** |
| both stick clicks | tap snapshot, hold our menu | chord | nothing | same |
| left menu button | Start; with the left grip Back; Back twice recenters | - | Start / Back | ME1 does not depend on it |
| fast stroke of an **empty-handed, fully released** hand at an enemy | melee (`TryMelee` after ME3's native admission) | melee after holsters | a call | ME1: any fast stroke, the gun hand included, and the fist. ME3's rule set is its own |
| a hand held back ("chamber") then struck | heavy melee: **off** (`mevr melee heavy-gestures on`), waits for question 7 | - | - | ME3 only |
| open left hand pushed | casts the equipped power at the lock, or where the ray lands (every aimed ME3 power is a place power) | power wheel | a call | ME1 refuses a target power without a lock. **An equipped grenade (a power on ME3) is thrown by the push** |
| left palm on chest / gun / over the head | self cast | power wheel | a call | same rule; which powers by ME3's own table |
| left grip at the hip pouch, clip to the gun | reload by hand: **off** (`[Reload] enabled = 0`) | - | - | sequels only; the owner turned it off |
| head lowered | crouch | posture, behind ME3's native movement admission | `ShouldCrouch` by call | same rule, stricter gate |
| fingertip on the left wrist | the omni-tool's rail | host | - | the target on Shepard's drawn forearm has no bones on ME3: the controllers' wrist is used |

# Contract by contract

| id | rating | how ME3 gets it | where it differs | evidence |
|---|---|---|---|---|
| INP-01 | same | `gp::FromControllers` and `Merge`, unchanged | - | [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):352-395 |
| INP-02 | same | `core/xr/Input` on Halcyon's ME3 session | - | [Input.cpp](../../../../src/core/xr/Input.cpp) |
| INP-03 | same core, other inputs | the same flags in the same order | host-only `gateRightClick` and `keepPowerClick` (both true); in the conversation mode and both map modes the options are replaced by `ConversationTriggers`; ME3 alone keeps the pad while its map or traversal presentation is active | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):129-155; [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp):185-202 |
| INP-04 | same | same latches | - | [GamepadMapping.cpp](../../../../src/game/GamepadMapping.cpp) |
| INP-05 | same | `menus_le2::FilterPad` after the merge | - | [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):389-391 |
| INP-06 | adapter | the sequels' `RefuseGameDraw` | vacuous by default: with `le3PutAway = 0` the weapon is never away on a mission. Not run | holsters audit, HOL-16; [HandCalibration.cpp](../../../../src/game/HandCalibration.cpp):313-317 |
| INP-07 | partial | same holsters module; modes by `IsFootOrWheelMode` (ME3's 0, 3, 4) | the gun is drawn (switched to) at a slot; letting go never holsters, at the slot or elsewhere; a heavy weapon refuses a grab (the owner's answer to question 13) | [GameAdapter.hpp](../../../../src/engine/GameAdapter.hpp):74-77; holsters audit HOL-11, 12 |
| INP-08 | n/a | ME3's grenades are powers; no grenade slot | a right squeeze at the left chest is an ordinary squeeze | holsters audit HOL-22 to 30 |
| INP-09 | missing | no fist: the staged rules need trigger and grip under 0.15 | partly forced (no weapon-away state by default), partly the rule set | [Mod.cpp](../../../../src/mod/Mod.cpp):1091-1098 |
| INP-10 | same core, other inputs | the same trigger latch | our drawn menu, never the game's wheel; taken in mode 0 only (ME1: on foot and in both wheel modes) | [SquadWeapons.cpp](../../../../src/game/SquadWeapons.cpp):1693-1701 |
| INP-11 | same core, other inputs | `leftTrigger = lb`; no LT from the support grip since be1a47ba (the weapons audit's WPN-09, option B) | the trigger IS LT in conversations, the galaxy map and orbital scanning; LT from the grip was ME3's zoom | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):129-155; weapons audit WPN-09 |
| INP-12 | adapter (fixed) | the same order in `Mod.cpp`; the two absent claimants' zones held (lane AU6, c8f6987e) | No grenade slot and no vent, but their zones are kept. A new left squeeze within the grenade slot's radius (`game/Throwables` `ClaimChest`, its body frame `PlaceSlot` shared with ME1's) or over the receiver (ME1's vent zone, `game/Vent` `ClaimReceiver`, where the support grip didn't take it) is claimed and does nothing: no buzz (the owner: no haptic cue at the chest on ME2), no radio, no squad order. The reload's claim is still not asked by orders or voice (dormant while reload is off). V on the combat fixture: `throwables: a left squeeze at the left chest ... held` and `vent: a left squeeze over the receiver (-0.05 m along the gun, 0.13 above, 0.08 beside) ... held` (no squad on the fixture to order) | [Throwables.cpp](../../../../src/game/Throwables.cpp) `ClaimChest`; [Vent.cpp](../../../../src/game/Vent.cpp) `ClaimReceiver` |
| INP-13 | same | host mapping on a new squeeze | the hot-receiver exception does not exist | [Vent.cpp](../../../../src/game/Vent.cpp):184 |
| INP-14 | same core, other inputs | A passes | ME3's A is a context button; the mod refuses only the cover snap. The traversal double tap is compiled and off | [Posture.cpp](../../../../src/game/Posture.cpp):139-145; [Mod.cpp](../../../../src/mod/Mod.cpp):202, 1215 |
| INP-15 | same | `melee::AdjustPad`, every game | ME3 loses what it binds to B; traced on ME2 only (`melee-b.ps1`), never on ME3 | [SupportGrip.cpp](../../../../src/game/SupportGrip.cpp):284; holsters audit HOL-38 |
| INP-16 | adapter | same arc and selector | the order goes through `SquadPlace`'s own sequel function, not the one dispatcher | powers audit, PWR-33 |
| INP-17 | same | same core rule | - | [SquadPlace.hpp](../../../../src/core/vr/SquadPlace.hpp) |
| INP-18 | same core, other inputs | X reserved by the same flag | X has a game meaning ME1's X lacks; with the hand reload off there is no reload on demand | [PowerWheel.cpp](../../../../src/game/PowerWheel.cpp):1981-1989 |
| INP-19 | same | same flag | - | powers audit PWR-06 |
| INP-20 | unknown | Y passes | what ME3 binds to Y on foot is not in the repository; `mevr calltrace 5` with a Y press on a mission save would say | - |
| INP-21 | adapter | the mod calls `SFXGameModeBase.EnterPowerWheel` after 0.3 s (mode 3), a late-opening wheel closed by `WatchPowerWheel`; a tap does nothing | R3 kept from ME3 (question 14). Save 31: the call was made and "didn't open it" (no combat on the Normandy), so the wheel itself is unseen | research/le2-vrik.md "Sequel controls"; [HalcyonSequelBridge.cpp](../../../../src/plugin/HalcyonSequelBridge.cpp):372-377 |
| INP-22 | missing | Halcyon ME3's recenter is a double tap of Back | Back comes only from the left menu button with the left grip; left: our menu's Recenter and opening the menu | `src/halcyon/le3/me2_menu.cpp` lines 129-174, 476-477 |
| INP-23 | same | `GateClick`; R3 gated the same way | - | [GamepadMapping.cpp](../../../../src/game/GamepadMapping.cpp) |
| INP-24 | same | `UpdateStickChord` in the sequels' menu glue | - | [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp):172 |
| INP-25 | same core, other inputs | `game/Comfort` | an ME3-only gate, `NativeLocomotionAllowed`, in front of it | [Comfort.cpp](../../../../src/game/Comfort.cpp):128-133, 286-288 |
| INP-26 | missing | the flick is read and can never step | ME3's `AimModes` are not read (weapons audit WPN-20, 21) | [Sights.cpp](../../../../src/game/Sights.cpp):856-890 |
| INP-27 | same core, other inputs | `WalkTowardHand`, then Halcyon's head-yaw turn | then ME3's traversal-presentation compensation on the left stick on foot, and the stick zeroed in cinematics: two host-side steps ME1 and ME2 lack | [HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp):204-220 |
| INP-28 | reinvented | `ik::melee::Gestures` with `Rules::Staged`, fed by `Le3Tick`, admitted by `AllowMeleeGesture` | another rule set, another suppression list (0.3 s after a draw, ME1 0.5 s), ME1's `[Throwables]` speed and travel keys ignored, every control of the hand released. The holsters audit rates the component rows (HOL-31 to 36) one by one and found the enemy cone's yaw never set on ME3 | [Mod.cpp](../../../../src/mod/Mod.cpp):1077-1098, 1149-1186; holsters audit |
| INP-29 | same core, other inputs | same push rule | every aimed ME3 power is `Aim::Place`: no "needs a lock" refusal; grenades are powers | powers audit PWR-13 to 16 |
| INP-30 | same core, other inputs | same `StepSelfCast` | which powers count by ME3's adapter | powers audit PWR-17 to 20 |
| INP-31 | partial | `OmniToolRail` in the sequels' menu glue; the rail itself seen on Save 31 through `mevr omni` | the forearm target is built from `hud_pieces::Pieces`, which ME3's mod never sends (`game/HudPieces` stands down; no `setHudPieces`), so the tap uses the controllers. The tap itself was not replayed on ME3 | [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp):346-385; [HudPieces.cpp](../../../../src/game/HudPieces.cpp):177-185 |
| INP-32 | adapter | the game's crouch through ME3's admission (`MovementAdmitted`: on foot, no action, walking, move input, alive; not in cover, not storming) | stricter than ME1's and ME2's gate | [Posture.cpp](../../../../src/game/Posture.cpp):300-306, 364-370 |
| INP-33 | partial | the sequels' deny-list of five functions | as ME2 (the wheel's pick and the backup weapon are not on it); vacuous while the gun is never away | holsters audit HOL-15 |
| INP-34 | same core, other inputs | by the same mapping | two of the five do not hold: the left trigger is LT in conversations and maps, and recentering needs a menu button | this file |
| INP-35 | n/a | ME3 has no driven vehicle of the Mako's kind (the Atlas and turrets: the vehicles audit) | - | vehicles audit |
| INP-36 | n/a | as INP-35 | - | vehicles audit |
| INP-37 | unknown | in ME3's vehicle and Atlas modes (1, 2) the squad does not take the trigger (mode 0 only) and nothing replaces the options, so the left trigger is LB | by accident of the gate; what the Atlas needs on LT and LB is not in the repository. An Atlas save and `mevr calltrace` would settle it | [SquadWeapons.cpp](../../../../src/game/SquadWeapons.cpp):1697-1698 |
| INP-38 | n/a | no cockpit | - | vehicles audit |
| INP-39 | unknown | each module's on-foot gate | not traced for modes 1, 2 and 6 (injured Shepard keeps the HUD gameplay mode). The raw-number comparison ME2's audit found in `MenuPause` is translated on ME3 | [MenuPause.cpp](../../../../src/game/MenuPause.cpp):182-188 |
| INP-40 | same | our menu and calibration in the sequels' glue | - | [MenusLe2.cpp](../../../../src/plugin/MenusLe2.cpp) |
| INP-41 | adapter | `plugin/GamePointer`; events as `SFXGameModeGUI.GuiKey` or `SFXGUIMovie.SendMouseEvent` | names only; a replayed ray click opened the squad record on Save 31 (ledger G26 "adapted V") | [the ledger](../../../trilogy-parity-ledger.md) G26 |
| INP-42 | same core, other inputs | the pad is the game's | the triggers are the interrupts; every mod owner stripped from the options; hand-walk off so the reply wheel is not rotated; first-person conversations are not on ME3 | [PadMerge.cpp](../../../../src/plugin/PadMerge.cpp):129-145 |
| INP-43 | n/a | ME3 never holds the game's weapon wheel; `kFlagSquadMenu` owns the sticks | - | powers audit PWR-24, 25 |
| INP-44 | adapter | `\` our menu, K first person, End and Home; keys only with the game in front | no R recenter (R is ME3's reload) | `src/halcyon/le3/me2_menu.cpp` lines 476-522 |

# Actions ME3 takes that ME1 does not

| input | ME3's action | forced by the game? | the owner |
|---|---|---|---|
| A with the stick pushed / held | roll, sprint (the game's own context button) | yes | not asked; ME1's sprint is the game's too, the roll is new |
| triggers in a conversation and the maps | interrupts, scanning | yes | approved (rule 11) |
| left grip at the left chest or over the receiver | nothing: the zones are held (lane AU6, c8f6987e) | no: ME1's zones kept with nothing in them | ME2's answer (no haptic cue at the chest) |
| support grip at the shoulder | LT, which zooms the weapon | LT's meaning is the game's; sending it is ours | complained 2026-11-17; the picture fixed, the cause open |
| open left hand pushed with a grenade equipped | throws it where the ray lands | grenades as powers: yes; throwing by the push: no | not asked |
| open left hand pushed with no lock | casts a target power at the ray's landing point | no (ME3's adapter marks every aimed power a place power) | not asked |
| right grip at a slot | switches weapon; the old one is not holstered first | ME3 has no holstered state; the put-down exists and is off | lane's default, not an owner answer |
| right grip at a slot with a heavy weapon | refused, buzz | yes (a switch throws it away) | approved (question 13) |
| stroke of a released, empty hand | melee by ME3's staged rules | no | "on by default as on ME1 and ME2" (2026-11-17); the differing rule set was not put to the owner |
| chamber and strike | heavy melee | ME3 has heavy melee | off; question 7 open |
| right stick click | never R3 | ME3's R3 is navigation and the map | approved (question 14) |
| B | also no melee, no heavy melee, no leave | ME3 binds them to B | approved (question 16) |
| left menu button with the grip, twice | recenter | Halcyon's choice | not asked; conflicts with "no menu buttons" |

# ME1 rules that do not hold on ME3

| ME1 rule | on ME3 | forced |
|---|---|---|
| the left trigger is never LT | LT in conversations and both map modes | yes |
| nothing uses a controller menu button | recentering does | no |
| R3 still reaches the game | never | yes (approved) |
| letting the gun go at its slot holsters it | never, by default | partly |
| the trigger never draws; a right fist punches with the weapon away | no weapon-away state for either | partly (follows from the row above) |
| a fast stroke of any hand is melee | only a hand with every control released | no |
| a left squeeze is a grenade or a vent before anything else | neither exists; their zones are held (no action) | yes; that the squeeze does nothing there is ours (lane AU6) |
| a push without a lock does not cast a target power | it casts at the ray's end | no |
| the right stick double-click recenters | does nothing | no |
| the weapon wheel is the game's own, steered at the hand | our drawn menu | not established as forced |
| a flick under ADS steps the zoom | never | no (the modes are unread) |
| A only uses | A also sprints and rolls | yes |

# Against ME2

| ME2 finding | on ME3 |
|---|---|
| left-grip squeezes at the chest and receiver become squad orders | **fixed (lane AU6, c8f6987e)**: held on both sequels |
| LT from the support grip is the game's zoom | same defect |
| recentering needs a menu button; the R3 double-click does nothing | same defect (the same upstream code in `src/halcyon/le3`) |
| X reserved hides the game's reload | same defect |
| the squad menu taken in mode 0 only, drawn instead of native | same defect |
| "only holsters draw" is a five-function deny-list | same, and vacuous by default |
| the reload's claim not asked by orders and voice | same defect (dormant) |
| Y not established | same |
| the wrist tap and game pointer untraced | different: the pointer is verified on ME3; the wrist target has no bones on ME3 |
| no fist punch (forced) | same |
| double A vaults | different: off on ME3; A's own roll and sprint instead |
| a gun let go in combat stays out, with a buzz | different: on ME3 it always stays out, silently |
| three "never" rules broken at first run, fixed a day later (R3 tap cast, B meleed, a grab drew where combat is off) | fine: the fixes are shared code. None was traced on an ME3 mission |
| raw mode numbers in `MenuPause.cpp` and `HandReticle.cpp` | fine in `MenuPause` (an ME3 branch); `HandReticle`'s `mode == 1` happens to be ME3's vehicle too |
| `Le2Tick`'s third copy of the body frame and on-foot gate | different: ME3 has a fourth, in `MeleeLink` |

# Reinvented

| ME3 code | does what this already did | what unifying takes |
|---|---|---|
| `Rules::Staged`, `Le3Tick` and the admission functions in [Mod.cpp](../../../../src/mod/Mod.cpp):1067-1118 (`ActionSceneAllowed`, `ActionHandFree`, `ActionControlsReleased`, `AllowMeleeGesture`) | ME1's `Rules::Physical` and `melee::Strike`'s order of reasons; the suppression list in `game/Throwables` | run `Physical` on ME3 behind ME3's native admission; keep `Staged` only for the heavy chamber if question 7 says yes. The holsters audit lists the rows |
| `MeleeLink`'s body frame (head-relative grip, torso yaw) in [Mod.cpp](../../../../src/mod/Mod.cpp):1167-1183 | `Throwables`' frame, `Holsters`' own, `Le2Tick`'s | one shared "on foot, body frame" sample |
| `ActionSceneAllowed` as "authored scene" test | `player::MovementAdmitted` (the one admission read comfort and posture already use on ME3) and ME1's `authored_moment` | one admission; today ME3 has two that can disagree |
| Halcyon ME3's Back double tap | ME1's right-stick double-click (`kRbDoubleClickMs`) | call the host's recenter from the mapping's double click in the sequels' bridge |
| the ME3 host's left-stick compensation in `PadFiltered` | `pad_merge::WalkTowardHand` and Halcyon's head-yaw turn already rotate the stick | not judged here: read with the comfort audit before adding a fourth rotation |

# Rediscovered problems

| problem on ME3 | already held by | cost |
|---|---|---|
| each B press on the area map restarted its close animation; the omni-tool's Close backed out 8 times | ME1's backout rule ("4 ticks apart, at most 6, until the game leaves the mode", research/57) | found and fixed 2026-11-16 ([gotcha](../../gotchas/me3-area-map-b-restarts-close.md)) |
| hand-walk rotated ME3's reply wheel selection | ME1's rule that the pad is the game's in a conversation; ME2's `ConversationTriggers` | one comment and two cleared flags in `PadMerge.cpp` |
| a pad signal's ME3 meaning found only when it misfired (RB tap, R3, B) | the ME2 audit's pattern, and research/19 "Hold only" | fixed in shared code on 2026-11-17, for both sequels at once |
| ME3 dropped the published controllers at a recenter, so every held grip and trigger let go for some ticks | ME1's rule that a press keeps its meaning (INP-04) | fixed 2026-11-16 ([HalcyonLe3Bridge.cpp](../../../../src/plugin/HalcyonLe3Bridge.cpp):174-183) |
| mode 0 does not mean "the player may act" (authored scenes keep it) | ME1's moments and seated scenes (research/69) | ME3 answered with native admission reads, three of them, not one |

The pattern is the ME2 audit's: the mapping came over with ME1's flags and no list of what each pad signal means in ME3.
For ME3 that list still does not exist for A, B, X, Y, L3 and LT.

# Forced by the game

Interrupts on the triggers; LT and RT in the maps; A as a context button; X as reload; B as melee; R3 as navigation and the
map; grenades as powers; heavy weapons; no heat; no holstered action state on a mission
([research/le2-vrik.md](../../../../research/le2-vrik.md) "ME3's holsters, from its own script"); twelve game modes.

# Not forced

The left-grip squeezes that fall through to squad orders; recentering on a menu button; the squad menu in mode 0 only;
LT sent by the support grip; the staged melee rules and their separate admission list; every aimed power a place power;
the gun never put away by default; the zoom flick dead; the wrist target without bones.

# What to do

1. **Write ME3's pad contract first**: for each XInput signal the ME3 function it reaches on foot, from `Coalesced.bin`
   (the reader exists: research/le2-vrik.md used it for RB and R3) and then `mevr calltrace` on a mission save. Closes
   INP-20 and the "(game knowledge)" cells; INP-37 and INP-39 need an Atlas save.
2. **Melee on ME1's rules** (holsters audit, its item on `Rules::Staged`): owner question whether ME3's stroke should be
   ME1's (the gun hand strikes) as the answer of 2026-11-17 reads. Files [Melee.cpp](../../../../src/game/Melee.cpp),
   [Mod.cpp](../../../../src/mod/Mod.cpp). Check: `tests/MeleeTests.cpp` comparing ME3's feed with `Physical`, then
   `mevr melee strike` on a fixture.
3. **Left grip with no claimant**: the ME2 audit's question 2, one answer for both sequels. Check: a replayed squeeze at
   the chest gives no `squad orders:` line.
4. **Putting the gun away** (`le3PutAway`): an owner question plus the combat fixture; it decides whether INP-06, 07, 09
   and 33 can be ME1's at all.
5. **Recenter without a menu button**: the right-stick double-click from the sequels' bridge, once for ME2 and ME3. Check:
   `recentered` in the log after a replayed double click.
6. **LT from the support grip**: with the weapons audit's item 3.
7. **Reload on demand**: an owner question (X is taken, the hand reload is off). No design proposed here.
8. **The power push without a lock, and grenades on the push**: owner questions (powers audit).
9. **The wrist target**: publish the arm bones on ME3 without the rest of `HudPieces`
   ([HudPieces.cpp](../../../../src/game/HudPieces.cpp):177-185). Check: snapshot `omniRail.target` under
   `tools/make-wrist-recording.py`.

# Citations

- [The catalog file](../../me1/gestures-and-input.md); [the ME2 audit](../me2/gestures-and-input.md) and
  [its summary](../me2/summary.md); [the parity ledger](../../../trilogy-parity-ledger.md) (G05, G08, G26, G28, G40; it
  has no row for input arbitration, and its G40 "adapted U" does not show ME3's stricter crouch gate);
  [the trilogy plan](../../../trilogy-plan.md) for the owner's answers to questions 7, 13, 14 and 16.
- [research/le2-vrik.md](../../../../research/le2-vrik.md), [research/le3-melee-audit.md](../../../../research/le3-melee-audit.md),
  [research/le3-traversal-adapter.md](../../../../research/le3-traversal-adapter.md),
  [research/le3-combat-fixtures.md](../../../../research/le3-combat-fixtures.md),
  [research/19](../../../../research/19-body-holsters.md), [research/20](../../../../research/20-melee-and-grenades.md).
- From reading alone: every row. From game knowledge, not a repository note: what ME3 binds to A, B, X and LT on foot.
