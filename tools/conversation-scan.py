# SPDX-License-Identifier: MIT
"""Every LE1 conversation, sorted by what first person makes of it (research/59 "The scan").

Reads the JSON lines tools/conversation-scan (C#, over Legendary Explorer's library) writes from the game's packages:

  tools/conversation-scan/bin/Release/net10.0-windows/conversation-scan.exe <CookedPCConsole> scan.jsonl
  python tools/conversation-scan.py scan.jsonl [--list framed|walks|stage|remote|free|ambient] [--csv out.csv]
  python tools/conversation-scan.py scan.jsonl --catalogue %LOCALAPPDATA%/MEVR/conversation-catalogue.json

The catalogue is what tools/conversation-launch.ps1 reads: per conversation start, the persistent level to open, the level chunk
to stream in (the start's package, BIOA_ICE50_06_DSG -> ICE50_06), the owner (its class, Tag, place and facing when it is placed
in the level) and the stage's place. Game data: keep it out of the repository.

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

Each conversation also gets a verdict for the First person mode (--verdicts lists them; --set-pieces prints the C++ list for
core/vr/ConversationWorld's IsSetPiece):
  screen    framed, or the game walks Shepard himself during a line (a move track on his group: the player's place would move
            under the headset): shown on the 3D screen
  check     a wide stage (its type's size 400 units or more: ST_2P_600_combat, a stand-off 6-10 m across) or an NPC's authored
            walk: in the world for now, each to be looked at with tools/conversation-launch.ps1
  world     everything else (remote, stage, free); ambient lines have no verdict

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
        m['shepardMove'] += any('InterpTrackMove' in g['tracks'] and re.search(r'shep|player', g['name'] or '', re.IGNORECASE) for g in groups)
        m['switchCamera'] += any('BioEvtSysTrackSwitchCamera' in g['tracks'] for g in groups)
        m['fade'] += any('InterpTrackFade' in g['tracks'] for g in groups)
    m['lines'] = conv['entries'] + conv['replies'] if conv else 0
    m['ambient'] = conv is not None and conv['entries'] > 0 and conv['ambient'] == conv['entries'] and conv['replies'] == 0
    m['stage'] = any(s.get('stage') for s in starts)
    sizes = [int(x.group(1)) for st in starts if st.get('stageType') for x in [re.search(r'_(\d{3,4})(_|$)', st['stageType'].split('.')[-1])] if x]
    m['stageSpread'] = max(sizes) if sizes else 0
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


def verdict(k, m):
    if k == 'ambient':
        return '-'
    if k == 'framed' or m['shepardMove']:
        return 'screen'
    if m['stageSpread'] >= 400 or k == 'walks':
        return 'check'
    return 'world'


def area(name):
    head = re.match(r'[a-z]+', name)
    return head.group(0).upper() if head else '?'


PERSISTENT = {'CRD': 'BIOA_CRD00', 'END': 'BIOA_END00', 'ICE': 'BIOA_ICE00', 'JUG': 'BIOA_JUG00', 'LAV': 'BIOA_LAV00', 'LOS': 'BIOA_LOS00',
              'NOR': 'BIOA_NOR00', 'PRO': 'BIOA_PRO00', 'STA': 'BIOA_STA00', 'WAR': 'BIOA_WAR00'}


def persistent(package):
    # BIOA_ICE50_06_DSG -> BIOA_ICE00; BIOA_UNC52_01_DSG -> BIOA_UNC52; BIOA_FRE32_00_DSG -> BIOA_FRE32; a persistent package itself.
    m = re.match(r'BIOA_([A-Z]{3})(\d\d)', package, re.IGNORECASE)
    if not m:
        return None
    area, number = m.group(1).upper(), m.group(2)
    if area in ('UNC', 'FRE'):
        return f'BIOA_{area}{number}'
    return PERSISTENT.get(area)


def catalogue(path, out):
    entries = []
    for line in open(path, encoding='utf-8'):
        r = json.loads(line)
        if r['kind'] != 'start' or not r.get('conv'):
            continue
        chunk = re.sub(r'^BIOA_', '', re.sub(r'_(DSG|LAY).*$', '', r['package']), flags=re.IGNORECASE)
        entries.append({
            'conversation': key(r['conv'], 'd'), 'conv': r['conv'], 'comment': r.get('comment'), 'action': r['path'],
            'actionTail': re.sub(r'^.*Main_Sequence\.', '', r['path']), 'package': r['package'], 'chunk': chunk,
            'map': persistent(r['package']), 'owner': r.get('owner'), 'ownerClass': r.get('ownerClass'), 'ownerTag': r.get('ownerTag'),
            'ownerAt': r.get('ownerAt'), 'ownerYaw': r.get('ownerYaw'), 'stage': r.get('stage'), 'stageAt': r.get('stageAt'),
        })
    entries.sort(key=lambda e: (e['conversation'], e['ownerAt'] is None, e['action']))
    with open(out, 'w', encoding='utf-8') as f:
        json.dump(entries, f, indent=0)
    placed = sum(1 for e in entries if e['ownerAt'])
    print(f"{len(entries)} starts of {len({e['conversation'] for e in entries})} conversations -> {out}; {placed} with a placed owner, "
          f"{sum(1 for e in entries if e['map'] is None)} without a known persistent level")


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if '--catalogue' in argv:
        catalogue(argv[1], argv[argv.index('--catalogue') + 1])
        return 0
    convs, starts, nodes = load(argv[1])
    rows = []
    for k in sorted(set(convs) | set(nodes)):
        if k not in convs:
            continue
        m = measure(k, convs.get(k), starts.get(k, []), nodes.get(k, []))
        rows.append((k, area(k), kind(m), m, convs[k]))
    by_kind = collections.Counter(r[2] for r in rows)
    print(f"{len(rows)} conversations: " + ', '.join(f'{k} {by_kind[k]}' for k in ('free', 'stage', 'remote', 'walks', 'framed', 'ambient')))
    by_verdict = collections.Counter(verdict(r[2], r[3]) for r in rows)
    print('verdicts: ' + ', '.join(f'{v} {by_verdict[v]}' for v in ('screen', 'check', 'world')))
    if '--set-pieces' in argv:
        names = sorted(k for k, a, kd, m, c in rows if verdict(kd, m) == 'screen')
        print('\n// tools/conversation-scan.py --set-pieces: framed, or Shepard walked by the game.')
        for i in range(0, len(names), 5):
            print('        ' + ', '.join(f'"{n}"' for n in names[i:i + 5]) + ',')
    if '--verdicts' in argv:
        print('\n| Conversation | Kind | Verdict | Why |\n|---|---|---|---|')
        for k, a, kd, m, c in rows:
            v = verdict(kd, m)
            if v in ('screen', 'check'):
                why = []
                if kd == 'framed': why.append(f"authored camera on {m['authoredCamera']} of {m['nodes']} lines")
                if m['shepardMove']: why.append(f"Shepard walked on {m['shepardMove']} lines")
                if m['stageSpread'] >= 400: why.append(f"stage {m['stageSpread'] / 100:.0f} m wide")
                if kd == 'walks': why.append(f"an NPC walks on {m['actorMove']} lines")
                print(f"| {k} | {kd} | {v} | {'; '.join(why)} |")
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
                        'switchCamera', 'fade', 'stage', 'stageTypes', 'starts', 'speakers', 'shepardMove', 'stageSpread', 'verdict'])
            for k, a, kd, m, c in rows:
                w.writerow([k, a, kd, m['lines'], m['nodes'], m['authoredCamera'], m['director'], m['cameraMove'], m['actorMove'],
                            m['switchCamera'], m['fade'], m['stage'], ' '.join(m['stageTypes']), m['starts'],
                            ' '.join(c['speakers']), m['shepardMove'], m['stageSpread'], verdict(kd, m)])
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
