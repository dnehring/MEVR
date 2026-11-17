# 40: Using things with A: what the game selects, and its prompt in VR (2026-10-30)

The user (2026-10-30): "One problem in gameplay is the issue of interacting with objects, where one needs to press
'a'. I find that the text tooltip is far too small and also it doesn't seem to reliably work." Their choices for the
fix: the object nearest the centre of the view gets the prompt ("where you look"), and the prompt shows big at the
object itself.

## 1. How LE1 chooses what A uses

Read with Ghidra on the LE1 project (2.0.0.48602); offsets checked against LExSDKv2's LE1 headers.

- `BioPlayerController.m_oPlayerSelection` (+0xAB8) is a `BioPlayerSelection`. Its native
  `UpdateSelection(float DeltaTime)` is the script thunk `execUpdateSelection` (0xB7E850) around the body at
  **0xC79D70**, which is also called natively from 0xC78C60. It runs once per tick for the player.
- The body takes the player controller (the selection's Outer), its pawn, and **the player camera's cached view**:
  `PlayerCamera` (+0x6B0) → `CameraCache.POV` at +0x45C (Location), +0x468 (Rotation), +0x474 (FOV).
- Candidates are actors rendered within the last second (`LastRenderTime`) whose selectability interface accepts the
  player. For each: the distance from the POV's location (within `m_fSelectionRangePlayer`, or `RangeVehicle` in the
  Mako) and the angle between the POV's forward and the direction to the candidate's target point. It must be inside
  half the POV's FOV, and inside a selection angle that goes linearly from `m_fSelectionAngleClose` at
  `m_fSelectionRangeClose` to `m_fSelectionAngleFar` at the far range (on foot; `m_fSelectionHalfAngleVehicle` in the
  Mako). The smallest angle wins, starting from `m_fSelectionConeHalfAngle`, and only if the POV's location can see it
  (`IsInLineOfSightFrom`). Every candidate in the cone is listed in `m_lSelectionsInRange` (target, angle, range).
- The result goes into `m_oCurrentSelectionTarget`. The flags at +0xA4 carry `m_bCurrentSelectionObstructed`,
  `...IsCombatTarget` and `...IsMagneticLocked`. A change of target notifies the controller.

**Why it misbehaves in VR.** Halcyon's head look turns the rendered view only. `CameraCache.POV` stays the game
camera's, which follows the body and the right stick. So the cone points where the body faces, not where the player
looks, and its half-FOV limit is the flat camera's. Looking at a crate while the body faces elsewhere selects nothing,
or something else.

## 2. The prompt

The HUD's target status widget (`BioSFHandler_HUD.UpdateTargetStatusWidget`) shows the selection at the top centre of
the HUD movie. Two lines: the object's name ("Medical Kit", "Ashley Williams", "Crate"), then its action ("Open",
"Talk"). **The A glyph appears in front of the action only while the object is in use range; out of range the action
is greyed and A does nothing.** A combat target shows its name, health bars and an arc instead.

Measured on 591 HUD snapshots (2026-10-30). The HUD frame is 2344x2392; the panel swapchain is half that. The text
spans x 0.45..0.54 and y 0.034..0.063 of the frame, and a combat target's arc spans x 0.36..0.62. The power bar's slots
sit at the top left (x < 0.32). The movie's 1280x720 stage is shown across the frame's width, aligned to the top
(core/vr/WheelCrop's "show all"), so in stage pixels the widget sits in x 384..896, y 30..100.

On the body-locked HUD panel (1.30 m wide at 1.5 m, about 47 degrees across) the name's letters are about 0.4 degrees
tall, and the widget is nowhere near the object.

## 3. What we do (2026-10-30)

- **Gaze** (`engine/SelectionGaze`, mod; `[Interact] gaze = 1`): a hook on 0xC79D70. For that call only, the camera's
  `CameraCache.POV` Location and Rotation are the head's place and forward in the world (from `view_anchor`, this
  tick's head pose, roll 0, the camera's own winding kept). They are put back right after (`__finally`), and journalled.
  The game's own selection rules, ranges, line of sight and use range all still apply. The POV's FOV is kept.
- **Label** (`game/Interact`, mod → HostServices v21 `setInteractLabel` → `plugin/HalcyonOverlays`, host;
  `[Interact] label = 1`): each tick the mod reads `m_oCurrentSelectionTarget` and its combat flag. It takes the middle
  of the target's `CollisionComponent.Bounds`, and puts the label `kRaise` (8) units above their top. The top is capped
  `kMaxRise` (50) units over the middle, and the label never goes more than `kAboveEye` (10) units over the eye. The
  mod sends that point in tracking space. The host cuts the widget's band (`vr::interact::DefaultCrop`, or
  `[Interact] crop`) out of the HUD panel, as the wheel panel does. It shows the band at full HUD resolution on a quad
  at that point, facing the eyes, `labelHeightDeg` (6) tall at any distance. The name's letters, about 18 of the band's
  130 pixels, come out near 0.9 degrees, about 2.5 times the panel's. Combat targets keep the HUD panel unless
  `combatLabel = 1`.
- Logs: `interact:` (each target change, with its name and distance; a heartbeat line with the selection updates and
  how many ran from the gaze) and `[MEVR-INTERACT]` (the label's place and size, its band). Snapshots: `"mod"."interact"`
  and a `label` layer.

## 4. Open **[verify]**

- That the gaze is what the player expects: an object at the edge of the view is selected only within the game's
  selection angle around the head's forward. If it feels too narrow, `m_fSelectionAngleClose`/`Far` can be widened.
- Powers aimed at "the selection" now follow the head on foot as well. Whether that matters (a power while aiming
  the gun elsewhere) is for the headset.
- The label's height over pawns: 8 units above the collision cylinder's top, capped under the eyes + 10.
- A selection the camera's line of sight saw but the head doesn't (or the reverse), near corners.
- Whether the band holds every widget: long names, the combat arc, other HUD movies (PC vs console layout).

## 5. ME2 label renderer and native selection boundary (2026-11-15)

ME2 now publishes the existing v21 interaction-label service and renders a selected object's prompt through
`HudPieceLayers::Interaction`. It reuses `InteractLabel::Place` and `HudPieceLayers::Show`, including the shared
HUD capture, colour treatment, quad allocation and snapshot layer naming. The source HUD rectangle is masked
only after its replacement was copied successfully, with room reserved to submit it. The label clears with host
mod state and its swapchain follows session cleanup. Menus, calibration, the power wheel and non-gameplay modes
suppress the replacement.

The ME2 crop still requires measurement in a protected native run. Until then the renderer requires an explicit
`[Interact] crop = x y width height`, in fractions of the captured HUD frame. All four values must be finite,
the dimensions positive, and the whole rectangle inside the frame. Without that rectangle the native HUD remains
visible and `mod.interact` reports no replacement label. Do not copy LE1's top-centre default into this setting.
The existing `label` and `labelHeightDeg` controls apply once a valid crop is supplied.

Head-gaze selection is still pending. LE1 hooks `int UpdateSelection(this, float)` at its verified address.
ME2's SDK and original native declaration specify `void UpdateSelection()` and a separate
`FindCurrentSelectionTarget()`. LE2's address remains zero. A correctly typed entry, its camera reads and native
callers must be verified before adding the hook. The interaction log now claims gaze selection only while the
hook is actually installed. Native `BioPlayerSelection` remains authoritative for the target reported and used.

Next native check uses a normal-loaded checkpoint with an unobstructed door or NPC prompt. Capture the HUD,
measure its band, enable that crop, and compare the `label` layer with both eyes and the actor A actually uses.
Include invalid/no target, range/occlusion, menu entry and a load transition. The render path is an offline-tested
implementation candidate until that check. Default crop, head/body disagreement and headset legibility remain open.

## 6. ME2's gaze, and the label at the object on ME2 and ME3 by default (2026-11-16, lane HU)

**ME2's selection is ME3's.** The natives list (`%LOCALAPPDATA%\MEVR\dump\natives-LE2-60AEEFA5.csv`, `[Debug] dumpNatives`) names
`BioPlayerSelection.FindCurrentSelectionTarget`'s thunk at `0xB4AF70` and `UpdateSelection`'s at `0xB4AFB0`. Ghidra on the LE2
project (`tools\ghidra\decompile.ps1 -Game LE2 0xB4AF70 0xB4AFB0 0xAC0530`): the first thunk calls `0xAC0530`, the second calls the
update body `0xACE8F0`, and `0xAC0530`'s only other caller is that body. `0xAC0530` is the target search, `void(this)`: it takes
the selection's outer controller (`+0x40`), that controller's `PlayerCamera` (`+0x6A0`), the camera's `CameraCache.POV.Location`
(`+0x464`) as the cone's origin, and the controller's own `Actor.Rotation` (`+0x124`, turned into a direction by `0x699C0`) as
its axis: the same contract as ME3's `0xA9BF90` (research/le3-interaction-gaze.md). So ME2 takes ME3's branch of
`engine/SelectionGaze` (the void(this) detour; `game/Interact` hands it the controller's rotation, swapped for the call only, put
back in `__finally`, journalled). Address row `UBioPlayerSelection::FindCurrentSelectionTarget` in `engine/Addresses.hpp`'s LE2
table, pattern `48 8B C4 48 89 48 08 55 53 57 41 54 41 55 41 56` (a .pdata start, unique in ME2's .text, `tools/rva-patterns.py`);
ReflectCheck rows for both functions and their empty parameter lists (`CheckParmsSize ... 0`); ctest
`mevr-selection-gaze-LE2` runs the detour's swap, restore and fault tests compiled for ME2.

**Each game's prompt band.** Every game's prompt is its HUD's top-centre widget, the 1280x720 stage shown across the frame's
width (measured on HUD panel snapshots, alpha > 40):

| game | the prompt's box (stage px) | its neighbours | band (`vr::hud_pieces::DefaultLabelCrop`) |
|---|---|---|---|
| ME1 | name and action inside x 384..896, y 30..100 (section 2) | | x 384..896, y 30..100 (`interact::DefaultCrop`) |
| ME2 | a hexagon x 602..679, y 31..78 (Freedom's Progress' `SFXDoor_0`, 1365x768 snapshots) | the power bar at the top left reaches x 433 (alpha > 5) | x 448..832, y 30..100 |
| ME3 | one box x 569..711, y 1..67, its name above the action line (`SFXSFHandler_PCHUD.TargetName` y 5..33, `TargetStatus` y 35..63, `mevr guimouse hud`) | | x 448..832, y 0..70 |

ME1's band left ME3's name on the HUD panel (it starts at y 30) and took the right end of ME2's power bar into ME2's label: hence
a band per game, keyed by the bridge's `Facts::hudLayout` (ME2 `Layout::Me2`, ME3 `Layout::Me3`). `[Interact] crop` still
overrides it. The label is on by default on both sequels, as on ME1 (`game/Interact` no longer requires a hand-set crop there),
and ME3's bridge gates it as ME2's (`OverlayLabel`: not while a screen of ours has the panel).

**In the game.**
- ME2, Save 4 (session `20261116-221216-522-LE2-hu-me2a`): `hooked UBioPlayerSelection::FindCurrentSelectionTarget`, `reflect: 726 of
  726 checks agree`; head ahead: `A would use SFXDoor BioD_ProFre_100SpacePort...SFXDoor_0, 13.4 m away (chosen from where you
  look)`, every selection update from the gaze, the `label` layer up; head turned 60 degrees right or left: nothing selected, no
  label; ahead again: the door and its label. The first label used ME1's band and showed the power bar's edge at its left: the
  band above is the fix. Again with it (session `20261116-230035-894-LE2-hu-me2g1`): the same selections, the label holding the
  hexagon alone (stage x 600..680), the power bar whole on the HUD panel (x 13..433).
- ME3, Save 31 (session `20261116-215851-868-LE3-hu-me3c`, two runs): walked to the War Room table (`-1131, 6571, -29`), head 30
  degrees left and 30 down: `SFXPointOfInterest_0` 1.35 m away, the `label` layer holding the whole box (War Terminal, A Use) at the
  console, the HUD panel empty (the band cut out), the eyes' world layer without any of the prompt; head 70 degrees right: nothing,
  no label; back: selected again.

**Not verified:** a long name's box (wider than the bands?), combat targets (`[Interact] combatLabel`), legibility in a headset.
