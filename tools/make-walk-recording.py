# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py) of a player standing still in the
room and walking Shepard with the left stick: forward, strafing right, backing up, strafing left, then standing, each for
`segment` seconds, in a loop. The head looks ahead (or turned --yaw degrees in the room, as after turning in place) and
can look down (--pitch) so the eye snapshots show the legs; both hands are held in front of the chest (--hands view: up in
front of the eyes, inside the view, so every eye snapshot shows Shepard's drawn hands where the controllers are: the audit's
check of the body's place, research/69 "Seeing the body in the audit"). For the legs
keeping the run (research/29 "The legs keep the run"): replay it with its inputs on the fake headset,

    python tools/make-walk-recording.py out.mevrrec [segment] [hz] [--yaw DEG] [--pitch DEG] [--eye-height M] [--stick S] [--grip]
        [--plan forward:14,stand:600] [--hands chest|view] [--hand-yaw DEG] [--fire EVERY[:HOLD]] [--crouch-drop M]
        [--lean-drop M] [--lean-forward M]
        [--tracking both|left|right|none]
    tools/replay.ps1 -Path out.mevrrec -Loop -Inputs

segment: seconds per stick direction (default 3). --yaw: the head (and the hands) turned DEG degrees right in the room.
--pitch: the head pitched DEG degrees (+ up; -60 shows the legs). --eye-height: the head's height in tracking space
(default 0 relative to the recentered head; the null headset's STAGE height is separately 1.7526 m). --stick: the stick's deflection (default 0.9, a run: above the
game's 0.70). --grip: the right grip held all the way (a gun drawn with `mevr weapon pistol` stays in the hand). --plan: the
segments by name and seconds instead of the loop of five (forward, strafe right, back, strafe left, stand): `stand:600` is a
player standing with tracked hands (an audit run's hands, tools/conversation-audit.ps1); `forward:14,stand:600` one who walks
with a scripted walk and keeps the stick pushed through its cut (the council hearing, research/69 "The body that stayed at the
mark"). --hands: chest (default, 0.35 m under the eyes and 0.30 m ahead: below a 45-degree view) or view (0.08 m under
the eyes, 0.25 m ahead and 0.09 m to each side: 0.28 m from the eyes, inside [Hands] reachNearFaceM's 0.30, so the drawn hands
sit on the controllers unscaled by the arm-length calibration; the wrists, 7 cm behind the grips, 24 degrees down and 27 to
the side). At 0.40 m ahead the hands were reach-scaled 0.16 m low, cut off at the snapshot's bottom edge (2026-11-13).
--tracking: which controllers have valid grip/aim poses (default both); none clears the recording's controller-active
flag while retaining the recorded head. The current replay adapter forces active=true while replaying, so none tests
invalid grips/aims; stop replay to test genuinely inactive controllers on the null headset. Use without replay -Inputs
to test loss/reacquisition without recorded gameplay button input.
--hand-yaw: the right controller (grip and aim) turned DEG degrees right of where the body faces, the head still ahead: hand
aim's check (shots follow the controller, not the view; research/le2-vrik.md "Hand aim"). --fire: the right trigger pulled all
the way for HOLD seconds (default 0.15) every EVERY seconds, from 2 s on (shots; replay with -Inputs). A plan's `crouch`
segment holds the stick still with the head (and the hands) --crouch-drop metres lower (default 0.6: below [Hands]
crouchBelow's 0.75 of a 1.65 m standing eye height), the physical crouch's check (research/23 section 7):
`--plan stand:8,crouch:4,stand:8`. A `lean` segment holds the head (and the hands) --lean-drop metres lower (default 0.17)
and --lean-forward metres ahead (default 0.35): a reach toward someone, not a crouch (research/23 "A lean is not a crouch").
The plan also accepts `l3` (the left stick click: the crouch toggle on foot), `back-button` (left menu with left grip, mapped to pad Back), `start-button` (left menu alone:
pad Start, the game's own pause menu), `r3` (right stick click), `x` (the left primary: pad X; ME3's system map binds
Exit to it) and `b` (the right secondary: pad B; ME3's orbit backs out with it). For ME2/ME3's existing Back double-tap recenter use
`--plan stand:2,back-button:0.12,stand:0.12,back-button:0.12,stand:12 --live-head` with replay `-Inputs`.
A segment may join several with `+` (the stick of the one that moves it, every button of each): `forward+lt:3` holds the
left trigger while pushing forward, `lt`/`rt` hold the left/right trigger alone (ME3's galaxy map scans with LT), and
`stick@DEG` (or `stick@DEG@MAG`) pushes the left stick DEG degrees clockwise from forward at MAG (default 1) times --stick:
`stick@90+rt:1.5` is the stick to the right with the right trigger held (tools/le2-halcyon-run/galaxy-steps.ps1 steers the
galaxy map's ship so).

ME1's double-tap uses `r3` instead. These are actual mapped inputs, not a direct recenter
request; native button side effects still apply. `--live-head` leaves head tracking to
the runtime (only controller poses/inputs are replayed), so recenter's base-space query
cannot mistake a head-relative recorded pose for an absolute head pose. Use an owned,
backed-up test fixture. ME3's first Back-transition trial currently faults in native
post-processing before recenter completes (research/le3-recenter-transition-failure.md);
this recording capability is not acceptance of that gameplay route."""
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)

# The left stick per segment (x right, y forward).
SEGMENTS = [("forward", 0.0, 1.0), ("strafe right", 1.0, 0.0), ("back", 0.0, -1.0), ("strafe left", -1.0, 0.0),
            ("stand", 0.0, 0.0)]
# Plan-only segments (not in the default loop): the head lowered, the stick still.
EXTRA = [("crouch", 0.0, 0.0), ("lean", 0.0, 0.0), ("a", 0.0, 0.0), ("sprint", 0.0, 1.0),
         ("back-button", 0.0, 0.0), ("start-button", 0.0, 0.0), ("r3", 0.0, 0.0), ("l3", 0.0, 0.0), ("x", 0.0, 0.0),
         ("b", 0.0, 0.0)]


def head_orientation(yaw_deg, pitch_deg):
    """xyzw: turned yaw (+ right) about +Y, then pitched (+ up) about the turned +X (OpenXR: -Z ahead)."""
    y, p = math.radians(-yaw_deg) / 2.0, math.radians(pitch_deg) / 2.0
    qy = (0.0, math.sin(y), 0.0, math.cos(y))
    qp = (math.sin(p), 0.0, 0.0, math.cos(p))
    return (qy[3] * qp[0] + qy[0] * qp[3] + qy[1] * qp[2] - qy[2] * qp[1],
            qy[3] * qp[1] - qy[0] * qp[2] + qy[1] * qp[3] + qy[2] * qp[0],
            qy[3] * qp[2] + qy[0] * qp[1] - qy[1] * qp[0] + qy[2] * qp[3],
            qy[3] * qp[3] - qy[0] * qp[0] - qy[1] * qp[1] - qy[2] * qp[2])


def option(name, default):
    return float(sys.argv[sys.argv.index(name) + 1]) if name in sys.argv else default


def main():
    yaw = option("--yaw", 0.0)
    pitch = option("--pitch", 0.0)
    eye_h = option("--eye-height", 0.0)
    stick = option("--stick", 0.9)
    grip = 1.0 if "--grip" in sys.argv else 0.0
    live_head = "--live-head" in sys.argv
    tracking = sys.argv[sys.argv.index("--tracking") + 1] if "--tracking" in sys.argv else "both"
    if tracking not in ("both", "left", "right", "none"):
        print("--tracking both|left|right|none")
        return 2
    hands = sys.argv[sys.argv.index("--hands") + 1] if "--hands" in sys.argv else "chest"
    hand_yaw = option("--hand-yaw", 0.0)
    fire_text = sys.argv[sys.argv.index("--fire") + 1] if "--fire" in sys.argv else ""
    fire_every = float(fire_text.split(":")[0]) if fire_text else 0.0
    fire_hold = float(fire_text.split(":")[1]) if ":" in fire_text else 0.15
    crouch_drop = option("--crouch-drop", 0.6)
    lean_drop = option("--lean-drop", 0.17)
    lean_forward = option("--lean-forward", 0.35)
    if hands not in ("chest", "view"):
        print("--hands chest|view")
        return 2
    taken = {i + 1 for i, a in enumerate(sys.argv) if a in ("--yaw", "--pitch", "--eye-height", "--stick", "--plan", "--hands", "--hand-yaw", "--fire",
                                                             "--crouch-drop", "--lean-drop", "--lean-forward", "--tracking")}
    args = [a for i, a in enumerate(sys.argv) if i > 0 and not a.startswith("--") and i not in taken]
    if not args:
        print(__doc__)
        return 2
    out = args[0]
    segment = float(args[1]) if len(args) > 1 else 3.0
    hz = float(args[2]) if len(args) > 2 else 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    head = head_orientation(yaw, pitch)
    hand_q = head_orientation(yaw, 0.0)  # pointing where the body faces
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    right = (c, 0.0, s)       # the head's right, laid flat (tracking: +X right, -Z ahead)
    ahead = (s, 0.0, -c)

    def at(r, u, f):
        return (r * right[0] + f * ahead[0], eye_h + u, r * right[2] + f * ahead[2])

    eyes = [(*head, side * ipd / 2 * right[0], eye_h, side * ipd / 2 * right[2]) for side in (-1, 1)]
    side_m, down, ahead_m = (0.09, 0.08, 0.25) if hands == "view" else (0.15, 0.35, 0.30)
    left_hand = (*hand_q, *at(-side_m, -down, ahead_m))
    right_hand = (*head_orientation(yaw + hand_yaw, 0.0), *at(side_m, -down, ahead_m))
    valid = (1 << 4) | (1 << 5)  # gripValid, aimValid
    by_name = {name: (name, sx, sy) for name, sx, sy in SEGMENTS + EXTRA + [("lt", 0.0, 0.0), ("rt", 0.0, 0.0)]}

    def segment_of(text):
        """'forward+lt' or 'stick@45@0.5+a': the name as given, the stick of the part that moves it."""
        sx = sy = 0.0
        for part in text.strip().split("+"):
            if part.startswith("stick@"):
                fields = part.split("@")
                angle = math.radians(float(fields[1]))
                mag = float(fields[2]) if len(fields) > 2 else 1.0
                sx, sy = math.sin(angle) * mag, math.cos(angle) * mag
            else:
                _, px, py = by_name[part]
                if px or py:
                    sx, sy = px, py
        return (text.strip(), sx, sy)

    if "--plan" in sys.argv:
        plan_text = sys.argv[sys.argv.index("--plan") + 1]
        steps = [(segment_of(name), float(seconds)) for name, seconds in (part.rsplit(":", 1) for part in plan_text.split(","))]
    else:
        steps = [(seg, segment) for seg in SEGMENTS]
    frames = []  # the segment of each frame
    for seg, seconds in steps:
        frames += [seg] * int(seconds * hz)
    n = len(frames)
    with open(out, "wb") as f:
        f.write(HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000)))
        for i in range(n):
            t = i / hz
            name, sx, sy = frames[i]
            parts = set(name.split("+"))
            trigger = 1.0 if (fire_every > 0 and t >= 2.0 and (t - 2.0) % fire_every < fire_hold) or "rt" in parts else 0.0
            dy = -crouch_drop if "crouch" in parts else -lean_drop if "lean" in parts else 0.0
            df = lean_forward if "lean" in parts else 0.0

            def lowered(pose):  # xyzw then xyz: the position's y lowered by dy, moved df ahead (a lean)
                return (*pose[:4], pose[4] + df * ahead[0], pose[5] + dy, pose[6] + df * ahead[2])

            e0, e1, lh, rh = lowered(eyes[0]), lowered(eyes[1]), lowered(left_hand), lowered(right_hand)
            flags = (0 if live_head else 1) | (0 if tracking == "none" else 2)
            f.write(FRAME.pack(int(t * 1e9), flags, *e0, *fov, *e1, *fov,
                               (valid if tracking in ("both", "left") else 0) | (8 if parts & {"back-button", "start-button"} else 0) | (2 if "x" in parts else 0)
                               | (1 if "l3" in parts else 0),
                               1.0 if "lt" in parts else 0.0,
                               1.0 if "back-button" in parts else 0.0, sx * stick, sy * stick, *lh, *lh,
                               (valid if tracking in ("both", "right") else 0) | (2 if parts & {"a", "sprint"} else 0) | (1 if "r3" in parts else 0) | (4 if "b" in parts else 0),
                               trigger, grip, 0.0, 0.0, *rh, *rh))
    starts = [sum(seconds for _, seconds in steps[:k]) for k in range(len(steps))]
    plan = ", ".join(f"{seg[0]} {start:g}-{start + seconds:g} s" for (seg, seconds), start in zip(steps, starts))
    print(f"{out}: {n} frames at {hz:g} Hz: {plan}; head yaw {yaw:g}, pitch {pitch:g}, stick {stick:g}, hands {hands}"
          f"; tracking {tracking}{', live runtime head' if live_head else ''}{', right grip held' if grip else ''}{f', right hand turned {hand_yaw:g}' if hand_yaw else ''}"
          f"{f', trigger {fire_hold:g} s every {fire_every:g} s' if fire_every else ''}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
