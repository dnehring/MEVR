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
  hidden (Halcyon's `[FPSHEAD]`), the neck stump may show at extreme pitches.
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

A report relayed by the owner: an idle sway "carried through to our movement/avatar". **Measured** with a new trace
(`mevr camera trace <seconds>`, `game/CameraProbe`: each mod tick a row of the pawn, the game camera and the Camera, God,
Head, Neck, LowerBack and Pelvis bones in the mesh component's space, to `%LOCALAPPDATA%\MEVR\camera-trace-<stamp>.csv`;
summarised by a script reading peak-to-peak and period per axis), the Presidium (John02 slot 89), Shepard standing still,
holstered, a still head, 20 s:

- **The view doesn't sway.** The game camera against the pawn and the Camera bone: 0.00 cm, 0.00 degrees (the explore
  posture's `Filter-CameraBone`, research/71 section 3.2, holds it).
- **The body does.** The pelvis, lower back, neck and head move against the whole body's bone (`Root`) by about 5.7 cm on
  one horizontal axis every 5-7 s, 2.3 cm on the other and 2.3 cm up and down: the idle animation's weight shift. LE1's
  skeleton hangs `Pelvis` and `LowerBack` side by side under `Root`, and the body follower slides `Root` under the view, so
  the hips and spine rock under a player who stands still, and with the hands on the controllers the shoulders and elbows
  rock instead (the trace ran without tracked hands, so the follower's own lean wasn't in it).

**Built** (`[Hands] steadyIdle`, **off by default until the headset check**): standing still, the torso solver
(`ik::TorsoGoal::anchor`, `feet`, `anchorWeight`, `maxAnchorShift`; `TorsoResult::anchorShift`, `anchorOffset`; unit test
`TestIdleSway`) slides the whole body (`Root`'s subtree, after its turn and slide) to hold the pelvis (the legs' common
parent) over the midpoint of the animated ankles, by the part of standing still (`1 - LegsState::weight`: the legs' blend
toward the run), at most 0.15 m; the writer then puts each ankle back where the animation had it and bends the knee toward
its animated place (`bone_pose::Leg::planted`), so the feet don't slide with the body. Not while the view is away from the
pawn (a scripted walk's legs stand by their own IK), not in third person, not with `legsFollowMotion = 0`. Log `arm IK:
standing still, Shepard's hips are held over his feet ...` once per stretch.

[verify] in the headset (with `steadyIdle = 1`): standing still and looking down, the hips and shoulders no longer rock and
the feet don't slide; starting to walk blends back to the game's walk without a jump. The fake headset couldn't show it: the
run with tracked hands came up with the follower in its third-person mode after the save's scene was ended.
