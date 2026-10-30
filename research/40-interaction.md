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
