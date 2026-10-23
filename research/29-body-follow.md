# 29: The body under the player: shoulders that stay with the head (2026-10-23)

User report (PS VR2, SteamVR): "Sometimes turning my head and walking won't sync with the body's
position and it will behave as if my shoulders are in a different location." After head turns, stick
walking or physical steps, Shepard's torso and shoulders (where the arms hang from) ended up offset or
turned from the player's real shoulders, so the arms reached from the wrong place. Intermittent.

This note: how the torso was placed, the five causes found (each reproduced offline in
`tests/Main.cpp` `TestBodyFollow`), what was checked and ruled out, the fix, and what to read next
session. Builds on [14](14-vrik.md) ("Shoulders from the headset") and [18](18-camera-and-hands.md)
(the `Camera` bone).

## How the torso was placed (before)

- **Hands** (`game/HandTargets`, first person): through the **view's own frame**
  (`engine::FromCameraView`: Halcyon's camera, its head look and its view offset), so each controller
  lands where the player sees it.
- **Torso** (`game/ArmIk`, `core/ik/TorsoPose`): from the **animation**. The spine (LowerBack >
  Chest > Chest1 > Chest2) twisted from the animated shoulders' facing toward a body yaw (at most 60
  degrees) and leaned (at most 35 degrees about LowerBack) so Shepard's **eyes** moved as far as the
  headset had moved from the recenter point (`TorsoGoal::relative`). Rotation only: nothing moved the
  body itself.
- **Body yaw** (`ik::BodyYaw`, tracking space): the head's yaw with a 34 degree deadzone, easing back at
  0.5 per second, pulled halfway toward the hands. Reset only when the torso stopped being driven.

So the arms joined two frames: hands in the view's, shoulders in the animation's.

## The causes

### 1. The pawn faces where it runs, and the spine can only twist 60 degrees

ME1's explore movement (`BioPlayerController.PlayerWalking.PlayerMoveExplore`; MassFPS's replacement,
its `SmoothExploreMovement` script ([11](11-massfps.md)), keeps the rule) calls
`SetDesiredRotation(MoveFacing)` with `MoveFacing = MoveRot`, the stick's direction, whenever the stick
moves; standing, it keeps `MyBP.Rotation`. With Halcyon's `moveFollowsHead` the stick is turned by the
head's look yaw (`RotateMoveStickByHeadLook`, `vr_menu.cpp`), so Shepard turns toward the head's yaw
**plus the stick's angle**: strafing turns him 90 degrees from the player, backing up 180, and after a
physical turn in place he keeps facing wherever he last ran. The animation's shoulders face the pawn;
the spine's 60 degree twist then left the shoulders **30 (strafe) to 120 (backing up) degrees off** the
player's. Reproduced: `TestBodyFollow` part 1 (90, 180 and -135 degrees away).

Combat mode turns the pawn too (toward ControlRotation, which the right stick turns while the head
looks elsewhere): the same limit, smaller angles.

### 2. The view isn't where the animation put Shepard's head

Halcyon's camera hangs off the animated `Camera` bone plus per-mode offsets (research/18), and the
hands follow the view. The torso followed the animation's head plus the headset's displacement. The
gap between the two is what `arm IK: first person camera vs Shepard's head bone` logs every 5 s. In the
user's last two sessions (`LE1MEVR.prev.log` and `LE1MEVR.log`, 2026-10-23 00:14:03 to 00:18:58), with
the headset within 8 cm of the recenter point:

| | forward | right | up (units) |
|---|---|---|---|
| range | -21.4 .. +25.3 | -13.1 .. +9.7 | -2.4 .. +24.8 |
| typical (00:17:51 on) | +17 .. +23 | +2 .. +9 | +12 .. +14 |

The snapshot of 2026-10-22 23:51 has `hostView.unitsPerMeter 100` (life-size movement on), so that is
**up to 25 cm** between where the player's shoulders are (seen from the view) and where Shepard's were,
and it changes as the animation plays (walking, stance changes): intermittent, worse while walking.
Reproduced: part 2 (the view 25 units ahead: the old head stays 25 units off).

### 3. Physical steps move only the view

Halcyon's lean (`xr_session.cpp`, the view offset block) moves the view by `headPos * leanGain` (forward
times `ForwardLeanEmphasis`: **3** unless `[Camera] lifeSize`); the pawn never moves. Nothing ever put
the body back under the head except a recenter. The torso's answer to a step was a lean: rotation only,
at most 35 degrees about LowerBack, so a step of more than about 0.4 m (life-size) left Shepard bent
over and his head short of the view. Without life-size the view moves three times as far forward as
the old goal's isotropic scale assumed. Reproduced: part 3 (0.6 m step: the old head 21 units behind
the view horizontally, bent at the limit).

### 4. Turning the head moves the eyes

The eyes sit about 9 cm ahead of and 10 cm above the head's pivot (the top of the neck). Turning the
head 90 degrees moves the eye point about 13 cm (17 cm looking down), and the old relative goal moved
Shepard's eyes, so **his spine leaned sideways when the player only turned the head**. Reproduced:
part 4.

### 5. The body yaw survived a recenter

A recenter re-origins Halcyon's tracking space at the head (yaw only): the head's tracking yaw jumps
to 0. `gBodyYaw` wasn't reset, so the body was left at the deadzone's edge (34 degrees off) and eased
back over several seconds. Reproduced: part 5.

### Checked and ruled out

- **The tracking-to-world mapping and the stick:** `FromCameraView` turns tracking space into the world
  by `(camera * look) * inverse(head)`; Halcyon's look yaw is the head's own yaw from the same smoothed
  quaternion it tags the frame with (`HeadEulerDegrees(headQuatForFrame)`, `lookSensitivity` 1), so the
  tracking frame's yaw is the camera's: a stick turn turns the whole frame with no stale base yaw.
- **`game/ViewLevel`** writes only ControlRotation's pitch and roll (`Level()`), and drops aim assist's
  zoom snap; it never writes yaw. (Since [31](31-comfort.md) `game/Comfort` writes the yaw of the player's own
  snap or smooth turn, in the mod's tick before arm IK, with the view anchor told: tracking space doesn't move,
  so the body neither re-anchors nor lags.)
- **Feedback into the camera:** Halcyon reads no bones (no `GetBoneLocation` or SpaceBases in
  `src/halcyon/le1`); the camera's hook is the `Camera` bone, a child of `God`, which the writer never
  writes (research/18). The first/third person decision uses the head bone as animated, before our
  writes.

## The fix (2026-10-23)

The body is placed from **this tick's** headset, controllers and view; the only state is in tracking
space and is re-anchored whenever it could be stale.

- **`core/ik/BodyFollow`** (engine-agnostic, tested):
  - `HeadPivot`: the eyes minus the head's own (0, 10 cm down, 9 cm back): turning or nodding doesn't
    move it.
  - `BodyFollow`: the body's yaw (`ik::BodyYaw`, as before) and **the feet**, a point on the floor plan
    that stays while the head's pivot leans less than `leanRadius` (0.20 m) from it, is dragged along
    at that radius beyond (a step), and settles under the pivot at 0.3 per second.
  - Re-anchors (feet under the pivot, yaw at the head's) on **start** (the torso wasn't driven: after a
    cutscene, conversation, menu, vehicle or level load), on **recenter** (the host's recenter counter,
    HostServices v10 floor state) and on a **tracking jump** (the pivot moving 0.5 m or the head's yaw
    1.2 rad in one tick: a recenter we weren't told about, lost tracking).
- **`core/ik/TorsoPose`** (`TorsoGoal::body`): before the spine, the **whole body** moves rigidly: the
  subtree of the spine's bottom joint's parent (LE1 `Root`: spine and legs; **never `God` or `Camera`**,
  which hang beside it; arm IK checks with `ik::InSubtrees` per skeleton and the writer keeps them anyway):
  - **turns** about the up axis through `Root` by the part of the facing turn the spine can't take
    (the spine keeps at most 30 degrees);
  - **slides** horizontally so `Root` sits over the feet as the view maps them, at most
    `[Hands] bodyMoveMaxM` (0.75 m);
  - then the spine twists and leans Shepard's **head bone** (not his eyes) to the head's pivot as mapped
    by the view: absolute, not relative.
  The writer writes from the body bone down when it moved (`TorsoResult::firstBone`).
- **`game/BodyFollow`**: the goal each tick (first person: the whole body under the view; first person
  with `bodyMove = 0`: the old spine-only relative lean; third person: the old goal, the hips still take
  the turn). `game/ArmIk` calls it; its old `TorsoFacingWorld` and `gBodyYaw` moved there.
- **Settings** (`[Hands]` in `ASI\LE1MEVR.ini`, appended with their defaults): `bodyMove = 1` (0: the
  spine alone, as before), `leanM = 0.20`, `bodyMoveMaxM = 0.75`. `bodyFollowsHead = 0` still turns the
  whole torso feature off.

Offline (`TestBodyFollow`): facing error under 2 degrees for the pawn 90/180/-135 degrees away (was 30 to
120); the head on the view within 1 unit for a 25-unit camera offset (was 25); within 6 units
horizontally after a 0.6 m step, bend 18 degrees (was 21 short, bent at the 35 degree limit); the pivot
exact for any head turn; the yaw at once after a recenter (was 34 degrees off); `God`, `Root` and
`Camera` bit-identical after every whole-body move.

## Logs and snapshot

- `torso: re-anchored (start | recenter | tracking jump): feet under the head's pivot (x, z) m, body yaw
  N deg (tracking space)`, at most once a second.
- `torso: shoulders turned off the player's | head off the view | body slide capped | large correction
  (mode): head yaw H (world W), body yaw B (world W), pawn P, ControlRotation C, camera V deg; turn wanted
  T = body X + spine Y (short Z), bend N deg; the view D m from where the game put Shepard, body slid S m;
  Shepard's head G m from the head's pivot; lean L m (head pivot (..), feet (..) m)`, at most every 2 s,
  when the facing stays 15 degrees short, the head 10 cm off, the slide is capped, or the correction was
  large (hips over 60 degrees, the view over 35 cm from the body): the inputs say which cause it was
  (pawn vs body yaw: cause 1; the view's distance: causes 2 and 3).
- The heartbeat: `torso: <mode>; re-anchored N times (last: ...), M off-the-player ticks; body yaw vs
  head; pawn vs body; last write: hips + spine of wanted, slid of wanted, head off`. The writer's
  `arm IK torso` line adds `body turn N of M deg, body shift N units (wanted N: capped), reference N
  from its goal`.
- Snapshot JSON: `mod.torso` (mode, last re-anchor, counts, recenters, the yaws in tracking space and
  in the world for head, body, pawn, ControlRotation and camera, pivot, feet, lean, units per meter,
  and the last write's turn, slide and gaps).

## Risks [verify in headset]

- **Legs:** the hips turn with the body, so running sideways or backward in explore mode shows legs
  running one way while the body faces another (a moonwalk); the slide makes the feet skate while the
  view moves relative to the pawn. In first person they're mostly out of sight.
- **Held leans:** the feet settle under a held lean in a few seconds (Shepard straightens up and his
  feet slide forward); `leanM` larger keeps leans longer.
- **The neck model** (10 cm down, 9 cm back) is an average; at 50 units per meter Shepard's own head
  is twice the player's size, so his head bone lands a few units off his eyes' natural place.
- **Clipping:** with the body under the view, looking down shows the chest from right above; the head is
  hidden (Halcyon's `[FPSHEAD]`), the neck stump may show at extreme pitches.
- **The cap:** a player who walks more than 0.75 m from where they recentered leaves Shepard behind (the
  log says capped and to recenter). Moving the pawn itself (room-scale) or turning the pawn to the body
  yaw would fix the legs and the cap, but they change game mechanics: not done.
- **No feedback loop expected:** `Camera` and `God` aren't written (489ef51's judder came from moving
  them); the hands don't depend on the body; the first/third person test uses the animated head.
- Bullet hits on the player use the pawn's cylinder **[verify]**: the mesh moving away from it changes
  nothing in play.
