# 55: Powers from the hand: a hand menu and a target lock (2026-11-04)

The user: how do we handle biotic and tech powers now, and could we do it like Blade & Sorcery, a menu floating at the
left (or right) hand that you steer up/down/left/right to pick the power, then a lock so the power lands on what you
meant? This note records how LE1 casts and aims a power, what we do with powers today, and what a hand menu with a
lock would be built from. Research only, nothing built.

Sources, all local and read-only: `SFXGame.pcc` and `Engine.pcc` read with Legendary Explorer Core 6.5
(`local-tools/LegendaryExplorer`, its script decompiler and 2DA reader, driven by a scratch console program); the
mod's natives dump (`%LOCALAPPDATA%\MEVR\dump\natives-LE1-60AEEFA2.csv`); Ghidra on a copy of the LE1 project
(the project itself was held by other sessions' MCP servers); LExSDKv2's LE1 headers for offsets. Nothing extracted
is kept in the repository.

## 1. What we do with powers today

- **Right stick click** (`[Holsters] powerWheel = r3`): a tap is RB tapped, the game's mapped power
  (`SFXGameModeDefault.TryUseMappedPower`); held, RB held opens the game's power wheel (`EnterPowerWheel`, game mode 2),
  paused, steered by the right stick, on the HUD panel by default (`[Squad] powerWheelAt = hud`: the user, 2026-10-22,
  the right hand is busy with the gun; `left`, `right` or `front` put it on a panel of its own; research/19).
- Holsters let the game draw for powers (`TryUseMappedPower`, `EnterPowerWheel` are allowed callers, research/19).
- **Where a power goes** depends on the power's aim type (section 3): a "selected target" power goes to
  `BioPlayerSelection`'s target, which since research/40 follows the **head's gaze** on foot; a "camera trace" power
  goes where the player camera points, which in VR follows the body and the stick, not the head or a hand
  **[verify]** (section 4). No power follows a controller.

## 2. How LE1 starts a power

- `SFXGameModeDefault.TryUseMappedPower()` (exec, script): nothing unless combat is enabled and a power is mapped;
  with the weapon away it only draws (`DrawPrimaryWeapon`); else the pawn's behavior `SetCurrentPower(MappedPower)`
  and `AcquireActive(class'BioActivePower')`.
- `SFXGameModeDefault.EnterPowerWheel()`: the same combat and weapon checks, then game mode 2
  (`SFXGameModePowerWheel`). Its `Activated` pauses the world (`WorldInfo.PauseGame(true)`), its `Deactivated`
  unpauses and calls `ApplyTacticalOrders()` (the squad's powers chosen in the wheel). The selection itself is in the
  HUD movie (`BioSFHandler_HUD`).
- **`BioPlayerInput.ActivatePower(Name nmPower, optional Actor oTarget, optional Vector vTargetLocation)`** (native,
  thunk 0xB7A1F0, body 0xC64C90): the engine's own "cast this power at this". It finds the player's pawn and behavior,
  asks the behavior whether powers are enabled (behavior vtable 0x3D8), `SetCurrentPower` (0x680), interrupts the
  current modal active if it is of one class (a flag at +0x98, then 0x410), `AcquireActive(BioActivePower)` (0x3F8),
  and on the new `BioActivePower` calls **`SetTargetActor(oTarget)`** (its vtable 0x318) and **`SetTargetLocation(
  vTargetLocation)`** (0x320). It is also what the weapon's alternate fire path (0xC62650) and 0xC71FA0 call. Unlike
  `TryUseMappedPower` it has no weapon-drawn check of its own **[verify what a cast with the weapon away does]**.
  The cheat manager's `ActivatePower(Name, string target)` exec wraps it (`BioCheatManager`).
- `BioActivePower` (the running power, the behavior's `m_oCurrentModalActive`): states not started, casting, release,
  using, finished; `m_vLocationToAimAt` (+0x90) and `m_oTargetToAimAt` (+0xDC); public natives `SetTargetActor`,
  `SetTargetLocation`, `CancelPower`, `AnimationReadyForPowerRelease` (the casting animation's notify,
  `BioPawn.ReleasePower`); private natives for where it lands: `GetReleaseLocationForPlayer` (thunk 0xBB2EE0, body
  virtual 0x3E0), `GetConeLocationForPlayer` (0xBB35E0), `GetProjectileLocation` (0xBB2990, virtual 0x3D0),
  `ReleaseProjectilePower` (0xBB21A0), `ReleaseRadiusPower`, `ReleaseConePower`, `ReleaseVerticalCylinderPower`.
- `BioPowerManager` (natives): `GetAimTargetForPlayer(float fMaxRange, out Actor, out Vector)` (thunk 0xB84410, virtual
  0x308) with **`m_fPowerHalfAngle = 4.0`** (config: a 4-degree half-angle aim cone), `CheckLOSToActor(source,
  destination, start, maxRange, bIgnoreFriendlies, out hit actor/location/normal)` (0xB83E50), `CheckLOSToLocation`,
  `CanImpactActor`, `AreActorsFriendly`.
- Biotics fly: `BioLinePowerProjectile` (default `Speed` 1300 units/s, 13 m/s; a 10-unit collision cylinder) carries
  `m_vTargetLocation` and `m_oTarget`, touches what it passes and can hit walls (`HitWall`). Whether it steers toward
  `m_oTarget` is **[verify]**.

## 3. Each player power's aim type (`BIOG_2DA_Talents_X.Powers_Powers`, Engine.pcc)

| Power | Discipline | Aim type | Script |
|---|---|---|---|
| Throw (`TK_Throw`) | Biotics | selected target | BioThrowScript |
| Lift (`TK_Lift`) | Biotics | selected target | BioLiftScript |
| Warp (`SD_Warp`) | Biotics | selected target | BioWarpScript |
| Singularity (`SD_Singularity`) | Biotics | camera trace | BioSingularityScript |
| Stasis (`KF_Stasis`) | Biotics | camera trace | BioStasisScript |
| Barrier (`KF_Barrier`) | Biotics | none (party) | BioShieldEntrenchScript |
| Neural Shock (`MD_Shock`) | Tech | selected target | BioNeuralShockScript |
| AI Hacking (`DC_Hacking`) | Tech | selected target | BioHackingScript |
| Overload (`EL_EMP_Delivery`) | Tech | camera trace | BioEMPBeacon |
| Damping (`EL_Damping_Delivery`) | Tech | camera trace | BioDampingBeacon |
| Sabotage (`DC_Sabotage_Delivery`) | Tech | camera trace | BioSabotageBeacon |
| Shield Boost, Unity, Adrenaline Burst, Immunity, Marksman, Overkill, Carnage, Assassination, medi-gel | Tech / Combat / Weapon | none (party) | ... |

The enum `EBioPowerAimType` has a third value, selected target at casting (none of the player's powers use it). So
five powers take an actor and five a place; the rest need no aim, which matters for a menu: those cast at once.

## 4. A hand menu with a lock, from what the engine gives us

**Possible: the engine already has the call a lock needs.** `ActivatePower(power, actor, place)` is how the game itself
starts a power at a chosen target, so a power picked in our menu can be cast at the actor our lock chose, on the game
thread through `ue3::runtime::Call`, with no hook. What it would be built from:

- **The roster:** the player's `BioEpicPawnBehavior.m_powers` (each `BioPower`: `m_nmPower`, `GetDisplayName`,
  `GetPowerIcon`, `IsEnabled`, `GetCooldownRemaining`, `m_eAimType`, `m_eHUDType`; HUD types "standard" and "weapon"
  are the mappable ones), read by reflection with a ReflectCheck row each, as `core/vr/SquadContext` plans for the squad.
- **The menu:** a panel at the casting hand drawn by the host (like `[Squad] style = custom`'s `plugin/SquadPanel`, or
  RmlUi in the main menu's style), its entries placed up, down, left, right (more on a ring), cooldown as a ring per
  entry. Picking by the hand's travel from where the menu opened (Blade & Sorcery's way: a wheel around the hand, move
  the hand onto the orb) or by the stick's direction. The game's own wheel pauses the world; ours would not have to (a
  mechanic choice: no pause, a pause, or slow time). **Decided (the user, 2026-11-05): no pause, no slow time:** the
  wheel only equips and the cast is a gesture (the push, section 7), so the world keeps running.
- **The lock:** candidates are hostile pawns rendered within the last second (the selection's own rule), scored by the
  angle from the casting hand's ray (the aim pose we already publish) and the distance; within the power's range and
  only if the game's own `CheckLOSToActor` sees them from the hand; kept with hysteresis (a wider cone to lose the lock
  than to take it, a short grace when line of sight blinks) so it doesn't jump. A ring at the locked target (a host quad
  as `[Interact] label` places one at the selected object) and a haptic tick on lock. For the camera-trace powers the
  place can also be the hand ray's hit in the world (`Actor.Trace`, as the sights and the Mako already do), so
  Singularity goes where you point even with no enemy there.
- **The cast:** the trigger, or a push of the open hand (`core/ik/Throwing` already tells a fast release from a slow
  one for grenades). Then `ActivatePower(power, locked actor, its place)`.
- **"It will actually land":** the lock only offers what the game would let hit (range, line of sight, a target the
  power can affect: `CanImpactActor`, `AreActorsFriendly`). A power that the game lets fly can still be blocked by
  something moving into its path or miss a target that moves away (13 m/s); making a locked power always hit would be a
  mechanic change and a setting.
- **The button:** the left hand's buttons are all taken today (X grenade, Y medi-gel, L3 crouch, the trigger the squad
  wheel, the grip the support grip, grenades and the vent). With physical grenades on, `grenadeButton = 0` frees X; a
  hand gesture (palm up, held) needs no button. The right stick click stays as it is.

**The open questions, cheapest first** (a console test on the fake headset settles most):

1. Does a target set through `ActivatePower` hold until the release for both aim types, or does the release re-read the
   selection or the camera (the virtual bodies behind `GetReleaseLocationForPlayer`, 0x3E0, and `GetAimTargetForPlayer`,
   0x308, aren't read yet)? If it re-reads, the fallbacks exist already: the selection is ours to steer
   (`engine/SelectionGaze` hooks 0xC79D70 and moves the camera's POV for that call only), and the same POV swap around
   the release would steer a camera trace.
2. What a cast with the weapon away does (the mapped power draws first; `ActivatePower` doesn't check).
3. Whether the projectile leaves from the hand our arm IK places (`BioPower.GetProjectileAttachPoint`) and whether it
   follows `m_oTarget`.
4. How the casting animation looks in first person with arm IK on (the power's `Anim`/`CoverAnim` drive the upper body).

## 5. The wheel's look (2026-11-04)

The user's concept (`concept/power-wheel.png`, local) is the model: glass tiles in a ring around the left palm, an icon
over each power's name, the chosen tile orange, grown and pulled out with a double chevron beyond it and a beam from the
palm, spokes to the others, thin rings, a gap where the forearm comes in. Then two of the user's corrections: the
concept's palm glyph was generic (our N7 mark, or nothing: `power_wheel_draw::Emblem`), and the wheel should speak the
**game's own menus'** language, with tiles whose fill brightens toward their edges as the game's buttons do.

**The game's language**, read from its own UI art (Startup_INT.pcc: the HUD movie `GUI_SF_HUD.PC_ME_HUD` and the
pause wheel `GUI_SF_InGameGui.PC_MainWheel`, their Texture2Ds exported with Legendary Explorer Core and sampled; nothing
extracted is kept):

| Element | The game's | Sampled from |
|---|---|---|
| Lines | cyan (125, 211, 232), thin, 45-degree cut corners | HUD gradients `G0`, `G16`, `G20`; frames `I192`, `I123` |
| A wheel button | a radial fill, brightest at its edge: blue (40, 133, 172) to (23, 72, 104) inside | main wheel `G2A` (and `GB`, lighter) |
| The chosen button | the same in orange: (220, 103, 27) to (117, 48, 2) | main wheel `G29` |
| Frames, highlights | orange (255, 157, 90); green (79, 227, 129) for ready | HUD `I213`, `I215` (the power slots' shield-shaped frames) |
| Panels | deep indigo (3, 19, 96) with fine horizontal scanlines | HUD `I192` |
| The HUD's own wheel | ring segments split by thin gaps, a bright cyan circle at the hub, indigo highlight segments | HUD `I1EA` |
| Fonts | BioMass (BioWare's display face), Aero Light, Myriad Pro (body) | the font movie `GUI_SF_Fonts.Fonts` |

None of the fonts can be bundled; Rajdhani Bold (already in `third_party/fonts`, SIL OFL) is the nearest of ours to
BioMass's squared forms, in the game's title case (`WheelView::upperCase` gives the concept's capitals). Using the
game's own BioMass at run time from the player's install is possible later (its glyphs are in the font movie).

**What is built** (offline look only, nothing in the game yet): `core/vr/PowerWheel` (the layout: six tiles over 253
degrees with the gap toward the forearm, each tile's outline, the hand's pick with a dead zone and hysteresis;
unit-tested), `plugin/PowerWheelDraw` (ImGui draw lists: the edge-lit fill as rings of the outline shrunk about the tile's
middle, scanlines, the cyan and orange lines, the indigo band and hub circle, spokes and beam, chevrons, the N7 mark,
names wrapped onto two lines on narrow tiles, cooldown and disabled states, the hint card), `plugin/PowerIcons` (all
nineteen player powers' icons painted from signed distance fields in each discipline's colours, plus a generic one),
`plugin/Glow` (a two-level bloom added as light in the premultiplied layer, its own device context state) and
`tools/power-wheel-preview` (`mevr-power-wheel-preview <folder>`: every state, the hint, the icon sheet).

## 6. In the game: X opens it at the left hand (2026-11-04)

The user's controls: everything on the left hand, no right-hand pinch; picking either by moving the left hand into place or
with the left stick (which then doesn't walk); X freed from the game's grenade for it. Built:

- **Opening and choosing** (`core/vr/PowerWheel`'s `Machine`, unit-tested): X held opens it; the hand's offset from where it
  opened, in the wheel's plane, picks the nearest tile past a 2.5 cm dead zone with 6 degrees of hysteresis; the left stick
  pushed past 0.5 picks by its direction (and the hand, left where it was, takes over again only once it moves on); letting go of
  X equips. Let go within 0.3 s (a tap) and it stays open: then a pinch of the left trigger, a squeeze of the left grip or X again
  equips. A trigger or grip already held when it opens confirms nothing until let go. Leaving the allowed state (not on foot,
  a menu, the controllers lost, combat disabled, the squad's wheel open) closes it unequipped.
- **Where** (`Place`, `InPlane`): its centre 4 cm from the palm (the left grip pose) toward the eyes, facing them, upright; fixed
  in the room while it's open; the host's quad 0.42 m wide (`[PowerWheel] widthM`), the hint card beside it the first 3 openings
  of a session.
- **The roster**: the pawn behaviour's `m_powers`, those in the shared table (`FindPower`, the 19 player powers of section 3),
  biotics first; cooldown as `m_fCooldownRemaining / m_fTotalCooldownTime`; `BioPower.IsEnabled` dims a tile.
- **Equipping** writes the power's name into `BioPlayerInput.MappedPower` (journalled): the game's own mapped power, which the
  right stick click's tap casts through `SFXGameModeDefault.TryUseMappedPower`. It's the stand-in for section 4's lock and fire.
- **The pad** (`gamepad::Options::powerWheel`, flag bit 24, host v28): while open the motion controllers' left stick, L3, X, left
  trigger and grip reach the game as nothing, each kept until released after it closes; the right hand stays the game's (turning,
  firing). On foot with the wheel on, X never reaches the game (`grenadeButton` off): the physical throw stays.
- **Not yet checked in the game** [verify]: the roster's reads and `IsEnabled` on a real save, the equip reaching
  `TryUseMappedPower`, the quad's placement at a tracked palm, the glow's cost on the render thread.

## 7. The lock and the push (2026-11-05)

Section 4's lock and cast, built on the wheel's equip: the power the wheel equipped (the game's `MappedPower`) is the one the
left hand aims and casts. Code: `core/vr/PowerWheel` (`UpdateLock`, `StepPush`, `LockView`; unit-tested in
`tests/PowerWheelTests.cpp`), `game/PowerWheel` (`TickLock`), the host's marker in `plugin/PowerWheel` + `PowerWheelDraw`
(`DrawLock`), HostServices v29 `setPowerLock`. Settings `[PowerWheel] lock*`, `push*`.

- **When:** on foot, the wheel closed (and nothing it took still held), the equipped power one of the player's in the shared
  table, the left controller's aim tracked, the left hand not on the gun (`support_grip::Held`) and the squad's wheel closed,
  and **the arm raised**: the hand `lockReachM` (0.20 m) ahead of the eyes along the head's flat forward. A power without aim
  (Barrier, the combat talents) scans nothing; the push casts it at once.
- **The lock:** each tick the pawns of `WorldInfo.PawnList` that are BioPawns, hostile to the player (`BioPawn.IsHostile`),
  not dead and not dormant (hidden or without collision: a base's crew before its encounter, as the Mako's gunner skips them),
  measured from the hand: the angle between the left aim ray and the pawn's chest (its location + 20 units), the distance, and
  `Actor.FastTrace` from the hand to the chest. `UpdateLock` keeps the current target while it stays within `lockKeepDeg` (16)
  and `lockRangeM` (35 m) and its line of sight hasn't been blocked longer than `lockGraceS` (0.35 s); otherwise it takes the
  best visible one within `lockAcquireDeg` (9): the smallest angle, 0.05 degree per metre deciding near ties. A new lock ticks
  the left hand. The cheap tests (angle, range) run before the script calls.
- **The automatic resolver** (`autoLock`, the user 2026-11-05: pointing within 9 degrees may be too fine in VR): with nothing
  pointed at, the best visible hostile within `autoLockHandDeg` (30) of the hand's ray or `autoLockGazeDeg` (20) of the eyes'
  forward (measured from the eyes) is locked: the least hand angle + gaze angle, 0.1 degree per metre, so the one hand and gaze
  agree on wins. It's kept while within those cones plus 6 degrees; any other hostile pointed at (within 9) takes over at once,
  and pointing at the automatic target makes it a pointed lock. A pointed lock that drifts past 16 degrees but stays in the
  automatic cones stays locked (automatic now, its lock time kept). Logged as `resolved automatically`; the console's `lock`
  and the snapshot's `"automatic"` say which.
- **A place power with nothing locked** (Singularity, Stasis, Overload, Damping, Sabotage: section 3's camera-trace powers):
  `Actor.Trace` along the hand's ray within the range; where it hits is where the power lands (the marker shows a dashed ring
  there). Nothing hit: no cast.
- **The push:** the left hand moving along its own aim ray at `pushSpeedMS` (1.6 m/s) or more, at least `pushTravelM`
  (0.10 m) of it, over the last 0.12 s; it fires once and re-arms when the hand slows below 40% of that speed, no sooner than
  `pushCooldownS` (0.6 s) after the last. Only an open hand: no push for 0.4 s after the left grip was squeezed (a grenade's
  throw from the chest or a squad order's point ends in a release that moves the hand forward).
- **The cast:** `BioPlayerInput.ActivatePower(nmPower, oTarget, vTargetLocation)` on the game thread (section 2's own path:
  `SetCurrentPower`, `AcquireActive`, then the running power's `SetTargetActor` / `SetTargetLocation`): a target power at the
  locked pawn and its chest, a place power at the locked pawn or else the landing point, a power without aim with neither. A
  cast that starts pulses the hand hard; one refused (nothing locked, cooling down, `IsEnabled` false, the game's `false`)
  buzzes long and soft, and the reason is logged.
- **The marker** (HostServices v29, `LockView`, size-checked): the mod sends the target in tracking space
  (`engine::ToTracking` of the chest) every tick; the host draws a 256 px canvas as the `powerlock` quad there, facing the
  eyes, the brackets' box `sizeDeg` (6, a place 4) across at any distance (`vr::interact::Place`, as the interaction label).
  Ready: orange brackets (the game's chosen colour) closing in over 0.18 s, a diamond at the chest; cooling down or blocked:
  blue, with a ring whose orange part grows as the cooldown runs; the equipped power's icon in a cut-corner box at the lower
  right; a cast flashes a growing ring. Offline: `mevr-power-wheel-preview`'s `lock-*.png`.
- **Console** (no tracked controllers needed): `mevr powerwheel push` (a push on the next tick), `aimhead on|off` (the head's
  ray stands in for the left hand's), `lock` (why it is or isn't ready, the target, the last scan's counts: pawns walked, awake,
  near the ray or gaze and in range, hostile; the ray and the gaze in the world; every awake pawn in range; the candidates and
  their line of sight; the last cast). Snapshot `"mod"."powerWheel"."lock"`, a `powerlock` layer.

### In the game (2026-11-05, the fake headset, VR00's Soldier save on UNC20)

- **Giving a Soldier the powers for a test:** `GiveBonusTalent` takes the `BonusID` column of
  `BIOG_2DA_Talents_X.Talent_BonusTalents`, not the row: `GiveBonusTalent 49` adds talent 247 (Throw's setup, rank 1) and
  talent 49 (Throw, rank 0, so its power's `m_nEnabled` stays 0); `ScaleTalentByIncrement 49 1` then gives Throw rank 1
  (`m_nEnabled` 1, the power enabled). Singularity: `GiveBonusTalent 57`, `ScaleTalentByIncrement 57 1`. Never save after.
- **Finding hostiles without a fight in the save:** UNC20's mercenaries (`BIOA_UNC20_00_DSG` BioPawn_1-5, about 900 m from the
  landing) are dormant: hidden, no collision, and hidden again by the game a while after `mevr gunner wake <n>`. Woken, with
  Shepard moved next to them (`god`, `ghost`, `set SFXPawn_Player Location (...)`; without ghost he fell through the
  terrain), `lock` counted 6 awake, 2 hostile in the cones, both "in sight" once the eyes were above the terrain.
- **The lock and the marker work:** nothing within 9 degrees, the automatic resolver took BioPawn_3 at 18.4 m, 18.3 degrees off
  the gaze ("resolved automatically"); the host's `powerlock` quad drew the orange brackets on it with the Throw icon, then
  blue brackets with the cooldown ring growing after the cast (snapshots).
- **The cast lands on the lock (section 4's question 1: yes):** the push's `ActivatePower` returned true for Throw and for
  Singularity (each one's cooldown started; the game's purple biotic screen flash showed). The player's `BioActivePower`
  afterwards: `m_oTargetToAimAt` = BioPawn_3, `m_vLocationToAimAt` = (64868.41, -39562.45, -26702.12), exactly the chest point
  the mod sent, `m_bAnimationReadyForPowerRelease` true, `m_ePowerState` 4: the target set through `ActivatePower` held
  through the release. `m_vCastingStartLocation` is the caster's location (question 3: the power leaves from the pawn, not
  the hand). The woken mercenary didn't visibly react (no throw, no vortex in snapshots 0.5-3.8 s after): a dormant pawn's
  stasis or the release coming later than 3.8 s [verify on an awake encounter].
- **The marker shows "ready" between the push and the release** (the cooldown starts at the release): a cast in progress
  could dim it too [open].

### An awake encounter (2026-11-05, later)

- **Waking UNC20's mercenaries the level's way:** dormant means their behaviour is switched off (`BioActorBehavior.bActive`
  false, `m_nALLEnabled` 0), not only hidden. The level's own `BIOA_UNC20_00_DSG` `Main_Sequence.BioSeqAct_ModifyPropertyPawn_1`
  (its "Active" row is `BioPawnBehavior.SetActive`) wakes all five: `mevr instances BioSeqAct_ModifyPropertyPawn
  00_DSG.TheWorld.PersistentLevel.Main_Sequence.BioSeq`, then `mevr activate 1 0` (`ModifyPropertyPawn_0` does something else).
  Then they're hostile, armed and fought by the squad. `EnableAi <tag> None` didn't wake them, and `DebugSpawnActor
  BIOA_UNC_C.Mercenary.MIN_MercenaryHumanMaleRocketSoldier 1200` spawned nothing. Their type is that
  `BioPawnChallengeScaledType`.
- **Throw lands on the locked hostile:** the automatic lock took BioPawn_3 at 18 m; at the push the world's `TimeSeconds` was
  233.31, and the mercenary's `m_fRagdollStartTime` went from 181.20 (an earlier cast) to **234.94**, 1.6 s later (the cast
  animation, then about 1.4 s of flight at 1300 units/s), and it was thrown 3.3 m straight away from Shepard and 1 m up
  ((64853, -39474, -26736) to (65177, -39555, -26642)). The first Throw at it did the same (3.3 m away, 1 m up within 2 s),
  while the other mercenaries stood where they were. **Singularity** at it: the mercenary rose about 1 m over 3 s, then dropped
  at 4 s (the vortex's lift and release; not separated from its own movement by a clock).
- **The lock let go of the thrown hostile while it was ragdolled** ("target nothing" 3 s after the push): the scan skipped
  pawns without actor collision as dormant, and a ragdoll drops its cylinder's. Since this test the dormant test is the
  behaviour's `bActive` (and hidden), not the collision. **Checked the same night (f1cf826):** Throw at the locked BioPawn_3
  ragdolled it again (its ragdoll start matched the push) and threw it 2.5 m; polled every 0.5 s for 9 s, the lock stayed on it
  ("BioPawn_3 (automatic) ... in sight") through the ragdoll and its recovery.
- **Still to check** [verify]: a cast with the weapon away (question 2), the casting animation
  with arm IK (question 4), the push's thresholds with tracked hands in the headset, the marker's size at 5-35 m in the
  headset.
