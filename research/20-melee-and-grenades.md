# 20: Physical melee and grenades (2026-10-22)

The user wants melee and grenades to be physical, as in good VR shooters. Until now they were
buttons: B is the melee (pistol whip or rifle butt, with a cooldown), X the grenade (research/19's
binding table). The user pointed to Overlord (MW2 VR, GPL-3.0, `reference/Overlord`) for grenades.

This note covers:
1. how VR shooters and VR mods throw and strike (Overlord's code and docs, open frameworks, mods;
   web sources);
2. how LE1 starts a melee and a grenade, what aims a grenade, what the grenade projectile does,
   how melee picks its target, and the grenade count and cooldown (bytecode, packages, Ghidra);
3. the design built (`core/ik/Throwing`, `game/Throwables`), the settings and diagnostics;
4. what is still open.

**How sources are marked.** Web sources are listed at the end and in [sources.md](sources.md).
**[search summary]**: a search engine's summary of a page that didn't load directly. **[inference]**:
our reasoning. **[verify]**: not yet seen in game. LE1's files were read read-only and locally (as in
[18](18-camera-and-hands.md), "Method"); RVAs are for 2.0.0.48602.

## Summary

| Question | Answer |
|----------|--------|
| What starts the player's melee? | B is `Console_Melee` = **`SFXGameModeDefault.TryMelee()`** (exec). It returns if combat is disabled; does nothing while switching weapons, using a power, throwing a grenade or sprinting; **with the weapon away it only draws it**; else it ends a running shot, sets the current power to `Pistol_Whip` (pistol in hand) or `Rifle_Butt` (anything else) and acquires a `BioActivePower` |
| What starts the player's grenade? | X is `Console_Grenade` = **`SFXGameModeDefault.UseGrenade()`** (exec). It returns if combat is disabled or the player sprints; **with the weapon away it only draws it**; returns if the controller can't instigate; else acquires a `BioActiveGrenade` |
| What aims the grenade? | Not `GetAdjustedAimFor`. At the throw animation's `ThrowGrenade` notify, the native `BioActiveGrenade` asks the controller's **grenade interface** (`BioBasePlayerController.m_grenadeInterface`, a `BioGrenadeInterfacePlayer`) for a target point up to `m_fStickyGrenadeTraceDistance` = 250 m away, and calls the script event **`BioGrenade.LaunchGrenadeAtPoint(vTarget)`**. The disc's own intended target comes from a trace along the **camera's** view (`GetPlayerViewPoint` **[inference]**: an event on the instigator's controller with a location and a rotation out) |
| What flies? | **`BioGrenadeProjectileFrisbee`**, a "smart disc": straight line at `Speed` 1300 units/s (13 m/s), no gravity, **no world collision until within 50 units of its intended target point**, then it sticks to what it touches there. A shipped but unused **`BioGrenadeProjectileBallistic`** is a rigid body thrown with a velocity, exploding on a timer (3 s) or when a valid pawn is within 1 m |
| Can we set the direction and strength? | **Yes, after spawn.** `LaunchGrenadeAtPoint` is an event called through `ProcessEvent`, so our post-handler gets the projectile. The disc: start it at the hand, point it at where the hand's throw would land (the native `SetVelocity` keeps its speed), and make that point its intended target. The ballistic class takes a velocity as is (its native `SetVelocity` sets the speed to the vector's length) |
| How does melee pick its target? | It doesn't need one. The powers are cones in front of Shepard (`m_ImpactVolumeHalfAngle` **60 degrees**, `m_MaximumRange` **100 units pistol, 150 rifle**); the animation plays either way. Damage 16 / 20, force 400, cooldown **0.962 s / 1.38 s** (2DA) |
| Grenade count and cooldown | The count is the player squad's inventory resource 3 (`BioPawnBehavior.GetGrenades()`, decremented by `BioPlayerSquad.OnThrowGrenade`; the maximum from `BioInventory.GetMaxGrenades`). No grenade cooldown was found in the game-effects table; `BioPawn.IsThrowingGrenade()` blocks a second throw while one runs **[verify: a cooldown in game]** |
| Release velocity (VR practice) | The fastest moment of the last ~100-120 ms, smoothed over a few samples around it (SteamVR's AdvancedEstimation, HIGGS, Quake VR), not a plain average (which blends in the wind-up: the Rescuties write-up). Overlord uses the displacement over the last ~80 ms, capped at 15 m/s, times a gain of 2.5 |
| Throw or drop? | A minimum release speed: HIGGS 1.0 m/s, Quake VR 1.5 m/s. Overlord: before the pin is pulled, letting go returns the grenade to the chest |
| Melee detection (VR practice) | Hand speed relative to the head (walking and turning add nothing), about 3-3.5 m/s, plus a stroke's travel in one direction (8-45 cm), once per stroke, a cooldown 0.4-0.8 s (Overlord: fist 2.8 m/s / 8 cm, firearm 3.5 m/s / 12 cm, 400 ms; Quake VR 3.2 m/s; PLANCK 4.5 m/s, 0.55 s) |

## 1. How VR shooters throw and strike

### Overlord (MW2 VR, GPL-3.0), read closely

Docs: `docs/vr-grenades.md`, `docs/vr-cheat-throwables.md`, `docs/vr-melee.md` (chest equipment
and physical melee), `docs/vr-body-equipment.md`, `docs/vr-hand-interaction-runtime.md`. Code:
`src/client/component/vr/gameplay/grenade_state.hpp`, `grenade_policy.hpp`, `melee_motion.hpp`.

- **Where grenades live:** chest slots (tactical in the centre, lethal on the right, a knife on the
  left), 18 cm apart, 35 cm below and 4.5 cm ahead of the shared body anchor (the same estimated
  body frame as the holsters, research/19). Pickup boxes 20 x 16 x 32 cm. A 200 ms "fresh grip
  intent": an old held squeeze can't pick up by moving past.
- **Hands:** either free hand; an occupied hand can't take one; a central arbiter keeps a grenade
  grip from overlapping a gun, a magazine or another interaction.
- **Arming:** the other hand pulls the pin (trigger, 8 cm along the grenade). **Before the pin is
  pulled, letting go returns the grenade to the chest** (no ammo spent). After it, letting go
  throws (or drops) it. Cooking with B/Y. Tracking or focus loss: a safe grenade goes back, a live
  one drops with zero velocity.
- **The release velocity** (`release_motion`): up to 8 pose samples; a gap over 150 ms or a jump
  over 0.75 m starts over; the velocity is the displacement from the oldest sample within 80 ms of
  the newest, divided by the time, **capped at 15 m/s**. Then **a gain of 2.5** (`vr_grenadeThrowSpeedScale`),
  applied once at release. Zero motion stays zero; no synthetic impulse ever.
- **The native boundary:** the game's own projectile, spawned with the hand's position, velocity
  and remaining fuse; the engine owns trajectory, collision, damage and AI reactions. Diagnostics
  report raw, scaled and admitted speeds.
- **Melee** (`melee_motion.hpp`): per contact point, speed and travel (a stroke's displacement;
  reversing direction starts a new stroke, so oscillation never adds up). Thresholds: knife 1.0 m/s
  / 4 cm, **fist 2.8 m/s / 8 cm, firearm 3.5 m/s / 12 cm**, shield 3.0 m/s / 10 cm. One player-wide
  **400 ms cooldown**, and a used contact must be left before another strike. Tracking validation:
  hand translation over 16 m/s or rotation over 35 rad/s invalidates a sample; history older than
  120 ms, recentering or a grip change can't create an attack; player translation and snap turning
  alone don't arm a strike. Native melee damage, sound and effects on a confirmed contact.

What we take, in our own code (no Overlord code copied): the shared body frame for the slot; a new
squeeze only; a slow release puts the grenade back; the velocity capped then multiplied by a gain
once; the game's own projectile, spawned by the game; speed plus stroke travel with direction
reversal resetting the stroke; one cooldown; one pulse per event. What we don't: pins and cooking
(ME1's grenades have no fuse to cook: they stick and blow), contact traces against NPCs (ME1's
melee is the game's cone power; we trigger it).

### Throwing elsewhere

From open code and docs (see the sources at the end):
- **SteamVR Interaction System** (`Throwable.cs`, `VelocityEstimator.cs`): the default takes the
  runtime's velocity 11 ms in the past; `AdvancedEstimation` keeps 30 updates, finds the fastest of
  the **last 10 frames**, averages **2 frames** around it; the release velocity is scaled by **1.1**.
- **Unity XR Interaction Toolkit:** smoothing over **0.25 s** (at most 20 frames), velocity scale
  **1.5**.
- **Meta Interaction SDK:** RANSAC over **10 samples**, excluding the **2 newest** (unreliable near
  the release). Meta's Unreal sample looks back **40 ms** for input and render latency, and blends a
  low-confidence throw's direction toward the head's forward.
- **HIGGS (Skyrim VR):** the fastest of the last 5 samples averaged with its 2 neighbours; below
  **1.0 m/s** a release is a gentle drop, not a throw.
- **Ultraleap:** a 50 ms window ending 20 ms before the release; a gain from 1.0 at 0 m/s to 1.5
  at 3 m/s.
- **Quake VR:** the peak speed within **0.12 s** before the release, smoothed ±0.017 s; direction
  from the 40 ms before the peak; an **analog early release** (the grip lets go when it drops 0.3
  below its peak while the hand moves faster than 1.5 m/s, "the runtime's grip button lets go
  late"); throws below 1.5 m/s are drops.
- **Rescuties (Game Developer write-up):** a moving average made throws feel "underwater" (it
  blends in the wind-up); the peak of recent frames got the speed right but a noisy direction; a
  linear regression over the last few frames was adopted.
- **Valheim VR:** for a game-owned projectile, `FullThrowSpeed` 5 m/s is the hand speed at which the
  game's projectile reaches its full speed: the closest analogue to ME1, whose grenade is the game's.
- **Perception:** release-point errors of ±10 ms go unnoticed (FauxThrow, arXiv 2208.02166).
- **Not found:** throw maths for Boneworks/Bonelab, H3VR, Pavlov, Onward, Contractors, Arizona
  Sunshine.

### Grenades in VR shooters

- **Onward:** grenades on the chest, taken with the trigger; the other hand pulls the pin; a stick
  click starts the 5 s fuse; releasing the trigger throws (UploadVR guide).
- **Pavlov:** a grip click takes one from the vest; trigger to ready, the other hand pulls the pin,
  releasing throws (Steam thread, 2018; may be outdated).
- **Half-Life: Alyx:** primed by a button, 4-5 s; enemy grenades can be caught and thrown back.
- **Medal of Honor: Above and Beyond:** pins and real throws, no landing indicator **[search summary]**.
- **Forefront:** a stick selects the range, plus a hard enough throw; a player called it
  "unnecessarily difficult" (BetaHub bug report).
- **Returning a weak throw to the pouch:** not found in any game; Overlord's "let go before the pin
  returns it" is the closest.

### Melee elsewhere

- **PLANCK (Skyrim VR):** hand speed averaged over 56 ms; a swing arms above **4.5 m/s** and fires
  at the speed peak; the hand's velocity must not point backwards from the head's forward (dot >
  -0.8); active 0.2 s, **cooldown 0.55 s**; a failed swing gives a long buzz.
- **Quake VR:** **3.2 m/s relative to the head**; a gun-butt strike needs **0.45 m of travel within
  0.3 s**; an anti-waggle rule (the wrist must average 1.1 m/s over 0.12 s); its recorded tests
  treat aiming, walking with a gun and reloading as "no hit".
- **Valheim VR:** 3 m/s at a point 12.5 cm along the weapon; a hit haptic of 0.2 s, 100 Hz, 0.5.
- **Skyrim VR / Fallout 4 VR (vanilla ini):** `fMeleeLinearVelocityThreshold` 2.0 (Skyrim; players
  raise it to 4 to stop attacks while walking) and 200 game units (Fallout 4, about 2.9 m/s
  **[inference]**); snap turning and locomotion add velocity and cause unwanted swings.
- **Onward:** the knife deals damage only with the trigger held (no accidental stabs).
- **Half-Life: Alyx:** melee was cut (no force feedback; it never felt right).
- **Impact haptics:** HIGGS fires from 0.2 m/s, a 10 ms pulse scaled by speed; a D2R PCVR PR uses a
  sharp 10 ms pulse on hit.

### Accessibility

Meta's checklist (playable one-handed, everything reachable seated, an alternative to gestures),
the Game Accessibility Guidelines (key actions doable with digital controls; gestures only as an
alternative), Ultraleap and Owlchemy (no exact timing, simpler gestures available). No shipped VR
shooter documented a button fallback for melee or grenades specifically; the guidelines ask for one.
**So B and X stay, behind settings.**

## 2. How LE1 starts a melee and a grenade

Sources, all read-only and local: `SFXGame.pcc`, `Engine.pcc`, `Core.pcc` decompressed with the
game's Oodle DLL; a scratch UE3 bytecode disassembler (UE3's token set, object references 4 bytes on
disk and 8 in memory: every function it read parsed to its exact in-memory size, and natives are
named from `Core`'s and `Engine`'s native indices); the 2DA tables in `Engine.pcc`
(`BIOG_2DA_GameProperties_X.GameProperty_GameEffects`, `BIOG_2DA_Talents_X.Talent_TalentEffectLevels`);
GhidraMCP on the LE1 project and the natives dump. Nothing extracted is kept or committed.

### The bindings

From research/19 section 6 (`BIOInput.ini` in `Coalesced_INT.bin`, `SFXGameModeDefault.Bindings`):
B = `Console_Melee` = `TryMelee`, X = `Console_Grenade` = `UseGrenade`, both execs on
`SFXGameModeDefault` (on foot). We call the same execs through `ProcessEvent` on the controller's
`SFXGameModeDefault`, as holsters call `SwitchToWeapon`.

### `SFXGameModeDefault.TryMelee()` (bytecode)

```
if (!BioPlayerInput(Outer.PlayerInput).IsCombatEnabled()) return;
MyBP = SFXPawn_Player(Outer.Pawn);
if (MyBP != None) {
    if (MyBP.IsSwitchingWeapons() || MyBP.IsUsingPower() || MyBP.IsThrowingGrenade() || Outer.bStorming) return;
    if (!MyBP.IsInCombatActionState()) { MyBP.DrawPrimaryWeapon(); return; }       // weapon away: only draws
    if (BioActiveShoot(MyBP.m_oBehavior.m_oCurrentModalActive) != None)
        MyBP.m_oBehavior.ReleaseActiveByClass(class'BioActiveShoot');               // stops a burst
    if (MyBP.m_oBehavior.m_oEquipment.m_eQuickSlotCurrent == 0)                     // the pistol
        MyBP.m_oBehavior.SetCurrentPower('Pistol_Whip', Cooldown);
    else
        MyBP.m_oBehavior.SetCurrentPower('Rifle_Butt', Cooldown);
    MyBP.m_oBehavior.AcquireActive(class'BioActivePower');
}
```

(`SetCurrentPower(name, out float fCooldownRemaining)` is native; TryMelee ignores both its result
and the cooldown it reports. The power's own cooldown applies inside the power.)

### The melee powers (2DA)

`GP_MeleeCoreGFX` (talent `Player_Squad_Powers`), the same at every level:

| Power | Cone half-angle | Range | Cooldown | Damage | Force | Kickback | Cast time |
|-------|-----------------|-------|----------|--------|-------|----------|-----------|
| `Pistol_Whip` (pistol) | 60 degrees | 100 units | 0.962 s | 16 | 400 | 0.4 | 0 |
| `Rifle_Butt` (shotgun, rifles) | 60 degrees | 150 units | 1.38 s | 20 | 400 | 0.7 | 0 |

Damage rises with Assault Training (+30 to +64%) and for krogan (+50 to +175%). The cone is the
power's `m_ImpactVolumeHalfAngle`: **melee needs no target**; it hits whatever is in the cone when
the animation releases it. Which direction the cone faces (the pawn's rotation, or the aim) is
**[verify]**.

### `SFXGameModeDefault.UseGrenade()` (bytecode)

```
if (!BioPlayerInput(Outer.PlayerInput).IsCombatEnabled()) return;
if (Outer.bStorming) return;
MyBP = SFXPawn_Player(Outer.Pawn);
if (MyBP != None) {
    if (!MyBP.IsInCombatActionState()) { MyBP.DrawPrimaryWeapon(); return; }   // weapon away: only draws
    if (!Outer.IsAbleToInstigate()) return;
    MyBP.m_oBehavior.AcquireActive(class'BioActiveGrenade');
}
```

### The grenade active (native, GhidraMCP)

`BioActiveGrenade` has no script beyond `GUIState`; its code was found through the fields it uses
(the pawn's `m_eGrenadeThrowAnimationState`, `+0xA44`, and the active's own ballistic fields):

- **Update** (`0xBF0DE0`): with `m_state` 1, it calls a script event on `m_stuckGrenade` and clears
  it (a detonation of the stuck disc **[inference]**). Otherwise, once: `0xBF0250` sets the pawn's
  `BioGrenade` (behavior `+0x3B4`) `m_clsProjectile` to **`BioGrenadeProjectileFrisbee`** (to
  `BioGrenadeProjectileBallistic` when a ballistic target marker exists) and starts the throw
  animation. With `m_isBallisticGrenadeEnabled` (off in `Default__BioActiveGrenade`) a prototype
  mode instead ramps `m_fBallisticGrenadeSpeed` from 1000 to 5000 units/s at 200/s after 0.5 s,
  pitches the aim up by `m_fBallisticGrenadePitchOffset`, and draws a debug arc to a marker actor
  (`0xBF1F10`, the world's line batcher).
- **The animation states** (`0xBF1BD0`) follow the pawn's notifies (`BioPawn.StartThrowGrenade`,
  `CreateGrenade`, `ThrowGrenade` set `m_eGrenadeThrowAnimationState` 0, 1, 2):
  - 1 shows the `BioGrenade`'s mesh (`+0x3D0`) in Shepard's hand;
  - **2 hides it and launches**: the grenade interface (`0xBC6AF0`: the controller's
    `m_grenadeInterface`, `+0x7BC` on `BioBasePlayerController`, `+0x5B4` on `BioAiController`)
    is asked for a target point within `m_fStickyGrenadeTraceDistance` (25000 units; its vtable
    `+0x2C0`), then **`ProcessEvent(BioGrenade, LaunchGrenadeAtPoint, {vTarget})`**. With a
    ballistic marker instead: `LaunchGrenade(velocity)`, the velocity from the interface's rotation
    (`+0x2C8`, else the pawn's) pitched up, times the ramped speed (`0xBC6A50`). `LaunchGrenadeAtActor`
    is never called for the player. The FName globals are `0x141696900`, `908`, `910` (alphabetical:
    `LaunchGrenade`, `...AtActor`, `...AtPoint`) **[inference]**;
  - if the returned projectile is a disc it becomes `m_stuckGrenade`; then a behavior `+0x19C`
    virtual (the squad's `OnThrowGrenade`, which takes one grenade from the inventory
    **[inference]**).
- The interface's own `+0x2C0` (what it traces along: the camera or the aim) wasn't located: its
  vtable has no name. Our launch handler logs the game's target next to ours **[verify]**.

So **`GetAdjustedAimFor` (our hand aim) doesn't aim grenades**, and the throw happens at the
animation's notify, some time after `UseGrenade` (measured by the mod: `launched N s after`).

### The projectiles (bytecode, defaults, GhidraMCP)

`BioGrenade.MakeProjectile` spawns `m_clsProjectile` at `GetPhysicalFireStartLoc()` (native
`GetLaunchPoint`) with the owner's rotation, sets `Instigator`, and calls `Initialize(damage, item)`.
`LaunchGrenadeAtPoint(vTarget)` then calls the projectile's `SetTargetPoint(vTarget)`;
`LaunchGrenade(vVelocity)` its `SetVelocity(vVelocity)`. Both are script events: our `ProcessEvent`
post-handler sees them with the projectile in `ReturnValue`.

**`BioGrenadeProjectileFrisbee`** (the grenade players know; vtable `0x1312C78`), defaults: `Speed`
1300 (inherited), `LifeSpan` 10, **`bCollideWorld` false**:
- `SetTargetActor` (`0xC50830`): `m_oHeatSeekTarget` = the actor, then `SetTargetPoint` at it.
- **`SetTargetPoint`** (`0xC509D0`): `SetVelocity(target - Location)`; for the player, calls
  an event with a location and a rotation out on its `InstigatorController` (`GetPlayerViewPoint`
  **[inference]**), projects the pawn onto the view ray, and
  traces along the **view direction** (a world trace helper, vtable `+0x2F0`) to set
  `m_oIntendedTarget` and **`m_vIntendedTargetLocation`**; `m_vStartLocation` = its location. (For
  an AI it traces along the pawn's rotation from its aim node.)
- **`SetVelocity`** (`0xC516E0`): the direction normalized, times `Speed`; the rotation to match;
  `setPhysics(PHYS_Projectile)`. A straight line at constant speed.
- **Script `Tick`:** with a heat-seek target, re-aims at it each tick; **`bCollideWorld` becomes true
  within 50 units of `m_vIntendedTargetLocation`**. So the disc passes through anything before its
  intended point.
- **`DoTouch`** (`0xC40870`): ignores touches nearer the start than the intended point (unless it's
  the intended actor), then the script `Stick`: attaches through the actor's sticky-grenade
  interface, stops, makes noise. It explodes when `LifeSpan` runs out (`ShouldExplode`), or on the
  active's detonation **[verify which, in game]**.

**`BioGrenadeProjectileBallistic`** (vtable `0x1312430`), defaults: `m_fProximityRadius` 100,
`LifeSpan` 3 (from `BioGrenadeProjectile`), physical material restitution 0.65:
- **`SetVelocity`** (`0xC51540`): `Velocity` = the vector, `Speed` = its length, a random spin,
  **`setPhysics(10)`** (rigid body).
- `SetTargetPoint` (`0xC50880`): a ballistic solution (flight time from the horizontal distance over
  the class's default speed, the physics volume's gravity). `SetTargetActor` leads a moving target.
- `ShouldExplode`: `LifeSpan` over, or a valid `BioPawn` within `m_fProximityRadius`.
- Never spawned in normal play (the ballistic mode is off) **[verify that it flies]**.

### The count and the cooldown

- `BioPawnBehavior.GetGrenades()` (script event): the player squad's
  `GetInventoryResourceInt(3)`. `AdjustGrenades(n)` changes it; `BioPlayerSquad.HasGrenades(pawn)`
  is the squad's capability and a count above 0; `BioPlayerSquad.OnThrowGrenade(pawn)` takes one.
  The maximum: `BioInventory.GetMaxGrenades()`.
- No `GE_Grenade*CooldownTime` exists in the game-effects table (the melee powers have theirs), and
  `UseGrenade` checks no cooldown. `BioPawn.IsThrowingGrenade()` is true while a throw runs
  **[verify in game: any grenade cooldown]**.

## 3. The design (built 2026-10-22)

### Grenades

- **The slot:** `[Throwables] slot = -0.13, -0.33, 0.00, 0.10` (right, up, forward, radius, meters
  from the upright eye at a 1.65 m eye height): the left side of the chest, GoldenEye VR's chest
  slot mirrored (12.9 cm to the side, 32.7 cm below the eyes, 10 cm radius; research/19) and near
  Overlord's chest. Clear of the belly (shotgun) slot and the left shoulder. The body frame is the
  holsters' (yaw from arm IK's torso or a lagging head yaw, held while a hand is in the slot or holds
  a grenade; the anchor the upright eye with an 80 ms height filter; the scale from the standing eye
  height the holsters measure). Contact haptics as a holster's (research/19 section 7).
- **The grab:** a **new** squeeze of either hand inside the slot, when no grenade is held. The left
  grip's squeeze counts only if the host's pad mapping says it began **away from the gun** (else
  it's the two-handed grip): position decides. Such a squeeze is **claimed** before holsters and the
  squad menu tick: the squad menu ignores it (no wheel), holsters see it as a squeeze away from every
  weapon slot. Refused (a soft buzz, a log line) with no grenades left, combat disabled, sprinting,
  a throw still running, or the last throw not launched yet.
- **The right hand** can throw too (`hands = both`): while it holds a grenade, and after it threw
  or put one back until the right grip squeezes again, holsters treat the grip as still holding the
  gun (no return to the holster under the throw).
- **ME1 throws only with a weapon out:** taking a grenade with the weapon away calls `UseGrenade`
  once, which only draws it (`drawForThrow = 1`); a throw released before the draw is done waits
  for it (up to 2 s).
- **While held:** a faint continuous buzz (`holdBuzz`). **What shows in the hand:** nothing for now.
  Proposal: the game shows Shepard's grenade mesh (`BioGrenade.m_Mesh`) during its own throw
  animation (state 1); attaching it to Shepard's IK'd hand bone while held would show it in the VR
  hand, but the active's states hide and show it themselves, so it needs its own care **[open]**.
- **The release:** the analog grip lets go at 0.4, or early when it drops 0.3 below its peak while
  the hand moves faster than 1.5 m/s (Quake VR; `earlyRelease`). The velocity: the fastest moment
  of the last 120 ms (each a least-squares slope over ±20 ms), smoothed ±30 ms (`ik::throwing::
  EstimateRelease`). Sample times come from the controller sample sequence at the learned frame
  period (`SampleClock`), not the game tick's clock.
- **Throw or put back:** under `throwSpeedMinMS` (1.5) it goes back (a stow click). Else: the speed
  capped at 15 m/s (Overlord), times `throwGain` (2.5, Overlord), mapped into the world with the
  view anchor, and its arc (real gravity, at the view's scale) traced with `Actor.Trace` from the
  hand: the first hit is where the throw lands. Then `UseGrenade` (a whoosh pulse).
- **The launch** (post-handler of `LaunchGrenadeAtPoint` / `AtActor` / `LaunchGrenade`, only for the
  player's `BioGrenade` and only while our throw awaits it):
  - **`flight = disc` (default):** the disc is moved to the hand (`SetLocation`, `fromHand`), sent
    toward the landing point (`SetVelocity`: its own speed), and the landing point and actor become
    its intended target (no homing). It flies straight there through anything in between (the
    game's own disc does that too) and sticks there: a lob over cover lands behind it.
  - **`flight = arc` (experimental):** `BioGrenade.LaunchGrenade(velocity)` with `m_clsProjectile`
    switched to the ballistic class for the call; moved to the hand; the disc destroyed; the
    launch's return value replaced so the active doesn't keep the destroyed disc. A real thrown
    object, but a code path the game never uses.
- A grenade thrown with X (no physical throw waiting) is left as the game aimed it (logged once).

### Melee

- **The stroke** (`ik::throwing::SwingDetector`, one per hand): the grip pose relative to the head
  (walking, crouching and snap turns add nothing), its velocity a slope over 40 ms; a stroke is the
  motion above the speed (`meleeSpeedMS` 3.0 for a free hand, `meleeGunSpeedMS` 3.5 for the gun
  hand: Overlord's fist and firearm); a sharp turn (direction changes by more than 60 degrees)
  starts a new stroke, so jitter never adds up; once the stroke has travelled `meleeTravelM` (0.12)
  it is judged once: not backward against the body's front (a pull back), not suppressed, not in
  the cooldown (`meleeCooldownS` 0.8).
- **Suppressed:** the hand holding a grenade, and both hands for 0.5 s after a grenade is let go (a
  throw is a fast forward stroke and moves the whole body); the right hand while its trigger is pulled, while it's in a weapon slot, or
  within 0.5 s of a holster draw; the left hand on the foregrip (the right hand's stroke counts
  then).
- **The game's side:** combat enabled, the weapon out (with it away ME1's melee only draws it: no
  strike), not sprinting, switching, using a power or throwing; and with `meleeNeedsEnemy` (default)
  a living hostile `BioPawn` (`IsHostile`, `IsDead`, through `WorldInfo.PawnList`) within 2 m and
  75 degrees of the body's front (the game's cone is 1-1.5 m and 60 degrees from Shepard's centre).
  Then `TryMelee()` and a tick on that hand. The target's health (`GetPercentHealth`) is watched
  for 1.2 s: a drop is a hit (a thump, 70 ms, full strength) and logged; none is logged too.

### The buttons

`meleeButton = 1`, `grenadeButton = 1` (default): B and X stay. With 0 the host's pad mapping keeps
B or X from the game **on foot only** (menus keep B), through `gamepad::kFlagNoMeleeButton` /
`kFlagNoGrenadeButton` (HostServices v14; an older host ignores them and the mod logs it once). B
or X as the power wheel's RB source stays RB.

### Diagnostics

Logged once per event (`throwables:` lines): settings; entering and leaving the slot; a grab (hand,
grenades left, the draw); refusals and why; the throw (hand speed, the peak's time before the
release, samples, tracking direction, pitch, world velocity, where it lands and on what); the
`UseGrenade` call and whether Shepard throws; the launch (delay after the release, the game's
target, what was done: moved, re-aimed, distance, speed); grenades the game threw its own way;
whether `Actor.Trace`'s hit point came back through `ProcessEvent` (else bisected); melee strokes
judged (speed, travel, front component) and refusals with reasons, the strike and its target, the
hit or no damage. A heartbeat line (grabs, throws, put back, refusals, launches, strikes, hits,
peak hand speeds, grenades left); `throwables` in the snapshot's mod status.

### Tests (offline, `tests/Main.cpp`)

`TestThrowVelocity` (constant velocity, the peak of a throw that slows before the late release, the
sequence clock under uneven observation), `TestThrowGrip` (early release), `TestGrenadeMachine`
(grab only on a new squeeze in the slot, refusal, one holder, exit hysteresis, throw, put back,
tracking loss, a hand not allowed), `TestSwing` (slow aim, one strike per punch, a pull back
refused, the cooldown, a sideways swipe, jitter, suppression, a tracking glitch), `TestArcPoints`,
`TestFaceButtonsOff` (B and X kept from the game, RB still RB, the flags).

## 4. Open

- **[verify]** In game: the launch delay after `UseGrenade` (the animation); that the disc re-aimed
  from the hand flies and sticks where the throw lands; the game's own target vs ours (the logs);
  `Actor.Trace`'s hit point through `ProcessEvent`; the ballistic class (`flight = arc`) at all.
- **[verify]** Melee: which way the game's cone faces (Shepard's facing follows Halcyon's camera
  and our arm IK; a strike to the side may whiff); the reach and cone gate; false strikes while
  aiming and reloading; the health-drop hit detection.
- **[verify]** A grenade cooldown; what blows the disc up (its `LifeSpan` or the detonation).
- Showing the grenade in the hand while held (above).
- The throw animation: Shepard's upper body plays it while arm IK holds the arms; how that looks.
- Seated play: the slot is within seated reach (the chest); melee speeds can be lowered in the ini.
- Left-handed play: not supported in the mod yet; the slot would mirror.

## Sources (consulted 2026-10-22)

- Overlord, read locally: `docs/vr-grenades.md`, `docs/vr-cheat-throwables.md`, `docs/vr-melee.md`,
  `docs/vr-body-equipment.md`, `docs/vr-hand-interaction-runtime.md`;
  `src/client/component/vr/gameplay/grenade_state.hpp`, `grenade_policy.hpp`, `melee_motion.hpp`.
- SteamVR Unity plugin: [Throwable.cs](https://raw.githubusercontent.com/ValveSoftware/steamvr_unity_plugin/master/Assets/SteamVR/InteractionSystem/Core/Scripts/Throwable.cs),
  [VelocityEstimator.cs](https://raw.githubusercontent.com/ValveSoftware/steamvr_unity_plugin/master/Assets/SteamVR/InteractionSystem/Core/Scripts/VelocityEstimator.cs)
- Unity XRI: [XRGrabInteractable.cs](https://raw.githubusercontent.com/needle-mirror/com.unity.xr.interaction.toolkit/master/Runtime/Interaction/Interactables/XRGrabInteractable.cs),
  [API docs](https://docs.unity3d.com/Packages/com.unity.xr.interaction.toolkit@2.5/api/UnityEngine.XR.Interaction.Toolkit.XRGrabInteractable.html)
- Meta: [RANSACVelocity](https://developers.meta.com/horizon/reference/interaction/v72/class_oculus_interaction_throw_r_a_n_s_a_c_velocity),
  [Unity throwing sample](https://developers.meta.com/horizon/documentation/unity/unity-sample-isdk-throwing),
  [Unreal HandGameplay ThrowingComponent](https://raw.githubusercontent.com/oculus-samples/Unreal-HandGameplay/main/Plugins/OculusHandTools/Source/OculusThrowAssist/Private/ThrowingComponent.cpp)
- HIGGS: [config.h](https://raw.githubusercontent.com/adamhynek/higgs/master/include/config.h), [hand.cpp](https://raw.githubusercontent.com/adamhynek/higgs/master/src/hand.cpp);
  PLANCK: [config.h](https://raw.githubusercontent.com/adamhynek/activeragdoll/master/include/config.h)
- Quake VR: [vr_cvars.inc](https://raw.githubusercontent.com/vittorioromeo/quakevr/master/Quake/vr/vr_cvars.inc),
  [THROWING.md](https://raw.githubusercontent.com/vittorioromeo/quakevr/master/docs/vr-port/THROWING.md) (GitHub reports GPL-2.0: numbers and concepts only)
- Valheim VR: [VHVRConfig.cs](https://raw.githubusercontent.com/brandonmousseau/vhvr-mod/master/ValheimVRMod/Utilities/VHVRConfig.cs),
  [WeaponCollision.cs](https://raw.githubusercontent.com/brandonmousseau/vhvr-mod/master/ValheimVRMod/Scripts/WeaponCollision.cs)
- Ultraleap: [SlidingWindowThrow.cs (npm mirror)](https://cdn.jsdelivr.net/npm/com.sxer.thirdpart.leapmotion@0.0.1/Ultraleap/Tracking/Interaction%20Engine/Runtime/Scripts/Internal/SlidingWindowThrow.cs)
- [Game Developer: why throwing in VR sucks (Rescuties)](https://www.gamedeveloper.com/design/why-throwing-in-vr-sucks-and-how-to-make-it-better)
- [FauxThrow, arXiv 2208.02166](https://ar5iv.labs.arxiv.org/html/2208.02166)
- [UploadVR: Onward guide](https://uploadvr.com/onward-guide-tips-strategies/), [Steam: Pavlov grenades (2018)](https://steamcommunity.com/app/555160/discussions/0/3307213006845757086),
  [Combine OverWiki: Half-Life: Alyx grenade](https://combineoverwiki.net/wiki/Grenade_(Half-Life:_Alyx)),
  [Game Informer: Medal of Honor VR](https://gameinformer.com/news/2019/09/25/medal-of-honor-above-and-beyond-takes-the-franchise-to-vr) (search summary only),
  [BetaHub: Forefront bug #133](https://app.betahub.io/projects/pr-7682550116/issues/133)
- [Kotaku: why you can't use a crowbar in Half-Life: Alyx](https://kotaku.com/why-you-cant-use-a-crowbar-in-half-life-alyx-1842491319),
  [Steam: Fallout 4 VR melee threshold](https://steamcommunity.com/app/611660/discussions/0/1635237606656502416/),
  [Steam: Skyrim VR melee threshold while walking](https://steamcommunity.com/app/798810/discussions/0/3288067088100079550/),
  [D2R PCVR PR #69](https://github.com/bearstonem/d2r-pcvr/pull/69)
- Accessibility: [Meta accessibility checklist](https://developers.meta.com/vr/design/accessibility-checklist/),
  [Game Accessibility Guidelines](https://gameaccessibilityguidelines.com/full-list/),
  [Ultraleap VR gaming accessibility](https://docs.ultraleap.com/ultralab/vr-gaming-accessibility-recommendations.html),
  [Owlchemy via Unity](https://unity.com/blog/games/accessible-vr-game-design-tips-from-owlchemy-labs)
- Not found: throw maths for Boneworks/Bonelab, H3VR, Pavlov, Onward, Contractors, Arizona Sunshine;
  melee numbers for Blade & Sorcery, Pavlov's gun bash, Arizona Sunshine 2, Into the Radius, Medal of
  Honor; a shipped VR shooter that returns a weak throw to the pouch.
