# SPDX-License-Identifier: MIT
"""The conversation sweep's data side (research/69 "The sweep: plan"): what to expect of every conversation before the game
runs (layer 0), the runner's work list, and the judge of a finished pass. No game needed for any of it.

  python tools/conversation-sweep.py predict scan.jsonl [--out predictions.json] [--table]
  python tools/conversation-sweep.py plan scan.jsonl --out plan.json [--barks 2]
  python tools/conversation-sweep.py canary plan.json tools/conversation-canary.json --out-plan canary-plan.json
        --out-expect canary-expect.json [--broken]
  python tools/conversation-sweep.py judge results.jsonl [more.jsonl ...] [--scan scan.jsonl] [--predictions predictions.json]
        [--baseline baseline.json] [--write-baseline baseline.json] [--expect canary.json] [--report report.md] [--strict]

scan.jsonl is tools/conversation-scan's output (the game's data: keep it, and everything made from it, out of the
repository; the counts are ours).

predict: per conversation its kind and verdict (tools/conversation-scan.py), how its level and chunk are reached, and its
risks, each a thing the data shows and a run can confirm:
  set-piece        on the 3D screen by the scan's verdict: a run must show it went there
  wide-stage       a stage 4 m or more across (the "check" verdict)
  npc-walk         an authored walk of an NPC during a line
  shepard-walk     the game walks Shepard himself during a line
  flowed           a chain start of it plays as barks (the mod's flow table)
  plot-chain       a chain that stays a conversation because it applies plot effects after its first line
  plot-effects     lines of it set plot values or fire level script: playing it changes the story for what plays after
  trigger-near     a trigger or trigger volume within 6 m of where it plays (by the trigger's placed origin: a volume's
                   extent is not in the scan), so the walk's trigger guard may hold the player
  remote           a transmission: nobody in the room
  named-state      its chunk shows only under a named streaming state (the plot's): the launcher sets it
  no-route         no streaming volume or state of its level lists its chunk
  owner-spawned    its owner is not placed in the level (the plot spawns it): no place to stand Shepard
  no-start         no level script starts it (code does): the sweep cannot fire it
  conditional      every start entry has a plot conditional: a fired action may find none that holds

plan: the runner's work list, one scene per conversation that a level script starts (its best start: a placed owner
first), grouped by the level to open and the chunk to stream, plus a sample of barks per area.

judge: reads the runner's results (one JSON object per scene; tools/conversation-survey.ps1's records are understood too)
and sorts every scene into "inside every limit" or one or more exception kinds:
  not-reached      the level, chunk or start action was never there
  no-start         the action fired and no conversation (or flowed scene) followed
  crash, hang      the game died or stopped answering during the scene
  forced-end       it did not end by itself (ended by the runner after the time-out)
  undone           kept > 0: someone was put on a mark and moved back after the fact (the source hook should withhold it)
  drawn-jump       drawn > 0: a jump reached a drawn frame
  eye-far          Shepard's head bone 0.8 m or more from the player's held eyes
  no-place         the mod's presence rules named it as no place for the player, and the verdict is not "screen"
  not-on-screen    a set piece that played in the world
  not-in-world     a world scene that did not play in the world
  lines-short      fewer lines spoken than the entries the runner saw it pass through
  wheel-unanswered a wheel opened more often than the runner answered
  chain-not-played a flowed scene expected, and it played as a conversation or not at all
  chain-cut        a flowed scene that ended on an entry the graph goes on from, or that another conversation ended
The result record (the runner's contract):
  {"conversation": "sta20_chorban", "map": "BIOA_STA00", "chunk": "STA20_03", "action": "...", "route": "open",
   "expect": {"verdict": "world", "flowed": false, "afterCutscene": false},
   "outcome": "played" | "no conversation" | "not reached" | "action not loaded" | "crash" | "hang",
   "ended": "itself" | "forced", "summary": {the mod's convo: summary}, "summaries": [every one in the scene's window],
   "flow": {the mod's convo flow: summary}, "driver": {"picks": 3, "skips": 12}}
(the summary's "entries" are the entries the conversation passed through, in order; outcome "reached" is a reach-only run)
With --baseline every scene is also compared with the last accepted pass; --write-baseline accepts this one. --expect
checks named scenes against what they must show (the canary set, `canary` writes it; "kinds" exactly, "has" and "lacks"
some of them: {"nor10_oculon_approach": {"kinds": [], "min":
{"sourceKept": 20}, "max": {"kept": 0}, "flowMin": {"barksRefused": 2}}}; a run with a rule switched off lists the kind that
must appear) and exits 1 when
one differs: that is how a judge that passes everything is caught. --strict exits 1 on any exception not in the baseline.
"""
import collections
import importlib.util
import json
import math
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
_spec = importlib.util.spec_from_file_location('conversation_scan', os.path.join(HERE, 'conversation-scan.py'))
cs = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(cs)

TRIGGER_NEAR_UNITS = 600.0
EYE_FAR_M = 0.8


def option(argv, name, default=None):
    return argv[argv.index(name) + 1] if name in argv and argv.index(name) + 1 < len(argv) else default


option_value = option


def conversation_key(name):
    # 'BioConversation sta20_chorban_D.sta20_chorban_dlg' / 'sta20_chorban_D.sta20_chorban_dlg' / 'sta20_chorban' -> 'sta20_chorban'
    tail = (name or '').split(' ')[-1]
    return cs.key(tail, 'd') if '.' in tail or tail.lower().endswith('_d') else tail.lower()


# ---- layer 0: predictions -------------------------------------------------------------------------------------------------------

def read_scan(path):
    convs, starts, nodes = cs.load(path)
    triggers, levels = [], {}
    for line in open(path, encoding='utf-8'):
        if '"kind":"trigger"' in line or '"kind":"level"' in line:
            r = json.loads(line)
            if r['kind'] == 'trigger':
                triggers.append(r)
            else:
                levels[r['package']] = r['streaming']
    return convs, starts, nodes, triggers, levels


def predictions(path):
    convs, starts, nodes, triggers, levels = read_scan(path)
    entries = cs.catalogue(path, None, quiet=True)
    by_conv = collections.defaultdict(list)
    for e in entries:
        by_conv[e['conversation']].append(e)
    roots, roots_of = cs.level_roots(levels)
    by_root = collections.defaultdict(list)
    for t in triggers:
        for root in roots_of(t['package']):
            by_root[root].append(t)
    out = []
    for k in sorted(convs):
        conv = convs[k]
        m = cs.measure(k, conv, starts.get(k, []), nodes.get(k, []))
        kd = cs.kind(m)
        v = cs.verdict(kd, m)
        f = cs.flow(conv)
        mine = by_conv.get(k, [])
        best = mine[0] if mine else None
        flowed = bool(f and f['flips'] and mine and cs.flow_wanted(k, kd, v))
        effects = sum(1 for n in (conv.get('nodes') or []) + (conv.get('replyNodes') or []) if n.get('transition', -1) != -1 or n.get('script', -1) != -1)
        risks = []
        if v == 'screen':
            risks.append('set-piece')
        if m['stageSpread'] >= 400:
            risks.append('wide-stage')
        if kd == 'walks':
            risks.append('npc-walk')
        if m['shepardMove']:
            risks.append('shepard-walk')
        if flowed:
            risks.append('flowed')
        if f and f['plotChains']:
            risks.append('plot-chain')
        if effects:
            risks.append('plot-effects')
        if kd == 'remote':
            risks.append('remote')
        near = None
        if best:
            at = best.get('ownerAt') or best.get('stageAt')
            if at:
                d = sorted(((math.dist(at, t['at']), t) for t in by_root.get(best['map'], [])), key=lambda x: x[0])
                close = [(dist, t) for dist, t in d if dist <= TRIGGER_NEAR_UNITS]
                if close:
                    risks.append('trigger-near')
                    near = {'count': len(close), 'nearestM': round(close[0][0] / 100, 2), 'names': [t['name'].split('.')[-1] for _, t in close[:3]]}
            if not best['touchShows'] and best['streamStates']:
                risks.append('named-state')
            if not best['volumeShows'] and not best['streamStates']:
                risks.append('no-route')
            if not best.get('ownerAt'):
                risks.append('owner-spawned')
        else:
            risks.append('no-start')
        starting = conv.get('starting') or []
        if starting and all(0 <= s < len(conv['nodes']) and conv['nodes'][s].get('conditional', -1) != -1 for s in starting):
            risks.append('conditional')
        out.append({
            'conversation': k, 'area': cs.area(k), 'kind': kd, 'verdict': v, 'lines': m['lines'], 'starts': len(mine),
            'map': best['map'] if best else None, 'chunk': best['chunk'] if best else None, 'flowed': flowed,
            'flowEntries': f['flips'] if flowed else [], 'plotEffects': effects, 'stageSpreadM': m['stageSpread'] / 100,
            'triggers': near, 'risks': risks,
            'sweep': 'bark' if kd == 'ambient' else 'screen' if v == 'screen' else 'smoke',
        })
    return out, entries


def predict(argv):
    rows, _ = predictions(argv[0])
    out = option(argv, '--out')
    if out:
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(rows, f, indent=0)
    sweep = collections.Counter(r['sweep'] for r in rows)
    print(f"{len(rows)} conversations: {sweep['smoke']} for the smoke pass, {sweep['screen']} set pieces (must go to the screen), "
          f"{sweep['bark']} barks (sampled)" + (f" -> {out}" if out else ''))
    risk = collections.Counter(x for r in rows for x in r['risks'] if r['sweep'] != 'bark')
    print('risks among the ' + str(sweep['smoke'] + sweep['screen']) + ' that enter the conversation mode: '
          + ', '.join(f'{k} {n}' for k, n in risk.most_common()))
    clean = sum(1 for r in rows if r['sweep'] == 'smoke' and not set(r['risks']) - {'plot-effects', 'conditional'})
    print(f"{clean} smoke scenes carry no risk but plot effects or conditional starts")
    if '--table' in argv:
        print('\n| Conversation | Kind | Verdict | Level | Risks |\n|---|---|---|---|---|')
        for r in rows:
            if r['sweep'] != 'bark':
                print(f"| {r['conversation']} | {r['kind']} | {r['verdict']} | {r['map'] or '-'} | {', '.join(r['risks'])} |")
    return 0


# ---- the runner's work list -----------------------------------------------------------------------------------------------------

def plan(argv):
    rows, entries = predictions(argv[0])
    out = option(argv, '--out')
    barks_per_area = int(option(argv, '--barks', '2'))
    first = {}
    for e in entries:
        first.setdefault(e['conversation'], e)
    scenes, unreachable, barks = [], [], collections.defaultdict(list)
    for r in rows:
        e = first.get(r['conversation'])
        if e is None:
            if r['sweep'] != 'bark':
                unreachable.append({'conversation': r['conversation'], 'why': 'no level script starts it'})
            continue
        scene = {
            'conversation': r['conversation'], 'map': e['map'], 'maps': e.get('maps', []), 'chunk': e['chunk'], 'package': e['package'],
            'action': e['action'], 'actionTail': e['actionTail'], 'ownerAt': e['ownerAt'], 'ownerYaw': e['ownerYaw'], 'stageAt': e['stageAt'],
            'touchShows': e['touchShows'], 'streamStates': e['streamStates'], 'kind': r['kind'], 'verdict': r['verdict'],
            'flowed': r['flowed'], 'flowEntries': r['flowEntries'], 'risks': r['risks'], 'sweep': r['sweep'],
        }
        if r['sweep'] == 'bark':
            if len(barks[r['area']]) < barks_per_area and e['ownerAt']:
                barks[r['area']].append(scene)
            continue
        scenes.append(scene)
    scenes += [s for area in sorted(barks) for s in barks[area]]
    by_map = collections.defaultdict(lambda: collections.defaultdict(list))
    for s in scenes:
        by_map[s['map']][s['chunk']].append(s)
    maps = [{'map': mp, 'chunks': [{'chunk': c, 'scenes': by_map[mp][c]} for c in sorted(by_map[mp])]} for mp in sorted(by_map, key=str)]
    doc = {'scenes': len(scenes), 'maps': maps, 'unreachable': unreachable}
    if out:
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(doc, f, indent=0)
    print(f"{len(scenes)} scenes in {len(maps)} levels ({sum(1 for s in scenes if s['sweep'] == 'bark')} of them barks, sampled); "
          f"{len(unreachable)} conversations no level script starts" + (f" -> {out}" if out else ''))
    for mp in maps:
        n = sum(len(c['scenes']) for c in mp['chunks'])
        print(f"   {mp['map']}: {n} scenes in {len(mp['chunks'])} chunks")
    return 0


# ---- the canary set -------------------------------------------------------------------------------------------------------------

def canary(argv):
    """The canary set's own small plan and its expectations, from the full plan and tools/conversation-canary.json."""
    doc = json.load(open(argv[0], encoding='utf-8'))
    spec = json.load(open(argv[1], encoding='utf-8'))
    part = spec['broken'] if '--broken' in argv else spec
    scenes = {s['conversation']: s for lv in doc['maps'] if lv['map'] == spec['level'] for c in lv['chunks'] for s in c['scenes']}
    chunks, missing = [], []
    for name in part['order']:
        if name not in scenes:
            missing.append(name)
            continue
        scene = dict(scenes[name])
        for option in ('walks', 'alsoFire'):
            if option in part['scenes'].get(name, {}):
                scene[option] = part['scenes'][name][option]
        chunks.append({'chunk': scene['chunk'], 'scenes': [scene]})
    out_plan, out_expect = option_value(argv, '--out-plan'), option_value(argv, '--out-expect')
    if out_plan:
        with open(out_plan, 'w', encoding='utf-8') as f:
            json.dump({'scenes': len(chunks), 'maps': [{'map': spec['level'], 'chunks': chunks}], 'unreachable': []}, f, indent=0)
    if out_expect:
        with open(out_expect, 'w', encoding='utf-8') as f:
            json.dump({k: v['expect'] for k, v in part['scenes'].items() if 'expect' in v}, f, indent=0)
    print(f"the canary set{' (broken: ' + ', '.join(f'{k} = {v}' for k, v in part.get('set', {}).items()) + ')' if '--broken' in argv else ''}: "
          f"{len(chunks)} scenes at {spec['level']}" + (f"; not in the plan: {', '.join(missing)}" if missing else ''))
    return 1 if missing else 0


# ---- the judge ------------------------------------------------------------------------------------------------------------------

def load_results(paths):
    """The last record per conversation (a re-run replaces the first), and how many records there were."""
    last, count = collections.OrderedDict(), 0
    for path in paths:
        for line in open(path, encoding='utf-8-sig'):
            line = line.strip()
            if not line:
                continue
            r = json.loads(line)
            if r.get('outcome') == 'already played':  # the survey's note for a second action of a conversation it had played
                continue
            count += 1
            s = r.get('summary') or {}
            k = r.get('conversation') or (conversation_key(s['name']) if s.get('name') else None) \
                or (conversation_key((r.get('flow') or {}).get('name')) if r.get('flow') else None)
            if not k:
                k = 'action:' + str(r.get('source') or r.get('action') or count)  # a survey record that reached nothing
            r['conversation'] = k
            last.pop(k, None)
            last[k] = r
    return last, count


def terminal(conv, entry):
    """True if a play can end at `entry`: it has no reply, or a reply that leads nowhere (the dialog's end)."""
    nodes, replies = conv['nodes'], conv.get('replyNodes') or []
    if not 0 <= entry < len(nodes):
        return True
    mine = [replies[r] for r in nodes[entry]['replies'] if 0 <= r < len(replies)]
    return not mine or any(not r['entries'] for r in mine)


def judge_one(r, conv, prediction):
    """The exception kinds of one scene's record, each with a few words of detail."""
    kinds = []
    def add(kind, detail=''):
        kinds.append((kind, detail))
    expect = dict(r.get('expect') or {})
    if prediction:
        expect.setdefault('verdict', prediction['verdict'])
        expect.setdefault('flowed', prediction['flowed'])
    outcome = (r.get('outcome') or '').lower()
    s = r.get('summary')
    flow = r.get('flow')
    if outcome == 'reached':  # a reach-only run: the start action was found, nothing was fired
        return kinds
    if outcome in ('crash', 'hang'):
        add(outcome, r.get('detail', ''))
    if outcome in ('not reached', 'action not loaded') or 'never reached' in outcome:
        add('not-reached', r.get('detail', outcome))
        return kinds
    if not s and not flow:
        if outcome not in ('crash', 'hang'):
            add('no-start', r.get('detail', outcome or 'no summary'))
        return kinds
    if r.get('ended') == 'forced' or 'ended by' in outcome or 'time' in outcome:
        add('forced-end', outcome)
    if expect.get('flowed'):
        if not flow:
            add('chain-not-played', 'it played as a conversation' if s else 'nothing played')
        else:
            path = flow.get('entries') or []
            why = flow.get('why', '')
            if why == 'another conversation began' and conversation_key(flow.get('next')) != conversation_key(flow.get('name')):
                add('chain-cut', f"{flow.get('next')} began over it at entry {path[-1] if path else '?'}")
            elif conv and path and not terminal(conv, path[-1]):
                add('chain-cut', f"ended at entry {path[-1]}, which the graph goes on from ({why})")
    if s:
        world = bool(s.get('world'))
        verdict = expect.get('verdict')
        if world:
            if s.get('kept', 0) > 0:
                add('undone', f"kept {s['kept']}")
            if s.get('drawn', 0) > 0:
                add('drawn-jump', f"drawn {s['drawn']}")
            if s.get('eyeToViewM', 0) >= EYE_FAR_M and not expect.get('afterCutscene'):
                add('eye-far', f"{s['eyeToViewM']:.2f} m")
        if s.get('noPlace') and verdict != 'screen':
            add('no-place', s['noPlace'])
        if verdict == 'screen' and world and not s.get('autoScreened'):
            add('not-on-screen', 'a set piece in the world')
        if verdict in ('world', 'check') and not world and r.get('worldMode', True) and not s.get('autoScreened'):
            add('not-in-world', 'world false')
        driver = r.get('driver') or {}
        if 'picks' in driver and s.get('wheel', 0) > driver['picks']:
            add('wheel-unanswered', f"wheel {s['wheel']}, picks {driver['picks']}")
        path = s.get('entries') or driver.get('entries')  # the mod's own list of the entries it passed through
        if conv and path:
            text = sum(1 for e in set(path) if 0 <= e < len(conv['nodes']) and conv['nodes'][e].get('text') and not conv['nodes'][e].get('nonText'))
            if s.get('lines', 0) < text:
                add('lines-short', f"lines {s.get('lines', 0)}, {text} entries with text seen")
    return kinds


BASELINE_FIELDS = ('world', 'kept', 'drawn', 'sourceKept', 'wheel', 'noPlace', 'autoScreened', 'lines')


def judge(argv):
    paths = []
    for a in argv:
        if a.startswith('--'):
            break
        paths.append(a)
    results, count = load_results(paths)
    convs = {}
    scan = option(argv, '--scan')
    if scan:
        convs, _, _ = cs.load(scan)
    preds = {}
    if option(argv, '--predictions'):
        preds = {p['conversation']: p for p in json.load(open(option(argv, '--predictions'), encoding='utf-8'))}
    judged = collections.OrderedDict()
    for k, r in results.items():
        judged[k] = judge_one(r, convs.get(k), preds.get(k))
    lines = []
    inside = [k for k, kinds in judged.items() if not kinds]
    by_kind = collections.defaultdict(list)
    for k, kinds in judged.items():
        for kind, detail in kinds:
            by_kind[kind].append((k, detail))
    lines.append(f"{len(results)} scenes judged from {count} records: {len(inside)} inside every limit, {len(results) - len(inside)} exceptions"
                 + ('' if scan else ' (no --scan: the graph checks, lines-short and chain-cut by the graph, were skipped)'))
    for kind in sorted(by_kind, key=lambda x: -len(by_kind[x])):
        lines.append(f"\n## {kind} ({len(by_kind[kind])})\n")
        lines.append('| Conversation | Level | Detail | Predicted risks |\n|---|---|---|---|')
        for k, detail in by_kind[kind]:
            lines.append(f"| {k} | {results[k].get('map') or '-'} | {detail} | {', '.join((preds.get(k) or {}).get('risks', []))} |")
    if preds:
        # How well layer 0 called it: per risk, how many of the scenes carrying it ended as an exception.
        lines.append('\n## Predictions against the run\n\n| Risk | Scenes run | Exceptions | Share |\n|---|---|---|---|')
        per = collections.defaultdict(lambda: [0, 0])
        for k, kinds in judged.items():
            for risk in (preds.get(k) or {}).get('risks', []) or ['(none)']:
                per[risk][0] += 1
                per[risk][1] += bool(kinds)
        for risk, (n, bad) in sorted(per.items(), key=lambda x: -x[1][1]):
            lines.append(f"| {risk} | {n} | {bad} | {100 * bad / n:.0f}% |")
    status = 0
    record = {k: {'kinds': sorted({kind for kind, _ in kinds}),
                  **{f: (results[k].get('summary') or {}).get(f) for f in BASELINE_FIELDS}} for k, kinds in judged.items()}
    base_path = option(argv, '--baseline')
    if base_path:
        base = json.load(open(base_path, encoding='utf-8'))
        changed, new = [], []
        tolerance = int(option(argv, '--lines-tolerance', '0'))
        for k, now in record.items():
            was = base.get(k)
            if was is None:
                new.append(k)
                continue
            diff = [f"{f}: {was.get(f)} -> {now.get(f)}" for f in ('kinds',) + BASELINE_FIELDS
                    if was.get(f) != now.get(f) and not (f == 'lines' and None not in (was.get(f), now.get(f)) and abs(was[f] - now[f]) <= tolerance)]
            if diff:
                changed.append((k, diff))
        gone = [k for k in base if k not in record]
        lines.append(f"\n## Against the baseline ({os.path.basename(base_path)})\n\n{len(changed)} scenes differ, {len(new)} are new, {len(gone)} were not run")
        for k, diff in changed:
            lines.append(f"- {k}: {'; '.join(diff)}")
        if '--strict' in argv and any(set(record[k]['kinds']) - set((base.get(k) or {}).get('kinds', [])) for k in record):
            status = 1
    elif '--strict' in argv and len(inside) != len(results):
        status = 1
    if option(argv, '--expect'):
        want = json.load(open(option(argv, '--expect'), encoding='utf-8'))
        failed = []
        for k, w in want.items():
            if k not in record:
                failed.append(f"{k}: not in the results")
                continue
            got, s = record[k]['kinds'], results[k].get('summary') or {}
            if 'kinds' in w and sorted(w['kinds']) != got:
                failed.append(f"{k}: kinds {got}, expected {sorted(w['kinds'])}")
            for kind in w.get('has') or []:
                if kind not in got:
                    failed.append(f"{k}: kinds {got}, expected {kind} among them")
            for kind in w.get('lacks') or []:
                if kind in got:
                    failed.append(f"{k}: kinds {got}, expected no {kind}")
            for f, lo in (w.get('min') or {}).items():
                if s.get(f, 0) < lo:
                    failed.append(f"{k}: {f} {s.get(f)}, expected at least {lo}")
            for f, hi in (w.get('max') or {}).items():
                if s.get(f, 0) > hi:
                    failed.append(f"{k}: {f} {s.get(f)}, expected at most {hi}")
            flow = results[k].get('flow') or {}
            for f, lo in (w.get('flowMin') or {}).items():
                if flow.get(f, 0) < lo:
                    failed.append(f"{k}: the flowed scene's {f} {flow.get(f)}, expected at least {lo}")
        lines.append(f"\n## The canary set ({os.path.basename(option(argv, '--expect'))})\n\n{len(want) - len({x.split(':')[0] for x in failed})} of {len(want)} as expected")
        lines += [f"- {x}" for x in failed]
        if failed:
            status = 1
    text = '\n'.join(lines)
    print(text)
    if option(argv, '--report'):
        with open(option(argv, '--report'), 'w', encoding='utf-8') as f:
            f.write(text + '\n')
    if option(argv, '--write-baseline'):
        with open(option(argv, '--write-baseline'), 'w', encoding='utf-8') as f:
            json.dump(record, f, indent=0, sort_keys=True)
    return status


def main(argv):
    if len(argv) < 3 or argv[1] not in ('predict', 'plan', 'judge', 'canary'):
        print(__doc__)
        return 2
    return {'predict': predict, 'plan': plan, 'judge': judge, 'canary': canary}[argv[1]](argv[2:])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
