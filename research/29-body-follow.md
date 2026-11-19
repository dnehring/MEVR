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

- **Legs** (addressed 2026-11-04 below, "The legs keep the run"): the hips turned with the body, so running
  sideways or backward in explore mode showed legs running one way while the body faced another (a moonwalk).
  Now, moving, the legs keep the pawn's facing up to the spine's 60 degrees; what's left: backing up still turns
  the legs 120 degrees off the run (the explore set has no backward run), and the slide still makes the feet skate
  while the view moves relative to the pawn. In first person they're mostly out of sight.
- **Held leans:** the feet settle under a held lean in a few seconds (Shepard straightens up and his
  feet slide forward); `leanM` larger keeps leans longer.
- **The neck model** (10 cm down, 9 cm back) is an average; at 50 units per meter Shepard's own head
  is twice the player's size, so his head bone lands a few units off his eyes' natural place.
- **Clipping:** with the body under the view, looking down shows the chest from right above; the head is
  hidden (Halcyon's `[FPSHEAD]`), the neck stump may show at extreme pitches. Measured 2026-11-18 ("The eyes inside the
  body", below): squats and bends put the eyes inside the collar and chest; `[Hands] crouchBody` fixes them.
- **The cap:** a player who walks more than 0.75 m from where they recentered leaves Shepard behind (the
  log says capped and to recenter). Moving the pawn itself (room-scale) would fix it but changes a game
  mechanic; its complete design is below ("Room-scale: the design", 2026-11-04), not built: it needs the
  game to verify.
- **No feedback loop expected:** `Camera` and `God` aren't written (489ef51's judder came from moving
  them); the hands don't depend on the body; the first/third person test uses the animated head.
- Bullet hits on the player use the pawn's cylinder **[verify]**: the mesh moving away from it changes
  nothing in play.

## The legs keep the run (2026-11-04)

The first risk above, fixed. The whole-body turn (`TorsoGoal::body`) turned the body bone (`Root`: the spine and
the legs) by whatever the spine's 30 degrees couldn't take of the turn from the animated shoulders to the body yaw.
Standing that's right. Moving, it turned the legs off the way the run animation runs: strafing (the shoulders wanted
90 degrees from the pawn) the legs ran 60 degrees off where Shepard went, backing up (180) 150. The feet skated.

### Where the game's animations point

From research/38 section C (`BioPlayerController.PlayerWalking`): `PlayerMoveExplore` turns the pawn to the stick's
direction (`SetDesiredRotation(MoveRot)`, at 270 degrees a second), so the explore run always runs where the pawn
faces; `PlayerMoveCombat` (weapon drawn) faces ControlRotation and accelerates along the pawn's own axes, so its
strafe and backpedal are relative to the pawn's facing too **[verify: the combat AnimTree's directional blend wasn't
read]**. Either way the legs' animation belongs to the pawn's rotation: the mesh component's forward, arm IK's
`BodyAxes` (`BodyFrame::forward`). That is the legs' facing here, not the thighs' line from the pose (the gait's own
hip sway would come back as a counter-turn: robotic hips).

### The rule

While the pawn moves, the legs keep the pawn's facing and only the spine twists toward the body yaw, up to
`[Hands] legsTwistDeg` (60, the old spine-only limit) from the legs; past that the whole body, legs too, turns by the
rest, so the shoulders still face the player's way (the arms hang from them: cause 1 above stays fixed). Standing, the
old rule. `core/ik/BodyFollow`'s `LegsTurn(facing, limit)` and `LegsFollow`; `TorsoGoal::legsWeight`,
`legsTurnDegrees`, `legsTwistDegrees`.

| Case (shoulders wanted from the pawn's facing) | Spine | Whole body (legs) | Before |
|---|---|---|---|
| Standing, any (or the speed unreadable) | at most 30 from the animated shoulders | the rest | the same |
| Moving where the view looks (about 0) | the facing | 0 | 0 |
| Strafing (90) | 60 | 30: the legs 30 off the run | 60 |
| Backing up (180) | 60, on the side the facing reached first | 120 | 150 |
| Weapon drawn, the player turned 90 degrees in the room | 60 | 30 | 60 |

- **Backing up.** The explore set has no backward run, so no rule makes it right; the options were (1) twist to the
  limit and turn the body by the rest (chosen), (2) the legs toward the view, as before (the full moonwalk: a forward
  run sliding backward), (3) the legs flipping to the view past some angle (a 120-degree jump of the hips, at a
  threshold the facing crosses back and forth), (4) a real backward run (the explore run's sequences at a negative
  `AnimNodeSequence.Rate`, or blended to the combat backpedal: AnimTree writes like research/38's, **[open]**). (1)
  is continuous in the angle (no snap anywhere), keeps the shoulders the player's, leaves the legs on the run up to 60
  degrees and 30 degrees closer to it than before beyond, writes no animation, and backing up in first person the player looks ahead with the
  legs under and behind the view. Near 180 the sign of the facing flips with noise, which would swing the legs
  2 x 60 degrees from one side to the other: the facing is unwrapped (it goes on past 180 on the side it came from)
  until it is 30 degrees past, then takes the short side again (`LegsSettings::sideBandRadians`).
- **Weapon drawn.** The game's strafe and backpedal are drawn against the pawn's facing (ControlRotation), so the old
  turn spoiled them whenever the body yaw left it: a player turned in the room (the head's yaw away from
  ControlRotation, cause 1's "combat mode turns the pawn too") walked with the directional legs 60 degrees off for a
  90-degree turn. Same rule. The animation's shoulders may be turned from the legs already (a stance into the gun):
  the spine's limit counts from the legs, so from the animated shoulders it may twist the limit plus that
  (`TorsoResult::shouldersFromLegsDegrees`, logged). With `[Hands] exploreStance = 1` (off by default) the body
  composes from the explore pose while the pawn strafes in combat: the explore run's legs under a strafing pawn are
  the game's own moonwalk, not body follow's **[verify if it's ever turned on]**.
- **Third person** with `bodyMove`: the same rule (the hips turn there too).
- **Moving or standing:** `Engine.Actor.Velocity`, horizontal, above 60 units a second, standing again below 30
  (LE1's walk is 130, its run 400: research/38's `CreatureSpeeds`). Between the rules the body's turn blends by a
  weight at 4 a second (a quarter second each way), the short way round, from the turn measured as it is that tick
  (the pawn's own turn toward the stick shows at once, no lag). A re-anchor (start, recenter, tracking jump) or the
  torso not being driven starts over: no side kept, the weight straight where it belongs. The velocity unreadable
  (a failed lookup, LE2/LE3): standing, with one warning.

### Code, settings, logs

- `core/ik/BodyFollow` (`LegsSettings`, `LegsInput`, `LegsState`, `LegsFollow`, `LegsTurn`), `core/ik/TorsoPose`
  (the blend, the spine's limit from the legs, `YawBetween`, `TorsoResult::standingTurnDegrees` /
  `shouldersFromLegsDegrees`), `game/BodyFollow` (the speed, the facing from the pawn's to the wanted, the goal's
  fields). Mod only (hot-reloaded); no host change, no new reflection lookup (`Engine.Actor.Velocity` has its
  ReflectCheck row), no engine write beyond the pose the writer already journals.
- `[Hands] legsFollowMotion = 1` (0: the old rule moving too), `legsTwistDeg = 60` (30..90), appended to older files.
- `torso: legs: keep the run (400 u/s, holstered, weight 1.00): shoulders 90 deg from the legs, the spine 60 (limit
  60) + the whole body 30; last write: the whole body 30 deg (the standing rule: 60), the spine 60; the animation had
  the shoulders 0 deg from the legs` at most every 5 s while moving; the heartbeat's `torso:` line carries the same
  state, the moves begun and the ticks spent keeping the run, and `hips N (standing rule M)`; the off-the-player line
  ends with it. Snapshot: `mod.torso.legs` (`moving`, `weight`, `speedUps`, `weaponDrawn`, `shouldersFromLegsDeg`,
  `legsTurnDeg`, `spineFromLegsDeg`, `twistLimitDeg`, `starts`) and `mod.torso.result.standingTurnDeg` /
  `shouldersFromLegsDeg`.
- Offline: `TestBodyFollow` part 6: the turn rule; the speed's hysteresis and the weight's blend; the side kept past
  180 and given up 30 degrees on; the pose for strafes either way, backing up and 45 degrees (the legs exactly
  `LegsTurn` off the pawn, the shoulders on the wanted facing within 2 degrees, not limited, `God`, `Root` and
  `Camera` bit-identical); weight 0 bit-identical to the old solve; half way, including through 180; a stance with the
  shoulders 30 degrees from the legs twisted 90 from them without stopping short.

### Checking it in the game (fake headset)

1. Build `MEVR-LE1-mod` from main (mod only: the host and HostServices are unchanged; hot reload is enough).
2. `tools/fake-headset.ps1 -On`, `tools/launch-game.ps1` (or `-Steam`), load an on-foot save (Eden Prime); VR on.
3. `python tools/make-walk-recording.py <dir>\walk.mevrrec 3 90 --pitch -60` (the stick forward, strafe right, back,
   strafe left, stand, 3 s each; the head looking down at the legs) and `tools/replay.ps1 -Path <dir>\walk.mevrrec
   -Loop -Inputs`.
4. The log's `torso: legs:` lines: strafing, shoulders about 90 from the legs, the whole body about 30 (the standing
   rule 60); backing up, about 180 and 120 (150), the side not flipping between lines; standing, `legs: standing` and
   the heartbeat's hips equal to the standing rule's.
5. `tools/snapshot.ps1 -Count 15 -Interval 1`: each JSON's `mod.torso.legs` and `result`; the left-eye PNGs show the
   legs under the view: strafing, the feet pointing along the move.
6. A/B: `[Hands] legsFollowMotion = 0` in `ASI\LE1MEVR.ini` (read within a second), the same snapshots: the body's
   turn back to the standing rule.
7. Weapon drawn: `[Holsters] releaseGrace = -1` (the gun stays out without a held grip), `tools/console.ps1 "mevr
   weapon pistol"`, and a recording with the player turned in the room: `--yaw 90` (`--grip` holds the right grip
   instead). Walking: `weapon drawn`, shoulders about 90 from the legs, the whole body 30.

## Room-scale: the design (2026-11-04, not built)

The cap above (Shepard left behind past `bodyMoveMaxM`) asks for the pawn to follow the player's real steps. It is a
mechanic change (the player's position moves without the stick) and touches the camera, the view anchor and arm IK on
the game thread, and the game's movement physics; none of it can be checked without the game, so it is designed here
and not built.

### The facts it rests on

- **The view.** Halcyon's view is the game camera's `CameraCache.POV` (made in `Camera.UpdateCamera` on the game
  thread: the `Camera` bone plus the camera mode's offsets) moved by a view-space offset it sets at Present for the next
  `CalcSceneView`: the head's tracking position times the lean gain (`xr_session.cpp`, the "Render-side view offset"
  block; forward times `ForwardLeanEmphasis`). The mod maps tracking space through the same view
  (`view_anchor::FromHost`: `engine::FromCameraView(POV, look, offset, head)`). So tracking space's origin sits at the
  POV, turned with the camera's yaw: the room hangs on the pawn's camera. Stick walking moves both; a stick turn turns
  the room about the camera.
- **The tick.** The mod ticks after `UGameEngine::Tick` returns (`plugin/Entry.cpp`): the world tick, the camera's
  update and the viewport's draw are inside it. What the mod changes in its tick reaches the next frame's
  `UpdateCamera` and `CalcSceneView` together (research/31 relies on the same order for the mod's own turn).
- **The body's state** (feet, the head's pivot) is in tracking space.

### The step

Each tick, after body follow's update and before arm IK computes anything: `W` = the body's wanted slide (the feet as
the view maps them minus `Root`, horizontal, world units). Past `[Hands] roomScaleM` (0.30 m in units), the pawn moves
by `W (|W| - r) / |W|`, at most `roomScaleMaxMS` (2 m/s) times the tick. `Engine.Actor.Move(Delta)` (a native with its
own opcode: through `ue3::runtime::Call`, gotchas/process-event-skips-opcode-natives) sweeps the pawn's cylinder:
walls stop it. The real displacement `D` is the pawn's `Location` after minus before (Move returns false when
blocked; partial moves happen). Only on foot (mode 0), first person, the whole body on, `Physics` walking, not in
cover, and not while something holds Shepard's view (a wrist screen's `view_anchor::Hold`, the galaxy hologram's or a
world screen's held view) or the menu pauses. New ReflectCheck rows: `Engine.Actor.Move` (Delta, ReturnValue),
`Engine.Actor.Physics`.

### The view stays still: the room offset

Without more the view moves `D` with the pawn (the room hangs on it): the world slides, the move doubled. The mod keeps
`O`, the pawn's room-scale displacement in the camera's yaw frame (forward, right; units): `O += inverse(yaw(POV)) D`,
and a `Camera.UpdateCamera` post-handler (on foot, the player's camera) subtracts `yaw(POV) O` from the POV's location
(`write_journal::Wrote("BodyFollow.roomOffset", ...)`). Not in cinematic modes (the director's camera), the Mako or the
galaxy map's own view. Post-handlers run in registration order (`engine/ProcessEvent`), so this one registers before
`world_ui`'s and `galaxy_map`'s, which record "Shepard's view" from the POV on foot.

- **No host seam.** The pawn's move and `O` change in the same mod tick; the next `UpdateCamera` sees both (the bone
  moved `D`, the offset takes it back). Tracking space's origin stays put in the world, so body follow's feet need no
  re-anchor and its jump test sees nothing (the raw head doesn't move). The alternative, a `[MEVR]` seam subtracting
  `O` from the head's position in Halcyon's lean (HostServices v28), would apply it at Present, a frame apart from the
  move (a jitter of one tick's step), and needs a host redeploy.
- **Kept until a recenter** (the host floor counter: the room re-origins at the head and the view goes back over
  Shepard, as a recenter does now) or a new pawn. Turning the setting off stops moving the pawn but keeps `O` to the
  next recenter, so nothing jumps.
- **A stick turn** turns the room about the camera, and `O` with it (it's in the camera's frame): the view swings by
  the small `head - O`, not by the whole walked distance (a side benefit: today a snap turn swings the view on the
  full offset from the recenter point).

### What else has to know `O`

- **The comfort turn's guess** (`view_anchor` with `vr::comfort::TurnCamera`): when the mod turns ControlRotation
  without the pawn (explore), the POV (bone minus `yaw O`) moves by `(I - R) yaw O`, but the guess keeps the location:
  up to 0.77 `|O|` off for a 45-degree snap, one tick of misplaced hands and body. Guess on `POV + yaw O`, then subtract
  `yaw' O`. With the pawn turned the guess is right as it is (the bone and the POV turn about the same pivot).
- **Arm IK's transform this tick:** if Move doesn't update the mesh's `LocalToWorld` at once **[verify]**, the targets
  must be put in the moved component (translated by `-D`), or the hands and body sit `D` off for a frame.
- **Snapshots and logs:** `mod.torso.roomScale` (on, `O`, the last `D`, blocked ticks), `torso: room-scale: ...`.

### The game's side (the risks)

- **Slopes and stairs:** Move sweeps sideways; an upward ramp or a step blocks it (the walking physics' step-up isn't
  run). `Actor.MoveSmooth` slides along what it hits; three moves (up by the step height, across, down) is UE's
  `stepUp`. Measure where a plain Move stops on Eden Prime's ramps first.
- **Ledges:** a move over an edge leaves the pawn without a floor and the next walking tick makes it fall (realistic;
  a fall to death from a step in the room too: a trace down before each move could refuse a drop deeper than a step).
- **Triggers:** Move touches volumes as walking would (`BioTriggerStream` streams areas in), which is the point.
- **Conversations and cutscenes:** Halcyon's lean applies in a VR cine as well (the head's offset on the director's
  camera); `O` doesn't touch it, so the walked distance offsets the cine camera, as it does today.
- The slide's cap stays as the backstop (a blocked move).

### When it's built

`[Hands] roomScale` (on; 0 off), `roomScaleM` 0.30, `roomScaleMaxMS` 2.0 in the default text and the migration list;
an entry in `docs/trilogy-design.md`'s change log; the step, `O`'s bookkeeping (a stick turn after a move keeps the
view, a recenter clears it) and the turn guess's correction in `core/` with tests; in the game, a head recording
walking 2 m forward in the room (`make-walk-recording.py` moves the stick; it needs a head-translation option): no
`capped` lines, Shepard under the head at the end, and against a wall the move blocked and the old cap.

## The idle sway (2026-11-14)

A report relayed by the owner: an idle sway carried into the avatar. The whole audit is research/72. In short: the idle
animation moves `Root` itself (at hip height, the whole body's bone) 2-3 cm in the mesh's space, up to 16 cm in an idle
fidget, over planted feet; the camera doesn't move. The slide above puts `Root` under the view whatever the animation did,
so in first person the hips stay still and the **feet slide** the other way. `[Hands] steadyIdle` (off by default until the
headset check) plants them: the torso's `feetCorrection` (`Root`'s animated offset from its rest, turned with the body, by the
part of standing still), applied by the writer's `Leg::planted`. A first version the same day held the pelvis over the feet
instead, read from a trace without `Root`; it would have put the sway back into the hips and was replaced.

## The eyes inside the body (2026-11-18)

The owner, from ME1 in the headset: "One problem I had in me1 is that sometimes I could clip and look into my own body." No
log, snapshot or recording of that session was kept, so the cause was measured offline.

### How it was measured

`tests/EyesInBodyTests.cpp` (in `mevr-tests`; `MEVR_EYES_IN_BODY_REPORT=1` prints the table) replays the first-person path
tick by tick at 90 Hz: the player's head, `ik::HeadPivot` and `ik::BodyFollow` (the feet), the torso goal as `game/BodyFollow`
builds it (Shepard's head bone at the head's pivot, `Root` slid under the feet, the hips turning past the spine's 30
degrees), the body scale as `engine/BonePose` applies it (goals through the inverse, the pose scaled about the floor under
the view), `ik::PoseTorso`, the legs. The skeleton is LE1's Shepard (`HMM_ARM_MEDb`): the hierarchy of the `skeleton:` lines in
`LE1MEVR.log` (God > Root > LowerBack > Chest > Chest1 > Chest2; Root > Pelvis > the hips; God > Camera), the bone heights of
research/72's idle trace (Root 100.9, LowerBack 104.0, Chest2 140.1, Neck 153.0, Head 165.8, the collars 148.3, the hips 99.3,
the ankles 11.1 units), the eyes at 169.4 (research/58), the knees, Chest, Chest1 and Neck1 between their neighbours. The
player is the owner (eyes 1.84 m, so the body scale is 1.086). The armour is modelled as capsules round the spine (radii: the
chest 14, the collar 11, the neck seal 7, the shoulders 8 units before the scale); "inside" is the eyes' depth in them, and
within 10 units of a surface (the near plane, `LocalPlayer.NearClipPlane`) the eye can see into it.

| The player's head (crouchBody 0: the code before this fix) | eyes from Shepard's face | inside the armour |
|---|---|---|
| standing straight | 1.0 cm | no (7.4 cm clear) |
| a squat of 10 / 20 / 30 / 40 / 46 cm (46: the physical crouch's 0.75 for the owner) | 10 / 20 / 30 / 39 / 44 cm below | 1.5 cm clear / **1.3 / 3.4 / 4.3 / 4.9 cm inside** |
| a bend at the hips of 30 / 45 / 60 / 70 degrees | 8-28 cm below, 7-29 cm behind | **0.4-1.8 cm inside** while bending, 3 cm clear after |
| from the game's crouch (Shepard 45 units lower), the eyes at 0.60 / 0.45 of standing | 25 / 44 cm below | **1.4 / 5.8 cm inside** |
| leaning back 10-20 cm, a quick 90-degree head turn | 2-5 cm below | no (8-9 cm clear) |
| standing, looking down 60 / 80 degrees | (the hidden face doesn't nod) | no, 4.3 / 1.8 cm clear: within the near plane |

### The cause

The body follows the head horizontally only. The slide (`TorsoGoal::bodyMoves`) moves `Root` over the feet but keeps its
animated height (`bodyRise` was only for a body drawn away from its pawn, research/69), and the lean keeps the spine's
length: it turns the upper body about `LowerBack` (at most 35 degrees) so Shepard's head bone points at the head's pivot,
never nearer. Body scale (research/58) sizes Shepard so his standing eyes are the player's standing eyes; when the player's
head goes lower than his spine reaches from his standing hips (a squat above the game's crouch, a bend, a kneel below the
crouch's own pose) nothing lowers him, so the eyes sank into his collar and chest, under the near plane. A bend also walks
the feet forward under the pivot (the 0.20 m lean radius, then the settle), which only makes it worse. The quick turn, the
lean back and the 0.75 m slide cap don't put the eyes inside (the cap leaves the body behind, not around the eyes); the
scale's clamp does for a player under 1.44 m eye height, which the fix covers too.

### The fix: the body goes down (or up) to the head (`[Hands] crouchBody`, on by default)

One core for the three games (`core/ik/TorsoPose`, used by `engine/BonePose` and `game/BodyFollow` on ME1, ME2 and ME3; it acts
wherever the skeleton has both eye bones and both legs, ME2's being ME1's, research/le2-vrik.md):
- **`TorsoGoal::crouch`** (with `bodyMoves`, an absolute goal): after the slide, the whole body (`Root`'s subtree; never `God`
  or `Camera`) moves along its up by the gap between where the spine can hold Shepard's head bone and where it belongs. The
  head bone's target is the head's pivot raised by how far it sits above Shepard's own pivot (`neckDown`, the neck model's
  10 cm, less his eyes' height above the head bone: 3.6 units on LE1), which is where it is when the player stands straight
  at the body's scale, so standing nothing moves. The lean then aims at that target. A gap within 1% of the spine moves
  nothing, and the move grows from zero past it (no step). Moving (the legs keeping the run, `crouchLoose`), a gap within
  `crouchSlack` (6 cm) is the run's own bob (research/72: Root 9 cm up and down) and is left alone.
  `TorsoResult::bodyLift` (+ up), `crouchGap`, `liftLimited`.
- **As far as the legs allow** (`ik::LegsRoom`): down until a leg's hip-to-ankle is 25% of its length (a full squat), up
  until 98% (straight); the lesser of both legs; without both legs in the request, no move at all (the head-only and
  menu-held torsos carry no legs and keep the old behaviour). **Up only with `[Hands] crouchBodyUp = 1`** (off by default
  until the owner says yes, `TorsoGoal::crouchUp`): a head above Shepard's (the game's crouch while the player stands or
  half stands, the weapon-drawn stance's 16 cm lower head, research/58) was never inside the body, and bringing the body up
  to it straightens legs the game animates bent.
- **The legs stand the body on the floor** (`ik::LegUnderBody`, `engine/BonePose`'s `Leg::planted`): each ankle back by the
  lift (plus the planted feet's correction, research/72), its rotation kept (the foot keeps its angle to the floor), the knee
  bending the way it bends, toward the body's forward as the move grows. `game/ArmIk` puts both legs in the request whenever
  the goal crouches. The conversations' standing legs (`Leg::stand`, research/69) take the lift out of the walk animation's
  foot the same way.
- `game/BodyFollow` fills it in first person with the whole body under the view and both eye bones known.

The same cases with crouchBody 1: every squat to the physical crouch's threshold puts the eyes 1.6 cm from Shepard's face,
7 cm clear of the armour (as standing), the feet on the floor to the millimetre, the knees 20-40 cm forward; bends are never
inside (2.8 cm clear at worst, while bending); from the game's crouch the body goes down 24 cm for a head at 0.60 of
standing, the face within 1.6 cm, and with crouchBodyUp comes up 19 cm for one in the hysteresis band (0.84; without it the
eyes stay 19 cm above the face, as before); moving, a squat leaves the eyes 7 cm
under the face (the slack) and clear; standing, leaning back, looking down and the quick turn are as before (within 0.5 cm).

### What is left

- **A kneel below the game's crouch** (the eyes at 0.45 of standing, 0.83 m for the owner): the legs fold no further, the
  eyes still 2.3 cm inside. The body would have to kneel (a knee on the floor) or lean back.
- **Looking straight down** (80 degrees and more, standing or bent): the eyes come within 2 cm of the modelled collar, so
  the near plane may cut the armour's top. The capsules are a guess at the armour; only the headset tells.
- **The bend's posture**: a bend draws a squat (the body down and upright, the knees forward), not a hip hinge; the eyes
  are at the face, the drawn legs not where the player's are.
- **A head above Shepard's** (the game's crouch by the button with the player standing: the eyes 45 cm over his; the
  hysteresis band; the weapon-drawn stance): the view floats over the body as before unless `crouchBodyUp = 1`, which brings
  the body up, the legs straight under the crouch's upper body (the game still crouched for cover and collision). A new look
  for the owner to judge before it is a default.

### Setting, logs, snapshot, the run

- `[Hands] crouchBody = 1` (appended with its default; 0: the body at the game's height, as before); `crouchBodyUp = 0`
  (1: up too). Both read live.
- Logs `torso: crouching with you: the body down N cm to your head (Shepard's head N cm too high), the knees bending, the
  feet on the floor` and `torso: back to the game's height after a crouch with you: at most N cm down, N cm up; N ticks held
  short by the legs` (at most once a second each); the `torso:` off line and the heartbeat add the lift; the writer's `arm
  IK torso` line `body lift N units (gap N, held by the legs)`; `hands:` says `the body goes down to the head`.
- Snapshot `mod.torso`: `crouchBody`, `crouches`, `result.bodyLiftM`, `crouchGapM`, `liftLimited`.
- In the game: `tools\fake-headset-session.ps1 -Tag eyes -Run tools\eyes-in-body.ps1` (replayed heads standing, squatting 15
  and 30 cm and a bend's head looking 60 degrees down, crouchBody 0 and 1 each: snapshots and skeleton traces, the floor lock
  off and the scale held for the replay). Prepared on 2026-11-18 while the owner was in the headset; not run yet. In the
  headset: notes/headset-checklist.md.

## The floor of a held scene (2026-11-18)

The owner, on the council chamber's bridge (the hearing, `sta90_council_saren_D`, in first person): "it looks like I'm floating
... my knees are kind of hanging". The snapshot (19:48:57, mode 5, "the whole body where the player stands, away from the
pawn") had `bodyLiftM -0.247`, `crouchGapM 0.253` at body scale 1.101; the next run's log `torso: back to the game's height
after a crouch with you: at most 69 cm down` in the same scene.

**Cause.** Arm IK scales the body (research/58) about `view_anchor::FloorReference()`'s floor. In a held view that was the
floor on foot captured when the hold began (`view_anchor::Hold` took its reference only on the first call), so after a
scripted ride (the moment up to the council chamber: `moment: summary {... "riseM": 2.33 ...}`, then `floor lock: IK hold
start: ... floor Z 61729.148`, the floor 2.3 m below) the body was scaled about a pivot 2.3 m under its feet: lifted by
(s - 1) x 2.33 m = 0.24-0.28 m at 1.10-1.12. Before `crouchBody` the whole body stood in the air with the eyes in its chest
(probably the "look into my own body" that began this section, and research/69 item 44's "still floating above where I should
be" of 2026-11-17, whose fix brought the eyes to the right height but not the body); with it the knees bent to bring the
head to the eyes and the feet stayed in the air.

**Fix.** game/ConversationWorld's `HeldFloorView(pawn)`: the floor on foot's reference with `pawnFloorZ` set to the floor
under the drawn body (the pawn's `Location.z - CollisionHeight`, at `gC.detachedAt` while the view is detached: BodyAway's
`riseUnits`), passed with every hold (the conversation's and the moment's), and `view_anchor::Hold` takes a passed reference
on every call. Log `convo: at N s the body's size is taken about the floor under it, N m above the floor on foot when the
scene began` (once a conversation, past 10 cm). [verify] in the headset: the council bridge, the feet on the floor, the legs
straight standing; on the fake headset the moment up to the chamber and the hearing (`tools/conversation-from-save.ps1`'s
council save) with snapshots of the legs.

## Backing up on the sequels (2026-11-18, lane M2W)

The owner, ME2 in the headset: "Walking backwards, my avatar does a side strafe instead of walking backward."

### What the log shows

- **Exploring (weapon away), ME2 turns the pawn to run where the stick points**, as ME1's explore set does (research/38 section C):
  `movement: response:` after a pull back, `the pawn turned 150-173 deg meanwhile`, and the torso lines while backing up read
  `pawn 144 vs body -60 deg (world)`, `legs: keep the run (354 u/s, holstered, weight 1.00): shoulders -203 deg from the legs,
  the spine -60 (limit 60) + the whole body -143` (20:37:43), `-159 ... + the whole body -99` (20:37:26), `-125 ... -65`
  (20:37:30). So this section's rule for 180 degrees holds: the spine at its 60, the whole body (the legs) turned the rest toward
  the view, which leaves the legs' run 120 degrees off the way Shepard goes: a forward run sliding back and sideways, the owner's
  "side strafe".
- **With a weapon drawn ME2 backpedals**: the pawn keeps ControlRotation's facing (`pawn 163, ControlRotation 163`) and the
  shoulders stay within 2-43 degrees of the legs while moving (20:40:10-20:43:06, a dozen lines): the combat set's directional
  animations, the legs right.
- **ME1 is the same**: the same explore turn and the same rule; the table above already says backing up leaves the legs 120
  degrees off ("The explore set has no backward run, so no rule makes it right").
- **ME2's explore set has no backward run either** (LExSDKv2's LE2 headers): `BioAnimNodeLocomotionMoving` has lean and incline
  only (`AngleLeanLeft/Right`, `AngleAscend/Descend`, `Min/Mid/MaxSpeed`), no direction; the directional blend
  (`Engine.AnimNodeBlendDirectional`) is presumably the combat branch's [verify: `mevr animtree Directional` on ME2].

### The options (the owner's to choose; nothing built or changed)

1. **Keep it** (today's): continuous, no snap, the legs on the run up to 60 degrees; backing up reads as a strafe.
2. **A larger twist limit** (`[Hands] legsTwistDeg` 90, the existing setting's top): backing up, the legs end 90 degrees off the
   motion instead of 120; still sideways, the spine visibly wrung. A setting change only.
3. **Backpedal exploring, the run reversed:** while the stick points behind the view exploring in VR, the pawn keeps the view's
   facing (its `DesiredRotation` written back after `PlayerMoveCombatOrExplore`, where the speed from the push is set) and the
   explore run's sequences play at a negative `AnimNodeSequence.Rate` (this note's option 4 of 2026-11-04): the legs face forward
   and step backward. Unknowns: whether ME2's `BioAnimMovementSync` (the run's sync group) rewrites the rate each tick, and how a
   reversed run reads at speed.
4. **Backpedal with the combat legs:** the same facing, and the legs composed from the combat branch's directional locomotion
   while backing up (the node choosing explore or combat composed the other way round, as ME1's `exploreStance` composes explore
   while drawn: research/38 A). Unknowns: whether the combat branch animates with no weapon in hand, and that node's name on ME2.

3 and 4 need the AnimTree read in the game first: `tools/le2-halcyon-run/posture-walk.ps1 -Phases tree` logs the locomotion
nodes (`mevr animtree Locomotion`, `Directional`, `Combat`, `Storm`) and a backing-up replay with the head pitched down
(`--pitch -60`) under a camera trace.

## The crouch that went third person (2026-11-18, lane ANC)

The owner, ME2 in the headset (Freedom's Progress from Save 4, the run 20:34-20:51 of 2026-11-18, MEVR 0.3.2 at 237aeb1b, its
`LE2MEVR.log`): "almost like clicks and moves Shepard to be behind cover, where it's almost magnetic". Lane CV2 found that ME2's own
cover was never entered (research/23 section 7.6); lane M2W saw arm IK fall to its third-person anchor once the game crouched
Shepard (research/23 section 8.1). This section is the cause and the fix.

### What the log shows (each a `grep` on the run's log)

- **Every crouch behind a barrier left first person**, 7 times: `arm IK: body anchor: third person (tracking origin at Shepard's
  animated head), view 75 units from Shepard's animated head bone` at 20:37:12.747, 20:37:20.108, 20:46:24.515, 20:46:40.436,
  20:47:53.783, 20:48:38.851, 20:49:11.281 (75 or 76 units each time: the 75-unit exit), each after `posture: crouch request
  accepted ... Engine.Pawn.ShouldCrouch(true)`. Each came back (`body anchor: first person (the view's frame), view 44 units`)
  0.5-1.2 s later, when the game stood him up; at 20:49:11 the game had stood him while the player stayed down, 17 s.
- **The late ones fell as the player stood up again**: the anchor at 20:46:24.515, `posture: physical stand: eyes 1.77 m` at
  20:46:25.427 (after the stand's 0.2 s dwell), first person at 20:46:25.664; the same at 20:46:40.436, 40.886, 41.153. The view
  rose with the player's head (the floor lock keeps the eye at the pawn's floor plus the headset's height: `floor lock: ...
  STAGE eye N m`) while Shepard was still crouched, so the vertical gap to his head grew. The first (20:37:12.7) came 0.3 s
  after a crouch at 1.69 m (0.91 of standing): the game's crouch takes his head much lower than a player who crouched 17 cm.
- **Crouched and still in first person the view was 41 units across from his head and 35 up** (`arm IK: first person camera vs
  Shepard's head bone: forward 35.2, right 20.5, up 34.9 units`, 20:46:19.732); standing it is 20-35 forward, 0-25 up.
- **In the third person the body stops following the view**: `torso: third person: the hips turn ... slid 0.00 of 0.00 m` (the
  heartbeat at 20:37:13.539) against `the view 0.24 m from where the game put Shepard, body slid 0.24 m` a quarter second before.
  So the drawn body jumped from under the player's eyes back to where the game had the pawn (0.24-0.76 m), and forward again a
  second later. Crouched behind a barrier the player leans toward it: the body clicking to the barrier and back.

### The cause

`game/HandTargets` chose the frame by the 3D distance between the view and Shepard's animated head bone (scaled as drawn,
`game/ArmIk`'s `headBone`): first person within 45 units, third person past 75. That distance has two parts that mean different
things. Across, it says where the camera is against Shepard (Halcyon's third-person camera is a metre or more behind him; its
first-person eye 20-40 units from the head bone). Up, with the floor lock on, it says only how the player stands against the
animation: the view's height is the player's own head. ME2's crouch lowers his head 45-60 units (scaled 1.095), the physical
crouch asks for it with the player's eyes only 15-35 cm down (research/23 section 8.1: the owner's calibrated threshold), and a
player standing up rises before the game stands Shepard: 30-60 units up and 41 across made 75. ME1 has the same rule and the same
crouch: the owner's "weapons very low" after a crouch (2026-11-09, gotchas/arm-ik-third-person-anchor) was this too.

### The fix (every game; `core/ik/BodyFollow`, `game/HandTargets`, `game/ArmIk`)

1. `core/ik/BodyFollow.hpp`: `struct AnchorSettings { enterUnits = 45; leaveUnits = 75; }`, `struct AnchorInput { bool haveHead;
   Vec3 viewFromHead; bool playerHeight; }` (world axes, Unreal units, +Z up), `double AnchorGap(AnchorInput)` (the whole
   length, or `hypot(x, y)` with `playerHeight`; -1 without a head or for a non-finite gap) and `bool FirstPersonAnchor(bool
   wasFirstPerson, AnchorInput, AnchorSettings = {})` (the old hysteresis on that gap: stay within `leaveUnits`, enter within
   `enterUnits`; never without a head).
2. `game/ArmIk.cpp` `Drive`, just before `hand_targets::Current`: `viewHeightIsPlayers` is true when the host's view is
   floor-locked (`vr::host_view::Latest()`: `valid && floorLocked`, and not `view_anchor::Held()`), or, without the floor lock
   (no floor from the runtime, `[Body] floorLock = 0`), when Shepard crouches (`posture::PhysicallyCrouched()` or
   `player::Crouched(pawn)`) on foot out of cover (`player::FloorLockSample()`: `onFoot && !cover`).
3. `game/HandTargets::Current(animatedHead, inView, allowHeadOnly, viewHeightIsPlayers)` puts it in the `AnchorInput` and keeps
   the result's `anchorGap` and `viewHeightIsPlayers` in `Targets` for the log.
4. The log line: `arm IK: body anchor: first person (the view's frame), view N units from Shepard's animated head bone (M across:
   the view's height is your own head's, so only the horizontal distance counts), 100.0 units per meter`.

**Why this and not a larger threshold or the crouched head's expected place.** The floor lock already says the view's height is
the player's whatever the animation does, so the vertical gap carries nothing about the camera: excusing it covers the game's
crouch, a squat with the game standing, the weapon-drawn stance's lower head and the player standing up first, with no number to
tune. The horizontal limit stays 75 units, so a view that steps far from him still leaves, and a third-person camera (100-200
units behind) is never first person: Halcyon's K view and cover are never floor-locked (`vr::floor_lock::Applies`), and the
crouch excuse asks for no cover.

**Tests** (`tests/CrouchAnchorTests.cpp`, `RunCrouchAnchorTests`): the owner's crouch and stand-up as ticks of view offsets from
the log (standing (24, 8, -1); crouched (35, 20, 35); rising (35, 20, 55-66); stood (25, 5, 36)): the old rule leaves first person
while he rises, the new one never; a squat 70 units under his head with the game standing; third-person cameras (-150, 40, 30),
(-110, 35, 10), (-80, 0, 0) third person either way; the hysteresis across (76 leaves, 74 stays, 50 doesn't enter, 44 does); no
head and non-finite offsets.

**On ME1** the same: a crouch (physical or L3) or a squat keeps the body under the view and the hands at the controllers.

**Left.** A backing-up turn still swings his animated head across: the explore run turns the pawn 160-180 degrees and the head
bone with it (`forward 70.3, right 21.1, up 3.0`, 20:37:47.717), third person for 1.2 s at 20:37:51.288. That is horizontal, not
this rule's: "Backing up on the sequels" above (question 19). [verify] in the game: `tools/le2-halcyon-run/crouch-anchor.ps1
-Phases button,physical` (not run); the headset: notes/headset-checklist.md.

## A run never lowers the body (2026-11-18, lane ANC)

The same run: `torso: crouching with you: the body down 17 cm to your head (Shepard's head 17 cm too high)` at 20:37:01.851 and
20:37:02.949, `14 cm (17)` at 20:39:21.753, and so on: 13 lines of 12-18 cm, most within a second of a run (`movement:
response: the stick pushed ... speed 0, 400, 400 ...; the pawn turned 156-180 deg meanwhile`: mostly the owner backing up, the
explore run turning the pawn round), with the eyes at 1.73-1.84 m (`floor lock: ... STAGE eye`) against 1.87 m standing. The legs
bent under a body that went down mid-run.

### What the log shows

- **The animation stands higher in and after ME2's run.** The heartbeat's `arm IK torso (component space)` line at 20:37:03.534,
  with the legs settling after a run (`legs: settling (0 u/s, holstered, weight 0.27)`): the animated shoulders at z 152.9 against
  the idle's 145.5-146.6, `body lift -13.4 units (gap 15.7)`; at 20:51:14.818, shoulders 153.3, `lift -9.4 (gap 12.3)`. At idle
  the same player's gap was 4 cm (`Shepard's head 4 cm too high`, the lift 3 cm, dozens of lines).
- **The slack doesn't cover it.** `TorsoGoal::crouchSlack` (6 cm, game/BodyFollow's `kCrouchSlackMeters`) leaves the run's bob
  alone by the legs' weight (`crouchLoose`, `ik::LegsFollow`'s blend, 4 a second). ME2 reaches 400 u/s in 0.1 s and stops as fast,
  so in the first and last steps of a run the weight is near 0 while the pose is already, or still, the run's; and the gap (15-19
  cm) is three times the slack anyway.

### The cause

The crouch's gap ("The eyes inside the body" above) is measured on this tick's animated pose: how far his head bone would stand
above its target with the spine pointed straight at it, the target being the player's head pivot raised by the neck model. That
equals the player's own head drop only while the animation stands as he stands idle. A pose that holds his head higher than
standing (ME2's run and the steps after it) reads as the player crouching, and the body went down to it.

### The fix (every game; `core/ik/TorsoPose`, `game/ArmIk`, `engine/BonePose`)

1. `TorsoGoal::crouchMaxDrop` (component units, infinity by default): `PoseTorso`'s crouch takes the drop as `min(maxDrop (the legs'
   room), crouchMaxDrop)`; `TorsoResult::liftByHead` says the head's limit held it (`liftLimited` is set too).
2. `ik::CrouchDropLimit(standingEyesZ, neckDown, pivotZ, margin)` = `max(0, standingEyesZ - neckDown - pivotZ) + margin` (infinity
   for a non-finite input): how far the player's head pivot is below where it is when the player stands straight at the body's
   scale. That is the gap a standing Shepard shows: his head bone stands at `standingEyesZ - e` (`e`: his eyes over the head
   bone), the target at `pivotZ + neckDown - e`.
3. `game/ArmIk` fills it right after `request.bodyScale` is set, when the torso is enabled and crouches: `standingEyesZ = pivotZ +
   bodyScale * body_scale::ShepardStandingEyeM() * perMeter` (`pivotZ` the scale's floor under the view; `perMeter` the units per
   metre times the component's scale when the reference is in world units: the floor-locked and seated branches set
   `worldReference`), `goal.neckDown`, `goal.head.z` (the pivot, drawn), margin `kCrouchDropMarginMeters` 0.02 m. No standing
   reference yet: no limit (the old behaviour).
4. `engine/BonePose` unscales it with the goal's other lengths (`bs::UnscaleLength`), before the solve on the unscaled pose.
5. Logs: `torso: crouching with you: ... (held at your own head's drop: the animation stands higher than standing)`, the writer's
   `arm IK torso` line `body lift N units (gap N, held at the player's own head drop)`, the torso's off line `held at your own
   head's drop`; snapshot `mod.torso.result.liftByHead`.

So a run with the player standing lowers the body by at most the margin below what standing still gives; the player's own crouch
still lowers it as far as the head went (a 20 cm squat under the run's high pose: 20 cm, not 33). Because the limit is measured at
the head pivot (as the gap is), a head looking down, whose eyes sit lower over the pivot, doesn't open it. At the top of such a pose
the eyes sit low in Shepard's neck, as before crouchBody (the test's 13-unit pose: the eyes 12.6 cm under his drawn face, 0.5
cm clear of the modelled armour, so within the near plane): the run's own bob, which the slack was for. The other way (the
body down to the eyes) is what bent the knees mid-run in the owner's session.

**Tests** (`tests/EyesInBodyTests.cpp`): `Options::raised` raises LowerBack's subtree (the spine, collars, arms, head, eyes) 13
units, the run's pose; standing, without the limit the body goes down 12 cm (the slack off) or 6 cm (the slack on), with it at most
2.5 cm; squats of 10, 20, 30 cm under that pose go down the head's own drop (within 2.5 cm), the feet on the floor; every earlier
case (squats, bends, leans, the game's crouch, a short player) runs with the limit and passes unchanged; `CrouchDropLimit`'s values;
the solve holding a 30-unit gap at a limit of 8 with `liftByHead`, and a limit of 50 changing nothing. `MEVR_EYES_IN_BODY_REPORT=1`
prints the two "ME2's run" tables.

**On ME1** the same rule; squats, bends and the game's crouch are unchanged (the tests), and a pose of ME1's that stands higher
than his idle (a run's bob past the slack, an idle fidget) no longer lowers the body below the player's head. [verify] in the game:
`tools/le2-halcyon-run/crouch-anchor.ps1 -Phases run` (ME2, not run): no `crouching with you: the body down N cm` past 3 cm while
running and backing up with the head standing.
