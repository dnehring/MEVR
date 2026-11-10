# SPDX-License-Identifier: MIT
"""Tests for tools/conversation-sweep.py and the catalogue's level choice in tools/conversation-scan.py, over a synthetic
scan (no game data is committed). Run: python -I tests/ConversationSweepTests.py (ctest mevr-conversation-sweep).

The synthetic game: two persistent levels, BIOA_STA00 (which streams its own chunk and the Normandy's wake-up chunk) and
BIOA_NOR00 (which streams only its own chunk), and four conversations:
  sta20_talk     a stage conversation with a wheel, in STA00's chunk, a trigger 2 m from its owner
  nor10_wake     in the wake-up chunk, which only STA00 streams although its name says NOR, shown by a named state
  sta70_scene    a flowed scene: a chain start (entries 0 -> 1 -> 2) before a wheel start (entry 3)
  sta20_bark     every line ambient
"""
import importlib.util
import json
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, '..', 'tools', file))
    m = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(m)
    return m


sweep = module('conversation_sweep', 'conversation-sweep.py')
scan = sweep.cs
failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
        print(f'FAIL {what}')


def entry(replies=(), ambient=False, text=1, conditional=-1, transition=-1):
    return {'ambient': ambient, 'nonText': False, 'speaker': 0, 'text': text, 'conditional': conditional, 'transition': transition,
            'script': -1, 'replies': list(replies)}


def reply(entries=(), kind='REPLY_STANDARD'):
    return {'type': kind, 'ambient': False, 'nonText': False, 'text': 1, 'conditional': -1, 'transition': -1, 'script': -1, 'entries': list(entries)}


def conversation(name, nodes, replies, starting):
    return {'kind': 'conversation', 'name': f'{name}_D.{name}_dlg', 'entries': len(nodes), 'replies': len(replies), 'speakers': ['a'],
            'intimacy': {}, 'styles': {}, 'ambient': sum(1 for n in nodes if n['ambient']), 'unskippable': 0, 'starting': starting,
            'nodes': nodes, 'replyNodes': replies, 'package': 'BIOA_X_LOC_INT'}


def start(name, package, at, stage=True):
    r = {'kind': 'start', 'path': f'{package}.TheWorld.PersistentLevel.Main_Sequence.{name}.BioSeqAct_StartConversation_0',
         'conv': f'{name}_D.{name}_dlg', 'comment': None, 'package': package, 'ownerClass': 'BioPawn', 'ownerTag': name,
         'owner': f'{package}.TheWorld.PersistentLevel.BioPawn_0', 'ownerAt': at, 'ownerYaw': 0.0}
    if stage:
        r.update({'stage': f'{package}.TheWorld.PersistentLevel.BioStage_0', 'stageType': 'BIOG_Stages.ST_2P_150', 'stageAt': at})
    return r


def volume(package, name, state, visible):
    return {'kind': 'volume', 'name': f'{package}.TheWorld.PersistentLevel.{name}', 'package': package,
            'states': [{'state': state, 'in': 'None', 'visible': visible, 'load': []}]}


AUTO = 'REPLY_AUTOCONTINUE'
RECORDS = [
    {'kind': 'level', 'name': 'BIOA_STA00.TheWorld.PersistentLevel.BioWorldInfo_0', 'package': 'BIOA_STA00',
     'streaming': ['BIOA_STA20_01_DSG', 'BIOA_STA70_02_DSG', 'BIOA_NOR10_07wake_DSG']},
    {'kind': 'level', 'name': 'BIOA_NOR00.TheWorld.PersistentLevel.BioWorldInfo_0', 'package': 'BIOA_NOR00', 'streaming': ['BIOA_NOR10_01_DSG']},
    conversation('sta20_talk', [entry([0]), entry([])], [reply([1])], [0]),
    conversation('nor10_wake', [entry([0]), entry([])], [reply([1])], [0]),
    # entries 0 -> 1 -> 2 by auto-continues (a chain), then a wheel start at 3; the chain start has a conditional, so the wheel
    # start can be reached.
    conversation('sta70_scene', [entry([0], conditional=5), entry([1]), entry([]), entry([2]), entry([])],
                 [reply([1], AUTO), reply([2], AUTO), reply([4])], [0, 3]),
    conversation('sta20_bark', [entry([], ambient=True)], [], [0]),
    start('sta20_talk', 'BIOA_STA20_01_DSG', [1000.0, 0.0, 0.0]),
    start('nor10_wake', 'BIOA_NOR10_07wake_DSG', [5000.0, 0.0, 0.0]),
    start('sta70_scene', 'BIOA_STA70_02_DSG', [9000.0, 0.0, 0.0], stage=False),
    start('sta20_bark', 'BIOA_STA20_01_DSG', [1200.0, 0.0, 0.0], stage=False),
    volume('BIOA_STA00', 'BioTriggerStream_0', 'None', ['sta20_01', 'sta70_02']),
    volume('BIOA_STA00', 'BioTriggerStream_1', 'WakeUp', ['nor10_07wake']),
    {'kind': 'trigger', 'name': 'BIOA_STA20_01_DSG.TheWorld.PersistentLevel.BioTriggerVolume_3', 'class': 'BioTriggerVolume',
     'package': 'BIOA_STA20_01_DSG', 'at': [1200.0, 0.0, 0.0]},
]


def summary(name, **over):
    s = {'name': f'BioConversation {name}_D.{name}_dlg', 'why': 'the game left its conversation mode', 'seconds': 20.0, 'stage': True,
         'lines': 2, 'wheel': 1, 'shots': 4, 'world': True, 'kept': 0, 'drawn': 0, 'letGo': 0, 'eyeToViewM': 0.1, 'sourceKept': 0,
         'noPlace': '', 'autoScreened': False}
    s.update(over)
    return s


def main():
    with tempfile.TemporaryDirectory(prefix='mevr-sweep-') as tmp:
        path = os.path.join(tmp, 'scan.jsonl')
        with open(path, 'w', encoding='utf-8') as f:
            for r in RECORDS:
                f.write(json.dumps(r, separators=(',', ':')) + '\n')

        # The catalogue: the level to open comes from the streaming lists, not from the package's name.
        entries = {e['conversation']: e for e in scan.catalogue(path, None, quiet=True)}
        check(entries['nor10_wake']['map'] == 'BIOA_STA00', f"the wake-up chunk opens the level that streams it, not {entries['nor10_wake']['map']}")
        check(entries['nor10_wake']['mapByName'] == 'BIOA_NOR00', 'the name alone says the Normandy')
        check(entries['nor10_wake']['streamStates'] == ['WakeUp'] and not entries['nor10_wake']['touchShows'], 'the wake-up chunk needs its named state')
        check(entries['sta20_talk']['map'] == 'BIOA_STA00' and entries['sta20_talk']['touchShows'], 'a plain chunk is shown by a touch')
        roots, roots_of = scan.level_roots({'A': ['B'], 'B': ['C'], 'D': ['C']})
        check(roots == ['A', 'D'], f'a level another level streams is not persistent: {roots}')
        check(roots_of('C') == ['A', 'D'] and roots_of('b') == ['A'], 'a package reached through a streamed level counts, whatever its case')

        # Layer 0.
        rows, _ = sweep.predictions(path)
        p = {r['conversation']: r for r in rows}
        check(p['sta20_talk']['sweep'] == 'smoke' and 'trigger-near' in p['sta20_talk']['risks'], f"a trigger 2 m away is near: {p['sta20_talk']['risks']}")
        check(p['sta20_talk']['triggers']['nearestM'] == 2.0, 'its distance in metres')
        check('named-state' in p['nor10_wake']['risks'] and 'trigger-near' not in p['nor10_wake']['risks'], f"the wake-up scene: {p['nor10_wake']['risks']}")
        check(p['sta70_scene']['flowed'] and p['sta70_scene']['flowEntries'] == [0, 1, 2], f"the chain's entries: {p['sta70_scene']['flowEntries']}")
        check(p['sta20_bark']['sweep'] == 'bark', 'every line ambient: a bark')

        # The judge.
        results = os.path.join(tmp, 'results.jsonl')
        preds = os.path.join(tmp, 'predictions.json')
        with open(preds, 'w', encoding='utf-8') as f:
            json.dump(rows, f)
        records = [
            {'conversation': 'sta20_talk', 'map': 'BIOA_STA00', 'outcome': 'played', 'ended': 'itself', 'summary': summary('sta20_talk'),
             'driver': {'picks': 1, 'skips': 3, 'entries': [0, 1]}},
            {'conversation': 'nor10_wake', 'map': 'BIOA_STA00', 'outcome': 'played', 'ended': 'itself',
             'summary': summary('nor10_wake', kept=3, drawn=1, eyeToViewM=2.3, sourceKept=25)},
            {'conversation': 'sta70_scene', 'map': 'BIOA_STA00', 'outcome': 'played', 'ended': 'itself',
             'flow': {'name': 'sta70_scene', 'why': 'it began again as a conversation', 'seconds': 13.0, 'flagged': 3, 'entries': [0, 1, 2],
                      'next': 'sta70_scene', 'nextAmbient': False, 'barksRefused': 2, 'scenesHeld': 1, 'scenesRefused': 0},
             'summary': summary('sta70_scene')},
        ]
        def run(recs, *more, write='now.json'):
            with open(results, 'w', encoding='utf-8') as f:
                for r in recs:
                    f.write(json.dumps(r) + '\n')
            base = os.path.join(tmp, write)
            status = sweep.judge([results, '--scan', path, '--predictions', preds, '--write-baseline', base, *more])
            return status, json.load(open(base, encoding='utf-8'))
        status, got = run(records)
        check(got['sta20_talk']['kinds'] == [], f"a clean scene: {got['sta20_talk']['kinds']}")
        check(got['nor10_wake']['kinds'] == ['drawn-jump', 'eye-far', 'undone'], f"three limits broken: {got['nor10_wake']['kinds']}")
        check(got['sta70_scene']['kinds'] == [], f"a whole chain with its follow-up: {got['sta70_scene']['kinds']}")
        # A small move the game repeated and the mod accepted (`yielded`): its one undone step and drawn frame are no fault;
        # one more than that is.
        snap = [{'conversation': 'sta20_talk', 'map': 'BIOA_STA00', 'outcome': 'played', 'ended': 'itself',
                 'summary': summary('sta20_talk', kept=2, drawn=2, yielded=2), 'driver': {'picks': 1, 'skips': 3, 'entries': [0, 1]}}]
        _, got_snap = run(snap, write='snap.json')
        check(got_snap['sta20_talk']['kinds'] == [], f"a walk's last step accepted: {got_snap['sta20_talk']['kinds']}")
        snap[0]['summary'] = summary('sta20_talk', kept=3, drawn=2, yielded=2)
        _, got_snap = run(snap, write='snap.json')
        check(got_snap['sta20_talk']['kinds'] == ['undone'], f"one more undone than accepted: {got_snap['sta20_talk']['kinds']}")
        check(status == 0, 'without --strict or --expect the judge reports and returns 0')

        # A chain cut short, a chain that never played, a forced end, an unanswered wheel, short lines, nothing started.
        cut = dict(records[2], flow=dict(records[2]['flow'], entries=[0, 1], why='another conversation began', next='sta20_bark'))
        unplayed = dict(records[2])
        del unplayed['flow']
        forced = dict(records[0], ended='forced', summary=summary('sta20_talk', wheel=2, lines=1))
        nothing = {'conversation': 'nor10_wake', 'map': 'BIOA_STA00', 'outcome': 'no conversation'}
        _, got = run([cut, forced, nothing])
        check(got['sta70_scene']['kinds'] == ['chain-cut'], f"a bark over the chain: {got['sta70_scene']['kinds']}")
        check(got['sta20_talk']['kinds'] == ['forced-end', 'lines-short', 'wheel-unanswered'], f"a forced end: {got['sta20_talk']['kinds']}")
        check(got['nor10_wake']['kinds'] == ['no-start'], f"nothing started: {got['nor10_wake']['kinds']}")
        _, got = run([unplayed])
        check(got['sta70_scene']['kinds'] == ['chain-not-played'], f"the chain as a conversation: {got['sta70_scene']['kinds']}")
        ended_early = dict(records[2], flow=dict(records[2]['flow'], entries=[0, 1], why='it ended', next=''))
        _, got = run([ended_early])
        check(got['sta70_scene']['kinds'] == ['chain-cut'], f"a chain that stopped on a line the graph goes on from: {got['sta70_scene']['kinds']}")

        # A set piece in the world, a world scene on the screen.
        rows2 = [dict(r, verdict='screen') if r['conversation'] == 'sta20_talk' else r for r in rows]
        with open(preds, 'w', encoding='utf-8') as f:
            json.dump(rows2, f)
        _, got = run([records[0], dict(records[1], summary=summary('nor10_wake', world=False))])
        check(got['sta20_talk']['kinds'] == ['not-on-screen'], f"a set piece in the world: {got['sta20_talk']['kinds']}")
        check(got['nor10_wake']['kinds'] == ['not-in-world'], f"a world scene not in the world: {got['nor10_wake']['kinds']}")
        with open(preds, 'w', encoding='utf-8') as f:
            json.dump(rows, f)

        # The baseline: the same pass differs nowhere; a changed one is named, and --strict fails on a new exception kind.
        base = os.path.join(tmp, 'base.json')
        run(records, write='base.json')
        status, _ = run(records, '--baseline', base, '--strict')
        check(status == 0, 'the same pass against its own baseline is no failure')
        worse = [dict(records[0], summary=summary('sta20_talk', kept=2))] + records[1:]
        status, _ = run(worse, '--baseline', base, '--strict')
        check(status == 1, 'a new exception against the baseline fails under --strict')

        # The canary set: what named scenes must show; a judge that passes everything fails it.
        canary = os.path.join(tmp, 'canary.json')
        with open(canary, 'w', encoding='utf-8') as f:
            json.dump({'nor10_wake': {'kinds': ['drawn-jump', 'eye-far', 'undone'], 'min': {'sourceKept': 20}},
                       'sta20_talk': {'kinds': [], 'max': {'kept': 0}},
                       'sta70_scene': {'kinds': [], 'flowMin': {'barksRefused': 2, 'scenesHeld': 1}}}, f)
        status, _ = run(records, '--expect', canary)
        check(status == 0, 'the canary set as expected')
        fixed = [records[0], dict(records[1], summary=summary('nor10_wake', sourceKept=25)), records[2]]
        status, _ = run(fixed, '--expect', canary)
        check(status == 1, 'a canary that must fail and passes is caught')
        status, _ = run(worse, '--expect', canary)
        check(status == 1, 'a canary that must pass and fails is caught')

        # "has" and "lacks": some of the kinds, for a run with a rule switched off.
        with open(canary, 'w', encoding='utf-8') as f:
            json.dump({'nor10_wake': {'has': ['undone'], 'lacks': ['forced-end']}}, f)
        status, _ = run(records, '--expect', canary)
        check(status == 0, 'the broken run shows the kind it must')
        status, _ = run(fixed, '--expect', canary)
        check(status == 1, 'the broken run without that kind is caught')

        # The canary's own plan: the named scenes in the given order, with their options; expectations per scene.
        full = os.path.join(tmp, 'full.json')
        sweep.plan([path, '--out', full])
        spec = os.path.join(tmp, 'spec.json')
        with open(spec, 'w', encoding='utf-8') as f:
            json.dump({'level': 'BIOA_STA00', 'order': ['sta70_scene', 'nor10_wake'],
                       'scenes': {'sta70_scene': {'alsoFire': ['X.BioSeqAct_StartConversation_0'], 'expect': {'kinds': []}},
                                  'nor10_wake': {'walks': [[0, 1, 4]], 'expect': {'max': {'kept': 0}}},
                                  'sta20_talk': {'expect': {'lacks': ['no-start']}}},
                       'broken': {'set': {'Cinematics.worldStaySource': '0'}, 'order': ['nor10_wake'],
                                  'scenes': {'nor10_wake': {'expect': {'has': ['undone']}}}}}, f)
        cplan, cexpect = os.path.join(tmp, 'cplan.json'), os.path.join(tmp, 'cexpect.json')
        check(sweep.canary([full, spec, '--out-plan', cplan, '--out-expect', cexpect]) == 0, 'the canary plan is written')
        cdoc = json.load(open(cplan, encoding='utf-8'))
        order = [s['conversation'] for c in cdoc['maps'][0]['chunks'] for s in c['scenes']]
        check(order == ['sta70_scene', 'nor10_wake'], f'the canary scenes in the given order: {order}')
        check(cdoc['maps'][0]['chunks'][0]['scenes'][0]['alsoFire'] == ['X.BioSeqAct_StartConversation_0'], 'a scene carries its options')
        check(sorted(json.load(open(cexpect, encoding='utf-8'))) == ['nor10_wake', 'sta20_talk', 'sta70_scene'], 'expectations for every named scene')
        check(sweep.canary([full, spec, '--broken', '--out-plan', cplan, '--out-expect', cexpect]) == 0, 'the broken run has its own plan')
        check(json.load(open(cexpect, encoding='utf-8')) == {'nor10_wake': {'has': ['undone']}}, 'and its own expectations')

        # The work list.
        out = os.path.join(tmp, 'plan.json')
        sweep.plan([path, '--out', out])
        doc = json.load(open(out, encoding='utf-8'))
        check(doc['scenes'] == 4 and [m['map'] for m in doc['maps']] == ['BIOA_STA00'], f"one level, four scenes: {doc['scenes']} {[m['map'] for m in doc['maps']]}")
        chunks = {c['chunk']: [s['conversation'] for s in c['scenes']] for c in doc['maps'][0]['chunks']}
        check(chunks.get('NOR10_07wake') == ['nor10_wake'], f'the wake-up chunk under the level that streams it: {chunks}')

        # More of the game: a second conversation of sta20_talk's shape, a start with its way in through the player's own
        # event, a conversation only voice-over actions play (under an animated cutscene), a matinee that walks the player.
        more = RECORDS + [
            conversation('sta20_talk2', [entry([0]), entry([])], [reply([1])], [0]),
            dict(start('sta20_talk2', 'BIOA_STA20_01_DSG', [3000.0, 0.0, 0.0]),
                 via={'event': 'BIOA_STA20_01_DSG.TheWorld.PersistentLevel.Main_Sequence.Talk2.BioSeqEvt_OnPlayerActivate_0',
                      'eventClass': 'BioSeqEvt_OnPlayerActivate', 'originator': 'BioPawn_0', 'originatorAt': [3000.0, 0.0, 0.0], 'eventName': None,
                      'firstOp': 'BIOA_STA20_01_DSG.TheWorld.PersistentLevel.Main_Sequence.Talk2.BioSeqAct_PMCheckState_0',
                      'firstOpClass': 'BioSeqAct_PMCheckState', 'firstOpInput': 0, 'hops': 2, 'between': ['BioSeqAct_PMCheckState']}),
            conversation('pro20_cutscene_lines', [entry([])], [], [0]),
            {'kind': 'vo', 'package': 'BIOA_PRO20_E_CIN', 'conv': 'pro20_cutscene_lines_D.pro20_cutscene_lines_dlg',
             'path': 'BIOA_PRO20_E_CIN.TheWorld.PersistentLevel.Main_Sequence.ANIMCUTSCENE_Pro_20_E.BioSeqAct_FaceOnlyVO_0'},
            {'kind': 'matinee', 'package': 'BIOA_STA70_02_DSG', 'length': 7.5,
             'path': 'BIOA_STA70_02_DSG.TheWorld.PersistentLevel.Main_Sequence.WalkIn.SeqAct_Interp_0',
             'groups': [{'class': 'InterpGroupDirector', 'name': None, 'tracks': ['InterpTrackDirector']},
                        {'class': 'InterpGroup', 'name': 'Player', 'tracks': ['InterpTrackMove', 'InterpTrackAnimControl']}],
             'via': {'event': 'x', 'eventClass': 'BioSeqEvt_OnPlayerActivate', 'originator': 'BioPawn_9', 'originatorAt': [9000.0, 100.0, 0.0],
                     'eventName': None, 'firstOp': 'BIOA_STA70_02_DSG.TheWorld.PersistentLevel.Main_Sequence.WalkIn.BioSeqAct_PMCheckState_0',
                     'firstOpClass': 'BioSeqAct_PMCheckState', 'firstOpInput': 0, 'hops': 4, 'between': ['BioSeqAct_PMCheckState', 'BioSeqAct_BioToggleCinematicMode'],
                     'chain': [{'op': 'BIOA_STA70_02_DSG.TheWorld.PersistentLevel.Main_Sequence.WalkIn.BioSeqAct_PMCheckState_0', 'class': 'BioSeqAct_PMCheckState', 'input': 0},
                               {'op': 'BIOA_STA70_02_DSG.TheWorld.PersistentLevel.Main_Sequence.WalkIn.BioSeqAct_BioToggleCinematicMode_0', 'class': 'BioSeqAct_BioToggleCinematicMode', 'input': 0},
                               {'op': 'BIOA_STA70_02_DSG.TheWorld.PersistentLevel.Main_Sequence.WalkIn.SeqAct_Interp_0', 'class': 'SeqAct_Interp', 'input': 0}]}},
        ]
        path2 = os.path.join(tmp, 'scan2.jsonl')
        with open(path2, 'w', encoding='utf-8') as f:
            for r in more:
                f.write(json.dumps(r, separators=(',', ':')) + '\n')
        rows, _ = sweep.predictions(path2)
        p = {r['conversation']: r for r in rows}
        check(p['pro20_cutscene_lines']['sweep'] == 'vo' and p['pro20_cutscene_lines']['risks'] == ['cutscene-vo'],
              f"voice-over under a cutscene never enters the conversation mode: {p['pro20_cutscene_lines']['sweep']} {p['pro20_cutscene_lines']['risks']}")
        check(p['sta20_talk']['tier'] == 'A' and p['sta20_talk']['shape'] == p['sta20_talk2']['shape'], 'two simple conversations of one shape')
        check(p['sta70_scene']['tier'] == 'B', f"a flowed chain is tier B: {p['sta70_scene']['tier']}")

        # A sample: one of each simple shape; with --per-place, one of each place risk per level too; --also adds by name.
        sample = os.path.join(tmp, 'sample.json')
        sweep.plan([path2, '--out', sample, '--sample'])
        names = lambda doc: sorted(s['conversation'] for m in doc['maps'] for c in m['chunks'] for s in c['scenes'])
        got = names(json.load(open(sample, encoding='utf-8')))
        check(len({'sta20_talk', 'sta20_talk2', 'nor10_wake'} & set(got)) == 1, f'one of the three simple scenes of a shape: {got}')
        check('sta70_scene' in got and 'sta20_bark' not in got and 'pro20_cutscene_lines' not in got, f'a flowed scene, no bark, no voice-over: {got}')
        sweep.plan([path2, '--out', sample, '--sample', '--per-place', '1'])
        got = names(json.load(open(sample, encoding='utf-8')))
        check('sta20_talk' in got and 'sta20_talk2' in got, f'the scene near a trigger and the one with no place risk: {got}')
        also = os.path.join(tmp, 'also.txt')
        with open(also, 'w', encoding='utf-8') as f:
            f.write('sta20_talk2\n')
        reach_file = os.path.join(tmp, 'reach.jsonl')
        with open(reach_file, 'w', encoding='utf-8') as f:
            f.write(json.dumps({'conversation': 'sta20_talk', 'outcome': 'not reached'}) + '\n')
            f.write(json.dumps({'conversation': 'sta20_talk2', 'outcome': 'reached'}) + '\n')
        sweep.plan([path2, '--out', sample, '--sample', '--reach', reach_file, '--also', also])
        doc = json.load(open(sample, encoding='utf-8'))
        check('sta20_talk' not in names(doc) and 'sta20_talk2' in names(doc) and doc['unreached'] == ['sta20_talk'], f"a scene a reach run missed is left out and listed: {names(doc)} {doc['unreached']}")

        # The way in through the player's own event travels with the scene.
        full2 = os.path.join(tmp, 'full2.json')
        sweep.plan([path2, '--out', full2])
        scenes = {s['conversation']: s for m in json.load(open(full2, encoding='utf-8'))['maps'] for c in m['chunks'] for s in c['scenes']}
        check(scenes['sta20_talk2'].get('via', {}).get('opTail') == 'Talk2.BioSeqAct_PMCheckState_0' and scenes['sta20_talk2']['via']['opClass'] == 'BioSeqAct_PMCheckState',
              f"the first action behind the event: {scenes['sta20_talk2'].get('via')}")
        check('via' not in scenes['sta20_talk'], 'a start without an event upstream has no way in')

        # The moments: a directed matinee that moves the player, as a scene the runner can fire.
        mfile = os.path.join(tmp, 'moments.json')
        sweep.moments([path2, '--out', mfile])
        mdoc = json.load(open(mfile, encoding='utf-8'))
        ms = [s for m in mdoc['maps'] for c in m['chunks'] for s in c['scenes']]
        check(len(ms) == 1 and ms[0]['moment'] and ms[0]['map'] == 'BIOA_STA00' and ms[0]['actionClass'] == 'BioSeqAct_PMCheckState'
              and ms[0]['actionTail'] == 'WalkIn.BioSeqAct_PMCheckState_0' and ms[0]['ownerAt'] == [9000.0, 100.0, 0.0] and ms[0]['lengthS'] == 7.5,
              f'one moment, fired through its event, Shepard stood by the originator: {ms}')
        # The way to it action by action: the runner's step past a plot check the fresh level does not pass.
        check([c['class'] for c in ms[0]['chain']] == ['BioSeqAct_PMCheckState', 'BioSeqAct_BioToggleCinematicMode', 'SeqAct_Interp']
              and ms[0]['chain'][1]['tail'] == 'WalkIn.BioSeqAct_BioToggleCinematicMode_0' and ms[0]['chain'][2]['input'] == 0,
              f"the moment's chain of actions: {ms[0]['chain']}")
        # ... and the judge takes a moment's record for what it is.
        with open(results, 'w', encoding='utf-8') as f:
            f.write(json.dumps({'conversation': ms[0]['conversation'], 'map': 'BIOA_STA00', 'outcome': 'played', 'modes': [6],
                                'track': {'seconds': 7.4, 'pathM': 11.7, 'modes': [6]},
                                'view': {'seconds': 12.4, 'walkedM': 0.0, 'carriedM': 3.06, 'dips': 0},
                                'led': [{'name': 'sta90_hearing', 'world': True, 'opening': 'walk in', 'markM': 21.08}]}) + '\n')
        base = os.path.join(tmp, 'moment-base.json')
        check(sweep.judge([results, '--write-baseline', base]) == 0 and json.load(open(base, encoding='utf-8'))[ms[0]['conversation']]['kinds'] == [],
              'a moment that ran is no exception')

    print(f"{'FAILED' if failures else 'passed'}: conversation sweep tests ({len(failures)} failures)")
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
