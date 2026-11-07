# SPDX-License-Identifier: MIT
"""Every LE1 conversation, sorted by what first person makes of it (research/59 "The scan").

Reads the JSON lines tools/conversation-scan (C#, over Legendary Explorer's library) writes from the game's packages:

  tools/conversation-scan/bin/Release/net10.0-windows/conversation-scan.exe <CookedPCConsole> scan.jsonl
  python tools/conversation-scan.py scan.jsonl [--list framed|walks|stage|remote|free|ambient] [--csv out.csv]

Per conversation it joins the conversation (lines, speakers), its starts in the levels' scripts (a stage: the marks the game
puts the actors on) and its lines' matinees (each line's BioSeqEvt_ConvNode -> SeqAct_Interp), and sorts it into one kind:

  ambient   every line ambient: barks played around the player, no conversation mode
  framed    authored camera work on a quarter or more of its lines (a director track, or a moving camera): a set piece
            composed for a camera elsewhere, which first person can't show as framed
  remote    a call or transmission: started on the game's transmission stage (one person, a hologram or a voice), or named
            for one (a distress call); the runtime survey saw these as one shot on Shepard for the whole conversation
  walks     someone walks along an authored path during a line (a move track on an actor)
  stage     played on a stage: the game puts the actors on its marks (the jumps research/59 keeps in place)
  free      no stage, no authored camera or walk: the actors stay where they stand, the stage cameras cut between them

The output describes the game's own data: keep it out of the repository (the table of counts is ours).
"""
import collections
import csv
import json
import re
import sys

CAMERA_GROUP = re.compile(r'cam|flyer|shot', re.IGNORECASE)


def key(name, suffix):
    # 'ice50_mira_D.ice50_mira_dlg' / 'ice50_mira_N.Node_Data_Sequence...' -> 'ice50_mira'
    head = name.split('.')[0]
    return re.sub('_' + suffix + '$', '', head, flags=re.IGNORECASE).lower()


def load(path):
    convs, starts, nodes = {}, collections.defaultdict(list), collections.defaultdict(list)
    for line in open(path, encoding='utf-8'):
        r = json.loads(line)
        if r['kind'] == 'conversation':
            convs.setdefault(key(r['name'], 'd'), r)
        elif r['kind'] == 'start' and r.get('conv'):
            starts[key(r['conv'], 'd')].append(r)
        elif r['kind'] == 'cinematic':
            nodes[key(r['path'], 'n')].append(r)
    return convs, starts, nodes


def measure(name, conv, starts, nodes):
    seen = set()
    m = collections.Counter()
    for n in nodes:
        node_id = n['props'].get('m_nNodeID')
        if node_id in seen:
            continue
        seen.add(node_id)
        groups = [g for mt in n['matinees'] for g in mt['groups']]
        m['nodes'] += 1
        director = any(g['class'] == 'InterpGroupDirector' for g in groups)
        camera_move = any('InterpTrackMove' in g['tracks'] and CAMERA_GROUP.search(g['name'] or '') for g in groups)
        actor_move = any('InterpTrackMove' in g['tracks'] and not CAMERA_GROUP.search(g['name'] or '') and g['class'] != 'InterpGroupDirector'
                         for g in groups)
        m['director'] += director
        m['cameraMove'] += camera_move
        m['authoredCamera'] += director or camera_move
        m['actorMove'] += actor_move
        m['switchCamera'] += any('BioEvtSysTrackSwitchCamera' in g['tracks'] for g in groups)
        m['fade'] += any('InterpTrackFade' in g['tracks'] for g in groups)
    m['lines'] = conv['entries'] + conv['replies'] if conv else 0
    m['ambient'] = conv is not None and conv['entries'] > 0 and conv['ambient'] == conv['entries'] and conv['replies'] == 0
    m['stage'] = any(s.get('stage') for s in starts)
    m['remote'] = any('transmission' in (s.get('stageType') or '').lower() for s in starts) or bool(re.search(r'transmission|distress', name))
    m['stageTypes'] = sorted({(s.get('stageType') or '-').split('.')[-1] for s in starts})
    m['starts'] = len(starts)
    return m


def kind(m):
    if m['ambient']:
        return 'ambient'
    if m['nodes'] and m['authoredCamera'] * 4 >= m['nodes']:
        return 'framed'
    if m['remote']:
        return 'remote'
    if m['actorMove']:
        return 'walks'
    if m['stage']:
        return 'stage'
    return 'free'


def area(name):
    head = re.match(r'[a-z]+', name)
    return head.group(0).upper() if head else '?'


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    convs, starts, nodes = load(argv[1])
    rows = []
    for k in sorted(set(convs) | set(nodes)):
        if k not in convs:
            continue
        m = measure(k, convs.get(k), starts.get(k, []), nodes.get(k, []))
        rows.append((k, area(k), kind(m), m, convs[k]))
    by_kind = collections.Counter(r[2] for r in rows)
    print(f"{len(rows)} conversations: " + ', '.join(f'{k} {by_kind[k]}' for k in ('free', 'stage', 'remote', 'walks', 'framed', 'ambient')))
    areas = sorted({r[1] for r in rows})
    print('\n| Area | free | stage | remote | walks | framed | ambient |\n|---|---|---|---|---|---|---|')
    for a in areas:
        c = collections.Counter(r[2] for r in rows if r[1] == a)
        print(f"| {a} | {c['free']} | {c['stage']} | {c['remote']} | {c['walks']} | {c['framed']} | {c['ambient']} |")
    if '--list' in argv:
        want = argv[argv.index('--list') + 1]
        print(f'\n| Conversation | Lines | Nodes | Authored camera | Actor moves | Fades | Stage |\n|---|---|---|---|---|---|---|')
        for k, a, kd, m, c in rows:
            if kd == want:
                print(f"| {k} | {m['lines']} | {m['nodes']} | {m['authoredCamera']} | {m['actorMove']} | {m['fade']} | {'yes' if m['stage'] else 'no'} |")
    if '--csv' in argv:
        with open(argv[argv.index('--csv') + 1], 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['conversation', 'area', 'kind', 'lines', 'nodes', 'authoredCamera', 'director', 'cameraMove', 'actorMove',
                        'switchCamera', 'fade', 'stage', 'stageTypes', 'starts', 'speakers'])
            for k, a, kd, m, c in rows:
                w.writerow([k, a, kd, m['lines'], m['nodes'], m['authoredCamera'], m['director'], m['cameraMove'], m['actorMove'],
                            m['switchCamera'], m['fade'], m['stage'], ' '.join(m['stageTypes']), m['starts'], ' '.join(c['speakers'])])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
