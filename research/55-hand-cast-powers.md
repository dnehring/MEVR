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
  mechanic choice: no pause, a pause, or slow time).
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
