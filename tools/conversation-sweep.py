# SPDX-License-Identifier: MIT
"""The conversation sweep's data side (research/69 "The sweep: plan"): what to expect of every conversation before the game
runs (layer 0), the runner's work list, and the judge of a finished pass. No game needed for any of it.

  python tools/conversation-sweep.py predict scan.jsonl [--out predictions.json] [--table]
  python tools/conversation-sweep.py plan scan.jsonl --out plan.json [--barks 2]
  python tools/conversation-sweep.py plan scan.jsonl --out sample.json --sample [--per-shape 2] [--per-place 1]
        [--reach reach-scenes.jsonl] [--also names.txt] [--barks 1]
  python tools/conversation-sweep.py moments scan.jsonl --out moments.json     the matinees that walk Shepard, as a plan
  python tools/conversation-sweep.py reach <folder of a -ReachOnly run>
  python tools/conversation-sweep.py saves scan.jsonl list.json --base <career save> [--table saves.md] [--dry-run]
        saves for looking at scenes in the headset: the player in front of each scene, before it starts (--remove)
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
  no-start         no level script starts it or plays its lines: the sweep cannot fire it
  cutscene-vo      (not swept) no start action; voice-over actions in an animated cutscene's sequence play its lines
  gameplay-vo      (not swept) no start action; voice-over actions play its lines on foot (a squadmate's remark, radio)
  conditional      every start entry has a plot conditional: a fired action may find none that holds

plan: the runner's work list, one scene per conversation that a level script starts (its best start: a placed owner
first), grouped by the level to open and the chunk to stream, plus a sample of barks per area.

judge: reads the runner's results (one JSON object per scene; tools/conversation-survey.ps1's records are understood too)
and sorts every scene into "inside every limit" or one or more exception kinds:
  not-reached      the level, chunk or start action was never there
  no-start         the action fired and no conversation (or flowed scene) followed
  crash, hang      the game died or stopped answering during the scene
  forced-end       it did not end by itself (ended by the runner after the time-out)
  undone           kept > yielded: someone was put on a mark and moved back after the fact (the source hook should withhold
                   it); one undone step per small move the game repeated and the mod then accepted (`yielded`) is expected
  drawn-jump       drawn > yielded: a jump reached a drawn frame
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


def voice_overs(path):
    """Per conversation, the level script's voice-over actions that play its lines (BioSeqAct_FaceOnlyVO): how many, and
    how many of them sit in an animated cutscene's sequence."""
    out = collections.defaultdict(lambda: [0, 0])
    for line in open(path, encoding='utf-8'):
        if '"kind":"vo"' in line:
            r = json.loads(line)
            k = cs.key(r['conv'], 'd')
            out[k][0] += 1
            out[k][1] += 'ANIMCUTSCENE' in r['path'].upper()
    return out


def chunk_of(package):
    # BIOA_STA60_05A_DSG -> sta60_05a: how a streaming state spells the chunk
    return re.sub(r'_(DSG|LAY|ART|SND|PLC|CIN|DS[0-9]).*$', '', re.sub(r'^BIOA_', '', package, flags=re.IGNORECASE), flags=re.IGNORECASE).lower()


def stream_ways(path):
    """Per persistent level, the way to each chunk: the chunks to stream in, one after the other, from the level's own
    package. A volume lives in a chunk and shows others when touched (a named state: when the plot's streaming state is
    on); a chunk deep in a hub (the Citadel's wards, Noveria's hot labs) is shown only by volumes of chunks nearer the
    entrance, so the runner streams the way hop by hop. The fewest hops, plain states preferred over named ones."""
    import heapq
    levels, volumes = {}, []
    for line in open(path, encoding='utf-8'):
        if '"kind":"volume"' in line or '"kind":"level"' in line:
            r = json.loads(line)
            if r['kind'] == 'volume':
                volumes.append(r)
            else:
                levels[r['package']] = r['streaming']
    roots, roots_of = cs.level_roots(levels)
    ways = {}
    for root in roots:
        edges = collections.defaultdict(list)
        for v in volumes:
            if root not in roots_of(v['package']):
                continue
            home = '' if v['package'].lower() == root.lower() else chunk_of(v['package'])
            for s in v['states']:
                cost = 1.25 if cs.named(s) else 1.0
                for name in s.get('visible') or []:
                    edges[home].append((cost, re.sub(r'^bioa_', '', (name or '').lower())))
        dist, prev, heap = {'': 0.0}, {}, [(0.0, '')]
        while heap:
            d, c = heapq.heappop(heap)
            if d > dist.get(c, 1e9):
                continue
            for cost, nxt in edges.get(c, ()):
                if d + cost < dist.get(nxt, 1e9):
                    dist[nxt] = d + cost
                    prev[nxt] = c
                    heapq.heappush(heap, (d + cost, nxt))
        def way(c):
            out = []
            while c:
                out.append(c)
                c = prev.get(c, '')
            return out[::-1]
        ways[root] = {c: way(c) for c in dist if c}
    return ways


def predictions(path):
    convs, starts, nodes, triggers, levels = read_scan(path)
    entries = cs.catalogue(path, None, quiet=True)
    by_conv = collections.defaultdict(list)
    for e in entries:
        by_conv[e['conversation']].append(e)
    roots, roots_of = cs.level_roots(levels)
    vo = voice_overs(path)
    by_root = collections.defaultdict(list)
    for t in triggers:
        for root in roots_of(t['package']):
            by_root[root].append(t)
    out = []
    for k in sorted(convs):
        conv = convs[k]
        m = cs.measure(k, conv, starts.get(k, []), nodes.get(k, []))
        kd = cs.kind(m)
        v = cs.verdict(kd, m, k)
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
        elif vo.get(k):
            risks.append('cutscene-vo' if vo[k][1] else 'gameplay-vo')
        else:
            risks.append('no-start')
        # How much the scene asks of First person, from the conversation's own make-up (not from where it plays):
        #   C authored: the game moves someone or frames the scene itself (a set piece, an authored walk, Shepard walked)
        #   B staged+:  more than the plain case: a flowed chain, a chain kept for its plot, a wide stage, a transmission,
        #               or a stage for three or more
        #   A simple:   two people on a small stage, or nobody placed at all, and a wheel
        # and its shape: kind, stage type, wheel or watched, and those marks. Scenes of one shape exercise the same rules, so
        # a costly pass (full length, the headset) can take a few of each shape; where a scene plays (a trigger near, a
        # spawned owner, a named state) is its own risk, per scene, and only a run of that scene answers it.
        wheel = bool(conv.get('nodes')) and cs.has_wheel(conv, range(len(conv['nodes'])))
        big_stage = any(x.group(1) not in ('1', '2') for st in m['stageTypes'] for x in [re.match(r'ST_(\d+)P', st)] if x)
        make_up = [x for x in risks if x in ('set-piece', 'npc-walk', 'shepard-walk', 'flowed', 'plot-chain', 'wide-stage', 'remote')]
        tier = 'C' if set(make_up) & {'set-piece', 'npc-walk', 'shepard-walk'} else 'B' if make_up or big_stage else 'A'
        stage_kinds = sorted({(x.group(1) + 'P' if x else st) for st in m['stageTypes'] if st != '-' for x in [re.match(r'ST_(\d+)P', st)]})
        shape = ' | '.join([kd, ','.join(stage_kinds) or 'no stage', 'wheel' if wheel else 'watched', ','.join(sorted(make_up)) or '-'])
        starting = conv.get('starting') or []
        if starting and all(0 <= s < len(conv['nodes']) and conv['nodes'][s].get('conditional', -1) != -1 for s in starting):
            risks.append('conditional')
        out.append({
            'conversation': k, 'area': cs.area(k), 'kind': kd, 'verdict': v, 'lines': m['lines'], 'starts': len(mine),
            'map': best['map'] if best else None, 'chunk': best['chunk'] if best else None, 'flowed': flowed,
            'flowEntries': f['flips'] if flowed else [], 'plotEffects': effects, 'stageSpreadM': m['stageSpread'] / 100,
            'triggers': near, 'risks': risks, 'tier': tier, 'shape': shape,
            'voiceOvers': vo[k][0] if k in vo else 0,
            # A conversation no level script starts and whose lines voice-over actions play never enters the conversation
            # mode: nothing for First person to place (a cutscene's lines go where the cutscene goes, the 3D screen).
            'sweep': 'bark' if kd == 'ambient' else 'vo' if not mine and vo.get(k) else 'screen' if v == 'screen' else 'smoke',
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
          f"{sweep['bark']} barks (sampled), {sweep['vo']} played only as voice-over (never the conversation mode: "
          f"{sum(1 for r in rows if r['sweep'] == 'vo' and 'cutscene-vo' in r['risks'])} of them under animated cutscenes)"
          + (f" -> {out}" if out else ''))
    risk = collections.Counter(x for r in rows for x in r['risks'] if r['sweep'] in ('smoke', 'screen'))
    print('risks among the ' + str(sweep['smoke'] + sweep['screen']) + ' that enter the conversation mode: '
          + ', '.join(f'{k} {n}' for k, n in risk.most_common()))
    swept = [r for r in rows if r['sweep'] in ('smoke', 'screen')]
    tiers = collections.Counter(r['tier'] for r in swept)
    place = {'trigger-near', 'owner-spawned', 'named-state', 'no-route'}
    print(f"by make-up: {tiers['A']} simple (A), {tiers['B']} staged and more (B), {tiers['C']} authored (C); "
          f"{len({r['shape'] for r in swept})} shapes, {sum(1 for n in collections.Counter(r['shape'] for r in swept).values() if n == 1)} of them with one scene; "
          f"{sum(1 for r in swept if set(r['risks']) & place)} scenes with a risk from where they play")
    clean = sum(1 for r in rows if r['sweep'] == 'smoke' and not set(r['risks']) - {'plot-effects', 'conditional'})
    print(f"{clean} smoke scenes carry no risk but plot effects or conditional starts")
    if '--table' in argv:
        print('\n| Conversation | Kind | Verdict | Level | Risks |\n|---|---|---|---|---|')
        for r in rows:
            if r['sweep'] in ('smoke', 'screen'):
                print(f"| {r['conversation']} | {r['kind']} | {r['verdict']} | {r['map'] or '-'} | {', '.join(r['risks'])} |")
    return 0


# ---- the runner's work list -----------------------------------------------------------------------------------------------------

def plan(argv):
    rows, entries = predictions(argv[0])
    ways = stream_ways(argv[0])
    out = option(argv, '--out')
    barks_per_area = int(option(argv, '--barks', '2'))
    # --sample: a pass that does not run every scene of a kind (research/69 "The sweep: progress", sampling). Taken:
    #   every authored scene (tier C);
    #   of each other shape, --per-shape scenes of tier B (default 2) and one of tier A;
    #   with --per-place N (default 0), also N scenes of each level's each place risk (a trigger near, a spawned owner, a
    #   named state, no route) per tier: what a scene's make-up cannot say, where it plays, is sampled by level;
    #   the scenes named in --also (a file of names, one a line: an earlier pass's exceptions).
    # --reach <reach-scenes.jsonl> keeps scenes a reach run could not get to out of the sample (they are listed), and
    # prefers the ones it reached. No barks unless --barks is given.
    keep, skipped_unreached = None, []
    if '--sample' in argv:
        per_shape, per_place = int(option(argv, '--per-shape', '2')), int(option(argv, '--per-place', '0'))
        also = set()
        if option(argv, '--also'):
            also = {line.strip() for line in open(option(argv, '--also'), encoding='utf-8-sig') if line.strip()}
        reached, unreached = set(), set()
        if option(argv, '--reach'):
            for line in open(option(argv, '--reach'), encoding='utf-8-sig'):
                if line.strip():
                    x = json.loads(line)
                    (unreached if x.get('outcome') in ('not reached', 'action not loaded') else reached).add(x['conversation'])
            unreached -= reached
        level_of = {}
        for e in entries:
            level_of.setdefault(e['conversation'], e['map'])
        place = ('trigger-near', 'owner-spawned', 'named-state', 'no-route')
        taken, placed, keep = collections.Counter(), collections.Counter(), set(also)
        candidates = [r for r in rows if r['sweep'] in ('smoke', 'screen') and r['conversation'] in level_of]
        for r in sorted(candidates, key=lambda r: (r['conversation'] not in reached, r['conversation'])):
            k = r['conversation']
            if k in unreached:
                skipped_unreached.append(k)
                continue
            limit = 10 ** 6 if r['tier'] == 'C' else per_shape if r['tier'] == 'B' else 1
            if taken[r['shape']] < limit:
                taken[r['shape']] += 1
                keep.add(k)
            cells = [(level_of[k], r['tier'], x) for x in r['risks'] if x in place] or [(level_of[k], r['tier'], 'no place risk')]
            if any(placed[c] < per_place for c in cells):
                keep.add(k)
            if k in keep:
                for c in cells:
                    placed[c] += 1
        barks_per_area = int(option(argv, '--barks', '0'))
    first = {}
    for e in entries:
        first.setdefault(e['conversation'], e)
    scenes, unreachable, barks = [], [], collections.defaultdict(list)
    for r in rows:
        e = first.get(r['conversation'])
        if e is None:
            if r['sweep'] in ('smoke', 'screen'):
                unreachable.append({'conversation': r['conversation'], 'why': 'no level script starts it'})
            continue
        scene = {
            'conversation': r['conversation'], 'map': e['map'], 'maps': e.get('maps', []), 'chunk': e['chunk'], 'package': e['package'],
            'action': e['action'], 'actionTail': e['actionTail'], 'ownerAt': e['ownerAt'], 'ownerYaw': e['ownerYaw'], 'stageAt': e['stageAt'],
            'touchShows': e['touchShows'], 'streamStates': e['streamStates'], 'kind': r['kind'], 'verdict': r['verdict'],
            'flowed': r['flowed'], 'flowEntries': r['flowEntries'], 'risks': r['risks'], 'sweep': r['sweep'],
            'streamVia': ways.get(e['map'], {}).get(chunk_of(e['package']), []),
        }
        # The scene's own way in (the scan's `via`): the first action behind the event of the player's doing that leads to
        # this start. Fired instead of the bare start, the level's script runs as authored (its plot checks, streaming
        # states, teleports and matinees, then the start); kept to the player's own events and short chains.
        via = e.get('via')
        if via and via['eventClass'] in ('BioSeqEvt_OnPlayerActivate', 'SeqEvent_Touch', 'BioSeqEvt_ArtPlaceableUsed', 'SeqEvent_Used',
                                         'BioSeqEvt_ForegroundStreamingDone') and via['hops'] <= 10 and via['between']:
            scene['via'] = {'opTail': re.sub(r'^.*Main_Sequence[.]', '', via['firstOp']), 'opClass': via['firstOpClass'], 'input': via['firstOpInput'],
                            'event': via['eventClass'], 'originator': via.get('originator'), 'hops': via['hops'], 'between': via['between']}
        scene['tier'], scene['shape'], scene['lines'] = r['tier'], r['shape'], r['lines']
        if r['sweep'] == 'bark':
            if len(barks[r['area']]) < barks_per_area and e['ownerAt']:
                barks[r['area']].append(scene)
            continue
        if keep is not None and r['conversation'] not in keep:
            continue
        scenes.append(scene)
    scenes += [s for area in sorted(barks) for s in barks[area]]
    by_map = collections.defaultdict(lambda: collections.defaultdict(list))
    for s in scenes:
        by_map[s['map']][s['chunk']].append(s)
    maps = [{'map': mp, 'chunks': [{'chunk': c, 'scenes': by_map[mp][c]} for c in sorted(by_map[mp])]} for mp in sorted(by_map, key=str)]
    doc = {'scenes': len(scenes), 'maps': maps, 'unreachable': unreachable, 'unreached': sorted(skipped_unreached)}
    if keep is not None:
        by_tier = collections.Counter(s['tier'] for s in scenes if s['sweep'] != 'bark')
        total = collections.Counter(r['tier'] for r in rows if r['sweep'] in ('smoke', 'screen'))
        print('the sample: ' + ', '.join(f"{by_tier[t]} of {total[t]} tier {t}" for t in 'ABC')
              + (f"; {len(skipped_unreached)} left out as not reached" if skipped_unreached else ''))
    if out:
        with open(out, 'w', encoding='utf-8') as f:
            json.dump(doc, f, indent=0)
    print(f"{len(scenes)} scenes in {len(maps)} levels ({sum(1 for s in scenes if s['sweep'] == 'bark')} of them barks, sampled); "
          f"{len(unreachable)} conversations no level script starts" + (f" -> {out}" if out else ''))
    for mp in maps:
        every = [s for c in mp['chunks'] for s in c['scenes']]
        hops = collections.Counter(len(s['streamVia']) for s in every)
        print(f"   {mp['map']}: {len(every)} scenes in {len(mp['chunks'])} chunks; streaming hops "
              + ', '.join(f"{'no way' if k == 0 else k}: {n}" for k, n in sorted(hops.items())))
    return 0


# ---- the authored moments -------------------------------------------------------------------------------------------------------

def moment_scenes(path):
    """The level-script matinees that move the player by a track, outside animated cutscenes, per level: the scenes of the
    moments plan (and of the validation saves)."""
    levels, rows = {}, []
    for line in open(path, encoding='utf-8'):
        if '"kind":"level"' in line:
            r = json.loads(line)
            levels[r['package']] = r['streaming']
        elif '"kind":"matinee"' in line and '"via"' in line:
            r = json.loads(line)
            if 'ANIMCUTSCENE' in r['path'].upper() or 'Node_Data_Sequence' in r['path'] or '_LOC_' in r['package'].upper():
                continue
            if not any(g['class'] == 'InterpGroupDirector' for g in r['groups']):
                continue
            rows.append(r)
    roots, roots_of = cs.level_roots(levels)
    ways = stream_ways(path)
    by_map = collections.defaultdict(list)
    for r in rows:
        via = r['via']
        maps = roots_of(r['package'])
        by_name = cs.persistent(r['package'])
        mp = by_name if by_name in maps else (maps[0] if maps else by_name)
        tail = re.sub(r'^.*Main_Sequence[.]', '', r['path'])
        name = 'moment_' + re.sub(r'[^a-z0-9]+', '_', (r['package'].replace('BIOA_', '') + '_' + tail.split('.')[-2 if '.' in tail else 0]).lower()).strip('_')
        chunk = re.sub(r'^BIOA_', '', re.sub(r'_(DSG|LAY).*$', '', r['package']), flags=re.IGNORECASE)
        taken = {s['conversation'] for v in by_map.values() for s in v}
        if name in taken:
            name = next(f'{name}_{i}' for i in range(2, 99) if f'{name}_{i}' not in taken)
        by_map[mp].append({
            'conversation': name, 'moment': True, 'map': mp, 'maps': maps, 'chunk': chunk, 'package': r['package'],
            'actionTail': re.sub(r'^.*Main_Sequence[.]', '', via['firstOp']), 'actionClass': via['firstOpClass'], 'actionInput': via['firstOpInput'],
            'matinee': tail, 'lengthS': round(r['length'], 1), 'event': via['eventClass'], 'between': via['between'],
            # The way to the matinee action by action: where the first one is a plot check a fresh level does not pass, the
            # runner fires the first one behind it that is no check (a test rig's shortcut; the plot is not set).
            'chain': [dict({'tail': re.sub(r'^.*Main_Sequence[.]', '', c['op']), 'class': c['class'], 'input': c['input']},
                           **{k: c[k] for k in ('out', 'plot') if k in c}) for c in via.get('chain', [])],
            'ownerAt': via.get('originatorAt'), 'ownerYaw': via.get('originatorYaw') or 0, 'ownerClass': via.get('originatorClass'),
            'stageAt': None, 'touchShows': True, 'streamStates': [],
            'kind': 'moment', 'verdict': 'moment', 'tier': 'C', 'shape': 'moment', 'flowed': False, 'flowEntries': [], 'risks': ['shepard-walk'],
            'sweep': 'moment', 'streamVia': ways.get(mp, {}).get(chunk_of(r['package']), []),
        })
    return by_map


def moments(argv):
    """The level-script matinees that move the player by a track, outside animated cutscenes (research/69 "Authored scenes"),
    as a plan the runner can play: each fired through the first action behind the event that leads to it, Shepard stood by
    that event's originator. The council's walk-in is one of them."""
    path = argv[0]
    out = option(argv, '--out')
    by_map = moment_scenes(path)
    maps = [{'map': mp, 'chunks': [{'chunk': s['chunk'], 'scenes': [s]} for s in sorted(by_map[mp], key=lambda s: (s['chunk'], s['conversation']))]}
            for mp in sorted(by_map, key=str)]
    total = sum(len(v) for v in by_map.values())
    if out:
        with open(out, 'w', encoding='utf-8') as f:
            json.dump({'scenes': total, 'maps': maps, 'unreachable': []}, f, indent=0)
    print(f"{total} moments in {len(maps)} levels" + (f" -> {out}" if out else ''))
    for mp in maps:
        for c in mp['chunks']:
            for s in c['scenes']:
                print(f"   {mp['map']:12s} {s['conversation']:44s} {s['lengthS']:5.1f} s  by {s['event'].replace('BioSeqEvt_', '').replace('SeqEvent_', '')}"
                      f"{' at a placed originator' if s['ownerAt'] else ''}; before it: {', '.join(x.replace('BioSeqAct_', '').replace('SeqAct_', '') for x in s['between'][:6]) or '-'}")
    return 0


# ---- validation saves ------------------------------------------------------------------------------------------------------------

def plot_for(chain):
    """The plot values a scene's way from its event wants: each plot check on it (BioSeqAct_PMCheckState) with the output
    the way leaves it by. Returns ({index: 0|1}, [what could not be set])."""
    plot, unset = {}, []
    for c in chain or []:
        if 'PMCheck' not in c.get('class', ''):
            continue
        if c['class'].endswith('PMCheckState') and 'plot' in c and c.get('out') in ('True', 'False'):
            plot[int(c['plot'])] = 1 if c['out'] == 'True' else 0
        else:
            unset.append(f"{c['class'].replace('BioSeqAct_', '')}{' ' + str(c['plot']) if 'plot' in c else ''} (leaves by {c.get('out', '?')})")
    return plot, unset


def stand_before(at, yaw_deg, approach_m):
    """A place approach_m in front of something placed at `at` and facing yaw_deg (Unreal: 0 along +X, 90 along +Y), 20 units
    up (the game drops the player onto the floor), and the facing that looks back at it."""
    import math
    r = math.radians(yaw_deg or 0.0)
    return ([at[0] + math.cos(r) * approach_m * 100.0, at[1] + math.sin(r) * approach_m * 100.0, at[2] + 20.0],
            ((yaw_deg or 0.0) + 180.0) % 360.0)


def saves(argv):
    """Saves for looking at scenes in the headset (the owner, 2026-11-10: "a system which will allow me the save to appear
    ahead of Anderson (before the dialogue would engage) and all of the other scenes we need to validate"): for each scene
    of a list, a copy of a career save with the scene's level, the player a few metres in front of the person (or the
    event's originator) and facing it, before anything engages, and the plot values the scene's own script checks on the
    way set as it wants them (tools/le1-save.py does the writing). Numbered slots of the career, so the game's Load screen
    lists them (it reads the folder once, at the game's start); a table says which slot is which scene; --remove deletes
    what the last run wrote.

      conversation-sweep.py saves scan.jsonl list.json --base <career save.pcsav> [--out-dir <career folder>] [--first-slot 80]
            [--approach 4] [--table saves.md] [--manifest file.json] [--dry-run]
      conversation-sweep.py saves --remove [--manifest file.json]

    list.json: {"scenes": [{"scene": "<conversation or moment name>", "title": "...", "approachM": 4, "plot": {"123": 1},
    "at": [x, y, z], "yaw": 0, "action": "<text in the start's path>"}]} (all but "scene" optional; "at"/"yaw" replace the
    scan's place; "plot" adds to the checks')."""
    import subprocess
    manifest_path = option(argv, '--manifest') or os.path.join(os.environ.get('LOCALAPPDATA', '.'), 'MEVR', 'validation-saves.json')
    if '--remove' in argv:
        if not os.path.exists(manifest_path):
            print(f"nothing to remove: no {manifest_path}")
            return 0
        listed = json.load(open(manifest_path, encoding='utf-8'))
        gone = 0
        for f in listed.get('files', []):
            if os.path.exists(f):
                os.remove(f)
                gone += 1
        os.remove(manifest_path)
        print(f"removed {gone} of {len(listed.get('files', []))} validation saves; {manifest_path} deleted")
        return 0
    scan_path, list_path = argv[0], argv[1]
    base = option(argv, '--base')
    if not base or not os.path.exists(base):
        print('saves: --base <career save.pcsav> is needed')
        return 2
    out_dir = option(argv, '--out-dir') or os.path.dirname(os.path.abspath(base))
    first = int(option(argv, '--first-slot', '80'))
    approach = float(option(argv, '--approach', '4'))
    dry = '--dry-run' in argv
    table = option(argv, '--table')
    career = re.match(r'(.+?)_[^_]+$', os.path.splitext(os.path.basename(base))[0])
    career = career.group(1) if career else os.path.splitext(os.path.basename(base))[0]
    wanted = json.load(open(list_path, encoding='utf-8'))['scenes']
    starts = collections.defaultdict(list)
    for e in cs.catalogue(scan_path, None, quiet=True):
        starts[e['conversation']].append(e)
    moments_by = {s['conversation']: s for v in moment_scenes(scan_path).values() for s in v}
    tool = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'le1-save.py')
    rows, files, commands = [], [], []
    for i, item in enumerate(wanted):
        name = item['scene']
        slot = first + i
        row = {'slot': slot, 'scene': name, 'title': item.get('title') or name}
        rows.append(row)
        if name in moments_by:
            s = moments_by[name]
            level, at, yaw, chain, what = s['map'], s.get('ownerAt'), s.get('ownerYaw') or 0, s.get('chain'), s.get('ownerClass') or 'the event\'s originator'
            event = s.get('event')
        else:
            found = [e for e in starts.get(name, []) if not item.get('action') or item['action'] in e['action']]
            found.sort(key=lambda e: (e.get('ownerAt') is None, (e.get('via') or {}).get('originatorAt') is None))
            if not found:
                row['skipped'] = 'no such conversation start or moment in the scan'
                continue
            e = found[0]
            via = e.get('via') or {}
            level, chain = e['map'], via.get('chain')
            event = via.get('eventClass')
            if e.get('ownerAt'):
                at, yaw, what = e['ownerAt'], e.get('ownerYaw') or 0, e.get('ownerClass') or 'the owner'
            else:
                at, yaw, what = via.get('originatorAt'), via.get('originatorYaw') or 0, via.get('originatorClass') or 'the event\'s originator'
        if item.get('at'):
            at, yaw, what = item['at'], item.get('yaw') or 0, 'the list\'s place'
        if not at or not level:
            row['skipped'] = 'no placed owner or originator in the scan (give "at" and "yaw" in the list)' if level else 'no persistent level known'
            continue
        plot, unset = plot_for(chain)
        plot.update({int(k): int(v) for k, v in (item.get('plot') or {}).items()})
        place, face = stand_before(at, yaw, float(item.get('approachM', approach)))
        out = os.path.join(out_dir, f"{career}_{slot:02d}.pcsav")
        row['starts'] = ('when you use it' if event and 'OnPlayerActivate' in event else 'when you walk in (a trigger)' if event and 'Touch' in event
                         else 'by itself when its area loads' if event and 'LevelLoaded' in event else 'by the level script' if event else '?')
        row.update({'file': out, 'level': level, 'place': [round(v, 1) for v in place], 'facing': round(face, 1), 'before': what,
                    'plot': plot, 'unset': unset})
        cmd = [sys.executable, '-I', tool, 'write', base, out, '--level', level, '--location', f"{place[0]:.1f}", f"{place[1]:.1f}", f"{place[2]:.1f}",
               '--yaw', f"{face:.1f}", '--force']
        for k in sorted(plot):
            cmd += ['--bool', f"{k}={plot[k]}"]
        commands.append(cmd)
        if dry:
            continue
        if os.path.exists(out) and out not in json.load(open(manifest_path, encoding='utf-8')).get('files', []) if os.path.exists(manifest_path) else os.path.exists(out):
            row['skipped'] = f"{os.path.basename(out)} exists and is not one of ours: not replaced (another --first-slot)"
            continue
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            row['skipped'] = 'le1-save.py: ' + (done.stderr or done.stdout).strip().splitlines()[-1][:160]
            continue
        files.append(out)
    if not dry:
        os.makedirs(os.path.dirname(manifest_path), exist_ok=True)
        earlier = json.load(open(manifest_path, encoding='utf-8')).get('files', []) if os.path.exists(manifest_path) else []
        with open(manifest_path, 'w', encoding='utf-8') as f:
            json.dump({'base': base, 'files': sorted(set(earlier) | set(files))}, f, indent=1)
    lines = [f"# Validation saves ({career}, from {os.path.basename(base)})", '',
             'Start the game after they are written (the Load screen reads the folder once), load the slot, walk up and start the scene yourself.', '',
             '| Slot | Scene | Level | You stand | It starts | Plot set | Not set (may keep the scene from starting) |', '|---|---|---|---|---|---|---|']
    for r in rows:
        if r.get('skipped'):
            lines.append(f"| {r['slot']} | {r['title']} (`{r['scene']}`) | - | **not written**: {r['skipped']} | | | |")
        else:
            lines.append(f"| {r['slot']} | {r['title']} (`{r['scene']}`) | {r['level']} | "
                         f"in front of {r['before']}, at {r['place'][0]:.0f} {r['place'][1]:.0f} {r['place'][2]:.0f} facing {r['facing']:.0f} deg | {r['starts']} | "
                         f"{', '.join(f'{k}={v}' for k, v in sorted(r['plot'].items())) or '-'} | {'; '.join(r['unset']) or '-'} |")
    text = '\n'.join(lines) + '\n'
    if table:
        with open(table, 'w', encoding='utf-8') as f:
            f.write(text)
    print(text)
    print(f"{len(files)} saves written to {out_dir}" + (' (dry run: nothing written)' if dry else f"; listed in {manifest_path} (saves --remove deletes them)"))
    return commands if '--return-commands' in argv else 0


# ---- the reach matrix -----------------------------------------------------------------------------------------------------------

def reach(argv):
    """The reach matrix from a -ReachOnly run's folder (reach.jsonl, reach-scenes.jsonl): per level, whether `open` got to
    gameplay, how its chunks streamed and how many start actions were found; then every scene that was not reached."""
    folder = argv[0]
    levels, chunks, scenes = collections.OrderedDict(), collections.defaultdict(list), collections.defaultdict(list)
    for line in open(os.path.join(folder, 'reach.jsonl'), encoding='utf-8-sig'):
        r = json.loads(line)
        if r['kind'] == 'level':
            levels[r['map']] = r
        else:
            chunks[r['map']].append(r)
    path = os.path.join(folder, 'reach-scenes.jsonl' if os.path.exists(os.path.join(folder, 'reach-scenes.jsonl')) else 'results.jsonl')
    for line in open(path, encoding='utf-8-sig'):
        r = json.loads(line)
        scenes[r['map']].append(r)
    print('| Level | open | s | Chunks ready | by touch | by state | Scenes reached | Started by itself |\n|---|---|---|---|---|---|---|---|')
    total = reached = 0
    for mp, lv in levels.items():
        cs_, sc = chunks[mp], scenes[mp]
        ok = [s for s in sc if s.get('outcome') not in ('not reached', 'action not loaded')]
        total += len(sc)
        reached += len(ok)
        print(f"| {mp} | {'yes' if lv['opened'] else 'NO'} | {lv['seconds']} | {sum(1 for c in cs_ if c['ready'])} of {len(cs_)} | "
              f"{sum(1 for c in cs_ if ' by ' in c['route'] or c['route'].startswith('touch'))} | {sum(1 for c in cs_ if 'state ' in c['route'])} | "
              f"{len(ok)} of {len(sc)} | {', '.join(lv.get('byItself') or [])} |")
    print(f"\n{reached} of {total} scenes reached in {len(levels)} levels")
    missed = [s for sc in scenes.values() for s in sc if s.get('outcome') in ('not reached', 'action not loaded')]
    if missed:
        print('\n| Not reached | Level | Chunk | Stream route | Why |\n|---|---|---|---|---|')
        for s in missed:
            print(f"| {s['conversation']} | {s['map']} | {s.get('chunk', '-')} | {s.get('stream', '-')} | {s.get('detail', s.get('outcome'))} |")
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
    """True if a play can end at `entry`: it has no reply, a reply that leads nowhere (the dialog's end), or only replies
    and entries behind plot conditionals (which this career's plot may all refuse: Ilos's security system ended so)."""
    nodes, replies = conv['nodes'], conv.get('replyNodes') or []
    if not 0 <= entry < len(nodes):
        return True
    mine = [replies[r] for r in nodes[entry]['replies'] if 0 <= r < len(replies)]
    if not mine or any(not r['entries'] for r in mine):
        return True
    def open_way(r):
        return r.get('conditional', -1) == -1 and any(0 <= e < len(nodes) and nodes[e].get('conditional', -1) == -1 for e in r['entries'])
    return not any(open_way(r) for r in mine)


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
    if not s and not flow and r.get('modes') is not None and outcome == 'played':
        return kinds  # a moment (a matinee that walks Shepard): it ran; what it showed is in its modes and the log
    if not s and not flow:
        if outcome not in ('crash', 'hang'):
            add('no-start', r.get('detail', outcome or 'no summary'))
        return kinds
    # A run that only looks at the opening (-Pace opening) ends every scene itself once the opening has settled: no fault,
    # and what is counted over a whole scene (lines, the wheel, a chain's end) is not judged.
    opening_only = r.get('ended') == 'opening' or r.get('pace') == 'opening'
    if not opening_only and (r.get('ended') == 'forced' or 'ended by' in outcome or 'time' in outcome):
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
            # A small placement the game repeats is undone once and accepted at its second asking (the summary's `yielded`,
            # cw::Insists): that one undone step, and the frame it may have reached, are the rule at work, not a fault.
            yielded = s.get('yielded', 0)
            if s.get('kept', 0) > yielded:
                add('undone', f"kept {s['kept']}" + (f" ({yielded} of them before a small move was accepted)" if yielded else ''))
            if s.get('drawn', 0) > yielded:
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
        if not opening_only and 'picks' in driver and s.get('wheel', 0) > driver['picks']:
            add('wheel-unanswered', f"wheel {s['wheel']}, picks {driver['picks']}")
        path = s.get('entries') or driver.get('entries')  # the mod's own list of the entries it passed through
        if conv and path:
            text = sum(1 for e in set(path) if 0 <= e < len(conv['nodes']) and conv['nodes'][e].get('text') and not conv['nodes'][e].get('nonText'))
            if s.get('lines', 0) < text:
                add('lines-short', f"lines {s.get('lines', 0)}, {text} entries with text seen")
    return kinds


BASELINE_FIELDS = ('world', 'kept', 'drawn', 'sourceKept', 'wheel', 'noPlace', 'autoScreened', 'lines', 'opening')


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
    # How the openings ended (worldOpening = auto): a cut only where the composition changed.
    openings = collections.Counter((r.get('summary') or {}).get('opening') for r in results.values() if (r.get('summary') or {}).get('world'))
    openings.pop(None, None)
    if openings:
        cuts = sum((r.get('summary') or {}).get('cuts', 0) for r in results.values())
        lines.append('openings in the world: ' + ', '.join(f'{k} {n}' for k, n in openings.most_common()) + f'; dips from black in all: {cuts}')
        far = sorted(((r['summary'].get('markM', 0), k, r['summary'].get('opening')) for k, r in results.items()
                      if (r.get('summary') or {}).get('world') and (r['summary'].get('markM') or 0) >= 1.5), reverse=True)
        if far:
            lines.append("Shepard's mark 1.5 m or more from where the player stood: " + ', '.join(f'{k} {m:.1f} m ({o})' for m, k, o in far[:25]))
    # The moments (level-script matinees that walk Shepard): how each was held and what it led to.
    mom = [(k, r) for k, r in results.items() if r.get('modes') is not None]
    if mom:
        lines.append('\n## Moments\n\n| Moment | Level | As authored: s, m, modes | In first person: s, walked, carried, dips | Script moves | Led to (opening) |\n|---|---|---|---|---|---|')
        for k, r in mom:
            t, v = r.get('track') or {}, r.get('view') or {}
            led = ', '.join(f"{x['name']} ({x.get('opening') or ('world' if x.get('world') else 'screen')}{', %.1f m' % x['markM'] if x.get('markM') else ''})"
                            for x in r.get('led') or []) or '-'
            lines.append(f"| {k.replace('moment_', '')} | {r.get('map', '-')} | {t.get('seconds', '-')} s, {t.get('pathM', '-')} m, {t.get('modes', r.get('modes'))} | "
                         + (f"{v.get('seconds')} s, {v.get('walkedM')} m, {v.get('carriedM')} m, {v.get('dips')}" if v else 'not held')
                         + f" | {'; '.join(r.get('script') or []) or '-'} | {led} |")
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
    if len(argv) < 3 or argv[1] not in ('predict', 'plan', 'judge', 'canary', 'reach', 'moments', 'saves'):
        print(__doc__)
        return 2
    return {'predict': predict, 'plan': plan, 'judge': judge, 'canary': canary, 'reach': reach, 'moments': moments, 'saves': saves}[argv[1]](argv[2:])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
