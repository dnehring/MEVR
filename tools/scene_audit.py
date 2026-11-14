# SPDX-License-Identifier: MIT
"""Rules over the stance records of first-person scenes (research/69 "Auditing scenes by rule"; the skill
`first-person-scene-audit`). The mod writes the records (`mevr convo stances on`, %LOCALAPPDATA%\\MEVR\\stances); this judges
them: findings per scene, a scene's numbers, and a baseline to compare a later run with. Used by tools/conversation-stance.py
(`--findings`, `--baseline-write`, `--baseline-compare`) and tests/SceneAuditTests.py.

"You" is where the player's eyes are drawn (`player.eyes`); records without it (before 2026-11-12 19:00) are not judged.
"""
import datetime
import math
import os

BODY_AWAY_M = 0.3      # Shepard's head bone this far (flat) from the drawn eyes: a look at him is no longer at the player
LOOK_AWAY_M = 0.5      # ... this far: the eyes stand-in should have taken the look (game/ConversationWorld StepEyes)
SETTLE_S = 1.5         # after a scene's start or a dip, the view and Shepard's mesh settle this long before the gap counts
FACE_OFF_DEG = 35.0    # a face meant for the player this far off his eyes, two records running: a finding
BEHIND_DEG = 120.0     # someone speaking to the player this far from where his body faces: he has to turn round
TOO_CLOSE_M = 0.5      # a person's centre this near the eyes
SQUAD_BEHIND_DEG = 150.0
WALK_PAUSES = 4        # a scripted walk that paused this often: the player's pace and the walk's do not match
WALK_LEFT_M = 5.0      # a walk with someone beside Shepard this far from the player and not paused: the pause did not hold
HANDS_OFF_M = 0.3      # a drawn wrist this far from its controller's grip (player.hands, arm_ik::MeasureHands): the body is not
                       # where the player is (research/69 "Seeing the body in the audit")
UNDRAWN_S = 1.0        # a mesh the player can see that no view drew for this long (its LastRenderTime; research/69 "Obvious bugs,
                       # measured"): the body in view (a drawn wrist inside the head's view), someone of the scene in sight
NEAR_PLANE_MAX = 15    # units: the frame's near plane over this cuts the floor and the body away from the eyes (on foot 10)

# Never excused by a baseline: Shepard's body away from the player's eyes. The council hearing's 0.70 m "known opening
# transient" went into the first baseline (2026-11-12), and in the owner's headset session the same opening left the body up to
# 5.7 m away for 225 of the hearing's 227 seconds (2026-11-13, research/69 "The body that stayed at the mark"): a baseline
# compares a run with the last one, it is no judgement of what the player sees.
ABSOLUTE = ('eyes-off-body', 'hands-off-controllers', 'body-not-drawn', 'near-plane')

RULES = {
    'look-at-body': (3, "a look meant for the player lands on Shepard's hidden body"),
    'eyes-off-body': (2, "Shepard's head bone stays away from the player's eyes after the scene settled"),
    'face-off-you': (2, "a face meant for the player points past him"),
    'speaker-behind': (2, 'someone speaking to the player stands behind him'),
    'too-close': (2, "someone stands within half a metre of the player's eyes"),
    'squad-behind': (1, 'a squadmate stands right behind the player'),
    'walk-pauses': (1, 'a scripted walk paused again and again'),
    'walk-left-behind': (3, 'a scripted walk with someone beside Shepard went on without the player'),
    'hands-off-controllers': (3, "Shepard's drawn hands stand away from the player's controllers: his body is not where the player is"),
    'body-not-drawn': (3, "the player's body is in view but no view drew it"),
    'person-not-drawn': (3, 'someone of the scene the player can see was not drawn'),
    'near-plane': (3, "the frame's near plane cuts away what stands near the eyes"),
}


def body_undrawn(st):
    """Seconds since a view drew the player's body while a drawn wrist stood inside the head's view (records since
    2026-11-13: `player.bodyDrawnAgoS`, `player.hands`), else None."""
    player = st['player']
    ago = player.get('bodyDrawnAgoS')
    hands = player.get('hands') or {}
    in_view = any(isinstance(h, dict) and h.get('inView') for h in (hands.get('left'), hands.get('right')))
    return ago if ago is not None and in_view and not player.get('detached') else None


def hands_off(st):
    """The drawn wrists' distance from the controllers, the larger of the two (metres), when both were measured (records since
    2026-11-13: `player.hands`, arm_ik::MeasureHands); else None."""
    hands = st['player'].get('hands') or {}
    sides = [hands.get('left'), hands.get('right')]
    if not all(isinstance(h, dict) and h.get('fromGripM') is not None for h in sides):
        return None
    return max(h['fromGripM'] for h in sides)


def body_gap(st):
    """How far (m, flat) Shepard's head bone stands from the player's eyes: the record's own measure, else (older records:
    the held place) from the people's list."""
    gap = st['player'].get('bodyFromEyesM')
    if gap is not None:
        return gap
    shep = next((q for q in st['people'] if q.get('shepard') and q.get('head')), None)
    return math.dist(shep['head'][:2], st['player']['at'][:2]) / 100 if shep else None


def looks_at(p, st):
    """Whom a face points at: the game's look-at target when the face points at where it aims (within 20 degrees), the
    player within 20, Shepard's body away from the player within 25 (and nearer it than the player), else None. (Two people
    standing close together are told apart by the game's target, not by the angle alone: Garrus looking at Girard beside
    Shepard's body, 2026-11-12.) A look at Shepard is a look at the player while his head bone is with the player's eyes."""
    face = p.get('face') or {}
    if not face or p.get('hidden') or p.get('shepard') or not p.get('head'):
        return None
    gap = body_gap(st)
    away = gap is not None and gap >= (BODY_AWAY_M if st['player'].get('eyes') else 1.0)
    look = p.get('gameLook') or {}
    aim, target = look.get('aim'), look.get('target')
    if aim and target and math.dist(aim[:2], p['head'][:2]) > 10:
        a = math.degrees(math.atan2(aim[1] - p['head'][1], aim[0] - p['head'][0]))
        if abs((face['yaw'] - a + 180) % 360 - 180) < 20:
            if target == 'you' or target.startswith('BioLookAtTarget'):
                return 'you'
            if target == 'Shepard':
                return "Shepard's body" if away else 'you'
            return target
    you, shep = face.get('toYouDeg'), face.get('toShepardDeg')
    if you is not None and you < 20:
        return 'you'
    if away and shep is not None and shep < 25 and (you is None or shep < you):
        return "Shepard's body"
    return None


def stamp_of(path):
    return datetime.datetime.strptime(os.path.basename(path)[:23], '%Y-%m-%d_%H-%M-%S-%f')


def group(stances):
    """Records (each with '_file') into scenes: by day, number and conversation (a moment by its number alone: its first record
    may come before its matinee is named), records more than two minutes apart split (the numbers start again each game).
    Returns {(day, number, name, part): [records by time]}, name the last record's conversation."""
    groups = {}
    for st in stances:
        name = 'moment' if st.get('opening') == 'moment' else st['conversation']
        groups.setdefault((os.path.basename(st['_file'])[:10], st['number'], name), []).append(st)
    split = {}
    for key, records in groups.items():
        records.sort(key=lambda s: s['_file'])
        part, last = 0, None
        for st in records:
            stamp = stamp_of(st['_file'])
            if last is not None and (stamp - last).total_seconds() > 120:
                part += 1
            last = stamp
            split.setdefault(key + (part,), []).append(st)
    return {(k[0], k[1], records[-1]['conversation'], k[3]): records for k, records in split.items()}


def scene_key(records):
    """A scene's name across runs: the conversation's last part; a moment by its matinee."""
    name = records[-1]['conversation']
    return name.split('.')[-1] if records[-1].get('opening') != 'moment' else name


def meant_for_you(p):
    target = (p.get('gameLook') or {}).get('target') or ''
    return target in ('Shepard', 'you') or target.startswith('BioLookAtTarget')


def bearing_off(st, p):
    """Degrees between the facing the scene gave the player (the held yaw without his own right-stick turns, `turnedDeg`: a
    speaker behind a player who turned away is his choice, not the composition's) and the bearing from his eyes to p."""
    e = st['player'].get('eyes') or st['player']['at']
    b = math.degrees(math.atan2(p['at'][1] - e[1], p['at'][0] - e[0]))
    facing = st['player']['yaw'] - (st['player'].get('turnedDeg') or 0)
    return abs((b - facing + 180) % 360 - 180)


def settled_flags(records):
    """For each record: whether the scene has settled (SETTLE_S after its start or its last dip)."""
    out, since = [], None
    for st in records:
        t = st.get('seconds', 0.0)
        if since is None or st.get('tag') in ('begin', 'after-dip', 'moment-begin'):
            since = t
        out.append(t - since >= SETTLE_S)
    return out


def face_axis_unknown(name, records):
    """A person whose face reads 80-100 degrees off the player in nearly every record: its head bone's axis is no face (a
    hanar: Opold, 2026-11-12)."""
    offs = [p['face']['toYouDeg'] for st in records for p in st['people']
            if p['name'] == name and p.get('face') and p['face'].get('toYouDeg') is not None]
    return len(offs) >= 3 and sum(1 for o in offs if 80 <= o <= 100) >= 0.8 * len(offs)


def findings(records):
    """The findings of one scene: [{rule, severity, what, at, tag, who, detail}], most severe first."""
    out = []
    if not records or not records[-1]['player'].get('eyes'):
        return out
    settled = settled_flags(records)
    moment = records[-1].get('opening') == 'moment'

    staged = bool(records[-1].get('staged'))

    def add(rule, st, who, detail):
        sev, what = RULES[rule]
        if rule in ('face-off-you', 'speaker-behind') and staged:
            sev = 1  # a staged scene's facings and marks are its own (the re-face rule leaves them): Udina beside the player, facing
            # the Council; Ashley speaking from 140 degrees behind at Kirrahe's camp (her mark)
        if not any(f['rule'] == rule and f['who'] == who for f in out):
            out.append({'rule': rule, 'severity': sev, 'what': what, 'at': round(st.get('seconds', 0.0), 1), 'tag': st.get('tag', '?'),
                        'who': who, 'detail': detail})

    off_run = {}
    behind_run = {}
    gap_records = []
    hands_run = 0
    for st, ok in zip(records, settled):
        off = hands_off(st)
        hands_run = hands_run + 1 if ok and off is not None and off >= HANDS_OFF_M else 0
        if hands_run >= 2:
            add('hands-off-controllers', st, 'Shepard', f"his drawn hands {off:.2f} m from the controllers, two records running")
        # (Not the record written as a scene ends: the view may already be the next one's, a merchant's shop screen: Opold.)
        live = ok and st.get('tag') != 'end'
        undrawn = body_undrawn(st)
        if live and undrawn is not None and undrawn >= UNDRAWN_S:
            add('body-not-drawn', st, 'Shepard', f"his body in view, not drawn for {undrawn:.1f} s")
        near = st['player'].get('nearPlane')
        if ok and not moment and near is not None and near > NEAR_PLANE_MAX:
            add('near-plane', st, 'the view', f"drawn with a near plane of {near:.0f} units")
        gap = body_gap(st)
        player = st['player']
        for p in st['people']:
            if p.get('shepard') or p.get('hidden'):
                continue
            face = p.get('face') or {}
            target = (p.get('gameLook') or {}).get('target')
            if ok and target == 'Shepard' and gap is not None and gap >= LOOK_AWAY_M:
                add('look-at-body', st, p['name'], f"its look target is Shepard with his head {gap:.2f} m from the eyes")
            if ok and meant_for_you(p) and face.get('toYouDeg') is not None and face['toYouDeg'] > FACE_OFF_DEG \
                    and not face_axis_unknown(p['name'], records):
                off_run[p['name']] = off_run.get(p['name'], 0) + 1
                if off_run[p['name']] >= 2:
                    add('face-off-you', st, p['name'], f"{face['toYouDeg']:.0f} deg off the eyes")
            else:
                off_run[p['name']] = 0
            if ok and p.get('role') == 'speaker' and meant_for_you(p) and bearing_off(st, p) > BEHIND_DEG:
                behind_run[p['name']] = behind_run.get(p['name'], 0) + 1
                if behind_run[p['name']] >= 2:
                    add('speaker-behind', st, p['name'], f"{bearing_off(st, p):.0f} deg from where the player faces")
            else:
                behind_run[p['name']] = 0
            if live and p.get('inSight') and (p.get('drawnAgoS') or 0) >= UNDRAWN_S:
                add('person-not-drawn', st, p['name'], f"in sight, not drawn for {p['drawnAgoS']:.1f} s")
            e = player.get('eyes')
            if ok and e and math.dist(p['at'][:2], e[:2]) < TOO_CLOSE_M * 100:
                add('too-close', st, p['name'], f"{math.dist(p['at'][:2], e[:2]) / 100:.2f} m from the eyes")
            if p.get('squad') and st.get('tag') in ('opening', 'settled', 'end') and bearing_off(st, p) > SQUAD_BEHIND_DEG:
                add('squad-behind', st, p['name'], f"{bearing_off(st, p):.0f} deg from where the player faces")
        if ok and not moment and not player.get('detached') and gap is not None and gap >= BODY_AWAY_M:
            gap_records.append((st, gap))
        walk = st.get('walkAlong') or {}
        if moment and walk.get('beside', 0) >= 1 and not walk.get('paused') and (walk.get('gapM') or 0) > WALK_LEFT_M:
            add('walk-left-behind', st, 'the walk', f"{walk['gapM']:.1f} m from Shepard's track place, not paused")
    if len(gap_records) >= 2:
        st, worst = max(gap_records, key=lambda r: r[1])
        add('eyes-off-body', st, 'Shepard', f"{len(gap_records)} records at {BODY_AWAY_M} m or more, at most {worst:.2f} m")
    last_walk = records[-1].get('walkAlong') or {}
    if moment and last_walk.get('pauses', 0) >= WALK_PAUSES:
        add('walk-pauses', records[-1], 'the walk', f"{last_walk['pauses']} pauses, {last_walk.get('pausedS', 0):.0f} s in all")
    out.sort(key=lambda f: (-f['severity'], f['at']))
    return out


def numbers(records):
    """A scene's numbers, for a baseline: the gap once settled (90th percentile), looks meant for the player and where they
    land, the faces' median angle off the eyes, the findings' rules."""
    settled = settled_flags(records)
    moment = records[-1].get('opening') == 'moment'
    gaps = sorted(g for st, ok in zip(records, settled)
                  if ok and not moment and not st['player'].get('detached') and (g := body_gap(st)) is not None)
    meant = at_you = at_body = 0
    offs = []
    for st in records:
        for p in st['people']:
            if p.get('shepard') or p.get('hidden') or not p.get('face') or not meant_for_you(p):
                continue
            meant += 1
            whom = looks_at(p, st)
            at_you += whom == 'you'
            at_body += whom == "Shepard's body"
            if p['face'].get('toYouDeg') is not None and not face_axis_unknown(p['name'], records):
                offs.append(p['face']['toYouDeg'])
    offs.sort()
    return {
        'opening': records[-1].get('opening', '-'), 'staged': bool(records[-1].get('staged')), 'moment': moment,
        'records': len(records),
        'gapP90M': round(gaps[int(0.9 * (len(gaps) - 1))], 2) if gaps else None,
        'looksMeant': meant, 'atYou': at_you, 'atBody': at_body,
        'faceOffMedianDeg': offs[len(offs) // 2] if offs else None,
        'handsOffMaxM': max((o for st, ok in zip(records, settled) if ok and (o := hands_off(st)) is not None), default=None),
        'findings': sorted({f['rule'] for f in findings(records)}),
    }


def compare(base, now):
    """Regressions of a run against a baseline: [(scene, what)]; scenes missing from the run as notes."""
    out = []
    for key, b in sorted(base.items()):
        n = now.get(key)
        if n is None:
            out.append((key, 'not played in this run'))
            continue
        for rule in n['findings']:
            if rule not in b['findings']:
                out.append((key, f"new finding: {rule} ({RULES[rule][1]})"))
            elif rule in ABSOLUTE:
                out.append((key, f"{rule} again ({RULES[rule][1]}): never excused by a baseline"))
        if n['gapP90M'] is not None and (b['gapP90M'] is None or n['gapP90M'] > b['gapP90M'] + 0.1):
            out.append((key, f"Shepard's head from the eyes {b['gapP90M']} -> {n['gapP90M']} m"))
        elif n['gapP90M'] is not None and n['gapP90M'] >= BODY_AWAY_M:
            out.append((key, f"Shepard's head from the eyes {n['gapP90M']} m (90th percentile), {BODY_AWAY_M} m or more whatever the baseline"))
        if n['atBody'] > b['atBody']:
            out.append((key, f"looks at his body {b['atBody']} -> {n['atBody']}"))
        if n['faceOffMedianDeg'] is not None and b['faceOffMedianDeg'] is not None and n['faceOffMedianDeg'] > b['faceOffMedianDeg'] + 6:
            out.append((key, f"faces off the eyes, median {b['faceOffMedianDeg']} -> {n['faceOffMedianDeg']} deg"))
        if b['looksMeant'] > 0 and n['looksMeant'] == 0:
            out.append((key, 'no look meant for the player any more (did the scene play the same way?)'))
    return out
