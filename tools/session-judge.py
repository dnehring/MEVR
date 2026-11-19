# SPDX-License-Identifier: MIT
"""Judges a play session's logs for the bad signs we already know (lane SMOKE, 2026-11-19): the owner found ME2 bugs in the
headset that their own log already showed (VR cover counting 0 of 164 hits as covered, the vault refused at a barrier the
mission requires, arm IK falling to its third-person anchor at each crouch, holsters refusing in a fight), and nothing read it.

    python tools/session-judge.py --mod <Win64>/Logs/LE2MEVR.log [--halcyon %LOCALAPPDATA%/MELE2VR/MELE2VR_Log.txt]
        [--crash-dir %LOCALAPPDATA%/MEVR [--since-file-time ISO]] [--since HH:MM:SS] [--until HH:MM:SS]
        [--json out.json] [--expect rule,rule...] [--expect-quiet rule,rule...] [--all]

It prints one block per rule that fired: its severity (bad, warn, note), count, first and last times, why it matters and up to
three of its lines. Exit code 0; with --expect, 1 unless each named rule fired (the canary: the owner's real log, and the ctest's
synthetic one), with --expect-quiet, 1 if a named rule fired (the synthetic clean stretch).

Rules live in RULES below, one entry each: a plain one is an id, a severity, a regular expression over the line's text (after the
"[time] [level] " prefix) and a why. A rule may add `when` (a state the line must be in: the judge keeps a little state as it
reads, see State), `skip` (a regex of lines it never counts), `minimum` (fires only from that many lines on) and `detail` (a
function of the match that adds to the block's summary; `detail_state` the same with the judge's state). Rules that need more than lines (the VR cover ratio, flip-flops within a
window) are functions in AGGREGATES. Add a sign as one entry; the ctest (tests/session-judge/) shows it the lines it must catch.
"""
import argparse
import datetime
import json
import os
import re
import sys
from collections import Counter, defaultdict

LINE = re.compile(r"^\[(\d\d:\d\d:\d\d(?:\.\d+)?)\] \[(\w+)\] (.*)$")


class State:
    """What the judge knows at each line: on foot or not (arm IK's and every module's 'not on foot' lines), physically crouched or
    not (posture's lines), the last speed the legs logged and when."""

    def __init__(self):
        self.on_foot = None          # unknown until a module says
        self.physical_crouch = False
        self.last_speed = 0.0
        self.last_speed_t = -1e9
        self.crouch_starts = []      # every physical crouch's time and every stand's (judge_lines reads them ahead)
        self.stands = []

    def update(self, t, text):
        mode = re.search(r"\(game mode (\d+)", text)  # 'not on foot (game mode 7)', 'main menu: leave: ... (game mode 0, ...'
        if mode:
            self.mode = int(mode.group(1))
            self.on_foot = self.mode == 0
        if re.match(r"(arm IK|holsters|posture): not on foot", text) or text.startswith("arm IK: no player body"):
            self.on_foot = False
        elif (text.startswith("arm IK: driving") or text.startswith("arm IK: body anchor: first person") or
              text == "holsters: active") and getattr(self, "mode", 0) == 0:
            self.on_foot = True
        if text.startswith("posture: physical crouch") or text.startswith("posture: crouch request accepted"):
            self.physical_crouch = True
        elif text.startswith("posture: physical stand") or text.startswith("posture: stand request accepted"):
            self.physical_crouch = False
        m = re.search(r"legs: keep the run \((\d+) u/s", text)
        if m:
            self.last_speed, self.last_speed_t = float(m.group(1)), t

    def moving(self, t):
        return self.last_speed > 60.0 and t - self.last_speed_t <= 2.5

    def near_posture_change(self, t):
        """A physical crouch begins within CROUCH_LEAD_S after t, or a stand ended within CROUCH_LEAD_S before it: the torso
        follows the head down before the posture line (the detector's 0.2 s dwell, an eased descent: research/le2-vrik.md) and up
        after it."""
        return (any(t <= c <= t + CROUCH_LEAD_S for c in self.crouch_starts) or
                any(s <= t <= s + CROUCH_LEAD_S for s in self.stands))


# How long the torso's 'crouching with you' may lead a physical crouch's posture line (and trail a stand's): the detector's dwell
# (0.2 s) plus a descent eased over most of a second.
CROUCH_LEAD_S = 1.0
CROUCH_START = re.compile(r"^posture: (?:physical crouch|crouch request accepted)")
CROUCH_STAND = re.compile(r"^posture: (?:physical stand|stand request accepted)")


def seconds(stamp):
    h, m, s = stamp.split(":")
    return int(h) * 3600 + int(m) * 60 + float(s)


def crouch_drop(m, state, t):
    cm = int(m.group(1))
    return cm > 3 and not state.physical_crouch and not state.near_posture_change(t)


def first_chance_ours(m, state, t):
    return True


# id, severity, pattern, why, optional keys (when, skip, minimum, detail). The pattern is searched in the text after the prefix.
RULES = [
    dict(id="armik-third-person-on-foot", severity="bad",
         pattern=r"^arm IK: body anchor: third person",
         when=lambda m, s, t: s.on_foot is not False,
         why="arm IK fell to its third-person anchor while on foot: the drawn hands leave the controllers and the body is pulled "
             "toward Shepard's animated head (the owner's 'almost magnetic' pull at a barrier, at each crouch; research/29 "
             "\"The crouch that went third person\")"),
    dict(id="traversal-refused-at-slot", severity="warn",
         pattern=r"^traversal: next to (\S+ slot \d+) \(vault over to (\S+ slot \d+)\): the pawn ([\d.]+) m from the slot, facing "
                 r"(\d+) deg off its direction \(dot (-?[\d.]+)\); the game refuses it",
         # Close to the slot and facing it (a walk past at 1.5 m, or the far side after a vault, is not a try).
         when=lambda m, s, t: float(m.group(3)) <= 0.8 and int(m.group(4)) <= 50,
         detail=lambda m: f"{m.group(1)} -> {m.group(2)} at {m.group(3)} m, {m.group(4)} deg",
         why="the game refused the vault with Shepard close to a slot and facing it (ME2's CanPerformMantle: within 50 uu, "
             "facing dot >= 0.70, the landing clear): the player stands at the barrier and the game says no "
             "(research/le2-traversal.md)"),
    dict(id="traversal-double-tap-refused", severity="bad",
         pattern=r"^traversal: double tap with nothing the game admits here",
         why="a double A the game admitted nothing for: the player asked to vault and nothing happened"),
    dict(id="traversal-step-failed", severity="warn",
         pattern=r"^traversal: the step to (\S+ slot \d+) ended after ([\d.]+) s, ([\d.]+) m walked, the pawn ([\d.]+) m from the slot.*: "
                 r"(nothing nearer|it took too long|its slot is gone)",
         detail=lambda m: f"{m.group(1)}: {m.group(5)} at {m.group(4)} m",
         why="a double A stepped toward a slot and didn't vault: the walk stalled (a wall, a person, the slot's edge) or the game "
             "never admitted the vault on the way (the landing, the slot's flags): the player asked to vault and walked instead "
             "(research/le2-traversal.md \"The step to the vault\")"),
    dict(id="traversal-seated-in-gameplay", severity="warn",
         pattern=r"^traversal: A pressed \(#\d+\): a press that can't vault: authored seated scene blocks traversal",
         when=lambda m, s, t: s.on_foot is not False,
         minimum=2,
         why="A presses refused as an 'authored seated scene' while on foot (a log from before 2026-11-19: that wording covered "
             "menus, wheels, mode 7 screens and matinee moves alike; the owner's three of 2026-11-18 confirmed our squad menu. "
             "Newer logs name the reason: traversal-scene-blocked-matinee)"),
    dict(id="traversal-scene-blocked-matinee", severity="warn",
         pattern=r"^traversal: A pressed \(#\d+\): a press that can't vault: the scene blocks traversal: (an authored scene moves "
                 r"Shepard|no player pawn|no player controller|the mod's engine access isn't up)",
         when=lambda m, s, t: s.on_foot is not False,
         minimum=2,
         detail=lambda m: m.group(1),
         why="A presses on foot refused for a scene reason that should end with gameplay (a matinee's move track still taken "
             "for Shepard, or no pawn): the vault and melee stay off while the player walks (Mod.cpp ActionSceneWhy; research/"
             "le2-traversal.md \"The refusal that named the wrong reason\"). A menu, a wheel or not on foot is expected and not "
             "counted"),
    dict(id="reflect-field-misfit", severity="bad",
         pattern=r"^reflect: field (\S+) refused on 0x[0-9A-Fa-f]+ \(([^)]*)\): not an object of that class",
         detail=lambda m: f"{m.group(1)} on {m.group(2)}",
         why="a field asked of an object that isn't of its class: a pointer a module kept after the engine freed its object, the "
             "memory now another object's (refused: before 2026-11-19 such a write corrupted that object and ME2's third same-map "
             "reload crashed in the purge; research/le2-reload-crash.md). Find the module and keep the pointer as a ue3::Kept"),
    dict(id="kept-object-reused", severity="note",
         pattern=r"^(camera shake|camera collision): .*; (\d+) kept now hold another object",
         detail=lambda m: m.group(1),
         why="a pointer kept across a load now points at another object (the engine reused the memory): ue3::Kept left it alone "
             "(the restore that crashed ME2's third same-map reload, research/le2-reload-crash.md)"),
    dict(id="engine-free-corrupt-pointer", severity="bad",
         pattern=r"^first-chance access violation \S+ on thread \d+ at ucrtbase\.dll\+(0x[0-9A-Fa-f]+) \(reading "
                 r"0xFFFFFFFFFFFFFF[F][0-9A-F]\)",
         detail=lambda m: f"ucrtbase.dll+{m.group(1)}",
         why="the C runtime read 8 bytes before a pointer of 1 to 7 (_aligned_free's header): the engine freed an array whose data "
             "pointer someone had set to a small number, typically a bool written into memory a freed object had left (ME2's "
             "third same-map reload, 2026-11-19: a camera mode's bIsCameraShakeEnabled put back into another object's empty "
             "array; research/le2-reload-crash.md)"),
    dict(id="holster-stays-out", severity="bad",
         pattern=r"^holsters: (?:\S+: )?(?:the (?:let-go )?(.+?) stays out|let go .* the (.+?) stays out)",
         detail=lambda m: (m.group(1) or m.group(2) or "").strip(),
         why="a gun the player let go stayed in Shepard's hand (TryHolster refuses in combat; the let-go must put it back by "
             "SetActionState: research/le2-vrik.md \"A gun let go in a fight\")"),
    dict(id="holster-unasked-draw", severity="warn",
         pattern=r"^holsters: the (.+?) came out without a holster grab(?!.*: with the load)",
         detail=lambda m: m.group(1),
         why="a weapon came out with no grab at a holster (the wheels' Deactivated, OnCombatStart, Kismet: native draws no hook "
             "refuses): only holsters draw, every game (the owner, 2026-11-16)"),
    dict(id="holster-not-taken", severity="bad",
         pattern=r"^holsters: the game didn't holster the (.+?) within",
         detail=lambda m: m.group(1),
         why="the mod asked the game to holster a gun and it stayed out (ME2's TryHolster returns silently: a load's own drawn "
             "rifle, called mid-equip, 2026-11-19; research/le2-vrik.md \"The drawn gun after a load\")"),
    dict(id="holster-load-draw", severity="note",
         pattern=r"^holsters: the (.+?) came out without a holster grab .*: with the load",
         detail=lambda m: m.group(1),
         why="the save's own drawn gun after a load, put back as any unasked draw (only holsters draw; the open hand holds no "
             "gun)"),
    dict(id="holster-left-in-hand", severity="warn",
         pattern=r"^holsters: the (.+?) came out (?:again: left in the hand|a third time)",
         detail=lambda m: m.group(1),
         why="the game kept drawing the weapon and the holsters gave up putting it back"),
    dict(id="holster-game-draw-refused", severity="note",
         pattern=r"^holsters: the game wanted to draw your weapon \(([^)]*)\): refused",
         detail=lambda m: m.group(1).replace("Function ", ""),
         why="the game's own draw was refused (expected with onlyHolstersDraw; many in a fight mean the trigger or a button "
             "kept asking for a holstered gun)"),
    dict(id="crouchbody-drop-not-crouched", severity="bad",
         pattern=r"^torso: crouching with you: the body down (\d+) cm",
         when=crouch_drop,
         detail_state=lambda m, s, t: "while running" if s.moving(t) else "standing or walking",
         why="the body was lowered more than 3 cm 'to your head' while the player wasn't physically crouched (a run, the idle, "
             "the game's own crouch): the body sinks under the view (research/29 \"A run never lowers the body\"). Within "
             "1 s before a physical crouch's posture line, or after a stand's, it is the descent or the rise: not counted"),
    dict(id="vr-cover-crouched-head-unread", severity="bad",
         pattern=r"^vr cover: .*Shepard crouched but his crouched (?:head|eyes|eye height) unread",
         why="a hit with Shepard crouched by the game was judged from the real eyes because his crouched head or floor "
             "couldn't be read: crouched behind a low wall, the player counts as in the open (research/23 section 7.8: while "
             "the game has Shepard crouched, his crouched head judges)"),
    dict(id="process-event-off-thread", severity="bad",
         pattern=r"^process event: off the game thread",
         why="UnrealScript called off the game thread races the game thread's script VM (the 2026-10-23 crash, research/36)"),
    dict(id="crash-report", severity="bad",
         pattern=r"^crash breadcrumbs(?: \(also in|: the game's code faulted)",
         block=r"^(exception .*|  \[[0-9]\] .*)$",
         why="a crash report in the log: the exception and the innermost native frames below (crash-breadcrumbs.txt or "
             "crash-firstchance.txt hold the whole report: stacks, the last ProcessEvents, the mod's writes)"),
    dict(id="first-chance-av-ours", severity="warn",
         pattern=r"^first-chance access violation \S+ on thread \d+ at (LE\dMEVR(?:_mod)?\.(?:asi|dll))\+(0x[0-9A-Fa-f]+)",
         detail=lambda m: f"{m.group(1)}+{m.group(2)}",
         skip_context="scope capture source:",
         why="an access violation inside our own code (handled, but a guarded read that faults is a pointer we got wrong; the scope "
             "capture's texture search faults by design and is left out)"),
    dict(id="first-chance-av-game", severity="warn",
         pattern=r"^first-chance access violation \S+ on thread \d+ at (MassEffect\d\.exe)\+(0x[0-9A-Fa-f]+)",
         detail=lambda m: f"{m.group(1)}+{m.group(2)}",
         why="the game's own code faulted (crash-firstchance.txt has the report when it was at a null address)"),
    dict(id="swapchain-refused", severity="bad",
         pattern=r"^swapchains: (.+?) \d+x\d+ refused",
         detail=lambda m: m.group(1),
         why="the runtime refused a swapchain (the limit of 16 a session): a quad of ours goes missing until one frees "
             "(gotchas/openxr-swapchain-limit)"),
    dict(id="game-thread-call-faulted", severity="warn",
         pattern=r"^game thread calls: .* ([1-9]\d*) faulted",
         why="a call queued for the game thread faulted"),
    dict(id="posture-game-insists", severity="warn",
         pattern=r"^posture: the game (.+?) again soon after we put him back",
         detail=lambda m: m.group(1),
         why="the game undid the player's posture twice within 1.5 s: the crouch follows the game, not the player, there"),
    dict(id="posture-not-taken", severity="warn",
         pattern=r"^posture: the game didn't take the (\w+) after",
         detail=lambda m: m.group(1),
         why="a crouch or stand the mod asked for never happened"),
    dict(id="message-box-in-play", severity="note",
         pattern=r"^\[MEVR-CROSSHAIR\] tag=MessageBox",
         when=lambda m, s, t: s.on_foot is not False,
         why="a message box over play (Shepard's death, Critical Mission Failure, shows one; so do the game's prompts)"),
]

PREFIXED = re.compile(r"^([A-Za-z][A-Za-z0-9 \-]{1,40}?):")


def module_of(text):
    m = PREFIXED.match(text.strip())
    return m.group(1).strip() if m else text.strip().split(" ")[0]


class Finding:
    def __init__(self, rule):
        self.rule = rule
        self.count = 0
        self.first = self.last = None
        self.examples = []
        self.details = Counter()
        self.summary = ""

    def add(self, stamp, line, detail=None):
        self.count += 1
        self.first = self.first or stamp
        self.last = stamp
        if len(self.examples) < 3:
            self.examples.append(line[:400])
        if detail:
            self.details[detail] += 1


def read_lines(path):
    if not path or not os.path.exists(path):
        return []
    with open(path, "rb") as f:
        raw = f.read()
    text = raw.decode("utf-8-sig", errors="replace")
    return text.splitlines()


BLOCKS = {}  # id(entry) of a line -> the continuation lines after it (a crash report's block)


def parse(lines, since=None, until=None):
    """(stamp, seconds, level, text, raw) per log line; continuation lines go to BLOCKS under the line before them."""
    BLOCKS.clear()
    out = []
    kept = True
    for raw in lines:
        m = LINE.match(raw)
        if not m:
            if out and kept:
                BLOCKS.setdefault(len(out) - 1, []).append(raw)
            continue
        stamp, level, text = m.group(1), m.group(2), m.group(3)
        t = seconds(stamp)
        kept = not ((since is not None and t < since) or (until is not None and t > until))
        if kept:
            out.append((stamp, t, level, text, raw))
    return out


def judge_lines(entries):
    findings = {}
    state = State()
    for stamp, t, level, text, raw in entries:  # the posture changes ahead of each line (crouch_drop's window)
        body = text.strip()
        if CROUCH_START.match(body):
            state.crouch_starts.append(t)
        elif CROUCH_STAND.match(body):
            state.stands.append(t)
    compiled = [(r, re.compile(r["pattern"])) for r in RULES]
    for i, (stamp, t, level, text, raw) in enumerate(entries):
        body = text.strip()
        state.update(t, body)
        for rule, rx in compiled:
            m = rx.search(body)
            if not m:
                continue
            if "when" in rule and not rule["when"](m, state, t):
                continue
            if "skip_context" in rule:
                # Lines of the same burst (within 50 ms) followed by the context line are excused.
                near = [e for e in entries[i + 1:i + 30] if e[1] - t <= 0.05]
                if any(rule["skip_context"] in e[3] for e in near):
                    continue
            f = findings.setdefault(rule["id"], Finding(rule))
            detail = rule["detail"](m) if "detail" in rule else rule["detail_state"](m, state, t) if "detail_state" in rule else None
            f.add(stamp, raw, detail)
            if "block" in rule:
                rx_block = re.compile(rule["block"])
                f.examples += [b[:300] for b in BLOCKS.get(i, []) if rx_block.match(b)][:6]
    for f in findings.values():
        if f.details:
            f.summary = "; ".join(f"{k} x{v}" for k, v in f.details.most_common(6))
    return {k: f for k, f in findings.items() if f.count >= f.rule.get("minimum", 1)}


# Signs that need more than one line at a time. Each returns a Finding or None.
def vr_cover_ratio(entries):
    """The last 'N hits judged since the start: M in cover' (VR cover's heartbeat), and the per-hit verdicts: a fight where almost
    nothing counted as covered."""
    last = None
    verdicts = Counter()
    first_hit = last_hit = None
    examples = []
    crouched = None  # the heartbeat's hits judged from Shepard's crouched head and of them covered (research/23 section 7.8)
    for stamp, t, level, text, raw in entries:
        m = re.search(r"vr cover: (\d+) hits judged since the start(?: \([^)]*\))?: (\d+) in cover", text)
        if m:
            last = (stamp, int(m.group(1)), int(m.group(2)), raw)
            e = re.search(r"; (\d+) judged from Shepard's crouched (?:head|eyes) \((\d+) of them in cover\)", text)
            if e:
                crouched = (int(e.group(1)), int(e.group(2)))
        m = re.search(r"vr cover: a hit from (\S+) at ([\d.]+) m: ([a-z ]+?)(?: by | \(|; | \||$)", text)
        if m:
            verdicts[m.group(3)] += 1
            first_hit = first_hit or stamp
            last_hit = stamp
            if len(examples) < 2:
                examples.append(raw[:400])
    hits = last[1] if last else sum(verdicts.values())
    covered = last[2] if last else verdicts.get("covered", 0)
    if hits < 20 or covered > 0.05 * hits:
        return None
    rule = dict(id="vr-cover-never-covered", severity="bad",
                why="VR cover judged a fight's hits and almost none counted as covered: either the player never stood behind "
                    "anything or the judging misses the cover they used (the owner's 0 of 164, 2026-11-18; research/23 7.6-7.7)")
    f = Finding(rule)
    f.count = hits
    f.first, f.last = first_hit or (last[0] if last else None), last_hit or (last[0] if last else None)
    f.summary = f"{covered} of {hits} hits covered" + (f"; per hit: " + ", ".join(f"{k} {v}" for k, v in verdicts.most_common()) if verdicts else "")
    if crouched:
        f.summary += f"; {crouched[0]} judged from Shepard's crouched head, {crouched[1]} of them covered"
    f.examples = ([last[3][:400]] if last else []) + examples
    return f


def presses_refused_near_slot(entries):
    """A pressed for a vault that did nothing while a slot that vaults was in reach (the mod's 'next to' line, not yet cleared
    by 'no slot ... any more'): the player at a barrier asking to cross it (the owner's Freedom's Progress barrier)."""
    near = None
    found = []
    for stamp, t, level, text, raw in entries:
        m = re.match(r"traversal: next to (\S+ slot \d+) \(vault over to [^)]*\): the pawn ([\d.]+) m from the slot, facing (\d+) deg", text)
        if m:
            near = (m.group(1), float(m.group(2)), int(m.group(3)), t)
            continue
        if text.startswith("traversal: no slot that vaults or climbs within"):
            near = None
            continue
        if near and t - near[3] <= 10.0 and (
                re.match(r"traversal: A pressed \(#\d+\): first tap: nothing to vault or climb from here", text) or
                text.startswith("traversal: double tap with nothing the game admits here")):
            found.append((stamp, raw, near))
    if not found:
        return None
    rule = dict(id="traversal-press-refused-near-slot", severity="bad",
                why="the player pressed A for a vault with a slot in reach and nothing happened: the mod's reach and the game's "
                    "CanPerformMantle (0.5 m, facing within 45 degrees, from the pawn, not the head) are tighter than where the "
                    "player stands at the barrier")
    f = Finding(rule)
    f.count = len(found)
    f.first, f.last = found[0][0], found[-1][0]
    f.examples = [x[1][:400] for x in found[:3]]
    f.summary = "; ".join(f"{s}: {n[0]} at {n[1]:.2f} m, facing {n[2]} deg" for s, _, n in found[:6])
    return f


def posture_flips(entries):
    """Crouch and stand changes in quick succession: four or more within 10 s."""
    changes = [(stamp, t, raw) for stamp, t, level, text, raw in entries
               if re.match(r"posture: (physical (crouch|stand)|(crouch|stand) request accepted|the game .* while you)", text)]
    worst = []
    j = 0
    for i in range(len(changes)):
        while changes[i][1] - changes[j][1] > 10.0:
            j += 1
        if i - j + 1 >= 4 and i - j + 1 > len(worst):
            worst = changes[j:i + 1]
    if not worst:
        return None
    rule = dict(id="posture-flip-flop", severity="bad",
                why="the crouch flipped back and forth (four or more crouch/stand changes within 10 s): the game and the player's "
                    "posture fight, or the thresholds sit on the player's height")
    f = Finding(rule)
    f.count = len(worst)
    f.first, f.last = worst[0][0], worst[-1][0]
    f.examples = [w[2][:400] for w in worst[:3]]
    f.summary = f"{len(changes)} changes in all; the worst window {len(worst)} within {worst[-1][1] - worst[0][1]:.1f} s"
    return f


def anchor_flips(entries):
    """Arm IK's anchor switching often: each switch moves the body under the player."""
    switches = [(stamp, raw) for stamp, t, level, text, raw in entries if text.startswith("arm IK: body anchor:")]
    third = [s for s in switches if "third person" in s[1]]
    if len(third) < 3:
        return None
    rule = dict(id="armik-anchor-flapping", severity="bad",
                why="arm IK switched between its first- and third-person anchors again and again in one session")
    f = Finding(rule)
    f.count = len(switches)
    f.first, f.last = switches[0][0], switches[-1][0]
    f.examples = [t[1][:400] for t in third[:3]]
    f.summary = f"{len(switches)} anchor lines, {len(third)} to third person"
    return f


def low_frame_rate(entries):
    """The mod's heartbeat under 45 fps for three heartbeats or more."""
    low = []
    for stamp, t, level, text, raw in entries:
        m = re.match(r"heartbeat #\d+: ([\d.]+) fps", text)
        if m and float(m.group(1)) < 45.0:
            low.append((stamp, float(m.group(1)), raw))
    if len(low) < 3:
        return None
    rule = dict(id="frame-rate-low", severity="note",
                why="the game ran under 45 fps for a while (on the fake headset the null driver paces it: compare with the "
                    "owner's session before calling it a regression)")
    f = Finding(rule)
    f.count = len(low)
    f.first, f.last = low[0][0], low[-1][0]
    rates = sorted(r for _, r, _ in low)
    f.summary = f"{len(low)} heartbeats under 45 fps, median {rates[len(rates) // 2]:.1f}"
    f.examples = [low[0][2][:400]]
    return f


def warnings_by_module(entries):
    """Every warn and error line, grouped by the module that wrote it."""
    groups = defaultdict(list)
    for stamp, t, level, text, raw in entries:
        if level in ("warn", "warning", "error"):
            groups[(level, module_of(text))].append((stamp, raw))
    found = []
    for (level, module), items in sorted(groups.items(), key=lambda kv: -len(kv[1])):
        rule = dict(id=f"{level}:{module}", severity="warn" if level == "error" else "note",
                    why=f"{level} lines from '{module}'")
        f = Finding(rule)
        f.count = len(items)
        f.first, f.last = items[0][0], items[-1][0]
        f.examples = [raw[:400] for _, raw in items[:2]]
        found.append(f)
    return found


AGGREGATES = [vr_cover_ratio, presses_refused_near_slot, posture_flips, anchor_flips, low_frame_rate]


def halcyon_findings(lines):
    """Halcyon's log: lines that say error, failed or exception (its own words), grouped by their first words."""
    groups = defaultdict(list)
    for raw in lines:
        if re.search(r"\b(ERROR|Error|error|FAILED|failed|exception|Exception)\b", raw) and "0 failed" not in raw:
            key = re.sub(r"[0-9]+", "N", raw.strip())[:70]
            groups[key].append(raw)
    found = []
    for key, items in sorted(groups.items(), key=lambda kv: -len(kv[1]))[:8]:
        rule = dict(id="halcyon:" + key[:40], severity="note", why="a Halcyon log line that says error or failed")
        f = Finding(rule)
        f.count = len(items)
        f.examples = [items[0][:400]]
        found.append(f)
    return found


def crash_files(directory, since_iso):
    found = []
    if not directory:
        return found
    since = None
    if since_iso:
        try:
            since = datetime.datetime.fromisoformat(since_iso.replace("Z", "+00:00")).timestamp()
        except ValueError:
            since = None
    for name in ("crash-breadcrumbs.txt", "crash-firstchance.txt", "crash-exit.txt"):
        path = os.path.join(directory, name)
        if not os.path.exists(path):
            continue
        mtime = os.path.getmtime(path)
        if since is not None and mtime < since:
            continue
        rule = dict(id="crash-file:" + name, severity="bad" if name != "crash-exit.txt" else "warn",
                    why=f"{name} was written during this session: read it first (the exception, registers, stacks, last calls)")
        f = Finding(rule)
        f.count = 1
        f.first = f.last = datetime.datetime.fromtimestamp(mtime).strftime("%H:%M:%S")
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            f.examples = [line.rstrip()[:300] for line in fh.readlines()[:3]]
        found.append(f)
    return found


ORDER = {"bad": 0, "warn": 1, "note": 2}


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--mod", required=True, help="the mod's log (LE{n}MEVR.log or a copy)")
    ap.add_argument("--halcyon", help="Halcyon's log (MELE{n}VR_Log.txt or a copy)")
    ap.add_argument("--crash-dir", help="%%LOCALAPPDATA%%/MEVR: crash files written during the session count")
    ap.add_argument("--since-file-time", help="ISO time: crash files older than this are ignored")
    ap.add_argument("--since", help="HH:MM:SS: lines before are ignored")
    ap.add_argument("--until", help="HH:MM:SS: lines after are ignored")
    ap.add_argument("--json", help="write the findings as JSON")
    ap.add_argument("--expect", default="", help="rules that must fire (exit 1 otherwise)")
    ap.add_argument("--expect-quiet", default="", help="rules that must not fire (exit 1 otherwise)")
    ap.add_argument("--all", action="store_true", help="print the notes too (warn lines by module, Halcyon's errors)")
    args = ap.parse_args(argv)

    entries = parse(read_lines(args.mod), seconds(args.since) if args.since else None,
                    seconds(args.until) if args.until else None)
    findings = list(judge_lines(entries).values())
    for aggregate in AGGREGATES:
        f = aggregate(entries)
        if f:
            findings.append(f)
    findings += warnings_by_module(entries)
    findings += halcyon_findings(read_lines(args.halcyon)) if args.halcyon else []
    findings += crash_files(args.crash_dir, args.since_file_time)
    findings.sort(key=lambda f: (ORDER[f.rule["severity"]], -f.count))

    span = f"{entries[0][0]} to {entries[-1][0]}" if entries else "no lines"
    print(f"session judge: {args.mod}: {len(entries)} lines ({span})")
    counts = Counter(f.rule["severity"] for f in findings)
    print(f"  {counts.get('bad', 0)} bad, {counts.get('warn', 0)} warn, {counts.get('note', 0)} note")
    for f in findings:
        if f.rule["severity"] == "note" and not args.all and not f.rule["id"] in ("frame-rate-low", "holster-game-draw-refused", "message-box-in-play"):
            continue
        print(f"\n[{f.rule['severity']}] {f.rule['id']}: {f.count}" + (f" ({f.first} to {f.last})" if f.first else ""))
        print(f"  why: {f.rule['why']}")
        if f.summary:
            print(f"  {f.summary}")
        for e in f.examples:
            print(f"  | {e}")
    if args.json:
        with open(args.json, "w", encoding="utf-8") as out:
            json.dump({"log": args.mod, "lines": len(entries), "span": span,
                       "findings": [dict(id=f.rule["id"], severity=f.rule["severity"], count=f.count, first=f.first,
                                         last=f.last, why=f.rule["why"], summary=f.summary, examples=f.examples)
                                    for f in findings]}, out, indent=1)
    fired = {f.rule["id"] for f in findings}
    status = 0
    for rule in [r for r in args.expect.split(",") if r]:
        if rule not in fired:
            print(f"EXPECTED but not found: {rule}")
            status = 1
    for rule in [r for r in args.expect_quiet.split(",") if r]:
        if rule in fired:
            print(f"EXPECTED QUIET but found: {rule}")
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
