# SPDX-License-Identifier: MIT
"""Tests for tools/scene_audit.py (the rules over first-person scenes' stance records, research/69 "Auditing scenes by
rule"), over synthetic records (no game data). Run: python -I tests/SceneAuditTests.py (ctest mevr-scene-audit)."""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location('scene_audit', os.path.join(HERE, '..', 'tools', 'scene_audit.py'))
sa = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sa)
failures = []


def check(ok, what):
    if not ok:
        failures.append(what)
        print('FAILED:', what)


def person(name, at, face_to_you, target='Shepard', role='speaker', squad=False, head_z=170.0):
    return {'name': name, 'at': [at[0], at[1], 0.0], 'head': [at[0], at[1], head_z], 'yaw': 180.0, 'role': role, 'squad': squad,
            'shepard': False, 'hidden': False, 'face': {'yaw': 180.0, 'toYouDeg': face_to_you, 'toShepardDeg': face_to_you},
            'gameLook': {'target': target, 'rank': 99999, 'aim': None}}


def record(t, tag, gap, people, yaw=0.0, eyes=(0.0, 0.0, 160.0), detached=False, opening='walk in', staged=False, walk=None,
           conversation='test_dlg', number=1, turned=0.0):
    shep = {'name': 'Shepard', 'at': [eyes[0] - gap * 100, eyes[1], 0.0], 'head': [eyes[0] - gap * 100, eyes[1], 165.0],
            'yaw': yaw, 'shepard': True, 'hidden': False, 'role': 'listener'}
    return {'conversation': conversation, 'number': number, 'tag': tag, 'seconds': t, 'opening': opening, 'staged': staged,
            '_file': f'2026-11-12_20-00-{int(t):02d}-{int((t % 1) * 1000):03d}-{number}-{conversation}-{tag}.json',
            'player': {'at': [eyes[0] + 43, eyes[1], eyes[2] + 18], 'yaw': yaw, 'eyes': list(eyes), 'bodyFromEyesM': gap,
                       'detached': detached, 'turnedDeg': turned},
            'people': [shep] + people, 'walkAlong': walk}


def rules(records):
    return {f['rule'] for f in sa.findings(records)}


# A clean scene: Shepard under the eyes, the speaker 1.5 m ahead looking at the player.
ahead = lambda off: person('Chorban', (150.0, 0.0), off)
clean = [record(0.0, 'begin', 0.08, [ahead(10)]), record(2.0, 'opening', 0.08, [ahead(8)]), record(5.0, 'line-3', 0.07, [ahead(12)])]
check(rules(clean) == set(), f'a clean scene has no findings: {rules(clean)}')

# The cut that put the eyes 0.35 m behind his head: eyes-off-body once settled (twice or more), not in the first 1.5 s.
cut = [record(0.0, 'begin', 0.9, [ahead(10)], opening='cut'), record(0.6, 'after-dip', 0.36, [ahead(10)], opening='cut'),
       record(2.5, 'opening', 0.35, [ahead(10)], opening='cut'), record(6.0, 'line-1', 0.35, [ahead(10)], opening='cut')]
check('eyes-off-body' in rules(cut), 'a lasting gap after a cut is eyes-off-body')
check('eyes-off-body' not in rules(cut[:3]), 'one settled record is not enough')

# A look on Shepard's body while it stands half a metre away (the stand-in did not take it); not in a view's own walk start.
look = [record(0.0, 'begin', 0.7, [ahead(10)]), record(2.0, 'opening', 0.7, [ahead(10)])]
check('look-at-body' in rules(look), 'a look on the body 0.7 m away is look-at-body')
taken = [record(0.0, 'begin', 3.7, [person('Girard', (150.0, 0.0), 10, target='you')], detached=True),
         record(2.0, 'opening', 3.7, [person('Girard', (150.0, 0.0), 12, target='you')], detached=True)]
check('look-at-body' not in rules(taken), 'a look the stand-in took is no finding')

# A face meant for the player 50 degrees off, twice running: face-off-you; once: nothing; a hanar's constant 90: nothing.
off = [record(0.0, 'begin', 0.08, [ahead(10)]), record(2.0, 'opening', 0.08, [ahead(50)]), record(4.0, 'line-1', 0.08, [ahead(52)])]
check('face-off-you' in rules(off), 'a face 50 deg off twice running is face-off-you')
check('face-off-you' not in rules(off[:2]), 'once is not enough')
hanar = [record(t, tag, 0.07, [person('Opold', (150.0, 0.0), 90)]) for t, tag in ((0.0, 'begin'), (2.0, 'opening'), (4.0, 'line'), (6.0, 'end'))]
check('face-off-you' not in rules(hanar), "a hanar's head axis (90 deg throughout) is no finding")
staged_off = [dict(r, staged=True) for r in off]
check(next(f for f in sa.findings(staged_off) if f['rule'] == 'face-off-you')['severity'] == 1, "a staged scene's own facing is minor")

# The speaker behind the player (he would turn round); someone in his face; a squadmate right behind.
behind = person('Zabaleta', (-150.0, 0.0), 10)
check('speaker-behind' in rules([record(0.0, 'begin', 0.08, [behind]), record(2.0, 'opening', 0.08, [behind]), record(4.0, 'line', 0.08, [behind])]),
      'a speaker behind')
turned_away = [record(t, tag, 0.08, [ahead(10)], yaw=180.0, turned=180.0) for t, tag in ((0.0, 'begin'), (2.0, 'turned-180'), (4.0, 'line'))]
check('speaker-behind' not in rules(turned_away), 'a speaker behind a player who turned away himself is no finding')
staged_behind = [dict(record(t, tag, 0.08, [behind]), staged=True) for t, tag in ((0.0, 'begin'), (2.0, 'opening'), (4.0, 'line'))]
check(next(f for f in sa.findings(staged_behind) if f['rule'] == 'speaker-behind')['severity'] == 1, "a staged scene's own mark behind is minor")
close = person('Garrus', (30.0, 0.0), 40, target='Chorban', role='bystander', squad=True)
check('too-close' in rules([record(0.0, 'begin', 0.08, [close]), record(2.0, 'opening', 0.08, [close])]), 'someone 0.3 m from the eyes is too close')
squad_back = person('Tali', (-120.0, 5.0), 100, target='Chorban', role='bystander', squad=True)
check('squad-behind' in rules([record(2.0, 'opening', 0.08, [squad_back])]), 'a squadmate right behind')

# A scripted walk: left behind while someone walks beside Shepard and the walk is not paused; paused often.
walk_rec = lambda t, gap, paused, pauses: record(t, 'moment', gap, [], opening='moment', detached=True, conversation='moment_x',
                                                  walk={'paused': paused, 'gapM': gap, 'beside': 1, 'line': False, 'pauses': pauses, 'pausedS': 3.0})
check('walk-left-behind' in rules([walk_rec(1.0, 2.0, False, 0), walk_rec(3.0, 6.0, False, 0)]), 'a walk that left the player 6 m behind')
check('walk-left-behind' not in rules([walk_rec(1.0, 2.0, False, 0), walk_rec(3.0, 6.0, True, 1)]), 'a paused walk is not leaving him')
check('walk-pauses' in rules([walk_rec(1.0, 2.0, False, 1), walk_rec(9.0, 1.0, False, 5)]), 'five pauses in one walk')

# Grouping: two runs of the same conversation number on one day, minutes apart, are two scenes.
a = record(1.0, 'begin', 0.1, [ahead(10)])
b = dict(record(2.0, 'opening', 0.1, [ahead(10)]), _file='2026-11-12_20-09-00-000-1-test_dlg-opening.json')
check(len(sa.group([a, b])) == 2, 'records nine minutes apart are two runs')

# The baseline: a worse run is a regression, the same run is none, a scene not played is a note.
base = {'test_dlg': sa.numbers(clean)}
check(sa.compare(base, {'test_dlg': sa.numbers(clean)}) == [], 'the same run is no regression')
worse = sa.compare(base, {'test_dlg': sa.numbers(look)})
check(any('looks at his body' in w for _, w in worse) or any('new finding' in w for _, w in worse), f'a worse run regresses: {worse}')
check(sa.compare(base, {}) == [('test_dlg', 'not played in this run')], 'a scene not played is a note')
# A body away from the eyes is never excused by a baseline that already had it (the council hearing, 2026-11-13).
cut_base = {'test_dlg': sa.numbers(cut)}
again = sa.compare(cut_base, {'test_dlg': sa.numbers(cut)})
check(any('never excused' in w for _, w in again), f'eyes-off-body in the baseline still fails: {again}')
check(any('whatever the baseline' in w for _, w in again), f'a 90th-percentile gap of 0.3 m or more still fails: {again}')

# The drawn hands against the controllers (research/69 "Seeing the body in the audit", 2026-11-13): two settled records with a
# wrist 0.3 m or more off are a finding, never excused by a baseline; one is not; records without the measure are not judged.
def with_hands(st, left, right):
    st['player']['hands'] = {'firstPerson': True, 'left': {'fromGripM': left, 'inView': True},
                             'right': {'fromGripM': right, 'inView': True}}
    return st


left_behind = [with_hands(record(t, 'moment', 0.0, [], opening='moment'), 0.07, 3.85) for t in (0.0, 1.5, 3.0, 4.5)]
check('hands-off-controllers' in rules(left_behind), 'a body left behind (its hands 3.85 m off) is hands-off-controllers')
check('hands-off-controllers' not in rules(left_behind[:2]), 'one settled record off is not enough (the first is before the scene settled)')
held = [with_hands(record(t, 'moment', 0.0, [], opening='moment'), 0.07, 0.08) for t in (0.0, 1.5, 3.0, 4.5)]
check('hands-off-controllers' not in rules(held), 'hands on the controllers are no finding')
check('hands-off-controllers' not in rules([record(t, 'moment', 0.0, [], opening='moment') for t in (0.0, 1.5, 3.0, 4.5)]),
      'records without the measure are not judged')
lb_base = {'test_dlg': sa.numbers(left_behind)}
check(any('hands-off-controllers again' in w for _, w in sa.compare(lb_base, {'test_dlg': sa.numbers(left_behind)})),
      'hands-off-controllers in the baseline still fails')

print(f"scene audit tests: {'ok' if not failures else f'{len(failures)} FAILED'}")
sys.exit(1 if failures else 0)
