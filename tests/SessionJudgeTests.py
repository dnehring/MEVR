# SPDX-License-Identifier: MIT
"""Tests for tools/session-judge.py (the known bad signs in a play session's log, lane SMOKE 2026-11-19), over synthetic log
lines written in the mod's own wording (no player log, no game data). Run: python -I tests/SessionJudgeTests.py (ctest
mevr-session-judge)."""
import importlib.util
import io
import json
import os
import shutil
import sys
import tempfile
from contextlib import redirect_stdout

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('session_judge', os.path.join(HERE, '..', 'tools', 'session-judge.py'))
sj = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sj)
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print('FAILED:', what)


def at(t):
    return f"[20:{40 + t // 60:02d}:{t % 60:02d}.000]"


def line(t, text, level='info'):
    return f"{at(t)} [{level}] {text}"


def line_ms(t, ms, text, level='info'):
    """A line at second t plus ms milliseconds (the torso's lines within a tick of the posture's)."""
    return f"[20:{40 + t // 60:02d}:{t % 60:02d}.{ms:03d}] [{level}] {text}"


# A fight on foot with every sign that bit on 2026-11-18, in the mod's wording.
BAD = [
    line(0, "arm IK: driving: left and right following the controllers, shoulders under the headset"),
    line(1, "arm IK: body anchor: first person (the view's frame), view 30 units from Shepard's animated head bone, 100.0 units per meter"),
    line(2, "arm IK: body anchor: third person (tracking origin at Shepard's animated head), view 75 units from Shepard's animated head bone, 100.0 units per meter"),
    line(3, "arm IK: body anchor: first person (the view's frame), view 44 units from Shepard's animated head bone, 100.0 units per meter"),
    line(4, "arm IK: body anchor: third person (tracking origin at Shepard's animated head), view 76 units from Shepard's animated head bone, 100.0 units per meter"),
    line(5, "arm IK: body anchor: first person (the view's frame), view 44 units from Shepard's animated head bone, 100.0 units per meter"),
    line(6, "arm IK: body anchor: third person (tracking origin at Shepard's animated head), view 75 units from Shepard's animated head bone, 100.0 units per meter"),
] + [
    line(10 + i, "traversal: next to CoverLink_5 slot 1 (vault over to CoverLink_3 slot 1): the pawn 0.56 m from the slot, facing 17 deg "
         "off its direction (dot 0.96); the game refuses it (CanPerformMantle: within 50 uu, dot >= 0.70, the landing clear)") for i in range(4)
] + [
    line(14, "traversal: A pressed (#2): first tap: nothing to vault or climb from here (3.10 s after the one before)"),
    line(15, "traversal: double tap with nothing the game admits here (CoverLink_5 slot 1 at 0.56 m)"),
    line(16, "holsters: let go 0.20 s ago away from hipLeft: the shotgun stays out: the game won't holster it now (in combat: SFXGameModeDefault.TryHolster refuses then)"),
    line(17, "holsters: the sniper rifle came out without a holster grab (right trigger 0.00, grip 0.00; left trigger 0.00, grip 0.00): not through DrawPrimaryWeapon, so it wasn't refused"),
    line(18, "torso: legs: keep the run (400 u/s, weapon drawn, weight 1.00): shoulders -7 deg from the legs"),
    line(19, "torso: crouching with you: the body down 17 cm to your head (Shepard's head 17 cm too high), the knees bending, the feet on the floor"),
    line(20, "process event: off the game thread: Function SFXGame.BioPawn.SetLookAtTarget on thread 1234 (3 calls, 1 new; research/36)"),
    line(21, "swapchains: sight 1024x512 refused (XrResult -10, the runtime's limit); 16 of ours live, 2 freed"),
    line(22, "first-chance access violation 0xC0000005 on thread 77 at LE2MEVR_mod.dll+0x1234 (reading 0x10)", 'warn'),
    line(23, r"crash breadcrumbs (also in %LOCALAPPDATA%\MEVR\crash-breadcrumbs.txt):", 'error'),
    "==== MEVR crash breadcrumbs (research/36) ====",
    "exception 0xC0000005 (access violation) at ucrtbase.dll+0x6A80D, reading 0xFFFFFFFFFFFFFFF8",
    "  [0] ucrtbase.dll+0x6A80D  (rsp 0x1)",
    "  [1] MassEffect2.exe+0x8C6915  (rsp 0x2)",
    "==== end of crash breadcrumbs ====",
] + [
    line(24, "first-chance access violation 0xC0000005 on thread 136492 at ucrtbase.dll+0x6A80D (reading 0xFFFFFFFFFFFFFFF8)", 'warn'),
    line(25, "reflect: field SFXGame.SFXCameraMode.bIsCameraShakeEnabled refused on 0x2EB9A2D8620 (Class Engine.Thing): not an object "
         "of that class (a pointer kept after the engine freed its object, the memory now another object's?)", 'warn'),
    line(26, "traversal: A pressed (#4): a press that can't vault: the scene blocks traversal: an authored scene moves Shepard (a "
         "matinee's move track) (2.00 s after the one before)"),
    line(27, "traversal: A pressed (#5): a press that can't vault: the scene blocks traversal: an authored scene moves Shepard (a "
         "matinee's move track) (1.00 s after the one before)"),
    line(28, "holsters: the game didn't holster the assault rifle within 3 s (combat disabled here, the pawn busy, or mid-switch); now "
         "holding the assault rifle", 'warn'),
    line(29, "traversal: the step to CoverLink_5 slot 1 ended after 1.10 s, 0.52 m walked, the pawn 0.71 m from the slot, the view "
         "0.40 m, the stick written 30 times: nothing nearer for a second (a wall, a person, the slot's edge)"),
] + [
    line(30 + i, "vr cover: a hit from SFXPawn_AssaultDrone_1 at 23.2 m: exposed | the eyes 1.75 m over the floor") for i in range(3)
] + [
    line(40, "vr cover: 64 hits judged since the start ([Cover] judge = either): 0 in cover (0 by the level look alone, 0 cut by the "
         "game's cone), 64 in the open, 0 with cover too far, 0 from overhead, 0 without a shooter, 0 without a view; 10 judged from "
         "Shepard's crouched eyes (0 of them in cover), 4 with Shepard crouched but his crouched eyes unread (the real eyes judged); "
         "last: x"),
    # Shepard crouched by the game, his crouched eyes unread: the real eyes judged (research/23 section 7.8).
    line(41, "vr cover: a hit from SFXPawn_AssaultDrone_1 at 14.0 m: exposed | judged from the real eyes 1.85 m over the floor, "
         "Shepard crouched but his crouched eye height unread, the shooter's eyes +0.40 m from theirs (+2 deg)"),
] + [
    line(50 + i, f"posture: physical {'crouch' if i % 2 == 0 else 'stand'}: eyes 1.63 m = 0.88 of the standing 1.85 m") for i in range(5)
] + [
    line(60 + i * 5, "heartbeat #1: 30.8 fps, GWorld=0x1, objects=1, script hooks live") for i in range(4)
]

# A quiet stretch: the same modules saying the good things, and the excused look-alikes.
CLEAN = [
    line(0, "arm IK: driving: left and right following the controllers, shoulders under the headset"),
    line(1, "arm IK: body anchor: first person (the view's frame), view 30 units from Shepard's animated head bone, 100.0 units per meter"),
    line(2, "traversal: next to CoverLink_10 slot 0 (vault over to CoverLink_14 slot 1): the pawn 0.30 m from the slot, facing 5 deg "
         "off its direction (dot 0.99); the game admits it (CanPerformMantle: within 50 uu, dot >= 0.70, the landing clear)"),
    line(2, "traversal: next to CoverLink_41 slot 0 (vault over to CoverLink_15 slot 3): the pawn 1.52 m from the slot, facing 0 deg "
         "off its direction (dot 1.00); the game refuses it (CanPerformMantle: within 50 uu, dot >= 0.70, the landing clear)"),
    line(2, "traversal: no slot that vaults or climbs within 1.5 m any more"),
    line(3, "traversal: A pressed (#3): first tap: nothing to vault or climb from here (9.00 s after the one before)"),
    line(3, "holsters: let go of the pistol away from its slot: it goes back in 0.35 s unless you squeeze again"),
    line(4, "holsters: the console: the pistol goes back in its holster (in combat, where SFXGameModeDefault.TryHolster refuses: its own last step, BioPawnBehavior.SetActionState(explore))"),
    # The player's own crouch lowers the body: not a sign.
    line(5, "posture: physical crouch: eyes 1.20 m = 0.65 of the standing 1.85 m, below 0.75 for 0.20 s"),
    line(6, "torso: crouching with you: the body down 30 cm to your head (Shepard's head 30 cm too high), the knees bending, the feet on the floor"),
    line(7, "posture: physical stand: eyes 1.80 m = 0.97 of the standing 1.85 m, above 0.85 for 0.20 s"),
    line(8, "torso: crouching with you: the body down 2 cm to your head (Shepard's head 2 cm too high), the knees bending, the feet on the floor"),
    # The scope capture's guarded texture search faults by design, in one burst before its result line.
    line(9, "first-chance access violation 0xC0000005 on thread 5 at LE2MEVR.asi+0xF4E5F (reading 0x40000000400)", 'warn'),
    line(9, "scope capture source: the render target's D3D11 texture: resource 0x1 +0xA0 -> +0x8 = 0x2, 1024x1024 format 10"),
    # Off foot, a third-person anchor is the cinematic's.
    line(10, "arm IK: not on foot"),
    line(11, "arm IK: body anchor: third person (tracking origin at Shepard's animated head), view 300 units from Shepard's animated head bone, 100.0 units per meter"),
    line(12, "arm IK: driving: left and right following the controllers, shoulders under the headset"),
    # A press a menu or a wheel took is expected, and a load's own drawn gun goes back (notes, not signs).
    line(13, "traversal: A pressed (#6): a press that can't vault: the scene blocks traversal: the power wheel is open"),
    line(14, "traversal: A pressed (#7): a press that can't vault: the scene blocks traversal: the squad's weapon wheel is open"),
    line(15, "holsters: the assault rifle came out without a holster grab (right trigger 0.00, grip 0.00; left trigger 0.00, grip "
         "0.00): with the load (0.2 s after this pawn came: the save's drawn gun, or the level's start)"),
    line(15, "holsters: the assault rifle is holstered after 0.15 s"),
    line(16, "camera shake: the game's screen shakes back on (0 of 2 camera modifiers; VR is off); 1 kept now hold another object (a "
         "load freed them and reused the memory): left alone"),
    # A step to the vault that vaulted, and one the player's own stick took over: no sign.
    line(17, "traversal: the step to CoverLink_5 slot 1 ended after 0.26 s, 0.78 m walked, the pawn 0.35 m from the slot, the view "
         "0.27 m, the stick written 8 times: the game admits the vault from here"),
    line(18, "traversal: the step to CoverLink_5 slot 1 ended after 0.03 s, 0.00 m walked, the pawn 1.35 m from the slot, the view "
         "1.00 m, the stick written 1 times: the player's own stick took over"),
    line(20, "vr cover: 30 hits judged since the start ([Cover] judge = either): 12 in cover (0 by the level look alone, 0 cut by the "
         "game's cone), 18 in the open, 0 with cover too far, 0 from overhead, 0 without a shooter, 0 without a view; last: x"),
    line(30, "heartbeat #1: 90.0 fps, GWorld=0x1, objects=1, script hooks live"),
    # The torso follows the head down before the physical crouch's posture line (an eased descent: research/le2-vrik.md, lane
    # POS) and up after the stand's: not a sign.
    line_ms(40, 600, "torso: crouching with you: the body down 4 cm to your head (Shepard's head 4 cm too high), the knees bending, the feet on the floor"),
    line_ms(41, 0, "torso: crouching with you: the body down 6 cm to your head (Shepard's head 6 cm too high), the knees bending, the feet on the floor"),
    line_ms(41, 0, "posture: physical crouch: eyes 1.42 m = 0.76 of the standing 1.86 m, below 0.87 for 0.20 s"),
    line_ms(44, 0, "posture: physical stand: eyes 1.80 m = 0.97 of the standing 1.86 m, above 0.92 for 0.20 s"),
    line_ms(44, 500, "torso: crouching with you: the body down 5 cm to your head (Shepard's head 5 cm too high), the knees bending, the feet on the floor"),
    # Shepard crouched by L3, judged from his crouched eyes, covered.
    line(46, "vr cover: a hit from SFXPawn_AssaultDrone_2 at 12.1 m: covered by StaticMeshCollectionActor_12 0.48 m from the eyes, "
         "facing (0.10, 0.99); the game's cone cuts it to nothing | judged from Shepard's crouched eyes 0.92 m over the floor (the "
         "real eyes 0.93 m higher), the shooter's eyes +0.90 m from theirs (+4 deg)"),
]

BAD_RULES = ["armik-third-person-on-foot", "armik-anchor-flapping", "traversal-refused-at-slot", "traversal-double-tap-refused",
             "traversal-press-refused-near-slot", "vr-cover-crouched-eyes-unread",
             "holster-stays-out", "holster-unasked-draw", "crouchbody-drop-not-crouched", "process-event-off-thread",
             "swapchain-refused", "first-chance-av-ours", "vr-cover-never-covered", "posture-flip-flop", "frame-rate-low",
             "crash-report", "engine-free-corrupt-pointer", "reflect-field-misfit", "traversal-scene-blocked-matinee",
             "holster-not-taken", "traversal-step-failed"]
QUIET_RULES = [r for r in BAD_RULES if r != "frame-rate-low"] + ["frame-rate-low"]

root = tempfile.mkdtemp(prefix=f"mevr-tests-{os.getpid()}-")
try:
    bad = os.path.join(root, 'bad.log')
    clean = os.path.join(root, 'clean.log')
    with open(bad, 'w', encoding='utf-8') as f:
        f.write("\n".join(BAD) + "\n")
    with open(clean, 'w', encoding='utf-8') as f:
        f.write("\n".join(CLEAN) + "\n")
    out_json = os.path.join(root, 'bad.json')

    buffer = io.StringIO()
    with redirect_stdout(buffer):
        status = sj.main(['--mod', bad, '--json', out_json, '--expect', ",".join(BAD_RULES)])
    check(status == 0, "every bad rule fires on the synthetic fight:\n" + buffer.getvalue())
    with open(out_json, encoding='utf-8') as f:
        found = {x['id']: x for x in json.load(f)['findings']}
    check(found.get('traversal-refused-at-slot', {}).get('summary', '').startswith('CoverLink_5 slot 1 -> CoverLink_3 slot 1 at 0.56 m, 17 deg x4'),
          "the refused vault names its slot and count: " + str(found.get('traversal-refused-at-slot')))
    check(found.get('vr-cover-never-covered', {}).get('summary', '').startswith('0 of 64 hits covered'),
          "VR cover's ratio from its heartbeat: " + str(found.get('vr-cover-never-covered')))
    check("10 judged from Shepard's crouched eyes, 0 of them covered" in found.get('vr-cover-never-covered', {}).get('summary', ''),
          "VR cover's ratio names the hits judged from Shepard's crouched eyes: " + str(found.get('vr-cover-never-covered')))
    check(found.get('vr-cover-crouched-eyes-unread', {}).get('count') == 2,
          "the crouched hits judged from the real eyes (a hit's line and the heartbeat): " + str(found.get('vr-cover-crouched-eyes-unread')))
    check(found.get('armik-third-person-on-foot', {}).get('count') == 3, "three third-person anchors on foot")
    check(found.get('traversal-press-refused-near-slot', {}).get('count') == 2,
          "the first tap and the double tap refused next to the slot: " + str(found.get('traversal-press-refused-near-slot')))
    check(any('ucrtbase.dll+0x6A80D' in e for e in found.get('crash-report', {}).get('examples', [])),
          "the crash report shows its exception line: " + str(found.get('crash-report')))
    check(found.get('posture-flip-flop', {}).get('count') == 5, "five posture changes in one window")
    check(found.get('warn:first-chance access violation 0xC0000005 on thread 77 at LE2MEVR_mod.dll+0x1234 (reading 0x10)') is None
          and any(k.startswith('warn:') for k in found), "warn lines are grouped by their module, not one entry per line")

    buffer = io.StringIO()
    with redirect_stdout(buffer):
        status = sj.main(['--mod', clean, '--expect-quiet', ",".join(QUIET_RULES)])
    check(status == 0, "a clean stretch fires none of them:\n" + buffer.getvalue())

    # --since and --until cut the log.
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        status = sj.main(['--mod', bad, '--since', '20:40:10', '--until', '20:40:14', '--expect', 'traversal-refused-at-slot',
                          '--expect-quiet', 'armik-third-person-on-foot,holster-stays-out'])
    check(status == 0, "--since/--until keep only the window:\n" + buffer.getvalue())

    # The torso's drop and the crouch's window (research/le2-vrik.md, lane POS: the drop logged in the same tick as the physical
    # crouch, before its posture line): within 1 s ahead of the crouch it is the descent; 1.5 s ahead, or 1.5 s after a stand, it
    # is a body lowered while standing.
    window = os.path.join(root, 'window.log')
    with open(window, 'w', encoding='utf-8') as f:
        f.write("\n".join([
            line_ms(0, 0, "torso: crouching with you: the body down 5 cm to your head (Shepard's head 5 cm too high), the knees bending, the feet on the floor"),
            line_ms(1, 500, "posture: physical crouch: eyes 1.42 m = 0.76 of the standing 1.86 m, below 0.87 for 0.20 s"),
            line_ms(5, 0, "posture: physical stand: eyes 1.80 m = 0.97 of the standing 1.86 m, above 0.92 for 0.20 s"),
            line_ms(6, 500, "torso: crouching with you: the body down 5 cm to your head (Shepard's head 5 cm too high), the knees bending, the feet on the floor"),
        ]) + "\n")
    window_json = os.path.join(root, 'window.json')
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        status = sj.main(['--mod', window, '--json', window_json, '--expect', 'crouchbody-drop-not-crouched'])
    with open(window_json, encoding='utf-8') as f:
        windowed = {x['id']: x for x in json.load(f)['findings']}
    check(status == 0 and windowed.get('crouchbody-drop-not-crouched', {}).get('count') == 2,
          "a drop 1.5 s ahead of a crouch and 1.5 s after a stand still counts:\n" + buffer.getvalue())

    # A rule that must fire and doesn't fails the call (the canary's contract).
    buffer = io.StringIO()
    with redirect_stdout(buffer):
        status = sj.main(['--mod', clean, '--expect', 'holster-stays-out'])
    check(status == 1, "--expect fails when the rule is quiet")
finally:
    shutil.rmtree(root, ignore_errors=True)

if failures:
    print(f"{len(failures)} session judge test(s) failed")
    sys.exit(1)
print("session judge tests passed")
