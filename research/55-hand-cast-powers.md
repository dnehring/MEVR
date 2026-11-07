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

- **Right stick click** (`[Holsters] powerWheel = r3`): a tap was RB tapped, the game's mapped power
  (`SFXGameModeDefault.TryUseMappedPower`; since 2026-11-05 a tap casts nothing: research/19 "Hold only"); held, it opens
  the game's power wheel (`EnterPowerWheel`, game mode 2),
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
2. What a cast with the weapon away does (the mapped power draws first; `ActivatePower` doesn't check). **Answered
   2026-11-05 (section 9): the cooldown is spent and nothing happens, unless the mod sends the release.**
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

### The omni-tool's projection (2026-11-05, evening; 03a6a08)

The user, seeing the squad wheel (game/SquadPlace, drawn by the same code) in the game: "it looks very flat and cheesy, it doesn't
look like something that would be in mass effect". Mock-ups were made first, offline, from the game's own art (its main wheel,
`GUI_SF_InGameGui.PC_MainWheel`, rendered by `mevr-gfx-dump`; the player's own cache, never committed). The user's answers, in
order: the power wheel's radial tiles stay (deliberately unlike the game's horizontal bars: room for the hand to travel into a tile,
and an icon); "use the games own font"; icons "not too cartoony"; "like it could be projected from the omni tool"; no centre ring;
the selected tile must "change color"; the icons holographic too; and the projector not "a flat line going through the middle".
The look now (plugin/PowerWheelDraw, both wheels):
- **Tiles** are thin orange light over see-through glass, the omni-tool gauntlet's hologram colours (game/OmniTool's, measured from
  the eye image: mid (233, 147, 81), core (255, 205, 103)); **the chosen tile turns ME1's cyan** (the game's UI lines (125, 211, 232)
  family), with white text and icon. Disabled tiles dim, a cooling tile keeps its veil and its partly lit border.
- **No rings, spokes, N7 mark or chevrons.** The light comes from an **emitter in the wheel's gap** (where the forearm and the gauntlet
  are: lower left on the power wheel, straight down on the squad wheel): a soft cone to each tile's inner edge, brightest at the
  source, thin rays along its sides, dust drifting outward with time.
- **The names are the game's own Biomass**, built from the player's game-art cache (`GUI_SF_Fonts`, copied by game/GameArt) through
  core/gfx, in capitals; Rajdhani when the cache is missing. Biomass's W had drawn as "\\A" since the rail used it: its advance,
  35,000 in DefineFont3's 20x units, overflows the spec's SI16 and was read negative (core/gfx reads it unsigned now, 0454c48).
- **The icons are holograms** (`power_icons::Hologram`): the painted picture's bright core strokes only, as white light the tile
  tints (orange, cyan when chosen): bright edges over a see-through body fading downward, a halo, a faint ghost a few pixels up,
  scanlines. The pictures behind them are still ours; the game's own power icons live in `GUI_SF_HUD`, not cached yet.
- The hint card and the lock marker take the palette (the marker cyan when ready, orange while cooling); the hint keeps its panel.
Checked offline in `mevr-power-wheel-preview` (every state); in the game the drawing is the same code that drew the squad wheel at
20:18 (positions and pick checked then).

### The light from the omni-tool, alive (2026-11-06)

The user, in the headset: the "central light point which projects outward is static and it needs to be more dynamic and actually
connect to the omni-tool". The emitter had been a fixed point in the wheel's gap, so it stayed where the forearm was when the wheel
opened, while the hand moved to pick.
- **The source is the omni-tool's display on Shepard's hand.** game/PowerWheel reads it (`omni_tool::DisplayWorld`, the
  `OmniHand_Loop` particle system's transform: the gauntlet the wheel already wears) each tick, converts it to tracking space, and
  sends it in the left grip's frame (`View::source`, `sourceValid`; the View grew, so host and mod deploy together). A display more
  than 30 cm from the grip is ignored, because its first transforms are the animation's arm. Each frame plugin/PowerWheel puts that
  offset back on the grip the host has (`FromFrame`), so the light moves with the hand without the game tick's delay. It then takes
  the point where the eyes see it on the wheel: `SeenInPlane`, the line from the head through the display meets the wheel's plane;
  straight across when that line runs within 5 degrees of the plane or meets it behind the eye; unit-tested. Fallbacks: no display
  on the arm, the left grip itself; no controllers, the console's hand (`mevr powerwheel hand x y`); else the gap as before. The
  host logs `power wheel: its light comes from ...` and the mod `... #N its light comes from the omni-tool's display, (x, y, z) cm
  from the left grip`. The left-hand-mode branch (research/61) publishes the off-hand as `State::left` and moves the gauntlet to
  it, so this follows without change.
- **It moves.** The cones shimmer in five bands across, each breathing on its own clock. Soft pulses of light run out along each
  cone to its tile: one at a time, slow, to the others; two, faster and brighter, to the chosen tile. The rays along the sides
  flicker. At the source a core breathes inside two rings of arcs turning opposite ways, and a ring flares out from it when the
  pick changes. The dust still drifts.
Checked offline (`mevr-power-wheel-preview`'s `source-*.png`: the light at the hand, moved toward Throw, a new pick, and four
frames 0.1 s apart). Not run in the game yet: the fake headset has no tracked controllers, so a check there needs a replay with the
left hand (and `mevr powerwheel open`). How it looks over the real hand is for the headset.

### How to cast it, on a card (2026-11-06)

The user: "we may need instructions to show the gestures and how to actually use the powers". The wheel's hint card said how to pick
("pinch/close to equip", which no longer matched: letting go of X equips), and the menu's controller guide still had X as the grenade
button and nothing on the lock, the push or the self-cast gestures. Now there are two kinds of help:
- **At the moment it's needed, in the room.** After an equip, a card stands where the wheel was, 8 cm above its centre, on the hint
  card's canvas (no extra swapchain). It shows the power's name in the game's capitals with "equipped", the gesture drawn in the
  omni-tool's hologram lines and looping every 1.8 s, and two lines on how to cast it. It goes at the cast (push or gesture), after
  8 s, at the next opening, or off foot. It shows for the first `[PowerWheel] coach` equips (3) of each power per session; 0 turns
  it off. Which card a power gets (`vr::power_wheel::Coach`, `CoachFor`, `CoachLines`; unit-tested): its self-cast gesture when it
  has one and `selfGestures` is on (Chest: the hand comes to the chest and a ring fills while it's held; Gun: the hand drops onto a
  pistol's top, which lights; Raise: the hand goes up over the head and the ring fills), else how it aims (Target: brackets on an
  enemy, the hand rises toward them and pushes; Place: a ring on the ground in their place; Forward: a push straight on). The mod
  sends it in the View (`coach`, `coachFor`, `coachName`; `Latest` clears it once stale), the host draws it (`CoachLayer`,
  `draw::DrawCoach`). Logs `power wheel: how to cast <power> shown (<kind>; n of N this session)`, `... gone (<why>, after s)` (mod)
  and `... drawn (<kind>) where the wheel was` (host).
- **For reference, in our menu.** The controller guide's X reads "Power wheel" while `[PowerWheel] on` (the hand's wheel, with a
  note that grenades come from the chest), and a new Powers group has three rows: Pick a power, Cast it (the lock and the push, place
  powers where the hand points, the gun kept in the right hand), Powers on yourself (the three gestures and their powers). Each falls
  back to the game's power wheel when its setting is off (`GuideInput::handWheel`, `castLock`, `selfGestures`, from `[PowerWheel]`).
- The wheel's own hint card now says: move your left hand toward a power to pick it, then let go of X to equip it; a card then shows
  how to cast it.
Checked offline: `mevr-power-wheel-preview`'s `coach-<kind>-<0..2>.png` (each gesture at three points of its loop) and
`mevr-menu-preview`'s guide page. In the headset: whether the card stands in a good place after the wheel closes, and whether the
drawings say the gestures well enough on their own.

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
  push casts (section 7). Until 2026-11-05 the right stick click's tap cast it through `SFXGameModeDefault.TryUseMappedPower`
  too; the click is hold-only since (research/19 "Hold only").
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
- **Still to check** [verify]: ~~a cast with the weapon away (question 2)~~ (section 9), the casting animation
  with arm IK (question 4), the push's thresholds with tracked hands in the headset, the marker's size at 5-35 m in the
  headset.

## 8. The cast from the hand: the player's powers have no projectile (2026-11-05)

The user asked for the cast to leave the left hand. Built, measured in the game on the fake headset (UNC20's awake
mercenaries, Throw and Singularity), and taken back out (a321189, reverted): **in LE1 the player's powers fly nothing**.

- **How a release happens:** never through script. The casting animation's notify is the event `BioPawn.ReleasePower`
  (ProcessEvent from native code), whose script calls `BioActivePower.AnimationReadyForPowerRelease`; about 30 ms later the
  running power's native tick does the release. The private native `ReleaseProjectilePower` (exec 0xBB21A0, which only calls
  the power's vtable slot 0x358) is never called from script, so a CallFunction handler on it never runs (a call trace of
  "release projectile" during casts showed none).
- **Other casters' powers fly:** each spawns a `BioLinePowerProjectile` (seen by a ProcessEvent post-handler on
  `Engine.Projectile.PostBeginPlay`, the spawn's event, before `InitializeData` fills its caster, power and target; 15 in a
  few minutes from the woken mercenaries BioPawn_1, _2, _5). The class's `InitializeData` and `TickAuthoritative` are natives
  that hand over to vtable slots 0x818 and 0x478.
- **The player's don't:** Throw and Singularity cast through `ActivatePower` (and landing on the locked mercenary: it was
  thrown each time) spawned no projectile of any class in the 4 s after the cast; every power projectile seen had another
  caster (the player was SFXPawn_Player_1). So Shepard's powers land at the release, at the target, with no travel; the
  1.6 s from push to effect (section 7) is the casting animation, not a flight.
- **Clearing the running power's `m_bCasterIsPlayer`** right after the cast (an experiment) changed nothing: still no
  projectile, so the projectile path isn't that flag.

**What's possible instead** (the user's choice): a visual only, a biotic bolt or beam drawn from the left hand to the target
at the release (`BioPawn.ReleasePower` on the player's pawn marks the moment; the game's own casting beam,
`BioActivePower.m_oCachedBeamEffect` / `m_bDisplayingCastingBeam`, may be one to reuse [verify]), which changes nothing in
the rules; or a real projectile for the player's powers (spawning a `BioLinePowerProjectile` at the hand with
`InitializeData` and holding back the game's instant release), which makes powers travel and miss like the enemies' (a
mechanic change, and the release isn't reachable from script). The mover of a321189 (the spawn handler, a launch from the
hand at the projectile's own speed toward its own target, `vr::power_wheel::FromHand` / `LaunchPoint`, unit-tested) is the
start of the second if it's wanted.

## 9. The powers' own effects, and a bolt from the hand (2026-11-05)

`mevr powerwheel vfx [all]` lists each power's `BioPowerVFXAppearance` (`BioPower.m_VFXAppearance`): nine slots of
`FPowerVFXData` (a `BioVFXTemplate` and `bUsePowerTime`: `PlayerCrust`, `PlayerMaterial`, `UsingEffect`, `FramebufferEffect`,
`TargetCrust`, `TargetMaterial`, `ImpactVisualEffect`, `WorldImpactVisualEffect`, `CastingBeam`), the projectile's attach point
(`nmProjectileAttachPoint`), and the projectile's and the release's own templates (`vfxProjectileEffect`, `vfxReleaseEffect`). A
template carries a prefab (its particle systems), a sound, a framebuffer effect and a lifetime. Read on UNC20 with VR00's Soldier
plus Throw and Singularity, and the woken mercenaries:

| Power | The player's version | An enemy version loaded there |
|---|---|---|
| Throw | charge on the body (`BIOG_V_B_ChargeInflux.Biotics_Charge_001`, at `RightWrist`), the biotic screen flash, the target's crust and world impact (`BIOG_V_B_TK_Impact01_Z`), release `Throw_Frambuffer`; **no projectile** | `TK_Throw_NPC` (BioPawn_5): projectile `BIOG_V_B_TK_Attack01_Z.Prefab.ThrowProjectile_VFX` |
| Singularity | the same charge and flash, `Weaken_FrameBuffer`, target crust and world impact `BIOG_V_B_Weaken_Z`; no projectile | none loaded |
| Warp, Lift, Stasis | (not the Soldier's) | `SD_Warp_NPC` `WarpProjectile_VFX`, `TK_Lift_PLC` `VFX_LiftProjectile_PF`, `KF_Stasis_NPC` `V_Stasis_Projectile` |
| Unity, medi-gel, Shield Boost, Adrenaline, Immunity | the omni-tool's own prefabs (`BIOG_V_T_Omni_Tool_Z` `OmniTool_1PieceLoop` / `2Piece`) on the caster, at `LeftWrist` or `RightWrist`; the target's crust | the same |
| Carnage | projectile `v_CarnageProjectile_PF`, release `v_Carnage_muzzle_VFX` (a weapon power) | `BW_Carnage_NPC` the same |
| Marksman, Overkill, Assassination, Carnage's setup | no appearance at all | |

So the player's biotics keep their charge, flash and impact but have no projectile picture; the enemies' versions of the same
power have one each, loaded only where an enemy uses that power.

**The prototype** (`mevr powerwheel bolt [on|off|speed <units/s>]`, on): at our push's cast of a power that aims, the projectile
effect of the player's power or else of an enemy version found among the loaded powers by label (`TK_Throw` -> `TK_Throw_NPC`,
`_PLC`...) is spawned at the left hand, 6 cm ahead of the aim pose, by `BioVisualEffect.CreateVFXOnPosition` on the class's default
object, faced at the target, and moved each tick (`Actor.SetLocation`) in a straight line to the locked hostile's chest or the place,
then let go (`SetLifeTime` 0.25 s). The flight takes as long as the game takes to release the power after the cast, learned per
power from `BioPawn.ReleasePower` on the player's pawn (a ProcessEvent post-handler): **0.57-0.58 s for Throw and for Singularity**
(section 7's 1.6 s was the ragdoll's start, not the release). Measured: Throw's bolt flew 23.6 m in 0.58 s, the game released the
power with it 99% of the way, it arrived 0.61 s after the cast. A slow test bolt (`speed 400`) showed the effect: **a translucent
blue biotic hand**, a one-shot burst of about a second (gone by 1.8 s even though it was still flying), which suits the push;
the game's own impact glow showed at the target at the release. Singularity, with no enemy version loaded, casts with no bolt
(logged).

**Open:** the hand's scale and brightness at arm's length in the headset (authored to be seen from afar); its orientation (faced
along the flight, the shape's own axes [verify]); powers with no enemy version loaded (a fallback: the template by name, a package
load, or one of our own); the charge (`PlayerCrust`) plays at Shepard's right wrist while the left hand casts; the tech powers'
omni-tool and the self-cast powers have no flight to show.

### The weapon out and the weapon away (2026-11-05, later)

The user: casts must work with the left hand alone while the right hand holds a weapon. Measured on the fake headset with
the game's own holster toggle (`TryHolster` from the console; the holsters are idle without tracked controllers) and
`mevr melee strike` as the probe of the weapon's state (its first refusal says whether the weapon is away):

- **Weapon out:** Throw cast, the release at 0.58 s, the locked mercenary thrown 2.8 m and lifted. Nothing in the push or the
  lock needs an empty right hand (only the left hand on the gun, the wheel or the squad wheel blocks a cast).
- **Weapon away:** the cast is accepted (`ActivatePower` true) and **the cooldown starts, but the power never goes off**: no
  `BioPawn.ReleasePower`, the target didn't move (Throw), so the power was spent for nothing. The release is the casting
  animation's notify, and the weapon-away animation sends none; ME1 itself never casts with the weapon away (the mapped power and
  the wheel draw it first, section 2).
- **The fix (on):** after a push's cast with the weapon away (`BioPawn.IsInCombatActionState` false), the mod sends
  `BioPawn.ReleasePower` itself (ProcessEvent, no parameters) at the release delay learned with the weapon out (0.58 s), unless
  the game released first. Measured: Throw with the weapon away thrown 2.7 m and 1.9 m on two casts, the gun stayed holstered.
  `mevr powerwheel unarmed on|off`; log `power wheel: the mod (the weapon is away) released the power ...`. Only our push's casts:
  the game's own wheel and mapped power still draw the weapon first, as in ME1. [verify] the casting animation with no gun and
  the self-cast powers (Barrier and the rest) this way.
- **The fallback bolt** (no enemy version loaded) flies the power's own charge (`Biotics_Charge_001`): standalone it shows as a
  translucent biotic forearm and open hand for a moment, then blue streaks along the flight (snapshots of Singularity's).

## 10. Self-cast gestures, and the omni-tool on tech casts (2026-11-05)

The user: the self-cast powers may want gestures of their own; any tech power must show the omni-tool gauntlet. Nine of the
19 mappable powers aim at nothing (section 3), and a push at nothing reads wrong for a shield or a buff. Every gesture follows
the left-hand rule (the left hand alone casts while the right holds a gun, research memory) and stays off the left trigger and
grip, which are the squad's wheel, its orders and the grenades: **open-hand placements, no buttons**.

| Gesture | Powers | Why |
|---|---|---|
| **Chest**: the open left palm on the chest (0.28 m below the eyes, 0.14 m ahead along the head's flat forward, within 0.13 m), held 0.35 s | Barrier, Shield Boost, Immunity, Adrenaline Burst | a shield or a buff on yourself: the hand to the heart |
| **Gun**: the open palm slapped onto the top of the gun in the right hand (6 cm up and 5 cm ahead of its aim pose, within 0.10 m; entering at 0.6 m/s or more casts at once, a slow touch after 0.35 s) | Marksman, Overkill, Carnage, Assassination | the gun's own modes: charge the gun |
| **Raise**: the open hand above the eyes (0.08 m), held 0.35 s | Unity | a rally: revive and heal the squad |

Rules (`vr::power_wheel::SelfGestureFor`, `StepSelfCast`, pure, `TestSelfCast`): open = the grip and the trigger both under 0.3;
once per stay in the zone (leave it to re-arm); a tick in the left hand on arriving, the cast's pulse after. With
`[PowerWheel] selfGestures = 1` (default) the push refuses those powers and says which gesture casts them (0: the push casts them
too). The gun gesture needs a drawn gun in the right hand (those powers need their weapon anyway). A self-cast with the weapon away
gets the mod's release (section 9). `mevr powerwheel selfcast` casts the equipped one as the gesture would (no tracked hands).

**The omni-tool:** a power shows the gauntlet (`game/OmniTool`, the game's own `OmniTool_2PieceLoop` on the left forearm) when it's a
tech power or its own effects are the omni-tool's (its appearance's `PlayerCrust` or release names `Omni_Tool`: Shield Boost, Unity,
Immunity, Adrenaline Burst, from section 9's map): while it's aimed with the arm raised, while its gesture's hand is in place, and
2.5 s after its cast. The game itself plays its omni-tool crust at the right wrist for most of these (`RightWrist`), which in VR is
the gun hand: [verify] whether both show, and whether the game's should be hidden.

### In the game (2026-11-05, the fake headset, VR00's Soldier save on UNC20, main 2e8f28d)

- **Switching powers on for a test:** the Soldier's own talents don't rise by cheat: `ScaleTalentByIncrement 35 3` (Assault
  Training; Adrenaline Burst comes at rank 3) and `ScaleTalentByIncrement 42 4` (Fitness; Immunity at rank 4) changed nothing.
  Bonus talents do: `GiveBonusTalent 63` then `ScaleTalentByIncrement 63 1` gives Barrier, `84` the same way Electronics
  (Overload) (`Talent_BonusTalents`' BonusIDs).
- **Barrier, the chest gesture:** the push refused it ("Barrier isn't pushed: the open hand on the chest casts it");
  `mevr powerwheel selfcast` cast it. With the weapon away the mod released it 0.56 s after the cast (section 9); with the weapon
  drawn the game released it itself, 0.48 s after. Its cooldown ran (99% to go just after), and the player's `BioActivePower`
  held Barrier's `BioShieldEntrenchScript`.
- **The gauntlet:** Overload (tech), aimed at an automatic lock (`aimhead on`, a mercenary 18 m away), kept the omni-tool on
  after the wheel closed (the open wheel shows it anyway). The push cast Overload at that mercenary (the game released it 0.26 s
  later). Equipping Barrier more than 2.5 s after that cast turned the omni-tool off. It's on Shepard's mesh at
  `LeftElbowTwist2` and `LeftWrist` (4 particle systems); the fake headset's level view doesn't show the forearm, so its look
  is a headset check.
- **Fixed on the way:** the equip line told self-cast powers "a push of the left hand casts it"; it names the gesture now.
- **Not checked:** a self-cast power that uses the omni-tool (Shield Boost, Immunity, Adrenaline Burst and Unity can't be switched
  on on this save), the 2.5 s after a cast once nothing is aimed, and the game's own omni-tool effect at the right wrist. In the
  headset: the zones' sizes and the dwell, false casts (a hand resting on the chest, reaching for the foregrip past the gun's top).

### Visible self-cast targets and confirmation (2026-11-07)

Owner QA: "The one power I had didn't seem to work. We may need a better confirmation for abilities like Overkill."
The earlier gun sphere sat on the right controller, with no visible target or dwell progress. The three-equip coach limit
made that especially hard to diagnose later in a session. This change is built and checked offline; no game or headset run.

- **Gun geometry:** `weapon::CurrentAsDrawn` supplies the live model's origin, muzzle and bore, including its kick (the
  same verified weapon that `Current`/`BoneWorld` describe). Project the grip/origin onto the bore, then make a capsule
  from there to the muzzle, **0.04 m above the bore**, **0.085 m radius**, using `sights::GunTop` for the gun's roll, as the
  HUD gun-status anchor does. This uses the same segment-distance principle as `ik::MakeSupportZone`; it does not claim
  a support grip or share its closed-hand input. The open left hand (both grip and trigger **< 0.3**) must enter at
  **>= 0.6 m/s** to slap-cast at once, or remain for **0.35 s**. Once per physical stay; closing/reopening the hand in
  that stay does not rearm it. Entry speed measures hand motion against this frame's capsule, so moving the gun onto a
  still palm cannot count as a slap. `support_grip::Held` also forces closed-hand input, so the target can stay visible
  while supporting the gun without allowing a cast.
- **Targets and progress:** always available while a self-gesture power is equipped, independent of the coach count.
  Gun: faint orange on the drawn gun, brighter within radius + **0.15 m**, cyan on open-hand contact. A distant marker
  sits at the capsule's middle; up close it follows the nearest point along its top. Chest: the unchanged centre
  **0.28 m below / 0.14 m ahead** of the eyes, radius **0.13 m**, target within **0.30 m**. Raise: the unchanged
  **0.08 m above the eyes** threshold, marker beneath the raised hand (near within **0.15 m**, faint within **0.30 m**).
  A ring fills clockwise with the published dwell, for all three. The marker canvas is **0.18 m** across; its base
  ring is about **0.083 m** across.
- **Confirmation:** `ActivatePower` returning true gives a cyan check and expanding ring for **0.28 s**, and the power's
  name plus "Activated" by the left hand for **1.0 s**, fading in the last **0.2 s**. The host uses its game-font label
  when cached (fallback otherwise). Both hands pulse **55 ms**, twice with **160 ms** between onsets, at **2.2 x** the
  haptic setting (clamped to 1; default **0.77**). Confirmation survives taking the support grip immediately afterward.
  This confirms that the game accepted the cast, not that the later release/effect was independently observed.
- **Refusals:** red cross/flash, the power's name and "Cooling down N%", "Not available now", or "Draw your weapon",
  with the same display timings. Both hands buzz **85 ms**, twice **220 ms** apart, **1.4 x** haptic (default **0.49**).
  Without a drawn model the gun target stays hidden; a tracked right aim held within **0.45 m below the eyes** supplies
  a refusal-only probe, **0.35 m** along its aim, lifted **0.04 m**, same radius and slap/dwell rules. It can never cast.
  Missing drawn geometry refuses as unavailable. Holstered weapon takes precedence over cooldown, then cooldown over
  unavailable. Gesture refusals no longer say "push not cast"; a self-gesture movement is not also processed as a push.
- **QA logging:** a reach starts within radius + **0.30 m** (Raise: **0.30 m below** its threshold), finishes at a cast,
  refusal, retreat, tracking loss or an input/power change. One `power wheel: gesture attempt:` line names the power and
  gesture, closest distance **to the capsule axis / zone centre** in metres, entry speed in m/s, open/closed and drawn
  weapon state, and cast/refused reason/missed. No extra missed line after a reported cast/refusal. An interruption
  flushes an unfinished reach as missed. A reach which never enters has entry speed zero.
- **Mod/host boundary:** `View::self` carries the target, look, progress and confirmation through the existing
  `setPowerWheel`; the host checks `sizeof(View)`, and stale publication clears the entire self view after **250 ms**.
  `SelfLayers` draws on the existing **powerlock** canvas; the left-hand message takes the existing **powerhint** canvas
  from the coach while visible. No new canvas swapchain or OpenXR allocation is introduced. Geometry, look, attempt
  lifecycle, refusal precedence and confirmation timing live in `core/vr/PowerWheel`, covered by `RunPowerWheelTests`.
- **Offline review:** `mevr-power-wheel-preview build/power-confirmation-preview` produces `self-target-faint.png`,
  `self-target-near.png`, `self-target-in-range.png`, `self-ring-half.png`, `self-success.png`, `self-refusal.png`, plus
  `self-success-message.png` and `self-refusal-message.png`. These are the host's actual drawing; reviewed visually.
  Validation: all four requested hot-reload build targets and monolithic LE1 passed, with warnings as errors;
  CTest **7/7 passed** (58.52 s), SPDX and whitespace checks clean. The managed shell required explicit selection of
  installed SDK **10.0.22621.0**, and CTest was reconfigured to an accessible bundled Python interpreter; no source/build
  flags were relaxed. In-headset target alignment on each gun model, reach comfort, readability and haptic feel remain
  **[verify]**. No game launch, deployment or push was performed.
