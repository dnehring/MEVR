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

    print(f"{'FAILED' if failures else 'passed'}: conversation sweep tests ({len(failures)} failures)")
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
