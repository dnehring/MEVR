# SPDX-License-Identifier: MIT
"""Writes a synthetic .mevrrec (src/core/vr/PoseRecording.hpp v1, see tools/mevrrec.py): a standing player who raises the
left wrist in front of the chest as if reading a watch, looks down at it, and taps it with the right index fingertip at the
given times (research/57, the omni-tool: the tap opens the rail and the game's omni-tool, a second tap closes them). Between
taps the right hand rests low beside the body. For checking the omni-tool's wrist screens and rail on the fake headset.

Usage: python tools/make-wrist-recording.py out.mevrrec [seconds 60] [taps 4] [eye height 0] [look wrist] [B presses] [hovers] [beside]
  taps:       the tap times in seconds, comma-separated ("4" opens once; "4,40" opens, then closes; "none" holds the wrist up)
  eye height: the head's height in tracking space (0 on the fake headset, whose null driver puts the head at the origin)
  look:       "wrist" turns the head to look at the raised wrist (yaw and pitch); a number only pitches it that many
              degrees (negative is down)
  B presses:  times (comma-separated) the right controller's B is held 0.2 s; replay with -Inputs for the game to see it
  hovers:     times (comma-separated) the fingertip comes to 0.06 m beside the target and stays 3 s without tapping (the
              target's armed look, for a snapshot)
  beside:     how far beside the target a hover stays (0.06 m; 0 rests on it: a tap, then held there)
The tap (core/vr/OmniRail's Tap): the fingertip, 0.035 m along the right aim ray, comes from farther than 0.08 m to within
0.04 m of the tap's target (OmniRail's TargetPoint: 0.035 m below the wrist point, the left grip pose plus [Hud]
wristOffset, -0.03 0.06 0.05 in its axes by default).
Then: tools/replay.ps1 -Path out.mevrrec -Loop (poses only; no -Inputs: no buttons), with [Pointer] setCursor = 0 so the
replayed rays never move the desktop's cursor; tools/snapshot.ps1 after the tap; tools/replay.ps1 -Stop.

Shepard's drawn arm ([OmniTool] tapOnBody = 1, the default; research/le2-menus.md "The tap on Shepard's drawn arm"): once the
mod sends his arm bones (HudPieces) the tap's target sits on his drawn left forearm and the tapping fingertip is his drawn right
index, which arm IK puts 11-20 cm from the controllers' points above. Two flags aim at it, in two passes:
  --probe        no taps: from 2 s the right hand comes `beside` (0.06 m) to the right of the aim point and stays there to the
                 end, so a snapshot (tools/snapshot.ps1) records the drawn target ("omniRail"."target"."at"), the drawn
                 fingertip ("tip") and the replayed right aim pose ("controllers"."right"."aim").
  --body FILE    a snapshot's JSON from such a probe: the aim point moves by (target - fingertip) from the right aim pose the
                 snapshot shows (A + T - P), and each tap sweeps 0.04 m on through it (so a short or long miss along the
                 approach still passes within the 0.04 m a tap needs). With --probe too: the probe holds beside the corrected
                 point (a second probe refines it).
A weapon drawn refuses the tap ("a weapon is drawn (holster it first)"): holster first (ME2: `mevr omni holster` on the
console). Without the flags the recording is the one this tool always wrote."""
import json
import math
import struct
import sys
import time

MAGIC = b"MEVRREC\0"
HEADER = struct.Struct("<8sIIIIq")
POSE = "7f"
FRAME = struct.Struct("<qI" + (POSE + "4f") * 2 + ("I4f" + POSE + POSE) * 2)

IDENTITY = (0.0, 0.0, 0.0, 1.0)  # looking along -Z (OpenXR: -Z forward, +Y up, +X right)
WRIST_OFFSET = (-0.03, 0.06, 0.05)  # [Hud] wristOffset's default, in the left grip's axes (identity here)
TIP_AHEAD = 0.035  # plugin/OmniToolRail's kTipAheadM
TARGET_BELOW = 0.035  # core/vr/OmniRail's kTargetBelowM: the tap's target below the wrist point
SWEEP_M = 0.04  # --body: a tap goes this far on through the aim point (OmniRail's TapSettings::pressM)
PROBE_FROM_S = 2.0  # --probe: the right hand sets off then, beside the aim point 1.5 s later


def lerp(a, b, f):
    return tuple(x + (y - x) * f for x, y in zip(a, b))


def smooth(f):
    f = min(max(f, 0.0), 1.0)
    return f * f * (3.0 - 2.0 * f)


def head_quat(yaw_deg, pitch_deg):
    """Yaw about +Y (positive turns left), then pitch about the head's own X (negative looks down); (x, y, z, w)."""
    sy, cy = math.sin(math.radians(yaw_deg) / 2.0), math.cos(math.radians(yaw_deg) / 2.0)
    sp, cp = math.sin(math.radians(pitch_deg) / 2.0), math.cos(math.radians(pitch_deg) / 2.0)
    return (cy * sp, sy * cp, -sy * sp, cy * cp)


def body_correction(path):
    """(A + T - P): the right aim pose a snapshot shows, moved by its drawn target minus its drawn fingertip."""
    with open(path, encoding="utf-8") as f:
        snap = json.load(f)
    target = (snap.get("omniRail") or {}).get("target") or {}
    at, tip = target.get("at"), target.get("tip")
    aim = (((snap.get("controllers") or {}).get("right") or {}).get("aim") or {}).get("position")
    if not at or not tip or not aim:
        sys.exit(f"{path}: no omniRail.target.at/tip or controllers.right.aim.position (a snapshot from a host with the tip; "
                 "taken while a --probe recording replayed)")
    if not target.get("onBody"):
        print(f"{path}: warning: the target wasn't on Shepard's drawn arm (onBody false): the mod's arm bones didn't reach the host",
              file=sys.stderr)
    corrected = tuple(a + t - p for a, t, p in zip(aim, at, tip))
    print(f"{path}: drawn target ({at[0]:.3f}, {at[1]:.3f}, {at[2]:.3f}), drawn fingertip ({tip[0]:.3f}, {tip[1]:.3f}, {tip[2]:.3f}) "
          f"{math.dist(at, tip) * 100:.1f} cm off it, right aim ({aim[0]:.3f}, {aim[1]:.3f}, {aim[2]:.3f}): the aim point "
          f"({corrected[0]:.3f}, {corrected[1]:.3f}, {corrected[2]:.3f})")
    return corrected


def main():
    args = list(sys.argv)
    probe = "--probe" in args
    if probe:
        args.remove("--probe")
    body = None
    if "--body" in args:
        i = args.index("--body")
        if i + 1 >= len(args):
            sys.exit("--body needs a snapshot's JSON")
        body = args[i + 1]
        del args[i:i + 2]
    out = args[1]
    seconds = float(args[2]) if len(args) > 2 else 60.0
    tap_times = args[3] if len(args) > 3 else "4"
    taps = [] if tap_times.lower() == "none" else [float(t) for t in tap_times.split(",") if t.strip()]
    eye_h = float(args[4]) if len(args) > 4 else 0.0
    look = args[5] if len(args) > 5 else "wrist"
    presses = [float(x) for x in (args[6] if len(args) > 6 else "").split(",") if x.strip()]
    hovers = [float(x) for x in (args[7] if len(args) > 7 else "").split(",") if x.strip()]
    beside = float(args[8]) if len(args) > 8 else 0.06
    if probe:
        taps, hovers = [], []
    hz = 90.0
    ipd = 0.064
    fov = (-0.785, 0.785, 0.785, -0.785)  # left, right, up, down (radians)
    # The left hand: low at the side, then raised before the chest, the wrist's back up (a watch read).
    left_rest = (-0.22, eye_h - 0.60, -0.15)
    left_up = (-0.10, eye_h - 0.32, -0.34)
    wrist_up = tuple(a + b for a, b in zip(left_up, WRIST_OFFSET))
    # The right hand: low at the side; tapping, the aim pose sits TIP_AHEAD behind the wrist along -Z (identity: the ray
    # points -Z), so the fingertip is on the tap's target; it comes from 0.15 m to the right.
    right_rest = (0.22, eye_h - 0.60, -0.15)
    target_up = (wrist_up[0], wrist_up[1] - TARGET_BELOW, wrist_up[2])
    right_on = (target_up[0], target_up[1], target_up[2] + TIP_AHEAD)
    if body is not None:
        right_on = body_correction(body)  # where the drawn fingertip meets the drawn target (--body)
    right_hover = (right_on[0] + beside, right_on[1], right_on[2])
    right_near = (right_on[0] + 0.15, right_on[1] - 0.03, right_on[2])
    # --body: each tap sweeps SWEEP_M on through the aim point along the approach (the correction is one probe's estimate).
    approach = tuple(o - n for o, n in zip(right_on, right_near))
    length = math.sqrt(sum(c * c for c in approach))
    right_past = tuple(o + c / length * SWEEP_M for o, c in zip(right_on, approach)) if body is not None else right_on
    # The head: turned to look at the raised wrist (as a person reading a watch does), or only pitched by `look` degrees.
    if look == "wrist":
        flat = math.hypot(wrist_up[0], wrist_up[2])
        yaw = math.degrees(math.atan2(-wrist_up[0], -wrist_up[2]))
        pitch = math.degrees(math.atan2(wrist_up[1] - eye_h, flat))
    else:
        yaw, pitch = 0.0, float(look)
    frames = []
    n = int(seconds * hz)
    for i in range(n):
        t = i / hz
        raise_f = smooth((t - 1.0) / 1.5)  # the wrist comes up over 1-2.5 s and stays
        left = lerp(left_rest, left_up, raise_f)
        head = head_quat(yaw * raise_f, pitch * raise_f)
        right = right_rest
        for tap in taps:
            # 1.0 s before the tap: over to near the wrist; 0.25 s in; 0.2 s on it; 0.25 s out; back to rest by +1.2 s.
            d = t - tap
            if -1.0 <= d < -0.25:
                right = lerp(right_rest, right_near, smooth((d + 1.0) / 0.75))
            elif -0.25 <= d < 0.0:
                right = lerp(right_near, right_on, smooth((d + 0.25) / 0.25))
            elif 0.0 <= d < 0.2:
                right = lerp(right_on, right_past, smooth(d / 0.2)) if body is not None else right_on
            elif 0.2 <= d < 0.45:
                right = lerp(right_past, right_near, smooth((d - 0.2) / 0.25))
            elif 0.45 <= d < 1.2:
                right = lerp(right_near, right_rest, smooth((d - 0.45) / 0.75))
        for hover in hovers:
            # 1.0 s over to near, 0.5 s to eside the target, held 3 s, then back to rest over 1 s.
            d = t - hover
            if 0.0 <= d < 1.0:
                right = lerp(right_rest, right_near, smooth(d))
            elif 1.0 <= d < 1.5:
                right = lerp(right_near, right_hover, smooth((d - 1.0) / 0.5))
            elif 1.5 <= d < 4.5:
                right = right_hover
            elif 4.5 <= d < 5.5:
                right = lerp(right_hover, right_rest, smooth(d - 4.5))
        if probe:
            # --probe: over beside the aim point from 2 s (once the wrist is up) and held there to the end.
            d = t - PROBE_FROM_S
            if 0.0 <= d < 1.0:
                right = lerp(right_rest, right_near, smooth(d))
            elif 1.0 <= d < 1.5:
                right = lerp(right_near, right_hover, smooth((d - 1.0) / 0.5))
            elif d >= 1.5:
                right = right_hover
        eyes = [(*head, side * ipd / 2, eye_h, 0.0) for side in (-1, 1)]
        left_grip = (*IDENTITY, *left)
        left_aim = (*IDENTITY, left[0], left[1] + 0.05, left[2] - 0.02)
        right_grip = (*IDENTITY, right[0], right[1] - 0.05, right[2] + 0.02)
        right_aim = (*IDENTITY, *right)
        buttons = (1 << 4) | (1 << 5)  # gripValid, aimValid
        b_down = any(0.0 <= t - p < 0.2 for p in presses)  # the right controller's B (secondary), with replay -Inputs
        frame = FRAME.pack(int(t * 1e9), 3,
                           *eyes[0], *fov, *eyes[1], *fov,
                           buttons, 0.0, 0.0, 0.0, 0.0, *left_grip, *left_aim,
                           buttons | ((1 << 2) if b_down else 0), 0.0, 0.0, 0.0, 0.0, *right_grip, *right_aim)
        frames.append(frame)
    header = HEADER.pack(MAGIC, 1, HEADER.size, FRAME.size, 0, int(time.time() * 1000))
    with open(out, "wb") as f:
        f.write(header)
        for fr in frames:
            f.write(fr)
    print(f"{out}: {n} frames, {seconds} s at {hz} Hz; the left wrist up from 2.5 s at ({wrist_up[0]:.2f}, {wrist_up[1]:.2f}, "
          f"{wrist_up[2]:.2f}) m, the head turned {yaw:.0f} and pitched {pitch:.0f} degrees; taps at {', '.join(f'{t:g}' for t in taps)} s")
    if probe:
        print(f"  probe: the right aim pose held at ({right_hover[0]:.3f}, {right_hover[1]:.3f}, {right_hover[2]:.3f}) from "
              f"{PROBE_FROM_S + 1.5:g} s: snapshot it, then --body <the snapshot's .json>")
    elif body is not None:
        print(f"  the taps aimed at ({right_on[0]:.3f}, {right_on[1]:.3f}, {right_on[2]:.3f}), sweeping {SWEEP_M:g} m on through it")


if __name__ == "__main__":
    main()
